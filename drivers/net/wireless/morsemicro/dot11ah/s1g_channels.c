/*
 * Copyright 2017-2024 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/crc32.h>
#include <linux/ieee80211.h>
#include "s1g_ieee80211.h"

#include "dot11ah.h"
#include "tim.h"
#include "../morse.h"
#include "s1g_channels_rules.c"
#include "channel_alphas.h"

#define CHANNEL_FLAGS_BW_MASK 0x7c000

/**
 * Morse dot11ah S1G channel operations.
 */
struct morse_dot11ah_ch_ops {
	int (*prim_1mhz_channel_loc_to_idx)(int op_bw_mhz,
					    int pr_bw_mhz,
					    int pr_chan_num,
					    int chan_centre_freq_num,
					    int chan_loc);
	int (*calculate_primary_s1g)(int op_bw_mhz,
				     int pr_bw_mhz,
				     int chan_centre_freq_num,
				     int pr_1mhz_chan_idx);
	int (*s1g_op_chan_pri_chan_to_5g)(int s1g_op_chan,
					  int s1g_pri_chan);
	int (*get_pri_1mhz_chan)(int primary_channel,
				 int primary_channel_width_mhz,
				 bool pri_1_mhz_loc_upper);
	int (*transform_overlapping_5g_chan)(int chan_5g);
};

/**
 * Morse dot11ah S1G channel list.
 *
 * Includes mapping to 5G channels
 */
struct morse_dot11ah_ch_map {
	char alpha[3];
	const struct morse_dot11ah_ch_ops *ops;
	u32 num_mapped_channels;
	struct morse_dot11ah_channel *s1g_channels;
	u32 channelization_scheme;
};

static int prim_1mhz_channel_loc_to_idx_default(int op_bw_mhz, int pr_bw_mhz, int pr_chan_num,
						int chan_centre_freq_num, int chan_loc)
{
	switch (op_bw_mhz) {
	case 1:
		return 0;
	case 2:
		return chan_loc;
	case 4:
		if (pr_bw_mhz == 1)
			return (((pr_chan_num - chan_centre_freq_num) + 3) / 2);
		else
			return (((pr_chan_num - chan_centre_freq_num) + 2) / 2) + chan_loc;
	case 8:
		if (pr_bw_mhz == 1)
			return (((pr_chan_num - chan_centre_freq_num) + 7) / 2);
		else
			return (((pr_chan_num - chan_centre_freq_num) + 6) / 2) + chan_loc;
	default:
		return -ENOENT;
	}
}

static int prim_1mhz_channel_loc_to_idx_jp(int op_bw_mhz, int pr_bw_mhz, int pr_chan_num,
					   int chan_centre_freq_num, int chan_loc)
{
	switch (op_bw_mhz) {
	case 1:
		return 0;
	case 2:
		return chan_loc;
	case 4:
		if (chan_centre_freq_num == 36) {
			return pr_chan_num == 13 ? 0 :
			       pr_chan_num == 15 ? 1 :
			       pr_chan_num == 17 ? 2 :
			       pr_chan_num == 19 ? 3 :
			       pr_chan_num == 2  ? 0 + chan_loc :
			       pr_chan_num == 6  ? 2 + chan_loc : -EINVAL;
		} else if (chan_centre_freq_num == 38) {
			return pr_chan_num == 15 ? 0 :
			       pr_chan_num == 17 ? 1 :
			       pr_chan_num == 19 ? 2 :
			       pr_chan_num == 21 ? 3 :
			       pr_chan_num == 4  ? 0 + chan_loc :
			       pr_chan_num == 8  ? 2 + chan_loc : -EINVAL;
		} else {
			return -EINVAL;
		}
	case 8:
	default:
		return -EINVAL;
	}
}

static int prim_1mhz_channel_loc_to_idx_au(int op_bw_mhz, int pr_bw_mhz, int pr_chan_num,
					    int chan_centre_freq_num, int chan_loc)
{
	if (chan_centre_freq_num > 50) {
		switch (op_bw_mhz) {
		case 4:
			if (pr_bw_mhz == 1)
				return (((pr_chan_num - chan_centre_freq_num) + 19) / 2);
			else
				return (((pr_chan_num - chan_centre_freq_num) + 18) / 2) + chan_loc;
		case 8:
			if (pr_bw_mhz == 1)
				return (((pr_chan_num - chan_centre_freq_num) + 23) / 2);
			else
				return (((pr_chan_num - chan_centre_freq_num) + 22) / 2) + chan_loc;
		default:
				return -ENOENT;
		}
	} else {
		return prim_1mhz_channel_loc_to_idx_default(op_bw_mhz, pr_bw_mhz, pr_chan_num,
							chan_centre_freq_num, chan_loc);
	}
}

static int calculate_primary_s1g_channel_default(int op_bw_mhz, int pr_bw_mhz,
						 int chan_centre_freq_num, int pr_1mhz_chan_idx)
{
	int chan_loc = (pr_1mhz_chan_idx % 2);

	switch (op_bw_mhz) {
	case 1:
		return chan_centre_freq_num;
	case 2:
		if (pr_bw_mhz == 1)
			return chan_centre_freq_num + ((chan_loc == 0) ? -1 : 1);
		else
			return chan_centre_freq_num;
	case 4:
		if (pr_bw_mhz == 1)
			return ((2 * pr_1mhz_chan_idx) - 3) + chan_centre_freq_num;
		else
			return ((pr_1mhz_chan_idx / 2) * 4) - 2 + chan_centre_freq_num;
	case 8:
		if (pr_bw_mhz == 1)
			return ((2 * pr_1mhz_chan_idx) - 7) + chan_centre_freq_num;
		else
			return ((pr_1mhz_chan_idx / 2) * 4) - 6 + chan_centre_freq_num;
	}

	return -EINVAL;
}

