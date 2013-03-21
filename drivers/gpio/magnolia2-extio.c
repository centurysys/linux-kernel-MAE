/*
 * drivers/gpio/magnolia2-extio.c
 *
 * Century Systems Magnolia2 Ext-IO DIO support
 *
 * Copyright (c) 2010 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 *
 * Based on code originally from:
 *  linux/arch/arm/mach-ep93xx/core.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <asm/gpio.h>

struct extio_gpio_chip {
	struct gpio_chip chip;

	int direction_out:1;
	int counter:1;
	int filter:1;
	int group:3;

	u32 offset;
};

static u16 *base;
static struct resource *resource;

#define to_extio_gpio_chip(c) container_of(c, struct extio_gpio_chip, chip)

static void magnolia2_gpio_set(struct gpio_chip *chip, unsigned offset, int val);

static int magnolia2_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	int status = -EPERM;

	if (magnolia2_chip->direction_out == 0)
		status = 0;

	return status;
}

static int magnolia2_gpio_direction_output(struct gpio_chip *chip,
					   unsigned offset, int value)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	int status = -EPERM;

	if (magnolia2_chip->direction_out == 1) {
		/* OK */
		magnolia2_gpio_set(chip, offset, value);
		status = 0;
	}

	return status;
}

static int magnolia2_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	unsigned long flags;
	u16 *addr, val;
	int status;

	local_irq_save(flags);

	addr = (u16 *)((u32) base + magnolia2_chip->offset);
	val = __raw_readw(addr);
	status = (val >> (offset)) & 1;

	local_irq_restore(flags);

	return status;
}

static void magnolia2_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	unsigned long flags;
	u16 *addr, tmp;

	local_irq_save(flags);

	addr = (u16 *)((u32) base + magnolia2_chip->offset);

	tmp = __raw_readw(addr);

	if (value)
		tmp |= 1 << offset;
	else
		tmp &= ~(1 << offset);

	__raw_writew(tmp, addr);

	local_irq_restore(flags);
}

static void magnolia2_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
//	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
//	u16 data_reg;
//	int i;
}

static int magnolia2_gpio_get_counter(struct gpio_chip *chip, unsigned offset,
				      int *value)
{
	unsigned long flags;
	u16 *addr;

	local_irq_save(flags);

	addr = (u16 *)((u32) base + 0x22 + offset * 2);
	*value = (int) (__raw_readw(addr));

	local_irq_restore(flags);

	return 0;
}

static int magnolia2_gpio_clear_counter(struct gpio_chip *chip, unsigned offset)
{
	unsigned long flags;
	u16 *addr, val;

	local_irq_save(flags);

	addr = (u16 *)((u32) base + 0x20);
	val = (__raw_readw(addr)) | 1 << (offset + 4);
	__raw_writew(val, addr);

	local_irq_restore(flags);

	return 0;
}

static int magnolia2_gpio_ctrl_counter(struct gpio_chip *chip, unsigned offset,
				       int enable)
{
	unsigned long flags;
	u16 *addr, ctrl;

	local_irq_save(flags);

	addr = (u16 *)((u32) base + 0x20);
	ctrl = __raw_readw(addr) & 0x000f;

	if (enable)
		ctrl |= 1 << offset;
	else
		ctrl &= ~(1 << offset);
	__raw_writew(ctrl, addr);

	local_irq_restore(flags);

	return 0;
}

static int magnolia2_gpio_set_filter(struct gpio_chip *chip, int value)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	u16 *addr, tmp;
	unsigned long flags;

	if (!magnolia2_chip->filter)
		return -EIO;

	local_irq_save(flags);

	addr = (u16 *) ((u32) base + 0x18);
	tmp = __raw_readw(addr);

	tmp &= ~(3 << (chip->base * 2));
	tmp |= value << (chip->base * 2);
	__raw_writew(tmp, addr);

	local_irq_restore(flags);

	return 0;
}

static int magnolia2_gpio_get_filter(struct gpio_chip *chip, int *value)
{
	struct extio_gpio_chip *magnolia2_chip = to_extio_gpio_chip(chip);
	u16 *addr, tmp;
	unsigned long flags;

	if (!magnolia2_chip->filter)
		return -EIO;

	local_irq_save(flags);

	addr = (u16 *) ((u32) base + 0x18);
	tmp = __raw_readw(addr);
	*value = (tmp >> (chip->base * 2)) & 0x0003;

	local_irq_restore(flags);

	return 0;
}

static int magnolia2_gpio_set_polarity(struct gpio_chip *chip, unsigned offset, 
				       int val)
{
	return 0;
}

static int magnolia2_gpio_get_polarity(struct gpio_chip *chip, unsigned offset,
				       int *val)
{
	return 0;
}

