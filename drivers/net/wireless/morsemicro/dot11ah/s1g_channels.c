/*
 * Copyright 2017-2022 Morse Micro
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

#define CHANNEL_FLAGS_BW_MASK 0x7c000

/* power should match regdb_Sub-1_GHz.tsv (repo: morse_regdb) */
#define CHANS1GHZ(channel, frequency, offset, chflags, power, ch5g)  { \
	.ch = { \
		.band = NL80211_BAND_5GHZ, \
		.center_freq = (frequency), \
		.freq_offset = offset, \
		.hw_value = (channel), \
		.flags = chflags, \
		.max_antenna_gain = 0, \
		.max_power = power, \
		.max_reg_power = power, \
	}, \
	.hw_value_map = ch5g, \
}

/**
 * Morse dot11ah S1G channel list.
 *
 * Includes mapping to 5G channels
 */
struct morse_dot11ah_ch_map {
	char alpha[3];
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
	u32 num_mapped_channels;
	struct morse_dot11ah_channel s1g_channels[];
};

#if KERNEL_VERSION(5, 10, 11) > MAC80211_VERSION_CODE
static int ch_flag_to_chan_bw(enum morse_dot11ah_channel_flags flags)
#else
static int ch_flag_to_chan_bw(enum ieee80211_channel_flags flags)
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
	int offset;

	switch (op_bw_mhz) {
	case 1:
		return chan_centre_freq_num;
	case 2:
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
		default:
			return -ENOENT;
		}
	} else if (primary_channel_width_mhz == 1) {
		return primary_channel;
	} else {
		return -EINVAL;
	}
}

