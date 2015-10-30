/*
 * Plum-XIO IRQ GPIO support. (c) 2015 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2015 Takeyoshi Kikuchi, kikuchi@centurysys.co.jp
 *
 * Based on code from Freescale,
 * Copyright (C) 2004-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <asm-generic/bug.h>

/* register offset */
#define XIO_ENABLE	0x00
#define XIO_STATUS	0x02

struct xioirq_gpio_port {
	struct list_head node;
	void __iomem *base;
	int irq;
	struct irq_domain *domain;
	struct bgpio_chip bgc;
};

static const struct of_device_id xioirq_gpio_dt_ids[] = {
	{ .compatible = "plum,xioirq-gpio", },
	{ /* sentinel */ }
};

static int xioirq_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct xioirq_gpio_port *port =
		container_of(bgc, struct xioirq_gpio_port, bgc);

	return irq_create_mapping(port->domain, offset);
}

static struct xioirq_gpio_port *xioirq_gpio_to_port(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct xioirq_gpio_port *port =
		container_of(bgc, struct xioirq_gpio_port, bgc);

	return port;
}

static void xioirq_gpio_irq_mask(struct irq_data *d)
{
	struct xioirq_gpio_port *port = irq_data_get_irq_chip_data(d);
	u8 reg;

	reg = readb(port->base + XIO_ENABLE);
	reg &= ~(1 << (d->hwirq));
	writeb(reg, port->base + XIO_ENABLE);
}

static void xioirq_gpio_irq_unmask(struct irq_data *d)
{
	struct xioirq_gpio_port *port = irq_data_get_irq_chip_data(d);
	u8 reg;

	reg = readb(port->base + XIO_ENABLE);
	reg |= 1 << (d->hwirq);
	writeb(reg, port->base + XIO_ENABLE);
}

static int __maybe_unused xioirq_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct xioirq_gpio_port *port = irq_data_get_irq_chip_data(d);
	u32 gpio_idx = d->hwirq;
	u8 irq_enable;

	irq_enable = readb(port->base + XIO_ENABLE);
	irq_enable &= ~(1 << gpio_idx);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		irq_enable |= (1 << gpio_idx);
	default:
		return -EINVAL;
	}

	writeb(irq_enable, port->base + XIO_ENABLE);

	return 0;
}

static void _xioirq_gpio_irq_handler(struct xioirq_gpio_port *port, u8 irq_stat)
{
	while (irq_stat != 0) {
		int irqoffset = fls(irq_stat) - 1;

		generic_handle_irq(irq_find_mapping(port->domain, irqoffset));

		irq_stat &= ~(1 << irqoffset);
	}
}

static irqreturn_t xioirq_gpio_irq_handler(int irq, void *data)
{
	int handled = 0;
	u8 irq_stat, irq_enable;
	struct xioirq_gpio_port *port = data;

	irq_stat = readb(port->base + XIO_STATUS);
	irq_enable = readb(port->base + XIO_ENABLE);

	/* clear pending irq */
	writeb(irq_stat, port->base + XIO_STATUS);

	if (irq_stat & irq_enable) {
		handled = 1;
		_xioirq_gpio_irq_handler(port, irq_stat & irq_enable);
	}

	return IRQ_RETVAL(handled);
}

static struct irq_chip xioirq_gpio_irq_chip = {
	.name			= "xioirq_gpio",
	.irq_mask		= xioirq_gpio_irq_mask,
	.irq_unmask		= xioirq_gpio_irq_unmask,
};


#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void xioirq_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	struct xioirq_gpio_port *port = xioirq_gpio_to_port(gc);

	seq_printf(s, "-----------------------------\n");
	seq_printf(s, " XIO ENABLE:  %02x\n",
		   readb_relaxed(port->base + XIO_ENABLE));
	seq_printf(s, " XIO STATUS:  %02x\n",
		   readb_relaxed(port->base + XIO_STATUS));
	seq_printf(s, "-----------------------------\n");
}
#else
#define xioirq_gpio_dbg_show NULL
#endif

