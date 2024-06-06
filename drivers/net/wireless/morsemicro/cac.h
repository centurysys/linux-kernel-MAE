#ifndef _CAC_H_
#define _CAC_H_

/*
 * Copyright 2023 Morse Micro
 */

#include <linux/types.h>
#include <linux/ieee80211.h>

#include "morse.h"

/**
 * 802.11ah CAC (Centralized Authentication Control)
 * 802.11REVme 9.4.2.202 and 11.3.9.2
 */

#define CAC_THRESHOLD_MAX	1023	/* IEEE 802.11REVme 9.4.2.202 */
#define CAC_THRESHOLD_STEP	64	/* Threshold steps up and down by this much */
#define CAC_INDEX_MAX		((CAC_THRESHOLD_MAX + 1) / CAC_THRESHOLD_STEP)
#define CAC_RANDOM_MAX		(CAC_THRESHOLD_MAX - 1)	/* IEEE 802.11REVme 11.3.9.2 */

struct morse_vif;

enum cac_command {
	CAC_COMMAND_DISABLE = 0,
	CAC_COMMAND_ENABLE = 1
};

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
	 * Threshold value for restricting authentications and associations, stored as a
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
 * morse_cac_insert_ie() - Insert a CAC IE into an sk_buff
 *
 * @ies_mask: Contains array of information elements.
 * @vif: The VIF the IE was received on.
 * @fc: The packet frame_control field.
 */
void morse_cac_insert_ie(struct dot11ah_ies_mask *ies_mask, struct ieee80211_vif *vif, __le16 fc);

/**
 * morse_cac_is_enabled() - Indicate whether CAC is enabled on an interface
 *
 * @mors_vif	Virtual interface
 *
 * Return: True if CAC is enabled on the interface
 */
bool morse_cac_is_enabled(struct morse_vif *mors_vif);

/**
 * morse_cac_deinit() - De-initialise CAC on an interface
 *
 * @mors_vif	Virtual interface
 *
 * Return: 0 if the command succeeded, else an error code
 */
int morse_cac_deinit(struct morse_vif *mors_vif);

/**
 * morse_cac_init() - Initialise CAC on an interface
 *
 * @mors	The global Morse structure
 * @mors_vif	Virtual interface
 *
 * Return: 0 if the command succeeded, else an error code
 */
int morse_cac_init(struct morse *mors, struct morse_vif *mors_vif);

#endif /* !_CAC_H_ */
