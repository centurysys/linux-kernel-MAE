/*
 * drivers/gpio/magnolia2-dio.c
 *
 * Century Systems Magnolia2 Ext-IO DIO support
 *
 * Copyright (c) 2010 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
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
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/sched.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

//#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(fmt, ## args)
#else
#define DPRINTK(fmt, args...) do {} while (0)
#endif

#define NUM_GROUP	8
#define PORT_PER_GROUP	4
#define NUM_PORTS	(NUM_GROUP * PORT_PER_GROUP)
#define NUM_OUT_PORTS	32

/* registers */
#define DIO_REG_BOARD_CTRL		0x00
#define DIO_REG_FAILOUT_SHIFT		15

#define DIO_REG_BOARD_STATUS		0x02

#define DIN_REG_PORT_STATUS(x)		(x < 16 ? 0x08 : 0x0a)
#define DIN_REG_IRQ_STATUS(x)		(x < 16 ? 0x0c : 0x0e)
#define DIN_REG_IRQ_ENABLE(x)		(x < 16 ? 0x10 : 0x12)
#define DIN_REG_IRQ_POLARITY(x)	(x < 16 ? 0x14 : 0x16)
#define DIN_REG_FILTER			0x18
#define DIN_REG_HWCOUNTER_CTRL		0x20
#define DIN_REG_MATCHIRQ_STATUS	0x22
#define DIN_REG_MATCHIRQ_ENABLE	0x24
#define DIN_REG_OVERFLOW		0x26
#define DIN_REG_HWCOUNTER(x) \
	(x < 8 ? (0x30 + x * 2) : ((x >= 16 && x < 32) ? (0x40 + (x - 16) * 2) : -1))
#define DIN_REG_MATCH_REG(x) \
	(x < 8 ? (0x50 + x * 2) : ((x >= 16 && x < 32) ? (0x60 + (x - 16) * 2) : -1))

#define DIN_REG_SHIFT(x)		(x % 16)
#define DIN_REG_BIT(x)			(1 << DIN_REG_SHIFT(x))
#define DIN_HWCOUNTER_SHIFT(x)		(x < 8 ? x : x - 8)
#define DIN_HWCOUNTER_BIT(x)		(1 << DIN_HWCOUNTER_SHIFT(x))

#define DOUT_REG_PORT_CTRL(x)		(x < 16 ? 0x80 : 0x82)


/* proc entry */
#define DIN_DIR			"driver/din"
#define DIN_SUBCLASS_GROUP		"group%02d"
#define DIN_ENTRY_FILTER		"filter"

#define DIN_SUBCLASS_PORT		"port%02d"
#define DIN_ENTRY_COUNTER_VAL		"counter_val"
#define DIN_ENTRY_COUNTER_VAL_DIFF	"counter_val_diff"
#define DIN_ENTRY_COUNTER_CTRL		"counter_ctrl"
#define DIN_ENTRY_HWCOUNTER_VAL	"hwcounter_val"
#define DIN_ENTRY_HWCOUNTER_CTRL	"hwcounter_ctrl"
#define DIN_ENTRY_POLARITY		"polarity"
#define DIN_ENTRY_EVENT		"event"
#define DIN_ENTRY_STATUS		"val"
#define DIN_ENTRY_STATUS_LNK		"port%02d"

#define DOUT_DIR			"driver/dout"
#define DOUT_ENTRY_PORT		"val%02d"

#define FAILOUT_ENTRY			"fail"

#define DIN_ENTRY_ALL			"driver/din/all"
#define DOUT_ENTRY_ALL			"driver/dout/all"

#define DIN_ENTRY_VAL_PRIMARY		"driver/din/primary"
#define DIN_ENTRY_VAL_SECONDARY	"driver/din/secondary"

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

static struct proc_dir_entry *dir_din  = NULL;
static struct proc_dir_entry *dir_dout = NULL;

static const char *onoff_string[] = {
	"off",
	"on"
};

static const char *polarity_string[] = {
	"rising",
	"falling"
};

static const char *filter_string[] = {
	"through",
	"50us",
	"1ms",
	"20ms",
};

static const char *failout_string[] = {
	"normal",
	"fail"
};

static unsigned long iobase;

struct magnolia2_din_group {
	const char *name;
	int hw_counter;
	int base;
	int filter;

	struct proc_dir_entry *entry;
};

struct magnolia2_din_port {
	char name[16];
	u32 counter_val;
	u32 counter_val_prev;
	u16 hwcounter_val;

	int enable_count;

	struct proc_dir_entry *entry;
	wait_queue_head_t wq;

	int counter_ctrl;
	int hwcounter_ctrl;
	int polarity;
};

#define MAGNOLIA2_DIN_GROUP(_name, _hwcounter, _base)\
{\
	.name = _name,\
	.hw_counter = _hwcounter,\
	.base = _base,\
}

