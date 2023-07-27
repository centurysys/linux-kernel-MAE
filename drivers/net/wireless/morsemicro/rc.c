/*
 * Copyright 2022 Morse Micro
 *
 */

#include <linux/slab.h>
#include <linux/timer.h>
#include "morse.h"
#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "rc.h"

/* Enable/Disable the fixed rate (Disabled by default) */
static bool enable_fixed_rate __read_mostly;
module_param(enable_fixed_rate, bool, 0644);
MODULE_PARM_DESC(enable_fixed_rate, "Enable the fixed rate");

/* Set the fixed mcs (Take effect when enable_fixed_rate is activated) */
static int fixed_mcs __read_mostly = 4;
module_param(fixed_mcs, int, 0644);
MODULE_PARM_DESC(fixed_mcs, "Set the fixed mcs (work when enable_fixed_rate is on)");

/* Set the fixed bandwidth (Take effect when enable_fixed_rate is activated) */
static int fixed_bw __read_mostly = 2;
module_param(fixed_bw, int, 0644);
MODULE_PARM_DESC(fixed_bw, "Set the fixed bandwidth (work when enable_fixed_rate is on)");

/* Set the fixed spatial stream (Take effect when enable_fixed_rate is activated) */
static int fixed_ss __read_mostly = 1;
module_param(fixed_ss, int, 0644);
MODULE_PARM_DESC(fixed_ss, "Set the fixed spatial stream value (work when enable_fixed_rate is on)");

/* Set the fixed guard (Take effect when enable_fixed_rate is activated) */
static int fixed_guard __read_mostly;
module_param(fixed_guard, int, 0644);
MODULE_PARM_DESC(fixed_guard, "Set the fixed guard value (work when enable_fixed_rate is on)");

#define MORSE_RC_MMRC_BW_TO_FLAGS(X)				\
	(((X) == MMRC_BW_1MHZ) ? MORSE_SKB_RATE_FLAGS_1MHZ :	\
	((X) == MMRC_BW_2MHZ) ? MORSE_SKB_RATE_FLAGS_2MHZ :	\
	((X) == MMRC_BW_4MHZ) ? MORSE_SKB_RATE_FLAGS_4MHZ :	\
	((X) == MMRC_BW_8MHZ) ? MORSE_SKB_RATE_FLAGS_8MHZ :	\
	MORSE_SKB_RATE_FLAGS_2MHZ)

#define MORSE_RC_FLAGS_TO_MMRC_BW(X)				\
	(((X) & MORSE_SKB_RATE_FLAGS_1MHZ) ? MMRC_BW_1MHZ :	\
	((X) & MORSE_SKB_RATE_FLAGS_2MHZ) ? MMRC_BW_2MHZ :	\
	((X) & MORSE_SKB_RATE_FLAGS_4MHZ) ? MMRC_BW_4MHZ :	\
	((X) & MORSE_SKB_RATE_FLAGS_8MHZ) ? MMRC_BW_8MHZ :	\
	MMRC_BW_2MHZ)

#define MORSE_RC_BW_TO_MMRC_BW(X) \
	(((X) == 1) ? MMRC_BW_1MHZ : \
	((X) == 2) ? MMRC_BW_2MHZ : \
	((X) == 4) ? MMRC_BW_4MHZ : \
	((X) == 8) ? MMRC_BW_8MHZ : \
	MMRC_BW_2MHZ)

static void morse_rc_work(struct work_struct *work)
{
	struct morse_rc *mrc = container_of(work, struct morse_rc, work);
	struct list_head *pos;

	spin_lock_bh(&mrc->lock);

	list_for_each(pos, &mrc->stas) {
		struct morse_rc_sta *mrc_sta =
			container_of(pos, struct morse_rc_sta, list);
		unsigned long now = jiffies;

		mrc_sta->last_update = now;

		mmrc_update(&mrc_sta->tb);
	}

	spin_unlock_bh(&mrc->lock);

	mod_timer(&mrc->timer, jiffies + msecs_to_jiffies(100));
}

static void morse_rc_timer(struct timer_list *t)
{
	struct morse_rc *mrc = from_timer(mrc, t, timer);
	struct morse *mors  = mrc->mors;

	queue_work(mors->net_wq, &mors->mrc.work);
}

