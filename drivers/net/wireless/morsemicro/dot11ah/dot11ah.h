#ifndef _MORSE_DOT11AH_H_
#define _MORSE_DOT11AH_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/types.h>
#include <net/mac80211.h>
#include <linux/ieee80211.h>
#include "s1g_ieee80211.h"


#define IEEE80211_FC_NEXT_TBTT					0x0100
#define IEEE80211_FC_COMPRESS_SSID				0x0200
#define IEEE80211_FC_ANO					0x0400
#define IEEE80211_FC_BSS_BW					0x3800

#define IEEE80211_FC_S1G_SECURITY_SUPPORTED			0x4000
#define IEEE80211AH_UNKNOWN_SSID				"Unknown S1G Compressed"

#define STYPE_S1G_BEACON_BSS_BW_OFFSET				11

#define IEEE80211AH_GET_FC_BSS_BW(x) ((x & IEEE80211_FC_BSS_BW) >> STYPE_S1G_BEACON_BSS_BW_OFFSET)

/* Offsets and values for 9.4.2.200.2 S1G Capabilities Information field */
/* Note these are 0 indexed in code, 1 indexed in the standard */
/* Octet 1  */
#define S1G_CAP0_SUPP_WIDTH_OFFSET   6
#define S1G_CAP0_SET_SUPP_WIDTH(x) ((x << S1G_CAP0_SUPP_WIDTH_OFFSET) & S1G_CAP0_SUPP_CH_WIDTH)
#define S1G_CAP0_GET_SUPP_WIDTH(x) ((x & S1G_CAP0_SUPP_CH_WIDTH) >> S1G_CAP0_SUPP_WIDTH_OFFSET)
#define S1G_CAP0_SUPP_2MHZ  S1G_CAP0_SET_SUPP_WIDTH(0)
#define S1G_CAP0_SUPP_4MHZ  S1G_CAP0_SET_SUPP_WIDTH(1)
#define S1G_CAP0_SUPP_8MHZ  S1G_CAP0_SET_SUPP_WIDTH(2)
#define S1G_CAP0_SUPP_16MHZ S1G_CAP0_SET_SUPP_WIDTH(3)

/* Octet 2  */
/* Octet 3  */
/**
 * 802.11me Table 9-300 (Subfields of the S1G Capabilities Information field).
 *
 * Set to 0 if RX of travelling pilots is not supported
 * Set to 1 if RX of 1NSS travelling pilots is supported with STBC
 * 2 is reserved
 * Set to 3 if RX of 1NSS & 2NSS is sipported with STBC
 */
#define S1G_CAP2_GET_TRAV_PILOT(byte2) \
	(((byte2) >> 6) & 0x03)
#define S1G_CAP2_SET_TRAV_PILOT(trav_pilot) \
	(((trav_pilot & 0x3) << 6))

enum trav_pilot_support {
	TRAV_PILOT_RX_NOT_SUPPORTED = 0,
	TRAV_PILOT_RX_1NSS = 1,
	TRAV_PILOT_RESERVED1 = 2,
	TRAV_PILOT_RX_1_2_NSS = 3
};

/* Octet 4  */
#define S1G_CAP3_MPDU_MAX_LEN_OFFSET 2
#define S1G_CAP3_SET_MPDU_MAX_LEN(x) ((x << S1G_CAP3_MPDU_MAX_LEN_OFFSET) & S1G_CAP3_MAX_MPDU_LEN)
#define S1G_CAP3_MPDU_MAX_LEN_3895   S1G_CAP3_SET_MPDU_MAX_LEN(0)
#define S1G_CAP3_MPDU_MAX_LEN_7991   S1G_CAP3_SET_MPDU_MAX_LEN(1)

#define S1G_CAP3_MAX_AMPDU_LEN_EXP_OFFSET 3
#define S1G_CAP3_SET_MAX_AMPDU_LEN_EXP(x) ((x << S1G_CAP3_MAX_AMPDU_LEN_EXP_OFFSET) &\
					  S1G_CAP3_MAX_AMPDU_LEN_EXP)

#define S1G_CAP3_MIN_AMPDU_START_SPC_OFFSET 5
#define S1G_CAP3_SET_MIN_AMPDU_START_SPC(x) ((x << S1G_CAP3_MIN_AMPDU_START_SPC_OFFSET) &\
					S1G_CAP3_MIN_MPDU_START)
