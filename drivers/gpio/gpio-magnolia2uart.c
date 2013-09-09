/*
 *  drivers/gpio/gpio-magnolia2uart.c
 *
 * Century Systems Magnolia2 UART PORT2 DIO support
 *
 * Copyright (c) 2013 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 *
 *  Derived from drivers/i2c/chips/pca9539.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>

#include <mach/board-magnolia2.h>
#include <asm/gpio.h>

struct port2dio_chip {
	struct gpio_chip gpio_chip;
	struct magnolia2_gpio_port *port;
};

#define to_port2dio_gpio_chip(c) container_of(c, struct port2dio_chip, gpio_chip)

extern void port2_gpio_active(void);
extern void port2_gpio_inactive(void);

static int port2dio_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct port2dio_chip *magnolia2_chip = to_port2dio_gpio_chip(gc);
	__u32 pin = magnolia2_chip->port->pin;

	gpio_direction_input(pin);

	return 0;
}

static int port2dio_gpio_direction_output(struct gpio_chip *gc,
					  unsigned off, int val)
{
	struct port2dio_chip *magnolia2_chip = to_port2dio_gpio_chip(gc);
	__u32 pin = magnolia2_chip->port->pin;

	gpio_direction_output(pin, (val == 0 ? 0 : 1));

	return 0;
}

static int port2dio_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct port2dio_chip *magnolia2_chip = to_port2dio_gpio_chip(gc);
	__u32 pin = magnolia2_chip->port->pin;

	return gpio_get_value(pin);
}

static void port2dio_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct port2dio_chip *magnolia2_chip = to_port2dio_gpio_chip(gc);
	__u32 pin = magnolia2_chip->port->pin;

	gpio_set_value(pin, (val == 0 ? 0 : 1));
}

extern void magnolia2_uartgpio_init(void);

static int port2dio_probe(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct port2dio_chip *chip;
	int i;

	printk("Magnolia2 UART-PORT2 GPIO driver\n");

	chip = kzalloc(sizeof(struct port2dio_chip) * priv->nr_gpio,
		       GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	for (i = 0; i < priv->nr_gpio; i++) {
		chip[i].gpio_chip.label = priv->ports[i].name;
		chip[i].gpio_chip.direction_input = port2dio_gpio_direction_input;
		chip[i].gpio_chip.direction_output = port2dio_gpio_direction_output;
		chip[i].gpio_chip.get = port2dio_gpio_get_value;
		chip[i].gpio_chip.set = port2dio_gpio_set_value;

		chip[i].gpio_chip.base = 250 + i;
		chip[i].gpio_chip.ngpio = 1;
		chip[i].gpio_chip.can_sleep = 1;
		chip[i].gpio_chip.owner = THIS_MODULE;

		chip[i].port = &priv->ports[i];

		gpiochip_add(&chip[i].gpio_chip);

		gpio_request(chip[i].port->pin, chip[i].port->name);
	}

	magnolia2_uartgpio_init();

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int port2dio_remove(struct platform_device *pdev)
{
	struct magnolia2_gpio_private *priv = pdev->dev.platform_data;
	struct port2dio_chip *chip = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->nr_gpio; i++) {
		gpio_free(chip[i].port->pin);
		gpiochip_remove(&chip[i].gpio_chip);
	}

	platform_set_drvdata(pdev, NULL);
	kfree(chip);

	return 0;
}

static struct platform_driver port2dio_driver = {
	.driver = {
		.name	= "magnolia2_gpio_dio",
		.owner	= THIS_MODULE,
	},
	.probe  = port2dio_probe,
	.remove = port2dio_remove,
};

static int __init port2dio_init(void)
{
	return platform_driver_register(&port2dio_driver);
}
module_init(port2dio_init);

static void __exit port2dio_exit(void)
{
	platform_driver_unregister(&port2dio_driver);
}
module_exit(port2dio_exit);

MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Magnolia2 UART PORT2 GPIO");
