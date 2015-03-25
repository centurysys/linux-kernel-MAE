/*
 * NXR SFP/SFP+ driver
 *
 * Copyright (C) 2015 Century Systems
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/nxr/nxr_misc.h>
#include <linux/nxr/nxr_debug.h>

#define MAX_BUF_SIZE	256
#define SFP_ADDR_SIZE	1

struct sfp_priv {
	struct i2c_client * client;
	struct mutex lock;
	struct proc_dir_entry * proc_entry;
};

static const char * version = "0.1";
struct proc_dir_entry * sfp_proc_root = NULL;

static int
get_sfp_data(struct i2c_client * client, unsigned char * buf, int len, int offset)
{
	struct sfp_priv * priv = i2c_get_clientdata(client);
	int ret;
	char addr[] = {offset & 0xff};
	struct i2c_msg msg[] = {
		{.addr = client->addr, .flags = 0, .buf = addr, .len = SFP_ADDR_SIZE},
		{.addr = client->addr, .flags = I2C_M_RD, .buf = buf, .len = len}
	};

	if (!client) {
		dev_warn(&client->dev, "%s: chip not found.\n", __func__);
		return -ENODEV;
	}

	if (!len)
		return 0;

	memset(buf, 0 , len);
	mutex_lock(&priv->lock);
	ret = i2c_transfer(client->adapter, msg, 2);
	mutex_unlock(&priv->lock);
	if (ret != 2) {
		dev_err(&client->dev, "%s: i2c read error (addr %02x, ret == %i)\n", __func__, client->addr, ret);
		return -EIO;
	}

	return 0;
}

static int
sfp_proc_show(struct seq_file * seq, void * unused)
{
	struct sfp_priv * priv = seq->private;
	struct i2c_client * client = priv->client;
	u8 buf[MAX_BUF_SIZE];
	int ret;

	ret = get_sfp_data(client, buf, MAX_BUF_SIZE, 0);
	if (ret < 0) {
		seq_printf(seq, "non-SFP(Address 0x%02x) connections\n", (client->addr << 1));
		return 0;
	}

	seq_printf(seq, "### SFP Memory(Address 0x%02x) ###\n", (client->addr << 1));
	nxr_proc_print_hex_dump(seq, "", DUMP_PREFIX_OFFSET, 16, 4, buf, MAX_BUF_SIZE, false);

	return 0;
}

static int
sfp_proc_open(struct inode * inode, struct file * file)
{
	return single_open(file, sfp_proc_show, PDE_DATA(inode));
}

static const struct file_operations sfp_proc_fops = {
	.owner = THIS_MODULE,
	.open = sfp_proc_open,
	.read = seq_read,
	.llseek = noop_llseek,
};

static int
init_proc(struct sfp_priv * priv)
{
	struct i2c_client * client = priv->client;
	struct proc_dir_entry * entry;

	if (!sfp_proc_root) {
		sfp_proc_root = proc_mkdir("driver/sfp", NULL);
	}

	entry = proc_create_data(client->name, 0400, sfp_proc_root, &sfp_proc_fops, priv);
	if (!entry) {
		dev_err(&client->dev, "%s : proc_create failed\n", __func__);
		return -ENOMEM;
	}
	priv->proc_entry = entry;

	return 0;
}

static void
remove_proc(struct sfp_priv * priv)
{
	if (priv->proc_entry)
		proc_remove(priv->proc_entry);
}

static int
sfp_probe(struct i2c_client * client, const struct i2c_device_id * id)
{
	struct i2c_adapter * adapter = client->adapter;
	struct sfp_priv * priv;
	int ret;

	ret = i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_WRITE_BYTE);
	if(!ret)
		return -EIO;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);
	priv->client = client;
	mutex_init(&priv->lock);

	return init_proc(priv);
}

static int
sfp_remove(struct i2c_client * client)
{
	struct sfp_priv * priv = i2c_get_clientdata(client);

	remove_proc(priv);

	i2c_unregister_device(priv->client);

	return 0;
}

#if 0
static const struct of_device_id sf_of_id_table[] = {
	{.compatible = "sfp0-a0"},
	{.compatible = "sfp0-a2"},
	{.compatible = "sfp1-a0"},
	{.compatible = "sfp1-a2"},
	{.compatible = "sfp2-a0"},
	{.compatible = "sfp2-a2"},
	{.compatible = "sfp3-a0"},
	{.compatible = "sfp3-a2"},
	{},
};
#endif

static const struct i2c_device_id sfp_id[] = {
	{"sfp0-a0", 0},
	{"sfp0-a2", 0},
	{"sfp1-a0", 0},
	{"sfp1-a2", 0},
	{"sfp2-a0", 0},
	{"sfp2-a2", 0},
	{"sfp3-a0", 0},
	{"sfp3-a2", 0},
	{},
};

static struct i2c_driver sfp_driver = {
	.driver = {
		.name		= "sfp",
		.owner		= THIS_MODULE,
		//.of_match_table	= of_match_ptr(sf_of_id_table),
	},
	.probe		= sfp_probe,
	.remove		= sfp_remove,
	.id_table	= sfp_id,
};

#ifdef CONFIG_NXR1300
#include "../arch/powerpc/sysdev/nxr1300/nxr1300.h"

#define SFP_PLUS1_MOD_OUT_IRQ	(0x80000000 >> 12)
#define SFP_PLUS1_MOD_IN_IRQ	(0x80000000 >> 13)
#define SFP_PLUS0_MOD_OUT_IRQ	(0x80000000 >> 14)
#define SFP_PLUS0_MOD_IN_IRQ	(0x80000000 >> 15)
#define SFP_PLUSx_MOD_MASK	(SFP_PLUS1_MOD_OUT_IRQ | SFP_PLUS1_MOD_IN_IRQ | SFP_PLUS0_MOD_OUT_IRQ | SFP_PLUS0_MOD_IN_IRQ)

#define SFP1_MOD_OUT_IRQ	(0x80000000 >> 20)
#define SFP1_MOD_IN_IRQ		(0x80000000 >> 21)
#define SFP0_MOD_OUT_IRQ	(0x80000000 >> 22)
#define SFP0_MOD_IN_IRQ		(0x80000000 >> 23)
#define SFPx_MOD_MASK		(SFP1_MOD_OUT_IRQ | SFP1_MOD_IN_IRQ | SFP0_MOD_OUT_IRQ | SFP0_MOD_IN_IRQ)

struct sfp_event {
	int sfp_1g_irq;
	int sfp_10g_irq;
	spinlock_t lock;
};

struct sfp_event * g_event = NULL;

static u32
sfp_get_status(struct sfp_event * event)
{
	u32 status = 0;
	unsigned long flag;

	spin_lock_irqsave(&event->lock, flag);
	nxr1300_cpld_int_ctrl_stat1_get(&status);
	spin_unlock_irqrestore(&event->lock, flag);
#if 0
	printk("INTCTLSTA1: %08x\n", status);
#endif

	return status;
}

static void
sfp_10g_disable_irq(void)
{
	nxr1300_cpld_int_input_mask1_clr(SFP_PLUSx_MOD_MASK);
}

static void
sfp_10g_enable_irq(void)
{
	nxr1300_cpld_int_input_mask1_set(SFP_PLUSx_MOD_MASK);
}

static void
sfp_10g_clr_irq(void)
{
	nxr1300_cpld_int_ctrl_stat1_set(SFP_PLUSx_MOD_MASK);
}

static irqreturn_t
sfp_10g_interrupt(int irq, void * data)
{
	struct sfp_event * event = data;
	u32 status;

	status = sfp_get_status(event);
	if (!(status & SFP_PLUSx_MOD_MASK))
		return IRQ_NONE;

	if (status & SFP_PLUS1_MOD_OUT_IRQ)
		nxr_sfp_klogd(" Detaching SFP_PLUS1\n");
	if (status & SFP_PLUS1_MOD_IN_IRQ)
		nxr_sfp_klogd(" Attaching SFP_PLUS1\n");
	if (status & SFP_PLUS0_MOD_OUT_IRQ)
		nxr_sfp_klogd(" Detaching SFP_PLUS0\n");
	if (status & SFP_PLUS0_MOD_IN_IRQ)
		nxr_sfp_klogd(" Attaching SFP_PLUS0\n");

	sfp_10g_clr_irq();

	return IRQ_HANDLED;
}

static void
sfp_1g_disable_irq(void)
{
	nxr1300_cpld_int_input_mask1_clr(SFPx_MOD_MASK);
}

static void
sfp_1g_enable_irq(void)
{
	nxr1300_cpld_int_input_mask1_set(SFPx_MOD_MASK);
}

static void
sfp_1g_clr_irq(void)
{
	nxr1300_cpld_int_ctrl_stat1_set(SFPx_MOD_MASK);
}

static irqreturn_t
sfp_1g_interrupt(int irq, void * data)
{
	struct sfp_event * event = data;
	u32 status;

	status = sfp_get_status(event);
	if (!(status & SFPx_MOD_MASK))
		return IRQ_NONE;

	if (status & SFP1_MOD_OUT_IRQ)
		nxr_sfp_klogd(" Detaching SFP1\n");
	if (status & SFP1_MOD_IN_IRQ)
		nxr_sfp_klogd(" Attaching SFP1\n");
	if (status & SFP0_MOD_OUT_IRQ)
		nxr_sfp_klogd(" Detaching SFP0\n");
	if (status & SFP0_MOD_IN_IRQ)
		nxr_sfp_klogd(" Attaching SFP0\n");
	
	sfp_1g_clr_irq();

	return IRQ_HANDLED;
}

static int
get_irq(const char * node)
{
	struct device_node * np = NULL;
	int ret;

	np = of_find_compatible_node(NULL, NULL, node);
	if (!np) {
		err("could not find a %s node\n", node);
		return -ENODEV;
	}

	ret = of_irq_to_resource(np, 0, NULL);
	if (ret == NO_IRQ) {
		info("Can't get %s property '%s'\n", np->full_name, "interrupts");
		return -EINVAL;
	}

	return ret;
}

static int
event_init(void)
{
	struct sfp_event * event;
	int ret;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	spin_lock_init(&event->lock);

	/* 10G */
	ret = get_irq("nxr,sfp0");
	if (ret < 0)
		return ret;

	event->sfp_10g_irq = ret;

	ret = request_irq(event->sfp_10g_irq, sfp_10g_interrupt, IRQF_SHARED, "sfp-10g", event);
	if (ret) {
		err("failed to install irq (%d)\n", event->sfp_10g_irq);
		return ret;
	}

	/* 1G */
	ret = get_irq("nxr,sfp2");
	if (ret < 0) {
		free_irq(event->sfp_10g_irq, NULL);
		return ret;
	}

	event->sfp_1g_irq = ret;

	ret = request_irq(event->sfp_1g_irq, sfp_1g_interrupt, IRQF_SHARED, "sfp-1g", event);
	if (ret) {
		err("failed to install irq (%d)\n", event->sfp_1g_irq);
		free_irq(event->sfp_10g_irq, NULL);
		return ret;
	}

	nxr1300_cpld_int_input_mask1_clr(SFP_PLUSx_MOD_MASK | SFPx_MOD_MASK);
	nxr1300_cpld_int_output_mask1_set(SFP_PLUSx_MOD_MASK | SFPx_MOD_MASK);

	g_event = event;

	sfp_10g_enable_irq();
	sfp_1g_enable_irq();

	return 0;
}

static void
event_exit(void)
{
	if (g_event) {
		free_irq(g_event->sfp_10g_irq, NULL);
		free_irq(g_event->sfp_1g_irq, NULL);
		kfree(g_event);
	}
}

#endif

static int __init
sfp_init(void)
{
	int ret;

	printk("NXR SFP/SFP+ driver %s\n", version);

	ret = i2c_add_driver(&sfp_driver);
	if (ret)
		return ret;

#ifdef CONFIG_NXR1300
	ret = event_init();
	if (ret)
		return ret;
#endif

	return 0;
}

static void __exit
sfp_exit(void)
{
	i2c_del_driver(&sfp_driver);
#ifdef CONFIG_NXR1300
	event_exit();
#endif
}

module_init(sfp_init);
module_exit(sfp_exit);
