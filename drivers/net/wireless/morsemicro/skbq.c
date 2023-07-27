/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>

#include "morse.h"
#include "debug.h"
#include "skbq.h"
#include "mac.h"
#include "command.h"
#include "skb_header.h"
#include "ipmon.h"

/* Enable/Disable avoid buffer bloating */
static int max_txq_len __read_mostly = 22;
module_param(max_txq_len, int, 0644);
MODULE_PARM_DESC(max_txq_len, "Maximum number of queued TX packets");

static uint32_t tx_status_lifetime_ms __read_mostly = (15 * 1000); /* 15 sec default lifetime */
module_param(tx_status_lifetime_ms, uint, 0644);
MODULE_PARM_DESC(tx_status_lifetime_ms, "Maximum lifetime for pending tx packets before considered dropped");

/**
 * Private driver data stored in skb control buffer after a packet has been given to the chip, and
 * is awaiting the tx_status to come back.
 */
struct morse_tx_status_drv_data {
	/** jiffies for when this packet has spent too long in the pending queue.
	 * If now > jiffies_when_expired, we can consider the tx lost.
	 */
	unsigned long jiffies_when_expired;
};

static int __skbq_data_tx_finish(struct morse_skbq *mq, struct sk_buff *skb,
			struct morse_skb_tx_status *tx_sts);

static struct sk_buff *__skbq_get_pending_by_id(struct morse *mors,
			struct morse_skbq *mq,
			u32 pkt_id);

#ifndef CONFIG_MORSE_RC
/**
 * translate morse_skb_tx_status to mac80211 tx status control values
 */
static void
morse_skb_tx_status_to_tx_control(struct morse *mors, struct sk_buff *skb,
	struct morse_skb_tx_status *tx_sts,
	struct ieee80211_tx_info *txi)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct morse_sta *msta = NULL;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_rate *r = &txi->status.rates[0];
	int i, count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);
	/* There will always be at least one rate tried. */
	unsigned int last_i = 0;

	/* Need the RCU lock to find a station, and must hold it until we're done with sta */
	rcu_read_lock();
	vif = (txi->control.vif) ? txi->control.vif : morse_get_vif(mors);
	sta = ieee80211_find_sta(vif, hdr->addr1);
	if (sta)
		msta = (struct morse_sta *)sta->drv_priv;

	ieee80211_tx_info_clear_status(txi);
	if (!(tx_sts->flags & MORSE_TX_STATUS_FLAGS_NO_ACK))
		if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;

	if (tx_sts->flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED) {
		mors->debug.page_stats.tx_ps_filtered++;
		txi->flags |= IEEE80211_TX_STAT_TX_FILTERED;

		morse_dbg(mors, "from_chip ps filtered [sn:%d]%s\n",
			IEEE80211_SEQ_TO_SN(hdr->seq_ctrl),
			!!(txi->flags & IEEE80211_TX_INTFL_RETRIED) ?
				" mac80211 will drop" : "");

		/* Clear TX CTL AMPDU flag so that this frame gets rescheduled in
		 * ieee80211_handle_filtered_frame(). This flag will get set
		 * again by mac80211's tx path on rescheduling.
		 */
		txi->flags &= ~IEEE80211_TX_CTL_AMPDU;
		if (msta && !msta->tx_ps_filter_en) {
			morse_dbg(mors, "TX ps filter set sta[%pM],[sn:%d]\n",
				msta->addr, IEEE80211_SEQ_TO_SN(hdr->seq_ctrl));
			msta->tx_ps_filter_en = true;
		}
	}

	/* Inform mac80211 that the SP (elicited by a PS-Poll or u-APSD) is over */
	if (sta && (txi->flags & IEEE80211_TX_STATUS_EOSP)) {
		txi->flags &= ~IEEE80211_TX_STATUS_EOSP;
		ieee80211_sta_eosp(sta);
	}

	rcu_read_unlock();

	for (i = 0; i < count; i++) {
		if (tx_sts->rates[i].count > 0) {
			last_i = i;
			r[i].count =  tx_sts->rates[i].count;

			/* Update MCS0/10 failure stats. */
			if (tx_sts->rates[i].mcs == 0)
				mors->debug.mcs_stats_tbl.mcs0.tx_fail += tx_sts->rates[i].count;
			if (tx_sts->rates[i].mcs == 10)
				mors->debug.mcs_stats_tbl.mcs10.tx_fail += tx_sts->rates[i].count;
		} else {
			r[i].idx = -1;
		}
	}

	/* Check if the last attempt was successful and if it was MCS0/10. */
	if (tx_sts->rates[last_i].count > 0) {
		if (tx_sts->rates[last_i].mcs == 0) {
			mors->debug.mcs_stats_tbl.mcs0.tx_success++;
			mors->debug.mcs_stats_tbl.mcs0.tx_fail--;
		} else if (tx_sts->rates[last_i].mcs == 10) {
			mors->debug.mcs_stats_tbl.mcs10.tx_success++;
			mors->debug.mcs_stats_tbl.mcs10.tx_fail--;
		}
	}
}
#endif /* CONFIG_MORSE_RC */

static inline u32 __morse_skbq_size(const struct morse_skbq *mq)
{
	return mq->skbq_size;
}

static inline u32 __morse_skbq_space(const struct morse_skbq *mq)
{
	return MORSE_SKBQ_SIZE - __morse_skbq_size(mq);
}

static inline bool __morse_skbq_over_threshold(struct morse_skbq *mq)
{
	return max_txq_len ?
		(mq->skbq.qlen >= max_txq_len) :
		(__morse_skbq_space(mq) <= 2 * 1024);
}

