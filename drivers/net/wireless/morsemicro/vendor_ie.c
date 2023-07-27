/*
 * Copyright 2022 Morse Micro
 *
 */
#include <linux/ieee80211.h>
#include <net/mac80211.h>

#include "vendor_ie.h"
#include "morse.h"
#include "vendor.h"
#include "mac.h"
#include "dot11ah/s1g_ieee80211.h"

/**
 * Limit the max size of appended vendor elements. This is required to avoid the beacon getting
 * fragmented at MCS0 1MHz primary bandwith, which is not permitted by the 802.11 protocol.
 * Default size is set to support 2 max size Vendor IEs. 2 x (255 + 2) = 514.
 * (note: +2 for element ID and length)
 */
static uint max_total_vendor_ie_bytes = 2 * (U8_MAX + 2);
module_param(max_total_vendor_ie_bytes, uint, 0644);
MODULE_PARM_DESC(max_total_vendor_ie_bytes, "Max total bytes for runtime vendor IEs");


/**
 * @brief Add a vendor IE to the vendor IE list, to be inserted in specified management frames
 *
 * @param mors_if Interface to insert the IEs on
 * @param mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frames to insert the vendor element in
 * @param data The vendor element data (starting from OUI)
 * @param data_len Length of data
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_add_to_ie_list(struct morse_vif *mors_if, u16 mgmt_type_mask,
					u8 *data, u16 data_len)
{
	struct vendor_ie_list_item *item;
	const u8 full_ie_length = data_len + sizeof(item->ie.element_id) + sizeof(item->ie.len);

	/* Make sure we are within bounds. Vendor IEs must have at least an OUI & OUI type. */
	if (data_len <= sizeof(item->ie.oui) || data_len > MORSE_MAX_VENDOR_IE_SIZE)
		return -EINVAL;

	if (!mors_if)
		return -ENODEV;

	if ((morse_vendor_ie_get_ies_length(mors_if, mgmt_type_mask) + full_ie_length) >
			max_total_vendor_ie_bytes)
		return -ENOSPC;

	item = kzalloc(sizeof(*item) + data_len, GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->mgmt_type_mask = mgmt_type_mask;
	item->ie.element_id = WLAN_EID_VENDOR_SPECIFIC;
	item->ie.len = data_len;
	memcpy(item->ie.oui, data, data_len);

	spin_lock_bh(&mors_if->vendor_ie.lock);
	list_add_tail(&item->list, &mors_if->vendor_ie.ie_list);
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	return 0;
}

/**
 * @brief Clear the vendor IE list for particular management frame types
 *
 * @param mors_if Interface to operate on
 * @param mgmt_type_mask Bitmask of management frame types to clear, or MORSE_VENDOR_IE_TYPE_ALL
 *			 to clear all
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_clear_ie_list(struct morse_vif *mors_if, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *vendor_ie, *tmp;

	if (!mors_if)
		return 0;

	spin_lock_bh(&mors_if->vendor_ie.lock);
	list_for_each_entry_safe(vendor_ie, tmp, &mors_if->vendor_ie.ie_list, list) {
		if (vendor_ie->mgmt_type_mask & mgmt_type_mask) {
			list_del(&vendor_ie->list);
			kfree(vendor_ie);
		}
	}
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	return 0;
}

/**
 * @brief Process received information elements. Will search for vendor IEs with OUIs that match
 * those in the virtual interface's OUI filter, and will call the call back for each match.
 *
 * @param vif Virtual interface IEs were received on
 * @param ies Information elements of the frame to process
 * @param length total length of ies
 * @param mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify the management
 *			 frame type of the received frame
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_process_rx_ies(struct ieee80211_vif *vif, u8 *ies, u16 length,
		u16 mgmt_type_mask)
{
	int ret = 0;
	struct morse_vif *mors_if = ieee80211_vif_to_morse_vif(vif);
	struct ieee80211_vendor_ie *vie = (struct ieee80211_vendor_ie *) ies;
	const u8 *pos = ies;
	const u8 *const end = ies + length;
	struct vendor_ie_oui_filter_list_item *item;
	const u8 min_vendor_ie_length = sizeof(*vie) - sizeof(vie->element_id) - sizeof(vie->len);

	while ((pos < end) && (ret == 0)) {
		pos = cfg80211_find_ie(WLAN_EID_VENDOR_SPECIFIC, pos, length);
		if (pos == NULL)
			break;

		vie = (struct ieee80211_vendor_ie *) pos;

		if (vie->len >= min_vendor_ie_length) {
			spin_lock_bh(&mors_if->vendor_ie.lock);
			list_for_each_entry(item, &mors_if->vendor_ie.oui_filter_list, list) {
				if ((memcmp(item->oui, vie->oui, sizeof(vie->oui)) == 0) &&
						(item->mgmt_type_mask & mgmt_type_mask)) {
					ret = item->on_vendor_ie_match(vif, vie);
					if (ret)
						break;
				}
			}
			spin_unlock_bh(&mors_if->vendor_ie.lock);
		}

		pos = ((u8 *) vie) + sizeof(vie->element_id) + sizeof(vie->len) + vie->len;
	}
	return ret;
}

/**
 * @brief Helper function to get a pointer to the information elements on a received S1G beacon
 *
 * @param bcn S1G Beacon frame
 * @return pointer to start of information elements
 */
