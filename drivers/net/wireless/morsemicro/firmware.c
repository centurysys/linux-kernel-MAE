/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <net/mac80211.h>
#include <linux/elf.h>

#include "morse.h"
#include "bus.h"
#include "debug.h"
#include "firmware.h"
#include "mac.h"
#include "vendor.h"

struct fw_init_params {
	bool download_fw;
	bool get_host_table_ptr;
	bool verify_fw;
};

/* The last mac address octet value */
u8 macaddr_octet = 0xFF;
module_param(macaddr_octet, byte, 0644);
MODULE_PARM_DESC(macaddr_octet,
		"MACaddr octet 6. 0xFF randomises the value. Ignored if fw MACaddr present");

/* When setting the mac address, the special value 00 will randomise the last 3 octets */
#define MORSE_RANDOMISE_OCTETS "00:00:00"

/* The last 3 mac address octet values */
static char macaddr_suffix[9] = "00:00:00";
module_param_string(macaddr_suffix, macaddr_suffix, ARRAY_SIZE(macaddr_suffix), 0644);
MODULE_PARM_DESC(macaddr_suffix,
	"MACaddr octets 4, 5 and 6. 00:00:00 (default) randomises the value, ignored if fw MACaddr present");

static int sdio_reset_time = CONFIG_MORSE_SDIO_RESET_TIME;
module_param(sdio_reset_time, int, 0644);
MODULE_PARM_DESC(sdio_reset_time, "Time to wait (in msec) after SDIO reset");

static int get_file_header(const u8 *data, Elf32_Ehdr *ehdr)
{
	Elf32_Ehdr *p = (Elf32_Ehdr *)data;

	/* Magic check */
	if (p->e_ident[EI_MAG0] != ELFMAG0 ||
	    p->e_ident[EI_MAG1] != ELFMAG1 ||
	    p->e_ident[EI_MAG2] != ELFMAG2 ||
	    p->e_ident[EI_MAG3] != ELFMAG3)
		return -1;

	/* elf32 and little endian */
	if (p->e_ident[EI_DATA] != ELFDATA2LSB || p->e_ident[EI_CLASS] != ELFCLASS32)
		return -1;

	ehdr->e_phoff = le32_to_cpu(p->e_phoff);
	ehdr->e_phentsize = le16_to_cpu(p->e_phentsize);
	ehdr->e_phnum = le16_to_cpu(p->e_phnum);
	ehdr->e_shoff = le32_to_cpu(p->e_shoff);
	ehdr->e_shentsize = le16_to_cpu(p->e_shentsize);
	ehdr->e_shnum = le16_to_cpu(p->e_shnum);
	ehdr->e_shstrndx = le16_to_cpu(p->e_shstrndx);
	ehdr->e_entry = le32_to_cpu(p->e_entry);

	return 0;
}

static void morse_parse_firmware_info(struct morse *mors, const u8 *data, int length)
{
	const struct morse_fw_info_tlv *tlv = (const struct morse_fw_info_tlv *)data;

	while ((u8 *)tlv < (data + length)) {
		switch (le16_to_cpu(tlv->type)) {
		case MORSE_FW_INFO_TLV_BCF_ADDR:
			{
				/* Put this in a get_unaligned just in case it's not aligned */
				mors->bcf_address = le32_to_cpu(get_unaligned((u32 *)tlv->val));
				break;
			}
		default:
			/* Just skip unknown types */
			break;
		}
		tlv = (const struct morse_fw_info_tlv *)((u8 *)tlv + le16_to_cpu(tlv->length) +
							 sizeof(*tlv));
	}
}

/**
 * @brief Fill a section header from the buffered ELF
 *
 * @param data Buffer where the ELF resides
 * @param ehdr Header of the ELF File
 * @param shdr Header of the target section
 * @param i Index of the section header table index
 * @return int
 */
static int get_section_header(const u8 *data, Elf32_Ehdr *ehdr, Elf32_Shdr *shdr, int i)
{
	Elf32_Shdr *p = (Elf32_Shdr *)(data + ehdr->e_shoff + (i * ehdr->e_shentsize));

	shdr->sh_name = le32_to_cpu(p->sh_name);
	shdr->sh_type = le32_to_cpu(p->sh_type);
	shdr->sh_offset = le32_to_cpu(p->sh_offset);
	shdr->sh_addr = le32_to_cpu(p->sh_addr);
	shdr->sh_size = le32_to_cpu(p->sh_size);
	shdr->sh_flags = le32_to_cpu(p->sh_flags);

	return 0;
}

