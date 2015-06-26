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

	return irq_find_mapping(port->domain, offset);
}

static struct plum_gpio_port *plum_gpio_to_port(struct gpio_chip *gc, unsigned offset)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct plum_gpio_port *port =
		container_of(bgc, struct plum_gpio_port, bgc);

	return port;
}

static int gpio_set_irq_type(struct irq_data *d, u32 type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio_port *port = gc->private;
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

/* handle 32 interrupts in one status register */
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

static void plum_gpio_irq_handler(u32 irq, struct irq_desc *desc)
{
	u8 irq_stat;
	struct plum_gpio_port *port = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);

	chained_irq_enter(chip, desc);

	irq_stat = readb(port->base + GPIO_INT_STATUS) & \
		readb(port->base + GPIO_INT_ENABLE);

	_plum_gpio_irq_handler(port, irq_stat);

	chained_irq_exit(chip, desc);
}

/*
 * Set interrupt number "irq" in the GPIO as a wake-up source.
 * While system is running, all registered GPIO interrupts need to have
 * wake-up enabled. When system is suspended, only selected GPIO interrupts
 * need to have wake-up enabled.
 * @param  irq          interrupt source number
 * @param  enable       enable as wake-up if equal to non-zero
 * @return       This function returns 0 on success.
 */
static int gpio_set_wake_irq(struct irq_data *d, u32 enable)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio_port *port = gc->private;

	if (enable) {
		enable_irq_wake(port->irq);
	} else {
		disable_irq_wake(port->irq);
	}

	return 0;
}

static int plum_gpio_set_debounce(struct gpio_chip *gc, unsigned offset, unsigned debounce)
{
	struct plum_gpio_port *port = plum_gpio_to_port(gc, offset);
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
	struct plum_gpio_port *port = plum_gpio_to_port(gc, offset);
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

/**
 * irq_ack_set_bit - Ack pending interrupt via setting bit
 * @d: irq_data
 */
static void irq_ack_set_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	writeb((u8) mask, gc->reg_base + ct->regs.ack);
	irq_gc_unlock(gc);
}

/**
 * irq_mask_clr_bit - Mask chip via clearing bit in mask register
 * @d: irq_data
 *
 * Chip has a single mask register. Values of this register are cached
 * and protected by gc->lock
 */
static void irq_mask_clr_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	*ct->mask_cache &= ~mask;
	writeb((u8) *ct->mask_cache, gc->reg_base + ct->regs.mask);
	irq_gc_unlock(gc);
}

/**
 * irq_mask_set_bit - Mask chip via setting bit in mask register
 * @d: irq_data
 *
 * Chip has a single mask register. Values of this register are cached
 * and protected by gc->lock
 */
static void irq_mask_set_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	*ct->mask_cache |= mask;
	writeb((u8) *ct->mask_cache, gc->reg_base + ct->regs.mask);
	irq_gc_unlock(gc);
}

static void __init plum_gpio_init_gc(struct plum_gpio_port *port, int irq_base)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("plum-DI", 1, irq_base,
				    port->base, handle_level_irq);
	gc->private = port;

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_ack_set_bit;
	ct->chip.irq_mask = irq_mask_clr_bit;
	ct->chip.irq_unmask = irq_mask_set_bit;
	ct->chip.irq_set_type = gpio_set_irq_type;
	ct->chip.irq_set_wake = gpio_set_wake_irq;
	ct->regs.ack = GPIO_INT_STATUS;
	ct->regs.mask = GPIO_INT_ENABLE;

	irq_setup_generic_chip(gc, IRQ_MSK(8), IRQ_GC_INIT_NESTED_LOCK,
			       IRQ_NOREQUEST, 0);
}

static int plum_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *child;
	struct device *dev = &pdev->dev;
	struct plum_gpio_port *port;
	struct resource *iores;
	int irq_base;
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

	/* setup one handler for each entry */
	irq_set_chained_handler(port->irq, plum_gpio_irq_handler);
	irq_set_handler_data(port->irq, port);

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

	port->bgc.gc.names = port->bgc.names;

	err = gpiochip_add(&port->bgc.gc);
	if (err) {
		dev_info(&pdev->dev, "%s gpiochip_add() failed with errno %d\n",
			 __func__, err);
		goto out_bgpio_remove;
	}

	irq_base = irq_alloc_descs(-1, 0, 8, numa_node_id());
	if (irq_base < 0) {
		err = irq_base;
		dev_info(&pdev->dev, "%s irq_alloc_descs() failed with errno %d\n",
			 __func__, err);
		goto out_gpiochip_remove;
	}

	port->domain = irq_domain_add_legacy(np, 8, irq_base, 0,
					     &irq_domain_simple_ops, NULL);
	if (!port->domain) {
		err = -ENODEV;
		goto out_irqdesc_free;
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

	/* gpio-plum-ext can be a generic irq chip */
	plum_gpio_init_gc(port, irq_base);

	return 0;

out_irqdesc_free:
	irq_free_descs(irq_base, 8);
out_gpiochip_remove:
	WARN_ON(gpiochip_remove(&port->bgc.gc) < 0);
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
