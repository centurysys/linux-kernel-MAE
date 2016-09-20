/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <linux/reset-controller.h>
#include <dt-bindings/reset/qcom,gcc-ipq807x.h>

#include "common.h"
#include "clk-regmap.h"
#include "reset.h"

struct clk *clk;

static int clk_dummy_is_enabled(struct clk_hw *hw)
{
	return 1;
};

static int clk_dummy_enable(struct clk_hw *hw)
{
	return 0;
};

static void clk_dummy_disable(struct clk_hw *hw)
{
	return;
};

static u8 clk_dummy_get_parent(struct clk_hw *hw)
{
	return 0;
};

static int clk_dummy_set_parent(struct clk_hw *hw, u8 index)
{
	return 0;
};

static int clk_dummy_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	return 0;
};

static int clk_dummy_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	return 0;
};

static unsigned long clk_dummy_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	return parent_rate;
};

const struct clk_ops clk_dummy_ops = {
	.is_enabled = clk_dummy_is_enabled,
	.enable = clk_dummy_enable,
	.disable = clk_dummy_disable,
	.get_parent = clk_dummy_get_parent,
	.set_parent = clk_dummy_set_parent,
	.set_rate = clk_dummy_set_rate,
	.recalc_rate = clk_dummy_recalc_rate,
	.determine_rate = clk_dummy_determine_rate,
};

static struct clk_regmap dummy = {
	.hw.init = &(struct clk_init_data){
		.name = "dummy_clk_src",
		.parent_names = (const char *[]){ "xo"},
		.num_parents = 1,
		.ops = &clk_dummy_ops,
	},
};

static struct clk_regmap i2c_clk = {
	.hw.init = &(struct clk_init_data){
		.name = "dummy_clk_src_i2c",
		.parent_names = (const char *[]){ "xo"},
		.num_parents = 1,
		.ops = &clk_dummy_ops,
	},
};

static const struct of_device_id gcc_dummy_match_table[] = {
	{ .compatible = "qcom,gcc-ipq807x" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_dummy_match_table);

static struct clk_regmap *gcc_ipq807x_clks[] = {
	[GCC_DUMMY_CLK] = &dummy,
	[GCC_I2C_CLK] = &i2c_clk,
};

static const struct qcom_reset_map gcc_ipq807x_resets[] = {
	[GCC_PCIE0_BCR] = { 0x01875004, 0},
	[GCC_PCIE0_PHY_BCR]  = { 0x01875038, 0},
	[GCC_PCIE0PHY_PHY_BCR] = { 0x0187503C, 0},
};

static const struct regmap_config gcc_ipq807x_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xffffc,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_ipq807x_desc = {
	.config = &gcc_ipq807x_regmap_config,
	.clks = gcc_ipq807x_clks,
	.num_clks = ARRAY_SIZE(gcc_ipq807x_clks),
	.resets = gcc_ipq807x_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq807x_resets),
};

static int gcc_dummy_probe(struct platform_device *pdev)
{
	int ret;

	clk = clk_register_fixed_rate(&pdev->dev, "xo", NULL, CLK_IS_ROOT,
				      19200000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = qcom_cc_probe(pdev, &gcc_ipq807x_desc);

	dev_dbg(&pdev->dev, "Registered dummy clock provider\n");
	return ret;
}

static int gcc_dummy_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver gcc_dummy_driver = {
	.probe		= gcc_dummy_probe,
	.remove		= gcc_dummy_remove,
	.driver		= {
		.name	= "gcc-dummy",
		.owner	= THIS_MODULE,
		.of_match_table = gcc_dummy_match_table,
	},
};

static int __init gcc_dummy_init(void)
{
	return platform_driver_register(&gcc_dummy_driver);
}
core_initcall(gcc_dummy_init);

static void __exit gcc_dummy_exit(void)
{
	platform_driver_unregister(&gcc_dummy_driver);
}
module_exit(gcc_dummy_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. GCC IPQ807x Driver");
MODULE_LICENSE("Dual BSD/GPLv2");
MODULE_ALIAS("platform:gcc-ipq807x");