static const struct morse_dot11ah_ch_map mors_us_map = {
		.alpha = "US",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 48,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(1, 902, 500, IEEE80211_CHAN_1MHZ, 3000, 132),
			CHANS1GHZ(3, 903, 500, IEEE80211_CHAN_1MHZ, 3000, 136),
			CHANS1GHZ(5, 904, 500, IEEE80211_CHAN_1MHZ, 3000, 36),
			CHANS1GHZ(7, 905, 500, IEEE80211_CHAN_1MHZ, 3000, 40),
			CHANS1GHZ(9, 906, 500, IEEE80211_CHAN_1MHZ, 3000, 44),
			CHANS1GHZ(11, 907, 500, IEEE80211_CHAN_1MHZ, 3000, 48),
			CHANS1GHZ(13, 908, 500, IEEE80211_CHAN_1MHZ, 3000, 52),
			CHANS1GHZ(15, 909, 500, IEEE80211_CHAN_1MHZ, 3000, 56),
			CHANS1GHZ(17, 910, 500, IEEE80211_CHAN_1MHZ, 3000, 60),
			CHANS1GHZ(19, 911, 500, IEEE80211_CHAN_1MHZ, 3000, 64),
			CHANS1GHZ(21, 912, 500, IEEE80211_CHAN_1MHZ, 3000, 100),
			CHANS1GHZ(23, 913, 500, IEEE80211_CHAN_1MHZ, 3000, 104),
			CHANS1GHZ(25, 914, 500, IEEE80211_CHAN_1MHZ, 3000, 108),
			CHANS1GHZ(27, 915, 500, IEEE80211_CHAN_1MHZ, 3000, 112),
			CHANS1GHZ(29, 916, 500, IEEE80211_CHAN_1MHZ, 3000, 116),
			CHANS1GHZ(31, 917, 500, IEEE80211_CHAN_1MHZ, 3000, 120),
			CHANS1GHZ(33, 918, 500, IEEE80211_CHAN_1MHZ, 3000, 124),
			CHANS1GHZ(35, 919, 500, IEEE80211_CHAN_1MHZ, 3000, 128),
			CHANS1GHZ(37, 920, 500, IEEE80211_CHAN_1MHZ, 3000, 149),
			CHANS1GHZ(39, 921, 500, IEEE80211_CHAN_1MHZ, 3000, 153),
			CHANS1GHZ(41, 922, 500, IEEE80211_CHAN_1MHZ, 3000, 157),
			CHANS1GHZ(43, 923, 500, IEEE80211_CHAN_1MHZ, 3000, 161),
			CHANS1GHZ(45, 924, 500, IEEE80211_CHAN_1MHZ, 3000, 165),
			CHANS1GHZ(47, 925, 500, IEEE80211_CHAN_1MHZ, 3000, 169),
			CHANS1GHZ(49, 926, 500, IEEE80211_CHAN_1MHZ, 3000, 173),
			CHANS1GHZ(51, 927, 500, IEEE80211_CHAN_1MHZ, 3000, 177),
			/* 2MHz */
			CHANS1GHZ(2, 903, 0, IEEE80211_CHAN_2MHZ, 3000, 134),
			CHANS1GHZ(6, 905, 0, IEEE80211_CHAN_2MHZ, 3000, 38),
			CHANS1GHZ(10, 907, 0, IEEE80211_CHAN_2MHZ, 3000, 46),
			CHANS1GHZ(14, 909, 0, IEEE80211_CHAN_2MHZ, 3000, 54),
			CHANS1GHZ(18, 911, 0, IEEE80211_CHAN_2MHZ, 3000, 62),
			CHANS1GHZ(22, 913, 0, IEEE80211_CHAN_2MHZ, 3000, 102),
			CHANS1GHZ(26, 915, 0, IEEE80211_CHAN_2MHZ, 3000, 110),
			CHANS1GHZ(30, 917, 0, IEEE80211_CHAN_2MHZ, 3000, 118),
			CHANS1GHZ(34, 919, 0, IEEE80211_CHAN_2MHZ, 3000, 126),
			CHANS1GHZ(38, 921, 0, IEEE80211_CHAN_2MHZ, 3000, 151),
			CHANS1GHZ(42, 923, 0, IEEE80211_CHAN_2MHZ, 3000, 159),
			CHANS1GHZ(46, 925, 0, IEEE80211_CHAN_2MHZ, 3000, 167),
			CHANS1GHZ(50, 927, 0, IEEE80211_CHAN_2MHZ, 3000, 175),
			/* 4MHz */
			CHANS1GHZ(8, 906, 0, IEEE80211_CHAN_4MHZ, 3000, 42),
			CHANS1GHZ(16, 910, 0, IEEE80211_CHAN_4MHZ, 3000, 58),
			CHANS1GHZ(24, 914, 0, IEEE80211_CHAN_4MHZ, 3000, 106),
			CHANS1GHZ(32, 918, 0, IEEE80211_CHAN_4MHZ, 3000, 122),
			CHANS1GHZ(40, 922, 0, IEEE80211_CHAN_4MHZ, 3000, 155),
			CHANS1GHZ(48, 926, 0, IEEE80211_CHAN_4MHZ, 3000, 171),
			/* 8MHz */
			CHANS1GHZ(12, 908, 0, IEEE80211_CHAN_8MHZ, 3000, 50),
			CHANS1GHZ(28, 916, 0, IEEE80211_CHAN_8MHZ, 3000, 114),
			CHANS1GHZ(44, 924, 0, IEEE80211_CHAN_8MHZ, 3000, 163),
		},
}; /* End US Map */

