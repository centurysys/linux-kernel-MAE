/*
 * Copyright 2005-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __ASM_ARCH_MXC_BOARD_MA8XX_H__
#define __ASM_ARCH_MXC_BOARD_MA8XX_H__

/*
 * Include Files
 */
#include <mach/mxc_uart.h>

#define MXC_INT_GPIO_P1(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 0 + (x))
#define MXC_INT_GPIO_P2(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 1 + (x))
#define MXC_INT_GPIO_P3(x) MXC_GPIO_TO_IRQ(GPIO_NUM_PIN * 2 + (x))

#ifndef __ASSEMBLY__

struct ma8xx_gpio_port {
	char *name;
	__u32 pin;
	int irq;
	int dir_ro;
};

struct ma8xx_gpio_private {
	int nr_gpio;
	struct ma8xx_gpio_port *ports;
};

extern void (*ma8xx_power_off_prepare)(void);

#endif /* __ASSEMBLY__ */

/*!
 * @defgroup BRDCFG_MX31 Board Configuration Options
 * @ingroup MSL_MX31
 */

/*!
 * @file mach-mx3/board-ma8xx.h
 *
 * @brief This file contains all the board level configuration options.
 *
 * It currently hold the options defined for MX31 ADS Platform.
 *
 * @ingroup BRDCFG_MX31
 */

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
#define UART3_ENABLED           0

/* UART 4 configuration */
#define UART4_MODE              MODE_DTE
#define UART4_IR                NO_IRDA
#define UART4_ENABLED           0	/* Disable UART 4 as its pins are shared with ATA */

/* UART 5 configuration */
#define UART5_MODE              MODE_DTE
#define UART5_IR                NO_IRDA
#define UART5_ENABLED           0

/* for debug-macro.S */
#define MXC_LL_EXTUART_PADDR	(CS4_BASE_ADDR)
#define MXC_LL_EXTUART_VADDR	CS4_IO_ADDRESS(MXC_LL_EXTUART_PADDR)
#undef  MXC_LL_EXTUART_16BIT_BUS

#define MXC_LL_UART_PADDR	UART2_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART2_BASE_ADDR)

/*!
 * External UART A.
 */
#define MA8XX_EXT_UARTA      0x00000000
/*!
 * External UART B.
 */
#define MA8XX_EXT_UARTB      0x00000008


#endif				/* __ASM_ARCH_MXC_BOARD_MA8XX_H__ */
