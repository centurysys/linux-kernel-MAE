/*
 * Copyright 2022 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include "morse.h"
#include "mac.h"
#include "rc.h"
#include "debug.h"
#include "mmrc-submodule/src/core/mmrc.h"

/** Packet size in bytes used to display per-rate airtime */
#define AIRTIME_DISPLAY_PACKET_SIZE_BYTES 1200

struct station_stats_iter {
	const struct ieee80211_vif *on_vif;
	struct seq_file *file;
};

static void morse_print_station_stats(void *data, struct ieee80211_sta *sta)
{
	struct station_stats_iter *iter_data = data;
	struct seq_file *file = iter_data->file;
	u32 last_tx_rate_kbps;
	u32 last_rx_rate_kbps;
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct morse_skb_rx_status *status;

	if (!msta) {
		MORSE_WARN_ON(FEATURE_ID_DEFAULT, 1);
		return;
	}

	if (msta->vif != iter_data->on_vif)
		return;

	status = &msta->last_rx.data_status;
	if (!msta->last_rx.is_data_set) {
		seq_printf(file, "Mesh Peer link %pM (rc unknown)\n", msta->addr);
		return;
	}

	msta->last_sta_rx_rate.guard = morse_ratecode_sgi_get(status->morse_ratecode);
	msta->last_sta_rx_rate.flags = morse_ratecode_rts_get(status->morse_ratecode);
	msta->last_sta_rx_rate.rate =
	    morse_ratecode_mcs_index_get(status->morse_ratecode);
	msta->last_sta_rx_rate.ss =
	    morse_ratecode_nss_index_get(status->morse_ratecode);
	msta->last_sta_rx_rate.bw =
	    morse_ratecode_bw_index_get(status->morse_ratecode);

	last_tx_rate_kbps =
	    BPS_TO_KBPS(mmrc_calculate_theoretical_throughput(msta->last_sta_tx_rate));
	last_rx_rate_kbps =
	    BPS_TO_KBPS(mmrc_calculate_theoretical_throughput(msta->last_sta_rx_rate));

	seq_printf(file, "Mesh Peer link %pM\n", msta->addr);
	seq_printf(file,
		   "    Last TX PHY rate: %u.%uMbps  MCS: %u BW: %uMHz  NSS: %u  %s\n",
		   last_tx_rate_kbps / 1000, last_tx_rate_kbps % 1000,
		   msta->last_sta_tx_rate.rate,
		   morse_ratecode_bw_index_to_s1g_bw_mhz(msta->last_sta_tx_rate.bw),
		   msta->last_sta_tx_rate.ss,
		   msta->last_sta_tx_rate.guard ? "short GI" : "long GI");
	seq_printf(file,
		   "    Last RX PHY rate: %u.%uMbps  MCS: %u BW: %uMHz  NSS: %u  %s\n",
		   last_rx_rate_kbps / 1000, last_rx_rate_kbps % 1000,
		   msta->last_sta_rx_rate.rate,
		   morse_ratecode_bw_index_to_s1g_bw_mhz(msta->last_sta_rx_rate.bw),
		   msta->last_sta_rx_rate.ss,
		   msta->last_sta_rx_rate.guard ? "short GI" : "long GI");
	seq_printf(file, "    RSSI: %d dBm\n", (s8)le16_to_cpu(status->rssi));
}

static int mesh_stats_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	int vif_id;

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct station_stats_iter iter_data = {
			.on_vif = vif,
			.file = file,
		};

		if (!vif || vif->type != NL80211_IFTYPE_MESH_POINT)
			continue;

		seq_printf(file, "%s: Peer Stats\n", morse_vif_name(vif));
		ieee80211_iterate_stations_atomic(mors->hw, morse_print_station_stats, &iter_data);
	}
	return 0;
}

static u8 bw_index_to_mhz(u8 bw_idx)
{
	if (bw_idx == MMRC_BW_8MHZ)
		return 8;
	if (bw_idx == MMRC_BW_4MHZ)
		return 4;
	if (bw_idx == MMRC_BW_2MHZ)
		return 2;
	if (bw_idx == MMRC_BW_16MHZ)
		return 16;

	return 1;
}

