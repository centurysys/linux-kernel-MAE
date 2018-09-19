// SPDX-License-Identifier: GPL-2.0
/*
 * Plum-XIO IRQ GPIO support. (c) 2018 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 *
 * Base on code from Faraday Technolog.
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-gemini/gpio.c:
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * Based on plat-mxc/gpio.c:
 * MXC GPIO support. (c) 2008 Daniel Mack <daniel@caiaq.de>
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 */
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

/* register offset */
#define XIO_ENABLE	0x00
#define XIO_STATUS	0x02
#define XIO_VALUE	0x04

/**
 * struct xioirq_gpio - Gemini GPIO state container
 * @dev: containing device for this instance
 * @gc: gpiochip for this instance
 */
struct xioirq_gpio {
	struct device *dev;
	struct gpio_chip gc;
	void __iomem *base;
	int irq;
	resource_size_t size;
};

static void xioirq_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct xioirq_gpio *port = gpiochip_get_data(gc);
	u8 reg;

	reg = readb(port->base + XIO_ENABLE);
	reg &= ~(1 << (d->hwirq));
	writeb(reg, port->base + XIO_ENABLE);
}

static void xioirq_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct xioirq_gpio *port = gpiochip_get_data(gc);
	u8 reg;

	reg = readb(port->base + XIO_ENABLE);
	reg |= 1 << (d->hwirq);
	writeb(reg, port->base + XIO_ENABLE);
}

static int xioirq_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct xioirq_gpio *port = gpiochip_get_data(gc);
	u32 gpio_idx = d->hwirq;
	u8 irq_enable;

	irq_enable = readb(port->base + XIO_ENABLE);
	irq_enable &= ~(1 << gpio_idx);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler_locked(d, handle_level_irq);
		irq_enable |= (1 << gpio_idx);
		break;

	case IRQ_TYPE_NONE:
		irq_set_handler_locked(d, handle_bad_irq);
		break;

	default:
		return -EINVAL;
	}

	writeb(irq_enable, port->base + XIO_ENABLE);

	return 0;
}

static struct irq_chip xioirq_gpio_irqchip = {
	.name = "xioirq_gpio",
	.irq_mask = xioirq_gpio_mask_irq,
	.irq_unmask = xioirq_gpio_unmask_irq,
	.irq_set_type = xioirq_gpio_set_irq_type,
};

static irqreturn_t xioirq_gpio_irq_handler(int irq, void *data)
{
	struct xioirq_gpio *port = (struct xioirq_gpio *) data;
	struct gpio_chip *gc = &port->gc;
	int offset, handled = 0;
	u8 stat, enable;

	stat = readb(port->base + XIO_STATUS);
	enable = readb(port->base + XIO_ENABLE);

	/* clear pending irq */
	writeb(stat, port->base + XIO_STATUS);

	stat &= enable;

	if (stat)
		handled = 1;

	while (stat != 0) {
		offset = fls(stat) - 1;
		generic_handle_irq(irq_find_mapping(gc->irqdomain,
						    offset));
		stat &= ~(1 << offset);
	}

	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void xioirq_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	struct xioirq_gpio *port = gpiochip_get_data(gc);

	seq_printf(s, "-----------------------------\n");
	seq_printf(s, " XIO ENABLE:  %02x\n",
		   readb_relaxed(port->base + XIO_ENABLE));
	seq_printf(s, " XIO STATUS:  %02x\n",
		   readb_relaxed(port->base + XIO_STATUS));

	if (port->size > 4) {
		seq_printf(s, " XIO VALUE:   %02x\n",
			   readb_relaxed(port->base + XIO_VALUE));
	}
	seq_printf(s, "-----------------------------\n");
}
#else
#define xioirq_gpio_dbg_show NULL
#endif

static int xioirq_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *child;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xioirq_gpio *port;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	port->irq = platform_get_irq(pdev, 0);
	if (!port->irq)
		return -EINVAL;

	port->size = res->end - res->start + 1;

	ret = bgpio_init(&port->gc, dev, 1,
			 port->base + (port->size == 4 ? XIO_STATUS : XIO_VALUE),
			 NULL, NULL, NULL, NULL, BGPIOF_NO_OUTPUT);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}
	port->gc.label = "xioirq-gpio";
	port->gc.base = -1;
	port->gc.parent = dev;
	port->gc.owner = THIS_MODULE;
	port->gc.dbg_show = xioirq_gpio_dbg_show;

#ifdef CONFIG_GPIO_GENERIC_EXPORT_BY_DT
	port->gc.bgpio_names = devm_kzalloc(dev, sizeof(char *) * port->gc.ngpio, GFP_KERNEL);

	for_each_child_of_node(np, child) {
		const char *name;
		u32 reg;
		int ret;

		name = of_get_property(child, "label", NULL);
		ret = of_property_read_u32(child, "reg", &reg);

		if (name && ret == 0 && reg >= 0 && reg < port->gc.ngpio) {
			port->gc.bgpio_names[reg] = name;
		}
	}
#endif
	ret = devm_gpiochip_add_data(dev, &port->gc, port);
	if (ret)
		return ret;

	/* Disable, unmask and clear all interrupts */
	writeb(0x00, port->base + XIO_ENABLE);
	writeb(0xff, port->base + XIO_STATUS);

	ret = gpiochip_irqchip_add(&port->gc, &xioirq_gpio_irqchip,
				   0, handle_bad_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_info(dev, "could not add irqchip\n");
		return ret;
	}
	gpiochip_set_chained_irqchip(&port->gc, &xioirq_gpio_irqchip,
				     port->irq, NULL);

	ret = devm_request_irq(port->gc.parent, port->irq,
			       xioirq_gpio_irq_handler,
			       IRQF_SHARED, dev_name(port->gc.parent), port);

#ifdef CONFIG_GPIO_GENERIC_EXPORT_BY_DT
	{
		int i, status, gpio;

		for (i = 0; i < port->gc.ngpio; i++) {
			if (port->gc.bgpio_names[i] != NULL) {
				gpio = port->gc.base + i;

				status = gpio_request(gpio, port->gc.bgpio_names[i]);

				if (status == 0) {
					status = gpio_export(gpio, false);
					if (status < 0)
						gpio_free(gpio);
				}
			}
		}
	}
#endif
	dev_info(dev, "xioirq-gpio @%p registered\n", port->base);

	return 0;
}

static const struct of_device_id xioirq_gpio_of_match[] = {
	{
		.compatible = "plum,xioirq-gpio",
	},
	{},
};

static struct platform_driver xioirq_gpio_driver = {
	.driver = {
		.name		= "xioirq_gpio",
		.of_match_table = of_match_ptr(xioirq_gpio_of_match),
	},
	.probe	= xioirq_gpio_probe,
};

static int __init gpio_xioirq_init(void)
{
	return platform_driver_register(&xioirq_gpio_driver);
}
postcore_initcall(gpio_xioirq_init);

MODULE_AUTHOR("Century Systems, Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Century Systems XIOIRQ GPIO Driver rev.2");
MODULE_LICENSE("GPL");
