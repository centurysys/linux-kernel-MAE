#ifndef _CAC_H_
#define _CAC_H_
/*
 * Copyright 2022 Morse Micro
 */

#include <linux/types.h>
#include <linux/ieee80211.h>

#include "debug.h"

/**
 * 802.11ah CAC (Centralised Authentication Control)
 * 802.11REVme 9.4.2.202 and 11.3.9.2
 */

#define MORSE_CAC_DBG	1
#define MORSE_CAC_PRINT(_m, _f, _a...) __morse_dbg(MORSE_CAC_DBG, _m, _f, ##_a)

#define CAC_THRESHOLD_MAX	1023	/* IEEE 802.11REVme 9.4.2.202 */
#define CAC_THRESHOLD_STEP	64	/* Threshold steps up and down by this much */
#define CAC_INDEX_MAX		((CAC_THRESHOLD_MAX + 1) / CAC_THRESHOLD_STEP)
#define CAC_RANDOM_MAX		(CAC_THRESHOLD_MAX - 1) /* IEEE 802.11REVme 11.3.9.2 */

/**
 * CAC configuration and counters (AP only).
 */
struct morse_cac {
	struct morse *mors;
	spinlock_t lock;
	struct timer_list timer;
	int cac_period_used;

	/**
	 * CAC enabled
	 */
	bool enabled;

	/**
	 * Threshold value for restricting authentications and asssociations, stored as a
	 * factor of CAC_THRESHOLD_MAX.
	 * A value of CAC_INDEX_MAX means there are no restrictions.
	 * A value of 0 will mean that only STAs already associating or not supporting CAC can
	 * associate.
	 */
	u8 threshold_index;

	/**
	 * Authentication request frames received.
	 */
	u32 arfs;
};

/**
 * @brief Keep a count of received initial authentication request packets (AP only).
 */
void morse_cac_count_auth(const struct ieee80211_vif *vif,
	const struct ieee80211_mgmt *hdr, size_t len);

/**
 * @brief De-initialise CAC
 */
int morse_cac_deinit(const struct morse *mors);

/**
 * @brief Initialise CAC
 */
int morse_cac_init(struct morse *mors);

#endif  /* !_CAC_H_ */
