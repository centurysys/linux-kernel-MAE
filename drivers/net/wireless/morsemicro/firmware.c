/*
 * Copyright 2017-2022 Morse Micro
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
#include "vendor.h"


/* The last mac address octet value */
u8 macaddr_octet = 0xFF;
module_param(macaddr_octet, byte, 0644);
MODULE_PARM_DESC(macaddr_octet,
		"MACaddr octet 6. 0xFF randomises the value. Ignored if fw MACaddr present");

/* When setting the mac address, the special value 00 will randomise the last 3 octets */
#define MORSE_RANDOMISE_OCTETS "00:00:00"

/* The last 3 mac address octet values */
static char macaddr_suffix[9] = "00:00:00";
module_param_string(macaddr_suffix,
		    macaddr_suffix,
		    ARRAY_SIZE(macaddr_suffix),
		    0644);
MODULE_PARM_DESC(macaddr_suffix,
		 "MACaddr octets 4, 5 and 6. 00:00:00 (default) randomises the value, ignored if fw MACaddr present");

static int sdio_reset_time = CONFIG_MORSE_SDIO_RESET_TIME;
module_param(sdio_reset_time, int, 0644);
MODULE_PARM_DESC(sdio_reset_time, "Time to wait (in msec) after SDIO reset");

static int get_file_header(const uint8_t *data, Elf32_Ehdr *ehdr)
{
	Elf32_Ehdr *p = (Elf32_Ehdr *)data;

	// Magic check
	if ((p->e_ident[EI_MAG0] != ELFMAG0) ||
		(p->e_ident[EI_MAG1] != ELFMAG1) ||
		(p->e_ident[EI_MAG2] != ELFMAG2) ||
		(p->e_ident[EI_MAG3] != ELFMAG3))
		return -1;

	/* elf32 and little endian */
	if ((p->e_ident[EI_DATA]  != ELFDATA2LSB) ||
		(p->e_ident[EI_CLASS] != ELFCLASS32))
		return -1;

	ehdr->e_phoff	 = le32_to_cpu(p->e_phoff);
	ehdr->e_phentsize = le16_to_cpu(p->e_phentsize);
	ehdr->e_phnum	 = le16_to_cpu(p->e_phnum);
	ehdr->e_shoff = le32_to_cpu(p->e_shoff);
	ehdr->e_shentsize = le16_to_cpu(p->e_shentsize);
	ehdr->e_shnum	 = le16_to_cpu(p->e_shnum);
	ehdr->e_shstrndx = le16_to_cpu(p->e_shstrndx);

	return 0;
}

