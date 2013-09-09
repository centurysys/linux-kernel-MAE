/*
 *  Copyright 2009 Century Systems, Co.,Ltd. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307	 USA
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>

#include <mach/board-magnolia2.h>

#define DSW_ENTRY_NAME "driver/dsw"

#define PROC_READ_RETURN(page,start,off,count,eof,len)	\
	{						\
		len -= off;				\
		if (len < count) {			\
			*eof = 1;			\
			if (len <= 0)			\
				return 0;		\
		} else					\
			len = count;			\
		*start = page + off;			\
		return len;				\
	}

struct magnolia2_dipsw_info {
	struct input_dev *idev;
	struct magnolia2_gpio_port *port;
};

extern void gpio_dipsw_active(void);
extern void gpio_dipsw_inactive(void);

static struct platform_device *magnolia2_pdev;

static inline u8 magnolia2_dsw_get(void)
{
	struct magnolia2_gpio_private *priv = magnolia2_pdev->dev.platform_data;
	struct magnolia2_dipsw_info *info = platform_get_drvdata(magnolia2_pdev);
	int i, pin_stat;
	u8 val = 0;

	for (i = 0; i < priv->nr_gpio; i++) {
		pin_stat = gpio_get_value(info[i].port->pin);

		if (!pin_stat)
			val |= 1 << i;
	}

	return val;
}

static int magnolia2_dsw_read_proc(char *page, char **start, off_t off,
				   int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%.2X\n", magnolia2_dsw_get());

	PROC_READ_RETURN(page, start, off, count, eof, len);
}

static int magnolia2_dipsw_open(struct input_dev *idev)
{
#if 0
	struct magnolia2_dipsw_info *info = dev_get_drvdata(&idev->dev);

	gpio_direction_input(info->port->pin);
	enable_irq(info->port->irq);

	if (gpio_get_value(info->port->pin))
		irq_set_irq_type(info->port->irq, IRQT_FALLING);
	else
		irq_set_irq_type(info->port->irq, IRQT_RISING);
#endif

	return 0;
}

static void magnolia2_dipsw_close(struct input_dev *idev)
{
	struct magnolia2_dipsw_info *info = dev_get_drvdata(&idev->dev);

	disable_irq(info->port->irq);
}

static int magnolia2_dipsw_probe(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct magnolia2_dipsw_info *info;
	int ret, i;

	printk("Magnolia2 DIPSW driver\n");

	info = kzalloc(sizeof(struct magnolia2_dipsw_info) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!create_proc_read_entry(DSW_ENTRY_NAME, 0, 0, magnolia2_dsw_read_proc, NULL)) {
		printk(KERN_ERR "%s: DIPSW create proc error\n", __FUNCTION__);
		return -1;
	}

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].idev = input_allocate_device();

		if (!info[i].idev) {
			while (i-- >= 0)
				input_free_device(info[i].idev);
			return -ENOMEM;
		}
	}

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].idev->name = priv->ports[i].name;
		info[i].idev->phys = NULL;
		info[i].idev->id.bustype = BUS_HOST;
		info[i].idev->dev.parent = &pdev->dev;
		info[i].idev->open = magnolia2_dipsw_open;
		info[i].idev->close = magnolia2_dipsw_close;

		info[i].idev->evbit[0] = BIT(EV_SW);
		info[i].idev->swbit[0] = BIT(i);
		info[i].port = &priv->ports[i];

		gpio_request(info[i].port->pin, info[i].port->name);
		gpio_direction_input(info[i].port->pin);
		dev_set_drvdata(&info[i].idev->dev, &info[i]);
	}

	platform_set_drvdata(pdev, info);

	/* for procfs */
	magnolia2_pdev = pdev;

	return 0;

#if 0
_err_input_register_device:
	for (i = 0; i < priv->nr_gpio; i++)
		free_irq(priv->ports[i].irq, info[i].idev);

_err_request_irq:
	for (i = 0; i < priv->nr_gpio; i++)
		input_free_device(info[i].idev);
#endif

	kfree(info);

	return ret;
}

static int magnolia2_dipsw_remove(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct magnolia2_dipsw_info *info = platform_get_drvdata(pdev);
	int i;

	remove_proc_entry(DSW_ENTRY_NAME, NULL);
	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++) {
		gpio_free(info[i].port->pin);
	}

	kfree(info);

	return 0;
}

static struct platform_driver magnolia2_dipsw_driver = {
	.probe	= magnolia2_dipsw_probe,
	.remove = __devexit_p(magnolia2_dipsw_remove),
	.driver	= {
		.name = "magnolia2_dipsw",
	},
};

static int __init
magnolia2_dipsw_init(void)
{
	return platform_driver_register(&magnolia2_dipsw_driver);
}

static void __exit
magnolia2_dipsw_exit(void)
{
	platform_driver_unregister(&magnolia2_dipsw_driver);
}

module_init(magnolia2_dipsw_init);
module_exit(magnolia2_dipsw_exit);

MODULE_AUTHOR("Century Systems");
MODULE_DESCRIPTION("Magnolia2 DIP-SW driver");
MODULE_LICENSE("GPL v2");
