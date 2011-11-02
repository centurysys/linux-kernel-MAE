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
/*!
 * @file mach-mx35/serial.c
 *
 * @brief This file contains the UART initiliazation.
 *
 * @ingroup MSL_MX35
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <mach/hardware.h>
#include <mach/mxc_uart.h>
#include <mach/spba.h>
#include "serial.h"
#include "board-mx35evb.h"
#include "board-mx35_3stack.h"
#ifdef CONFIG_MACH_MAGNOLIA2
#include <mach/board-magnolia2.h>
#include "mx35_pins.h"
#endif

#if defined(CONFIG_SERIAL_MXC) || defined(CONFIG_SERIAL_MXC_MODULE)

/*!
 * This is an array where each element holds information about a UART port,
 * like base address of the UART, interrupt numbers etc. This structure is
 * passed to the serial_core.c file. Based on which UART is used, the core file
 * passes back the appropriate port structure as an argument to the control
 * functions.
 */
static uart_mxc_port mxc_ports[] = {
	[0] = {
	       .port = {
			.membase = (void *)IO_ADDRESS(UART1_BASE_ADDR),
			.mapbase = UART1_BASE_ADDR,
			.iotype = SERIAL_IO_MEM,
			.irq = UART1_INT1,
			.fifosize = 32,
			.flags = ASYNC_BOOT_AUTOCONF,
			.line = 0,
                },
	       .ints_muxed = UART1_MUX_INTS,
	       .irqs = {UART1_INT2, UART1_INT3},
	       .mode = UART1_MODE,
	       .ir_mode = UART1_IR,
	       .enabled = UART1_ENABLED,
	       .hardware_flow = UART1_HW_FLOW,
	       .cts_threshold = UART1_UCR4_CTSTL,
	       .dma_enabled = UART1_DMA_ENABLE,
	       .dma_rxbuf_size = UART1_DMA_RXBUFSIZE,
	       .rx_threshold = UART1_UFCR_RXTL,
	       .tx_threshold = UART1_UFCR_TXTL,
	       .shared = UART1_SHARED_PERI,
	       .dma_tx_id = MXC_DMA_UART1_TX,
	       .dma_rx_id = MXC_DMA_UART1_RX,
	       .rxd_mux = MXC_UART_RXDMUX,
	       .ir_tx_inv = MXC_IRDA_TX_INV,
	       .ir_rx_inv = MXC_IRDA_RX_INV,
#ifdef CONFIG_MXC_UART_DSR_GPIO
               .pin_DSR = MX35_PIN_ATA_DATA7,
#endif
        },
	[1] = {
	       .port = {
			.membase = (void *)IO_ADDRESS(UART2_BASE_ADDR),
			.mapbase = UART2_BASE_ADDR,
			.iotype = SERIAL_IO_MEM,
			.irq = UART2_INT1,
			.fifosize = 32,
			.flags = ASYNC_BOOT_AUTOCONF,
			.line = 1,
                },
	       .ints_muxed = UART2_MUX_INTS,
	       .irqs = {UART2_INT2, UART2_INT3},
	       .mode = UART2_MODE,
	       .ir_mode = UART2_IR,
	       .enabled = UART2_ENABLED,
	       .hardware_flow = UART2_HW_FLOW,
	       .cts_threshold = UART2_UCR4_CTSTL,
	       .dma_enabled = UART2_DMA_ENABLE,
	       .dma_rxbuf_size = UART2_DMA_RXBUFSIZE,
	       .rx_threshold = UART2_UFCR_RXTL,
	       .tx_threshold = UART2_UFCR_TXTL,
	       .shared = UART2_SHARED_PERI,
	       .dma_tx_id = MXC_DMA_UART2_TX,
	       .dma_rx_id = MXC_DMA_UART2_RX,
	       .rxd_mux = MXC_UART_IR_RXDMUX,
	       .ir_tx_inv = MXC_IRDA_TX_INV,
	       .ir_rx_inv = MXC_IRDA_RX_INV,
#ifdef CONFIG_MXC_UART_DSR_GPIO
               .pin_DSR = MX35_PIN_TX4_RX1,
#endif
        },
#if UART3_ENABLED == 1
	[2] = {
	       .port = {
			.membase = (void *)IO_ADDRESS(UART3_BASE_ADDR),
			.mapbase = UART3_BASE_ADDR,
			.iotype = SERIAL_IO_MEM,
			.irq = UART3_INT1,
			.fifosize = 32,
			.flags = ASYNC_BOOT_AUTOCONF,
			.line = 2,
                },
	       .ints_muxed = UART3_MUX_INTS,
	       .irqs = {UART3_INT2, UART3_INT3},
	       .mode = UART3_MODE,
	       .ir_mode = UART3_IR,
	       .enabled = UART3_ENABLED,
	       .hardware_flow = UART3_HW_FLOW,
	       .cts_threshold = UART3_UCR4_CTSTL,
	       .dma_enabled = UART3_DMA_ENABLE,
	       .dma_rxbuf_size = UART3_DMA_RXBUFSIZE,
	       .rx_threshold = UART3_UFCR_RXTL,
	       .tx_threshold = UART3_UFCR_TXTL,
	       .shared = UART3_SHARED_PERI,
	       .dma_tx_id = MXC_DMA_UART3_TX,
	       .dma_rx_id = MXC_DMA_UART3_RX,
	       .rxd_mux = MXC_UART_RXDMUX,
	       .ir_tx_inv = MXC_IRDA_TX_INV,
	       .ir_rx_inv = MXC_IRDA_RX_INV,
        },
#endif
};