static inline bool __morse_skbq_under_threshold(struct morse_skbq *mq)
{
	return max_txq_len ?
		(mq->skbq.qlen < (max_txq_len - 2)) :
		(__morse_skbq_space(mq) >= (5 * 1024));
}

static int __morse_skbq_put(struct morse_skbq *mq, struct sk_buff *skb)
{
	if (skb->len > __morse_skbq_space(mq)) {
		morse_info(mq->mors, "Morse SKBQ out of memory %d:%d:%d\n",
			   skb->len, __morse_skbq_space(mq), mq->skbq_size);
		return -ENOMEM;
	}
	__skb_queue_tail(&mq->skbq, skb);
	mq->skbq_size += skb->len;
	return 0;
}

static void __morse_skbq_pkt_id(struct morse_skbq *mq, struct sk_buff *skb)
{
	struct morse_buff_skb_header *hdr =
				(struct morse_buff_skb_header *)skb->data;

	hdr->tx_info.pkt_id = cpu_to_le32(mq->pkt_seq++);
}

static struct morse_skbq *__morse_skbq_match_tx_status_to_skbq(
	struct morse *mors,
	const struct morse_skb_tx_status *tx_sts)
{
	struct morse_skbq *mq = NULL;

	switch (tx_sts->channel) {
	case MORSE_SKB_CHAN_DATA:
	case MORSE_SKB_CHAN_DATA_NOACK:
	case MORSE_SKB_CHAN_LOOPBACK: {
		int aci = dot11_tid_to_ac(tx_sts->tid);

		mq = mors->cfg->ops->skbq_tc_q_from_aci(mors, aci);
		break;
	}
	case MORSE_SKB_CHAN_MGMT:
		mq = mors->cfg->ops->skbq_mgmt_tc_q(mors);
		break;
	case MORSE_SKB_CHAN_BEACON:
		mq = mors->cfg->ops->skbq_bcn_tc_q(mors);
		break;
	default:
		morse_err(mors, "unexpected channel on reported tx status [%d]\n",
			  tx_sts->channel);
	}

	return mq;
}

static void insert_pending_skb_to_skbq(struct morse_skbq *mq,
	struct sk_buff *skb, __le32 insertion_id)
{
	struct sk_buff *pfirst, *pnext;
	struct morse_buff_skb_header *mhdr;
	struct sk_buff *tail = skb_peek_tail(&mq->skbq);

	/* Remove it from the pending list */
	__skb_unlink(skb, &mq->pending);

	/* Add the size back to the SKBq */
	mq->skbq_size += skb->len;

	if (tail == NULL) {
		/* List is empty */
		__skb_queue_tail(&mq->skbq, skb);
		return;
	}

	/* Check if it should just be inserted on to the end */
	mhdr = (struct morse_buff_skb_header *) tail->data;
	MORSE_WARN_ON(insertion_id == mhdr->tx_info.pkt_id);
	if (insertion_id >= mhdr->tx_info.pkt_id) {
		__skb_queue_tail(&mq->skbq, skb);
		return;
	}

	/* Otherwise, re-insert to correct spot in skbq */
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		mhdr = (struct morse_buff_skb_header *) pfirst->data;

		MORSE_WARN_ON(insertion_id == mhdr->tx_info.pkt_id);
		if (insertion_id <= mhdr->tx_info.pkt_id) {
			__skb_queue_before(&mq->skbq, pfirst, skb);
			return;
		}
	}

	/* Shouldn't get to here */
	BUG_ON(1);
}

static void __skbq_drop_pending_skb(struct morse_skbq *mq, struct sk_buff *skb)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);

	__skb_unlink(skb, &mq->pending);
	skb_pull(skb, sizeof(struct morse_buff_skb_header));

	/* If this frame is the last frame in a PS-Poll or u-APSD SP,
	 * then mac80211 must be informed that the SP is now over.
	 */
	if (txi->flags & IEEE80211_TX_STATUS_EOSP) {
		struct ieee80211_vif *vif = (txi->control.vif) ? txi->control.vif :
			morse_get_vif(mq->mors);
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		struct ieee80211_sta *sta;

		if (vif) {
			rcu_read_lock();
			sta = ieee80211_find_sta(vif, hdr->addr1);
			if (sta)
				ieee80211_sta_eosp(sta);
			rcu_read_unlock();
		}
	}

	ieee80211_free_txskb(mq->mors->hw, skb);
	mq->mors->debug.page_stats.tx_status_dropped++;
}

static bool tx_skb_is_ps_filtered(struct morse_skbq *mq, struct sk_buff *skb,
	struct morse_skb_tx_status *tx_sts)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = (txi->control.vif) ?
		txi->control.vif : morse_get_vif(mq->mors);
	struct morse_vif *mors_if = ieee80211_vif_to_morse_vif(vif);

	BUG_ON(!(tx_sts->flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED));

	if ((tx_sts->flags & MORSE_TX_STATUS_PAGE_INVALID) ||
		!mors_if->supports_ps_filter) {
		/* Do not rebuffer invalid pages, or on VIFs that do not support
		 * PS filtering.
		 */
		__skbq_drop_pending_skb(mq, skb);
		return true;
	}

	/* mac80211 handles per-station re-buffering in AP mode */
	if (vif->type != NL80211_IFTYPE_STATION)
		return false;

	BUG_ON(tx_sts->channel != MORSE_SKB_CHAN_DATA);
	BUG_ON(!(mq->flags & MORSE_CHIP_IF_FLAGS_DATA));

	/* In STA mode, the driver re-buffers internally as mac80211 does not
	 * support this.
	 */
	insert_pending_skb_to_skbq(mq, skb, tx_sts->pkt_id);
	return true;
}

