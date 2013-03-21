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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/board-magnolia2.h>
#include "iomux.h"

/*!
 * @file mach-mx35/magnolia2_gpio.c
 *
 * @brief This file contains all the GPIO setup functions for the board.
 *
 * @ingroup GPIO_MX35
 */

/*!
 * This system-wise GPIO function initializes the pins during system startup.
 * All the statically linked device drivers should put the proper GPIO
 * initialization code inside this function. It is called by \b fixup_mx31ads()
 * during system startup. This function is board specific.
 */
void magnolia2_gpio_init(void)
{
	/* config CS5 */
	mxc_request_iomux(MX35_PIN_CS5, MUX_CONFIG_FUNC);

#if 0	/* not mounted. */
	/* configure capture pin for ckil input */
	mxc_request_iomux(MX35_PIN_CAPTURE, MUX_CONFIG_ALT4);
	mxc_iomux_set_pad(MX35_PIN_CAPTURE,
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_100K_PU | PAD_CTL_PUE_PUD);
	mxc_iomux_set_input(MUX_IN_CCM_32K_MUXED, INPUT_CTL_PATH0);
#endif
}

/*!
 * Setup GPIO for a UART port to be active
 *
 * @param  port		a UART port
 * @param  no_irda	indicates if the port is used for SIR
 */
