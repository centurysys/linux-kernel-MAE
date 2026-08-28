// SPDX-License-Identifier: GPL-2.0
/*
 * Plum-XIO IRQ GPIO support.
 *
 * Copyright (C) 2018-2026 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * The GPIO block is an 8-bit input-only GPIO controller backed by a CPLD.
 * Each instance has its own interrupt enable/status registers, while several
 * instances may share the same parent IRQ line.
 *
 * Linux 6.12 version:
 *   - use the generic bgpio helper for the GPIO data path
 *   - use an immutable gpio irqchip
 *   - let gpiolib create the child IRQ domain
 *   - request the parent IRQ directly with IRQF_SHARED
 *   - demultiplex pending bits with generic_handle_domain_irq()
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Register offsets. */
#define XIO_ENABLE	0x00
#define XIO_STATUS	0x02
#define XIO_VALUE	0x04

struct xioirq_gpio {
	struct device *dev;
	struct gpio_chip gc;
	void __iomem *base;
	raw_spinlock_t lock;
	resource_size_t size;
};

static void xioirq_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct xioirq_gpio *port = gpiochip_get_data(gc);
	unsigned long flags;
	u8 reg;

	raw_spin_lock_irqsave(&port->lock, flags);
	reg = readb(port->base + XIO_ENABLE);
	reg &= ~BIT(d->hwirq);
	writeb(reg, port->base + XIO_ENABLE);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void xioirq_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct xioirq_gpio *port = gpiochip_get_data(gc);
	unsigned long flags;
	u8 reg;

	raw_spin_lock_irqsave(&port->lock, flags);
	reg = readb(port->base + XIO_ENABLE);
	reg |= BIT(d->hwirq);
	writeb(reg, port->base + XIO_ENABLE);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static int xioirq_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	/*
	 * The CPLD exposes only interrupt enable and latched status registers;
	 * there is no programmable polarity/type register here.  Keep the
	 * trigger types accepted by the legacy driver and preserve its child
	 * IRQ flow-handler semantics.  Mask/unmask is handled separately by
	 * the irq_chip callbacks above.
	 */
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler_locked(d, handle_level_irq);
		return 0;

	case IRQ_TYPE_NONE:
		irq_set_handler_locked(d, handle_bad_irq);
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct irq_chip xioirq_gpio_irqchip = {
	.name = "xioirq-gpio",
	.irq_mask = xioirq_gpio_mask_irq,
	.irq_unmask = xioirq_gpio_unmask_irq,
	.irq_set_type = xioirq_gpio_set_irq_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t xioirq_gpio_irq_handler(int irq, void *data)
{
	struct xioirq_gpio *port = data;
	struct gpio_chip *gc = &port->gc;
	unsigned long flags;
	unsigned long pending;
	u8 status, enable;
	unsigned int offset;

	/*
	 * This is a shared parent IRQ.  Read only this instance's status and
	 * return IRQ_NONE when none of its enabled inputs are pending.
	 */
	raw_spin_lock_irqsave(&port->lock, flags);
	status = readb(port->base + XIO_STATUS);
	enable = readb(port->base + XIO_ENABLE);

	/* XIO_STATUS is write-one-to-clear. */
	writeb(status, port->base + XIO_STATUS);
	pending = status & enable;
	raw_spin_unlock_irqrestore(&port->lock, flags);

	if (!pending)
		return IRQ_NONE;

	for_each_set_bit(offset, &pending, gc->ngpio)
		generic_handle_domain_irq(gc->irq.domain, offset);

	return IRQ_HANDLED;
}

static int xioirq_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xioirq_gpio *port;
	struct gpio_irq_chip *girq;
	struct resource *res;
	void __iomem *data_reg;
	int irq;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;
	raw_spin_lock_init(&port->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	port->size = resource_size(res);
	if (port->size < XIO_STATUS + sizeof(u8)) {
		dev_err(dev, "register window too small: %pa bytes\n", &port->size);
		return -EINVAL;
	}

	/*
	 * Legacy hardware has two layouts:
	 *
	 *   4-byte window: ENABLE + STATUS, with STATUS also used as GPIO data
	 *   5+ byte window: ENABLE + STATUS + VALUE
	 */
	if (port->size == XIO_VALUE)
		data_reg = port->base + XIO_STATUS;
	else if (port->size > XIO_VALUE)
		data_reg = port->base + XIO_VALUE;
	else {
		dev_err(dev, "unsupported register window size: %pa bytes\n",
			&port->size);
		return -EINVAL;
	}

	ret = bgpio_init(&port->gc, dev, sizeof(u8), data_reg,
			 NULL, NULL, NULL, NULL, BGPIOF_NO_OUTPUT);
	if (ret) {
		dev_err(dev, "failed to initialize generic GPIO: %d\n", ret);
		return ret;
	}

	port->gc.label = dev_name(dev);
	port->gc.base = -1;
	port->gc.parent = dev;
	port->gc.owner = THIS_MODULE;

	/*
	 * The parent IRQ is shared by multiple independent XIO GPIO blocks.
	 * Do not describe it as a cascaded parent to gpiolib; this driver
	 * requests the shared parent itself and demultiplexes each instance.
	 */
	girq = &port->gc.irq;
	gpio_irq_chip_set_chip(girq, &xioirq_gpio_irqchip);
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->parent_handler = NULL;

	/* Start with every child IRQ masked and clear stale pending status. */
	writeb(0x00, port->base + XIO_ENABLE);
	writeb(0xff, port->base + XIO_STATUS);

	ret = devm_gpiochip_add_data(dev, &port->gc, port);
	if (ret) {
		dev_err(dev, "failed to register GPIO chip: %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, xioirq_gpio_irq_handler,
			       IRQF_SHARED, dev_name(dev), port);
	if (ret) {
		dev_err(dev, "failed to request shared parent IRQ %d: %d\n",
			irq, ret);
		return ret;
	}

	platform_set_drvdata(pdev, port);

	dev_info(dev, "registered 8-bit XIO IRQ GPIO on shared parent IRQ %d\n",
		 irq);

	return 0;
}

static const struct of_device_id xioirq_gpio_of_match[] = {
	{ .compatible = "plum,xioirq-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, xioirq_gpio_of_match);

static struct platform_driver xioirq_gpio_driver = {
	.probe = xioirq_gpio_probe,
	.driver = {
		.name = "xioirq-gpio",
		.of_match_table = xioirq_gpio_of_match,
	},
};
module_platform_driver(xioirq_gpio_driver);

MODULE_AUTHOR("Century Systems, Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Century Systems Plum-XIO IRQ GPIO driver");
MODULE_LICENSE("GPL");