int morse_rc_init(struct morse *mors)
{
	morse_warn(mors, "rate control algorithm: 'MMRC'\n");
	INIT_LIST_HEAD(&mors->mrc.stas);
	spin_lock_init(&mors->mrc.lock);

	INIT_WORK(&mors->mrc.work, morse_rc_work);
	timer_setup(&mors->mrc.timer, morse_rc_timer, 0);

	mors->mrc.mors = mors;
	mod_timer(&mors->mrc.timer, jiffies + msecs_to_jiffies(100));
	return 0;
}

int morse_rc_deinit(struct morse *mors)
{
	cancel_work_sync(&mors->mrc.work);
	del_timer_sync(&mors->mrc.timer);

	return 0;
}

static void morse_rc_sta_config_guard_per_bw(bool enable_sgi_rc,
				struct ieee80211_sta *sta,
				struct mmrc_sta_capabilities *caps)
{
	if (sta->ht_cap.ht_supported) {
		if (caps->bandwidth & MMRC_MASK(MMRC_BW_1MHZ)) {
			caps->guard_per_bw[MMRC_BW_1MHZ] |= MMRC_MASK(MMRC_GUARD_LONG);
			if (enable_sgi_rc && (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20))
				caps->guard_per_bw[MMRC_BW_1MHZ] |= MMRC_MASK(MMRC_GUARD_SHORT);
		}

		if (caps->bandwidth & MMRC_MASK(MMRC_BW_2MHZ)) {
			caps->guard_per_bw[MMRC_BW_2MHZ] |= MMRC_MASK(MMRC_GUARD_LONG);
			if (enable_sgi_rc && (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40))
				caps->guard_per_bw[MMRC_BW_2MHZ] |= MMRC_MASK(MMRC_GUARD_SHORT);
		}
	}

	if (sta->vht_cap.vht_supported) {
		if (caps->bandwidth & MMRC_MASK(MMRC_BW_4MHZ)) {
			caps->guard_per_bw[MMRC_BW_4MHZ] |= MMRC_MASK(MMRC_GUARD_LONG);
			if (enable_sgi_rc &&
					(sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80))
				caps->guard_per_bw[MMRC_BW_4MHZ] |= MMRC_MASK(MMRC_GUARD_SHORT);
		}

		if (caps->bandwidth & MMRC_MASK(MMRC_BW_8MHZ)) {
			caps->guard_per_bw[MMRC_BW_8MHZ] |= MMRC_MASK(MMRC_GUARD_LONG);
			if (enable_sgi_rc &&
					(sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160))
				caps->guard_per_bw[MMRC_BW_8MHZ] |= MMRC_MASK(MMRC_GUARD_SHORT);
		}
	}
}

int morse_rc_sta_add(struct morse *mors,
		     struct ieee80211_sta *sta)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct mmrc_sta_capabilities caps;
	int oper_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;

	memset(&caps, 0, sizeof(caps));

	/* Configure STA for support up to 8MHZ */
	while (oper_bw_mhz > 0) {
		caps.bandwidth |= MMRC_MASK(MORSE_RC_BW_TO_MMRC_BW(oper_bw_mhz));
		oper_bw_mhz >>= 1;
	}

	/* Configure STA for support for 1 Spatial Streams */
	caps.spatial_streams |= MMRC_MASK(MMRC_SPATIAL_STREAM_1);
	/* Configure STA for support for MCS0 -> MCS7 and MCS10 */
	caps.rates = MMRC_MASK(MMRC_MCS0) | MMRC_MASK(MMRC_MCS1) |
		     MMRC_MASK(MMRC_MCS2) | MMRC_MASK(MMRC_MCS3) |
		     MMRC_MASK(MMRC_MCS4) | MMRC_MASK(MMRC_MCS5) |
		     MMRC_MASK(MMRC_MCS6) | MMRC_MASK(MMRC_MCS7) |
		     MMRC_MASK(MMRC_MCS10);
	/* Configure STA for short and long guard */
	if (mors->custom_configs.enable_sgi_rc)
		caps.guard = MMRC_MASK(MMRC_GUARD_LONG) | MMRC_MASK(MMRC_GUARD_SHORT);
	else
		caps.guard = MMRC_MASK(MMRC_GUARD_LONG);
	morse_rc_sta_config_guard_per_bw(mors->custom_configs.enable_sgi_rc, sta, &caps);

	/* Set max rates */
	if (mors->hw->max_rates > 0 && mors->hw->max_rates < IEEE80211_TX_MAX_RATES)
		caps.max_rates = mors->hw->max_rates;
	else
		caps.max_rates = IEEE80211_TX_MAX_RATES;

	/* Set max reties */
	if (mors->hw->max_rate_tries >= MMRC_MIN_CHAIN_ATTEMPTS &&
			mors->hw->max_rate_tries < MMRC_MAX_CHAIN_ATTEMPTS)
		caps.max_retries = mors->hw->max_rate_tries;
	else
		caps.max_retries = MMRC_MAX_CHAIN_ATTEMPTS;

	/* Initialise the STA in MMRC */
	mmrc_sta_init(&msta->rc.tb, &caps);

	/* Set last update time */
	msta->rc.last_update = jiffies;

	spin_lock_bh(&mors->mrc.lock);
	list_add(&msta->rc.list, &mors->mrc.stas);
	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

