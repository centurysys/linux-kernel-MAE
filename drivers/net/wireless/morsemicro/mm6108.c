/*
 * Copyright 2017-2022 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <linux/firmware.h>
#include <linux/delay.h>

#include "hw.h"
#include "morse.h"
#include "firmware.h"
#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "pageset.h"

/* Generates IRQ to RISC */
#define MM6108_REG_TRGR_BASE			0x100a6000
#define MM6108_REG_INT_BASE			0x100a6050

#define MM6108_REG_MSI				0x02000000

#define MM6108_REG_MANIFEST_PTR_ADDRESS		0x10054d40

#define MM6108_REG_HOST_MAGIC_VALUE		0xDEADBEEF

#define MM6108_REG_RESET			0x10054050
#define MM6108_REG_RESET_VALUE			0xDEAD
#define MM6108_REG_APPS_BOOT_ADDR		0x10054020 /* APPS core boot address */

#define MM6108_REG_CHIP_ID			0x10054d20

#define MM6108_REG_CLK_CTRL			0x1005406C
#define MM6108_REG_CLK_CTRL_VALUE		0xef
#define MM6108_REG_EARLY_CLK_CTRL_VALUE		0xe5

#define MM6108_REG_AON_ADDR			0x10058094
#define MM6108_REG_AON_LATCH_ADDR		0x1005807C
#define MM6108_REG_AON_LATCH_MASK		0x1

#define MM6108_REG_XTAL_INIT_SEQ_ADDR_1		0x10012008 /* Gpio output en address */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_2		0x1001200C /* Gpio output value address */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_3		0x1005805C /* Digpll en address */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_4		0x10054000 /* System clk control  address */

#define MM6108_REG_XTAL_INIT_SEQ_AON_CLK_VAL	0x19	   /* Aon clk switch to 32 kHz val */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_1_VAL	0x2	   /* Gpio 1 output_en val */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_2_VAL	0x2	   /* Gpio 1 output_val val */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_3_VAL	0x21D	   /* Digpll enable val */
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_4_VAL	0x1B06	   /* System clk Control val */

/* 64bit hardware clock registers (microsecond tick) */
#define MM6108_REG_CLINT_MTIME_0_ADDR		0x200bff8
#define MM6108_REG_CLINT_MTIME_1_ADDR		0x200bffc

/** Delay after initiating digital reset for external xtal init */
#define MM6108_XTAL_INIT_DIG_RESET_DELAY_MS 50
/** Extra delay to wait on sdio transactions until the xtal has been initialised */
#define MM6108_XTAL_INIT_BUS_TRANS_DELAY_MS 2

#define MM6108_DMEM_ADDR_START		0x80100000

/* 16 bits of OTP for board type */
#define MM6108_BOARD_TYPE_MASK		GENMASK(15, 0)

/* MM610X board type max value */
#define MM610X_BOARD_TYPE_MAX_VALUE (MM6108_BOARD_TYPE_MASK - 1)

/* 10 bits of OTP for country code */
#define MM6108_COUNTRY_CODE_MASK	GENMASK(25, 16)

/**
 * 40 uS is needed after each block
 */
#define MM6108_SPI_INTER_BLOCK_DELAY_NANO_S	40000

#define MM6108_REG_OTP_DATA_BASE_ADDRESS	0x10054118

/* CLK enables */
#define MM610X_CORE_CLK_ENABLE_MASK (GENMASK(3, 0))

#define MM6108_FW_BASE				"mm6108"

#define MM6108_COREDUMP_MEMORY_REGION_TOTAL_LEN	297748