static int stats_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct list_head *pos;
	struct mmrc_table *tb;
	const struct mmrc_stats_table *rate_stats;
	struct mmrc_rate ratei;
	u16 i, col_idx;
	u32 total_sent_packets = 0;
	u16 caps_size;
	u32 avg_throughput;
	u32 sent;
	u32 sent_success;

	const char *const header[] = {
		"                     --------------Rate--------------", "  Throughput",
		" Probability", " ---------Last---------", " ---------Total---------",
		" ------MPDU-------", " ---------AMPDU---------"
	};
	const char *const cols[] = {
		" BW  ", "Guard", "Evidence", "Selection", "MCS  ", "SS", "Index", "Airtime",
		" Max ", " Avg ", "    Average", "Retry", "Success", " Attempt", "    Success",
		"    Attempt", "Success", " Attempt", "Attempt", "Success", "Evidence", "prob"
	};

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *mors_sta = container_of(mrc_sta, struct morse_sta, rc);
		struct ieee80211_sta *sta = container_of((void *)mors_sta,
							 struct ieee80211_sta, drv_priv);

		tb = mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		seq_puts(file, "\nMorse Micro S1G RC Algorithm Statistics\n");
		seq_printf(file, "Peer: %pM\n", mors_sta->addr);

		for (i = 0; i < ARRAY_SIZE(header); i++)
			seq_printf(file, "%s", header[i]);
		seq_puts(file, "\n");
		for (i = 0; i < ARRAY_SIZE(cols); i++)
			seq_printf(file, "%s ", cols[i]);
		seq_puts(file, "\n");

		for (i = 0; i < caps_size; i++) {
			col_idx = 0;
			ratei = get_rate_row(tb, i);
			if (!validate_rate(tb, &ratei) || i != ratei.index)
				continue;

			rate_stats = &tb->table[ratei.index];

			seq_printf(file, "%2uMHz ", bw_index_to_mhz(ratei.bw));
			col_idx++;
			seq_printf(file, "%*s ", (int)strlen(cols[col_idx++]),
					ratei.guard == MMRC_GUARD_SHORT ? "SGI" : "LGI");
			seq_printf(file, "%*d ", (int)strlen(cols[col_idx++]),
					rate_stats->evidence);

			/* Display rate selection of last update */
			seq_printf(file, "    %c%c%c%c%c ",
				ratei.index == tb->best_tp.index ? 'A' : ' ',
				ratei.index == tb->second_tp.index ? 'B' : ' ',
				ratei.index == tb->baseline.index ? 'C' : ' ',
				ratei.index == tb->best_prob.index ? 'P' : ' ',
				ratei.index == tb->current_lookaround_rate_index ? 'L' : ' '
			);
			col_idx++;

			seq_printf(file, "MCS%-2u ", ratei.rate);
			col_idx++;
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), ratei.ss + 1);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), ratei.index);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					mmrc_calculate_rate_airtime(&ratei,
							AIRTIME_DISPLAY_PACKET_SIZE_BYTES));

			/* Maximum TP for this rate */
			seq_printf(file, "%2u.%02u ",
				   rate_stats->max_throughput / 1000000,
				   rate_stats->max_throughput % 1000000 / 10000);
			col_idx++;

			/* Running average TP for this rate */
			avg_throughput =
			    rate_stats->sum_throughput / rate_stats->avg_throughput_counter;
			seq_printf(file, "%2u.%02u ", avg_throughput / 1000000,
				   avg_throughput % 1000000 / 10000);
			col_idx++;

			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), rate_stats->prob);

			sent = rate_stats->singleton_sent + rate_stats->back_mpdu_sent;
			sent_success = rate_stats->singleton_success
					+ rate_stats->back_mpdu_success;
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					sent - sent_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					sent_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), sent);

			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->total_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->total_sent);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->back_mpdu_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->back_mpdu_sent);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->ampdu_sent);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->ampdu_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->ampdu_evidence);
			seq_printf(file, "%*u\n", (int)strlen(cols[col_idx++]),
					rate_stats->ampdu_prob);
			total_sent_packets += rate_stats->total_sent;
		}

		seq_printf(file, "\n Max A-MSDU length: %d bytes\n",
			   morse_mac_sta_get_max_rc_amsdu_len(sta));
		seq_printf(file,
			   " Amount of packets sent: %u including: %u look-around packets\n\n",
			   total_sent_packets - tb->total_lookaround, tb->total_lookaround);
	}

	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

