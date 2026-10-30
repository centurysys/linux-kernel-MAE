#ifndef _CAC_H_
#define _CAC_H_

/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <linux/types.h>
#include <linux/ieee80211.h>

#include "morse.h"

/**
 * 802.11ah CAC (Centralized Authentication Control)
 * 802.11REVme 9.4.2.202 and 11.3.9.2
 */

/** CAC threshold max, from IEEE 802.11REVme section 9.4.2.202 */
#define CAC_THRESHOLD_MAX	(1023)

#define CAC_CFG_CHANGE_RULE_MAX		(8)

struct morse_vif;

enum cac_command {
	CAC_COMMAND_DISABLE = 0,
	CAC_COMMAND_ENABLE = 1,
	CAC_COMMAND_CFG_GET = 2,
	CAC_COMMAND_CFG_SET = 3
};

/** CAC threshold change rule */
struct cac_threshold_change_rule {
	/* @arfs: Threshold in Authentication Request Frames per Second */
	u16 arfs;

	/** @threshold_change: Change in threshold to apply if condition is matched, between
	 *		       %-CAC_THRESHOLD_MAX and %CAC_THRESHOLD_MAX.
	 */
	s16 threshold_change;
};

/**
 * CAC threshold change rules (AP only).
 */
struct cac_threshold_change_rules {
	/** @rule_tot: Total number of rules. */
	u8 rule_tot;

	/** @rule: Individual rules. */
	struct cac_threshold_change_rule rule[CAC_CFG_CHANGE_RULE_MAX];
};

struct morse_cac_config {
	/**
	 * @enabled: True if CAC is enabled.
	 */
	bool enabled;

	/**
	 * @rules: Threshold change rules.
	 */
	struct cac_threshold_change_rules rules;
};

/**
 * CAC configuration and counters (AP only).
 */
struct morse_cac {
	struct morse *mors;
	 /* Serialise CAC timer functions */
	spinlock_t lock;
	struct timer_list timer;
	int cac_period_used;

	/** @conf: CAC configuration and rules */
	struct morse_cac_config *conf;

	/**
	 * @threshold_value: Threshold value for restricting authentications and associations,
	 *		     between 0 and CAC_THRESHOLD_MAX.
	 * * CAC_INDEX_MAX means there are no restrictions.
	 * * 0 means that only STAs already associating or not supporting CAC can associate.
	 */
	u16 threshold_value;

	/** @arfs: Authentication request frames received per second. */
	u16 arfs;

	/** @arrfs: Authentication request retry frames received per second. */
	u16 arrfs;

	/** @prfs: Probe request frames received per second. */
	u16 prfs;
};

/**
 * cac_threshold_pc2val(): Convert a threshold percentage into a raw value.
 * @val: Percentage value.
 *
 * Return: Raw value.
 */
static inline s16 cac_threshold_pc2val(s16 val)
{
	return val * CAC_THRESHOLD_MAX / 100;
}

/**
 * cac_threshold_val2pc(): Convert a raw value into a threshold percentage.
 * @val: Raw value.
 *
 * Return: Threshold as a percentage.
 *
 * The value is rounded up for consistency with values rounded down by cac_threshold_pc2val().
 */
static inline s16 cac_threshold_val2pc(s16 val)
{
	if (val < 0)
		return (val * 100 - (CAC_THRESHOLD_MAX - 1)) / CAC_THRESHOLD_MAX;
	else
		return (val * 100 + CAC_THRESHOLD_MAX - 1) / CAC_THRESHOLD_MAX;
}

/**
 * morse_cac_count_auth() - Keep a count of received initial authentication request
 *			    packets (AP only).
 * @mors_vif:	Virtual interface.
 * @hdr:	802.11 header.
 */
void morse_cac_count_auth(const struct ieee80211_vif *vif, const struct ieee80211_mgmt *hdr);

/**
 * morse_cac_count_probe_reqs() - Keep a count of received probe request packets (AP only).
 * @vif:	mac80211 VIF.
 */
void morse_cac_count_probe_reqs(const struct ieee80211_vif *vif);

/**
 * morse_cac_insert_ie() - Insert a CAC IE into an sk_buff.
 * @ies_mask:	Contains array of information elements.
 * @vif:	mac80211 VIF
 * @fc:		The packet frame_control field.
 */
void morse_cac_insert_ie(struct dot11ah_ies_mask *ies_mask, struct ieee80211_vif *vif, __le16 fc);

/**
 * morse_cac_is_enabled() - Indicate whether CAC is enabled on an interface.
 * @mors_vif:	Virtual interface.
 *
 * Return: True if CAC is enabled on the interface.
 */
bool morse_cac_is_enabled(struct morse_vif *mors_vif);

/**
 * morse_cac_get_rules() - Get threshold change rules.
 * @mors_vif:	Virtual interface.
 * @rules	Structure to receive threshold change rules.
 * @rule_tot	Field to receive number of rules.
 */
void morse_cac_get_rules(struct morse_vif *mors_vif, struct cac_threshold_change_rules *rules,
			 u8 *rule_tot);

/**
 * morse_cac_set_rules() - Configure threshold change rules.
 * @mors_vif:	Virtual interface.
 * @rules	Threshold change rules.
 */
void morse_cac_set_rules(struct morse_vif *mors_vif, struct cac_threshold_change_rules *rules);

/**
 * morse_cac_deinit() - De-initialise CAC on an interface.
 * @mors_vif:	Virtual interface.
 *
 * Return: 0 if the command succeeded, else an error code.
 */
int morse_cac_deinit(struct morse_vif *mors_vif);

/**
 * morse_cac_init() - Initialise CAC on an interface.
 * @mors:	Global Morse structure.
 * @mors_vif:	Virtual interface.
 * @in_reconfig:
 *		The HW state is restarting and reconfig is in progress.
 *
 * Return: 0 if the command succeeded, else an error code.
 */
int morse_cac_init(struct morse *mors, struct morse_vif *mors_vif, bool in_reconfig);

/**
 * morse_cac_reconfig() - Restore the CAC config.
 * @mors:	Global Morse structure.
 * @mors_vif:	Virtual interface.
 */
void morse_cac_reconfig(struct morse *mors, struct morse_vif *mors_vif);

#endif /* !_CAC_H_ */
