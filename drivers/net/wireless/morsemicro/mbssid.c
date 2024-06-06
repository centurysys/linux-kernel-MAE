/*
 * Copyright 2023 Morse Micro
 */
#include <linux/timer.h>
#include <linux/bitfield.h>

#include "morse.h"
#include "mac.h"
#include "debug.h"
#include "mbssid.h"

int morse_command_process_bssid_info(struct morse_vif *mors_if, struct morse_cmd_mbssid *cmd_mbssid)
{
	int vif_id;
	struct morse_vif *mors_tx_if;
	struct ieee80211_vif *vif_tmp = NULL;
	struct ieee80211_vif *vif;
	struct morse *mors;

	if (!mors_if)
		return -EFAULT;

	vif = morse_vif_to_ieee80211_vif(mors_if);
	mors = morse_vif_to_morse(mors_if);

	if (!morse_mbssid_ie_enabled(mors))
		return -ENOENT;

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		vif_tmp = morse_get_vif_from_vif_id(mors, vif_id);

		if (!vif_tmp)
			continue;

		if (strcmp(morse_vif_name(vif_tmp), cmd_mbssid->transmitter_iface) == 0) {
			/* Transmitter iface found. Let's break here */
			break;
		}
	}

	if (!vif_tmp)
		return -ENOENT;

	mors_tx_if = ieee80211_vif_to_morse_vif(vif_tmp);

	if (cmd_mbssid->max_bssid_indicator > mors->max_vifs)
		mors_if->mbssid_info.max_bssid_indicator = mors->max_vifs;
	else
		mors_if->mbssid_info.max_bssid_indicator = cmd_mbssid->max_bssid_indicator;
	mors_if->mbssid_info.transmitter_vif_id = mors_tx_if->id;

	if (mors_if->id != mors_tx_if->mbssid_info.transmitter_vif_id) {
		/*
		 * Disable the regular beacon timers (in chip and driver) for this interface,
		 * and instead set up a new timer to retrieve the required IEs for this interface
		 * which will be inserted into the transmitting interface's beacons.
		 */
		morse_beacon_finish(mors_if);
		morse_cmd_stop_beacon_timer(mors, mors_if);
	}

	return 0;
}

struct sk_buff *morse_mac_get_mbssid_beacon_ies(struct morse_vif *mors_vif)
{
	struct ieee80211_vif *vif;
	struct morse *mors;
	struct ieee80211_mgmt *non_tx_beacon;

	mors = morse_vif_to_morse(mors_vif);
	vif = morse_vif_to_ieee80211_vif(mors_vif);
	if (mors_vif->beacon_buf)
		return mors_vif->beacon_buf;

	mors_vif->beacon_buf = ieee80211_beacon_get(mors->hw, vif);
	if (!mors_vif->beacon_buf) {
		MORSE_DBG(mors, "MBSSID: ieee80211_beacon_get failed, id %d\n", mors_vif->id);
		return NULL;
	}
	MORSE_DBG(mors, "MBSSID: Got beacon for VIF %d from mac80211\n", mors_vif->id);
	non_tx_beacon = (struct ieee80211_mgmt *)mors_vif->beacon_buf->data;
	mors_vif->ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, non_tx_beacon->u.beacon.variable,
					     mors_vif->beacon_buf->len -
					     ieee80211_hdrlen(non_tx_beacon->frame_control));
	return mors_vif->beacon_buf;
}

/**
 * morse_insert_mbssid_ie_subelem_head - Insert an MBSSID IE subelement into a frame and set the
 *                                       header fields. The rest of the element should then be
 *                                       updated by the caller.
 *
 * @mors_if:      Non-transmitting AP iface
 * @ie_buf:       Pointer to buffer filling IE
 * @ssid_ie_len:  Length field from the SSID IE, which is used to calculate the subelement length.
 */
static void morse_insert_mbssid_ie_subelem_head(struct morse_vif *mors_if, u8 *ie_buf,
						int ssid_ie_len)
{
	struct mbssid_subelement sub_elem;
	u8 ie_size = sizeof(sub_elem.element_id) + sizeof(sub_elem.len);

	sub_elem.element_id = MBSSID_SUBELEMENT_NONTX_BSSID_PROFILE;
	sub_elem.len = ssid_ie_len + sizeof(sub_elem.ssid_ie) + sizeof(sub_elem.idx_ie);
	memcpy(ie_buf, &sub_elem, ie_size);
}

