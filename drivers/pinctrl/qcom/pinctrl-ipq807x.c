/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc ipq807x_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),
	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),
	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),
	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),
	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),
	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),
	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),
	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),
	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),
	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "SDC1_CLK"),
	PINCTRL_PIN(71, "SDC1_CMD"),
	PINCTRL_PIN(72, "SDC1_DATA"),
	PINCTRL_PIN(73, "SDC2_CLK"),
	PINCTRL_PIN(74, "SDC2_CMD"),
	PINCTRL_PIN(75, "SDC2_DATA"),
	PINCTRL_PIN(76, "QDSD_CLK"),
	PINCTRL_PIN(77, "QDSD_CMD"),
	PINCTRL_PIN(78, "QDSD_DATA0"),
	PINCTRL_PIN(79, "QDSD_DATA1"),
	PINCTRL_PIN(80, "QDSD_DATA2"),
	PINCTRL_PIN(81, "QDSD_DATA3"),
};

#define DECLARE_MSM_GPIO_PINS(pin) \
	static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_MSM_GPIO_PINS(0);
DECLARE_MSM_GPIO_PINS(1);
DECLARE_MSM_GPIO_PINS(2);
DECLARE_MSM_GPIO_PINS(3);
DECLARE_MSM_GPIO_PINS(4);
DECLARE_MSM_GPIO_PINS(5);
DECLARE_MSM_GPIO_PINS(6);
DECLARE_MSM_GPIO_PINS(7);
DECLARE_MSM_GPIO_PINS(8);
DECLARE_MSM_GPIO_PINS(9);
DECLARE_MSM_GPIO_PINS(10);
DECLARE_MSM_GPIO_PINS(11);
DECLARE_MSM_GPIO_PINS(12);
DECLARE_MSM_GPIO_PINS(13);
DECLARE_MSM_GPIO_PINS(14);
DECLARE_MSM_GPIO_PINS(15);
DECLARE_MSM_GPIO_PINS(16);
DECLARE_MSM_GPIO_PINS(17);
DECLARE_MSM_GPIO_PINS(18);
DECLARE_MSM_GPIO_PINS(19);
DECLARE_MSM_GPIO_PINS(20);
DECLARE_MSM_GPIO_PINS(21);
DECLARE_MSM_GPIO_PINS(22);
DECLARE_MSM_GPIO_PINS(23);
DECLARE_MSM_GPIO_PINS(24);
DECLARE_MSM_GPIO_PINS(25);
DECLARE_MSM_GPIO_PINS(26);
DECLARE_MSM_GPIO_PINS(27);
DECLARE_MSM_GPIO_PINS(28);
DECLARE_MSM_GPIO_PINS(29);
DECLARE_MSM_GPIO_PINS(30);
DECLARE_MSM_GPIO_PINS(31);
DECLARE_MSM_GPIO_PINS(32);
DECLARE_MSM_GPIO_PINS(33);
DECLARE_MSM_GPIO_PINS(34);
DECLARE_MSM_GPIO_PINS(35);
DECLARE_MSM_GPIO_PINS(36);
DECLARE_MSM_GPIO_PINS(37);
DECLARE_MSM_GPIO_PINS(38);
DECLARE_MSM_GPIO_PINS(39);
DECLARE_MSM_GPIO_PINS(40);
DECLARE_MSM_GPIO_PINS(41);
DECLARE_MSM_GPIO_PINS(42);
DECLARE_MSM_GPIO_PINS(43);
DECLARE_MSM_GPIO_PINS(44);
DECLARE_MSM_GPIO_PINS(45);
DECLARE_MSM_GPIO_PINS(46);
DECLARE_MSM_GPIO_PINS(47);
DECLARE_MSM_GPIO_PINS(48);
DECLARE_MSM_GPIO_PINS(49);
DECLARE_MSM_GPIO_PINS(50);
DECLARE_MSM_GPIO_PINS(51);
DECLARE_MSM_GPIO_PINS(52);
DECLARE_MSM_GPIO_PINS(53);
DECLARE_MSM_GPIO_PINS(54);
DECLARE_MSM_GPIO_PINS(55);
DECLARE_MSM_GPIO_PINS(56);
DECLARE_MSM_GPIO_PINS(57);
DECLARE_MSM_GPIO_PINS(58);
DECLARE_MSM_GPIO_PINS(59);
DECLARE_MSM_GPIO_PINS(60);
DECLARE_MSM_GPIO_PINS(61);
DECLARE_MSM_GPIO_PINS(62);
DECLARE_MSM_GPIO_PINS(63);
DECLARE_MSM_GPIO_PINS(64);
DECLARE_MSM_GPIO_PINS(65);
DECLARE_MSM_GPIO_PINS(66);
DECLARE_MSM_GPIO_PINS(67);
DECLARE_MSM_GPIO_PINS(68);
DECLARE_MSM_GPIO_PINS(69);

