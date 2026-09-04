/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "morse.h"
#include "debug.h"
#include "bus.h"
#include "led.h"
#include "mac.h"
#include "ps.h"

#include <linux/leds.h>
#include <linux/workqueue.h>

/* Enable/disable chip-attached LEDs (default ON)
 * Note: The softmac driver still exposes halow-traffic triggers to hook to LEDs attached to the
 * host (not the hardware) regardless of this modparam
 */
static bool enable_hw_leds __read_mostly = true;
module_param(enable_hw_leds, bool, 0644);
MODULE_PARM_DESC(enable_hw_leds, "Enable HW-attached LEDs");

/* This is based on the iwlwifi.c blink rates. */
static const struct ieee80211_tpt_blink morse_blink[] = {
	{ .throughput = 0, .blink_time = 334 },
	{ .throughput = 512 - 1, .blink_time = 260 },
	{ .throughput = 1024 - 1, .blink_time = 220 },
	{ .throughput = 2 * 1024 - 1, .blink_time = 190 },
	{ .throughput = 4 * 1024 - 1, .blink_time = 170 },
	{ .throughput = 7 * 1024 - 1, .blink_time = 150 },
	{ .throughput = 11 * 1024 - 1, .blink_time = 130 },
	{ .throughput = 16 * 1024 - 1, .blink_time = 110 },
	{ .throughput = 22 * 1024 - 1, .blink_time = 80 },
	{ .throughput = 29 * 1024 - 1, .blink_time = 50 },
};

/**
 * Create the throughput LED trigger.
 *
 * Registers the above throughput table to allocate an LED trigger that blinks faster as throughput
 * increases. Stores the name of this trigger for later use by morse_led_get_tpt_led_name to attach
 * it to an LED device.
 *
 * The kernel will allocate memory to create this trigger, which it frees itself when
 * ieee80211_unregister_hw is called. Other LED triggers need not be handled the same way because
 * their allocation/deallocation is handled in a different path.
 *
 * @mors: Global morse struct
 */
static void morse_led_create_tpt_trigger(struct morse *mors)
{
	mors->cfg->led_group.tpt_trigger_name = ieee80211_create_tpt_led_trigger(mors->hw,
							IEEE80211_TPT_LEDTRIG_FL_CONNECTED,
							morse_blink, ARRAY_SIZE(morse_blink));
}

/**
 * Get the name of the throughput LED trigger.
 *
 * Must be called after the trigger is allocated by morse_led_create_tpt_trigger.
 *
 * @mors: Global morse struct
 *
 * Returns: name of the TPT trigger
 */
static const char *morse_led_get_tpt_led_name(struct morse *mors)
{
	return mors->cfg->led_group.tpt_trigger_name;
}

static void morse_led_callback(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct morse_led *led = container_of(led_cdev, struct morse_led, led_cdev);

	led->active = brightness > 0 ? 1 : 0;
	schedule_work(&led->work);
}

static void led_work_function(struct work_struct *work)
{
	struct morse *mors;
	struct device *dev;
	struct morse_led *led = container_of(work, struct morse_led, work);

	dev = led->led_cdev.dev->parent;
	if (!dev)
		return;

	mors = (struct morse *)dev_get_drvdata(dev);
	if (!mors || !mors->cfg || !mors->cfg->gpio_write_output)
		return;

	mors->cfg->gpio_write_output(mors, led->pin_num, led->active);
}

static int morse_led_feature_enabled(struct morse *mors)
{
	return enable_hw_leds &&
	       mors->cfg->led_group.enable_led_support &&
	       !MORSE_DEVICE_TYPE_IS_FPGA(mors->chip_id) &&
	       !morse_ps_is_supported(mors);
}

static const char *morse_led_get_trigger_softmac(struct morse *mors, enum led_mode mode)
{
	const char *trigger;

	switch (mode) {
	case MORSE_LED_TX:
		trigger = ieee80211_get_tx_led_name(mors->hw);
		break;
	case MORSE_LED_RX:
		trigger = ieee80211_get_rx_led_name(mors->hw);
		break;
	case MORSE_LED_ASSOC:
		trigger = ieee80211_get_assoc_led_name(mors->hw);
		break;
	case MORSE_LED_RADIO:
		trigger = ieee80211_get_radio_led_name(mors->hw);
		break;
	case MORSE_LED_TPT:
		trigger = morse_led_get_tpt_led_name(mors);
		break;
	case MORSE_LED_DEFAULT_OFF:
		/* Still register led but defer control to user space */
		trigger = NULL;
		break;
	case MORSE_LED_DEFAULT_ON:
		trigger = "default-on";
		break;
	default:
		MORSE_WARN(mors, "Unknown LED behaviour\n");
		trigger = NULL;
	}
	return trigger;
}