static void morse_parse_firmware_info(struct morse *mors, const u8 *data,
				      int length)
{
	const struct morse_fw_info_tlv *tlv = (const struct morse_fw_info_tlv *)data;

	while ((u8 *)tlv < (data + length)) {
		switch (le16_to_cpu(tlv->type)) {
		case MORSE_FW_INFO_TLV_BCF_ADDR:
		{
			/* Put this in a get_unaligned just incase it's not aligned */
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
static int get_section_header(const uint8_t *data, Elf32_Ehdr *ehdr, Elf32_Shdr *shdr, int i)
{
	Elf32_Shdr *p = (Elf32_Shdr *)(data + ehdr->e_shoff +
				       (i * ehdr->e_shentsize));

	shdr->sh_name = le32_to_cpu(p->sh_name);
	shdr->sh_type = le32_to_cpu(p->sh_type);
	shdr->sh_offset = le32_to_cpu(p->sh_offset);
	shdr->sh_addr = le32_to_cpu(p->sh_addr);
	shdr->sh_size = le32_to_cpu(p->sh_size);
	shdr->sh_flags = le32_to_cpu(p->sh_flags);

	return 0;
}

static int morse_firmware_load(struct morse *mors, const struct firmware *fw, u8 *buf)
{
	int i;
	int ret = 0;
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	Elf32_Shdr shdr;
	Elf32_Shdr sh_strtab;
	const char *sh_strs;

	if (get_file_header(fw->data, &ehdr) != 0) {
		morse_err(mors, "Wrong file format\n");
		return -1;
	}

	if (get_section_header(fw->data, &ehdr, &sh_strtab, ehdr.e_shstrndx) != 0) {
		morse_err(mors, "Invalid firmware. Missing string table\n");
		return -1;
	}

	sh_strs = (const char *)fw->data + sh_strtab.sh_offset;

	for (i = 0; i < ehdr.e_phnum; i++) {
		int status;
		int address;

		Elf32_Phdr *p = (Elf32_Phdr *)(buf + ehdr.e_phoff + i * ehdr.e_phentsize);

		phdr.p_type = le32_to_cpu(p->p_type);
		phdr.p_offset = le32_to_cpu(p->p_offset);
		phdr.p_paddr = le32_to_cpu(p->p_paddr);
		phdr.p_filesz = le32_to_cpu(p->p_filesz);
		phdr.p_memsz = le32_to_cpu(p->p_memsz);

		/* In current design, the iflash/dflash are only used
		 * in self-hosted mode. For hosted mode, if the
		 * sections are found in the combined image, driver
		 * needs to skip them.
		 */
		address = phdr.p_paddr;
		if ((address == IFLASH_BASE_ADDR) || (address == DFLASH_BASE_ADDR))
			continue;

		if ((phdr.p_type != PT_LOAD) || (!phdr.p_memsz))
			continue;

		if (phdr.p_filesz &&
				phdr.p_offset &&
				((phdr.p_offset + phdr.p_filesz) < fw->size)) {
			morse_claim_bus(mors);
			status = morse_dm_write(mors, address, buf + phdr.p_offset,
							ROUND_BYTES_TO_WORD(phdr.p_filesz));
			morse_release_bus(mors);
			if (status) {
				ret = -1;
				break;
			}
		}
	}

	for (i = 0; i < ehdr.e_shnum; i++) {
		if (get_section_header(buf, &ehdr, &shdr, i) != 0)
			continue;

		/* This is the firmware info. Parse it */
		if (strncmp(sh_strs + shdr.sh_name, ".fw_info", sizeof(".fw_info")) == 0)
			morse_parse_firmware_info(mors, buf + shdr.sh_offset,
						  shdr.sh_size);
	}

	return ret;
}

static int morse_bcf_load(struct morse *mors, const struct firmware *bcf,
				   unsigned int bcf_address, u8 *buf)
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

	if (get_file_header(bcf->data, &ehdr) != 0) {
		morse_err(mors, "Wrong file format\n");
		return -1;
	}

	if (get_section_header(bcf->data, &ehdr, &sh_strtab, ehdr.e_shstrndx) != 0) {
		morse_err(mors, "Invalid BCF - missing string table\n");
		return -1;
	}

	reglen = strlen(reg_prefix);

	sh_strs = (const char *)bcf->data + sh_strtab.sh_offset;

	/* Download board config section to firmware */
	for (i = 0; i < ehdr.e_shnum; i++) {
		if (get_section_header(buf, &ehdr, &shdr, i) != 0)
			continue;

		if (strncmp(sh_strs + shdr.sh_name, ".board_config",
				sizeof(".board_config")) == 0) {
			config_len = ROUND_BYTES_TO_WORD(shdr.sh_size);
			morse_info(mors, "Write BCF board_config to chip - addr %x size %d",
					bcf_address, config_len);
			morse_claim_bus(mors);
			status = morse_dm_write(mors, bcf_address, buf + shdr.sh_offset,
						config_len);
			morse_release_bus(mors);
			if (status) {
				ret = -1;
				morse_err(mors, "Failed to write BCF data");
			}
			break;
		}
	}

	/* Download regdom section for the configured country to firmware */
	for (; i < ehdr.e_shnum; i++) {
		if (get_section_header(buf, &ehdr, &shdr, i) != 0)
			continue;

		if (strncmp(sh_strs + shdr.sh_name, reg_prefix, reglen) != 0)
			continue;	/* Not a regdom section */

		if (strncmp(sh_strs + shdr.sh_name + 8, mors->country, 2) !=  0)
			continue;	/* Not the configured regdom */

		bcf_address += config_len;
		regdom_len = ROUND_BYTES_TO_WORD(shdr.sh_size);
		morse_info(mors, "Write BCF %s to chip - addr %x size %d",
			sh_strs + shdr.sh_name, bcf_address, regdom_len);
		if ((config_len + regdom_len) > BCF_DATABASE_SIZE) {
			ret = -1;
			morse_err(mors, "BCF len (%u + %u) exceeds buffer size %u",
				config_len, regdom_len, BCF_DATABASE_SIZE);
			break;
		}
		if (regdom_len < 0) {
			morse_err(mors, "Config invalid: %d", regdom_len);
			ret = -1;
			break;
		}
		morse_claim_bus(mors);
		status = morse_dm_write(mors, bcf_address, buf + shdr.sh_offset,
						regdom_len);
		morse_release_bus(mors);
		if (status) {
			ret = -1;
			morse_err(mors, "Failed to write regdom data");
			break;
		}
		break;
	}
	if (i >= ehdr.e_shnum) {
		morse_err(mors, "Country code %s not found in BCF", mors->country);
		return -1;
	}

	return ret;
}

static int morse_firmware_reset(struct morse *mors)
{
	morse_claim_bus(mors);

	if (MORSE_REG_RESET(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_RESET(mors),
				MORSE_REG_RESET_VALUE(mors));
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
		for (idx = 0; idx < count; idx++, address += 4)
			/* clear AON in case there is any latched sleeps */
			morse_reg32_write(mors, address, 0x0);

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

	if (MORSE_REG_MAC_BOOT_ADDR(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_MAC_BOOT_ADDR(mors),
				  MORSE_REG_MAC_BOOT_ADDR_VALUE(mors));

	if (MORSE_REG_CLK_CTRL(mors) != 0)
		morse_reg32_write(mors, MORSE_REG_CLK_CTRL(mors),
				  MORSE_REG_CLK_CTRL_VALUE(mors));

	morse_reg32_write(mors, MORSE_REG_MSI(mors),
					MORSE_REG_MSI_HOST_INT(mors));
	morse_release_bus(mors);
	return 0;
}

static int morse_firmware_magic_verify(struct morse *mors)
{
	int ret = 0;
	u32 magic = ~MORSE_REG_HOST_MAGIC_VALUE(mors);  /* not the magic value */

	morse_claim_bus(mors);

	morse_reg32_read(mors, mors->cfg->host_table_ptr +
		offsetof(struct host_table, magic_number), &magic);

	if (magic != MORSE_REG_HOST_MAGIC_VALUE(mors)) {
		morse_err(mors, "FW magic mismatch 0x%08x:0x%08x\n",
			MORSE_REG_HOST_MAGIC_VALUE(mors), magic);
		ret = -EIO;
	}
	morse_release_bus(mors);

	return ret;
}

int morse_firmware_check_compatability(struct morse *mors)
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

	/* Firmware on device must be recent enough for driver */
	if (ret == 0 && (major != MORSE_DRIVER_SEMVER_MAJOR ||
			minor < MORSE_DRIVER_SEMVER_MINOR)) {

		morse_err(mors,
			"Incompatible FW version: (Driver) %d.%d.%d, (Chip) %d.%d.%d\n",
			MORSE_DRIVER_SEMVER_MAJOR,
			MORSE_DRIVER_SEMVER_MINOR,
			MORSE_DRIVER_SEMVER_PATCH,
			major, minor, patch);
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
			morse_err(mors, "FW manifest pointer not set\n");
			ret = -EIO;
			break;
		}
		usleep_range(5000, 10000);
	}
	morse_release_bus(mors);
	return ret;
}

int morse_firmware_read_ext_host_table(struct morse *mors, struct extended_host_table *ext_host_table)
{
	int ret = 0;
	u32 host_tbl_ptr = mors->cfg->host_table_ptr;
	u32 ext_host_tbl_ptr;
	u32 ext_host_tbl_ptr_addr = host_tbl_ptr
				    + offsetof(struct host_table, extended_host_table_addr);
	size_t ext_host_tbl_size = sizeof(struct extended_host_table);

	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, ext_host_tbl_ptr_addr, &ext_host_tbl_ptr);
	if (ret)
		goto exit;
	/* check if this fw populated the extended host table */
	if (ext_host_tbl_ptr == 0)
		goto exit;
	ret = morse_dm_read(mors, ext_host_tbl_ptr, (u8 *)ext_host_table, ext_host_tbl_size);
	morse_release_bus(mors);

	if (ret)
		goto exit;

	return ret;

exit:
	morse_release_bus(mors);
	morse_err(mors, "%s failed %d\n", __func__, ret);
	return ret;
}

/**
 * @brief Set the mac addr based on 1) chip config if set, 2) user value
 *        or 3) fall back to a randomised value prefixed with
 *        the morse OUI.
 *        MAC address can be overriden entirely using `iw wlanX hw ether`
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
	u8 macaddr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	memset(mac_addr_unset, 0x00, sizeof(mac_addr_unset));

	/* Set the first 3 octets to the Morse Micro OUI */
	memcpy(macaddr, morse_oui, sizeof(morse_oui));

	use_fw_mac = !!memcmp(mac_addr_unset, fw_mac_addr, sizeof(mac_addr_unset));

	use_user_mac = use_fw_mac ? false :
				    !!strncmp(macaddr_suffix, MORSE_RANDOMISE_OCTETS,
				      strlen(macaddr_suffix));

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
			morse_info(mors,
				  "Last octet set from macaddr_octet, interface MAC is %pM\n",
				  macaddr);
		} else {
			morse_info(mors,
				  "Randomised last three octets of interface MAC to %pM\n",
				  macaddr);
		}
	} else if (use_fw_mac) {
		memcpy(macaddr, fw_mac_addr, sizeof(macaddr));
	}
	memcpy(&mors->macaddr, &macaddr, sizeof(mors->macaddr));
}

