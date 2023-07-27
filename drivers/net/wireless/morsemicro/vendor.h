#ifndef _MORSE_VENDOR_H_
#define _MORSE_VENDOR_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <net/mac80211.h>

/** MORSE OUI as u32 */
#define MORSE_OUI	0x0CBF74

/** MORSE OUI as const array */
static const u8 morse_oui[] = {0x0C, 0xBF, 0x74};

/** Operational bits in mm vendor ie */
#define MORSE_VENDOR_IE_OPS0_DTIM_CTS_TO_SELF   BIT(0)
#define MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU       BIT(1)

/** Oui type of the caps & ops mm vendor ie */
#define MORSE_VENDOR_IE_CAPS_OPS_OUI_TYPE    (0)

/** Morse vendor specific frame sub categories */
#define MORSE_VENDOR_SPECIFIC_FRAME_SUBCAT_WAKE (0x01)

enum morse_vendor_cmds {
	MORSE_VENDOR_CMD_TO_MORSE = 0
};

enum morse_vendor_events {
	MORSE_VENDOR_EVENT_VENDOR_IE_FOUND = 0
};

enum morse_vendor_attributes {
	MORSE_VENDOR_ATTR_DATA = 0
};

/** Morse vendor capability & operations ie */
struct dot11_morse_vendor_caps_ops_ie {
	u8 oui[3];
	u8 oui_type;
	struct {
		u8 major;
		u8 minor;
		u8 patch;
		u8 reserved;
	} __packed sw_ver;
	u32 hw_ver;
	u8 cap0;
	u8 ops0;
} __packed;

/**
 * @brief Get the IE length of the vendor IE for a given OUI type.
 *
 * @param pkt The packet to potentially insert into.
 * @param oui_type The OUI Type.
 * @return int Non-zero if IE should be inserted into pkt.
 */
int morse_vendor_get_ie_len_for_pkt(struct sk_buff *pkt, int oui_type);

/**
 * @brief Inserts a morse vendor capability and operation vendor ie
 *        into a given packet.
 *
 * @param mors The mors object which will dictate how the ie is filled.
 * @param skb The packet to insert the data into.
 * @return struct sk_buff* A pointer to the modified skb.
 */
struct sk_buff *morse_vendor_insert_caps_ops_ie(struct morse *mors,
	struct sk_buff *skb);

/**
 * @brief Receive and process our vendor caps ops IE on reception
 *        of a management frame.
 *
 * @param mors The mors object that contains pointers to structures
 *             that are influenced by IE.
 * @param mgmt A pointer to the mgmt frame.
 * @param ies_mask The ies_mask which points to ies in the frame.
 */
void morse_vendor_rx_caps_ops_ie(struct morse *mors,
	const struct ieee80211_mgmt *mgmt, struct dot11ah_ies_mask *ies_mask);

/**
 * @brief Reset any transient info filled via a vendor IE on a STA object.
 *        Nominally called on disassociation.
 *
 * @param vif  The interface this reset is occurring for.
 * @param mors_sta A pointer to the sta object.
 */
void morse_vendor_reset_sta_transient_info(struct ieee80211_vif *vif,
	struct morse_sta *mors_sta);

/**
 * @brief Send a vendor_ie_found netlink event
 *
 * @param vif Interface to send the event on
 * @param vie Vendor IE which was found
 * @return int 0 on success else error code
 */
int morse_vendor_send_vendor_ie_found_event(struct ieee80211_vif *vif,
	struct ieee80211_vendor_ie *vie);

void morse_set_vendor_commands_and_events(struct wiphy *wiphy);

#endif  /* !_MORSE_VENDOR_H_ */
