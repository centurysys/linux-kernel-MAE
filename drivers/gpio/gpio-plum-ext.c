// SPDX-License-Identifier: GPL-2.0
/*
 * Plum-extension GPIO support. (c) 2018 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>

/* register offset */
#define GPIO_STATUS		0x00
#define GPIO_INT_STATUS	0x04
#define GPIO_INT_ENABLE	0x08
#define GPIO_EDGE_SEL		0x0c
#define GPIO_FILTER		0x10
#define GPIO_COUNTER_CTRL	0x12
#define GPIO_MATCH_STATUS	0x14
#define GPIO_MATCH_ENABLE	0x16
#define GPIO_OVERFLOW		0x18
#define GPIO_COUNTER(x)	(0x1a + x * 2)
#define GPIO_COMPARE(x)	(0x22 + x * 2)

#define EDGE_RISING(x)		(0 << (x))
#define EDGE_FALLING(x)	(1 << (x))

#define FILTER_NONE(x)		(0 << ((x) * 2))
#define FILTER_1ms(x)		(1 << ((x) * 2))
#define FILTER_5ms(x)		(2 << ((x) * 2))
#define FILTER_20ms(x)		(3 << ((x) * 2))

#ifdef CONFIG_GPIO_FILTER
static unsigned reg2filter[] = { 0, 1, 5, 20 };
#endif

/**
 * struct plum_gpio - Gemini GPIO state container
 * @dev: containing device for this instance
 * @gc: gpiochip for this instance
 */
struct plum_gpio {
	struct device *dev;
	struct gpio_chip gc;
	void __iomem *base;
	raw_spinlock_t lock;
	u8 irq_enable;
#ifdef CONFIG_GPIO_HWCOUNTER
	int num_counters;
	u8 wakeup_mask;
#endif
	int irq;
	u32 both_edges;
	resource_size_t size;
};

static void plum_gpio_sync_irq(struct plum_gpio *port)
{
	u8 reg;

	reg = port->irq_enable;
#ifdef CONFIG_GPIO_HWCOUNTER
	reg &= ~port->wakeup_mask;
#endif
	writeb(reg, port->base + GPIO_INT_ENABLE);
}

static void plum_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);

	port->irq_enable &= ~(1 << (d->hwirq));
	plum_gpio_sync_irq(port);
}

static void plum_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);

	port->irq_enable |= 1 << (d->hwirq);
	plum_gpio_sync_irq(port);
}

static int plum_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);
	u32 gpio_idx = d->hwirq;
	u32 gpio = port->gc.base + gpio_idx;
	u8 edge_sel, val;
	bool set_handler = true;

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
			pr_debug("plum-gpio: set GPIO %d to low trigger\n", gpio);
		} else {
			edge_sel |= EDGE_RISING(gpio_idx);
			pr_debug("plum-gpio: set GPIO %d to high trigger\n", gpio);
		}
		port->both_edges |= 1 << gpio_idx;
		break;
	case IRQ_TYPE_NONE:
		set_handler = false;
		break;
	default:
		return -EINVAL;
	}

	writeb(edge_sel, port->base + GPIO_EDGE_SEL);

	if (set_handler) {
		irq_set_handler_locked(d, handle_level_irq);
	} else {
		irq_set_handler_locked(d, handle_bad_irq);
	}

	return 0;
}

static struct irq_chip plum_gpio_irqchip = {
	.name = "plum_gpio",
	.irq_mask = plum_gpio_mask_irq,
	.irq_unmask = plum_gpio_unmask_irq,
	.irq_set_type = plum_gpio_set_irq_type,
};

static void plum_flip_edge(struct plum_gpio *port, u32 gpio)
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

static void plum_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	struct plum_gpio *port = container_of(gc, struct plum_gpio, gc);
	int offset;
	unsigned long flags;
	u8 stat, enable;

	chained_irq_enter(irqchip, desc);
	raw_spin_lock_irqsave(&port->lock, flags);

	stat = readb(port->base + GPIO_INT_STATUS);
	enable = readb(port->base + GPIO_INT_ENABLE);

	/* clear pending irq */
	writeb(stat, port->base + GPIO_INT_STATUS);
	stat &= enable;

	raw_spin_unlock_irqrestore(&port->lock, flags);

	while (stat != 0) {
		offset = fls(stat) - 1;

		if (port->both_edges & (1 << offset))
			plum_flip_edge(port, offset);

		generic_handle_irq(irq_find_mapping(gc->irq.domain,
						    offset));
		stat &= ~(1 << offset);
	}

	chained_irq_exit(irqchip, desc);
}

