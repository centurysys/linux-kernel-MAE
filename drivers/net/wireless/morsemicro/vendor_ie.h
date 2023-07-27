#ifndef _MORSE_VENDOR_IE_H_
#define _MORSE_VENDOR_IE_H_

/*
 * Copyright 2017-2022 Morse Micro
 */

#include "morse.h"
#include "command.h"

enum morse_vendor_ie_mgmt_type_flags {
	MORSE_VENDOR_IE_TYPE_BEACON	= BIT(0),
	MORSE_VENDOR_IE_TYPE_PROBE_REQ  = BIT(1),
	MORSE_VENDOR_IE_TYPE_PROBE_RESP = BIT(2),
	/* ... etc. */

	MORSE_VENDOR_IE_TYPE_ALL	= GENMASK(15, 0)
};

/** Max amount of data in a vendor IE. Limited by the length field being 1 byte */
#define MORSE_MAX_VENDOR_IE_SIZE		(U8_MAX)

/**
 * @brief Vendor information element list item to insert into managment frames
 */
struct vendor_ie_list_item {
	struct list_head list;
	/** Management type bitmask which this vendor IE should be inserted into */
	u16 mgmt_type_mask;
	/** The vendor information element to insert */
	struct __packed {
		struct ieee80211_vendor_ie ie;
		u8 data[0];
	};
};

/**
 * @brief Vendor IE OUI filter list item. The callback will be called if a managment frame with
 *	  a vendor element that matches an OUI in the list is found.
 */
struct vendor_ie_oui_filter_list_item {
	struct list_head list;
	/** Management type bitmask which this item applies to */
	u16 mgmt_type_mask;
	/** OUI to match */
	u8 oui[OUI_SIZE];
	/** Callback function to call when a matching vendor element is found */
	int (*on_vendor_ie_match)(struct ieee80211_vif *vif, struct ieee80211_vendor_ie *vendor_ie);
};

/**
 * @brief Initialise the structures for vendor IE processing in the interface
 *
 * @param mors_if Interface to initialise
 */
void morse_vendor_ie_init_interface(struct morse_vif *mors_if);

/**
 * @brief Uninitialise and free the structures for vendor IE processing in the interface
 *
 * @param mors_if Interface to uninitialise
 */
void morse_vendor_ie_deinit_interface(struct morse_vif *mors_if);


/**
 * @brief Get the total length of the currently configured vendor IEs.
 *
 * @param mors_if Interface to operate on
 * @param mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frames to calculate the lengths for
 * @return length in bytes, or 0 if none configured
 */
u16 morse_vendor_ie_get_ies_length(struct morse_vif *mors_if, u16 mgmt_type_mask);

/**
 * @brief Append configured vendor IEs onto an skb
 *
 * @param mors_if Interface with configured vendor IEs
 * @param pkt SKB in which to insert vendor IEs (assumes is large enough)
 * @param mgmt_type_mask Bitmask of @ref morse_vendor_ie_mgmt_type_flags to specify which
 *			 management frames to insert
 * @return 0 on success, else error code
 */
int morse_vendor_ie_add_ies(struct morse_vif *mors_if, struct sk_buff *pkt, u16 mgmt_type_mask);

/**
 * @brief Process a received S1G beacon and send a vendor event for each vendor element received
 *	  with an OUI that matches one in the OUI filter
 *
 * @param vif virtual interface the beacon was received on
 * @param skb the beacon SKB
 */
void morse_vendor_ie_process_rx_s1g_beacon(struct ieee80211_vif *vif, struct sk_buff *skb);

/**
 * @brief Handle a vendor IE config command
 *
 * @note Only supports beacons for now
 *
 * @param mors_if Interface command was received for
 * @param cfg received config information
 * @return 0 on success, else error code
 */
int morse_vendor_ie_handle_config_cmd(struct morse_vif *mors_if,
					struct morse_cmd_vendor_ie_config *cfg);

#endif  /* !_MORSE_VENDOR_IE_H_ */
