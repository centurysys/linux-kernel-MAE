/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 *  Copyright (C) 2009-2010 Century Systems, Co.LTd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/input.h>
#include <linux/nodemask.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/ata.h>
#if defined(CONFIG_MTD) || defined(CONFIG_MTD_MODULE)
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/flash.h>
#endif

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/keypad.h>
#include <mach/common.h>
#include <mach/common.h>
#include <mach/memory.h>
#include <mach/gpio.h>
#include <mach/mmc.h>
#include <mach/spba.h>
#include <net/ax88796.h>

#include <board-ma8xx.h>
#include "crm_regs.h"
#include "iomux.h"
/*!
 * @file mach-mx3/ma8xx.c
 *
 * @brief This file contains the board-specific initialization routines.
 *
 * @ingroup MSL_MX31
 */

extern void mxc_cpu_init(void) __init;
extern void ma8xx_gpio_init(void) __init;
extern void mxc_cpu_common_init(void);
extern int mxc_clocks_init(void);
extern void __init early_console_setup(char *);
extern int mxc_init_devices(void);

static void ma8xx_nop_release(struct device *dev)
{
	/* Nothing */
}

unsigned long board_get_ckih_rate(void)
{
	return 26000000;
}

#ifdef CONFIG_MA8XX_OLD
/* 1st trial board */
static struct resource ma8xx_smc911x_resources[] = {
	[0] = {
		.start	= CS5_BASE_ADDR,
		.end	= CS5_BASE_ADDR + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MXC_INT_GPIO_P1(0),
		.end	= MXC_INT_GPIO_P1(0),
		.flags	= IORESOURCE_IRQ,
	},
};
struct smsc911x_platform_config {
	unsigned int irq_polarity;
	unsigned int irq_type;
};
static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= 0,
	.irq_type	= 1,
};
static struct platform_device ma8xx_smc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.dev		= {
		.platform_data = &smsc911x_config,
	},
	.num_resources	= ARRAY_SIZE(ma8xx_smc911x_resources),  
	.resource	= ma8xx_smc911x_resources,
};
#else
static struct ax_plat_data ax88796_platdata = {
	.flags          = 0,
	.wordlength     = 2,
	.dcr_val        = 0x1,
	.rcr_val        = 0x40,
};

static struct resource ax88796_resources[] = {
	[0] = {
		.start	= CS5_BASE_ADDR,
		.end	= CS5_BASE_ADDR + (0x1f * 2) - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= CS5_BASE_ADDR + (0x1f * 2),
		.end	= CS5_BASE_ADDR + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= MXC_INT_GPIO_P1(0),
		.end	= MXC_INT_GPIO_P1(0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ma8xx_ax88796_device = {
	.name           = "ax88796",
	.id             = 0,

	.dev    = {
		.platform_data = &ax88796_platdata,
	},

	.num_resources  = ARRAY_SIZE(ax88796_resources),
	.resource       = ax88796_resources,
};
#endif

static void
ma8xx_eth_init(void)
{
#ifdef CONFIG_MA8XX_OLD
	platform_device_register(&ma8xx_smc911x_device);
#else
	platform_device_register(&ma8xx_ax88796_device);
#endif
}

static unsigned char eth_addr[6];

static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if ((*str == '.') || (*str == ':'))
			str++;
		num  = str2hexnum(*str++) << 4;
		num |= str2hexnum(*str++);
		ea[i] = num;
	}
}

static int __init ma8xx_ethaddr_setup(char *str)
{
        str2eaddr(eth_addr, str);

        return 1;
}
__setup("ethaddr=", ma8xx_ethaddr_setup);

void ma8xx_get_ethaddr(unsigned char *dev_addr)
{
        memcpy(dev_addr, eth_addr, sizeof(eth_addr));
}
EXPORT_SYMBOL(ma8xx_get_ethaddr);

static int __init parse_tag_century_uboot(const struct tag *tag)
{
        printk("Using UBoot passing parameters structure\n");
  
        memcpy(eth_addr, tag->u.century.macAddr, 6);

	return 0;
}

__tagtable(ATAG_CENTURY, parse_tag_century_uboot);

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
/*!
 * The serial port definition structure.
 */
static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase  = (void *)(IO_ADDRESS(CS4_BASE_ADDR) + MA8XX_EXT_UARTA),
		.mapbase  = (unsigned long)(CS4_BASE_ADDR + MA8XX_EXT_UARTA),
		.irq      = MXC_INT_GPIO_P1(1),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
        },