static const struct morse_coredump_mem_region mm6108_coredump_mem_regions[] = {
	{ .addr = 0x80100000, .len = 131072 },
	{ .addr = 0x80200000, .len = 65536 },
	{ .addr = 0x80300000, .len = 65536 },
	{ .addr = 0x80400000, .len = 32768 },
	{ .addr = 0x1008802c, .len = 400 },
	{ .addr = 0x0c000004, .len = 280 },
	{ .addr = 0x10054084, .len = 180 },
	{ .addr = 0x100a9000, .len = 180 },
	{ .addr = 0x100a90e0, .len = 156 },
	{ .addr = 0x10054000, .len = 128 },
	{ .addr = 0x100a6000, .len = 116 },
	{ .addr = 0x100a0100, .len = 92 },
	{ .addr = 0x10089000, .len = 84 },
	{ .addr = 0x100a7000, .len = 80 },
	{ .addr = 0x100a5000, .len = 68 },
	{ .addr = 0x10060000, .len = 64 },
	{ .addr = 0x100a0180, .len = 60 },
	{ .addr = 0x100a0080, .len = 56 },
	{ .addr = 0x100a8010, .len = 56 },
	{ .addr = 0x100a0040, .len = 44 },
	{ .addr = 0x100a1000, .len = 44 },
	{ .addr = 0x100a2000, .len = 44 },
	{ .addr = 0x100a3000, .len = 44 },
	{ .addr = 0x100a4000, .len = 44 },
	{ .addr = 0x10088000, .len = 40 },
	{ .addr = 0x02004000, .len = 32 },
	{ .addr = 0x100aa100, .len = 32 },
	{ .addr = 0x100a0000, .len = 28 },
	{ .addr = 0x100a3030, .len = 28 },
	{ .addr = 0x100a4030, .len = 28 },
	{ .addr = 0x02000000, .len = 20 },
	{ .addr = 0x100a8050, .len = 20 },
	{ .addr = 0x100a9200, .len = 20 },
	{ .addr = 0x100ab060, .len = 20 },
	{ .addr = 0x100a90c0, .len = 16 },
	{ .addr = 0x100ab100, .len = 16 },
	{ .addr = 0x0c001000, .len = 12 },
	{ .addr = 0x0c002000, .len = 12 },
	{ .addr = 0x0c002080, .len = 12 },
	{ .addr = 0x0c002100, .len = 12 },
	{ .addr = 0x0c002180, .len = 12 },
	{ .addr = 0x100ab040, .len = 12 },
	{ .addr = 0x100ac000, .len = 12 },
	{ .addr = 0x100ac100, .len = 12 },
	{ .addr = 0x100ac200, .len = 12 },
	{ .addr = 0x100ac300, .len = 12 },
	{ .addr = 0x100ac400, .len = 12 },
	{ .addr = 0x100ac500, .len = 12 },
	{ .addr = 0x100ac600, .len = 12 },
	{ .addr = 0x0200bff8, .len = 8 },
	{ .addr = 0x100a9180, .len = 8 },
	{ .addr = 0x100ac020, .len = 8 },
	{ .addr = 0x100ac120, .len = 8 },
	{ .addr = 0x100ac220, .len = 8 },
	{ .addr = 0x100ac320, .len = 8 },
	{ .addr = 0x100ac420, .len = 8 },
	{ .addr = 0x100ac520, .len = 8 },
	{ .addr = 0x100ac620, .len = 8 },
	{ .addr = 0x0c200000, .len = 4 },
	{ .addr = 0x0c201000, .len = 4 },
	{ .addr = 0x0c202000, .len = 4 },
	{ .addr = 0x0c203000, .len = 4 },
	{ .addr = 0x10054600, .len = 4 },
	{ .addr = 0x10054700, .len = 4 },
	{ .addr = 0x10054d20, .len = 4 },
	{ .addr = 0x10054d40, .len = 4 },
	{ .addr = 0x10054d50, .len = 4 },
	{ .addr = 0x100a8000, .len = 4 },
	{ .addr = 0x100aa000, .len = 4 },
	{ .addr = 0x100aa080, .len = 4 },
	{ .addr = 0x100ab000, .len = 4 },
	{ .addr = 0x100ab080, .len = 4 },
	{ .addr = 0x100ab0c0, .len = 4 },
	{ .addr = 0x100ac018, .len = 4 },
	{ .addr = 0x100ac118, .len = 4 },
	{ .addr = 0x100ac218, .len = 4 },
	{ .addr = 0x100ac318, .len = 4 },
	{ .addr = 0x100ac418, .len = 4 },
	{ .addr = 0x100ac518, .len = 4 },
	{ .addr = 0x100ac618, .len = 4 },
};

