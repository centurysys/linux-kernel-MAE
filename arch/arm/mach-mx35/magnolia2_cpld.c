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
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/hardware.h>
#include <mach/gpio.h>

//#include "board-magnolia2.h"
#include <mach/board-magnolia2.h>
#include "crm_regs.h"
#include "iomux.h"

/*!
 * @file mach-mx35/magnolia2_cpld.c
 *
 * @brief This file contains the board specific initialization routines.
 *
 * @ingroup MSL_MX35
 */

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
 * Reset Ethernet PHY
 */
void magnolia2_eth_phy_reset(int active)
{
}
EXPORT_SYMBOL(magnolia2_eth_phy_reset);

/*
 * Reset FeliCa R/W
 */
void magnolia2_felica_rw_reset(int active)
{
}
EXPORT_SYMBOL(magnolia2_felica_rw_reset);

/*
 * Reset WiFi module
 */
void magnolia2_wifi_reset(int active)
{
}
EXPORT_SYMBOL(magnolia2_wifi_reset);

/*
 * control SDcard slot power
 */
void magnolia2_SDcard_power_control(int on)
{
}
EXPORT_SYMBOL(magnolia2_SDcard_power_control);

/*
 * get CPLD revision
 */
u8 magnolia2_get_CPLD_revision(void)
{
	return 0;
}
EXPORT_SYMBOL(magnolia2_get_CPLD_revision);

/*
 * get CPU board ID
 */
u8 magnolia2_get_board_ID(void)
{
	return 1;
}
EXPORT_SYMBOL(magnolia2_get_board_ID);