/* Octet 5  */
#define S1G_CAP4_STA_TYPE_OFFSET	6
#define S1G_CAP4_SET_STA_TYPE(x) ((x << S1G_CAP4_STA_TYPE_OFFSET) & S1G_CAP4_STA_TYPE)
#define S1G_CAP4_STA_TYPE_BOTH		S1G_CAP4_SET_STA_TYPE(0)
#define S1G_CAP4_STA_TYPE_SENSOR	S1G_CAP4_SET_STA_TYPE(1)
#define S1G_CAP4_STA_TYPE_NON_SENSOR	S1G_CAP4_SET_STA_TYPE(2)
/* Octet 6  */
/* Octet 7  */
/* Octet 8  */
/* Octet 9  */
#define S1G_CAP8_COLOR_OFFSET	2
#define S1G_CAP8_SET_COLOR(x)	((x << S1G_CAP8_COLOR_OFFSET) & S1G_CAP8_COLOR)
#define S1G_CAP8_GET_COLOR(x)	((x & S1G_CAP8_COLOR) >> S1G_CAP8_COLOR_OFFSET)
/* Octet 10 */

#define IEEE80211AH_MCS7_1SS_RX_SUPPORT			(0x01)
#define IEEE80211AH_MCS7_1SS_TX_SUPPORT			(0x02)


#define WLAN_EID_S1G_RPS				(208)
#define WLAN_EID_S1G_CAC				(222)

#if KERNEL_VERSION(5, 10, 11) > MAC80211_VERSION_CODE
#define IEEE80211_NDP_FTYPE_CF_END			(0)
#define IEEE80211_NDP_FTYPE_PS_POLL			(1)
#define IEEE80211_NDP_FTYPE_ACK				(2)
#define IEEE80211_NDP_FTYPE_PS_POLL_ACK			(3)
#define IEEE80211_NDP_FTYPE_BA				(4)
#define IEEE80211_NDP_FTYPE_BF_REPORT_POLL		(5)
#define IEEE80211_NDP_FTYPE_PAGING			(6)
#define IEEE80211_NDP_FTYPE_PREQ			(7)

#define WLAN_EID_AID_REQUEST				(210)
#define WLAN_EID_AID_RESPONSE				(211)
#define WLAN_EID_S1G_BCN_COMPAT				(213)
#define WLAN_EID_S1G_SHORT_BCN_INTERVAL			(214)
#define WLAN_EID_S1G_CAPABILITIES			(217)
#define WLAN_EID_S1G_OPERATION				(232)
#define WLAN_EID_RSNX					(244)
#endif

#if KERNEL_VERSION(5, 10, 0) > MAC80211_VERSION_CODE
#define S1G_CAP0_S1G_LONG			BIT(0)
#define S1G_CAP0_SGI_1MHZ			BIT(1)
#define S1G_CAP0_SGI_2MHZ			BIT(2)
#define S1G_CAP0_SGI_4MHZ			BIT(3)
#define S1G_CAP0_SGI_8MHZ			BIT(4)
#define S1G_CAP0_SGI_16MHZ			BIT(5)
#define S1G_CAP0_SUPP_CH_WIDTH			GENMASK(7, 6)

#define S1G_SUPP_CH_WIDTH_2			0
#define S1G_SUPP_CH_WIDTH_4			1
#define S1G_SUPP_CH_WIDTH_8			2
#define S1G_SUPP_CH_WIDTH_16			3
#define S1G_SUPP_CH_WIDTH_MAX(cap) ((1 << FIELD_GET(S1G_CAP0_SUPP_CH_WIDTH, \
						    cap[0])) << 1)

#define S1G_CAP1_RX_LDPC			BIT(0)
#define S1G_CAP1_TX_STBC			BIT(1)
#define S1G_CAP1_RX_STBC			BIT(2)
#define S1G_CAP1_SU_BFER			BIT(3)
#define S1G_CAP1_SU_BFEE			BIT(4)
#define S1G_CAP1_BFEE_STS			GENMASK(7, 5)

#define S1G_CAP2_SOUNDING_DIMENSIONS		GENMASK(2, 0)
#define S1G_CAP2_MU_BFER			BIT(3)
#define S1G_CAP2_MU_BFEE			BIT(4)
#define S1G_CAP2_PLUS_HTC_VHT			BIT(5)
#define S1G_CAP2_TRAVELING_PILOT		GENMASK(7, 6)

