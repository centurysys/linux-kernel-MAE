#ifndef _MORSE_FW_H_
#define _MORSE_FW_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <linux/firmware.h>
#include "capabilities.h"
#include "misc.h"

#define BCF_DATABASE_SIZE	(1024)	/* From firmware */
#define MORSE_FW_DIR		"morse"
#define MORSE_FW_MAX_SIZE	(13 * 32 * 1024)
#define MORSE_BCF_MAX_SIZE	(BCF_DATABASE_SIZE * 1024)

#define IFLASH_BASE_ADDR	0x400000
#define DFLASH_BASE_ADDR	0xC00000

#define MAX_BCF_NAME_LEN	64

/* FW_CAPABILITIES_FLAGS_WIDTH = ceil(MORSE_CAPS_MAX_HW_LEN / 32) */
#define FW_CAPABILITIES_FLAGS_WIDTH (4)

#if (FW_CAPABILITIES_FLAGS_WIDTH > CAPABILITIES_FLAGS_WIDTH)
	#error "Capability subset filled by firmware is to big"
#endif


enum morse_fw_info_tlv_type {
	MORSE_FW_INFO_TLV_BCF_ADDR = 1
};

struct morse_fw_info_tlv {
	__le16 type;
	__le16 length;
	u8 val[0];
} __packed;

enum morse_fw_host_table_capab_tag {
	/* The S1G capability tag */
	MORSE_FW_HOST_TABLE_CAPAB_TAG_S1G = 0,
};
struct fw_capabilities_header {
	/** The tag used to identify which capability this represents */
	__le16 tag;
	/** The length of the capability structure including this header */
	__le16 length;
} __packed;

struct fw_capabilities_s1g {
	/** The common header for the capabilities */
	struct fw_capabilities_header header;
	/** The capability flags */
	__le32 flags[FW_CAPABILITIES_FLAGS_WIDTH];
	/**
	 * The minimum A-MPDU start spacing required by firmware.
	 * Value | Description
	 * ------|------------
	 * 0     | No restriction
	 * 1     | 1/4 us
	 * 2     | 1/2 us
	 * 3     | 1 us
	 * 4     | 2 us
	 * 5     | 4 us
	 * 6     | 8 us
	 * 7     | 16 us
	 */
	u8 ampdu_mss;
	/** The beamformee STS capability value */
	u8 beamformee_sts_capability;
	/* Number of sounding dimensions */
	u8 number_sounding_dimensions;
	/**
	 * The maximum A-MPDU length. This is the exponent value such that
	 * (2^(13 + exponent) - 1) is the length
	 */
	u8 maximum_ampdu_length;
} __packed;

struct extended_host_table {
	/** The length of this table */
	__le32 extended_host_table_length;
	/** Device MAC address */
	u8 dev_mac_addr[6];
	/** The S1G capabilities */
	struct fw_capabilities_s1g s1g_caps;
	/** padding to the nearest 4 byte boundary */
	u8 padding[2];
} __packed;

int morse_firmware_init(struct morse *mors,
			const char *fw_name,
			bool dl_firmware, bool chk_firmware);

/**
 * @brief Perform non-destructive-reset of the chip,
 *		  preserving the existing SDIO enumeration whilst
 *		  resetting the firmware state
 *
 * @param mors The global morse config object
 * @return int 0 if the firmware resets to initial state, -error otherwise
 */
int morse_firmware_exec_ndr(struct morse *mors);

/**
 * morse_firmware_read_ext_host_table - Read the firmware's extended host table for device capabilities
 *
 * @mors pointer to the chip object
 * @ext_host_table pointer to the extended host table to fill
 *
 * Return: 0 if the table was read successfully, error if otherwise.
 */
int morse_firmware_read_ext_host_table(struct morse *mors, struct extended_host_table *ext_host_table);

int morse_firmware_get_host_table_ptr(struct morse *mors);

#endif  /* !_MORSE_FW_H_ */
