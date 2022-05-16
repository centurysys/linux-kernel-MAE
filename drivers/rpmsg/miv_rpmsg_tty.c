// SPDX-License-Identifier: GPL-2.0
/*
 * Remote processor messaging transport - tty driver
 *
 * Copyright (c) 2021 - 2022 Microchip Technology Inc. All rights reserved.
 *
 * Author: Valentina Fernandez <valentina.fernandezalanis@microchip.com>
 *
 * Derived from the imx_rpmsg implementation:
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 */

#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/rpmsg.h>
#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/module.h>
#include <linux/tty_flip.h>
#include <linux/tty_driver.h>

/* this needs to be less then (RPMSG_BUF_SIZE - sizeof(struct rpmsg_hdr)) */
#define RPMSG_MAX_SIZE		256

struct rpmsgtty_port {
	struct rpmsg_device *rpdev;
	struct tty_driver *rpmsgtty_driver;
	struct tty_port port;
	spinlock_t rx_lock; /* port lock */
};

static int rpmsg_tty_callback(struct rpmsg_device *rpdev, void *data, int len,
			      void *priv, u32 src)
{
	struct rpmsgtty_port *cport = dev_get_drvdata(&rpdev->dev);
	unsigned char *cbuf;
	int space;

	/* flush the recv-ed none-zero data to tty node */
	if (len == 0)
		return 0;

	dev_dbg(&rpdev->dev, "msg(<- src 0x%x) len %d\n", src, len);

	spin_lock_bh(&cport->rx_lock);
	space = tty_prepare_flip_string(&cport->port, &cbuf, len);
	if (space <= 0) {
		dev_err(&rpdev->dev, "No memory for tty_prepare_flip_string\n");
		spin_unlock_bh(&cport->rx_lock);
		return -ENOMEM;
	}

	memcpy(cbuf, data, len);
	tty_flip_buffer_push(&cport->port);
	spin_unlock_bh(&cport->rx_lock);

	return 0;
}

static struct tty_port_operations  rpmsgtty_port_ops = { };

static int rpmsgtty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct rpmsgtty_port *cport = driver->driver_state;

	return tty_port_install(&cport->port, driver, tty);
}

static int rpmsgtty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void rpmsgtty_close(struct tty_struct *tty, struct file *filp)
{
	return tty_port_close(tty->port, tty, filp);
}

static int rpmsgtty_write(struct tty_struct *tty, const unsigned char *buf,
			  int total)
{
	struct rpmsgtty_port *rptty_port = container_of(tty->port, struct rpmsgtty_port, port);
	struct rpmsg_device *rpdev = rptty_port->rpdev;
	const unsigned char *tbuf;
	int count, ret = 0;

	if (!buf) {
		dev_err(&rpdev->dev, "buf shouldn't be null.\n");
		return -ENOMEM;
	}

	count = total;
	tbuf = buf;
	do {
		ret = rpmsg_send(rpdev->ept, (void *)tbuf,
				 count > RPMSG_MAX_SIZE ? RPMSG_MAX_SIZE : count);
		if (ret) {
			dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
			return ret;
		}

		if (count > RPMSG_MAX_SIZE) {
			count -= RPMSG_MAX_SIZE;
			tbuf += RPMSG_MAX_SIZE;
		} else {
			count = 0;
		}
	} while (count > 0);

	return total;
}

static unsigned int rpmsgtty_write_room(struct tty_struct *tty)
{
	return RPMSG_MAX_SIZE;
}

static const struct tty_operations rpmsgtty_ops = {
	.install = rpmsgtty_install,
	.open = rpmsgtty_open,
	.close = rpmsgtty_close,
	.write = rpmsgtty_write,
	.write_room = rpmsgtty_write_room,
};

static int rpmsg_tty_probe(struct rpmsg_device *rpdev)
{
	struct rpmsgtty_port *cport;
	struct tty_driver *rpmsgtty_driver;
	int ret;

	cport = devm_kzalloc(&rpdev->dev, sizeof(*cport), GFP_KERNEL);
	if (!cport)
		return -ENOMEM;

	rpmsgtty_driver = tty_alloc_driver(1, TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(rpmsgtty_driver))
		return dev_err_probe(&rpdev->dev, PTR_ERR(rpmsgtty_driver),
				     "failed to allocate tty driver");

	rpmsgtty_driver->driver_name = "rpmsg_tty";
	rpmsgtty_driver->name = kasprintf(GFP_KERNEL, "ttyRPMSG%d", rpdev->dst);
	rpmsgtty_driver->major = UNNAMED_MAJOR;
	rpmsgtty_driver->minor_start = 0;
	rpmsgtty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	rpmsgtty_driver->init_termios = tty_std_termios;

	tty_set_operations(rpmsgtty_driver, &rpmsgtty_ops);

	tty_port_init(&cport->port);
	cport->port.ops = &rpmsgtty_port_ops;
	spin_lock_init(&cport->rx_lock);
	cport->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, cport);
	rpmsgtty_driver->driver_state = cport;
	cport->rpmsgtty_driver = rpmsgtty_driver;

	ret = tty_register_driver(cport->rpmsgtty_driver);
	if (ret < 0) {
		tty_driver_kref_put(cport->rpmsgtty_driver);
		tty_port_destroy(&cport->port);
		return dev_err_probe(&rpdev->dev, ret, "failed to register rpmsg tty\n");
	}

	dev_info(&rpdev->dev, "rpmsg tty driver registered\n");

	return 0;
}

static void rpmsg_tty_remove(struct rpmsg_device *rpdev)
{
	struct rpmsgtty_port *cport = dev_get_drvdata(&rpdev->dev);

	dev_info(&rpdev->dev, "rpmsg tty driver removed\n");

	tty_unregister_driver(cport->rpmsgtty_driver);
	tty_driver_kref_put(cport->rpmsgtty_driver);
	tty_port_destroy(&cport->port);
}

static struct rpmsg_device_id rpmsg_driver_tty_id_table[] = {
	{ .name	= "rpmsg-virtual-tty-channel" },
	{ }
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_tty_id_table);
static struct rpmsg_driver rpmsg_tty_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_tty_id_table,
	.probe		= rpmsg_tty_probe,
	.callback	= rpmsg_tty_callback,
	.remove		= rpmsg_tty_remove,
};

static int __init init(void)
{
	return register_rpmsg_driver(&rpmsg_tty_driver);
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rpmsg_tty_driver);
}
module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valentina Fernandez <valentina.fernandezalanis@microchip.com>");
MODULE_DESCRIPTION("Mi-V virtio remote processor messaging tty driver");
