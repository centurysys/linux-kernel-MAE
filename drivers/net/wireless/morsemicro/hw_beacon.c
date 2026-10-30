/*
 * Copyright 2017-2023 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <linux/interrupt.h>
#include <linux/ieee80211.h>

#include "mac.h"
#include "beacon.h"
#include "debug.h"
#include "dot11ah/dot11ah.h"
#include "skb_header.h"
#include "utils.h"
#include "mbssid.h"

static const u8 morse_supported_beacon_offload_ies[] = {
		WLAN_EID_S1G_BCN_COMPAT,
		WLAN_EID_TIM,
		WLAN_EID_S1G_CAPABILITIES,
		WLAN_EID_S1G_OPERATION,
		WLAN_EID_S1G_SHORT_BCN_INTERVAL,
		WLAN_EID_SSID,
		WLAN_EID_VENDOR_SPECIFIC,
};

#define BEACON_TEMPLATE_IE_COUNT ARRAY_SIZE(morse_supported_beacon_offload_ies)

static int beacon_offload_get_cmd_size(int ie_len, struct dot11ah_ies_mask *ies_mask)
{
	int size;

	size = sizeof(struct morse_cmd_req_beacon_offload);
	size += sizeof(struct morse_cmd_beacon_offload_tlv_dtim_cnt);
	size += sizeof(struct morse_cmd_beacon_offload_tlv_frame_ctrl);
	size += sizeof(struct morse_cmd_beacon_offload_tlv_change_seq);
	size += sizeof(struct morse_cmd_beacon_offload_tlv_tx_info);
	size += sizeof(struct morse_cmd_beacon_offload_tlv_ies) + ie_len;

	if (ies_mask->ies[WLAN_EID_SSID].len != 0)
		size += sizeof(struct morse_cmd_beacon_offload_tlv_cssid);

	return size;
}

/**
 * beacon_get_next_dtim_count - Return the DTIM count @count beacons in the future
 *
 * The DTIM count decrements each beacon interval, wrapping from 0 to (dtim_period - 1).
 *
 * @current_dtim_count: DTIM count in the current beacon
 * @dtim_period: DTIM period configured for the BSS
 * @count: Number of beacon intervals to advance
 *
 * Return: DTIM count @count beacons later.
 */
static inline u8 beacon_get_next_dtim_count(u16 current_dtim_count, u16 dtim_period,
					     unsigned int count)
{
	return dtim_period ? (current_dtim_count + dtim_period - count) % dtim_period : 0;
}

/**
 * beacon_get_previous_dtim_count - Return the DTIM count @count beacons in the past
 *
 * The DTIM count decrements each beacon interval, wrapping from 0 to (dtim_period - 1).
 *
 * @current_dtim_count: DTIM count in the current beacon
 * @dtim_period: DTIM period configured for the BSS
 * @count: Number of beacon intervals to go back
 *
 * Return: DTIM count @count beacons earlier.
 */
static inline u8 beacon_get_previous_dtim_count(u16 current_dtim_count, u16 dtim_period,
						 unsigned int count)
{
	return dtim_period ? (current_dtim_count + count) % dtim_period : 0;
}

/**
 * increment_mac80211_dtim_count - Advance mac80211's DTIM count to @expected_dtim
 *
 * Repeatedly fetches beacons from mac80211 (which decrements its internal DTIM
 * counter each call) until the DTIM count in the beacon matches @expected_dtim.
 *
 * @mors: Morse device
 * @vif: Virtual interface
 * @expected_dtim: Target DTIM count to synchronise to
 *
 * Return: true on success, false if a beacon could not be retrieved from mac80211.
 */
static bool increment_mac80211_dtim_count(struct morse *mors, struct ieee80211_vif *vif,
					    u8 expected_dtim)
{
	struct sk_buff *beacon;
	struct ieee80211_mgmt *beacon_mgmt;
	const u8 *tim_ie;
	u8 current_dtim;

	do {
		beacon = MORSE_IEEE_BEACON_GET(mors, vif);

		if (!beacon) {
			MORSE_BEACON_ERR(mors, "%s: ieee80211_beacon_get failed\n", __func__);
			return false;
		}
		beacon_mgmt = (struct ieee80211_mgmt *)beacon->data;
		tim_ie = cfg80211_find_ie(WLAN_EID_TIM, beacon_mgmt->u.beacon.variable,
			beacon->len - (ieee80211_hdrlen(beacon_mgmt->frame_control)
				+ 12));
		current_dtim = tim_ie[2];
		kfree_skb(beacon);

	} while (current_dtim != expected_dtim);

	return true;
}