static int morse_firmware_load(struct morse *mors, const struct firmware *fw)
{
	int i;
	int ret = 0;
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	Elf32_Shdr shdr;
	Elf32_Shdr sh_strtab;
	const char *sh_strs;

	u8 *fw_buf = devm_kmalloc(mors->dev, ROUND_BYTES_TO_WORD(fw->size), GFP_KERNEL);

	if (!fw_buf)
		return -ENOMEM;

	if (get_file_header(fw->data, &ehdr) != 0) {
		MORSE_ERR(mors, "Wrong file format\n");
		return -1;
	}

	if (get_section_header(fw->data, &ehdr, &sh_strtab, ehdr.e_shstrndx) != 0) {
		MORSE_ERR(mors, "Invalid firmware. Missing string table\n");
		return -1;
	}

	sh_strs = (const char *)fw->data + sh_strtab.sh_offset;

	for (i = 0; i < ehdr.e_phnum; i++) {
		int status;
		int address;

		Elf32_Phdr *p = (Elf32_Phdr *)(fw->data + ehdr.e_phoff + i * ehdr.e_phentsize);

		phdr.p_type = le32_to_cpu(p->p_type);
		phdr.p_offset = le32_to_cpu(p->p_offset);
		phdr.p_paddr = le32_to_cpu(p->p_paddr);
		phdr.p_filesz = le32_to_cpu(p->p_filesz);
		phdr.p_memsz = le32_to_cpu(p->p_memsz);

		/* In current design, the iflash/dflash are only used in self-hosted mode. For
		 * hosted mode, if the sections are found in the combined image, driver
		 * needs to skip them.
		 */
		address = phdr.p_paddr;
		if (address == IFLASH_BASE_ADDR || address == DFLASH_BASE_ADDR)
			continue;

		if (phdr.p_type != PT_LOAD || !phdr.p_memsz)
			continue;

		if (phdr.p_filesz && phdr.p_offset &&
					(phdr.p_offset + phdr.p_filesz) < fw->size) {
			u32 padded_size = ROUND_BYTES_TO_WORD(phdr.p_filesz);

			memcpy(fw_buf, fw->data + phdr.p_offset, padded_size);
			/* Set padding to 0xff */
			memset(fw_buf + phdr.p_filesz, 0xff, padded_size - phdr.p_filesz);
			morse_claim_bus(mors);
			status = morse_dm_write(mors, address, fw_buf, padded_size);
			morse_release_bus(mors);
			if (status) {
				ret = -1;
				break;
			}
		}
	}

	for (i = 0; i < ehdr.e_shnum; i++) {
		if (get_section_header(fw->data, &ehdr, &shdr, i) != 0)
			continue;

		/* This is the firmware info. Parse it */
		if (strncmp(sh_strs + shdr.sh_name, ".fw_info", sizeof(".fw_info")) == 0)
			morse_parse_firmware_info(mors, fw->data + shdr.sh_offset, shdr.sh_size);
	}

	if (ehdr.e_entry != 0) {
		int status;

		MORSE_INFO(mors, "Overwriting boot address to 0x%x\n",
			   ehdr.e_entry);
		morse_claim_bus(mors);
		status = morse_reg32_write(mors, MORSE_REG_BOOT_ADDR(mors),
					   ehdr.e_entry);
		morse_release_bus(mors);
		if (status)
			ret = -1;
	}

	devm_kfree(mors->dev, fw_buf);
	return ret;
}

