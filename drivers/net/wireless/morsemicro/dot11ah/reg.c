/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/string.h>

#include "dot11ah.h"
#include "debug.h"

#define AUTO_BW	NL80211_RRF_AUTO_BW

/**
 * DOC: How To Modify Regulatory (reg.c) and Channel Mapping (s1g_channels.c)
 *
 * - Both are covered here as they are dependent.
 * The available channel maps are stored in `s1g_channels.c`, in the `channel_map` array.
 * This array is built of `morse_dot11ah_ch_map` structs.
 *
 * This struct defines a region/country alpha for the map along with an array of
 * `morse_dot11ah_channels`, which are the explicit map between a 5g channel and:
 * - An s1g channel
 * - the s1g frequency
 * - the s1g bandwidth.
 *
 * In order to make use of these channels, their frequencies need to fall
 * within the allow-listed spectrum defined in a 'regulatory database'
 * entry for the desired region. These entries are found in this file.
 *
 * In order to add a new channel map you must:
 *	1. Define the channel map for your region (alpha), and add it to the
 *	   `mapped_channels` array.
 *	2. Define, in this file, a new morse_regdomain structure for your
 *	   region. Use the naming format `mors_<YOUR ALPHA>_regdom`.
 *	3. Using the `MORSE_REG_RULE` macro, define the blocks of 5G spectrum
 *	   containing your mapped 5G channels.
 *	4. Optional - Add the s1g frequency spectrum for the s1g channels.
 */
#define REG_RULE_KHZ(start, end, bw, gain, eirp, reg_flags)	\
{								\
	.freq_range.start_freq_khz = start,			\
	.freq_range.end_freq_khz = end,				\
	.freq_range.max_bandwidth_khz = bw,			\
	.power_rule.max_antenna_gain = DBI_TO_MBI(gain),	\
	.power_rule.max_eirp = DBM_TO_MBM(eirp),		\
	.flags = reg_flags,					\
	.dfs_cac_ms = 0,					\
}

/**
 * The duty cycle for AP and STA is provided in 100ths of a percent. E.g. 10000 = 100%
 */
#define MORSE_REG_RULE_KHZ(start, end, bw, gain, eirp, reg_flags, duty_cycle_ap,	\
			   duty_cycle_sta, duty_cycle_omit_ctrl_resp, mpsw_min_us,	\
			   mpsw_max_us, mpsw_win_length_us)				\
{											\
	.dot11_reg = REG_RULE_KHZ(start, end, bw, gain, eirp, reg_flags),		\
	.duty_cycle.ap = duty_cycle_ap,							\
	.duty_cycle.sta = duty_cycle_sta,						\
	.duty_cycle.omit_ctrl_resp = duty_cycle_omit_ctrl_resp,				\
	.mpsw.airtime_min_us = mpsw_min_us,						\
	.mpsw.airtime_max_us = mpsw_max_us,						\
	.mpsw.window_length_us = mpsw_win_length_us					\
}

#define MORSE_REG_RULE(start, end, bw, gain, eirp, reg_flags, duty_cycle_ap,			\
		       duty_cycle_sta, duty_cycle_omit_ctrl_resp, mpsw_min_us,			\
		       mpsw_max_us, mpsw_win_length_us)						\
	MORSE_REG_RULE_KHZ(MHZ_TO_KHZ(start), MHZ_TO_KHZ(end), MHZ_TO_KHZ(bw), gain, eirp,	\
			   reg_flags, duty_cycle_ap, duty_cycle_sta, duty_cycle_omit_ctrl_resp, \
			   mpsw_min_us, mpsw_max_us, mpsw_win_length_us)

