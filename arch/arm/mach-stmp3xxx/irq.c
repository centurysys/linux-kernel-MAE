/*
 * Freescale STMP37XX/STMP378X common interrupt handling code
 *
 * Author: Vladislav Buzov <vbuzov@embeddedalley.com>
 *
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/sysdev.h>

#include <mach/regs-icoll.h>

void __init stmp3xxx_init_irq(struct irq_chip *chip1,
		struct irq_chip *chip2, int (*is_chip_2)(int))
{
	unsigned int i;
	struct irq_chip *chip;

	/* Reset the interrupt controller */
	HW_ICOLL_CTRL_CLR(BM_ICOLL_CTRL_CLKGATE);
	udelay(10);
	HW_ICOLL_CTRL_CLR(BM_ICOLL_CTRL_SFTRST);
	udelay(10);
	HW_ICOLL_CTRL_SET(BM_ICOLL_CTRL_SFTRST);
	while (!(HW_ICOLL_CTRL_RD() & BM_ICOLL_CTRL_CLKGATE))
		continue;
	HW_ICOLL_CTRL_CLR(BM_ICOLL_CTRL_SFTRST | BM_ICOLL_CTRL_CLKGATE);

	/* Disable all interrupts initially */
	for (i = 0; i < NR_IRQS; i++) {
		chip = chip1;
		if (is_chip_2 && is_chip_2(i))
			chip = chip2;
		chip->mask(i);
		set_irq_chip(i, chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	/* Ensure vector is cleared */
	HW_ICOLL_LEVELACK_WR(1);
	HW_ICOLL_LEVELACK_WR(2);
	HW_ICOLL_LEVELACK_WR(4);
	HW_ICOLL_LEVELACK_WR(8);

	HW_ICOLL_VECTOR_WR(0);
	/* Barrier */
	(void) HW_ICOLL_STAT_RD();
}

#define irq_suspend NULL
#define irq_resume NULL

static struct sysdev_class irq_class = {
	.name		= "irq",
	.suspend	= irq_suspend,
	.resume		= irq_resume,
};

static struct sys_device irq_device = {
	.id	= 0,
	.cls	= &irq_class,
};

static int __init irq_init_sysfs(void)
{
	int ret = sysdev_class_register(&irq_class);
	if (ret == 0)
		ret = sysdev_register(&irq_device);
	return ret;
}

device_initcall(irq_init_sysfs);