static int morse_bcf_load(struct morse *mors, const struct firmware *bcf,
			  unsigned int bcf_address)
{
	int i;
	size_t reglen;
	int ret = 0;
	Elf32_Ehdr ehdr;
	Elf32_Shdr shdr;
	Elf32_Shdr sh_strtab;
	const char *sh_strs;
	int status = -1;
	int config_len = -1;
	int regdom_len;
	const char *reg_prefix = ".regdom_";
	u8 *bcf_buf = devm_kmalloc(mors->dev, ROUND_BYTES_TO_WORD(bcf->size), GFP_KERNEL);

	if (!bcf_buf)
		return -ENOMEM;

	if (get_file_header(bcf->data, &ehdr) != 0) {
		MORSE_ERR(mors, "Wrong file format\n");
		return -1;
	}

	if (get_section_header(bcf->data, &ehdr, &sh_strtab, ehdr.e_shstrndx) != 0) {
		MORSE_ERR(mors, "Invalid BCF - missing string table\n");
		return -1;
	}

	reglen = strlen(reg_prefix);

	sh_strs = (const char *)bcf->data + sh_strtab.sh_offset;

	/* Download board config section to firmware */
	for (i = 0; i < ehdr.e_shnum; i++) {
		if (get_section_header(bcf->data, &ehdr, &shdr, i) != 0)
			continue;

		if (strncmp(sh_strs + shdr.sh_name, ".board_config",
							sizeof(".board_config")) == 0) {
			config_len = ROUND_BYTES_TO_WORD(shdr.sh_size);
			MORSE_INFO(mors, "Write BCF board_config to chip - addr %x size %d",
				   bcf_address, config_len);
			if (config_len > BCF_DATABASE_SIZE) {
				MORSE_ERR(mors, "BCF section is too big %u\n", config_len);
				ret = -1;
				break;
			}

			memcpy(bcf_buf, bcf->data + shdr.sh_offset, config_len);
			/* Set padding to 0xff */
			memset(bcf_buf + shdr.sh_size, 0xff, config_len - shdr.sh_size);

			morse_claim_bus(mors);
			status = morse_dm_write(mors, bcf_address, bcf_buf, config_len);
			morse_release_bus(mors);
			if (status) {
				MORSE_ERR(mors, "Failed to write BCF data");
				ret = -1;
			}
			break;
		}
	}

	/* Download regdom section for the configured country to firmware */
	for (; i < ehdr.e_shnum; i++) {
		if (get_section_header(bcf->data, &ehdr, &shdr, i) != 0)
			continue;

		if (strncmp(sh_strs + shdr.sh_name, reg_prefix, reglen) != 0)
			continue;	/* Not a regdom section */

		if (strncmp(sh_strs + shdr.sh_name + 8, mors->country, 2) != 0)
			continue;	/* Not the configured regdom */

		bcf_address += config_len;
		regdom_len = ROUND_BYTES_TO_WORD(shdr.sh_size);
		MORSE_INFO(mors, "Write BCF %s to chip - addr %x size %d",
			   sh_strs + shdr.sh_name, bcf_address, regdom_len);
		if ((config_len + regdom_len) > BCF_DATABASE_SIZE) {
			ret = -1;
			MORSE_ERR(mors, "BCF len (%u + %u) exceeds buffer size %u",
				  config_len, regdom_len, BCF_DATABASE_SIZE);
			break;
		}
		if (regdom_len < 0) {
			MORSE_ERR(mors, "Config invalid: %d", regdom_len);
			ret = -1;
			break;
		}

		memcpy(bcf_buf, bcf->data + shdr.sh_offset, regdom_len);
		memset(bcf_buf + shdr.sh_size, 0xff, regdom_len - shdr.sh_size);

		morse_claim_bus(mors);
		status = morse_dm_write(mors, bcf_address, bcf_buf, regdom_len);
		morse_release_bus(mors);
		if (status) {
			ret = -1;
			MORSE_ERR(mors, "Failed to write regdom data");
			break;
		}
		break;
	}
	if (i >= ehdr.e_shnum) {
		MORSE_ERR(mors, "Country code %s not found in BCF", mors->country);
		devm_kfree(mors->dev, bcf_buf);
		return -1;
	}
	devm_kfree(mors->dev, bcf_buf);
	return ret;
}

