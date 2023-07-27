#ifndef _IEEE80211_H_
#define _IEEE80211_H_

#include <linux/version.h>

#ifdef MAC80211_BACKPORT_VERSION_CODE
#define MAC80211_VERSION_CODE MAC80211_BACKPORT_VERSION_CODE
#else
#define MAC80211_VERSION_CODE LINUX_VERSION_CODE
#endif

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

/*
 * This should include all modified/missing bits from ieee80211
 */

/* EDCA IE and access category parameters */
struct __ieee80211_edca_ac_rec {
	u8 aifsn;
	u8 ecw_min_max;
	u16 txop_limit;
} __packed;

struct __ieee80211_edca_ie {
	u8 wme_qos_info;
	u8 update_edca_info;
	struct __ieee80211_edca_ac_rec ac_be;
	struct __ieee80211_edca_ac_rec ac_bk;
	struct __ieee80211_edca_ac_rec ac_vi;
	struct __ieee80211_edca_ac_rec ac_vo;
} __packed;

/**
 * VENDOR IE data format for easy type casting
 */
struct __ieee80211_vendor_ie_elem {
	u8 oui[3];
	u8 oui_type;
	u8 oui_sub_type;
	u8 attr[];
} __packed;

#define IS_WMM_IE(ven_ie) (ven_ie->oui[0] == 0x00 && \
							ven_ie->oui[1] == 0x50 && \
							ven_ie->oui[2] == 0xf2 && \
							ven_ie->oui_type == 2 && \
							ven_ie->oui_sub_type == 1)

/* Following values are as per S1G Operation IE channel width subfields (Table 10-32 in 802.11-2020) */
#define S1G_CHAN_1MHZ 0
#define S1G_CHAN_2MHZ 1
#define S1G_CHAN_4MHZ 3
#define S1G_CHAN_8MHZ 7
#define S1G_CHAN_16MHZ 15

/*
 * These structures are already in K5.10, lets use them
 */
#if KERNEL_VERSION(5, 10, 11) > MAC80211_VERSION_CODE

#define IEEE80211_STYPE_S1G_BEACON	0x0010

#define IEEE80211_S1G_BCN_NEXT_TBTT	0x100

/* convert frequencies */
#define MHZ_TO_KHZ(freq) ((freq) * 1000)
#define KHZ_TO_MHZ(freq) ((freq) / 1000)

/**
 * struct ieee80211_channel - channel definition
 *
 * The new ieee80211_channel from K5.10 supporting S1G.
 */
struct ieee80211_channel_s1g {
	enum nl80211_band band;
	u32 center_freq;
	u16 freq_offset;
	u16 hw_value;
	u32 flags;
	int max_antenna_gain;
	int max_power;
	int max_reg_power;
	bool beacon_found;
	u32 orig_flags;
	int orig_mag, orig_mpwr;
	enum nl80211_dfs_state dfs_state;
	unsigned long dfs_state_entered;
	unsigned int dfs_cac_ms;
};

struct ieee80211_ext {
	__le16 frame_control;
	__le16 duration;
	union {
		struct {
			u8 sa[ETH_ALEN];
			__le32 timestamp;
			u8 change_seq;
			u8 variable[0];
		} __packed s1g_beacon;
		struct {
			u8 sa[ETH_ALEN];
			__le32 timestamp;
			u8 change_seq;
			u8 next_tbtt[3];
			u8 variable[0];
		} __packed s1g_short_beacon;
	} u;
} __packed __aligned(2);

/**
 * enum nl80211_band - Frequency band
 * @__NL80211_BAND_XXX: Place holder for other bands (already defined in nl80211.h)
 * @NL80211_BAND_S1GHZ: around 900MHz, supported by S1G PHYs
 * @NUM_NL80211_BANDS: number of bands, avoid using this in userspace
 *	since newer kernel versions may support more bands
 */
enum nl80211_band_s1g {
	__NL80211_BAND_2GHZ,
	__NL80211_BAND_5GHZ,
	__NL80211_BAND_60GHZ,
	__NL80211_BAND_6GHZ,
	NL80211_BAND_S1GHZ,

	__NUM_NL80211_BANDS,
};


/**
 * enum morse_dot11ah_channel_flags - channel flags
 *
 * These are in Linux 5.10, should be disabled
 *
 * @IEEE80211_CHAN_1MHZ: 1 MHz bandwidth is permitted
 *	on this channel.
 * @IEEE80211_CHAN_2MHZ: 2 MHz bandwidth is permitted
 *	on this channel.
 * @IEEE80211_CHAN_4MHZ: 4 MHz bandwidth is permitted
 *	on this channel.
 * @IEEE80211_CHAN_8MHZ: 8 MHz bandwidth is permitted
 *	on this channel.
 * @IEEE80211_CHAN_16MHZ: 16 MHz bandwidth is permitted
 *	on this channel.
 *
 */
