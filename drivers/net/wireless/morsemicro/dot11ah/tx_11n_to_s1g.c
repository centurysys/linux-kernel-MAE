/*
 * Copyright 2020 Morse Micro
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/crc32.h>
#include <linux/ieee80211.h>
#include <linux/bitfield.h>

#include "dot11ah.h"
#include "tim.h"
#include "debug.h"
#include "../morse.h"
#include "../cac.h"


#define HZ_TO_KHZ(x) ((x) / 1000)
#define LOWER_32_BITS(x) ((x) & UINT_MAX)
#define UPPER_32_BITS(x) (((x) >> 32) & UINT_MAX)

static int morse_dot11ah_set_s1g_capab(struct ieee80211_vif *vif);

/* API's used to insert various S1G information elements (used only in this file) */

static int morse_dot11ah_insert_s1g_aid_request(u8 *pkt)
{
	/* For now we won't request anything */
	u8 s1g_aid_request[] = {
		0x00
	};

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&s1g_aid_request,
			WLAN_EID_AID_REQUEST,
			sizeof(s1g_aid_request));

	return sizeof(s1g_aid_request) + 2;
}

static int morse_dot11ah_insert_s1g_aid_response(u8 *pkt, u16 aid)
{
	u8 s1g_aid_response[] = {
		0x00, 0x00, 0x00, 0x00, 0x00
	};

	/* The 1st and 2nd octet are the AID */
	*((u16 *)&s1g_aid_response[0]) = cpu_to_le16(aid);

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&s1g_aid_response,
			WLAN_EID_AID_RESPONSE,
			sizeof(s1g_aid_response));

	return sizeof(s1g_aid_response) + 2;
}

static int morse_dot11ah_insert_s1g_compatibility(
	u8 *pkt, u16 beacon_int, u16 capab_info, u32 tsf_completion)
{
	const struct dot11ah_s1g_beacon_compatibility_ie s1g_compatibility = {
		.beacon_interval = cpu_to_le16(beacon_int),
		.information = cpu_to_le16(capab_info),
		.tsf_completion = cpu_to_le32(tsf_completion),
	};

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&s1g_compatibility,
			WLAN_EID_S1G_BCN_COMPAT,
			sizeof(s1g_compatibility));

	return sizeof(s1g_compatibility) + 2;
}

/** Inserts the S1G capability element */
static int morse_dot11ah_insert_s1g_capability(
	struct ieee80211_vif *vif, const struct ieee80211_ht_cap *ht_cap, u8 *pkt, u8 type,	bool enable_ampdu)
{
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;

	morse_dot11ah_set_s1g_capab(vif);
	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&mors_vif->s1g_cap_ie,
			WLAN_EID_S1G_CAPABILITIES,
			sizeof(mors_vif->s1g_cap_ie));

	return sizeof(mors_vif->s1g_cap_ie) + 2;
}

/* Insert S1G TIM */
static int morse_dot11ah_insert_s1g_tim(
	struct ieee80211_vif *vif, u8 *pkt, const struct ieee80211_tim_ie *tim, u8 virtual_map_len)
{
	int length;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct dot11ah_s1g_tim_ie s1g_tim_ie;

	/* enc_mode here is 3 bits, carrying both encoding mode and inverse bitmap fields
	 * TODO: add the inverse_bitmap field separate in morsectrl instead of muxing it with enc_mode
	 */
	enum dot11ah_tim_encoding_mode enc_mode =
		mors_if ? (mors_if->custom_configs->enc_mode & 0x03) : 0;
	bool inverse_bitmap =
		mors_if ? ((mors_if->custom_configs->enc_mode & 0x04) >> 2) : 0;

	if (tim)
		length = morse_dot11_tim_to_s1g(&s1g_tim_ie, tim, virtual_map_len, enc_mode, inverse_bitmap,
			mors_if->ap->largest_aid);
	else
		length = sizeof(s1g_tim_ie);

	if (pkt)
		morse_dot11_insert_ie(pkt,
			      (const u8 *)&s1g_tim_ie,
			      WLAN_EID_TIM,
			      length);

	return length + 2;
}

static int morse_dot11ah_insert_s1g_short_beacon_interval(u8 *pkt, u16 beacon_int)
{
	const struct dot11ah_short_beacon_ie short_beacon_int = {
		.short_beacon_int = cpu_to_le16(beacon_int)
	};

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&short_beacon_int,
			WLAN_EID_S1G_SHORT_BCN_INTERVAL,
			sizeof(short_beacon_int));

	return sizeof(short_beacon_int) + 2;
}

static int morse_dot11ah_insert_s1g_cac(u8 *pkt, u8 index)
{
	struct dot11ah_s1g_auth_control_ie cac_ie = { 0 };

	if (pkt) {
		u16 threshold = index * CAC_THRESHOLD_STEP;

		/* Max index converts to (threshold max + 1), so adjust */
		if (threshold > CAC_THRESHOLD_MAX)
			threshold = CAC_THRESHOLD_MAX;
		cac_ie.parameters = FIELD_PREP(DOT11AH_S1G_CAC_THRESHOLD, threshold);
		morse_dot11_insert_ie(pkt,
			(const u8 *)&cac_ie,
			WLAN_EID_S1G_CAC,
			sizeof(cac_ie));
	}

	return sizeof(cac_ie) + 2;
}

static int morse_dot11ah_insert_s1g_operation(u8 *pkt, struct s1g_operation_parameters *params)
{
	u8 op_bw_mhz = 2;
	u8 pri_bw_mhz = 2;
	u8 chan_centre_freq_num = 38;
	u8 pri_1mhz_chan_idx = 0;
	u8 pri_1mhz_chan_location = 0;
	u8 s1g_operating_class = 0;
	/** Basic S1G-MCS and NSS Set */
	u8 s1g_mcs_and_nss_set[] = {0xCC, 0xC4};

	u8 s1g_operation[] = {
		0x00,
		0x00,
		0x00,
		0x00,
		s1g_mcs_and_nss_set[1],
		s1g_mcs_and_nss_set[0]
	};

	if (params) {
		op_bw_mhz = params->op_bw_mhz;

		pri_bw_mhz = params->pri_bw_mhz;

		pri_1mhz_chan_idx = params->pri_1mhz_chan_idx;

		pri_1mhz_chan_location =
			(pri_1mhz_chan_idx % 2);

		chan_centre_freq_num = params->chan_centre_freq_num;

		s1g_operating_class = params->s1g_operating_class;
	}

	s1g_operation[0] =
		IEEE80211AH_S1G_OPERATION_SET_PRIM_CHAN_BW(pri_bw_mhz) |
		IEEE80211AH_S1G_OPERATION_SET_OP_CHAN_BW(op_bw_mhz) |
		IEEE80211AH_S1G_OPERATION_SET_PRIM_CHAN_LOC(pri_1mhz_chan_location);

	/* TODO: Set this to the actual operating class E.g. 71 for AU 8MHz Channel*/
	/* Operating Class subfield */
	s1g_operation[1] = s1g_operating_class;

	/* Primary Channel Number subfield */
	s1g_operation[2] = morse_dot11ah_calculate_primary_s1g_channel(
		op_bw_mhz, pri_bw_mhz,
		chan_centre_freq_num, pri_1mhz_chan_idx);

	/* Channel Centre Frequency subfield */
	s1g_operation[3] = chan_centre_freq_num;

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&s1g_operation,
			WLAN_EID_S1G_OPERATION,
			sizeof(s1g_operation));

	return sizeof(s1g_operation) + 2;
}

static int morse_dot11ah_insert_country_ie(u8 *pkt, struct s1g_operation_parameters *params)
{
	struct dot11ah_country_ie country_ie;
	const struct morse_regdomain *regdom;

	const char *region = morse_dot11ah_get_region_str();

	memset(&country_ie, 0, sizeof(country_ie));

	regdom = morse_reg_alpha_lookup(region);
	if (!regdom)
		return 0;

	if (params)
		morse_mac_set_country_info_from_regdom(regdom, params, &country_ie);

	if (pkt)
		morse_dot11_insert_ie(pkt,
			(const u8 *)&country_ie,
			WLAN_EID_COUNTRY,
			sizeof(country_ie));

	return sizeof(country_ie) + 2;
}
/* API's to convert the 11n frames coming from Linux to S1G ready to transmit */

static uint16_t morse_dot11ah_listen_interval_to_s1g(uint16_t li)
{
	uint16_t s1g_li;

	/* if mulitple of 10, directly use 10 scale */
	if ((li > 0x3FFF) || (li % 10 == 0)) {
		uint16_t usf =
			IEEE80211_LI_USF_10 << IEEE80211_S1G_LI_USF_SHIFT;

		s1g_li = li / 10;
		s1g_li |= usf;
	} else {
		s1g_li = li;
	}

	return s1g_li;
}

