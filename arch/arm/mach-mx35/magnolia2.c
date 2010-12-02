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

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/input.h>
#include <linux/nodemask.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/ata.h>
#include <linux/pmic_external.h>
#include <linux/mfd/mc9s08dz60/pmic.h>
#include <linux/regulator/consumer.h>
#if defined(CONFIG_MTD) || defined(CONFIG_MTD_MODULE)
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>

#include <asm/mach/flash.h>
#endif

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/memory.h>
#include <mach/gpio.h>
#include <mach/mmc.h>
#include <mach/board-magnolia2.h>

#include "crm_regs.h"
#include "iomux.h"

/*!
 * @file mach-mx35/magnolia2.c
 *
 * @brief This file contains the board specific initialization routines.
 *
 * @ingroup MSL_MX35
 */

unsigned int magnolia2_board_io;

static struct tag_magnolia2_uboot uboot_tag;

static int __init parse_tag_magnolia2_uboot(const struct tag *tag)
{
        printk("Using UBoot passing parameters structure\n");

        memcpy(&uboot_tag, &tag->u.magnolia2, sizeof(uboot_tag));

        return 0;
}

__tagtable(ATAG_MAGNOLIA2, parse_tag_magnolia2_uboot);

static void mxc_nop_release(struct device *dev)
{
	/* Nothing */
}

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
                *type = uboot_tag.rs2.type;
                *config = uboot_tag.rs2.config;
                break;

        case 2:
                *enable = 0;
                *type = 0;
                *config = 0;
                break;

        default:
                res = -1;
        }

        return res;
}
EXPORT_SYMBOL(magnolia2_get_uart_info);

/*!
 * The serial port definition structure (FOMA)
 */
