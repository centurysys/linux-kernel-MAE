/*
 * Copyright 2005-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/usb/fsl_xcvr.h>

#include <mach/arc_otg.h>
#include "usb.h"

static struct fsl_usb2_platform_data usbh2_config = {
	.name              = "Host 2",
	.platform_init     = fsl_usb_host_init,
	.platform_uninit   = fsl_usb_host_uninit,
	.operating_mode    = FSL_USB2_MPH_HOST,
#ifndef CONFIG_MACH_MAGNOLIA2
	.phy_mode          = FSL_USB2_PHY_SERIAL,
#else
	.phy_mode          = FSL_USB2_PHY_ULPI,
#endif
	.power_budget      = 500,		/* 500 mA max power */
	.gpio_usb_active   = gpio_usbh2_active,
	.gpio_usb_inactive = gpio_usbh2_inactive,
#ifndef CONFIG_MACH_MAGNOLIA2
	.transceiver       = "serial",
#else
	.transceiver       = "isp1504",
#endif
};

static struct resource usbh2_resources[] = {
	[0] = {
		.start = (u32) (USB_H2REGS_BASE),
		.end   = (u32) (USB_H2REGS_BASE + 0x1ff),
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = MXC_INT_USB_HS,
		.flags = IORESOURCE_IRQ,
	},
};


static int __init usbh2_init(void)
{
	pr_debug("%s: \n", __func__);

	/* i.MX35 1.0 should work in INCR mode */
	if (cpu_is_mx35_rev(CHIP_REV_2_0) < 0) {
		usbh2_config.change_ahb_burst = 1;
		usbh2_config.ahb_burst_mode = 0;
	}

	host_pdev_register(usbh2_resources, ARRAY_SIZE(usbh2_resources),
			   &usbh2_config);
	return 0;
}
module_init(usbh2_init);
