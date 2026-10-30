/*
 * Copyright 2022 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef _MORSE_RC_H_
#define _MORSE_RC_H_

#include "morse.h"
#include "mmrc-submodule/src/core/mmrc.h"
#include "linux/list.h"
#include <net/mac80211.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/if_ether.h>

#if KERNEL_VERSION(5, 10, 0) <= MAC80211_VERSION_CODE
/* #define AIRTIME_REPORTING_SUPPORTED - not yet supported */
#endif

/* This initial value is for MMRC, and there is another in minstrel_rc.h for Minstrel */
#define INIT_MAX_RATES_NUM 4

#ifdef CONFIG_MORSE_DEBUGFS
/* Number of mmrc_feedback() events buffered for the mmrc_feedback_csv debugfs log.
 * Must be a power of two for kfifo. When full, the oldest event is dropped.
 */
#define MMRC_FEEDBACK_LOG_DEPTH 1024

/**
 * A single mmrc_feedback() event, captured for the mmrc_feedback_csv debugfs log.
 * Holds the full retry chain reported by the firmware plus the ack result so it
 * can be replayed (one CSV row per used rate) when userspace drains the log.
 */
struct mmrc_feedback_log_entry {
	/** Monotonic timestamp (ns) of when the feedback was logged */
	u64 timestamp_ns;
	/** Peer MAC address the feedback relates to */
	u8 addr[ETH_ALEN];
	/** True if the frame was acked */
	u8 acked;
	/** Bitmap indicating if attempts were sent as part of an AMPDU */
	u8 per_attempt_ampdu_info;
	/** Bitmap indicating if attempts had a lost blockack */
	u8 per_attempt_missing_block_ack;
	/** The retry chain used to send the frame, as reported by the chip */
	struct mmrc_rate_table rates;
};
#endif

struct morse_rc {
	/* Serialise rate control queue manipulation and timer functions */
	spinlock_t lock;
	struct list_head stas;
	struct timer_list timer;
	struct delayed_work dwork;
	struct morse *mors;
#ifdef CONFIG_MORSE_DEBUGFS
	/* Ring buffer of recent mmrc_feedback() events drained by mmrc_feedback_csv.
	 * Producer runs under @lock; consumers (debugfs reads) also take @lock so the
	 * drop-oldest-on-full pop stays mutually exclusive with reads.
	 */
	DECLARE_KFIFO(feedback_log, struct mmrc_feedback_log_entry, MMRC_FEEDBACK_LOG_DEPTH);
#endif
};

struct morse_rc_sta {
	struct mmrc_table *tb;
	struct list_head list;

	unsigned long last_update;
};

int morse_rc_init(struct morse *mors);

int morse_rc_deinit(struct morse *mors);

int morse_rc_sta_add(struct morse *mors, struct ieee80211_vif *vif, struct ieee80211_sta *sta);

#define morse_rc_set_fixed_rate(mors, sta, mcs, bw, ss, guard) \
	_morse_rc_set_fixed_rate(mors, sta, mcs, bw, ss, guard, __func__)
bool _morse_rc_set_fixed_rate(struct morse *mors,
			      struct ieee80211_sta *sta,
			      int mcs, int bw, int ss, int guard, const char *caller);

bool morse_rc_get_enable_fixed_rate(void);
int morse_rc_get_fixed_bandwidth(void);
int morse_rc_get_fixed_mcs(void);
int morse_rc_get_fixed_ss(void);
int morse_rc_get_fixed_guard(void);

void morse_rc_sta_remove(struct morse *mors, struct ieee80211_sta *sta);

void morse_rc_sta_fill_tx_rates(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct ieee80211_sta *sta, int tx_bw, bool rts_allowed);

void morse_rc_sta_feedback_rates(struct morse *mors, struct sk_buff *skb,
				 struct ieee80211_sta *sta, struct morse_skb_tx_status *tx_sts);

#ifdef CONFIG_MORSE_DEBUGFS
/**
 * Record an mmrc_feedback() event into the mmrc_feedback_csv ring buffer.
 *
 * @note Must be called with mors->mrc.lock held (the feedback path already does).
 *
 * @mors: Global morse struct
 * @addr: Peer MAC address the feedback relates to
 * @rates: The retry chain used to send the frame, as reported by the chip
 * @per_attempt_ampdu_info: Bitmap indicating if each attempt was sent as part of an aggregate
 * @per_attempt_missing_block_ack: Bitmap indicating if each aggregated attempt received no blockack
 * @acked: True if the frame was acked
 */
void morse_rc_log_feedback(struct morse *mors, const u8 *addr,
			   const struct mmrc_rate_table *rates,
			   u8 per_attempt_ampdu_info, u8 per_attempt_missing_block_ack, bool acked);
#endif

void morse_rc_sta_state_check(struct morse *mors,
			      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			      enum ieee80211_sta_state old_state,
			      enum ieee80211_sta_state new_state);

/**
 * Validate an MMRC rate descriptor against supported ranges.
 *
 * @param rate MMRC rate structure
 *
 * @return true if the bandwidth and MCS index are within supported ranges,
 *         false otherwise.
 */
static inline bool mmrc_rate_is_valid(const struct mmrc_rate *rate)
{
	return (rate && rate->bw < MMRC_SUPP_NUM_BW &&
			rate->rate < MMRC_SUPP_NUM_MCS);
}

/**
 * morse_rc_reinit_all_stas() - Reinitialize the associated stations when
 * there is a change in BW due to ECSA or channel switch.
 *
 * @mors: Global morse struct
 * @mors_vif: The morse VIF struct
 */
void morse_rc_reinit_all_stas(struct morse *mors, struct ieee80211_vif *vif);

/**
 * morse_rc_reinit_sta() - Reinitialize a single associated station's
 * rate control for the current operating bandwidth.
 *
 * @vif: The vif the station is associated with
 * @sta: The station to reinitialize
 */
void morse_rc_reinit_sta(struct ieee80211_vif *vif, struct ieee80211_sta *sta);

/**
 * morse_rc_update_work() - Work handler to reinitialise rate control for a
 * station after a bandwidth change. Deferred from sta_rc_update.
 *
 * @work: Work item
 */
void morse_rc_update_work(struct work_struct *work);

/**
 * morse_rc_vif_update_mcast_rate() - Goes through the list of STAs connected to
 * VIF to find and update MMRC rate for multicast traffic.
 *
 * @mors: Global morse struct
 * @mors_vif: The morse VIF struct
 */
void morse_rc_vif_update_mcast_rate(struct morse *mors, struct morse_vif *mors_vif);

/**
 * morse_rc_calculate_airtime() - Calculate airtime to transmit/receive frame at given rate.
 *
 * @param rc The rate of transfer.
 * @param size The size of the frame being transferred.
 *
 * Return: Airtime in microseconds, or 0 if the rate is invalid or unsupported.
 */
u32 morse_rc_calculate_airtime(morse_rate_code_t rc, size_t size);

#endif /* !_MORSE_RC_H_ */
