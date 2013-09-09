/*
 * drivers/misc/rs422_switch.c
 *
 * Century Systems Magnolia2 RS-422 switch support
 *
 * Copyright (c) 2011 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/reboot.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

#include <linux/rs422_switch.h>

//#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(fmt, ## args)
#else
#define DPRINTK(fmt, args...) do {} while (0)
#endif

/* registers */
#define REG_PORT_STATUS	0x02
#define REG_PORT_SEL		0x03
#define REG_IRQ_CTRL		0x04

/* PORT_STATUS */
#define STAT_PORT_A	(1 << 0)
#define STAT_PORT_B	(1 << 1)
#define STAT_SELECTED	(1 << 4)

/* PORT_SEL */
#define SEL_PORT	(1 << 0)

/* IRQ_CTRL */
#define IRQ_CTRL_ENABLE (1 << 0)
#define IRQ_CTRL_CLEAR	(1 << 4)

#define SW_ENTRY	"driver/rs422switch"
#define DRIVER_NAME	"rs422switch"

static unsigned long iobase;

static inline u8 sw_read_reg(int offset)
{
	u8 val;

	val = __raw_readb((volatile void *) (iobase + offset));
	DPRINTK("%s: offset 0x%02x -> val: 0x%02x\n", __FUNCTION__, offset, val);

	return val;
}

static inline void sw_write_reg(int offset, u8 val)
{
	__raw_writeb(val, (volatile void *) (iobase + offset));
}

#define PROC_READ_RETURN\
	if (len <= off + count)\
		*eof = 1;\
	*start = page + off;\
	len -= off;\
\
	if (len > count)\
		len = count;\
\
	if (len < 0)\
		len = 0;\
\
	return len

static irqreturn_t rs422_switch_irq(int irq, void *devid)
{
	return IRQ_HANDLED;
}

static int __rs422_switch_read_proc(char *buf)
{
	char *p = buf;
	u8 reg;

	p += sprintf(p, "=== RS-422 Switch board status ===\n");
	reg = sw_read_reg(REG_PORT_STATUS);
	p += sprintf(p, "  STATUS   : 0x%02x\n", reg);
	p += sprintf(p, "    PORT_A    : %s\n", ((reg & STAT_PORT_A) ? "NG" : "OK"));
	p += sprintf(p, "    PORT_B    : %s\n", ((reg & STAT_PORT_B) ? "NG" : "OK"));
	p += sprintf(p, "    OPERATION : PORT_%s\n",
		     ((reg & STAT_SELECTED) ? "B" : "A"));

	reg = sw_read_reg(REG_PORT_SEL);
	p += sprintf(p, "  SEL      : 0x%02x\n", reg);
	p += sprintf(p, "    SEL PORT  : %s\n", ((reg & SEL_PORT) ? "B" : "A"));

	reg = sw_read_reg(REG_IRQ_CTRL);
	p += sprintf(p, "  IRQ_CTRL : 0x%02x\n", reg);
	p += sprintf(p, "    IRQ       : %sabled\n",
		     ((reg & IRQ_CTRL_ENABLE) ? "En" : "Dis"));

	return p - buf;
}

static int rs422_switch_read_proc(char *page, char **start, off_t off, int count,
				  int *eof, void *data)
{
	int len;
	unsigned long flags;

	local_irq_save(flags);
	len = __rs422_switch_read_proc(page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* ioctl: RS422_IOC_GET_STATUS */
static int _get_port_status(struct rs422_port_status *port_stat)
{
	u8 reg;

	reg = sw_read_reg(REG_PORT_STATUS);
	port_stat->port_a_ok = (reg & STAT_PORT_A) ? 0 : 1;
	port_stat->port_b_ok = (reg & STAT_PORT_B) ? 0 : 1;
	port_stat->port_op = (reg & STAT_SELECTED) ? 1 : 0;

	return 0;
}

/* ioctl: RS422_IOC_SELECT_PORT */
static int _select_port(int port)
{
	if (port < 0 || port > 1)
		return -EINVAL;

	sw_write_reg(REG_PORT_SEL, (u8) port);
	return 0;
}

static int rs422_switch_ioctl(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	int err = 0, retval = 0;
	struct rs422_port_status port_stat;
	int port_sel;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != RS422_SWITCH_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > RS422_SWITCH_IOC_MAXNR)
		return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	switch (cmd) {
	case RS422_IOC_GET_STATUS:
		DPRINTK("# %s: RS422_IOC_GET_STATUS\n", __FUNCTION__);
		retval = _get_port_status(&port_stat);
		if (retval == 0) {
			if (copy_to_user((void *) arg, &port_stat,
					 sizeof(struct rs422_port_status)))
				retval = -EFAULT;
		}
		break;

	case RS422_IOC_SELECT_PORT:
		DPRINTK("# %s: RS422_IOC_SELECT_PORT\n", __FUNCTION__);
		if (copy_from_user(&port_sel, (int *) arg,
				   sizeof(int))) {
			retval = -EFAULT;
			break;
		}
		retval = _select_port(port_sel);
		break;

	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static struct file_operations rs422_switch_fops = {
	.owner = THIS_MODULE,
	.compat_ioctl = rs422_switch_ioctl,
};

static struct miscdevice rs422_switch_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRIVER_NAME,
	.fops = &rs422_switch_fops,
};

static int rs422_switch_probe(struct platform_device *pdev)
{
	struct resource *res;
	int len, ret, irq;

	printk("Magnolia2 RS-422 Switcher driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto err1;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		goto err1;
	}

	len = res->end - res->start + 1;

	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto err1;
	}

	iobase = (unsigned long) ioremap(res->start, len);

	ret = request_irq(irq, rs422_switch_irq, IRQF_TRIGGER_LOW,
			  pdev->name, NULL);
	if (ret) {
		printk(KERN_ERR "request_irq() failed with %d\n", ret);
		goto err2;
	}

	misc_register(&rs422_switch_dev);
	create_proc_read_entry(SW_ENTRY, 0, 0, rs422_switch_read_proc, NULL);

	DPRINTK("RS422_IOC_GET_STATUS : 0x%08lx\n", RS422_IOC_GET_STATUS);
	DPRINTK("RS422_IOC_SELECT_PORT: 0x%08lx\n", RS422_IOC_SELECT_PORT);

	return 0;

err2:
	iounmap((void *) iobase);
	release_mem_region(res->start, res->end - res->start + 1);
err1:
	return ret;
}

static int rs422_switch_remove(struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (iobase) {
		misc_deregister(&rs422_switch_dev);
		remove_proc_entry(SW_ENTRY, NULL);
		free_irq(irq, NULL);
		iounmap((void *) iobase);

		release_mem_region(res->start, res->end - res->start + 1);
	}

	return 0;
}

static struct platform_driver rs422_switch_driver = {
	.driver = {
		.name	= "rs422_switch",
		.owner	= THIS_MODULE,
	},
	.probe		= rs422_switch_probe,
	.remove	= rs422_switch_remove,
};

static int __init rs422_switch_init(void)
{
	return platform_driver_register(&rs422_switch_driver);
}
module_init(rs422_switch_init);

static void rs422_switch_exit(void)
{
	platform_driver_unregister(&rs422_switch_driver);
}
module_exit(rs422_switch_exit);

MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RS-422 Switcher");
