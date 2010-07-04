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

struct ma8xx_dout_info {
	struct led_classdev cdev;
	struct ma8xx_gpio_port *port;
};

extern void gpio_dout_active(void);
extern void gpio_dout_inactive(void);

static int dout_num;
static struct ma8xx_dout_info *dout_info;

static void ma8xx_dout_set(struct led_classdev *cdev,
                           enum led_brightness value)
{
	struct ma8xx_dout_info *info = dev_get_drvdata(cdev->dev);

	mxc_set_gpio_direction(info->port->pin, 0);             /* OUTPUT */

	if (value)
		mxc_set_gpio_dataout(info->port->pin, 1);
	else
		mxc_set_gpio_dataout(info->port->pin, 0);
}

static int ma8xx_dout_probe(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_dout_info *info;
	int ret, i;

	info = kzalloc(sizeof(struct ma8xx_dout_info) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

        dout_num = priv->nr_gpio;

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].port = &priv->ports[i];
		info[i].cdev.name = priv->ports[i].name;
		info[i].cdev.brightness_set = ma8xx_dout_set;

		ret = led_classdev_register(&pdev->dev, &info[i].cdev);
		if (ret < 0) {
			while (i-- >= 0)
				led_classdev_unregister(&info[i].cdev);

			kfree(info);
			return ret;
		} else
                        ma8xx_dout_set(&info[i].cdev, 0);
	}

        gpio_dout_active();

        dout_info = info;
	platform_set_drvdata(pdev, info);

	return 0;
}

static int ma8xx_dout_remove(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_dout_info *info = platform_get_drvdata(pdev);
	int i;

        dout_info = NULL;
	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++) {
                ma8xx_dout_set(&info[i].cdev, 0);
		led_classdev_unregister(&info[i].cdev);
        }

        gpio_dout_inactive();

	kfree(info);

	return 0;
}

extern void led_trigger_set_default(struct led_classdev *led_cdev);

#if 0
static void ma8xx_dout_halt(void)
{
        int i;

        if (!dout_info)
                return;

        for (i = 0; i < dout_num; i++) {
                dout_info[i].cdev.default_trigger = "timer";
                led_trigger_set_default(&dout_info[i].cdev);
        }

        for (i = 0; i < dout_num; i++)
                ma8xx_dout_set(&dout_info[i].cdev, 0);
}
#endif

static struct platform_driver ma8xx_dout_driver = {
	.probe	= ma8xx_dout_probe,
	.remove	= __devexit_p(ma8xx_dout_remove),
	.driver	= {
		.name	= "ma8xx_dout",
	},
};

static int __init ma8xx_dout_init(void)
{
	return platform_driver_register(&ma8xx_dout_driver);
}

static void __exit ma8xx_dout_exit(void)
{
	platform_driver_unregister(&ma8xx_dout_driver);
}

module_init(ma8xx_dout_init);
module_exit(ma8xx_dout_exit);

MODULE_AUTHOR("Century Systems");
MODULE_DESCRIPTION("MA-8xx Contact-OUT driver");
MODULE_LICENSE("GPL v2");
