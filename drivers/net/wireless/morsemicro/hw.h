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

#define MORSE_SDIO_RW_ADDR_BOUNDARY_MASK ((u32)0xFFFF0000)

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
#define MORSE_REG_OTP_DATA_WORD(mors, word) \
		((mors)->cfg->regs->otp_data_base_address + (4 * (word)))

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

#define MORSE_REG_BOOT_ADDR(mors)	((mors)->cfg->regs->boot_address)
#define MORSE_REG_BOOT_ADDR_VALUE(mors)	\
					((mors)->cfg->regs->boot_value)

#define MORSE_REG_AON_ADDR(mors)	((mors)->cfg->regs->aon)
#define MORSE_REG_AON_COUNT(mors)	((mors)->cfg->regs->aon_count)
#define MORSE_REG_AON_LATCH_ADDR(mors)	\
					((mors)->cfg->regs->aon_latch)
#define MORSE_REG_AON_LATCH_MASK(mors)	\
					((mors)->cfg->regs->aon_latch_mask)

/** Bit 17 to 24 reserved for the beacon VIF 0 to 7 interrupts */
#define MORSE_INT_BEACON_VIF_MASK_ALL		(GENMASK(24, 17))
#define MORSE_INT_BEACON_BASE_NUM			(17)

#define MORSE_INT_NDP_PROBE_REQ_PV0_NUM		(25)
#define MORSE_INT_NDP_PROBE_REQ_PV0_MASK	BIT(MORSE_INT_NDP_PROBE_REQ_PV0_NUM)
#define MORSE_INT_NDP_PROBE_REQ_PV1_NUM		(26)
#define MORSE_INT_NDP_PROBE_REQ_PV1_MASK	BIT(MORSE_INT_NDP_PROBE_REQ_PV1_NUM)

#define MORSE_WAKEPIN_RPI_GPIO_DEFAULT                  (3)
#define MORSE_ASYNC_WAKEUP_FROM_CHIP_RPI_GPIO_DEFAULT   (7)
#define MORSE_RESETPIN_RPI_GPIO_DEFAULT                 (5)
#define MORSE_SPI_HW_IRQ_RPI_GPIO_DEFAULT               (25)

/* OTP Bootrom XTAL wait bits[89:86] for MM610x */
#define MM610X_OTP_DATA2_XTAL_WAIT_POS	GENMASK(25, 22)

/* OTP Supplemental Chip ID */
#define MM610X_OTP_DATA2_SUPPLEMENTAL_CHIP_ID GENMASK(23, 16)

/* OTP 8MHz support bit[48] for MM610x */
#define MM610X_OTP_DATA1_8MHZ_SUPPORT	BIT(18)

#define CHIP_TYPE_SILICON	0x0
#define CHIP_TYPE_FPGA		0x1

/* Chip ID for MM6108 */
#define MM6108A0_ID MORSE_DEVICE_ID(0x6, 2, CHIP_TYPE_SILICON)
#define MM6108A1_ID MORSE_DEVICE_ID(0x6, 3, CHIP_TYPE_SILICON)
#define MM6108A2_ID MORSE_DEVICE_ID(0x6, 4, CHIP_TYPE_SILICON)


/* Last element to declare in valid_chip_ids[] */
#define CHIP_ID_END		0xFFFFFFFF

enum host_table_firmware_flags {
	/** Firmware supports S1G */
	MORSE_FW_FLAGS_SUPPORT_S1G = BIT(0),
	/** BUSY GPIO pin is active low */
	MORSE_FW_FLAGS_BUSY_ACTIVE_LOW = BIT(1)
};

struct host_table {
	__le32 magic_number;
	__le32 fw_version_number;
	__le32 host_flags;
	__le32 firmware_flags;
	__le32 memcmd_cmd_addr;
	__le32 memcmd_resp_addr;
	__le32 extended_host_table_addr;
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
	u32 boot_address;
	u32 boot_value;
	u32 otp_data_base_address;
	u32 pager_base_address;
	u32 aon_latch;
	u32 aon_latch_mask;
	u32 aon;
	u8 aon_count;
};

struct morse_hw_cfg {
	const struct morse_hw_regs *regs;

	/**
	 * @fw_base: The initial part of the firmware filename.
	 * For example, "mm6108" will become /lib/firmware/morse/mm6108.bin.
	 */
	const char *fw_base;

	const struct morse_firmware *fw;
	const struct chip_if_ops *ops;

	/**
	 * Get hardware version
	 *
	 * @chip_id: Registered chip ID when loading the driver
	 */
	const char *(*get_hw_version)(u32 chip_id);

	/**
	 * Get PS Wakeup delay depending on chip id
	 *
	 * @chip_id: Registered chip ID when loading the driver
	 */
	 u8 (*get_ps_wakeup_delay_ms)(u32 chip_id);

	/**
	 * Enable SDIO burst mode
	 *
	 * @return inter_block_delay_ns
	 */
	int (*enable_sdio_burst_mode)(struct morse *mors);

	/**
	 * Perform necessary actions to prepare the chip before firmware load
	 *
	 * @return error code
	 */
	int (*pre_load_prepare)(struct morse *mors);

	/**
	 * Return the board type burnt into OTP
	 *
	 * @return the board type if available, else -EINVAL
	 */
	int (*get_board_type)(struct morse *mors);

	/**
	 * @bus_double_read: Decide if the bus workaround is required to recover
	 * the page header repeated words
	 */
	bool bus_double_read;
	bool mm_ps_gpios_supported;
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
extern struct morse_hw_cfg mm6108_cfg;

/**
 * morse_hw_reset - performs a hardware reset on the chip by toggling
 *  the reset gpio.
 * @reset_pin: RPi GPIO pin number connected to chip reset pin
 *
 * Return: int return code.
 */
int morse_hw_reset(int reset_pin);

/**
 * is_otp_xtal_wait_supported - checks the xtal wait bit.
 * @mors: morse struct containing the config
 *
 * Return: true if the check is successful or
 *  false if any step fails
 */
bool is_otp_xtal_wait_supported(struct morse *mors);

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

#endif /* !_MORSE_HW_H_ */