void morse_hw_beacon_update_mac80211_dtim_count(struct morse_vif *mors_vif, u16 dtim_count)
{
	struct morse *mors;
	struct ieee80211_vif *vif;
	unsigned int skipped_tasklet_events;
	u8 dtim_period;
	u8 expected_dtim;

	vif = morse_vif_to_ieee80211_vif(mors_vif);
	mors = morse_vif_to_morse(mors_vif);

	lockdep_assert_held(&mors_vif->beacon_offload.lock);

	dtim_period = vif->bss_conf.dtim_period;
	/* Set mac80211 dtim count to the value before our expected count */
	expected_dtim = beacon_get_previous_dtim_count(dtim_count, dtim_period, 1);
	MORSE_BEACON_DBG(mors, "%s: updating the dtim count: %u", __func__, expected_dtim);
	skipped_tasklet_events = mors_vif->beacon_offload.skipped_tasklet_events;
	mors_vif->beacon_offload.skipped_tasklet_events = 0;

	/* Account for any skipped beacons during the beacon offload stop command */
	if (skipped_tasklet_events) {
		expected_dtim = beacon_get_next_dtim_count(expected_dtim, dtim_period,
							   skipped_tasklet_events);
		MORSE_BEACON_DBG(mors,
				"%s: decremented expected dtim by %d due to skipped beacon tasklet(s). New expected dtim %d\n",
				__func__, skipped_tasklet_events, expected_dtim);
	}

	increment_mac80211_dtim_count(mors, vif, (u8)expected_dtim);

	mors_vif->dtim_count = dtim_count;
}

/**
 * hw_beacon_pack_tlv_hdr - Pack a TLV header with a given tag and length
 *
 * @tag: Tag to pack
 * @len: Length of the TLV including the hdr
 */
static inline void hw_beacon_pack_tlv_hdr(struct morse_cmd_beacon_offload_tlv_hdr *hdr,
					  u16 tag, u16 len)
{
	hdr->tag = cpu_to_le16(tag);
	hdr->len = cpu_to_le16(len - sizeof(*hdr));
}

static u8 *beacon_offload_add_frame_ctrl_tlv(struct morse *mors, u8 *buf, __le16 fc)
{
	struct morse_cmd_beacon_offload_tlv_frame_ctrl *fc_tlv =
		(struct morse_cmd_beacon_offload_tlv_frame_ctrl *)buf;

	hw_beacon_pack_tlv_hdr(&fc_tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_FRAME_CTRL,
			       sizeof(*fc_tlv));

	MORSE_BEACON_DBG(mors, "%s: packing frame control (len: %d)\n", __func__, fc_tlv->hdr.len);
	memcpy(fc_tlv->frame_ctrl, &fc, sizeof(fc_tlv->frame_ctrl));

	return buf + sizeof(*fc_tlv);
}

static u8 *beacon_offload_add_change_seq_tlv(struct morse *mors, u8 *buf, u16 change_seq)
{
	struct morse_cmd_beacon_offload_tlv_change_seq *tlv =
		(struct morse_cmd_beacon_offload_tlv_change_seq *)buf;

	hw_beacon_pack_tlv_hdr(&tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_CHANGE_SEQ,
			       sizeof(*tlv));

	MORSE_BEACON_DBG(mors, "%s: packing change seq (len: %d)\n", __func__, tlv->hdr.len);
	tlv->change_seq = cpu_to_le16(change_seq);

	return buf + sizeof(*tlv);
}

