/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/of_gpio.h>

#include "of.h"

void morse_of_probe(struct device *dev, struct morse_hw_cfg *cfg,
		    const struct of_device_id *match_table)
{
	struct device_node *np = dev->of_node;
	const struct of_device_id *of_id;
	int gpio_pin;

	if (np) {
		of_id = of_match_node(match_table, np);
		if (of_id) {
			dev_info(dev, "Reading gpio pins configuration from device tree\n");
			gpio_pin = of_get_named_gpio(np, "power-gpios", 0);
			cfg->mm_wake_gpio = (gpio_pin < 0) ?
			    MORSE_WAKEPIN_RPI_GPIO_DEFAULT : gpio_pin;
			gpio_pin = of_get_named_gpio(np, "power-gpios", 1);
			cfg->mm_ps_async_gpio = (gpio_pin < 0) ?
			    MORSE_ASYNC_WAKEUP_FROM_CHIP_RPI_GPIO_DEFAULT : gpio_pin;
			gpio_pin = of_get_named_gpio(np, "reset-gpios", 0);
			cfg->mm_reset_gpio = (gpio_pin < 0) ?
			    MORSE_RESETPIN_RPI_GPIO_DEFAULT : gpio_pin;
			gpio_pin = of_get_named_gpio(np, "spi-irq-gpios", 0);
			cfg->mm_spi_irq_gpio = (gpio_pin < 0) ?
			    MORSE_SPI_HW_IRQ_RPI_GPIO_DEFAULT : gpio_pin;
			return;
		}
		dev_err(dev, "Couldn't match device table\n");
	} else {
		dev_err(dev, "Device node not found\n");
	}
	dev_info(dev, "No pin configs found, using defaults...\n");
	cfg->mm_ps_async_gpio = MORSE_ASYNC_WAKEUP_FROM_CHIP_RPI_GPIO_DEFAULT;
	cfg->mm_wake_gpio = MORSE_WAKEPIN_RPI_GPIO_DEFAULT;
	cfg->mm_reset_gpio = MORSE_RESETPIN_RPI_GPIO_DEFAULT;
}