static struct plat_serial8250_port foma_serial_platform_data[] = {
	{
		.membase  = (void *)(IO_ADDRESS(CS4_BASE_ADDR) + MAGNOLIA2_EXT_UART_FOMA),
		.mapbase  = (unsigned long)(CS4_BASE_ADDR + MAGNOLIA2_EXT_UART_FOMA),
		.irq      = MXC_INT_GPIO_P3(2),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
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
	.start = CS4_BASE_ADDR,
	.end = CS4_BASE_ADDR + 3,
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


/*------------------------------------------------------------------------------
resource: an expansion FL-net card (flnet)
------------------------------------------------------------------------------*/
static struct resource flnet_extio_resource = {
	.start = CS4_BASE_ADDR,
	.end = CS4_BASE_ADDR + 0x0B,
	.flags = IORESOURCE_MEM,
};


/*------------------------------------------------------------------------------
platform_device: an expansion FL-net card (flnet)
------------------------------------------------------------------------------*/
static void dummy_flnet_release(struct device *dev)
{
	return;
}

static struct platform_device flnet_card_device = {
	.name = "flnet_card",
	.id = 0,
	.dev = {
		.release = dummy_flnet_release,
	},
	.num_resources = 1,
	.resource = &flnet_extio_resource,
};


/*------------------------------------------------------------------------------
resource: an expansion FL-net card (fldin)
------------------------------------------------------------------------------*/
static struct resource fldin_extio_resource[] = {
	{
	.start = CS4_BASE_ADDR + 0x10,
	.end = CS4_BASE_ADDR + 0x11,
	.flags = IORESOURCE_MEM,
	},
	{
	.start = MXC_INT_GPIO_P3(2),
	.end = MXC_INT_GPIO_P3(2),
	.flags = IORESOURCE_IRQ,
	}
};


/*------------------------------------------------------------------------------
platform_device: an expansion FL-net card (fldin)
------------------------------------------------------------------------------*/
static void dummy_fldin_release(struct device *dev)
{
	return;
}

static struct platform_device fldin_card_device = {
	.name = "fldin_card",
	.id = 0,
	.dev = {
		.release = dummy_fldin_release,
	},
	.num_resources = 2,
	.resource = fldin_extio_resource,
};



static int __init magnolia2_init_extio4(void)
{
        u32 cs4_board_id;

        cs4_board_id = uboot_tag.cs4.id;

        if (cs4_board_id != 0x0f)
                printk("Magnolia2 External I/O(CS4): board_id = %d\n", cs4_board_id);
        else
                return 0;

        switch (cs4_board_id) {
        case 0x01:
                /* FOMA Ubiquitous module */
                mxc_request_iomux(MX35_PIN_ATA_DA2, MUX_CONFIG_GPIO);
                mxc_set_gpio_direction(MX35_PIN_ATA_DA2, 1);     /* INPUT */

                mxc_iomux_set_pad(MX35_PIN_ATA_DA2, PAD_CTL_HYS_SCHMITZ |
                                  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PU | PAD_CTL_PUE_PUD);

                platform_device_register(&foma_serial_device);
                platform_device_register(&foma_extio_device);
                break;

        case 0x02:
		/* FL-net module */
		// an expansion FL-net card (flnet)
		platform_device_register(&flnet_card_device);
		// an expansion FL-net card (fldin)
		platform_device_register(&fldin_card_device);
                break;

        default:
                break;
        }

        return 0;
}

/* CS5: AI/DIO Extension */
static struct spi_board_info mxc_spi_board_info_aidio[] __initdata = {
	{
                .modalias = "ltc185x",
                .max_speed_hz = 1500000,	/* max spi SCK clock speed in HZ */
                .bus_num = 1,
                .chip_select = 0,
        },
	{
                .modalias = "ltc185x",
                .max_speed_hz = 1500000,	/* max spi SCK clock speed in HZ */
                .bus_num = 1,
                .chip_select = 1,
        },
};

static struct resource dio_extio_resource[] = {
        {
                .start = CS5_BASE_ADDR,
                .end = CS5_BASE_ADDR + 0x84 - 1,
                .flags = IORESOURCE_MEM,
        },
        {
                .start = MXC_INT_GPIO_P3(1),
                .end = MXC_INT_GPIO_P3(1),
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
		.membase  = (void *)(IO_ADDRESS(CS5_BASE_ADDR) + MAGNOLIA2_EXT_UART_CAN),
		.mapbase  = (unsigned long)(CS5_BASE_ADDR + MAGNOLIA2_EXT_UART_CAN),
		.irq      = MXC_INT_GPIO_P3(1),
		.uartclk  = 7372800,
		.regshift = 1,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
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
        u32 cs5_board_id;

        cs5_board_id = uboot_tag.cs5.id;

        if (cs5_board_id != 0x0f)
                printk("Magnolia2 External I/O(CS5): board_id = %d\n", cs5_board_id);
        else
                return 0;

        switch (cs5_board_id) {
        case 0x01:
                /* AI/DIO module */
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
		__raw_writel(0x0000d484, IO_ADDRESS(CSCR_U(5)));
		__raw_writel(0x8c884561, IO_ADDRESS(CSCR_L(5)));
		__raw_writel(0xac8e1300, IO_ADDRESS(CSCR_A(5)));
		
                break;

        default:
                break;
        }

        return 0;
}

/* MTD NOR flash */

#if defined(CONFIG_MTD_MXC) || defined(CONFIG_MTD_MXC_MODULE)

static struct mtd_partition mxc_nor_partitions[] = {
	{
                .name = "uboot",
                .size = 256 * 1024,
                .offset = 0x00000000,
                .mask_flags = MTD_WRITEABLE	/* force read-only */
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
                .name = "rootfs",
                .size = 28 * 1024 * 1024,
                .offset = MTDPART_OFS_APPEND,
        },
};

static struct flash_platform_data mxc_flash_data = {
	.map_name = "cfi_probe",
	.width = 2,
	.parts = mxc_nor_partitions,
	.nr_parts = ARRAY_SIZE(mxc_nor_partitions),
};

static struct resource mxc_flash_resource = {
	.start = CS0_BASE_ADDR,
	.end = CS0_BASE_ADDR + SZ_32M - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device mxc_nor_mtd_device = {
	.name = "mxc_nor_flash",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mxc_flash_data,
        },
	.num_resources = 1,
	.resource = &mxc_flash_resource,
};

static void mxc_init_nor_mtd(void)
{
	(void)platform_device_register(&mxc_nor_mtd_device);
}
#else
static void mxc_init_nor_mtd(void)
{
}
#endif

static struct mxc_lcd_platform_data lcd_data = {
	.io_reg = "LCD"
};

static struct platform_device lcd_dev = {
	.name = "lcd_claa",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = (void *)&lcd_data,
        },
};

static void mxc_init_lcd(void)
{
	platform_device_register(&lcd_dev);
}

#if defined(CONFIG_FB_MXC_SYNC_PANEL) || defined(CONFIG_FB_MXC_SYNC_PANEL_MODULE)
/* mxc lcd driver */
static struct platform_device mxc_fb_device = {
	.name = "mxc_sdc_fb",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.coherent_dma_mask = 0xFFFFFFFF,
        },
};

static void mxc_init_fb(void)
{
	(void)platform_device_register(&mxc_fb_device);
}
#else
static inline void mxc_init_fb(void)
{
}
#endif

#ifdef CONFIG_I2C_MXC_SELECT1
static struct i2c_board_info mxc_i2c1_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("lm77", 0x48),
        },{
		I2C_BOARD_INFO("24c08", 0x50),
        },{
		I2C_BOARD_INFO("ds1307", 0x68),
        }
};
#endif

