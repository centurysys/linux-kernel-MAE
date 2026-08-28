// SPDX-License-Identifier: GPL-2.0
/*
 * Century Systems PLUM extension digital-input GPIO controller.
 *
 * Linux 6.12 port:
 *   - generic GPIO access via bgpio
 *   - GPIO IRQ domain via gpio_irq_chip
 *   - shared parent IRQ demultiplexed by this driver
 *   - debounce via gpio_chip.set_config(PIN_CONFIG_INPUT_DEBOUNCE)
 *   - hardware pulse counters via the Generic Counter subsystem
 *
 * Original driver:
 * Copyright (C) 2018 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 */

#include <linux/bitops.h>
#include <linux/counter.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/spinlock.h>

/* Register offsets */
#define PLUM_GPIO_STATUS		0x00
#define PLUM_GPIO_INT_STATUS		0x04
#define PLUM_GPIO_INT_ENABLE		0x08
#define PLUM_GPIO_EDGE_SEL		0x0c
#define PLUM_GPIO_FILTER		0x10
#define PLUM_GPIO_COUNTER_CTRL		0x12
#define PLUM_GPIO_MATCH_STATUS		0x14
#define PLUM_GPIO_MATCH_ENABLE		0x16
#define PLUM_GPIO_OVERFLOW		0x18
#define PLUM_GPIO_COUNTER(x)		(0x1a + (x) * 2)
#define PLUM_GPIO_COMPARE(x)		(0x22 + (x) * 2)

#define PLUM_GPIO_MAX_LINES		8
#define PLUM_GPIO_MAX_COUNTERS		4

#define PLUM_FILTER_NONE		0
#define PLUM_FILTER_1MS			1
#define PLUM_FILTER_5MS			2
#define PLUM_FILTER_20MS		3

struct plum_gpio;

struct plum_counter_priv {
	struct plum_gpio *gpio;
};

struct plum_gpio {
	struct device *dev;
	struct gpio_chip gc;
	void __iomem *base;
	raw_spinlock_t lock;
	u8 irq_enable;
	u8 both_edges;

	unsigned int num_counters;
	struct counter_device *counter;
	struct counter_count *counter_counts;
};

/* -------------------------------------------------------------------------- */
/* GPIO interrupt controller                                                 */
/* -------------------------------------------------------------------------- */

static void plum_gpio_sync_irq_locked(struct plum_gpio *port)
{
	writeb(port->irq_enable, port->base + PLUM_GPIO_INT_ENABLE);
}

static void plum_gpio_ack_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);

	writeb(BIT(d->hwirq), port->base + PLUM_GPIO_INT_STATUS);
}

static void plum_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&port->lock, flags);
	port->irq_enable &= ~BIT(d->hwirq);
	plum_gpio_sync_irq_locked(port);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void plum_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&port->lock, flags);
	port->irq_enable |= BIT(d->hwirq);
	plum_gpio_sync_irq_locked(port);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static int plum_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned int offset = d->hwirq;
	unsigned long flags;
	u8 edge_sel;
	u8 value;
	int ret = 0;

	if (offset >= gc->ngpio)
		return -EINVAL;

	raw_spin_lock_irqsave(&port->lock, flags);

	edge_sel = readb(port->base + PLUM_GPIO_EDGE_SEL);
	port->both_edges &= ~BIT(offset);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		edge_sel &= ~BIT(offset);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		edge_sel |= BIT(offset);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		/*
		 * The CPLD can select only one edge at a time. Arm the
		 * opposite edge of the current input level and flip it after
		 * every interrupt to emulate both-edge triggering.
		 */
		value = readb(port->base + PLUM_GPIO_STATUS);
		if (value & BIT(offset))
			edge_sel |= BIT(offset);
		else
			edge_sel &= ~BIT(offset);
		port->both_edges |= BIT(offset);
		break;

	case IRQ_TYPE_NONE:
		break;

	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	writeb(edge_sel, port->base + PLUM_GPIO_EDGE_SEL);

out_unlock:
	raw_spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

