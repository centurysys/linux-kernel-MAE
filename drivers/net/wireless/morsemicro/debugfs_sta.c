// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Morse Micro
 *
 * Per-station debugfs files under stations/<MAC>/morse/.
 * Registered via the .sta_add_debugfs mac80211 callback.
 * This file is only compiled when CONFIG_MAC80211_DEBUGFS=y.
 */

#include <linux/debugfs.h>
#include <linux/ieee80211.h>
#include <linux/bitfield.h>
#include <linux/math64.h>

#include "morse.h"
#include "debug.h"
#include "misc.h"
#include "rc.h"
#include "morse_rate_code.h"

static void print_s1g_cap(char **p, const char *end, bool cond, const char *str)
{
	if (cond)
		*p += scnprintf(*p, end - *p, "    %s\n", str);
}

static const char * const s1g_ch_width_str[] = {
	"1/2 MHz",
	"1/2/4 MHz",
	"1/2/4/8 MHz",
	"1/2/4/8/16 MHz",
};

static const char * const s1g_trav_pilot_str[] = {
	"Not supported",
	"1 space-time stream only",
	"Reserved",
	"1 and 2 space-time streams with STBC",
};

static const char * const s1g_min_mpdu_spacing_str[] = {
	"No restriction",
	"1/4 us",
	"1/2 us",
	"1 us",
	"2 us",
	"4 us",
	"8 us",
	"16 us",
};

/* Used when peer is an AP */
static const char * const s1g_sta_type_ap_str[] = {
	"Sensor and Non-sensor STA",
	"Sensor STA",
	"Non-sensor STA",
	"Reserved",
};

/* Used when peer is a non-AP STA */
static const char * const s1g_sta_type_str[] = {
	"Reserved",
	"Sensor STA",
	"Non-sensor STA",
	"Reserved",
};

/* Used when peer is an AP */
static const char * const s1g_sector_beam_ap_str[] = {
	"Not supported",
	"Group sectorization",
	"TXOP-based sectorization",
	"Group and TXOP-based sectorization",
};

/* Used when peer is a non-AP STA */
static const char * const s1g_sector_beam_str[] = {
	"Not supported",
	"Supported",
	"Reserved",
	"Reserved",
};

static const char * const s1g_vht_link_adapt_str[] = {
	"No Feedback",
	"Reserved",
	"Unsolicited MFB only",
	"Solicited and unsolicited MFB",
};

static const char * const s1g_mcs_map_str[] = {
	"MCS 2", "MCS 7", "MCS 9+", "Not supported",
};

static const char * const s1g_1mhz_map_str[] = {
	"Same SS/MCS as MCS Map",
	"Single SS, max MCS 2",
	"Single SS, max MCS 7",
	"Single SS, max MCS 9+",
};

