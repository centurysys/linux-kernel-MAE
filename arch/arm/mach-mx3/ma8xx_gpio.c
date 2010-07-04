/*
 * Copyright 2005-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Century Systems Co.,Ltd. All Rights Reserved.
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
#include "board-ma8xx.h"
#include "iomux.h"

/*!
 * @file mach-mx3/ma8xx_gpio.c
 *
 * @brief This file contains all the GPIO setup functions for the board.
 *
 * @ingroup GPIO_MX31
 */

void gpio_activate_audio_port(void);

/*!
 * This system-wise GPIO function initializes the pins during system startup.
 * All the statically linked device drivers should put the proper GPIO initialization
 * code inside this function. It is called by \b fixup_mx31ads() during
 * system startup. This function is board specific.
 */
void ma8xx_gpio_init(void)
{
	/* config CS4 */
	mxc_request_iomux(MX31_PIN_CS4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	/* config CS5 */
	mxc_request_iomux(MX31_PIN_CS5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	/* Connect DAM ports 5 to enable audio I/O */
	gpio_activate_audio_port();

	/* config WATCHDOG reset */
	mxc_request_iomux(MX31_PIN_WATCHDOG_RST, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
}

/*!
 * Setup GPIO for a UART port to be active
 *
 * @param  port         a UART port
 * @param  no_irda      indicates if the port is used for SIR
 */
void gpio_uart_active(int port, int no_irda)
{
	/*
	 * Configure the IOMUX control registers for the UART signals
	 */
	switch (port) {
	case 0:
		/* UART 1 IOMUX Configs */
		mxc_request_iomux(MX31_PIN_RXD1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_TXD1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_RTS1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CTS1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DTR_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DSR_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_RI_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DCD_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
	case 1:
		/* UART 2 IOMUX Configs */
		mxc_request_iomux(MX31_PIN_TXD2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_RXD2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);

	default:
		break;
	}

	/*
	 * TODO: Configure the Pad registers for the UART pins
	 */
}

/*!
 * Setup GPIO for a UART port to be inactive
 *
 * @param  port         a UART port
 * @param  no_irda      indicates if the port is used for SIR
 */
void gpio_uart_inactive(int port, int no_irda)
{
	switch (port) {
	case 0:
		mxc_request_gpio(MX31_PIN_RXD1);
		mxc_request_gpio(MX31_PIN_TXD1);
		mxc_request_gpio(MX31_PIN_RTS1);
		mxc_request_gpio(MX31_PIN_CTS1);
		mxc_request_gpio(MX31_PIN_DTR_DTE1);
		mxc_request_gpio(MX31_PIN_DSR_DTE1);
		mxc_request_gpio(MX31_PIN_RI_DTE1);
		mxc_request_gpio(MX31_PIN_DCD_DTE1);

		mxc_free_iomux(MX31_PIN_RXD1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_TXD1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_RTS1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_CTS1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_DTR_DTE1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_DSR_DTE1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_RI_DTE1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_DCD_DTE1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);

		break;
	case 1:
		mxc_request_gpio(MX31_PIN_TXD2);
		mxc_request_gpio(MX31_PIN_RXD2);

		mxc_free_iomux(MX31_PIN_TXD2, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_RXD2, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		break;
	default:
		break;
	}
}

/*!
 * Configure the IOMUX GPR register to receive shared SDMA UART events
 *
 * @param  port         a UART port
 */
void config_uartdma_event(int port)
{
	switch (port) {
	case 1:
		/* Configure to receive UART 2 SDMA events */
		mxc_iomux_set_gpr(MUX_PGP_FIRI, false);
		break;
	case 2:
		/* Configure to receive UART 3 SDMA events */
		mxc_iomux_set_gpr(MUX_CSPI1_UART3, true);
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_uart_active);
EXPORT_SYMBOL(gpio_uart_inactive);
EXPORT_SYMBOL(config_uartdma_event);

/*!
 * Setup GPIO for a CSPI device to be active
 *
 * @param  cspi_mod         an CSPI device
 */
void gpio_spi_active(int cspi_mod)
{
	switch (cspi_mod) {
	case 0:
		/* SPI1 */
		mxc_request_iomux(MX31_PIN_CSPI1_MISO, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_MOSI, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_SCLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_SPI_RDY, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_SS0, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_SS1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI1_SS2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
	case 1:
		/* SPI2 */
		mxc_request_iomux(MX31_PIN_CSPI2_MISO, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_MOSI, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SPI_RDY, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SS0, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SS1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SS2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
	case 2:
		/* SPI3 */
		/*
		   mxc_request_iomux(MX31_PIN_CSPI2_MISO, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_MOSI, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SPI_RDY, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		 */
		break;
	default:
		break;
	}
}

/*!
 * Setup GPIO for a CSPI device to be inactive
 *
 * @param  cspi_mod         a CSPI device
 */
void gpio_spi_inactive(int cspi_mod)
{
	/* Do nothing as CSPI pins doesn't have/support GPIO mode */
}

/*!
 * Setup GPIO for an I2C device to be active
 *
 * @param  i2c_num         an I2C device
 */
void gpio_i2c_active(int i2c_num)
{
        u32 config;

	switch (i2c_num) {
	case 0:
		mxc_request_iomux(MX31_PIN_I2C_CLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_I2C_DAT, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);

                config = PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD | 
                        PAD_CTL_100K_PU | PAD_CTL_ODE_OpenDrain;

                mxc_iomux_set_pad(MX31_PIN_I2C_CLK, config);
                mxc_iomux_set_pad(MX31_PIN_I2C_DAT, config);
		break;
	case 1:
		mxc_request_iomux(MX31_PIN_CSPI2_MOSI, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI2_MISO, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		break;
	case 2:
		mxc_request_iomux(MX31_PIN_CSPI2_SS2, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		break;
	default:
		break;
	}

}

/*!
 * Setup GPIO for an I2C device to be inactive
 *
 * @param  i2c_num         an I2C device
 */
void gpio_i2c_inactive(int i2c_num)
{
	switch (i2c_num) {
	case 0:
		break;
	case 1:
		break;
	case 2:
		mxc_request_iomux(MX31_PIN_CSPI2_SS2, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_ALT1);
		break;
	default:
		break;
	}
}

/*!
 * This function activates DAM ports 5 to enable
 * audio I/O. Thsi function is called from mx31ads_gpio_init
 * function, which is board-specific.
 */
void gpio_activate_audio_port(void)
{
	/* config Audio port 5 */
	mxc_request_iomux(MX31_PIN_SCK5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SRXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_STXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SFS5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
}

EXPORT_SYMBOL(gpio_activate_audio_port);


/*!
 * Setup GPIO for SDHC to be active
 *
 * @param module SDHC module number
 */
void gpio_sdhc_active(int module)
{
	switch (module) {
	case 0:
		mxc_request_iomux(MX31_PIN_SD1_CLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_CMD, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA0, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA3, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);

		mxc_iomux_set_pad(MX31_PIN_SD1_CLK,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		mxc_iomux_set_pad(MX31_PIN_SD1_CMD,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA0,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA1,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA2,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA3,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
		break;
	case 1:
		mxc_request_iomux(MX31_PIN_PC_CD2_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_CD1_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_WAIT_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_READY, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_VS1, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_PWRON, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
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
	switch (module) {
	case 0:
		mxc_request_iomux(MX31_PIN_SD1_CLK, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_CMD, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA0, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA1, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA2, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA3, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);

		mxc_iomux_set_pad(MX31_PIN_SD1_CLK,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		mxc_iomux_set_pad(MX31_PIN_SD1_CMD,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA0,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA1,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA2,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA3,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
		break;
	case 1:
		/* TODO:what are the pins for SDHC2? */
		mxc_request_iomux(MX31_PIN_PC_CD2_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_CD1_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_WAIT_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_READY, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_VS1, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_PWRON, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_sdhc_inactive);

/*
 * Probe for the card. If present the GPIO data would be set.
 */
int sdhc_get_card_det_status(struct device *dev)
{
	if (to_platform_device(dev)->id == 0) {
		return mxc_get_gpio_datain(MX31_PIN_ATA_DMACK);
	} else
                return 0;
}

EXPORT_SYMBOL(sdhc_get_card_det_status);

int sdhc_get_ro(struct device *dev)
{
	if (to_platform_device(dev)->id == 0)
		return mxc_get_gpio_datain(MX31_PIN_ATA_RESET_B);
	else
		return 0;
}
EXPORT_SYMBOL(sdhc_get_ro);

/*
 * Return the card detect pin.
 */
int sdhc_init_card_det(int id)
{
	if (id == 0) {
		return IOMUX_TO_IRQ(MX31_PIN_ATA_DMACK);
	} else
                return -1;
}

EXPORT_SYMBOL(sdhc_init_card_det);

/*!
 * Setup GPIO for LCD to be active
 *
 */
void gpio_lcd_active(void)
{
	mxc_request_iomux(MX31_PIN_LD0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD6, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD7, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD8, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD9, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD10, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD11, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD12, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD13, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD14, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD15, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD16, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	        // LD16
	mxc_request_iomux(MX31_PIN_LD17, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	        // LD17
	mxc_request_iomux(MX31_PIN_VSYNC3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// VSYNC
	mxc_request_iomux(MX31_PIN_HSYNC, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	        // HSYNC
	mxc_request_iomux(MX31_PIN_FPSHIFT, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// CLK
	mxc_request_iomux(MX31_PIN_DRDY0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	        // DRDY
	mxc_request_iomux(MX31_PIN_D3_REV, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// REV
	mxc_request_iomux(MX31_PIN_CONTRAST, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// CONTR
	mxc_request_iomux(MX31_PIN_D3_SPL, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// SPL
	mxc_request_iomux(MX31_PIN_D3_CLS, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// CLS
}

/*!
 * Setup GPIO for LCD to be inactive
 *
 */
void gpio_lcd_inactive(void)
{
}

/*!
 * Setup pins for SLCD to be active
 *
 */
void slcd_gpio_config(void)
{
	mxc_request_iomux(MX31_PIN_LD0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD6, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD7, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD8, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD9, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD10, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD11, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD12, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD13, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD14, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD15, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD16, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_LD17, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	mxc_request_iomux(MX31_PIN_READ, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	/* read */
	mxc_request_iomux(MX31_PIN_WRITE, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	/* write */
	mxc_request_iomux(MX31_PIN_PAR_RS, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	/* RS */
	mxc_request_iomux(MX31_PIN_LCS0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	/* chip select */
}

/* *INDENT-OFF* */
/*
 * USB Host 1
 * pins conflict with SPI1, ATA, UART3
 */
int gpio_usbh1_active(void)
{
	if (mxc_request_iomux(MX31_PIN_CSPI1_MOSI,	/* USBH1_RXDM */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_MISO,	/* USBH1_RXDP */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS0,	/* USBH1_TXDM */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS1,	/* USBH1_TXDP */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS2,	/* USBH1_RCV  */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SCLK,	/* USBH1_OEB (_TXOE) */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SPI_RDY,	/* USBH1_FS   */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1)) {
		return -EINVAL;
	}

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI,		/* USBH1_RXDM */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO,		/* USBH1_RXDP */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0,		/* USBH1_TXDM */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1,		/* USBH1_TXDP */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2,		/* USBH1_RCV  */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK,		/* USBH1_OEB (_TXOE) */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY,	/* USBH1_FS   */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_gpr(MUX_PGP_USB_SUSPEND, true);

	return 0;
}

EXPORT_SYMBOL(gpio_usbh1_active);

void gpio_usbh1_inactive(void)
{
	/* Do nothing as pins don't have/support GPIO mode */

}

EXPORT_SYMBOL(gpio_usbh1_inactive);

/*
 * USB Host 2
 * pins conflict with UART5, PCMCIA
 */
int gpio_usbh2_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBH2_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_PC_VS2,		/* USBH2_DATA2 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_PC_BVD1,		/* USBH2_DATA3 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_PC_BVD2,		/* USBH2_DATA4 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_PC_RST,		/* USBH2_DATA5 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_IOIS16,		/* USBH2_DATA6 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_PC_RW_B,		/* USBH2_DATA7 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFWE_B,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFRE_B,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFALE,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFCLE,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFWP_B,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE) ||
	    mxc_request_iomux(MX31_PIN_NFCE_B,
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE)) {
		return -EINVAL;
	}

#define H2_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)
	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SRXD6, H2_PAD_CFG);	/* USBH2_DATA2 */
	mxc_iomux_set_pad(MX31_PIN_STXD6, H2_PAD_CFG);	/* USBH2_DATA3 */
	mxc_iomux_set_pad(MX31_PIN_SFS3, H2_PAD_CFG);	/* USBH2_DATA4 */
	mxc_iomux_set_pad(MX31_PIN_SCK3, H2_PAD_CFG);	/* USBH2_DATA5 */
	mxc_iomux_set_pad(MX31_PIN_SRXD3, H2_PAD_CFG);	/* USBH2_DATA6 */
	mxc_iomux_set_pad(MX31_PIN_STXD3, H2_PAD_CFG);	/* USBH2_DATA7 */
#undef H2_PAD_CFG

	mxc_iomux_set_gpr(MUX_PGP_UH2, true);

	return 0;
}

EXPORT_SYMBOL(gpio_usbh2_active);

void gpio_usbh2_inactive(void)
{
	iomux_config_gpr(MUX_PGP_UH2, false);

	iomux_config_pad(MX31_PIN_USBH2_CLK,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DIR,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_NXT,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_STP,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DATA0,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DATA1,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SRXD6,		/* USBH2_DATA2 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_STXD6,		/* USBH2_DATA3 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SFS3,			/* USBH2_DATA4 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SCK3,			/* USBH2_DATA5 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SRXD3,		/* USBH2_DATA6 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_STXD3,		/* USBH2_DATA7 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));

	mxc_free_iomux(MX31_PIN_NFWE_B,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFRE_B,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFALE,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFCLE,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFWP_B,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFCE_B,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
}

EXPORT_SYMBOL(gpio_usbh2_inactive);

/*
 * USB OTG HS port
 */
int gpio_usbotg_hs_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBOTG_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA2,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA3,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA4,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA5,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA6,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA7,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC)) {
		return -EINVAL;
	}

	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	return 0;
}

EXPORT_SYMBOL(gpio_usbotg_hs_active);

void gpio_usbotg_hs_inactive(void)
{
	/* Do nothing as  pins doesn't have/support GPIO mode */

}

EXPORT_SYMBOL(gpio_usbotg_hs_inactive);

/*
 * USB OTG FS port
 */
int gpio_usbotg_fs_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBOTG_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA2,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA3,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA4,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA5,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA6,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA7,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USB_PWR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC)) {
		return -EINVAL;
	}

	return 0;
}

EXPORT_SYMBOL(gpio_usbotg_fs_active);

void gpio_usbotg_fs_inactive(void)
{
	/* Do nothing as  pins doesn't have/support GPIO mode */

}

EXPORT_SYMBOL(gpio_usbotg_fs_inactive);
/* *INDENT-ON* */

/*!
 * Setup GPIO for PCMCIA interface
 *
 */
void gpio_pcmcia_active(void)
{
	iomux_config_mux(MX31_PIN_PC_CD1_B, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_CD2_B, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_WAIT_B, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_READY, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_PWRON, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_VS1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_VS2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_BVD1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_BVD2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_RST, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_IOIS16, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_RW_B, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_PC_POE, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	mxc_request_iomux(MX31_PIN_SDBA1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SDBA0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	iomux_config_mux(MX31_PIN_EB0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_EB1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_OE, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_LBA, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_RW, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

        /* CF_PWREN */
	iomux_config_mux(MX31_PIN_PWMO, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);

	mdelay(1);
}
EXPORT_SYMBOL(gpio_pcmcia_active);

/*!
 * Setup GPIO for pcmcia to be inactive
 */
void gpio_pcmcia_inactive(void)
{
}
EXPORT_SYMBOL(gpio_pcmcia_inactive);

void gpio_pcmcia_power_on(void)
{
	mxc_set_gpio_direction(MX31_PIN_PWMO, 0 /* OUTPUT */);
	mxc_set_gpio_dataout(MX31_PIN_PWMO, 1   /* HIGH */);
}
EXPORT_SYMBOL(gpio_pcmcia_power_on);

void gpio_pcmcia_power_off(void)
{
	mxc_set_gpio_direction(MX31_PIN_PWMO, 0 /* OUTPUT */);
	mxc_set_gpio_dataout(MX31_PIN_PWMO, 0   /* LOW */);
}
EXPORT_SYMBOL(gpio_pcmcia_power_off);

void gpio_audio_active(int select)
{
	iomux_config_mux(MX31_PIN_SCK4, OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1);
	iomux_config_mux(MX31_PIN_STXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_SRXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_SCK5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	iomux_config_mux(MX31_PIN_SFS5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_audio_active);

void gpio_audio_inactive(int select)
{
}
EXPORT_SYMBOL(gpio_audio_inactive);

/* DIPSW */
void gpio_dipsw_active(void)
{
        mxc_request_iomux(MX31_PIN_ATA_CS0, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_ATA_CS1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
}
EXPORT_SYMBOL(gpio_dipsw_active);

void gpio_dipsw_inactive(void)
{
        mxc_request_iomux(MX31_PIN_ATA_CS0, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
        mxc_request_iomux(MX31_PIN_ATA_CS1, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
        mxc_request_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
        mxc_request_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_dipsw_inactive);

/* PUSHSW */
void gpio_pushsw_active(void)
{
        mxc_request_iomux(MX31_PIN_LCS0, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_SD_D_CLK, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
}
EXPORT_SYMBOL(gpio_pushsw_active);

void gpio_pushsw_inactive(void)
{
        mxc_request_iomux(MX31_PIN_LCS0, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
        mxc_request_iomux(MX31_PIN_SD_D_CLK, OUTPUTCONFIG_FUNC,
                          INPUTCONFIG_FUNC);
}
EXPORT_SYMBOL(gpio_pushsw_inactive);

/* Contact-IN */
void gpio_din_active(void)
{
        mxc_request_iomux(MX31_PIN_GPIO3_0, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_GPIO3_1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_SCLK0, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_SRST0, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
}
EXPORT_SYMBOL(gpio_din_active);

void gpio_din_inactive(void)
{
        mxc_free_iomux(MX31_PIN_GPIO3_0, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_GPIO3_1, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_SCLK0, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_SRST0, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
}
EXPORT_SYMBOL(gpio_din_inactive);

/* Contact-OUT */
void gpio_dout_active(void)
{
        mxc_request_iomux(MX31_PIN_DTR_DCE1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_DSR_DCE1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_RI_DCE1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);
        mxc_request_iomux(MX31_PIN_DCD_DCE1, OUTPUTCONFIG_GPIO,
                          INPUTCONFIG_GPIO);

        mxc_iomux_set_pad(MX31_PIN_DTR_DCE1,
                          PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
        mxc_iomux_set_pad(MX31_PIN_DSR_DCE1,
                          PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
        mxc_iomux_set_pad(MX31_PIN_RI_DCE1,
                          PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
        mxc_iomux_set_pad(MX31_PIN_DCD_DCE1,
                          PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
}
EXPORT_SYMBOL(gpio_dout_active);

void gpio_dout_inactive(void)
{
        mxc_free_iomux(MX31_PIN_DTR_DCE1, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_DSR_DCE1, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_RI_DCE1, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
        mxc_free_iomux(MX31_PIN_DCD_DCE1, OUTPUTCONFIG_GPIO,
                       INPUTCONFIG_GPIO);
}
EXPORT_SYMBOL(gpio_dout_inactive);