static int stats_abridged_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct list_head *pos;
	struct mmrc_table *tb;
	const struct mmrc_stats_table *rate_stats;
	struct mmrc_rate ratei;
	u16 i;
	u16 col_idx;
	u32 total_sent_packets = 0;
	u32 sent;
	u32 sent_success;
	s8 rate_idxs[5];

	const char *const header[] = {
		"--------------Rate-------------", " ---Probability--",
		" ---------Last--------", "   ------Total------",
		" ----MPDU-----"
	};
	const char *const cols[] = {
		"SS", "MCS", "BW", "Guard", "Selection", "Index",
		"Evidence", "Average", "Retry", "Success", "Attempt",
		"  Success", "  Attempt", "Success", " Fail"
	};

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *mors_sta = container_of(mrc_sta, struct morse_sta, rc);
		struct ieee80211_sta *sta = container_of((void *)mors_sta,
							 struct ieee80211_sta, drv_priv);

		tb = mrc_sta->tb;

		seq_puts(file, "\nMorse Micro S1G Rate Control Algorithm Statistics\n");
		seq_printf(file, "Peer: %pM\n", mors_sta->addr);
		seq_printf(file, "Cycle: %u\n", tb->cycle_cnt);

		for (i = 0; i < ARRAY_SIZE(header); i++)
			seq_printf(file, "%s", header[i]);
		seq_puts(file, "\n");
		for (i = 0; i < ARRAY_SIZE(cols); i++)
			seq_printf(file, "%s ", cols[i]);
		seq_puts(file, "\n");

		rate_idxs[0] = tb->best_tp.index;
		rate_idxs[1] = tb->second_tp.index;
		rate_idxs[2] = tb->best_prob.index;
		rate_idxs[3] = tb->baseline.index;
		if (tb->num_lookaround_candidates != 0 &&
		    tb->current_lookaround_rate_index != tb->best_tp.index &&
		    tb->current_lookaround_rate_index != tb->second_tp.index &&
		    tb->current_lookaround_rate_index != tb->best_prob.index &&
		    tb->current_lookaround_rate_index != tb->baseline.index)
			rate_idxs[4] = tb->current_lookaround_rate_index;
		else
			rate_idxs[4] = -1;

		for (i = 0; i < ARRAY_SIZE(rate_idxs); i++) {
			if (rate_idxs[i] == -1)
				break;

			col_idx = 0;
			ratei = get_rate_row(tb, rate_idxs[i]);
			if (!validate_rate(tb, &ratei) || rate_idxs[i] != ratei.index)
				continue;

			rate_stats = &tb->table[ratei.index];

			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), ratei.ss + 1);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), ratei.rate);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
				   bw_index_to_mhz(ratei.bw));
			seq_printf(file, "%*s ", (int)strlen(cols[col_idx++]),
					ratei.guard == MMRC_GUARD_SHORT ? "SGI" : "LGI");

			/* Display rate selection of last update */
			seq_printf(file, "    %c%c%c%c%c ",
				ratei.index == tb->current_lookaround_rate_index ? 'L' : ' ',
				ratei.index == tb->best_tp.index ? 'A' : ' ',
				ratei.index == tb->second_tp.index ? 'B' : ' ',
				ratei.index == tb->best_prob.index ? 'C' : ' ',
				ratei.index == tb->baseline.index ? 'D' : ' '
			);

			col_idx++;

			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), ratei.index);

			seq_printf(file, "%*d ", (int)strlen(cols[col_idx++]),
					rate_stats->evidence);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), rate_stats->prob);

			sent = rate_stats->singleton_sent + rate_stats->back_mpdu_sent;
			sent_success = rate_stats->singleton_success
					+ rate_stats->back_mpdu_success;
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					sent - sent_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					sent_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]), sent);

			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->total_success);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->total_sent);
			seq_printf(file, "%*u ", (int)strlen(cols[col_idx++]),
					rate_stats->back_mpdu_success);
			seq_printf(file, "%*u\n", (int)strlen(cols[col_idx++]),
					rate_stats->back_mpdu_sent);
			total_sent_packets += rate_stats->total_sent;
		}

		seq_printf(file, "\nMax A-MSDU length: %d bytes\n",
			   morse_mac_sta_get_max_rc_amsdu_len(sta));
		seq_printf(file,
			   "Number of packets sent: %u including %u look-around packets\n\n",
			   total_sent_packets, tb->total_lookaround);
	}

	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