#if defined(CONFIG_FEC) || defined(CONFIG_FEC_MODULE)
unsigned int expio_intr_fec = MXC_INT_GPIO_P3(0);

EXPORT_SYMBOL(expio_intr_fec);
#endif

#if defined(CONFIG_MMC_IMX_ESDHCI) || defined(CONFIG_MMC_IMX_ESDHCI_MODULE)
static struct mxc_mmc_platform_data mmc1_data = {
	.ocr_mask = MMC_VDD_32_33,
	.caps = MMC_CAP_4_BIT_DATA,
	.min_clk = 150000,
	.max_clk = 52000000,
	.card_inserted_state = 0,
	.status = sdhc_get_card_det_status,
	.wp_status = sdhc_write_protect,
	.clock_mmc = "sdhc_clk",
};

/*!
 * Resource definition for the SDHC1
 */
static struct resource mxcsdhc1_resources[] = {
	[0] = {
                .start = MMC_SDHC1_BASE_ADDR,
                .end = MMC_SDHC1_BASE_ADDR + SZ_4K - 1,
                .flags = IORESOURCE_MEM,
        },
	[1] = {
                .start = MXC_INT_MMC_SDHC1,
                .end = MXC_INT_MMC_SDHC1,
                .flags = IORESOURCE_IRQ,
        },
	[2] = {
                /* Card detection IRQ */
                .start = MXC_INT_GPIO_P1(28),
                .end = MXC_INT_GPIO_P1(28),
                .flags = IORESOURCE_IRQ,
        },
};

/*! Device Definition for MXC SDHC1 */
static struct platform_device mxcsdhc1_device = {
	.name = "mxsdhci",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mmc1_data,
        },
	.num_resources = ARRAY_SIZE(mxcsdhc1_resources),
	.resource = mxcsdhc1_resources,
};

static struct mxc_mmc_platform_data mmc3_data = {
	.ocr_mask = MMC_VDD_32_33,
	.caps = MMC_CAP_4_BIT_DATA,
	.min_clk = 150000,
	.max_clk = 50000000,
	.card_inserted_state = 0,
	.status = sdhc_get_card_det_status,
	.wp_status = sdhc_write_protect,
	.clock_mmc = "sdhc_clk",
};