#define S1G_CAP3_RD_RESPONDER			BIT(0)
#define S1G_CAP3_HT_DELAYED_BA			BIT(1)
#define S1G_CAP3_MAX_MPDU_LEN			BIT(2)
#define S1G_CAP3_MAX_AMPDU_LEN_EXP		GENMASK(4, 3)
#define S1G_CAP3_MIN_MPDU_START			GENMASK(7, 5)

#define S1G_CAP4_UPLINK_SYNC			BIT(0)
#define S1G_CAP4_DYNAMIC_AID			BIT(1)
#define S1G_CAP4_BAT				BIT(2)
#define S1G_CAP4_TIME_ADE			BIT(3)
#define S1G_CAP4_NON_TIM			BIT(4)
#define S1G_CAP4_GROUP_AID			BIT(5)
#define S1G_CAP4_STA_TYPE			GENMASK(7, 6)

#define S1G_CAP5_CENT_AUTH_CONTROL		BIT(0)
#define S1G_CAP5_DIST_AUTH_CONTROL		BIT(1)
#define S1G_CAP5_AMSDU				BIT(2)
#define S1G_CAP5_AMPDU				BIT(3)
#define S1G_CAP5_ASYMMETRIC_BA			BIT(4)
#define S1G_CAP5_FLOW_CONTROL			BIT(5)
#define S1G_CAP5_SECTORIZED_BEAM		GENMASK(7, 6)

#define S1G_CAP6_OBSS_MITIGATION		BIT(0)
#define S1G_CAP6_FRAGMENT_BA			BIT(1)
#define S1G_CAP6_NDP_PS_POLL			BIT(2)
#define S1G_CAP6_RAW_OPERATION			BIT(3)
#define S1G_CAP6_PAGE_SLICING			BIT(4)
#define S1G_CAP6_TXOP_SHARING_IMP_ACK		BIT(5)
#define S1G_CAP6_VHT_LINK_ADAPT			GENMASK(7, 6)

#define S1G_CAP7_TACK_AS_PS_POLL		BIT(0)
#define S1G_CAP7_DUP_1MHZ			BIT(1)
#define S1G_CAP7_MCS_NEGOTIATION		BIT(2)
#define S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE	BIT(3)
#define S1G_CAP7_NDP_BFING_REPORT_POLL		BIT(4)
#define S1G_CAP7_UNSOLICITED_DYN_AID		BIT(5)
#define S1G_CAP7_SECTOR_TRAINING_OPERATION	BIT(6)
#define S1G_CAP7_TEMP_PS_MODE_SWITCH		BIT(7)

#define S1G_CAP8_TWT_GROUPING			BIT(0)
#define S1G_CAP8_BDT				BIT(1)
#define S1G_CAP8_COLOR				GENMASK(4, 2)
#define S1G_CAP8_TWT_REQUEST			BIT(5)
#define S1G_CAP8_TWT_RESPOND			BIT(6)
#define S1G_CAP8_PV1_FRAME			BIT(7)

#define S1G_CAP9_LINK_ADAPT_PER_CONTROL_RESPONSE BIT(0)

#define S1G_OPER_CH_WIDTH_PRIMARY_1MHZ	BIT(0)
#endif

#define IEEE80211AH_AMPDU_SUPPORTED		(BIT(3))

/* Older kernels do not have WLAN EID extensions defined */
#if KERNEL_VERSION(4, 10, 0) > MAC80211_VERSION_CODE
#define WLAN_EID_EXTENSION			(255)
#define WLAN_EID_EXT_FILS_SESSION		(4)
#define DOT11AH_MAX_EID				(WLAN_EID_EXTENSION + 1)
#else
#define DOT11AH_MAX_EID				(256)
#endif

#define DOT11AH_VERSION __stringify(MORSE_VERSION)
/**
 * The Primary Channel Width subfield, located in B0 of this
 * field, and the BSS Operating Channel Width subfield,
 * located in B1–B4 of this field, are defined in Table 10-32
 * (S1G BSS operating channel width(11ah)).
 */
#define IEEE80211AH_S1G_OPERATION_GET_OP_CHAN_BW(byte0) \
	(((((byte0) >> 1)) & 0xF) + 1)
#define IEEE80211AH_S1G_OPERATION_SET_OP_CHAN_BW(op_bw_mhz) \
	((((op_bw_mhz) - 1) << 1) & 0x1E)
#define IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_BW(byte0) \
	(((byte0) & 0x01) ? 1 : 2)
#define IEEE80211AH_S1G_OPERATION_SET_PRIM_CHAN_BW(pri_bw_mhz) \
	((((pri_bw_mhz) == 2) ? 0 : 1) & 0x01)