static const struct morse_dot11ah_ch_map mors_au_map = {
		.alpha = "AU",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 23,
		.s1g_channels = {
			 /* 1MHz */
			CHANS1GHZ(27, 915, 500, IEEE80211_CHAN_1MHZ, 3000, 112),
			CHANS1GHZ(29, 916, 500, IEEE80211_CHAN_1MHZ, 3000, 116),
			CHANS1GHZ(31, 917, 500, IEEE80211_CHAN_1MHZ, 3000, 120),
			CHANS1GHZ(33, 918, 500, IEEE80211_CHAN_1MHZ, 3000, 124),
			CHANS1GHZ(35, 919, 500, IEEE80211_CHAN_1MHZ, 3000, 128),
			CHANS1GHZ(37, 920, 500, IEEE80211_CHAN_1MHZ, 3000, 149),
			CHANS1GHZ(39, 921, 500, IEEE80211_CHAN_1MHZ, 3000, 153),
			CHANS1GHZ(41, 922, 500, IEEE80211_CHAN_1MHZ, 3000, 157),
			CHANS1GHZ(43, 923, 500, IEEE80211_CHAN_1MHZ, 3000, 161),
			CHANS1GHZ(45, 924, 500, IEEE80211_CHAN_1MHZ, 3000, 165),
			CHANS1GHZ(47, 925, 500, IEEE80211_CHAN_1MHZ, 3000, 169),
			CHANS1GHZ(49, 926, 500, IEEE80211_CHAN_1MHZ, 3000, 173),
			CHANS1GHZ(51, 927, 500, IEEE80211_CHAN_1MHZ, 3000, 177),
			/* 2MHz */
			CHANS1GHZ(30, 917, 0, IEEE80211_CHAN_2MHZ, 3000, 118),
			CHANS1GHZ(34, 919, 0, IEEE80211_CHAN_2MHZ, 3000, 126),
			CHANS1GHZ(38, 921, 0, IEEE80211_CHAN_2MHZ, 3000, 151),
			CHANS1GHZ(42, 923, 0, IEEE80211_CHAN_2MHZ, 3000, 159),
			CHANS1GHZ(46, 925, 0, IEEE80211_CHAN_2MHZ, 3000, 167),
			CHANS1GHZ(50, 927, 0, IEEE80211_CHAN_2MHZ, 3000, 175),
			/* 4MHz */
			CHANS1GHZ(32, 918, 0, IEEE80211_CHAN_4MHZ, 3000, 122),
			CHANS1GHZ(40, 922, 0, IEEE80211_CHAN_4MHZ, 3000, 155),
			CHANS1GHZ(48, 926, 0, IEEE80211_CHAN_4MHZ, 3000, 171),
			/* 8MHz */
			CHANS1GHZ(44, 924, 0, IEEE80211_CHAN_8MHZ, 3000, 163),
		},
}; /* End AU Map */

static const struct morse_dot11ah_ch_map mors_nz_map = {
		.alpha = "NZ",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 23,
		.s1g_channels = {
			 /* 1MHz */
			CHANS1GHZ(27, 915, 500, IEEE80211_CHAN_1MHZ, 3000, 112),
			CHANS1GHZ(29, 916, 500, IEEE80211_CHAN_1MHZ, 3000, 116),
			CHANS1GHZ(31, 917, 500, IEEE80211_CHAN_1MHZ, 3000, 120),
			CHANS1GHZ(33, 918, 500, IEEE80211_CHAN_1MHZ, 3000, 124),
			CHANS1GHZ(35, 919, 500, IEEE80211_CHAN_1MHZ, 3000, 128),
			CHANS1GHZ(37, 920, 500, IEEE80211_CHAN_1MHZ, 3000, 149),
			CHANS1GHZ(39, 921, 500, IEEE80211_CHAN_1MHZ, 3000, 153),
			CHANS1GHZ(41, 922, 500, IEEE80211_CHAN_1MHZ, 3000, 157),
			CHANS1GHZ(43, 923, 500, IEEE80211_CHAN_1MHZ, 3000, 161),
			CHANS1GHZ(45, 924, 500, IEEE80211_CHAN_1MHZ, 3000, 165),
			CHANS1GHZ(47, 925, 500, IEEE80211_CHAN_1MHZ, 3000, 169),
			CHANS1GHZ(49, 926, 500, IEEE80211_CHAN_1MHZ, 3000, 173),
			CHANS1GHZ(51, 927, 500, IEEE80211_CHAN_1MHZ, 3000, 177),
			/* 2MHz */
			CHANS1GHZ(30, 917, 0, IEEE80211_CHAN_2MHZ, 3000, 118),
			CHANS1GHZ(34, 919, 0, IEEE80211_CHAN_2MHZ, 3000, 126),
			CHANS1GHZ(38, 921, 0, IEEE80211_CHAN_2MHZ, 3000, 151),
			CHANS1GHZ(42, 923, 0, IEEE80211_CHAN_2MHZ, 3000, 159),
			CHANS1GHZ(46, 925, 0, IEEE80211_CHAN_2MHZ, 3000, 167),
			CHANS1GHZ(50, 927, 0, IEEE80211_CHAN_2MHZ, 3000, 175),
			/* 4MHz */
			CHANS1GHZ(32, 918, 0, IEEE80211_CHAN_4MHZ, 3000, 122),
			CHANS1GHZ(40, 922, 0, IEEE80211_CHAN_4MHZ, 3000, 155),
			CHANS1GHZ(48, 926, 0, IEEE80211_CHAN_4MHZ, 3000, 171),
			/* 8MHz */
			CHANS1GHZ(44, 924, 0, IEEE80211_CHAN_8MHZ, 3000, 163),
		},
}; /* End NZ Map */

