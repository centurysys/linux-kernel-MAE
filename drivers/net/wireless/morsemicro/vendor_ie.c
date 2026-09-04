/*
 * Copyright 2022 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include <linux/ieee80211.h>
#include <net/mac80211.h>

#include "vendor_ie.h"
#include "morse.h"
#include "debug.h"
#include "vendor.h"
#include "wiphy.h"
#include "mac.h"
#include "dot11ah/s1g_ieee80211.h"

/**
 * Limit the max size of appended vendor elements. This is required to avoid the beacon getting
 * fragmented at MCS0 1MHz primary bandwidth, which is not permitted by the 802.11 protocol.
 * Default size is set to support 2 max size Vendor IEs. 2 x (255 + 2) = 514.
 * (note: +2 for element ID and length)
 */
static uint max_total_vendor_ie_bytes = 2 * (U8_MAX + 2);
module_param(max_total_vendor_ie_bytes, uint, 0644);
MODULE_PARM_DESC(max_total_vendor_ie_bytes, "Max total bytes for runtime vendor IEs");

/**
 * Add a vendor IE to the vendor IE list, to be inserted in specified management frames
 *
 * @mors_vif Interface to insert the IEs on
 * @mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frames to insert the vendor element in
 * @data The vendor element data (starting from OUI)
 * @data_len Length of data
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_add_to_ie_list(struct morse_vif *mors_vif, u16 mgmt_type_mask,
					  u8 *data, u16 data_len)
{
	struct morse_vendor_ie *vie_config;
	struct vendor_ie_list_item *item;
	struct ieee80211_vendor_ie *ie;
	const u8 ie_hdr_len = sizeof(ie->element_id) + sizeof(ie->len);
	const u16 full_ie_length = ie_hdr_len + data_len;

	/* Make sure we are within bounds. Vendor IEs must have at least an OUI & OUI type. */
	if (data_len < (sizeof(ie->oui) + sizeof(ie->oui_type)) ||
	    data_len > MORSE_MAX_VENDOR_IE_SIZE)
		return -EINVAL;

	if (!mors_vif)
		return -ENODEV;

	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config)
		return -ENXIO;

	if (morse_vendor_ie_get_ies_length(mors_vif, mgmt_type_mask) + full_ie_length >
	    max_total_vendor_ie_bytes)
		return -ENOSPC;

	item = kzalloc(sizeof(*item) + full_ie_length, GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->mgmt_type_mask = mgmt_type_mask;
	ie = (struct ieee80211_vendor_ie *)item->element;
	ie->element_id = WLAN_EID_VENDOR_SPECIFIC;
	ie->len = (u8)data_len;

	memcpy(item->element + ie_hdr_len, data, data_len);

	spin_lock_bh(&mors_vif->vendor_ie.lock);
	list_add_tail(&item->list, &vie_config->ie_list);
	spin_unlock_bh(&mors_vif->vendor_ie.lock);

	return 0;
}

/**
 * Clear the vendor IE list for particular management frame types
 *
 * @mors_vif Interface to operate on
 * @mgmt_type_mask Bitmask of management frame types to clear, or MORSE_VENDOR_IE_TYPE_ALL
 *			 to clear all
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_clear_ie_list(struct morse_vif *mors_vif, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *vendor_ie, *tmp;
	struct morse_vendor_ie *vie_config;

	if (!mors_vif)
		return 0;

	spin_lock_bh(&mors_vif->vendor_ie.lock);
	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config)
		goto exit;

	list_for_each_entry_safe(vendor_ie, tmp, &vie_config->ie_list, list) {
		if (vendor_ie->mgmt_type_mask & mgmt_type_mask) {
			list_del(&vendor_ie->list);
			kfree(vendor_ie);
		}
	}

exit:
	spin_unlock_bh(&mors_vif->vendor_ie.lock);
	return 0;
}

int morse_vendor_ie_process_rx_ies(struct wireless_dev *wdev, const u8 *ies, u16 length,
				   u16 mgmt_type)
{
	int ret = 0;
	const u8 *pos = ies;
	const u8 *const end = ies + length;
	struct vendor_ie_oui_filter_list_item *item;
	struct morse_vendor_ie *vie_config;
	const struct ieee80211_vendor_ie *vie = (const struct ieee80211_vendor_ie *)ies;
	const u8 min_vendor_ie_length = sizeof(*vie) - sizeof(vie->element_id) - sizeof(vie->len);
	struct morse_vif *mors_vif = morse_wdev_to_morse_vif(wdev);

	if (!mors_vif)
		return -ENODEV;

	vie_config = mors_vif->vendor_ie.vie_config;

	while ((pos < end) && (ret == 0)) {
		pos = cfg80211_find_ie(WLAN_EID_VENDOR_SPECIFIC, pos, length);
		if (!pos)
			break;

		vie = (const struct ieee80211_vendor_ie *)pos;

		if (vie_config && vie->len >= min_vendor_ie_length) {
			spin_lock_bh(&mors_vif->vendor_ie.lock);
			list_for_each_entry(item, &vie_config->oui_filter_list, list) {
				if ((memcmp(item->oui, vie->oui, sizeof(vie->oui)) == 0) &&
				    (item->mgmt_type_mask & mgmt_type)) {
					ret = item->on_vendor_ie_match(wdev, mgmt_type, vie);
					if (ret)
						break;
				}
			}
			spin_unlock_bh(&mors_vif->vendor_ie.lock);
		}

		pos = (u8 *)vie + sizeof(vie->element_id) + sizeof(vie->len) + vie->len;
	}
	return ret;
}

/**
 * Find a previously configured OUI in the OUI filter
 *
 * @mors_vif interface containing the OUI filter
 * @oui OUI to find
 * @return Pointer to the list item, or NULL if no match
 */
