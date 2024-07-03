/*
 * Copyright 2020 Morse Micro
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/crc32.h>
#include <linux/ieee80211.h>
#include "s1g_ieee80211.h"

#include "dot11ah.h"
#include "tim.h"
#include "../morse.h"
#include "../mesh.h"

/* Validity of the cssid entry */
#define MORSE_CSSID_ENTRY_VALIDITY_TIME	(60 * HZ)

spinlock_t cssid_list_lock;
static LIST_HEAD(cssid_list);

/*
 * Static functions used only here
 */
static int __init morse_dot11ah_init(void)
{
	int ret = 0;

	spin_lock_init(&cssid_list_lock);
	pr_info("Morse Micro Dot11ah driver registration. Version %s\n", DOT11AH_VERSION);
	return ret;
}

static void __exit morse_dot11ah_exit(void)
{
	morse_dot11ah_clear_list();
}

/** morse_dot11ah_cssid_has_expired - Checks if the given cssid entry is expired or not.
 * @item: pointer to cssid item
 *
 * Return: True if the cssid entry has expired.
 */
static bool morse_dot11ah_cssid_has_expired(struct morse_dot11ah_cssid_item *item)
{
	u32 age_limit;

	if (item->mesh_beacon)
		age_limit =
			msecs_to_jiffies(MORSE_TU_TO_MS(MESH_CONFIG_NEIGHBOR_ENTRY_VALIDITY_IN_TU));
	else
		age_limit = MORSE_CSSID_ENTRY_VALIDITY_TIME;

	if (time_before(item->last_seen + age_limit, jiffies))
		return true;

	return false;
}

/*
 * Public functions used in  dot11ah module
 */
struct morse_dot11ah_cssid_item *morse_dot11ah_find_cssid(u32 cssid)
{
	struct morse_dot11ah_cssid_item *item, *tmp;

	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		if (item->cssid == cssid) {
			item->last_seen = jiffies;
			return item;
		}

		if (morse_dot11ah_cssid_has_expired(item)) {
			list_del(&item->list);
			kfree(item->ies);
			kfree(item);
		}
	}

	return NULL;
}

/** Use of this function and any returned items must be protected with cssid_list_lock */
struct morse_dot11ah_cssid_item *morse_dot11ah_find_bssid(u8 bssid[ETH_ALEN])
{
	struct morse_dot11ah_cssid_item *item, *tmp;

	if (!bssid)
		return NULL;

	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		if (memcmp(item->bssid, bssid, ETH_ALEN) == 0) {
			item->last_seen = jiffies;
			return item;
		}
	}
	return NULL;
}

