/*
 *  Copyright (C) 2009 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/export.h>

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/smc911x.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/spi/spi.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/clk.h>
#ifdef CONFIG_MAGNOLIA2_EXTRS485
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/workqueue.h>
#include <asm/delay.h>
#endif

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>
#include <mach/ulpi.h>
#include <mach/audmux.h>
#include <mach/iomux-mx35.h>
#include <mach/board-magnolia2.h>

#include "devices-imx35.h"

#define GPIO_PORT(_name, _gpio)				       \
	{						       \
		.name = (_name),			       \
			.pin  = (_gpio),		       \
			.irq  = IMX_GPIO_TO_IRQ(_gpio),	       \
			}

#define LED_PORT(_name, _shift)			\
	{						\
		.name = (_name),			\
			.shift	= (_shift),		\
			}

unsigned int magnolia2_board_io;

static struct tag_magnolia2_uboot uboot_tag;

static int __init parse_tag_magnolia2_uboot(const struct tag *tag)
{
	printk("Using UBoot passing parameters structure\n");

	memcpy(&uboot_tag, &tag->u.magnolia2, sizeof(uboot_tag));

	return 0;
}

__tagtable(ATAG_MAGNOLIA2, parse_tag_magnolia2_uboot);

#if 0
static void mxc_nop_release(struct device *dev)
{
	/* Nothing */
}
#endif

/* SPI */
static int spi0_internal_chipselect[] = {
	MXC_SPI_CS(0),
	MXC_SPI_CS(1),
};

static const struct spi_imx_master spi0_pdata __initconst = {
	.chipselect	   = spi0_internal_chipselect,
	.num_chipselect	       = ARRAY_SIZE(spi0_internal_chipselect),
};

int magnolia2_get_extio_id(int sel)
{
	return 1;
}
EXPORT_SYMBOL(magnolia2_get_extio_id);

int magnolia2_is_audio_enable(void)
{
	return uboot_tag.audio == 0 ? 0 : -1;
}
EXPORT_SYMBOL(magnolia2_is_audio_enable);

#if defined(CONFIG_USB_NET_SMSC95XX) || defined(CONFIG_USB_NET_SMSC95XX_MODULE)
struct macaddr_table {
	u8 mac_key[3];
	u8 mac_val[3];
};

struct macaddr_table fec2smsc[] = {
	{
/*		{0x77, 0x81, 0x5f}, {0x77, 0x02, 0x00}*/
		{0x77, 0x81, 0x67}, {0x77, 0x02, 0x00}
	},{
		{0x77, 0x81, 0x60}, {0x77, 0x02, 0x01}
	},{
		{0x77, 0x81, 0x61}, {0x77, 0x02, 0x02}
	},{
		{0x77, 0x81, 0x62}, {0x77, 0x02, 0x03}
	},{
		{0x77, 0x81, 0x63}, {0x77, 0x02, 0x04}
	},{
		{0x77, 0x81, 0x64}, {0x77, 0x02, 0x05}
	},{
		{0x77, 0x81, 0x65}, {0x77, 0x02, 0x06}
	},{
		{0x77, 0x81, 0x66}, {0x77, 0x02, 0x07}
	},{
		{0x77, 0x81, 0x34}, {0x77, 0x02, 0x08}
	},{
		{0x77, 0x81, 0x35}, {0x77, 0x02, 0x09}
	}
};

int magnolia2_smsc95xx_get_ether_addr(u8 *data)
{
	int i;
	__u8 *macAddr = uboot_tag.macAddr;

	for (i = 0; i <= sizeof(fec2smsc); i++) {
		if (macAddr[3] == fec2smsc[i].mac_key[0] &&
		    macAddr[4] == fec2smsc[i].mac_key[1] &&
		    macAddr[5] == fec2smsc[i].mac_key[2]) {
			data[0] = 0x00;
			data[1] = 0x80;
			data[2] = 0x6d;
			data[3] = fec2smsc[i].mac_val[0];
			data[4] = fec2smsc[i].mac_val[1];
			data[5] = fec2smsc[i].mac_val[2];

			return 0;
		}
	}

	return -1;
}
EXPORT_SYMBOL(magnolia2_smsc95xx_get_ether_addr);
#endif

int magnolia2_get_uart_info(int port, u32 *enable, u32 *type, u32 *config)
{
	int res = 0;

	switch (port) {
	case 0:
		*enable = uboot_tag.rs1.enable;
#ifndef CONFIG_MXC_UART_BUGGY_UBOOTOPT
		*type = uboot_tag.rs1.type;
		*config = uboot_tag.rs1.config;
#else
		*type = uboot_tag.rs1.config;
		*config = uboot_tag.rs1.type;
#endif
		break;

	case 1:
		*enable = uboot_tag.rs2.enable;
#ifndef CONFIG_MXC_UART_BUGGY_UBOOTOPT
		*type = uboot_tag.rs2.type;
		*config = uboot_tag.rs2.config;
#else
		*type = uboot_tag.rs2.config;
		*config = uboot_tag.rs2.type;
#endif
		break;

	case 2:
		*enable = 1;
		*type = 0;
		*config = 0;
		break;

	default:
		res = -1;
	}

	return res;
}
EXPORT_SYMBOL(magnolia2_get_uart_info);

/*--------------------------------------------------------------------*
 * Ext-IO 4							      *
 *--------------------------------------------------------------------*/

#define EXTIO4_PIN_IRQ	      IMX_GPIO_NR(3, 2)

/*
 * The serial port definition structure (FOMA)
 */
static struct plat_serial8250_port foma_serial_platform_data[] = {
	{
		.membase  = (void *)(MX35_IO_ADDRESS(MX3x_CS4_BASE_ADDR) + MAGNOLIA2_EXT_UART_FOMA),
		.mapbase  = (unsigned long)(MX3x_CS4_BASE_ADDR + MAGNOLIA2_EXT_UART_FOMA),
		.irq	  = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype	  = UPIO_MEM,
		.flags	  = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{},
};

static struct platform_device foma_serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = foma_serial_platform_data,
	},
};

static struct resource foma_extio_resource = {
	.start = MX3x_CS4_BASE_ADDR,
	.end = MX3x_CS4_BASE_ADDR + 3,
	.flags = IORESOURCE_MEM,
};

static struct platform_device foma_extio_device = {
	.name = "foma_extio",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = 1,
	.resource = &foma_extio_resource,
};