/**
 * Table 10-32 (S1G BSS operating channel width(11ah)).
 *
 * B5 bits indicates the location of 1 MHz primary
 * channel within the 2 MHz primary channel
 *  * B5 is set to 0 to indicate that is located at the lower
 *    side of 2 MHz primary channel.
 *  * B5 is set to 1 to indicate that is located at the upper
 *    side of 2 MHz primary channel.
 */
#define IEEE80211AH_S1G_OPERATION_GET_PRIM_CHAN_LOC(byte0) \
	(((byte0) >> 5) & 0x01)
#define IEEE80211AH_S1G_OPERATION_SET_PRIM_CHAN_LOC(pri_chan_loc) \
	(((pri_chan_loc) << 5) & 0x20)

/*
 * TWT definitions.
 */
#if KERNEL_VERSION(5, 15, 0) > MAC80211_VERSION_CODE
#define WLAN_EID_S1G_TWT				(216)

#define IEEE80211_TWT_CONTROL_NDP			BIT(0)
#define IEEE80211_TWT_CONTROL_RESP_MODE			BIT(1)
#define IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST	BIT(3)
#define IEEE80211_TWT_CONTROL_RX_DISABLED		BIT(4)
#define IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT		BIT(5)

#define IEEE80211_TWT_REQTYPE_REQUEST			BIT(0)
#define IEEE80211_TWT_REQTYPE_SETUP_CMD			GENMASK(3, 1)
#define IEEE80211_TWT_REQTYPE_TRIGGER			BIT(4)
#define IEEE80211_TWT_REQTYPE_IMPLICIT			BIT(5)
#define IEEE80211_TWT_REQTYPE_FLOWTYPE			BIT(6)
#define IEEE80211_TWT_REQTYPE_FLOWID			GENMASK(9, 7)
#define IEEE80211_TWT_REQTYPE_WAKE_INT_EXP		GENMASK(14, 10)
#define IEEE80211_TWT_REQTYPE_PROTECTION		BIT(15)

enum ieee80211_twt_setup_cmd {
	TWT_SETUP_CMD_REQUEST,
	TWT_SETUP_CMD_SUGGEST,
	TWT_SETUP_CMD_DEMAND,
	TWT_SETUP_CMD_GROUPING,
	TWT_SETUP_CMD_ACCEPT,
	TWT_SETUP_CMD_ALTERNATE,
	TWT_SETUP_CMD_DICTATE,
	TWT_SETUP_CMD_REJECT,
};

struct ieee80211_twt_params {
	__le16 req_type;
	__le64 twt;
	u8 min_twt_dur;
	__le16 mantissa;
	u8 channel;
} __packed;

struct ieee80211_twt_setup {
	u8 dialog_token;
	u8 element_id;
	u8 length;
	u8 control;
	u8 params[];
} __packed;
#endif

#define IEEE80211_TWT_CONTROL_NEG_TYPE			BIT(2)
#define IEEE80211_TWT_REQTYPE_SETUP_CMD_OFFSET		(1)
#define IEEE80211_TWT_REQTYPE_IMPLICIT_OFFSET		(5)
#define IEEE80211_TWT_REQTYPE_FLOWID_OFFSET		(7)
#define IEEE80211_TWT_REQTYPE_WAKE_INT_EXP_OFFSET	(10)

/**
 * S1G listen interval definitions
 *
 * These flags are used for listen interval conversion
 *
 * @IEEE80211_S1G_LI_USF: S1G listen interval unified scale factor mask
 * @IEEE80211_S1G_LI_UNSCALED: S1G listen interval unscaled interval
 */
#define IEEE80211_S1G_LI_USF					(BIT(14) | BIT(15))
#define IEEE80211_S1G_LI_UNSCALED_INTERVAL		GENMASK(13, 0)

#define IEEE80211_S1G_LI_USF_SHIFT				14

#define WLAN_ACTION_NDP_ADDBA_REQ				(128)
#define WLAN_ACTION_NDP_ADDBA_RESP				(129)
#define WLAN_ACTION_NDP_DELBA					(130)

#define MORSE_FC_BSS_BW_INVALID				(255)

/**
 * As per standard 9.2.4.1.18, BSS BW SUBFILED value 1 indicates that the Min or Max BSS BW is
 * equal to the BW of the PPDU carrying the BSS BW filed.
 */
#define MORSE_FC_BSS_BW_UNDEFINED				(1)