static inline u8 *get_elements_from_s1g_beacon(struct ieee80211_ext *bcn)
{
	return (ieee80211_is_s1g_short_beacon(bcn->frame_control) ?
			bcn->u.s1g_short_beacon.variable :
			bcn->u.s1g_beacon.variable);
}

/**
 * @brief Find a previously configured OUI in the OUI filter
 *
 * @param mors_if interface containing the OUI filter
 * @param oui OUI to find
 * @return Pointer to the list item, or NULL if no match
 */
static struct vendor_ie_oui_filter_list_item *oui_filter_find_oui(struct morse_vif *mors_if,
		u8 *oui)
{
	struct vendor_ie_oui_filter_list_item *item;

	list_for_each_entry(item, &mors_if->vendor_ie.oui_filter_list, list) {
		if (memcmp(item->oui, oui, sizeof(item->oui)) == 0)
			return item;
	}

	return NULL;
}

/**
 * @brief Clear the specified mask bits on the OUI filter list item, and remove it if the mask is 0.
 *
 * @param mors_if interface to operate on
 * @param item Filter list item to try to remove
 * @param mgmt_type_mask Bitmask of flags to clear
 */
static void try_remove_oui(struct morse_vif *mors_if,
		struct vendor_ie_oui_filter_list_item *item, u16 mgmt_type_mask)
{
	if (item->mgmt_type_mask & mgmt_type_mask)
		item->mgmt_type_mask &= ~mgmt_type_mask;

	if (item->mgmt_type_mask == 0) {
		list_del(&item->list);
		kfree(item);

		BUG_ON(mors_if->vendor_ie.n_oui_filters == 0);
		mors_if->vendor_ie.n_oui_filters--;
	}
}

/**
 * @brief Add an OUI to the OUI filter. If the OUI is already in the list, this function will
 * instead update the managment frame mask.
 *
 * @param mors_if Interface containing OUI filter
 * @param mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frame types this filter applies to
 * @param oui OUI to match
 * @param on_vendor_ie_match Callback function to call when a vendor IE matching the OUI is found.
 *			     Is only applied for new OUIs (ie. OUIs not already in the list)
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_add_oui_to_filter(struct morse_vif *mors_if, u16 mgmt_type_mask,
	u8 *oui,
	int (*on_vendor_ie_match)(struct ieee80211_vif *, struct ieee80211_vendor_ie *))
{
	int ret = 0;
	struct vendor_ie_oui_filter_list_item *item;

	if (!mors_if)
		return -ENODEV;

	spin_lock_bh(&mors_if->vendor_ie.lock);
	item = oui_filter_find_oui(mors_if, oui);
	if (!item) {
		if (mors_if->vendor_ie.n_oui_filters >= MAX_NUM_OUI_FILTERS) {
			ret = -ENOSPC;
			goto exit;
		}

		if (on_vendor_ie_match == NULL) {
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

		list_add_tail(&item->list, &mors_if->vendor_ie.oui_filter_list);
		mors_if->vendor_ie.n_oui_filters++;
	} else {
		if (item->mgmt_type_mask & mgmt_type_mask)
			ret = -EEXIST;
	}

	item->mgmt_type_mask |= mgmt_type_mask;

exit:
	spin_unlock_bh(&mors_if->vendor_ie.lock);
	if (!ret && (mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON)) {
		ret = morse_cmd_update_beacon_vendor_ie_oui_filter(morse_vif_to_morse(mors_if), mors_if);
		if (ret) {
			/* command failed, remove the OUI from the list before we return */
			spin_lock_bh(&mors_if->vendor_ie.lock);
			try_remove_oui(mors_if, item, mgmt_type_mask);
			spin_unlock_bh(&mors_if->vendor_ie.lock);
		}
	}

	return ret;
}

/**
 * @brief Clear all OUI filters matching the specified mask
 *
 * @param mors_if Interface containing OUI filter
 * @param mgmt_type_mask Management frame types to clear
 * @return 0 on success, else error code
 */
