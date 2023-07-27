/*
 * Copyright 2022 Morse Micro
 *
 */

#include "mac.h"

int morse_mac_bw_from_skb(struct morse *mors, u16 tx_rate_flags, int max_bw_mhz)
{
	int bw_max_scale;
	struct ieee80211_conf *conf = &mors->hw->conf;

	/* set the bandwidth scaler depending on our channel width */
	switch (max_bw_mhz) {
	case 1:
		return max_bw_mhz;
	case 2:
		bw_max_scale = 2;
		break;
	case 4:
	case 8:
		bw_max_scale = 4;
		break;
	default:
		return max_bw_mhz;
	}

	/* For a VHT STA, the VHT mcs rate flag will always be set (even for sub 80 rates)
	 * 4MHz channels are CHAN_WIDTH_80, 8MHz are CHAN_WIDTH_160.
	 * Minstrel only supports up to VHT80, so we don't parse IEEE80211_TX_RC_160_MHZ_WIDTH
	 *                      VHT Rate Width
	 * | S1G Operating BW | 20 | 40 | 80 |
	 * |------------------|----|----|----|
	 * |         4        | 1  | 2  | 4  |
	 * |         8        | 2  | 4  | 8  |
	 */
	if (tx_rate_flags & IEEE80211_TX_RC_VHT_MCS) {
		if ((conf->chandef.width != NL80211_CHAN_WIDTH_80) &&
		    (conf->chandef.width != NL80211_CHAN_WIDTH_160))
			return max_bw_mhz;
		if (tx_rate_flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			return max_bw_mhz;
		else if (tx_rate_flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			return max_bw_mhz/2;
		else
			return max_bw_mhz/bw_max_scale;
	}

	/* Can't do subbands for channel does not support HT40 (width != 40MHz) */
	if (conf->chandef.width != NL80211_CHAN_WIDTH_40)
		return max_bw_mhz;

	/* For an HT STA - we map HT 20 and 40;
	 * Map 40MHz to highest BW.
	 * Map 20MHz to least BW (1MHz in 2MHz, or 2MHz in 4MHz or 4MHz in 8MHz)
	 */
	if (tx_rate_flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		return max_bw_mhz;
	else
		return max_bw_mhz/2;
}

void morse_rc_sta_fill_tx_rates(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct ieee80211_sta *sta,
				int tx_bw_mhz,
				bool rts_allowed)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct morse_sta *mors_sta = NULL;
	int pri_bw_mhz = mors->custom_configs.channel_info.pri_bw_mhz;
	int i;
	int mcs;

	if (sta != NULL)
		mors_sta = (struct morse_sta *)sta->drv_priv;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		int bw_rate_flag_mhz;

		if (info->control.rates[0].idx >= 0 &&
		    info->control.rates[0].flags &
		    (IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_VHT_MCS)) {
			if (ieee80211_is_data_qos(hdr->frame_control)) {
				if (morse_mac_is_subband_enable()) {
					/* For data packets, update BW based on ministrel */
					tx_bw_mhz = morse_mac_bw_from_skb(mors, info->control.rates[i].flags,
									 tx_bw_mhz);
					/* Place a floor on the tx bw from the s1g bss params */
					if (mors_sta)
						tx_bw_mhz = max(pri_bw_mhz, tx_bw_mhz);
				}
			}
			mcs = info->control.rates[i].idx;

			if (info->control.rates[0].flags & IEEE80211_TX_RC_VHT_MCS)
				mcs = ieee80211_rate_get_vht_mcs(&info->control.rates[i]);

			tx_info->rates[i].mcs = mcs;
			tx_info->rates[i].count = info->control.rates[i].count;
		} else if (morse_get_ap_vif(mors) != NULL &&
				morse_get_ap_vif(mors)->type == NL80211_IFTYPE_ADHOC) {
			/* If IBSS node; use simple rate table:
			 *  [0] => MCS3, 2 attempts
			 *  [1] => MCS2, 2 attempts
			 *  [2] => MCS1, 2 attempts
			 *  [3] => MCS0, 2 attempts
			 */
			int max_rate_tries = morse_mac_get_max_rate_tries();
			int max_rates = morse_mac_get_max_rate();

			if (i < max_rates)
				tx_info->rates[i].mcs = ((IEEE80211_TX_MAX_RATES - 1) - i);
			else
				tx_info->rates[i].mcs = -1;

			tx_info->rates[i].count = min_t(u8, max_rate_tries, 2);
		} else {
			/* Legacy Rates (not an MCS index).
			 * We currently don't support passing rates, so default to
			 * MCS0.
			 */
			tx_info->rates[i].mcs = 0;
			tx_info->rates[i].count = 4;
		}

		bw_rate_flag_mhz = (tx_bw_mhz == 1) ? MORSE_SKB_RATE_FLAGS_1MHZ :
				   (tx_bw_mhz == 2) ? MORSE_SKB_RATE_FLAGS_2MHZ :
				   (tx_bw_mhz == 4) ? MORSE_SKB_RATE_FLAGS_4MHZ :
				   (tx_bw_mhz == 8) ? MORSE_SKB_RATE_FLAGS_8MHZ :
				   MORSE_SKB_RATE_FLAGS_2MHZ;


		tx_info->rates[i].flags = cpu_to_le16(bw_rate_flag_mhz);
	}
}