/**
 * morse_insert_mbssid_index_ie - Insert MBSSID Index IE of one subelement
 *
 * @mors_if:      Non-transmitting AP iface
 * @ie_buf:       Pointer to buffer filling IE
 */
static void morse_insert_mbssid_index_ie(struct morse_vif *mors_if, u8 *ie_buf)
{
	struct sub_elem_mbssid_idx_ie idx_ie;
	struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_if);

	idx_ie.element_id = WLAN_EID_MULTI_BSSID_IDX;
	idx_ie.len = sizeof(idx_ie.mbssid_index);
	idx_ie.mbssid_index.bssid_index = mors_if->id;
	idx_ie.mbssid_index.dtim_period = vif->bss_conf.dtim_period;
	idx_ie.mbssid_index.dtim_count = (mors_if->dtim_count + 1) % vif->bss_conf.dtim_period;
	memcpy(ie_buf, &idx_ie, sizeof(idx_ie));
}

/**
 * morse_insert_mbssid_ie_subelem - Insert subelement IE of one non-transmitting bss
 *
 * @mors_if:   Non-transmitting AP iface
 * @buf:       Pointer to buffer filling IE
 * @ie_buf:    Head pointer to buffer filling IE
 */
static void morse_insert_mbssid_ie_subelem(struct morse_vif *mors_if, u8 **buf, u8 *ie_buf)
{
	u8 ssid_ie_len;
	struct mbssid_ie mbssid_ie;
	struct sub_elem_ssid_ie *elem_ssid_ie;
	struct sub_elem_mbssid_idx_ie idx_ie;
	const u8 *ssid_ie = mors_if->ssid_ie;

	if (!ssid_ie)
		return;

	elem_ssid_ie = (struct sub_elem_ssid_ie *)ssid_ie;
	ssid_ie_len = elem_ssid_ie->len;

	if ((*buf + ssid_ie_len + sizeof(mbssid_ie)) - ie_buf > MBSSID_IE_SIZE_MAX)
		return;

	morse_insert_mbssid_ie_subelem_head(mors_if, *buf, ssid_ie_len);
	*buf += sizeof(mbssid_ie.sub_elem.element_id) + sizeof(mbssid_ie.sub_elem.len);

	memcpy(*buf, ssid_ie, sizeof(*elem_ssid_ie) + ssid_ie_len);
	*buf += sizeof(*elem_ssid_ie) + ssid_ie_len;

	morse_insert_mbssid_index_ie(mors_if, *buf);
	*buf += sizeof(idx_ie);
}

void morse_mbssid_insert_ie(struct morse_vif *mors_if, struct morse *mors,
			    struct dot11ah_ies_mask *ies_mask)
{
	int vif_id;
	u8 mbssid_ie_buf[MBSSID_IE_SIZE_MAX];
	struct mbssid_ie mbssid_ie;
	u8 *tmp = mbssid_ie_buf;

	if (!morse_mbssid_ie_enabled(mors))
		return;

	if (mors_if->mbssid_info.max_bssid_indicator <= 1)
		return;

	/* We need only non-Tx BSSID in IE. Excluding Tx BSS. */
	*tmp = mors_if->mbssid_info.max_bssid_indicator - 1;
	tmp += sizeof(mbssid_ie.max_bssid_indicator);

	for (vif_id = 0; vif_id < (mors->max_vifs); vif_id++) {
		struct morse_vif *mors_if_tmp;
		struct ieee80211_vif *vif_tmp;

		vif_tmp = morse_get_vif_from_vif_id(mors, vif_id);

		if (!vif_tmp)
			continue;

		if (vif_tmp->type != NL80211_IFTYPE_AP)
			continue;

		mors_if_tmp = ieee80211_vif_to_morse_vif(vif_tmp);

		if (mors_if_tmp->id == mors_if->mbssid_info.transmitter_vif_id)
			continue;

		if (!morse_mac_get_mbssid_beacon_ies(mors_if_tmp))
			continue;

		morse_insert_mbssid_ie_subelem(mors_if_tmp, &tmp, mbssid_ie_buf);
	}
	if ((tmp - mbssid_ie_buf) > sizeof(mbssid_ie)) {
		morse_dot11ah_insert_element(ies_mask,
					     WLAN_EID_MULTIPLE_BSSID,
					     mbssid_ie_buf, (tmp - mbssid_ie_buf));
	}
}

