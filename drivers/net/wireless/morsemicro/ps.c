/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/gpio.h>

#include "morse.h"
#include "debug.h"
#include "skbq.h"
#include "mac.h"
#include "bus.h"
#include "hw.h"
#include "ps.h"

static inline u8 morse_ps_get_wakeup_delay_ms(struct morse *mors)
{
	return mors->cfg->get_ps_wakeup_delay_ms(mors->chip_id);
}

static int __morse_ps_wakeup(struct morse_ps *mps)
{
	struct morse *mors = container_of(mps, struct morse, ps);

	if (mps->suspended) {
		morse_dbg(mors, "%s: Wakeup Pin Set\n", __func__);
		/* bring chip up (and give it sometime to recover) */
		gpio_direction_output(mors->cfg->mm_wake_gpio, 1);

		/* MM6108A0 takes < 7ms to be active */
		mdelay(morse_ps_get_wakeup_delay_ms(mors));

		/* Enable SDIO bus and start getting interrupts */
		morse_set_bus_enable(mors, true);
		mps->suspended = false;
	}

	return 0;
}

static int morse_ps_wakeup(struct morse_ps *mps)
{
	int ret;

	mutex_lock(&mps->lock);
	ret = __morse_ps_wakeup(mps);
	mutex_unlock(&mps->lock);

	return ret;
}

static int __morse_ps_sleep(struct morse_ps *mps)
{
	struct morse *mors = container_of(mps, struct morse, ps);

	if (!mps->suspended) {
		morse_dbg(mors, "%s: Wakeup Pin Clear\n", __func__);
		mps->suspended = true;
		/* Disable SDIO bus and start getting interrupts */
		morse_set_bus_enable(mors, false);
		/* we are  asleep, bring wakeup pin up */
		gpio_direction_output(mors->cfg->mm_wake_gpio, 0);
	}

	return 0;
}