static u8 *beacon_offload_add_dtim_count_tlv(struct morse *mors, u8 *buf, u16 dtim_cnt)
{
	struct morse_cmd_beacon_offload_tlv_dtim_cnt *tlv =
		(struct morse_cmd_beacon_offload_tlv_dtim_cnt *)buf;

	hw_beacon_pack_tlv_hdr(&tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_DTIM_CNT,
			       sizeof(*tlv));

	MORSE_BEACON_DBG(mors, "%s: packing DTIM count: (len: %d)\n", __func__, tlv->hdr.len);
	tlv->dtim_cnt = cpu_to_le16(dtim_cnt);

	return buf + sizeof(*tlv);
}

static u8 *beacon_offload_add_tx_info_tlv(struct morse *mors, u8 *buf, u8 bw_mhz)
{
	struct morse_cmd_beacon_offload_tlv_tx_info *tlv =
		(struct morse_cmd_beacon_offload_tlv_tx_info *)buf;

	hw_beacon_pack_tlv_hdr(&tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_TX_INFO,
			       sizeof(*tlv));

	MORSE_BEACON_DBG(mors, "%s: packing tx info: (len: %d)\n", __func__, tlv->hdr.len);
	tlv->bw_mhz = bw_mhz;

	return buf + sizeof(*tlv);
}

static u8 *beacon_offload_add_cssid_tlv(struct morse *mors, u8 *buf, u32 cssid)
{
	struct morse_cmd_beacon_offload_tlv_cssid *tlv =
		(struct morse_cmd_beacon_offload_tlv_cssid *)buf;

	hw_beacon_pack_tlv_hdr(&tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_CSSID,
			       sizeof(*tlv));

	MORSE_BEACON_DBG(mors, "%s: packing CSSID (len: %d)\n", __func__, tlv->hdr.len);
	memcpy(&tlv->cssid, &cssid, MORSE_CMD_BEACON_OFFLOAD_CSSID_LEN);

	return buf + sizeof(*tlv);
}

static u8 *beacon_offload_add_ies_tlv(struct morse *mors, u8 *buf, u8 *ies, int ies_length)
{
	struct morse_cmd_beacon_offload_tlv_ies *tlv =
		(struct morse_cmd_beacon_offload_tlv_ies *)buf;

	hw_beacon_pack_tlv_hdr(&tlv->hdr, MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_IES,
			       sizeof(*tlv) + ies_length);

	MORSE_BEACON_DBG(mors, "%s: packing IEs (len: %d)\n", __func__, tlv->hdr.len);
	memcpy(tlv->buf, ies, ies_length);

	return buf + sizeof(*tlv) + ies_length;
}

void morse_hw_beacon_offload_dump_cmd(struct morse *mors, struct morse_cmd_req_beacon_offload *req)
{
	struct morse_cmd_beacon_offload_tlv_hdr *tlv;
	struct morse_cmd_beacon_offload_tlv_dtim_cnt *dtim = NULL;
	struct morse_cmd_beacon_offload_tlv_frame_ctrl *fc = NULL;
	struct morse_cmd_beacon_offload_tlv_change_seq *change = NULL;
	struct morse_cmd_beacon_offload_tlv_cssid *cssid = NULL;
	struct morse_cmd_beacon_offload_tlv_ies *ies = NULL;
	struct morse_cmd_beacon_offload_tlv_tx_info *tx = NULL;

	u8 *end = ((u8 *)req) + le16_to_cpu(req->hdr.len) + sizeof(req->hdr);

	u32 flags = le32_to_cpu(req->flags);
	bool start = (flags & MORSE_CMD_BEACON_OFFLOAD_FLAGS_START) ? true : false;

	/* If no logs, just return */
	if (!morse_log_is_enabled(FEATURE_ID_BEACON, MORSE_MSG_DEBUG))
		return;

	MORSE_BEACON_DBG(mors, "%s: beacon offload: %s", __func__, start ? "start" : "stop");

	if (!start)
		return;

	tlv = (struct morse_cmd_beacon_offload_tlv_hdr *)req->variable;
	while (((u8 *)tlv) < end) {
		u16 tag = le16_to_cpu(tlv->tag);

		switch (tag) {
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_DTIM_CNT:
			dtim = (struct morse_cmd_beacon_offload_tlv_dtim_cnt *)tlv;
			break;
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_FRAME_CTRL:
			fc = (struct morse_cmd_beacon_offload_tlv_frame_ctrl *)tlv;
			break;
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_CHANGE_SEQ:
			change = (struct morse_cmd_beacon_offload_tlv_change_seq *)tlv;
			break;
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_CSSID:
			cssid = (struct morse_cmd_beacon_offload_tlv_cssid *)tlv;
			break;
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_IES:
			ies = (struct morse_cmd_beacon_offload_tlv_ies *)tlv;
			break;
		case MORSE_CMD_BEACON_OFFLOAD_TLV_TAG_TX_INFO:
			tx = (struct morse_cmd_beacon_offload_tlv_tx_info *)tlv;
			break;
		default:
			MORSE_BEACON_DBG(mors, "    unkown TLV (tag: %u)\n", tag);
			break;
		}
		tlv = (struct morse_cmd_beacon_offload_tlv_hdr *)(((u8 *)tlv) +
			le16_to_cpu(tlv->len) + sizeof(*tlv));
	}

	if (dtim)
		MORSE_BEACON_DBG(mors, "    DTIM count: %u\n", dtim->dtim_cnt);

	if (fc)
		MORSE_BEACON_DBG(mors, "    frame control: 0x%02x%02x\n",
			fc->frame_ctrl[0], fc->frame_ctrl[1]);

	if (change)
		MORSE_BEACON_DBG(mors, "    change sequence: %u\n", change->change_seq);

	if (tx)
		MORSE_BEACON_DBG(mors, "    BW MHz: %u\n", tx->bw_mhz);

	if (cssid)
		MORSE_BEACON_DBG(mors, "    compressed SSID: 0x%02x%02x%02x%02x\n",
			cssid->cssid[0], cssid->cssid[1], cssid->cssid[2], cssid->cssid[3]);

	if (ies)
		MORSE_BEACON_HEXDUMP_DBG("Beacon IEs:", ies->buf, le16_to_cpu(ies->hdr.len));
}