static const char *morse_led_get_trigger_fullmac(struct morse *mors, enum led_mode mode)
{
	const char *trigger;

	switch (mode) {
	/* Several triggers unimplemented for fullmac, default to off */
	case MORSE_LED_TX:
	case MORSE_LED_RX:
	case MORSE_LED_ASSOC:
	case MORSE_LED_RADIO:
	case MORSE_LED_TPT:
	case MORSE_LED_DEFAULT_OFF:
		trigger = NULL;
		break;
	case MORSE_LED_DEFAULT_ON:
		trigger = "default-on";
		break;
	default:
		MORSE_WARN(mors, "Unknown LED behaviour\n");
		trigger = NULL;
	}
	return trigger;
}

static int morse_led_register(struct morse *mors, struct morse_led *led, int pin_num,
			const char *name, enum led_mode mode)
{
	int ret = 0;

	led->mode = mode;
	if (mode == MORSE_LED_DISABLED)
		return 0;

	ret = (!mors->cfg->gpio_enable_output || mors->cfg->gpio_enable_output(mors, pin_num, 1));
	if (ret) {
		MORSE_WARN(mors, "LED could not enable GPIO %d output\n", pin_num);
		led->mode = MORSE_LED_DISABLED;
		return ret;
	}

	if (is_fullmac_mode())
		led->led_cdev.default_trigger = morse_led_get_trigger_fullmac(mors, led->mode);
	else
		led->led_cdev.default_trigger = morse_led_get_trigger_softmac(mors, led->mode);

	led->pin_num = pin_num;
	led->led_cdev.name = name;
	led->led_cdev.max_brightness = 1;
	led->led_cdev.brightness_set = morse_led_callback;

	INIT_WORK(&led->work, led_work_function);

	MORSE_INFO(mors, "Register LED %s: pin %d, trigger [%s]", name, pin_num,
		   led->led_cdev.default_trigger ? led->led_cdev.default_trigger : "none");

	ret = led_classdev_register(mors->dev, &led->led_cdev);
	if (ret) {
		MORSE_WARN(mors, "Failed to register LED device on pin %d\n", pin_num);
		if (mors->cfg->gpio_write_output)
			mors->cfg->gpio_write_output(mors, led->pin_num, 0);
		if (mors->cfg->gpio_enable_output)
			mors->cfg->gpio_enable_output(mors, led->pin_num, 0);
		led->mode = MORSE_LED_DISABLED;
	}

	return ret;
}

void morse_led_init(struct morse *mors)
{
	struct morse_led_group *led_group = &mors->cfg->led_group;

	/* Unconditionally create the LED throughput trigger in a softmac driver, even if the rest
	 * of the LED feature is disabled. Allows a host GPIO LED to use this trigger.
	 */
	if (!is_fullmac_mode())
		morse_led_create_tpt_trigger(mors);

	if (!morse_led_feature_enabled(mors))
		return;

	/* LED pin numbers are currently hard coded. Default behaviour is hardcoded too, but can be
	 * changed from userspace
	 */
	led_group->num_leds = NUM_LEDS;
	morse_led_register(mors, &led_group->leds[0], LED_G, "morse_led0", MORSE_LED_ASSOC);
	morse_led_register(mors, &led_group->leds[1], LED_R, "morse_led1", MORSE_LED_TPT);
	morse_led_register(mors, &led_group->leds[2], LED_PWR, "morse_led2", MORSE_LED_DEFAULT_ON);
}

void morse_led_exit(struct morse *mors)
{
	int i;
	struct morse_led *led;
	struct morse_led_group *led_group;

	if (!morse_led_feature_enabled(mors))
		return;

	led_group = &mors->cfg->led_group;
	for (i = 0; i < led_group->num_leds; i++) {
		led = &led_group->leds[i];

		if (led->mode == MORSE_LED_DISABLED)
			continue;

		led_classdev_unregister(&led->led_cdev);
		cancel_work_sync(&led->work);
		if (mors->cfg->gpio_write_output)
			mors->cfg->gpio_write_output(mors, led->pin_num, 0);
		if (mors->cfg->gpio_enable_output)
			mors->cfg->gpio_enable_output(mors, led->pin_num, 0);
		led->mode = MORSE_LED_DISABLED;
	}
}

void morse_led_restore_after_restart(struct morse *mors)
{
	int i;
	struct morse_led *led;
	struct morse_led_group *led_group;

	if (!morse_led_feature_enabled(mors))
		return;

	led_group = &mors->cfg->led_group;
	for (i = 0; i < led_group->num_leds; i++) {
		led = &led_group->leds[i];

		if (led->mode == MORSE_LED_DISABLED)
			continue;

		if (!mors->cfg->gpio_enable_output ||
		    mors->cfg->gpio_enable_output(mors, led->pin_num, 1)) {
			MORSE_WARN(mors, "LED could not re-enable GPIO %d output\n", led->pin_num);
			led_classdev_unregister(&led->led_cdev);
			cancel_work_sync(&led->work);
			led->mode = MORSE_LED_DISABLED;
			continue;
		}

		if (mors->cfg->gpio_write_output)
			mors->cfg->gpio_write_output(mors, led->pin_num, led->led_cdev.brightness);
	}
}