u32 morse_dot11ah_store_cssid(struct dot11ah_ies_mask *ies_mask, u16 capab_info, u8 *s1g_ies,
			      int s1g_ies_len, u8 *bssid)
{
	/**
	 * Kernel allocations in this function must be atomic as it occurs
	 * within a spin lock.
	 */
	struct morse_dot11ah_cssid_item *item, *stored;
	u32 cssid;
	int length;
	const u8 *ssid;
	bool mesh_beacon = false;

	if (morse_is_mesh_network(ies_mask)) {
		printk(KERN_ERR " bssid: 0x%08x\n", (u32) bssid);


		/* Use source addr for mesh networks as different mesh STAs beacon with same
		 * MESH ID
		 */
		if (!bssid) {
			dump_stack();
			cssid = 0;
		} else {
			cssid = ~crc32(~0, bssid, ETH_ALEN);
		}
		/* Store mesh id in ssid field of cssid entry for mesh beacons */
		ssid = ies_mask->ies[WLAN_EID_MESH_ID].ptr;
		length = ies_mask->ies[WLAN_EID_MESH_ID].len;
		mesh_beacon = true;
	} else {
		printk(KERN_ERR " ies_mask: 0x%08x\n", (u32) ies_mask);
		printk(KERN_ERR " ptr: 0x%08x\n", (u32) ies_mask->ies[WLAN_EID_SSID].ptr);

		/* In Linux we have to pass ~0 as the seed value, and then invert the outcome */
		cssid = ~crc32(~0, ies_mask->ies[WLAN_EID_SSID].ptr,
			       ies_mask->ies[WLAN_EID_SSID].len);
		ssid = ies_mask->ies[WLAN_EID_SSID].ptr;
		length = ies_mask->ies[WLAN_EID_SSID].len;
	}

	spin_lock_bh(&cssid_list_lock);
	stored = morse_dot11ah_find_cssid(cssid);

	if (stored) {
		if (stored->capab_info != capab_info && capab_info != 0)
			stored->capab_info = capab_info;

		if (stored->ies_len != s1g_ies_len && s1g_ies) {
			kfree(stored->ies);

			stored->ies = kmalloc(s1g_ies_len, GFP_ATOMIC);
			BUG_ON(!stored->ies);
			memcpy(stored->ies, s1g_ies, s1g_ies_len);
			stored->ies_len = s1g_ies_len;
		}

		if (bssid)
			memcpy(stored->bssid, bssid, ETH_ALEN);

		spin_unlock_bh(&cssid_list_lock);
		return cssid;
	}

	item = kmalloc(sizeof(*item), GFP_ATOMIC);
	BUG_ON(!item);

	item->cssid = cssid;
	item->ssid_len = length;
	item->last_seen = jiffies;
	item->capab_info = capab_info;
	item->fc_bss_bw_subfield = MORSE_FC_BSS_BW_INVALID;
	memcpy(item->ssid, ssid, length);
	item->mesh_beacon = mesh_beacon;

	item->ies = kmalloc(s1g_ies_len, GFP_ATOMIC);
	BUG_ON(!item->ies);

	item->ies_len = s1g_ies_len;

	memcpy(item->ies, s1g_ies, s1g_ies_len);

	if (bssid)
		memcpy(item->bssid, bssid, ETH_ALEN);
	else
		memset(item->bssid, 0, ETH_ALEN);

	list_add(&item->list, &cssid_list);

	spin_unlock_bh(&cssid_list_lock);

	return cssid;
}

bool morse_dot11ah_find_s1g_operation_for_ssid(const char *ssid, size_t ssid_len,
					       struct s1g_operation_parameters *params)
{
	struct morse_dot11ah_cssid_item *item = NULL;
	u8 *ies = NULL;
	u32 cssid = ~crc32(~0, ssid, ssid_len);
	bool found = false;

	spin_lock_bh(&cssid_list_lock);
	item = morse_dot11ah_find_cssid(cssid);

	if (item) {
		ies = (u8 *)morse_dot11_find_ie(WLAN_EID_S1G_OPERATION, item->ies, item->ies_len);
		if (ies) {
			u8 prim_chan_num = ies[4];
			u8 chan_loc = IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_LOC(ies[2]);

			params->chan_centre_freq_num = ies[5];
			params->op_bw_mhz =
				IEEE80211AH_S1G_OPERATION_GET_OP_CHAN_BW(ies[2]);
			params->pri_bw_mhz =
				IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_BW(ies[2]);
			params->pri_1mhz_chan_idx =
				morse_dot11ah_prim_1mhz_chan_loc_to_idx(params->op_bw_mhz,
					params->pri_bw_mhz,
					prim_chan_num, params->chan_centre_freq_num,
					chan_loc);

			params->s1g_operating_class = ies[3];

			found = true;
		}
	}

	spin_unlock_bh(&cssid_list_lock);
	return found;
}

/*
 * Exported functions used elsewhere in morse driver
 */

bool morse_mac_find_channel_info_for_bssid(u8 bssid[ETH_ALEN], struct morse_channel_info *info)
{
	bool found = false;
	u8 *op = NULL;
	struct morse_dot11ah_cssid_item *item = NULL;

	spin_lock_bh(&cssid_list_lock);

	item = morse_dot11ah_find_bssid(bssid);

	if (item) {
		op = (u8 *)morse_dot11_find_ie(WLAN_EID_S1G_OPERATION, item->ies, item->ies_len);
		if (op) {
			u8 prim_chan_num = op[4];
			u8 op_chan_num = op[5];
			u8 chan_loc = IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_LOC(op[2]);

			info->op_bw_mhz = IEEE80211AH_S1G_OPERATION_GET_OP_CHAN_BW(op[2]);
			info->pri_bw_mhz = IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_BW(op[2]);
			info->pri_1mhz_chan_idx =
				morse_dot11ah_prim_1mhz_chan_loc_to_idx(info->op_bw_mhz,
					info->pri_bw_mhz, prim_chan_num, op_chan_num, chan_loc);
			info->op_chan_freq_hz =
				KHZ_TO_HZ(morse_dot11ah_channel_to_freq_khz(op_chan_num));
			found = true;
		}
	}

	spin_unlock_bh(&cssid_list_lock);

	return found;
}
EXPORT_SYMBOL(morse_mac_find_channel_info_for_bssid);