void morse_rc_reinit_stas(struct morse *mors, struct ieee80211_vif *vif)
{
	struct list_head *pos;
	struct morse_vif *mors_if = ieee80211_vif_to_morse_vif(vif);
	struct list_head *morse_sta_list = &mors_if->ap->stas;

	morse_info(mors, "%s: no_of_stations=%d\n", __func__, mors_if->ap->num_stas);
	list_for_each(pos, morse_sta_list) {
		struct morse_sta *msta  =
			list_entry(pos, struct morse_sta, list);
		int oper_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;
		struct ieee80211_sta *sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);

		if (msta && sta)
			morse_info(mors, "%s:Reinitialize the sta %pM with new op_bw=%d, ts=%ld\n", __func__,
							sta->addr,
							oper_bw_mhz,
							jiffies);
		else {
			morse_warn(mors, "%s:msta or sta null\n", __func__);
			continue;
		}

		morse_rc_sta_remove(mors, sta);
		morse_ecsa_update_sta_caps(mors, sta);
		morse_rc_sta_add(mors, sta);
		/* Set fixed rate */
		if (enable_fixed_rate && morse_rc_set_fixed_rate(mors, sta,
					fixed_mcs, fixed_bw, fixed_ss, fixed_guard))
			morse_err(mors, "%s:Set fixed rate failed\n", __func__);
	}
}

bool morse_rc_set_fixed_rate(struct morse *mors,
			struct ieee80211_sta *sta,
			int mcs,
			int bw,
			int ss,
			int guard)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct list_head *pos;
	struct mmrc_rate fixed_rate;
	bool ret_val = true;

	fixed_rate.rate = mcs;
	fixed_rate.bw = bw;
	/* Code spatial streams is zero based while user starts at 1, like the real spatial streams*/
	fixed_rate.ss = (ss - 1);
	fixed_rate.guard = guard;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta =
			list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			if (!mmrc_set_fixed_rate(&msta->rc.tb, fixed_rate))
				ret_val = false;
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);
	return ret_val;
}

void morse_rc_sta_remove(struct morse *mors,
			struct ieee80211_sta *sta)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;

	spin_lock_bh(&mors->mrc.lock);
	list_del(&msta->rc.list);
	spin_unlock_bh(&mors->mrc.lock);
}

static void morse_rc_sta_fill_basic_rates(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				int tx_bw)
{
	int i;
	int bw_rate_flag_mhz = MORSE_BW_TO_FLAGS(tx_bw);

	tx_info->rates[0].mcs = 0;
	tx_info->rates[0].count = 4;
	tx_info->rates[0].flags = cpu_to_le16(bw_rate_flag_mhz);

	for (i = 1; i < IEEE80211_TX_MAX_RATES; i++) {
		tx_info->rates[i].mcs = -1;
		tx_info->rates[i].count = -1;
		tx_info->rates[i].flags = 0;
	}
}

static int morse_rc_sta_get_rates(struct morse *mors,
			struct morse_sta *msta,
			struct mmrc_rate_table *rates,
			size_t size)
{
	int ret = -ENOENT;
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta =
			list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			ret = 0;
			mmrc_get_rates(&msta->rc.tb, rates, size);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);

	return ret;
}