static ssize_t morse_sta_s1g_capa_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	bool peer_is_ap = (mors_sta->vif->type == NL80211_IFTYPE_STATION);
	const char * const *sta_type_str = peer_is_ap ? s1g_sta_type_ap_str : s1g_sta_type_str;
	const char * const *sector_beam_str = peer_is_ap ?
					      s1g_sector_beam_ap_str : s1g_sector_beam_str;
	const u8 *ci = mors_sta->s1g_cap.ie.capab_info;
	const u8 *mcs = mors_sta->s1g_cap.ie.supp_mcs_nss;
	u8  rx_mcs_map  = mcs[0];
	u16 rx_lgi_rate = ((u16)BMGET(mcs[2], S1G_MCS_NSS_RX_LGI_HIGH) << 8) | mcs[1];
	u8  tx_mcs_map  = (u8)((BMGET(mcs[3], S1G_MCS_NSS_TX_MAP_HIGH) << 7) |
				BMGET(mcs[2], S1G_MCS_NSS_TX_MAP_LOW));
	u16 tx_lgi_rate = ((u16)BMGET(mcs[4], S1G_MCS_NSS_TX_LGI_HIGH) << 7) |
			  BMGET(mcs[3], S1G_MCS_NSS_TX_LGI_LOW);
	u8  rx_1mhz_map = BMGET(mcs[4], S1G_MCS_NSS_RX_1MHZ_MAP);
	u8  tx_1mhz_map = BMGET(mcs[4], S1G_MCS_NSS_TX_1MHZ_MAP);
	ssize_t bufsz = 4096;
	ssize_t ret;
	char *buf, *p;
	int i;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	p += scnprintf(p, bufsz + buf - p, "S1G supported\n");

	/* cap[0]: S1G_LONG, Short GI per BW, Supported Channel Width */
	p += scnprintf(p, bufsz + buf - p, "cap[0]: 0x%02x\n", ci[0]);
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_S1G_LONG, "S1G_LONG PPDUs");
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_SGI_1MHZ, "Short GI 1 MHz");
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_SGI_2MHZ, "Short GI 2 MHz");
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_SGI_4MHZ, "Short GI 4 MHz");
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_SGI_8MHZ, "Short GI 8 MHz");
	print_s1g_cap(&p, buf + bufsz, ci[0] & S1G_CAP0_SGI_16MHZ, "Short GI 16 MHz");
	p += scnprintf(p, bufsz + buf - p,
		       "    Channel Width: %s\n",
		       s1g_ch_width_str[(unsigned int)FIELD_GET(S1G_CAP0_SUPP_CH_WIDTH, ci[0])]);

	/* cap[1]: RX LDPC, TX/RX STBC, SU BF, Beamformee STS */
	p += scnprintf(p, bufsz + buf - p, "cap[1]: 0x%02x\n", ci[1]);
	print_s1g_cap(&p, buf + bufsz, ci[1] & S1G_CAP1_RX_LDPC, "RX LDPC");
	print_s1g_cap(&p, buf + bufsz, ci[1] & S1G_CAP1_TX_STBC, "TX STBC");
	print_s1g_cap(&p, buf + bufsz, ci[1] & S1G_CAP1_RX_STBC, "RX STBC");
	print_s1g_cap(&p, buf + bufsz, ci[1] & S1G_CAP1_SU_BFER, "SU Beamformer");
	print_s1g_cap(&p, buf + bufsz, ci[1] & S1G_CAP1_SU_BFEE, "SU Beamformee");
	if (ci[1] & S1G_CAP1_SU_BFEE)
		p += scnprintf(p, bufsz + buf - p,
			       "    Beamformee STS: %u\n",
			       (unsigned int)FIELD_GET(S1G_CAP1_BFEE_STS, ci[1]));

	/* cap[2]: Sounding dimensions, MU BF, +HTC-VHT, Traveling Pilot */
	p += scnprintf(p, bufsz + buf - p, "cap[2]: 0x%02x\n", ci[2]);
	if (ci[1] & S1G_CAP1_SU_BFER)
		p += scnprintf(p, bufsz + buf - p,
			       "    Sounding Dimensions: %u\n",
			       (unsigned int)FIELD_GET(S1G_CAP2_SOUNDING_DIMENSIONS, ci[2]));
	print_s1g_cap(&p, buf + bufsz, ci[2] & S1G_CAP2_MU_BFER, "MU Beamformer");
	print_s1g_cap(&p, buf + bufsz, ci[2] & S1G_CAP2_MU_BFEE, "MU Beamformee");
	print_s1g_cap(&p, buf + bufsz, ci[2] & S1G_CAP2_PLUS_HTC_VHT, "+HTC-VHT");
	if (FIELD_GET(S1G_CAP2_TRAVELING_PILOT, ci[2]))
		p += scnprintf(p, bufsz + buf - p,
			       "    Traveling Pilot: %s\n",
			       s1g_trav_pilot_str[(unsigned int)FIELD_GET(S1G_CAP2_TRAVELING_PILOT,
									  ci[2])]);

	/* cap[3]: RD Responder, HT Delayed BA, Max MPDU length,
	 * A-MPDU exponent, Min MPDU spacing
	 */
	p += scnprintf(p, bufsz + buf - p, "cap[3]: 0x%02x\n", ci[3]);
	print_s1g_cap(&p, buf + bufsz, ci[3] & S1G_CAP3_RD_RESPONDER, "RD Responder");
	print_s1g_cap(&p, buf + bufsz, ci[3] & S1G_CAP3_HT_DELAYED_BA, "HT Delayed BA");
	p += scnprintf(p, bufsz + buf - p,
		       "    Max MPDU Length: %s\n",
		       (ci[3] & S1G_CAP3_MAX_MPDU_LEN) ? "7991 octets" : "3895 octets");
	p += scnprintf(p, bufsz + buf - p,
		       "    Max A-MPDU Length Exponent: %u (%lu octets max)\n",
		       (unsigned int)FIELD_GET(S1G_CAP3_MAX_AMPDU_LEN_EXP, ci[3]),
		       (1UL << (13 + (unsigned int)FIELD_GET(S1G_CAP3_MAX_AMPDU_LEN_EXP,
							      ci[3]))) - 1);
	p += scnprintf(p, bufsz + buf - p,
		       "    Min MPDU Start Spacing: %s\n",
		       s1g_min_mpdu_spacing_str[(unsigned int)FIELD_GET(S1G_CAP3_MIN_MPDU_START,
								       ci[3])]);

	/* cap[4]: Uplink Sync, Dynamic AID, BAT, TIM ADE, Non-TIM, Group AID, STA Type */
	p += scnprintf(p, bufsz + buf - p, "cap[4]: 0x%02x\n", ci[4]);
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_UPLINK_SYNC, "Uplink Sync");
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_DYNAMIC_AID, "Dynamic AID");
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_BAT, "BAT (Block Ack TWT)");
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_TIME_ADE, "TIM ADE");
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_NON_TIM, "Non-TIM");
	print_s1g_cap(&p, buf + bufsz, ci[4] & S1G_CAP4_GROUP_AID, "Group AID");
	p += scnprintf(p, bufsz + buf - p,
		       "    STA Type: %s\n",
		       sta_type_str[(unsigned int)FIELD_GET(S1G_CAP4_STA_TYPE, ci[4])]);

	/* cap[5]: Auth control, A-MSDU, A-MPDU, Asymmetric BA, Flow Control, Sectorized Beam */
	p += scnprintf(p, bufsz + buf - p, "cap[5]: 0x%02x\n", ci[5]);
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_CENT_AUTH_CONTROL,
		      "Centralized Auth Control");
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_DIST_AUTH_CONTROL,
		      "Distributed Auth Control");
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_AMSDU, "A-MSDU");
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_AMPDU, "A-MPDU");
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_ASYMMETRIC_BA, "Asymmetric BA");
	print_s1g_cap(&p, buf + bufsz, ci[5] & S1G_CAP5_FLOW_CONTROL, "Flow Control");
	if (FIELD_GET(S1G_CAP5_SECTORIZED_BEAM, ci[5]))
		p += scnprintf(p, bufsz + buf - p,
			       "    Sectorized Beam: %s\n",
			       sector_beam_str[(unsigned int)FIELD_GET(S1G_CAP5_SECTORIZED_BEAM,
								   ci[5])]);

	/* cap[6]: OBSS Mitigation, Fragment BA, NDP PS-Poll, RAW,
	 * Page Slicing, TXOP, VHT Link Adapt
	 */
	p += scnprintf(p, bufsz + buf - p, "cap[6]: 0x%02x\n", ci[6]);
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_OBSS_MITIGATION, "OBSS Mitigation");
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_FRAGMENT_BA, "Fragment BA");
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_NDP_PS_POLL, "NDP PS-Poll");
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_RAW_OPERATION, "RAW Operation");
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_PAGE_SLICING, "Page Slicing");
	print_s1g_cap(&p, buf + bufsz, ci[6] & S1G_CAP6_TXOP_SHARING_IMP_ACK,
		      "TXOP Sharing Implicit Ack");
	if (ci[2] & S1G_CAP2_PLUS_HTC_VHT)
		p += scnprintf(p, bufsz + buf - p,
			       "    VHT Link Adaptation: %s\n",
			       s1g_vht_link_adapt_str[(unsigned int)
					FIELD_GET(S1G_CAP6_VHT_LINK_ADAPT, ci[6])]);

	/* cap[7]: TACK, Dup 1 MHz, MCS Negotiation, 1 MHz Ctrl Preamble,
	 * NDP BF, Unsol DynAID, Sector Train, Temp PS
	 */
	p += scnprintf(p, bufsz + buf - p, "cap[7]: 0x%02x\n", ci[7]);
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_TACK_AS_PS_POLL, "TACK as PS-Poll");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_DUP_1MHZ, "Duplicate 1 MHz");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_MCS_NEGOTIATION, "MCS Negotiation");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE,
		      "1 MHz Control Response Preamble");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_NDP_BFING_REPORT_POLL,
		      "NDP Beamforming Report Poll");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_UNSOLICITED_DYN_AID,
		      "Unsolicited Dynamic AID");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_SECTOR_TRAINING_OPERATION,
		      "Sector Training Operation");
	print_s1g_cap(&p, buf + bufsz, ci[7] & S1G_CAP7_TEMP_PS_MODE_SWITCH, "Temp PS Mode Switch");

	/* cap[8]: TWT Grouping, BDT, COLOR, TWT Request/Respond */
	p += scnprintf(p, bufsz + buf - p, "cap[8]: 0x%02x\n", ci[8]);
	print_s1g_cap(&p, buf + bufsz, ci[8] & S1G_CAP8_TWT_GROUPING, "TWT Grouping");
	print_s1g_cap(&p, buf + bufsz, ci[8] & S1G_CAP8_BDT, "BDT (Bidirectional TXOP)");
	if (FIELD_GET(S1G_CAP8_COLOR, ci[8]))
		p += scnprintf(p, bufsz + buf - p,
			       "    COLOR: %u\n",
			       (unsigned int)FIELD_GET(S1G_CAP8_COLOR, ci[8]));
	print_s1g_cap(&p, buf + bufsz, ci[8] & S1G_CAP8_TWT_REQUEST, "TWT Requester");
	print_s1g_cap(&p, buf + bufsz, ci[8] & S1G_CAP8_TWT_RESPOND, "TWT Responder");

	/* cap[9]: Link Adaptation Without NDP CMAC PPDU */
	p += scnprintf(p, bufsz + buf - p, "cap[9]: 0x%02x\n", ci[9]);
	print_s1g_cap(&p, buf + bufsz, ci[9] & S1G_CAP9_LINK_ADAPT_PER_CONTROL_RESPONSE,
		      "Link Adapt w/o NDP CMAC PPDU");

	/* Supported S1G-MCS and NSS Set (802.11-2024 9.4.2.199.3)
	 *
	 * MCS Map per-SS encoding: 0=MCS 2, 1=MCS 7, 2=MCS 9+, 3=not supported
	 * Highest LGI Rate: 9-bit value in Mbps; 0 means not specified
	 */
	if (mors_sta->s1g_cap.ie_len < sizeof(mors_sta->s1g_cap.ie)) {
		p += scnprintf(p, bufsz + buf - p,
			       "MCS/NSS: not available (truncated IE)\n");
		goto out;
	}

	p += scnprintf(p, bufsz + buf - p, "Supported S1G-MCS/NSS set:");
	for (i = 0; i < (int)sizeof(mors_sta->s1g_cap.ie.supp_mcs_nss); i++)
		p += scnprintf(p, bufsz + buf - p, " %02x", mcs[i]);
	p += scnprintf(p, bufsz + buf - p, "\n");

	p += scnprintf(p, bufsz + buf - p, "Rx S1G-MCS Map:\n");
	for (i = 0; i < 4; i++) {
		u8 val = (rx_mcs_map >> (i * 2)) & 0x03;

		if (val != 3)
			p += scnprintf(p, bufsz + buf - p, "    %d SS: %s\n",
				       i + 1, s1g_mcs_map_str[val]);
	}
	if (rx_lgi_rate)
		p += scnprintf(p, bufsz + buf - p,
			       "Rx Highest LGI Rate: %u Mbps\n", rx_lgi_rate);

	p += scnprintf(p, bufsz + buf - p, "Tx S1G-MCS Map:\n");
	for (i = 0; i < 4; i++) {
		u8 val = (tx_mcs_map >> (i * 2)) & 0x03;

		if (val != 3)
			p += scnprintf(p, bufsz + buf - p, "    %d SS: %s\n",
				       i + 1, s1g_mcs_map_str[val]);
	}
	if (tx_lgi_rate)
		p += scnprintf(p, bufsz + buf - p,
			       "Tx Highest LGI Rate: %u Mbps\n", tx_lgi_rate);

	p += scnprintf(p, bufsz + buf - p,
		       "Rx 1 MHz SS/MCS: %s\n", s1g_1mhz_map_str[rx_1mhz_map]);
	p += scnprintf(p, bufsz + buf - p,
		       "Tx 1 MHz SS/MCS: %s\n", s1g_1mhz_map_str[tx_1mhz_map]);
