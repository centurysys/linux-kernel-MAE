/*
 * Copyright 2025 Morse Micro
 *
 */

#include "morse.h"
#include "mem_access.h"
#include "bus.h"
#include "hw.h"
#include "debug.h"

#define MEM_ACCESS_RESPONSE_TIMEOUT_MS	(5)

#define MEM_ACCESS_STATE_DISABLED		(0)
#define MEM_ACCESS_STATE_RESTRICTED		(0x494E4954)
#define MEM_ACCESS_STATE_RESTRICTION_OVERRIDDEN	(~MEM_ACCESS_STATE_RESTRICTED)

#define MEM_ACCESS_REQUEST_STARTED		(0xFEEDDA7A)
#define MEM_ACCESS_REQUEST_GRANTED_BURST_MODE	(0xACCE55B)
#define MEM_ACCESS_REQUEST_GRANTED		(0xACCE55)
#define MEM_ACCESS_REQUEST_DENIED		(0xBAADBAAD)

/**
 * mem_access_cache_store - store the memory access request address in the cache
 *
 * @mors: Global morse struct
 * @start_addr: Memory access request start address to be stored
 * @end_addr: Memory access request end address to be stored
 */
static void mem_access_cache_store(struct morse *mors, u32 start_addr, u32 end_addr)
{
	mors->mem_access.configured_region.start = start_addr;
	mors->mem_access.configured_region.end = end_addr;
}

/**
 * mem_access_is_within_region - check memory access request is within a region
 *
 * @mors: Global morse struct
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: true if within the given region, false if not
 */
static bool mem_access_is_within_region(u32 address, int len, struct morse_hw_memory region)
{
	if (WARN_ON(len < 0) ||
	    WARN_ON(address > U32_MAX - len))
		return false;

	if (address < region.start ||
	    address >= region.end)
		return false;

	if ((address + len) > region.end)
		return false;

	return true;
}

/**
 * mem_access_is_within_fixed_regions - check memory access request is within the fix regions
 *
 * @mors: Global morse struct
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: true if within fixed regions, false if not
 */
static bool mem_access_is_within_fixed_regions(struct morse *mors, u32 address, int len)
{
	const struct mem_access_cfg *cfg = mors->cfg->mem_access_cfg;
	int i;

	for (i = 0; i < ARRAY_SIZE(cfg->fixed_regions); i++) {
		if (!mem_access_is_within_region(address, len, cfg->fixed_regions[i]))
			continue;

		/* Memory is within fixed regions.
		 * If writing to digital reset, cache and restriction enforcement should be reset
		 */
		if (address == MORSE_REG_RESET(mors)) {
			mors->mem_access.restriction_is_enforced = false;
			mem_access_reset_cache(mors);
			MORSE_DBG(mors, "Reset detected, resetting the mem access");
		}

		return true;
	}

	return false;
}

/**
 * mem_access_is_cached - check memory access request is cached
 *
 * @mors: Global morse struct
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: true if cached, false if not
 */
static bool mem_access_is_cached(struct morse *mors, u32 address, int len)
{
	return mem_access_is_within_region(address, len, mors->mem_access.configured_region);
}

/**
 * mem_access_request_is_expandable - check memory access request can be expanded
 *
 * @mors: Global morse struct
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: true if request can be expanded, false if not
 */
static bool mem_access_request_is_expandable(struct morse *mors, u32 address, int len)
{
	const struct mem_access_cfg *cfg = mors->cfg->mem_access_cfg;

	return mem_access_is_within_region(address, len, cfg->expand_cache_access);
}

/**
 * mem_access_send_request - send memory access request to the chip
 *
 * @mors: Global morse struct
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: 0 if success, otherwise error code
 */
static int mem_access_send_request(struct morse *mors, u32 address, int len)
{
	int ret;
	const u32 status = MEM_ACCESS_REQUEST_STARTED;

	ret = mors->bus_ops->reg32_write(mors, MORSE_REG_MEM_ACCESS_STATUS(mors), status);
	if (ret) {
		MORSE_WARN(mors, "Failed to set access request status: %d\n", ret);
		return ret;
	}

	ret = mors->bus_ops->reg32_write(mors, MORSE_REG_MEM_ACCESS_ADDR(mors), address);
	if (ret) {
		MORSE_WARN(mors, "Failed to set access request address: %d\n", ret);
		return ret;
	}

	ret = mors->bus_ops->reg32_write(mors, MORSE_REG_MEM_ACCESS_SIZE(mors), len);
	if (ret) {
		MORSE_WARN(mors, "Failed to set access request size: %d\n", ret);
		return ret;
	}

	/* Trigger the processing of the request */
	ret = mors->bus_ops->reg32_write(mors,
					 MORSE_MEM_ACCESS_TRGR_SET(mors),
					 MORSE_MEM_ACCESS_IRQ_BIT);
	if (ret) {
		MORSE_WARN(mors, "Failed to set the request trigger: %d\n", ret);
		return ret;
	}

	MORSE_DBG(mors,
		  "Triggered access request: Status: 0x%08X Address: 0x%08X Length: 0x%08X\n",
		  status,
		  address,
		  len);

	return ret;
}