static void morse_skbq_tx_status_process(struct morse *mors,
					 struct sk_buff *skb, u8 channel)
{
	int i;
	int mismatch = 0;
	struct morse_skb_tx_status *tx_sts =
		(struct morse_skb_tx_status *)skb->data;
	int count = skb->len / sizeof(*tx_sts);

	BUG_ON(channel != 0xFF);

	for (i = 0; i < count; tx_sts++, i++) {
		struct sk_buff *tx_skb;
		struct morse_skbq *mq = __morse_skbq_match_tx_status_to_skbq(
			mors, tx_sts);
		bool is_ps_filtered =
			(tx_sts->flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED);
		if (!mq) {
			morse_dbg(mors, "No pending skbq match found [pktid:%d chan:%d]\n",
				tx_sts->pkt_id, tx_sts->channel);
			mismatch++;
			continue;
		}

		spin_lock_bh(&mq->lock);
		tx_skb = __skbq_get_pending_by_id(mors,
			mq, tx_sts->pkt_id);
		if (!tx_skb) {
			morse_dbg(mors, "No pending pkt match found [pktid:%d chan:%d]\n",
				tx_sts->pkt_id, tx_sts->channel);
			mismatch++;
			spin_unlock_bh(&mq->lock);
			continue;
		}

		if (tx_sts->flags & MORSE_TX_STATUS_PAGE_INVALID)
			mors->debug.page_stats.tx_status_page_invalid++;

		if (is_ps_filtered) {
			if (tx_skb_is_ps_filtered(mq, tx_skb, tx_sts)) {
				/* Has been consumed by tx_skb_is_ps_filtered */
				spin_unlock_bh(&mq->lock);
				continue;
			}
		}

		skb_pull(tx_skb, sizeof(struct morse_buff_skb_header));

		morse_skbq_skb_finish(mq, tx_skb, tx_sts);

		spin_unlock_bh(&mq->lock);
	}

	morse_dbg(mors, "TX status %d (%d mismatch)\n", count, mismatch);

	if (mors->ps.enable &&
		!mors->ps.suspended &&
		(mors->cfg->ops->skbq_get_tx_buffered_count(mors) == 0)) {
		/* Evaluate ps to check if it was gated on a pending tx status */
		queue_delayed_work(mors->chip_wq, &mors->ps.delayed_eval_work, 0);
	}
}

static void morse_skbq_dispatch_work(struct work_struct *dispatch_work)
{
	struct morse_skbq *mq = container_of(dispatch_work, struct morse_skbq,
					     dispatch_work);
	struct morse *mors = mq->mors;
	struct morse_buff_skb_header *hdr;
	struct sk_buff_head skbq;
	struct sk_buff *pfirst, *pnext;
	u8 channel;
	int count = 0;

	__skb_queue_head_init(&skbq);

	morse_skbq_deq(mq, &skbq, morse_skbq_size(mq));

	skb_queue_walk_safe(&skbq, pfirst, pnext) {
		__skb_unlink(pfirst, &skbq);
		/* Header endianess has already be adjusted */
		hdr = (struct morse_buff_skb_header *)pfirst->data;
		channel = hdr->channel;
		/* Remove alignment tail */
		__skb_trim(pfirst, pfirst->len - hdr->tail);
		/* Remove morse header */
		__skb_pull(pfirst, sizeof(*hdr));
		if (channel == MORSE_SKB_CHAN_COMMAND)
			/* Commands/Events */
			morse_cmd_resp_process(mors, pfirst, channel);
		else if (channel == MORSE_SKB_CHAN_TX_STATUS) {
			morse_skbq_tx_status_process(mors, pfirst, channel);
			dev_kfree_skb_any(pfirst);
		} else if (channel == MORSE_SKB_CHAN_LOOPBACK) {
			dev_kfree_skb_any(pfirst);
		} else
			morse_mac_skb_recv(mors, pfirst, channel,
					   &hdr->rx_status);
		count++;
	}

	/* rerun recv in case skbq was full and we coudln't copy data */
	set_bit(MORSE_RX_PEND, &mors->chip_if->event_flags);
	queue_work(mors->chip_wq, &mors->chip_if_work);
}

/**
 * Remove and free all entries in a queue.
 */
int morse_skbq_purge(struct morse_skbq *mq, struct sk_buff_head *skbq)
{
	struct sk_buff *skb;
	int cnt = 0;

	spin_lock_bh(&mq->lock);
	while ((skb = __skb_dequeue(skbq))) {
		cnt++;
		dev_kfree_skb_any(skb);
	}
	spin_unlock_bh(&mq->lock);

	return cnt;
}

int morse_skbq_enq(struct morse_skbq *mq, struct sk_buff_head *skbq)
{
	int size, count = 0;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	size = __morse_skbq_space(mq);
	skb_queue_walk_safe(skbq, pfirst, pnext) {
		if (pfirst->len > size)
			break;
		__skb_unlink(pfirst, skbq);
		__skb_queue_tail(&mq->skbq, pfirst);
		count += pfirst->len;
		size -= pfirst->len;
		mq->skbq_size += pfirst->len;
	}
	spin_unlock_bh(&mq->lock);
	return count;
}

