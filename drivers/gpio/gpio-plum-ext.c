/*
 * Plum-extention GPIO support. (c) 2015 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
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
#define GPIO_STATUS		0x00
#define GPIO_INT_STATUS	0x04
#define GPIO_INT_ENABLE	0x08
#define GPIO_EDGE_SEL		0x0c
#define GPIO_FILTER		0x10

#define EDGE_RISING(x)		(0 << (x))
#define EDGE_FALLING(x)	(1 << (x))

#define FILTER_NONE(x)		(0 << ((x) * 2))
#define FILTER_1ms(x)		(1 << ((x) * 2))
#define FILTER_5ms(x)		(2 << ((x) * 2))
#define FILTER_20ms(x)		(3 << ((x) * 2))

static unsigned reg2filter[] = { 0, 1, 5, 20 };


struct plum_gpio_port {
	struct list_head node;
	void __iomem *base;
	int irq;
	struct irq_domain *domain;
	struct bgpio_chip bgc;
	u32 both_edges;
};

static const struct of_device_id plum_gpio_dt_ids[] = {
	{ .compatible = "plum-gpio", },
	{ .compatible = "plum,ext-DI", },
	{ /* sentinel */ }
};

static int plum_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct plum_gpio_port *port =
		container_of(bgc, struct plum_gpio_port, bgc);

	return irq_create_mapping(port->domain, offset);
}

static struct plum_gpio_port *plum_gpio_to_port(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct plum_gpio_port *port =
		container_of(bgc, struct plum_gpio_port, bgc);

	return port;
}

static void plum_gpio_irq_mask(struct irq_data *d)
{
	struct plum_gpio_port *port = irq_data_get_irq_chip_data(d);
	u8 reg;

	reg = readb(port->base + GPIO_INT_ENABLE);
	reg &= ~(1 << (d->hwirq));
	writeb(reg, port->base + GPIO_INT_ENABLE);
}

static void plum_gpio_irq_unmask(struct irq_data *d)
{
	struct plum_gpio_port *port = irq_data_get_irq_chip_data(d);
	u8 reg;

	reg = readb(port->base + GPIO_INT_ENABLE);
	reg |= 1 << (d->hwirq);
	writeb(reg, port->base + GPIO_INT_ENABLE);
}

static int plum_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct plum_gpio_port *port = irq_data_get_irq_chip_data(d);
	u32 val;
	u32 gpio_idx = d->hwirq;
	u32 gpio = port->bgc.gc.base + gpio_idx;
	u8 edge_sel;

	edge_sel = readb(port->base + GPIO_EDGE_SEL);

	port->both_edges &= ~(1 << gpio_idx);
	edge_sel &= ~(1 << gpio_idx);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		edge_sel |= EDGE_RISING(gpio_idx);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		edge_sel |= EDGE_FALLING(gpio_idx);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val = gpio_get_value(gpio);
		if (val) {
			edge_sel |= EDGE_FALLING(gpio_idx);
			pr_debug("plum-di: set GPIO %d to low trigger\n", gpio);
		} else {
			edge_sel |= EDGE_RISING(gpio_idx);
			pr_debug("plum-di: set GPIO %d to high trigger\n", gpio);
		}
		port->both_edges |= 1 << gpio_idx;
		break;
	default:
		return -EINVAL;
	}

	writeb(edge_sel, port->base + GPIO_EDGE_SEL);

	return 0;
}

static void plum_flip_edge(struct plum_gpio_port *port, u32 gpio)
{
	u8 edge_sel;
	int pol;

	edge_sel = readb(port->base + GPIO_EDGE_SEL);

	if (edge_sel & (1 << gpio))
		pol = 0;
	else
		pol = 1;

	edge_sel &= ~(1 << gpio);
	edge_sel |= (pol << gpio);

	writeb(edge_sel, port->base + GPIO_EDGE_SEL);
}

static void _plum_gpio_irq_handler(struct plum_gpio_port *port, u8 irq_stat)
{
	while (irq_stat != 0) {
		int irqoffset = fls(irq_stat) - 1;

		if (port->both_edges & (1 << irqoffset))
			plum_flip_edge(port, irqoffset);

		generic_handle_irq(irq_find_mapping(port->domain, irqoffset));

		irq_stat &= ~(1 << irqoffset);
	}
}

static irqreturn_t plum_gpio_irq_handler(int irq, void *data)
{
	int handled = 0;
	u8 irq_stat, irq_enable;
	struct plum_gpio_port *port = data;

	irq_stat = readb(port->base + GPIO_INT_STATUS);
	irq_enable = readb(port->base + GPIO_INT_ENABLE);

	/* clear pending irq */
	writeb(irq_stat, port->base + GPIO_INT_STATUS);

	if (irq_stat & irq_enable) {
		handled = 1;
		_plum_gpio_irq_handler(port, irq_stat & irq_enable);
	}

	return IRQ_RETVAL(handled);
}