/**
 * mem_access_wait_for_response - wait for memory access response to be set from the chip
 *
 * @mors: Global morse struct
 * @access_grant_cb: Optional callback that will be called upon access grant
 *
 * Return: 0 if memory access request was granted, otherwise error code
 */
static int mem_access_wait_for_response
	(struct morse *mors, void (*access_grant_cb)(struct morse *mors, bool burst_enabled))
{
	int ret = 0;
	u32 status = MEM_ACCESS_REQUEST_STARTED;
	unsigned long expiry = jiffies + msecs_to_jiffies(MEM_ACCESS_RESPONSE_TIMEOUT_MS);

	while (!ret && time_before(jiffies, expiry) && (status == MEM_ACCESS_REQUEST_STARTED)) {
		ret = mors->bus_ops->reg32_read(mors, MORSE_REG_MEM_ACCESS_STATUS(mors), &status);
		if (ret) {
			MORSE_WARN(mors, "Failed to get access request status: %d\n", ret);
			return ret;
		}
	}

	switch (status) {
	case MEM_ACCESS_REQUEST_STARTED:
		MORSE_WARN(mors, "Memory access response timed out\n");
		ret = -ETIMEDOUT;
		break;
	case MEM_ACCESS_REQUEST_DENIED:
		MORSE_WARN(mors, "Memory access request denied\n");
		ret = -EACCES;
		break;
	case MEM_ACCESS_REQUEST_GRANTED_BURST_MODE:
		fallthrough;
	case MEM_ACCESS_REQUEST_GRANTED:
		if (!access_grant_cb)
			break;

		access_grant_cb(mors, status == MEM_ACCESS_REQUEST_GRANTED_BURST_MODE);
		break;
	default:
		MORSE_ERR(mors, "Memory access request unknown response received: %08X\n", status);
		ret = -EFAULT;
		break;
	}

	return ret;
}

bool mem_access_is_restriction_enforced(struct morse *mors)
{
	int ret = 0;
	u32 state = MEM_ACCESS_STATE_DISABLED;

	if (!mors->cfg->mem_access_cfg)
		return false;

	if (mors->mem_access.restriction_is_overridden)
		return false;

	if (morse_hw_is_on(mors) && mors->mem_access.restriction_is_enforced)
		return true;

	ret = mors->bus_ops->reg32_read(mors, MORSE_REG_MEM_ACCESS_STATE(mors), &state);
	if (ret)
		MORSE_WARN(mors, "Failed to get access to mem access state: %d\n", ret);

	if (state == MEM_ACCESS_STATE_RESTRICTED) {
		MORSE_DBG_RATELIMITED(mors, "Memory access restriction enforcement detected\n");
		mors->mem_access.restriction_is_enforced = true;
	} else if (state == MEM_ACCESS_STATE_RESTRICTION_OVERRIDDEN) {
		mors->mem_access.restriction_is_enforced = false;
		mors->mem_access.restriction_is_overridden = true;
	} else {
		mors->mem_access.restriction_is_enforced = false;
	}

	return mors->mem_access.restriction_is_enforced;
}

void mem_access_reset_cache(struct morse *mors)
{
	mem_access_cache_store(mors, 0, 0);
}

int mem_access_request(struct morse *mors,
		       void (*access_grant_cb)(struct morse *mors, bool burst_enabled),
		       u32 address,
		       int len)
{
	int ret;

	if (!mem_access_is_restriction_enforced(mors))
		return 0;

	if (mem_access_is_within_fixed_regions(mors, address, len))
		return 0;

	if (mem_access_is_cached(mors, address, len))
		return 0;

	if (mem_access_request_is_expandable(mors, address, len)) {
		struct morse_hw_memory new_mem = mors->cfg->mem_access_cfg->expand_cache_access;

		address = new_mem.start;
		len = new_mem.end - new_mem.start;
	}

	ret = mem_access_send_request(mors, address, len);
	if (ret) {
		MORSE_ERR(mors, "%s: req 0x%08X:%d failed (ret:%d)", __func__, address, len, ret);
		return ret;
	}

	ret = mem_access_wait_for_response(mors, access_grant_cb);
	if (ret) {
		MORSE_ERR(mors, "%s: resp 0x%08X:%d failed (ret:%d)", __func__, address, len, ret);
		return ret;
	}

	mem_access_cache_store(mors, address, address + len);

	return ret;
}
