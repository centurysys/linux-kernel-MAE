/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__
#define __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__

#ifdef CONFIG_MACH_MAGNOLIA2

/*!
 * @defgroup BRDCFG_MX35 Board Configuration Options
 * @ingroup MSL_MX35
 */

/*!
 * @file mach-mx35/board-magnolia2.h
 *
 * @brief This file contains all the board level configuration options.
 *
 * It currently hold the options defined for MX35 3STACK Platform.
 *
 * @ingroup BRDCFG_MX35
 */

/*
 * Include Files
 */
#include <mach/mxc_uart.h>

#define MXC_INT_GPIO_P1(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 0 + (x))
#define MXC_INT_GPIO_P2(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 1 + (x))
#define MXC_INT_GPIO_P3(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 2 + (x))

#ifndef __ASSEMBLY__

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

#endif /* __ASSEMBLY__ */

/*!
 * @name MXC UART EVB board level configurations
 */
/*! @{ */
/*!
 * Specifies if the Irda transmit path is inverting
 */
#define MXC_IRDA_TX_INV         0
/*!
 * Specifies if the Irda receive path is inverting
 */
#define MXC_IRDA_RX_INV         0

/* UART 1 configuration */
/*!
 * This define specifies if the UART port is configured to be in DTE or
 * DCE mode. There exists a define like this for each UART port. Valid
 * values that can be used are \b MODE_DTE or \b MODE_DCE.
 */
#define UART1_MODE              MODE_DTE
/*!
 * This define specifies if the UART is to be used for IRDA. There exists a
 * define like this for each UART port. Valid values that can be used are
 * \b IRDA or \b NO_IRDA.
 */
#define UART1_IR                NO_IRDA
/*!
 * This define is used to enable or disable a particular UART port. If
 * disabled, the UART will not be registered in the file system and the user
 * will not be able to access it. There exists a define like this for each UART
 * port. Specify a value of 1 to enable the UART and 0 to disable it.
 */
#define UART1_ENABLED           1
/*! @} */
/* UART 2 configuration */
#define UART2_MODE              MODE_DTE
#define UART2_IR                NO_IRDA
#define UART2_ENABLED           1

/* UART 3 configuration */
#define UART3_MODE              MODE_DTE
#define UART3_IR                NO_IRDA
#define UART3_ENABLED           1

#define MXC_LL_UART_PADDR	UART3_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART3_BASE_ADDR)

/*! @} */

#define AHB_FREQ                133000000
#define IPG_FREQ                66500000

#define MAGNOLIA2_CTRL_ADDR     0xa8000000
#define MAGNOLIA2_STATUS_ADDR   0xa8000001
#define MAGNOLIA2_LED_ADDR      0xa8000002

#define MAGNOLIA2_EXT_UART_FOMA 0x10
#define MAGNOLIA2_EXT_UART_CAN	0x10

extern void magnolia2_gpio_init(void) __init;
extern void gpio_tsc_active(void);
extern void gpio_tsc_inactive(void);
extern unsigned int sdhc_get_card_det_status(struct device *dev);
extern int sdhc_write_protect(struct device *dev);

/* CPLD functions */
void magnolia2_usbh2_reset(int active);
void magnolia2_eth_phy_reset(int active);
void magnolia2_felica_rw_reset(int active);
void magnolia2_wifi_reset(int active);
void magnolia2_SDcard_power_control(int on);
u8 magnolia2_get_CPLD_revision(void);
u8 magnolia2_get_board_ID(void);

#endif				/* CONFIG_MACH_MAGNOLIA2 */
#endif				/* __ASM_ARCH_MXC_BOARD_MAGNOLIA2_H__ */