/**
 * enum ieee80211_li_usf - listen interval unified scal factors
 * @IEEE80211_LI_USF_1: 1
 * @IEEE80211_LI_USF_10: 10
 * @IEEE80211_LI_USF_1000: 1000
 * @IEEE80211_LI_USF_10000: 10000
 */
enum ieee80211_li_usf {
	IEEE80211_LI_USF_1			= 0,
	IEEE80211_LI_USF_10			= 1,
	IEEE80211_LI_USF_1000		= 2,
	IEEE80211_LI_USF_10000		= 3,
};

/**
 * S1G frequency range
 */
#define MORSE_S1G_FREQ_MIN_KHZ 750000
#define MORSE_S1G_FREQ_MAX_KHZ 950000

/**
 * struct morse_dot11ah_channel - S1G channel definition
 *
 * This structure describes a single S1G channel for use
 * with morse dot11ah.
 *
 * @ch: iee80211_channel channel definition
 * @hw_value_map: 5G channel map
 */
struct morse_dot11ah_channel {
	/* Steal definition from Linux if necessary. */
	struct ieee80211_channel_s1g ch;
	u16 hw_value_map;
};


enum station_type {
	STA_TYPE_MIXED = 0x00,
	STA_TYPE_SENSOR = 0x01,
	STA_TYPE_NON_SENSOR = 0x02,
	STAY_TYPE_UNKNOWN = 0xFF
};

/**
 * The BSS BW subfield indicates the minimum and the
 * maximum operating bandwidths of the BSS as defined
 * in Table 9-8 (Frame Control field BSS BW setting(11ah)).
 */
enum ieee80211ah_s1g_fc_bss_bw {
	FC_BSS_BW_1_in_2 = 0x0000,
	FC_BSS_BW_EQUAL = 0x0800,
	FC_BSS_BW_1_in_4 = 0x1000,
	FC_BSS_BW_2_in_4 = 0x1800,
	FC_BSS_BW_1_in_8 = 0x2000,
	FC_BSS_BW_2_in_8 = 0x2800,
	FC_BSS_BW_1_in_16 = 0x3000,
	FC_BSS_BW_2_in_16 = 0x3800
};

/**
 * Lookup table for BSS BW based on Minimum BSS BW
 * (MHz) and Maximum BSS BW (MHz).
 */
static const int
ieee80211ah_s1g_fc_bss_bw_lookup[17][17] = {
	[1] = {
		[1] = FC_BSS_BW_EQUAL,
		[2] = FC_BSS_BW_1_in_2,
		[4] = FC_BSS_BW_1_in_4,
		[8] = FC_BSS_BW_1_in_8,
		[16] = FC_BSS_BW_1_in_16,
	},
	[2] = {
		[2] = FC_BSS_BW_EQUAL,
		[4] = FC_BSS_BW_2_in_4,
		[8] = FC_BSS_BW_2_in_8,
		[16] = FC_BSS_BW_2_in_16,
	},
	[4] = {
		[4] = FC_BSS_BW_EQUAL,
	},
	[8] = {
		[8] = FC_BSS_BW_EQUAL,
	},
	[16] = {
		[16] = FC_BSS_BW_EQUAL,
	}
};

/* See Section 9.2.4.1.18 BSS BW subfield, Table 9-8—Frame Control field BSS BW setting */
static const int s1g_fc_bss_bw_lookup_min[8] = {
	[0] = 1,
	[1] = -1,
	[2] = 1,
	[3] = 2,
	[4] = 1,
	[5] = 2,
	[6] = 1,
	[7] = 2,
};

/* See Section 9.2.4.1.18 BSS BW subfield, Table 9-8—Frame Control field BSS BW setting */
static const int s1g_fc_bss_bw_lookup_max[8] = {
	[0] = 2,
	[1] = -1,
	[2] = 4,
	[3] = 4,
	[4] = 8,
	[5] = 8,
	[6] = 16,
	[7] = 16,
};

#define MORSE_IS_FC_BSS_BW_SUBFIELD_VALID(fc_bss_bw)	((fc_bss_bw != MORSE_FC_BSS_BW_INVALID) && \
	(fc_bss_bw < ARRAY_SIZE(s1g_fc_bss_bw_lookup_min)) && \
	(fc_bss_bw != MORSE_FC_BSS_BW_UNDEFINED))

/* TODO: replace with ieee80211_mgmt.s1g_assoc_resp defined now in K5.10 */
struct morse_dot11ah_s1g_assoc_resp {
	__le16 frame_control;
	__le16 duration;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	__le16 seq_ctrl;
	__le16 capab_info;
	__le16 status_code;
	/* followed by Supported rates */
	u8 variable[0];
};

