/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Century Systems
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/io.h>

#define NGPIO 8

#define REG_ENABLE	0
#define REG_STATUS	2

struct xioirq_gpio {
	struct gpio_chip chip;
	unsigned long register_base;
	int irq;

	const char *gpio_name[NGPIO];
	int gpio_map[NGPIO];
};

static irqreturn_t xio_interrupt(int irq, void *data)
{
	struct xioirq_gpio *gpio = data;
	int handled = 0;
	u8 val;

	/* clear all pending status */
	val = __raw_readb((void __iomem *) gpio->register_base + REG_STATUS);

	if (val != 0) {
		__raw_writeb(val, (void __iomem *) gpio->register_base + REG_STATUS);
		handled = 1;
	}

	return IRQ_RETVAL(handled);
}

static void xioirq_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct xioirq_gpio *gpio = container_of(chip, struct xioirq_gpio, chip);
	int shift;
	u8 val;

	shift = gpio->gpio_map[offset];
	val = __raw_readb((void __iomem *) gpio->register_base + REG_ENABLE);
	val &= ~(1 << shift);
	val |= (value ? 1 : 0) << shift;

	__raw_writeb(val, (void __iomem *) gpio->register_base + REG_ENABLE);
}

static int xioirq_gpio_dir_out(struct gpio_chip *chip, unsigned offset,
			       int value)
{
	xioirq_gpio_set(chip, offset, value);

	return 0;
}

static int xioirq_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct xioirq_gpio *gpio = container_of(chip, struct xioirq_gpio, chip);
	int shift;
	u8 val;

	shift = gpio->gpio_map[offset];
	val = __raw_readb((void __iomem *) gpio->register_base + REG_ENABLE);

	return (val & (1 << shift)) ? 1 : 0;
}

static int xioirq_gpio_probe(struct platform_device *pdev)
{
	struct xioirq_gpio *gpio;
	struct gpio_chip *chip;
	struct resource *res_mem;
	struct device_node *np = pdev->dev.of_node, *child;
	enum of_gpio_flags flags;
	int gpio_irq, gpios = 0;
	int err = 0;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	chip = &gpio->chip;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem == NULL) {
		dev_err(&pdev->dev, "found no memory resource\n");
		err = -ENXIO;
		goto out;
	}
	if (!devm_request_mem_region(&pdev->dev, res_mem->start,
				     resource_size(res_mem),
				     res_mem->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		err = -ENXIO;
		goto out;
	}
	gpio->register_base = (unsigned long) devm_ioremap(&pdev->dev, res_mem->start,
							   resource_size(res_mem));

	gpio->irq = irq_of_parse_and_map(np, 0);

	gpio_irq = of_get_named_gpio_flags(np, "irq-gpio", 0, &flags);
	if (gpio_is_valid(gpio_irq))
		gpio_request(gpio_irq, "gpio-xioirq");

	for_each_child_of_node(np, child) {
		const char *name;
		u32 reg;
		int ret;

		name = of_get_property(child, "label", NULL);
		ret = of_property_read_u32(child, "reg", &reg);

		if (name && ret == 0 && reg >= 0 && reg < NGPIO) {
			gpio->gpio_name[gpios] = name;
			gpio->gpio_map[gpios] = reg;
			gpios++;
		}
	}

	if (gpios == 0) {
		err = -ENXIO;
		goto out;
	}

	pdev->dev.platform_data = chip;
	chip->label = "xioirq-gpio";
	chip->names = gpio->gpio_name;
	chip->dev = &pdev->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->can_sleep = false;
	chip->ngpio = gpios;
	chip->get = xioirq_gpio_get;
	chip->direction_output = xioirq_gpio_dir_out;
	chip->set = xioirq_gpio_set;
	err = gpiochip_add(chip);
	if (err)
		goto out;

	if (request_irq(gpio->irq, xio_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_SHARED,
			"XIOIRQ", gpio) < 0) {
		pr_warn("%s: Can't get IRQ %d (XIO)\n", __FUNCTION__, gpio->irq);
	}

	dev_info(&pdev->dev, "XIOIRQ GPIO driver probed.\n");
out:
	return err;
}

static int xioirq_gpio_remove(struct platform_device *pdev)
{
	struct gpio_chip *chip = pdev->dev.platform_data;
	return gpiochip_remove(chip);
}

static struct of_device_id xioirq_gpio_match[] = {
	{
		.compatible = "plum,xioirq-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, xioirq_gpio_match);

static struct platform_driver xioirq_gpio_driver = {
	.driver = {
		.name		= "xioirq_gpio",
		.owner		= THIS_MODULE,
		.of_match_table = xioirq_gpio_match,
	},
	.probe		= xioirq_gpio_probe,
	.remove	= xioirq_gpio_remove,
};

module_platform_driver(xioirq_gpio_driver);

MODULE_DESCRIPTION("Century Systems XIOIRQ GPIO Driver");
MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_LICENSE("GPL");