static struct resource rs422_switch_resources[] = {
	{
		.start = MX3x_CS4_BASE_ADDR,
		.end = MX3x_CS4_BASE_ADDR + 4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device rs422_switch_device = {
	.name = "rs422_switch",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = ARRAY_SIZE(rs422_switch_resources),
	.resource = rs422_switch_resources,
};

static struct resource umfxs_resources[] = {
	{
		.start = MX3x_CS4_BASE_ADDR,
		.end = MX3x_CS4_BASE_ADDR + 32,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device umfxs_device = {
	.name = "umfxs",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = ARRAY_SIZE(umfxs_resources),
	.resource = umfxs_resources,
};

static struct resource kcmv_io_resources[] = {
	{
		.start = MX3x_CS4_BASE_ADDR,
		.end = MX3x_CS4_BASE_ADDR + 3,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device kcmv_io_device = {
	.name = "kcmv_io",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = ARRAY_SIZE(kcmv_io_resources),
	.resource = kcmv_io_resources,
};

static struct resource kcmp_io_resources[] = {
	{
		.start = MX3x_CS4_BASE_ADDR,
		.end = MX3x_CS4_BASE_ADDR + 3,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device kcmp_io_device = {
	.name = "kcmp_io",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = ARRAY_SIZE(kcmp_io_resources),
	.resource = kcmp_io_resources,
};

/*
 * The serial port definition structure (XBee)
 */
static struct plat_serial8250_port xbee_serial_platform_data[] = {
	{
		.membase  = (void *)(MX35_IO_ADDRESS(MX3x_CS4_BASE_ADDR) + MAGNOLIA2_EXT_UART_XBEE),
		.mapbase  = (unsigned long)(MX3x_CS4_BASE_ADDR + MAGNOLIA2_EXT_UART_XBEE),
		.irq	  = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype	  = UPIO_MEM,
		.flags	  = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{},
};

static struct platform_device xbee_serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = xbee_serial_platform_data,
	},
};

/*
 * The serial port definition structure (DB9-UART)
 */
#ifdef CONFIG_MAGNOLIA2_EXTRS485
static void db9_serial_trx_control(struct uart_port *port, int txenable, int rxenable);
#endif

static struct plat_serial8250_port db9_serial_platform_data[] = {
	{
		.membase  = (void *)(MX35_IO_ADDRESS(MX3x_CS4_BASE_ADDR) + MAGNOLIA2_EXT_UART_DB9),
		.mapbase  = (unsigned long)(MX3x_CS4_BASE_ADDR + MAGNOLIA2_EXT_UART_DB9),
		.irq	  = IMX_GPIO_TO_IRQ(EXTIO4_PIN_IRQ),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype	  = UPIO_MEM,
		.flags	  = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,

#ifdef CONFIG_MAGNOLIA2_EXTRS485
		.trxctrl = db9_serial_trx_control,
#endif
	},
	{},
};

#ifdef CONFIG_MAGNOLIA2_EXTRS485
static struct resource db9_serial_io_resources[] = {
	{
		.start = MX3x_CS4_BASE_ADDR + 8,
		.end = MX3x_CS4_BASE_ADDR + 8,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device db9_serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = db9_serial_platform_data,
	},
};

struct db9_rs485_info {
	unsigned long		iobase;
	unsigned long		iobase_8250;
	struct delayed_work	trx_work;

	unsigned long		baud;
};

static struct workqueue_struct *db9_rs485_workqueue;
static struct db9_rs485_info db9_info;

#define TXENABLE   (1 << 0)
#define RXENABLE_N (1 << 1)

static inline void db9_driver_ctrl(unsigned long ioaddr, int txenable, int rxenable)
{
	volatile u8 reg;

	reg = readb(db9_info.iobase) & ~(TXENABLE|RXENABLE_N);

	if (txenable)
		reg |= TXENABLE;
	if (!rxenable)
		reg |= RXENABLE_N;

	writeb(reg, db9_info.iobase);
}

static void db9_rs485_worker(struct work_struct *work)
{
	unsigned long char_time, interval_us, loops_us;
	volatile u8 reg;

	if (db9_info.baud > 0) {
		char_time = 1000000 / (db9_info.baud / 10);
		interval_us =  char_time / 10;

		for (loops_us = char_time * 16; loops_us > 0; loops_us -= interval_us) {
			reg = readb(db9_info.iobase_8250 + UART_LSR);
			if (reg & UART_LSR_TEMT)
				break;

			udelay(interval_us);
		}
	}

	db9_driver_ctrl(db9_info.iobase, 0, 1);
}

static void db9_serial_setup_resource(struct plat_serial8250_port *port)
{
	struct resource *res = &db9_serial_io_resources[0];
	unsigned long db9_iobase;
	int len = res->end - res->start + 1;

	db9_rs485_workqueue = create_singlethread_workqueue("db9_rs485");
	if (unlikely(!db9_rs485_workqueue))
		return;

	INIT_DELAYED_WORK(&db9_info.trx_work, db9_rs485_worker);

	if (request_mem_region(res->start, len, "db9_uart_ctrl")) {
		db9_iobase = (unsigned long) ioremap(res->start, len);
		printk(" DB9 RS-485/422 control ioaddr: 0x%08x -> 0x%08lx (mapped)\n",
		       res->start, db9_iobase);

		db9_info.iobase = db9_iobase;
	}
}

static void db9_serial_trx_control(struct uart_port *port, int txenable, int rxenable)
{
	if (txenable == 0 && rxenable == 1) {
		/* RS-485 transmit finished */
		db9_info.baud = port->baud;
		db9_info.iobase_8250 = (unsigned int) port->membase;

		queue_delayed_work(db9_rs485_workqueue, &db9_info.trx_work, 0);
	} else
		db9_driver_ctrl(db9_info.iobase, txenable, rxenable);
}
#endif

static struct resource um01hw_extio_resource = {
	.start = MX3x_CS4_BASE_ADDR,
	.end = MX3x_CS4_BASE_ADDR + 2,
	.flags = IORESOURCE_MEM,
};

static struct platform_device um01hw_extio_device = {
	.name = "um01hw_extio",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = 1,
	.resource = &um01hw_extio_resource,
};

#define LED_PORT(_name, _shift) \
	{					\
		.name = (_name),		\
			.shift	= (_shift),	\
			}

static struct resource um01hw_led_resources[] = {
	[0] = {
		.start = MX3x_CS4_BASE_ADDR + 3,
		.end = MX3x_CS4_BASE_ADDR + 3,
		.flags = IORESOURCE_MEM,
	},
};

static struct magnolia2_led_port um01hw_led_ports[] = {
	LED_PORT("um01hw_r1", 7),
	LED_PORT("um01hw_g1", 6),
	LED_PORT("um01hw_r2", 5),
	LED_PORT("um01hw_g2", 4),
};

static struct magnolia2_led_private um01hw_led_priv = {
	.nr_ports = ARRAY_SIZE(um01hw_led_ports),
	.ports = um01hw_led_ports,
};

static struct platform_device um01hw_led_device = {
	.name = "um01hw_led",
	.id = 0,
	.dev = {
		.platform_data = &um01hw_led_priv,
	},
	.num_resources = ARRAY_SIZE(um01hw_led_resources),
	.resource = um01hw_led_resources,
};


static struct resource xbee_extio_resource = {
	.start = MX3x_CS4_BASE_ADDR + 8,
	.end = MX3x_CS4_BASE_ADDR + 0x0a,
	.flags = IORESOURCE_MEM,
};

static struct platform_device xbee_extio_device = {
	.name = "xbee_extio",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = 1,
	.resource = &xbee_extio_resource,
};


static int __init magnolia2_init_extio4(void)
{
	u32 cs4_board_id, cs4_board_rev;

#define EXTIO4_PAD_CTL IOMUX_PAD(0x734, 0x2d0, 5, 0x0, 0, \
				 (PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE | \
				  PAD_CTL_PUS_100K_UP | PAD_CTL_PUE_PUD))

