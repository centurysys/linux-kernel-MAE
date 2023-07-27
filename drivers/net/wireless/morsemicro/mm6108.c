/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/firmware.h>
#include <linux/delay.h>

#include "hw.h"
#include "morse.h"
#include "firmware.h"
#include "bus.h"
#include "debug.h"
#include "pageset.h"

/* Generates IRQ to RISC */
#define MM6108_REG_TRGR_BASE		0x100a6000
#define MM6108_REG_INT_BASE		0x100a6050

#define MM6108_REG_MSI			0x02000000

#define MM6108_REG_MANIFEST_PTR_ADDRESS	0x10054d40

#define MM6108_REG_HOST_MAGIC_VALUE	0xDEADBEEF

#define MM6108_REG_RESET		0x10054050
#define MM6108_REG_RESET_VALUE		0xDEAD

#define MM6108_REG_CHIP_ID		0x10054d20

#define MM6108_REG_CLK_CTRL		0x1005406C
#define MM6108_REG_CLK_CTRL_VALUE	0xef
#define MM6108_REG_EARLY_CLK_CTRL_VALUE	0xe5

#define MM6108_REG_MAC_BOOT_ADDR        0x10054024
#define MM6108_REG_MAC_BOOT_ADDR_VALUE  0x100000

#define MM6108_REG_AON_ADDR		0x10058094
#define MM6108_REG_AON_LATCH_ADDR	0x1005807C
#define MM6108_REG_AON_LATCH_MASK	0x1

#define MM6108_DMEM_ADDR_START		0x80100000

/* MM610X board type max value */
#define MM610X_BOARD_TYPE_MAX_VALUE (0xF - 1)

/**
 * 40 uS is needed after each block
 */
#define MM6108_SPI_INTER_BLOCK_DELAY_NANO_S	40000

#define MM6108_REG_EFUSE_DATA_BASE_ADDRESS	0x10054118

#define MORSE_FW_NAME			"mm6108.bin"
#define MORSE_FW_VIRTUAL_STA_NAME	"mm6108vs.bin"
#define MORSE_FW_THIN_LMAC_NAME		"mm6108tlm.bin"

/* Chip ID for MM6108AX */
#define MM6108A0_ID MORSE_DEVICE_ID(0x6, 2, 0)
#define MM6108A1_ID MORSE_DEVICE_ID(0x6, 3, 0)

static u8 mm610x_get_wakeup_delay_ms(u32 chip_id)
{
	if (chip_id == MM6108A0_ID || chip_id == MM6108A1_ID)
		return 10;
	else
		return 20;
}

static int mm610x_enable_burst_mode(struct morse *mors)
{
	return MM6108_SPI_INTER_BLOCK_DELAY_NANO_S;
}

static int mm610x_read_board_type(struct morse *mors)
{
	int ret = -EINVAL;
	u32 efuse_data2;

	if (MORSE_REG_EFUSE_DATA2(mors) != 0) {
		morse_claim_bus(mors);
		ret = morse_reg32_read(mors, MORSE_REG_EFUSE_DATA2(mors), &efuse_data2);
		morse_release_bus(mors);

		if (ret < 0)
			return ret;

		return (efuse_data2 >> 27) & 0xF;
	}
	return ret;
}

static const struct morse_hw_regs mm6108a_regs = {
	/* Register address maps */
	.irq_base_address = MM6108_REG_INT_BASE,
	.trgr_base_address = MM6108_REG_TRGR_BASE,
	/* chip id */
	.chip_id_address = MM6108_REG_CHIP_ID,

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

	/* Host boot WAR */
	.mac_boot_address = MM6108_REG_MAC_BOOT_ADDR,
	.mac_boot_value = MM6108_REG_MAC_BOOT_ADDR_VALUE,

	.pager_base_address = MM6108_DMEM_ADDR_START,
};

struct morse_hw_cfg mm6108a_cfg = {
	.regs =  &mm6108a_regs,
	.fw_name = MORSE_FW_DIR "/" MORSE_FW_NAME,
	.ops = &morse_pageset_sw_ops,
	.get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms
};

static const struct morse_hw_regs mm6108b_regs = {
	/* Register address maps */
	.irq_base_address = MM6108_REG_INT_BASE,
	.trgr_base_address = MM6108_REG_TRGR_BASE,
	/* chip id */
	.chip_id_address = MM6108_REG_CHIP_ID,

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

	.pager_base_address = MM6108_DMEM_ADDR_START,
};

struct morse_hw_cfg mm6108b_cfg = {
	.regs =  &mm6108b_regs,
	.fw_name = MORSE_FW_DIR "/" MORSE_FW_NAME,
	.ops = &morse_pageset_sw_ops,
	.get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms
};

static const struct morse_hw_regs mm6108c_regs = {
	/* Register address maps */
	.irq_base_address = MM6108_REG_INT_BASE,
	.trgr_base_address = MM6108_REG_TRGR_BASE,
	/* chip id */
	.chip_id_address = MM6108_REG_CHIP_ID,

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

	/* efuse data base address */
	.efuse_data_base_address = MM6108_REG_EFUSE_DATA_BASE_ADDRESS,

	.pager_base_address = MM6108_DMEM_ADDR_START,

	/* AON registers */
	.aon_latch = MM6108_REG_AON_LATCH_ADDR,
	.aon_latch_mask = MM6108_REG_AON_LATCH_MASK,
	.aon = MM6108_REG_AON_ADDR,
	.aon_count = 2,
};

struct morse_hw_cfg mm6108c_cfg = {
	.regs =  &mm6108c_regs,
	.fw_name = MORSE_FW_DIR "/" MORSE_FW_NAME,
	.ops = &morse_pageset_hw_ops,
	.get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms,
	.enable_sdio_burst_mode = mm610x_enable_burst_mode,
	.get_board_type = mm610x_read_board_type,
	.board_type_max_value = MM610X_BOARD_TYPE_MAX_VALUE,
	.valid_chip_ids = {
		MM6108A0_ID,
		MM6108A1_ID,
		CHIP_ID_END},
};

struct morse_hw_cfg mm6108c_tlm_cfg = {
	.regs =  &mm6108c_regs,
	.fw_name = MORSE_FW_DIR "/" MORSE_FW_THIN_LMAC_NAME,
	.ops = &morse_pageset_hw_ops,
	.get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms,
	.get_board_type = mm610x_read_board_type,
	.board_type_max_value = MM610X_BOARD_TYPE_MAX_VALUE,
};

struct morse_hw_cfg mm6108c_vs_cfg = {
	.regs =  &mm6108c_regs,
	.fw_name = MORSE_FW_DIR "/" MORSE_FW_VIRTUAL_STA_NAME,
	.ops = &morse_pageset_hw_ops,
	.get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms,
	.get_board_type = mm610x_read_board_type,
	.board_type_max_value = MM610X_BOARD_TYPE_MAX_VALUE
};

MODULE_FIRMWARE(MORSE_FW_DIR "/" MORSE_FW_NAME);