static int mxc_uart_state[] = {0, 0, 0};
extern int magnolia2_get_uart_info(int port, u32 *enable, u32 *type, u32 *config);
void gpio_uart_active(int port, int no_irda)
{
	u32 enable, type, config;

	if (mxc_uart_state[port] == 1)
		// already activated.
		return;

	magnolia2_get_uart_info(port, &enable, &type, &config);

	/*
	 * Configure the IOMUX control registers for the UART signals
	 */
	switch (port) {
	case 0:
		/* UART 1 IOMUX Configs */
		mxc_request_iomux(MX35_PIN_TXD1, MUX_CONFIG_FUNC);	/* TxD */
		mxc_request_iomux(MX35_PIN_RXD1, MUX_CONFIG_FUNC);	/* RxD */

		if (type == 1) {
			/* RS-422 or RS-485 */
			mxc_request_iomux(MX35_PIN_MLB_DAT, MUX_CONFIG_GPIO); /* GPIO3(4) */
			mxc_request_iomux(MX35_PIN_MLB_SIG, MUX_CONFIG_GPIO); /* GPIO3(5) */
			mxc_set_gpio_dataout(MX35_PIN_MLB_DAT, 0);	      /* TxD: disable */
			mxc_set_gpio_dataout(MX35_PIN_MLB_SIG, 1);	      /* RxD: disable */
			mxc_set_gpio_direction(MX35_PIN_MLB_DAT, 0);	      /* GPIO OUT */
			mxc_set_gpio_direction(MX35_PIN_MLB_SIG, 0);	      /* GPIO OUT */
			mxc_iomux_set_input(MUX_IN_GPIO3_IN_4, INPUT_CTL_PATH1);
			mxc_iomux_set_input(MUX_IN_GPIO3_IN_5, INPUT_CTL_PATH1);

			mxc_iomux_set_pad(MX35_PIN_MLB_DAT,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_MLB_SIG,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
		} else {
			/* RS-232 */
			mxc_request_iomux(MX35_PIN_RTS1, MUX_CONFIG_FUNC);	/* RTS */
			mxc_request_iomux(MX35_PIN_CTS1, MUX_CONFIG_FUNC);	/* CTS */
			mxc_request_iomux(MX35_PIN_ATA_DATA6, MUX_CONFIG_ALT2); /* DTR */
#ifndef CONFIG_MXC_UART_DSR_GPIO
			mxc_request_iomux(MX35_PIN_ATA_DATA7, MUX_CONFIG_ALT2); /* DSR */
#else
			mxc_request_iomux(MX35_PIN_ATA_DATA7, MUX_CONFIG_GPIO); /* DSR(GPIO) */
			mxc_set_gpio_direction(MX35_PIN_ATA_DATA7, 1);		/* GPIO IN */
#endif
			mxc_request_iomux(MX35_PIN_ATA_DATA8, MUX_CONFIG_ALT2); /* RI  */
			mxc_request_iomux(MX35_PIN_ATA_DATA9, MUX_CONFIG_ALT2); /* DCD */
		}

		mxc_iomux_set_pad(MX35_PIN_TXD1,
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
		mxc_iomux_set_pad(MX35_PIN_RXD1,
				  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);

		if (type == 0) {
			mxc_iomux_set_pad(MX35_PIN_RTS1,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_CTS1,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_ATA_DATA6,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_ATA_DATA7,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_ATA_DATA8,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_ATA_DATA9,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
		}

		break;

	case 1:
		/* UART 2 IOMUX Configs */
		mxc_request_iomux(MX35_PIN_TXD2, MUX_CONFIG_FUNC);	/* TxD */
		mxc_request_iomux(MX35_PIN_RXD2, MUX_CONFIG_FUNC);	/* RxD */

		if (type == 1) {
			/* RS-422 or RS-485 */
			mxc_request_iomux(MX35_PIN_CTS2, MUX_CONFIG_GPIO);	/* TXEX */
			mxc_request_iomux(MX35_PIN_RTS2, MUX_CONFIG_GPIO);	/* nRXEN */
			mxc_set_gpio_dataout(MX35_PIN_CTS2, 0);		/* TxD: disable */
			mxc_set_gpio_dataout(MX35_PIN_RTS2, 1);		/* RxD: disable */
			mxc_set_gpio_direction(MX35_PIN_CTS2, 0);		/* GPIO OUT */
			mxc_set_gpio_direction(MX35_PIN_RTS2, 0);		/* GPIO OUT */

			mxc_iomux_set_pad(MX35_PIN_CTS2,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_RTS2,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
		} else {
			/* RS-232 */
			mxc_request_iomux(MX35_PIN_RTS2, MUX_CONFIG_FUNC);	/* RTS */
			mxc_request_iomux(MX35_PIN_CTS2, MUX_CONFIG_FUNC);	/* CTS */
			mxc_request_iomux(MX35_PIN_TX5_RX0, MUX_CONFIG_ALT4);	/* DTR */
#ifndef CONFIG_MXC_UART_DSR_GPIO
			mxc_request_iomux(MX35_PIN_TX4_RX1, MUX_CONFIG_ALT4);	/* DSR */
#else
			mxc_request_iomux(MX35_PIN_TX4_RX1, MUX_CONFIG_GPIO);	/* DSR(GPIO) */
			mxc_set_gpio_direction(MX35_PIN_TX4_RX1, 1);		/* GPIO IN */
#endif
			mxc_request_iomux(MX35_PIN_TX1, MUX_CONFIG_ALT4);	/* RI  */
			mxc_request_iomux(MX35_PIN_TX0, MUX_CONFIG_ALT4);	/* DCD */
		}

		mxc_iomux_set_pad(MX35_PIN_TXD2,
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
		mxc_iomux_set_pad(MX35_PIN_RXD2,
				  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);

		if (type == 0) {
			mxc_iomux_set_pad(MX35_PIN_RTS2,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_CTS2,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_TX5_RX0,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_TX4_RX1,
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
			mxc_iomux_set_pad(MX35_PIN_TX1,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
			mxc_iomux_set_pad(MX35_PIN_TX0,
					  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
					  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
		}

		break;

	case 2:
		/* UART 3 IOMUX Configs */
		mxc_request_iomux(MX35_PIN_ATA_DATA11, MUX_CONFIG_ALT1); /* TxD */
		mxc_request_iomux(MX35_PIN_ATA_DATA10, MUX_CONFIG_ALT1); /* RxD */

		mxc_iomux_set_pad(MX35_PIN_ATA_DATA11,
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PD);
		mxc_iomux_set_pad(MX35_PIN_ATA_DATA10,
				  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
				  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);

		mxc_iomux_set_input(MUX_IN_UART3_UART_RXD_MUX, INPUT_CTL_PATH2);

		break;

	default:
		break;
	}

	mxc_uart_state[port] = 1;
}

EXPORT_SYMBOL(gpio_uart_active);

/*!
 * Setup GPIO for a UART port to be inactive
 *
 * @param  port		a UART port
 * @param  no_irda	indicates if the port is used for SIR
 */
void gpio_uart_inactive(int port, int no_irda)
{
	u32 enable, type, config;

//	if (mxc_uart_state[port] == 0)
		// already inactivated.
		return;

	magnolia2_get_uart_info(port, &enable, &type, &config);

	switch (port) {
	case 0:
		mxc_request_gpio(MX35_PIN_TXD1);
		mxc_request_gpio(MX35_PIN_RXD1);

		if (type == 1) {
			/* RS-422 or RS-485 */
			mxc_set_gpio_dataout(MX35_PIN_MLB_DAT, 0); /* TxD: disable */
			mxc_set_gpio_dataout(MX35_PIN_MLB_SIG, 1); /* RxD: disable */
			mxc_free_gpio(MX35_PIN_MLB_DAT); /* GPIO3(4) */
			mxc_free_gpio(MX35_PIN_MLB_SIG); /* GPIO3(5) */
		} else {
			mxc_request_gpio(MX35_PIN_RTS1);
			mxc_request_gpio(MX35_PIN_CTS1);
			mxc_request_gpio(MX35_PIN_ATA_DATA6);
#ifndef CONFIG_MXC_UART_DSR_GPIO
			mxc_request_gpio(MX35_PIN_ATA_DATA7);
#endif
			mxc_request_gpio(MX35_PIN_ATA_DATA8);
			mxc_request_gpio(MX35_PIN_ATA_DATA9);

			mxc_free_iomux(MX35_PIN_TXD1, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_RXD1, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_RTS1, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_CTS1, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_ATA_DATA6, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_ATA_DATA7, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_ATA_DATA8, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_ATA_DATA9, MUX_CONFIG_GPIO);
		}

		break;

	case 1:
		mxc_request_gpio(MX35_PIN_TXD2);
		mxc_request_gpio(MX35_PIN_RXD2);

		if (type == 1) {
			/* RS-422 or RS-485 */
			mxc_set_gpio_dataout(MX35_PIN_CTS2, 0); /* TxD: disable */
			mxc_set_gpio_dataout(MX35_PIN_RTS2, 1); /* RxD: disable */
			mxc_free_gpio(MX35_PIN_CTS2); /* GPIO3(4) */
			mxc_free_gpio(MX35_PIN_RTS2); /* GPIO3(5) */
		} else {
			mxc_request_gpio(MX35_PIN_RTS2);
			mxc_request_gpio(MX35_PIN_CTS2);
			mxc_request_gpio(MX35_PIN_TX5_RX0);
#ifndef CONFIG_MXC_UART_DSR_GPIO
			mxc_request_gpio(MX35_PIN_TX4_RX1);
#endif
			mxc_request_gpio(MX35_PIN_TX1);
			mxc_request_gpio(MX35_PIN_TX0);

			mxc_free_iomux(MX35_PIN_TXD2, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_RXD2, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_RTS2, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_CTS2, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_TX5_RX0, MUX_CONFIG_GPIO);
#ifndef CONFIG_MXC_UART_DSR_GPIO
			mxc_free_iomux(MX35_PIN_TX4_RX1, MUX_CONFIG_GPIO);
#else
			mxc_free_gpio(MX35_PIN_TX4_RX1);
#endif
			mxc_free_iomux(MX35_PIN_TX1, MUX_CONFIG_GPIO);
			mxc_free_iomux(MX35_PIN_TX0, MUX_CONFIG_GPIO);
		}

		break;

	case 2:
		mxc_request_gpio(MX35_PIN_ATA_DATA11);
		mxc_request_gpio(MX35_PIN_ATA_DATA10);

		mxc_free_iomux(MX35_PIN_ATA_DATA11, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_ATA_DATA10, MUX_CONFIG_GPIO);

		mxc_iomux_set_input(MUX_IN_UART3_UART_RXD_MUX, INPUT_CTL_PATH0);

		break;

	default:
		break;
	}

	mxc_uart_state[port] = 0;
}
EXPORT_SYMBOL(gpio_uart_inactive);

static inline void mxc_uart_control_tx(uart_mxc_port *umxc, int enable)
{
	mxc_set_gpio_dataout(umxc->TxEnable, enable);
}

static inline void mxc_uart_control_rx(uart_mxc_port *umxc, int enable)
{
	mxc_set_gpio_dataout(umxc->RxEnable, !enable);
}

void mxc_uart_control_txrx(struct uart_port *port, unsigned int mctrl)
{
	uart_mxc_port *umxc = (uart_mxc_port *) port;
	int line = umxc->port.line;

	if (mxc_uart_state[line] == 0)
		/* not enabled */
		return;

	if (umxc->driver_type == 1 && umxc->driver_duplex == 0) {
		/* RS-485 only */
		int txe, rxe;
		unsigned long flags;

		/* Control TxEN */
		if (mctrl & TIOCM_OUT1)
			txe = 1;	/* Enable Tx */
		else
			txe = 0;	/* Disable Tx */

		/* Control RxEN */
		if (mctrl & TIOCM_OUT2)
			rxe = 0;	/* Disable Rx */
		else
			rxe = 1;	/* Enable Rx */

		spin_lock_irqsave(&port->lock, flags);
		if (txe == 0 && !port->ops->tx_empty(port)) {
			umxc->txrx_pending = 1;
			umxc->txe = txe;
			umxc->rxe = rxe;
			if (umxc->tx_available == 0) {
				volatile unsigned int cr;

				/* Enable Transmit complete intr */
				cr = readl(umxc->port.membase + MXC_UARTUCR4);
				cr |= MXC_UARTUCR4_TCEN;
				writel(cr, umxc->port.membase + MXC_UARTUCR4);
			}
		} else {
			umxc->txrx_pending = 0;
			mxc_uart_control_tx(umxc, txe);
			mxc_uart_control_rx(umxc, rxe);
		}
		spin_unlock_irqrestore(&port->lock, flags);
	}
}
EXPORT_SYMBOL(mxc_uart_control_txrx);

/*!
 * Configure the IOMUX GPR register to receive shared SDMA UART events
 *
 * @param  port		a UART port
 */
void config_uartdma_event(int port)
{
}

EXPORT_SYMBOL(config_uartdma_event);

int magnolia2_get_fec_int(void)
{
	return mxc_get_gpio_datain(MX35_PIN_ATA_DA0);
}
EXPORT_SYMBOL(magnolia2_get_fec_int);

static int fec_initialized = 0;

void gpio_fec_active(void)
{
	if (fec_initialized == 1)
		return;

	mxc_request_iomux(MX35_PIN_FEC_TX_CLK, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_CLK, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA0, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA1, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA2, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA3, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA0, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA1, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA2, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA3, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TX_EN, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_DV, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_CRS, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_COL, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TX_ERR, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_ERR, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_MDC, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_MDIO, MUX_CONFIG_FUNC);

	mxc_request_iomux(MX35_PIN_ATA_DA0, MUX_CONFIG_GPIO);
	mxc_set_gpio_direction(MX35_PIN_ATA_DA0, 1);
	mxc_iomux_set_input(MUX_IN_GPIO3_IN_0, INPUT_CTL_PATH1);

#define FEC_PAD_CTL_COMMON (PAD_CTL_DRV_3_3V|PAD_CTL_PUE_PUD| \
			PAD_CTL_ODE_CMOS|PAD_CTL_DRV_NORMAL|PAD_CTL_SRE_SLOW)
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_CLK, FEC_PAD_CTL_COMMON |
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_CLK,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_DV,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_COL,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA0,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA0,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_EN,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_MDC,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_MDIO,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_22K_PU);
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_ERR,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_ERR,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_CRS,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA1,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA1,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA2,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA2,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA3,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA3,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
#undef FEC_PAD_CTL_COMMON

	/* FEC PHY reset */
	magnolia2_eth_phy_reset(0);
	msleep(10);
	magnolia2_eth_phy_reset(1);
	msleep(100);

	fec_initialized = 1;
}

EXPORT_SYMBOL(gpio_fec_active);

void gpio_fec_inactive(void)
{
#if 0
	mxc_request_gpio(MX35_PIN_FEC_TX_CLK);
	mxc_request_gpio(MX35_PIN_FEC_RX_CLK);
	mxc_request_gpio(MX35_PIN_FEC_RDATA0);
	mxc_request_gpio(MX35_PIN_FEC_RDATA1);
	mxc_request_gpio(MX35_PIN_FEC_RDATA2);
	mxc_request_gpio(MX35_PIN_FEC_RDATA3);
	mxc_request_gpio(MX35_PIN_FEC_TDATA0);
	mxc_request_gpio(MX35_PIN_FEC_TDATA1);
	mxc_request_gpio(MX35_PIN_FEC_TDATA2);
	mxc_request_gpio(MX35_PIN_FEC_TDATA3);
	mxc_request_gpio(MX35_PIN_FEC_TX_EN);
	mxc_request_gpio(MX35_PIN_FEC_RX_DV);
	mxc_request_gpio(MX35_PIN_FEC_CRS);
	mxc_request_gpio(MX35_PIN_FEC_COL);
	mxc_request_gpio(MX35_PIN_FEC_TX_ERR);
	mxc_request_gpio(MX35_PIN_FEC_RX_ERR);
	mxc_request_gpio(MX35_PIN_FEC_MDC);
	mxc_request_gpio(MX35_PIN_FEC_MDIO);

	//mxc_free_iomux(MX35_PIN_ATA_DA0, MUX_CONFIG_GPIO);

	mxc_free_iomux(MX35_PIN_FEC_TX_CLK, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RX_CLK, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RDATA0, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RDATA1, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RDATA2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RDATA3, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TDATA0, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TDATA1, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TDATA2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TDATA3, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TX_EN, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RX_DV, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_CRS, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_COL, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_TX_ERR, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_RX_ERR, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_MDC, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_FEC_MDIO, MUX_CONFIG_GPIO);
#endif
}

EXPORT_SYMBOL(gpio_fec_inactive);

/*!
 * Setup GPIO for an I2C device to be active
 *
 * @param  i2c_num	   an I2C device
 */
void gpio_i2c_active(int i2c_num)
{
#define PAD_CONFIG (PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD | PAD_CTL_ODE_OpenDrain)
	switch (i2c_num) {
	case 0:
		mxc_request_iomux(MX35_PIN_I2C1_CLK, MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_I2C1_DAT, MUX_CONFIG_SION);

		mxc_iomux_set_pad(MX35_PIN_I2C1_CLK, PAD_CONFIG);
		mxc_iomux_set_pad(MX35_PIN_I2C1_DAT, PAD_CONFIG);

		break;

	case 1:
		mxc_request_iomux(MX35_PIN_I2C2_CLK, MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_I2C2_DAT, MUX_CONFIG_SION);

		mxc_iomux_set_pad(MX35_PIN_I2C2_CLK, PAD_CONFIG);
		mxc_iomux_set_pad(MX35_PIN_I2C2_DAT, PAD_CONFIG);

		break;

	case 2:
		mxc_request_iomux(MX35_PIN_TX3_RX2, MUX_CONFIG_ALT1);
		mxc_request_iomux(MX35_PIN_TX2_RX3, MUX_CONFIG_ALT1);

		mxc_iomux_set_pad(MX35_PIN_TX3_RX2, PAD_CONFIG);
		mxc_iomux_set_pad(MX35_PIN_TX2_RX3, PAD_CONFIG);

		break;

	default:
		break;
	}

#undef PAD_CONFIG

}

EXPORT_SYMBOL(gpio_i2c_active);

/*!
 * Setup GPIO for an I2C device to be inactive
 *
 * @param  i2c_num	   an I2C device
 */
void gpio_i2c_inactive(int i2c_num)
{
	switch (i2c_num) {
	case 0:
		break;

	case 1:
		break;

	case 2:
		mxc_request_iomux(MX35_PIN_TX3_RX2, MUX_CONFIG_GPIO);
		mxc_request_iomux(MX35_PIN_TX2_RX3, MUX_CONFIG_GPIO);

		break;

	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_i2c_inactive);

/*!
 * Setup GPIO for a CSPI device to be active
 *
 * @param  cspi_mod	    an CSPI device
 */
void gpio_spi_active(int cspi_mod)
{
	unsigned int pad_val;

	switch (cspi_mod) {
	case 0:
		/* SPI1 : eXternal IO Board */
		mxc_request_iomux(MX35_PIN_CSPI1_MOSI, MUX_CONFIG_FUNC);	/* MOSI */
		mxc_request_iomux(MX35_PIN_CSPI1_MISO, MUX_CONFIG_FUNC);	/* MISO */
		mxc_request_iomux(MX35_PIN_CSPI1_SCLK, MUX_CONFIG_FUNC);	/* SCLK */
		mxc_request_iomux(MX35_PIN_CSPI1_SPI_RDY, MUX_CONFIG_FUNC);	/* RDY	*/
		mxc_request_iomux(MX35_PIN_CSPI1_SS0, MUX_CONFIG_FUNC);		/* SS0	*/
		mxc_request_iomux(MX35_PIN_CSPI1_SS1, MUX_CONFIG_FUNC);		/* SS1	*/
		mxc_request_iomux(MX35_PIN_GPIO1_1, MUX_CONFIG_ALT3);		/* SS2	*/
		mxc_request_iomux(MX35_PIN_ATA_CS0, MUX_CONFIG_ALT1);		/* SS3	*/

		pad_val = PAD_CTL_DRV_3_3V | PAD_CTL_HYS_SCHMITZ |
			PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
#ifdef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_SRE_SLOW |
#endif
			PAD_CTL_100K_PD | PAD_CTL_DRV_NORMAL;
		mxc_iomux_set_pad(MX35_PIN_CSPI1_MOSI, pad_val);
		mxc_iomux_set_pad(MX35_PIN_CSPI1_MISO, pad_val);
		mxc_iomux_set_pad(MX35_PIN_CSPI1_SCLK, pad_val);

		mxc_iomux_set_pad(MX35_PIN_CSPI1_SPI_RDY,
				  PAD_CTL_DRV_3_3V | PAD_CTL_HYS_SCHMITZ |
				  PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
				  PAD_CTL_100K_PU | PAD_CTL_DRV_NORMAL);

		pad_val = PAD_CTL_DRV_3_3V | PAD_CTL_HYS_SCHMITZ |
			PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
			PAD_CTL_100K_PU | PAD_CTL_ODE_CMOS |
#ifdef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_SRE_SLOW |
#endif
			PAD_CTL_DRV_NORMAL;
		mxc_iomux_set_pad(MX35_PIN_CSPI1_SS0, pad_val);
		mxc_iomux_set_pad(MX35_PIN_CSPI1_SS1, pad_val);
		mxc_iomux_set_pad(MX35_PIN_GPIO1_1, pad_val);
		mxc_iomux_set_pad(MX35_PIN_ATA_CS0, pad_val);

		/* SW_SELECT_INPUT */
		mxc_iomux_set_input(MUX_IN_CSPI1_SS2_B, INPUT_CTL_PATH0);
		mxc_iomux_set_input(MUX_IN_CSPI1_SS3_B, INPUT_CTL_PATH1);

		break;
	case 1:
		/* SPI2 */
		break;

	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_spi_active);

/*!
 * Setup GPIO for a CSPI device to be inactive
 *
 * @param  cspi_mod	    a CSPI device
 */
void gpio_spi_inactive(int cspi_mod)
{
	switch (cspi_mod) {
	case 0:
		/* SPI1 */
		mxc_request_gpio(MX35_PIN_CSPI1_MOSI);
		mxc_request_gpio(MX35_PIN_CSPI1_MISO);
		mxc_request_gpio(MX35_PIN_CSPI1_SCLK);
		mxc_request_gpio(MX35_PIN_CSPI1_SPI_RDY);
		mxc_request_gpio(MX35_PIN_CSPI1_SS0);
		mxc_request_gpio(MX35_PIN_CSPI1_SS1);
		mxc_request_gpio(MX35_PIN_GPIO1_1);
		mxc_request_gpio(MX35_PIN_ATA_CS0);

		mxc_free_iomux(MX35_PIN_CSPI1_MOSI, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_CSPI1_MISO, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_CSPI1_SCLK, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_CSPI1_SPI_RDY, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_CSPI1_SS0, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_CSPI1_SS1, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_GPIO1_1, MUX_CONFIG_GPIO);
		mxc_free_iomux(MX35_PIN_ATA_CS0, MUX_CONFIG_GPIO);

		mxc_iomux_set_input(MUX_IN_CSPI1_SS3_B, INPUT_CTL_PATH0);

		break;

	case 1:
		/* SPI2 */
		break;

	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_spi_inactive);

/*!
 * Setup GPIO for LCD to be active
 */
void gpio_lcd_active(void)
{
	mxc_request_iomux(MX35_PIN_LD0, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD1, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD2, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD3, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD4, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD5, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD6, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD7, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD8, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD9, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD10, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD11, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD12, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD13, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD14, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD15, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD16, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_LD17, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_D3_VSYNC, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_D3_HSYNC, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_D3_FPSHIFT, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_D3_DRDY, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_CONTRAST, MUX_CONFIG_FUNC);
}

EXPORT_SYMBOL(gpio_lcd_active);

/*!
 * Setup GPIO for LCD to be inactive
 */
void gpio_lcd_inactive(void)
{
}

EXPORT_SYMBOL(gpio_lcd_inactive);

/*!
 * Setup GPIO for SDHC to be active
 *
 * @param module SDHC module number
 */
void gpio_sdhc_active(int module)
{
	unsigned int pad_val;

	switch (module) {
	case 0:
		/* eSDHCv2-1 : SD card slot */
		mxc_request_iomux(MX35_PIN_SD1_CLK,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_SD1_CMD,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_SD1_DATA0,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_SD1_DATA1,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_SD1_DATA2,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_request_iomux(MX35_PIN_SD1_DATA3,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);

		mxc_request_iomux(MX35_PIN_NFCE_B,
				  MUX_CONFIG_GPIO);	/* GPIO1_22 : WriteProtect   */
		mxc_request_iomux(MX35_PIN_CSI_MCLK,
				  MUX_CONFIG_GPIO);	/* GPIO1_28 : Card Detection */

		mxc_set_gpio_direction(MX35_PIN_NFCE_B, 1);
		mxc_set_gpio_direction(MX35_PIN_CSI_MCLK, 1);

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
#else
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_HIGH |
#endif
			PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
		mxc_iomux_set_pad(MX35_PIN_SD1_CMD, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA0, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA1, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA2, pad_val);

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_DRV_MAX | PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
#else
			PAD_CTL_DRV_HIGH | PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
#endif
		mxc_iomux_set_pad(MX35_PIN_SD1_CLK, pad_val);

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
#else
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_HIGH |
#endif
			PAD_CTL_100K_PU | PAD_CTL_SRE_FAST;
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA3, pad_val);

		break;

	case 2:
		/* eSDHCv2-3 : SDIO WiFi (optional) */
		mxc_request_iomux(MX35_PIN_LD19,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* CLK */
		mxc_request_iomux(MX35_PIN_LD18,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* CMD */
		mxc_request_iomux(MX35_PIN_LD20,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* DAT0 */
		mxc_request_iomux(MX35_PIN_LD21,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* DAT1 */
		mxc_request_iomux(MX35_PIN_LD22,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* DAT2 */
		mxc_request_iomux(MX35_PIN_LD23,
				  MUX_CONFIG_ALT3 | MUX_CONFIG_SION);	/* DAT3 */

		mxc_request_iomux(MX35_PIN_GPIO1_0,
				  MUX_CONFIG_FUNC);	/* GPIO1_0 : WriteProtect   */
		mxc_request_iomux(MX35_PIN_COMPARE,
				  MUX_CONFIG_GPIO);	/* GPIO1_5 : Card Detection */

		mxc_set_gpio_direction(MX35_PIN_GPIO1_0, 1);
		mxc_set_gpio_direction(MX35_PIN_COMPARE, 1);

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX | PAD_CTL_47K_PU |
#else
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_NORMAL | PAD_CTL_100K_PU |
#endif
		PAD_CTL_SRE_FAST;

		mxc_iomux_set_pad(MX35_PIN_LD18, pad_val);	/* CMD	*/
		mxc_iomux_set_pad(MX35_PIN_LD20, pad_val);	/* DAT0 */
		mxc_iomux_set_pad(MX35_PIN_LD21, pad_val);	/* DAT1 */
		mxc_iomux_set_pad(MX35_PIN_LD22, pad_val);	/* DAT2 */

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_DRV_MAX | PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
#else
			PAD_CTL_DRV_NORMAL | PAD_CTL_100K_PU | PAD_CTL_SRE_FAST;
#endif
		mxc_iomux_set_pad(MX35_PIN_LD19, pad_val);	/* CLK	*/

		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
#else
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_NORMAL |
#endif
			PAD_CTL_100K_PU | PAD_CTL_SRE_FAST;

		mxc_iomux_set_pad(MX35_PIN_LD23, pad_val);	/* DAT3 */

		break;

	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_sdhc_active);

/*!
 * Setup GPIO for SDHC1 to be inactive
 *
 * @param module SDHC module number
 */
void gpio_sdhc_inactive(int module)
{
	unsigned int pad_val;

	switch (module) {
	case 0:
		mxc_free_iomux(MX35_PIN_SD1_CLK,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_free_iomux(MX35_PIN_SD1_CMD,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_free_iomux(MX35_PIN_SD1_DATA0,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_free_iomux(MX35_PIN_SD1_DATA1,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_free_iomux(MX35_PIN_SD1_DATA2,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);
		mxc_free_iomux(MX35_PIN_SD1_DATA3,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);

		pad_val = PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW;
		mxc_iomux_set_pad(MX35_PIN_SD1_CLK, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_CMD, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA0, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA1, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA2, pad_val);
		mxc_iomux_set_pad(MX35_PIN_SD1_DATA3, pad_val);

		mxc_free_gpio(MX35_PIN_NFCE_B);	     /* GPIO1_22 : WriteProtect	  */
		mxc_free_gpio(MX35_PIN_CSI_MCLK);    /* GPIO1_28 : Card Detection */

		break;

	case 2:
		mxc_free_iomux(MX35_PIN_LD19,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* CLK */
		mxc_free_iomux(MX35_PIN_LD18,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* CMD */
		mxc_free_iomux(MX35_PIN_LD20,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* DAT0 */
		mxc_free_iomux(MX35_PIN_LD21,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* DAT1 */
		mxc_free_iomux(MX35_PIN_LD22,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* DAT2 */
		mxc_free_iomux(MX35_PIN_LD23,
			       MUX_CONFIG_FUNC | MUX_CONFIG_SION);   /* DAT3 */

		pad_val = PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW;
		mxc_iomux_set_pad(MX35_PIN_LD19, pad_val);
		mxc_iomux_set_pad(MX35_PIN_LD18, pad_val);
		mxc_iomux_set_pad(MX35_PIN_LD20, pad_val);
		mxc_iomux_set_pad(MX35_PIN_LD21, pad_val);
		mxc_iomux_set_pad(MX35_PIN_LD22, pad_val);
		mxc_iomux_set_pad(MX35_PIN_LD23, pad_val);

		break;

	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_sdhc_inactive);

/*
 * Probe for the card. If present the GPIO data would be set.
 */
unsigned int sdhc_get_card_det_status(struct device *dev)
{
	unsigned int ret;
	int tmp, id;
	iomux_pin_name_t pin;

	id = to_platform_device(dev)->id;

	switch (id) {
	case 0:
		pin = MX35_PIN_CSI_MCLK;
		break;

	case 2:
		pin = MX35_PIN_COMPARE;
		break;

	default:
		return 0;
	}

	tmp = mxc_get_gpio_datain(pin);

	if (tmp == -1) {
		printk(KERN_ERR "Get cd status error.");
		ret = 1;
	} else
		ret = (unsigned int) tmp;

	return ret;
}

EXPORT_SYMBOL(sdhc_get_card_det_status);

/*!
 * Get pin value to detect write protection
 */
int sdhc_write_protect(struct device *dev)
{
	unsigned int rc = 0;
	int tmp;

	if (to_platform_device(dev)->id == 0) {
		tmp = mxc_get_gpio_datain(MX35_PIN_NFCE_B);

		if (tmp == -1)
			printk(KERN_ERR "Get wp status error.");
		else
			rc = (unsigned int) tmp;
	}

	return rc;
}

EXPORT_SYMBOL(sdhc_write_protect);

/*
 *  USB Host2
 */
extern void magnolia2_usbh2_phy_reset(void);
int gpio_usbh2_active(void)
{
	unsigned int pad_val;

	mxc_request_iomux(MX35_PIN_GPIO3_0, MUX_CONFIG_ALT1);	/* CLK */
	mxc_request_iomux(MX35_PIN_NFRE_B, MUX_CONFIG_ALT1);	/* DIR */
	mxc_request_iomux(MX35_PIN_NFCLE, MUX_CONFIG_ALT1);	/* NXT */
	mxc_request_iomux(MX35_PIN_NFALE, MUX_CONFIG_ALT1);	/* STP */
	mxc_request_iomux(MX35_PIN_SD2_DATA1, MUX_CONFIG_ALT4); /* DATA0 */
	mxc_request_iomux(MX35_PIN_SD2_DATA2, MUX_CONFIG_ALT4); /* DATA1 */
	mxc_request_iomux(MX35_PIN_SD2_DATA3, MUX_CONFIG_ALT4); /* DATA2 */
	mxc_request_iomux(MX35_PIN_NFWE_B, MUX_CONFIG_ALT1);	/* DATA3 */
	mxc_request_iomux(MX35_PIN_SD2_CMD, MUX_CONFIG_ALT4);	/* DATA4 */
	mxc_request_iomux(MX35_PIN_SD2_CLK, MUX_CONFIG_ALT4);	/* DATA5 */
	mxc_request_iomux(MX35_PIN_SD2_DATA0, MUX_CONFIG_ALT4); /* DATA6 */
	mxc_request_iomux(MX35_PIN_NFWP_B, MUX_CONFIG_ALT1);	/* DATA7 */

	pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
		PAD_CTL_DRV_MAX |
#else
		PAD_CTL_DRV_NORMAL |
#endif
		PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
	mxc_iomux_set_pad(MX35_PIN_GPIO3_0, pad_val);
	mxc_iomux_set_pad(MX35_PIN_NFRE_B, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_DATA1, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_DATA2, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_DATA3, pad_val);
	mxc_iomux_set_pad(MX35_PIN_NFWE_B, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_CMD, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_CLK, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SD2_DATA0, pad_val);
	mxc_iomux_set_pad(MX35_PIN_NFWP_B, pad_val);

	pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
#ifndef CONFIG_MACH_MAGNOLIA2
		PAD_CTL_DRV_MAX |
#else
		PAD_CTL_DRV_NORMAL |
#endif
		PAD_CTL_100K_PD | PAD_CTL_SRE_FAST;
	mxc_iomux_set_pad(MX35_PIN_NFCLE, pad_val);
	mxc_iomux_set_pad(MX35_PIN_NFALE, pad_val);

	/* SW_SELECT_INPUT */
	mxc_iomux_set_input(MUX_IN_USB_UH2_DIR, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_NXT, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_0, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_1, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_2, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_3, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_4, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_5, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_6, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_DATA_7, INPUT_CTL_PATH0);
	mxc_iomux_set_input(MUX_IN_USB_UH2_USB_OC, INPUT_CTL_PATH1); /* Overcurrent */

	/* USB Phy reset */
	//magnolia2_usbh2_phy_reset();
	//mdelay(5);

	return 0;
}

EXPORT_SYMBOL(gpio_usbh2_active);

void gpio_usbh2_inactive(void)
{
	mxc_request_gpio(MX35_PIN_GPIO3_0);
	mxc_request_gpio(MX35_PIN_NFRE_B);
	mxc_request_gpio(MX35_PIN_NFCLE);
	mxc_request_gpio(MX35_PIN_NFALE);
	mxc_request_gpio(MX35_PIN_SD2_DATA1);
	mxc_request_gpio(MX35_PIN_SD2_DATA2);
	mxc_request_gpio(MX35_PIN_SD2_DATA3);
	mxc_request_gpio(MX35_PIN_NFWE_B);
	mxc_request_gpio(MX35_PIN_SD2_CMD);
	mxc_request_gpio(MX35_PIN_SD2_CLK);
	mxc_request_gpio(MX35_PIN_SD2_DATA0);
	mxc_request_gpio(MX35_PIN_NFWP_B);

	mxc_free_iomux(MX35_PIN_GPIO3_0, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_NFRE_B, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_NFCLE, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_NFALE, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_DATA1, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_DATA2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_DATA3, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_NFWE_B, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_CMD, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_CLK, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_SD2_DATA0, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_NFWP_B, MUX_CONFIG_GPIO);
}

EXPORT_SYMBOL(gpio_usbh2_inactive);

/*!
 * This function activates DAM ports 3 to enable
 * audio I/O.
 */
void gpio_activate_audio_ports(void)
{
	unsigned int pad_val;

	mxc_request_iomux(MX35_PIN_STXD4, MUX_CONFIG_FUNC);	/* TxD	  */
	mxc_request_iomux(MX35_PIN_SRXD4, MUX_CONFIG_FUNC);	/* RxD	  */
	mxc_request_iomux(MX35_PIN_SCK4, MUX_CONFIG_FUNC);	/* SCK4	  */
	mxc_request_iomux(MX35_PIN_STXFS4, MUX_CONFIG_FUNC);	/* STXFS4 */

	pad_val = PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PU |
		PAD_CTL_PUE_PUD;
	mxc_iomux_set_pad(MX35_PIN_STXD4, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SRXD4, pad_val);
	mxc_iomux_set_pad(MX35_PIN_SCK4, pad_val);
	mxc_iomux_set_pad(MX35_PIN_STXFS4, pad_val);
}

EXPORT_SYMBOL(gpio_activate_audio_ports);

/*!
 * This function deactivates DAM ports 3 to disable
 * audio I/O.
 */
void gpio_inactivate_audio_ports(void)
{
	mxc_free_iomux(MX35_PIN_STXD4, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_SRXD4, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_SCK4, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_STXFS4, MUX_CONFIG_FUNC);
}

EXPORT_SYMBOL(gpio_inactivate_audio_ports);

void gpio_dipsw_active(void)
{
	mxc_request_iomux(MX35_PIN_ATA_DATA15, MUX_CONFIG_GPIO);
	mxc_request_iomux(MX35_PIN_ATA_INTRQ, MUX_CONFIG_GPIO);
	mxc_request_iomux(MX35_PIN_ATA_BUFF_EN, MUX_CONFIG_GPIO);
	mxc_request_iomux(MX35_PIN_ATA_DMARQ, MUX_CONFIG_GPIO);

	mxc_iomux_set_input(MUX_IN_GPIO2_IN_28, INPUT_CTL_PATH1);
	mxc_iomux_set_input(MUX_IN_GPIO2_IN_29, INPUT_CTL_PATH1);
	mxc_iomux_set_input(MUX_IN_GPIO2_IN_30, INPUT_CTL_PATH1);
	mxc_iomux_set_input(MUX_IN_GPIO2_IN_31, INPUT_CTL_PATH1);
}
EXPORT_SYMBOL(gpio_dipsw_active);

void gpio_dipsw_inactive(void)
{
	mxc_free_iomux(MX35_PIN_ATA_DATA15, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_ATA_INTRQ, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_ATA_BUFF_EN, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_ATA_DMARQ, MUX_CONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_dipsw_inactive);

/* PUSHSW */
void gpio_pushsw_active(void)
{
	mxc_request_iomux(MX35_PIN_ATA_DATA13, MUX_CONFIG_GPIO);
	mxc_request_iomux(MX35_PIN_ATA_DATA14, MUX_CONFIG_GPIO);

	mxc_iomux_set_input(MUX_IN_GPIO2_IN_26, INPUT_CTL_PATH1);
	mxc_iomux_set_input(MUX_IN_GPIO2_IN_27, INPUT_CTL_PATH1);
}
EXPORT_SYMBOL(gpio_pushsw_active);

void gpio_pushsw_inactive(void)
{
	mxc_free_iomux(MX35_PIN_ATA_DATA13, MUX_CONFIG_FUNC);
	mxc_free_iomux(MX35_PIN_ATA_DATA14, MUX_CONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_pushsw_inactive);

/* DIO */
void gpio_dio_active(void)
{
	mxc_request_iomux(MX35_PIN_ATA_DA1, MUX_CONFIG_GPIO);
	mxc_set_gpio_direction(MX35_PIN_ATA_DA1, 1);
}
EXPORT_SYMBOL(gpio_dio_active);

void gpio_dio_inactive(void)
{
	mxc_free_iomux(MX35_PIN_ATA_DA1, MUX_CONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_dio_inactive);

#ifdef CONFIG_MXC_UART1_USE_AS_GPIO
/* use UART1 (PORT2) as GPIO */
void port2_gpio_active(void)
{
	mxc_request_iomux(MX35_PIN_RXD2, MUX_CONFIG_GPIO);	/* GPIO 0 */
	mxc_request_iomux(MX35_PIN_TXD2, MUX_CONFIG_GPIO);	/* GPIO 1 */
	mxc_request_iomux(MX35_PIN_RTS2, MUX_CONFIG_GPIO);	/* GPIO 2 */
	mxc_request_iomux(MX35_PIN_CTS2, MUX_CONFIG_GPIO);	/* GPIO 3 */

	mxc_set_gpio_dataout(MX35_PIN_RXD2, 0);
	mxc_set_gpio_dataout(MX35_PIN_TXD2, 0);
	mxc_set_gpio_dataout(MX35_PIN_RTS2, 0);
	mxc_set_gpio_dataout(MX35_PIN_CTS2, 0);

	mxc_set_gpio_direction(MX35_PIN_RXD2, 1);		/* GPIO IN */
	mxc_set_gpio_direction(MX35_PIN_TXD2, 1);		/* GPIO IN */
	mxc_set_gpio_direction(MX35_PIN_RTS2, 1);		/* GPIO IN */
	mxc_set_gpio_direction(MX35_PIN_CTS2, 1);		/* GPIO IN */

	mxc_iomux_set_pad(MX35_PIN_RXD2,
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX35_PIN_TXD2,
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX35_PIN_RTS2,
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX35_PIN_CTS2,
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
}
EXPORT_SYMBOL(port2_gpio_active);

void port2_gpio_inactive(void)
{
	mxc_set_gpio_dataout(MX35_PIN_RXD2, 0);
	mxc_set_gpio_dataout(MX35_PIN_TXD2, 0);
	mxc_set_gpio_dataout(MX35_PIN_RTS2, 0);
	mxc_set_gpio_dataout(MX35_PIN_CTS2, 0);

	mxc_set_gpio_direction(MX35_PIN_RXD2, 1);
	mxc_set_gpio_direction(MX35_PIN_TXD2, 1);
	mxc_set_gpio_direction(MX35_PIN_RTS2, 1);
	mxc_set_gpio_direction(MX35_PIN_CTS2, 1);

	mxc_free_iomux(MX35_PIN_RXD2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_TXD2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_RTS2, MUX_CONFIG_GPIO);
	mxc_free_iomux(MX35_PIN_CTS2, MUX_CONFIG_GPIO);
}
EXPORT_SYMBOL(port2_gpio_inactive);
#endif