static const char *mm610x_get_hw_version(u32 chip_id)
{
	switch (chip_id) {
	case MM6108A0_ID:
		return "MM6108A0";
	case MM6108A1_ID:
		return "MM6108A1";
	case MM6108A2_ID:
		return "MM6108A2";
	}
	return "unknown";
}

static char *mm610x_get_fw_path(u32 chip_id)
{
	const char *fw_variant = "";

	if (is_thin_lmac_mode())
		fw_variant = MORSE_FW_THIN_LMAC_STRING;
	else if (is_virtual_sta_test_mode())
		fw_variant = MORSE_FW_VIRTUAL_STA_STRING;

	return kasprintf(GFP_KERNEL, MORSE_FW_DIR "/" MM6108_FW_BASE "%s" MORSE_FW_EXT, fw_variant);
}

static u8 mm610x_get_warm_boot_time_ms(u32 chip_id)
{
	/* MM6108A0 takes < 7ms to be active */
	if (chip_id == MM6108A0_ID || chip_id == MM6108A1_ID)
		return 10;
	else
		return 20;
}

static u32 mm610x_get_cold_boot_time_ms(u32 chip_id)
{
	return 500;
}

static u32 mm610x_get_firmware_trigger_delay_ms(u32 chip_id)
{
	return 1;
}

static u32 mm610x_get_spi_inter_block_delay_ns(bool burst_enabled)
{
	return MM6108_SPI_INTER_BLOCK_DELAY_NANO_S;
}

static void mm610x_enable_ext_xtal_delay(struct morse *mors, bool enable)
{
	/* Set XTAL init bus transaction delay on each digital reset */
	mors->cfg->xtal_init_bus_trans_delay_ms = (enable) ?
		MM6108_XTAL_INIT_BUS_TRANS_DELAY_MS : 0;
}

static int mm610x_ext_xtal_init(struct morse *mors)
{
	/* switch aon-clk to 32 kHz and latch it */
	morse_reg32_write(mors, MM6108_REG_AON_LATCH_ADDR,
			MM6108_REG_XTAL_INIT_SEQ_AON_CLK_VAL);
	msleep(mors->cfg->xtal_init_bus_trans_delay_ms);

	/* enable gpio 1 which will enable the xtal */
	morse_reg32_write(mors, MM6108_REG_XTAL_INIT_SEQ_ADDR_1,
			MM6108_REG_XTAL_INIT_SEQ_ADDR_1_VAL);
	msleep(mors->cfg->xtal_init_bus_trans_delay_ms);
	morse_reg32_write(mors, MM6108_REG_XTAL_INIT_SEQ_ADDR_2,
			MM6108_REG_XTAL_INIT_SEQ_ADDR_2_VAL);
	msleep(mors->cfg->xtal_init_bus_trans_delay_ms);

	/* enable digital pll */
	morse_reg32_write(mors, MM6108_REG_XTAL_INIT_SEQ_ADDR_3,
			MM6108_REG_XTAL_INIT_SEQ_ADDR_3_VAL);
	msleep(mors->cfg->xtal_init_bus_trans_delay_ms);

	/* switch clocks */
	morse_reg32_write(mors, MM6108_REG_XTAL_INIT_SEQ_ADDR_4,
			MM6108_REG_XTAL_INIT_SEQ_ADDR_4_VAL);
	msleep(mors->cfg->xtal_init_bus_trans_delay_ms);

	/* It's now initialised no need to wait further in bus logic */
	mm610x_enable_ext_xtal_delay(mors, false);
	return 0;
}