static struct resource mxcsdhc3_resources[] = {
	[0] = {
                .start = MMC_SDHC3_BASE_ADDR,
                .end = MMC_SDHC3_BASE_ADDR + SZ_4K - 1,
                .flags = IORESOURCE_MEM,
        },
	[1] = {
                .start = MXC_INT_MMC_SDHC3,
                .end = MXC_INT_MMC_SDHC3,
                .flags = IORESOURCE_IRQ,
        },
	[2] = {
                /* Card detection IRQ */
                .start = MXC_INT_GPIO_P1(5),
                .end = MXC_INT_GPIO_P1(5),
                .flags = IORESOURCE_IRQ,
        },
};

static struct platform_device mxcsdhc3_device = {
	.name = "mxsdhci",
	.id = 2,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mmc3_data,
        },
	.num_resources = ARRAY_SIZE(mxcsdhc3_resources),
	.resource = mxcsdhc3_resources,
};

static inline void mxc_init_mmc(void)
{
	(void)platform_device_register(&mxcsdhc1_device);
	(void)platform_device_register(&mxcsdhc3_device);
}
#else
static inline void mxc_init_mmc(void)
{
}
#endif

#define GPIO_PORT(_name, _pin, _irq)            \
	{                                       \
		.name = (_name),                \
                        .pin  = (_pin),         \
                        .irq  = (_irq),         \
                        }

#define LED_PORT(_name, _shift) \
	{                                       \
		.name = (_name),                \
                        .shift  = (_shift),     \
                        }

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
	GPIO_PORT("sw_init", MX35_PIN_ATA_DATA13, MXC_INT_GPIO_P2(26)),
	GPIO_PORT("sw_eject", MX35_PIN_ATA_DATA14, MXC_INT_GPIO_P2(27)),
};

static struct magnolia2_gpio_private magnolia2_switch_in_priv = {
        .nr_gpio = ARRAY_SIZE(magnolia2_switch_in_ports),
	.ports	= magnolia2_switch_in_ports,
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
	GPIO_PORT("dipsw1", MX35_PIN_ATA_DATA15, MXC_INT_GPIO_P2(28)),
	GPIO_PORT("dipsw2", MX35_PIN_ATA_INTRQ, MXC_INT_GPIO_P2(29)),
	GPIO_PORT("dipsw3", MX35_PIN_ATA_BUFF_EN, MXC_INT_GPIO_P2(30)),
	GPIO_PORT("dipsw4", MX35_PIN_ATA_DMARQ, MXC_INT_GPIO_P2(31))
};

static struct magnolia2_gpio_private magnolia2_dipsw_in_priv = {
        .nr_gpio = ARRAY_SIZE(magnolia2_dipsw_in_ports),
	.ports	= magnolia2_dipsw_in_ports,
};

static struct platform_device magnolia2_dipsw_in_device = {
	.name	= "magnolia2_dipsw",
	.id	= 0,
	.dev = {
		.platform_data = &magnolia2_dipsw_in_priv,
	},
};

static void magnolia2_dipsw_in_init(void)
{
	magnolia2_dipsw_in_priv.nr_gpio = ARRAY_SIZE(magnolia2_dipsw_in_ports);
	platform_device_register(&magnolia2_dipsw_in_device);
}

/* Sound */
#if defined(CONFIG_SND_SOC_IMX_MAGNOLIA2_TLV320AIC31) \
        || defined(CONFIG_SND_SOC_IMX_MAGNOLIA2_TLV320AIC31_MODULE)
static int mxc_tlv320aic31_plat_init(void)
{
	return 0;
}

static struct mxc_audio_platform_data mxc_tlv320aic31_data = {
	.ssi_num = 1,
	.src_port = 1,
	.ext_port = 4,
	.init = mxc_tlv320aic31_plat_init,
};

static struct platform_device mxc_alsa_device = {
	.name = "magnolia2-aic31",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mxc_tlv320aic31_data,
        },

};

static void mxc_init_tlv320aic31(void)
{
        printk("Magnolia2 Audio: %sabled.\n",
               (uboot_tag.audio == 0) ? "En" : "Dis");

        if (uboot_tag.audio == 0)
                platform_device_register(&mxc_alsa_device);
}
#else
static void mxc_init_tlv320aic31(void)
{
}
#endif

