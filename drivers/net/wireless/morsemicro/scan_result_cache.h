#ifndef _MORSE_SCAN_RESULT_CACHE_H_
#define _MORSE_SCAN_RESULT_CACHE_H_
/*
 * Copyright 2025 Morse Micro
 */
#include "morse.h"

/**
 * scan_result_cache() - Store a scan result frame to the cache.
 *
 * @mors: Morse context
 * @rx: Received scan result
 * @rx_status: RX status of received scan result
 */
void scan_result_cache(struct morse *mors,
		       const struct sk_buff *rx,
		       const struct morse_skb_rx_status *rx_status);

/**
 * scan_result_cache_flush() - Flush the contents of the cache.
 *
 * @mors: Morse context
 */
void scan_result_cache_flush(struct morse *mors);

/**
 * scan_result_cache_replay() - Replay the scan result cache into the RX path.
 *
 * @mors: Morse context
 *
 * Return: Number of cached entries replayed.
 */
int scan_result_cache_replay(struct morse *mors);

/**
 * scan_result_cache_is_empty() - Determine if the cache is empty
 *
 * @mors: Morse context
 *
 * Return: true if empty
 */
bool scan_result_cache_is_empty(struct morse *mors);

/**
 * scan_result_cache_init() - Initialise the scan result cache.
 *
 * @mors: Morse context
 */
void scan_result_cache_init(struct morse *mors);

/**
 * scan_result_cache_sysfs_init() - Initialise the scan result cache sysfs entry.
 *
 * Will appear under the devices /sysfs/ entry as 'scan_result_cache' and
 * 'scan_result_cache_clear'.
 *
 * @mors: Morse context
 *
 * Return: 0 on success
 */
int scan_result_cache_sysfs_init(struct morse *mors);

/**
 * scan_result_cache_sysfs_finish() - De-initialise the scan result cache sysfs entry
 *
 * @mors: Morse context
 */
void scan_result_cache_sysfs_finish(struct morse *mors);

/**
 * scan_result_cache_destroy() - De-initialise the scan result cache
 *
 * @mors: Morse context
 */
void scan_result_cache_destroy(struct morse *mors);

#endif /* !_MORSE_SCAN_RESULT_CACHE_H_ */