int morse_dot11_find_bssid_on_channel(u32 op_chan_freq_hz, u8 bssid[ETH_ALEN])
{
	bool found = false;
	struct morse_dot11ah_cssid_item *item, *tmp;

	spin_lock_bh(&cssid_list_lock);

	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		u8 *op = (u8 *)morse_dot11_find_ie(WLAN_EID_S1G_OPERATION, item->ies,
						   item->ies_len);

		if (op) {
			u8 ap_chan_num = op[5];
			u32 ap_freq =
				morse_dot11ah_channel_to_freq_khz(ap_chan_num);

			if (op_chan_freq_hz == KHZ_TO_HZ(ap_freq)) {
				memcpy(bssid, item->bssid, ETH_ALEN);
				found = true;
				break;
			}
		}
	}

	spin_unlock_bh(&cssid_list_lock);

	return found ? 0 : -ENOENT;
}
EXPORT_SYMBOL(morse_dot11_find_bssid_on_channel);

void morse_dot11ah_clear_list(void)
{
	struct morse_dot11ah_cssid_item *item, *tmp;

	spin_lock_bh(&cssid_list_lock);
	/* Free allocated list */
	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		list_del(&item->list);
		kfree(item->ies);
		kfree(item);
	}
	spin_unlock_bh(&cssid_list_lock);
}
EXPORT_SYMBOL(morse_dot11ah_clear_list);

bool morse_dot11ah_find_s1g_caps_for_bssid(u8 *bssid, struct ieee80211_s1g_cap *s1g_caps)
{
	struct morse_dot11ah_cssid_item *item;
	bool found = false;
	u8 *ie = NULL;

	spin_lock_bh(&cssid_list_lock);

	item = morse_dot11ah_find_bssid(bssid);
	if (item) {
		ie = (u8 *)morse_dot11_find_ie(WLAN_EID_S1G_CAPABILITIES, item->ies, item->ies_len);
		if (ie) {
			found = true;
			memcpy((u8 *)s1g_caps, (ie + 2), *(ie + 1));
		}
	}

	spin_unlock_bh(&cssid_list_lock);
	return found;
}
EXPORT_SYMBOL(morse_dot11ah_find_s1g_caps_for_bssid);

bool morse_dot11ah_find_bss_bw(u8 *bssid, u8 *fc_bss_bw_subfield)
{
	struct morse_dot11ah_cssid_item *bssid_item = NULL;
	bool found = false;

	spin_lock_bh(&cssid_list_lock);
	bssid_item = morse_dot11ah_find_bssid(bssid);

	if (bssid_item) {
		*fc_bss_bw_subfield = bssid_item->fc_bss_bw_subfield;
		found = true;
	}
	spin_unlock_bh(&cssid_list_lock);

	return found;
}
EXPORT_SYMBOL(morse_dot11ah_find_bss_bw);

bool morse_dot11ah_is_mesh_peer_known(const u8 *peer_mac_addr)
{
	struct morse_dot11ah_cssid_item *item, *tmp;
	u32 cssid;
	bool ret = false;

	if (!peer_mac_addr)
		return false;

	cssid = ~crc32(~0, peer_mac_addr, ETH_ALEN);
	spin_lock_bh(&cssid_list_lock);
	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		if (item->cssid == cssid) {
			/* Check if entry has expired */
			if (morse_dot11ah_cssid_has_expired(item)) {
				list_del(&item->list);
				kfree(item->ies);
				kfree(item);
			} else {
				ret = true;
			}
			break;
		}
	}
	spin_unlock_bh(&cssid_list_lock);

	return ret;
}
EXPORT_SYMBOL(morse_dot11ah_is_mesh_peer_known);