static struct morse_regdomain mors_au_regdom = {
	.n_reg_rules = 6,
	.alpha2 = "AU",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE(915, 916, 1, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		MORSE_REG_RULE(916, 920, 4, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		MORSE_REG_RULE(920, 928, 8, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),

		/* S1G->11ac Mapped Frequencies */
		/* 27 => 112 */
		MORSE_REG_RULE(5550, 5570, 20, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 29 -> 35 => 116 -> 128 */
		MORSE_REG_RULE(5570, 5650, 80, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 37 -> 51 => 149 -> 177 */
		MORSE_REG_RULE(5735, 5895, 160, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	}
};

static struct morse_regdomain mors_cn_regdom = {
	.n_reg_rules = 0,
	.alpha2 =  "CN",
	.reg_rules = {
		/* S1G Actual Frequencies */

		/* S1G->11ac Mapped Frequencies */
	},
};

static struct morse_regdomain mors_eu_regdom = {
	.n_reg_rules = 4,
	.alpha2 = "EU",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE(863, 868, 1, 0, 16, AUTO_BW, 1000, 280, false, 0, 0, 0),
		MORSE_REG_RULE_KHZ(916400, 919400, 1000, 0, 16, AUTO_BW, 1000, 280, false, 0, 0, 0),

		/* S1G->11ac Mapped Frequencies */
		/* 1 -> 3 => 132 -> 136 */
		MORSE_REG_RULE(5650, 5690, 20, 0, 16, AUTO_BW, 1000, 280, false, 0, 0, 0),
		/* 5 -> 9 => 36 -> 44 */
		MORSE_REG_RULE(5170, 5230, 20, 0, 16, AUTO_BW, 1000, 280, false, 0, 0, 0),
	},
};

static struct morse_regdomain mors_in_regdom = {
	.n_reg_rules = 2,
	.alpha2 = "IN",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE(865, 868, 1, 0, 16, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* S1G->11ac Mapped Frequencies */
		/* 5 -> 9 => 36 -> 44 */
		MORSE_REG_RULE(5170, 5230, 20, 0, 16, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	},
};

static struct morse_regdomain mors_jp_regdom = {
	.n_reg_rules = 2,
	.alpha2 =  "JP",
	.reg_rules = {
		/* S1G Actual Frequencies */
		/* 13 -> 21*/
		MORSE_REG_RULE(922, 928, 1, 0, 16, AUTO_BW, 1000, 1000, true, 2000, 50000, 2000),
		/* S1G->11ac Mapped Frequencies */
		MORSE_REG_RULE(5170, 5330, 80, 0, 16, AUTO_BW, 1000, 1000, true, 2000, 50000, 2000),
	},
};

static struct morse_regdomain mors_kr_regdom = {
	.n_reg_rules = 7,
	.alpha2 =  "KR",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE_KHZ(917500, 921500, 2000, 0, 4, AUTO_BW, 10000, 10000,
				   false, 0, 0, 0),
		MORSE_REG_RULE_KHZ(921500, 923500, 2000, 0, 10, AUTO_BW, 10000, 10000,
				   false, 0, 0, 0),
		MORSE_REG_RULE_KHZ(919500, 923500, 4000, 0, 4, AUTO_BW, 10000, 10000,
				   false, 0, 0, 0),
		/* S1G->11ac Mapped Frequencies */
		/* 1, 2, 3 => 132, 134, 136 */
		MORSE_REG_RULE(5650, 5690, 40, 0, 5, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 5, 6, 7 => 36, 38, 40 */
		MORSE_REG_RULE(5170, 5210, 40, 0, 5, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 9, 10, 11 => 44, 46, 48 */
		MORSE_REG_RULE(5210, 5250, 40, 0, 10, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 8 => 42 */
		MORSE_REG_RULE(5170, 5250, 80, 0, 5, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	},
};

static struct morse_regdomain mors_nz_regdom = {
	.n_reg_rules = 4,
	.alpha2 =  "NZ",
	.reg_rules = {
		MORSE_REG_RULE(920, 928, 8, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* S1G->11ac Mapped Frequencies */
		/* 27 => 112 */
		MORSE_REG_RULE(5550, 5570, 20, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 29 -> 35 => 116 -> 128 */
		MORSE_REG_RULE(5570, 5650, 80, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 37 -> 51 => 149 -> 177 */
		MORSE_REG_RULE(5735, 5895, 160, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	},
};

static struct morse_regdomain mors_sg_regdom = {
	.n_reg_rules = 6,
	.alpha2 =  "SG",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE(866, 869, 2, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		MORSE_REG_RULE(920, 925, 4, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* S1G->11ac Mapped Frequencies */
		/* 7 => 40 */
		MORSE_REG_RULE(5190, 5210, 20, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 9 -> 11 => 44 -> 48 */
		MORSE_REG_RULE(5210, 5250, 40, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 37 -> 43 => 149 -> 161 */
		MORSE_REG_RULE(5735, 5815, 80, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 45 => 165 */
		MORSE_REG_RULE(5815, 5835, 20, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	},
};

static struct morse_regdomain mors_us_regdom = {
	.n_reg_rules = 7,
	.alpha2 =  "US",
	.reg_rules = {
		/* S1G Actual Frequencies */
		MORSE_REG_RULE(902, 904, 2, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		MORSE_REG_RULE(904, 920, 16, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		MORSE_REG_RULE(920, 928, 8, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),

		/* S1G->11ac Mapped Frequencies */
		/* 1 -> 3 => 132 -> 136 */
		MORSE_REG_RULE(5650, 5690, 40, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 5 -> 19 => 36 -> 64 */
		MORSE_REG_RULE(5170, 5330, 160, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 21 -> 35 => 100 -> 128 */
		MORSE_REG_RULE(5490, 5650, 160, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
		/* 37 -> 51 => 149 -> 177 */
		MORSE_REG_RULE(5735, 5895, 160, 0, 30, AUTO_BW, 10000, 10000, false, 0, 0, 0),
	},
};

/* our reg db will now be an array of defined regdomains */
static struct morse_regdomain *mors_regions[] = {
	&mors_au_regdom,
	&mors_cn_regdom,
	&mors_eu_regdom,
	&mors_in_regdom,
	&mors_jp_regdom,
	&mors_kr_regdom,
	&mors_nz_regdom,
	&mors_sg_regdom,
	&mors_us_regdom,
};

const struct morse_regdomain *morse_reg_alpha_lookup(const char *alpha)
{
	int i;

	if (!alpha)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(mors_regions); i++)
		if (!strncmp(mors_regions[i]->alpha2, alpha, strlen(alpha)))
			return mors_regions[i];
	return NULL;
}

/**
 * morse_reg_set_alpha - Set the regulatory domain rules for a given country
 * @alpha: The desired ISO/IEC Alpha2 Country to apply regulatory rules in
 *
 * Finds a set of regulatory rules based on a given alpha code, looking through
 * the internally-defined domains.
 *
 * Return: A pointer to the matching regdomain, defaults to MM.
 *
 */

const struct morse_regdomain *morse_reg_set_alpha(const char *alpha)
{
	const struct morse_regdomain *regdom;

	regdom = morse_reg_alpha_lookup(alpha);
	if (!(regdom) || !alpha)
		return NULL;

	morse_dot11ah_channel_set_map(regdom->alpha2);

	return regdom;
}
EXPORT_SYMBOL(morse_reg_set_alpha);

struct ieee80211_regdomain *morse_regdom_to_ieee80211(const struct morse_regdomain *morse_domain)
{
	struct ieee80211_regdomain *new = kmalloc(sizeof(*new) + (morse_domain->n_reg_rules *
						(sizeof(struct ieee80211_reg_rule))), GFP_KERNEL);
	int i;

	if (!new)
		return NULL;

	new->n_reg_rules = morse_domain->n_reg_rules;
	memcpy(new->alpha2, morse_domain->alpha2, ARRAY_SIZE(morse_domain->alpha2));
	for (i = 0; i < morse_domain->n_reg_rules; i++) {
		memcpy(&new->reg_rules[i], &morse_domain->reg_rules[i].dot11_reg,
		       sizeof(morse_domain->reg_rules[i].dot11_reg));
	}

	return new;
}
EXPORT_SYMBOL(morse_regdom_to_ieee80211);

const struct morse_reg_rule *morse_regdom_get_rule_for_freq(const char *alpha, int frequency)
{
	const struct morse_regdomain *regdom = morse_reg_alpha_lookup(alpha);
	int i;

	for (i = 0; i < regdom->n_reg_rules; i++) {
		if (frequency >= regdom->reg_rules[i].dot11_reg.freq_range.start_freq_khz &&
			frequency <= regdom->reg_rules[i].dot11_reg.freq_range.end_freq_khz)
			return &regdom->reg_rules[i];
	}

	return NULL;
}
EXPORT_SYMBOL(morse_regdom_get_rule_for_freq);

int morse_mac_set_country_info_from_regdom(const struct morse_regdomain *morse_domain,
				struct s1g_operation_parameters *params,
				struct dot11ah_country_ie *country_ie)
{
	const struct ieee80211_freq_range *fr;
	struct country_operating_triplet *oper_triplet;
	int i;
	int start_chan = 0;
	int end_chan = 0;
	int eirp;
	int bw;
	int ret;

	u8 op_bw_mhz = MORSE_OPERATING_CH_WIDTH_DEFAULT;
	u8 pri_bw_mhz = MORSE_PRIM_CH_WIDTH_DEFAULT;
	u8 chan_centre_freq_num = MORSE_OPERATING_CHAN_DEFAULT;
	u8 pri_1mhz_chan_idx = 0;
	u8 pri_ch_op_class = 0;

	if (params) {
		op_bw_mhz = params->op_bw_mhz;
		pri_bw_mhz = params->pri_bw_mhz;
		pri_1mhz_chan_idx = params->pri_1mhz_chan_idx;
		pri_ch_op_class = params->prim_global_op_class;
		chan_centre_freq_num = params->chan_centre_freq_num;
	}

	ret = strscpy(country_ie->country, morse_domain->alpha2,
			ARRAY_SIZE(country_ie->country));

	/* alpha2 has 2 characters */
	if (ret < 2)
		dot11ah_warn("Invalid alpha2 string\n");

	country_ie->country[2] = MORSE_GLOBAL_OPERATING_CLASS_TABLE;

	oper_triplet = &country_ie->ie_triplet;

	oper_triplet->op_triplet_id = MORSE_COUNTRY_OPERATING_TRIPLET_ID;
	oper_triplet->primary_band_op_class = pri_ch_op_class;
	oper_triplet->coverage_class = 0;
	oper_triplet->start_chan = morse_dot11ah_calc_prim_s1g_chan(op_bw_mhz, pri_bw_mhz,
						    chan_centre_freq_num, pri_1mhz_chan_idx);

	oper_triplet->chan_num = 1;

	for (i = 0; i < morse_domain->n_reg_rules; i++) {
		fr = &morse_domain->reg_rules[i].dot11_reg.freq_range;
		eirp = morse_domain->reg_rules[i].dot11_reg.power_rule.max_eirp;
		bw = KHZ_TO_MHZ(morse_domain->reg_rules[i].dot11_reg.freq_range.max_bandwidth_khz);

		if (fr->start_freq_khz > MORSE_S1G_FREQ_MIN_KHZ &&
			fr->end_freq_khz < MORSE_S1G_FREQ_MAX_KHZ) {
			start_chan =  morse_dot11ah_freq_khz_bw_mhz_to_chan(fr->start_freq_khz, bw);
			end_chan = morse_dot11ah_freq_khz_bw_mhz_to_chan(fr->end_freq_khz, bw);
		}
		if (oper_triplet->start_chan >= start_chan &&
			oper_triplet->start_chan < end_chan) {
			/* TODO: SW-7983 - Advertise minimum of EIRP from BCF vs reg rule */
			oper_triplet->max_eirp_dbm = MBM_TO_DBM(eirp);
		}
	}
	return 0;
}
EXPORT_SYMBOL(morse_mac_set_country_info_from_regdom);