static struct magnolia2_din_group din_groups[] = {
	MAGNOLIA2_DIN_GROUP("group0", 1,  0),
	MAGNOLIA2_DIN_GROUP("group1", 1,  4),
	MAGNOLIA2_DIN_GROUP("group2", 0,  8),
	MAGNOLIA2_DIN_GROUP("group3", 0, 12),
	MAGNOLIA2_DIN_GROUP("group4", 1, 16),
	MAGNOLIA2_DIN_GROUP("group5", 1, 20),
	MAGNOLIA2_DIN_GROUP("group6", 0, 24),
	MAGNOLIA2_DIN_GROUP("group7", 0, 28),
};

#define MAGNOLIA2_DIN_PORT(_name) \
{\
	.name = _name\
}

static struct magnolia2_din_port din_ports[NUM_PORTS];

static void magnolia2_remove_proc_entries(void);

static inline u16 dio_read_reg(int offset)
{
	u16 val;

	val = __raw_readw((volatile void *) (iobase + offset));
	DPRINTK("%s: offset 0x%02x -> val: 0x%04x\n", __FUNCTION__, offset, val);

	return val;
}

static inline void dio_write_reg(u16 val, int offset)
{
	DPRINTK("%s: offset 0x%02x <- val: 0x%04x\n", __FUNCTION__, offset, val);

	__raw_writew(val, (volatile void *) (iobase + offset));
}

static inline void dio_reg_set_bit(int shift, int offset)
{
	u16 val;

	val = dio_read_reg(offset);
	val |= 1 << shift;
	dio_write_reg(val, offset);
}

static inline void dio_reg_clear_bit(int shift, int offset)
{
	u16 val;

	val = dio_read_reg(offset);
	val &= ~(1 << shift);
	dio_write_reg(val, offset);
}

static inline int dio_reg_get_bit(int shift, int offset)
{
	u16 val;

	val = dio_read_reg(offset);
	return ((val & (1 << shift)) == 0 ? 0 : 1);
}

static int get_user_parameter(char **pbuf_k, const char __user *buf_u, unsigned long count)
{
	char *buf;

	if (!(buf = kzalloc(count + 1, GFP_KERNEL)))
		return -ENOMEM;

	if (copy_from_user(buf, buf_u, count)) {
		kfree(buf);
		return -EFAULT;
	}

	if (buf[count - 1] == '\n')
		buf[count - 1] = (char) 0;

	*pbuf_k = buf;
	return 0;
}

static void init_ports(void)
{
	int i;

	memset(din_ports, 0, sizeof(struct magnolia2_din_port) * NUM_PORTS);

	for (i = 0; i < NUM_PORTS; i++) {
		sprintf(din_ports[i].name, "port%02d", i);
		init_waitqueue_head(&din_ports[i].wq);
	}
}

/*
 *  Interrupt handlers
 */
static int din_check_status_irq(void)
{
	int fs, portno, nums = 0;
	u32 irq_status;
	struct magnolia2_din_port *din_port;

	irq_status  = dio_read_reg(DIN_REG_IRQ_STATUS(0));		/* Primary */
	irq_status |= dio_read_reg(DIN_REG_IRQ_STATUS(16)) << 16;	/* Secondary */

	while ((fs = ffs(irq_status)) > 0) {
		portno = fs -1;

		din_port = &din_ports[portno];

		if (din_port->counter_ctrl == 1)
			din_port->counter_val++;

		dio_reg_set_bit(DIN_REG_SHIFT(portno), DIN_REG_IRQ_STATUS(portno));
		wake_up_interruptible_all(&din_port->wq);

		irq_status &= ~(1 << portno);
		nums++;
	}

	return nums;
}

static int din_check_match_irq(void)
{
	int fs, portno, nums = 0;
	u16 irq_status, irq_status_save, match_reg, overflow_reg, reg;
	struct magnolia2_din_port *din_port;

	irq_status_save = irq_status = dio_read_reg(DIN_REG_MATCHIRQ_STATUS);
	overflow_reg = dio_read_reg(DIN_REG_OVERFLOW);

	while ((fs = ffs(irq_status)) > 0) {
		portno = (fs <= 8 ? (fs - 1) : (fs + 7));
		din_port = &din_ports[portno];

		match_reg = dio_read_reg(DIN_REG_MATCH_REG(portno));

		if (match_reg == 0x0000 &&
		    overflow_reg & DIN_HWCOUNTER_BIT(portno)) {
			/* lap-round */
			din_port->hwcounter_val++;
			reg = 0x8000;
		} else
			reg = 0x0000;

		dio_write_reg(reg, DIN_REG_MATCH_REG(portno));

		irq_status &= ~(1 << (fs - 1));
		nums++;
	}

	if (nums > 0) {
		dio_write_reg(irq_status_save, DIN_REG_MATCHIRQ_STATUS);
		dio_write_reg(overflow_reg, DIN_REG_OVERFLOW);
	}

	return nums;
}

static irqreturn_t magnolia2_din_irq(int irq, void *devid)
{
	int nums;

	nums  = din_check_status_irq();
	nums += din_check_match_irq();

	if (nums == 0)
		printk(KERN_ERR "%s: spurious interrupt?", __FUNCTION__);

	return IRQ_HANDLED;
}

