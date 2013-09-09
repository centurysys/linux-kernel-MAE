/*
 * MX35 CPU type detection
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/iim.h>

static int mx35_cpu_rev = -1;

static int mx35_read_cpu_rev(void)
{
	u32 rev;

	rev = __raw_readl(MX35_IO_ADDRESS(MX35_IIM_BASE_ADDR + MXC_IIMSREV));
	switch (rev) {
	case 0x00:
		return IMX_CHIP_REVISION_1_0;
	case 0x10:
		return IMX_CHIP_REVISION_2_0;
	case 0x11:
		return IMX_CHIP_REVISION_2_1;
	default:
		return IMX_CHIP_REVISION_UNKNOWN;
	}
}

int mx35_revision(void)
{
	if (mx35_cpu_rev == -1)
		mx35_cpu_rev = mx35_read_cpu_rev();

	return mx35_cpu_rev;
}
EXPORT_SYMBOL(mx35_revision);

#ifdef CONFIG_MACH_MAGNOLIA2
static int __init post_cpu_init(void)
{
	void *l2_base;
	unsigned long aips_reg;

//	iram_init(MX35_IRAM_BASE_ADDR, MX35_IRAM_SIZE);

	/*
	 * S/W workaround: Clear the off platform peripheral modules
	 * Supervisor Protect bit for SDMA to access them.
	 */
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x40));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x44));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x48));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x4C));
	aips_reg = __raw_readl(MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x50));
	aips_reg &= 0x00FFFFFF;
	__raw_writel(aips_reg, MX35_IO_ADDRESS(MX35_AIPS1_BASE_ADDR + 0x50));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x40));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x44));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x48));
	__raw_writel(0x0, MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x4C));
	aips_reg = __raw_readl(MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x50));
	aips_reg &= 0x00FFFFFF;
	__raw_writel(aips_reg, MX35_IO_ADDRESS(MX35_AIPS2_BASE_ADDR + 0x50));

	return 0;
}

postcore_initcall(post_cpu_init);
#endif