static const unsigned int sdc1_clk_pins[] = { 142 };
static const unsigned int sdc1_cmd_pins[] = { 143 };
static const unsigned int sdc1_data_pins[] = { 144 };
static const unsigned int sdc1_rclk_pins[] = { 145 };
static const unsigned int sdc2_clk_pins[] = { 146 };
static const unsigned int sdc2_cmd_pins[] = { 147 };
static const unsigned int sdc2_data_pins[] = { 148 };
static const unsigned int qdsd_clk_pins[] = { 76 };
static const unsigned int qdsd_cmd_pins[] = { 77 };
static const unsigned int qdsd_data0_pins[] = { 78 };
static const unsigned int qdsd_data1_pins[] = { 79 };
static const unsigned int qdsd_data2_pins[] = { 80 };
static const unsigned int qdsd_data3_pins[] = { 81 };

#define FUNCTION(fname)			                \
	[msm_mux_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define REG_BASE 0x1000000
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned)ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			msm_mux_NA, /* gpio mode */	\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9			\
		},				        \
		.nfuncs = 10,				\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned)ARRAY_SIZE(pg_name##_pins),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.mux_bit = -1,				\
		.pull_bit = pull,			\
		.drv_bit = drv,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = -1,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

enum ipq807x_functions {
	msm_mux_blsp_spi1,
	msm_mux_,
	msm_mux_blsp_spi2,
	msm_mux_blsp_spi3,
	msm_mux_blsp_spi4,
	msm_mux_blsp_spi5,
	msm_mux_blsp_spi6,
	msm_mux_mdp_vsync,
	msm_mux_cam_mclk,
	msm_mux_cci_i2c,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cam_irq,
	msm_mux_ois_sync,
	msm_mux_cci_async,
	msm_mux_cam1_standby,
	msm_mux_cam1_rst,
	msm_mux_cci_timer4,
	msm_mux_accel_int,
	msm_mux_alsp_int,
	msm_mux_mag_int,
	msm_mux_NA,
};

static const char * const blsp_spi1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const _groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28",
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio37",
	"gpio38", "gpio41", "gpio49", "gpio50", "gpio53", "gpio59", "gpio60",
	"gpio63", "gpio67",
};
static const char * const blsp_spi2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_spi3_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_spi4_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};
static const char * const blsp_spi5_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};
static const char * const blsp_spi6_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const mdp_vsync_groups[] = {
	"gpio24", "gpio25",
};
static const char * const cam_mclk_groups[] = {
	"gpio26", "gpio27", "gpio28",
};
static const char * const cci_i2c_groups[] = {
	"gpio29", "gpio30", "gpio31", "gpio32",
};
static const char * const cci_timer0_groups[] = {
	"gpio33",
};
static const char * const cci_timer1_groups[] = {
	"gpio34",
};
static const char * const cam_irq_groups[] = {
	"gpio35", "gpio45", "gpio47", "gpio57",
};
static const char * const ois_sync_groups[] = {
	"gpio36", "gpio46", "gpio48", "gpio58",
};
static const char * const cci_async_groups[] = {
	"gpio38", "gpio50", "gpio60",
};
static const char * const cam1_standby_groups[] = {
	"gpio39", "gpio51", "gpio61",
};
static const char * const cam1_rst_groups[] = {
	"gpio40", "gpio52", "gpio62", "gpio66",
};
static const char * const cci_timer4_groups[] = {
	"gpio41", "gpio53", "gpio63", "gpio67",
};
static const char * const accel_int_groups[] = {
	"gpio42", "gpio54", "gpio64", "gpio68",
};
static const char * const alsp_int_groups[] = {
	"gpio43", "gpio55", "gpio65", "gpio69", "gpio69",
};
static const char * const mag_int_groups[] = {
	"gpio44", "gpio56",
};