static int morse_firmware_reset(struct morse *mors)
{
	morse_claim_bus(mors);

	if (MORSE_REG_RESET(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_RESET(mors), MORSE_REG_RESET_VALUE(mors));
	/* SDIO needs some time after reset */
	if (sdio_reset_time > 0)
		msleep(sdio_reset_time);

	if (MORSE_REG_EARLY_CLK_CTRL_VALUE(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_CLK_CTRL(mors),
				  MORSE_REG_EARLY_CLK_CTRL_VALUE(mors));

	morse_release_bus(mors);
	return 0;
}

static void morse_firmware_clear_aon(struct morse *mors)
{
	int idx;
	u8 count = MORSE_REG_AON_COUNT(mors);
	u32 address = MORSE_REG_AON_ADDR(mors);
	u32 mask = MORSE_REG_AON_LATCH_MASK(mors);
	u32 latch;

	if (address)
		for (idx = 0; idx < count; idx++, address += 4) {
				/* clear AON in case there is any latched sleeps */
				morse_reg32_write(mors, address, 0x0);
		}

	address = MORSE_REG_AON_LATCH_ADDR(mors);
	if (address) {
		/* invoke AON latch procedure */
		morse_reg32_read(mors, address, &latch);
		morse_reg32_write(mors, address, latch & ~(mask));
		mdelay(5);
		morse_reg32_write(mors, address, latch | mask);
		mdelay(5);
		morse_reg32_write(mors, address, latch & ~(mask));
		mdelay(5);
	}
}

static int morse_firmware_trigger(struct morse *mors)
{
	morse_claim_bus(mors);

	/*
	 * If not coming from a full reset, some AON flags may be latched.
	 * Make sure to clear any hanging AON bits (can affect booting).
	 */
	morse_firmware_clear_aon(mors);

	if (MORSE_REG_CLK_CTRL(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_CLK_CTRL(mors), MORSE_REG_CLK_CTRL_VALUE(mors));

	morse_reg32_write(mors, MORSE_REG_MSI(mors), MORSE_REG_MSI_HOST_INT(mors));
	morse_release_bus(mors);
	return 0;
}

static int morse_firmware_magic_verify(struct morse *mors)
{
	int ret = 0;
	__le32 magic = ~MORSE_REG_HOST_MAGIC_VALUE(mors);	/* not the magic value */

	morse_claim_bus(mors);

	morse_reg32_read(mors, mors->cfg->host_table_ptr +
			 offsetof(struct host_table, magic_number), &magic);

	if (le32_to_cpu(magic) != MORSE_REG_HOST_MAGIC_VALUE(mors)) {
		MORSE_ERR(mors, "FW magic mismatch 0x%08x:0x%08x\n",
			  MORSE_REG_HOST_MAGIC_VALUE(mors), magic);
		ret = -EIO;
	}
	morse_release_bus(mors);

	return ret;
}

static int morse_firmware_get_fw_flags(struct morse *mors)
{
	int ret = 0;
	__le32 fw_flags = 0;

	morse_claim_bus(mors);

	ret = morse_reg32_read(mors, mors->cfg->host_table_ptr +
			       offsetof(struct host_table, firmware_flags), &fw_flags);

	mors->firmware_flags = le32_to_cpu(fw_flags);

	morse_release_bus(mors);

	return ret;
}

static int morse_firmware_check_compatibility(struct morse *mors)
{
	int ret = 0;
	u32 fw_version;
	u32 major;
	u32 minor;
	u32 patch;

	morse_claim_bus(mors);

	ret = morse_reg32_read(mors, mors->cfg->host_table_ptr +
			       offsetof(struct host_table, fw_version_number), &fw_version);

	morse_release_bus(mors);

	major = MORSE_SEMVER_GET_MAJOR(fw_version);
	minor = MORSE_SEMVER_GET_MINOR(fw_version);
	patch = MORSE_SEMVER_GET_PATCH(fw_version);

	/* Firmware on device must be recent enough for driver
	 * '+ 1' circumvents a compiler warning for "unsigned 'minor' is never less than zero".
	 */
	if (ret == 0 && (major != MORSE_DRIVER_SEMVER_MAJOR ||
			 (minor + 1) < (MORSE_DRIVER_SEMVER_MINOR + 1))) {
		MORSE_ERR(mors,
			  "Incompatible FW version: (Driver) %d.%d.%d, (Chip) %d.%d.%d\n",
			  MORSE_DRIVER_SEMVER_MAJOR,
			  MORSE_DRIVER_SEMVER_MINOR,
			  MORSE_DRIVER_SEMVER_PATCH, major, minor, patch);
		ret = -EPERM;
	}

	return ret;
}

static int morse_firmware_invalidate_host_ptr(struct morse *mors)
{
	int ret;

	mors->cfg->host_table_ptr = 0;
	morse_claim_bus(mors);
	ret = morse_reg32_write(mors, MORSE_REG_HOST_MANIFEST_PTR(mors), 0);
	morse_release_bus(mors);
	return ret;
}

int morse_firmware_get_host_table_ptr(struct morse *mors)
{
	int ret = 0;
	unsigned long timeout;

	/* Otherwise, wait here (polling) for HT Avail */
	timeout = jiffies + msecs_to_jiffies(1000);
	morse_claim_bus(mors);
	while (1) {
		ret = morse_reg32_read(mors,
				       MORSE_REG_HOST_MANIFEST_PTR(mors),
				       &mors->cfg->host_table_ptr);

		if (mors->cfg->host_table_ptr != 0)
			break;

		if (time_after(jiffies, timeout)) {
			MORSE_ERR(mors, "FW manifest pointer not set\n");
			ret = -EIO;
			break;
		}
		usleep_range(5000, 10000);
	}
	morse_release_bus(mors);
	return ret;
}

static int morse_firmware_read_ext_host_table(struct morse *mors,
					      struct extended_host_table **ext_host_table)
{
	int ret = 0;
	u32 host_tbl_ptr = mors->cfg->host_table_ptr;
	u32 ext_host_tbl_ptr;
	u32 ext_host_tbl_ptr_addr = host_tbl_ptr
	    + offsetof(struct host_table, extended_host_table_addr);
	u32 ext_host_tbl_len;
	u32 ext_host_tbl_len_ptr_addr;
	struct extended_host_table *host_tbl = NULL;

	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, ext_host_tbl_ptr_addr, &ext_host_tbl_ptr);
	if (ret)
		goto exit;

	/* check if this fw populated the extended host table */
	if (ext_host_tbl_ptr == 0)
		goto exit;

	ext_host_tbl_len_ptr_addr = ext_host_tbl_ptr +
	    offsetof(struct extended_host_table, extended_host_table_length);

	/* read the length of the extended host table */
	ret = morse_reg32_read(mors, ext_host_tbl_len_ptr_addr, &ext_host_tbl_len);
	if (ret)
		goto exit;

	/* Round up to the nearest word, as dm reads must be multiples of word size */
	ext_host_tbl_len = ROUND_BYTES_TO_WORD(ext_host_tbl_len);

	host_tbl = kmalloc(ext_host_tbl_len, GFP_KERNEL);
	if (!host_tbl)
		goto exit;

	ret = morse_dm_read(mors, ext_host_tbl_ptr, (u8 *)host_tbl, ext_host_tbl_len);

	morse_release_bus(mors);

	if (ret)
		goto exit;

	*ext_host_table = host_tbl;

	return ret;

exit:
	morse_release_bus(mors);
	kfree(host_tbl);
	MORSE_ERR(mors, "%s failed %d\n", __func__, ret);
	return ret;
}

/**
 * @brief Set the MAC address based on 1) chip config if set, 2) user value or
 *        3) fall back to a value prefixed with the morse OUI.
 *
 *        MAC address can be overridden entirely using `iw wlanX hw ether`.
 *
 * @param fw_mac_addr The mac address read from the fw manifest table
 * @param mors The global morse config object
 */
static void set_mac_addr(struct morse *mors, u8 *fw_mac_addr)
{
	char *token;
	int res;
	int i;
	bool use_user_mac = false;
	bool use_fw_mac = false;
	char *user_mac = &macaddr_suffix[0];
	u8 mac_addr_unset[ETH_ALEN];
	u8 macaddr[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	memset(mac_addr_unset, 0x00, sizeof(mac_addr_unset));

	/* Set the first 3 octets to the Morse Micro OUI */
	memcpy(macaddr, morse_oui, sizeof(morse_oui));

	use_fw_mac = !!memcmp(mac_addr_unset, fw_mac_addr, sizeof(mac_addr_unset));

	use_user_mac = use_fw_mac ? false :
	    !!strncmp(macaddr_suffix, MORSE_RANDOMISE_OCTETS, strlen(macaddr_suffix));

	if (use_user_mac && !use_fw_mac) {
		/* Parse the user MAC suffix */
		for (i = 3; i < ETH_ALEN; i++) {
			token = strsep(&user_mac, ":");
			if (token) {
				if (!kstrtoint(token, 16, &res)) {
					macaddr[i] = (u8)res;
					continue;
				}
			}
			use_user_mac = false;
			break;
		}
	}

	if (!use_user_mac && !use_fw_mac) {
		get_random_bytes((void *)&macaddr[sizeof(macaddr) - 3], 3);
		/* handle the user passing just the last octet */
		if (macaddr_octet != 0xFF) {
			macaddr[sizeof(macaddr) - 1] = macaddr_octet;
			MORSE_INFO(mors,
				   "Last octet set from macaddr_octet, interface MAC is %pM\n",
				   macaddr);
		} else {
			MORSE_INFO(mors,
				   "Randomised last three octets of interface MAC to %pM\n",
				   macaddr);
		}
	} else if (use_fw_mac) {
		memcpy(macaddr, fw_mac_addr, sizeof(macaddr));
	}
	memcpy(&mors->macaddr, &macaddr, sizeof(mors->macaddr));
}

static void update_capabilities_from_ext_host_table(struct morse *mors,
						    struct extended_host_table_capabilities_s1g
						    *caps)
{
	int i;

	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++) {
		mors->capabilities.flags[i] = le32_to_cpu(caps->flags[i]);
		MORSE_INFO(mors, "Firmware Manifest Flags%d: 0x%x", i, le32_to_cpu(caps->flags[i]));
	}
	mors->capabilities.ampdu_mss = caps->ampdu_mss;
	mors->capabilities.morse_mmss_offset = caps->morse_mmss_offset;
	mors->capabilities.beamformee_sts_capability = caps->beamformee_sts_capability;
	mors->capabilities.maximum_ampdu_length_exponent = caps->maximum_ampdu_length;
	mors->capabilities.number_sounding_dimensions = caps->number_sounding_dimensions;

	MORSE_INFO(mors, "\tAMPDU Minimum start spacing: %u\n", caps->ampdu_mss);
	MORSE_INFO(mors, "\tMorse Minimum Start Spacing offset: %u\n", caps->morse_mmss_offset);
	MORSE_INFO(mors, "\tBeamformee STS Capability: %u\n", caps->beamformee_sts_capability);
	MORSE_INFO(mors, "\tNumber of Sounding Dimensions: %u\n", caps->number_sounding_dimensions);
	MORSE_INFO(mors, "\tMaximum AMPDU Length Exponent: %u\n", caps->maximum_ampdu_length);
}

static void update_pager_bypass_tx_status_addr(struct morse *mors,
					       struct extended_host_table_pager_bypass_tx_status
					       *bypass)
{
	mors->chip_if->tx_status_addr_location = le32_to_cpu(bypass->tx_status_buffer_addr);
	MORSE_INFO(mors, "TX Status pager bypass enabled: buffer addr 0x%08x\n",
		   le32_to_cpu(bypass->tx_status_buffer_addr));
}

static void update_validate_skb_checksum(struct morse *mors,
					 struct extended_host_table_insert_skb_checksum
					 *validate_checksum)
{
	mors->chip_if->validate_skb_checksum = validate_checksum->insert_and_validate_checksum;
	MORSE_DBG(mors, "Validate checksum inserted by fw %s\n",
		  validate_checksum->insert_and_validate_checksum ? "enabled" : "disabled");
}

static void update_pager_pkt_memory(struct morse *mors,
	struct extended_host_table_pager_pkt_memory *pkt_memory)
{
	BUG_ON(!mors);
	BUG_ON(!mors->chip_if);

	mors->chip_if->pkt_memory.base_addr = le32_to_cpu(pkt_memory->base_addr);
	mors->chip_if->pkt_memory.page_len = pkt_memory->page_len;
	mors->chip_if->pkt_memory.page_len_reserved = pkt_memory->page_len_reserved;
	mors->chip_if->pkt_memory.num = pkt_memory->num;
}

int morse_firmware_parse_extended_host_table(struct morse *mors)
{
	int ret;
	u8 *head;
	u8 *end;
	struct extended_host_table *ext_host_table = NULL;

	ret = morse_firmware_get_fw_flags(mors);
	if (ret)
		goto exit;

	ret = morse_firmware_read_ext_host_table(mors, &ext_host_table);
	if (ret || !ext_host_table)
		goto exit;

	set_mac_addr(mors, ext_host_table->dev_mac_addr);

	MORSE_INFO(mors, "Firmware Manifest MAC: %pM", ext_host_table->dev_mac_addr);

	/* Parse the TLVs */
	head = ext_host_table->ext_host_table_data_tlvs;
	end = ((u8 *)ext_host_table) + le32_to_cpu(ext_host_table->extended_host_table_length);

	while (head < end) {
		struct extended_host_table_tlv_hdr *hdr =
		    (struct extended_host_table_tlv_hdr *)head;

		switch (le16_to_cpu(hdr->tag)) {
		case MORSE_FW_HOST_TABLE_TAG_S1G_CAPABILITIES:
			update_capabilities_from_ext_host_table(mors,
					(struct extended_host_table_capabilities_s1g *)hdr);
			break;

		case MORSE_FW_HOST_TABLE_TAG_PAGER_BYPASS_TX_STATUS:
			update_pager_bypass_tx_status_addr(mors,
					(struct extended_host_table_pager_bypass_tx_status *)hdr);
			break;

		case MORSE_FW_HOST_TABLE_TAG_INSERT_SKB_CHECKSUM:
			update_validate_skb_checksum(mors,
					(struct extended_host_table_insert_skb_checksum *)hdr);
			break;


		case MORSE_FW_HOST_TABLE_TAG_PAGER_PKT_MEMORY:
			update_pager_pkt_memory(mors,
					(struct extended_host_table_pager_pkt_memory *)hdr);
			break;

		/* add more TLV cases here .. */
		default:
			break;
		}

		head += le16_to_cpu(hdr->length);
		if (hdr->length == 0) {
			MORSE_WARN(mors, "Found a 0 length TLV in the extended host table\n");
			break;
		}
	}

	kfree(ext_host_table);
	return ret;
exit:
	MORSE_ERR(mors, "%s failed %d\n", __func__, ret);
	return ret;
}

/* Caller must kfree() the returned value. */
char *morse_firmware_build_fw_path(struct morse *mors)
{
	const char *fw_variant = "";

	if (is_thin_lmac_mode())
		fw_variant = MORSE_FW_THIN_LMAC_SUFFIX;
	else if (is_virtual_sta_test_mode())
		fw_variant = MORSE_FW_VIRTUAL_STA_SUFFIX;

	return kasprintf(GFP_KERNEL,
			 MORSE_FW_DIR "/%s%s" MORSE_FW_EXT, mors->cfg->fw_base, fw_variant);
}

static int morse_firmware_get_init_params(uint test_mode, struct fw_init_params *init_params)
{
	bool download_fw = true;
	bool get_host_table_ptr = true;
	bool verify_fw = true;

	switch (test_mode) {
	case MORSE_CONFIG_TEST_MODE_DISABLED:
		download_fw = true;
		get_host_table_ptr = true;
		verify_fw = true;
		break;
	case MORSE_CONFIG_TEST_MODE_DOWNLOAD_ONLY:
		download_fw = true;
		get_host_table_ptr = false;
		verify_fw = false;
		break;
	case MORSE_CONFIG_TEST_MODE_DOWNLOAD_AND_GET_HOST_TBL_PTR:
		download_fw = true;
		get_host_table_ptr = true;
		verify_fw = false;
		break;
	case MORSE_CONFIG_TEST_MODE_GET_HOST_TBL_PTR_ONLY:
		download_fw = false;
		get_host_table_ptr = true;
		verify_fw = false;
		break;
	case MORSE_CONFIG_TEST_MODE_RESET:
		download_fw = false;
		get_host_table_ptr = false;
		verify_fw = false;
		break;
	case MORSE_CONFIG_TEST_MODE_BUS:
		download_fw = false;
		get_host_table_ptr = false;
		verify_fw = false;
		break;
	default:
		/* Not a valid case */
		return -EINVAL;
	}

	init_params->download_fw = download_fw;
	init_params->get_host_table_ptr = get_host_table_ptr;
	init_params->verify_fw = verify_fw;

	return 0;
}

static int morse_firmware_init_preloaded(struct morse *mors,
					 const struct firmware *fw,
					 const struct firmware *bcf,
					 enum morse_config_test_mode test_mode)
{
	int ret = 0;
	int retries = 3;
	struct fw_init_params init_params;

	ret = morse_firmware_get_init_params(test_mode, &init_params);
	if (ret)
		goto exit;

	while (retries--) {

			ret = morse_firmware_reset(mors);

			/* Perform pre load chip preparation */
			if (mors->cfg->pre_load_prepare)
				ret = ret ? ret : mors->cfg->pre_load_prepare(mors);


		if (init_params.download_fw) {
			ret = ret ? ret : morse_firmware_invalidate_host_ptr(mors);
			ret = ret ? ret : morse_firmware_load(mors, fw);
			ret = ret ? ret : morse_bcf_load(mors, bcf, mors->bcf_address);
			ret = ret ? ret : morse_firmware_trigger(mors);
		}
		if (init_params.get_host_table_ptr)
			ret = ret ? ret : morse_firmware_get_host_table_ptr(mors);
		if (init_params.verify_fw) {
			ret = ret ? ret : morse_firmware_magic_verify(mors);
			ret = ret ? ret : morse_firmware_check_compatibility(mors);
		}
		if (!ret)
			break;
	}

exit:
	return ret;
}

int morse_firmware_init(struct morse *mors, enum morse_config_test_mode test_mode)
{
	int n;
	int ret = 0;
	char *fw_path = NULL;
	const char *fw_name;
	char bcf_path[MAX_BCF_NAME_LEN];
	const char *bcf_name = bcf_path;
	const struct firmware *fw = NULL;
	const struct firmware *bcf = NULL;
	int board_id = 0;
	char *p;
	bool use_full_path = true;

#ifdef CONFIG_ANDROID
	/* Use filenames only - Android sets the path */
	use_full_path = false;
#endif


	fw_path = morse_firmware_build_fw_path(mors);
	if (!fw_path) {
		ret = -ENOMEM;
		goto exit;
	}
	fw_name = fw_path;

	if (mors->cfg->get_board_type)
		board_id = mors->cfg->get_board_type(mors);

	if (strlen(board_config_file) > 0) {
		n = snprintf(bcf_path, sizeof(bcf_path), "%s/%s", MORSE_FW_DIR, board_config_file);
	} else if (strlen(mors->board_serial) > 0) {
		if (memcmp(mors->board_serial, "default", sizeof("default")) == 0 &&
		    (board_id > 0 && board_id < mors->cfg->board_type_max_value)) {
			/* Use board ID read from chip if it's non-zero and the board serial is
			 * default.
			 */
			n = snprintf(bcf_path, sizeof(bcf_path),
				     "%s/bcf_boardtype_%02d.bin", MORSE_FW_DIR, board_id);
		} else {
			/* fallback to the old style */
			n = snprintf(bcf_path, sizeof(bcf_path),
				     "%s/bcf_%s.bin", MORSE_FW_DIR, mors->board_serial);
		}
	} else {
		MORSE_ERR(mors, "%s: BCF or Serial parameters are not defined\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (n < 0 || n > (sizeof(bcf_path) - 1)) {
		MORSE_ERR(mors, "%s: Failed to create a BCF path\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (!use_full_path) {
		p = strrchr(fw_name, '/');
		if (p)
			fw_name = p + 1;

		p = strrchr(bcf_name, '/');
		if (p)
			bcf_name = p + 1;
	}

	MORSE_INFO(mors, "Loading firmware from %s", fw_name);
	ret = request_firmware(&fw, fw_name, mors->dev);
	if (ret != 0)
		goto exit;

	MORSE_INFO(mors, "Loading BCF from %s", bcf_name);
	ret = request_firmware(&bcf, bcf_name, mors->dev);
	if (ret != 0)
		goto exit;

	/* Clear out extra ACK timeout, its value is unknown */
	mors->extra_ack_timeout_us = -1;

	ret = morse_firmware_init_preloaded(mors, fw, bcf, test_mode);

exit:
	release_firmware(fw);
	release_firmware(bcf);

	kfree(fw_path);

	if (ret)
		MORSE_ERR(mors, "%s failed: %d\n", __func__, ret);
	else
		MORSE_INFO(mors, "Firmware initialized\n");

	return ret;
}

int morse_firmware_exec_ndr(struct morse *mors)
{
	int ret = 0;

		ret = morse_firmware_reset(mors);
	if (ret)
		MORSE_ERR(mors, "%s: Failed to reset: %d\n", __func__, ret);
	ret = morse_firmware_init(mors, test_mode);
	if (ret)
		MORSE_ERR(mors, "%s: Failed to reload: %d\n", __func__, ret);
	ret = mors->cfg->ops->init(mors);
	if (ret)
		MORSE_ERR(mors, "%s: chip_if_init failed: %d\n", __func__, ret);
	ret = morse_firmware_parse_extended_host_table(mors);
	if (ret)
		MORSE_ERR(mors, "failed to parse extended host table: %d\n", ret);

	return ret;
}
