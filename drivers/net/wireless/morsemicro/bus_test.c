/*
 * Copyright 2017-2023 Morse Micro
 *
 */
#include "morse.h"
#include "bus.h"
#include "debug.h"

#ifdef CONFIG_MORSE_ENABLE_TEST_MODES

#define BUS_TEST_MAX_BLOCK_SIZE	(64 * 1024)
#define	BUS_TEST_SIZE_LIST	{512, 560, 2048, 2048 + 128, \
				4 * 1024, 4 * 1024 + 3 * 128, \
				6 * 1024 + 508, 16 * 1024, \
				32 * 1024, BUS_TEST_MAX_BLOCK_SIZE}

/* Since different chips have different address spaces,
 * it is up to the developer to define a list of addresses
 * to be tested
 * Default configuration is the DMEM start address.
 */
#define BUS_TEST_READ_WRITE_ADDRESS_LIST {mors->cfg->regs->pager_base_address,}

static int morse_bus_write_read_compare(struct morse *mors, int size, u8 value, u32 address)
{
	int ret;
	u8 *write_buff, *read_buff;

	write_buff = kmalloc(size, GFP_KERNEL);
	read_buff = kmalloc(size, GFP_KERNEL);

	memset(write_buff, value, size);
	ret = morse_dm_write(mors, address, write_buff, size);
	MORSE_INFO(mors, "%s: Writing %d bytes (0x%02X) to 0x%08X %s\n", __func__,
		   size, value, address, ret < 0 ? "FAILED" : "PASSED");
	if (ret < 0)
		goto exit;
	memset(read_buff, ~value, size);
	ret = morse_dm_read(mors, address, read_buff, size);
	MORSE_INFO(mors, "%s: Reading %d bytes from 0x%08X %s\n", __func__,
		   size, address, ret < 0 ? "FAILED" : "PASSED");
	if (ret < 0)
		goto exit;

	ret = memcmp(write_buff, read_buff, size);

	MORSE_INFO(mors, "%s: Verifying %d bytes %s\n", __func__,
		   size, ret != 0 ? "FAILED" : "PASSED");
	ret = ret ? -EPROTO : ret;

exit:
	kfree(write_buff);
	kfree(read_buff);
	return ret;
}

int morse_bus_test(struct morse *mors, const char *bus_name)
{
	int size_idx, ret;
	u32 chip_id;
	u32 cmp_size_list[] = BUS_TEST_SIZE_LIST;
	u32 address_list[] = BUS_TEST_READ_WRITE_ADDRESS_LIST;

	MORSE_INFO(mors, "---==[ START %s BUS TEST ]==---\n", bus_name);
	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	if (!morse_hw_is_valid_chip_id(chip_id, mors->cfg->valid_chip_ids)) {
		MORSE_ERR(mors, "%s ChipId=0x%x) is not valid.\n", __func__, mors->chip_id);
		ret = -1;
		goto exit;
	}
	MORSE_INFO(mors, "%s: Reading Chip ID 0x%04X: %s\n",
		   __func__, chip_id, ret < 0 ? "FAILED" : "PASSED");

	if (ret < 0)
		goto exit;

	for (size_idx = 0; size_idx < ARRAY_SIZE(cmp_size_list); size_idx++) {
		int cmp_size = cmp_size_list[size_idx];
		int address_idx;

		for (address_idx = 0; address_idx < ARRAY_SIZE(address_list); address_idx++) {
			MORSE_INFO(mors, "%s: Writing, Reading and verifying:\n", __func__);
			ret =
			    morse_bus_write_read_compare(mors, cmp_size, 0xAA,
							 address_list[address_idx]);
			if (ret)
				goto exit;

			MORSE_INFO(mors, "%s: Clearing, Reading and verifying:\n", __func__);
			ret =
			    morse_bus_write_read_compare(mors, cmp_size, 0,
							 address_list[address_idx]);
			if (ret)
				goto exit;
		}
	}

	ret = morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	MORSE_INFO(mors, "%s: Final Reading Chip ID %s\n", __func__, ret < 0 ? "FAILED" : "PASSED");

	if (ret < 0)
		goto exit;

exit:
	morse_release_bus(mors);
	MORSE_INFO(mors, "---==[ %s BUS TEST %s ]==---\n", bus_name, ret ? "FAILED" : "PASSED");
	return ret;
}
#endif
