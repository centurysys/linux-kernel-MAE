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

void morse_mac_send_buffered_bc(struct morse *mors);
struct morse *morse_mac_create(size_t priv_size, struct device *dev);
void morse_mac_destroy(struct morse *mors);
int  morse_mac_skb_recv(struct morse *mors, struct sk_buff *skb, u8 channel,
			struct morse_skb_rx_status *hdr_rx_status);
int morse_mac_event_recv(struct morse *mors, struct sk_buff *skb);
int morse_mac_register(struct morse *mors);
void morse_mac_unregister(struct morse *mors);
int morse_mac_pkt_to_s1g(struct morse *mors, struct sk_buff *skb,
	int *tx_bw_mhz);

int morse_mac_watchdog_create(struct morse *mors);
void morse_mac_mcs0_10_stats_dump(struct morse *mors, struct seq_file *file);

bool is_thin_lmac_mode(void);
bool is_virtual_sta_test_mode(void);
bool is_multi_interface_mode(void);

/* Return a pointer to the 0th vif index */
struct ieee80211_vif *morse_get_vif(struct morse *mors);

/* Return a pointer to the AP vif if present otherwise NULL */
struct ieee80211_vif *morse_get_ap_vif(struct morse *mors);

/* Return a pointer to the STA vif if present otherwise NULL */
struct ieee80211_vif *morse_get_sta_vif(struct morse *mors);

bool morse_mac_is_subband_enable(void);
int morse_mac_get_max_rate_tries(void);
int morse_mac_get_max_rate(void);

int morse_mac_get_watchdog_interval_secs(void);

int morse_mac_send_vendor_wake_action_frame(struct morse *mors,
	const u8 *dest_addr, const u8 *payload, int payload_len);

int morse_mac_twt_traffic_control(struct morse *mors, int interface_id,
	bool pause_data_traffic);

/**
 * Function for filling the Tx meta info (rate info) for driver
 * generated management frames.
 */
void morse_fill_tx_info(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct morse_vif *mors_if,
				int tx_bw_mhz);

void morse_mac_schedule_probe_req(struct morse *mors);

/* Process ECSA IE and store the channel info. Also starts chan switch timer in sta mode */
void morse_mac_process_ecsa_ie(struct morse *mors, struct ieee80211_vif *vif,
			struct sk_buff *skb);

/* Process tx completion of skb used mainly for beacon change sequence */
void morse_mac_process_bcn_change_seq_tx_finish(struct morse *mors,
				struct sk_buff *skb);

/* Process tx completion of skb used mainly for ECSA */
void morse_mac_esca_beacon_tx_done(struct morse *mors, struct sk_buff *skb);

/* Update STA caps as per op bw */
void morse_ecsa_update_sta_caps(struct morse *mors, struct ieee80211_sta *sta);

#endif  /* !_MORSE_MAC_H_ */