#ifdef CONFIG_GPIO_FILTER
static int plum_gpio_set_debounce(struct gpio_chip *gc, unsigned offset, unsigned debounce)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
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
	struct plum_gpio *port = gpiochip_get_data(gc);
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
#endif

#ifdef CONFIG_GPIO_HWCOUNTER
static int plum_gpio_clear_overflow(struct plum_gpio *port, unsigned offset)
{
	u8 reg;

	if (offset >= port->num_counters)
		return -EINVAL;

	reg = 1 << offset;
	writeb(reg, port->base + GPIO_OVERFLOW);

	return 0;
}

static int plum_gpio_is_overflow(struct plum_gpio *port, unsigned offset)
{
	u8 reg;
	int overflow = 0;

	if (offset >= port->num_counters)
		return -EINVAL;

	reg = readb(port->base + GPIO_OVERFLOW);

	if (reg & (1 << offset)) {
		overflow = 1;
	}

	return overflow;
}

static int plum_gpio_set_hwcounter(struct gpio_chip *gc, unsigned offset, unsigned counter)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	u8 reg;

	if (offset >= port->num_counters)
		return -EINVAL;

	plum_gpio_clear_overflow(port, offset);

	reg = (u8) (counter & 0xff);
	writeb(reg, port->base + GPIO_COUNTER(offset));

	return 0;
}

static unsigned plum_gpio_get_hwcounter(struct gpio_chip *gc, unsigned offset)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned counter;
	unsigned long flags;
	int overflow;

	if (offset >= port->num_counters)
		return 0;

	raw_spin_lock_irqsave(&port->lock, flags);

	counter = (u32) readb(port->base + GPIO_COUNTER(offset));
	overflow = plum_gpio_is_overflow(port, offset);

	if (overflow && counter != 0xff) {
		plum_gpio_clear_overflow(port, offset);
		counter |= 0x100;
	}

	raw_spin_unlock_irqrestore(&port->lock, flags);

	return counter;
}

static int plum_gpio_set_hwcounter_enable(struct gpio_chip *gc, unsigned offset, int enable)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	u8 reg;

	if (offset >= port->num_counters)
		return -EINVAL;

	enable = !!enable;

	reg = readb(port->base + GPIO_COUNTER_CTRL);
	reg &= ~(1 << offset);
	reg |= enable << offset;
	writeb(reg, port->base + GPIO_COUNTER_CTRL);

	return 0;
}

static unsigned plum_gpio_get_hwcounter_enable(struct gpio_chip *gc, unsigned offset)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned enable;

	if (offset >= port->num_counters)
		return 0;

	enable = (unsigned) readb(port->base + GPIO_COUNTER_CTRL);
	enable &= (1 << offset);
	enable = !!enable;

	return enable;
}

static int plum_gpio_set_wakeup_enable(struct gpio_chip *gc, unsigned offset, int enable)
{
	struct plum_gpio *port = gpiochip_get_data(gc);

	if (offset >= port->num_counters)
		return -EINVAL;

	enable = !!enable;

	if (enable)
		port->wakeup_mask &= ~(1 << offset);
	else
		port->wakeup_mask |= (1 << offset);

	plum_gpio_sync_irq(port);
	return 0;
}

static unsigned plum_gpio_get_wakeup_enable(struct gpio_chip *gc, unsigned offset)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned enable;

	if (offset >= port->num_counters)
		return 0;

	enable = !(port->wakeup_mask & (1 << offset));

	return enable;
}
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

extern void gpiolib_dbg_show(struct seq_file *s, struct gpio_device *gdev);

static void plum_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	struct plum_gpio *port = gpiochip_get_data(gc);

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