void morse_rc_sta_fill_tx_rates(struct morse *mors,
			    struct morse_skb_tx_info *tx_info,
			    struct sk_buff *skb,
			    struct ieee80211_sta *sta,
			    int tx_bw,
			    bool rts_allowed)
{
	int ret, i;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct mmrc_rate_table rates;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	morse_rc_sta_fill_basic_rates(mors, tx_info, tx_bw);

	/* Use basic rates for non data packets */
	if (!msta || !ieee80211_is_data_qos(hdr->frame_control))
		return;

	ret = morse_rc_sta_get_rates(mors, msta, &rates, skb->len);

	/* If station not found, go for basic rates */
	if (ret < 0)
		return;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		tx_info->rates[i].mcs = rates.rates[i].rate;
		tx_info->rates[i].count = rates.rates[i].attempts;
		tx_info->rates[i].flags = cpu_to_le16(MORSE_RC_MMRC_BW_TO_FLAGS(rates.rates[i].bw));

		if (rts_allowed && (rates.rates[i].flags & BIT(MMRC_FLAGS_CTS_RTS)))
			tx_info->rates[i].flags |= cpu_to_le16(MORSE_SKB_RATE_FLAGS_RTS);

		if (rates.rates[i].guard == MMRC_GUARD_SHORT)
			tx_info->rates[i].flags |= cpu_to_le16(MORSE_SKB_RATE_FLAGS_SGI);

		/* Update skb tx_info */
		info->control.rates[i].idx = rates.rates[i].rate;
		info->control.rates[i].count = rates.rates[i].attempts;
		info->control.rates[i].flags = tx_info->rates[i].flags;
	}
}

static int morse_rc_sta_get_attempts(struct morse *mors,
			      struct morse_skb_tx_status *tx_sts)
{

	int attempts = 0;
	int i, count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);

	for (i = 0; i < count; i++) {
		if (tx_sts->rates[i].count > 0)
			attempts +=  tx_sts->rates[i].count;
		else
			break;
	}

	return attempts;
}

static void morse_rc_sta_set_rates(struct morse *mors,
			    struct morse_sta *msta,
			    struct mmrc_rate_table *rates,
			    int attempts,
			    bool is_agg_mode,
			    uint32_t success,
			    uint32_t failure)
{
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta =
			list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			if (is_agg_mode)
				mmrc_feedback_agg(&msta->rc.tb, rates, attempts, success, failure);
			else
				mmrc_feedback(&msta->rc.tb, rates, attempts);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);
}

void morse_rc_sta_feedback_rates(struct morse *mors,
				 struct sk_buff *skb,
				 struct morse_skb_tx_status *tx_sts)
{
	int attempts;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *r = &txi->status.rates[0];
	int count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);
	int i;
	struct mmrc_rate_table rates;
	struct morse_sta *msta = NULL;
	struct ieee80211_vif *vif = NULL;
	uint16_t rate_flags;
	uint32_t agg_success, agg_packets;

	/* Need the RCU lock to find a station, and must hold it until we're done with sta */
	rcu_read_lock();

	if (txi->control.vif)
		vif = txi->control.vif;
	else
		vif = morse_get_vif(mors);

	sta = ieee80211_find_sta(vif, hdr->addr1);

	if (!sta)
		goto exit;

	msta = (struct morse_sta *)sta->drv_priv;

	if (!msta || !ieee80211_is_data_qos(hdr->frame_control))
		/* use basic rates for non-data packets */
		goto exit;

	attempts = morse_rc_sta_get_attempts(mors, tx_sts);
	if (attempts <= 0)
		/* Did we really send the packet? */
		goto exit;

	/* Update mmrc rates struct from ieee80211_tx_info feedback */
	for (i = 0; i < count; i++) {
		rates.rates[i].rate = txi->control.rates[i].idx;
		rate_flags = txi->control.rates[i].flags;
		/* No spatial stream support in header so using fixed value */
		rates.rates[i].ss = MMRC_SPATIAL_STREAM_1;
		rates.rates[i].guard = MORSE_SKB_RATE_FLAGS_SGI_GET(rate_flags);
		rates.rates[i].bw = MORSE_RC_FLAGS_TO_MMRC_BW(rate_flags);
		rates.rates[i].flags = MORSE_SKB_RATE_FLAGS_RTS_GET(rate_flags);
		rates.rates[i].attempts = txi->control.rates[i].count;
	}

	if (msta) {
		/* Save the rate information. This will used to update stations tx rate stats*/
		msta->last_sta_tx_rate.bw = rates.rates[0].bw;
		msta->last_sta_tx_rate.rate = rates.rates[0].rate;
		msta->last_sta_tx_rate.guard = rates.rates[0].guard;
	}

	if (tx_sts->ampdu_info) {
		agg_success = MORSE_TXSTS_AMPDU_INFO_GET_SUC(tx_sts->ampdu_info);
		agg_packets = MORSE_TXSTS_AMPDU_INFO_GET_LEN(tx_sts->ampdu_info);
		morse_rc_sta_set_rates(mors, msta, &rates, attempts, true, agg_success, agg_packets - agg_success);
	} else {
		morse_rc_sta_set_rates(mors, msta, &rates, attempts, false, 0, 0);
	}

