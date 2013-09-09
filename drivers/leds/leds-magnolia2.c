/*
 *  Copyright 2008 Atmark Techno, Inc. All Rights Reserved.
 *  Copyright 2009 Century Systems Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <asm/io.h>
#include "leds.h"

#include <mach/board-magnolia2.h>

static volatile u8 *led_base;

struct magnolia2_led_info {
	struct led_classdev cdev;
	int shift;
};

static int led_num;
static struct magnolia2_led_info *led_info;

static void magnolia2_led_halt(void);
static long magnolia2_panic_blink(int state);

static void magnolia2_led_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct magnolia2_led_info *info = dev_get_drvdata(cdev->dev);
	volatile u8 current_val;

	current_val = *led_base;

	if (value)
		current_val |= 1 << info->shift;
	else
		current_val &= ~(1 << info->shift);

	*led_base = current_val;
}

static int magnolia2_led_probe(struct platform_device *pdev)
{
	struct magnolia2_led_private *priv = pdev->dev.platform_data;
	struct resource *res;
	struct magnolia2_led_info *info;
	int ret, i, size, shift;

	printk("Magnolia2 LED driver\n");

	info = kzalloc(sizeof(struct magnolia2_led_info) * priv->nr_ports,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		ret = -ENOMEM;
		goto out1;
	}

	size = res->end - res->start + 1;
	if (!request_mem_region(res->start, size, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed.\n");
		ret = -ENOMEM;
		goto out1;
	}

	led_base = (u8 *)ioremap(res->start, size);
	if (!led_base) {
		printk(KERN_ERR "ioremap failed.\n");
		ret = -ENOMEM;
		goto out2;
	}

	for (i = 0; i < priv->nr_ports; i++) {
		info[i].shift = shift = priv->ports[i].shift;
		info[i].cdev.name = priv->ports[i].name;
		info[i].cdev.brightness_set = magnolia2_led_set;
		info[i].cdev.max_brightness = 1;
		info[i].cdev.brightness = (shift == 0 ? 1 : 0);

		info[i].cdev.default_trigger = (shift == 4 ? "heartbeat" :  /* Power  [R] */
						(shift == 5 ? "mmc0" :	    /* Status [R] */
						 "none"));

		ret = led_classdev_register(&pdev->dev, &info[i].cdev);
		if (ret < 0) {
			while (i-- >= 0)
				led_classdev_unregister(&info[i].cdev);

			kfree(info);
			return ret;
		} else
			magnolia2_led_set(&info[i].cdev, (shift == 0 ? 1 : 0));
	}

	led_info = info;
	led_num = priv->nr_ports;

	platform_set_drvdata(pdev, info);

	magnolia2_power_off_prepare = magnolia2_led_halt;
	panic_blink = magnolia2_panic_blink;

	return 0;

out2:
	release_mem_region(res->start, size);
out1:
	kfree(info);

	return ret;
}

static int magnolia2_led_remove(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct magnolia2_led_info *info = platform_get_drvdata(pdev);
	int i;

	led_info = NULL;
	magnolia2_power_off_prepare = NULL;
	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++)
		led_classdev_unregister(&info[i].cdev);

	kfree(info);

	return 0;
}

extern void led_trigger_set_default(struct led_classdev *led_cdev);

static void magnolia2_led_halt(void)
{
	int i;

	if (!led_info)
		return;

	for (i = 0; i < led_num; i++) {
		led_info[i].cdev.default_trigger = "default-on";
		led_trigger_set_default(&led_info[i].cdev);
	}

	for (i = 0; i < led_num; i++)
		led_set_brightness(&led_info[i].cdev, (i == 3 ? 1 : 0));
}

#define DELAY do { mdelay(1); if (++delay > 10) return delay; } while(0)

/*
 *
 */
static long magnolia2_panic_blink(int state)
{
	int i;
	long delay = 0;
	static int init = 0, led;

	if (unlikely(init == 0)) {
		for (i = 0; i < led_num; i++) {
			led_info[i].cdev.default_trigger = "default-on";
			led_trigger_set_default(&led_info[i].cdev);

			led_set_brightness(&led_info[i].cdev, 0);
		}

		init = 1;
	}

	led = (state) ? 1 : 0;
	led_set_brightness(&led_info[7].cdev, led);
	//magnolia2_led_set(&led_info[7].cdev, led);

	DELAY;

	return delay;
}

static struct platform_driver magnolia2_led_driver = {
	.probe	= magnolia2_led_probe,
	.remove	= __devexit_p(magnolia2_led_remove),
	.driver	= {
		.name	= "magnolia2_led",
	},
};

static int __init magnolia2_led_init(void)
{
	return platform_driver_register(&magnolia2_led_driver);
}

static void __exit magnolia2_led_exit(void)
{
	platform_driver_unregister(&magnolia2_led_driver);
}

module_init(magnolia2_led_init);
module_exit(magnolia2_led_exit);

MODULE_AUTHOR("Century Systems");
MODULE_DESCRIPTION("Magnolia2 LED driver");
MODULE_LICENSE("GPL v2");
