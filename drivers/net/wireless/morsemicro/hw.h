#ifndef _MORSE_HW_H_
#define _MORSE_HW_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include "chip_if.h"

/* To be moved to sdio.c */
#define MORSE_REG_ADDRESS_BASE		0x10000
#define MORSE_REG_ADDRESS_WINDOW_0	MORSE_REG_ADDRESS_BASE
#define MORSE_REG_ADDRESS_WINDOW_1	(MORSE_REG_ADDRESS_BASE + 1)
#define MORSE_REG_ADDRESS_CONFIG	(MORSE_REG_ADDRESS_BASE + 2)

#define MORSE_SDIO_RW_ADDR_BOUNDARY_MASK (0xFFFF0000)

#define MORSE_CONFIG_ACCESS_1BYTE	0
#define MORSE_CONFIG_ACCESS_2BYTE	1
#define MORSE_CONFIG_ACCESS_4BYTE	2

/* Generates IRQ to RISC */
#define MORSE_REG_TRGR_BASE(mors)	((mors)->cfg->regs->trgr_base_address)
#define MORSE_REG_TRGR1_STS(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x00)
#define MORSE_REG_TRGR1_SET(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x04)
#define MORSE_REG_TRGR1_CLR(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x08)
#define MORSE_REG_TRGR1_EN(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x0C)
#define MORSE_REG_TRGR2_STS(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x10)
#define MORSE_REG_TRGR2_SET(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x14)
#define MORSE_REG_TRGR2_CLR(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x18)
#define MORSE_REG_TRGR2_EN(mors)	(MORSE_REG_TRGR_BASE(mors) + 0x1C)

#define MORSE_REG_INT_BASE(mors)	((mors)->cfg->regs->irq_base_address)
#define MORSE_REG_INT1_STS(mors)	(MORSE_REG_INT_BASE(mors) + 0x00)
#define MORSE_REG_INT1_SET(mors)	(MORSE_REG_INT_BASE(mors) + 0x04)
#define MORSE_REG_INT1_CLR(mors)	(MORSE_REG_INT_BASE(mors) + 0x08)
#define MORSE_REG_INT1_EN(mors)		(MORSE_REG_INT_BASE(mors) + 0x0C)
#define MORSE_REG_INT2_STS(mors)	(MORSE_REG_INT_BASE(mors) + 0x10)
#define MORSE_REG_INT2_SET(mors)	(MORSE_REG_INT_BASE(mors) + 0x14)
#define MORSE_REG_INT2_CLR(mors)	(MORSE_REG_INT_BASE(mors) + 0x18)
#define MORSE_REG_INT2_EN(mors)		(MORSE_REG_INT_BASE(mors) + 0x1C)

#define MORSE_REG_CHIP_ID(mors)		((mors)->cfg->regs->chip_id_address)
#define MORSE_REG_EFUSE_DATA0(mors)	((mors)->cfg->regs->efuse_data_base_address)
#define MORSE_REG_EFUSE_DATA1(mors)	(MORSE_REG_EFUSE_DATA0(mors) + 0x04)
#define MORSE_REG_EFUSE_DATA2(mors)	(MORSE_REG_EFUSE_DATA0(mors) + 0x08)

#define MORSE_REG_MSI(mors)		((mors)->cfg->regs->msi_address)
#define MORSE_REG_MSI_HOST_INT(mors)	((mors)->cfg->regs->msi_value)

#define MORSE_REG_HOST_MAGIC_VALUE(mors)	\
					((mors)->cfg->regs->magic_num_value)

#define MORSE_REG_RESET(mors)		((mors)->cfg->regs->cpu_reset_address)
#define MORSE_REG_RESET_VALUE(mors)	((mors)->cfg->regs->cpu_reset_value)

#define MORSE_REG_HOST_MANIFEST_PTR(mors)	\
				((mors)->cfg->regs->manifest_ptr_address)

#define MORSE_REG_EARLY_CLK_CTRL_VALUE(morse)	\
				((mors)->cfg->regs->early_clk_ctrl_value)

#define MORSE_REG_CLK_CTRL(mors)	((mors)->cfg->regs->clk_ctrl_address)
#define MORSE_REG_CLK_CTRL_VALUE(mors)	((mors)->cfg->regs->clk_ctrl_value)

#define MORSE_REG_MAC_BOOT_ADDR(mors)	((mors)->cfg->regs->mac_boot_address)
#define MORSE_REG_MAC_BOOT_ADDR_VALUE(mors)	\
					((mors)->cfg->regs->mac_boot_value)

#define MORSE_REG_AON_ADDR(mors)	((mors)->cfg->regs->aon)
#define MORSE_REG_AON_COUNT(mors)	((mors)->cfg->regs->aon_count)
#define MORSE_REG_AON_LATCH_ADDR(mors)	\
					((mors)->cfg->regs->aon_latch)
#define MORSE_REG_AON_LATCH_MASK(mors)	\
					((mors)->cfg->regs->aon_latch_mask)


#define MORSE_INT_BEACON_NUM		(17)
#define MORSE_INT_BEACON_MASK		(1 << MORSE_INT_BEACON_NUM)