#ifdef CONFIG_MA8XX_16550_2
	{
		.membase  = (void *)(IO_ADDRESS(CS4_BASE_ADDR) + MA8XX_EXT_UARTB),
		.mapbase  = (unsigned long)(CS4_BASE_ADDR + MA8XX_EXT_UARTB),
		.irq      = MXC_INT_GPIO_P1(2),
		.uartclk  = 7372800,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
        },
#endif
	{},
};

static struct platform_device serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = serial_platform_data,
        },
};

static int __init ma8xx_init_extuart(void)
{
	return platform_device_register(&serial_device);
}
#else
static inline int ma8xx_init_extuart(void)
{
	return 0;
}
#endif
/* MTD NOR flash */

#if defined(CONFIG_MTD_MXC) || defined(CONFIG_MTD_MXC_MODULE)

static struct mtd_partition mxc_nor_partitions[] = {
	{
                .name = "uboot",
                .size = 256 * 1024,
                .offset = 0x00000000,
                .mask_flags = MTD_WRITEABLE	/* force read-only */
        },{
                .name = "config",
                .size = 256 * 1024,
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

static struct flash_platform_data ma8xx_flash_data = {
	.map_name = "cfi_probe",
	.width = 2,
	.parts = mxc_nor_partitions,
	.nr_parts = ARRAY_SIZE(mxc_nor_partitions),
};

static struct resource ma8xx_flash_resource = {
	.start = CS0_BASE_ADDR,
	.end = CS0_BASE_ADDR + SZ_64M - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ma8xx_nor_mtd_device = {
	.name = "mxc_nor_flash",
	.id = 0,
	.dev = {
		.release = ma8xx_nop_release,
		.platform_data = &ma8xx_flash_data,
        },
	.num_resources = 1,
	.resource = &ma8xx_flash_resource,
};

static void ma8xx_init_nor_mtd(void)
{
	(void)platform_device_register(&ma8xx_nor_mtd_device);
}
#else
static void ma8xx_init_nor_mtd(void)
{
}
#endif

static struct spi_board_info mxc_spi_board_info[] __initdata = {
	{
                .modalias = "pmic_spi",
                .irq = IOMUX_TO_IRQ(MX31_PIN_GPIO1_3),
                .max_speed_hz = 4000000,
                .bus_num = 2,
                .chip_select = 0,
        },
};

#if defined(CONFIG_MA8XX_VGA) && (defined(CONFIG_FB_MXC_SYNC_PANEL) || defined(CONFIG_FB_MXC_SYNC_PANEL_MODULE))
static const char fb_default_mode[] = "CRT-VGA";

/* mxc lcd driver */
static struct platform_device mxc_fb_device = {
	.name = "mxc_sdc_fb",
	.id = 0,
	.dev = {
		.release = ma8xx_nop_release,
		.platform_data = &fb_default_mode,
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

#if defined(CONFIG_I2C_MXC) || defined(CONFIG_I2C_MXC_MODULE)

#ifdef CONFIG_I2C_MXC_SELECT1
static struct i2c_board_info __initdata ma8xx_i2c0_info[] = {
	{
		I2C_BOARD_INFO("ds1307", 0x68),
        },
};
#endif

#ifdef CONFIG_I2C_MXC_SELECT2
static struct i2c_board_info __initdata ma8xx_i2c1_info[] = {
	{
		I2C_BOARD_INFO("aic3x-i2c", 0x18),
	},
};
#endif
#endif

/* LED */
#define GPIO_PORT(_name, _pin, _irq)            \
	{                                       \
		.name = (_name),                \
                        .pin  = (_pin),         \
                        .irq  = (_irq),         \
                        }

struct ma8xx_gpio_port ma8xx_led_ports[] = {
        /* Green LED */
	GPIO_PORT("led_g0", MX31_PIN_KEY_COL4, 0),
	GPIO_PORT("led_g1", MX31_PIN_KEY_COL5, 0),
	GPIO_PORT("led_g2", MX31_PIN_KEY_COL6, 0),
	GPIO_PORT("led_g3", MX31_PIN_KEY_COL7, 0),

        /* Red LED */
	GPIO_PORT("led_r0", MX31_PIN_KEY_ROW4, 0),
	GPIO_PORT("led_r1", MX31_PIN_KEY_ROW5, 0),
	GPIO_PORT("led_r2", MX31_PIN_KEY_ROW6, 0),
	GPIO_PORT("led_r3", MX31_PIN_KEY_ROW7, 0)
};

static struct ma8xx_gpio_private ma8xx_led_priv = {
	.ports	= ma8xx_led_ports,
};

static struct platform_device ma8xx_led_device = {
	.name	= "ma8xx_led",
	.id	= 0,
	.dev = {
		.platform_data = &ma8xx_led_priv,
	},
};

static void ma8xx_led_device_init(void)
{
	ma8xx_led_priv.nr_gpio = ARRAY_SIZE(ma8xx_led_ports);
	platform_device_register(&ma8xx_led_device);
}

#ifdef CONFIG_MA8XX_CONTACT_IN
/* Contact-in */
struct ma8xx_gpio_port ma8xx_contact_in_ports[] = {
	GPIO_PORT("din0", MX31_PIN_GPIO3_0, MXC_INT_GPIO_P3(0)),
	GPIO_PORT("din1", MX31_PIN_GPIO3_1, MXC_INT_GPIO_P3(1)),
	GPIO_PORT("din2", MX31_PIN_SCLK0, MXC_INT_GPIO_P3(2)),
	GPIO_PORT("din3", MX31_PIN_SRST0, MXC_INT_GPIO_P3(3)),
};

static struct ma8xx_gpio_private ma8xx_contact_in_priv = {
	.ports	= ma8xx_contact_in_ports,
};

static struct platform_device ma8xx_contact_in_device = {
	.name	= "ma8xx_din",
	.id	= 0,
	.dev = {
		.platform_data = &ma8xx_contact_in_priv,
	},
};

static void ma8xx_contact_in_init(void)
{
	ma8xx_contact_in_priv.nr_gpio = ARRAY_SIZE(ma8xx_contact_in_ports);
	platform_device_register(&ma8xx_contact_in_device);
}
#else
static void ma8xx_contact_in_init(void)
{
}
#endif

#ifdef CONFIG_MA8XX_CONTACT_OUT
/* Contact-out */
struct ma8xx_gpio_port ma8xx_contact_out_ports[] = {
	GPIO_PORT("dout0", MX31_PIN_DTR_DCE1, 0),
	GPIO_PORT("dout1", MX31_PIN_DSR_DCE1, 0),
	GPIO_PORT("dout2", MX31_PIN_RI_DCE1, 0),
	GPIO_PORT("dout3", MX31_PIN_DCD_DCE1, 0),
};

static struct ma8xx_gpio_private ma8xx_contact_out_priv = {
	.ports	= ma8xx_contact_out_ports,
};

static struct platform_device ma8xx_contact_out_device = {
	.name	= "ma8xx_dout",
	.id	= 0,
	.dev = {
		.platform_data = &ma8xx_contact_out_priv,
	},
};

static void ma8xx_contact_out_init(void)
{
	ma8xx_contact_out_priv.nr_gpio = ARRAY_SIZE(ma8xx_contact_out_ports);
	platform_device_register(&ma8xx_contact_out_device);
}
#else
static void ma8xx_contact_out_init(void)
{
}
#endif

/* PUSH Swtich */
struct ma8xx_gpio_port ma8xx_switch_in_ports[] = {
	GPIO_PORT("sw_eject", MX31_PIN_LCS0, MXC_INT_GPIO_P3(22)),
	GPIO_PORT("sw_init", MX31_PIN_SD_D_CLK, MXC_INT_GPIO_P3(23)),
};

static struct ma8xx_gpio_private ma8xx_switch_in_priv = {
	.ports	= ma8xx_switch_in_ports,
};

static struct platform_device ma8xx_switch_in_device = {
	.name	= "ma8xx_pushsw",
	.id	= 0,
	.dev = {
		.platform_data = &ma8xx_switch_in_priv,
	},
};

static void ma8xx_switch_in_init(void)
{
	ma8xx_switch_in_priv.nr_gpio = ARRAY_SIZE(ma8xx_switch_in_ports);
	platform_device_register(&ma8xx_switch_in_device);
}

/* DIPSW */
struct ma8xx_gpio_port ma8xx_dipsw_in_ports[] = {
	GPIO_PORT("dipsw1", MX31_PIN_ATA_CS0, MXC_INT_GPIO_P3(26)),
	GPIO_PORT("dipsw2", MX31_PIN_ATA_CS1, MXC_INT_GPIO_P3(27)),
	GPIO_PORT("dipsw3", MX31_PIN_ATA_DIOR, MXC_INT_GPIO_P3(28)),
	GPIO_PORT("dipsw4", MX31_PIN_ATA_DIOW, MXC_INT_GPIO_P3(29))
};

static struct ma8xx_gpio_private ma8xx_dipsw_in_priv = {
	.ports	= ma8xx_dipsw_in_ports,
};

static struct platform_device ma8xx_dipsw_in_device = {
	.name	= "ma8xx_dipsw",
	.id	= 0,
	.dev = {
		.platform_data = &ma8xx_dipsw_in_priv,
	},
};

static void ma8xx_dipsw_in_init(void)
{
	ma8xx_dipsw_in_priv.nr_gpio = ARRAY_SIZE(ma8xx_dipsw_in_ports);
	platform_device_register(&ma8xx_dipsw_in_device);
}

/* MMC device data */

#if defined(CONFIG_MMC_MXC) || defined(CONFIG_MMC_MXC_MODULE)
extern unsigned int sdhc_get_card_det_status(struct device *dev);
extern int sdhc_init_card_det(int id);
extern int sdhc_get_ro(struct device *dev);

static struct mxc_mmc_platform_data mmc0_data = {
	.ocr_mask = MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30,
	.min_clk = 150000,
	.max_clk = 25000000,
	.card_inserted_state = 1,
	.status = sdhc_get_card_det_status,
        .wp_status = sdhc_get_ro,
	.power_mmc = "VMMC1",
};
#if 0
static struct mxc_mmc_platform_data mmc1_data = {
	.ocr_mask = MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30,
	.min_clk = 150000,
	.max_clk = 25000000,
	.card_inserted_state = 1,
	.status = sdhc_get_card_det_status,
        .wp_status = sdhc_get_ro,
	.power_mmc = "VMMC2",
};
#endif

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
                .start = 0,
                .end = 0,
                .flags = IORESOURCE_IRQ,
        },
};

#if 0
/*!
 * Resource definition for the SDHC2
 */
static struct resource mxcsdhc2_resources[] = {
	[0] = {
                .start = MMC_SDHC2_BASE_ADDR,
                .end = MMC_SDHC2_BASE_ADDR + SZ_4K - 1,
                .flags = IORESOURCE_MEM,
        },
	[1] = {
                .start = MXC_INT_MMC_SDHC2,
                .end = MXC_INT_MMC_SDHC2,
                .flags = IORESOURCE_IRQ,
        },
	[2] = {
                .start = 0,
                .end = 0,
                .flags = IORESOURCE_IRQ,
        },
};
#endif

/*! Device Definition for MXC SDHC1 */
static struct platform_device mxcsdhc1_device = {
	.name = "mxcmci",
	.id = 0,
	.dev = {
		.release = ma8xx_nop_release,
		.platform_data = &mmc0_data,
        },
	.num_resources = ARRAY_SIZE(mxcsdhc1_resources),
	.resource = mxcsdhc1_resources,
};

#if 0
/*! Device Definition for MXC SDHC2 */
static struct platform_device mxcsdhc2_device = {
	.name = "mxcmci",
	.id = 1,
	.dev = {
		.release = ma8xx_nop_release,
		.platform_data = &mmc1_data,
        },
	.num_resources = ARRAY_SIZE(mxcsdhc2_resources),
	.resource = mxcsdhc2_resources,
};
#endif

static inline void mxc_init_mmc(void)
{
	int cd_irq;

	cd_irq = sdhc_init_card_det(0);
	if (cd_irq) {
		mxcsdhc1_device.resource[2].start = cd_irq;
		mxcsdhc1_device.resource[2].end = cd_irq;
	}

#if 0
	cd_irq = sdhc_init_card_det(1);
	if (cd_irq) {
		mxcsdhc2_device.resource[2].start = cd_irq;
		mxcsdhc2_device.resource[2].end = cd_irq;
	}
#endif

	spba_take_ownership(SPBA_SDHC1, SPBA_MASTER_A | SPBA_MASTER_C);
	(void)platform_device_register(&mxcsdhc1_device);
#if 0
	spba_take_ownership(SPBA_SDHC2, SPBA_MASTER_A | SPBA_MASTER_C);
	(void)platform_device_register(&mxcsdhc2_device);
#endif
}
#else
static inline void mxc_init_mmc(void)
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
static void __init fixup_ma8xx_board(struct machine_desc *desc, struct tag *tags,
                                     char **cmdline, struct meminfo *mi)
{
	mxc_cpu_init();
}

void (*ma8xx_power_off_prepare)(void);
EXPORT_SYMBOL(ma8xx_power_off_prepare);

/*
 *
 */
static void ma8xx_power_off(void)
{
        if (ma8xx_power_off_prepare)
                ma8xx_power_off_prepare();
}

/*!
 * Board specific initialization.
 */
static void __init ma8xx_board_init(void)
{
	mxc_cpu_common_init();
	early_console_setup(saved_command_line);
	mxc_init_devices();
	mxc_gpio_init();
	ma8xx_gpio_init();
        ma8xx_eth_init();
	ma8xx_init_extuart();
	ma8xx_init_nor_mtd();
        ma8xx_led_device_init();
        ma8xx_switch_in_init();
        ma8xx_dipsw_in_init();

        ma8xx_contact_in_init();
        ma8xx_contact_out_init();

#ifdef CONFIG_I2C_MXC_SELECT1
	i2c_register_board_info(0, ma8xx_i2c0_info, ARRAY_SIZE(ma8xx_i2c0_info));
#endif
#ifdef CONFIG_I2C_MXC_SELECT2
	i2c_register_board_info(1, ma8xx_i2c1_info, ARRAY_SIZE(ma8xx_i2c1_info));
#endif
	spi_register_board_info(mxc_spi_board_info,
				ARRAY_SIZE(mxc_spi_board_info));

	mxc_init_fb();
	mxc_init_mmc();

        pm_power_off = ma8xx_power_off;
}

#define PLL_PCTL_REG(pd, mfd, mfi, mfn)                                 \
	((((pd) - 1) << 26) + (((mfd) - 1) << 16) + ((mfi)  << 10) + mfn)

/* For 26MHz input clock */
#define PLL_532MHZ		PLL_PCTL_REG(1, 13, 10, 3)
#define PLL_399MHZ		PLL_PCTL_REG(1, 52, 7, 35)
#define PLL_133MHZ		PLL_PCTL_REG(2, 26, 5, 3)

/* For 27MHz input clock */
#define PLL_532_8MHZ		PLL_PCTL_REG(1, 15, 9, 13)
#define PLL_399_6MHZ		PLL_PCTL_REG(1, 18, 7, 7)
#define PLL_133_2MHZ		PLL_PCTL_REG(3, 5, 7, 2)

#define PDR0_REG(mcu, max, hsp, ipg, nfc)                               \
	(MXC_CCM_PDR0_MCU_DIV_##mcu | MXC_CCM_PDR0_MAX_DIV_##max |      \
	 MXC_CCM_PDR0_HSP_DIV_##hsp | MXC_CCM_PDR0_IPG_DIV_##ipg |      \
	 MXC_CCM_PDR0_NFC_DIV_##nfc)

/* working point(wp): 0 - 133MHz; 1 - 266MHz; 2 - 399MHz; 3 - 532MHz */
/* 26MHz input clock table */
static struct cpu_wp cpu_wp_26[] = {
	{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 133000000,
                .pdr0_reg = PDR0_REG(4, 4, 4, 2, 6),},
	{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 266000000,
                .pdr0_reg = PDR0_REG(2, 4, 4, 2, 6),},
	{
                .pll_reg = PLL_399MHZ,
                .pll_rate = 399000000,
                .cpu_rate = 399000000,
                .pdr0_reg = PDR0_REG(1, 3, 3, 2, 6),},
	{
                .pll_reg = PLL_532MHZ,
                .pll_rate = 532000000,
                .cpu_rate = 532000000,
                .pdr0_reg = PDR0_REG(1, 4, 4, 2, 6),},
};

struct cpu_wp *get_cpu_wp(int *wp)
{
	*wp = 4;

        return cpu_wp_26;
}

/*
 * The following uses standard kernel macros defined in arch.h in order to
 * initialize __mach_desc_MXA8XX data structure.
 */
/* *INDENT-OFF* */
MACHINE_START(MA8XX, "Century Systems MA-8xx")
/* Maintainer: Century Systems Co.,Ltd. */
#ifdef CONFIG_SERIAL_8250_CONSOLE
	.phys_io = CS4_BASE_ADDR,
	.io_pg_offst = ((CS4_BASE_ADDR_VIRT) >> 18) & 0xfffc,
#else
	.phys_io = AIPS1_BASE_ADDR,
	.io_pg_offst = ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.fixup = fixup_ma8xx_board,
	.map_io = mxc_map_io,
	.init_irq = mxc_init_irq,
	.init_machine = ma8xx_board_init,
	.timer = &mxc_timer,
        MACHINE_END