enum morse_dot11ah_channel_flags {
	IEEE80211_CHAN_1MHZ		= 1<<14,
	IEEE80211_CHAN_2MHZ		= 1<<15,
	IEEE80211_CHAN_4MHZ		= 1<<16,
	IEEE80211_CHAN_8MHZ		= 1<<17,
	IEEE80211_CHAN_16MHZ		= 1<<18,
};

/**
 * ieee80211_channel_to_khz - convert ieee80211_channel to frequency in KHz
 * @chan: struct ieee80211_channel to convert
 * Return: The corresponding frequency (in KHz)
 */
static inline u32
ieee80211_channel_to_khz(const struct ieee80211_channel_s1g *chan)
{
	return MHZ_TO_KHZ(chan->center_freq) + chan->freq_offset;
}

/**
 * ieee80211_is_ext - check if type is IEEE80211_FTYPE_EXT
 * @fc: frame control bytes in little-endian byteorder
 */
static inline int ieee80211_is_ext(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
		   cpu_to_le16(IEEE80211_FTYPE_EXT);
}

/**
 * ieee80211_is_s1g_beacon - check if type is S1G Beacon
 * @fc: frame control bytes in little-endian byteorder
 */
static inline int ieee80211_is_s1g_beacon(__le16 fc)
{
	return (ieee80211_is_ext(fc) &&
		((fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) ==
			cpu_to_le16(IEEE80211_STYPE_S1G_BEACON)));
}

/**
 * ieee80211_next_tbtt_present - check if IEEE80211_FTYPE_EXT &&
 * IEEE80211_STYPE_S1G_BEACON && IEEE80211_S1G_BCN_NEXT_TBTT
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_next_tbtt_present(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT | IEEE80211_STYPE_S1G_BEACON) &&
	       fc & cpu_to_le16(IEEE80211_S1G_BCN_NEXT_TBTT);
}

/**
 * ieee80211_is_s1g_short_beacon - check if next tbtt present bit is set. Only
 * true for S1G beacons when they're short.
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_s1g_short_beacon(__le16 fc)
{
	return ieee80211_is_s1g_beacon(fc) && ieee80211_next_tbtt_present(fc);
}

/**
 * ieee80211_channel_to_freq_khz - convert channel number to frequency
 * @chan: channel number
 * @band: band, necessary due to channel number overlap
 * Return: The corresponding frequency (in KHz), or 0 if the conversion failed.
 */
u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band_s1g band);

#else

#define ieee80211_channel_s1g ieee80211_channel

#endif

#if KERNEL_VERSION(5, 8, 0) > MAC80211_VERSION_CODE
struct ieee80211_s1g_cap {
	u8 capab_info[10];
	u8 supp_mcs_nss[5];
} __packed;
#endif

#if KERNEL_VERSION(4, 12, 0) > MAC80211_VERSION_CODE
struct ieee80211_bss_max_idle_period_ie {
	__le16 max_idle_period;
	u8 idle_options;
} __packed;
#endif

/*
 * This function need to be patched for Linux 5.10, so bring it here for now.
 */

/**
 * ieee80211_freq_khz_to_channel - convert frequency to channel number
 * @freq: center frequency in KHz
 * Return: The corresponding channel, or 0 if the conversion failed.
 */
int __ieee80211_freq_khz_to_channel(u32 freq);

/*
 * Below function is an extension. This needs to be patched on 5.10 kernel.
 * Kernel func in include/linux/ieee80211.h is checking only for the presence
 * of next TBTT bit in frame control, whereas compressed SSID is more acccurate
 * flag to check if it's short beacon. For now we will use below function.
 */
#define IEEE80211_FCTL_COMPR_SSID	0x0200
/**
 * ieee80211_is_s1g_short_beacon_local - check if type is S1G Beacon is short beacon
 * @fc: frame control bytes in little-endian byteorder
 */
static inline int ieee80211_is_s1g_short_beacon_local(__le16 fc)
{
	return (ieee80211_is_s1g_beacon(fc) &&
		((fc & cpu_to_le16(IEEE80211_FCTL_COMPR_SSID)) != 0));
}

#endif  /* !_IEEE80211_H_ */
