// SPDX-License-Identifier: (GPL-2.0)
/*
 * Microchip PolarFire SoC (MPFS) GPIO controller driver
 *
 * Copyright (c) 2018-2022 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Lewis Hanly <lewis.hanly@microchip.com>
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define MPFS_GPIO_CTRL(i)		(0x4 * (i))
#define MAX_NUM_GPIO			32
#define MPFS_GPIO_EN_INT		3
#define MPFS_GPIO_EN_OUT_BUF		BIT(2)
#define MPFS_GPIO_EN_IN			BIT(1)
#define MPFS_GPIO_EN_OUT		BIT(0)

#define MPFS_GPIO_TYPE_INT_EDGE_BOTH	0x80
#define MPFS_GPIO_TYPE_INT_EDGE_NEG	0x60
#define MPFS_GPIO_TYPE_INT_EDGE_POS	0x40
#define MPFS_GPIO_TYPE_INT_LEVEL_LOW	0x20
#define MPFS_GPIO_TYPE_INT_LEVEL_HIGH	0x00
#define MPFS_GPIO_TYPE_INT_MASK		GENMASK(7, 5)
#define MPFS_IRQ_REG			0x80
#define MPFS_INP_REG			0x84
#define MPFS_OUTP_REG			0x88

struct mpfs_gpio_chip {
	void __iomem *base;
	struct clk *clk;
	raw_spinlock_t lock;
	struct gpio_chip gc;
};

static void mpfs_gpio_assign_bit(void __iomem *addr, unsigned int bit_offset, bool value)
{
	unsigned long reg = readl(addr);

	__assign_bit(bit_offset, &reg, value);
	writel(reg, addr);
}

static int mpfs_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	u32 gpio_cfg;
	unsigned long flags;

	if (gpio_index >= NUM_GPIO)
		return -EINVAL;

	raw_spin_lock_irqsave(&mpfs_gpio->lock, flags);

	gpio_cfg = readl(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));
	gpio_cfg |= MPFS_GPIO_EN_IN;
	gpio_cfg &= ~(MPFS_GPIO_EN_OUT | MPFS_GPIO_EN_OUT_BUF);
	writel(gpio_cfg, mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));

	raw_spin_unlock_irqrestore(&mpfs_gpio->lock, flags);

	return 0;
}

static int mpfs_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio_index, int value)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	u32 gpio_cfg;
	unsigned long flags;

	if (gpio_index >= NUM_GPIO)
		return -EINVAL;

	raw_spin_lock_irqsave(&mpfs_gpio->lock, flags);

	gpio_cfg = readl(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));
	gpio_cfg |= MPFS_GPIO_EN_OUT | MPFS_GPIO_EN_OUT_BUF;
	gpio_cfg &= ~MPFS_GPIO_EN_IN;
	writel(gpio_cfg, mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));

	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_OUTP_REG, gpio_index, value);

	raw_spin_unlock_irqrestore(&mpfs_gpio->lock, flags);

	return 0;
}

static int mpfs_gpio_get_direction(struct gpio_chip *gc,
				   unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	u32 gpio_cfg;

	if (gpio_index >= NUM_GPIO)
		return -EINVAL;

	gpio_cfg = readl(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));

	if (gpio_cfg & MPFS_GPIO_EN_IN)
		return 1;

	return 0;
}

static int mpfs_gpio_get(struct gpio_chip *gc,
			 unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);

	if (gpio_index >= NUM_GPIO)
		return -EINVAL;

	return !!(readl(mpfs_gpio->base + MPFS_INP_REG) & BIT(gpio_index));
}

static void mpfs_gpio_set(struct gpio_chip *gc, unsigned int gpio_index, int value)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	unsigned long flags;

	if (gpio_index >= NUM_GPIO)
		return;

	raw_spin_lock_irqsave(&mpfs_gpio->lock, flags);

	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_OUTP_REG,
			     gpio_index, value);

	raw_spin_unlock_irqrestore(&mpfs_gpio->lock, flags);
}

static int mpfs_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	int gpio_index = irqd_to_hwirq(data);
	u32 interrupt_type;
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	u32 gpio_cfg;
	unsigned long flags;

	if (gpio_index >= NUM_GPIO)
		return -EINVAL;

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		interrupt_type = MPFS_GPIO_TYPE_INT_EDGE_BOTH;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		interrupt_type = MPFS_GPIO_TYPE_INT_EDGE_NEG;
		break;

	case IRQ_TYPE_EDGE_RISING:
		interrupt_type = MPFS_GPIO_TYPE_INT_EDGE_POS;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		interrupt_type = MPFS_GPIO_TYPE_INT_LEVEL_HIGH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
	default:
		interrupt_type = MPFS_GPIO_TYPE_INT_LEVEL_LOW;
		break;
	}

	raw_spin_lock_irqsave(&mpfs_gpio->lock, flags);

	gpio_cfg = readl(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));
	gpio_cfg &= ~MPFS_GPIO_TYPE_INT_MASK;
	gpio_cfg |= interrupt_type;
	writel(gpio_cfg, mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index));

	raw_spin_unlock_irqrestore(&mpfs_gpio->lock, flags);

	return 0;
}

static void mpfs_gpio_irq_enable(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	int gpio_index = irqd_to_hwirq(data) % MAX_NUM_GPIO;

	mpfs_gpio_direction_input(gc, gpio_index);
	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_IRQ_REG, gpio_index, 1);
	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index),
			     MPFS_GPIO_EN_INT, 1);
}

static void mpfs_gpio_irq_disable(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	int gpio_index = irqd_to_hwirq(data) % MAX_NUM_GPIO;

	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_IRQ_REG, gpio_index, 1);
	mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_GPIO_CTRL(gpio_index),
			     MPFS_GPIO_EN_INT, 0);
}

static struct irq_chip mpfs_gpio_irqchip = {
	.name = "mpfs_gpio_irqchip",
	.irq_set_type = mpfs_gpio_irq_set_type,
	.irq_enable = mpfs_gpio_irq_enable,
	.irq_disable = mpfs_gpio_irq_disable,
	.flags = IRQCHIP_MASK_ON_SUSPEND,
};

static irqreturn_t mpfs_gpio_irq_handler(int irq, void *mpfs_gpio_data)
{
	struct mpfs_gpio_chip *mpfs_gpio = mpfs_gpio_data;
	unsigned long status;
	int offset;

	status = readl(mpfs_gpio->base + MPFS_IRQ_REG);

	for_each_set_bit(offset, &status, mpfs_gpio->gc.ngpio) {
		mpfs_gpio_assign_bit(mpfs_gpio->base + MPFS_IRQ_REG, offset, 1);
		generic_handle_irq(irq_find_mapping(mpfs_gpio->gc.irq.domain, offset));
	}
	return IRQ_HANDLED;
}

static int mpfs_gpio_probe(struct platform_device *pdev)
{
	struct clk *clk;
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct mpfs_gpio_chip *mpfs_gpio;
	int i, ret, ngpios;
	struct gpio_irq_chip *irq_c;

	mpfs_gpio = devm_kzalloc(dev, sizeof(*mpfs_gpio), GFP_KERNEL);
	if (!mpfs_gpio)
		return -ENOMEM;

	mpfs_gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mpfs_gpio->base)) {
		dev_err(dev, "failed to allocate device memory\n");
		return PTR_ERR(mpfs_gpio->base);
	}
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk), "failed to get clock\n");

	ret = clk_prepare_enable(clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable clock\n");

	mpfs_gpio->clk = clk;

	raw_spin_lock_init(&mpfs_gpio->lock);

	ngpios = MAX_NUM_GPIO;
	device_property_read_u32(dev, "ngpios", &ngpios);
	if (ngpios > MAX_NUM_GPIO)
		ngpios = MAX_NUM_GPIO;

	mpfs_gpio->gc.direction_input = mpfs_gpio_direction_input;
	mpfs_gpio->gc.direction_output = mpfs_gpio_direction_output;
	mpfs_gpio->gc.get_direction = mpfs_gpio_get_direction;
	mpfs_gpio->gc.get = mpfs_gpio_get;
	mpfs_gpio->gc.set = mpfs_gpio_set;
	mpfs_gpio->gc.base = -1;
	mpfs_gpio->gc.ngpio = ngpio;
	mpfs_gpio->gc.label = dev_name(dev);
	mpfs_gpio->gc.parent = dev;
	mpfs_gpio->gc.owner = THIS_MODULE;

	irq_c = &mpfs_gpio->gc.irq;
	irq_c->chip = &mpfs_gpio_irqchip;
	irq_c->chip->parent_device = dev;
	irq_c->handler = handle_simple_irq;

	ret = devm_irq_alloc_descs(&pdev->dev, -1, 0, ngpio, 0);
	if (ret < 0) {
		dev_err(dev, "failed to allocate descs\n");
		goto cleanup_clock;
	}

	/*
	 * Setup the interrupt handlers. Interrupts can be
	 * direct and/or non-direct mode, based on register value:
	 * GPIO_INTERRUPT_FAB_CR.
	 */
	for (i = 0; i < ngpios; i++) {
		int irq = platform_get_irq_optional(pdev, i);

		if (irq < 0)
			continue;

		ret = devm_request_irq(&pdev->dev, irq,
				       mpfs_gpio_irq_handler,
				       IRQF_SHARED, mpfs_gpio->gc.label, mpfs_gpio);
		if (ret) {
			dev_err(&pdev->dev, "failed to request irq %d: %d\n",
				irq, ret);
			goto cleanup_clock;
		}
	}

	ret = gpiochip_add_data(&mpfs_gpio->gc, mpfs_gpio);
	if (ret)
		goto cleanup_clock;

	platform_set_drvdata(pdev, mpfs_gpio);
	dev_info(dev, "Microchip MPFS GPIO registered %d GPIOs\n", ngpio);

	return 0;

cleanup_clock:
	clk_disable_unprepare(mpfs_gpio->clk);
	return ret;
}

static int mpfs_gpio_remove(struct platform_device *pdev)
{
	struct mpfs_gpio_chip *mpfs_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&mpfs_gpio->gc);
	clk_disable_unprepare(mpfs_gpio->clk);

	return 0;
}

static const struct of_device_id mpfs_gpio_match[] = {
	{ .compatible = "microchip,mpfs-gpio", },
	{ /* end of list */ },
};

static struct platform_driver mpfs_gpio_driver = {
	.probe = mpfs_gpio_probe,
	.driver = {
		.name = "microchip,mpfs-gpio",
		.of_match_table = of_match_ptr(mpfs_gpio_match),
	},
	.remove = mpfs_gpio_remove,
};

builtin_platform_driver(mpfs_gpio_driver);