static irqreturn_t morse_ps_irq_handle(int irq, void *arg)
{
	struct morse_ps *mps = (struct morse_ps *) arg;
	struct morse *mors = container_of(mps, struct morse, ps);

	if (irq == gpio_to_irq(mors->cfg->mm_ps_async_gpio)) {
		morse_dbg(mors,
			"%s: Aysnc wakeup Request IRQ, waking up.\n", __func__);
		/* There is a delay in waking up, so pass to a queue */
		queue_work(mors->chip_wq, &mps->async_wake_work);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void morse_ps_async_wake_work(struct work_struct *work)
{
	struct morse_ps *mps = container_of(work,
		struct morse_ps, async_wake_work);

	/* We are here because the chip asked use to wakeup */
	morse_ps_wakeup(mps);
}

void morse_ps_bus_activity(struct morse *mors, int timeout_ms)
{
	mutex_lock(&mors->ps.lock);
	mors->ps.bus_ps_timeout = jiffies + msecs_to_jiffies(timeout_ms);
	mutex_unlock(&mors->ps.lock);
}

int __morse_ps_evaluate(struct morse_ps *mps)
{
	struct morse *mors = container_of(mps, struct morse, ps);
	bool needs_wake = false;
	bool eval_later = false;
	bool event_flags =
		(mors->chip_if->event_flags & ~(MORSE_DATA_TRAFFIC_PAUSE_PEND));

	if (!mps->enable)
		return 0;

	needs_wake = (mps->wakers > 0);
	needs_wake |= (event_flags > 0);
	needs_wake |= (mors->cfg->ops->skbq_get_tx_buffered_count(mors) > 0);

	if (!needs_wake &&
		mps->dynamic_ps_en &&
		morse_is_data_tx_allowed(mors) &&
		time_before(jiffies, mps->bus_ps_timeout)) {

		/* Eval later if there is nothing explicitly holding the bus awake,
		 * but the bus ps timeout has been set to some time in the future
		 * (i.e. network traffic has recently occurred).
		 *
		 * In TWT, the device may go into TWT sleep immediately without
		 * caring about recent network traffic.
		 */
		needs_wake = true;
		eval_later = true;
	}

	if (needs_wake) {
		__morse_ps_wakeup(mps);
	} else {
		if (!gpio_get_value(mors->cfg->mm_ps_async_gpio)) {
			__morse_ps_sleep(mps);
		} else {
			/* Chip has something to send across the bus, re-evaluate later */
			eval_later |= true;
		}
	}

	if (eval_later) {
		unsigned long expire = jiffies +
			msecs_to_jiffies(DEFAULT_BUS_TIMEOUT_MS);

		if (mps->dynamic_ps_en && time_after(mps->bus_ps_timeout, expire))
			expire = mps->bus_ps_timeout;

		expire = expire - jiffies;
		morse_dbg(mors, "%s: Delaying eval work by %d ms\n",
			__func__, jiffies_to_msecs(expire));

		cancel_delayed_work(&mps->delayed_eval_work);
		queue_delayed_work(mors->chip_wq,
			&mps->delayed_eval_work, expire);
	}

	return 0;
}

static void morse_ps_evaluate_work(struct work_struct *work)
{
	struct morse_ps *mps = container_of(work,
		struct morse_ps, delayed_eval_work.work);
	struct morse *mors = container_of(mps, struct morse, ps);

	if (!mps->enable)
		return;

	mutex_lock(&mps->lock);
	morse_dbg(mors, "%s: Wakers: %d\n", __func__, mps->wakers);
	__morse_ps_evaluate(mps);
	mutex_unlock(&mps->lock);
}

int morse_ps_enable(struct morse *mors)
{
	int ret;
	struct morse_ps *mps = &mors->ps;

	if (!mps->enable)
		return 0;

	mutex_lock(&mps->lock);
	BUG_ON(mps->wakers == 0);
	mps->wakers--;
	morse_dbg(mors, "%s: Wakers: %d\n", __func__, mps->wakers);
	ret = __morse_ps_evaluate(mps);
	mutex_unlock(&mps->lock);

	return ret;
}

int morse_ps_disable(struct morse *mors)
{
	int ret;
	struct morse_ps *mps = &mors->ps;

	if (!mps->enable)
		return 0;

	mutex_lock(&mps->lock);
	mps->wakers++;
	morse_dbg(mors, "%s: Wakers: %d\n", __func__, mps->wakers);
	ret = __morse_ps_evaluate(mps);
	mutex_unlock(&mps->lock);

	return ret;
}

int morse_ps_init(struct morse *mors, bool enable, bool enable_dynamic_ps)
{
	int ret;
	int irq = gpio_to_irq(mors->cfg->mm_ps_async_gpio);
	struct morse_ps *mps = &mors->ps;

	mps->enable = enable;
	mps->bus_ps_timeout = 0;
	mps->dynamic_ps_en = enable_dynamic_ps;
	mps->suspended = false;
	mps->wakers = 1;	/* we default to being on */
	mutex_init(&mps->lock);

	/* Default to allow chip to wakeup */
	ret = gpio_request(mors->cfg->mm_wake_gpio, "morse-wakeup-ctrl");
	if (ret < 0) {
		morse_pr_err("Failed to acquire wakeup gpio.\n");
		return ret;
	}
	gpio_direction_output(mors->cfg->mm_wake_gpio, 1);

	/**
	 * SW-1674: Should be the following, but issues observed.
	 * gpio_request_one(mors->cfg->mm_wake_gpio, GPIOF_OPEN_DRAIN, NULL);
	 */
	if (mps->enable) {
		INIT_WORK(&mps->async_wake_work, morse_ps_async_wake_work);
		INIT_DELAYED_WORK(&mps->delayed_eval_work,
			morse_ps_evaluate_work);

		gpio_request(mors->cfg->mm_ps_async_gpio, "morse-async-wakeup-ctrl");

		/* The following input gpio must be configured with pull-down */
		gpio_direction_input(mors->cfg->mm_ps_async_gpio);

		ret = request_irq(
			irq,
			(irq_handler_t) morse_ps_irq_handle,
			IRQF_TRIGGER_RISING,
			"async_wakeup_from_chip",
			mps);

		MORSE_WARN_ON(ret);
	}

	return 0;
}

void morse_ps_finish(struct morse *mors)
{
	struct morse_ps *mps = &mors->ps;
	int irq = gpio_to_irq(mors->cfg->mm_ps_async_gpio);

	if (mps->enable) {
		mps->enable = false;
		mps->dynamic_ps_en = false;
		free_irq(irq, mps);
		gpio_free(mors->cfg->mm_ps_async_gpio);
		cancel_work_sync(&mps->async_wake_work);
		cancel_delayed_work_sync(&mps->delayed_eval_work);
	}

	gpio_free(mors->cfg->mm_wake_gpio);
}
