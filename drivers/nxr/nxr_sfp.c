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
#include <linux/of_gpio.h>
#include <linux/nxr/nxr_misc.h>
#include <linux/nxr/nxr_debug.h>

#define MAX_BUF_SIZE	256
#define SFP_ADDR_SIZE	1

#define SFP_VENDOR_ADDR   20

struct sfp_priv {
	struct i2c_client * client;
	struct mutex lock;
	struct proc_dir_entry * proc_entry;
	struct proc_dir_entry * proc_type;
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
sfp_type_proc_show(struct seq_file * seq, void * unused)
{
	struct sfp_priv * priv = seq->private;
	struct i2c_client * client = priv->client;
	u8 buf[MAX_BUF_SIZE];
	int ret;

	ret = get_sfp_data(client, buf, MAX_BUF_SIZE, 0);

	if (ret < 0)
		seq_printf(seq, "none\n");
	else if (!strncmp("NTT", &buf[SFP_VENDOR_ADDR], 3))
		seq_printf(seq, "onu\n");
	else
		seq_printf(seq, "sfp\n");

	return 0;
}

static int
sfp_type_proc_open(struct inode * inode, struct file * file)
{
	return single_open(file, sfp_type_proc_show, PDE_DATA(inode));
}

static const struct file_operations sfp_type_proc_fops = {
	.owner = THIS_MODULE,
	.open = sfp_type_proc_open,
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

	if (!strncmp("sfp0-a0",client->name, 7))  {
		entry = proc_create_data("type", 0400, sfp_proc_root, &sfp_type_proc_fops, priv);
		if (!entry) {
			dev_err(&client->dev, "%s : proc_create failed\n", __func__);
			return -ENOMEM;
		}
		priv->proc_type = entry;
	}
	return 0;
}

static void
remove_proc(struct sfp_priv * priv)
{
	if (priv->proc_entry)
		proc_remove(priv->proc_entry);

	if (priv->proc_type)
		proc_remove(priv->proc_type);
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
	},
	.probe		= sfp_probe,
	.remove		= sfp_remove,
	.id_table	= sfp_id,
};

struct sfp_event {
	int sfp_moddef_irq;
	int sfp_moddef_gpio;
	int sfp_tx_disable;
	spinlock_t lock;
};

enum {
	SFP_MOD_PRESENT = 0,
	SFP_MOD_NOT_PRESENT,
};

struct sfp_event * g_event = NULL;

int
nxr_sfp_tx_disable(void)
{
	if (!gpio_is_valid(g_event->sfp_tx_disable))
		return -EIO;

	gpio_direction_output(g_event->sfp_tx_disable, 1);

	return 0;	
}
EXPORT_SYMBOL(nxr_sfp_tx_disable);

int
nxr_sfp_tx_enable(void)
{
	if (!g_event)
		return -EIO;

	if (!gpio_is_valid(g_event->sfp_tx_disable))
		return -EIO;

	gpio_direction_output(g_event->sfp_tx_disable, 0);

	return 0;
}
EXPORT_SYMBOL(nxr_sfp_tx_enable);

static irqreturn_t
sfp_moddef_interrupt(int irq, void * data)
{
	struct sfp_event * event = data;
	int status;

	status = gpio_get_value_cansleep(event->sfp_moddef_gpio);
	if (status == SFP_MOD_PRESENT) {
		nxr_sfp_klogd(" Attaching SFP0\n");
	} else {
		nxr_sfp_klogd(" Detaching SFP0\n");
	}
	
	return IRQ_HANDLED;
}

static int
event_of_init(const char * node, struct sfp_event * event)
{
	struct device_node * np = NULL;
	int gpio, irq;
	enum of_gpio_flags flags;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, node);
	if (!np) {
		err("could not find a %s node\n", node);
		return -ENODEV;
	}

	gpio = of_get_named_gpio_flags(np, "sfp-tx-disable", 0, &flags);
	if (gpio_is_valid(gpio) == 0) {
		info("Can't get %s property '%s'\n", np->full_name, "sfp-tx-disable");
		goto fail1;
	}

	ret = gpio_request(gpio, "sfp_tx_disable");
	if (ret < 0) {
		info("Failed to request GPIO %d, error %d\n", gpio, ret);
		goto fail1;
	}
	event->sfp_tx_disable = gpio;

	gpio = of_get_named_gpio_flags(np, "sfp-moddef", 0, &flags);
	if (gpio_is_valid(gpio) == 0) {
		info("Can't get %s property '%s'\n", np->full_name, "sfp-moddef");
		gpio_free(event->sfp_tx_disable);
		goto fail2;
	}

	ret = gpio_request(gpio, "sfp_moddef");
	if (ret < 0) {
		info("Failed to request GPIO %d, error %d\n", gpio, ret);
		goto fail2;
	}
	event->sfp_moddef_gpio = gpio;

	irq = gpio_to_irq(gpio);
	if (irq < 0) {
		info("Can't get irq property\n");
		goto fail3;
	}
	event->sfp_moddef_irq = irq;

	return 0;
fail3:
	gpio_free(event->sfp_moddef_gpio);
fail2:
	gpio_free(event->sfp_tx_disable);
fail1:

	return -EINVAL;
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

	g_event = event;

	ret = event_of_init("nxr,sfp0", event);
	if (ret < 0)
		goto fail;

	ret = request_any_context_irq(event->sfp_moddef_irq, sfp_moddef_interrupt,
			IRQF_SHARED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"sfp0-moddef", event);
	if (ret < 0) {
		err("failed to install irq (%d)\n", event->sfp_moddef_irq);
		goto fail;
	}
	return 0;
fail:
	kfree(g_event);

	return ret;
}

static void
event_exit(void)
{
	if (g_event) {
		if (gpio_is_valid(g_event->sfp_tx_disable))
			gpio_free(g_event->sfp_tx_disable);

		if (gpio_is_valid(g_event->sfp_moddef_gpio)) {
			free_irq(g_event->sfp_moddef_irq, NULL);
			gpio_free(g_event->sfp_moddef_gpio);
		}

		kfree(g_event);
	}
}

static int __init
sfp_init(void)
{
	int ret;

	printk("NXR SFP/SFP+ driver %s\n", version);

	ret = i2c_add_driver(&sfp_driver);
	if (ret)
		return ret;

	ret = event_init();
	if (ret)
		return ret;

	return 0;
}

static void __exit
sfp_exit(void)
{
	i2c_del_driver(&sfp_driver);
	event_exit();
}

module_init(sfp_init);
module_exit(sfp_exit);
