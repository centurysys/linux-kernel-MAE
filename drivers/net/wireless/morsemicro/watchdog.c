/*
 * Copyright 2017-2023 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include "bus.h"
#include "coredump.h"
#include "debug.h"
#include "firmware.h"
#include "hw.h"
#include "led.h"
#include "mac.h"
#include "morse.h"
#include "ps.h"
#include "watchdog.h"
#include "wiphy.h"

/* Enable/Disable watchdog support */
static bool enable_watchdog __read_mostly = ENABLE_WATCHDOG_DEFAULT;
module_param(enable_watchdog, bool, 0644);
MODULE_PARM_DESC(enable_watchdog, "Enable watchdog");

/* Set watchdog interval. User can update the watchdog interval in run time */
static uint watchdog_interval_secs __read_mostly = 30;
module_param(watchdog_interval_secs, uint, 0644);
MODULE_PARM_DESC(watchdog_interval_secs, "Watchdog interval in seconds");

#define MORSE_HEALTH_CHECK_RETRIES 1

static enum hrtimer_restart morse_watchdog_fire(struct hrtimer *timer)
{
	ktime_t interval;
	struct morse *mors = container_of(timer, struct morse, watchdog.timer);

	if (mors->watchdog.ping) {
		if (!mors->watchdog.paused)
			mors->watchdog.ping(mors);
	}

	/* Get the updated watchdog interval secs */
	mors->watchdog.interval_secs = watchdog_interval_secs;

	interval = ktime_set(mors->watchdog.interval_secs, 0);

#if defined(MAC80211_BACKPORT_VERSION_CODE) && (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	hrtimer_forward_now(&mors->watchdog.timer, interval.tv64);
#else
	hrtimer_forward_now(&mors->watchdog.timer, interval);
#endif

	return HRTIMER_RESTART;
}

static void watchdog_timer_start(struct morse *mors)
{
	ktime_t interval = ktime_set(mors->watchdog.interval_secs, 0);

#if defined(MAC80211_BACKPORT_VERSION_CODE) && (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	hrtimer_start(&mors->watchdog.timer, interval.tv64, HRTIMER_MODE_REL);
#else
	hrtimer_start(&mors->watchdog.timer, interval, HRTIMER_MODE_REL);
#endif
}

static void morse_watchdog_reset_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse, recovery.bus_reset);

	MORSE_INFO(mors, "Resetting Bus...\n");
	morse_bus_reset(mors);
}

static void morse_watchdog_quiesce_before_restart(struct morse *mors)
{
	if (is_fullmac_mode()) {
		morse_wiphy_stop_tx_queues(mors);
		return;
	}

	ieee80211_stop_queues(mors->hw);
}

static void morse_watchdog_restore_after_restart(struct morse *mors)
{
	if (is_fullmac_mode()) {
		morse_wiphy_restarted(mors);
	} else if (morse_mlme_is_started(mors)) {
		mors->mlme.in_reconfig = true;
		ieee80211_restart_hw(mors->hw);
	}
}

static void morse_watchdog_intf_cleanup_during_restart(struct morse *mors)
{
	if (is_fullmac_mode())
		morse_wiphy_cleanup(mors);
	else
		morse_mac_cleanup_during_restart(mors);
}

static int morse_watchdog_restart(struct morse *mors)
{
	int ret;
	u32 chip_id;
	const bool reset_hw = true;
	const bool reattach_hw = false;

	dev_warn(mors->dev, "%s: Restarting HW", __func__);
	lockdep_assert_held(&mors->lock);
	morse_hw_set_state(mors, MORSE_HW_STATE_RESTARTING);

	/* Should not be attempting restart while system is in suspend */
	MORSE_WARN_ON(FEATURE_ID_DEFAULT,
		      test_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags));

	/* Disable powersave before starting restart work */
	morse_ps_wakers_inc(mors);
	morse_ps_force_eval(mors);
	/* Ignore all interrupts coming from the chip until it is cleanly restarted.
	 * If it is still alive at this point it may continue passing rx frames.
	 * The morse_coredump() call below may also cause a hw stop notification IRQ.
	 */
	morse_bus_set_irq(mors, false);
	morse_hw_irq_clear(mors);
	/* Stop Tx */
	morse_watchdog_quiesce_before_restart(mors);

	set_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED, &mors->state_flags);
	set_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags);

#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
	if (morse_mac_is_airtime_fairness_enabled())
		tasklet_kill(&mors->tasklet_txq);