#ifdef CONFIG_GPIO_HWCOUNTER
	if (port->num_counters > 0) {
		int i;

		seq_printf(s, " num_counters:           %02x\n", port->num_counters);
		seq_printf(s, " Counter Control:        %02x\n",
			   readb_relaxed(port->base + GPIO_COUNTER_CTRL));
		seq_printf(s, " Match IRQ Status:       %02x\n",
			   readb_relaxed(port->base + GPIO_MATCH_STATUS));
		seq_printf(s, " Match IRQ Enable:       %02x\n",
			   readb_relaxed(port->base + GPIO_MATCH_ENABLE));
		seq_printf(s, " Overflow Flag:          %02x\n",
			   readb_relaxed(port->base + GPIO_OVERFLOW));
		seq_printf(s, " irq_enable:             %02x\n", port->irq_enable);
		seq_printf(s, " wakeup_mask:            %02x\n", port->wakeup_mask);

		for (i = 0; i < port->num_counters; i++) {
			seq_printf(s, " Enable  (%d):             %d\n", i,
				   !!(readb_relaxed(port->base + GPIO_COUNTER_CTRL) & (1 << i)));
			seq_printf(s, " Counter (%d):            %02x\n", i,
				   readb_relaxed(port->base + GPIO_COUNTER(i)));
			seq_printf(s, " Compare (%d):            %02x\n", i,
				   readb_relaxed(port->base + GPIO_COMPARE(i)));
			seq_printf(s, " Overflow(%d):             %d\n", i,
				   !!(readb_relaxed(port->base + GPIO_OVERFLOW) & (1 << i)));
		}
	}
#endif
	seq_printf(s, "-----------------------------\n");

	gpiolib_dbg_show(s, gc->gpiodev);
}
#else
#define plum_gpio_dbg_show NULL
#endif

static int plum_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *child;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct plum_gpio *port;
	struct gpio_irq_chip *girq;
	u32 num_counters;
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
	raw_spin_lock_init(&port->lock);

	ret = bgpio_init(&port->gc, dev, 1,
			 port->base + GPIO_STATUS,
			 NULL, NULL, NULL, NULL, BGPIOF_NO_OUTPUT);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}
	port->gc.label = "plum-gpio";
	port->gc.base = -1;
	port->gc.parent = dev;
	port->gc.owner = THIS_MODULE;
#ifdef CONFIG_GPIO_FILTER
	port->gc.set_debounce = plum_gpio_set_debounce;
	port->gc.get_debounce = plum_gpio_get_debounce;
#endif
	port->gc.dbg_show = plum_gpio_dbg_show;

#ifdef CONFIG_GPIO_HWCOUNTER
	ret = of_property_read_u32(np, "num-counters", &num_counters);
	if (!ret) {
		dev_info(dev, "num-counters: %u\n", num_counters);
		port->num_counters = num_counters;

		if (num_counters > 0) {
			port->wakeup_mask = (1 << (port->num_counters)) - 1;
			port->gc.set_hwcounter = plum_gpio_set_hwcounter;
			port->gc.get_hwcounter = plum_gpio_get_hwcounter;
			port->gc.set_hwcounter_enable = plum_gpio_set_hwcounter_enable;
			port->gc.get_hwcounter_enable = plum_gpio_get_hwcounter_enable;
			port->gc.set_wakeup_enable = plum_gpio_set_wakeup_enable;
			port->gc.get_wakeup_enable = plum_gpio_get_wakeup_enable;
		}
	}
#endif

#ifdef CONFIG_GPIO_PLUM_EXPORT_BY_DT
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

	/* Disable, unmask and clear all interrupts */
	writeb(0x00, port->base + GPIO_INT_ENABLE);
	writeb(0xff, port->base + GPIO_INT_STATUS);
	writeb(0x00, port->base + GPIO_FILTER);

	girq = &port->gc.irq;
	girq->chip = &plum_gpio_irqchip;
	girq->parent_handler = plum_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = platform_get_irq(pdev, 0);
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;

	ret = devm_gpiochip_add_data(dev, &port->gc, port);
	if (ret)
		return ret;

#ifdef CONFIG_GPIO_PLUM_EXPORT_BY_DT
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
	dev_info(dev, "plum-gpio @%p registered\n", port->base);

	return 0;
}

static const struct of_device_id plum_gpio_of_match[] = {
	{ .compatible = "plum-gpio", },
	{ .compatible = "plum,ext-DI", },
	{},
};

static struct platform_driver plum_gpio_driver = {
	.driver = {
		.name	= "gpio-plum-DI",
		.of_match_table = of_match_ptr(plum_gpio_of_match),
	},
	.probe	= plum_gpio_probe,
};

static int __init gpio_plum_init(void)
{
	return platform_driver_register(&plum_gpio_driver);
}
postcore_initcall(gpio_plum_init);

MODULE_AUTHOR("Century Systems, Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Century Systems Plum-extio GPIO Driver");
MODULE_LICENSE("GPL");