/**
 * morse_firmware_parse_host_table - Read the firmware's host table for device capabilities
 *
 * @mors: morse struct to which chip belongs
 *
 * Return: 0 if the table was parsed successfully, error if otherwise.
 */
static int morse_firmware_parse_host_table(struct morse *mors)
{
	int ret;
	int i;
	struct extended_host_table local_ext_host_tbl;

	ret = morse_firmware_read_ext_host_table(mors, &local_ext_host_tbl);

	if (ret)
		goto exit;

	/* update the chip object */
	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
		mors->capabilities.flags[i] = le32_to_cpu(local_ext_host_tbl.s1g_caps.flags[i]);
	mors->capabilities.ampdu_mss = local_ext_host_tbl.s1g_caps.ampdu_mss;
	mors->capabilities.beamformee_sts_capability = local_ext_host_tbl.s1g_caps.beamformee_sts_capability;
	mors->capabilities.maximum_ampdu_length_exponent = local_ext_host_tbl.s1g_caps.maximum_ampdu_length;
	mors->capabilities.number_sounding_dimensions = local_ext_host_tbl.s1g_caps.number_sounding_dimensions;

	set_mac_addr(mors, local_ext_host_tbl.dev_mac_addr);

	morse_info(mors, "Firmware Manifest MAC: %pM", local_ext_host_tbl.dev_mac_addr);
	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
		morse_info(mors, "Firmware Manifest Flags%d: 0x%x", i,
			   le32_to_cpu(local_ext_host_tbl.s1g_caps.flags[i]));

	return ret;

exit:
	morse_err(mors, "%s failed %d\n", __func__, ret);
	return ret;
}

