/*
 * Copyright 2025 Morse Micro
 *
 */

#include "hw.h"

/**
 * mem_access_is_restriction_enforced - check if memory access restriction is enforced
 *
 * @mors: Global morse struct
 *
 * Return: true if restriction is enforced, false if not
 */
bool mem_access_is_restriction_enforced(struct morse *mors);

/**
 * mem_access_reset_cache - reset memory access cache
 *
 * @mors: Global morse struct
 */
void mem_access_reset_cache(struct morse *mors);

/**
 * mem_access_request - process memory access request from the bus
 *
 * @mors: Global morse struct
 * @access_grant_cb: Optional callback that will be called upon access grant
 * @address: Memory access request start address
 * @len: Memory access request size
 *
 * Return: 0 if success, otherwise error code
 */
int mem_access_request(struct morse *mors,
		       void (*access_grant_cb)(struct morse *mors, bool burst_enabled),
		       u32 address,
		       int len);