/* support functions */
static void enable_port_irq(int portno)
{
	struct magnolia2_din_port *din_port;
	unsigned long flags;

	din_port = &din_ports[portno];

	local_irq_save(flags);
	if (++din_port->enable_count == 1)
		dio_reg_set_bit(portno % 16, DIN_REG_IRQ_ENABLE(portno));
	local_irq_restore(flags);
}

static void disable_port_irq(int portno)
{
	struct magnolia2_din_port *din_port;
	unsigned long flags;

	din_port = &din_ports[portno];

	local_irq_save(flags);
	if (din_port->enable_count > 0) {
		if (--din_port->enable_count == 0)
			dio_reg_clear_bit(portno % 16, DIN_REG_IRQ_ENABLE(portno));
	} else
		printk(KERN_ERR "%s: unbalanced PortIRQ enable/disable.", __FUNCTION__);
	local_irq_restore(flags);
}

/* proc functions */

/* port status */
static inline int __get_port_status(int portno, char *buf)
{
	char *p = buf;
	u16 status;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	status = dio_read_reg(DIN_REG_PORT_STATUS(portno));
	DPRINTK("%s: port[%d] offset 0x%02x -> val(all) 0x%04x\n",
		__FUNCTION__, portno, DIN_REG_PORT_STATUS(portno), status);

	p += sprintf(p, "%d\n", (status & DIN_REG_BIT(portno)) >> (DIN_REG_SHIFT(portno)));

	return p - buf;
}