static void plum_gpio_flip_edge(struct plum_gpio *port, unsigned int offset)
{
	unsigned long flags;
	u8 edge_sel;

	raw_spin_lock_irqsave(&port->lock, flags);
	edge_sel = readb(port->base + PLUM_GPIO_EDGE_SEL);
	edge_sel ^= BIT(offset);
	writeb(edge_sel, port->base + PLUM_GPIO_EDGE_SEL);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static irqreturn_t plum_gpio_irq_handler(int irq, void *data)
{
	struct plum_gpio *port = data;
	struct gpio_chip *gc = &port->gc;
	u8 status;
	u8 enable;
	u8 pending;
	u8 disabled_pending;
	unsigned int offset;

	status = readb(port->base + PLUM_GPIO_INT_STATUS);
	if (!status)
		return IRQ_NONE;

	enable = readb(port->base + PLUM_GPIO_INT_ENABLE);
	pending = status & enable;

	/*
	 * A disabled source must not keep the shared parent IRQ asserted.
	 * Enabled sources are acknowledged by the child irq_chip callback.
	 */
	disabled_pending = status & ~enable;
	if (disabled_pending)
		writeb(disabled_pending, port->base + PLUM_GPIO_INT_STATUS);

	if (!pending)
		return IRQ_NONE;

	while (pending) {
		offset = __ffs(pending);

		if (port->both_edges & BIT(offset))
			plum_gpio_flip_edge(port, offset);

		generic_handle_domain_irq(gc->irq.domain, offset);
		pending &= ~BIT(offset);
	}

	return IRQ_HANDLED;
}

static const struct irq_chip plum_gpio_irqchip = {
	.name = "plum-ext-di",
	.irq_ack = plum_gpio_ack_irq,
	.irq_mask = plum_gpio_mask_irq,
	.irq_unmask = plum_gpio_unmask_irq,
	.irq_set_type = plum_gpio_set_irq_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/* -------------------------------------------------------------------------- */
/* GPIO debounce                                                              */
/* -------------------------------------------------------------------------- */

static int plum_gpio_set_debounce(struct gpio_chip *gc, unsigned int offset,
				  u32 debounce_us)
{
	struct plum_gpio *port = gpiochip_get_data(gc);
	unsigned int group;
	unsigned int shift;
	unsigned int filter;
	unsigned long flags;
	u8 reg;

	if (offset >= gc->ngpio)
		return -EINVAL;

	/* One two-bit filter selection is shared by each group of four lines. */
	group = offset / 4;
	shift = group * 2;

	if (debounce_us == 0)
		filter = PLUM_FILTER_NONE;
	else if (debounce_us <= 1000)
		filter = PLUM_FILTER_1MS;
	else if (debounce_us <= 5000)
		filter = PLUM_FILTER_5MS;
	else if (debounce_us <= 20000)
		filter = PLUM_FILTER_20MS;
	else
		return -EINVAL;

	raw_spin_lock_irqsave(&port->lock, flags);
	reg = readb(port->base + PLUM_GPIO_FILTER);
	reg &= ~(0x3 << shift);
	reg |= filter << shift;
	writeb(reg, port->base + PLUM_GPIO_FILTER);
	raw_spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int plum_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 argument = pinconf_to_config_argument(config);

	if (param != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	return plum_gpio_set_debounce(gc, offset, argument);
}

/* -------------------------------------------------------------------------- */
/* Generic Counter subsystem                                                  */
/* -------------------------------------------------------------------------- */

#if IS_REACHABLE(CONFIG_COUNTER)

static int plum_counter_count_read(struct counter_device *counter,
				   struct counter_count *count, u64 *value)
{
	struct plum_counter_priv *priv = counter_priv(counter);
	struct plum_gpio *port = priv->gpio;
	unsigned int offset = count->id;
	unsigned long flags;
	u8 raw;
	u8 overflow;

	if (offset >= port->num_counters)
		return -EINVAL;

	/*
	 * Preserve the historical PLUM behaviour: the hardware counter is
	 * eight bits wide and GPIO_OVERFLOW latches one wrap. A read can
	 * therefore report one additional overflow bit (0x100). The overflow
	 * latch is cleared once it has been consumed.
	 */
	raw_spin_lock_irqsave(&port->lock, flags);
	raw = readb(port->base + PLUM_GPIO_COUNTER(offset));
	overflow = readb(port->base + PLUM_GPIO_OVERFLOW);

	*value = raw;
	if ((overflow & BIT(offset)) && raw != 0xff) {
		writeb(BIT(offset), port->base + PLUM_GPIO_OVERFLOW);
		*value |= BIT(8);
	}
	raw_spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int plum_counter_count_write(struct counter_device *counter,
				    struct counter_count *count, u64 value)
{
	struct plum_counter_priv *priv = counter_priv(counter);
	struct plum_gpio *port = priv->gpio;
	unsigned int offset = count->id;
	unsigned long flags;

	if (offset >= port->num_counters)
		return -EINVAL;
	if (value > U8_MAX)
		return -ERANGE;

	raw_spin_lock_irqsave(&port->lock, flags);
	writeb(BIT(offset), port->base + PLUM_GPIO_OVERFLOW);
	writeb(value, port->base + PLUM_GPIO_COUNTER(offset));
	raw_spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int plum_counter_function_read(struct counter_device *counter,
				      struct counter_count *count,
				      enum counter_function *function)
{
	*function = COUNTER_FUNCTION_INCREASE;
	return 0;
}

static int plum_counter_enable_read(struct counter_device *counter,
				    struct counter_count *count, u8 *enable)
{
	struct plum_counter_priv *priv = counter_priv(counter);
	struct plum_gpio *port = priv->gpio;
	unsigned int offset = count->id;

	if (offset >= port->num_counters)
		return -EINVAL;

	*enable = !!(readb(port->base + PLUM_GPIO_COUNTER_CTRL) & BIT(offset));
	return 0;
}

static int plum_counter_enable_write(struct counter_device *counter,
				     struct counter_count *count, u8 enable)
{
	struct plum_counter_priv *priv = counter_priv(counter);
	struct plum_gpio *port = priv->gpio;
	unsigned int offset = count->id;
	unsigned long flags;
	u8 reg;

	if (offset >= port->num_counters)
		return -EINVAL;

	raw_spin_lock_irqsave(&port->lock, flags);
	reg = readb(port->base + PLUM_GPIO_COUNTER_CTRL);
	if (enable)
		reg |= BIT(offset);
	else
		reg &= ~BIT(offset);
	writeb(reg, port->base + PLUM_GPIO_COUNTER_CTRL);
	raw_spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static const struct counter_ops plum_counter_ops = {
	.count_read = plum_counter_count_read,
	.count_write = plum_counter_count_write,
	.function_read = plum_counter_function_read,
};

static const enum counter_function plum_counter_functions[] = {
	COUNTER_FUNCTION_INCREASE,
};

static struct counter_comp plum_counter_ext[] = {
	COUNTER_COMP_ENABLE(plum_counter_enable_read,
			    plum_counter_enable_write),
};

static int plum_counter_register(struct plum_gpio *port)
{
	struct device *dev = port->dev;
	struct plum_counter_priv *priv;
	struct counter_device *counter;
	const char *line_name;
	unsigned int i;
	int ret;

	if (!port->num_counters)
		return 0;

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;

	priv = counter_priv(counter);
	priv->gpio = port;

	port->counter_counts = devm_kcalloc(dev, port->num_counters,
					    sizeof(*port->counter_counts),
					    GFP_KERNEL);
	if (!port->counter_counts)
		return -ENOMEM;

	for (i = 0; i < port->num_counters; i++) {
		struct counter_count *count = &port->counter_counts[i];

		count->id = i;
		if (!of_property_read_string_index(dev->of_node, "gpio-line-names", i,
						   &line_name))
			count->name = devm_kasprintf(dev, GFP_KERNEL,
						    "%s counter", line_name);
		else
			count->name = devm_kasprintf(dev, GFP_KERNEL,
						    "DI%u counter", i);
		if (!count->name)
			return -ENOMEM;

		count->functions_list = plum_counter_functions;
		count->num_functions = ARRAY_SIZE(plum_counter_functions);
		count->ext = plum_counter_ext;
		count->num_ext = ARRAY_SIZE(plum_counter_ext);
	}

	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &plum_counter_ops;
	counter->counts = port->counter_counts;
	counter->num_counts = port->num_counters;

	ret = devm_counter_add(dev, counter);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register Counter device\n");

	port->counter = counter;
	return 0;
}

#else

static int plum_counter_register(struct plum_gpio *port)
{
	if (port->num_counters)
		dev_warn(port->dev,
			 "Counter subsystem unavailable; hardware counters disabled\n");

	return 0;
}

#endif

/* -------------------------------------------------------------------------- */
/* Platform driver                                                            */
/* -------------------------------------------------------------------------- */

static int plum_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct plum_gpio *port;
	struct gpio_irq_chip *girq;
	u32 num_counters = 0;
	int irq;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;
	port->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	raw_spin_lock_init(&port->lock);

	ret = bgpio_init(&port->gc, dev, 1,
			 port->base + PLUM_GPIO_STATUS,
			 NULL, NULL, NULL, NULL, BGPIOF_NO_OUTPUT);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize GPIO chip\n");

	port->gc.label = "plum-ext-di";
	port->gc.parent = dev;
	port->gc.owner = THIS_MODULE;
	port->gc.set_config = plum_gpio_set_config;

	/* Disable and clear all GPIO interrupt sources before registration. */
	writeb(0x00, port->base + PLUM_GPIO_INT_ENABLE);
	writeb(0xff, port->base + PLUM_GPIO_INT_STATUS);
	writeb(0x00, port->base + PLUM_GPIO_FILTER);
	port->irq_enable = 0;

	girq = &port->gc.irq;
	gpio_irq_chip_set_chip(girq, &plum_gpio_irqchip);
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;
	/*
	 * Multiple PLUM DI blocks share one physical parent IRQ. The parent is
	 * intentionally not described as a gpio_irq_chip parent; each instance
	 * requests the shared IRQ itself and demultiplexes its own status bits.
	 */
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->parent_handler = NULL;

	ret = devm_gpiochip_add_data(dev, &port->gc, port);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register GPIO chip\n");

	ret = devm_request_irq(dev, irq, plum_gpio_irq_handler, IRQF_SHARED,
			       dev_name(dev), port);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request shared IRQ\n");

	ret = device_property_read_u32(dev, "num-counters", &num_counters);
	if (!ret) {
		if (num_counters > PLUM_GPIO_MAX_COUNTERS)
			return dev_err_probe(dev, -EINVAL,
					     "num-counters must be <= %u\n",
					     PLUM_GPIO_MAX_COUNTERS);
		port->num_counters = num_counters;
	}

	/* Keep hardware counters disabled until explicitly enabled by userspace. */
	if (port->num_counters) {
		writeb(0x00, port->base + PLUM_GPIO_COUNTER_CTRL);
		writeb(GENMASK(port->num_counters - 1, 0),
		       port->base + PLUM_GPIO_OVERFLOW);
	}

	ret = plum_counter_register(port);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, port);

	dev_info(dev, "PLUM extension DI GPIO registered%s\n",
		 port->num_counters ? " with Counter support" : "");

	return 0;
}

static const struct of_device_id plum_gpio_of_match[] = {
	{ .compatible = "plum,ext-DI" },
	{ .compatible = "plum-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, plum_gpio_of_match);

static struct platform_driver plum_gpio_driver = {
	.probe = plum_gpio_probe,
	.driver = {
		.name = "gpio-plum-ext-di",
		.of_match_table = plum_gpio_of_match,
	},
};
module_platform_driver(plum_gpio_driver);

MODULE_AUTHOR("Century Systems, Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Century Systems PLUM extension DI GPIO and Counter driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(COUNTER);