struct morse_dot11ah_cssid_item {
	struct list_head list;
	__le32 cssid;
	unsigned long last_seen;
	u16 capab_info;
	u8 bssid[ETH_ALEN];
	int ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	int ies_len;
	u8 *ies;
	u8 fc_bss_bw_subfield;
};

enum morse_dot11ah_region {
	MORSE_AU,
	MORSE_EU,
	MORSE_IN,
	MORSE_JP,
	MORSE_KR,
	MORSE_NZ,
	MORSE_SG,
	MORSE_US,
	REGION_UNSET = 0xFF,
};

struct morse_channel_info {
	/** Operating Channel Frequency Hz */
	u32 op_chan_freq_hz;
	/** Operating Bandwidth MHz */
	u8 op_bw_mhz;
	/** Primary channel Bandwidth MHz */
	u8 pri_bw_mhz;
	/** Primary 1mhz channel index */
	u8 pri_1mhz_chan_idx;
	/** S1G operating class */
	u8 s1g_operating_class;
	/** Primary channel S1G operating class */
	u8 pri_global_operating_class;
};

struct dot11ah_short_beacon_ie {
	__le16 short_beacon_int;
} __packed;


/* TODO: replace with ieee80211_s1g_bcn_compat_ie defined now in K5.10 */
struct dot11ah_s1g_beacon_compatibility_ie {
	__le16 information;
	__le16 beacon_interval;
	__le32 tsf_completion;
} __packed;

/* TODO: replace with ieee80211_s1g_oper_ie defined now in K5.10 */
struct s1g_operation_parameters {
	u8 chan_centre_freq_num;
	u8 op_bw_mhz;
	u8 pri_bw_mhz;
	u8 pri_1mhz_chan_idx;
	u8 s1g_operating_class;
	u8 prim_global_op_class;
};

struct dot11ah_update_rx_beacon_vals {
	__le16 capab_info;
	__le16 bcn_int;
	u8 tim_len;
	const u8 *tim_ie;
};

struct s1g_operation_params_expanded {
	u8 op_class;
	u8 pri_ch;
	u8 op_ch;
	bool upper_1mhz;
	bool primary_2mhz;
	bool use_mcs10;
	u8 op_bw;
};

/** CAC control field - 0: centralised control, 1: distributed control */
#define DOT11AH_S1G_CAC_CONTROL		BIT(0)
/** CAC deferral field - 0: use a threshold value, 1: use a deferral time */
#define DOT11AH_S1G_CAC_DEFERRAL	BIT(1)
#define DOT11AH_S1G_CAC_RESERVED	GENMASK(5, 2)
/** CAC threshold */
#define DOT11AH_S1G_CAC_THRESHOLD	GENMASK(15, 6)

enum dot11ah_s1g_auth_control {
	DOT11AH_S1G_AUTH_CONTROL_CAC = 0,
	DOT11AH_S1G_AUTH_CONTROL_DAC = 1
};

struct dot11ah_s1g_auth_control_ie {
	u16 parameters;
} __packed;

#define MORSE_COUNTRY_OPERATING_TRIPLET_ID 201
#define MORSE_GLOBAL_OPERATING_CLASS_TABLE 0x04
#define MORSE_OPERATING_CHAN_DEFAULT 38
#define MORSE_OPERATING_CH_WIDTH_DEFAULT 2
#define MORSE_PRIM_CH_WIDTH_DEFAULT 2

struct country_ie_triplet {
	u8 first_chan;
	u8 chan_num;
	u8 max_eirp_dbm;
} __packed;

struct country_operating_triplet {
	u8 op_triplet_id;
	u8 primary_band_op_class;
	u8 coverage_class;
	u8 start_chan;
	u8 chan_num;
	u8 max_eirp_dbm;
} __packed;

struct dot11ah_country_ie {
	char country[3];
	struct country_operating_triplet ie_triplet;
} __packed;

/**
 * @brief Stores an individual IE in a dot11ah_ies_mask
 *
 * @ptr: Pointer to the information element
 * @len: Length of the individual information element value
 * @next: Pointer to next ie_element of the same element ID (if multiple of the same IE in a single
 * management frame eg. VENDOR_SPECIFIC or EXTENSION)
 */
struct ie_element {
	const u8 *ptr;
	u8 len;
	struct ie_element *next;
};