static int morse_dot11_required_tx_ies_size(struct dot11ah_ies_mask *ies_mask)
{
	int s1g_len = 0;
	int eid = 0;
	struct ie_element *elem;

	for (eid = 0; eid < DOT11AH_MAX_EID; eid++) {
		if (ies_mask->ies[eid].ptr != NULL)
			s1g_len += ies_mask->ies[eid].len + 2;
		else if (eid == WLAN_EID_SSID && ies_mask->ies[eid].ptr == NULL)
			s1g_len += 2;

		/* check for any extra elements with the same ID */
		for (elem = ies_mask->ies[eid].next; elem != NULL; elem = elem->next)
			s1g_len += (elem->len + 2);
	}

	return s1g_len;
}

static u8 *morse_dot11ah_insert_required_tx_ie(struct dot11ah_ies_mask *ies_mask, u8 *pos)
{
	int eid = 0;

	for (eid = 0; eid < DOT11AH_MAX_EID; eid++)
		pos = morse_dot11_insert_ie_from_ies_mask(pos, ies_mask, eid);

	return pos;
}

static int morse_dot11ah_assoc_req_to_s1g_size(struct ieee80211_vif *vif, struct sk_buff *skb, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *assoc_req = (struct ieee80211_mgmt *) skb->data;
	u8 *assoc_req_ies = ieee80211_is_assoc_req(assoc_req->frame_control) ?
		assoc_req->u.assoc_req.variable :
		assoc_req->u.reassoc_req.variable;
	int header_length = assoc_req_ies - skb->data;
	int assoc_req_ies_len = skb->len - header_length;
	/* Initially, the size equals to the incoming header length */
	int s1g_length = header_length;

	if (morse_dot11ah_parse_ies(assoc_req_ies, assoc_req_ies_len, ies_mask) < 0) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", assoc_req_ies, assoc_req_ies_len);
		return -EINVAL;
	}

	s1g_length += morse_dot11ah_insert_s1g_aid_request(NULL);
	s1g_length += morse_dot11ah_insert_s1g_capability(vif, NULL, NULL, 0, false);
	s1g_length += morse_dot11ah_insert_s1g_operation(NULL, NULL);
	/* Mask all the elements that are not requiered */
	morse_dot11ah_mask_ies(ies_mask, false, false);
	s1g_length += morse_dot11_required_tx_ies_size(ies_mask);
	s1g_length += ies_mask->fils_data_len;

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	return s1g_length;
}

static void morse_dot11ah_assoc_req_to_s1g(
	struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *assoc_req = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_mgmt *s1g_assoc_req;
	const struct ieee80211_ht_cap *ht_cap;
	u8 *s1g_ies = NULL;
	u8 *assoc_req_ies = ieee80211_is_assoc_req(assoc_req->frame_control) ?
		assoc_req->u.assoc_req.variable :
		assoc_req->u.reassoc_req.variable;
	int header_length = assoc_req_ies - skb->data;
	int assoc_req_ies_len = skb->len - header_length;
	u16 li = ieee80211_is_assoc_req(assoc_req->frame_control) ?
		le16_to_cpu(assoc_req->u.assoc_req.listen_interval) :
		le16_to_cpu(assoc_req->u.reassoc_req.listen_interval);
	u16 s1g_li;
	struct s1g_operation_parameters s1g_oper_params = {
		.chan_centre_freq_num =
			morse_dot11ah_freq_khz_bw_mhz_to_chan(
				HZ_TO_KHZ(mors_if->custom_configs->channel_info.op_chan_freq_hz),
				mors_if->custom_configs->channel_info.op_bw_mhz),
		.op_bw_mhz = mors_if->custom_configs->channel_info.op_bw_mhz,
		.pri_bw_mhz = mors_if->custom_configs->channel_info.pri_bw_mhz,
		.pri_1mhz_chan_idx = mors_if->custom_configs->channel_info.pri_1mhz_chan_idx,
		.s1g_operating_class = mors_if->custom_configs->channel_info.s1g_operating_class
	};

	if (morse_dot11ah_parse_ies(assoc_req_ies, assoc_req_ies_len, ies_mask)) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", assoc_req_ies, assoc_req_ies_len);
	}
	/* An atomic allocation is required as this function can be called from
	 * the beacon tasklet.
	 */
	s1g_assoc_req = kmalloc(s1g_length, GFP_ATOMIC);
	BUG_ON(!s1g_assoc_req);

	/* Fill in the new assoc request header, copied from incoming frame */
	memcpy(s1g_assoc_req, assoc_req, header_length);

	/* Overwrite listen interval if set by morsectrl
	 * Convert to S1G (USF/UI) format if its from wpa_supplicant,
	 * morsectrl is already in correct format
	 */
	s1g_li = mors_if->custom_configs->listen_interval_ovr ?
		mors_if->custom_configs->listen_interval :
		morse_dot11ah_listen_interval_to_s1g(li);

	if (ieee80211_is_assoc_req(s1g_assoc_req->frame_control))
		s1g_assoc_req->u.assoc_req.listen_interval = cpu_to_le16(s1g_li);
	else
		s1g_assoc_req->u.reassoc_req.listen_interval  = cpu_to_le16(s1g_li);

	s1g_ies = ieee80211_is_assoc_req(s1g_assoc_req->frame_control) ?
		s1g_assoc_req->u.assoc_req.variable :
		s1g_assoc_req->u.reassoc_req.variable;

	ht_cap = (const struct ieee80211_ht_cap *) ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr;
	morse_dot11ah_mask_ies(ies_mask, false, false);

	/* Enable ECSA */
	if (ies_mask->ies[WLAN_EID_EXT_CAPABILITY].ptr) {
		u8 *ext_capa1 = (u8 *)ies_mask->ies[WLAN_EID_EXT_CAPABILITY].ptr;

		ext_capa1[0] |= WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
	}

	s1g_ies = morse_dot11ah_insert_required_tx_ie(ies_mask, s1g_ies);

	s1g_ies += morse_dot11ah_insert_s1g_aid_request(s1g_ies);

	s1g_ies += morse_dot11ah_insert_s1g_capability(vif, ht_cap,
		s1g_ies, mors_if->custom_configs->sta_type,
		mors_if->custom_configs->enable_ampdu);

	if (ies_mask->ies[WLAN_EID_SSID].ptr != NULL && (ies_mask->ies[WLAN_EID_SSID].len > 0)) {
		morse_dot11ah_find_s1g_operation_for_ssid(
			ies_mask->ies[WLAN_EID_SSID].ptr, ies_mask->ies[WLAN_EID_SSID].len, &s1g_oper_params);
	}

	s1g_ies += morse_dot11ah_insert_s1g_operation(s1g_ies, &s1g_oper_params);

	/* This must be last */
	if (ies_mask->fils_data != NULL)
		s1g_ies = morse_dot11_insert_ie_no_header(s1g_ies, ies_mask->fils_data,
								ies_mask->fils_data_len);

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	s1g_length = s1g_ies - (u8 *)s1g_assoc_req;
	if (skb->len < s1g_length)
		skb_put(skb, s1g_length - skb->len);

	memcpy(skb->data, s1g_assoc_req, s1g_length);
	kfree(s1g_assoc_req);

	skb_trim(skb, s1g_length);
}

static int morse_dot11ah_assoc_resp_to_s1g_size(struct ieee80211_vif *vif, struct sk_buff *skb, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *assoc_resp = (struct ieee80211_mgmt *) skb->data;
	u8 *assoc_resp_ies = assoc_resp->u.assoc_resp.variable;
	int header_length = assoc_resp_ies - skb->data;
	int assoc_resp_ies_len = skb->len - header_length;
	/* Initially, the size equals to the incoming header length */
	int s1g_length = header_length;

	/* AID is present in the HT header_length calculated above, but not in S1G header */
	s1g_length -= sizeof(assoc_resp->u.assoc_resp.aid);
	s1g_length += morse_dot11ah_insert_s1g_aid_response(NULL, 0);
	s1g_length += morse_dot11ah_insert_s1g_operation(NULL, NULL);
	s1g_length += morse_dot11ah_insert_s1g_capability(vif, NULL, NULL, 0, false);

	if (morse_dot11ah_parse_ies(assoc_resp_ies, assoc_resp_ies_len, ies_mask) < 0) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", assoc_resp_ies, assoc_resp_ies_len);
		return -EINVAL;
	}

	// Mask all eid's that are no needed
	morse_dot11ah_mask_ies(ies_mask, true, false);
	// Let's get the length
	s1g_length += morse_dot11_required_tx_ies_size(ies_mask);
	s1g_length += ies_mask->fils_data_len;

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_EXT_CAPABILITY
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	return s1g_length;
}