	cs4_board_id  = uboot_tag.cs4.id;
	cs4_board_rev = uboot_tag.cs4.revision;

	if (cs4_board_rev != 0xff && cs4_board_id != 0x0f)
		printk("Magnolia2 External I/O(CS4): board_id = %d, board_rev = %d\n",
		       cs4_board_id, cs4_board_rev);
	else
		return 0;

	switch (cs4_board_id) {
	case 0x01:
		/* FOMA Ubiquitous module */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "foma_uart_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&foma_serial_device);
		platform_device_register(&foma_extio_device);
		break;

	case 0x02:
		/* FL-net module */
		// an expansion FL-net card (flnet)
//		  platform_device_register(&flnet_card_device);
		// an expansion FL-net card (fldin)
//		  platform_device_register(&fldin_card_device);
		break;

	case 0x03:
		/* Fire and Disaster Management Agency RS-422 switcher */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "rs422_sw_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&rs422_switch_device);
		break;

	case 0x04:
		/* UM01-HW, FXS interface module (NA-001/F) */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "umfxs_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&umfxs_device);
		break;

	case 0x08:
		/* KCMV-200 module */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "kcmv_wakeup_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&kcmv_io_device);
		break;

	case 0x09:
		/* KCMP module */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "kcmp_wakeup_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&kcmp_io_device);
		break;

	case 0x0b:
		/* UM01-HW & XBee module */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "xbee_uart_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

		platform_device_register(&xbee_serial_device);
		platform_device_register(&um01hw_extio_device);
		platform_device_register(&um01hw_led_device);
		platform_device_register(&xbee_extio_device);
		break;

	case 0x0c:
	{
#ifdef CONFIG_MAGNOLIA2_EXTRS485
		struct plat_serial8250_port *port;
		port = (struct plat_serial8250_port *) db9_serial_device.dev.platform_data;
#endif
		/* UM01-HW & RS-485/232 module */
		mxc_iomux_v3_setup_pad(EXTIO4_PAD_CTL);
		gpio_request(EXTIO4_PIN_IRQ, "ext_uart_irq");
		gpio_direction_input(EXTIO4_PIN_IRQ);

#ifdef CONFIG_MAGNOLIA2_EXTRS485
		if (cs4_board_rev == 4)
			db9_serial_setup_resource(port);
		else
			port[0].trxctrl = NULL;

		platform_device_register(&db9_serial_device);
#endif
		platform_device_register(&um01hw_extio_device);
		platform_device_register(&um01hw_led_device);

		break;
	}
	default:
		break;
	}

