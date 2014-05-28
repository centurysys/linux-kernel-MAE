/*
 * OMAP L3 Interconnect error handling driver
 *
 * Copyright (C) 2011 Texas Corporation
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	Sricharan <r.sricharan@ti.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "omap_l3_noc.h"

/*
 * Interrupt Handler for L3 error detection.
 *	1) Identify the L3 clockdomain partition to which the error belongs to.
 *	2) Identify the slave where the error information is logged
 *	3) Print the logged information.
 *	4) Add dump stack to provide kernel trace.
 *
 * Two Types of errors :
 *	1) Custom errors in L3 :
 *		Target like DMM/FW/EMIF generates SRESP=ERR error
 *	2) Standard L3 error:
 *		- Unsupported CMD.
 *			L3 tries to access target while it is idle
 *		- OCP disconnect.
 *		- Address hole error:
 *			If DSS/ISS/FDIF/USBHOSTFS access a target where they
 *			do not have connectivity, the error is logged in
 *			their default target which is DMM2.
 *
 *	On High Secure devices, firewall errors are possible and those
 *	can be trapped as well. But the trapping is implemented as part
 *	secure software and hence need not be implemented here.
 */
static irqreturn_t l3_interrupt_handler(int irq, void *_l3)
{

	struct omap_l3 *l3 = _l3;
	int inttype, i, k;
	int err_src = 0;
	u32 std_err_main, err_reg, clear, masterid;
	void __iomem *base, *l3_targ_base;
	char *target_name, *master_name = "UN IDENTIFIED";
	static u32 mask0[MAX_L3_MODULES], mask1[MAX_L3_MODULES];

	/* Get the Type of interrupt */
	inttype = irq == l3->app_irq ? L3_APPLICATION_ERROR : L3_DEBUG_ERROR;

	for (i = 0; i < l3->num_modules; i++) {
		/*
		 * Read the regerr register of the clock domain
		 * to determine the source
		 */
		base = l3->l3_base[i];
		err_reg = __raw_readl(base + l3->l3_flag_mux[i] +
					+ L3_FLAGMUX_REGERR0 + (inttype << 3));
		err_reg &= inttype ? ~mask1[i] : ~mask0[i];

		/* Get the corresponding error and analyse */
		if (err_reg) {
			/* Identify the source from control status register */
			err_src = __ffs(err_reg);

			if ((err_src >= l3->num_targets[i]) ||
			    (*(l3->l3_targets[i] + err_src) ==
			    L3_FLAGMUX_TARGET_OFS_INVALID)) {
				u32 val;
				void __iomem *reg = base + l3->l3_flag_mux[i] +
					L3_FLAGMUX_MASK0 + (inttype << 3);

				pr_warn("L3 %s error: target %d clkdm %d %s\n",
					inttype ? "debug" : "application",
					err_src, i, "(unclearable)");
				val = readl(reg);
				val &= ~(1 << err_src);
				if (inttype)
					mask1[i] |= (1 << err_src);
				else
					mask0[i] |= (1 << err_src);
				writel(val, reg);
				break;
			}

			/* Read the stderrlog_main_source from clk domain */
			l3_targ_base = base + *(l3->l3_targets[i] + err_src);
			std_err_main =  __raw_readl(l3_targ_base +
					L3_TARG_STDERRLOG_MAIN);
			masterid = __raw_readl(l3_targ_base +
					L3_TARG_STDERRLOG_MSTADDR);

			switch (std_err_main & CUSTOM_ERROR) {
			case STANDARD_ERROR:
				target_name =
					l3->target_names[i][err_src];
				WARN(true, "L3 standard error: TARGET:%s at address 0x%x\n",
					target_name,
					__raw_readl(l3_targ_base +
						L3_TARG_STDERRLOG_SLVOFSLSB));
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel(clear, l3_targ_base +
					L3_TARG_STDERRLOG_MAIN);
				break;

			case CUSTOM_ERROR:
				target_name =
					l3->target_names[i][err_src];
				for (k = 0; k < l3->num_masters; k++) {
					if (masterid == ((l3->masters_names)[k]).id)
						master_name =
							((l3->masters_names)[k]).name;
				}
				WARN(true, "L3 custom error: MASTER:%s TARGET:%s\n",
					master_name, target_name);
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel(clear, l3_targ_base +
					L3_TARG_STDERRLOG_MAIN);
				break;

			default:
				/* Nothing to be handled here as of now */
				break;
			}
		/* Error found so break the for loop */
		break;
		}
	}
	return IRQ_HANDLED;
}

static const struct of_device_id l3_noc_match[] = {
	{.compatible = "ti,omap4-l3-noc", .data = &omap_l3_data},
	{.compatible = "ti,am4372-l3-noc", .data = &am4372_l3_data},
	{},
};
MODULE_DEVICE_TABLE(of, l3_noc_match);

static int omap_l3_probe(struct platform_device *pdev)
{
	static struct omap_l3 *l3;
	struct resource	*res;
	int ret, i;
	const struct of_device_id *of_id =
				of_match_device(l3_noc_match, &pdev->dev);

	l3 = (struct omap_l3 *)of_id->data;

	if (!l3)
		return -EINVAL;

	platform_set_drvdata(pdev, l3);

	for (i = 0; i < l3->num_modules; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res == NULL)
			return -ENOENT;
		l3->l3_base[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(l3->l3_base[i]))
			return PTR_ERR(l3->l3_base[i]);
	}

	/*
	 * Setup interrupt Handlers
	 */
	l3->debug_irq = platform_get_irq(pdev, 0);
	ret = request_irq(l3->debug_irq,
			l3_interrupt_handler,
			IRQF_DISABLED, "l3-dbg-irq", l3);
	if (ret) {
		pr_crit("L3: request_irq failed to register for 0x%x\n",
						l3->debug_irq);
		return ret;
	}

	l3->app_irq = platform_get_irq(pdev, 1);
	ret = request_irq(l3->app_irq,
			l3_interrupt_handler,
			IRQF_DISABLED, "l3-app-irq", l3);
	if (ret) {
		pr_crit("L3: request_irq failed to register for 0x%x\n",
						l3->app_irq);
		free_irq(l3->debug_irq, l3);
	}

	return ret;
}

static int omap_l3_remove(struct platform_device *pdev)
{
	struct omap_l3 *l3 = platform_get_drvdata(pdev);

	free_irq(l3->app_irq, l3);
	free_irq(l3->debug_irq, l3);

	return 0;
}

static struct platform_driver omap_l3_driver = {
	.probe		= omap_l3_probe,
	.remove		= omap_l3_remove,
	.driver		= {
		.name		= "omap_l3_noc",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(l3_noc_match),
	},
};

static int __init omap_l3_init(void)
{
	return platform_driver_register(&omap_l3_driver);
}
postcore_initcall_sync(omap_l3_init);

static void __exit omap_l3_exit(void)
{
	platform_driver_unregister(&omap_l3_driver);
}
module_exit(omap_l3_exit);