static struct vendor_ie_oui_filter_list_item *oui_filter_find_oui(struct morse_vif *mors_vif,
								  u8 *oui)
{
	struct vendor_ie_oui_filter_list_item *item;
	struct morse_vendor_ie *vie_config = mors_vif->vendor_ie.vie_config;

	if (!vie_config)
		return NULL;

	list_for_each_entry(item, &vie_config->oui_filter_list, list) {
		if (memcmp(item->oui, oui, sizeof(item->oui)) == 0)
			return item;
	}

	return NULL;
}

/**
 * Clear the specified mask bits on the OUI filter list item, and remove it if the mask is 0.
 *
 * @mors_vif interface to operate on
 * @item Filter list item to try to remove
 * @mgmt_type_mask Bitmask of flags to clear
 */
static void try_remove_oui(struct morse_vif *mors_vif,
			   struct vendor_ie_oui_filter_list_item *item, u16 mgmt_type_mask)
{
	struct morse_vendor_ie *vie_config = mors_vif->vendor_ie.vie_config;

	if (item->mgmt_type_mask & mgmt_type_mask)
		item->mgmt_type_mask &= ~mgmt_type_mask;

	if (item->mgmt_type_mask == 0) {
		list_del(&item->list);
		kfree(item);

		if (!vie_config)
			return;

		if (vie_config->n_oui_filters == 0)
			MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, 1);
		else
			vie_config->n_oui_filters--;
	}
}

/**
 * Add an OUI to the OUI filter. If the OUI is already in the list, this function will
 * instead update the management frame mask.
 *
 * @mors_vif Interface containing OUI filter
 * @mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frame types this filter applies to
 * @oui OUI to match
 * @on_vendor_ie_match Callback function to call when a vendor IE matching the OUI is found.
 *			     Is only applied for new OUIs (ie. OUIs not already in the list)
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_add_oui_to_filter(struct morse_vif *mors_vif, u16 mgmt_type_mask,
					     u8 *oui,
					     int (*on_vendor_ie_match)(struct wireless_dev *, u16,
					     const struct ieee80211_vendor_ie *))
{
	int ret = 0;
	struct vendor_ie_oui_filter_list_item *item;
	struct morse_vendor_ie *vie_config;

	if (!mors_vif)
		return -ENODEV;

	spin_lock_bh(&mors_vif->vendor_ie.lock);
	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config) {
		ret = -ENXIO;
		goto exit;
	}

	item = oui_filter_find_oui(mors_vif, oui);
	if (!item) {
		if (vie_config->n_oui_filters >= MAX_NUM_OUI_FILTERS) {
			ret = -ENOSPC;
			goto exit;
		}

		if (!on_vendor_ie_match) {
			ret = -EINVAL;
			goto exit;
		}

		item = kzalloc(sizeof(*item), GFP_ATOMIC);
		if (!item) {
			ret = -ENOMEM;
			goto exit;
		}

		memcpy(item->oui, oui, sizeof(item->oui));
		item->on_vendor_ie_match = on_vendor_ie_match;

		list_add_tail(&item->list, &vie_config->oui_filter_list);
		vie_config->n_oui_filters++;
	} else {
		if (item->mgmt_type_mask & mgmt_type_mask)
			ret = -EEXIST;
	}

	item->mgmt_type_mask |= mgmt_type_mask;

exit:
	spin_unlock_bh(&mors_vif->vendor_ie.lock);
	if (!ret && (mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON)) {
		ret =
		    morse_cmd_update_beacon_vendor_ie_oui_filter(morse_vif_to_morse(mors_vif),
								 mors_vif);
		if (ret) {
			/* command failed, remove the OUI from the list before we return */
			spin_lock_bh(&mors_vif->vendor_ie.lock);
			try_remove_oui(mors_vif, item, mgmt_type_mask);
			spin_unlock_bh(&mors_vif->vendor_ie.lock);
		}
	}

	return ret;
}