static int calculate_primary_s1g_channel_jp(int op_bw_mhz, int pr_bw_mhz, int chan_centre_freq_num,
					    int pr_1mhz_chan_idx)
{
	int chan_loc = (pr_1mhz_chan_idx % 2);
	int offset;

	switch (op_bw_mhz) {
	case 1:
		return chan_centre_freq_num;
	case 2:
		/* The JP850 2MHz channel is numbered contiguously with its 1MHz primaries
		 * (94 and 96), so it follows the default derivation rather than the offsets
		 * used by the JP 920MHz channels.
		 */
		if (chan_centre_freq_num == 95) {
			if (pr_bw_mhz == 1)
				return chan_centre_freq_num + ((chan_loc == 0) ? -1 : 1);
			if (pr_bw_mhz == 2)
				return chan_centre_freq_num;
			return -EINVAL;
		}
		if (pr_bw_mhz == 1) {
			offset = pr_1mhz_chan_idx ? 13 : 11;
			return (chan_centre_freq_num + offset);
		} else {
			return chan_centre_freq_num;
		}
	case 4:
		if (pr_bw_mhz == 1) {
			offset = pr_1mhz_chan_idx == 0 ? 23 :
				 pr_1mhz_chan_idx == 1 ? 21 :
				 pr_1mhz_chan_idx == 2 ? 19 :
				 pr_1mhz_chan_idx == 3 ? 17 : -1;
		} else {
			offset = ((pr_1mhz_chan_idx == 0) || (pr_1mhz_chan_idx == 1)) ? 34 :
				 ((pr_1mhz_chan_idx == 2) || (pr_1mhz_chan_idx == 3)) ? 30 : -1;
		}
		return (offset > 0) ? (chan_centre_freq_num - offset) : -EINVAL;
	default:
		return -ENOENT;
	}
}

static int calculate_primary_s1g_channel_au(int op_bw_mhz, int pr_bw_mhz,
					     int chan_centre_freq_num, int pr_1mhz_chan_idx)
{
	if (chan_centre_freq_num > 50) {
		switch (op_bw_mhz) {
		case 4:
			if (pr_bw_mhz == 1)
				return ((2 * pr_1mhz_chan_idx) - 19) + chan_centre_freq_num;
			else
				return ((pr_1mhz_chan_idx / 2) * 4) - 18 + chan_centre_freq_num;
		case 8:
			if (pr_bw_mhz == 1)
				return ((2 * pr_1mhz_chan_idx) - 23) + chan_centre_freq_num;
			else
				return ((pr_1mhz_chan_idx / 2) * 4) - 22 + chan_centre_freq_num;
		}
	} else {
		return calculate_primary_s1g_channel_default(op_bw_mhz, pr_bw_mhz,
				chan_centre_freq_num, pr_1mhz_chan_idx);
	}

	return -EINVAL;
}

static int s1g_op_chan_pri_chan_to_5g_default(int s1g_op_chan, int s1g_pri_chan)
{
	return morse_dot11ah_s1g_chan_to_5g_chan(s1g_pri_chan);
}

static int s1g_op_chan_pri_chan_to_5g_jp(int s1g_op_chan, int s1g_pri_chan)
{
	int ht20mhz_offset;
	/* In the JP regulatory, some primary channels have duplicate
	 * entries so to get the correct 5g value, the op chan
	 * must be considered.
	 */
	if ((s1g_op_chan == 4 ||
	     s1g_op_chan == 8 ||
	     s1g_op_chan == 38) &&
	    s1g_pri_chan != 21) {
		ht20mhz_offset = 12;
	} else {
		ht20mhz_offset = 0;
	}

	return morse_dot11ah_s1g_chan_to_5g_chan(s1g_pri_chan) + ht20mhz_offset;
}

static int s1g_op_chan_pri_chan_to_5g_au(int s1g_op_chan, int s1g_pri_chan)
{
	int htchan_offset = 0;
	int chan_5g;

	/* In the AU regulatory, some channels have duplicate entries.
	 * The operating channel must be considered while mapping S1G channels
	 * to 5G channels
	 */
	if (s1g_op_chan == 43 && s1g_pri_chan <= 42)
		htchan_offset = 48;

	if (s1g_op_chan == 51)
		htchan_offset = 105;

	if (s1g_op_chan == 55 || s1g_op_chan == 59) {
		if (s1g_pri_chan <= 42)
			htchan_offset = 105;
		else
			htchan_offset = 57;
	}

	chan_5g = morse_dot11ah_s1g_chan_to_5g_chan(s1g_pri_chan) + htchan_offset;
	return chan_5g;
}

static int transform_overlapping_5g_chan_jp(int chan_5g)
{
	int ht20mhz_offset = 0;

	/* In the JP regulatory, some S1G primary channels map to multiple 5G channels.
	 * Only the first mapping is maintained, so transform the overlapping channels back
	 * to the known set.
	 */
	if (chan_5g == 52 || chan_5g == 56 || chan_5g == 60)
		ht20mhz_offset = 12;

	return chan_5g - ht20mhz_offset;
}