#define MORSE_INT_NDP_PROBE_REQ_PV0_NUM  (18)
#define MORSE_INT_NDP_PROBE_REQ_PV0_MASK (1 << MORSE_INT_NDP_PROBE_REQ_PV0_NUM)
#define MORSE_INT_NDP_PROBE_REQ_PV1_NUM  (19)
#define MORSE_INT_NDP_PROBE_REQ_PV1_MASK (1 << MORSE_INT_NDP_PROBE_REQ_PV1_NUM)

#define MORSE_WAKEPIN_RPI_GPIO_DEFAULT                  (3)
#define MORSE_ASYNC_WAKEUP_FROM_CHIP_RPI_GPIO_DEFAULT   (7)
#define MORSE_RESETPIN_RPI_GPIO_DEFAULT                 (5)
#define MORSE_SPI_HW_IRQ_RPI_GPIO_DEFAULT               (25)

/* EFuse Bootrom XTAL wait bits[89:86] for MM610x */
#define MM610X_EFUSE_DATA2_XTAL_WAIT_POS	GENMASK(25, 22)

/* EFuse Supplemental Chip ID */
#define MM610X_EFUSE_DATA2_SUPPLEMENTAL_CHIP_ID GENMASK(23, 16)

/* EFuse 8MHz support bit[48] for MM610x */
#define MM610X_EFUSE_DATA1_8MHZ_SUPPORT	BIT(18)

#define CHIP_TYPE_FPGA 0x1

/* Last element to declare in valid_chip_ids[] */
#define CHIP_ID_END		0xFFFFFFFF

struct host_table {
	u32 magic_number;
	u32 fw_version_number;
	u32 host_flags;
	u32 firmware_flags;
	u32 memcmd_cmd_addr;
	u32 memcmd_resp_addr;
	u32 extended_host_table_addr;
	struct morse_chip_if_host_table chip_if;
} __packed;

/**
 * struct morse_hw_memory - On chip memory address space
 *
 * Identifies memory space on chip.
 * Used to optimise chip access
 *
 * @start: Start address of memory space
 * @end: End address of memory space
 */
struct morse_hw_memory {
	u32 start;
	u32 end;
};

struct morse_hw_regs {
	u32 irq_base_address;
	u32 trgr_base_address;
	u32 cpu_reset_address;
	u32 cpu_reset_value;
	u32 msi_address;
	u32 msi_value;
	u32 chip_id_address;
	u32 manifest_ptr_address;
	u32 host_table_address;
	u32 magic_num_value;
	u32 clk_ctrl_address;
	u32 clk_ctrl_value;
	u32 early_clk_ctrl_value;
	u32 mac_boot_address;
	u32 mac_boot_value;
	u32 efuse_data_base_address;
	u32 pager_base_address;
	u32 aon_latch;
	u32 aon_latch_mask;
	u32 aon;
	u8  aon_count;
};

struct morse_hw_cfg {
	const struct morse_hw_regs *regs;
	const char *fw_name;
	const struct morse_firmware *fw;
	const struct chip_if_ops *ops;

	/**
	 * Get PS Wakeup delay depending on chip id
	 *
	 * @chip_id: Registered chip ID when loading the driver
	 */
	u8  (*get_ps_wakeup_delay_ms)(u32 chip_id);

	/**
	 * Enable SDIO burst mode
	 *
	 * @return inter_block_delay_ns
	 */
	int (*enable_sdio_burst_mode)(struct morse *mors);

	/**
	 * Return the board type burnt into OTP
	 *
	 * @return the board type if available, else -EINVAL
	 */
	int (*get_board_type)(struct morse *mors);

	u32 board_type_max_value;
	u32 fw_count;
	u32 host_table_ptr;
	u32 mm_reset_gpio;
	u32 mm_wake_gpio;
	u32 mm_ps_async_gpio;
	u32 mm_spi_irq_gpio;
	u32 valid_chip_ids[];
};


int morse_hw_irq_enable(struct morse *mors, u32 irq, bool enable);
int morse_hw_irq_handle(struct morse *mors);
int morse_hw_irq_clear(struct morse *mors);

/* MM6108 */
extern struct morse_hw_cfg mm6108c_cfg;
extern struct morse_hw_cfg mm6108c_vs_cfg;
extern struct morse_hw_cfg mm6108c_tlm_cfg;

/**
 * morse_hw_reset - performs a hardware reset on the chip by toggling
 *  the reset gpio.
 * @reset_pin: RPi GPIO pin number connected to chip reset pin
 *
 * Return: int return code.
 */
int morse_hw_reset(int reset_pin);


/**
 * is_efuse_xtal_wait_supported - checks the xtal wait bit.
 * @mors: morse struct containing the config
 *
 * Return: true if the check is successful or
 *  false if any step fails
 */
bool is_efuse_xtal_wait_supported(struct morse *mors);

/**
 * morse_hw_is_valid_chip_id - Check valid chip id that support driver by
 * iterating over valid_chip_ids[] until it hits CHIP_ID_END
 *
 * @chip_id: Chip ID code received from the chip
 * @vaild_chip_ids: An array containing valid chip id that support driver
 * with CHIP_ID_END as the last element
 *
 * Return: true if the registered chip id is in the array
 *	   false if not found
 */
bool morse_hw_is_valid_chip_id(u32 chip_id, u32 *valid_chip_ids);

#endif  /* !_MORSE_HW_H_ */