int morse_skbq_deq(struct morse_skbq *mq,
			  struct sk_buff_head *skbq,
			  int size)
{
	int count = 0;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		if (pfirst->len > size)
			break;
		__skb_unlink(pfirst, &mq->skbq);
		__skb_queue_tail(skbq, pfirst);
		count += pfirst->len;
		size -= pfirst->len;
		mq->skbq_size -= pfirst->len;
	}
	spin_unlock_bh(&mq->lock);
	return count;
}

/* Remove given number of items from the head of the queue. */
int morse_skbq_deq_num_items(struct morse_skbq *mq,
			  struct sk_buff_head *skbq,
			  int num_items)
{
	int count = 0;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		if (count >= num_items)
			break;
		__skb_unlink(pfirst, &mq->skbq);
		__skb_queue_tail(skbq, pfirst);
		++count;
		mq->skbq_size -= pfirst->len;
	}
	spin_unlock_bh(&mq->lock);
	return count;
}

int morse_skbq_enq_prepend(struct morse_skbq *mq, struct sk_buff_head *skbq)
{
	int size, count = 0;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	size = __morse_skbq_space(mq);

	/*
	 * We are doing a reverse walk here to ensure the order remains the same.
	 * This means the last member of the queue goes in, on top of the queue first
	 * and gets pushed down as more members get added to the top of the queue.
	 */
	skb_queue_reverse_walk_safe(skbq, pfirst, pnext) {
		if (pfirst->len > size)
			break;
		__skb_unlink(pfirst, skbq);
		__skb_queue_head(&mq->skbq, pfirst);
		count += pfirst->len;
		size -= pfirst->len;
		mq->skbq_size += pfirst->len;
	}
	spin_unlock_bh(&mq->lock);
	return count;
}

void morse_skbq_show(const struct morse_skbq *mq, struct seq_file *file)
{
	seq_printf(file, "pkts:%d skbq:%d pending:%d\n",
		   mq->skbq.qlen, mq->skbq_size, mq->pending.qlen);
}

void morse_skbq_stop_tx_queues(struct morse *mors)
{
	if (!mors->started)
		return;

	/* Wake/Stop mac80211 queues is not needed when using pull interface */
	if (!mors->custom_configs.enable_airtime_fairness) {
		int queue;

		mors->debug.page_stats.queue_stop++;
		for (queue = IEEE80211_AC_VO; queue <= IEEE80211_AC_BK; queue++)
			ieee80211_stop_queue(mors->hw, queue);
	}

	set_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags);
}

/*
 * Wake all Tx queues if all queues are below threshold
 */
void morse_skbq_may_wake_tx_queues(struct morse *mors)
{
	int queue;
	struct morse_skbq *qs;
	int num_qs;
	bool could_wake;

	if (!mors->started)
		return;

	could_wake = true;
	mors->cfg->ops->skbq_get_tx_qs(mors, &qs, &num_qs);
	for (queue = 0; queue < num_qs; queue++) {
		struct morse_skbq *mq = &qs[queue];

		if (!could_wake)
			break;

		spin_lock_bh(&mq->lock);
		could_wake &= (__morse_skbq_under_threshold(mq));
		spin_unlock_bh(&mq->lock);
	}

	if (!could_wake)
		return;

	/* Wake/Stop mac80211 queues is not needed when using pull interface */
	if (!mors->custom_configs.enable_airtime_fairness) {
		for (queue = IEEE80211_AC_VO; queue <= IEEE80211_AC_BK; queue++)
			ieee80211_wake_queue(mors->hw, queue);
	}

	clear_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags);
}