/**
 * struct dot11ah_ies_mask - Stores IE values
 *
 * @ies: Array of IEs, indexed by element ID
 * @more_than_one_ie: bitmask where if bit is set, there are multiple IEs with the same element ID
 *	in the mask
 * @fils_data: FILS Session element and encrypted data, which if present, is always at the end of a
 *	management frame
 * @fils_data_length: Length of the FILS Session element and encrypted data
 */
struct dot11ah_ies_mask {
	struct ie_element ies[DOT11AH_MAX_EID];
	/* makes freeing/clearing easier */
	DECLARE_BITMAP(more_than_one_ie, DOT11AH_MAX_EID);
	u8 *fils_data;
	int fils_data_len;
};

extern spinlock_t cssid_list_lock;

struct morse_channel {
	u32 frequency_khz;
	u8 channel_5g;
	u8 channel_s1g;
	u8 bandwidth_mhz;
} __packed;

struct morse_reg_rule {
	struct ieee80211_reg_rule dot11_reg;
	struct {
		u32 ap;
		u32 sta;
		bool omit_ctrl_resp;
	} duty_cycle;
	/** Minimum Packet Spacing Window*/
	struct {
		/** Minimum airtime duration that will trigger packet spacing */
		u32 airtime_min_us;
		/** Maximum allowable airtime. Packets longer than this will be rejected */
		u32 airtime_max_us;
		/** The spacing time to apply between eligible packets */
		u32 window_length_us;
	} mpsw;
};

struct morse_regdomain {
	u32 n_reg_rules;
	char alpha2[3];
	struct morse_reg_rule reg_rules[];
};

int morse_dot11ah_s1g_to_11n_rx_packet_size(struct ieee80211_vif *vif,
	struct sk_buff *skb, struct dot11ah_ies_mask *ies_mask);

void morse_dot11ah_s1g_to_11n_rx_packet(struct ieee80211_vif *vif,
	struct sk_buff *skb, int length_11n, struct dot11ah_ies_mask *ies_mask);

int morse_dot11ah_11n_to_s1g_tx_packet_size(
	struct ieee80211_vif *vif, struct sk_buff *skb, bool short_beacon, struct dot11ah_ies_mask *ies_mask);

struct dot11ah_ies_mask *morse_dot11ah_ies_mask_alloc(void);

void morse_dot11ah_ies_mask_free(struct dot11ah_ies_mask *ies_mask);

void morse_dot11ah_mask_ies(struct dot11ah_ies_mask *ies_mask, bool mask_ext_cap, bool is_beacon);

void morse_dot11ah_ies_mask_clear(struct dot11ah_ies_mask *ies_mask);

u8 *morse_dot11_insert_ie_from_ies_mask(u8 *pos, const struct dot11ah_ies_mask *ies_mask, int eid);

void morse_dot11ah_11n_to_s1g_tx_packet(struct ieee80211_vif *vif,
	struct sk_buff *skb, int s1g_length, bool short_beacon, struct dot11ah_ies_mask *ies_mask);

bool morse_mac_find_channel_info_for_bssid(
	u8 bssid[ETH_ALEN], struct morse_channel_info *info);

int morse_dot11_find_bssid_on_channel(u32 op_chan_freq_hz, u8 bssid[ETH_ALEN]);

void morse_dot11ah_update_channels_mapping(u32 *channel, u8 count);

void morse_dot11ah_clear_list(void);

bool morse_dot11ah_find_s1g_caps_for_bssid(u8 *bssid, struct ieee80211_s1g_cap *s1g_caps);

bool morse_dot11ah_find_bss_bw(u8 *bssid, u8 *fc_bss_bw_subfield);

bool morse_dot11ah_find_s1g_operation_for_ssid(
	const char *ssid, size_t ssid_len, struct s1g_operation_parameters *params);

int morse_dot11ah_prim_1mhz_channel_loc_to_idx(
	int op_bw_mhz, int pr_bw_mhz, int pr_chan_num, int chan_centre_freq_num, int chan_loc);

int morse_dot11ah_calculate_primary_s1g_channel(
	int op_bw_mhz, int pr_bw_mhz, int chan_centre_freq_num, int pr_1mhz_chan_idx);

int morse_dot11ah_calculate_primary_s1g_channel_loc(
	int prim_cent_freq, int op_chan_centre_freq, int op_bw_mhz);

int morse_dot11ah_channel_set_map(const char *alpha);