/**
 * morse_beacon_set_compatible_offload_ies - remove incompatible offload beacon ies from an ie mask
 * @mors: Morse stucture
 * @ies_mask: mask of the ies present in the beacon_ies
 *
 * Only allow compatible ies to be included in offloaded beacons. This will parse the ies present
 * in the ies mask and remove incompatible ies.
 */
static void morse_beacon_set_compatible_offload_ies(struct morse *mors,
						    struct dot11ah_ies_mask *ies_mask)
{
	int eid;
	int i;
	bool keep;

	for (eid = 0; eid < DOT11AH_MAX_EID; eid++) {
		if (!ies_mask->ies[eid].ptr)
			continue;
		/* check if ie is in the approved list */
		keep = false;
		for (i = 0; i < BEACON_TEMPLATE_IE_COUNT; i++) {
			if (eid == morse_supported_beacon_offload_ies[i]) {
				keep = true;
				break;
			}
		}

		if (!keep) {
			MORSE_BEACON_WARN(mors, "%s: ie unsupported in offload beacons (eid:%u)\n",
				__func__, eid);
			morse_dot11_clear_eid_from_ies_mask(ies_mask, eid);
		}
	}
}

/**
 * morse_beacon_insert_offload_ies - insert ies from an ie mask into a beacon buffer
 * @beacon_ies: pointer to the beacon ies buffer to insert ies
 * @ies_length: pointer to the beacon ies buffer length, will be updated with the new length
 * @ies_mask: mask of the ies present in the beacon_ies
 *
 * Insert ies present in an IE mask into a buffer and return the size in ies_length
 */
static void morse_beacon_insert_offload_ies(u8 *beacon_ies, int *ies_length,
				      struct dot11ah_ies_mask *ies_mask)
{
	int i;
	int eid;
	u8 *pos = beacon_ies;

	/* insert ies */
	for (i = 0; i < BEACON_TEMPLATE_IE_COUNT; i++) {
		eid = morse_supported_beacon_offload_ies[i];
		pos = morse_dot11_insert_ie(pos, ies_mask->ies[eid].ptr, eid,
						    ies_mask->ies[eid].len);
	}

	*ies_length = pos - beacon_ies;
}