static int morse_skbq_tx(struct morse_skbq *mq, struct sk_buff *skb, u8 channel)
{
	struct morse *mors = mq->mors;
	bool mq_over_threshold;
	int rc;

	/* TODO data Alignment */
	spin_lock_bh(&mq->lock);
	rc = __morse_skbq_put(mq, skb);
	if (rc) {
		morse_err(mors, "__morse_skbq_put channel %d failed (%d)\n", channel, rc);
		if (channel == MORSE_SKB_CHAN_DATA) {
			u16 queue = skb_get_queue_mapping(skb);

			morse_err(mors, "__morse_skbq_put queue %d status %d\n",
				queue,  ieee80211_queue_stopped(mors->hw, queue));
		}
	}

	/* Fill packet ID in TX info */
	__morse_skbq_pkt_id(mq, skb);

	mq_over_threshold = __morse_skbq_over_threshold(mq);
	spin_unlock_bh(&mq->lock);

	/* For data packets stop queues */
	if ((channel == MORSE_SKB_CHAN_DATA) && mq_over_threshold)
		morse_skbq_stop_tx_queues(mors);

#ifdef CONFIG_MORSE_IPMON
	{
		struct morse_buff_skb_header *hdr;
		static uint64_t time_start;

		hdr = (struct morse_buff_skb_header *) skb->data;
		if (channel == MORSE_SKB_CHAN_DATA)
			morse_ipmon(&time_start, skb, skb->data + sizeof(*hdr),
					le16_to_cpu(hdr->len), IPMON_LOC_CLIENT_DRV2,
					mors->debug.page_stats.queue_stop);
	}
#endif

	switch (channel) {
	case MORSE_SKB_CHAN_DATA:
	case MORSE_SKB_CHAN_LOOPBACK:
	case MORSE_SKB_CHAN_DATA_NOACK:
		if (morse_is_data_tx_allowed(mors)) {
			set_bit(MORSE_TX_DATA_PEND, &mors->chip_if->event_flags);
			queue_work(mors->chip_wq, &mors->chip_if_work);
		}
		break;
	case MORSE_SKB_CHAN_MGMT:
		set_bit(MORSE_TX_MGMT_PEND, &mors->chip_if->event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
		break;
	case MORSE_SKB_CHAN_BEACON:
		set_bit(MORSE_TX_BEACON_PEND, &mors->chip_if->event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
		break;
	case MORSE_SKB_CHAN_COMMAND:
		set_bit(MORSE_TX_COMMAND_PEND, &mors->chip_if->event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
		break;
	default:
		morse_err(mors, "Invalid SKB channel: %d\n", channel);
		break;
	}

	return rc;
}

/**
 * Get tx_status driver data from skb control buffer. Only valid once packet has been sent to
 * the chip
 */
static inline struct morse_tx_status_drv_data *__get_tx_status_driver_data(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct morse_tx_status_drv_data) >
		sizeof(tx_info->status.status_driver_data));
	return (struct morse_tx_status_drv_data *) &tx_info->status.status_driver_data[0];
}

/**
 * Move the skb to the pending queue, and take a timestamp of when we have waited too long for a
 * tx_status from the chip.
 */
static inline void __skbq_tx_move_to_pending(struct morse_skbq *mq, struct sk_buff *skb)
{
	struct morse_tx_status_drv_data *pend_info = __get_tx_status_driver_data(skb);

	/* Use coarse as we care more about this function being fast than being ms accurate.
	 */
	pend_info->jiffies_when_expired = jiffies + msecs_to_jiffies(tx_status_lifetime_ms);
	__skb_queue_tail(&mq->pending, skb);
}

/**
 * Has the packet spent too long in the pending queue waiting for a tx_status?
 */
static inline bool __has_pending_tx_skb_timed_out(struct sk_buff *skb)
{
	struct morse_tx_status_drv_data *info = __get_tx_status_driver_data(skb);

	/* If our timestamp value is in the past then we have timed out. */
	return time_is_before_jiffies(info->jiffies_when_expired);
}


int morse_skbq_tx_complete(struct morse_skbq *mq, struct sk_buff_head *skbq)
{
	bool skb_awaits_tx_status = false;
	struct morse *mors = mq->mors;
	struct sk_buff *pfirst, *pnext;
	struct sk_buff *peek = skb_peek(skbq);
	struct morse_buff_skb_header *hdr;

	if (peek == NULL)
		return 0;

	/* Move sent packets to pending list waiting for feedback */
	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(skbq, pfirst, pnext) {
		__skb_unlink(pfirst, skbq);
		hdr = (struct morse_buff_skb_header *) pfirst->data;
		/* Firmware doesn't give status on beacons
		 * so just free them, otherwise queue and wait response
		 */
		if (hdr->channel == MORSE_SKB_CHAN_BEACON ||
		    hdr->channel == MORSE_SKB_CHAN_LOOPBACK) {
			if (hdr->channel == MORSE_SKB_CHAN_BEACON)
				morse_mac_esca_beacon_tx_done(mors, pfirst);
			dev_kfree_skb_any(pfirst);
		} else {
			/* SKB has been given to the chip. Store the time and queue the skb onto the pending
			 * queue while we wait for the tx_status
			 */
			__skbq_tx_move_to_pending(mq, pfirst);
			skb_awaits_tx_status = true;
		}
	}
	spin_unlock_bh(&mq->lock);

	if (skb_awaits_tx_status) {
		spin_lock_bh(&mors->stale_status.lock);

		if (mors->stale_status.enabled)
			mod_timer(&mors->stale_status.timer, jiffies +
				msecs_to_jiffies(morse_skbq_get_tx_status_lifetime_ms()));

		spin_unlock_bh(&mors->stale_status.lock);
	}

	return 0;
}

/* Returns the first SKB in the pending list.
 * Should usually be matched against a TX_STATUS packet or a response
 * for command. Note: skb->data points to SKB header, user should skip
 * header and look for his own data.
 */
struct sk_buff *morse_skbq_tx_pending(struct morse_skbq *mq)
{
	struct sk_buff *pfirst;

	spin_lock_bh(&mq->lock);
	pfirst = skb_peek(&mq->pending);
	spin_unlock_bh(&mq->lock);
	return pfirst;
}

/* Get a pending frame by its ID. This will also drop frames with
 * older packet ids that are in the list
 */
static struct sk_buff *__skbq_get_pending_by_id(struct morse *mors,
					struct morse_skbq *mq,
					u32 pkt_id)
{
	struct sk_buff *pfirst, *pnext;
	struct sk_buff *ret = NULL;

	/* Move sent packets to pending list waiting for feedback */
	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		struct morse_buff_skb_header *hdr;

		hdr = (struct morse_buff_skb_header *)pfirst->data;
		if (hdr->tx_info.pkt_id == pkt_id) {
			ret = pfirst;
			break;

		} else if (hdr->tx_info.pkt_id < pkt_id &&
				__has_pending_tx_skb_timed_out(pfirst)) {
			/* Returned TX statuses may appear out-of-order during AMPDU */
			morse_dbg(mors,
				"%s: pending TX SKB timed out [id:%d,chan:%d] (curr:%d)\n",
				__func__, hdr->tx_info.pkt_id, hdr->channel, pkt_id);
			__skbq_drop_pending_skb(mq, pfirst);
			mq->mors->debug.page_stats.tx_status_flushed++;
		}
	}

	return ret;
}

int morse_skbq_check_for_stale_tx(struct morse *mors,
	struct morse_skbq *mq)
{
	int flushed = 0;
	struct sk_buff *pfirst;
	struct sk_buff *pnext;

	if (mq->pending.qlen == 0)
		return 0;