static int stats_csv_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct list_head *pos;
	struct mmrc_table *tb;
	const struct mmrc_stats_table *rate_stats;
	struct mmrc_rate ratei;
	u16 i;
	u16 caps_size;
	u32 avg_throughput;
	u32 sent;
	u32 sent_success;

	seq_puts(file, "bandwidth,guard,evidence,rate_selection,mcs,ss,index,airtime,");
	seq_puts(file, "max_throughput,average_throughput,probability,last_retry,last_success,");
	seq_puts(file,
		 "last_attempts,total_success,total_attempts,mpdu_success,mpdu_attempts,");
	seq_puts(file,
		 "ampdu_attempts,ampdu_success,ampdu_evidence,ampdu_prob,mac_address\n");

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *sta = container_of(mrc_sta, struct morse_sta, rc);

		tb = mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		for (i = 0; i < caps_size; i++) {
			ratei = get_rate_row(tb, i);
			if (!validate_rate(tb, &ratei) || i != ratei.index)
				continue;

			rate_stats = &tb->table[ratei.index];

			seq_printf(file, "%uMHz", bw_index_to_mhz(ratei.bw));
			seq_printf(file, ",%cGI", ratei.guard == MMRC_GUARD_SHORT ? 'S' : 'L');
			seq_printf(file, ",%d,", rate_stats->evidence);

			/* Display rate selection of last update */
			if (ratei.index == tb->best_tp.index)
				seq_puts(file, "A");
			if (ratei.index == tb->second_tp.index)
				seq_puts(file, "B");
			if (ratei.index == tb->baseline.index)
				seq_puts(file, "C");
			if (ratei.index == tb->best_prob.index)
				seq_puts(file, "P");
			if (ratei.index == tb->current_lookaround_rate_index)
				seq_puts(file, "L");

			seq_printf(file, ",MCS%u,%u", ratei.rate, ratei.ss + 1);
			seq_printf(file, ",%u", ratei.index);
			seq_printf(file, ",%u",
				   mmrc_calculate_rate_airtime(&ratei,
							       AIRTIME_DISPLAY_PACKET_SIZE_BYTES));

			/* Maximum TP for this rate */
			seq_printf(file, ",%u.%u",
				   rate_stats->max_throughput / 1000000,
				   rate_stats->max_throughput % 1000000 / 10000);
			/* Average TP for this rate */
			avg_throughput =
			    rate_stats->sum_throughput / rate_stats->avg_throughput_counter;
			seq_printf(file, ",%u.%u", avg_throughput / 1000000,
				   avg_throughput % 1000000 / 10000);

			seq_printf(file, ",%u", rate_stats->prob);
			sent = rate_stats->singleton_sent + rate_stats->back_mpdu_sent;
			sent_success = rate_stats->singleton_success
					+ rate_stats->back_mpdu_success;
			seq_printf(file, ",%u", sent - sent_success);
			seq_printf(file, ",%u", sent_success);
			seq_printf(file, ",%u", sent);
			seq_printf(file, ",%u", rate_stats->total_success);
			seq_printf(file, ",%u", rate_stats->total_sent);
			seq_printf(file, ",%u", rate_stats->back_mpdu_success);
			seq_printf(file, ",%u", rate_stats->back_mpdu_sent);
			seq_printf(file, ",%u", rate_stats->ampdu_sent);
			seq_printf(file, ",%u", rate_stats->ampdu_success);
			seq_printf(file, ",%u", rate_stats->ampdu_evidence);
			seq_printf(file, ",%u", rate_stats->ampdu_prob);
			seq_printf(file, ",%pM\n", sta->addr);
		}
	}

	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

