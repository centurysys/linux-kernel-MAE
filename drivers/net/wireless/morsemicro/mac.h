#ifndef _MORSE_MAC_H_
#define _MORSE_MAC_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <linux/skbuff.h>
#include "morse.h"
#include "command.h"
#include "skb_header.h"

/* The maximum number of frames to send after a DTIM to firmware */
#define MORSE_MAX_MC_FRAMES_AFTER_DTIM (10)

extern struct ieee80211_supported_band mors_band_5ghz;

void morse_mac_send_buffered_bc(struct ieee80211_vif *vif);
struct morse *morse_mac_create(size_t priv_size, struct device *dev);
void morse_mac_destroy(struct morse *mors);
int morse_mac_skb_recv(struct morse *mors, struct sk_buff *skb,
		       struct morse_skb_rx_status *hdr_rx_status);
int morse_mac_event_recv(struct morse *mors, struct sk_buff *skb);
int morse_mac_register(struct morse *mors);
void morse_mac_unregister(struct morse *mors);
void morse_mac_rx_status(struct morse *mors,
			 struct morse_skb_rx_status *hdr_rx_status,
			 struct ieee80211_rx_status *rx_status, struct sk_buff *skb);
void morse_mac_skb_free(struct morse *mors, struct sk_buff *skb);

void morse_mac_update_custom_s1g_capab(struct morse_vif *mors_vif,
				       struct dot11ah_ies_mask *ies_mask,
				       enum nl80211_iftype vif_type);
int morse_mac_pkt_to_s1g(struct morse *mors, struct sk_buff **skb, int *tx_bw_mhz);

int morse_mac_watchdog_create(struct morse *mors);
void morse_mac_mcs0_10_stats_dump(struct morse *mors, struct seq_file *file);
void morse_mac_fill_tx_info(struct morse *mors, struct morse_skb_tx_info *tx_info,
				   struct sk_buff *skb, struct ieee80211_vif *vif,
				   int tx_bw_mhz, struct ieee80211_sta *sta);

bool is_thin_lmac_mode(void);
bool is_virtual_sta_test_mode(void);

/* Return a pointer to vif from vif id of tx status */
struct ieee80211_vif *morse_get_vif_from_tx_status(struct morse *mors,
						   struct morse_skb_tx_status *hdr_tx_status);

/* Return a pointer to vif from vif id */
struct ieee80211_vif *morse_get_vif_from_vif_id(struct morse *mors, int vif_id);
/* same as above but does not take the vif array lock */
struct ieee80211_vif *__morse_get_vif_from_vif_id(struct morse *mors, int vif_id);

/**
 * Return a pointer to the 1st valid VIF.
 * NOTE: Please don't use this func. This will be deprecated soon.
 */
struct ieee80211_vif *morse_get_vif(struct morse *mors);

/* Return a pointer to vif from vif id of rx status */
struct ieee80211_vif *morse_get_vif_from_rx_status(struct morse *mors,
						   struct morse_skb_rx_status *hdr_rx_status);

/* Return a pointer to the AP vif if present otherwise NULL */
struct ieee80211_vif *morse_get_ap_vif(struct morse *mors);

/* Return a pointer to the STA vif if present otherwise NULL */
struct ieee80211_vif *morse_get_sta_vif(struct morse *mors);

/* Return a pointer to the IBSS vif if present otherwise NULL */
struct ieee80211_vif *morse_get_ibss_vif(struct morse *mors);

/* Return iface name for the valid vif */
char *morse_vif_name(struct ieee80211_vif *vif);
/**
 * @brief Determine if the iface is AP type (AP or Ad-hoc or Mesh Point).
 *
 * @param vif Interface pointer to check
 *
 * @returns TRUE if iface is AP type
 */
static inline bool morse_mac_is_iface_ap_type(struct ieee80211_vif *vif)
{
	return (vif &&
		(vif->type == NL80211_IFTYPE_AP ||
		 vif->type == NL80211_IFTYPE_ADHOC ||
		 ieee80211_vif_is_mesh(vif)));
}

/**
 * @brief Determine if the iface is Infrastructure BSS type (AP or STA).
 *
 * @param vif Interface pointer to check
 *
 * @returns TRUE if iface is Infrastructure BSS type
 */
static inline bool morse_mac_is_iface_infra_bss_type(struct ieee80211_vif *vif)
{
	return (vif &&
		(vif->type == NL80211_IFTYPE_AP ||
		 vif->type == NL80211_IFTYPE_STATION));
}

/**
 * @brief Determine if the iface is valid type.
 *
 * @param vif Interface pointer to check
 *
 * @returns TRUE if iface is valid type
 */
static inline bool morse_mac_is_iface_type_supported(struct ieee80211_vif *vif)
{
	/* Station, AP, Adhoc or Mesh Point */
	return ((vif->type == NL80211_IFTYPE_STATION) || morse_mac_is_iface_ap_type(vif));
}

bool morse_mac_is_subband_enable(void);
int morse_mac_get_max_rate_tries(void);
int morse_mac_get_max_rate(void);

int morse_mac_get_watchdog_interval_secs(void);

int morse_mac_send_vendor_wake_action_frame(struct morse *mors, const u8 *dest_addr,
					    const u8 *payload, int payload_len);

int morse_mac_traffic_control(struct morse *mors, int interface_id,
			      bool pause_data_traffic, int sources);

/**
 * Function for filling the Tx meta info (rate info) for driver
 * generated management frames.
 */
void morse_fill_tx_info(struct morse *mors,
			struct morse_skb_tx_info *tx_info,
			struct sk_buff *skb, struct morse_vif *mors_if, int tx_bw_mhz);

/* Process ECSA IE and store the channel info. Also starts chan switch timer in sta mode */
void morse_mac_process_ecsa_ie(struct morse *mors, struct ieee80211_vif *vif, struct sk_buff *skb);

/* Process tx completion of skb used mainly for beacon change sequence */
void morse_mac_process_bcn_change_seq_tx_finish(struct morse *mors, struct sk_buff *skb);

/* Process tx completion of skb used mainly for ECSA */
void morse_mac_ecsa_beacon_tx_done(struct morse *mors, struct sk_buff *skb);

s32 morse_mac_set_txpower(struct morse *mors, s32 power);

/**
 * morse_mac_get_ie_pos() - Parse 802.11n and S1G skb data header and tail positions
 * @skb: The SKB to get positions from
 * @ies_len: Total length of the skb information elements.
 * @header_length: Total length skb 802.11 header.
 * @is_s1g_pkt: Indicates if the skb data is an s1g packet
 *
 * Return: the start position of the information elements if management
 * frame / s1g beacon, else NULL
 */
u8 *morse_mac_get_ie_pos(struct sk_buff *skb, int *ies_len, int *header_length, bool is_s1g_pkt);

/**
 * morse_mac_tx_mgmt_frame - Utility func to transmit driver generated management frames
 *
 * @vif: pointer to the virtual interface
 * @skb: pointer to the management packet buffer
 *
 * Return: 0 on success, else relevant error
 */
int morse_mac_tx_mgmt_frame(struct ieee80211_vif *vif, struct sk_buff *skb);

u64 morse_mac_generate_timestamp_for_frame(struct morse_vif *mors_vif);
#endif /* !_MORSE_MAC_H_ */