/**
 * morse_beacon_generate_template - generate a beacon template
 * @mors_vif: Morse VIF stucture
 * @bcn_skb: pointer to a pointer to return the beacon template to
 *
 * Generate a beacon template for beacon offloading. If successful bcn_skb will be updated to point
 * to a valid beacon skb, it is the callers responsibility to free this skb.
 */
static int morse_beacon_generate_template(struct morse_vif *mors_vif,  struct sk_buff **bcn_skb)
{
	struct morse *mors;
	struct sk_buff *beacon_template = NULL;
	struct dot11ah_ies_mask *ies_mask = NULL;
	struct ieee80211_mgmt *beacon_mgmt;
	struct ieee80211_vif *vif;
	struct ieee80211_mutable_offsets offs;
	u8 page_slice_no = S1G_TIM_PAGE_SLICE_ENTIRE_PAGE;
	u8 *s1g_beacon_ies;
	int s1g_ies_length;
	int s1g_hdr_length;
	u8 page_index = 0;
	int ret = 0;

	mors = morse_vif_to_morse(mors_vif);
	vif = morse_vif_to_ieee80211_vif(mors_vif);

	if (!morse_mac_is_iface_ap_type(vif))
		return -EINVAL;

	ies_mask = morse_dot11ah_ies_mask_alloc();
	if (!ies_mask)
		return -ENOMEM;

	MORSE_BEACON_INFO(mors, "%s: generating beacon template\n", __func__);
	beacon_template = MORSE_IEEE_BEACON_GET_TEMPLATE(mors, vif, &offs);

	if (!beacon_template) {
		morse_dot11ah_ies_mask_free(ies_mask);
		MORSE_BEACON_ERR_RATELIMITED(mors, "%s: ieee80211_beacon_get_template failed\n",
			__func__);
		return -EAGAIN;
	}

	beacon_mgmt = (struct ieee80211_mgmt *)beacon_template->data;

	s1g_beacon_ies = morse_mac_get_ie_pos(beacon_template, &s1g_ies_length,
					      &s1g_hdr_length, false);

	/* Parse out the original IEs so we can mess with them */
	if (morse_dot11ah_parse_ies(s1g_beacon_ies, s1g_ies_length, ies_mask) < 0) {
		MORSE_BEACON_WARN_RATELIMITED(mors, "%s: failed to parse beacon IEs\n", __func__);
		goto exit;
	}

	morse_beacon_insert_ies(mors_vif, ies_mask, beacon_mgmt->frame_control, false);

	if (ies_mask->ies[WLAN_EID_TIM].ptr)
		morse_dot11ah_insert_s1g_tim(vif, ies_mask, page_slice_no, page_index);

	ret = morse_beacon_convert_ies_and_populate_skb(mors_vif, &beacon_template, ies_mask,
							&s1g_ies_length, false);
	if (ret)
		goto exit;

	*bcn_skb = beacon_template;
exit:
	if (ret) {
		kfree_skb(beacon_template);
		*bcn_skb = NULL;
	}

	morse_dot11ah_ies_mask_free(ies_mask);
	return ret;
}

bool morse_hw_beacon_can_offload(struct morse_vif *mors_vif)
{
	struct morse *mors;
	struct ieee80211_vif *vif;

	if (!mors_vif)
		return false;

	mors = morse_vif_to_morse(mors_vif);
	vif = morse_vif_to_ieee80211_vif(mors_vif);

	if (vif->type != NL80211_IFTYPE_AP)
		return false;

	if (ieee80211_vif_is_mesh(vif))
		return false;

	if (morse_raw_is_enabled(mors_vif))
		return false;

	if (morse_cac_is_enabled(mors_vif))
		return false;

	if (morse_mbssid_ie_enabled(mors))
		return false;

	if (mors_vif->page_slicing_info.enabled)
		return false;

	if (morse_beacon_get_rsn_mode() != RSN_BEACON_DISABLED)
		return false;

	if (morse_beacon_get_short_bcn_dtim_override() > 0)
		return false;

	return true;
}

