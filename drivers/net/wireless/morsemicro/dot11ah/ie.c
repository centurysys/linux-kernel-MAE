/*
 * Copyright 2020 Morse Micro
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/crc32.h>
#include <linux/ieee80211.h>

#include "dot11ah.h"
#include "tim.h"
#include "debug.h"
#include "../morse.h"

static void free_ies_list(struct ie_element *list_head)
{
	struct ie_element *next, *cur;

	for (cur = list_head; cur != NULL; cur = next) {
		next = cur->next;
		kfree(cur);
	}
}

struct dot11ah_ies_mask *morse_dot11ah_ies_mask_alloc(void)
{
	struct dot11ah_ies_mask *ies_mask = NULL;

	ies_mask = kzalloc(sizeof(*ies_mask), GFP_KERNEL);

	return ies_mask;
}
EXPORT_SYMBOL(morse_dot11ah_ies_mask_alloc);

void morse_dot11ah_ies_mask_free(struct dot11ah_ies_mask *ies_mask)
{
	int pos;

	if (ies_mask == NULL)
		return;

	for_each_set_bit(pos, ies_mask->more_than_one_ie, DOT11AH_MAX_EID)
		free_ies_list(ies_mask->ies[pos].next);

	kfree(ies_mask);
}
EXPORT_SYMBOL(morse_dot11ah_ies_mask_free);

void morse_dot11ah_ies_mask_clear(struct dot11ah_ies_mask *ies_mask)
{
	int pos;

	if (ies_mask == NULL)
		return;

	for_each_set_bit(pos, ies_mask->more_than_one_ie, DOT11AH_MAX_EID) {
		free_ies_list(ies_mask->ies[pos].next);
	}

	/* clear the ies_mask */
	memset(ies_mask, 0, sizeof(*ies_mask));
}
EXPORT_SYMBOL(morse_dot11ah_ies_mask_clear);

int morse_dot11ah_parse_ies(const u8 *start, size_t len, struct dot11ah_ies_mask *ies_mask)
{
	size_t left = len;
	const u8 *pos = start;

	if (!start || ies_mask == NULL) {
		dot11ah_warn("Null ref when parsing IEs\n");
		return -EINVAL;
	}

	while (left >= 2) {
		u8 id;
		u8 elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if ((id == WLAN_EID_EXTENSION) && (left > 0)) {
			u8 id_extension = *pos;

			/* If present, the FILS Session element is the last unencrypted element in
			 * the frame. The IDs and lengths of the following encrypted elements cannot
			 * be determined, so this element and the remaining data is treated as a
			 * single block of data.
			 */
			if (id_extension == WLAN_EID_EXT_FILS_SESSION) {
				dot11ah_debug("Have FILS session element\n");
				ies_mask->fils_data = (u8 *)(pos - 2);
				ies_mask->fils_data_len = left + 2;
				left = 0;
				break;
			}
		}

		if (elen > left) {
			dot11ah_warn("Element length larger than remaining bytes. have %u expecting %zu\n",
				elen, left);
			return -EINVAL;
		}

		if (ies_mask->ies[id].ptr == NULL) {
			ies_mask->ies[id].ptr = pos;
			ies_mask->ies[id].len = elen;

		} else {
			struct ie_element *new;
			struct ie_element *cur;

			for (cur = &ies_mask->ies[id]; cur->next != NULL; cur = cur->next)
				continue; /* walk to the end of the list */

			new = kzalloc(sizeof(*new), GFP_KERNEL);
			if (new == NULL)
				return -ENOMEM;

			new->ptr = pos;
			new->len = elen;

			/* add new element */
			cur->next = new;
			set_bit(id, ies_mask->more_than_one_ie);
		}

		left -= elen;
		pos += elen;
	}

	if (left != 0) {
		dot11ah_warn("Leftover bytes after parsing %zu\n", left);
		return -EINVAL;
	}

	return 0;
}

