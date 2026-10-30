/*
 * Copyright 2025 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <net/cfg80211.h>
#include <linux/device.h>
#include <linux/pm_wakeirq.h>
#include <linux/gpio.h>

#include "bus.h"
#include "command.h"
#include "debug.h"
#include "mac.h"
#include "morse_commands.h"
#include "ps.h"
#include "wowlan.h"
#include "trace.h"

static const struct wiphy_wowlan_support morse_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
};

#define MORSE_WOWLAN_DBG(_m, _f, _a...) morse_dbg(FEATURE_ID_WOWLAN, _m, _f, ##_a)
#define MORSE_WOWLAN_INFO(_m, _f, _a...) morse_info(FEATURE_ID_WOWLAN, _m, _f, ##_a)
#define MORSE_WOWLAN_WARN(_m, _f, _a...) morse_warn(FEATURE_ID_WOWLAN, _m, _f, ##_a)
#define MORSE_WOWLAN_ERR(_m, _f, _a...) morse_err(FEATURE_ID_WOWLAN, _m, _f, ##_a)

void morse_init_wowlan(struct morse *mors)
{
	struct ieee80211_hw *hw = mors->hw;

	hw->wiphy->wowlan = &morse_wowlan_support;
	MORSE_WOWLAN_DBG(mors, "%s: WoWLAN initialised\n", __func__);
}

static void quiesce_morse_hw(struct morse *mors)
{
	if (mors->cfg->ops->suspend)
		mors->cfg->ops->suspend(mors);

	/* Temporarily disable global hw irq to flush work */
	morse_bus_set_irq(mors, false);
	morse_hw_irq_clear(mors);

	/* Cancel pending chip work and flush all in-flight data/cmds */
	cancel_work_sync(&mors->chip_if_work);
	mors->cfg->ops->flush_tx_data(mors);
	mors->cfg->ops->flush_cmds(mors);

	/* Enable global interrupts again */
	morse_bus_set_irq(mors, true);
}

static void resume_morse_hw(struct morse *mors)
{
	if (!mors->cfg->ops->resume)
		return;

	/* Keep hardware awake while we interact with it directly */
	morse_ps_wakers_inc(mors);
	morse_ps_force_eval(mors);

	mors->cfg->ops->resume(mors);

	/* Release hardware veto but do not force eval. That will happen later in resume path. */
	morse_ps_wakers_dec(mors);
}

int morse_wowlan_op_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	struct morse *mors = hw->priv;
	const int headless_cfg = MORSE_CMD_HEADLESS_CFG_OPTION_KEEP_IFACES |
			MORSE_CMD_HEADLESS_CFG_OPTION_BUFFER_RX |
			MORSE_CMD_HEADLESS_CFG_OPTION_NOTIFY_ON_ANY_RX;
	int ret;
	struct ieee80211_vif *ap_vif;
	struct morse_vif *mors_vif;
	int num_valid_vifs;
	bool beacon_offload_enabled = false;
	bool probe_offload_enabled = false;
	bool hw_quiesced = false;
	bool hw_detached = false;
	bool wakers_set = false;

	trace_wowlan_suspend(wowlan ? wowlan->any : 0);
	mutex_lock(&mors->lock);

	if (!wowlan) {
		MORSE_WOWLAN_INFO(mors, "%s: No WoWLAN triggers\n", __func__);
		ret = -ENOTSUPP;
		goto exit;
	}

	num_valid_vifs = morse_count_vifs(mors);
	if (num_valid_vifs != 1) {
		MORSE_WOWLAN_INFO(mors,
			"%s: WoWLAN not supported with more than 1 interface, have %d\n",
			__func__,
			num_valid_vifs
		);
		ret = EINVAL;
		goto exit;
	}

	ap_vif = morse_get_ap_vif(mors);
	if (!ap_vif) {
		MORSE_WOWLAN_INFO(mors,
			"%s: WoWLAN not supported for non-AP interfaces\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	mors_vif = ieee80211_vif_to_morse_vif(ap_vif);

	MORSE_WOWLAN_INFO(mors,
			  "%s: %s%s%s%s%s\n",
			  __func__,
			  (wowlan->any) ? "any " : "",
			  (wowlan->magic_pkt) ? "magic-pkt " : "",
			  (wowlan->disconnect) ? "disconnect " : "",
			  (wowlan->n_patterns) ? "patterns " : "",
			  (wowlan->rfkill_release) ? "rfkill-rel" : "");

	/* Automatically enable required offloads */
	ret = morse_cmd_beacon_offload(mors_vif, MORSE_BEACON_OFFLOAD_OP_START);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: beacon offload failed (ret:%d)\n", __func__, ret);
		goto exit;
	}
	beacon_offload_enabled = true;

	ret = morse_cmd_probe_response_offload(mors, mors_vif, true);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: probe offload failed (ret:%d)\n", __func__, ret);
		goto exit;
	}
	probe_offload_enabled = true;

	/* Keep HW awake during quiesce + detach, morse_ps_system_suspend()
	 * will release waker and wake veto on HW.
	 */
	morse_ps_wakers_inc(mors);
	morse_ps_force_eval(mors);
	wakers_set = true;

	set_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags);
	quiesce_morse_hw(mors);
	hw_quiesced = true;

	ret = morse_hw_detach(mors, headless_cfg);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: failed to detach (ret:%d)\n", __func__, ret);
		goto exit;
	}
	hw_detached = true;

	/* Prepare power save subsystem for suspend */
	ret = morse_ps_system_suspend(mors);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: failed to enable suspend wake source (ret:%d)\n",
			__func__, ret);
		goto exit;
	}

	if (mors->bus_ops->config_pm_flags)
		mors->bus_ops->config_pm_flags(mors);

