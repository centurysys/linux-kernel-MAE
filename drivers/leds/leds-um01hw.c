/*
 *  Copyright 2008 Atmark Techno, Inc. All Rights Reserved.
 *  Copyright 2009-2013 Century Systems Co., Ltd.
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

struct um01hw_led_info {
	struct led_classdev cdev;
	int shift;
};

static int led_num;
static struct um01hw_led_info *led_info;

static void um01hw_led_halt(void);

static void um01hw_led_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct um01hw_led_info *info = dev_get_drvdata(cdev->dev);
	volatile u8 current_val;

	current_val = *led_base;

	if (value)
		current_val |= 1 << info->shift;
	else
		current_val &= ~(1 << info->shift);

	*led_base = current_val;
}

static int um01hw_led_probe(struct platform_device *pdev)
{
	struct magnolia2_led_private *priv = pdev->dev.platform_data;
	struct resource *res;
	struct um01hw_led_info *info;
	int ret, i, size, shift;

	printk("Magnolia2 UM01-HW extension LED driver\n");

	info = kzalloc(sizeof(struct um01hw_led_info) * priv->nr_ports,
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
		info[i].cdev.brightness_set = um01hw_led_set;
		info[i].cdev.max_brightness = 1;
		info[i].cdev.brightness = (shift == 0 ? 1 : 0);

		info[i].cdev.default_trigger = "none";

		ret = led_classdev_register(&pdev->dev, &info[i].cdev);
		if (ret < 0) {
			while (i-- >= 0)
				led_classdev_unregister(&info[i].cdev);

			kfree(info);
			return ret;
		} else
			um01hw_led_set(&info[i].cdev, (shift == 0 ? 1 : 0));
	}

	led_info = info;
	led_num = priv->nr_ports;

	platform_set_drvdata(pdev, info);

	return 0;

out2:
	release_mem_region(res->start, size);
out1:
	kfree(info);

	return ret;
}

static int um01hw_led_remove(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct um01hw_led_info *info = platform_get_drvdata(pdev);
	int i;

	led_info = NULL;
	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++)
		led_classdev_unregister(&info[i].cdev);

	kfree(info);

	return 0;
}

extern void led_trigger_set_default(struct led_classdev *led_cdev);

static void um01hw_led_halt(void)
{
	int i;

	if (!led_info)
		return;

	for (i = 0; i < led_num; i++) {
		led_info[i].cdev.default_trigger = "default-on";
		led_trigger_set_default(&led_info[i].cdev);
	}

	for (i = 0; i < led_num; i++)
		led_set_brightness(&led_info[i].cdev, 0);
}

static struct platform_driver um01hw_led_driver = {
	.probe	= um01hw_led_probe,
	.remove	= __devexit_p(um01hw_led_remove),
	.driver	= {
		.name	= "um01hw_led",
	},
};

static int __init um01hw_led_init(void)
{
	return platform_driver_register(&um01hw_led_driver);
}

static void __exit um01hw_led_exit(void)
{
	platform_driver_unregister(&um01hw_led_driver);
}

module_init(um01hw_led_init);
module_exit(um01hw_led_exit);

MODULE_AUTHOR("Century Systems");
MODULE_DESCRIPTION("UM01-HW extension LED driver");
MODULE_LICENSE("GPL v2");