	return 0;
}

/*--------------------------------------------------------------------*
 * Ext-IO 5							      *
 *--------------------------------------------------------------------*/

#define EXTIO5_PIN_IRQ	      IMX_GPIO_NR(3, 1)

/* CS5: AI/DIO Extension */
static struct spi_board_info mxc_spi_board_info_aidio[] __initdata = {
	{
		.modalias = "ltc185x",
		.max_speed_hz = 1500000,	/* max spi SCK clock speed in HZ */
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_3,
	},
	{
		.modalias = "ltc185x",
		.max_speed_hz = 1500000,	/* max spi SCK clock speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_MODE_3,
	},
};

static struct resource dio_extio_resource[] = {
	{
		.start = MX3x_CS5_BASE_ADDR,
		.end = MX3x_CS5_BASE_ADDR + 0x84 - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IMX_GPIO_TO_IRQ(EXTIO5_PIN_IRQ),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device dio_extio_device = {
	.name = "magnolia2_DIO",
	.id = 0,
	.dev = {
		// Nothing
	},
	.num_resources = 2,
	.resource = dio_extio_resource,
};

static iomux_v3_cfg_t dio_extio_pads[] = {
#define PAD_CONFIG_SPI (PAD_CTL_DRV_3_3V | PAD_CTL_HYS_SCHMITZ |	\
			PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |		      \
			PAD_CTL_SRE_SLOW |				  \
			PAD_CTL_PUS_100K_DOWN | PAD_CTL_DRV_NORMAL)
	MX35_PAD_CSPI1_MOSI__CSPI1_MOSI |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* MOSI */
	MX35_PAD_CSPI1_MISO__CSPI1_MISO |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* MISO */
	MX35_PAD_CSPI1_SCLK__CSPI1_SCLK |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* SCLK */
	MX35_PAD_CSPI1_SPI_RDY__CSPI1_RDY,
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* RDY  */
#undef PAD_CONFIG_SPI
#define PAD_CONFIG_SPI (PAD_CTL_DRV_3_3V | PAD_CTL_HYS_SCHMITZ |	\
			PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |		      \
			PAD_CTL_PUS_100K_UP | PAD_CTL_ODE_CMOS |	\
			PAD_CTL_SRE_SLOW | PAD_CTL_DRV_NORMAL)
	MX35_PAD_CSPI1_SS0__CSPI1_SS0,
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* SS0  */
	MX35_PAD_CSPI1_SS1__CSPI1_SS1 |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* SS1  */
	MX35_PAD_GPIO1_1__CSPI1_SS2 |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* SS2  */
	MX35_PAD_ATA_CS0__CSPI1_SS3 |
	MUX_PAD_CTRL(PAD_CONFIG_SPI),	     /* SS3  */
#undef PAD_CONFIG_SPI
};

/* CS5: PWR/CAN Extension */
static struct spi_board_info mxc_spi_board_info_can[] __initdata = {
	{
		.modalias = "cortex-m3",
		.max_speed_hz = 1000000,	/* max spi SCK clock speed in HZ */
		.bus_num = 1,
		.chip_select = 0,
	},
};

static struct plat_serial8250_port can_serial_platform_data[] = {
	{
		.membase  = (void *)(MX35_IO_ADDRESS(MX3x_CS5_BASE_ADDR) + MAGNOLIA2_EXT_UART_CAN),
		.mapbase  = (unsigned long)(MX3x_CS5_BASE_ADDR + MAGNOLIA2_EXT_UART_CAN),
		//.irq	    = IOMUX_TO_IRQ(MX35_PIN_ATA_DA1),
		.irq	  = IMX_GPIO_TO_IRQ(EXTIO5_PIN_IRQ),
		.uartclk  = 7372800,
		.regshift = 1,
		.iotype	  = UPIO_MEM,
		.flags	  = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{},
};

static struct platform_device can_serial_device = {
	.name = "serial8250",
	.id = 1,
	.dev = {
		.platform_data = can_serial_platform_data,
	},
};

static int __init magnolia2_init_extio5(void)
{
	u32 cs5_board_id, cs5_board_rev;

	cs5_board_id  = uboot_tag.cs5.id;
	cs5_board_rev = uboot_tag.cs5.revision;

	if (cs5_board_rev != 0xff && cs5_board_id != 0x0f)
		printk("Magnolia2 External I/O(CS5): board_id = %d, board_rev = %d\n",
		       cs5_board_id, cs5_board_rev);
	else
		return 0;

	switch (cs5_board_id) {
	case 0x01:
		/* AI/DIO module */
		mxc_iomux_v3_setup_multiple_pads(dio_extio_pads,
						 ARRAY_SIZE(dio_extio_pads));
		platform_device_register(&dio_extio_device);
		spi_register_board_info(mxc_spi_board_info_aidio,
					ARRAY_SIZE(mxc_spi_board_info_aidio));
		break;

	case 0x02:
		/* PWR/CAN module */
		platform_device_register(&can_serial_device);
		spi_register_board_info(mxc_spi_board_info_can,
					ARRAY_SIZE(mxc_spi_board_info_can));

		/* update bus timing */
		printk("Update CS5 bus timing...\n");
//		  __raw_writel(0x0000d484, CSCR_U(5));
//		  __raw_writel(0x8c884561, CSCR_L(5));
//		  __raw_writel(0xac8e1300, CSCR_A(5));

		break;

	case 0x08:
		/* DI-17 module */
		platform_device_register(&dio_extio_device);
		break;

	default:
		break;
	}

	return 0;
}

static struct mtd_partition mxc_nor_partitions[] = {
	{
		.name = "uboot",
		.size = 256 * 1024,
		.offset = 0x00000000,
		.mask_flags = MTD_WRITEABLE	   /* force read-only */
	},{
		.name = "ubootenv",
		.size = 128 * 1024,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
	},{
		.name = "config",
		.size = 128 * 1024,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
	},{
		.name = "kernel",
		.size = 3584 * 1024,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
	},{
#ifndef CONFIG_MAGNOLIA2_MTD_SPLIT
		.name = "rootfs",
		.size = 28 * 1024 * 1024,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
#else
		.name = "rootfs",
		.size = (CONFIG_MAGNOLIA2_MTD_ROOTPART_MB * 1024 * 1024),
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
	},{
//		.name = "usr_squash",
		.name = CONFIG_MAGNOLIA2_MTD_OPTPART,
		.size = (28 - CONFIG_MAGNOLIA2_MTD_ROOTPART_MB) * 1024 * 1024,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = 0
	},{
		.name = "rootfs_compat",
		.size = 28 * 1024 * 1024,
		.offset = 4096 * 1024,
		.mask_flags = 0
#endif
	},
};

/* LED */
static struct resource magnolia2_led_resources[] = {
	[0] = {
		.start = MAGNOLIA2_LED_ADDR,
		.end = MAGNOLIA2_LED_ADDR,
		.flags = IORESOURCE_MEM,
	},
};

static struct magnolia2_led_port magnolia2_led_ports[] = {
	LED_PORT("led_g0", 3),
	LED_PORT("led_g1", 2),
	LED_PORT("led_g2", 1),
	LED_PORT("led_g3", 0),
	LED_PORT("led_r0", 7),
	LED_PORT("led_r1", 6),
	LED_PORT("led_r2", 5),
	LED_PORT("led_r3", 4)
};

static struct magnolia2_led_private magnolia2_led_priv = {
	.nr_ports = ARRAY_SIZE(magnolia2_led_ports),
	.ports = magnolia2_led_ports,
};

static struct platform_device magnolia2_led_device = {
	.name = "magnolia2_led",
	.id = 0,
	.dev = {
		.platform_data = &magnolia2_led_priv,
	},
	.num_resources = ARRAY_SIZE(magnolia2_led_resources),
	.resource = magnolia2_led_resources,
};

static void magnolia2_led_init(void)
{
	platform_device_register(&magnolia2_led_device);
};

/* PUSH Swtich */
struct magnolia2_gpio_port magnolia2_switch_in_ports[] = {
	GPIO_PORT("sw_init", IMX_GPIO_NR(2, 26)),
	GPIO_PORT("sw_eject", IMX_GPIO_NR(2, 27)),
};

static struct magnolia2_gpio_private magnolia2_switch_in_priv = {
	.nr_gpio = ARRAY_SIZE(magnolia2_switch_in_ports),
	.ports	 = magnolia2_switch_in_ports,
};

static struct platform_device magnolia2_switch_in_device = {
	.name	= "magnolia2_pushsw",
	.id	= 0,
	.dev = {
		.platform_data = &magnolia2_switch_in_priv,
	},
};

static void magnolia2_switch_in_init(void)
{
	platform_device_register(&magnolia2_switch_in_device);
}

/* DIPSW */
struct magnolia2_gpio_port magnolia2_dipsw_in_ports[] = {
	GPIO_PORT("dipsw1", IMX_GPIO_NR(2, 28)),
	GPIO_PORT("dipsw2", IMX_GPIO_NR(2, 29)),
	GPIO_PORT("dipsw3", IMX_GPIO_NR(2, 30)),
	GPIO_PORT("dipsw4", IMX_GPIO_NR(2, 31)),
};

static struct magnolia2_gpio_private magnolia2_dipsw_in_priv = {
	.nr_gpio = ARRAY_SIZE(magnolia2_dipsw_in_ports),
	.ports = magnolia2_dipsw_in_ports,
};

static struct platform_device magnolia2_dipsw_in_device = {
	.name = "magnolia2_dipsw",
	.id   = 0,
	.dev = {
		.platform_data = &magnolia2_dipsw_in_priv,
	},
};

static void magnolia2_dipsw_in_init(void)
{
	magnolia2_dipsw_in_priv.nr_gpio = ARRAY_SIZE(magnolia2_dipsw_in_ports);
	platform_device_register(&magnolia2_dipsw_in_device);
}

#ifdef CONFIG_MXC_UART1_USE_AS_GPIO
/* GPIO DIO */
struct magnolia2_gpio_port magnolia2_gpio_dio_ports[] = {
	GPIO_PORT("gpio_dio0", IMX_GPIO_NR(3, 11)),
	GPIO_PORT("gpio_dio1", IMX_GPIO_NR(3, 10)),
	GPIO_PORT("gpio_dio2", IMX_GPIO_NR(3, 13)),
	GPIO_PORT("gpio_dio3", IMX_GPIO_NR(3, 12)),
};

static struct magnolia2_gpio_private magnolia2_gpio_dio_priv = {
	.nr_gpio = ARRAY_SIZE(magnolia2_gpio_dio_ports),
	.ports   = magnolia2_gpio_dio_ports,
};

static struct platform_device magnolia2_gpio_dio_device = {
	.name = "magnolia2_gpio_dio",
	.id   = 0,
	.dev = {
		.platform_data = &magnolia2_gpio_dio_priv,
	},
};

static void magnolia2_gpio_dio_init(void)
{
	magnolia2_gpio_dio_priv.nr_gpio = ARRAY_SIZE(magnolia2_gpio_dio_ports);
	platform_device_register(&magnolia2_gpio_dio_device);
}
#else
static void magnolia2_gpio_dio_init(void)
{
}
#endif /* CONFIG_MXC_UART1_USE_AS_GPIO */

/* Sound */
#if defined(CONFIG_SND_SOC_TLV320AIC3X) \
	|| defined(CONFIG_SND_SOC_TLV320AIC3X_MODULE)
static const struct imxi2c_platform_data magnolia2_i2c1_data __initconst = {
	.bitrate = 50000,
};

static struct i2c_board_info magnolia2_i2c1_devices[] = {
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	},
};

static const struct imx_ssi_platform_data magnolia2_ssi_pdata __initconst = {
	.flags = IMX_SSI_SYN | IMX_SSI_USE_I2S_SLAVE,
};

static void mxc_init_tlv320aic31(void)
{
	printk("Magnolia2 Audio: %sabled.\n",
	       (uboot_tag.audio == 0) ? "En" : "Dis");

	if (uboot_tag.audio != 0)
		return;

	/* SSI unit master I2S codec connected to SSI_AUD4 */
	mxc_audmux_v2_configure_port(0,
			MXC_AUDMUX_V2_PTCR_SYN |
			MXC_AUDMUX_V2_PTCR_TFSDIR |
			MXC_AUDMUX_V2_PTCR_TFSEL(3) |
			MXC_AUDMUX_V2_PTCR_TCLKDIR |
			MXC_AUDMUX_V2_PTCR_TCSEL(3),
			MXC_AUDMUX_V2_PDCR_RXDSEL(3)
	);
	mxc_audmux_v2_configure_port(3,
			MXC_AUDMUX_V2_PTCR_SYN,
			MXC_AUDMUX_V2_PDCR_RXDSEL(0)
	);

	imx35_add_imx_ssi(0, &magnolia2_ssi_pdata);

	i2c_register_board_info(1, magnolia2_i2c1_devices,
				ARRAY_SIZE(magnolia2_i2c1_devices));
	imx35_add_imx_i2c1(&magnolia2_i2c1_data);
}
#else
static void mxc_init_tlv320aic31(void)
{
}
#endif

/*
 * uart
 */
#define TXEN_PIN IMX_GPIO_NR(3, 4)
#define RXEN_PIN IMX_GPIO_NR(3, 5)
#define DSR1_PIN IMX_GPIO_NR(2, 20)
#define DSR2_PIN IMX_GPIO_NR(1, 11)

static struct imxuart_platform_data uart_pdata[] = {
	{
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.flags = 0,
	},
};

static iomux_v3_cfg_t uart0_rs232_pads[] = {
	/* TxD */
	MX35_PAD_TXD1__UART1_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RxD */
	MX35_PAD_RXD1__UART1_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* RTS */
	MX35_PAD_RTS1__UART1_RTS |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* CTS */
	MX35_PAD_CTS1__UART1_CTS |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* DTR */
	MX35_PAD_ATA_DATA6__UART1_DTR |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* DSR (GPIO) */
	MX35_PAD_ATA_DATA7__GPIO2_20 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RI */
	MX35_PAD_ATA_DATA8__UART1_RI |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* DCD */
	MX35_PAD_ATA_DATA9__UART1_DCD |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP)
};

static iomux_v3_cfg_t uart0_rs485_pads[] = {
	/* TxD */
	MX35_PAD_TXD1__UART1_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RxD */
	MX35_PAD_RXD1__UART1_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* TXEN */
	MX35_PAD_MLB_DAT__GPIO3_4 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RXENn */
	MX35_PAD_MLB_SIG__GPIO3_5 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN)
};

static iomux_v3_cfg_t uart1_rs232_pads[] = {
	/* TxD */
	MX35_PAD_TXD2__UART2_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RxD */
	MX35_PAD_RXD2__UART2_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* RTS */
	MX35_PAD_RTS2__UART2_RTS |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* CTS */
	MX35_PAD_CTS2__UART2_CTS |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* DTR */
	MX35_PAD_TX5_RX0__UART2_DTR |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* DSR (GPIO) */
	MX35_PAD_TX4_RX1__GPIO1_11 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),
	/* RI */
	MX35_PAD_TX1__UART2_RI |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* DCD */
	MX35_PAD_TX0__UART2_DCD |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
};

static iomux_v3_cfg_t uart1_dio_pads[] = {
	/* TxD as GPIO */
	MX35_PAD_TXD2__GPIO3_11 |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* RxD as GPIO*/
	MX35_PAD_RXD2__GPIO3_10 |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* CTS as GPIO */
	MX35_PAD_CTS2__GPIO3_13 |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
	/* RTS as GPIO */
	MX35_PAD_RTS2__GPIO3_12 |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),
};

static void magnolia2_init_uart(void)
{
	int port, enable, type, config;

	for (port = 0; port < ARRAY_SIZE(uart_pdata); port++) {
		magnolia2_get_uart_info(port, &enable, &type, &config);

		if (port < 2) {
			printk("Magnolia2 UART%d: ", port);
			if (enable == 0) {
				printk("Disabled.\n");
				continue;
			}
		}
		switch (port) {
		case 0:
			if (type == 0) {
				printk("RS-232\n");
				mxc_iomux_v3_setup_multiple_pads(
					uart0_rs232_pads,
					ARRAY_SIZE(uart0_rs232_pads));
				gpio_request(DSR1_PIN, "dsr1");
				gpio_direction_input(DSR1_PIN);
			} else {
				if (config == 0)
					printk("RS-485\n");
				else
					printk("RS-422\n");
				mxc_iomux_v3_setup_multiple_pads(
					uart0_rs485_pads,
					ARRAY_SIZE(uart0_rs485_pads));

				gpio_request(TXEN_PIN, "rs-485_txen");
				gpio_direction_output(TXEN_PIN, 0);
				gpio_request(RXEN_PIN, "rs-485_rxenn");
				gpio_direction_output(RXEN_PIN, 1);
				uart_pdata[port].flags = 0;
			}
			break;
		case 1:
			if (type == 0) {
				if (config == 0) {
					printk("RS-232\n");
				} else {
#ifndef CONFIG_MXC_UART1_USE_AS_GPIO
					printk("FeliCa R/W\n");
#else
					printk("DIO\n");
					mxc_iomux_v3_setup_multiple_pads(
						uart1_dio_pads,
						ARRAY_SIZE(uart1_dio_pads));

					/* do not register as UART */
					continue;
#endif
				}

				mxc_iomux_v3_setup_multiple_pads(
					uart1_rs232_pads,
					ARRAY_SIZE(uart1_rs232_pads));
				gpio_request(DSR2_PIN, "dsr2");
				gpio_direction_input(DSR2_PIN);
			} else {
				if (config == 0)
					printk("RS-485\n");
				else
					printk("RS-422\n");
			}
			break;
		default:
			break;
		}
		uart_pdata[port].port_index = port;
		uart_pdata[port].driver_type = type;
		uart_pdata[port].driver_duplex = config;

		imx35_add_imx_uart(port, &uart_pdata[port]);
	}
}

void magnolia2_uartgpio_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(
		uart1_dio_pads,
		ARRAY_SIZE(uart1_dio_pads));
}
EXPORT_SYMBOL(magnolia2_uartgpio_init);

void magnolia2_uart_open(struct imxuart_platform_data *port)
{
	if (port->port_index == 0) {
		if (port->driver_type == 1) {
			if (port->driver_duplex == 0) {
				/* RS-485 Enable RxD, Disable TxD */
				//printk("#485 open\n");
				gpio_set_value(TXEN_PIN, 0);
				gpio_set_value(RXEN_PIN, 0);
			} else {
				/* RS-422 Enable TxD/RxD */
				//printk("#422 open\n");
				gpio_set_value(TXEN_PIN, 1);
				gpio_set_value(RXEN_PIN, 0);
			}
		}
	}
}
EXPORT_SYMBOL(magnolia2_uart_open);

void magnolia2_uart_close(struct imxuart_platform_data *port)
{
	if (port->port_index == 0) {
		if (port->driver_type == 1) {
			/* Disable TxD/RxD */
			//printk("#422/485 close\n");
			gpio_set_value(TXEN_PIN, 0);
			gpio_set_value(RXEN_PIN, 1);
		}
	}
}
EXPORT_SYMBOL(magnolia2_uart_close);

void magnolia2_uart_txrx(struct imxuart_platform_data *port, int txe, int rxe)
{
	if (port->port_index == 0) {
		if (port->driver_type == 1 && port->driver_duplex == 0) {
			/* RS-485 only */
			gpio_set_value(TXEN_PIN, txe);
			gpio_set_value(RXEN_PIN, !rxe);
		}
	}
}
EXPORT_SYMBOL(magnolia2_uart_txrx);

int magnolia2_uart_getdsr(struct imxuart_platform_data *port)
{
	int val = 1;

	if (port->driver_type == 0) {
		if (port->port_index == 0) {
			val = gpio_get_value(DSR1_PIN);
		} else if (port->port_index == 1) {
			val = gpio_get_value(DSR2_PIN);
		}
	}
	return val;
}
EXPORT_SYMBOL(magnolia2_uart_getdsr);

void (*magnolia2_power_off_prepare)(void);
EXPORT_SYMBOL(magnolia2_power_off_prepare);

/*
 *
 */
static void magnolia2_power_off(void)
{
	printk("%s: start...\n", __FUNCTION__);

	if (magnolia2_power_off_prepare)
		magnolia2_power_off_prepare();
}

static int earlywdt_enable = 0;

static int __init magnolia2_earlywdt_setup(char *str)
{
	if (strncmp(str, "1", 1) == 0) {
		printk("early WDT enable.\n");
		earlywdt_enable = 1;
	}

	return 1;
}
__setup("early_wdt=", magnolia2_earlywdt_setup);

void magnolia2_set_earlywdt(int flag)
{
	if (flag) {
		printk("early WDT enable.\n");
		earlywdt_enable = 1;
	} else {
		printk("early WDT disable.\n");
		earlywdt_enable = 0;
	}
}
EXPORT_SYMBOL(magnolia2_set_earlywdt);

static int led_dme_mode = 0;

static int __init magnolia2_ledmode_setup(char *str)
{
	if (strncmp(str, "1", 1) == 0) {
		printk("FOMA LED DME mode.\n");
		led_dme_mode = 1;
	}

	return 1;
}
__setup("led_dme_mode=", magnolia2_ledmode_setup);

int magnolia2_get_led_mode(void)
{
	return led_dme_mode;
}
EXPORT_SYMBOL(magnolia2_get_led_mode);

#ifndef __MXC_WDT_H__
#define MXC_WDT_WCR		0x00
#define MXC_WDT_WSR		0x02
#define MXC_WDT_WRSR		0x04
#define WCR_WOE_BIT		(1 << 6)
#define WCR_WDA_BIT		(1 << 5)
#define WCR_SRS_BIT		(1 << 4)
#define WCR_WRE_BIT		(1 << 3)
#define WCR_WDE_BIT		(1 << 2)
#define WCR_WDBG_BIT		(1 << 1)
#define WCR_WDZST_BIT		(1 << 0)
#define WDT_MAGIC_1		0x5555
#define WDT_MAGIC_2		0xAAAA

#define TIMER_MARGIN_MAX	    127
#define TIMER_MARGIN_DEFAULT	    60	      /* 60 secs */
#define TIMER_MARGIN_MIN	1
#endif

static void __init magnolia2_misc_init(void)
{
	if (uboot_tag.early_wdt == 0) {
		printk("early WDT enable (set by TAG).\n");
		earlywdt_enable = 1;
	}

	if (uboot_tag.dme_led == 0) {
		printk("FOMA LED DME mode (set by TAG).\n");
		led_dme_mode = 1;
	}
}

static void __init early_wdt_init(void)
{
	u32 wdt_base_reg;
	struct clk *mxc_wdt_clk;
	u16 val;

#define WDOG_SEC_TO_COUNT(s)  (((s * 2) - 1) << 8)

	wdt_base_reg = (u32) MX35_IO_ADDRESS(MX35_WDOG_BASE_ADDR);

	mxc_wdt_clk = clk_get_sys("imx2-wdt.0", NULL);
	if (!IS_ERR(mxc_wdt_clk))
		clk_enable(mxc_wdt_clk);

	mb();

	val = __raw_readw(wdt_base_reg + MXC_WDT_WRSR) & 0x0003;
	printk("i.MX35 WDT: WRSR = 0x%04x\n", val);

	if (val & 0x0002)
		printk(" 0x02: Reset is the result of a WDOG time-out.\n");
	if (val & 0x0001)
		printk(" 0x01: Reset is the result of a software reset.\n");

	if (earlywdt_enable == 1) {
		printk(" Starting early WDT, 128seconds.\n");
		/* wdt_config */
		val = __raw_readw(wdt_base_reg + MXC_WDT_WCR);
		val |= 0xFF00 | WCR_WOE_BIT | WCR_WDA_BIT | WCR_SRS_BIT;
		val &= ~WCR_WRE_BIT;
		__raw_writew(val, wdt_base_reg + MXC_WDT_WCR);

		/* wdt_set_timeout */
		val = __raw_readw(wdt_base_reg + MXC_WDT_WCR);
		val = (val & 0x00FF) | WDOG_SEC_TO_COUNT(128);
		__raw_writew(val, wdt_base_reg + MXC_WDT_WCR);

		/* wdt_enable */
		val = __raw_readw(wdt_base_reg + MXC_WDT_WCR);
		val |= WCR_WDE_BIT;
		__raw_writew(val, wdt_base_reg + MXC_WDT_WCR);

		/* wdt_ping */
		__raw_writew(WDT_MAGIC_1, wdt_base_reg + MXC_WDT_WSR);
		__raw_writew(WDT_MAGIC_2, wdt_base_reg + MXC_WDT_WSR);
	}
}

static struct physmap_flash_data magnolia2_flash_data = {
	.width    = 2,
	.parts    = mxc_nor_partitions,
	.nr_parts = ARRAY_SIZE(mxc_nor_partitions),
};

static struct resource magnolia2_flash_resource = {
	.start = 0xa0000000,
	.end   = 0xa1ffffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device magnolia2_flash = {
	.name = "physmap-flash",
	.id   = 0,
	.dev  = {
		.platform_data	= &magnolia2_flash_data,
	},
	.resource = &magnolia2_flash_resource,
	.num_resources = 1,
};

static const struct imxi2c_platform_data magnolia2_i2c0_data __initconst = {
	.bitrate = 50000,
};

static struct i2c_board_info magnolia2_i2c0_devices[] = {
	{
		I2C_BOARD_INFO("lm77", 0x48),
	},{
		I2C_BOARD_INFO("24c08", 0x50),
	},{
		I2C_BOARD_INFO("ds1307", 0x68),
	}
};


static struct platform_device *devices[] __initdata = {
	&magnolia2_flash,
};

static iomux_v3_cfg_t magnolia2_pads[] = {
#if 0
	/* UART1 */
	MX35_PAD_TXD1__UART1_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* TxD		*/
	MX35_PAD_RXD1__UART1_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RxD		*/
	MX35_PAD_RTS1__UART1_RTS |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RTS		*/
	MX35_PAD_CTS1__UART1_CTS |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* CTS		*/
	MX35_PAD_ATA_DATA6__UART1_DTR |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* DTR		*/
	MX35_PAD_ATA_DATA7__GPIO2_20 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* DSR (GPIO)	*/
	MX35_PAD_ATA_DATA8__UART1_RI |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RI		*/
	MX35_PAD_ATA_DATA9__UART1_DCD |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* DCD		*/
#endif

#if 0
	/* UART2 */
	MX35_PAD_TXD2__UART2_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* TxD		*/
	MX35_PAD_RXD2__UART2_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RxD		*/
	MX35_PAD_RTS2__UART2_RTS |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RTS		*/
	MX35_PAD_CTS2__UART2_CTS |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* CTS		*/
	MX35_PAD_TX5_RX0__UART2_DTR |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* DTR		*/
	MX35_PAD_TX4_RX1__GPIO1_11 |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* DSR (GPIO)	*/
	MX35_PAD_TX1__UART2_RI |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RI		*/
	MX35_PAD_TX0__UART2_DCD |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* DCD		*/
#endif

	/* UART3 */
	MX35_PAD_ATA_DATA11__UART3_TXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_PUE | PAD_CTL_PUS_100K_DOWN),	/* TxD		*/
	MX35_PAD_ATA_DATA10__UART3_RXD_MUX |
	MUX_PAD_CTRL(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
		     PAD_CTL_PUE | PAD_CTL_PUS_100K_UP),	/* RxD		*/