exit:
	if (ret) {
		/* Unwind any suspend preparation that has been done, but don't clobber ret, so that
		 * the returned error can be propagated up to mac80211
		 */
		if (hw_detached)
			morse_hw_attach(mors, MORSE_CMD_HEADLESS_CFG_OPTION_KEEP_IFACES);
		if (hw_quiesced) {
			resume_morse_hw(mors);
			/* morse_wowlan_op_resume() will not be called if error is returned
			 * from this handler
			 */
			clear_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags);
		}
		if (wakers_set)
			morse_ps_wakers_dec(mors);
		if (probe_offload_enabled)
			morse_cmd_probe_response_offload(mors, mors_vif, false);
		if (beacon_offload_enabled)
			morse_cmd_beacon_offload(mors_vif, MORSE_BEACON_OFFLOAD_OP_STOP);
	}

	mutex_unlock(&mors->lock);
	trace_wowlan_suspend_return(ret);
	return ret;
}

int morse_wowlan_op_resume(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;
	struct ieee80211_vif *vif;
	struct morse_vif *mors_vif;
	int ret;

	trace_wowlan_resume(0);
	mutex_lock(&mors->lock);

	morse_ps_system_resume(mors);
	ret = morse_hw_attach(mors, MORSE_CMD_HEADLESS_CFG_OPTION_KEEP_IFACES);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: reattach failed (ret:%d)\n", __func__, ret);
		goto exit;
	}

	if (!test_and_clear_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags))
		MORSE_WOWLAN_WARN(mors, "%s: not in suspend on resume\n", __func__);

	resume_morse_hw(mors);

	vif = morse_get_ap_vif(mors);
	mors_vif = (vif) ? ieee80211_vif_to_morse_vif(vif) : NULL;
	if (!mors_vif) {
		/* This is unexpected but not catastrophic - go straight to exit */
		MORSE_WARN_ON(FEATURE_ID_WOWLAN, 1);
		ret = 0;
		goto exit;
	}

	ret = morse_cmd_beacon_offload(mors_vif, MORSE_BEACON_OFFLOAD_OP_STOP);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: stop beacon offload failed (ret:%d)\n", __func__, ret);
		goto exit;
	}

	ret = morse_cmd_probe_response_offload(mors, mors_vif, false);
	if (ret) {
		MORSE_WOWLAN_ERR(mors, "%s: stop presp offload failed (ret:%d)\n", __func__, ret);
		goto exit;
	}

exit:
	trace_wowlan_resume_return(ret);

	if (ret) {
		/* Unconditionally clear suspend system flag on failure */
		clear_bit(MORSE_STATE_FLAG_SYSTEM_IN_SUSPEND, &mors->state_flags);

		if (!morse_coredump_new(mors, MORSE_COREDUMP_REASON_FAILED_TO_RESUME))
			set_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags);

		mutex_unlock(&mors->lock);

		MORSE_WOWLAN_ERR(mors, "%s: failed - requesting HW restart\n", __func__);
		morse_hw_restart(mors);
		/* zero ret despite failures above, recovery will occur in restart flow */
		ret = 0;
	} else {
		mutex_unlock(&mors->lock);
		MORSE_WOWLAN_INFO(mors, "%s: complete\n", __func__);
	}

	return ret;
}

void morse_wowlan_op_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct morse *mors = hw->priv;

	/* Allow the hw to wake the system out of suspend */
	device_init_wakeup(mors->dev, enabled);
	MORSE_WOWLAN_INFO(mors, "%s: device is %swakeup capable\n",
		  __func__, device_can_wakeup(mors->dev) ? "" : "not ");
}