const u8 *morse_dot11_find_ie(u8 eid, const u8 *ies, int length)
{
	return cfg80211_find_ie(eid, ies, length);
}

u8 *morse_dot11_insert_ie(u8 *dst, const u8 *src, u8 eid, u8 len)
{
	*dst++ = eid;
	*dst++ = len;

	if ((src != NULL) && (len > 0)) {
		/* Zero-length IE does not need a memcpy, only EID and LEN */
		memcpy(dst, src, len);
		dst += len;
	}
	return dst;
}

u8 *morse_dot11_insert_ie_no_header(u8 *dst, const u8 *src, u8 len)
{
	memcpy(dst, src, len);
	dst += len;
	return dst;
}

u8 *morse_dot11_insert_ie_from_ies_mask(u8 *pos, const struct dot11ah_ies_mask *ies_mask, int eid)
{
	struct ie_element *cur;

	if (ies_mask->ies[eid].ptr == NULL)
		return pos;

	pos = morse_dot11_insert_ie(pos, ies_mask->ies[eid].ptr, eid, ies_mask->ies[eid].len);

	/* insert any extras */
	for (cur = ies_mask->ies[eid].next; cur != NULL; cur = cur->next)
		pos = morse_dot11_insert_ie(pos, cur->ptr, eid, cur->len);

	return pos;
}

void morse_dot11ah_mask_ies(struct dot11ah_ies_mask *ies_mask, bool mask_ext_cap, bool is_beacon)
{
	/* Masks all the information elements are not needed when sending a packet */
	ies_mask->ies[WLAN_EID_DS_PARAMS].ptr = NULL;
	ies_mask->ies[WLAN_EID_ERP_INFO].ptr = NULL;
	ies_mask->ies[WLAN_EID_EXT_SUPP_RATES].ptr = NULL;
	ies_mask->ies[WLAN_EID_HT_CAPABILITY].ptr = NULL;
	ies_mask->ies[WLAN_EID_HT_OPERATION].ptr = NULL;
	ies_mask->ies[WLAN_EID_SUPP_RATES].ptr = NULL;
	ies_mask->ies[WLAN_EID_VHT_CAPABILITY].ptr = NULL;
	ies_mask->ies[WLAN_EID_VHT_OPERATION].ptr = NULL;
#if KERNEL_VERSION(5, 15, 0) <= MAC80211_VERSION_CODE
	ies_mask->ies[WLAN_EID_TX_POWER_ENVELOPE].ptr = NULL;
#else
	ies_mask->ies[WLAN_EID_VHT_TX_POWER_ENVELOPE].ptr = NULL;
#endif
	/* S1G parameters are masked as they will be added explicitly */
	ies_mask->ies[WLAN_EID_S1G_SHORT_BCN_INTERVAL].ptr = NULL;
	ies_mask->ies[WLAN_EID_S1G_CAPABILITIES].ptr = NULL;
	ies_mask->ies[WLAN_EID_S1G_OPERATION].ptr = NULL;
	ies_mask->ies[WLAN_EID_S1G_BCN_COMPAT].ptr = NULL;
	ies_mask->ies[WLAN_EID_S1G_CAC].ptr = NULL;
	ies_mask->ies[WLAN_EID_TIM].ptr = NULL;

	if (mask_ext_cap)
		ies_mask->ies[WLAN_EID_EXT_CAPABILITY].ptr = NULL;

	if (is_beacon) {
		/* SW-5966:DTIM current increases at DTIM period 1. This is beacuse after commit 0e1b241 the beacon size has increased
		 * Remove the extra elements.
		 */
		ies_mask->ies[WLAN_EID_RSN].ptr = NULL;
		ies_mask->ies[WLAN_EID_RSNX].ptr = NULL;
		ies_mask->ies[WLAN_EID_SUPPORTED_REGULATORY_CLASSES].ptr = NULL;
	}
}
