/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <linux/interrupt.h>

#include "mac.h"
#include "bus.h"
#include "raw.h"
#include "debug.h"
#include "dot11ah/dot11ah.h"
#include "skb_header.h"
#include "vendor.h"
#include "vendor_ie.h"


#define FRAGMENTATION_OVERHEAD			(36)

/**
 * Max beacon length limit for 1MHz, MCS0. If the beacon is larger than this it may get
 * fragmented by the FW, which is not permitted by the 802.11 protocol.
 */
#define DOT11AH_1MHZ_MCS0_MAX_BEACON_LENGTH			(764 - FRAGMENTATION_OVERHEAD)

static void morse_beacon_fill_tx_info(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct morse_vif *mors_if,
				int tx_bw_mhz)
{
	int bw_rate_flag_mhz = MORSE_BW_TO_FLAGS(tx_bw_mhz);
	(void) mors;
	(void) skb;

	tx_info->flags |=
		cpu_to_le32(MORSE_TX_CONF_FLAGS_VIF_ID_SET(mors_if->id));
	tx_info->rates[0].mcs = 0;
	tx_info->rates[0].count = 1;
	tx_info->rates[0].flags = cpu_to_le16(bw_rate_flag_mhz);
	mors->debug.mcs_stats_tbl.mcs0.tx_beacons++;
	mors->debug.mcs_stats_tbl.mcs0.tx_success++;
	tx_info->rates[1].mcs = -1;
	tx_info->rates[1].count = 0;
	tx_info->rates[2].mcs = -1;
	tx_info->rates[2].count = 0;
	tx_info->rates[3].mcs = -1;
	tx_info->rates[3].count = 0;
}