#endif

	/* Allow time for in-transit tx/rx packets to settle */
	mdelay(20);
	cancel_work_sync(&mors->chip_if_work);
	cancel_work_sync(&mors->tx_stale_work);
	WRITE_ONCE(mors->chip_if->event_flags, 0);
	mors->cfg->ops->flush_tx_data(mors);
	mors->cfg->ops->flush_cmds(mors);

	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	morse_release_bus(mors);

	if (ret < 0) {
		MORSE_ERR(mors, "%s: Failed to access HW (errno:%d)", __func__, ret);
		if (test_and_clear_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags))
			MORSE_WARN(mors, "%s: Skipping coredump", __func__);
		goto exit;
	}

	if (test_and_clear_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags)) {
		dev_warn(mors->dev, "%s: Generating core-dump (reason:%s)", __func__,
			morse_coredump_reason_to_str(mors->coredump.crash.reason));

		ret = morse_coredump(mors);
		if (ret)
			MORSE_ERR(mors, "%s: Core-dump failed (errno:%d)", __func__, ret);

		if (mors->user_coredump_comp)
			complete(mors->user_coredump_comp);

		MORSE_INFO(mors, "%s: Core-dump created", __func__);
	}

	morse_watchdog_intf_cleanup_during_restart(mors);

		ret = morse_firmware_prepare(mors, reset_hw, reattach_hw);
		if (ret < 0) {
			MORSE_ERR(mors, "%s: Failed to re-load firmware (errno:%d)", __func__, ret);
			goto exit;
		}

	ret = mors->cfg->ops->hw_restarted(mors);
	if (ret) {
		MORSE_ERR(mors, "%s: hw_restarted failed: %d\n", __func__, ret);
		goto exit;
	}

	ret = morse_firmware_parse_extended_host_table(mors);
	if (ret) {
		MORSE_ERR(mors, "%s: Failed to parse extended host table: %d\n",
			__func__, ret);
		goto exit;
	}

	/* Clear all state flags after reset */
	WRITE_ONCE(mors->state_flags, 0);
	morse_hw_set_state(mors, MORSE_HW_STATE_ON);
	morse_bus_set_irq(mors, true);
	morse_led_restore_after_restart(mors);
	morse_watchdog_restore_after_restart(mors);
exit:
	if (ret) {
		/* We failed to restart - HW is still considered 'stopped' */
		morse_hw_set_state(mors, MORSE_HW_STATE_STOPPED);
	}

	morse_ps_wakers_dec(mors);
	return ret;
}

/* Schedule the restart work from wherever a code restart
 * is deemed necessary. This can be triggered directly from the debugfs
 * or will be scheduled indirectly from a watchdog timeout.
 */
static void morse_watchdog_restart_work(struct work_struct *work)
{
	int ret;
	struct morse *mors = container_of(work, struct morse, recovery.driver_restart);

	mutex_lock(&mors->lock);
	morse_watchdog_pause(mors);
	ret = morse_watchdog_restart(mors);

	if (!ret) {
		mors->restart_counter++;
		morse_watchdog_resume(mors);
		MORSE_INFO(mors, "%s: HW restart success (count:%d)",
				   __func__, mors->restart_counter);
	} else {
		MORSE_ERR(mors, "%s: HW restart failed (errno:%d, count:%d)",
				  __func__,
				  ret,
				  mors->restart_counter);

		/* Driver will request to reset the bus.
		 * This should remove/re-install the driver
		 */
		morse_watchdog_cleanup(mors);
		schedule_work(&mors->recovery.bus_reset);
	}

	mutex_unlock(&mors->lock);
}

static void morse_watchdog_health_check_work(struct work_struct *work)
{
	int ret;
	int retries = 0;
	bool restart = false;
	struct morse *mors = container_of(work, struct morse, recovery.health_check);

	mutex_lock(&mors->lock);

	if (morse_hw_is_stopped(mors)) {
		/* Driver has already registered that the HW has stopped.
		 * Jump straight to restart.
		 */
		restart = true;
		goto exit;
	}

	if (!morse_hw_is_on(mors))
		goto exit; /* HW either off or restarting (do not restart) */

	if (test_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_CMD_BLOCKED, &mors->state_flags))
		goto exit;

	if (!morse_hw_headless_is_off(mors))
		goto exit;

	if (test_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags))
		goto exit;

	do {
		ret = morse_cmd_health_check(mors);
	} while (ret && retries++ < MORSE_HEALTH_CHECK_RETRIES);

	if (ret) {
		MORSE_ERR(mors, "%s: failed (ret:%d)\n", __func__, ret);
		morse_hw_set_state(mors, MORSE_HW_STATE_STOPPED);
		restart = true;
	} else {
		MORSE_DBG(mors, "%s: success\n", __func__);
	}

exit:
	if (restart) {
		if (!morse_coredump_new(mors, MORSE_COREDUMP_REASON_HEALTH_CHECK_FAILED))
			set_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags);

		MORSE_ERR(mors, "Health check indicates restart required");
		morse_watchdog_driver_restart(mors);
	}

	mutex_unlock(&mors->lock);
}

static int morse_watchdog_ping_health_check(struct morse *mors)
{
	schedule_work(&mors->recovery.health_check);
	MORSE_DBG(mors, "Scheduled a health check\n");

	return 0;
}