static void morse_dot11ah_assoc_resp_to_s1g(
	struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *assoc_resp = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct morse_dot11ah_s1g_assoc_resp *s1g_assoc_resp;
	const struct ieee80211_ht_cap *ht_cap;
	u8 *s1g_ies = NULL;
	u8 *assoc_resp_ies = assoc_resp->u.assoc_resp.variable;
	int header_length = assoc_resp_ies - skb->data;
	int assoc_resp_ies_len = skb->len - header_length;
	__le16 aid = assoc_resp->u.assoc_resp.aid & 0x3FFF;
	struct s1g_operation_parameters s1g_oper_params = {
		.chan_centre_freq_num =
			morse_dot11ah_freq_khz_bw_mhz_to_chan(
				HZ_TO_KHZ(mors_if->custom_configs->channel_info.op_chan_freq_hz),
				mors_if->custom_configs->channel_info.op_bw_mhz),
		.op_bw_mhz = mors_if->custom_configs->channel_info.op_bw_mhz,
		.pri_bw_mhz = mors_if->custom_configs->channel_info.pri_bw_mhz,
		.pri_1mhz_chan_idx = mors_if->custom_configs->channel_info.pri_1mhz_chan_idx,
		.s1g_operating_class = mors_if->custom_configs->channel_info.s1g_operating_class
	};

	/* An atomic allocation is required as this function can be called from
	 * the beacon tasklet.
	 */
	s1g_assoc_resp = kmalloc(s1g_length, GFP_ATOMIC);
	BUG_ON(!s1g_assoc_resp);

	/* Fill in the new assoc response header, copied from incoming frame
	 * AID is present in the HT header_length calculated above, but not in S1G header
	 */
	memcpy(s1g_assoc_resp, assoc_resp, header_length - sizeof(assoc_resp->u.assoc_resp.aid));

	s1g_ies = s1g_assoc_resp->variable;

	if (morse_dot11ah_parse_ies(assoc_resp_ies, assoc_resp_ies_len, ies_mask)) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", assoc_resp_ies, assoc_resp_ies_len);
	}

	if (ies_mask->ies[WLAN_EID_BSS_MAX_IDLE_PERIOD].ptr != NULL) {
		/* Update to S1G format */
		struct ieee80211_bss_max_idle_period_ie *bss_max_idle_period = (struct ieee80211_bss_max_idle_period_ie *) ies_mask->ies[WLAN_EID_BSS_MAX_IDLE_PERIOD].ptr;
		u16 idle_period = le16_to_cpu(bss_max_idle_period->max_idle_period);

		/* Overwrite max_idle_period if set by morsectrl
		 * Convert to S1G (USF/UI) format if its from hostapd,
		 * morsectrl is already in correct format
		 */
		u16 s1g_period = mors_if->custom_configs->listen_interval ?
			mors_if->custom_configs->listen_interval :
			morse_dot11ah_listen_interval_to_s1g(idle_period);

		/* Convert to S1G (USF/UI) format */
		bss_max_idle_period->max_idle_period =
			cpu_to_le16(s1g_period);

		ies_mask->ies[WLAN_EID_BSS_MAX_IDLE_PERIOD].ptr = (u8 *) bss_max_idle_period;
		ies_mask->ies[WLAN_EID_BSS_MAX_IDLE_PERIOD].len = sizeof(*bss_max_idle_period);
	}

	ht_cap = (const struct ieee80211_ht_cap *) ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr;
	morse_dot11ah_mask_ies(ies_mask, true, false);
	s1g_ies += morse_dot11ah_insert_s1g_aid_response(s1g_ies, aid);

	s1g_ies += morse_dot11ah_insert_s1g_capability(vif,
		ht_cap,
		s1g_ies,
		mors_if->custom_configs->sta_type,
		mors_if->custom_configs->enable_ampdu);

	s1g_ies += morse_dot11ah_insert_s1g_operation(s1g_ies, &s1g_oper_params);

	s1g_ies = morse_dot11ah_insert_required_tx_ie(ies_mask, s1g_ies);

	/* This must be last */
	if (ies_mask->fils_data != NULL)
		s1g_ies = morse_dot11_insert_ie_no_header(s1g_ies, ies_mask->fils_data,
								ies_mask->fils_data_len);

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_EXT_CAPABILITY
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	s1g_length = s1g_ies - (u8 *)s1g_assoc_resp;
	if (skb->len < s1g_length)
		skb_put(skb, s1g_length - skb->len);

	memcpy(skb->data, s1g_assoc_resp, s1g_length);
	kfree(s1g_assoc_resp);

	skb_trim(skb, s1g_length);
}

static int morse_dot11ah_probe_resp_to_s1g_size(struct ieee80211_vif *vif, struct sk_buff *skb, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *probe_resp = (struct ieee80211_mgmt *) skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	u8 *probe_resp_ies = probe_resp->u.probe_resp.variable;
	int header_length = probe_resp_ies - skb->data;
	int probe_resp_ies_len = skb->len - header_length;
	int s1g_length = header_length;

	/* Initially, the size equals to the incoming header length */

	if (morse_dot11ah_parse_ies(probe_resp_ies, probe_resp_ies_len, ies_mask) < 0)
		return -EINVAL;

	if (vif->bss_conf.dtim_period > 0)
		s1g_length += morse_dot11ah_insert_s1g_short_beacon_interval(NULL, 0);

	if (mors_if->cac.enabled)
		s1g_length += morse_dot11ah_insert_s1g_cac(NULL, 0);

	s1g_length += morse_dot11ah_insert_s1g_capability(vif, NULL, NULL, 0, false);
	s1g_length += morse_dot11ah_insert_s1g_operation(NULL, NULL);

	morse_dot11ah_mask_ies(ies_mask, false, false);
	s1g_length += morse_dot11_required_tx_ies_size(ies_mask);
	s1g_length += morse_dot11ah_insert_country_ie(NULL, NULL);

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_EXT_CAPABILITY
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */
	return s1g_length;
}

/* Check for ECSA IE in beacon/probe resp right after switching to new channel */
static void morse_dot11ah_check_for_ecsa_in_new_channel(struct ieee80211_vif *vif,
								struct dot11ah_ies_mask *ies_mask)
{
	struct morse_vif *mors_if;
	const u8 *ie;
	struct ieee80211_ext_chansw_ie *ecsa_ie_info =
			(struct ieee80211_ext_chansw_ie *)ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr;
	struct ieee80211_wide_bw_chansw_ie *wbcsie;
	u8 pri_bw_mhz, pri_1mhz_chan_idx, op_chan_bw;
	u32 op_chan_freq_hz;

	mors_if = (struct morse_vif *)vif->drv_priv;

	ecsa_ie_info =
		(struct ieee80211_ext_chansw_ie *)ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr;
	if (ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].ptr) {
		ie = cfg80211_find_ie(WLAN_EID_WIDE_BW_CHANNEL_SWITCH,
					ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].ptr,
					ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].len);
	} else
		ie = NULL;

	if (ie) {
		wbcsie = (struct ieee80211_wide_bw_chansw_ie *)(ie+2);
		op_chan_freq_hz = morse_dot11ah_s1g_chan_to_s1g_freq(wbcsie->new_center_freq_seg0);
		op_chan_bw = wbcsie->new_channel_width + 1;
	} else {
		op_chan_freq_hz =
			morse_dot11ah_s1g_chan_to_s1g_freq(ecsa_ie_info->new_ch_num);
		op_chan_bw = S1G_CHAN_1MHZ + 1;
	}

	pri_bw_mhz = (((morse_dot11ah_channel_get_flags(ecsa_ie_info->new_ch_num) > IEEE80211_CHAN_1MHZ) ?
										S1G_CHAN_2MHZ : S1G_CHAN_1MHZ) + 1);
	pri_1mhz_chan_idx = morse_dot11ah_calculate_primary_s1g_channel_loc(
				HZ_TO_KHZ(morse_dot11ah_s1g_chan_to_s1g_freq(ecsa_ie_info->new_ch_num)),
				HZ_TO_KHZ(op_chan_freq_hz),
				op_chan_bw);
	/*
	 * There is a rare case where mac80211 is taking time to update the beacon content while
	 * reserving & configuring hw for new channel announced in ECSA. This is resulting in old
	 * beacon content(ECSA IE) in new channel (only in 1st beacon and/or probe response).
	 */
	if ((pri_1mhz_chan_idx == mors_if->custom_configs->default_bw_info.pri_1mhz_chan_idx) &&
		(pri_bw_mhz == mors_if->custom_configs->default_bw_info.pri_bw_mhz)) {
		if ((op_chan_freq_hz == mors_if->custom_configs->channel_info.op_chan_freq_hz) &&
				(op_chan_bw == mors_if->custom_configs->channel_info.op_bw_mhz)) {
			/* mask the ECSA IEs */
			ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr = NULL;
			ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].len = false;
			ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].ptr = NULL;
			ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].len = false;
			dot11ah_debug("Mask ECSA And Channel Switch Wrapper IEs. op_chan=%d, [%d-%d-%d]\n",
									op_chan_freq_hz,
									op_chan_bw,
									pri_bw_mhz,
									pri_1mhz_chan_idx);
		}
	}
}