static const struct morse_dot11ah_ch_map mors_eu_map = {
		.alpha = "EU",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 8,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(1, 863, 500, IEEE80211_CHAN_1MHZ, 1600, 132),
			CHANS1GHZ(3, 864, 500, IEEE80211_CHAN_1MHZ, 1600, 136),
			CHANS1GHZ(5, 865, 500, IEEE80211_CHAN_1MHZ, 1600, 36),
			CHANS1GHZ(7, 866, 500, IEEE80211_CHAN_1MHZ, 1600, 40),
			CHANS1GHZ(9, 867, 500, IEEE80211_CHAN_1MHZ, 1600, 44),
			CHANS1GHZ(31, 916, 900, IEEE80211_CHAN_1MHZ, 1600, 120),
			CHANS1GHZ(33, 917, 900, IEEE80211_CHAN_1MHZ, 1600, 124),
			CHANS1GHZ(35, 918, 900, IEEE80211_CHAN_1MHZ, 1600, 128),
		},
}; /* End EU Map */

static const struct morse_dot11ah_ch_map mors_in_map = {
		.alpha = "IN",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 3,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(5, 865, 500, IEEE80211_CHAN_1MHZ, 1600, 36),
			CHANS1GHZ(7, 866, 500, IEEE80211_CHAN_1MHZ, 1600, 40),
			CHANS1GHZ(9, 867, 500, IEEE80211_CHAN_1MHZ, 1600, 44),
		},
}; /* End IN Map */

static const struct morse_dot11ah_ch_map mors_jp_map = {
		.alpha = "JP",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_jp,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_jp,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_jp,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_jp,
		.num_mapped_channels = 11,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(13, 923, 0, IEEE80211_CHAN_1MHZ, 1600, 36),
			CHANS1GHZ(15, 924, 0, IEEE80211_CHAN_1MHZ, 1600, 40),
			CHANS1GHZ(17, 925, 0, IEEE80211_CHAN_1MHZ, 1600, 44),
			CHANS1GHZ(19, 926, 0, IEEE80211_CHAN_1MHZ, 1600, 48),
			CHANS1GHZ(21, 927, 0, IEEE80211_CHAN_1MHZ, 1600, 64),
			/* 2MHz */
			CHANS1GHZ(2, 923, 500, IEEE80211_CHAN_2MHZ, 1600, 38),
			CHANS1GHZ(6, 925, 500, IEEE80211_CHAN_2MHZ, 1600, 46), /* Overlap ch38 */
			CHANS1GHZ(4, 924, 500, IEEE80211_CHAN_2MHZ, 1600, 54),
			CHANS1GHZ(8, 926, 500, IEEE80211_CHAN_2MHZ, 1600, 62),
			/* 4MHz */
			CHANS1GHZ(36, 924, 500, IEEE80211_CHAN_4MHZ, 1600, 42), /* Overlap ch4 */
			CHANS1GHZ(38, 925, 500, IEEE80211_CHAN_4MHZ, 1600, 58), /* Overlap ch4 */
		},
}; /* End JP Map */

