/*
 *  Copyright 2008 Atmark Techno, Inc. All Rights Reserved.
 *  Copyright 2009 Century Systems, Co.,Ltd. All Rights Reserved.
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
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>

#include <asm/arch/board-ma8xx.h>
#include <asm/arch/gpio.h>

#define DSW_ENTRY_NAME "driver/dsw"

#define PROC_READ_RETURN(page,start,off,count,eof,len) \
{					\
	len -= off;			\
	if (len < count) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = count;		\
	*start = page + off;		\
	return len;			\
}

struct ma8xx_dipsw_info {
	struct input_dev *idev;
	struct ma8xx_gpio_port *port;
};

extern void gpio_dipsw_active(void);
extern void gpio_dipsw_inactive(void);

static struct platform_device *ma8xx_pdev;

static inline u8 ma8xx_dsw_get(void)
{
	struct ma8xx_gpio_private *priv = ma8xx_pdev->dev.platform_data;
	struct ma8xx_dipsw_info *info = platform_get_drvdata(ma8xx_pdev);
	int i;
        u8 val = 0;

	for (i = 0; i < priv->nr_gpio; i++) {
                if (!mxc_get_gpio_datain(info[i].port->pin))
                        val |= 1 << i;
        }

        return val;
}

static int ma8xx_dsw_read_proc(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%.2X\n", ma8xx_dsw_get());

	PROC_READ_RETURN(page, start, off, count, eof, len);
}

static int ma8xx_dipsw_open(struct input_dev *idev)
{
	struct ma8xx_dipsw_info *info = dev_get_drvdata(&idev->dev);

#if 0
	mxc_set_gpio_direction(info->port->pin, 1);     /* INPUT */
	enable_irq(info->port->irq);

	if (mxc_get_gpio_datain(info->port->pin))
		set_irq_type(info->port->irq, IRQT_FALLING);
	else
		set_irq_type(info->port->irq, IRQT_RISING);
#endif

	return 0;
}

static void ma8xx_dipsw_close(struct input_dev *idev)
{
	struct ma8xx_dipsw_info *info = dev_get_drvdata(&idev->dev);

	disable_irq(info->port->irq);
}

static irqreturn_t ma8xx_dipsw_irq_handler(int irq, void *dev_id)
{
	struct input_dev *idev = (struct input_dev *) dev_id;
	struct ma8xx_dipsw_info *info = dev_get_drvdata(&idev->dev);

	if (mxc_get_gpio_datain(info->port->pin)) {
		set_irq_type(info->port->irq, IRQT_FALLING);
		input_event(idev, EV_SW, 0, 0);         /* Released */
	} else {
		set_irq_type(info->port->irq, IRQT_RISING);
		input_event(idev, EV_SW, 0, 1);         /* Pushed */
	}

	input_sync(idev);

	return IRQ_HANDLED;
}

static int ma8xx_dipsw_probe(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_dipsw_info *info;
	int ret, i;
        unsigned long flags;

	info = kzalloc(sizeof(struct ma8xx_dipsw_info) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!create_proc_read_entry(DSW_ENTRY_NAME, 0, 0, ma8xx_dsw_read_proc, NULL)) {
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

#if 0
        local_irq_save(flags);
	for (i = 0; i < priv->nr_gpio; i++) {
		ret = request_irq(priv->ports[i].irq,
				  ma8xx_dipsw_irq_handler,
				  IRQF_DISABLED,
				  priv->ports[i].name,
				  info[i].idev);
		if (ret < 0) {
			while (i-- >= 0)
				free_irq(priv->ports[i].irq, info[i].idev);

                        local_irq_restore(flags);
			goto _err_request_irq;
		}

		disable_irq(priv->ports[i].irq);
	}
        local_irq_restore(flags);
#endif

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].idev->name = priv->ports[i].name;
		info[i].idev->phys = NULL;
		info[i].idev->id.bustype = BUS_HOST;
		info[i].idev->dev.parent = &pdev->dev;

		info[i].idev->open = ma8xx_dipsw_open;
		info[i].idev->close = ma8xx_dipsw_close;

		info[i].idev->evbit[0] = BIT(EV_SW);
		info[i].idev->swbit[0] = BIT(i);
#if 0
		ret = input_register_device(info[i].idev);

		if (ret < 0) {
			while (i-- >= 0)
				input_unregister_device(info[i].idev);
			goto _err_input_register_device;
		}
#endif
		info[i].port = &priv->ports[i];
                mxc_set_gpio_direction(info[i].port->pin, 1);     /* INPUT */
		dev_set_drvdata(&info[i].idev->dev, &info[i]);
	}

	gpio_dipsw_active();

	platform_set_drvdata(pdev, info);

        /* for procfs */
        ma8xx_pdev = pdev;

	return 0;

_err_input_register_device:
#if 0
	for (i = 0; i < priv->nr_gpio; i++)
		free_irq(priv->ports[i].irq, info[i].idev);
#endif

_err_request_irq:
#if 0
	for (i = 0; i < priv->nr_gpio; i++)
		input_free_device(info[i].idev);
#endif

	kfree(info);

	return ret;
}

static int ma8xx_dipsw_remove(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_dipsw_info *info = platform_get_drvdata(pdev);
	int i;

	remove_proc_entry(DSW_ENTRY_NAME, NULL);

	gpio_dipsw_active();

	platform_set_drvdata(pdev, NULL);

#if 0
	for (i = 0; i < priv->nr_gpio; i++) {
		input_unregister_device(info[i].idev);
		free_irq(info[i].port->irq, info[i].idev);
		input_free_device(info[i].idev);
	}
#endif

	kfree(info);

	return 0;
}

static struct platform_driver ma8xx_dipsw_driver = {
	.probe	= ma8xx_dipsw_probe,
	.remove = __devexit_p(ma8xx_dipsw_remove),
	.driver	= {
		.name = "ma8xx_dipsw",
	},
};

static int __init
ma8xx_dipsw_init(void)
{
	return platform_driver_register(&ma8xx_dipsw_driver);
}

static void __exit
ma8xx_dipsw_exit(void)
{
	platform_driver_unregister(&ma8xx_dipsw_driver);
}

module_init(ma8xx_dipsw_init);
module_exit(ma8xx_dipsw_exit);

MODULE_AUTHOR("Atmark Techno, Inc.");
MODULE_DESCRIPTION("MA-8xx DIP-SW driver");
MODULE_LICENSE("GPL v2");