exit:
	ieee80211_tx_info_clear_status(txi);
	if (tx_sts) {
		if (!(tx_sts->flags & MORSE_TX_STATUS_FLAGS_NO_ACK) &&
				!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;

		if (tx_sts->flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED) {
			mors->debug.page_stats.tx_ps_filtered++;
			txi->flags |= IEEE80211_TX_STAT_TX_FILTERED;

			/* Clear TX CTL AMPDU flag so that this frame gets rescheduled in
			 * ieee80211_handle_filtered_frame(). This flag will get set
			 * again by mac80211's tx path on rescheduling.
			 */
			txi->flags &= ~IEEE80211_TX_CTL_AMPDU;
			if (msta) {
				if (!msta->tx_ps_filter_en)
					morse_dbg(mors, "TX ps filter set sta[%pM]\n", msta->addr);
				msta->tx_ps_filter_en = true;
			}
		}

		for (i = 0; i < count; i++) {
			if (tx_sts->rates[i].count > 0)
				r[i].count =  tx_sts->rates[i].count;
			else
				r[i].idx = -1;
		}
	} else {
		txi->control.rates[0].count = 1;
		txi->control.rates[1].idx = -1;
		if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;
	}
	/* single packet per A-MPDU (for now) */
	if (txi->flags & IEEE80211_TX_CTL_AMPDU) {
		txi->flags |= IEEE80211_TX_STAT_AMPDU;
		txi->status.ampdu_len = 1;
		txi->status.ampdu_ack_len = txi->flags & IEEE80211_TX_STAT_ACK ? 1 : 0;
	}

	/* Inform mac80211 that the SP (elicited by a PS-Poll or u-APSD) is over */
	if (sta && (txi->flags & IEEE80211_TX_STATUS_EOSP)) {
		txi->flags &= ~IEEE80211_TX_STATUS_EOSP;
		ieee80211_sta_eosp(sta);
	}

	rcu_read_unlock();
	ieee80211_tx_status(mors->hw, skb);
}

void morse_rc_sta_state_check(struct morse *mors, struct ieee80211_sta *sta,
		enum ieee80211_sta_state old_state, enum ieee80211_sta_state new_state)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;

	/* Add to Morse RC STA list */
	if ((old_state < new_state) && (new_state == IEEE80211_STA_ASSOC)) {
		/* Newly associated, add to RC */
		morse_rc_sta_add(mors, sta);

		/* Set fixed rate */
		if (enable_fixed_rate && morse_rc_set_fixed_rate(mors, sta,
					fixed_mcs, fixed_bw, fixed_ss, fixed_guard))
			morse_err(mors, "%s:Set fixed rate failed\n", __func__);
	} else if ((old_state > new_state) && (old_state == IEEE80211_STA_ASSOC)) {
		/* Lost Association, remove from list */
		morse_rc_sta_remove(mors, sta);
	} else if ((old_state < new_state) && (old_state == IEEE80211_STA_NONE)
								&& msta->rc.list.prev) {
		/* Special case for driver warning issue causing a sta to be left on the list */
		morse_info(mors, "Remove stale sta from rc list\n");
		morse_rc_sta_remove(mors, sta);
	}
}