#ifdef CONFIG_MORSE_DEBUGFS
/*
 * mmrc_feedback_csv: a streaming CSV log of every mmrc_feedback() event.
 *
 * morse_rc_log_feedback() pushes one entry per call into a ring buffer; reading
 * the debugfs file drains the buffer, emitting one CSV row per used rate in each
 * event's retry chain. Unlike a seq_file snapshot, this is consumed as it is read
 * (each row is returned exactly once), so a capture such as
 *
 *     cat /sys/kernel/debug/ieee80211/phyX/morse/mmrc_feedback_csv > feedback.csv
 *
 * (or a looping cat) records the raw per-frame feedback feeding mmrc_table_csv.
 */
#define MMRC_FEEDBACK_CSV_HEADER \
	"timestamp_ns,mac_address,index,bandwidth,guard,mcs,ss,attempts,aggs,aggs_dropped,acked\n"

/* Large enough for the header or for MMRC_MAX_CHAIN_LENGTH formatted CSV rows. */
#define MMRC_FEEDBACK_CSV_BUF_SZ 512

/* Per-open reader state: holds the CSV text of the current header/event so it can
 * be handed to userspace across as many read() calls as the user's buffer needs.
 */
struct mmrc_feedback_reader {
	struct morse *mors;
	bool header_sent;
	size_t len;	/* valid bytes in buf */
	size_t pos;	/* bytes already copied to userspace */
	char buf[MMRC_FEEDBACK_CSV_BUF_SZ];
};

void morse_rc_log_feedback(struct morse *mors, const u8 *addr, const struct mmrc_rate_table *rates,
			   u8 per_attempt_ampdu_info, u8 per_attempt_missing_block_ack, bool acked)
{
	struct mmrc_feedback_log_entry entry;
	int ret;

	/* Caller holds mors->mrc.lock. Drop the oldest event when full so the most
	 * recent feedback is retained (and so the pop here is mutually exclusive with
	 * the reader, which takes the same lock).
	 */
	if (kfifo_is_full(&mors->mrc.feedback_log)) {
		struct mmrc_feedback_log_entry drop;

		ret = kfifo_get(&mors->mrc.feedback_log, &drop);
	}

	entry.timestamp_ns = ktime_get_ns();
	memcpy(entry.addr, addr, ETH_ALEN);
	entry.acked = acked;
	entry.per_attempt_ampdu_info = per_attempt_ampdu_info;
	entry.per_attempt_missing_block_ack = per_attempt_missing_block_ack;
	entry.rates = *rates;

	ret = kfifo_put(&mors->mrc.feedback_log, entry);
	(void)ret;
}

/* Format one feedback event into CSV (one row per used rate). Returns bytes written. */
static size_t mmrc_feedback_format_entry(char *buf, size_t size,
					 const struct mmrc_feedback_log_entry *e)
{
	size_t n = 0;
	int i, j;
	int count = 0;

	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		const struct mmrc_rate *r = &e->rates.rates[i];
		u8 aggs = 0;
		u8 aggs_dropped = 0;

		/* The chain terminates at the first rate with no attempts. */
		if (!r->attempts)
			break;

		for (j = 0; j < r->attempts; j++) {
			aggs += MMRC_BIT(e->per_attempt_ampdu_info, count);
			aggs_dropped += MMRC_BIT(e->per_attempt_missing_block_ack, count);
			count++;
		}

		n += scnprintf(buf + n, size - n,
			       "%llu,%pM,%u,%uMHz,%cGI,MCS%u,%u,%u,%u,%u,%u\n",
			       e->timestamp_ns, e->addr, r->index,
			       bw_index_to_mhz(r->bw),
			       r->guard == MMRC_GUARD_SHORT ? 'S' : 'L',
			       r->rate, r->ss + 1, r->attempts,
			       aggs, aggs_dropped, e->acked);
	}

	return n;
}