	/* FEC */
#define FEC_PAD_CTL_COMMON (PAD_CTL_DRV_3_3V | PAD_CTL_PUE | \
			    PAD_CTL_ODE_CMOS | PAD_CTL_DSE_MED | PAD_CTL_SRE_SLOW)
	MX35_PAD_FEC_TX_CLK__FEC_TX_CLK |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RX_CLK__FEC_RX_CLK |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RX_DV__FEC_RX_DV |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_COL__FEC_COL |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RDATA0__FEC_RDATA_0 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_TDATA0__FEC_TDATA_0 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_TX_EN__FEC_TX_EN |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_MDC__FEC_MDC |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_MDIO__FEC_MDIO |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_22K_UP),
	MX35_PAD_FEC_TX_ERR__FEC_TX_ERR |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RX_ERR__FEC_RX_ERR |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_CRS__FEC_CRS |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RDATA1__FEC_RDATA_1 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_TDATA1__FEC_TDATA_1 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RDATA2__FEC_RDATA_2 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_TDATA2__FEC_TDATA_2 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_RDATA3__FEC_RDATA_3 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
		     PAD_CTL_PKE_ENABLE | PAD_CTL_PUS_100K_DOWN),
	MX35_PAD_FEC_TDATA3__FEC_TDATA_3 |
	MUX_PAD_CTRL(FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
		     PAD_CTL_PKE_NONE | PAD_CTL_PUS_100K_DOWN),