static int xioirq_gpio_irq_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct xioirq_gpio_port *port = d->host_data;

	irq_clear_status_flags(irq, IRQ_NOREQUEST);
	irq_set_chip_data(irq, port);
	irq_set_chip_and_handler(irq, &xioirq_gpio_irq_chip, handle_level_irq);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops xioirq_gpio_irq_domain_ops = {
	.map = xioirq_gpio_irq_map,
};

static int xioirq_gpio_irq_domain_init(struct xioirq_gpio_port *port,
				     struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int status;

	port->domain = irq_domain_add_linear(pdev->dev.of_node,
					     port->bgc.gc.ngpio,
					     &xioirq_gpio_irq_domain_ops,
					     port);
	
	/* enable real irq */
	status = devm_request_threaded_irq(dev,
					   port->irq,
					   xioirq_gpio_irq_handler,
					   NULL,
					   IRQF_SHARED,
					   dev_name(dev),
					   port);

	return 0;
}

static int xioirq_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *child;
	struct device *dev = &pdev->dev;
	struct xioirq_gpio_port *port;
	struct resource *iores;
	int err;

	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return port->irq;

	/* disable the interrupt and clear the status */
	writeb(0x00, port->base + XIO_ENABLE);
	writeb(0xff, port->base + XIO_STATUS);

	err = bgpio_init(&port->bgc, &pdev->dev, 1,
			 port->base + XIO_STATUS,
			 NULL, NULL,
			 NULL, NULL, 0);
	if (err) {
		dev_info(&pdev->dev, "%s bgpio_init() failed with errno %d\n",
			 __func__, err);
		goto out_bgio;
	}

	port->bgc.gc.dbg_show = xioirq_gpio_dbg_show;

	port->bgc.gc.to_irq = xioirq_gpio_to_irq;
	port->bgc.gc.base = -1;

	port->bgc.names = devm_kzalloc(dev, sizeof(char *) * port->bgc.bits, GFP_KERNEL);

	for_each_child_of_node(np, child) {
		const char *name;
		u32 reg;
		int ret;

		name = of_get_property(child, "label", NULL);
		ret = of_property_read_u32(child, "reg", &reg);

		if (name && ret == 0 && reg >= 0 && reg < port->bgc.gc.ngpio) {
			port->bgc.names[reg] = name;
		}
	}

	xioirq_gpio_irq_domain_init(port, pdev);

	port->bgc.gc.names = port->bgc.names;

	err = gpiochip_add(&port->bgc.gc);
	if (err) {
		dev_info(&pdev->dev, "%s gpiochip_add() failed with errno %d\n",
			 __func__, err);
		goto out_bgpio_remove;
	}

	if (err == 0) {
		int i, status, gpio;

		for (i = 0; i < port->bgc.gc.ngpio; i++) {
			if (port->bgc.names[i] != NULL) {
				gpio = port->bgc.gc.base + i;

				status = gpio_request(gpio, port->bgc.names[i]);

				if (status == 0) {
					status = gpio_export(gpio, true);
					if (status < 0)
						gpio_free(gpio);
				}
			}
		}
	}

	return 0;

out_bgpio_remove:
	bgpio_remove(&port->bgc);
out_bgio:
	dev_info(&pdev->dev, "%s failed with errno %d\n", __func__, err);
	return err;
}

static struct platform_driver xioirq_gpio_driver = {
	.driver		= {
		.name	= "xioirq_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = xioirq_gpio_dt_ids,
	},
	.probe		= xioirq_gpio_probe,
};

static int __init gpio_xioirq_init(void)
{
	return platform_driver_register(&xioirq_gpio_driver);
}
postcore_initcall(gpio_xioirq_init);

MODULE_AUTHOR("Century Systems, "
	      "Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Century Systems XIOIRQ GPIO Driver rev.2");
MODULE_LICENSE("GPL");