int morse_hw_beacon_offload_insert_tlvs(struct morse_vif *mors_vif,
	struct morse_cmd_req_beacon_offload **preq, size_t *cmd_size)
{
	struct morse *mors;
	struct sk_buff *beacon_template = NULL;
	struct ieee80211_ext *s1g_beacon;
	struct ieee80211_tim_ie *tim_ie;
	u8 *buf;
	u8 *beacon_ies;
	u8 tx_bw_mhz;
	u32 cssid;
	int size;
	int ies_length;
	int hdr_length;
	struct dot11ah_ies_mask *ies_mask = NULL;
	int ret = 0;

	if (!mors_vif || !mors_vif->custom_configs)
		return -EINVAL;

	mors = morse_vif_to_morse(mors_vif);

	ret = morse_beacon_generate_template(mors_vif, &beacon_template);
	if (!beacon_template || ret) {
		MORSE_BEACON_ERR(mors, "%s: failed to generate the beacon, ret: %d\n",
					     __func__, ret);
		return -EAGAIN;
	}

	beacon_ies = morse_mac_get_ie_pos(beacon_template, &ies_length, &hdr_length, true);
	if (!beacon_ies) {
		kfree_skb(beacon_template);
		return -EAGAIN;
	}

	ies_mask = morse_dot11ah_ies_mask_alloc();
	if (!ies_mask) {
		kfree_skb(beacon_template);
		return -ENOMEM;
	}

	if (morse_dot11ah_parse_ies(beacon_ies, ies_length, ies_mask) > 0) {
		ret = -EAGAIN;
		goto exit;
	}

	/* Clear the TIM in the beacon template even if there is traffic buffered for a STA.
	 * Since something has requested beacon offloading while there is traffic to be sent,
	 * we assume that there is an application level retry mechanism to ensure delivery
	 */
	tim_ie = (struct ieee80211_tim_ie *)ies_mask->ies[WLAN_EID_TIM].ptr;
	if (ies_mask->ies[WLAN_EID_TIM].len > 2) {
		MORSE_BEACON_INFO(mors,
			"%s: TIM indicates buffered traffic, clearing in template\n",
			__func__);
		tim_ie->bitmap_ctrl = 0;
		ies_length -= ies_mask->ies[WLAN_EID_TIM].len - 2;
		ies_mask->ies[WLAN_EID_TIM].len = 2;
	}

	/* remove any ies from the beacon template that are not supported by offload beacons */
	morse_beacon_set_compatible_offload_ies(mors, ies_mask);
	morse_beacon_insert_offload_ies(beacon_ies, &ies_length, ies_mask);

	/* update the caller's req buffer to make room for the TLV data */
	size = beacon_offload_get_cmd_size(ies_length, ies_mask);
	*preq = krealloc(*preq, size, GFP_KERNEL | __GFP_ZERO);
	if (!*preq) {
		ret = -ENOMEM;
		goto exit;
	}

	buf = (*preq)->variable;
	*cmd_size = size;
	s1g_beacon = (struct ieee80211_ext *)beacon_template->data;

	tx_bw_mhz = (mors->enable_subbands == SUBBANDS_MODE_DISABLED) ?
	    mors->custom_configs.channel_info.op_bw_mhz :
	    mors->custom_configs.channel_info.pri_bw_mhz;

	MORSE_BEACON_DBG(mors, "%s: Packing beacon offload TLVs (len: %d)\n",
				__func__, (int)(size - sizeof((*preq)->hdr)));
	buf = beacon_offload_add_frame_ctrl_tlv(mors, buf, s1g_beacon->frame_control);
	buf = beacon_offload_add_change_seq_tlv(mors, buf,
						s1g_beacon->u.s1g_beacon.change_seq);
	buf = beacon_offload_add_tx_info_tlv(mors, buf, tx_bw_mhz);
	buf = beacon_offload_add_ies_tlv(mors, buf, beacon_ies, ies_length);

	/* DTIM count handled by chip during beacon offload */
	buf = beacon_offload_add_dtim_count_tlv(mors, buf, 0);

	if (ies_mask->ies[WLAN_EID_SSID].len != 0) {
		cssid = morse_generate_cssid(ies_mask->ies[WLAN_EID_SSID].ptr,
					     ies_mask->ies[WLAN_EID_SSID].len);
		buf = beacon_offload_add_cssid_tlv(mors, buf, cssid);
	}

exit:
	kfree(ies_mask);
	kfree_skb(beacon_template);
	return ret;
}