static int mm610x_digital_reset(struct morse *mors)
{
	morse_claim_bus(mors);

	if (enable_ext_xtal_init)
		mm610x_enable_ext_xtal_delay(mors, true);

	if (MORSE_REG_RESET(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_RESET(mors), MORSE_REG_RESET_VALUE(mors));

	/* SDIO needs some time after reset */
	if (sdio_reset_time > 0)
		msleep(sdio_reset_time);

	if (MORSE_REG_EARLY_CLK_CTRL_VALUE(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_CLK_CTRL(mors),
				  MORSE_REG_EARLY_CLK_CTRL_VALUE(mors));

	if (enable_ext_xtal_init) {
		mdelay(MM6108_XTAL_INIT_DIG_RESET_DELAY_MS);
		mm610x_ext_xtal_init(mors);
	}

	mors->chip_was_reset = true;
	morse_release_bus(mors);
	return 0;
}

static int mm610x_read_board_type(struct morse *mors)
{
	int ret = -EINVAL;
	u32 otp_data4;

	if (MORSE_REG_OTP_DATA_WORD(mors, 4) != 0) {
		morse_claim_bus(mors);
		ret = morse_reg32_read(mors, MORSE_REG_OTP_DATA_WORD(mors, 4), &otp_data4);
		morse_release_bus(mors);

		if (ret < 0)
			return ret;

		return (otp_data4 & MM6108_BOARD_TYPE_MASK);
	}
	return ret;
}

static int mm610x_decode_country(u16 encoded_value, char *country)
{
	/* Minimum is AA which when offset gives us 0x21*/
	const int encode_min = BIT(0) | BIT(5);
	/* Maximum is all 10 bits set */
	const int encode_max = BIT(10) - 1;

	const char offset = 'A' - 1;

	if (encoded_value > encode_max || encoded_value < encode_min)
		return -ENOENT;

	country[0] = (char)(encoded_value & GENMASK(4, 0)) + offset;
	country[1] = (char)(encoded_value >> 5) + offset;

	return 0;
}

static int mm610x_read_encoded_country(struct morse *mors)
{
	int ret = -ENOENT;
	u32 otp_data4;

	if (MORSE_REG_OTP_DATA_WORD(mors, 4) != 0) {
		u32 country;

		morse_claim_bus(mors);
		ret = morse_reg32_read(mors, MORSE_REG_OTP_DATA_WORD(mors, 4), &otp_data4);
		morse_release_bus(mors);

		if (ret < 0)
			return ret;

		country = BMGET(otp_data4, MM6108_COUNTRY_CODE_MASK);
		ret = mm610x_decode_country(country, mors->country);
	}

	return ret;
}

static int mm610x_pre_coredump_hook(struct morse *mors, enum morse_coredump_method method)
{
	int ret;
	u32 clk_reg;

	if (method == COREDUMP_METHOD_USERSPACE_SCRIPT)
		return 0;

	morse_claim_bus(mors);

	ret = morse_reg32_read(mors, MORSE_REG_CLK_CTRL(mors), &clk_reg);
	if (ret)
		goto exit;

	/* Clear all clock enables for each core. This is to reduce the likelihood
	 * of bus contention while the driver initiates reads of imem/dmem.
	 */
	clk_reg &= ~(u32)(MM610X_CORE_CLK_ENABLE_MASK);
	ret = morse_reg32_write(mors, MORSE_REG_CLK_CTRL(mors), clk_reg);
	if (ret)
		goto exit;

exit:
	morse_release_bus(mors);

	return ret;
}

static bool mm610x_chip_id_matches(u32 chip_id)
{
	return chip_id == MM6108A0_ID ||
	       chip_id == MM6108A1_ID ||
	       chip_id == MM6108A2_ID;
}

static const struct morse_hw_regs mm6108_regs = {
	/* Register address maps */
	.irq_base_address = MM6108_REG_INT_BASE,
	.trgr_base_address = MM6108_REG_TRGR_BASE,

	/* Reset */
	.cpu_reset_address = MM6108_REG_RESET,
	.cpu_reset_value = MM6108_REG_RESET_VALUE,

	/* Pointer to manifest */
	.manifest_ptr_address = MM6108_REG_MANIFEST_PTR_ADDRESS,

	/* Trigger SWI */
	.msi_address = MM6108_REG_MSI,
	.msi_value = 0x1,
	/* Firmware */
	.magic_num_value = MM6108_REG_HOST_MAGIC_VALUE,

	/* Clock control */
	.clk_ctrl_address = MM6108_REG_CLK_CTRL,
	.clk_ctrl_value = MM6108_REG_CLK_CTRL_VALUE,
	.early_clk_ctrl_value = MM6108_REG_EARLY_CLK_CTRL_VALUE,

	/* OTP data base address */
	.otp_data_base_address = MM6108_REG_OTP_DATA_BASE_ADDRESS,

	.pager_base_address = MM6108_DMEM_ADDR_START,

	.test_mode_mem_base_address = MM6108_DMEM_ADDR_START,

	/* AON registers */
	.aon_latch = MM6108_REG_AON_LATCH_ADDR,
	.aon_latch_mask = MM6108_REG_AON_LATCH_MASK,
	.aon = MM6108_REG_AON_ADDR,
	.aon_count = 2,

	/* MTIME registers */
	.mtime_lower = MM6108_REG_CLINT_MTIME_0_ADDR,
	.mtime_upper = MM6108_REG_CLINT_MTIME_1_ADDR,

	/* hart0 boot address */
	.boot_address = MM6108_REG_APPS_BOOT_ADDR,
};

struct morse_hw_cfg mm6108_cfg = {
	.regs = &mm6108_regs,
	.chip_id_address = MM6108_REG_CHIP_ID,
	.ops = &morse_pageset_ops,
	.get_warm_boot_time_ms = mm610x_get_warm_boot_time_ms,
	.get_cold_boot_time_ms = mm610x_get_cold_boot_time_ms,
	.get_firmware_trigger_delay_ms = mm610x_get_firmware_trigger_delay_ms,
	.enable_sdio_burst_mode = NULL,
	.get_spi_inter_block_delay_ns = mm610x_get_spi_inter_block_delay_ns,
	.get_board_type = mm610x_read_board_type,
	.get_encoded_country = mm610x_read_encoded_country,
	.get_hw_version = mm610x_get_hw_version,
	.get_fw_path = mm610x_get_fw_path,
	.digital_reset = mm610x_digital_reset,
	.pre_coredump_hook = mm610x_pre_coredump_hook,
	.post_coredump_hook = NULL,
	.board_type_max_value = MM610X_BOARD_TYPE_MAX_VALUE,
	.bus_double_read = true,
	.mem_access_cfg = NULL,
	.enable_short_bcn_as_dtim = false,
	.led_group.enable_led_support = false,
	.enable_ext_xtal_delay = mm610x_enable_ext_xtal_delay,
	.set_security_manifest_ptr = NULL,
	.get_secureboot_status = NULL,
	.amsdu.enabled = true,
	.amsdu.has_1500_byte_limit = true,
	.gpios.busy = -ENOENT,
	.gpios.reset = -ENOENT,
	.gpios.spi_irq = -ENOENT,
	.gpios.wake = -ENOENT,
	.coredump.regions = mm6108_coredump_mem_regions,
	.coredump.regions_num = ARRAY_SIZE(mm6108_coredump_mem_regions),
	.coredump.regions_total_len = MM6108_COREDUMP_MEMORY_REGION_TOTAL_LEN,
};

struct morse_chip_series mm61xx_chip_series = {
	.cfg = &mm6108_cfg,
	.chip_id_matches = mm610x_chip_id_matches,
};

MODULE_FIRMWARE(MORSE_FW_DIR "/" MM6108_FW_BASE MORSE_FW_EXT);
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM6108_FW_BASE MORSE_FW_THIN_LMAC_STRING MORSE_FW_EXT);
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM6108_FW_BASE MORSE_FW_VIRTUAL_STA_STRING MORSE_FW_EXT);