static const struct msm_function ipq807x_functions[] = {
	FUNCTION(blsp_spi1),
	FUNCTION(),
	FUNCTION(blsp_spi2),
	FUNCTION(blsp_spi3),
	FUNCTION(blsp_spi4),
	FUNCTION(blsp_spi5),
	FUNCTION(blsp_spi6),
	FUNCTION(mdp_vsync),
	FUNCTION(cam_mclk),
	FUNCTION(cci_i2c),
	FUNCTION(cci_timer0),
	FUNCTION(cci_timer1),
	FUNCTION(cam_irq),
	FUNCTION(ois_sync),
	FUNCTION(cci_async),
	FUNCTION(cam1_standby),
	FUNCTION(cam1_rst),
	FUNCTION(cci_timer4),
	FUNCTION(accel_int),
	FUNCTION(alsp_int),
	FUNCTION(mag_int),
};

static const struct msm_pingroup ipq807x_groups[] = {
	PINGROUP(0, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, blsp_spi2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, blsp_spi2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, blsp_spi2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, blsp_spi2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, blsp_spi3, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, blsp_spi3, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, blsp_spi3, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, blsp_spi3, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, blsp_spi4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(13, blsp_spi4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(14, blsp_spi4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, blsp_spi4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, blsp_spi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, blsp_spi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, blsp_spi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, blsp_spi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, blsp_spi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, blsp_spi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, blsp_spi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, blsp_spi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, cci_timer0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, cci_timer1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, cci_async, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, cci_timer4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, cci_async, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, cci_timer4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, cci_async, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, cci_timer4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, cci_timer4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	SDC_QDSD_PINGROUP(sdc1_clk, 0x110a000, 13, 6),
	SDC_QDSD_PINGROUP(sdc1_cmd, 0x110a000, 11, 3),
	SDC_QDSD_PINGROUP(sdc1_data, 0x110a000, 9, 0),
	SDC_QDSD_PINGROUP(sdc1_rclk, 0x10a000, 15, 0),
	SDC_QDSD_PINGROUP(sdc2_clk, 0x1109000, 14, 6),
	SDC_QDSD_PINGROUP(sdc2_cmd, 0x1109000, 11, 3),
	SDC_QDSD_PINGROUP(sdc2_data, 0x1109000, 9, 0),
	SDC_QDSD_PINGROUP(qdsd_clk, 0x119c000, 3, 0),
	SDC_QDSD_PINGROUP(qdsd_cmd, 0x119c000, 8, 5),
	SDC_QDSD_PINGROUP(qdsd_data0, 0x119c000, 13, 10),
	SDC_QDSD_PINGROUP(qdsd_data1, 0x119c000, 18, 15),
	SDC_QDSD_PINGROUP(qdsd_data2, 0x119c000, 23, 20),
	SDC_QDSD_PINGROUP(qdsd_data3, 0x119c000, 28, 25),
};

static const struct msm_pinctrl_soc_data ipq807x_pinctrl = {
	.pins = ipq807x_pins,
	.npins = ARRAY_SIZE(ipq807x_pins),
	.functions = ipq807x_functions,
	.nfunctions = ARRAY_SIZE(ipq807x_functions),
	.groups = ipq807x_groups,
	.ngroups = ARRAY_SIZE(ipq807x_groups),
	.ngpios = 70,
};

static int ipq807x_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq807x_pinctrl);
}

static const struct of_device_id ipq807x_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq807x-pinctrl", },
	{ },
};

static struct platform_driver ipq807x_pinctrl_driver = {
	.driver = {
		.name = "ipq807x-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = ipq807x_pinctrl_of_match,
	},
	.probe = ipq807x_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init ipq807x_pinctrl_init(void)
{
	return platform_driver_register(&ipq807x_pinctrl_driver);
}
arch_initcall(ipq807x_pinctrl_init);

static void __exit ipq807x_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq807x_pinctrl_driver);
}
module_exit(ipq807x_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm ipq807x pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ipq807x_pinctrl_of_match);
