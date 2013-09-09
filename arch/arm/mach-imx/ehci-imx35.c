/*
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#ifdef CONFIG_MACH_MAGNOLIA2
#include <linux/delay.h>
#endif

#include <mach/hardware.h>
#include <mach/mxc_ehci.h>

#define USBCTRL_OTGBASE_OFFSET	0x600

#define MX35_OTG_SIC_SHIFT	29
#define MX35_OTG_SIC_MASK	(0x3 << MX35_OTG_SIC_SHIFT)
#define MX35_OTG_PM_BIT		(1 << 24)

#define MX35_H1_SIC_SHIFT	21
#define MX35_H1_SIC_MASK	(0x3 << MX35_H1_SIC_SHIFT)
#define MX35_H1_PM_BIT		(1 << 8)
#define MX35_H1_IPPUE_UP_BIT	(1 << 7)
#define MX35_H1_IPPUE_DOWN_BIT	(1 << 6)
#define MX35_H1_TLL_BIT		(1 << 5)
#define MX35_H1_USBTE_BIT	(1 << 4)
#ifdef CONFIG_MACH_MAGNOLIA2
#define MX35_H1_HEX_TEN		(1 << 26)

#define USBOTG_MIRROR_OFFSET	(USBCTRL_OTGBASE_OFFSET + 0x04)
#define OTGM_HULPICLK		(1 << 6)	/* Host ULPI PHY clock on */
#endif

#ifdef CONFIG_MACH_MAGNOLIA2
extern void magnolia2_usbh2_phy_reset(void);

static void mx35_check_ulpi_clock(void)
{
        int retry = 10, phy_ok = 0;
	volatile unsigned int v;

        while (retry > 0) {
		v = readl(MX35_IO_ADDRESS(MX35_USB_BASE_ADDR + USBOTG_MIRROR_OFFSET));

                if (v & OTGM_HULPICLK) {
                        phy_ok = 1;
                        printk("ULPI clock is running.\n");
                        break;
                }

                retry--;
                printk(KERN_ERR "%s: Host ULPI clock not running!\n",
                       __FUNCTION__);
                magnolia2_usbh2_phy_reset();
                mdelay(10);
        }

        if (!phy_ok) {
                panic("%s: Host ULPI clock not running 10 times, reboot...\n",
		      __FUNCTION__);
        }
}
#endif

int mx35_initialize_usb_hw(int port, unsigned int flags)
{
	unsigned int v;

	v = readl(MX35_IO_ADDRESS(MX35_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	switch (port) {
	case 0:	/* OTG port */
		v &= ~(MX35_OTG_SIC_MASK | MX35_OTG_PM_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX35_OTG_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX35_OTG_PM_BIT;

		break;
	case 1: /* H1 port */
		v &= ~(MX35_H1_SIC_MASK | MX35_H1_PM_BIT | MX35_H1_TLL_BIT |
			MX35_H1_USBTE_BIT | MX35_H1_IPPUE_DOWN_BIT | MX35_H1_IPPUE_UP_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX35_H1_SIC_SHIFT;
#ifdef CONFIG_MACH_MAGNOLIA2
		v |= MX35_H1_HEX_TEN;

		mx35_check_ulpi_clock();
#endif
		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX35_H1_PM_BIT;

		if (!(flags & MXC_EHCI_TTL_ENABLED))
			v |= MX35_H1_TLL_BIT;

		if (flags & MXC_EHCI_INTERNAL_PHY)
			v |= MX35_H1_USBTE_BIT;

		if (flags & MXC_EHCI_IPPUE_DOWN)
			v |= MX35_H1_IPPUE_DOWN_BIT;

		if (flags & MXC_EHCI_IPPUE_UP)
			v |= MX35_H1_IPPUE_UP_BIT;

		break;
	default:
		return -EINVAL;
	}

	writel(v, MX35_IO_ADDRESS(MX35_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	return 0;
}