static int transform_overlapping_5g_chan_au(int chan_5g)
{
	int htchan_offset = 0;

	/* In the AU regulatory, some S1G primary channels map to multiple 5G channels.
	 * Only the first mapping is maintained, so transform the overlapping channels back
	 * to the known set.
	 */
	if (chan_5g >= 100 && chan_5g <= 112)
		htchan_offset = 48;

	if (chan_5g >= 149 && chan_5g <= 169 && chan_5g != 155 && chan_5g != 163)
		htchan_offset = 105;

	if (chan_5g >= 173 && chan_5g <= 177)
		htchan_offset = 57;

	return chan_5g - htchan_offset;
}

static int get_pri_1mhz_chan_default(int primary_channel,
			      int primary_channel_width_mhz, bool pri_1_mhz_loc_upper)
{
	if (primary_channel_width_mhz == 2)
		return primary_channel += pri_1_mhz_loc_upper ? 1 : -1;
	else if (primary_channel_width_mhz == 1)
		return primary_channel;
	else
		return -EINVAL;
}

static int get_pri_1mhz_chan_jp(int primary_channel,
			     int primary_channel_width_mhz, bool pri_1_mhz_loc_upper)
{
	if (primary_channel_width_mhz == 2) {
		switch (primary_channel) {
		case 2:
			return pri_1_mhz_loc_upper ? 15 : 13;
		case 4:
			return pri_1_mhz_loc_upper ? 17 : 15;
		case 6:
			return pri_1_mhz_loc_upper ? 19 : 17;
		case 8:
			return pri_1_mhz_loc_upper ? 21 : 19;
		case 95:
			return pri_1_mhz_loc_upper ? 96 : 94;
		default:
			return -ENOENT;
		}
	} else if (primary_channel_width_mhz == 1) {
		return primary_channel;
	} else {
		return -EINVAL;
	}
}

static const struct morse_dot11ah_ch_ops mors_ch_ops_default = {
	.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
	.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
	.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
	.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
	.transform_overlapping_5g_chan = NULL,
};

static const struct morse_dot11ah_ch_ops mors_ch_ops_au = {
	.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_au,
	.calculate_primary_s1g = &calculate_primary_s1g_channel_au,
	.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_au,
	.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
	.transform_overlapping_5g_chan = &transform_overlapping_5g_chan_au,
};

static const struct morse_dot11ah_ch_ops mors_ch_ops_jp = {
	.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_jp,
	.calculate_primary_s1g = &calculate_primary_s1g_channel_jp,
	.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_jp,
	.get_pri_1mhz_chan = &get_pri_1mhz_chan_jp,
	.transform_overlapping_5g_chan = &transform_overlapping_5g_chan_jp,
};

/* AU map as per 802.11-2020 channelization */
static const struct morse_dot11ah_ch_map mors_au_2020_map = {
		.alpha = CHANNEL_ALPHA_AU,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(au_2020_s1g_channels),
		.s1g_channels = au_2020_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_2020,
};

/* AU map as per 802.11-2024 channelization */
static const struct morse_dot11ah_ch_map mors_au_2024_map = {
		.alpha = CHANNEL_ALPHA_AU,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(au_2024_s1g_channels),
		.s1g_channels = au_2024_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_2024,
};

