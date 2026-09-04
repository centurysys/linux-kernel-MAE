/*
 * Copyright 2025 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * Support for beacon offload - where beacons are generated in the chip instead
 * of in the driver.
 */

#ifndef _MORSE_BEACON_HW_H_
#define _MORSE_BEACON_HW_H_

#include "morse.h"

/**
 * enum morse_beacon_offload_op - Beacon offload operation
 *
 * @MORSE_BEACON_OFFLOAD_OP_STOP: Stop offloading beacons
 * @MORSE_BEACON_OFFLOAD_OP_START: Start offload beacons
 */
enum morse_beacon_offload_op {
	MORSE_BEACON_OFFLOAD_OP_STOP,
	MORSE_BEACON_OFFLOAD_OP_START,
};

/**
 * morse_hw_beacon_offload_insert_tlvs - Insert beacon offload TLVs into the req
 *
 * @mors_vif: Morse VIF structure
 * @req: address of the pointer to a morse_cmd_req_beacon_offload structure. On success *req
 *       will be reallocated (via krealloc) to a larger buffer and the TLVs inserted
 * @req_size: pointer to a size_t holding the current size of *req; on return this
 *            is updated to the new total size of the req.
 *
 * Helper function for generating the beacon offload command.
 *
 * @warning this function will resize the request based on the size of the TLVs
 * it is the callers responsibility to free the buffer
 *
 * Return: 0 on success, else error code
 */
int morse_hw_beacon_offload_insert_tlvs(struct morse_vif *mors_vif,
	struct morse_cmd_req_beacon_offload **req, size_t *req_size);

/**
 * morse_hw_beacon_offload_dump_cmd - Dump a filled out beacon offload command
 *
 * @mors: Morse structure
 * @req: command to dump
 */
void morse_hw_beacon_offload_dump_cmd(struct morse *mors, struct morse_cmd_req_beacon_offload *req);

/**
 * morse_hw_beacon_update_mac80211_dtim_count - Update the mac80211 dtim count
 *
 * @mors_vif: Morse VIF structure to update
 * @dtim_count: the updated value of the DTIM count
 *
 * Continuously call `ieee80211_beacon_get` until the dtim count it the value
 * prior to the @dtim_count
 */
void morse_hw_beacon_update_mac80211_dtim_count(struct morse_vif *mors_vif, u16 dtim_count);

/**
 * morse_hw_beacon_can_offload - can the morse vif support beacon offload
 *
 * @mors_vif: Morse VIF structure
 *
 * Return: True if supported else false
 */
bool morse_hw_beacon_can_offload(struct morse_vif *mors_vif);

#endif
