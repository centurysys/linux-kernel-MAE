/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <net/mac80211.h>
#include <net/netlink.h>

#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "vendor.h"

/** Extra overhead to account for any additional netlink framing */
#define VENDOR_EVENT_OVERHEAD			(30)

/* Allow/Disallow insertion of morse vendor ie */
static bool enable_mm_vendor_ie __read_mostly = true;
module_param(enable_mm_vendor_ie, bool, 0644);
MODULE_PARM_DESC(enable_mm_vendor_ie, "Allow/Disallow insertion of morse vendor ie");

static int
morse_vendor_cmd_to_morse(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data, int data_len)
{
	struct sk_buff *skb;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct morse *mors = hw->priv;
	int skb_len, dataout_len;
	void *datain, *dataout;

	if ((!data) || (data_len < sizeof(struct morse_cmd)))
		return -EINVAL;

	/* we need a non const cmd */
	datain =  kzalloc(sizeof(struct morse_cmd_vendor), GFP_KERNEL);
	if (!datain)
		return -ENOMEM;
	memcpy(datain, data, data_len);

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
				sizeof(struct morse_resp_vendor));
	if (!skb) {
		kfree(datain);
		return -ENOMEM;
	}

	skb_len = skb->len;
	dataout = skb_put(skb, sizeof(struct morse_resp_vendor));

	mutex_lock(&mors->lock);
	morse_cmd_vendor(mors, datain, data_len, dataout, &dataout_len);
	mutex_unlock(&mors->lock);
	skb_len += dataout_len;
	kfree(datain);
	skb_trim(skb, skb_len);
	return cfg80211_vendor_cmd_reply(skb);
}