static const struct morse_dot11ah_ch_map mors_kr_map = {
		.alpha = "KR",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 10,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(1, 918, 0, IEEE80211_CHAN_1MHZ, 477, 132),
			CHANS1GHZ(3, 919, 0, IEEE80211_CHAN_1MHZ, 477, 136),
			CHANS1GHZ(5, 920, 0, IEEE80211_CHAN_1MHZ, 477, 36),
			CHANS1GHZ(7, 921, 0, IEEE80211_CHAN_1MHZ, 477, 40),
			CHANS1GHZ(9, 922, 0, IEEE80211_CHAN_1MHZ, 1000, 44),
			CHANS1GHZ(11, 923, 0, IEEE80211_CHAN_1MHZ, 1000, 48),
			/* 2MHz */
			CHANS1GHZ(2, 918, 500, IEEE80211_CHAN_2MHZ, 477, 134),
			CHANS1GHZ(6, 920, 500, IEEE80211_CHAN_2MHZ, 477, 38),
			CHANS1GHZ(10, 922, 500, IEEE80211_CHAN_2MHZ, 1000, 46),
			/* 4MHz */
			CHANS1GHZ(8, 921, 500, IEEE80211_CHAN_4MHZ, 477, 42),
		},
}; /* End KR Map */

static const struct morse_dot11ah_ch_map mors_sg_map = {
		.alpha = "SG",
		.prim_1mhz_channel_loc_to_idx = &prim_1mhz_channel_loc_to_idx_default,
		.calculate_primary_s1g = &calculate_primary_s1g_channel_default,
		.s1g_op_chan_pri_chan_to_5g = &s1g_op_chan_pri_chan_to_5g_default,
		.get_pri_1mhz_chan = &get_pri_1mhz_chan_default,
		.num_mapped_channels = 12,
		.s1g_channels = {
			/* 1MHz */
			CHANS1GHZ(7, 866, 500, IEEE80211_CHAN_1MHZ, 3000, 40),
			CHANS1GHZ(9, 867, 500, IEEE80211_CHAN_1MHZ, 3000, 44),
			CHANS1GHZ(11, 868, 500, IEEE80211_CHAN_1MHZ, 3000, 48),
			CHANS1GHZ(37, 920, 500, IEEE80211_CHAN_1MHZ, 3000, 149),
			CHANS1GHZ(39, 921, 500, IEEE80211_CHAN_1MHZ, 3000, 153),
			CHANS1GHZ(41, 922, 500, IEEE80211_CHAN_1MHZ, 3000, 157),
			CHANS1GHZ(43, 923, 500, IEEE80211_CHAN_1MHZ, 3000, 161),
			CHANS1GHZ(45, 924, 500, IEEE80211_CHAN_1MHZ, 3000, 165),
			/* 2MHz */
			CHANS1GHZ(10, 868, 0, IEEE80211_CHAN_2MHZ, 3000, 46),
			CHANS1GHZ(38, 921, 0, IEEE80211_CHAN_2MHZ, 3000, 151),
			CHANS1GHZ(42, 923, 0, IEEE80211_CHAN_2MHZ, 3000, 159),
			/* 4MHz */
			CHANS1GHZ(40, 922, 0, IEEE80211_CHAN_4MHZ, 3000, 155),
		},
}; /* End SG Map */

const struct morse_dot11ah_ch_map *mapped_channels[] = {
	&mors_au_map,
	&mors_eu_map,
	&mors_in_map,
	&mors_jp_map,
	&mors_kr_map,
	&mors_nz_map,
	&mors_sg_map,
	&mors_us_map,
};

static const struct morse_dot11ah_ch_map *__mors_s1g_map;

int morse_dot11ah_channel_set_map(const char *alpha)
{
	int i;

	if (WARN_ON(!alpha))
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(mapped_channels); i++)
		if (!strncmp(mapped_channels[i]->alpha, alpha, strlen(alpha)))
			__mors_s1g_map = mapped_channels[i];

	if (!__mors_s1g_map)
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->prim_1mhz_channel_loc_to_idx))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->calculate_primary_s1g))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->prim_1mhz_channel_loc_to_idx))
		return -ENOENT;

	if (WARN_ON(!__mors_s1g_map->get_pri_1mhz_chan))
		return -ENOENT;

	return 0;
}