static void morse_beacon_tasklet(unsigned long data)
{
	struct morse_skbq *mq;
	struct sk_buff *beacon;
	struct ieee80211_mgmt *beacon_mgmt;
	struct morse *mors = (struct morse *)data;
	struct ieee80211_vif *vif;
	struct morse_vif *mors_if;
	struct morse_skb_tx_info tx_info = {0};
	u8 rps_ie_size;
	u16 vendor_ie_length;
	const u8 *tim_ie;
	int s1g_beacon_len;
	bool short_beacon;
	int tx_bw_mhz;
	struct dot11ah_ies_mask *ies_mask = NULL;
	int ii;
	struct morse_raw *raw;
	const struct chip_if_ops *chip_if_ops;

	if (!mors || !mors->cfg)
		return;

	vif = morse_get_ap_vif(mors);

	if ((!vif) ||
		((vif->type != NL80211_IFTYPE_AP) &&
		(vif->type != NL80211_IFTYPE_ADHOC)))
		return;

	raw = &mors->custom_configs.raw;
	chip_if_ops = mors->cfg->ops;

	/* If RAW is enabled and spreading is enabled, schedule an update of the RPS IE to run after
	 * this tasklet.
	 */
	if (raw->enabled) {
		for (ii = 0; ii < MAX_NUM_RAWS; ii++) {
			struct morse_raw_config *config = raw->configs[ii];

			if (config && config->enabled && config->nominal_sta_per_beacon) {
				schedule_work(&raw->refresh_aids_work);
				break;
			}
		}
	}

	ies_mask = morse_dot11ah_ies_mask_alloc();
	if (ies_mask == NULL)
		goto exit;

	mors_if = ieee80211_vif_to_morse_vif(vif);
	short_beacon = (mors_if->dtim_count != 0);

	beacon = ieee80211_beacon_get(mors->hw, vif);
	if (beacon == NULL) {
		morse_err(mors, "ieee80211_beacon_get failed\n");
		goto exit;
	}

	mq = chip_if_ops->skbq_bcn_tc_q(mors);
	if (!mq) {
		morse_err(mors, "chip_if_ops->skbq_bcn_tc_q(mors); failed, no matching Q found\n");
		kfree_skb(beacon);
		goto exit;
	}

	if (morse_skbq_size(mq) > 0) {
		morse_err(mors, "Previous beacon not consumed yet, dropping beacon request\n");
		kfree_skb(beacon);
		goto exit;
	}

	beacon_mgmt = (struct ieee80211_mgmt *)beacon->data;

	tim_ie = cfg80211_find_ie(WLAN_EID_TIM, beacon_mgmt->u.beacon.variable,
				   beacon->len -
				   (ieee80211_hdrlen(beacon_mgmt->frame_control)
				    + 12));

	if (tim_ie) {
		short_beacon = (tim_ie[2] != 0);
		if (tim_ie[2] == 0)
			mors_if->dtim_count = 0;
	}

	if (mors_if->ecsa_chan_configured) {
		short_beacon = false;
		morse_dbg(mors, "Tx full beacon. dtim_cnt=%d\n",
							((mors_if->dtim_count + 1) % vif->bss_conf.dtim_period));
	}

	/* IBSS does not support short beacons */
	if (vif->type == NL80211_IFTYPE_ADHOC)
		short_beacon = false;

	s1g_beacon_len = morse_dot11ah_11n_to_s1g_tx_packet_size(vif, beacon,
									short_beacon, ies_mask);

	if (s1g_beacon_len < 0) {
		morse_dbg(mors, "s1g packet len < 0\n");
		kfree_skb(beacon);
		goto exit;
	}

	vendor_ie_length = morse_vendor_ie_get_ies_length(mors_if, MORSE_VENDOR_IE_TYPE_BEACON);
	if (vendor_ie_length > 0)
		s1g_beacon_len += vendor_ie_length;

	rps_ie_size = morse_raw_get_rps_ie_size(mors);
	if (mors->custom_configs.raw.enabled && (rps_ie_size != 0))
		s1g_beacon_len += rps_ie_size + 2;

	if (beacon->len + skb_tailroom(beacon) < s1g_beacon_len) {
		struct sk_buff *skb2;

		skb2 = skb_copy_expand(beacon, skb_headroom(beacon),
				       s1g_beacon_len - beacon->len,
				       GFP_KERNEL);
		/* Just say we transmitted it */
		ieee80211_tx_status(mors->hw, beacon);
		beacon = skb2;
	}

	/* Insert RPS IE if RAW is enabled. We will place it at the end and it will be reordered by the
	 * 11n to s1g layer.
	 */
	if (mors->custom_configs.raw.enabled && (rps_ie_size != 0)) {
		u8 *dst = skb_put(beacon, rps_ie_size + 2);
		u8 *src = morse_raw_get_rps_ie(mors);

		BUG_ON(!src);

		*dst++ = WLAN_EID_S1G_RPS;
		*dst++ = rps_ie_size;
		memcpy(dst, src, rps_ie_size);
	}

	if (vendor_ie_length > 0)
		morse_vendor_ie_add_ies(mors_if, beacon, MORSE_VENDOR_IE_TYPE_BEACON);

	morse_dot11ah_ies_mask_clear(ies_mask);
	morse_dot11ah_11n_to_s1g_tx_packet(vif, beacon, s1g_beacon_len,
									short_beacon, ies_mask);

	mors_if->dtim_count = (mors_if->dtim_count + 1) %
		vif->bss_conf.dtim_period;

	if (beacon->len >= DOT11AH_1MHZ_MCS0_MAX_BEACON_LENGTH &&
			mors_if->custom_configs->channel_info.pri_bw_mhz == 1) {
		morse_err_ratelimited(mors, "S1G beacon is too big for 1MHz bandwidth (%u); dropping\n",
				beacon->len);
		goto exit;
	}

	/* Use full operating BW if subbands are disabled */
	tx_bw_mhz = (mors->enable_subbands == SUBBANDS_MODE_DISABLED) ?
			mors->custom_configs.channel_info.op_bw_mhz :
			mors->custom_configs.channel_info.pri_bw_mhz;
	morse_beacon_fill_tx_info(
		mors, &tx_info, beacon,
		mors_if, tx_bw_mhz);
	morse_skbq_skb_tx(mq, beacon, &tx_info, MORSE_SKB_CHAN_BEACON);

	/* TODO: currently due to the way we implement firmware beaconing,
	 * these might still get sent before the DTIM beacon.
	 */
	if (!test_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags))
		morse_mac_send_buffered_bc(mors);

exit:
	morse_dot11ah_ies_mask_free(ies_mask);
}

void morse_beacon_irq_handle(struct morse *mors)
{
	/* You are not safe, don't try to be smart */
	tasklet_schedule(&mors->bcon_tasklet);
}

int morse_beacon_init(struct morse *mors)
{
	morse_hw_irq_enable(mors, MORSE_INT_BEACON_NUM, true);
	tasklet_init(&mors->bcon_tasklet, morse_beacon_tasklet,
		     (unsigned long)mors);
	return 0;
}

void morse_beacon_finish(struct morse *mors)
{
	tasklet_kill(&mors->bcon_tasklet);
}