static int get_port_status(char *page, char **start, off_t off, int count,
			   int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_port_status(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* counter_val */
static int clear_counter_val(struct file *filep, const char __user *buf,
			     unsigned long count, void *data)
{
	int portno = (int) data;
	unsigned long flags;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	local_irq_save(flags);
	din_port->counter_val = 0;
	din_port->counter_val_prev = 0;
	local_irq_restore(flags);

	return count;
}

static inline int __get_counter_val(int portno, char *buf, int diff)
{
	char *p = buf;
	unsigned long val;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	val = (unsigned long) din_port->counter_val;
	if (diff) {
		val -= din_port->counter_val_prev;
		din_port->counter_val_prev = din_port->counter_val;
	}

	p += sprintf(p, "%lu\n0x%08lx\n", (unsigned long) val, val);

	return p - buf;
}

static int get_counter_val(char *page, char **start, off_t off, int count,
			   int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_counter_val(portno, page, 0);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

static int get_counter_val_diff(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_counter_val(portno, page, 1);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* hwcounter_val */
static int set_hwcounter_val(struct file *filep, const char __user *buf,
			     unsigned long count, void *data)
{
	int portno = (int) data, ret;
	char *_buf;
	unsigned long flags;
	u32 val;
	u16 regl, regh, match;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	if (((val = (u32) simple_strtoul(_buf, NULL, 10)) == 0) &&
	    ((val = (u32) simple_strtoul(_buf, NULL, 16)) == 0))
		val = 0;

	regl = 0x0000ffff & val;
	regh = (u16) ((val & 0xffff0000) >> 16);

	din_port = &din_ports[portno];

	local_irq_save(flags);

	dio_write_reg(regl, DIN_REG_HWCOUNTER(portno));

	if (regl >= 0x8000)
		match = 0x0000;
	else
		match = 0x8000;
	dio_write_reg(match, DIN_REG_MATCH_REG(portno));

	dio_write_reg(DIN_HWCOUNTER_BIT(portno), DIN_REG_MATCHIRQ_STATUS);
	din_port->hwcounter_val = regh;

	local_irq_restore(flags);

	return count;
}

static inline int __get_hwcounter_val(int portno, char *buf)
{
	char *p = buf;
	u32 val;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	val = (u32) dio_read_reg(DIN_REG_HWCOUNTER(portno));
	val |= ((u32) din_port->hwcounter_val) << 16;

	p += sprintf(p, "%lu\n0x%08x\n", (unsigned long) val, val);

	return p - buf;
}

static int get_hwcounter_val(char *page, char **start, off_t off, int count,
			     int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_hwcounter_val(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* counter_ctrl */
static int set_counter_ctrl(struct file *filep, const char __user *buf,
			    unsigned long count, void *data)
{
	int portno = (int) data, on, ret;
	char *_buf;
	unsigned long flags;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	din_port = &din_ports[portno];

	if (!strncmp(_buf, "on", count) || !strncmp(_buf, "1", count))
		on = 1;
	else if (!strncmp(_buf, "off", count) || !strncmp(_buf, "0", count))
		on = 0;
	else {
		printk(KERN_ERR "%s: value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EINVAL;
	}

	kfree(_buf);

	local_irq_save(flags);

	if (on == 1) {
		if (din_port->counter_ctrl == 0) {
			din_port->counter_ctrl = 1;
			enable_port_irq(portno);
		}
	} else {
		if (din_port->counter_ctrl == 1) {
			din_port->counter_ctrl = 0;
			disable_port_irq(portno);
		}
	}

	local_irq_restore(flags);

	return count;
}

static inline int __get_counter_ctrl(int portno, char *buf)
{
	char *p = buf;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	p += sprintf(p, "%s\n", onoff_string[din_port->counter_ctrl]);

	return p - buf;
}

static int get_counter_ctrl(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_counter_ctrl(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* hwcounter_ctrl */
static int set_hwcounter_ctrl(struct file *filep, const char __user *buf,
			      unsigned long count, void *data)
{
	int portno = (int) data, on, ret;
	char *_buf;
	unsigned long flags;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	din_port = &din_ports[portno];

	if (!strncmp(_buf, "on", count) || !strncmp(_buf, "1", count))
		on = 1;
	else if (!strncmp(_buf, "off", count) || !strncmp(_buf, "0", count))
		on = 0;
	else {
		printk(KERN_ERR "%s: value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EINVAL;
	}

	kfree(_buf);
	local_irq_save(flags);

	if (on == 1) {
		if (din_port->hwcounter_ctrl == 0) {
			din_port->hwcounter_ctrl = 1;
			dio_reg_set_bit(DIN_HWCOUNTER_SHIFT(portno),
					DIN_REG_HWCOUNTER_CTRL);
			dio_reg_set_bit(DIN_HWCOUNTER_SHIFT(portno),
					DIN_REG_MATCHIRQ_ENABLE);
		}
	} else {
		if (din_port->hwcounter_ctrl == 1) {
			din_port->hwcounter_ctrl = 0;
			dio_reg_clear_bit(DIN_HWCOUNTER_SHIFT(portno),
					  DIN_REG_MATCHIRQ_ENABLE);
			dio_reg_clear_bit(DIN_HWCOUNTER_SHIFT(portno),
					  DIN_REG_HWCOUNTER_CTRL);
		}
	}

	local_irq_restore(flags);

	return count;
}

static inline int __get_hwcounter_ctrl(int portno, char *buf)
{
	char *p = buf;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	p += sprintf(p, "%s\n", onoff_string[din_port->hwcounter_ctrl]);

	return p - buf;
}

static int get_hwcounter_ctrl(char *page, char **start, off_t off, int count,
			      int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_hwcounter_ctrl(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* polarity */
static int set_polarity(struct file *filep, const char __user *buf,
			unsigned long count, void *data)
{
	int portno = (int) data, polarity, ret;
	char *_buf;
	unsigned long flags;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	din_port = &din_ports[portno];

	if (din_port->enable_count > 0) {
		printk(KERN_ERR "%s: port %d already in use.\n", __FUNCTION__, portno);
		return -EALREADY;
	}

	if (!strncmp(_buf, polarity_string[1], count) || !strncmp(_buf, "1", count))
		polarity = 1;
	else if (!strncmp(_buf, polarity_string[0], count) || !strncmp(_buf, "0", count))
		polarity = 0;
	else {
		printk(KERN_ERR "%s: value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EINVAL;
	}

	kfree(_buf);

	local_irq_save(flags);

	if (polarity == 1)
		dio_reg_set_bit(DIN_REG_SHIFT(portno), DIN_REG_IRQ_POLARITY(portno));
	else
		dio_reg_clear_bit(DIN_REG_SHIFT(portno), DIN_REG_IRQ_POLARITY(portno));

	din_port->polarity = polarity;

	local_irq_restore(flags);

	return strlen(buf);
}

static inline int __get_polarity(int portno, char *buf)
{
	char *p = buf;
	struct magnolia2_din_port *din_port;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	din_port = &din_ports[portno];

	p += sprintf(p, "%s\n", polarity_string[din_port->polarity]);

	return p - buf;
}

static int get_polarity(char *page, char **start, off_t off, int count,
			int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_polarity(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* dout */
static int set_dout(struct file *filep, const char __user *buf,
		    unsigned long count, void *data)
{
	int portno = (int) data, on, ret;
	char *_buf;
	unsigned long flags;

	//printk("%s: portno = %d\n", __FUNCTION__, portno);

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	if (!strncmp(_buf, onoff_string[1], count) || !strncmp(_buf, "1", count))
		on = 1;
	else if (!strncmp(_buf, onoff_string[0], count) || !strncmp(_buf, "0", count))
		on = 0;
	else {
		printk(KERN_ERR "%s: value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EINVAL;
	}

	kfree(_buf);

	local_irq_save(flags);

	if (on == 1)
		dio_reg_set_bit(portno % 16, DOUT_REG_PORT_CTRL(portno));
	else
		dio_reg_clear_bit(portno % 16, DOUT_REG_PORT_CTRL(portno));

	local_irq_restore(flags);

	return count;
}

static inline int __get_dout(int portno, char *buf)
{
	char *p = buf;
	int val;

	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	val = dio_reg_get_bit(portno % 16, DOUT_REG_PORT_CTRL(portno));
	p += sprintf(p, "%d\n%s\n", val, onoff_string[val]);

	return p - buf;
}

static int get_dout(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	int portno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_dout(portno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* failout */
static int set_failout(struct file *filep, const char __user *buf,
		       unsigned long count, void *data)
{
	int on, ret;
	char *_buf;
	unsigned long flags;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	if (!strncmp(_buf, onoff_string[1], count) ||
	    !strncmp(_buf, failout_string[1], count) ||
	    !strncmp(_buf, "1", count))
		on = 1;
	else if (!strncmp(_buf, onoff_string[0], count) ||
		 !strncmp(_buf, failout_string[0], count) ||
		 !strncmp(_buf, "0", count))
		on = 0;
	else {
		printk(KERN_ERR "%s: value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EINVAL;
	}

	kfree(_buf);

	local_irq_save(flags);

	if (on == 1) {
		//dio_reg_set_bit(DIO_REG_FAILOUT_SHIFT, DIO_REG_BOARD_CTRL);
		__raw_writew(0xc000, iobase);
	} else {
		//dio_reg_clear_bit(DIO_REG_FAILOUT_SHIFT, DIO_REG_BOARD_CTRL);
		__raw_writew(0x2000, iobase);
	}

	local_irq_restore(flags);

	return count;
}

static inline int __get_failout(char *buf)
{
	char *p = buf;
	int val;

	val = dio_reg_get_bit(DIO_REG_FAILOUT_SHIFT, DIO_REG_BOARD_CTRL);
	//p += sprintf(p, "%d\n%s\n", val, onoff_string[val]);
	p += sprintf(p, "%d\n%s\n", val, failout_string[val]);

	return p - buf;
}

static int get_failout(char *page, char **start, off_t off, int count,
		       int *eof, void *data)
{
	int len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_failout(page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* filter */
static int set_filter(struct file *filep, const char __user *buf,
		      unsigned long count, void *data)
{
	int groupno = (int) data, filter = -1, i, ret;
	char *_buf, tmp[16];
	unsigned long flags;
	struct magnolia2_din_group *din_group;

	if (groupno < 0 || groupno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	din_group = &din_groups[groupno];

	for (i = 0; i < ARRAY_SIZE(filter_string); i++) {
		sprintf(tmp, "%d", i);

		if (!strncmp(_buf, filter_string[i], count) || !strncmp(_buf, tmp, count))
			filter = i;

		if (filter != -1)
			break;
	}

	if (filter == -1) {
		printk(KERN_ERR "%s: filter value error, '%s'\n", __FUNCTION__, _buf);
		kfree(_buf);
		return -EIO;
	}

	kfree(_buf);
	local_irq_save(flags);

	if (din_group->filter != filter) {
		dio_write_reg(filter << (groupno * 2), DIN_REG_FILTER);
		din_group->filter = filter;
	}

	local_irq_restore(flags);

	return count;
}

static inline int __get_filter(int groupno, char *buf)
{
	char *p = buf;
	struct magnolia2_din_group *din_group;

	if (groupno < 0 || groupno >= NUM_GROUP)
		return -EINVAL;

	din_group = &din_groups[groupno];

	p += sprintf(p, "%s\n", filter_string[din_group->filter]);

	return p - buf;
}

static int get_filter(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	int groupno = (int) data, len;
	unsigned long flags;

	local_irq_save(flags);
	len = __get_filter(groupno, page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* event */
static inline int din_asserted(int portno)
{
	u16 polarity, status, asserted;

	polarity = dio_read_reg(DIN_REG_IRQ_POLARITY(portno));
	status = dio_read_reg(DIN_REG_PORT_STATUS(portno));

	asserted = (((polarity ^ status) & (1 << (portno % 16))) != 0);

	DPRINTK("%s: polarity 0x%04x, status 0x%04x, asserted %d\n",
		__FUNCTION__, polarity, status, asserted);

	return asserted;
}

static int din_wait_event_timeout(struct file *filep, const char __user *buf,
				  unsigned long count, void *data)
{
	int portno, ret;
	u32 wait_ms;
	char *_buf;

	portno = (int) data;
	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	if ((ret = get_user_parameter(&_buf, buf, count)) < 0)
		return ret;

	if ((wait_ms = simple_strtoul(_buf, NULL, 10)) <= 0)
		return -EINVAL;

	DPRINTK("%s: port %d -> wait %d [ms], %d jiffies\n",
		__FUNCTION__, portno, wait_ms, wait_ms * HZ / 1000);
	enable_port_irq(portno);

	if (din_asserted(portno) == 0) {
		ret = wait_event_interruptible_timeout(din_ports[portno].wq,
						       (din_asserted(portno) != 0),
						       wait_ms * HZ / 1000);
		if (ret > 0)
			ret = count;
		else if (ret == 0) {
			//printk("%s: port %d timeouted...\n", __FUNCTION__, portno);
			ret = -ETIMEDOUT;
		}
	} else {
		//printk("port %d already asserted.\n", portno);
		ret = count;
	}

	disable_port_irq(portno);

	return ret;
}

static int din_wait_event(char *page, char **start, off_t off, int count,
			  int *eof, void *data)
{
	int portno, len;
	struct timespec ts[2];
	unsigned long elapsed;

	portno = (int) data;
	if (portno < 0 || portno >= NUM_PORTS)
		return -EINVAL;

	enable_port_irq(portno);

	if (din_asserted(portno) == 0) {
		ts[0] = current_kernel_time();

		if (wait_event_interruptible(din_ports[portno].wq,
					     din_asserted(portno) != 0))
			len = -ERESTARTSYS;
		else {
			ts[1] = current_kernel_time();

			elapsed = (ts[1].tv_sec - ts[0].tv_sec) * 1000 +
				(ts[1].tv_nsec - ts[0].tv_nsec) / 1000000;
			len = sprintf(page, "%lu\n%lu.%09lu\n", elapsed,
				      ts[1].tv_sec, ts[1].tv_nsec);
		}
	} else {
		ts[1] = current_kernel_time();
		len = sprintf(page, "0\n%lu.%09lu\n",
			      ts[1].tv_sec, ts[1].tv_nsec);
	}

	if (len > 0) {
		*eof = 1;

		*start = page + off;
		len -= off;

		if (len > count)
			len = count;
		if (len < 0)
			len = 0;
	}

	disable_port_irq(portno);

	return len;
}

/* din value */
static int __din_val_read_proc(char *buf, int sel)
{
	int i;
	char *p = buf;
	u16 data;

	data = dio_read_reg(DIN_REG_PORT_STATUS((sel == 0 ? 0 : 31)));

	for (i = 15; i >= 0; i--)
		*p++ = (char) ('0' + ((data & (1 << i)) != 0 ? 1 : 0));

	*p++ = '\n';

	return p - buf;
}

static int din_val_read_proc(char *page, char **start, off_t off, int count,
			     int *eof, void *data)
{
	int len, sel;
	unsigned long flags;

	sel = (int) data;

	local_irq_save(flags);
	len = __din_val_read_proc(page, sel);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

/* din all */
static int __din_all_read_proc(char *buf)
{
	int i;
	char *p = buf;
	u16 data;

	data = dio_read_reg(DIN_REG_PORT_STATUS(0));
	p += sprintf(p, " Pri port status:  0x%04x\n", data);
	data = dio_read_reg(DIN_REG_PORT_STATUS(31));
	p += sprintf(p, " Sec port status:  0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_STATUS(0));
	p += sprintf(p, " Pri IRQ status:   0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_STATUS(31));
	p += sprintf(p, " Sec IRQ status:   0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_ENABLE(0));
	p += sprintf(p, " Pri IRQ enable:   0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_ENABLE(31));
	p += sprintf(p, " Sec IRQ enable:   0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_POLARITY(0));
	p += sprintf(p, " Pri IRQ polarity: 0x%04x\n", data);
	data = dio_read_reg(DIN_REG_IRQ_POLARITY(31));
	p += sprintf(p, " Sec IRQ polarity: 0x%04x\n", data);

	for (i = 0; i < NUM_PORTS; i++) {
		p += sprintf(p, " port[%2d]: %10d (0x%08x) (%s) (%d)\n",
			     i, din_ports[i].counter_val,
			     din_ports[i].counter_val,
			     onoff_string[din_ports[i].counter_ctrl],
			     din_ports[i].enable_count);
	}

	return p - buf;
}

static int din_all_read_proc(char *page, char **start, off_t off, int count,
			     int *eof, void *data)
{
	int len;
	unsigned long flags;

	local_irq_save(flags);
	len = __din_all_read_proc(page);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}


static int magnolia2_create_proc_entries(void)
{
	int i, j, portno;
	char buf[32], dest[32];
	struct proc_dir_entry *dir_grp, *dir_port, *ent;

	/* driver/din */
	dir_din = proc_mkdir(DIN_DIR, NULL);
	if (!dir_din)
		return -ENOMEM;

	/* driver/din/group */
	for (i = 0; i < NUM_GROUP; i++) {
		dir_grp = proc_mkdir(din_groups[i].name, dir_din);
		ent = create_proc_entry(DIN_ENTRY_FILTER,
					S_IFREG|0644, dir_grp);

		/* din/group/filter */
		if (ent) {
			ent->write_proc = set_filter;
			ent->read_proc = get_filter;
			ent->data = (void *) i;
		}

		din_groups[i].entry = dir_grp;

		for (j = 0; j < PORT_PER_GROUP; j++) {
			portno = i * PORT_PER_GROUP + j;
			dir_port = proc_mkdir(din_ports[portno].name, dir_grp);

			din_ports[portno].entry = dir_port;

			/* din/group/port/counter_val */
			ent = create_proc_entry(DIN_ENTRY_COUNTER_VAL,
						S_IFREG|0644, dir_port);
			if (ent) {
				ent->write_proc = clear_counter_val;
				ent->read_proc = get_counter_val;
				ent->data = (void *) portno;
			}

			/* din/group/port/counter_val_diff */
			ent = create_proc_entry(DIN_ENTRY_COUNTER_VAL_DIFF,
						S_IFREG|0400, dir_port);
			if (ent) {
				ent->read_proc = get_counter_val_diff;
				ent->data = (void *) portno;
			}

			/* din/group/port/counter_ctrl */
			ent = create_proc_entry(DIN_ENTRY_COUNTER_CTRL,
						S_IFREG|0644, dir_port);

			if (ent) {
				ent->write_proc = set_counter_ctrl;
				ent->read_proc = get_counter_ctrl;
				ent->data = (void *) portno;
			}

			/* din/group/port/polarity */
			ent = create_proc_entry(DIN_ENTRY_POLARITY,
						S_IFREG|0644, dir_port);

			if (ent) {
				ent->write_proc = set_polarity;
				ent->read_proc = get_polarity;
				ent->data = (void *) portno;
			}

			/* din/group/port/event */
			ent = create_proc_entry(DIN_ENTRY_EVENT,
						S_IFREG|0666, dir_port);

			if (ent) {
				ent->write_proc = din_wait_event_timeout;
				ent->read_proc = din_wait_event;
				ent->data = (void *) portno;
			}

			/* din/group/port/status */
			ent = create_proc_entry(DIN_ENTRY_STATUS,
						S_IFREG|0444, dir_port);

			if (ent) {
				ent->read_proc = get_port_status;
				ent->data = (void *) portno;
			}

			/* din/port (symlink) */
			sprintf(dest, "%s/%s", din_groups[i].name, din_ports[portno].name);
			proc_symlink(din_ports[portno].name, dir_din, dest);

			if (din_groups[i].hw_counter) {
				/* din/group/port/hwcounter_val */
				ent = create_proc_entry(DIN_ENTRY_HWCOUNTER_VAL,
							S_IFREG|0644, dir_port);

				if (ent) {
					ent->write_proc = set_hwcounter_val;
					ent->read_proc = get_hwcounter_val;
					ent->data = (void *) portno;
				}

				/* din/group/port/hwcounter_ctrl */
				ent = create_proc_entry(DIN_ENTRY_HWCOUNTER_CTRL,
							S_IFREG|0644, dir_port);

				if (ent) {
					ent->write_proc = set_hwcounter_ctrl;
					ent->read_proc = get_hwcounter_ctrl;
					ent->data = (void *) portno;
				}
			}
		}
	}

	/* driver/dout */
	dir_dout = proc_mkdir(DOUT_DIR, NULL);
	if (!dir_dout) {
		magnolia2_remove_proc_entries();
		return -ENOMEM;
	}

	for (i = 0; i < NUM_OUT_PORTS; i++) {
		sprintf(buf, DOUT_ENTRY_PORT, i);
		ent = create_proc_entry(buf, S_IFREG|0644, dir_dout);

		if (ent) {
			ent->write_proc = set_dout;
			ent->read_proc = get_dout;
			ent->data = (void *) i;
		}
	}

	/* failout */
	ent = create_proc_entry(FAILOUT_ENTRY, S_IFREG|0644, dir_dout);

	if (ent) {
		ent->write_proc = set_failout;
		ent->read_proc = get_failout;
	}

	create_proc_read_entry(DIN_ENTRY_VAL_PRIMARY, 0, 0,
			       din_val_read_proc, (void *) 0);
	create_proc_read_entry(DIN_ENTRY_VAL_SECONDARY, 0, 0,
			       din_val_read_proc, (void *) 1);

	/* for debug */
	create_proc_read_entry(DIN_ENTRY_ALL, 0, 0, din_all_read_proc, NULL);

	return 0;
}

static void magnolia2_remove_proc_entries(void)
{
	int i, j, portno;
	struct proc_dir_entry *dir_grp, *dir_port;
	char buf[32];

	for (i = 0; i < NUM_GROUP; i++) {
		dir_grp = din_groups[i].entry;

		if (dir_grp) {
			for (j = 0; j < PORT_PER_GROUP; j++) {
				portno = i * PORT_PER_GROUP + j;

				dir_port = din_ports[portno].entry;

				if (dir_port) {
					remove_proc_entry(DIN_ENTRY_COUNTER_VAL,
							  dir_port);
					remove_proc_entry(DIN_ENTRY_COUNTER_VAL_DIFF,
							  dir_port);
					remove_proc_entry(DIN_ENTRY_COUNTER_CTRL,
							  dir_port);
					remove_proc_entry(DIN_ENTRY_POLARITY,
							  dir_port);
					remove_proc_entry(DIN_ENTRY_EVENT,
							  dir_port);
					remove_proc_entry(DIN_ENTRY_STATUS,
							  dir_port);

					if (din_groups[i].hw_counter) {
						remove_proc_entry(DIN_ENTRY_HWCOUNTER_VAL,
								  dir_port);
						remove_proc_entry(DIN_ENTRY_HWCOUNTER_CTRL,
								  dir_port);
					}
				}

				/* remove symlink */
				remove_proc_entry(din_ports[portno].name, dir_grp);

				remove_proc_entry(din_ports[portno].name, dir_din);
			}

			remove_proc_entry(DIN_ENTRY_FILTER, dir_grp);
		}

		remove_proc_entry(din_groups[i].name, dir_din);
	}

	remove_proc_entry(DIN_ENTRY_VAL_SECONDARY, NULL);
	remove_proc_entry(DIN_ENTRY_VAL_PRIMARY, NULL);

	remove_proc_entry(DIN_ENTRY_ALL, NULL);
	remove_proc_entry(DIN_DIR, NULL);
	dir_din = NULL;

	if (dir_dout) {
		for (i = 0; i < NUM_OUT_PORTS; i++) {
			sprintf(buf, DOUT_ENTRY_PORT, i);
			remove_proc_entry(buf, dir_dout);
		}

		remove_proc_entry(FAILOUT_ENTRY, dir_dout);

		remove_proc_entry(DOUT_DIR, NULL);
		dir_dout = NULL;
	}
}

static void clear_all_ctrl_regs(int enable)
{
#if 0
	int i;
	u16 val;

	dio_write_reg(0, DIN_REG_IRQ_ENABLE(0));
	dio_write_reg(0, DIN_REG_IRQ_ENABLE(31));
	dio_write_reg(0, DIN_REG_IRQ_POLARITY(0));
	dio_write_reg(0, DIN_REG_IRQ_POLARITY(31));
	dio_write_reg(0, DIN_REG_FILTER);
	dio_write_reg(0, DIN_REG_HWCOUNTER_CTRL);
	dio_write_reg(0, DIN_REG_MATCHIRQ_ENABLE);

	dio_write_reg(0xffff, DIN_REG_IRQ_STATUS(0));
	dio_write_reg(0xffff, DIN_REG_IRQ_STATUS(31));
	dio_write_reg(0xffff, DIN_REG_MATCHIRQ_STATUS);
	dio_write_reg(0xffff, DIN_REG_OVERFLOW);

	for (i = 0; i < 8; i++) {
		dio_write_reg(0, DIN_REG_HWCOUNTER(i));
		dio_write_reg(0xffff, DIN_REG_MATCH_REG(i));
	}

	for (i = 16; i < 24; i++) {
		dio_write_reg(0, DIN_REG_HWCOUNTER(i));
		dio_write_reg(0xffff, DIN_REG_MATCH_REG(i));
	}

	dio_write_reg(0, DOUT_REG_PORT_CTRL(0));
	dio_write_reg(0, DOUT_REG_PORT_CTRL(31));
#else
	u16 val;

	dio_write_reg(0x0001, DIO_REG_BOARD_CTRL);
#endif

	if (enable)
		val = 0x2000;
	else
		val = 0xc000;

	__raw_writew(val, iobase);
}

static int dio_halt(struct notifier_block *nb, unsigned long event, void *buf);
static int notifier_disabled = 0;

static struct notifier_block dio_notifier = {
	.notifier_call = dio_halt,
};

static int dio_halt(struct notifier_block *nb, unsigned long event, void *buf)
{
	if (notifier_disabled)
		return NOTIFY_OK;

	notifier_disabled = 1;

	clear_all_ctrl_regs(0);

	return NOTIFY_OK;
}

static int magnolia2_extio_probe(struct platform_device *pdev)
{
	struct resource *res;
	int len, ret, irq;

	printk("Magnolia2 AI/DIO Ext-IO driver (DIO)\n");

	init_ports();

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
	printk(" ioaddr: 0x%08x -> 0x%08lx (mapped)\n", res->start, iobase);

	ret = request_irq(irq, magnolia2_din_irq, IRQF_TRIGGER_LOW,
			  pdev->name, NULL);
	if (ret) {
		printk(KERN_ERR "request_irq() failed with %d\n", ret);
		goto err2;
	}

	if (magnolia2_create_proc_entries() < 0) {
		ret = -EFAULT;
		goto err3;
	}

	clear_all_ctrl_regs(1);
	register_reboot_notifier(&dio_notifier);

	return 0;

err3:
	free_irq(irq, NULL);
err2:
	iounmap((void *) iobase);
	release_mem_region(res->start, res->end - res->start + 1);
err1:
	return ret;
}

static int magnolia2_extio_remove(struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	unregister_reboot_notifier(&dio_notifier);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (iobase) {
		clear_all_ctrl_regs(0);

		magnolia2_remove_proc_entries();
		free_irq(irq, NULL);
		iounmap((void *) iobase);

		release_mem_region(res->start, res->end - res->start + 1);
	}

	return 0;
}

static struct platform_driver magnolia2_extio_driver = {
	.driver = {
		.name	= "magnolia2_DIO",
		.owner	= THIS_MODULE,
	},
	.probe		= magnolia2_extio_probe,
	.remove		= magnolia2_extio_remove,
};

static int __init magnolia2_extio_init(void)
{
	return platform_driver_register(&magnolia2_extio_driver);
}
module_init(magnolia2_extio_init);

static void magnolia2_extio_exit(void)
{
	platform_driver_unregister(&magnolia2_extio_driver);
}
module_exit(magnolia2_extio_exit);

MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Magnolia2 Ext-IO GPIO");