#define MAGNOLIA2_GPIO_IN(name, _offset, base_gpio, dir_out, _counter, _filter, _group, nums) \
	{								\
		.chip = {						\
			.label		  = (const char *) name,	\
			.direction_input  = magnolia2_gpio_direction_input, \
			.get		  = magnolia2_gpio_get,		\
			.direction_output = magnolia2_gpio_direction_output, \
			.set		  = magnolia2_gpio_set,		\
			.get_counter	  = magnolia2_gpio_get_counter, \
			.ctrl_counter	  = magnolia2_gpio_ctrl_counter, \
			.clear_counter	  = magnolia2_gpio_clear_counter, \
			.set_filter	  = magnolia2_gpio_set_filter,	\
			.get_filter	  = magnolia2_gpio_get_filter,	\
			.set_polarity	  = magnolia2_gpio_set_polarity, \
			.get_polarity	  = magnolia2_gpio_get_polarity, \
			.base		  = base_gpio,			\
			.ngpio		  = nums,			\
		},							\
			.direction_out    = dir_out,			\
				 .counter = _counter,			\
				 .filter  = _filter,			\
				 .group   = _group,			\
				 .offset  = _offset,			\
				 }

#define MAGNOLIA2_GPIO_OUT(name, _offset, base_gpio, dir_out, _counter, _filter, _group, nums) \
	{								\
	 .chip = {							\
			.label		  = (const char *) name,	\
			.direction_input  = NULL,			\
			.get		  = magnolia2_gpio_get,	\
			.direction_output = magnolia2_gpio_direction_output, \
			.set		  = magnolia2_gpio_set,	\
			.get_counter	  = magnolia2_gpio_get_counter, \
			.ctrl_counter	  = magnolia2_gpio_ctrl_counter, \
			.clear_counter	  = magnolia2_gpio_clear_counter, \
			.set_filter	  = magnolia2_gpio_set_filter,	\
			.get_filter	  = magnolia2_gpio_get_filter,	\
			.set_polarity	  = magnolia2_gpio_set_polarity, \
			.get_polarity	  = magnolia2_gpio_get_polarity, \
			.base		  = base_gpio,			\
			.ngpio		  = nums,			\
		},							\
			.direction_out    = dir_out,			\
				 .counter = _counter,			\
				 .filter  = _filter,			\
				 .group   = _group,			\
				 .offset  = _offset,			\
				 }

static struct extio_gpio_chip magnolia2_gpio_banks[] = {
	MAGNOLIA2_GPIO_IN("DinA",  0x08, 0x00, 0, 1, 1, 0,  4),
	MAGNOLIA2_GPIO_IN("DinB",  0x08, 0x04, 0, 0, 1, 1,  4),
	MAGNOLIA2_GPIO_IN("DinC",  0x08, 0x08, 0, 0, 1, 2,  4),
	MAGNOLIA2_GPIO_IN("DinD",  0x08, 0x0c, 0, 0, 1, 3,  4),
	MAGNOLIA2_GPIO_IN("DinE",  0x0a, 0x10, 0, 0, 1, 4,  4),
	MAGNOLIA2_GPIO_IN("DinF",  0x0a, 0x14, 0, 0, 1, 5,  4),
	MAGNOLIA2_GPIO_IN("DinG",  0x0a, 0x18, 0, 0, 1, 6,  4),
	MAGNOLIA2_GPIO_IN("DinH",  0x0a, 0x1c, 0, 0, 1, 7,  4),
	MAGNOLIA2_GPIO_OUT("DoutA", 0x30, 0x20, 1, 0, 0, 0, 16),
	MAGNOLIA2_GPIO_OUT("DoutB", 0x32, 0x30, 1, 0, 0, 1, 16)
};

static int magnolia2_extio_probe(struct platform_device *pdev)
{
	struct resource *res;
	int i, len, ret;

	printk("Magnolia2 AI/DIO Ext-IO driver (DIO)\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto err1;
	}

	len = res->end - res->start + 1;

	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto err1;
	}

	base = (void *) ioremap(res->start, len);

	for (i = 0; i < ARRAY_SIZE(magnolia2_gpio_banks); i++)
		gpiochip_add(&magnolia2_gpio_banks[i].chip);

	printk(" ioaddr: 0x%08x -> 0x%08x (mapped)\n", res->start, (u32) base);

	__raw_writew(0x2000, base);

	resource = res;
	return 0;

err1:
	return ret;
}

static int magnolia2_extio_remove(struct platform_device *pdev)
{
	struct resource *res;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (base) {
		__raw_writew(0xc000, base);
		iounmap(base);

		for (i = 0; i < ARRAY_SIZE(magnolia2_gpio_banks); i++)
			gpiochip_remove(&magnolia2_gpio_banks[i].chip);

		release_mem_region(res->start, res->end - res->start + 1);
	}

	return 0;
}

static struct platform_driver magnolia2_extio_driver = {
	.driver = {
		.name	= "magnolia2_DIO",
		.owner	= THIS_MODULE,
	},
	.probe		= magnolia2_extio_probe,
	.remove	= magnolia2_extio_remove,
};

static int __init magnolia2_extio_init(void)
{
	return platform_driver_register(&magnolia2_extio_driver);
}
module_init(magnolia2_extio_init);

static void magnolia2_extio_exit(void)
{
	platform_driver_unregister(&magnolia2_extio_driver);
}
module_exit(magnolia2_extio_exit);

MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Magnolia2 Ext-IO GPIO");
