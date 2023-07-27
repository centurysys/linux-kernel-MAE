/*
 * Copyright 2022 Morse Micro
 *
 */

#ifndef _MORSE_RC_H_
#define _MORSE_RC_H_

#include "morse.h"
#include "mmrc-submodule/src/core/mmrc.h"
#include "linux/list.h"
#include <linux/workqueue.h>

/* This initial value is for MMRC, and there is another in minstrel_rc.h for Minstrel */
#define INIT_MAX_RATES_NUM 4

struct morse_rc {
	spinlock_t lock;
	struct list_head stas;
	struct timer_list timer;
	struct work_struct work;
	struct morse *mors;
};

struct morse_rc_sta {
	struct mmrc_table tb;
	struct list_head list;

	unsigned long last_update;
};

int morse_rc_init(struct morse *mors);

int morse_rc_deinit(struct morse *mors);

int morse_rc_sta_add(struct morse *mors,
		     struct ieee80211_sta *sta);

bool morse_rc_set_fixed_rate(struct morse *mors,
			struct ieee80211_sta *sta,
			int mcs,
			int bw,
			int ss,
			int guard);

void morse_rc_sta_remove(struct morse *mors,
			 struct ieee80211_sta *sta);

void morse_rc_sta_fill_tx_rates(struct morse *mors,
			    struct morse_skb_tx_info *tx_info,
			    struct sk_buff *skb,
			    struct ieee80211_sta *sta,
			    int tx_bw,
			    bool rts_allowed);

void morse_rc_sta_feedback_rates(struct morse *mors,
				 struct sk_buff *skb,
				 struct morse_skb_tx_status *tx_sts);

void morse_rc_sta_state_check(struct morse *mors, struct ieee80211_sta *sta,
		enum ieee80211_sta_state old_state, enum ieee80211_sta_state new_state);

/*
 * Reinitialize the associated stations when there is a change in BW.
 * Must be called with mors->lock held
 */
void morse_rc_reinit_stas(struct morse *mors, struct ieee80211_vif *vif);

#endif  /* !_MORSE_RC_H_ */