#undef FEC_PAD_CTL_COMMON

	/* I2C1 */
#define I2C_PAD_CONFIG \
	(PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD | PAD_CTL_ODE_OpenDrain)
	MX35_PAD_I2C1_CLK__I2C1_SCL | MUX_PAD_CTRL(I2C_PAD_CONFIG),
	MX35_PAD_I2C1_DAT__I2C1_SDA | MUX_PAD_CTRL(I2C_PAD_CONFIG),

	/* I2C2 */
	MX35_PAD_I2C2_CLK__I2C2_SCL | MUX_PAD_CTRL(I2C_PAD_CONFIG),
	MX35_PAD_I2C2_DAT__I2C2_SDA | MUX_PAD_CTRL(I2C_PAD_CONFIG),

	/* I2C3 */
	MX35_PAD_TX3_RX2__I2C3_SCL | MUX_PAD_CTRL(I2C_PAD_CONFIG),
	MX35_PAD_TX2_RX3__I2C3_SDA | MUX_PAD_CTRL(I2C_PAD_CONFIG),
#undef I2C_PAD_CONFIG

	/* USB host */
#define USB_PAD_CONFIG (PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE | \
			PAD_CTL_DRV_NORMAL | PAD_CTL_PUS_100K_DOWN | PAD_CTL_SRE_FAST)
	MX35_PAD_GPIO3_0__USB_TOP_USBH2_CLK,		/* CLK	 */
	MX35_PAD_NFRE_B__USB_TOP_USBH2_DIR,		/* DIR	 */
	MX35_PAD_NFCLE__USB_TOP_USBH2_NXT |
	MUX_PAD_CTRL(USB_PAD_CONFIG),			/* NXT	 */
	MX35_PAD_NFALE__USB_TOP_USBH2_STP |
	MUX_PAD_CTRL(USB_PAD_CONFIG),			/* STP	 */
	MX35_PAD_SD2_DATA1__USB_TOP_USBH2_DATA_0,	/* DATA0 */
	MX35_PAD_SD2_DATA2__USB_TOP_USBH2_DATA_1,	/* DATA1 */
	MX35_PAD_SD2_DATA3__USB_TOP_USBH2_DATA_2,	/* DATA2 */
	MX35_PAD_NFWE_B__USB_TOP_USBH2_DATA_3,		/* DATA3 */
	MX35_PAD_SD2_CMD__USB_TOP_USBH2_DATA_4,	/* DATA4 */
	MX35_PAD_SD2_CLK__USB_TOP_USBH2_DATA_5,	/* DATA5 */
	MX35_PAD_SD2_DATA0__USB_TOP_USBH2_DATA_6,	/* DATA6 */
	MX35_PAD_NFWP_B__USB_TOP_USBH2_DATA_7,		/* DATA7 */
	MX35_PAD_USBOTG_OC__USB_TOP_USBH2_OC,		/* OC	 */
