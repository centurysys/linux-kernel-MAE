/*
 * Copyright 2022 Morse Micro.
 *
 */

#include <linux/types.h>
#include <linux/debugfs.h>
#include "morse.h"
#include "rc.h"
#include "mmrc-submodule/src/core/mmrc.h"

static int
stats_read(struct seq_file *file, void *data)
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

		tb = &mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		seq_puts(file, "\nMorse Micro S1G RC Algorithm Statistics:\n");
		seq_printf(file, "Peer %pM\n", sta->addr);
		seq_puts(file, "   bw   guard evid rate_sel mcs#/ss index airtime TP(max)  TP(avg) ");
		seq_puts(file, "prob last_rty last_suc last_att tot_suc tot_att\n");
		for (i = 0; i < caps_size; i++) {
			ratei = get_rate_row(tb, i);
			if (!validate_rate(&ratei) || (i != ratei.index))
				continue;

			rate_stats = &tb->table[ratei.index];

			bw = ratei.bw == MMRC_BW_2MHZ ? 2 :
				 ratei.bw == MMRC_BW_4MHZ ? 4 :
				 ratei.bw == MMRC_BW_8MHZ ? 8 :
				 ratei.bw == MMRC_BW_16MHZ ? 16 : 1;
			seq_printf(file, " %2uMHz ", bw);
			seq_printf(file, "  %cGI",
						  ratei.guard == MMRC_GUARD_SHORT ? 'S' : 'L');
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

			seq_printf(file, "    MCS%-2u/%1u", ratei.rate, ratei.ss + 1);
			seq_printf(file, " %3u", ratei.index);
			seq_printf(file, "    %5u", get_tx_time(&ratei));

			/* Maximum TP for this rate */
			seq_printf(file, " %3u.%02u",
						  rate_stats->max_throughput / 1000000,
						  rate_stats->max_throughput % 1000000 / 10000);
			/* Running average TP for this rate */
			avg_throughput = rate_stats->sum_throughput / rate_stats->avg_throughput_counter;
			seq_printf(file, "   %3u.%02u",
						  avg_throughput / 1000000,
						  avg_throughput % 1000000 / 10000);

			seq_printf(file, "   %3u", rate_stats->prob);
			seq_printf(file, "    %3u",
						  rate_stats->sent - rate_stats->sent_success);
			seq_printf(file, "      %3u", rate_stats->sent_success);
			seq_printf(file, "      %3u", rate_stats->sent);
			seq_printf(file, "       %-7u", rate_stats->total_success);
			seq_printf(file, "  %-7u\n", rate_stats->total_sent);
			total_sent_packets += rate_stats->total_sent;
		}
		seq_printf(file,
			"\n Amount of packets sent: %u including: %u look-around packets\n\n",
				total_sent_packets - tb->total_lookaround, tb->total_lookaround);
	}

	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

static int
stats_csv_read(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct list_head *pos;
	struct mmrc_table *tb;
	const struct mmrc_stats_table *rate_stats;
	struct mmrc_rate ratei;
	u16 i, bw;
	u16 caps_size;
	u32 avg_throughput;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		struct morse_sta *sta = container_of(mrc_sta, struct morse_sta, rc);

		tb = &mrc_sta->tb;
		caps_size = rows_from_sta_caps(&tb->caps);

		for (i = 0; i < caps_size; i++)	{
			ratei = get_rate_row(tb, i);
			rate_stats = &tb->table[ratei.index];
			if (!validate_rate(&ratei))
				continue;

			bw = ratei.bw == MMRC_BW_2MHZ ? 2 :
				 ratei.bw == MMRC_BW_4MHZ ? 4 :
				 ratei.bw == MMRC_BW_8MHZ ? 8 :
				 ratei.bw == MMRC_BW_16MHZ ? 16 : 1;
			seq_printf(file, "%uMHz", bw);
			seq_printf(file, ",%cGI",
				   ratei.guard == MMRC_GUARD_SHORT ? 'S' : 'L');
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

			seq_printf(file, ",MCS%u/%u", ratei.rate, ratei.ss + 1);
			seq_printf(file, ",%u", ratei.index);
			seq_printf(file, ",%u", get_tx_time(&ratei));

			/* Maximum TP for this rate */
			seq_printf(file, ",%u.%u",
				   rate_stats->max_throughput / 1000000,
				   rate_stats->max_throughput % 1000000 / 10000);
			/* Average TP for this rate */
			avg_throughput = rate_stats->sum_throughput / rate_stats->avg_throughput_counter;
			seq_printf(file, ",%u.%u",
				   avg_throughput / 1000000,
				   avg_throughput % 1000000 / 10000);

			seq_printf(file, ",%u", rate_stats->prob);
			seq_printf(file, ",%u",
				   rate_stats->sent - rate_stats->sent_success);
			seq_printf(file, ",%u", rate_stats->sent_success);
			seq_printf(file, ",%u", rate_stats->sent);
			seq_printf(file, ",%u", rate_stats->total_success);
			seq_printf(file, ",%u", rate_stats->total_sent);
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

		fixed_rate = get_rate_row(&mrc_sta->tb, value);
		mmrc_set_fixed_rate(&mrc_sta->tb, fixed_rate);
	}
	spin_unlock_bh(&mors->mrc.lock);
	return count;
}

static const struct file_operations mmrc_fixed_rate = {
	.open	= simple_open,
	.llseek	= no_llseek,
	.write	= set_fixed_rate,
};

void
mmrc_s1g_add_sta_debugfs(struct morse *mors)
{
	debugfs_create_devm_seqfile(mors->dev, "mmrc_table", mors->debug.debugfs_phy,
		stats_read);
	debugfs_create_devm_seqfile(mors->dev, "mmrc_table_csv", mors->debug.debugfs_phy,
		stats_csv_read);

	debugfs_create_file("fixed_rate", 0600, mors->debug.debugfs_phy, mors,
						&mmrc_fixed_rate);
}

