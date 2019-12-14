// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Kernel Timer Trigger for Suspend
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/suspend.h>

static ssize_t led_delay_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_on);
}

static ssize_t led_delay_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_cdev->blink_delay_on = state;

	return size;
}

static ssize_t led_delay_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_off);
}

static ssize_t led_delay_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_cdev->blink_delay_off = state;

	return size;
}

static DEVICE_ATTR(delay_on, 0644, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0644, led_delay_off_show, led_delay_off_store);

static struct attribute *timer_suspend_trig_attrs[] = {
	&dev_attr_delay_on.attr,
	&dev_attr_delay_off.attr,
	NULL
};
ATTRIBUTE_GROUPS(timer_suspend_trig);

static int timer_suspend_trig_activate(struct led_classdev *led_cdev)
{
	led_cdev->trigger_data = NULL;

	led_cdev->blink_delay_on = 500;
	led_cdev->blink_delay_off = 500;

	led_cdev->activated = true;

	return 0;
}

static void timer_suspend_trig_deactivate(struct led_classdev *led_cdev)
{
	/* Stop blinking */
	led_set_brightness(led_cdev, LED_OFF);
}

static struct led_trigger timer_suspend_led_trigger = {
	.name     = "timer-suspend",
	.activate = timer_suspend_trig_activate,
	.deactivate = timer_suspend_trig_deactivate,
	.groups = timer_suspend_trig_groups,
};

static int timer_suspend_pm_notifier(struct notifier_block *nb,
				     unsigned long code, void *unused)
{
	struct led_trigger *trig = &timer_suspend_led_trigger;
	struct led_classdev *led_cdev;

	read_lock(&trig->leddev_list_lock);

	list_for_each_entry(led_cdev, &trig->led_cdevs, trig_list) {
		switch (code) {
		case PM_SUSPEND_PREPARE:
			led_blink_set(led_cdev, &led_cdev->blink_delay_on,
				      &led_cdev->blink_delay_off);
			break;

		case PM_POST_SUSPEND:
			led_set_brightness(led_cdev, LED_OFF);
			break;
		}
	}

	read_unlock(&trig->leddev_list_lock);

	return NOTIFY_OK;
}

static struct notifier_block timer_suspend_pm_nb = {
	.notifier_call = timer_suspend_pm_notifier,
};

static int __init timer_suspend_trig_init(void)
{
	int rc = led_trigger_register(&timer_suspend_led_trigger);

	if (rc == 0) {
		register_pm_notifier(&timer_suspend_pm_nb);
	}

	return rc;
}

static void __exit timer_suspend_trig_exit(void)
{
	unregister_pm_notifier(&timer_suspend_pm_nb);
	led_trigger_unregister(&timer_suspend_led_trigger);
}

module_init(timer_suspend_trig_init);
module_exit(timer_suspend_trig_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Timer(Suspend) LED trigger");
MODULE_LICENSE("GPL");