#undef USB_PAD_CONFIG

	/* SSI */
	MX35_PAD_STXFS4__AUDMUX_AUD4_TXFS,
	MX35_PAD_STXD4__AUDMUX_AUD4_TXD,
	MX35_PAD_SRXD4__AUDMUX_AUD4_RXD,
	MX35_PAD_SCK4__AUDMUX_AUD4_TXC,

	/* esdhc1 (SD card) */
#define PAD_CONFIG_CLK (PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE | \
			PAD_CTL_DRV_HIGH | PAD_CTL_PUS_47K_UP | PAD_CTL_SRE_FAST)
#define PAD_CONFIG_DAT (PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE | \
			PAD_CTL_DRV_HIGH | PAD_CTL_PUS_47K_UP | PAD_CTL_SRE_FAST)
#define MAGNOLIA2_PAD_SD1_CMD__ESDHC1_CMD	IOMUX_PAD(0x694, 0x230, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_DAT)
#define MAGNOLIA2_PAD_SD1_CLK__ESDHC1_CLK	IOMUX_PAD(0x698, 0x234, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_CLK)
#define MAGNOLIA2_PAD_SD1_DATA0__ESDHC1_DAT0	IOMUX_PAD(0x69c, 0x238, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_DAT)
#define MAGNOLIA2_PAD_SD1_DATA1__ESDHC1_DAT1	IOMUX_PAD(0x6a0, 0x23c, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_DAT)
#define MAGNOLIA2_PAD_SD1_DATA2__ESDHC1_DAT2	IOMUX_PAD(0x6a4, 0x240, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_DAT)
#define MAGNOLIA2_PAD_SD1_DATA3__ESDHC1_DAT3	IOMUX_PAD(0x6a8, 0x244, IOMUX_CONFIG_SION, 0x0, 0, \
							  PAD_CONFIG_DAT)
	MAGNOLIA2_PAD_SD1_CMD__ESDHC1_CMD,
	MAGNOLIA2_PAD_SD1_CLK__ESDHC1_CLK,
	MAGNOLIA2_PAD_SD1_DATA0__ESDHC1_DAT0,
	MAGNOLIA2_PAD_SD1_DATA1__ESDHC1_DAT1,
	MAGNOLIA2_PAD_SD1_DATA2__ESDHC1_DAT2,
	MAGNOLIA2_PAD_SD1_DATA3__ESDHC1_DAT3,
	MX35_PAD_NF_CE0__GPIO1_22,	/* WriteProtect */
	MX35_PAD_CSI_MCLK__GPIO1_28,	/* CardDetect	*/