static void morse_dot11ah_convert_ecsa_info_to_s1g(struct morse_vif *mors_if,
					struct dot11ah_ies_mask *ies_mask)
{
	/* Update 5G Channels Info in ECSA IE and Wide Bandwidth channel switch IE to S1G */
	if (ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr) {
		struct ieee80211_ext_chansw_ie *pecsa =
			(struct ieee80211_ext_chansw_ie *)ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr;

		/* Disable legacy channel switch IE */
		ies_mask->ies[WLAN_EID_CHANNEL_SWITCH].ptr = NULL;

		pecsa->new_ch_num = morse_dot11ah_5g_chan_to_s1g_ch(pecsa->new_ch_num,
													 pecsa->new_operating_class);

		if (mors_if->ecsa_channel_info.pri_bw_mhz == (S1G_CHAN_2MHZ+1)) {
			if (mors_if->ecsa_channel_info.pri_1mhz_chan_idx%2)
				pecsa->new_ch_num -= 1;
			else
				pecsa->new_ch_num += 1;
		}

		pecsa->new_operating_class = mors_if->ecsa_channel_info.s1g_operating_class;

		if (ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].ptr) {
			const u8 *ie = cfg80211_find_ie(WLAN_EID_WIDE_BW_CHANNEL_SWITCH,
							ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].ptr,
							ies_mask->ies[WLAN_EID_CHANNEL_SWITCH_WRAPPER].len);

			if (ie) {
				struct ieee80211_wide_bw_chansw_ie *wbcsie =
								(struct ieee80211_wide_bw_chansw_ie *)(ie+2);

				wbcsie->new_center_freq_seg0 = morse_dot11ah_5g_chan_to_s1g_ch(
						wbcsie->new_center_freq_seg0, pecsa->new_operating_class);

				switch (wbcsie->new_channel_width) {
				case IEEE80211_VHT_CHANWIDTH_USE_HT:
					wbcsie->new_channel_width = S1G_CHAN_2MHZ;
					break;
				case IEEE80211_VHT_CHANWIDTH_80MHZ:
					wbcsie->new_channel_width = S1G_CHAN_4MHZ;
					break;
				case IEEE80211_VHT_CHANWIDTH_160MHZ:
					wbcsie->new_channel_width = S1G_CHAN_8MHZ;
					break;
				default:
					dot11ah_err("ECSA: Invalid Bandwidth in Wide Bandwidth Channel Switch IE\n");
					break;
				}
			}
		}
	}
}

static void morse_dot11ah_probe_resp_to_s1g(struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *probe_resp = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_mgmt *s1g_probe_resp;
	const struct ieee80211_ht_cap *ht_cap;
	u8 *s1g_ies = NULL;
	u8 *probe_resp_ies = probe_resp->u.probe_resp.variable;
	int header_length = probe_resp_ies - skb->data;
	int probe_resp_ies_len = skb->len - header_length;
	u64 now_usecs;

	struct s1g_operation_parameters s1g_oper_params = {
		.chan_centre_freq_num =
			morse_dot11ah_freq_khz_bw_mhz_to_chan(
				HZ_TO_KHZ(mors_if->custom_configs->channel_info.op_chan_freq_hz),
				mors_if->custom_configs->channel_info.op_bw_mhz),
		.op_bw_mhz = mors_if->custom_configs->channel_info.op_bw_mhz,
		.pri_bw_mhz = mors_if->custom_configs->channel_info.pri_bw_mhz,
		.pri_1mhz_chan_idx = mors_if->custom_configs->channel_info.pri_1mhz_chan_idx,
		.s1g_operating_class = mors_if->custom_configs->channel_info.s1g_operating_class,
		.prim_global_op_class = mors_if->custom_configs->channel_info.pri_global_operating_class
	};

	if (morse_dot11ah_parse_ies(probe_resp_ies, probe_resp_ies_len, ies_mask)) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", probe_resp_ies, probe_resp_ies_len);
	}
	/* An atomic allocation is required as this function can be called from
	 * the beacon tasklet.
	 */
	s1g_probe_resp = kmalloc(s1g_length, GFP_ATOMIC);
	BUG_ON(!s1g_probe_resp);

	/* Fill in the new probe response header, copied from incoming frame */
	memcpy(s1g_probe_resp, probe_resp, header_length);

	s1g_ies = s1g_probe_resp->u.probe_resp.variable;

	/* SW-2241: The capabilities field is advertising short slot time.
	 * Short slot time is relevant to 80211g (2.4GHz). We should set that to 0
	 * so that in future we can repurpose the bit for some other
	 * 802.11ah use.
	 */
	s1g_probe_resp->u.probe_resp.capab_info &= ~(WLAN_CAPABILITY_SHORT_SLOT_TIME);
	now_usecs = jiffies_to_usecs((get_jiffies_64() - mors_if->epoch));
	s1g_probe_resp->u.probe_resp.timestamp = cpu_to_le64(now_usecs);

	ht_cap = (const struct ieee80211_ht_cap *) ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr;

	morse_dot11ah_mask_ies(ies_mask, false, false);

	/* Enable ECSA */
	if (ies_mask->ies[WLAN_EID_EXT_CAPABILITY].ptr) {
		u8 *ext_capa1 = (u8 *)ies_mask->ies[WLAN_EID_EXT_CAPABILITY].ptr;

		ext_capa1[0] |= WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
	}

	if (ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr) {
		struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;

		morse_dot11ah_convert_ecsa_info_to_s1g(mors_if, ies_mask);
		if (mors_if->mask_ecsa_info_in_beacon)
			morse_dot11ah_check_for_ecsa_in_new_channel(vif, ies_mask);
	}

	s1g_ies = morse_dot11ah_insert_required_tx_ie(ies_mask, s1g_ies);

	s1g_ies += morse_dot11ah_insert_country_ie(s1g_ies, &s1g_oper_params);

	if (mors_if->cac.enabled)
		s1g_ies += morse_dot11ah_insert_s1g_cac(s1g_ies, mors_if->cac.threshold_index);

	s1g_ies += morse_dot11ah_insert_s1g_capability(vif,
		ht_cap,
		s1g_ies,
		mors_if->custom_configs->sta_type,
		mors_if->custom_configs->enable_ampdu);

	s1g_ies += morse_dot11ah_insert_s1g_operation(s1g_ies, &s1g_oper_params);

	if (vif->bss_conf.dtim_period > 0)
		s1g_ies += morse_dot11ah_insert_s1g_short_beacon_interval(
			s1g_ies, vif->bss_conf.beacon_int);


	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_EXT_CAPABILITY
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	s1g_length = s1g_ies - (u8 *)s1g_probe_resp;
	if (skb->len < s1g_length)
		skb_put(skb, s1g_length - skb->len);

	memcpy(skb->data, s1g_probe_resp, s1g_length);
	kfree(s1g_probe_resp);

	skb_trim(skb, s1g_length);
}

static int morse_dot11ah_probe_req_to_s1g_size(struct ieee80211_vif *vif, struct sk_buff *skb, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *probe_req = (struct ieee80211_mgmt *) skb->data;
	u8 *probe_req_ies = probe_req->u.probe_req.variable;
	int header_length = probe_req_ies - skb->data;
	int probe_req_ies_len = skb->len - header_length;
	/* Initially, the size equals to the incoming header length */
	int s1g_length = header_length;

	if (morse_dot11ah_parse_ies(probe_req_ies, probe_req_ies_len, ies_mask) < 0)
		return -EINVAL;

	morse_dot11ah_mask_ies(ies_mask, false, false);
	s1g_length += morse_dot11_required_tx_ies_size(ies_mask);
	s1g_length += morse_dot11ah_insert_s1g_capability(vif, NULL, NULL, 0, false);

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */
	return s1g_length;
}