/*!
 * Board specific fixup function. It is called by \b setup_arch() in
 * setup.c file very early on during kernel starts. It allows the user to
 * statically fill in the proper values for the passed-in parameters. None of
 * the parameters is used currently.
 *
 * @param  desc         pointer to \b struct \b machine_desc
 * @param  tags         pointer to \b struct \b tag
 * @param  cmdline      pointer to the command line
 * @param  mi           pointer to \b struct \b meminfo
 */
static void __init fixup_mxc_board(struct machine_desc *desc, struct tag *tags,
				   char **cmdline, struct meminfo *mi)
{
	mxc_cpu_init();

#ifdef CONFIG_DISCONTIGMEM
	do {
		int nid;
		mi->nr_banks = MXC_NUMNODES;
		for (nid = 0; nid < mi->nr_banks; nid++)
			SET_NODE(mi, nid);
	} while (0);
#endif
}

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

/* Probe Extension board */
static void __init magnolia2_cs4_init(void)
{




}

static void __init magnolia2_cs5_init(void)
{




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
#define MXC_WDT_WCR             0x00
#define MXC_WDT_WSR             0x02
#define MXC_WDT_WRSR            0x04
#define WCR_WOE_BIT             (1 << 6)
#define WCR_WDA_BIT             (1 << 5)
#define WCR_SRS_BIT             (1 << 4)
#define WCR_WRE_BIT             (1 << 3)
#define WCR_WDE_BIT             (1 << 2)
#define WCR_WDBG_BIT            (1 << 1)
#define WCR_WDZST_BIT           (1 << 0)
#define WDT_MAGIC_1             0x5555
#define WDT_MAGIC_2             0xAAAA

#define TIMER_MARGIN_MAX    	127
#define TIMER_MARGIN_DEFAULT	60	/* 60 secs */
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

#define WDOG_SEC_TO_COUNT(s)  ((s * 2) << 8)
#define WDOG_COUNT_TO_SEC(c)  ((c >> 8) / 2)

	wdt_base_reg = IO_ADDRESS(WDOG1_BASE_ADDR);

	mxc_wdt_clk = clk_get(NULL, "wdog_clk");
	clk_enable(mxc_wdt_clk);

        mb();

        val = __raw_readw(wdt_base_reg + MXC_WDT_WRSR) & 0x0003;
        printk("i.MX35 WDT: WRSR = 0x%04x\n", val);

        if (val & 0x0002)
                printk(" 0x02: Reset is the result of a WDOG time-out.\n");
        if (val & 0x0001)
                printk(" 0x01: Reset is the result of a software reset.\n");

        if (earlywdt_enable == 1) {
                printk(" Starting early WDT, 120seconds.\n");
                /* wdt_config */
                val = __raw_readw(wdt_base_reg + MXC_WDT_WCR);
                val |= 0xFF00 | WCR_WOE_BIT | WCR_WDA_BIT | WCR_SRS_BIT;
                val &= ~WCR_WRE_BIT;
                __raw_writew(val, wdt_base_reg + MXC_WDT_WCR);

                /* wdt_set_timeout */
                val = __raw_readw(wdt_base_reg + MXC_WDT_WCR);
                val = (val & 0x00FF) | WDOG_SEC_TO_COUNT(120);
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

/*!
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_cpu_common_init();

	early_console_setup(saved_command_line);

        magnolia2_misc_init();
        early_wdt_init();

	mxc_gpio_init();
	mxc_init_devices();

	magnolia2_gpio_init();
	mxc_init_nor_mtd();

	mxc_init_lcd();
	mxc_init_fb();

#ifdef CONFIG_I2C_MXC_SELECT1
	i2c_register_board_info(0, mxc_i2c1_board_info,
				ARRAY_SIZE(mxc_i2c1_board_info));
#endif
	mxc_init_mmc();

        /* Magnolia2 specific */
        magnolia2_init_extio4();
        magnolia2_init_extio5();

        magnolia2_led_init();
        magnolia2_dipsw_in_init();
        magnolia2_switch_in_init();

        mxc_init_tlv320aic31();

        pm_power_off = magnolia2_power_off;
}

#define PLL_PCTL_REG(brmo, pd, mfd, mfi, mfn)                           \
        (((brmo) << 31) + (((pd) - 1) << 26) + (((mfd) - 1) << 16) +    \
         ((mfi)  << 10) + mfn)

/* For 24MHz input clock */
#define PLL_665MHZ		PLL_PCTL_REG(1, 1, 48, 13, 41)
#define PLL_532MHZ		PLL_PCTL_REG(1, 1, 12, 11, 1)
#define PLL_399MHZ		PLL_PCTL_REG(0, 1, 16, 8, 5)

#if 0
/* working point(wp): 0,1 - 133MHz; 2,3 - 266MHz; 4,5 - 399MHz;*/
/* auto input clock table */
static struct cpu_wp cpu_wp_auto[] = {
	{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 133000000,
                .pdr0_reg = (0x2 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 133000000,
                .pdr0_reg = (0x6 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 266000000,
                .pdr0_reg = (0x1 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 266000000,
                .pdr0_reg = (0x5 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 399000000,
                .pdr0_reg = (0x0 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 399000000,
                .pdr0_reg = (0x6 << MXC_CCM_PDR0_AUTO_MUX_DIV_OFFSET),
        },
};
#endif

/* consumer input clock table */
static struct cpu_wp cpu_wp_con[] = {
	{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 133000000,
                .pdr0_reg = (0x6 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 133000000,
                .pdr0_reg = (0xE << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 266000000,
                .pdr0_reg = (0x2 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 266000000,
                .pdr0_reg = (0xA << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 399000000,
                .pdr0_reg = (0x1 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 399000000,
                .pdr0_reg = (0x9 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 532000000,
                .pdr0_reg = (0x0 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 532000000,
                .pdr0_reg = (0x8 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },{
                .pll_reg = PLL_665MHZ,
                .pll_rate = 665000000,
                .cpu_rate = 665000000,
                .pdr0_reg = (0x7 << MXC_CCM_PDR0_CON_MUX_DIV_OFFSET),
        },
};

struct cpu_wp *get_cpu_wp(int *wp)
{
        *wp = 9;
        return cpu_wp_con;

#if 0
	if (cpu_is_mx35_rev(CHIP_REV_2_0) >= 1) {
		*wp = 9;
		return cpu_wp_con;
	} else {
		if (__raw_readl(MXC_CCM_PDR0) & MXC_CCM_PDR0_AUTO_CON) {
			*wp = 9;
			return cpu_wp_con;
		} else {
			*wp = 6;
			return cpu_wp_auto;
		}
	}
#endif
}

static void __init mx35_3stack_timer_init(void)
{
	mxc_clocks_init(0, 0, 0, 0);
	mxc_timer_init("gpt_clk");
}

static struct sys_timer mxc_timer = {
	.init = mx35_3stack_timer_init,
};

/*
 * This is the Magnolia2 sched_clock implementation.
 */
unsigned long long sched_clock(void)
{
	return ((unsigned long long)jiffies - INITIAL_JIFFIES) * (NSEC_PER_SEC / HZ);
}

/*
 * The following uses standard kernel macros define in arch.h in order to
 * initialize __mach_desc_MX35_3DS data structure.
 */
/* *INDENT-OFF* */
MACHINE_START(MAGNOLIA2, "Century Systems Magnolia2")
/* Maintainer: Century Systems Co.,Ltd. */
	.phys_io = AIPS1_BASE_ADDR,
	.io_pg_offst = ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params = PHYS_OFFSET + 0x100,
	.fixup = fixup_mxc_board,
	.map_io = mxc_map_io,
	.init_irq = mxc_init_irq,
	.init_machine = mxc_board_init,
	.timer = &mxc_timer,
        MACHINE_END