#undef PAD_CONFIG_CLK
#undef PAD_CONFIG_DAT

	/* DIPSW */
	MX35_PAD_ATA_DATA15__GPIO2_28,
	MX35_PAD_ATA_INTRQ__GPIO2_29,
	MX35_PAD_ATA_BUFF_EN__GPIO2_30,
	MX35_PAD_ATA_DMARQ__GPIO2_31,

	/* PUSHSW */
	MX35_PAD_ATA_DATA13__GPIO2_26,
	MX35_PAD_ATA_DATA14__GPIO2_27,
};

#define SD1_GPIO_WP	   IMX_GPIO_NR(1, 22)
#define SD1_GPIO_CD	   IMX_GPIO_NR(1, 28)

#define SD3_GPIO_CD	   IMX_GPIO_NR(1, 5)

static int magnolia2_usbh1_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_SINGLE_UNI |
				      MXC_EHCI_IPPUE_DOWN);
}

static const struct mxc_usbh_platform_data usbh1_pdata __initconst = {
	.init	= magnolia2_usbh1_init,
	.portsc = MXC_EHCI_MODE_ULPI,
};

static struct esdhc_platform_data sd1_pdata = {
	.wp_gpio = SD1_GPIO_WP,
	.cd_gpio = SD1_GPIO_CD,
	.wp_type = ESDHC_WP_GPIO,
	.cd_type = ESDHC_CD_GPIO,
};

static struct esdhc_platform_data sd3_pdata = {
//	.wp_gpio = SD3_GPIO_WP,
	.cd_gpio = SD3_GPIO_CD,
//	.wp_type = ESDHC_WP_GPIO,
	.cd_type = ESDHC_CD_GPIO,
};

/*
 * Reset USB PHY
 */
void magnolia2_usbh2_phy_reset(void)
{
#define BOARD_CTRL 0xa8000000
	u8 reg, *addr;

	addr = ioremap(BOARD_CTRL, 1);

	reg = __raw_readl(addr);
	printk("%s: board_ctrl: 0x%02x\n", __FUNCTION__, reg);

	reg &= ~(1 << 7);
	__raw_writel(reg, addr);
	printk("%s: board_ctrl: 0x%02x\n", __FUNCTION__, reg);
	udelay(100);

	reg |= (1 << 7);
	printk("%s: board_ctrl: 0x%02x\n", __FUNCTION__, reg);
	__raw_writel(reg, addr);

	iounmap(addr);

	udelay(100);
}
EXPORT_SYMBOL(magnolia2_usbh2_phy_reset);

/*
 * Board specific initialization.
 */
static void __init magnolia2_init(void)
{
	imx35_soc_init();

	mxc_iomux_v3_setup_multiple_pads(magnolia2_pads, ARRAY_SIZE(magnolia2_pads));

	magnolia2_misc_init();
	early_wdt_init();

	mxc_init_tlv320aic31();

	imx35_add_fec(NULL);
	platform_add_devices(devices, ARRAY_SIZE(devices));
	imx35_add_imx2_wdt(NULL);

	magnolia2_init_uart();
	imx35_add_spi_imx0(&spi0_pdata);

	imx35_add_mxc_ehci_hs(&usbh1_pdata);

	imx35_add_sdhci_esdhc_imx(0, &sd1_pdata);
	imx35_add_sdhci_esdhc_imx(2, &sd3_pdata);

	/* Magnolia2 specific */
	magnolia2_init_extio4();
	magnolia2_init_extio5();

	/* i2c */
	i2c_register_board_info(0, magnolia2_i2c0_devices,
				ARRAY_SIZE(magnolia2_i2c0_devices));
	imx35_add_imx_i2c0(&magnolia2_i2c0_data);

	magnolia2_led_init();
	magnolia2_dipsw_in_init();
	magnolia2_gpio_dio_init();
	magnolia2_switch_in_init();

	pm_power_off_prepare = magnolia2_power_off;
}

static void __init magnolia2_timer_init(void)
{
	mx35_clocks_init();
}

struct sys_timer magnolia2_timer = {
	.init = magnolia2_timer_init,
};

MACHINE_START(MAGNOLIA2, "Century Systems Magnolia2")
	/* Maintainer: Century Systems Co.,Ltd. */
	.atag_offset = 0x100,
	.map_io = mx35_map_io,
	.init_early = imx35_init_early,
	.init_irq = mx35_init_irq,
	.handle_irq = imx35_handle_irq,
	.timer = &magnolia2_timer,
	.init_machine = magnolia2_init,
	MACHINE_END