static void morse_dot11ah_probe_req_to_s1g(
	struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *probe_req = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_mgmt *s1g_probe_req;
	const struct ieee80211_ht_cap *ht_cap;
	u8 *s1g_ies = NULL;
	u8 *probe_req_ies = probe_req->u.probe_req.variable;
	int header_length = probe_req_ies - skb->data;
	int probe_req_ies_len = skb->len - header_length;
	u8 zero_mac[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	/* An atomic allocation is required as this function can be called from
	 * the beacon tasklet.
	 */
	s1g_probe_req = kmalloc(s1g_length, GFP_ATOMIC);
	BUG_ON(!s1g_probe_req);

	/* Fill in the new probe request header, copied from incoming frame */
	memcpy(s1g_probe_req, probe_req, header_length);

	/* In IBSS mode, scan is triggered from mac80211 and does not set
	 * broadcast bssid to the probe request which resulted in no probe
	 * response from the nodes. Fill probe request with broadcast mac here
	 */
	if (ether_addr_equal_unaligned(s1g_probe_req->da, zero_mac))
		eth_broadcast_addr(s1g_probe_req->da);

	if (ether_addr_equal_unaligned(s1g_probe_req->bssid, zero_mac))
		eth_broadcast_addr(s1g_probe_req->bssid);

	s1g_ies = s1g_probe_req->u.probe_req.variable;

	if (morse_dot11ah_parse_ies(probe_req_ies, probe_req_ies_len, ies_mask)) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", probe_req_ies, probe_req_ies_len);
	}

	if (ies_mask->ies[WLAN_EID_SSID].ptr != NULL && (ies_mask->ies[WLAN_EID_SSID].len > 0)) {
		u32 cssid = ~crc32(~0, ies_mask->ies[WLAN_EID_SSID].ptr, ies_mask->ies[WLAN_EID_SSID].len);

		morse_dot11ah_store_cssid(cssid, ies_mask->ies[WLAN_EID_SSID].len, ies_mask->ies[WLAN_EID_SSID].ptr, 0, NULL, 0, NULL);

		s1g_ies = morse_dot11_insert_ie_from_ies_mask(s1g_ies, ies_mask, WLAN_EID_SSID);
	} else {
		/* Insert wild-card SSID (only EID and LEN=0) */
		s1g_ies = morse_dot11_insert_ie(s1g_ies,
			NULL,
			WLAN_EID_SSID,
			0);
	}
	ies_mask->ies[WLAN_EID_SSID].ptr = NULL;
	ht_cap = (const struct ieee80211_ht_cap *)ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr;
	morse_dot11ah_mask_ies(ies_mask, false, false);
	s1g_ies = morse_dot11ah_insert_required_tx_ie(ies_mask, s1g_ies);
	s1g_ies += morse_dot11ah_insert_s1g_capability(vif,
		ht_cap,
		s1g_ies,
		mors_if->custom_configs->sta_type,
		mors_if->custom_configs->enable_ampdu);

	/* Note: The following parameters should be stripped if they exist, but
	 * in the current implementation we only insert the elements we are
	 * interested in. So by default they will not be added. For reference,
	 * these IEs are:
	 * WLAN_EID_DS_PARAMS
	 * WLAN_EID_ERP_INFO
	 * WLAN_EID_EXT_SUPP_RATES
	 * WLAN_EID_HT_CAPABILITY
	 * WLAN_EID_HT_OPERATION
	 */

	s1g_length = s1g_ies - (u8 *)s1g_probe_req;
	if (skb->len < s1g_length)
		skb_put(skb, s1g_length - skb->len);

	memcpy(skb->data, s1g_probe_req, s1g_length);
	kfree(s1g_probe_req);

	skb_trim(skb, s1g_length);
}

static void morse_dot11ah_blockack_to_s1g(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_mgmt *back = (struct ieee80211_mgmt *)skb->data;

	switch (back->u.action.u.addba_req.action_code) {
	case WLAN_ACTION_ADDBA_REQ:
		back->u.action.u.addba_req.action_code = WLAN_ACTION_NDP_ADDBA_REQ;
		break;
	case WLAN_ACTION_ADDBA_RESP:
		back->u.action.u.addba_req.action_code = WLAN_ACTION_NDP_ADDBA_RESP;
		break;
	case WLAN_ACTION_DELBA:
		back->u.action.u.addba_req.action_code = WLAN_ACTION_NDP_DELBA;
		break;
	default:
		break;
	}
}

static int morse_dot11ah_beacon_to_s1g_size(
	struct ieee80211_vif *vif, struct sk_buff *skb, bool short_beacon, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *beacon = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	u8 *beacon_ies = beacon->u.beacon.variable;
	int header_length = beacon_ies - skb->data;
	int beacon_ies_len = skb->len - header_length;
	/* Initially, the size equals to s1g_beacon header length */
	int s1g_length = offsetof(struct ieee80211_ext, u.s1g_beacon.variable);

	if (morse_dot11ah_parse_ies(beacon_ies, beacon_ies_len, ies_mask) < 0)
		return -EINVAL;

	if (short_beacon) {
		if (ies_mask->ies[WLAN_EID_SSID].ptr != NULL)
			s1g_length += 4;
	} else {
		s1g_length += morse_dot11ah_insert_s1g_compatibility(NULL, 0, 0, 0);
		s1g_length += morse_dot11ah_insert_s1g_capability(vif, NULL, NULL, 0, false);
		s1g_length += morse_dot11ah_insert_s1g_operation(NULL, NULL);
		s1g_length += morse_dot11ah_insert_s1g_short_beacon_interval(NULL, 0);
		if (mors_if->cac.enabled)
			s1g_length += morse_dot11ah_insert_s1g_cac(NULL, 0);

		morse_dot11ah_mask_ies(ies_mask, true, true);
		s1g_length += morse_dot11_required_tx_ies_size(ies_mask);
	}

	/* SW-2951: Include the TIM IE in both short and regular beacons */
	if (vif->type != NL80211_IFTYPE_ADHOC) {
		/* SW-4741: in IBSS, TIM element is not relevant and should not be inserted */
		s1g_length += morse_dot11ah_insert_s1g_tim(vif, NULL, NULL, 0);
	}

	if (ies_mask->ies[WLAN_EID_S1G_RPS].ptr)
		s1g_length += ies_mask->ies[WLAN_EID_S1G_RPS].len + 2;

	return s1g_length;
}

/**
 * Utility function to find EDCA parameter IE data in incoming beacon
 * from mac80211. The parameter data can either be part of EDCA IE or
 * WMM IE, which is Vendor specific IE. IEs of the beacon are already
 * parsed and stored in ies_mask and for vendor IEs, there can be multiple
 * elements stored in ies[WLAN_EID_VENDOR_SPECIFIC] list.
 */
static u8 *morse_dot11ah_find_edca_param_set_ie(
	struct ieee80211_vif *vif, struct sk_buff *skb,
	struct dot11ah_ies_mask *ies_mask, u16 *edca_ie_len)
{
	struct __ieee80211_vendor_ie_elem  *ven_ie = NULL;
	struct ie_element *elem = NULL;

	/* Check for EDCA IE presence */
	if (ies_mask->ies[WLAN_EID_EDCA_PARAM_SET].ptr) {

		*edca_ie_len = ies_mask->ies[WLAN_EID_EDCA_PARAM_SET].len;

		return ((u8 *)ies_mask->ies[WLAN_EID_EDCA_PARAM_SET].ptr);

	} else if (ies_mask->ies[WLAN_EID_VENDOR_SPECIFIC].ptr != NULL) {

		/* Check for WMM IE in the list of vendor specific IEs */
		ven_ie = (struct __ieee80211_vendor_ie_elem  *) ies_mask->ies[WLAN_EID_VENDOR_SPECIFIC].ptr;
		elem = &ies_mask->ies[WLAN_EID_VENDOR_SPECIFIC];

		while (elem != NULL && elem->ptr != NULL) {

			ven_ie = (struct __ieee80211_vendor_ie_elem  *)elem->ptr;

			if (IS_WMM_IE(ven_ie)) {

				*edca_ie_len = ies_mask->ies[WLAN_EID_VENDOR_SPECIFIC].len - sizeof(*ven_ie);

				return ((u8 *)ven_ie->attr);
			};

			elem = elem->next;
		}
	}

	return NULL;
}

/**
 * Utility function to find if beacon is changed as per IEEE-2020 sec 10.46.2
 * System information update procedure :
 *
 * ##
 * The S1G AP shall increase the value (modulo 256) of the Change Sequence field
 * in the next transmitted S1G Beacon frame(s) when a critical update occurs to any
 * of the elements inside the S1G Beacon frame. The following events shall classify
 * as a critical update:
 *   a) Inclusion of an Extended Channel Switch Announcement
 *   b) Modification of the EDCA parameters
 *   c) Modification of the S1G Operation element
 * ##
 *
 * 1st one is checked for presence of IE in incoming beacon from mac80211
 * 2nd & 3rd  IE changes are tracked using CRC values of prior beacon frames.
 *
 */