int morse_watchdog_init(struct morse *mors)
{
	int ret = 0;

		INIT_WORK(&mors->recovery.health_check, morse_watchdog_health_check_work);
	INIT_WORK(&mors->recovery.bus_reset, morse_watchdog_reset_work);
	INIT_WORK(&mors->recovery.driver_restart, morse_watchdog_restart_work);
	mors->recovery.is_ready = true;

	HTIMER_INIT(&mors->watchdog.timer, &morse_watchdog_fire, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mors->watchdog.interval_secs = watchdog_interval_secs;
	mors->watchdog.ping = morse_watchdog_ping_health_check;
	mors->watchdog.consumers = 0;
	mors->watchdog.paused = 0;

	mutex_init(&mors->watchdog.lock);

	if (enable_watchdog)
		ret = morse_watchdog_start(mors);

	return ret;
}

int morse_watchdog_start(struct morse *mors)
{
	int ret = 0;

	mutex_lock(&mors->watchdog.lock);

	if (!hrtimer_active(&mors->watchdog.timer) && !mors->watchdog.paused) {
		MORSE_INFO(mors, "Starting ...\n");
		watchdog_timer_start(mors);
	}

	mors->watchdog.consumers++;

	mutex_unlock(&mors->watchdog.lock);

	MORSE_INFO(mors, "Started (interval=%ds, consumers=%d) ...\n",
		   mors->watchdog.interval_secs, mors->watchdog.consumers);

	return ret;
}

int morse_watchdog_refresh(struct morse *mors)
{
	int ret = -1;

	mutex_lock(&mors->watchdog.lock);

	if (mors->watchdog.consumers < 1)
		goto exit;

	if (hrtimer_active(&mors->watchdog.timer)) {
		hrtimer_cancel(&mors->watchdog.timer);
		watchdog_timer_start(mors);
		ret = 0;
	}

exit:
	mutex_unlock(&mors->watchdog.lock);
	return ret;
}

int morse_watchdog_stop(struct morse *mors)
{
	int ret = -1;

	mutex_lock(&mors->watchdog.lock);

	if (mors->watchdog.consumers < 1)
		goto exit;

	if (hrtimer_active(&mors->watchdog.timer)) {
		if (--mors->watchdog.consumers > 0) {
			MORSE_INFO(mors, "Ignored because %d consumers are using watchdog\n",
				   mors->watchdog.consumers);
			ret = 0;
		}

		hrtimer_cancel(&mors->watchdog.timer);
	} else {
		MORSE_INFO(mors, "Watchdog has been stopped\n");
	}

exit:
	mutex_unlock(&mors->watchdog.lock);
	return ret;
}

int morse_watchdog_pause(struct morse *mors)
{
	int ret = -1;

	mutex_lock(&mors->watchdog.lock);

	if (mors->watchdog.consumers < 1)
		goto exit;

	if (mors->watchdog.paused) {
		mors->watchdog.paused++;
		goto exit;
	}

	if (hrtimer_active(&mors->watchdog.timer)) {
		hrtimer_cancel(&mors->watchdog.timer);
		ret = 0;
	}
	mors->watchdog.paused++;
	MORSE_INFO(mors, "Watchdog has been paused\n");

exit:
	mutex_unlock(&mors->watchdog.lock);
	return ret;
}

int morse_watchdog_resume(struct morse *mors)
{
	int ret = -1;

	mutex_lock(&mors->watchdog.lock);

	if (mors->watchdog.consumers < 1)
		goto exit;

	if (mors->watchdog.paused > 0)
		mors->watchdog.paused--;
	else
		goto exit;	/* Nothing to do */

	if (mors->watchdog.paused)
		goto exit;	/* Still some callers that want to keep it paused */

	if (!hrtimer_active(&mors->watchdog.timer)) {
		watchdog_timer_start(mors);
		ret = 0;
	}

	MORSE_INFO(mors, "Watchdog has been resumed\n");

exit:
	mutex_unlock(&mors->watchdog.lock);
	return ret;
}

uint morse_watchdog_get_interval(struct morse *mors)
{
	return mors->watchdog.interval_secs;
}

int morse_watchdog_cleanup(struct morse *mors)
{
	if (mors->watchdog.consumers > 0) {
		mors->watchdog.consumers = 1;
		morse_watchdog_stop(mors);
	}

	return 0;
}

int morse_watchdog_driver_restart(struct morse *mors)
{
	schedule_work(&mors->recovery.driver_restart);
	MORSE_INFO(mors, "Scheduled a driver reset ...\n");

	return 0;
}

void morse_watchdog_health_check_finish(struct morse *mors)
{
	if (enable_watchdog)
		morse_watchdog_cleanup(mors);

	cancel_work_sync(&mors->recovery.health_check);
}