/* Convert a regional ISO alpha-2 string to a morse_region */
static enum morse_dot11ah_region morse_reg_get_region(const char *alpha)
{
	if (!alpha)
		return REGION_UNSET;
	if (!strcmp(alpha, "AU"))
		return MORSE_AU;
	if (!strcmp(alpha, "EU"))
		return MORSE_EU;
	if (!strcmp(alpha, "IN"))
		return MORSE_IN;
	if (!strcmp(alpha, "JP"))
		return MORSE_JP;
	if (!strcmp(alpha, "KR"))
		return MORSE_KR;
	if (!strcmp(alpha, "NZ"))
		return MORSE_NZ;
	if (!strcmp(alpha, "SG"))
		return MORSE_SG;
	if (!strcmp(alpha, "US"))
		return MORSE_US;
	/* If the region is unknown */
	return REGION_UNSET;
}

/* Return s1g frequency in HZ given s1g chan number */
u32 morse_dot11ah_s1g_chan_to_s1g_freq(int chan_s1g)
{
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		const struct ieee80211_channel_s1g *chan = &__mors_s1g_map->s1g_channels[ch].ch;

		if (chan_s1g == chan->hw_value)
			return KHZ_TO_HZ(ieee80211_channel_to_khz(chan));
	}

	return false;
}
EXPORT_SYMBOL(morse_dot11ah_s1g_chan_to_s1g_freq);

/* Return s1g channel number given 5g channel number and op class */
u16 morse_dot11ah_5g_chan_to_s1g_ch(u8 chan_5g, u8 op_class)
{
	enum nl80211_band new_band = NL80211_BAND_5GHZ;
	u32 new_freq, ch = 0;

	ieee80211_operating_class_to_band(op_class, &new_band);

	new_freq = ieee80211_channel_to_frequency(chan_5g, new_band);

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		if (chan_5g ==  __mors_s1g_map->s1g_channels[ch].hw_value_map)
			return __mors_s1g_map->s1g_channels[ch].ch.hw_value;
	}

	return false;
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
	int ch;

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++)
		if (chan_5g->hw_value ==  __mors_s1g_map->s1g_channels[ch].hw_value_map)
			return &__mors_s1g_map->s1g_channels[ch];

	return NULL;
}
EXPORT_SYMBOL(morse_dot11ah_5g_chan_to_s1g);