bool morse_dot11ah_add_mesh_peer(struct dot11ah_ies_mask *ies_mask, u16 capab_info,
				 const u8 *peer_mac_addr)
{
	if (!peer_mac_addr)
		return false;

	/* Create entry for this new mesh peer */
	morse_dot11ah_store_cssid(ies_mask, capab_info, NULL, 0, (u8 *)peer_mac_addr);

	return true;
}
EXPORT_SYMBOL(morse_dot11ah_add_mesh_peer);

bool morse_dot11ah_del_mesh_peer(const u8 *peer_mac_addr)
{
	struct morse_dot11ah_cssid_item *item, *tmp;
	u32 cssid;

	if (!peer_mac_addr)
		return false;

	/* Use peer mac addr for mesh, as all mesh STAs will use the same MESH ID */
	cssid = ~crc32(~0, peer_mac_addr, ETH_ALEN);
	spin_lock_bh(&cssid_list_lock);
	/* Free matching entry */
	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		if (item->cssid == cssid) {
			list_del(&item->list);
			kfree(item->ies);
			kfree(item);
			spin_unlock_bh(&cssid_list_lock);
			return true;
		}
	}
	spin_unlock_bh(&cssid_list_lock);
	return false;
}
EXPORT_SYMBOL(morse_dot11ah_del_mesh_peer);

/* Calculate primary channel location within operating bandwidth */
int morse_dot11_calc_prim_s1g_chan_loc(int prim_cent_freq, int op_chan_centre_freq, int op_bw_mhz)
{
	int pri_1mhz_location = 0;

	if (prim_cent_freq < op_chan_centre_freq)
		pri_1mhz_location =
			((op_bw_mhz - 1) - (op_chan_centre_freq - prim_cent_freq) / 500) / 2;
	else
		pri_1mhz_location =
			((op_bw_mhz - 1) + (prim_cent_freq - op_chan_centre_freq) / 500) / 2;

	return pri_1mhz_location;
}
EXPORT_SYMBOL(morse_dot11_calc_prim_s1g_chan_loc);

int morse_dot11ah_find_no_of_mesh_neighbors(u16 beacon_int)
{
	struct morse_dot11ah_cssid_item *item, *tmp;
	int mesh_neighbor_count = 0;

	spin_lock_bh(&cssid_list_lock);
	list_for_each_entry_safe(item, tmp, &cssid_list, list) {
		if (morse_dot11ah_cssid_has_expired(item)) {
			list_del(&item->list);
			kfree(item->ies);
			kfree(item);
		} else if (item->mesh_beacon && (item->beacon_int == beacon_int)) {
			mesh_neighbor_count++;
		}
	}
	spin_unlock_bh(&cssid_list_lock);

	return mesh_neighbor_count;
}
EXPORT_SYMBOL(morse_dot11ah_find_no_of_mesh_neighbors);

bool morse_dot11ah_is_page_slicing_enabled_on_bss(u8 *bssid)
{
	struct morse_dot11ah_cssid_item *item;
	bool enabled = false;
	u8 *ie = NULL;
	struct ieee80211_s1g_cap *s1g_caps;

	spin_lock_bh(&cssid_list_lock);

	item = morse_dot11ah_find_bssid(bssid);
	if (item) {
		ie = (u8 *)morse_dot11_find_ie(WLAN_EID_S1G_CAPABILITIES, item->ies, item->ies_len);
		if (ie) {
			s1g_caps = (struct ieee80211_s1g_cap *)(ie + 2);
			enabled = s1g_caps->capab_info[6] & S1G_CAP6_PAGE_SLICING;
		}
	}

	spin_unlock_bh(&cssid_list_lock);
	return enabled;
}
EXPORT_SYMBOL(morse_dot11ah_is_page_slicing_enabled_on_bss);

module_init(morse_dot11ah_init);
module_exit(morse_dot11ah_exit);

MODULE_AUTHOR("Morse Micro, Inc.");
MODULE_DESCRIPTION("S1G support for Morse Micro drivers");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DOT11AH_VERSION);
