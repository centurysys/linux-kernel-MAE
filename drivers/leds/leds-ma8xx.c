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
#include <linux/leds.h>
#include <linux/delay.h>

#include <asm/arch/gpio.h>
#include <asm/arch/board-ma8xx.h>

struct ma8xx_led_info {
	struct led_classdev cdev;
	struct ma8xx_gpio_port *port;
};

static int led_num;
static struct ma8xx_led_info *led_info;

static void ma8xx_led_halt(void);
static long ma8xx_panic_blink(long count);

static void ma8xx_led_set(struct led_classdev *cdev,
                          enum led_brightness value)
{
	struct ma8xx_led_info *info = dev_get_drvdata(cdev->dev);

	mxc_set_gpio_direction(info->port->pin, 0);             /* OUTPUT */

	if (value)
		mxc_set_gpio_dataout(info->port->pin, 0);
	else
		mxc_set_gpio_dataout(info->port->pin, 1);
}

static int ma8xx_led_probe(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_led_info *info;
	int ret, i;

	info = kzalloc(sizeof(struct ma8xx_led_info) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

        led_num = priv->nr_gpio;

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].port = &priv->ports[i];
		info[i].cdev.name = priv->ports[i].name;
		info[i].cdev.brightness_set = ma8xx_led_set;

                info[i].cdev.default_trigger = (i == 7 ? "heartbeat" :  /* Power  [R] */
                                                i == 2 ? "ide-disk" :   /* Status [G] */
                                                (i == 6 ? "mmc0" :      /* Status [R] */
                                                 "timer"));

		ret = led_classdev_register(&pdev->dev, &info[i].cdev);
		if (ret < 0) {
			while (i-- >= 0)
				led_classdev_unregister(&info[i].cdev);

			kfree(info);
			return ret;
		} else
                        ma8xx_led_set(&info[i].cdev, (i == 3 ? 1 : 0));
	}

        led_info = info;
	platform_set_drvdata(pdev, info);

        ma8xx_power_off_prepare = ma8xx_led_halt;
	panic_blink = ma8xx_panic_blink;

	return 0;
}

static int ma8xx_led_remove(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_led_info *info = platform_get_drvdata(pdev);
	int i;

        led_info = NULL;
        ma8xx_power_off_prepare = NULL;
	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++)
		led_classdev_unregister(&info[i].cdev);

	kfree(info);

	return 0;
}

extern void led_trigger_set_default(struct led_classdev *led_cdev);

static void ma8xx_led_halt(void)
{
        int i;

        if (!led_info)
                return;

        for (i = 0; i < led_num; i++) {
                led_info[i].cdev.default_trigger = "timer";
                led_trigger_set_default(&led_info[i].cdev);
        }

        for (i = 0; i < led_num; i++)
                ma8xx_led_set(&led_info[i].cdev, (i == 3 ? 1 : 0));
}

#define DELAY do { mdelay(1); if (++delay > 10) return delay; } while(0)

/*
 *
 */
static long ma8xx_panic_blink(long count)
{
        int i;
	long delay = 0;
        static int init = 0, led;
        static long last_blink;

        if (unlikely(init == 0)) {
                for (i = 0; i < led_num; i++) {
                        led_info[i].cdev.default_trigger = "timer";
                        led_trigger_set_default(&led_info[i].cdev);

                        ma8xx_led_set(&led_info[i].cdev, 0);
                }

                init = 1;
        }

	if (count - last_blink < 200)
		return 0;

        ma8xx_led_set(&led_info[7].cdev, led);

        led = (led == 0 ? 1 : 0);

        DELAY;

	last_blink = count;
        return delay;
}

static struct platform_driver ma8xx_led_driver = {
	.probe	= ma8xx_led_probe,
	.remove	= __devexit_p(ma8xx_led_remove),
	.driver	= {
		.name	= "ma8xx_led",
	},
};

static int __init ma8xx_led_init(void)
{
	return platform_driver_register(&ma8xx_led_driver);
}

static void __exit ma8xx_led_exit(void)
{
	platform_driver_unregister(&ma8xx_led_driver);
}

module_init(ma8xx_led_init);
module_exit(ma8xx_led_exit);

MODULE_AUTHOR("Century Systems");
MODULE_DESCRIPTION("MA-8xx LED driver");
MODULE_LICENSE("GPL v2");