out:

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_sta_s1g_capa_fops = {
	.read   = morse_sta_s1g_capa_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};

static ssize_t morse_sta_signal_read(struct file *file, char __user *userbuf,
				     size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	ssize_t bufsz = 256;
	ssize_t ret;
	char *buf, *p;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	p += scnprintf(p, bufsz + buf - p,
		       "Average RSSI: %d dBm\n", mors_sta->avg_rssi);

	if (mors_sta->last_rx.is_data_set) {
		s16 rssi = le16_to_cpu(mors_sta->last_rx.data_status.rssi);
		s8  noise = mors_sta->last_rx.data_status.noise_dbm;

		p += scnprintf(p, bufsz + buf - p, "Last data frame:\n");
		p += scnprintf(p, bufsz + buf - p, "    RSSI:  %d dBm\n", rssi);
		p += scnprintf(p, bufsz + buf - p, "    Noise: %d dBm\n", noise);
		p += scnprintf(p, bufsz + buf - p,
			       "    SNR:   %d dB\n", rssi - noise);
	}

	if (mors_sta->last_rx.is_mgmt_set) {
		s16 rssi = le16_to_cpu(mors_sta->last_rx.mgmt_status.rssi);
		s8  noise = mors_sta->last_rx.mgmt_status.noise_dbm;

		p += scnprintf(p, bufsz + buf - p, "Last mgmt frame:\n");
		p += scnprintf(p, bufsz + buf - p, "    RSSI:  %d dBm\n", rssi);
		p += scnprintf(p, bufsz + buf - p, "    Noise: %d dBm\n", noise);
		p += scnprintf(p, bufsz + buf - p,
			       "    SNR:   %d dB\n", rssi - noise);
	}

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_sta_signal_fops = {
	.read   = morse_sta_signal_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};

#ifdef CONFIG_MORSE_RC
static ssize_t morse_sta_last_tx_rate_read(struct file *file,
					   char __user *userbuf,
					   size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	struct mmrc_rate *rate = &mors_sta->last_sta_tx_rate;
	ssize_t bufsz = 128;
	ssize_t ret;
	char *buf, *p;
	u32 kbps;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	kbps = BPS_TO_KBPS(mmrc_calculate_theoretical_throughput(*rate));
	p += scnprintf(p, bufsz + buf - p,
		       "%u.%u Mbps  MCS %u  %uMHz  %uSS  %s\n",
		       kbps / 1000, kbps % 1000,
		       rate->rate,
		       morse_ratecode_bw_index_to_s1g_bw_mhz(rate->bw),
		       rate->ss + 1,
		       rate->guard == MMRC_GUARD_SHORT ? "SGI" : "LGI");
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_sta_last_tx_rate_fops = {
	.read   = morse_sta_last_tx_rate_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};

static ssize_t morse_sta_last_rx_rate_read(struct file *file,
					   char __user *userbuf,
					   size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	ssize_t bufsz = 128;
	ssize_t ret;
	char *buf, *p;
	struct mmrc_rate rx_rate;
	u32 kbps;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	if (!mors_sta->last_rx.is_data_set) {
		p += scnprintf(p, bufsz + buf - p, "No RX data\n");
		goto out;
	}

	rx_rate.guard = morse_ratecode_sgi_get(mors_sta->last_rx.data_status.morse_ratecode);
	rx_rate.rate  = morse_ratecode_mcs_index_get(mors_sta->last_rx.data_status.morse_ratecode);
	rx_rate.ss    = morse_ratecode_nss_index_get(mors_sta->last_rx.data_status.morse_ratecode);
	rx_rate.bw    = morse_ratecode_bw_index_get(mors_sta->last_rx.data_status.morse_ratecode);

	kbps = BPS_TO_KBPS(mmrc_calculate_theoretical_throughput(rx_rate));
	p += scnprintf(p, bufsz + buf - p,
		       "%u.%u Mbps  MCS %u  %uMHz  %uSS  %s\n",
		       kbps / 1000, kbps % 1000,
		       rx_rate.rate,
		       morse_ratecode_bw_index_to_s1g_bw_mhz(rx_rate.bw),
		       rx_rate.ss + 1,
		       rx_rate.guard == MMRC_GUARD_SHORT ? "SGI" : "LGI");
out:
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_sta_last_rx_rate_fops = {
	.read   = morse_sta_last_rx_rate_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};
#endif /* CONFIG_MORSE_RC */

static ssize_t morse_sta_traffic_stats_read(struct file *file, char __user *userbuf,
					    size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	struct morse_sta_stats *stats = mors_sta->bss_stats_sta.stats;
	static const char * const ac_names[] = { "BE", "BK", "VI", "VO" };
	ssize_t bufsz = 1024;
	ssize_t ret;
	char *buf, *p;
	u64 elapsed_s = 0;
	int ac;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	if (!mors_sta->debugfs_set) {
		p += scnprintf(p, bufsz + buf - p,
			       "Disabled (set collect_traffic_stats to enable)\n");
		goto done;
	}

	if (!stats) {
		p += scnprintf(p, bufsz + buf - p, "no stats (not yet associated)\n");
		goto done;
	}

	p += scnprintf(p, bufsz + buf - p,
		       "%-4s  %-14s  %-14s  %-9s  %-9s  %-16s  %-16s  %s\n",
		       "AC", "tx_bytes", "rx_bytes",
		       "tx_pkts", "rx_pkts", "tx_retries", "rx_retries", "tx_dropped");

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		u64 tx_pkts  = stats->num_tx_pkts[ac];
		u64 rx_pkts  = stats->num_rx_pkts[ac];
		u64 tx_retry = stats->num_tx_retries[ac];
		u64 rx_retry = stats->num_rx_retries[ac];
		char tx_retry_str[32];
		char rx_retry_str[32];

		if (!tx_pkts) {
			scnprintf(tx_retry_str, sizeof(tx_retry_str), "-");
		} else if (!tx_retry) {
			scnprintf(tx_retry_str, sizeof(tx_retry_str), "0");
		} else {
			u64 pct_x10 = div64_u64(tx_retry * 1000, tx_pkts);

			scnprintf(tx_retry_str, sizeof(tx_retry_str), "%llu (%llu.%llu%%)",
				  tx_retry, pct_x10 / 10, pct_x10 % 10);
		}

		if (!rx_pkts) {
			scnprintf(rx_retry_str, sizeof(rx_retry_str), "-");
		} else if (!rx_retry) {
			scnprintf(rx_retry_str, sizeof(rx_retry_str), "0");
		} else {
			u64 pct_x10 = div64_u64(rx_retry * 1000, rx_pkts);

			scnprintf(rx_retry_str, sizeof(rx_retry_str), "%llu (%llu.%llu%%)",
				  rx_retry, pct_x10 / 10, pct_x10 % 10);
		}

		p += scnprintf(p, bufsz + buf - p,
			       "%-4s  %-14u  %-14u  %-9llu  %-9llu  %-16s  %-16s  %u\n",
			       ac_names[ac],
			       stats->num_tx_bytes[ac],
			       stats->num_rx_bytes[ac],
			       tx_pkts, rx_pkts,
			       tx_retry_str, rx_retry_str,
			       stats->num_tx_dropped[ac]);
	}

	if (stats->last_reset_time) {
		u64 elapsed_ns = ktime_get_ns() - stats->last_reset_time;

		elapsed_s = div_u64(elapsed_ns, NSEC_PER_SEC);
	}
	p += scnprintf(p, bufsz + buf - p, "window: %llus\n", elapsed_s);

done:
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_sta_traffic_stats_fops = {
	.read   = morse_sta_traffic_stats_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};

void morse_sta_add_debugfs(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct dentry *dir)
{
	struct dentry *morse_dir;

	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;

	morse_dir = debugfs_create_dir("morse", dir);
	if (IS_ERR_OR_NULL(morse_dir))
		return;

	debugfs_create_bool("collect_traffic_stats", 0600, morse_dir, &mors_sta->debugfs_set);

	debugfs_create_file("s1g_capa", 0400, morse_dir, sta,
			    &morse_sta_s1g_capa_fops);
	debugfs_create_file("signal", 0400, morse_dir, sta,
			    &morse_sta_signal_fops);
#ifdef CONFIG_MORSE_RC
	debugfs_create_file("last_tx_rate", 0400, morse_dir, sta,
			    &morse_sta_last_tx_rate_fops);
	debugfs_create_file("last_rx_rate", 0400, morse_dir, sta,
			    &morse_sta_last_rx_rate_fops);
#endif
	debugfs_create_file("traffic_stats", 0400, morse_dir, sta,
			    &morse_sta_traffic_stats_fops);
}