static struct platform_device mxc_uart_device1 = {
	.name = "mxcintuart",
	.id = 0,
	.dev = {
		.platform_data = &mxc_ports[0],
        },
};

static struct platform_device mxc_uart_device2 = {
	.name = "mxcintuart",
	.id = 1,
	.dev = {
		.platform_data = &mxc_ports[1],
        },
};

#if UART3_ENABLED == 1
static struct platform_device mxc_uart_device3 = {
	.name = "mxcintuart",
	.id = 2,
	.dev = {
		.platform_data = &mxc_ports[2],
        },
};
#endif

#ifdef CONFIG_MACH_MAGNOLIA2
extern int magnolia2_get_uart_info(int port, u32 *enable, u32 *type, u32 *config);
#endif
static int __init mxc_init_uart(void)
{
#ifdef CONFIG_MACH_MAGNOLIA2
        u32 enable, type, config;

        magnolia2_get_uart_info(0, &enable, &type, &config);

        printk("Magnolia2 UART1: ");
        if (enable == 0)
                printk("Disabled\n");
        else {
                if (type == 0)
                        printk("RS-232\n");
                else {
                        if (config == 0)
                                printk("RS-485\n");
                        else
                                printk("RS-422\n");

			/* disable hardware flow control */
			mxc_ports[0].hardware_flow = 0;
                }

                mxc_ports[0].driver_type = type;
                mxc_ports[0].driver_duplex = config;
                mxc_ports[0].TxEnable = MX35_PIN_MLB_DAT;
                mxc_ports[0].RxEnable = MX35_PIN_MLB_SIG;

                platform_device_register(&mxc_uart_device1);
        }

        magnolia2_get_uart_info(1, &enable, &type, &config);

        printk("Magnolia2 UART2: ");
        if (enable == 0)
                printk("Disabled\n");
        else {
                if (type == 0) {
			if (config == 0)
				printk("RS-232\n");
			else
				printk("FeliCa R/W\n");
		} else {
			if (config == 0)
                                printk("RS-485\n");
                        else
                                printk("RS-422\n");

			/* disable hardware flow control */
			mxc_ports[1].hardware_flow = 0;
                }

                mxc_ports[1].driver_type = type;
                mxc_ports[1].driver_duplex = config;
                mxc_ports[1].TxEnable = MX35_PIN_CTS2;
                mxc_ports[1].RxEnable = MX35_PIN_RTS2;

                platform_device_register(&mxc_uart_device2);
        }
#else
	/* Register all the MXC UART platform device structures */
	platform_device_register(&mxc_uart_device1);
	platform_device_register(&mxc_uart_device2);
#endif

	/* Grab ownership of shared UARTs 3 and 4, only when enabled */
#if UART3_ENABLED == 1
#if UART3_DMA_ENABLE == 1
	spba_take_ownership(UART3_SHARED_PERI, (SPBA_MASTER_A | SPBA_MASTER_C));
#else
	spba_take_ownership(UART3_SHARED_PERI, SPBA_MASTER_A);
#endif				/* UART3_DMA_ENABLE */
	platform_device_register(&mxc_uart_device3);
#endif				/* UART3_ENABLED */

	return 0;
}

#else
static int __init mxc_init_uart(void)
{
	return 0;
}
#endif

arch_initcall(mxc_init_uart);