	/* Move sent packets to pending list waiting for feedback */
	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		struct morse_buff_skb_header *hdr =
			(struct morse_buff_skb_header *)pfirst->data;

		if (__has_pending_tx_skb_timed_out(pfirst)) {
			morse_dbg(mors, "%s: TX SKB timed out [id:%d,chan:%d]\n",
				__func__, hdr->tx_info.pkt_id, hdr->channel);
			__skbq_drop_pending_skb(mq, pfirst);
			mq->mors->debug.page_stats.tx_status_flushed++;
			flushed++;
		}
	}
	spin_unlock_bh(&mq->lock);

	return flushed;
}

/* Convert QoS NULL functions indicating PM set to NULL function.
 * This is a workaround a linux bug where only a check on null functions
 * is used to start power management. This was fixed in v5.5 and later.
 * For v5.5 and later kernels this function shouldn't be called.
 */
static void __skbq_qosnullfunc_to_nullfunc(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	__le16 fc = hdr->frame_control;

	if (ieee80211_is_qos_nullfunc(fc) && ieee80211_has_pm(fc)) {
		fc &= ~(cpu_to_le16(IEEE80211_STYPE_QOS_NULLFUNC));
		fc |= cpu_to_le16(IEEE80211_STYPE_NULLFUNC);
		hdr->frame_control = fc;
	}
}

/* Remove commands from pending (or skbq if not sent) */
static int __skbq_cmd_finish(struct morse_skbq *mq, struct sk_buff *skb)
{
	struct morse *mors = mq->mors;

	if (mq->pending.qlen > 0) {
		__skb_unlink(skb, &mq->pending);
		dev_kfree_skb(skb);
	} else if (mq->skbq.qlen > 0) {
		/* Command was probably timed out before being sent */
		morse_info(mors, "Command pending queue empty. Removing from SKBQ.\n");
		__skb_unlink(skb, &mq->skbq);
		if (mq->skbq_size >= skb->len)
			mq->skbq_size -= skb->len;
		dev_kfree_skb(skb);
	} else {
		morse_info(mors, "Command Q not found\n");
	}

	return 0;
}

struct morse_skbq_mon_ent {
	uint8_t sa[ETH_ALEN];
	uint8_t da[ETH_ALEN];
	uint32_t tot_sent;
	uint32_t qsize_cur;
	uint32_t qsize_max;
};

struct morse_skbq_mon_tbl {
	struct morse_skbq_mon_ent ent_all;
	struct morse_skbq_mon_ent ent_mcast;
	struct morse_skbq_mon_ent ent[8];
} *morse_skbq_mon;

/**
 * Dump the Per-station SKB queue monitor table
 * On first call the table is allocated.
 * On subsequent calls, the data is printed and the contents of the table are cleared.
 */
void morse_skbq_mon_dump(struct morse *mors, struct seq_file *file)
{
	struct morse_skbq_mon_ent *ent;
	int i;

	if (!morse_skbq_mon) {
		morse_skbq_mon = kcalloc(1, sizeof(*morse_skbq_mon), GFP_KERNEL);
		seq_puts(file, "Initialised per-station SKB queue monitoring\n");
		return;
	}

	seq_puts(file, "Idx Source            Dest              Total    Q Size   Max Size\n");

	for (i = 0; i < ARRAY_SIZE(morse_skbq_mon->ent); i++) {
		ent = &morse_skbq_mon->ent[i];
		if (is_zero_ether_addr(ent->sa))
			break;
		seq_printf(file, "%3d %pM %pM %-8d %-8d %-8d\n",
			i, ent->sa, ent->da, ent->tot_sent, ent->qsize_cur, ent->qsize_max);
	}

	ent = &morse_skbq_mon->ent_mcast;
	seq_printf(file, "%3s %-35s %-8d %-8d %-8d\n",
		"-", "Multicast/Broadcast", ent->tot_sent, ent->qsize_cur, ent->qsize_max);

	ent = &morse_skbq_mon->ent_all;
	seq_printf(file, "%3s %-35s %-8d %-8d %-8d\n",
		"-", "All Tx", ent->tot_sent, ent->qsize_cur, ent->qsize_max);

	/* reset the table */
	memset(morse_skbq_mon, 0, sizeof(*morse_skbq_mon));
}

/**
 * Get a per-STA queue monitor entry for a src/dst pair.
 *
 * @skb - SKB
 * @add - if true, create a new entry if not found
 */
static struct morse_skbq_mon_ent *morse_skbq_mon_get(struct morse *mors,
		struct sk_buff *skb, bool add)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	uint8_t *sa = ieee80211_get_SA(hdr);
	uint8_t *da = ieee80211_get_DA(hdr);
	struct morse_skbq_mon_ent *ent;
	int i;

	if (hdr->frame_control == 0xaa) {
		/* Header has not been stripped */
		hdr = (struct ieee80211_hdr *)(skb->data + sizeof(struct morse_buff_skb_header));
		sa = ieee80211_get_SA(hdr);
		da = ieee80211_get_DA(hdr);
	}

	if (is_zero_ether_addr(sa) || is_zero_ether_addr(da))
		return NULL;

	if (is_multicast_ether_addr(da))
		return &morse_skbq_mon->ent_mcast;

	for (i = 0; i < ARRAY_SIZE(morse_skbq_mon->ent); i++) {
		ent = &morse_skbq_mon->ent[i];
		if (memcmp(sa, ent->sa, ETH_ALEN) == 0 &&
		    memcmp(da, ent->da, ETH_ALEN) == 0) {
			return ent;
		}
		if (is_zero_ether_addr(ent->sa)) {
			if (!add)
				break;
			/* Not found - add entry */
			memcpy(&ent->sa[0], sa, ETH_ALEN);
			memcpy(&ent->da[0], da, ETH_ALEN);
			ent->tot_sent = 0;
			ent->qsize_cur = 0;
			ent->qsize_max = 0;
			morse_info(mors, "%s: add i=%d [%pM->%pM]\n", __func__, i, sa, da);
			return ent;
		}
	}

	morse_info(mors, "%s: [%pM %pM] NOT found\n", __func__, sa, da);

	return NULL;
}