static int morse_dot11ah_find_beacon_change(
	struct ieee80211_vif *vif, struct sk_buff *skb,
	struct dot11ah_ies_mask *ies_mask,
	struct s1g_operation_parameters *s1g_oper_params)
{
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	u8 update_change_seq = 0;
	u32 ncrc = 0, op_param_crc = 0;
	u16 edca_ie_len = 0;

	u8 *edca_param_set = NULL;

	edca_param_set = morse_dot11ah_find_edca_param_set_ie(vif, skb, ies_mask, &edca_ie_len);
	op_param_crc = ~crc32(~0, (void *)s1g_oper_params, sizeof(struct s1g_operation_parameters));

	/**
	 * Find the channel switch announcement or extended channel switch announcement
	 */
	if (ies_mask->ies[WLAN_EID_CHANNEL_SWITCH].ptr != NULL  ||
	    ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr != NULL
		) {
		if (!mors_if->chan_switch_in_progress) {
			update_change_seq = 1;
			mors_if->chan_switch_in_progress = true;
			dot11ah_info("Detected CSA parameters IE change\n");
		}
	} else {
		mors_if->chan_switch_in_progress = false;
	}

	if (edca_param_set) {

		ncrc = ~crc32(~0, (void *)edca_param_set, edca_ie_len);

		/**
		 * Check for any EDCA parameters update
		 */
		if (!mors_if->edca_param_crc) {
			mors_if->edca_param_crc = ncrc;

		} else if (ncrc != mors_if->edca_param_crc) {
			update_change_seq = 1;
			mors_if->edca_param_crc = ncrc;
			dot11ah_info("Detected EDCA parameters IE change\n");
		}
	}

	if (!mors_if->s1g_oper_param_crc) {

		mors_if->s1g_oper_param_crc = op_param_crc;

	} else if (op_param_crc != mors_if->s1g_oper_param_crc) {

		/**
		 * Check for any S1G operational IE updates
		 */
		update_change_seq = 1;
		mors_if->s1g_oper_param_crc = op_param_crc;
		dot11ah_info("Detected S1G operation parameters IEs\n");
	}

	return update_change_seq;

}

static void morse_dot11ah_beacon_to_s1g(
	struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, bool short_beacon, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_mgmt *beacon = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_ext *s1g_beacon;
	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_tim_ie *tim;
	u8 tim_virtual_map_len_11n;
	u8 *s1g_ies = NULL;
	u8 *beacon_ies = beacon->u.beacon.variable;
	int header_length = beacon_ies - skb->data;
	int beacon_ies_len = skb->len - header_length;
	u64 now_usecs;
	__le16 frame_control = IEEE80211_FTYPE_EXT | IEEE80211_STYPE_S1G_BEACON;
	struct s1g_operation_parameters s1g_oper_params = {
		.chan_centre_freq_num = morse_dot11ah_freq_khz_bw_mhz_to_chan(
				HZ_TO_KHZ(mors_if->custom_configs->channel_info.op_chan_freq_hz),
				mors_if->custom_configs->channel_info.op_bw_mhz),
		.op_bw_mhz = mors_if->custom_configs->channel_info.op_bw_mhz,
		.pri_bw_mhz = mors_if->custom_configs->channel_info.pri_bw_mhz,
		.pri_1mhz_chan_idx = mors_if->custom_configs->channel_info.pri_1mhz_chan_idx,
		.s1g_operating_class = mors_if->custom_configs->channel_info.s1g_operating_class
	};

	/* An atomic allocation is required as this function can be called from
	 * the beacon tasklet.
	 */
	s1g_beacon = kmalloc(s1g_length, GFP_ATOMIC);
	BUG_ON(!s1g_beacon);

	if (morse_dot11ah_parse_ies(beacon_ies, beacon_ies_len, ies_mask)) {
		dot11ah_warn("Failed to parse IEs\n");
		dot11ah_hexdump_warn("IEs:", beacon_ies, beacon_ies_len);
	}

	if (ies_mask->ies[WLAN_EID_SSID].ptr != NULL && short_beacon)
		frame_control |= IEEE80211_FC_COMPRESS_SSID;

	/* SW-1974: Use the presence of the RSN element in the 80211n beacon
	 * to determine if the security supported bit should be set.
	 */
	if (ies_mask->ies[WLAN_EID_RSN].ptr != NULL)
		frame_control |= IEEE80211_FC_S1G_SECURITY_SUPPORTED;

	frame_control |= ieee80211ah_s1g_fc_bss_bw_lookup
		[mors_if->custom_configs->channel_info.pri_bw_mhz]
		[mors_if->custom_configs->channel_info.op_bw_mhz];

	/* Fill in the new beacon header, copied from incoming frame */

	s1g_beacon->frame_control = cpu_to_le16(frame_control);
	s1g_beacon->duration = 0;

	/* Lower 32 bits Get inserted into the timestamp field here */
	now_usecs = jiffies_to_usecs((get_jiffies_64() - mors_if->epoch));
	s1g_beacon->u.s1g_beacon.timestamp = cpu_to_le32(LOWER_32_BITS(now_usecs));

	/* SW-4741: for IBSS, SA address MUST be set to the randomly generated BSSID
	 * This will not break infrastructure BSS mode anyway as for this both SA and
	 * BSSID in beacon are equivalent
	 */
	memcpy(s1g_beacon->u.s1g_beacon.sa, beacon->bssid, sizeof(s1g_beacon->u.s1g_beacon.sa));

	s1g_ies = s1g_beacon->u.s1g_beacon.variable;
	ht_cap = (const struct ieee80211_ht_cap *) ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr;
	tim = (const struct ieee80211_tim_ie *) ies_mask->ies[WLAN_EID_TIM].ptr;

	/* 11n TIM is either 2 bytes (with no virtual map), or 3 bytes + virtual map */
	tim_virtual_map_len_11n = (ies_mask->ies[WLAN_EID_TIM].len <= 2) ?
			0 : (ies_mask->ies[WLAN_EID_TIM].len - 3);

	morse_dot11ah_mask_ies(ies_mask, true, true);

	/* The SSID is 2 octets into the value returned by find ie, and the
	 * length is the second octet
	 */
	if (short_beacon) {
		if (ies_mask->ies[WLAN_EID_SSID].ptr != NULL) {
			/* In Linux we have to pass ~0 as the seed value, and then invert the outcome */
			u32 cssid = ~crc32(~0, ies_mask->ies[WLAN_EID_SSID].ptr, ies_mask->ies[WLAN_EID_SSID].len);

			morse_dot11ah_store_cssid(
				cssid, ies_mask->ies[WLAN_EID_SSID].len, ies_mask->ies[WLAN_EID_SSID].ptr, beacon->u.beacon.capab_info, NULL, 0, NULL);

			/* Insert CSSID (as first entry in s1g_beacon->variable for short beacon) */
			*((u32 *)s1g_ies) = cssid;
			s1g_ies += 4;
		}

		/* HaL-4.2.28 Requires broadcast/multicast traffic information to appear
		 * in beacons (even short beacons!). See SW-2820 for more information.
		 * See SW-2951, now we 'always' include TIM IE in short beacons
		 */
		if (vif->type != NL80211_IFTYPE_ADHOC)
			/* SW-4741: in IBSS, TIM element is not relevant and should not be inserted */
			s1g_ies += morse_dot11ah_insert_s1g_tim(vif, s1g_ies, tim, tim_virtual_map_len_11n);

		/* Need to mask RPS element and add it explicitly to maintain the order of information elements */
		if (ies_mask->ies[WLAN_EID_S1G_RPS].ptr) {
			s1g_ies = morse_dot11_insert_ie_from_ies_mask(s1g_ies, ies_mask, WLAN_EID_S1G_RPS);
			ies_mask->ies[WLAN_EID_S1G_RPS].ptr = NULL;
		}

	} else {
		struct morse_channel_info *chan_info = &mors_if->custom_configs->channel_info;
		struct s1g_operation_parameters s1g_oper_params = {
			.chan_centre_freq_num = morse_dot11ah_freq_khz_bw_mhz_to_chan(
					HZ_TO_KHZ(chan_info->op_chan_freq_hz),
					chan_info->op_bw_mhz),
			.op_bw_mhz = chan_info->op_bw_mhz,
			.pri_bw_mhz = chan_info->pri_bw_mhz,
			.pri_1mhz_chan_idx = chan_info->pri_1mhz_chan_idx,
			.s1g_operating_class = chan_info->s1g_operating_class
		};
		/* Order of the information elements specified in table 9-46 of IEEE802.11-2020 */
		s1g_ies += morse_dot11ah_insert_s1g_compatibility(
			s1g_ies,
			beacon->u.beacon.beacon_int *
			vif->bss_conf.dtim_period,
			beacon->u.beacon.capab_info,
			/** Upper 32 bits Get inserted into the tsf_completion field */
			UPPER_32_BITS(now_usecs));

		if (vif->type != NL80211_IFTYPE_ADHOC)
			/* SW-4741: in IBSS, TIM element is not relevant and should not be inserted */
			s1g_ies += morse_dot11ah_insert_s1g_tim(vif, s1g_ies, tim, tim_virtual_map_len_11n);

		/* Need to mask RPS element and add it explicitly to maintain the order of information elements */
		if (ies_mask->ies[WLAN_EID_S1G_RPS].ptr) {
			s1g_ies = morse_dot11_insert_ie_from_ies_mask(s1g_ies, ies_mask, WLAN_EID_S1G_RPS);
			ies_mask->ies[WLAN_EID_S1G_RPS].ptr = NULL;
		}

		s1g_ies += morse_dot11ah_insert_s1g_capability(vif,
			ht_cap,
			s1g_ies,
			mors_if->custom_configs->sta_type,
			mors_if->custom_configs->enable_ampdu);

		s1g_ies += morse_dot11ah_insert_s1g_operation(s1g_ies, &s1g_oper_params);

		s1g_ies += morse_dot11ah_insert_s1g_short_beacon_interval(
			s1g_ies, beacon->u.beacon.beacon_int);

		if (mors_if->cac.enabled)
			s1g_ies += morse_dot11ah_insert_s1g_cac(s1g_ies,
					mors_if->cac.threshold_index);

		if (ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr) {
			morse_dot11ah_convert_ecsa_info_to_s1g(mors_if, ies_mask);
			if (mors_if->mask_ecsa_info_in_beacon)
				morse_dot11ah_check_for_ecsa_in_new_channel(vif, ies_mask);
		}

		s1g_ies = morse_dot11ah_insert_required_tx_ie(ies_mask, s1g_ies);
	}

	/* Detect the change in beacon IEs and update the change seq number.
	 * Add mode check as beacon change sequence is not applicable for adhoc mode
	 */
	if ((vif->type == NL80211_IFTYPE_AP) && morse_dot11ah_find_beacon_change(vif, skb, ies_mask, &s1g_oper_params)) {
		mors_if->s1g_bcn_change_seq++;
		mors_if->s1g_bcn_change_seq = (mors_if->s1g_bcn_change_seq % 256);
		dot11ah_info("Updating the change seq num to %d\n", mors_if->s1g_bcn_change_seq);
	}

	s1g_beacon->u.s1g_beacon.change_seq = mors_if->s1g_bcn_change_seq;

	s1g_length = s1g_ies - (u8 *)s1g_beacon;
	if (skb->len < s1g_length)
		skb_put(skb, s1g_length - skb->len);

	memcpy(skb->data, s1g_beacon, s1g_length);
	kfree(s1g_beacon);

	skb_trim(skb, s1g_length);
}

