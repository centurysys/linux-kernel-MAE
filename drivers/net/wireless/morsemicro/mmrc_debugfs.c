/*
 * Copyright 2022 Morse Micro.
 *
 */

#include <linux/types.h>
#include <linux/debugfs.h>
#include "morse.h"
#include "mac.h"
#include "rc.h"
#include "mmrc-submodule/src/core/mmrc.h"

void morse_print_station_stats(struct morse_sta *msta, struct seq_file *file)
{
	u32 last_tx_rate_kbps;
	u32 last_rx_rate_kbps;

	msta->last_sta_rx_rate.guard = morse_ratecode_sgi_get(msta->last_rx_status.morse_ratecode);
	msta->last_sta_rx_rate.flags = morse_ratecode_rts_get(msta->last_rx_status.morse_ratecode);
	msta->last_sta_rx_rate.rate =
	    morse_ratecode_mcs_index_get(msta->last_rx_status.morse_ratecode);
	msta->last_sta_rx_rate.ss =
	    morse_ratecode_nss_index_get(msta->last_rx_status.morse_ratecode);
	msta->last_sta_rx_rate.bw =
	    morse_ratecode_bw_index_get(msta->last_rx_status.morse_ratecode);

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
	seq_printf(file, "    RSSI: %d dBm\n", (s8)le16_to_cpu(msta->last_rx_status.rssi));
}

static int mesh_stats_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	int vif_id;

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct list_head *morse_sta_list;
		struct list_head *pos;
		struct morse_vif *mors_if;

		if (!vif || vif->type != NL80211_IFTYPE_MESH_POINT)
			continue;

		mors_if = ieee80211_vif_to_morse_vif(vif);
		morse_sta_list = &mors_if->ap->stas;

		seq_printf(file, "%s: Peer Stats\n", morse_vif_name(vif));
		rcu_read_lock();
		list_for_each(pos, morse_sta_list) {
			struct morse_sta *msta = list_entry(pos, struct morse_sta, list);

			morse_print_station_stats(msta, file);
		}
		rcu_read_unlock();
	}
	return 0;
}