int morse_update_reg_rules_to_country_ie(struct dot11ah_country_ie *country_ie,
						int start_chan, int end_chan, int eirp, int reg_rule);

const struct morse_dot11ah_channel
	*morse_dot11ah_channel_get_s1g(struct ieee80211_channel *chan_5g);

const struct morse_dot11ah_channel
	*morse_dot11ah_channel_chandef_to_s1g(struct cfg80211_chan_def *chan_5g);

int morse_dot11ah_s1g_chan_to_5g_chan(int chan_s1g);

u32 morse_dot11ah_channel_get_flags(int chan_s1g);

struct morse_dot11ah_cssid_item *morse_dot11ah_find_cssid(u32 cssid);

struct morse_dot11ah_cssid_item *morse_dot11ah_find_bssid(u8 bssid[ETH_ALEN]);

void morse_dot11ah_store_cssid(
	u32 cssid, int length, const u8 *ssid, u16 capab_info, u8 *s1g_ies, int s1g_ies_len, u8 *bssid);

int morse_dot11ah_parse_ies(const u8 *start, size_t len, struct dot11ah_ies_mask *ies_mask);

const u8 *morse_dot11_find_ie(u8 eid, const u8 *ies, int length);

u8 *morse_dot11_insert_ie(u8 *dst, const u8 *src, u8 eid, u8 len);

u8 *morse_dot11_insert_ie_no_header(u8 *dst, const u8 *src, u8 len);

const struct morse_reg_rule *morse_regdom_get_rule_for_freq(const char *alpha, int frequency);

struct ieee80211_regdomain *morse_regdom_to_ieee80211(const struct morse_regdomain *morse_domain);

const struct morse_regdomain *morse_reg_set_alpha(const char *alpha);

int morse_dot11ah_freq_khz_bw_mhz_to_chan(u32 freq, u8 bw);

int morse_dot11ah_channel_to_freq_khz(int chan);

/**
 * morse_dot11ah_s1g_chan_bw_to_5g_chan() - Lookup the HT/VHT channel mapped to an S1G channel.
 *
 * @arg chan_s1g the S1G channel to look up
 * @arg bw_mhz  the width of the S1G channel
 *
 * Return: the HT/VHT channel or error if no map entry found.
 */
int morse_dot11ah_s1g_chan_bw_to_5g_chan(int chan_s1g, int bw_mhz);

/**
 *  morse_dot11ah_s1g_op_chan_pri_chan_to_5g() - Lookup the HT/VHT channel of an
 *                                S1G primary channel given an operating channel.
 *        in which it is configured.
 *
 * @arg s1g_op_chan The BSS operating channel
 * @arg s1g_pri_chan The BSS primary channel
 *
 * Return: the HT/VHT channel or error if no map entry found.
 */
int morse_dot11ah_s1g_op_chan_pri_chan_to_5g(int s1g_op_chan, int s1g_pri_chan);

/**
 * morse_dot11ah_get_pri_1mhz_chan() - Get the 1MHz Primary channel for a BSS configuration.
 *
 * @arg primary_channel The BSS primary channel (can be 1 or 2MHz wide)
 * @arg primary_channel_width_mhz The width of the primary channel
 * @arg pri_1_mhz_loc_upper The location of the 1MHz primary within the 2MHz primary,
 *                            specified in the BSS S1G operating element
 *
 * Return: The primary channel or error if no valid channel possible
 */
int morse_dot11ah_get_pri_1mhz_chan(int primary_channel, int primary_channel_width_mhz,
			    bool pri_1_mhz_loc_upper);

const char *morse_dot11ah_get_region_str(void);

int morse_dot11ah_get_num_channels(void);

const struct morse_regdomain *morse_reg_alpha_lookup(const char *alpha);

/**
 * @brief Fill out a channel list with the available channels for the currently configured country
 *
 * @param list pointer to \ref struct morse_channel array to store available channels
 * @return Number of channels available, or negative number on error
 */
int morse_dot11ah_fill_channel_list(struct morse_channel *list);

void morse_dot11ah_debug_init(u32 mask);

u32 morse_dot11ah_s1g_chan_to_s1g_freq(int chan_s1g);
u16 morse_dot11ah_5g_chan_to_s1g_ch(u8 chan_5g, u8 op_class);

int morse_mac_set_country_info_from_regdom(const struct morse_regdomain *morse_domain,
					struct s1g_operation_parameters *params,
					struct dot11ah_country_ie *country_ie);
#endif  /* !_MORSE_DOT11AH_H_ */