static int plum_gpio_set_debounce(struct gpio_chip *gc, unsigned offset, unsigned debounce)
{
	struct plum_gpio_port *port = plum_gpio_to_port(gc);
	int group, filter_val;
	u8 reg;

	if (offset < 4)
		group = 0;
	else if (offset < 8)
		group = 1;
	else
		return -EINVAL;

	if (debounce == 0) {
		filter_val = FILTER_NONE(group);
	} else if (debounce < 5) {
		filter_val = FILTER_1ms(group);
	} else if (debounce < 20) {
		filter_val = FILTER_5ms(group);
	} else {
		filter_val = FILTER_20ms(group);
	}

	reg = readb(port->base + GPIO_FILTER);
	reg &= ~FILTER_20ms(group);
	reg |= filter_val;

	writeb(reg, port->base + GPIO_FILTER);

	return 0;
}

static unsigned plum_gpio_get_debounce(struct gpio_chip *gc, unsigned offset)
{
	struct plum_gpio_port *port = plum_gpio_to_port(gc);
	int group;
	u8 reg;
	unsigned filter_val;

	if (offset < 4)
		group = 0;
	else
		group = 1;

	reg = readb(port->base + GPIO_FILTER);

	filter_val = (reg >> (2 * group)) & 0x03;

	return reg2filter[filter_val];
}

static struct irq_chip plum_gpio_irq_chip = {
	.name			= "plum_DI",
	.irq_mask		= plum_gpio_irq_mask,
	.irq_unmask		= plum_gpio_irq_unmask,
	.irq_set_type		= plum_gpio_irq_set_type,
};


#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void plum_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	struct plum_gpio_port *port = plum_gpio_to_port(gc);

	seq_printf(s, "-----------------------------\n");
	seq_printf(s, " DIN Pri L port status:  %02x\n",
		   readb_relaxed(port->base + GPIO_STATUS));
	seq_printf(s, " DIN Pri L IRQ status:   %02x\n",
		   readb_relaxed(port->base + GPIO_INT_STATUS));
	seq_printf(s, " DIN Pri L IRQ enable:   %02x\n",
		   readb_relaxed(port->base + GPIO_INT_ENABLE));
	seq_printf(s, " DIN Pri L IRQ polarity: %02x\n",
		   readb_relaxed(port->base + GPIO_EDGE_SEL));
	seq_printf(s, " DIN Filter select:      %02x\n",
		   readb_relaxed(port->base + GPIO_FILTER));
	seq_printf(s, "-----------------------------\n");
}
#else
#define plum_gpio_dbg_show NULL
#endif

static int plum_gpio_irq_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct plum_gpio_port *port = d->host_data;

	irq_clear_status_flags(irq, IRQ_NOREQUEST);
	irq_set_chip_data(irq, port);
	irq_set_chip_and_handler(irq, &plum_gpio_irq_chip, handle_level_irq);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops plum_gpio_irq_domain_ops = {
	.map = plum_gpio_irq_map,
};

static int plum_gpio_irq_domain_init(struct plum_gpio_port *port,
				     struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int status;

	port->domain = irq_domain_add_linear(pdev->dev.of_node,
					     port->bgc.gc.ngpio,
					     &plum_gpio_irq_domain_ops,
					     port);
	
	/* enable real irq */
	status = devm_request_threaded_irq(dev,
					   port->irq,
					   plum_gpio_irq_handler,
					   NULL,
					   IRQF_SHARED,
					   dev_name(dev),
					   port);

	return 0;
}

static int plum_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *child;
	struct device *dev = &pdev->dev;
	struct plum_gpio_port *port;
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
	writeb(0x00, port->base + GPIO_INT_ENABLE);
	writeb(0xff, port->base + GPIO_INT_STATUS);

	err = bgpio_init(&port->bgc, &pdev->dev, 1,
			 port->base + GPIO_STATUS,
			 NULL, NULL,
			 NULL, NULL, 0);
	if (err) {
		dev_info(&pdev->dev, "%s bgpio_init() failed with errno %d\n",
			 __func__, err);
		goto out_bgio;
	}

	port->bgc.gc.set_debounce = plum_gpio_set_debounce;
	port->bgc.gc.get_debounce = plum_gpio_get_debounce;
	port->bgc.gc.dbg_show = plum_gpio_dbg_show;

	port->bgc.gc.to_irq = plum_gpio_to_irq;
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

	plum_gpio_irq_domain_init(port, pdev);

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

static struct platform_driver plum_gpio_driver = {
	.driver		= {
		.name	= "gpio-plum-DI",
		.owner	= THIS_MODULE,
		.of_match_table = plum_gpio_dt_ids,
	},
	.probe		= plum_gpio_probe,
};

static int __init gpio_plum_init(void)
{
	return platform_driver_register(&plum_gpio_driver);
}
postcore_initcall(gpio_plum_init);

MODULE_AUTHOR("Century Systems, "
	      "Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Plum-extio GPIO");
MODULE_LICENSE("GPL");
