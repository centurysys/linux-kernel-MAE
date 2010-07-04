/*
 *  Copyright 2008 Atmark Techno, Inc. All Rights Reserved.
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

#include <asm/arch/gpio.h>
#include <asm/arch/board-ma8xx.h>

#define PSW_ENTRY_NAME          "driver/psw"
#define PSW_INIT_ENTRY_NAME     "driver/psw_init"
#define PSW_PCMCIA_ENTRY_NAME   "driver/psw_pcmcia1"

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

struct ma8xx_pushsw_info {
	struct input_dev *idev;
	struct ma8xx_gpio_port *port;
};

extern void gpio_pushsw_active(void);
extern void gpio_pushsw_inactive(void);

static struct platform_device *ma8xx_pdev;

static inline u8 ma8xx_psw_get(void)
{
	struct ma8xx_gpio_private *priv = ma8xx_pdev->dev.platform_data;
	struct ma8xx_pushsw_info *info = platform_get_drvdata(ma8xx_pdev);
	int i;
        u8 val = 0;

	for (i = 0; i < priv->nr_gpio; i++) {
                if (!mxc_get_gpio_datain(info[i].port->pin))
                        val |= 1 << (i * 4);
        }

        return val;
}

static int ma8xx_psw_read_proc(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%.2X\n", ma8xx_psw_get());

	PROC_READ_RETURN(page, start, off, count, eof, len);
}

static int ma8xx_psw_init_read_proc(char *page, char **start, off_t off,
                                    int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%.1X\n", ma8xx_psw_get() & 0x0f);

	PROC_READ_RETURN(page, start, off, count, eof, len);
}

static int ma8xx_psw_pcmcia_read_proc(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%.1X\n", (ma8xx_psw_get() & 0xf0) >> 4);

	PROC_READ_RETURN(page, start, off, count, eof, len);
}

static int ma8xx_pushsw_open(struct input_dev *idev)
{
	struct ma8xx_pushsw_info *info = dev_get_drvdata(&idev->dev);

//	mxc_set_gpio_direction(info->port->pin, 1);     /* INPUT */
	enable_irq(info->port->irq);

	if (mxc_get_gpio_datain(info->port->pin))
		set_irq_type(info->port->irq, IRQT_FALLING);
	else
		set_irq_type(info->port->irq, IRQT_RISING);

	return 0;
}

static void ma8xx_pushsw_close(struct input_dev *idev)
{
	struct ma8xx_pushsw_info *info = dev_get_drvdata(&idev->dev);

	disable_irq(info->port->irq);
}

static irqreturn_t ma8xx_pushsw_irq_handler(int irq, void *dev_id)
{
	struct input_dev *idev = (struct input_dev *) dev_id;
	struct ma8xx_pushsw_info *info = dev_get_drvdata(&idev->dev);

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

static int ma8xx_pushsw_probe(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_pushsw_info *info;
	int ret, i;
        unsigned long flags;

	info = kzalloc(sizeof(struct ma8xx_pushsw_info) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].idev = input_allocate_device();

		if (!info[i].idev) {
			while (i-- >= 0)
				input_free_device(info[i].idev);
			return -ENOMEM;
		}
	}

	if (!create_proc_read_entry(PSW_ENTRY_NAME, 0, 0, ma8xx_psw_read_proc, NULL)) {
		printk(KERN_ERR "%s: PUSHSW create proc error\n", __FUNCTION__);
                goto proc_error1;
	}

	if (!create_proc_read_entry(PSW_INIT_ENTRY_NAME, 0, 0, ma8xx_psw_init_read_proc, NULL)) {
		printk(KERN_ERR "%s: PSW INIT create proc error\n", __FUNCTION__);
		goto psw_init_error;
	}

	if (!create_proc_read_entry(PSW_PCMCIA_ENTRY_NAME, 0, 0, ma8xx_psw_pcmcia_read_proc, NULL)) {
		printk(KERN_ERR "%s: PSW PCMCIA slot create proc error\n", __FUNCTION__);
		goto psw_pcmcia_error;
	}

        local_irq_save(flags);
	for (i = 0; i < priv->nr_gpio; i++) {
		ret = request_irq(priv->ports[i].irq,
				  ma8xx_pushsw_irq_handler,
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

	for (i = 0; i < priv->nr_gpio; i++) {
		info[i].idev->name = priv->ports[i].name;
		info[i].idev->phys = NULL;
		info[i].idev->id.bustype = BUS_HOST;
		info[i].idev->dev.parent = &pdev->dev;

		info[i].idev->open = ma8xx_pushsw_open;
		info[i].idev->close = ma8xx_pushsw_close;

		info[i].idev->evbit[0] = BIT(EV_SW);
		info[i].idev->swbit[0] = BIT(i);

		ret = input_register_device(info[i].idev);

		if (ret < 0) {
			while (i-- >= 0)
				input_unregister_device(info[i].idev);
			goto _err_input_register_device;
		}

		info[i].port = &priv->ports[i];
                mxc_set_gpio_direction(info[i].port->pin, 1);     /* INPUT */
		dev_set_drvdata(&info[i].idev->dev, &info[i]);
	}

	gpio_pushsw_active();

	platform_set_drvdata(pdev, info);

        /* for procfs */
        ma8xx_pdev = pdev;

	return 0;

_err_input_register_device:
	for (i = 0; i < priv->nr_gpio; i++)
		free_irq(priv->ports[i].irq, info[i].idev);

_err_request_irq:
	for (i = 0; i < priv->nr_gpio; i++)
		input_free_device(info[i].idev);

psw_pcmcia_error:
	remove_proc_entry(PSW_INIT_ENTRY_NAME, NULL);
psw_init_error:
	remove_proc_entry(PSW_ENTRY_NAME, NULL);
        
proc_error1:

	kfree(info);

	return ret;
}

static int ma8xx_pushsw_remove(struct platform_device *pdev)
{
	struct ma8xx_gpio_private *priv = pdev->dev.platform_data;
	struct ma8xx_pushsw_info *info = platform_get_drvdata(pdev);
	int i;

	remove_proc_entry(PSW_ENTRY_NAME, NULL);
	remove_proc_entry(PSW_INIT_ENTRY_NAME, NULL);
	remove_proc_entry(PSW_PCMCIA_ENTRY_NAME, NULL);

	gpio_pushsw_active();

	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->nr_gpio; i++) {
		input_unregister_device(info[i].idev);
		free_irq(info[i].port->irq, info[i].idev);
		input_free_device(info[i].idev);
	}

	kfree(info);

	return 0;
}

static struct platform_driver ma8xx_pushsw_driver = {
	.probe	= ma8xx_pushsw_probe,
	.remove = __devexit_p(ma8xx_pushsw_remove),
	.driver	= {
		.name = "ma8xx_pushsw",
	},
};

static int __init
ma8xx_pushsw_init(void)
{
	return platform_driver_register(&ma8xx_pushsw_driver);
}

static void __exit
ma8xx_pushsw_exit(void)
{
	platform_driver_unregister(&ma8xx_pushsw_driver);
}

module_init(ma8xx_pushsw_init);
module_exit(ma8xx_pushsw_exit);

MODULE_AUTHOR("Atmark Techno, Inc.");
MODULE_DESCRIPTION("MA-8xx PUSH-SW driver");
MODULE_LICENSE("GPL v2");