int morse_dot11ah_11n_to_s1g_tx_packet_size(struct ieee80211_vif *vif,
	struct sk_buff *skb, bool short_beacon, struct dot11ah_ies_mask *ies_mask)
{
	int size;
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)skb->data;
	size = skb->len + skb_tailroom(skb);

	if (ieee80211_is_beacon(hdr->frame_control))
		size = morse_dot11ah_beacon_to_s1g_size(vif, skb, short_beacon, ies_mask);
	else if (ieee80211_is_probe_req(hdr->frame_control))
		size = morse_dot11ah_probe_req_to_s1g_size(vif, skb, ies_mask);
	else if (ieee80211_is_probe_resp(hdr->frame_control))
		size = morse_dot11ah_probe_resp_to_s1g_size(vif, skb, ies_mask);
	else if (ieee80211_is_assoc_req(hdr->frame_control) ||
		ieee80211_is_reassoc_req(hdr->frame_control))
		size = morse_dot11ah_assoc_req_to_s1g_size(vif, skb, ies_mask);
	else if (ieee80211_is_assoc_resp(hdr->frame_control) ||
		ieee80211_is_reassoc_resp(hdr->frame_control))
		size = morse_dot11ah_assoc_resp_to_s1g_size(vif, skb, ies_mask);

	return size;
}
EXPORT_SYMBOL(morse_dot11ah_11n_to_s1g_tx_packet_size);

void morse_dot11ah_11n_to_s1g_tx_packet(
	struct ieee80211_vif *vif, struct sk_buff *skb, int s1g_length, bool short_beacon, struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)skb->data;

	if (ieee80211_is_action(hdr->frame_control)) {
		struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)hdr;

		if (mgmt->u.action.category == WLAN_CATEGORY_BACK)
			morse_dot11ah_blockack_to_s1g(vif, skb);
	}
	if (ieee80211_is_beacon(hdr->frame_control))
		morse_dot11ah_beacon_to_s1g(vif, skb, s1g_length, short_beacon, ies_mask);
	else if (ieee80211_is_probe_req(hdr->frame_control))
		morse_dot11ah_probe_req_to_s1g(vif, skb, s1g_length, ies_mask);
	else if (ieee80211_is_probe_resp(hdr->frame_control))
		morse_dot11ah_probe_resp_to_s1g(vif, skb, s1g_length, ies_mask);
	else if (ieee80211_is_assoc_req(hdr->frame_control) ||
		ieee80211_is_reassoc_req(hdr->frame_control))
		morse_dot11ah_assoc_req_to_s1g(vif, skb, s1g_length, ies_mask);
	else if (ieee80211_is_assoc_resp(hdr->frame_control) ||
		ieee80211_is_reassoc_resp(hdr->frame_control))
		morse_dot11ah_assoc_resp_to_s1g(vif, skb, s1g_length, ies_mask);

	skb_trim(skb, s1g_length);
}
EXPORT_SYMBOL(morse_dot11ah_11n_to_s1g_tx_packet);

/**
 * @brief Sets the S1G caps based on the chip capabilities, modparams and user runtime configs
 *
 * @param vif The vif whose s1g capabilities element is to be configured
 * @return int 0 if configured successfully, < 0 with error code otherwise
 */