const struct morse_dot11ah_channel
*morse_dot11ah_channel_chandef_to_s1g(struct cfg80211_chan_def *chan_5g)
{
	int ch;
	int hwval;

	if (chan_5g->center_freq1 &&
			chan_5g->center_freq1 != chan_5g->chan->center_freq)
		hwval = ieee80211_frequency_to_channel(chan_5g->center_freq1);
	else
		hwval = ieee80211_frequency_to_channel(chan_5g->chan->center_freq);

	for (ch = 0; ch < __mors_s1g_map->num_mapped_channels; ch++) {
		if (hwval ==  __mors_s1g_map->s1g_channels[ch].hw_value_map)
			return &__mors_s1g_map->s1g_channels[ch];
	}

	return NULL;
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
	return __mors_s1g_map->s1g_op_chan_pri_chan_to_5g(s1g_op_chan, s1g_pri_chan);
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

int morse_dot11ah_channel_to_freq_khz(int chan)
{
	enum morse_dot11ah_region region;

	region = morse_reg_get_region(__mors_s1g_map->alpha);

	switch (region) {
	case MORSE_AU:
	case MORSE_NZ:
	case MORSE_US:
		return 902000 + chan * 500;
	case MORSE_EU:
		if (chan < 31)
			return 863000 + chan * 500;
		else
			return 901400 + chan * 500;
	case MORSE_IN:
		return 863000 + chan * 500;
	case MORSE_KR:
		return 917500 + chan * 500;
	case MORSE_SG:
		if (chan < 37)
			return 863000 + chan * 500;
		else
			return 902000 + chan * 500;
	case MORSE_JP:
		if (chan <= 21) {
			if (chan & 0x1)
				return 916500 + chan * 500;
			else
				return 922500 + chan * 500;
		} else {
			return 906500 + chan * 500;
		}
	case REGION_UNSET:
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL(morse_dot11ah_channel_to_freq_khz);

int morse_dot11ah_freq_khz_bw_mhz_to_chan(u32 freq, u8 bw)
{
	int channel = 0;
	enum morse_dot11ah_region region;

	region = morse_reg_get_region(__mors_s1g_map->alpha);

	switch (region) {
	case MORSE_AU:
	case MORSE_NZ:
	case MORSE_US:
		channel = (freq - 902000) /  500;
		break;
	case MORSE_EU:
		if (freq > 901400)
			channel = (freq - 901400) / 500;
		else
			channel = (freq - 863000) / 500;
		break;
	case MORSE_IN:
		channel = (freq - 863000) / 500;
		break;
	case MORSE_JP:
		if ((freq % 1000) == 500) {
			/* We are on a 500kHz centre */
			if (bw < 4)
				channel = (freq - 922500) / 500;
			else
				channel = (freq - 906500) / 500;
		} else {
			channel = (freq - 916500) / 500;
		}
		break;
	case MORSE_KR:
		channel = (freq - 917500) / 500;
		break;
	case MORSE_SG:
		if (freq > 902000)
			channel = (freq - 902000) / 500;
		else
			channel = (freq - 863000) / 500;
		break;
	case REGION_UNSET:
	default:
		break;
	}
	return channel;
}
EXPORT_SYMBOL(morse_dot11ah_freq_khz_bw_mhz_to_chan);

int morse_dot11ah_prim_1mhz_chan_loc_to_idx(int op_bw_mhz, int pr_bw_mhz, int pr_chan_num,
					    int chan_centre_freq_num, int chan_loc)
{
	return __mors_s1g_map->prim_1mhz_channel_loc_to_idx(op_bw_mhz,
							    pr_bw_mhz,
							    pr_chan_num,
							    chan_centre_freq_num,
							    chan_loc);
}

int morse_dot11ah_calc_prim_s1g_chan(int op_bw_mhz, int pr_bw_mhz,
						int chan_centre_freq_num, int pr_1mhz_chan_idx)
{
	return __mors_s1g_map->calculate_primary_s1g(op_bw_mhz,
						     pr_bw_mhz,
						     chan_centre_freq_num,
						     pr_1mhz_chan_idx);
}
EXPORT_SYMBOL(morse_dot11ah_calc_prim_s1g_chan);

int morse_dot11ah_get_pri_1mhz_chan(int primary_channel,
	int primary_channel_width_mhz, bool pri_1_mhz_loc_upper)
{
	return __mors_s1g_map->get_pri_1mhz_chan(primary_channel,
					   primary_channel_width_mhz, pri_1_mhz_loc_upper);
}
EXPORT_SYMBOL(morse_dot11ah_get_pri_1mhz_chan);

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
	struct morse_channel *chan;
	const struct morse_dot11ah_channel *map_entry;

	if (!list || !__mors_s1g_map)
		return -ENOENT;

	for (i = 0; i < __mors_s1g_map->num_mapped_channels; i++) {
		map_entry = &__mors_s1g_map->s1g_channels[i];
		chan = &list[i];
		chan->frequency_khz = ieee80211_channel_to_khz(&map_entry->ch);
		chan->channel_s1g = map_entry->ch.hw_value;
		chan->channel_5g = map_entry->hw_value_map;
		/* extract the s1g bandwidth from the channel flags */
		chan->bandwidth_mhz = ch_flag_to_chan_bw(map_entry->ch.flags);
	}

	return __mors_s1g_map->num_mapped_channels;
}
EXPORT_SYMBOL(morse_dot11ah_fill_channel_list);