/**
 * Increment or decrement a per-STA queue monitor entry for a src/dst pair.
 *
 * @skb - SKB
 * @incr - increment if true (sending Tx frame to firmware), decrement if false (Tx done)
 */
static void morse_skbq_mon_adjust(struct morse *mors, struct sk_buff *skb, bool incr)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct morse_skbq_mon_ent *ent;
	struct morse_skbq_mon_ent *ent_all = &morse_skbq_mon->ent_all;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	ent = morse_skbq_mon_get(mors, skb, incr);
	if (!ent)
		return;

	if (incr) {
		ent->tot_sent++;
		ent_all->tot_sent++;
		ent->qsize_cur++;
		ent_all->qsize_cur++;
		if (ent->qsize_max < ent->qsize_cur)
			ent->qsize_max = ent->qsize_cur;
		if (ent_all->qsize_max < ent_all->qsize_cur)
			ent_all->qsize_max = ent_all->qsize_cur;
	} else {
		if (ent->qsize_cur == 0 || ent_all->qsize_cur == 0) {
			morse_err(mors,
				"%s: [%pM %pM] Unexpected ctr %d/%d %d/%d %d/%d\n",
				__func__, ent->sa, ent->da,
				ent->qsize_cur, ent_all->qsize_cur,
				ent->tot_sent, ent_all->tot_sent,
				ent->qsize_max, ent_all->qsize_max);
			return;
		}
		ent->qsize_cur--;
		ent_all->qsize_cur--;
	}
}

#ifndef CONFIG_MORSE_RC
static void morse_skbq_tx_status_fill(struct morse *mors,
				struct sk_buff *skb,
				struct morse_skb_tx_status *tx_sts)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);

	/* Originally we were checking for the IEEE80211_TX_CTL_REQ_TX_STATUS
	 * flag but, for now to use minstrel, we have to provide
	 * a TX status otherwise it won't work
	 */
	/* TODO: replace fake tx status */
	if (tx_sts) {
		morse_skb_tx_status_to_tx_control(mors, skb, tx_sts, txi);

#if defined(CONFIG_MORSE_DEBUGFS) && defined(CONFIG_MORSE_DEBUG_TXSTATUS)
		morse_debug_log_tx_status(mors, tx_sts);
#endif
	} else {
		ieee80211_tx_info_clear_status(txi);

		txi->control.rates[0].count = 1;
		txi->control.rates[1].idx = -1;
		if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;
	}

	/* single packet per A-MPDU (for now) */
	if (txi->flags & IEEE80211_TX_CTL_AMPDU) {
		txi->flags |= IEEE80211_TX_STAT_AMPDU;
		txi->status.ampdu_len = 1;
		txi->status.ampdu_ack_len =
			txi->flags & IEEE80211_TX_STAT_ACK ? 1 : 0;
	}

	ieee80211_tx_status(mors->hw, skb);
}
#endif /* CONFIG_MORSE_RC */

/* TX status/Response received remove packet from pending TX finish */
static int __skbq_data_tx_finish(struct morse_skbq *mq, struct sk_buff *skb,
			 struct morse_skb_tx_status *tx_sts)
{
	struct morse *mors = mq->mors;
	struct ieee80211_vif *vif = morse_get_vif(mors);
	struct morse_vif *mors_if = NULL;

	if (vif)
		mors_if = (struct morse_vif *)vif->drv_priv;

	if (morse_skbq_mon)
		morse_skbq_mon_adjust(mors, skb, 0);

	__skb_unlink(skb, &mq->pending);

	/* Workaround Linux */
	__skbq_qosnullfunc_to_nullfunc(skb);

	morse_mac_process_bcn_change_seq_tx_finish(mors, skb);

	if (mors->hw->conf.flags & IEEE80211_CONF_MONITOR)
		dev_kfree_skb(skb);
	else
#ifdef CONFIG_MORSE_RC
		morse_rc_sta_feedback_rates(mors, skb, tx_sts);
#else
		morse_skbq_tx_status_fill(mors, skb, tx_sts);
#endif

	return 0;
}

int morse_skbq_skb_finish(struct morse_skbq *mq, struct sk_buff *skb,
			 struct morse_skb_tx_status *tx_sts)
{
	int ret_sts;

	if (mq->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
		ret_sts = __skbq_cmd_finish(mq, skb);
	else
		ret_sts = __skbq_data_tx_finish(mq, skb, tx_sts);

	return ret_sts;
}

int morse_skbq_tx_flush(struct morse_skbq *mq)
{
	struct sk_buff *pfirst, *pnext;
	struct morse *mors = mq->mors;
	int cnt = 0;

	spin_lock_bh(&mq->lock);

	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		cnt++;
		__skb_unlink(pfirst, &mq->pending);
		ieee80211_free_txskb(mors->hw, pfirst);
	}

	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		cnt++;
		__skb_unlink(pfirst, &mq->skbq);
		ieee80211_free_txskb(mors->hw, pfirst);
	}

	spin_unlock_bh(&mq->lock);

	return cnt;
}