/* AU map as per 802.11-REVmf channelization */
static const struct morse_dot11ah_ch_map mors_au_map = {
		.alpha = CHANNEL_ALPHA_AU,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(au_s1g_channels),
		.s1g_channels = au_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* BR map */
static const struct morse_dot11ah_ch_map mors_br_map = {
		.alpha = CHANNEL_ALPHA_BR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(br_s1g_channels),
		.s1g_channels = br_s1g_channels,
};

/* NZ map */
static const struct morse_dot11ah_ch_map mors_nz_map = {
		.alpha = CHANNEL_ALPHA_NZ,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(nz_s1g_channels),
		.s1g_channels = nz_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CA map */
static const struct morse_dot11ah_ch_map mors_ca_map = {
		.alpha = CHANNEL_ALPHA_CA,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ca_s1g_channels),
		.s1g_channels = ca_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* EU map */
static const struct morse_dot11ah_ch_map mors_eu_map = {
		.alpha = CHANNEL_ALPHA_EU,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(eu_s1g_channels),
		.s1g_channels = eu_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* GB map */
static const struct morse_dot11ah_ch_map mors_gb_map = {
		.alpha = CHANNEL_ALPHA_GB,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(gb_s1g_channels),
		.s1g_channels = gb_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* IN map */
static const struct morse_dot11ah_ch_map mors_in_map = {
		.alpha = CHANNEL_ALPHA_IN,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(in_s1g_channels),
		.s1g_channels = in_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* JP map */
static const struct morse_dot11ah_ch_map mors_jp_map = {
		.alpha = CHANNEL_ALPHA_JP,
		.ops = &mors_ch_ops_jp,
		.num_mapped_channels = ARRAY_SIZE(jp_s1g_channels),
		.s1g_channels = jp_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* KR map */
static const struct morse_dot11ah_ch_map mors_kr_map = {
		.alpha = CHANNEL_ALPHA_KR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(kr_s1g_channels),
		.s1g_channels = kr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* SG map */
static const struct morse_dot11ah_ch_map mors_sg_map = {
		.alpha = CHANNEL_ALPHA_SG,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(sg_s1g_channels),
		.s1g_channels = sg_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* US map */
static const struct morse_dot11ah_ch_map mors_us_map = {
		.alpha = CHANNEL_ALPHA_US,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(us_s1g_channels),
		.s1g_channels = us_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

static const struct morse_dot11ah_ch_map mors_mx_map = {
		.alpha = CHANNEL_ALPHA_MX,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(mx_s1g_channels),
		.s1g_channels = mx_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CL map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_cl_map = {
		.alpha = CHANNEL_ALPHA_CL,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(cl_s1g_channels),
		.s1g_channels = cl_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* AR map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_ar_map = {
		.alpha = CHANNEL_ALPHA_AR,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(ar_s1g_channels),
		.s1g_channels = ar_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* CO map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_co_map = {
		.alpha = CHANNEL_ALPHA_CO,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(co_s1g_channels),
		.s1g_channels = co_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* EC map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_ec_map = {
		.alpha = CHANNEL_ALPHA_EC,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(ec_s1g_channels),
		.s1g_channels = ec_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* HT map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_ht_map = {
		.alpha = CHANNEL_ALPHA_HT,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(ht_s1g_channels),
		.s1g_channels = ht_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* SV map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_sv_map = {
		.alpha = CHANNEL_ALPHA_SV,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(sv_s1g_channels),
		.s1g_channels = sv_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* UY map as per AU REVmf channelization */
static const struct morse_dot11ah_ch_map mors_uy_map = {
		.alpha = CHANNEL_ALPHA_UY,
		.ops = &mors_ch_ops_au,
		.num_mapped_channels = ARRAY_SIZE(uy_s1g_channels),
		.s1g_channels = uy_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_IEEE80211_REVMF,
};

/* AD map */
static const struct morse_dot11ah_ch_map mors_ad_map = {
		.alpha = CHANNEL_ALPHA_AD,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ad_s1g_channels),
		.s1g_channels = ad_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* AL map */
static const struct morse_dot11ah_ch_map mors_al_map = {
		.alpha = CHANNEL_ALPHA_AL,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(al_s1g_channels),
		.s1g_channels = al_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* AT map */
static const struct morse_dot11ah_ch_map mors_at_map = {
		.alpha = CHANNEL_ALPHA_AT,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(at_s1g_channels),
		.s1g_channels = at_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BA map */
static const struct morse_dot11ah_ch_map mors_ba_map = {
		.alpha = CHANNEL_ALPHA_BA,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ba_s1g_channels),
		.s1g_channels = ba_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BB map */
static const struct morse_dot11ah_ch_map mors_bb_map = {
		.alpha = CHANNEL_ALPHA_BB,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(bb_s1g_channels),
		.s1g_channels = bb_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BE map */
static const struct morse_dot11ah_ch_map mors_be_map = {
		.alpha = CHANNEL_ALPHA_BE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(be_s1g_channels),
		.s1g_channels = be_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BG map */
static const struct morse_dot11ah_ch_map mors_bg_map = {
		.alpha = CHANNEL_ALPHA_BG,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(bg_s1g_channels),
		.s1g_channels = bg_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BS map */
static const struct morse_dot11ah_ch_map mors_bs_map = {
		.alpha = CHANNEL_ALPHA_BS,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(bs_s1g_channels),
		.s1g_channels = bs_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* BZ map */
static const struct morse_dot11ah_ch_map mors_bz_map = {
		.alpha = CHANNEL_ALPHA_BZ,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(bz_s1g_channels),
		.s1g_channels = bz_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CH map */
static const struct morse_dot11ah_ch_map mors_ch_map = {
		.alpha = CHANNEL_ALPHA_CH,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ch_s1g_channels),
		.s1g_channels = ch_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CR map */
static const struct morse_dot11ah_ch_map mors_cr_map = {
		.alpha = CHANNEL_ALPHA_CR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(cr_s1g_channels),
		.s1g_channels = cr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CY map */
static const struct morse_dot11ah_ch_map mors_cy_map = {
		.alpha = CHANNEL_ALPHA_CY,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(cy_s1g_channels),
		.s1g_channels = cy_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* CZ map */
static const struct morse_dot11ah_ch_map mors_cz_map = {
		.alpha = CHANNEL_ALPHA_CZ,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(cz_s1g_channels),
		.s1g_channels = cz_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* DE map */
static const struct morse_dot11ah_ch_map mors_de_map = {
		.alpha = CHANNEL_ALPHA_DE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(de_s1g_channels),
		.s1g_channels = de_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* DK map */
static const struct morse_dot11ah_ch_map mors_dk_map = {
		.alpha = CHANNEL_ALPHA_DK,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(dk_s1g_channels),
		.s1g_channels = dk_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* EE map */
static const struct morse_dot11ah_ch_map mors_ee_map = {
		.alpha = CHANNEL_ALPHA_EE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ee_s1g_channels),
		.s1g_channels = ee_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* ES map */
static const struct morse_dot11ah_ch_map mors_es_map = {
		.alpha = CHANNEL_ALPHA_ES,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(es_s1g_channels),
		.s1g_channels = es_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* FI map */
static const struct morse_dot11ah_ch_map mors_fi_map = {
		.alpha = CHANNEL_ALPHA_FI,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(fi_s1g_channels),
		.s1g_channels = fi_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* FR map */
static const struct morse_dot11ah_ch_map mors_fr_map = {
		.alpha = CHANNEL_ALPHA_FR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(fr_s1g_channels),
		.s1g_channels = fr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* GE map */
static const struct morse_dot11ah_ch_map mors_ge_map = {
		.alpha = CHANNEL_ALPHA_GE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ge_s1g_channels),
		.s1g_channels = ge_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* GF map */
static const struct morse_dot11ah_ch_map mors_gf_map = {
		.alpha = CHANNEL_ALPHA_GF,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(gf_s1g_channels),
		.s1g_channels = gf_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* GR map */
static const struct morse_dot11ah_ch_map mors_gr_map = {
		.alpha = CHANNEL_ALPHA_GR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(gr_s1g_channels),
		.s1g_channels = gr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* GY map */
static const struct morse_dot11ah_ch_map mors_gy_map = {
		.alpha = CHANNEL_ALPHA_GY,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(gy_s1g_channels),
		.s1g_channels = gy_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* HN map */
static const struct morse_dot11ah_ch_map mors_hn_map = {
		.alpha = CHANNEL_ALPHA_HN,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(hn_s1g_channels),
		.s1g_channels = hn_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* HR map */
static const struct morse_dot11ah_ch_map mors_hr_map = {
		.alpha = CHANNEL_ALPHA_HR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(hr_s1g_channels),
		.s1g_channels = hr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* HU map */
static const struct morse_dot11ah_ch_map mors_hu_map = {
		.alpha = CHANNEL_ALPHA_HU,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(hu_s1g_channels),
		.s1g_channels = hu_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* IE map */
static const struct morse_dot11ah_ch_map mors_ie_map = {
		.alpha = CHANNEL_ALPHA_IE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ie_s1g_channels),
		.s1g_channels = ie_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* IS map */
static const struct morse_dot11ah_ch_map mors_is_map = {
		.alpha = CHANNEL_ALPHA_IS,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(is_s1g_channels),
		.s1g_channels = is_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* IT map */
static const struct morse_dot11ah_ch_map mors_it_map = {
		.alpha = CHANNEL_ALPHA_IT,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(it_s1g_channels),
		.s1g_channels = it_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* JM map */
static const struct morse_dot11ah_ch_map mors_jm_map = {
		.alpha = CHANNEL_ALPHA_JM,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(jm_s1g_channels),
		.s1g_channels = jm_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* KE map */
static const struct morse_dot11ah_ch_map mors_ke_map = {
		.alpha = CHANNEL_ALPHA_KE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ke_s1g_channels),
		.s1g_channels = ke_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* LI map */
static const struct morse_dot11ah_ch_map mors_li_map = {
		.alpha = CHANNEL_ALPHA_LI,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(li_s1g_channels),
		.s1g_channels = li_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* LT map */
static const struct morse_dot11ah_ch_map mors_lt_map = {
		.alpha = CHANNEL_ALPHA_LT,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(lt_s1g_channels),
		.s1g_channels = lt_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* LU map */
static const struct morse_dot11ah_ch_map mors_lu_map = {
		.alpha = CHANNEL_ALPHA_LU,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(lu_s1g_channels),
		.s1g_channels = lu_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* LV map */
static const struct morse_dot11ah_ch_map mors_lv_map = {
		.alpha = CHANNEL_ALPHA_LV,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(lv_s1g_channels),
		.s1g_channels = lv_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* MC map */
static const struct morse_dot11ah_ch_map mors_mc_map = {
		.alpha = CHANNEL_ALPHA_MC,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(mc_s1g_channels),
		.s1g_channels = mc_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* MD map */
static const struct morse_dot11ah_ch_map mors_md_map = {
		.alpha = CHANNEL_ALPHA_MD,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(md_s1g_channels),
		.s1g_channels = md_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* ME map */
static const struct morse_dot11ah_ch_map mors_me_map = {
		.alpha = CHANNEL_ALPHA_ME,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(me_s1g_channels),
		.s1g_channels = me_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* MK map */
static const struct morse_dot11ah_ch_map mors_mk_map = {
		.alpha = CHANNEL_ALPHA_MK,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(mk_s1g_channels),
		.s1g_channels = mk_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* MT map */
static const struct morse_dot11ah_ch_map mors_mt_map = {
		.alpha = CHANNEL_ALPHA_MT,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(mt_s1g_channels),
		.s1g_channels = mt_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* NI map */
static const struct morse_dot11ah_ch_map mors_ni_map = {
		.alpha = CHANNEL_ALPHA_NI,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ni_s1g_channels),
		.s1g_channels = ni_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* NL map */
static const struct morse_dot11ah_ch_map mors_nl_map = {
		.alpha = CHANNEL_ALPHA_NL,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(nl_s1g_channels),
		.s1g_channels = nl_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* NO map */
static const struct morse_dot11ah_ch_map mors_no_map = {
		.alpha = CHANNEL_ALPHA_NO,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(no_s1g_channels),
		.s1g_channels = no_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* PA map */
static const struct morse_dot11ah_ch_map mors_pa_map = {
		.alpha = CHANNEL_ALPHA_PA,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(pa_s1g_channels),
		.s1g_channels = pa_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* PL map */
static const struct morse_dot11ah_ch_map mors_pl_map = {
		.alpha = CHANNEL_ALPHA_PL,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(pl_s1g_channels),
		.s1g_channels = pl_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* PT map */
static const struct morse_dot11ah_ch_map mors_pt_map = {
		.alpha = CHANNEL_ALPHA_PT,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(pt_s1g_channels),
		.s1g_channels = pt_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* RO map */
static const struct morse_dot11ah_ch_map mors_ro_map = {
		.alpha = CHANNEL_ALPHA_RO,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ro_s1g_channels),
		.s1g_channels = ro_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* RS map */
static const struct morse_dot11ah_ch_map mors_rs_map = {
		.alpha = CHANNEL_ALPHA_RS,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(rs_s1g_channels),
		.s1g_channels = rs_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* SE map */
static const struct morse_dot11ah_ch_map mors_se_map = {
		.alpha = CHANNEL_ALPHA_SE,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(se_s1g_channels),
		.s1g_channels = se_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* SI map */
static const struct morse_dot11ah_ch_map mors_si_map = {
		.alpha = CHANNEL_ALPHA_SI,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(si_s1g_channels),
		.s1g_channels = si_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* SK map */
static const struct morse_dot11ah_ch_map mors_sk_map = {
		.alpha = CHANNEL_ALPHA_SK,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(sk_s1g_channels),
		.s1g_channels = sk_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* SM map */
static const struct morse_dot11ah_ch_map mors_sm_map = {
		.alpha = CHANNEL_ALPHA_SM,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(sm_s1g_channels),
		.s1g_channels = sm_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* TR map */
static const struct morse_dot11ah_ch_map mors_tr_map = {
		.alpha = CHANNEL_ALPHA_TR,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(tr_s1g_channels),
		.s1g_channels = tr_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* UA map */
static const struct morse_dot11ah_ch_map mors_ua_map = {
		.alpha = CHANNEL_ALPHA_UA,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ua_s1g_channels),
		.s1g_channels = ua_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* UG map */
static const struct morse_dot11ah_ch_map mors_ug_map = {
		.alpha = CHANNEL_ALPHA_UG,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(ug_s1g_channels),
		.s1g_channels = ug_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

/* VA map */
static const struct morse_dot11ah_ch_map mors_va_map = {
		.alpha = CHANNEL_ALPHA_VA,
		.ops = &mors_ch_ops_default,
		.num_mapped_channels = ARRAY_SIZE(va_s1g_channels),
		.s1g_channels = va_s1g_channels,
		.channelization_scheme = CHANNELIZATION_SCHEME_NONE,
};

const struct morse_dot11ah_ch_map *mapped_channels[] = {
	&mors_ad_map,
	&mors_al_map,
	&mors_ar_map,
	&mors_at_map,
	&mors_au_2020_map,
	&mors_au_2024_map,
	&mors_au_map,
	&mors_ba_map,
	&mors_bb_map,
	&mors_be_map,
	&mors_bg_map,
	&mors_br_map,
	&mors_bs_map,
	&mors_bz_map,
	&mors_ca_map,
	&mors_ch_map,
	&mors_cl_map,
	&mors_co_map,
	&mors_cr_map,
	&mors_cy_map,
	&mors_cz_map,
	&mors_de_map,
	&mors_dk_map,
	&mors_ec_map,
	&mors_ee_map,
	&mors_es_map,
	&mors_eu_map,
	&mors_fi_map,
	&mors_fr_map,
	&mors_gb_map,
	&mors_ge_map,
	&mors_gf_map,
	&mors_gr_map,
	&mors_gy_map,
	&mors_hn_map,
	&mors_hr_map,
	&mors_ht_map,
	&mors_hu_map,
	&mors_ie_map,
	&mors_in_map,
	&mors_is_map,
	&mors_it_map,
	&mors_jm_map,
	&mors_jp_map,
	&mors_ke_map,
	&mors_kr_map,
	&mors_li_map,
	&mors_lt_map,
	&mors_lu_map,
	&mors_lv_map,
	&mors_mc_map,
	&mors_md_map,
	&mors_me_map,
	&mors_mk_map,
	&mors_mt_map,
	&mors_mx_map,
	&mors_ni_map,
	&mors_nl_map,
	&mors_no_map,
	&mors_nz_map,
	&mors_pa_map,
	&mors_pl_map,
	&mors_pt_map,
	&mors_ro_map,
	&mors_rs_map,
	&mors_se_map,
	&mors_sg_map,
	&mors_si_map,
	&mors_sk_map,
	&mors_sm_map,
	&mors_sv_map,
	&mors_tr_map,
	&mors_ua_map,
	&mors_ug_map,
	&mors_us_map,
	&mors_uy_map,
	&mors_va_map,
};

static const struct morse_dot11ah_ch_map *__mors_s1g_map;

int morse_dot11ah_channel_set_map(const char *alpha, u32 channelization_scheme)
{
	int i;

	if (WARN_ON(!alpha))
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(mapped_channels); i++) {
		const struct morse_dot11ah_ch_map *s1g_ch_map = mapped_channels[i];

		if (strncmp(s1g_ch_map->alpha, alpha, strlen(alpha)))
			continue;

		/*
		 * Enforce channelization scheme only for countries that have
		 * multiple channelization. For all others, ignore the
		 * scheme and return the first alpha match.
		 */
		if (morse_dot11ah_is_multi_channelization_country(alpha) &&
		    channelization_scheme != s1g_ch_map->channelization_scheme)
			continue;

		__mors_s1g_map = s1g_ch_map;

		break;
	}

	if (!__mors_s1g_map)
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->ops))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->ops->prim_1mhz_channel_loc_to_idx))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->ops->calculate_primary_s1g))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->ops->s1g_op_chan_pri_chan_to_5g))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->ops->get_pri_1mhz_chan))
		return -ENOENT;

	return 0;
}

static struct morse_dot11ah_channel *lookup_s1g_chan_from_5g_chan(int chan_5g)
{
	int ch;

	if (__mors_s1g_map->ops->transform_overlapping_5g_chan)
		chan_5g = __mors_s1g_map->ops->transform_overlapping_5g_chan(chan_5g);

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		if (chan_5g == __mors_s1g_map->s1g_channels[ch].hw_value_map)
			return &__mors_s1g_map->s1g_channels[ch];
	}

	return NULL;
}

#if KERNEL_VERSION(5, 10, 11) > MAC80211_VERSION_CODE || \
	KERNEL_VERSION(6, 18, 0) <= MAC80211_VERSION_CODE
u8 ch_flag_to_chan_bw(enum morse_dot11ah_channel_flags flags)
#else
u8 ch_flag_to_chan_bw(enum ieee80211_channel_flags flags)
#endif
{
	int bw = flags & CHANNEL_FLAGS_BW_MASK;

	switch (bw) {
	case IEEE80211_CHAN_1MHZ:
		return 1;
	case IEEE80211_CHAN_2MHZ:
		return 2;
	case IEEE80211_CHAN_4MHZ:
		return 4;
	case IEEE80211_CHAN_8MHZ:
		return 8;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(ch_flag_to_chan_bw);

/* Return s1g frequency in HZ given s1g chan number */
u32 morse_dot11ah_s1g_chan_to_s1g_freq(int chan_s1g)
{
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		const struct ieee80211_channel_s1g *chan = &__mors_s1g_map->s1g_channels[ch].ch;

		if (chan_s1g == chan->hw_value)
			return KHZ_TO_HZ(ieee80211_channel_to_khz(chan));
	}

	return 0;
}
EXPORT_SYMBOL(morse_dot11ah_s1g_chan_to_s1g_freq);

/* Return s1g channel number given 5g channel number and op class */
u16 morse_dot11ah_5g_chan_to_s1g_ch(u8 chan_5g, u8 op_class)
{
	struct morse_dot11ah_channel *chan = lookup_s1g_chan_from_5g_chan(chan_5g);

	return (chan) ? chan->ch.hw_value : 0;
}
EXPORT_SYMBOL(morse_dot11ah_5g_chan_to_s1g_ch);

/* Returns a pointer to the s1g map region string */
const char *morse_dot11ah_get_region_str(void)
{
	return (const char *)__mors_s1g_map->alpha;
}
EXPORT_SYMBOL(morse_dot11ah_get_region_str);

const struct morse_dot11ah_channel *morse_dot11ah_s1g_freq_to_s1g(int freq, int bw)
{
	int ch;
	int _freq;
	int _bw;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		_freq = MHZ_TO_HZ(__mors_s1g_map->s1g_channels[ch].ch.center_freq) +
				KHZ_TO_HZ(__mors_s1g_map->s1g_channels[ch].ch.freq_offset);
		_bw = ch_flag_to_chan_bw(__mors_s1g_map->s1g_channels[ch].ch.flags);
		if (freq == _freq && bw == _bw)
			return &__mors_s1g_map->s1g_channels[ch];
	}

	return NULL;
}
EXPORT_SYMBOL(morse_dot11ah_s1g_freq_to_s1g);

const struct morse_dot11ah_channel
*morse_dot11ah_5g_chan_to_s1g(struct ieee80211_channel *chan_5g)
{
	return lookup_s1g_chan_from_5g_chan(chan_5g->hw_value);
}
EXPORT_SYMBOL(morse_dot11ah_5g_chan_to_s1g);

const struct morse_dot11ah_channel
*morse_dot11ah_channel_chandef_to_s1g(struct cfg80211_chan_def *chan_5g)
{
	int hwval;

	if (chan_5g->center_freq1 && chan_5g->center_freq1 != chan_5g->chan->center_freq)
		hwval = ieee80211_frequency_to_channel(chan_5g->center_freq1);
	else
		hwval = ieee80211_frequency_to_channel(chan_5g->chan->center_freq);

	return lookup_s1g_chan_from_5g_chan(hwval);
}
EXPORT_SYMBOL(morse_dot11ah_channel_chandef_to_s1g);

int morse_dot11ah_s1g_chan_to_5g_chan(int chan_s1g)
{
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++)
		if (chan_s1g ==  __mors_s1g_map->s1g_channels[ch].ch.hw_value)
			return __mors_s1g_map->s1g_channels[ch].hw_value_map;

	return -ENOENT;
}
EXPORT_SYMBOL(morse_dot11ah_s1g_chan_to_5g_chan);

int morse_dot11ah_s1g_chan_bw_to_5g_chan(int chan_s1g, int bw_mhz)
{
	int ch;
	int ch_bw;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++)
		if (chan_s1g ==  __mors_s1g_map->s1g_channels[ch].ch.hw_value) {
			ch_bw = ch_flag_to_chan_bw(__mors_s1g_map->s1g_channels[ch].ch.flags);
			if (ch_bw == bw_mhz)
				return __mors_s1g_map->s1g_channels[ch].hw_value_map;
		}
	return -ENOENT;
}
EXPORT_SYMBOL(morse_dot11ah_s1g_chan_bw_to_5g_chan);

int morse_dot11ah_s1g_op_chan_pri_chan_to_5g(int s1g_op_chan, int s1g_pri_chan)
{
	return __mors_s1g_map->ops->s1g_op_chan_pri_chan_to_5g(s1g_op_chan, s1g_pri_chan);
}
EXPORT_SYMBOL(morse_dot11ah_s1g_op_chan_pri_chan_to_5g);

u32 morse_dot11ah_channel_get_flags(int chan_s1g)
{
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++)
		if (chan_s1g ==  __mors_s1g_map->s1g_channels[ch].ch.hw_value)
			return __mors_s1g_map->s1g_channels[ch].ch.flags;

	/* Could not find the channel? it is safe to set flags = 0 */
	return 0;
}
EXPORT_SYMBOL(morse_dot11ah_channel_get_flags);

/**
 * morse_dot11ah_freq_khz_bw_mhz_to_chan() - Get the S1G channel number for a frequency/bandwidth
 * @freq: Frequency, in KHz
 * @bw: Bandwidth, in MHz
 *
 * Channel is resolved via a linear scan of @__mors_s1g_map, the regulatory-domain channel table
 * selected by the last successful morse_dot11ah_channel_set_map() call. Both the frequency
 * and bandwith must be correct for a successful conversion.
 *
 * Return: S1G channel number, or 0 if no matching channel was found in the active region's table
 */
int morse_dot11ah_freq_khz_bw_mhz_to_chan(u32 freq, u8 bw)
{
	const struct morse_dot11ah_channel *chan =
		morse_dot11ah_s1g_freq_to_s1g(KHZ_TO_HZ(freq), bw);

	if (chan)
		return chan->ch.hw_value;

	return 0;
}
EXPORT_SYMBOL(morse_dot11ah_freq_khz_bw_mhz_to_chan);

int morse_dot11ah_prim_1mhz_chan_loc_to_idx(int op_bw_mhz, int pr_bw_mhz, int pr_chan_num,
					    int chan_centre_freq_num, int chan_loc)
{
	return __mors_s1g_map->ops->prim_1mhz_channel_loc_to_idx(op_bw_mhz,
							    pr_bw_mhz,
							    pr_chan_num,
							    chan_centre_freq_num,
							    chan_loc);
}

int morse_dot11ah_calc_prim_s1g_chan(int op_bw_mhz, int pr_bw_mhz,
						int chan_centre_freq_num, int pr_1mhz_chan_idx)
{
	return __mors_s1g_map->ops->calculate_primary_s1g(op_bw_mhz,
						     pr_bw_mhz,
						     chan_centre_freq_num,
						     pr_1mhz_chan_idx);
}
EXPORT_SYMBOL(morse_dot11ah_calc_prim_s1g_chan);

int morse_dot11ah_get_pri_1mhz_chan(int primary_channel,
	int primary_channel_width_mhz, bool pri_1_mhz_loc_upper)
{
	return __mors_s1g_map->ops->get_pri_1mhz_chan(primary_channel,
					   primary_channel_width_mhz, pri_1_mhz_loc_upper);
}
EXPORT_SYMBOL(morse_dot11ah_get_pri_1mhz_chan);

int morse_dot11ah_ignore_channel(int chan_s1g, u32 bw_mhz)
{
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		if (chan_s1g ==  __mors_s1g_map->s1g_channels[ch].ch.hw_value &&
		    bw_mhz == ch_flag_to_chan_bw(__mors_s1g_map->s1g_channels[ch].ch.flags)) {
			__mors_s1g_map->s1g_channels[ch].ignored = true;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(morse_dot11ah_ignore_channel);

int morse_dot11ah_get_num_channels(void)
{
	if (!__mors_s1g_map)
		return 0;
	return __mors_s1g_map->num_mapped_channels;
}
EXPORT_SYMBOL(morse_dot11ah_get_num_channels);

int morse_dot11ah_fill_channel_list(struct morse_channel *list)
{
	int i;
	int count = 0;
	struct morse_channel *chan;
	const struct morse_dot11ah_channel *map_entry;

	if (!list || !__mors_s1g_map)
		return -ENOENT;

	for (i = 0; i < __mors_s1g_map->num_mapped_channels; i++) {
		map_entry = &__mors_s1g_map->s1g_channels[i];
		chan = &list[count];

		/* Skip ignored channels */
		if (map_entry->ignored)
			continue;

		chan->frequency_khz = ieee80211_channel_to_khz(&map_entry->ch);
		chan->channel_s1g = map_entry->ch.hw_value;
		chan->channel_5g = map_entry->hw_value_map;
		/* extract the s1g bandwidth from the channel flags */
		chan->bandwidth_mhz = ch_flag_to_chan_bw(map_entry->ch.flags);
		count++;
	}

	return count;
}
EXPORT_SYMBOL(morse_dot11ah_fill_channel_list);