static int mmrc_feedback_csv_open(struct inode *inode, struct file *file)
{
	struct mmrc_feedback_reader *rd = kzalloc(sizeof(*rd), GFP_KERNEL);

	if (!rd)
		return -ENOMEM;

	rd->mors = inode->i_private;
	file->private_data = rd;
	return 0;
}

static int mmrc_feedback_csv_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t mmrc_feedback_csv_read(struct file *file, char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	struct mmrc_feedback_reader *rd = file->private_data;
	struct morse *mors = rd->mors;
	size_t copied = 0;

	/* Emit the column header once, at the start of each open. */
	if (!rd->header_sent && rd->pos >= rd->len) {
		rd->len = scnprintf(rd->buf, sizeof(rd->buf), MMRC_FEEDBACK_CSV_HEADER);
		rd->pos = 0;
		rd->header_sent = true;
	}

	while (copied < count) {
		size_t avail = rd->len - rd->pos;

		if (avail) {
			size_t chunk = min(avail, count - copied);

			if (copy_to_user(ubuf + copied, rd->buf + rd->pos, chunk))
				return copied ? (ssize_t)copied : -EFAULT;
			rd->pos += chunk;
			copied += chunk;
			*ppos += chunk;
			continue;
		}

		/* Current text fully sent: pull the next event off the ring. */
		do {
			struct mmrc_feedback_log_entry entry;
			bool got;

			spin_lock_bh(&mors->mrc.lock);
			got = kfifo_get(&mors->mrc.feedback_log, &entry);
			spin_unlock_bh(&mors->mrc.lock);

			if (!got)
				return copied;	/* Drained; EOF until more arrives. */

			rd->len = mmrc_feedback_format_entry(rd->buf, sizeof(rd->buf), &entry);
			rd->pos = 0;
		} while (rd->len == 0);
	}

	return copied;
}

static const struct file_operations mmrc_feedback_csv_fops = {
	.owner = THIS_MODULE,
	.open = mmrc_feedback_csv_open,
	.read = mmrc_feedback_csv_read,
	.release = mmrc_feedback_csv_release,
#if KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE
	.llseek = no_llseek,
#endif
};
#endif /* CONFIG_MORSE_DEBUGFS */

static ssize_t set_fixed_rate(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	u8 value;
	struct mmrc_rate fixed_rate;
	struct list_head *pos;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);

		fixed_rate = get_rate_row(mrc_sta->tb, value);
		mmrc_set_fixed_rate(mrc_sta->tb, fixed_rate);
	}
	spin_unlock_bh(&mors->mrc.lock);
	return count;
}

static const struct file_operations mmrc_fixed_rate = {
	.open = simple_open,
#if KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE
	.llseek = no_llseek,
#endif
	.write = set_fixed_rate,
};

void mmrc_s1g_add_mesh_debugfs(struct morse *mors)
{
	debugfs_create_devm_seqfile(mors->dev, "mesh_stats", mors->debug.debugfs_phy,
				    mesh_stats_read);
}

void mmrc_s1g_add_sta_debugfs(struct morse *mors)
{
	debugfs_create_devm_seqfile(mors->dev, "mmrc_table", mors->debug.debugfs_phy, stats_read);
	debugfs_create_devm_seqfile(mors->dev, "mmrc_abridged_table", mors->debug.debugfs_phy,
				    stats_abridged_read);
	debugfs_create_devm_seqfile(mors->dev, "mmrc_table_csv", mors->debug.debugfs_phy,
				    stats_csv_read);
#ifdef CONFIG_MORSE_DEBUGFS
	debugfs_create_file("mmrc_feedback_csv", 0400, mors->debug.debugfs_phy, mors,
			    &mmrc_feedback_csv_fops);
#endif

	debugfs_create_file("fixed_rate", 0600, mors->debug.debugfs_phy, mors, &mmrc_fixed_rate);
}