static int morse_vendor_ie_clear_oui_filter(struct morse_vif *mors_if, u16 mgmt_type_mask)
{
	struct vendor_ie_oui_filter_list_item *item, *tmp;
	bool empty = false;

	if (!mors_if)
		return -ENODEV;

	spin_lock_bh(&mors_if->vendor_ie.lock);
	empty = list_empty(&mors_if->vendor_ie.oui_filter_list);

	list_for_each_entry_safe(item, tmp, &mors_if->vendor_ie.oui_filter_list, list)
		try_remove_oui(mors_if, item, mgmt_type_mask);
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	if (!empty && (mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON))
		return morse_cmd_update_beacon_vendor_ie_oui_filter(
				morse_vif_to_morse(mors_if), mors_if);

	return 0;
}

void morse_vendor_ie_init_interface(struct morse_vif *mors_if)
{
	INIT_LIST_HEAD(&mors_if->vendor_ie.ie_list);
	INIT_LIST_HEAD(&mors_if->vendor_ie.oui_filter_list);
	spin_lock_init(&mors_if->vendor_ie.lock);
}

void morse_vendor_ie_deinit_interface(struct morse_vif *mors_if)
{
	morse_vendor_ie_clear_ie_list(mors_if, MORSE_VENDOR_IE_TYPE_ALL);
	morse_vendor_ie_clear_oui_filter(mors_if, MORSE_VENDOR_IE_TYPE_ALL);
}

u16 morse_vendor_ie_get_ies_length(struct morse_vif *mors_if, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *vendor_ie;
	u16 vendor_ie_length = 0;

	if (!mors_if)
		return 0;

	spin_lock_bh(&mors_if->vendor_ie.lock);
	list_for_each_entry(vendor_ie, &mors_if->vendor_ie.ie_list, list) {
		if (vendor_ie->mgmt_type_mask & mgmt_type_mask) {
			vendor_ie_length += sizeof(vendor_ie->ie.element_id);
			vendor_ie_length += sizeof(vendor_ie->ie.len);
			vendor_ie_length += vendor_ie->ie.len;
		}
	}
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	return vendor_ie_length;
}

int morse_vendor_ie_add_ies(struct morse_vif *mors_if, struct sk_buff *pkt, u16 mgmt_type_mask)
{
	struct vendor_ie_list_item *item;
	u8 *dst;

	if (!mors_if)
		return 0;

	spin_lock_bh(&mors_if->vendor_ie.lock);
	list_for_each_entry(item, &mors_if->vendor_ie.ie_list, list) {
		if (item->mgmt_type_mask & mgmt_type_mask) {
			u16 ie_length = sizeof(item->ie.element_id) + sizeof(item->ie.len)
					+ item->ie.len;

			dst = skb_put(pkt, ie_length);
			memcpy(dst, &item->ie, ie_length);
		}
	}
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	return 0;
}

void morse_vendor_ie_process_rx_s1g_beacon(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_ext *bcn = (struct ieee80211_ext *) skb->data;
	struct morse_vif *mors_if = ieee80211_vif_to_morse_vif(vif);
	u8 *elements;
	u16 elem_len;

	if (list_empty(&mors_if->vendor_ie.oui_filter_list))
		return;

	elements = get_elements_from_s1g_beacon(bcn);
	elem_len = skb->len - (elements - (u8 *)skb->data);

	morse_vendor_ie_process_rx_ies(vif, elements, elem_len, MORSE_VENDOR_IE_TYPE_BEACON);
}

int morse_vendor_ie_handle_config_cmd(struct morse_vif *mors_if,
					struct morse_cmd_vendor_ie_config *cfg)
{
	int ret = -EINVAL;
	const u16 data_size = (cfg->hdr.len + sizeof(cfg->hdr)) -
				(sizeof(*cfg) - sizeof(cfg->data));

	/* Only support inserting / receiving vendor IEs in beacons for now */
	if (!cfg->mgmt_type_mask || (cfg->mgmt_type_mask & ~(MORSE_VENDOR_IE_TYPE_BEACON)))
		return -ENOTSUPP;

	switch (cfg->opcode) {
	case MORSE_VENDOR_IE_OP_ADD_ELEMENT:
	{
		ret = morse_vendor_ie_add_to_ie_list(mors_if, cfg->mgmt_type_mask,
							cfg->data, data_size);
		break;
	}
	case MORSE_VENDOR_IE_OP_CLEAR_ELEMENTS:
	{
		ret = morse_vendor_ie_clear_ie_list(mors_if, cfg->mgmt_type_mask);
		break;
	}
	case MORSE_VENDOR_IE_OP_ADD_FILTER:
	{
		if (data_size != OUI_SIZE)
			break;

		ret = morse_vendor_ie_add_oui_to_filter(mors_if, cfg->mgmt_type_mask, cfg->data,
				morse_vendor_send_vendor_ie_found_event);

		break;
	}
	case MORSE_VENDOR_IE_OP_CLEAR_FILTERS:
	{
		ret = morse_vendor_ie_clear_oui_filter(mors_if, cfg->mgmt_type_mask);
		break;
	}
	}

	return ret;
}