static int stats_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct list_head *pos;
	struct mmrc_table *tb;
	const struct mmrc_stats_table *rate_stats;
	struct mmrc_rate ratei;
	u16 i, bw;
	u32 total_sent_packets = 0;
	u16 caps_size;
	u32 avg_throughput;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *sta = container_of(mrc_sta, struct morse_sta, rc);

		tb = mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		seq_puts(file, "\nMorse Micro S1G RC Algorithm Statistics:\n");
		seq_printf(file, "Peer %pM\n", sta->addr);
		seq_puts(file,
			 "   bw   guard evid rate_sel mcs#/ss index airtime TP(max)  TP(avg) ");
		seq_puts(file,
			 "prob last_rty last_suc last_att  tot_suc  tot_att mpdu_suc mpdu_fail\n");
		for (i = 0; i < caps_size; i++) {
			ratei = get_rate_row(tb, i);
			if (!validate_rate(&ratei) || i != ratei.index)
				continue;

			rate_stats = &tb->table[ratei.index];

			bw = ratei.bw == MMRC_BW_2MHZ ? 2 :
			    ratei.bw == MMRC_BW_4MHZ ? 4 :
			    ratei.bw == MMRC_BW_8MHZ ? 8 : ratei.bw == MMRC_BW_16MHZ ? 16 : 1;
			seq_printf(file, " %2uMHz ", bw);
			seq_printf(file, "  %cGI", ratei.guard == MMRC_GUARD_SHORT ? 'S' : 'L');
			seq_printf(file, "   %-4d ", rate_stats->evidence);

			/* Display rate selection of last update */
			if (ratei.index == tb->best_tp.index)
				seq_puts(file, "A");
			else
				seq_puts(file, " ");
			if (ratei.index == tb->second_tp.index)
				seq_puts(file, "B");
			else
				seq_puts(file, " ");
			if (ratei.index == tb->baseline.index)
				seq_puts(file, "C");
			else
				seq_puts(file, " ");
			if (ratei.index == tb->best_prob.index)
				seq_puts(file, "P");
			else
				seq_puts(file, " ");
			if (ratei.index == tb->current_lookaround_rate_index)
				seq_puts(file, "L");
			else
				seq_puts(file, " ");

			seq_printf(file, "   MCS%-2u/%1u", ratei.rate, ratei.ss + 1);
			seq_printf(file, "%4u", ratei.index);
			seq_printf(file, "%9u", get_tx_time(&ratei));

			/* Maximum TP for this rate */
			seq_printf(file, "%4u.%02u",
				   rate_stats->max_throughput / 1000000,
				   rate_stats->max_throughput % 1000000 / 10000);
			/* Running average TP for this rate */
			avg_throughput =
			    rate_stats->sum_throughput / rate_stats->avg_throughput_counter;
			seq_printf(file, "%6u.%02u", avg_throughput / 1000000,
				   avg_throughput % 1000000 / 10000);

			seq_printf(file, "%6u", rate_stats->prob);
			seq_printf(file, "%7u", rate_stats->sent - rate_stats->sent_success);
			seq_printf(file, "%9u", rate_stats->sent_success);
			seq_printf(file, "%9u", rate_stats->sent);
			seq_printf(file, "%11u", rate_stats->total_success);
			seq_printf(file, "%9u", rate_stats->total_sent);
			seq_printf(file, "%7u", rate_stats->back_mpdu_success);
			seq_printf(file, "%9u\n", rate_stats->back_mpdu_failure);
			total_sent_packets += rate_stats->total_sent;
		}
		seq_printf(file,
			   "\n Amount of packets sent: %u including: %u look-around packets\n\n",
			   total_sent_packets - tb->total_lookaround, tb->total_lookaround);
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
	u16 i, bw;
	u16 caps_size;
	u32 avg_throughput;

	seq_puts(file, "bandwidth,guard,evidence,rate_selection,mcs,ss,index,airtime,");
	seq_puts(file, "max_throughput,average_throughput,probability,last_retry,last_success,");
	seq_puts(file,
		 "last_attempts,total_success,total_attempts,mpdu_success,mpdu_failures,mac_address\n");

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *sta = container_of(mrc_sta, struct morse_sta, rc);

		tb = mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		for (i = 0; i < caps_size; i++) {
			ratei = get_rate_row(tb, i);
			if (!validate_rate(&ratei) || i != ratei.index)
				continue;

			rate_stats = &tb->table[ratei.index];

			bw = ratei.bw == MMRC_BW_2MHZ ? 2 :
			    ratei.bw == MMRC_BW_4MHZ ? 4 :
			    ratei.bw == MMRC_BW_8MHZ ? 8 : ratei.bw == MMRC_BW_16MHZ ? 16 : 1;
			seq_printf(file, "%uMHz", bw);
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
			seq_printf(file, ",%u", get_tx_time(&ratei));

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
			seq_printf(file, ",%u", rate_stats->sent - rate_stats->sent_success);
			seq_printf(file, ",%u", rate_stats->sent_success);
			seq_printf(file, ",%u", rate_stats->sent);
			seq_printf(file, ",%u", rate_stats->total_success);
			seq_printf(file, ",%u", rate_stats->total_sent);
			seq_printf(file, ",%u", rate_stats->back_mpdu_success);
			seq_printf(file, ",%u", rate_stats->back_mpdu_failure);
			seq_printf(file, ",%pM\n", sta->addr);
		}
	}

	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

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
	.llseek = no_llseek,
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
	debugfs_create_devm_seqfile(mors->dev, "mmrc_table_csv", mors->debug.debugfs_phy,
				    stats_csv_read);

	debugfs_create_file("fixed_rate", 0600, mors->debug.debugfs_phy, mors, &mmrc_fixed_rate);
}