void morse_skbq_init(struct morse *mors, bool from_chip,
		     struct morse_skbq *mq, u16 flags)
{
	spin_lock_init(&mq->lock);
	__skb_queue_head_init(&mq->skbq);
	__skb_queue_head_init(&mq->pending);
	mq->mors = mors;
	mq->skbq_size = 0;
	mq->flags = flags;
	mq->pkt_seq = 0;
	if (from_chip)
		INIT_WORK(&mq->dispatch_work, morse_skbq_dispatch_work);
}

void morse_skbq_finish(struct morse_skbq *mq)
{
	if (mq->skbq_size > 0)
		morse_info(mq->mors, "Purging a non empty MorseQ. Dropping data!");

	/* Clean up link to chip_if */
	mq->mors->cfg->ops->skbq_close(mq);
	morse_skbq_purge(mq, &mq->skbq);
	morse_skbq_purge(mq, &mq->pending);
	mq->skbq_size = 0;
}

u32 morse_skbq_size(struct morse_skbq *mq)
{
	u32 count;

	spin_lock_bh(&mq->lock);
	count = __morse_skbq_size(mq);
	spin_unlock_bh(&mq->lock);
	return count;
}

u32 morse_skbq_count(struct morse_skbq *mq)
{
	u32 count = 0;

	spin_lock_bh(&mq->lock);
	count += mq->skbq.qlen;
	spin_unlock_bh(&mq->lock);
	return count;
}

u32 morse_skbq_count_tx_ready(struct morse_skbq *mq)
{
	struct morse *mors = mq->mors;

	if (!morse_is_data_tx_allowed(mors))
		return 0;

	return morse_skbq_count(mq);
}

u32 morse_skbq_space(struct morse_skbq *mq)
{
	u32 space;

	spin_lock_bh(&mq->lock);
	space = __morse_skbq_space(mq);
	spin_unlock_bh(&mq->lock);

	return space;
}

/* memory size assumed enough for header */
static void morse_skb_header_put(const struct morse_buff_skb_header *hdr,
			     uint8_t *buf)
{
	struct morse_buff_skb_header *buf_hdr =
				(struct morse_buff_skb_header *)buf;

	memcpy(buf_hdr, hdr, sizeof(*hdr));
	/* Adjust endianness */
	buf_hdr->len = cpu_to_le16(buf_hdr->len);
}


struct sk_buff *morse_skbq_alloc_skb(struct morse_skbq *mq, unsigned int length)
{
	size_t offset = (length & 0x03) ?
			(4 - (unsigned long)(length & 3)) : 0;
	int skb_len = sizeof(struct morse_buff_skb_header) + length + offset;
	struct sk_buff *skb;

	skb = dev_alloc_skb(skb_len);
	if (!skb)
		return NULL;
	skb_put(skb, skb_len);
	skb_pull(skb, sizeof(struct morse_buff_skb_header));
	return skb;
}

int morse_skbq_skb_tx(struct morse_skbq *mq, struct sk_buff *skb,
		      struct morse_skb_tx_info *tx_info, u8 channel)
{
	struct morse_buff_skb_header hdr;
	struct morse *mors;
	size_t offset;
	int ret = 0;

	if ((mq == NULL) || (skb == NULL))
		return -EINVAL;

	mors = mq->mors;

	offset = (skb->len & 0x03) ?
			(4 - (unsigned long)(skb->len & 3)) : 0;

	if (morse_skbq_mon)
		morse_skbq_mon_adjust(mors, skb, 1);

	/* Align size to words */
	if (offset && (offset > skb_tailroom(skb))) {
		/* TODO allocat new skb */
		struct sk_buff *tmp;

		morse_info(mors,
			"%s Unaligned SKB with not enough tailroom extending\n",
			  __func__);
		tmp = skb_copy_expand(skb, skb_headroom(skb),
					offset + skb_tailroom(skb), GFP_ATOMIC);
		if (!tmp) {
			morse_err(mors,
			  "%s Unaligned SKB with not enough tailroom to extend\n",
			  __func__);
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}
		dev_kfree_skb_any(skb);
		skb = tmp;
	}

	hdr.sync = MORSE_SKB_HEADER_SYNC;
	hdr.channel = channel;
	hdr.len = skb->len;
	hdr.tail = offset;
	if (tx_info)
		memcpy(&hdr.tx_info, tx_info, sizeof(*tx_info));
	else
		memset(&hdr.tx_info, 0, sizeof(hdr.tx_info));
	skb_push(skb, sizeof(hdr));
	morse_skb_header_put(&hdr, skb->data);

	skb_put(skb, offset);

	ret = morse_skbq_tx(mq, skb, channel);
	if (ret) {
		morse_err(mors, "morse_skbq_tx fail: %d\n", ret);
		dev_kfree_skb_any(skb);
	}
	return ret;
}

int morse_skbq_get_tx_status_lifetime_ms(void)
{
	return tx_status_lifetime_ms;
}

void morse_skbq_data_traffic_pause(struct morse *mors)
{
	set_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED, &mors->state_flags);
	/* power-save requirements will be re-evaluated by the caller */
}

void morse_skbq_data_traffic_resume(struct morse *mors)
{
	clear_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED, &mors->state_flags);

	/* Set the TX_DATA_PEND bit. This will kick the transmission path to
	 * send any frames pending in the TX buffers, and wake the mac80211
	 * data Qs if they were previously stopped.
	 */
	set_bit(MORSE_TX_DATA_PEND, &mors->chip_if->event_flags);
}