/**
 * Clear all OUI filters matching the specified mask
 *
 * @mors_vif Interface containing OUI filter
 * @mgmt_type_mask Management frame types to clear
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_clear_oui_filter(struct morse_vif *mors_vif, u16 mgmt_type_mask)
{
	struct morse_vendor_ie *vie_config;
	struct vendor_ie_oui_filter_list_item *item, *tmp;
	bool empty = false;

	if (!mors_vif)
		return -ENODEV;

	spin_lock_bh(&mors_vif->vendor_ie.lock);

	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config) {
		spin_unlock_bh(&mors_vif->vendor_ie.lock);
		return -ENXIO;
	}

	empty = list_empty(&vie_config->oui_filter_list);

	list_for_each_entry_safe(item, tmp, &vie_config->oui_filter_list, list)
		try_remove_oui(mors_vif, item, mgmt_type_mask);

	spin_unlock_bh(&mors_vif->vendor_ie.lock);

	if (!empty && (mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON))
		return morse_cmd_update_beacon_vendor_ie_oui_filter(morse_vif_to_morse(mors_vif),
								    mors_vif);

	return 0;
}

void morse_vendor_ie_init_interface(struct morse_vif *mors_vif, bool in_reconfig)
{
	const u8 *addr = NULL;
	struct morse_persistent_vif_configs *mors_vif_conf;
	struct morse *mors = morse_vif_to_morse(mors_vif);

	spin_lock_init(&mors_vif->vendor_ie.lock);

	mutex_lock(&mors->persistent_vif_config.lock);

	if (is_fullmac_mode()) {
		struct net_device *ndev = mors_vif->ndev;

		if (ndev)
			addr = ndev->dev_addr;
	} else {
		struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_vif);

		addr = vif->addr;
	}

	mors_vif_conf = morse_get_vif_conf_from_addr(mors, addr);
	if (!mors_vif_conf)
		goto exit;

	mors_vif->vendor_ie.vie_config = &mors_vif_conf->vie_config;

	if (in_reconfig)
		goto exit;

	INIT_LIST_HEAD(&mors_vif->vendor_ie.vie_config->ie_list);
	INIT_LIST_HEAD(&mors_vif->vendor_ie.vie_config->oui_filter_list);

exit:
	mutex_unlock(&mors->persistent_vif_config.lock);
}

void morse_vendor_ie_deinit_interface(struct morse_vif *mors_vif, bool is_restarting)
{
	if (!is_restarting) {
		morse_vendor_ie_clear_ie_list(mors_vif, MORSE_VENDOR_IE_TYPE_ALL);
		morse_vendor_ie_clear_oui_filter(mors_vif, MORSE_VENDOR_IE_TYPE_ALL);
	}
	mors_vif->vendor_ie.vie_config = NULL;
}

u16 morse_vendor_ie_get_ies_length(struct morse_vif *mors_vif, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *vendor_ie;
	struct morse_vendor_ie *vie_config;
	u16 vendor_ie_length = 0;

	if (!mors_vif || !mgmt_type_mask)
		return 0;

	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config)
		return 0;

	list_for_each_entry(vendor_ie, &vie_config->ie_list, list) {
		if (vendor_ie->mgmt_type_mask & mgmt_type_mask) {
			struct ieee80211_vendor_ie *ie =
					(struct ieee80211_vendor_ie *)vendor_ie->element;
			if (!ie)
				continue;

			vendor_ie_length += sizeof(ie->element_id);
			vendor_ie_length += sizeof(ie->len);
			vendor_ie_length += ie->len;
		}
	}

	return vendor_ie_length;
}

int morse_vendor_ie_add_ies(struct morse_vif *mors_vif,
			    struct dot11ah_ies_mask *ies_mask, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *item;
	struct ie_element *element;
	struct morse_vendor_ie *vie_config;

	if (!mors_vif || !mgmt_type_mask || !ies_mask)
		return 0;

	vie_config = mors_vif->vendor_ie.vie_config;
	if (!vie_config)
		return 0;

	list_for_each_entry(item, &vie_config->ie_list, list) {
		if (item->mgmt_type_mask & mgmt_type_mask) {
			struct ieee80211_vendor_ie *ie =
					(struct ieee80211_vendor_ie *)item->element;
			if (!ie)
				continue;

			element = morse_dot11_ies_create_ie_element(ies_mask,
								    WLAN_EID_VENDOR_SPECIFIC,
								    ie->len, false, false);

			if (!element)
				return -EINVAL;

			element->ptr = (u8 *)ie->oui;
		}
	}

	return 0;
}

void morse_vendor_ie_process_rx_mgmt(struct ieee80211_vif *vif, const struct sk_buff *skb)
{
	const struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	enum morse_vendor_ie_mgmt_type_flags type;
	const u8 *elements;
	u16 elem_len;

	if (!wdev || !mors_vif->vendor_ie.vie_config ||
	    list_empty(&mors_vif->vendor_ie.vie_config->oui_filter_list))
		return;

	if (ieee80211_is_s1g_beacon(mgmt->frame_control)) {
		type = MORSE_VENDOR_IE_TYPE_BEACON;
		elements = morse_dot11_find_s1g_beacon_ies((struct ieee80211_ext *)mgmt);
	} else if (ieee80211_is_probe_req(mgmt->frame_control)) {
		type = MORSE_VENDOR_IE_TYPE_PROBE_REQ;
		elements = mgmt->u.probe_req.variable;
	} else if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		type = MORSE_VENDOR_IE_TYPE_PROBE_RESP;
		elements = mgmt->u.probe_resp.variable;
	} else if (ieee80211_is_assoc_req(mgmt->frame_control) ||
			ieee80211_is_reassoc_req(mgmt->frame_control)) {
		type = MORSE_VENDOR_IE_TYPE_ASSOC_REQ;
		elements = mgmt->u.assoc_req.variable;
	} else if (ieee80211_is_assoc_resp(mgmt->frame_control) ||
			    ieee80211_is_reassoc_resp(mgmt->frame_control)) {
		struct morse_dot11ah_s1g_assoc_resp *s1g_assoc_resp =
			(struct morse_dot11ah_s1g_assoc_resp *)mgmt;

		type = MORSE_VENDOR_IE_TYPE_ASSOC_RESP;
		elements = s1g_assoc_resp->variable;
	} else {
		return;
	}

	elem_len = skb->len - (elements - (u8 *)skb->data);

	morse_vendor_ie_process_rx_ies(wdev, elements, elem_len, type);
}

int morse_vendor_ie_handle_config_cmd(struct morse_vif *mors_vif,
				      struct morse_cmd_req_vendor_ie_config *cfg)
{
	int ret = -EINVAL;
	const u16 data_size =
		le16_to_cpu(cfg->hdr.len) - sizeof(cfg->opcode) - sizeof(cfg->mgmt_type_mask);
	u16 mgmt_type_mask = le16_to_cpu(cfg->mgmt_type_mask);

	if (!mgmt_type_mask ||
	    (mgmt_type_mask &
	     ~(MORSE_CMD_VENDOR_IE_TYPE_FLAG_BEACON |
	       MORSE_CMD_VENDOR_IE_TYPE_FLAG_PROBE_REQ | MORSE_CMD_VENDOR_IE_TYPE_FLAG_PROBE_RESP |
	       MORSE_CMD_VENDOR_IE_TYPE_FLAG_ASSOC_REQ | MORSE_CMD_VENDOR_IE_TYPE_FLAG_ASSOC_RESP)))
		return -ENOTSUPP;

	switch (le16_to_cpu(cfg->opcode)) {
	case MORSE_CMD_VENDOR_IE_OP_ADD_ELEMENT:
		{
			ret = morse_vendor_ie_add_to_ie_list(mors_vif, mgmt_type_mask,
							     cfg->data, data_size);
			break;
		}
	case MORSE_CMD_VENDOR_IE_OP_CLEAR_ELEMENTS:
		{
			ret = morse_vendor_ie_clear_ie_list(mors_vif, mgmt_type_mask);
			break;
		}
	case MORSE_CMD_VENDOR_IE_OP_ADD_FILTER:
		{
			if (data_size != OUI_SIZE)
				break;

			ret = morse_vendor_ie_add_oui_to_filter(mors_vif, mgmt_type_mask,
				      cfg->data, morse_vendor_send_mgmt_vendor_ie_found_event);

			break;
		}
	case MORSE_CMD_VENDOR_IE_OP_CLEAR_FILTERS:
		{
			ret = morse_vendor_ie_clear_oui_filter(mors_vif, mgmt_type_mask);
			break;
		}
	}

	return ret;
}