static int morse_firmware_init_preloaded(struct morse *mors,
			const struct firmware *fw, const struct firmware *bcf,
			bool dl_firmware, bool chk_firmware, u8 *fw_buf, u8 *bcf_buf)
{
	int ret = 0;
	int retries = 3;

	while (retries--) {
		ret = morse_firmware_reset(mors);
		if (dl_firmware) {
			ret = ret ? ret : morse_firmware_invalidate_host_ptr(mors);
			ret = ret ? ret : morse_firmware_load(mors, fw, fw_buf);
			ret = ret ? ret : morse_bcf_load(mors, bcf, mors->bcf_address, bcf_buf);
			ret = ret ? ret : morse_firmware_trigger(mors);
			ret = ret ? ret : morse_firmware_get_host_table_ptr(mors);
		}
		if (chk_firmware) {
			ret = ret ? ret : morse_firmware_magic_verify(mors);
			ret = ret ? ret : morse_firmware_check_compatability(mors);
			ret = ret ? ret : morse_firmware_parse_host_table(mors);
		}
		if (!ret)
			break;
	}

	return ret;
}

int morse_firmware_init(struct morse *mors,
			const char *fw_name,
			bool dl_firmware, bool chk_firmware)
{
	int n;
	int ret = 0;
	char bcf_path[MAX_BCF_NAME_LEN];
	const struct firmware *fw = NULL;
	const struct firmware *bcf = NULL;
	u8 *fw_buf = NULL;
	u8 *bcf_buf = NULL;
	int board_id = 0;

	if (!fw_name) {
		morse_err(mors,
			"Couldn't find matching firmware for chip ID: 0x%08x\n",
			mors->chip_id);
		ret = -ENXIO;
		goto exit;
	}

	if (mors->cfg->get_board_type != NULL)
		board_id = mors->cfg->get_board_type(mors);

	if (strlen(board_config_file) > 0)
		n = snprintf(bcf_path, sizeof(bcf_path),
					"%s/%s", MORSE_FW_DIR,
					board_config_file);
	else if (strlen(mors->board_serial) > 0) {
		if (memcmp(mors->board_serial, "default", sizeof("default")) == 0 &&
				(board_id > 0 && board_id < mors->cfg->board_type_max_value)) {
			/* Use board ID read from chip if it's non-zero and the board serial is default */
			n = snprintf(bcf_path, sizeof(bcf_path),
					"%s/bcf_boardtype_%02d.bin", MORSE_FW_DIR,
					board_id);
		} else {
			/* fallback to the old style */
			n = snprintf(bcf_path, sizeof(bcf_path),
					"%s/bcf_%s.bin", MORSE_FW_DIR,
					mors->board_serial);
		}
	} else {
		morse_err(mors, "%s: BCF or Serial parameters are not defined\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (n < 0 || n > (sizeof(bcf_path) - 1)) {
		morse_err(mors, "%s: Failed to create a BCF path\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	fw_buf = kmalloc(MORSE_FW_MAX_SIZE, GFP_KERNEL);
	bcf_buf = kmalloc(MORSE_BCF_MAX_SIZE, GFP_KERNEL);
	if (fw_buf == NULL || bcf_buf == NULL)
		goto exit;

#if KERNEL_VERSION(4, 4, 94) < LINUX_VERSION_CODE
	ret = ret ? ret : request_firmware_into_buf(&fw, fw_name, mors->dev, fw_buf,
							MORSE_FW_MAX_SIZE);
	morse_info(mors, "Loading BCF from %s", bcf_path);
	ret = ret ? ret : request_firmware_into_buf(&bcf, bcf_path, mors->dev, bcf_buf,
							MORSE_BCF_MAX_SIZE);
#else
	ret = ret ? ret : request_firmware(&fw, fw_name, mors->dev);
	morse_info(mors, "Loading BCF from %s", bcf_path);
	ret = ret ? ret : request_firmware(&bcf, bcf_path, mors->dev);
	if (ret == 0) {
		memcpy(fw_buf, fw->data, ROUND_BYTES_TO_WORD(fw->size));
		memcpy(bcf_buf, bcf->data, ROUND_BYTES_TO_WORD(bcf->size));
	}
#endif

	ret = ret ? ret : morse_firmware_init_preloaded(mors, fw, bcf, dl_firmware, chk_firmware,
						fw_buf, bcf_buf);

	release_firmware(fw);
	release_firmware(bcf);

exit:
	kfree(fw_buf);
	kfree(bcf_buf);

	return ret;
}

int morse_firmware_exec_ndr(struct morse *mors)
{
	int ret = 0;

	ret = morse_firmware_reset(mors);
	if (ret)
		morse_err(mors, "%s: Failed to reset: %d\n", __func__, ret);
	ret = morse_firmware_init(mors, mors->cfg->fw_name, true, true);
	if (ret)
		morse_err(mors, "%s: Failed to reload: %d\n", __func__, ret);
	ret = mors->cfg->ops->init(mors);
	if (ret)
		morse_err(mors, "%s: chip_if_init failed: %d\n", __func__, ret);
	return ret;
}
