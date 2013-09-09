/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__
#define __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__

#include <mach/hardware.h>

#define MAGNOLIA2_CTRL_ADDR     0xa8000000
#define MAGNOLIA2_STATUS_ADDR   0xa8000001
#define MAGNOLIA2_LED_ADDR      0xa8000002

#define MAGNOLIA2_EXT_UART_FOMA 0x10
#define MAGNOLIA2_EXT_UART_CAN	0x10
#define MAGNOLIA2_EXT_UART_XBEE 0x10
#define MAGNOLIA2_EXT_UART_DB9  0x10

#define MXC_INT_GPIO_P1(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 0 + (x))
#define MXC_INT_GPIO_P2(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 1 + (x))
#define MXC_INT_GPIO_P3(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 2 + (x))

/* GPIO */
struct magnolia2_gpio_port {
	char *name;
	__u32 pin;
	int irq;
	int dir_ro;
};

struct magnolia2_gpio_private {
	int nr_gpio;
	struct magnolia2_gpio_port *ports;
};

/* LED */
struct magnolia2_led_port {
	char *name;
	int shift;
};

struct magnolia2_led_private {
        int nr_ports;
        struct magnolia2_led_port *ports;
};

extern void (*magnolia2_power_off_prepare)(void);

#endif /* __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__ */