static const struct wiphy_vendor_command morse_vendor_commands[] = {
	{
		.info = {
			.vendor_id = MORSE_OUI,
			.subcmd = MORSE_VENDOR_CMD_TO_MORSE,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
#if KERNEL_VERSION(5, 3, 0) <= MAC80211_VERSION_CODE
		.policy = VENDOR_CMD_RAW_DATA,
#endif
		.doit = morse_vendor_cmd_to_morse,
	},
};

static const struct nl80211_vendor_cmd_info morse_vendor_events[] = {
	[MORSE_VENDOR_EVENT_VENDOR_IE_FOUND] = {
		.vendor_id = MORSE_OUI,
		.subcmd = MORSE_VENDOR_EVENT_VENDOR_IE_FOUND
	}
};

void morse_set_vendor_commands_and_events(struct wiphy *wiphy)
{
	wiphy->vendor_commands = morse_vendor_commands;
	wiphy->n_vendor_commands = ARRAY_SIZE(morse_vendor_commands);
	wiphy->vendor_events = morse_vendor_events;
	wiphy->n_vendor_events = ARRAY_SIZE(morse_vendor_events);
}

static struct sk_buff *put_vendor_ie(
	struct dot11_morse_vendor_caps_ops_ie *data, struct sk_buff *pkt)
{
	u8 *dst;
	struct ieee80211_vendor_ie ie = {
		.element_id = WLAN_EID_VENDOR_SPECIFIC,
		.len = sizeof(*data),
	};

	/* Fill oui */
	memcpy(&data->oui, morse_oui, sizeof(ie.oui));
	data->oui_type = MORSE_VENDOR_IE_CAPS_OPS_OUI_TYPE;

	/* Copy into packet */
	dst = skb_put(pkt, sizeof(ie.element_id) + sizeof(ie.len) + sizeof(*data));
	*dst++ = ie.element_id;
	*dst++ = ie.len;
	memcpy(dst, data, sizeof(*data));
	return pkt;
}

struct sk_buff *morse_vendor_insert_caps_ops_ie(struct morse *mors,
				struct sk_buff *skb)
{
	struct ieee80211_vif *ap_vif = morse_get_ap_vif(mors);
	struct ieee80211_vif *sta_vif = morse_get_sta_vif(mors);
	struct morse_vif *mors_vif = NULL;
	struct dot11_morse_vendor_caps_ops_ie ie_data = {0};
	struct morse_sta *mors_sta = NULL;
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	bool is_assoc_reassoc_req = (ieee80211_is_assoc_req(mgmt->frame_control)
		|| ieee80211_is_reassoc_req(mgmt->frame_control));
	bool is_assoc_reassoc_resp = (ieee80211_is_assoc_resp(mgmt->frame_control)
		|| ieee80211_is_reassoc_resp(mgmt->frame_control));

	if (!enable_mm_vendor_ie)
		return skb;

	/* Fill common version information */
	ie_data.hw_ver = mors->chip_id;
	ie_data.sw_ver.reserved = 0;
	ie_data.sw_ver.major = mors->sw_ver.major;
	ie_data.sw_ver.minor = mors->sw_ver.minor;
	ie_data.sw_ver.patch = mors->sw_ver.patch;

	if (ap_vif != NULL && ap_vif->type == NL80211_IFTYPE_AP) {
		/* Always indicate usage of DTIM CTS-to-self */
		mors_vif = (struct morse_vif *)ap_vif->drv_priv;
		if (MORSE_OPS_IN_USE(&mors_vif->operations, DTIM_CTS_TO_SELF))
			ie_data.ops0 |= MORSE_VENDOR_IE_OPS0_DTIM_CTS_TO_SELF;

		/* See if we need to negotiate LEGACY AMSDU for sta */
		if (is_assoc_reassoc_resp) {
			sta = ieee80211_find_sta(ap_vif, mgmt->da);
			if (sta)
				mors_sta = (struct morse_sta *)sta->drv_priv;

			if (mors_sta && mors_sta->vendor_info.valid) {
				/* STA has previously indicated that it would like AMSDU */
				if (MORSE_OPS_IN_USE(&mors_sta->vendor_info.operations,
					LEGACY_AMSDU)) {
					if (mors->custom_configs.enable_legacy_amsdu)
						ie_data.ops0 |= MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU;
					else
						MORSE_OPS_CLEAR(&mors_sta->vendor_info.operations,
							LEGACY_AMSDU);
				}
			}
		}
	} else if (sta_vif != NULL && (sta_vif->type == NL80211_IFTYPE_STATION)) {
		if (is_assoc_reassoc_req) {
			/* Attempt to negotiate Legacy AMSDU */
			if (mors->custom_configs.enable_legacy_amsdu)
				ie_data.ops0 |= MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU;
		}
	}

	skb = put_vendor_ie(&ie_data, skb);

	return skb;
}

void morse_vendor_rx_caps_ops_ie(struct morse *mors,
	const struct ieee80211_mgmt *mgmt, struct dot11ah_ies_mask *ies_mask)
{
	struct ie_element *cur = &ies_mask->ies[WLAN_EID_VENDOR_SPECIFIC];
	struct ieee80211_vif *ap_vif = morse_get_ap_vif(mors);
	struct ieee80211_vif *sta_vif = morse_get_sta_vif(mors);
	struct dot11_morse_vendor_caps_ops_ie *ie = NULL;
	bool found = false;
	struct morse_sta *mors_sta = NULL;
	struct ieee80211_sta *sta = NULL;
	bool is_assoc_reassoc_req = (ieee80211_is_assoc_req(mgmt->frame_control)
		|| ieee80211_is_reassoc_req(mgmt->frame_control));
	bool is_assoc_reassoc_resp = (ieee80211_is_assoc_resp(mgmt->frame_control)
		|| ieee80211_is_reassoc_resp(mgmt->frame_control));

	while (cur != NULL && cur->ptr != NULL) {
		ie = (struct dot11_morse_vendor_caps_ops_ie *) cur->ptr;
		if (memcmp(ie->oui, morse_oui, sizeof(ie->oui)) == 0 &&
			ie->oui_type == MORSE_VENDOR_IE_CAPS_OPS_OUI_TYPE) {
			found = true;
			break;
		}
		cur = cur->next;
	}

	if (!found)
		return;

	BUG_ON(!is_assoc_reassoc_req && !is_assoc_reassoc_resp);

	if (ap_vif != NULL && ap_vif->type == NL80211_IFTYPE_AP && is_assoc_reassoc_req) {
		sta = ieee80211_find_sta(ap_vif, mgmt->sa);
		if (sta) {
			mors_sta = (struct morse_sta *)sta->drv_priv;
			memset(&mors_sta->vendor_info, 0, sizeof(mors_sta->vendor_info));

			/* Unconditionally fill version information */
			mors_sta->vendor_info.valid = true;
			mors_sta->vendor_info.chip_id = ie->hw_ver;
			mors_sta->vendor_info.sw_ver.major = ie->sw_ver.major;
			mors_sta->vendor_info.sw_ver.minor = ie->sw_ver.minor;
			mors_sta->vendor_info.sw_ver.patch = ie->sw_ver.patch;

			if ((ie->ops0 & MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU) &&
				mors->custom_configs.enable_legacy_amsdu)
				MORSE_OPS_SET(&mors_sta->vendor_info.operations,
					LEGACY_AMSDU);
			else
				MORSE_OPS_CLEAR(&mors_sta->vendor_info.operations,
					LEGACY_AMSDU);
		}
	} else if (sta_vif != NULL && (sta_vif->type == NL80211_IFTYPE_STATION) &&
				is_assoc_reassoc_resp) {
		struct morse_vif *mors_vif = (struct morse_vif *)sta_vif->drv_priv;

		memset(&mors_vif->bss_vendor_info, 0,
			sizeof(mors_vif->bss_vendor_info));
		mors_vif->bss_vendor_info.valid = true;
		mors_vif->bss_vendor_info.chip_id = ie->hw_ver;
		mors_vif->bss_vendor_info.sw_ver.major = ie->sw_ver.major;
		mors_vif->bss_vendor_info.sw_ver.minor = ie->sw_ver.minor;
		mors_vif->bss_vendor_info.sw_ver.patch = ie->sw_ver.patch;

		if (ie->ops0 & MORSE_VENDOR_IE_OPS0_DTIM_CTS_TO_SELF)
			MORSE_OPS_SET(&mors_vif->bss_vendor_info.operations,
				DTIM_CTS_TO_SELF);

		if ((ie->ops0 & MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU) &&
			mors->custom_configs.enable_legacy_amsdu) {
			/* AP agreed to our request for LEGACY AMSDU */
			MORSE_OPS_SET(&mors_vif->operations, LEGACY_AMSDU);
			MORSE_OPS_SET(&mors_vif->bss_vendor_info.operations,
				LEGACY_AMSDU);
		} else {
			MORSE_OPS_CLEAR(&mors_vif->operations, LEGACY_AMSDU);
		}
	}
}

void morse_vendor_reset_sta_transient_info(struct ieee80211_vif *vif,
	struct morse_sta *mors_sta)
{
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;

	memset(&mors_sta->vendor_info, 0, sizeof(mors_sta->vendor_info));
	if (vif->type == NL80211_IFTYPE_STATION) {
		memset(&mors_vif->operations, 0, sizeof(mors_vif->operations));
		memset(&mors_vif->bss_vendor_info, 0,
			sizeof(mors_vif->bss_vendor_info));
	}
}

int morse_vendor_get_ie_len_for_pkt(struct sk_buff *pkt, int oui_type)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)pkt->data;

	if (!enable_mm_vendor_ie)
		return 0;

	if (!(ieee80211_is_assoc_req(hdr->frame_control) ||
		ieee80211_is_reassoc_req(hdr->frame_control) ||
		ieee80211_is_assoc_resp(hdr->frame_control) ||
		ieee80211_is_reassoc_resp(hdr->frame_control)))
		return 0; /* No insertion of IE on any other frames */

	if (oui_type != MORSE_VENDOR_IE_CAPS_OPS_OUI_TYPE)
		return 0;

	return sizeof(struct dot11_morse_vendor_caps_ops_ie) + 2;
}

int morse_vendor_send_vendor_ie_found_event(struct ieee80211_vif *vif,
						struct ieee80211_vendor_ie *vie)
{
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct sk_buff *skb;
	int ret;

	skb = cfg80211_vendor_event_alloc(wdev->wiphy, NULL, vie->len + VENDOR_EVENT_OVERHEAD,
			MORSE_VENDOR_EVENT_VENDOR_IE_FOUND, GFP_KERNEL);

	if (!skb)
		return -ENOMEM;

	ret = nla_put(skb, MORSE_VENDOR_ATTR_DATA, vie->len, vie->oui);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	cfg80211_vendor_event(skb, GFP_KERNEL);
	return 0;
}