#if KERNEL_VERSION(5, 1, 0) < LINUX_VERSION_CODE
int morse_process_beacon_from_mbssid_ie(struct morse *mors, struct sk_buff *skb,
					struct dot11ah_ies_mask *ies_mask,
					struct ieee80211_vif *vif,
					struct morse_skb_rx_status *hdr_rx_status,
					struct ieee80211_rx_status *rx_status, int length_11n)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211_mgmt *mgmt;
	const struct ieee80211_ext *s1g_beacon;
	u8 new_bssid[ETH_ALEN];
	const u8 *mbssid_index_ie;
	const u8 *mbssid_ie;
	int mbssid_ie_len;
	const struct element *sub;
	int ret = 0;
	int max_bssid_indicator;
	u32 mbssid_ie_offset;
	struct mbssid_ie ie_elem;
	struct ieee80211_hw *hw = mors->hw;

	if (!ies_mask->ies[WLAN_EID_MULTIPLE_BSSID].ptr)
		return -ENOMEM;

	mbssid_ie = ies_mask->ies[WLAN_EID_MULTIPLE_BSSID].ptr;
	mbssid_ie_len = ies_mask->ies[WLAN_EID_MULTIPLE_BSSID].len;
	max_bssid_indicator = mbssid_ie[0];
	mbssid_ie_offset = sizeof(ie_elem.max_bssid_indicator) +
	    sizeof(ie_elem.sub_elem.element_id) + sizeof(ie_elem.sub_elem.len);

	for_each_element(sub, mbssid_ie + mbssid_ie_offset, mbssid_ie_len - mbssid_ie_offset) {
		struct sk_buff *skb_beacon;
		int mbssid_index;

		if (sub->id == WLAN_EID_SSID) {
			ies_mask->ies[WLAN_EID_SSID].ptr = (u8 *)sub->data;
			ies_mask->ies[WLAN_EID_SSID].len = (u8)sub->datalen;
		}

		if (sub->id != WLAN_EID_MULTI_BSSID_IDX ||
		    sub->datalen < sizeof(ie_elem.sub_elem.idx_ie.mbssid_index))
			continue;

		mbssid_index_ie = sub->data;
		/* This block is copied from cfg80211_parse_mbssid_data */
		if (!mbssid_index_ie || mbssid_index_ie[0] == 0 || mbssid_index_ie[0] > 46)
			continue;
		mbssid_index = mbssid_index_ie[0];

		skb_beacon = skb_copy(skb, GFP_ATOMIC);
		if (!skb_beacon)
			continue;

		s1g_beacon = (struct ieee80211_ext *)skb_beacon->data;
		hdr = (struct ieee80211_hdr *)skb_beacon->data;
		mgmt = (struct ieee80211_mgmt *)hdr;

		cfg80211_gen_new_bssid(s1g_beacon->u.s1g_beacon.sa,
				       max_bssid_indicator, mbssid_index, new_bssid);
		memcpy((void *)s1g_beacon->u.s1g_beacon.sa, new_bssid, ETH_ALEN);
		morse_mac_rx_status(mors, hdr_rx_status, rx_status, skb_beacon);
		memcpy(IEEE80211_SKB_RXCB(skb_beacon), rx_status, sizeof(*rx_status));

		if (skb_beacon->len + skb_tailroom(skb_beacon) < length_11n) {
			struct sk_buff *skb2;

			skb2 = skb_copy_expand(skb_beacon, skb_headroom(skb_beacon),
					       length_11n - skb_beacon->len, GFP_KERNEL);
			morse_mac_skb_free(mors, skb_beacon);
			skb_beacon = skb2;
			if (!skb_beacon)
				return -ENOMEM;
		}
		morse_dot11ah_s1g_to_11n_rx_packet(vif, skb_beacon, length_11n, ies_mask);

		if (skb_beacon->len > 0)
			ieee80211_rx_irqsafe(hw, skb_beacon);
		else
			morse_mac_skb_free(mors, skb_beacon);
	}
	return ret;
}
#endif

int morse_mbssid_ie_deinit_bss(struct morse *mors, struct morse_vif *mors_if)
{
	if (!mors || !mors_if)
		return -EINVAL;

	if (!morse_mbssid_ie_enabled(mors))
		return -ENOENT;

	/* Free up beacon buffer */
	if (mors_if->beacon_buf) {
		dev_kfree_skb_any(mors_if->beacon_buf);
		mors_if->beacon_buf = NULL;
	}

	return 0;
}

bool morse_mbssid_ie_enabled(struct morse *mors)
{
	return mors->enable_mbssid_ie;
}