static int morse_dot11ah_set_s1g_capab(struct ieee80211_vif *vif)
{
	struct morse_vif *mors_vif;
	struct ieee80211_s1g_cap *s1g_capab;
	const struct morse_custom_configs *mors_cfg;
	const u8 s1g_mcs_map = 0xFD;

	if (!vif)
		return -ENOENT;

	mors_vif = (struct morse_vif *)vif->drv_priv;

	if (!mors_vif)
		return -ENOENT;

	mors_cfg = mors_vif->custom_configs;
	s1g_capab = &mors_vif->s1g_cap_ie;

	memset(s1g_capab, 0, sizeof(*s1g_capab));

	/* Following the format given in
	 * 9.4.2.200.2 S1G Capabilities Information field
	 * Note these are 0 indexed in code, 1 indexed in the standard
	 */
	/* S1G Cap IE Octet 1 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, S1G_LONG))
		s1g_capab->capab_info[0] |= S1G_CAP0_S1G_LONG;

	if (mors_vif->custom_configs->enable_sgi_rc) {
		s1g_capab->capab_info[0] |= S1G_CAP0_SGI_1MHZ;
		s1g_capab->capab_info[0] |= MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 2MHZ)  ?
						S1G_CAP0_SGI_2MHZ  : 0;
		s1g_capab->capab_info[0] |= MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 4MHZ)  ?
						S1G_CAP0_SGI_4MHZ  : 0;
		s1g_capab->capab_info[0] |= MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 8MHZ)  ?
						S1G_CAP0_SGI_8MHZ  : 0;
		s1g_capab->capab_info[0] |= MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 16MHZ) ?
						S1G_CAP0_SGI_16MHZ : 0;

		/* SW-3993 - It is determined that for the current HaLow R1 test bed
		 * we have to signal 4MHz SGI support but not 4MHz width support.
		 * Hardcode it here, and TODO: Remove 4MHz SGI hardcoding
		 */
		s1g_capab->capab_info[0] |= S1G_CAP0_SGI_4MHZ;
	}

	s1g_capab->capab_info[0] |= MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 16MHZ) ?
						 S1G_CAP0_SUPP_16MHZ  :
				    MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 8MHZ)  ?
						S1G_CAP0_SUPP_8MHZ    :
				    MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 4MHZ)  ?
						S1G_CAP0_SUPP_4MHZ    : 0;

	/* S1G Cap IE Octet 3 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, MU_BEAMFORMEE))
		s1g_capab->capab_info[2] |= S1G_CAP2_MU_BFEE;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, MU_BEAMFORMER))
		s1g_capab->capab_info[2] |= S1G_CAP2_MU_BFER;

	if (mors_vif->custom_configs->enable_trav_pilot) {
		if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TRAVELING_PILOT_ONE_STREAM))
			s1g_capab->capab_info[2] |=
					S1G_CAP2_SET_TRAV_PILOT(TRAV_PILOT_RX_1NSS);
		else if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TRAVELING_PILOT_TWO_STREAM))
			s1g_capab->capab_info[2] |=
					S1G_CAP2_SET_TRAV_PILOT(TRAV_PILOT_RX_1_2_NSS);
	}

	/* S1G Cap IE Octet 4 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, RD_RESPONDER))
		s1g_capab->capab_info[3] |= S1G_CAP3_RD_RESPONDER;

	if (mors_vif->custom_configs->enable_ampdu) {
		s1g_capab->capab_info[3] |= S1G_CAP3_SET_MPDU_MAX_LEN(S1G_CAP3_MPDU_MAX_LEN_3895);
		s1g_capab->capab_info[3] |= S1G_CAP3_SET_MAX_AMPDU_LEN_EXP(
					    mors_vif->capabilities.maximum_ampdu_length_exponent);
		s1g_capab->capab_info[3] |= S1G_CAP3_SET_MIN_AMPDU_START_SPC(
					    mors_vif->capabilities.ampdu_mss);
	}

	/* S1G Cap IE Octet 5 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, UPLINK_SYNC))
		s1g_capab->capab_info[4] |= S1G_CAP4_UPLINK_SYNC;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, DYNAMIC_AID))
		s1g_capab->capab_info[4] |= S1G_CAP4_DYNAMIC_AID;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, BAT))
		s1g_capab->capab_info[4] |= S1G_CAP4_BAT;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TIM_ADE))
		s1g_capab->capab_info[4] |= S1G_CAP4_TIME_ADE;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, NON_TIM))
		s1g_capab->capab_info[4] |= S1G_CAP4_NON_TIM;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, GROUP_AID))
		s1g_capab->capab_info[4] |= S1G_CAP4_GROUP_AID;

	/* Determine user configured STA type, check the fw supports it */
	if (vif->type == NL80211_IFTYPE_AP) {
		switch (mors_vif->custom_configs->sta_type) {
		case STA_TYPE_SENSOR:
			if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, STA_TYPE_SENSOR))
				s1g_capab->capab_info[4] |= S1G_CAP4_STA_TYPE_SENSOR;
			break;
		case STA_TYPE_NON_SENSOR:
			if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, STA_TYPE_NON_SENSOR))
				s1g_capab->capab_info[4] |= S1G_CAP4_STA_TYPE_NON_SENSOR;
			break;
		case STA_TYPE_MIXED:
		default:
			s1g_capab->capab_info[4] &= ~S1G_CAP4_STA_TYPE;
			break;
		}
	} else if (vif->type == NL80211_IFTYPE_STATION) {
		if (mors_vif->custom_configs->sta_type == STA_TYPE_NON_SENSOR) {
			if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, STA_TYPE_NON_SENSOR))
				s1g_capab->capab_info[4] |= S1G_CAP4_STA_TYPE_NON_SENSOR;
		} else if (mors_vif->custom_configs->sta_type == STA_TYPE_SENSOR)
			if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, STA_TYPE_SENSOR))
				s1g_capab->capab_info[4] |= S1G_CAP4_STA_TYPE_SENSOR;
	}

	/* S1G Cap IE Octet 6 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, CAC))
		s1g_capab->capab_info[5] |= S1G_CAP5_CENT_AUTH_CONTROL;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, DAC))
		s1g_capab->capab_info[5] |= S1G_CAP5_DIST_AUTH_CONTROL;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, AMSDU))
		s1g_capab->capab_info[5] |= S1G_CAP5_AMSDU;

	if (mors_vif->custom_configs->enable_ampdu)
		if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, AMPDU))
			s1g_capab->capab_info[5] |= S1G_CAP5_AMPDU;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, ASYMETRIC_BA_SUPPORT))
		s1g_capab->capab_info[5] |= S1G_CAP5_ASYMMETRIC_BA;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, FLOW_CONTROL))
		s1g_capab->capab_info[5] |= S1G_CAP5_FLOW_CONTROL;

	/* TODO: Handle the following: */
	/* TXOP_SECTORIZATION */
	/* GROUP_SECTORIZATION */

	/* S1G Cap IE Octet 7 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, OBSS_MITIGATION))
		s1g_capab->capab_info[6] |= S1G_CAP6_OBSS_MITIGATION;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, FRAGMENT_BA))
		s1g_capab->capab_info[6] |= S1G_CAP6_FRAGMENT_BA;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, NDP_PSPOLL))
		s1g_capab->capab_info[6] |= S1G_CAP6_NDP_PS_POLL;

	if (mors_vif->custom_configs->raw.enabled)
		if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, RAW))
			s1g_capab->capab_info[6] |= S1G_CAP6_RAW_OPERATION;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, PAGE_SLICING))
		s1g_capab->capab_info[6] |= S1G_CAP6_PAGE_SLICING;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TXOP_SHARING_IMPLICIT_ACK))
		s1g_capab->capab_info[6] |= S1G_CAP6_TXOP_SHARING_IMP_ACK;

	/* TODO: handle VHT Link Adaptation Capable field properly */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, HTC_VHT_MFB))
		s1g_capab->capab_info[6] |= S1G_CAP6_VHT_LINK_ADAPT;

	/* S1G Cap IE Octet 8 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TACK_AS_PSPOLL))
		s1g_capab->capab_info[7] |= S1G_CAP7_TACK_AS_PS_POLL;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, DUPLICATE_1MHZ))
		s1g_capab->capab_info[7] |= S1G_CAP7_DUP_1MHZ;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, MCS_NEGOTIATION))
		s1g_capab->capab_info[7] |= S1G_CAP7_DUP_1MHZ;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, 1MHZ_CONTROL_RESPONSE_PREAMBLE) &&
	    mors_vif->ctrl_resp_out_1mhz_en)
		s1g_capab->capab_info[7] |= S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, NDP_BEAMFORMING_REPORT))
		s1g_capab->capab_info[7] |= S1G_CAP7_NDP_BFING_REPORT_POLL;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, UNSOLICIT_DYNAMIC_AID))
		s1g_capab->capab_info[7] |= S1G_CAP7_UNSOLICITED_DYN_AID;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, SECTOR_TRAINING))
		s1g_capab->capab_info[7] |= S1G_CAP7_SECTOR_TRAINING_OPERATION;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, TMP_PS_MODE_SWITCH))
		s1g_capab->capab_info[7] |= S1G_CAP7_TEMP_PS_MODE_SWITCH;

	/* S1G Cap IE Octet 9 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, BDT))
		s1g_capab->capab_info[8] |= S1G_CAP8_BDT;

	if (vif->type == NL80211_IFTYPE_AP)
		s1g_capab->capab_info[8] |= S1G_CAP8_SET_COLOR(mors_vif->bss_color);

	if (mors_vif->twt.requester)
		s1g_capab->capab_info[8] |= S1G_CAP8_TWT_REQUEST;

	if (mors_vif->twt.responder)
		s1g_capab->capab_info[8] |= S1G_CAP8_TWT_RESPOND;

	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, PV1))
		s1g_capab->capab_info[8] |= S1G_CAP8_PV1_FRAME;

	/* S1G Cap IE Octet 10 */
	if (MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, LINK_ADAPTATION_WO_NDP_CMAC))
		s1g_capab->capab_info[9] |= S1G_CAP9_LINK_ADAPT_PER_CONTROL_RESPONSE;

	/* 9.4.2.200.3 Supported S1G-MCS and NSS Set field */
	/* RX S1G-MCS MAP B0-B7 */
	s1g_capab->supp_mcs_nss[0] = (s1g_mcs_map & 0xFF);
	/* Rx Highest Supported Long GI Data Rate B8-B16 */
	s1g_capab->supp_mcs_nss[1] = 0x0;
	/* TX S1G-MCS Map B17-B24 */
	s1g_capab->supp_mcs_nss[2] = ((s1g_mcs_map << 1) & 0xFE);
	/* TX Highest Supported Long GI Data Rate B25-B33 */
	s1g_capab->supp_mcs_nss[3] = ((s1g_mcs_map >> 7) & 0x01);
	s1g_capab->supp_mcs_nss[4] = 0x0;

	return 0;
}
