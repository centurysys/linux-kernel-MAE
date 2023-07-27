/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <asm/div64.h>

#include "morse.h"
#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "command.h"
#include "vendor.h"
#include "vendor_ie.h"
#include "mac_config.h"
#include "dot11ah/dot11ah.h"
#include "skb_header.h"
#include "ps.h"
#include "raw.h"
#include "twt.h"
#include "watchdog.h"
#include "firmware.h"
#include "offload.h"
#ifdef CONFIG_MORSE_RC
#include "rc.h"
#else
#include "minstrel_rc.h"
#endif
#include "ipmon.h"

#ifdef CONFIG_MORSE_HW_TRACE
#include "hw_trace.h"
#endif

#include "monitor.h"

#define RATE(rate100m, _flags) { \
	.bitrate = (rate100m), \
	.flags = (_flags), \
	.hw_value = 0, \
}

#define CHAN5GHZ(channel, chflags)  { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = 5000 + 5*(channel), \
	.hw_value = (channel), \
	.flags = chflags, \
	.max_antenna_gain = 0, \
	.max_power = 22, \
}

#define STA_PRIV_TIMEOUT_MSEC (2000)

/* Supported TX/RX MCS mask: 0xFF -> Each bit represents MCS0-7 */
#define DEFAULT_MCS_RATE_MASK (0xFF)

/* Max 32 for legacy BA. 8 for 1MHZ NDP BA. 16 for 2+MHZ NDP BA */
#define DOT11AH_BA_MAX_MPDU_PER_AMPDU	(32)

/* Default alpha-2 code */
#define USER_ASSIGNED_ALPHA "ZZ"

/** When automatically trying MCS0 before MCS10, this is how many MCS0 attempts to make */
#define MCS0_BEFORE_MCS10_COUNT		(1)

#if KERNEL_VERSION(4, 17, 0) > MAC80211_VERSION_CODE
/* Mask used to obtain the TID from QoS control. */
#define QOS_CTRL_TID_MASK (0x0F)
#endif

enum dot11ah_powersave_mode {
	POWERSAVE_MODE_DISABLED = 0x00,
	POWERSAVE_MODE_PROTOCOL_ENABLED = 0x01,
	POWERSAVE_MODE_FULLY_ENABLED = 0x02,
	POWERSAVE_MODE_UNKNOWN = 0xFF
};

enum morse_mac_mcs10_mode {
	MCS10_MODE_DISABLED	= 0x00,
	MCS10_MODE_FORCED	= 0x01,
	MCS10_MODE_AUTO		= 0x02
};

/* Custom Module parameters */
/* On chip hardware encryption can be disabled through modparam */
static uint no_hwcrypt;
module_param(no_hwcrypt, uint, 0644);
MODULE_PARM_DESC(no_hwcrypt, "Disable on-chip hardware encryption");

/* TX/RX MCS MASK. Default 0xFF mask limits max mcs to 7 on both tx/rx */
static uint mcs_mask __read_mostly = DEFAULT_MCS_RATE_MASK;
module_param(mcs_mask, uint, 0644);
MODULE_PARM_DESC(mcs_mask, "Supported MCS Mask, e.g. MCS0-2 use mask 0x07");

/**
 * Set the MCS10 configuration
 * 0 - MCS10 disabled
 * 1 - MCS10 replaces MCS0
 * 2 - Initally try MCS0 and then MCS10
 */
static enum morse_mac_mcs10_mode mcs10_mode __read_mostly = MCS10_MODE_DISABLED;
module_param(mcs10_mode, uint, 0644);
MODULE_PARM_DESC(mcs10_mode, "Set MCS10 mode");

/* Enable/Disable channel survey */
static bool enable_survey __read_mostly = ENABLE_SURVEY_DEFAULT;
module_param(enable_survey, bool, 0644);
MODULE_PARM_DESC(enable_survey, "Enable channel survey");

/* Enable/Disable Subband transmission */
static enum morse_mac_subbands_mode
	enable_subbands __read_mostly = SUBBANDS_MODE_ENABLED;
module_param(enable_subbands, uint, 0644);
MODULE_PARM_DESC(enable_subbands, "Enable Subband Transmission");

/* Enable/Disable Powersave */
static enum dot11ah_powersave_mode enable_ps __read_mostly = CONFIG_MORSE_POWERSAVE_MODE;
module_param(enable_ps, uint, 0644);
MODULE_PARM_DESC(enable_ps, "Enable PS");

/* Enable/Disable Powersave */
static bool enable_dynamic_ps_offload __read_mostly = true;
module_param(enable_dynamic_ps_offload, bool, 0644);
MODULE_PARM_DESC(enable_dynamic_ps_offload, "Enable dynamic PS fw offload");

/* Enable/Disable Coredump */
static bool enable_coredump __read_mostly = true;
module_param(enable_coredump, bool, 0644);
MODULE_PARM_DESC(enable_coredump, "Enable creating coredumps on FW failures");

/*
 * When set to a value greater than 0, Thin LMAC Mode is enabled.
 */
static uint32_t thin_lmac __read_mostly;
module_param(thin_lmac, uint, 0644);
MODULE_PARM_DESC(thin_lmac, "Thin LMAC mode");

/*
 * When set to a value greater than 0, Virtual Station Test Mode is enabled, allowing up to
 * virtual_sta_max virtual interfaces to be configured in STA mode for emulating multiple physical
 * stations.
 */
static uint32_t virtual_sta_max __read_mostly;
module_param(virtual_sta_max, uint, 0644);
MODULE_PARM_DESC(virtual_sta_max, "Virtual STA test mode (max virtual STAs or 0 to disable)");

/* Enable/disable Multi Interface (base for EasyMesh, 11s mesh, 11ah replay, 11ak bridging) mode */
static bool enable_multi_interface __read_mostly;
module_param(enable_multi_interface, bool, 0644);
MODULE_PARM_DESC(enable_multi_interface, "Enable/Disable Multi Interface (dual interface) Support");

/* Allow/Disallow rate control to use SGI */
static bool enable_sgi_rc __read_mostly = true;
module_param(enable_sgi_rc, bool, 0644);
MODULE_PARM_DESC(enable_sgi_rc, "Allow/Disallow rate control to use SGI");

/* Enable/Disable broadcasting travelling pilot support */
static bool enable_trav_pilot __read_mostly = true;
module_param(enable_trav_pilot, bool, 0644);
MODULE_PARM_DESC(enable_trav_pilot, "Enable travelling pilots");

/* Enable/Disable RTS/CTS for 8MHz (Disabled by default) */
static bool enable_rts_8mhz __read_mostly;
module_param(enable_rts_8mhz, bool, 0644);
MODULE_PARM_DESC(enable_rts_8mhz, "Enable RTS/CTS protection for 8MHz");

/* Use CTS-to-self in place of RTS-CTS */
static bool enable_cts_to_self __read_mostly;
module_param(enable_cts_to_self, bool, 0644);
MODULE_PARM_DESC(enable_cts_to_self, "Use CTS-to-self in place of RTS-CTS");

/* Parse the regulatory domain, 2 char ISO-Alpha2 */
static char country[MORSE_COUNTRY_LEN] = CONFIG_MORSE_COUNTRY;
module_param_string(country, country, sizeof(country), 0644);
MODULE_PARM_DESC(country,
"The ISO/IEC alpha2 country code for the country in which this device is currently operating.");

/* Enable/Disable watchdog support */
static bool enable_watchdog __read_mostly = ENABLE_WATCHDOG_DEFAULT;
module_param(enable_watchdog, bool, 0644);
MODULE_PARM_DESC(enable_watchdog, "Enable watchdog");

/* Set watchdog interval. User can update the watchdog interval in run time */
static int watchdog_interval_secs __read_mostly = 30;
module_param(watchdog_interval_secs, int, 0644);
MODULE_PARM_DESC(watchdog_interval_secs, "Set watchdog interval in seconds");

/* Enable/Disable watchdog reset */
static bool enable_watchdog_reset __read_mostly;
module_param(enable_watchdog_reset, bool, 0644);
MODULE_PARM_DESC(enable_watchdog_reset, "Enable driver reset from watchdog");

/* Set limit on rate chain: could be 1, 2, 3 or 4 */
static int max_rates __read_mostly = INIT_MAX_RATES_NUM;
module_param(max_rates, int, 0644);
MODULE_PARM_DESC(max_rates, "Maximum number of rates to try");

/* Set maximum rate attempts, could be 1, 2, 3 or 4 */
static int max_rate_tries __read_mostly = 1;
module_param(max_rate_tries, int, 0644);
MODULE_PARM_DESC(max_rate_tries, "Maximum retries per rate");

/* Set maximum aggregation count */
static uint max_aggregation_count __read_mostly;
module_param(max_aggregation_count, uint, 0644);
MODULE_PARM_DESC(max_aggregation_count, "Maximum number of aggregated packets we can receive");

/* Enable/Disable RAW */
static bool enable_raw __read_mostly = true;
module_param(enable_raw, bool, 0644);
MODULE_PARM_DESC(enable_raw, "Enable RAW");

/* Enable/Disable mac80211 pull interface for airtime fairness */
static bool enable_airtime_fairness __read_mostly;
module_param(enable_airtime_fairness, bool, 0644);
MODULE_PARM_DESC(enable_airtime_fairness, "Enable mac80211 pull interface for airtime fairness");

/* Enable/disable the mac802.11 connection monitor */
static bool enable_mac80211_connection_monitor __read_mostly;
module_param(enable_mac80211_connection_monitor, bool, 0644);
MODULE_PARM_DESC(enable_mac80211_connection_monitor, "Enable mac80211 connection monitor");

/* Enable/disable twt feature */
static bool enable_twt __read_mostly = true;
module_param(enable_twt, bool, 0644);
MODULE_PARM_DESC(enable_twt, "Enable TWT support");

/**
 * Maximum TX power
 * TODO dynamically retrieve from chip.
 */
static int max_power_level __read_mostly = 22;
module_param(max_power_level, int, 0644);
MODULE_PARM_DESC(max_power_level, "Maximum transmitted power");

/* Set maximum multicast frames after DTIM (0 - Do not limit) */
static uint max_mc_frames __read_mostly = MORSE_MAX_MC_FRAMES_AFTER_DTIM;
module_param(max_mc_frames, uint, 0644);
MODULE_PARM_DESC(max_mc_frames, "Set maximum multicast frames after DTIM (0 for unlimited)");

/* Enable CAC (Call Authentication Control) (AP mode only) */
static uint enable_cac __read_mostly;
module_param(enable_cac, uint, 0644);
MODULE_PARM_DESC(enable_cac, "Enable Call Authentication Control (CAC)");

/* Enable Monitoring of Beacon Change Seq (STA mode only) */
static uint enable_bcn_change_seq_monitor __read_mostly;
module_param(enable_bcn_change_seq_monitor, uint, 0644);
MODULE_PARM_DESC(enable_bcn_change_seq_monitor, "Enable Monitoring of Beacon Change Sequence");

/* Enable/Disable FW ARP response offloading */
static bool enable_arp_offload __read_mostly = ENABLE_ARP_OFFLOAD_DEFAULT;
module_param(enable_arp_offload, bool, 0644);
MODULE_PARM_DESC(enable_arp_offload, "Enable ARP offload");

static bool enable_dhcpc_offload __read_mostly = ENABLE_DHCP_OFFLOAD_DEFAULT;
module_param(enable_dhcpc_offload, bool, 0644);
MODULE_PARM_DESC(enable_dhcpc_offload, "Enable DHCP client offload");

/* Enable/Disable FW IBSS Probe Req Filtering */
bool enable_ibss_probe_filtering __read_mostly = true;
module_param(enable_ibss_probe_filtering, bool, 0644);
MODULE_PARM_DESC(enable_ibss_probe_filtering, "Enable Probe Req Filtering in FW");

char dhcpc_lease_update_script[DHCPC_LEASE_UPDATE_SCRIPT_NAME_SIZE_MAX] =
			"/morse/scripts/dhcpc_update.sh";
module_param_string(dhcpc_lease_update_script, dhcpc_lease_update_script,
			sizeof(dhcpc_lease_update_script), 0644);
MODULE_PARM_DESC(dhcpc_lease_update_script, "Path to script called on DHCP lease updates");

/* Enable/Disable automatic duty cycle based on regulatory domain */
static bool enable_auto_duty_cycle __read_mostly = true;
module_param(enable_auto_duty_cycle, bool, 0644);
MODULE_PARM_DESC(enable_auto_duty_cycle, "Enable automatic duty cycling setting");

/* Enable/Disable automatic minimum packet spacing configuration based on regulatory domain */
static bool enable_auto_mpsw __read_mostly = true;
module_param(enable_auto_mpsw, bool, 0644);
MODULE_PARM_DESC(enable_auto_mpsw, "Enable automatic minimum packet spacing window setting");

static struct ieee80211_channel mors_5ghz_channels[] = {
	/* UNII-1 */
	CHAN5GHZ(36, 0),
	CHAN5GHZ(40, 0),
	CHAN5GHZ(44, 0),
	CHAN5GHZ(48, 0),
	/* UNII-2 */
	CHAN5GHZ(52, 0),
	CHAN5GHZ(56, 0),
	CHAN5GHZ(60, 0),
	CHAN5GHZ(64, 0),
	CHAN5GHZ(100, 0),
	CHAN5GHZ(104, 0),
	CHAN5GHZ(108, 0),
	CHAN5GHZ(112, 0),
	CHAN5GHZ(116, 0),
	CHAN5GHZ(120, 0),
	CHAN5GHZ(124, 0),
	CHAN5GHZ(128, 0),
	CHAN5GHZ(132, 0),
	CHAN5GHZ(136, 0),
	/* UNII-3 */
	CHAN5GHZ(149, 0),
	CHAN5GHZ(153, 0),
	CHAN5GHZ(157, 0),
	CHAN5GHZ(161, 0),
	CHAN5GHZ(165, 0),
	CHAN5GHZ(169, 0),
	CHAN5GHZ(173, 0),
	CHAN5GHZ(177, 0),

	/* 40MHz mapping */
	/* UNII-1 */
	CHAN5GHZ(38, 0),
	CHAN5GHZ(46, 0),
	/* UNII-2 */
	CHAN5GHZ(54, 0),
	CHAN5GHZ(62, 0),
	CHAN5GHZ(102, 0),
	CHAN5GHZ(110, 0),
	CHAN5GHZ(118, 0),
	CHAN5GHZ(126, 0),
	CHAN5GHZ(134, 0),
	CHAN5GHZ(151, 0),
	CHAN5GHZ(159, 0),
	CHAN5GHZ(167, 0),
	CHAN5GHZ(175, 0),

	/* 80MHz mapping */
	CHAN5GHZ(42, 0),
	CHAN5GHZ(58, 0),
	CHAN5GHZ(106, 0),
	CHAN5GHZ(122, 0),
	CHAN5GHZ(155, 0),
	CHAN5GHZ(171, 0),

	/* 160MHz mapping */
	CHAN5GHZ(50, 0),
	CHAN5GHZ(114, 0),
	CHAN5GHZ(163, 0),
};

static struct ieee80211_rate mors_2ghz_rates[] = {
	RATE(5, 0),
	RATE(10, 0),	/* 0x02 = 1.0 Mbps, basic rates for 2.4Ghz */
	RATE(15, 0),
	RATE(20, 0),	/* 0x04 = 2.0 Mbps, basic rates for 2.4Ghz */
	RATE(25, 0),
	RATE(30, 0),
	RATE(35, 0),
	RATE(40, 0),
	RATE(45, 0),
	RATE(55, 0),	/* 0x0B = 5.5 Mbps, basic rates for 2.4Ghz */
	RATE(60, 0),	/* 0x0C = 6.0 Mbps, basic rates for 5Ghz */
	RATE(65, 0),
	RATE(70, 0),
	RATE(85, 0),
	RATE(90, 0),
	RATE(110, 0),	/* 0x16 = 11.0 Mbps, basic rates for 2.4Ghz */
	RATE(120, 0),	/* 0x18 = 12.0 Mbps, basic rates for 5Ghz */
	RATE(125, 0),
	RATE(135, 0),
	RATE(150, 0),
	RATE(180, 0),
	RATE(240, 0)	/* 0x30 = 24.0 Mbps, basic rates for 5Ghz */
};

static struct ieee80211_supported_band mors_band_5ghz = {
	.band = NL80211_BAND_5GHZ,
	.channels = mors_5ghz_channels,
	.n_channels = ARRAY_SIZE(mors_5ghz_channels),
	.bitrates = mors_2ghz_rates,
	.n_bitrates = ARRAY_SIZE(mors_2ghz_rates),
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_GRN_FLD |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT) |
			IEEE80211_HT_CAP_MAX_AMSDU,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = DEFAULT_MCS_RATE_MASK,
			.rx_highest = __cpu_to_le16(0x41),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
	.vht_cap = {
		.vht_mcs = {
			.rx_highest = __cpu_to_le16(0x41),
		},
	},

};

bool is_thin_lmac_mode(void)
{
	return (thin_lmac > 0);
}

bool is_virtual_sta_test_mode(void)
{
	return (virtual_sta_max > 0);
}

static inline int morse_vif_max_tx_bw(struct morse_vif *mors_vif)
{
	int max_bw_mhz;
	struct morse_caps *capabs = &mors_vif->capabilities;

	max_bw_mhz = MORSE_CAPAB_SUPPORTED(capabs, 8MHZ) ? 8 :
		     MORSE_CAPAB_SUPPORTED(capabs, 4MHZ) ? 4 :
		     MORSE_CAPAB_SUPPORTED(capabs, 2MHZ) ? 2 : 1;

	return max_bw_mhz;
}

bool is_multi_interface_mode(void)
{
	return enable_multi_interface;
}

struct ieee80211_vif *morse_get_vif(struct morse *mors)
{
	return mors->vif[0];
}

struct ieee80211_vif *morse_get_ap_vif(struct morse *mors)
{
	int vif_id = 0;

	for (vif_id = 0; vif_id < MORSE_MAX_IF; vif_id++) {
		if ((mors->vif[vif_id] != NULL) &&
			((mors->vif[vif_id]->type == NL80211_IFTYPE_AP) ||
			(mors->vif[vif_id]->type == NL80211_IFTYPE_ADHOC)))
			return mors->vif[vif_id];
	}
	return NULL;
}

struct ieee80211_vif *morse_get_sta_vif(struct morse *mors)
{
	int vif_id = 0;

	for (vif_id = 0; vif_id < MORSE_MAX_IF; vif_id++) {
		if ((mors->vif[vif_id] != NULL) && (mors->vif[vif_id]->type == NL80211_IFTYPE_STATION))
			return mors->vif[vif_id];
	}
	return NULL;
}

#if KERNEL_VERSION(4, 12, 0) <= MAC80211_VERSION_CODE
static u8 morse_mac_rx_bw_to_skb_ht(struct morse *mors, u8 rx_bw_mhz)
{
	struct ieee80211_conf *conf = &mors->hw->conf;
	int op_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;

	/* Can't do subbands for channel does not support HT40 (width != 40MHz) */
	if (conf->chandef.width != NL80211_CHAN_WIDTH_40)
		return RATE_INFO_BW_20;

	switch (op_bw_mhz) {
	case 1:
		return RATE_INFO_BW_40;
	case 2:
		if (rx_bw_mhz <= 1)
			return RATE_INFO_BW_20;
		else
			return RATE_INFO_BW_40;
	case 4:
		if (rx_bw_mhz <= 2)
			return RATE_INFO_BW_20;
		else
			return RATE_INFO_BW_40;
	case 8:
		if (rx_bw_mhz <= 4)
			return RATE_INFO_BW_20;
		else
			return RATE_INFO_BW_40;
	default:
		return RATE_INFO_BW_40;
	}
}

static u8 morse_mac_rx_bw_to_skb_vht(struct morse *mors, u8 rx_bw_mhz)
{
	struct ieee80211_conf *conf = &mors->hw->conf;
	int op_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;

	/* Can't do 3 x subbands for channel does not support VHT80 or VHT160 */
	if ((conf->chandef.width != NL80211_CHAN_WIDTH_80) &&
	    (conf->chandef.width != NL80211_CHAN_WIDTH_160))
		return RATE_INFO_BW_20;

	if (op_bw_mhz < 4)
		return RATE_INFO_BW_20;

	switch (op_bw_mhz) {
	case 4:
		if (rx_bw_mhz == 1)
			return RATE_INFO_BW_20;
		if (rx_bw_mhz == 2)
			return RATE_INFO_BW_40;
		else
			return RATE_INFO_BW_80;
	case 8:
		if (rx_bw_mhz <= 2)
			return RATE_INFO_BW_20;
		if (rx_bw_mhz == 4)
			return RATE_INFO_BW_40;
		else
			return RATE_INFO_BW_80;
	default:
		return RATE_INFO_BW_80;
	}
}
#endif


static void morse_mac_apply_mcs10(struct morse *mors,
				struct morse_skb_tx_info *tx_info)
{
	u8 i, j;

	switch (mcs10_mode) {
	case MCS10_MODE_DISABLED:
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++)
			if ((tx_info->rates[i].flags & MORSE_SKB_RATE_FLAGS_1MHZ) &&
			    (tx_info->rates[i].mcs == 0))
				mors->debug.mcs_stats_tbl.mcs0.tx_count += tx_info->rates[i].count;

		return;

	case MCS10_MODE_FORCED:
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			if ((tx_info->rates[i].flags & MORSE_SKB_RATE_FLAGS_1MHZ) &&
			    (tx_info->rates[i].mcs == 0)) {
				tx_info->rates[i].mcs = 10;

				/* Update our statistics. */
				mors->debug.mcs_stats_tbl.mcs10.tx_count += tx_info->rates[i].count;
			}
		}
		return;

	case MCS10_MODE_AUTO:
	{
		int mcs0_first_idx = -1;
		int mcs0_last_idx = -1;

		/* Find out where our first and last MCS0 entries are. */
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			if (tx_info->rates[i].flags & MORSE_SKB_RATE_FLAGS_1MHZ) {
				mcs0_last_idx = i;
				if (mcs0_first_idx == -1)
					mcs0_first_idx = i;
			}
			/* If the MCS or count is -1 then we are at the end of the table. Break to
			 * allow us to reuse i indicating the end of the table.
			 */
			if (tx_info->rates[i].mcs == -1)
				break;
		}

		/* If there aren't any MCS0 (at 1MHz) entries we are done. */
		if (mcs0_first_idx < 0)
			return;

		/*
		 * If we are in MCS10_MODE_AUTO add MCS10 counts to the table if they will fit.
		 * There should be three cases:
		 *	- There is one MSC0 entry and the table is full -> do nothing
		 *	- There is one MSC0 entry and the table has space -> adjust MSC0 down and add MCS 10
		 *	- There are multiple MCS0 entries -> replace entries after the first with MCS 10
		 */
		/* Case 3 - replace addtional entries. */
		if (mcs0_last_idx > mcs0_first_idx) {
			mors->debug.mcs_stats_tbl.mcs0.tx_count +=
				tx_info->rates[mcs0_first_idx].count;

			for (j = mcs0_first_idx + 1; j < i; j++) {
				if ((tx_info->rates[j].mcs == 0) && (tx_info->rates[j].flags & MORSE_SKB_RATE_FLAGS_1MHZ)) {
					tx_info->rates[j].mcs = 10;
					mors->debug.mcs_stats_tbl.mcs10.tx_count +=
						tx_info->rates[j].count;
				}
			}
		/* Case 2 - add additional MCS10 entry. */
		} else if ((mcs0_last_idx == mcs0_first_idx) && (i < (IEEE80211_TX_MAX_RATES))) {
			int pre_mcs10_mcs0_count =
				min_t(u8, tx_info->rates[mcs0_last_idx].count,
					MCS0_BEFORE_MCS10_COUNT);
			int mcs10_count =
				tx_info->rates[mcs0_last_idx].count - pre_mcs10_mcs0_count;

			/* If there were less retries than our desired minimum MCS0 we don't add
			 * MCS10 retries.
			 */
			if (mcs10_count > 0) {
				/* Use the same flags for MCS10 as MCS0. */
				tx_info->rates[i].flags = tx_info->rates[mcs0_last_idx].flags;
				tx_info->rates[mcs0_last_idx].count = pre_mcs10_mcs0_count;
				tx_info->rates[i].count = mcs10_count;
			}
			/* Update our statistics. */
			mors->debug.mcs_stats_tbl.mcs10.tx_count += mcs10_count;
			mors->debug.mcs_stats_tbl.mcs0.tx_count += pre_mcs10_mcs0_count;
		/* Case 1 full table - increment MCS0 count. */
		} else {
			for (j = mcs0_first_idx; j < IEEE80211_TX_MAX_RATES; j++) {
				if (tx_info->rates[j].mcs == 0) {
					mors->debug.mcs_stats_tbl.mcs0.tx_count +=
						tx_info->rates[j].count;
				}
			}
		}
	}
	}
}

bool morse_mac_is_subband_enable(void)
{
	return (enable_subbands == SUBBANDS_MODE_ENABLED);
}

int morse_mac_get_max_rate_tries(void)
{
	return max_rate_tries;
}

int morse_mac_get_max_rate(void)
{
	return max_rates;
}

static void morse_mac_fill_tx_info(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct morse_vif *mors_if,
				int tx_bw_mhz,
				struct ieee80211_sta *sta)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct morse_sta *mors_sta = NULL;
	int op_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;
	int i;
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	bool rts_allowed = op_bw_mhz < 8 || enable_rts_8mhz; /* Disable RTS/CTS for 8MHz for now */

	if (sta != NULL)
		mors_sta = (struct morse_sta *)sta->drv_priv;

#ifdef CONFIG_MORSE_RC
	/* Include FCS length */
	rts_allowed &= ((skb->len + FCS_LEN) > mors->rts_threshold);
#else
	rts_allowed &= info->control.use_rts;
#endif

	morse_rc_sta_fill_tx_rates(mors, tx_info, skb, sta, tx_bw_mhz, rts_allowed);

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		/* SW-3200: WAR to prevent firmware crash when
		 * RTS/CTS is attempted to be sent at 4MHz
		 */
		if (rts_allowed
			/* || (info->control.flags & IEEE80211_TX_RC_USE_RTS_CTS)) */
			) {
			enum morse_skb_rate_flags flag = enable_cts_to_self ?
				MORSE_SKB_RATE_FLAGS_CTS :
				MORSE_SKB_RATE_FLAGS_RTS;

			tx_info->rates[i].flags |= cpu_to_le16(flag);
		}

		if (mors_if->ctrl_resp_in_1mhz_en)
			tx_info->rates[i].flags |=
				cpu_to_le32(MORSE_SKB_RATE_FLAGS_CTRL_RESP_1MHZ);

		/* If travelling pilot reception is supported always use it */
		if (mors_sta != NULL && enable_trav_pilot &&
		    (mors_sta->trav_pilot_support == TRAV_PILOT_RX_1NSS ||
		    mors_sta->trav_pilot_support == TRAV_PILOT_RX_1_2_NSS))
			tx_info->rates[i].flags |=
				cpu_to_le32(MORSE_SKB_RATE_FLAGS_USE_TRAV_PILOT);

		if (info->control.rates[i].flags & IEEE80211_TX_RC_SHORT_GI)
			tx_info->rates[i].flags |=
				cpu_to_le16(MORSE_SKB_RATE_FLAGS_SGI);
	}

	/* Apply change of MCS0 to MCS10 if required. */
	morse_mac_apply_mcs10(mors, tx_info);

	tx_info->flags |=
		cpu_to_le32(MORSE_TX_CONF_FLAGS_VIF_ID_SET(mors_if->id));

	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		tx_info->flags |= cpu_to_le32(MORSE_TX_CONF_FLAGS_CTL_AMPDU);

	if (info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER) {
		tx_info->flags |= cpu_to_le32(MORSE_TX_CONF_NO_PS_BUFFER);

		if (info->flags & IEEE80211_TX_STATUS_EOSP)
			tx_info->flags |= cpu_to_le32(MORSE_TX_CONF_FLAGS_IMMEDIATE_REPORT);
	}

	if (info->control.hw_key) {
		tx_info->flags |= cpu_to_le32(MORSE_TX_CONF_FLAGS_HW_ENCRYPT);

		tx_info->flags |= cpu_to_le32(
			MORSE_TX_CONF_FLAGS_KEY_IDX_SET(
				info->control.hw_key->hw_key_idx));
	}

	tx_info->tid = tid;
	if (mors_sta) {
		tx_info->tid_params = mors_sta->tid_params[tid];

		if (info->flags & IEEE80211_TX_CTL_CLEAR_PS_FILT) {
			if (mors_sta->tx_ps_filter_en)
				morse_dbg(mors, "TX ps filter cleared sta[%pM]\n", mors_sta->addr);
			mors_sta->tx_ps_filter_en = false;
		}
	}
}

static bool morse_mac_tx_ps_filtered_for_sta(struct morse *mors,
	struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct morse_sta *mors_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (!sta)
		return false;

	mors_sta = (struct morse_sta *)sta->drv_priv;

	if (!mors_sta->tx_ps_filter_en)
		return false;

	morse_dbg(mors, "Frame for sta[%pM] PS filtered\n", mors_sta->addr);
	mors->debug.page_stats.tx_ps_filtered++;

	info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	info->flags &= ~IEEE80211_TX_CTL_AMPDU;

	ieee80211_tx_status(mors->hw, skb);
	return true;
}

static void morse_mac_skb_free(struct morse *mors, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

int morse_mac_pkt_to_s1g(struct morse *mors, struct sk_buff *skb,
	int *tx_bw_mhz)
{
	int ret = 0;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int s1g_length;
	int vendor_ie_length = 0;
	struct dot11ah_ies_mask *ies_mask = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif != NULL ? info->control.vif : morse_get_vif(mors);
	struct morse_vif *mors_vif;
	struct morse_twt_event *twt_tx = NULL;
	bool is_assoc_req = false;
	bool is_assoc_resp = false;
	int twt_ie_size = 0;

	/* we only need the ies_mask if this is a management frame */
	if (ieee80211_is_mgmt(hdr->frame_control)) {
		ies_mask = morse_dot11ah_ies_mask_alloc();
		vendor_ie_length = morse_vendor_get_ie_len_for_pkt(skb,
			MORSE_VENDOR_IE_CAPS_OPS_OUI_TYPE);

		if (ies_mask == NULL) {
			ret = -ENOMEM;
			goto exit;
		}

		if (ieee80211_is_assoc_req(hdr->frame_control) ||
		    ieee80211_is_reassoc_req(hdr->frame_control))
			is_assoc_req = true;

		if (ieee80211_is_assoc_resp(hdr->frame_control) ||
		    ieee80211_is_reassoc_resp(hdr->frame_control))
			is_assoc_resp = true;
	}

	/* Check if the S1G frame is a different size, and
	 * if it is, ensure the space is correct
	 */
	if (vif != NULL) {
		s1g_length = morse_dot11ah_11n_to_s1g_tx_packet_size(vif, skb, false, ies_mask);
	} else {
		morse_dbg(mors, "NULL VIF\n");
		ret = -EINVAL;
		goto exit;
	}

	s1g_length += vendor_ie_length;

	mors_vif = ieee80211_vif_to_morse_vif(vif);
	if (is_assoc_resp) {
		twt_tx = morse_twt_peek_tx(mors, mors_vif, hdr->addr1);
		if (twt_tx) {
			twt_ie_size = morse_twt_get_ie_size(mors, twt_tx);
			morse_dbg(mors, "TWT IE size: %d\n", twt_ie_size);
			morse_twt_dump_event(mors, twt_tx);
		} else {
			morse_dbg(mors, "No TWT IEs for TX available\n");
		}
	}

	/* Send setup command TWT IE if available and an association request. */
	if (is_assoc_req && mors_vif->twt.req_event_tx) {
		twt_tx = (struct morse_twt_event *)mors_vif->twt.req_event_tx;
		twt_ie_size = morse_twt_get_ie_size(mors, twt_tx);
		morse_dbg(mors, "TWT IE size: %d\n", twt_ie_size);
		morse_twt_dump_event(mors, twt_tx);
	}

	if (twt_ie_size > 0)
		s1g_length += twt_ie_size + 2;

	if (s1g_length < 0) {
		morse_dbg(mors, "tx packet size < 0\n");
		ret = -EINVAL;
		goto exit;
	}

	if (skb->len + skb_tailroom(skb) < s1g_length) {
		struct sk_buff *skb2;

		/* skb_copy_expand() could fail on mem alloc. */
		skb2 = skb_copy_expand(skb, skb_headroom(skb),
				       s1g_length - skb->len,
				       GFP_KERNEL);
		morse_mac_skb_free(mors, skb);
		skb = skb2;

		if (skb == NULL) {
			ret = -ENOMEM;
			goto exit;
		}
	}

	if (twt_ie_size > 0) {
		skb = morse_twt_insert_ie(mors, twt_tx, skb, twt_ie_size);
		if (is_assoc_resp && morse_twt_dequeue_tx(mors, mors_vif, twt_tx))
			morse_warn_ratelimited(mors, "%s: Unable to dequeue TWT tx\n", __func__);
	}

	if (vendor_ie_length > 0)
		skb = morse_vendor_insert_caps_ops_ie(mors, skb);

	morse_dot11ah_ies_mask_clear(ies_mask);

	morse_dot11ah_11n_to_s1g_tx_packet(vif, skb, s1g_length, false, ies_mask);

	/*
	 * For almost all frames - default to sending at operating
	 * bandwidth. Rate control algorithms may later want to tweak this.
	 */
	*tx_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;

	if (mors->enable_subbands == SUBBANDS_MODE_DISABLED)
		goto exit;

	if (ieee80211_is_mgmt(hdr->frame_control)) {
		/* default all management frames to go out at current primary channel */
		*tx_bw_mhz = mors->custom_configs.channel_info.pri_bw_mhz;

		if (ieee80211_is_probe_resp(hdr->frame_control)) {
			/*
			 * TODO: Probe responses should be sent in the channel bandwidth of the
			 * probe request that elicits the response, but defaulting to send at 1 MHz
			 * in the 1 MHz primary channel will be ok.
			 */
			*tx_bw_mhz = 1;
		} else {
			struct morse_channel_info info;
			struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

			/* if bssid entry found (i.e. AP channel info), use that instead */
			if (morse_mac_find_channel_info_for_bssid(mgmt->bssid, &info))
				*tx_bw_mhz = info.pri_bw_mhz;
		}
	}

exit:
	morse_dot11ah_ies_mask_free(ies_mask);
	return ret;
}

static void
morse_aggr_check(struct morse_vif *mors_vif, struct ieee80211_sta *pubsta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct morse_sta *mors_sta = (struct morse_sta *)pubsta->drv_priv;
#if KERNEL_VERSION(4, 17, 0) > MAC80211_VERSION_CODE
	u8 *qos_ctrl = ieee80211_get_qos_ctl(hdr);
#endif
	u16 tid;

	if (mors_vif->custom_configs == NULL)
		return;

	if (!mors_vif->custom_configs->enable_ampdu)
		return;

	if (!mors_sta->ampdu_supported)
		return;

	if (mors_sta->state < IEEE80211_STA_AUTHORIZED)
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	if (unlikely(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

#if KERNEL_VERSION(4, 17, 0) > MAC80211_VERSION_CODE
	tid = qos_ctrl[0] & QOS_CTRL_TID_MASK;
#else
	tid = ieee80211_get_tid(hdr);
#endif

	if (mors_sta->tid_tx[tid] || mors_sta->tid_start_tx[tid])
		return;

	mors_sta->tid_start_tx[tid] = true;

	ieee80211_start_tx_ba_session(pubsta, tid, 0);
}


void morse_mac_schedule_probe_req(struct morse *mors)
{
	struct ieee80211_vif *vif = morse_get_vif(mors);
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;

	if (vif->type == NL80211_IFTYPE_STATION &&
		mors_if->is_sta_assoc) {
		tasklet_schedule(&mors->send_probe_req);
		morse_dbg(mors, "QoS NULL frame Tx completed! Scheduled to a send probe req\n");
		mors_if->waiting_for_probe_req_sched = false;
	}
}

static void morse_mac_ops_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct morse *mors = hw->priv;
	struct morse_skbq *mq;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct morse_vif *mors_if;
	struct morse_skb_tx_info tx_info = {0};
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool is_mgmt = (hdr && ieee80211_is_mgmt(hdr->frame_control));
	const int op_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;
	int tx_bw_mhz = op_bw_mhz;
	struct ieee80211_sta *sta = NULL;
	struct morse_sta *mors_sta = NULL;
	int vif_max_bw_mhz;
	int sta_max_bw_mhz = 0;

	if (info && info->control.vif)
		mors_if = (struct morse_vif *)info->control.vif->drv_priv;
	else
		mors_if = (struct morse_vif *)morse_get_vif(mors)->drv_priv;

	if (control != NULL)
		sta = control->sta;

	/* If we have a station, retrieve station-specific tx info */
	if (sta != NULL) {
#ifdef CONFIG_MORSE_IPMON
		static uint64_t time_start;

		morse_ipmon(&time_start, skb, skb->data, skb->len, IPMON_LOC_CLIENT_DRV1, 0);
#endif
		/* see if we should start aggregation */
		morse_aggr_check(mors_if, sta, skb);
		/* Get the s1g bw limit */
		mors_sta = (struct morse_sta *)sta->drv_priv;
		sta_max_bw_mhz = mors_sta->max_bw_mhz;
	}

	if (morse_mac_pkt_to_s1g(mors, skb, &tx_bw_mhz) < 0) {
		morse_dbg(mors, "Failed to convert packet to S1G. Dropping..\n");
		morse_mac_skb_free(mors, skb);
		return;
	}

	/* limit check the set tx_bw for the vif */
	vif_max_bw_mhz = morse_vif_max_tx_bw(mors_if);
	tx_bw_mhz = min(vif_max_bw_mhz, tx_bw_mhz);
	/* This will be true if we are an AP and have parsed the STA's S1G
	 * capabilities when it associated - STAs use the s1g operation
	 * from the AP to determine max bw
	 */
	if (sta_max_bw_mhz > 0)
		tx_bw_mhz = min(tx_bw_mhz, sta_max_bw_mhz);

	morse_mac_fill_tx_info(mors, &tx_info, skb, mors_if, tx_bw_mhz, sta);

	/* Function will automatically call tx_status on
	 * skb if frame should be rescheduled by mac80211 for power save filtering.
	 */
	if (morse_mac_tx_ps_filtered_for_sta(mors, skb, sta))
		return;

	if (is_mgmt)
		mq = mors->cfg->ops->skbq_mgmt_tc_q(mors);
	else
		mq = mors->cfg->ops->skbq_tc_q_from_aci(mors,
			dot11_tid_to_ac(tx_info.tid));

	morse_skbq_skb_tx(mq, skb, &tx_info,
		(is_mgmt) ? MORSE_SKB_CHAN_MGMT : MORSE_SKB_CHAN_DATA);
}

#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
/* The following functions are for airtime fairness */
static int morse_txq_send(struct morse *mors, struct ieee80211_txq *txq)
{
	struct ieee80211_tx_control control = {};

	control.sta = txq->sta;

	while (!test_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags)) {
		struct sk_buff *skb = ieee80211_tx_dequeue(mors->hw, txq);

		if (!skb)
			break;

		morse_mac_ops_tx(mors->hw, &control, skb);
	}

	return test_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags);
}

static bool morse_txq_schedule_list(struct morse *mors, enum morse_page_aci aci)
{
	struct ieee80211_txq *txq;
	bool tx_stopped = false;

	do {
		txq = ieee80211_next_txq(mors->hw, aci);
		if (!txq)
			break;

		tx_stopped = morse_txq_send(mors, txq);

		ieee80211_return_txq(mors->hw, txq, false);
	} while (!tx_stopped);

	return tx_stopped;
}

static bool morse_txq_schedule(struct morse *mors, enum morse_page_aci aci)
{
	bool tx_stopped = false;

	if (aci > MORSE_ACI_VO)
		return 0;

	rcu_read_lock();

	ieee80211_txq_schedule_start(mors->hw, aci);
	tx_stopped = morse_txq_schedule_list(mors, aci);
	ieee80211_txq_schedule_end(mors->hw, aci);

	rcu_read_unlock();

	return tx_stopped;
}

static void morse_txq_tasklet(struct tasklet_struct *t)
{
	s16 aci;
	bool tx_stopped;
	struct morse *mors = from_tasklet(mors, t, tasklet_txq);

	if (test_bit(MORSE_STATE_FLAG_DATA_QS_STOPPED, &mors->state_flags))
		return;

	for (aci = MORSE_ACI_VO; aci >= 0; aci--) {
		tx_stopped = morse_txq_schedule(mors, (enum morse_page_aci)aci);

		if (tx_stopped)
			/* Queues are stopped, probably filled */
			break;

		if (aci == MORSE_ACI_BE)
			break;
	}
}

static void morse_mac_ops_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct morse *mors = hw->priv;

	tasklet_schedule(&mors->tasklet_txq);
}
#endif

int morse_mac_twt_traffic_control(struct morse *mors, int interface_id, bool pause_data_traffic)
{
	int ret = -1;
	unsigned long *event_flags = &mors->chip_if->event_flags;
	struct morse_vif *mors_vif;

	if (interface_id >= MORSE_MAX_IF || mors->vif[interface_id] == NULL) {
		MORSE_WARN_ON(1);
		goto exit;
	}

	mors_vif = ieee80211_vif_to_morse_vif(mors->vif[interface_id]);

	if (!mors_vif->twt.requester) {
		/* TWT not supported.. LMAC should not be signalling traffic control */
		WARN_ONCE(1, "TWT not supported with multi interface\n");
		goto exit;
	}

	if (pause_data_traffic) {
		set_bit(MORSE_DATA_TRAFFIC_PAUSE_PEND, event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
		morse_watchdog_pause(mors);
	} else {
		set_bit(MORSE_DATA_TRAFFIC_RESUME_PEND, event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
		morse_watchdog_resume(mors);
	}

	ret = 0;
exit:
	return ret;
}

static int morse_mac_ops_start(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;

	mutex_lock(&mors->lock);
	/* Read and print FW version */
	morse_cmd_get_version(mors);
	mors->mon_if.id = 0XFFFF;
	mors->started = true;
	mors->state_flags = 0;
	mutex_unlock(&mors->lock);

	return 0;
}

static void morse_mac_ops_stop(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;
	struct morse_vif *mon_if = &mors->mon_if;

	mutex_lock(&mors->lock);
	/* Make sure we stop any monitor interfaces */
	if (mon_if->id != 0xFFFF) {
		morse_cmd_rm_if(mors, mon_if->id);
		mon_if->id = 0xFFFF;
		morse_info(mors, "monitor interfaced removed\n");
	}
	mors->started = false;
	mutex_unlock(&mors->lock);
}

static int add_to_valid_vif_id(struct morse *mors, struct ieee80211_vif *vif, bool *start_beacon)
{
	int vif_id = 0;

	for (vif_id = 0; vif_id < MORSE_MAX_IF; vif_id++) {
		if (mors->vif[vif_id] == NULL) {
			mors->vif[vif_id] = vif;
			if (mors->vif[vif_id]->type == NL80211_IFTYPE_AP)
				*start_beacon = true;
			return 0;
		}
	}
	morse_err(mors, "All elements in vif array filled\n");
	return -ENOMEM;
}

/**
 * SW-7260: Resetting the beacon change sequence related parameters.
 */
static void
morse_mac_reset_s1g_bcn_change_seq_params(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct morse *mors = hw->priv;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;

	if (!mors_if) {
		morse_err(mors, "%s mors_vif is NULL\n", __func__);
		return;
	}

	mors_if->s1g_bcn_change_seq = 0;
	mors_if->s1g_oper_param_crc = 0;
	mors_if->edca_param_crc = 0;
	mors_if->chan_switch_in_progress = false;
	mors_if->waiting_for_probe_req_sched = false;
}

static void
morse_mac_reset_sta_backup(struct morse *mors, struct morse_vif *mors_vif)
{
	memset(mors_vif->sta_backups, 0, sizeof(mors_vif->sta_backups));

	morse_dbg(mors, "STA backup entries cleared\n");
}

static void
morse_mac_save_sta_backup(struct morse *mors, struct morse_vif *mors_vif,
						  struct morse_sta *mors_sta)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mors_vif->sta_backups); i++) {
		if (!mors_vif->sta_backups[i].already_assoc_req ||
			time_after(jiffies, mors_vif->sta_backups[i].timeout)) {
			morse_dbg(mors, "Storing STA backup (slot %d) for %pM\n",
					i, mors_sta->addr);
			memcpy(&mors_vif->sta_backups[i], mors_sta, sizeof(*mors_sta));
			mors_vif->sta_backups[i].timeout =
					jiffies + msecs_to_jiffies(STA_PRIV_TIMEOUT_MSEC);
			return;
		}
	}
	morse_warn(mors, "No spare STA backup slot\n");
}

static void
morse_mac_restore_sta_backup(struct morse *mors, struct morse_vif *mors_vif,
							 struct morse_sta *mors_sta, u8 *addr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mors_vif->sta_backups); i++) {
		if (mors_vif->sta_backups[i].already_assoc_req &&
			ether_addr_equal_unaligned(mors_vif->sta_backups[i].addr, addr)) {
			morse_info(mors, "Retrieving STA backup (slot %d) for %pM\n",
					i, mors_sta->addr);
			memcpy(mors_sta, &mors_vif->sta_backups[i], sizeof(*mors_sta));
			memset(&mors_vif->sta_backups[i], 0, sizeof(mors_vif->sta_backups[i]));
			return;
		}
	}
	morse_dbg(mors, "No STA backup for %pM\n", mors_sta->addr);
}

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
static void morse_chswitch_timer(unsigned long addr)
#else
static void morse_chswitch_timer(struct timer_list *t)
#endif
{
#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
	struct morse_vif *mors_if = (struct morse_vif *)addr;
#else
	struct morse_vif *mors_if = from_timer(mors_if, t, chswitch_timer);
#endif
	struct ieee80211_vif *vif;
	struct morse *mors;

	if (!mors_if) {
		pr_info("ECSA: ERROR! mors_if NULL\n");
		return;
	}
	vif = morse_vif_to_ieee80211_vif(mors_if);
	mors = morse_vif_to_morse(mors_if);

	morse_info(mors, "%s: chswitch timer TS=%ld\n", __func__, jiffies);

	if (vif->type == NL80211_IFTYPE_AP)
		ieee80211_csa_finish(vif);
}

static void morse_ecsa_chswitch_work(struct work_struct *work)
{
	int ret;
	struct morse_vif *mors_if = container_of(work, struct morse_vif, ecsa_chswitch_work.work);
	struct morse_channel_info *ch;

	if (mors_if) {
		struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_if);
		struct morse *mors = morse_vif_to_morse(mors_if);

		if (vif->type == NL80211_IFTYPE_AP)
			ch = &mors_if->custom_configs->default_bw_info;
		else
			ch = &mors_if->assoc_sta_channel_info;

		mutex_lock(&mors->lock);
		ret = morse_cmd_set_channel(mors,
			ch->op_chan_freq_hz,
			ch->pri_1mhz_chan_idx,
			ch->op_bw_mhz,
			ch->pri_bw_mhz);
		if (ret)
			morse_err(mors, "%s: morse_cmd_set_channel failed %d", __func__, ret);
		mutex_unlock(&mors->lock);
	}
}

static bool morse_mac_ecsa_begin_channel_switch(struct morse *mors)
{
	int ret;

	mors->in_scan = true;
	ret = morse_cmd_cfg_scan(mors, true);
	if (ret) {
		morse_err(mors, "%s: morse_cmd_cfg_scan failed %d", __func__, ret);
		return false;
	} else {
		return true;
	}

}

static bool morse_mac_ecsa_finish_channel_switch(struct morse *mors)
{
	int ret;

	mors->in_scan = false;
	ret = morse_cmd_cfg_scan(mors, false);
	if (ret) {
		morse_err(mors, "%s: morse_cmd_cfg_scan failed %d", __func__, ret);
		return false;
	} else {
		return true;
	}
}

/* Update the ecsa channel config in mors_if and mors channel info */
static void morse_mac_ecsa_update_bss_chan_info(struct morse_vif *mors_if)
{
	struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_if);

	/* Update default bandwidth info used during channel change for 1mhz primary channel idx and bw */
	memcpy(&mors_if->custom_configs->default_bw_info, &mors_if->ecsa_channel_info,
					sizeof(mors_if->ecsa_channel_info));

	/* Update channel info used in AP mode for S1G Operation IE */
	memcpy(&mors_if->custom_configs->channel_info, &mors_if->ecsa_channel_info,
					sizeof(mors_if->ecsa_channel_info));

	/* Update assoc sta channel info used in STA mode to restore the primary channel config after scan */
	if (vif->type == NL80211_IFTYPE_STATION)
		memcpy(&mors_if->assoc_sta_channel_info, &mors_if->ecsa_channel_info,
					sizeof(mors_if->ecsa_channel_info));

	if (vif->type == NL80211_IFTYPE_AP)
		mors_if->mask_ecsa_info_in_beacon = true;
}

/*
 * API to verify if we are switching to new channel as part of ECSA and update
 * the ECSA channel info in mors and mors_if data structures.
 * It also configures scan state in the firmware to postpone the PHY calibration
 * so that AP can switch to new channel within beacon interval. Otherwise
 * channel change is taking 230 - 440msecs due to PHY DC calibration.
 * PHY calibration is not performed during scan.
 */
static bool morse_mac_ecsa_channel_switch_in_progress(struct morse *mors,
												u32 freq_hz,
												u8 op_bw_mhz,
												u8 *pri_bw_mhz,
												u8 *pri_1mhz_chan_idx)
{
	struct ieee80211_vif *vif = morse_get_vif(mors);
	struct morse_vif *mors_if = ieee80211_vif_to_morse_vif(vif);
	bool scan_configured = false;

	if (vif->csa_active &&
		(freq_hz == mors_if->ecsa_channel_info.op_chan_freq_hz) &&
		(op_bw_mhz == mors_if->ecsa_channel_info.op_bw_mhz)) {

		/* Update the new ecsa channel config in mors_if and mors channel info */
		morse_mac_ecsa_update_bss_chan_info(mors_if);

		/*
		 * Update pri_bw_mhz and pri_1mhz_chan_idx which are used in morse_mac_ops_config
		 * to switch to the new channel. mac80211 doesn't pass this primary chan info to driver.
		 */
		*pri_bw_mhz = mors_if->ecsa_channel_info.pri_bw_mhz;
		*pri_1mhz_chan_idx = mors_if->ecsa_channel_info.pri_1mhz_chan_idx;

		/* Clear the scan list in STA mode as cssid list contains AP with old S1G Op IE */
		if (vif->type == NL80211_IFTYPE_STATION) {
			morse_dot11ah_clear_list();
			/* Reset channel info */
			memset(&mors_if->ecsa_channel_info, 0, sizeof(mors_if->ecsa_channel_info));

			/* Reset beacon change seq */
			mors_if->s1g_bcn_change_seq = INVALID_BCN_CHANGE_SEQ_NUM;
		}

		/*
		 * SW-8055: Set ecsa_chan_configured to configure the channel again to perform DC calibration
		 * This change is not required once the periodic PHY DC calibration is enabled in fw and
		 * it will tracked through jira id SW-8055.
		 * We delay this until AP sends 1st beacon in new channel and on client side until
		 * it receives first beacon. On client side, 1st beacon is needed in mac80211 to unblock
		 * the traffic, if it has blocked during start of the ECSA.
		 */
		scan_configured = morse_mac_ecsa_begin_channel_switch(mors);

		mors_if->ecsa_chan_configured = true;

		morse_info(mors, "ECSA: %s: pri_bw_mhz=%d, pri_1mhz_chan_idx=%d, bcn_change_seq =%x",
									__func__,
									*pri_bw_mhz,
									*pri_1mhz_chan_idx,
									mors_if->s1g_bcn_change_seq);
	}
	return scan_configured;
}

static int
morse_mac_ops_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	int ret = 0;
	int i;
	struct morse *mors = hw->priv;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	bool start_beacon = false;

	/* Just STA, AP and ADHOC for now */
	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC) {
		morse_err(mors, "%s: Attempt to add type %d, not supported\n",
			  __func__, vif->type);
		return -EOPNOTSUPP;
	}

	mutex_lock(&mors->lock);

	ret = morse_cmd_add_if(mors, &mors_if->id, vif->addr, vif->type);
	if (ret) {
		morse_err(mors, "morse_cmd_add_if failed %d", ret);
		goto exit;
	}

	morse_vendor_ie_init_interface(mors_if);

	if (is_multi_interface_mode()) {
		ret = add_to_valid_vif_id(mors, vif, &start_beacon);
		if (ret) {
			morse_err(mors, "morse_cmd_add_if failed %d", ret);
			goto exit;
		}
	} else {
		mors->vif[0] = vif;
		if (mors->vif[0]->type == NL80211_IFTYPE_AP ||
			mors->vif[0]->type == NL80211_IFTYPE_ADHOC)
			start_beacon = true;
	}

	if (mors_if->id != (mors_if->id & MORSE_TX_CONF_FLAGS_VIF_ID_MASK)) {
		morse_err(mors, "%s invalid VIF %d\n", __func__, mors_if->id);
		ret = EOPNOTSUPP;
		goto exit;
	}
	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;

	/* Set control response frame bandwidth for this interface.
	 * May have already been set using vendor commands but the chip would
	 * reject while interface is down. Send again after interface is up
	 */
	if (mors_if->ctrl_resp_out_1mhz_en)
		morse_cmd_set_cr_bw(mors, mors_if, 0, 1);
	if (mors_if->ctrl_resp_in_1mhz_en)
		morse_cmd_set_cr_bw(mors, mors_if, 1, 1);

	mors_if->ap = NULL;
	if ((start_beacon) &&
			(morse_get_ap_vif(mors)->type == NL80211_IFTYPE_AP ||
			morse_get_ap_vif(mors)->type == NL80211_IFTYPE_ADHOC)) {
		mors_if->dtim_count = 0;
		mors_if->ap = kzalloc(sizeof(*mors_if->ap), GFP_KERNEL);
		morse_ndp_probe_req_resp_enable(mors, true);
		INIT_LIST_HEAD(&mors_if->ap->stas);
	}
	mors_if->custom_configs = &mors->custom_configs;
	mors_if->epoch = get_jiffies_64();

	/* Get and assign the interface's capabilities */
	ret = morse_cmd_get_capabilities(mors, mors_if->id, &mors_if->capabilities);
	if (ret) {
		/* If this command failed, We might cause a timeout for the callback */
		morse_err(mors, "%s: morse_cmd_get_capabilities failed for VIF %d",
			  __func__,
			  mors_if->id);
		goto exit;
	}

	/* Enable TWT features. */
	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		mors_if->twt.requester = false;
		mors_if->twt.responder =
			(enable_twt &&
			 MORSE_CAPAB_SUPPORTED(&mors_if->capabilities, TWT_RESPONDER));

		if (mors_if->twt.responder != enable_twt) {
			if (enable_twt)
				morse_err(mors,
					"%s: TWT is configured as a responder but it is not supported\n",
					__func__);
		}
		break;

	case NL80211_IFTYPE_STATION:
		mors_if->twt.requester =
			enable_twt &&
			MORSE_CAPAB_SUPPORTED(&mors_if->capabilities, TWT_REQUESTER);

		mors_if->twt.responder = false;

		if (!enable_twt)
			break;

		if (!mors_if->twt.requester) {
			morse_err(mors,
				"%s: TWT is configured as a requester but it is not supported\n",
				__func__);

			break;
		}

		mors_if->twt.requester =
			((enable_ps == POWERSAVE_MODE_FULLY_ENABLED) &&
			 enable_dynamic_ps_offload &&
			 !enable_mac80211_connection_monitor);

		if (!mors_if->twt.requester) {
			if (enable_ps != POWERSAVE_MODE_FULLY_ENABLED)
				morse_err(mors,
					"%s: TWT is configured as a requester but powersave is not fully enabled\n",
					__func__);

			if (enable_dynamic_ps_offload)
				morse_err(mors,
					"%s: TWT is configured as a requester but dynamic powersave offload is not enabled\n",
					__func__);

			if (!enable_mac80211_connection_monitor)
				morse_err(mors,
					"%s: TWT is configured as a requester but mac80211 connection monitor is not disabled\n",
					__func__);
		}
		break;

	default:
		break;
	}

	/* Initialize the change seq to 0. Other parameters keeping track of IE changes */
	morse_mac_reset_s1g_bcn_change_seq_params(hw, vif);

	/* Reset all stored private data backups, if any */
	morse_mac_reset_sta_backup(mors, mors_if);

	if (enable_bcn_change_seq_monitor &&
		morse_get_sta_vif(mors) &&
		morse_get_sta_vif(mors)->type == NL80211_IFTYPE_STATION)
		morse_send_probe_req_enable(mors, true);

	if (enable_cac)
		ret = morse_cac_init(mors);

	morse_dbg(mors, "FW Manifest Flags for VIF %d:", mors_if->id);
	for (i = 0; i < ARRAY_SIZE(mors_if->capabilities.flags); i++)
		morse_dbg(mors, "%d: 0x%x", i,
			   mors_if->capabilities.flags[i]);

	morse_info(mors, "ieee80211_add_interface %d\n", mors_if->id);
	ieee80211_wake_queues(mors->hw);
	mors->started = true;

	/* Init TWT. */
	morse_twt_init_vif(mors, mors_if);
	/* Only stations support PS filtering out-of-the-box
	 * (re-buffered internally to driver).
	 */
	mors_if->supports_ps_filter = (vif->type == NL80211_IFTYPE_STATION);

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
	init_timer(&mors_if->chswitch_timer);
	mors_if->chswitch_timer.data = (unsigned long)mors_if;
	mors_if->chswitch_timer.function = morse_chswitch_timer;
	add_timer(&mors_if->chswitch_timer);
#else
	timer_setup(&mors_if->chswitch_timer, morse_chswitch_timer, 0);
#endif
	mors_if->ecsa_chan_configured = false;
	mors_if->mask_ecsa_info_in_beacon = false;

	/* Reset channel info */
	memset(&mors_if->ecsa_channel_info, 0, sizeof(mors_if->ecsa_channel_info));

	INIT_DELAYED_WORK(&mors_if->ecsa_chswitch_work, morse_ecsa_chswitch_work);

exit:
	mutex_unlock(&mors->lock);

	return ret;
}

static void
morse_mac_ops_remove_interface(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif)
{
	int ret;
	struct morse *mors = hw->priv;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_vif *ap_vif = morse_get_ap_vif(mors);
	struct ieee80211_vif *sta_vif = morse_get_sta_vif(mors);

	mutex_lock(&mors->lock);
	ieee80211_stop_queues(hw);
	if ((ap_vif == NULL && sta_vif == NULL) || mors_if == NULL)
		goto exit;

	/* Make sure no beacons are sent */
	if ((ap_vif != NULL) &&
		(ap_vif->type == NL80211_IFTYPE_AP ||
		ap_vif->type == NL80211_IFTYPE_ADHOC)) {
		morse_ndp_probe_req_resp_enable(mors, false);
		kfree(mors_if->ap);
		mors_if->ap = NULL;
	}

	if (enable_bcn_change_seq_monitor &&
		sta_vif && sta_vif->type == NL80211_IFTYPE_STATION) {
		morse_send_probe_req_enable(mors, false);

		/* Free up probe req template buffer */
		if (mors_if->probe_req_buf)
			dev_kfree_skb_any(mors_if->probe_req_buf);

		mors_if->probe_req_buf = NULL;
	}

	if (enable_cac)
		morse_cac_deinit(mors);

	/* Cleanup TWT. */
	morse_twt_finish_vif(mors, mors_if);

	morse_vendor_ie_deinit_interface(mors_if);

	ret = morse_cmd_rm_if(mors, mors_if->id);
	if (ret) {
		morse_err(mors, "morse_cmd_rm_if failed %d", ret);
		goto exit;
	}

	del_timer_sync(&mors_if->chswitch_timer);
	flush_delayed_work(&mors_if->ecsa_chswitch_work);

	morse_info(mors, "ieee80211_rm_interface %d\n", mors_if->id);

	/* If data TX is stopped, the LMAC will eventually send the
	 * TWT traffic event to unblock TX on reception of the iface teardown
	 * command. To be safe, however, explicitly unblock traffic here as well
	 * to prevent unintended consequences if the to-host unblock event is lost.
	 */
	if (sta_vif &&
		test_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED, &mors->state_flags)) {
		set_bit(MORSE_DATA_TRAFFIC_RESUME_PEND, &mors->chip_if->event_flags);
		queue_work(mors->chip_wq, &mors->chip_if_work);
	}

exit:
	mors->vif[mors_if->id] = NULL;
	mutex_unlock(&mors->lock);
}

static int morse_mac_ops_config(struct ieee80211_hw *hw, u32 changed)
{
	int err = 0;
	struct morse *mors = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	mutex_lock(&mors->lock);
	if (!mors->started)
		goto exit;

	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL)
		morse_dbg(mors, "ieee80211_conf_change_listen_interval\n");

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		int ret = 0;
		struct morse_vif *mon_if = &mors->mon_if;

		morse_dbg(mors, "%s: change monitor mode: %s\n",
			       __func__, conf->flags & IEEE80211_CONF_MONITOR ?
			       "true" : "false");
		if (conf->flags & IEEE80211_CONF_MONITOR) {
			ret = morse_cmd_add_if(mors,
					       &mon_if->id,
					       mors->macaddr,
					       NL80211_IFTYPE_MONITOR);
			if (ret)
				morse_err(mors,
					  "monitor interface add failed %d\n",
					  ret);
			else
				morse_info(mors,
					   "monitor interfaced added %d\n",
					   mon_if->id);
		} else {
			if (mon_if->id != 0xFFFF) {
				morse_cmd_rm_if(mors, mon_if->id);
				morse_info(mors, "monitor interfaced removed\n");
			}
			mon_if->id = 0xFFFF;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS && !(conf->flags & IEEE80211_CONF_MONITOR))	{
		bool en_ps = !!(conf->flags & IEEE80211_CONF_PS);

		morse_info(mors, "%s: change power-save mode: %s (current %s)\n",
			__func__, en_ps ? "true" : "false",
			mors->config_ps ? "true" : "false");

		if (mors->config_ps != en_ps) {
			mors->config_ps = en_ps;
			if (enable_ps == POWERSAVE_MODE_FULLY_ENABLED) {
				/* SW-2638:
				 * If we have GPIO pins wired. Let's control host-to-chip PS mechanism.
				 * Otherwise, ignore the command altogether.
				 */
				if (en_ps) {
					morse_cmd_set_ps(mors, true, enable_dynamic_ps_offload);
					morse_ps_enable(mors);
				} else {
					morse_ps_disable(mors);
					morse_cmd_set_ps(mors, false, false);
				}
			}
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER && !(conf->flags & IEEE80211_CONF_MONITOR)) {
		int ret = 0;
		s32 out_power;

		if (mors->max_power_level == INT_MAX) {
			s32 power_level;

			mors->max_power_level = max_power_level;
			/* Retrieve maximum TX power the chip can transmit */
			ret = morse_cmd_get_max_txpower(mors, &power_level);
			if (ret)
				morse_err(mors,
					"get max txpower failed (%d), using default max power %d\n",
					ret, max_power_level);
			else
				mors->max_power_level = power_level;

			morse_info(mors, "Maximum TX power level detected %d\n",
					mors->max_power_level);
		}

		/* Limit to chip maximum TX power */
		out_power = min(conf->power_level, mors->max_power_level);

		if (out_power != mors->tx_power_dBm) {
			ret = morse_cmd_set_txpower(mors, &out_power,
							out_power);
			morse_info(mors, "morse_cmd_set_txpower %s %d\n",
					ret ? "fail" : "success",
					ret ? ret : out_power);
		}

		if (!ret) {
			conf->power_level = out_power;
			mors->tx_power_dBm = out_power;
		}
	}

	if (hw->conf.chandef.chan &&
		!(hw->conf.chandef.chan->flags & IEEE80211_CHAN_DISABLED) &&
		(changed & IEEE80211_CONF_CHANGE_CHANNEL)) {
		struct morse_channel_info info;
		int ret;
		bool scan_configured = false;
		const struct morse_dot11ah_channel *chan_s1g;
		const struct morse_reg_rule *mors_reg_rule = NULL;
		u32 freq_hz;
		u8 op_bw_mhz;
		u8 pri_1mhz_chan_idx = mors->custom_configs.default_bw_info.pri_1mhz_chan_idx;
		u8 pri_bw_mhz = mors->custom_configs.default_bw_info.pri_bw_mhz;
		u8 bssid[ETH_ALEN];
		const char *region = morse_dot11ah_get_region_str();

		/* Convert 5G channel to S1G channel */
		chan_s1g = morse_dot11ah_channel_chandef_to_s1g(&conf->chandef);
		if (!chan_s1g) {
			if (!mors->in_scan)
				morse_dbg(mors,
					  "%s: Set channel index %d failed: not in region map %s\n",
					  __func__,
					  conf->chandef.chan->hw_value, region);

			err = -ENOENT;
			goto exit;
		}

		freq_hz = KHZ_TO_HZ(morse_dot11ah_channel_to_freq_khz(chan_s1g->ch.hw_value));
		mors_reg_rule = morse_regdom_get_rule_for_freq(region, ieee80211_channel_to_khz(&chan_s1g->ch));

		op_bw_mhz = (chan_s1g->ch.flags & IEEE80211_CHAN_8MHZ) ? 8 :
			    (chan_s1g->ch.flags & IEEE80211_CHAN_4MHZ) ? 4 :
			    (chan_s1g->ch.flags & IEEE80211_CHAN_2MHZ) ? 2 : 1;

		if (mors->in_scan) {
			/* SW-2278 For interop:
			 * Other vendors appear to be responding to our
			 * 1Mhz probes requests with 2MHz probe responses -
			 *
			 * As a WAR, we will always configure our operating
			 * width to 2MHz to be able to receive these responses.
			 */
			pri_bw_mhz = op_bw_mhz > 1 ? 2 : 1;

		} else if ((morse_get_vif(mors) != NULL) &&
			   (morse_get_vif(mors)->type != NL80211_IFTYPE_AP) &&
			   (morse_get_vif(mors)->bss_conf.bssid != NULL)) {

			scan_configured = morse_mac_ecsa_channel_switch_in_progress(mors,
							freq_hz, op_bw_mhz, &pri_bw_mhz, &pri_1mhz_chan_idx);

			/* If we are a STA and have a BSS/AP conf, try to use the AP's chan info */
			memcpy(bssid, morse_get_vif(mors)->bss_conf.bssid, ETH_ALEN);
			if (morse_mac_find_channel_info_for_bssid(bssid, &info)) {
				if (freq_hz == info.op_chan_freq_hz) {
					pri_bw_mhz = info.pri_bw_mhz;
					pri_1mhz_chan_idx = info.pri_1mhz_chan_idx;
				}
			}
		} else if ((morse_get_vif(mors) == NULL) ||
			   (morse_get_vif(mors)->type != NL80211_IFTYPE_AP)) {
			if (!morse_dot11_find_bssid_on_channel(freq_hz, bssid)) {
				/* If we don't have a VIF or aren't an AP, use
				 * channel info from the first bssid in the stored list.
				 * WARNING:
				 * When there are multiple APs, This can cause incorrect
				 * channel config leading to problems such as auth failure.
				 */
				morse_info(mors,
					"%s: Using first stored bssid info for channel config\n", __func__);
				morse_mac_find_channel_info_for_bssid(bssid, &info);
				pri_bw_mhz = info.pri_bw_mhz;
				pri_1mhz_chan_idx = info.pri_1mhz_chan_idx;
			}
		} else if (morse_get_vif(mors)->type == NL80211_IFTYPE_AP) {
			scan_configured = morse_mac_ecsa_channel_switch_in_progress(mors,
								 freq_hz, op_bw_mhz, &pri_bw_mhz, &pri_1mhz_chan_idx);
		}

		/* Final sanity check:
		 * pri_bw_mhz is either 1MHZ or 2MHZ
		 * pri_bw_mhz shouldn't 2 if op_bw_mhz is 1
		 * pri_1mhz_index is based on op_bw_mhz
		 */
		pri_bw_mhz = min_t(u8, pri_bw_mhz, 2);
		pri_bw_mhz = min_t(u8, pri_bw_mhz, op_bw_mhz);
		pri_1mhz_chan_idx = op_bw_mhz == 8 ? min_t(u8, pri_1mhz_chan_idx, 7) :
				    op_bw_mhz == 4 ? min_t(u8, pri_1mhz_chan_idx, 3) :
				    op_bw_mhz == 2 ? min_t(u8, pri_1mhz_chan_idx, 1) : 0;

		mors->channel_num_80211n = conf->chandef.chan->hw_value;
		morse_info(mors, "ieee80211_conf_change_channel CH %d [%d-%d-%d]\n",
			chan_s1g->ch.hw_value, op_bw_mhz, pri_bw_mhz, pri_1mhz_chan_idx);

		ret = morse_cmd_set_channel(mors,
			freq_hz, pri_1mhz_chan_idx, op_bw_mhz, pri_bw_mhz);

		if (scan_configured)
			morse_mac_ecsa_finish_channel_switch(mors);

		if (ret)
			morse_err(mors, "morse_cmd_set_channel fail %d\n", ret);
		else {
			struct morse_channel_info *stored_info =
				&mors->custom_configs.channel_info;

			if (freq_hz != DEFAULT_FREQUENCY)
				stored_info->op_chan_freq_hz = freq_hz;

			if (pri_1mhz_chan_idx
				!= DEFAULT_1MHZ_PRIMARY_CHANNEL_INDEX)
				stored_info->pri_1mhz_chan_idx =
					pri_1mhz_chan_idx;

			if (op_bw_mhz != DEFAULT_BANDWIDTH)
				stored_info->op_bw_mhz = op_bw_mhz;

			if (pri_bw_mhz != DEFAULT_BANDWIDTH)
				stored_info->pri_bw_mhz = pri_bw_mhz;

			/* Validate that primary does not exceed operating */
			stored_info->pri_bw_mhz =
				(stored_info->op_bw_mhz == 1) ?
				1 : stored_info->pri_bw_mhz;
		}

		if (mors_reg_rule != NULL) {
			if (enable_auto_duty_cycle) {
				u32 duty_cycle = mors_reg_rule->duty_cycle.sta;

				if (morse_get_vif(mors) != NULL &&
				    morse_get_vif(mors)->type == NL80211_IFTYPE_AP)
					duty_cycle = mors_reg_rule->duty_cycle.ap;

				morse_dbg(mors, "Setting duty cycle to %d (omit_ctrl_resp %d)",
						duty_cycle,
						mors_reg_rule->duty_cycle.omit_ctrl_resp ? 1 : 0);
				ret = morse_cmd_set_duty_cycle(mors, duty_cycle,
						mors_reg_rule->duty_cycle.omit_ctrl_resp);
				if (ret)
					morse_err(mors, "morse_cmd_set_duty_cycle failed %d\n", ret);
			}

			if (enable_auto_mpsw) {
				morse_dbg(mors, "Setting MPSW to min %d us max %d us, window %d us\n",
						mors_reg_rule->mpsw.airtime_min_us,
						mors_reg_rule->mpsw.airtime_max_us,
						mors_reg_rule->mpsw.window_length_us);
				ret = morse_cmd_set_mpsw(mors, mors_reg_rule->mpsw.airtime_min_us,
						mors_reg_rule->mpsw.airtime_max_us,
						mors_reg_rule->mpsw.window_length_us);
				if (ret)
					morse_err(mors, "morse_cmd_set_mpsw failed %d\n", ret);
			}
		} else
			morse_warn(mors,
				"No reg rule for %s freq %d - duty cycle and mpsw not set\n",
				region, HZ_TO_KHZ(freq_hz));

	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		morse_dbg(mors, "ieee80211_conf_change_retry_limits\n");

exit:
	mutex_unlock(&mors->lock);
	return err;
}

static void
morse_mac_ops_bss_info_changed(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *info, u32 changed)
{
	struct morse *mors = hw->priv;
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	mutex_lock(&mors->lock);

	if (changed & BSS_CHANGED_BEACON)
		morse_info(mors, "BSS Changed beacon data, reset flag=%d, csa_active=%d ecsa_chan_configured=%d\n",
				   mors_if->mask_ecsa_info_in_beacon, vif->csa_active, mors_if->ecsa_chan_configured);

	if (changed & BSS_CHANGED_BANDWIDTH) {
		morse_info(mors, "BSS Changed BW, changed=0x%x, jiffies=%ld, csa_active=%d\n",
				   changed, jiffies, vif->csa_active);
#ifdef CONFIG_MORSE_RC
		if (vif->csa_active && (vif->type == NL80211_IFTYPE_AP) && mors_if->ap->num_stas)
			morse_rc_reinit_stas(mors, vif);
#endif
	}

	if ((changed & BSS_CHANGED_BEACON_INT) ||
		(changed & BSS_CHANGED_SSID)) {
		int ret;

		u32 cssid = ~crc32(~0, info->ssid, info->ssid_len);

		ret = morse_cmd_cfg_bss(mors, mors_if->id,
			info->beacon_int, info->dtim_period, cssid);
		if (ret)
			morse_err(mors, "morse_cmd_cfg_bss fail %d\n", ret);
		else
			morse_info(mors, "Beacon interval set %d\n",
				   info->beacon_int);
	}

	/**
	 * SW-5031: Keep track of IBSS network notifications. These
	 * are invoked when
	 * a. node joins the IBSS
	 * b. creates new IBSS
	 * c. node leaves the IBSS or disconnects from IBSS
	 *
	 * For a & b, bss_conf.enable_beacon is set to TRUE and for only
	 * case b (creates new IBSS) bss_conf.ibss_creator is set to true.
	 *
	 * For c, bss_conf.enable_beacon is set to false
	 */
	if ((changed & BSS_CHANGED_IBSS) &&
		vif->type == NL80211_IFTYPE_ADHOC) {
		int ret = 0;

		/** If enable_beacon is set to false, stop the IBSS.
		 *  enable_beacon seems to be set false even for BSS.
		 *  Need to review later if it needs to be checked for
		 *  AP mode as well.
		 */
		bool stop_ibss = (!vif->bss_conf.enable_beacon);

		ret = morse_cmd_cfg_ibss(mors, mors_if->id,
					vif->bss_conf.bssid, vif->bss_conf.ibss_creator, stop_ibss);
		if (ret)
			morse_err(mors, "morse_cmd_cfg_ibss fail %d\n", ret);
		else
			morse_info(mors, "IBSS creator: %d stop_ibss:%d\n",
					vif->bss_conf.ibss_creator, stop_ibss);
	}

	/**
	 * SW-5445: Get the template probe request buffer populated in this
	 * event handler and use it on detection of beacon change seq number
	 */
	if (vif->type == NL80211_IFTYPE_STATION &&
			changed & BSS_CHANGED_ASSOC &&
			bss_conf) {

		mors_if->is_sta_assoc = bss_conf->assoc;

		/* Request for new template buffer only on new association */
		if (enable_bcn_change_seq_monitor && mors_if->is_sta_assoc) {
			/* Free up old template buffer */
			if (mors_if->probe_req_buf)
				dev_kfree_skb_any(mors_if->probe_req_buf);

			mors_if->probe_req_buf = ieee80211_ap_probereq_get(mors->hw, vif);
			mors_if->s1g_bcn_change_seq = INVALID_BCN_CHANGE_SEQ_NUM;

			if (mors_if->probe_req_buf == NULL)
				morse_err(mors, "%s: ieee80211_ap_probereq_get failed\n", __func__);
		}
	}

	/** SW-4817: Note that we are 'repurposing' this to configure ARP offload.
	 * Instead of arp_addr_list being used purely for ARP filtering (as mac80211 expects),
	 * the firmware will AUTOMATICALLY respond to ARP requests addressed to the first IP in this
	 * table. ie. ARP requests addressed to the first IP of this table will NEVER make their way
	 * to linux, instead having the response generated and transmitted in FW.
	 * The other IPs in this table will behave as mac80211 expects and will be allowed to pass.
	 */
	if ((changed & BSS_CHANGED_ARP_FILTER)
			&& vif->type == NL80211_IFTYPE_STATION
			&& mors_if->custom_configs->enable_arp_offload)
		morse_cmd_arp_offload_update_ip_table(mors, mors_if->id,
				info->arp_addr_cnt, info->arp_addr_list);

	mutex_unlock(&mors->lock);
}

static int
morse_mac_ops_get_survey(struct ieee80211_hw *hw, int idx,
			 struct survey_info *survey)
{
	struct morse *mors = hw->priv;
	struct ieee80211_supported_band *sband;
	const struct morse_dot11ah_channel *chan_s1g;
	struct morse_channel_survey fw_survey;
	u32 op_ch_bw;
	u32 freq_hz;
	int ret;

	if (!enable_survey)
		return -ENOENT;

	sband = hw->wiphy->bands[NL80211_BAND_5GHZ];
	if (idx >= sband->n_channels)
		return -ENOENT;

	survey->channel = &sband->channels[idx];

	chan_s1g = morse_dot11ah_channel_get_s1g(survey->channel);
	if (!chan_s1g) {
		/* SW-4684: Channel is not supported in regdom, but we will upset Linux wireless if we
		 * return ENOENT here (nl80211_dump_survey loop will break if any error is returned).
		 * Alternatively, return 0 and set channel to NULL instead (to skip channel)
		 * TODO: a better way is to loop over the supported regdom channels only instead of
		 * the comprehensive supported list sband->channels
		 */
		survey->channel = NULL;
		survey->filled = 0;
		return 0;
	}

	freq_hz = KHZ_TO_HZ(ieee80211_channel_to_khz(&chan_s1g->ch));

	op_ch_bw = (chan_s1g->ch.flags & IEEE80211_CHAN_1MHZ) ? 1 :
		   (chan_s1g->ch.flags & IEEE80211_CHAN_2MHZ) ? 2 :
		   (chan_s1g->ch.flags & IEEE80211_CHAN_4MHZ) ? 4 : 8;

	morse_dbg(mors, "%s: halow channel %d", __func__, chan_s1g->ch.hw_value);

	ret = morse_cmd_survey_channel(mors, &fw_survey, freq_hz);
	if (ret) {
		morse_err(mors, "%s:channel %d: error %d\n", __func__, freq_hz, ret);
		return -EIO;
	}

	survey->noise = fw_survey.noise;
	survey->time = do_div(fw_survey.time_listen, 1000);
	survey->time_rx = do_div(fw_survey.time_rx, 1000);
	survey->filled = SURVEY_INFO_NOISE_DBM |
			 SURVEY_INFO_TIME |
			 SURVEY_INFO_TIME_RX;

	return 0;
}

static void
morse_mac_ops_configure_filter(struct ieee80211_hw *hw,
			unsigned int changed_flags,
			unsigned int *total_flags, u64 multicast)
{
	struct morse *mors = hw->priv;

	*total_flags &= 0;
	(void)mors;
}

static void morse_mac_ops_sw_scan_start(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    const u8 *mac_addr)
{
	int ret;
	struct morse *mors = hw->priv;

	mutex_lock(&mors->lock);
	if (!mors->started) {
		morse_info(mors, "%s: Not started. Aborting\n", __func__);
		goto exit;
	}

	mors->in_scan = true;

	/* Some APs may change their configurations, clear cached AP list */
	morse_dot11ah_clear_list();

	ret = morse_cmd_cfg_scan(mors, true);
	if (ret)
		morse_err(mors, "%s: morse_cmd_cfg_scan failed %d", __func__, ret);

exit:
	mutex_unlock(&mors->lock);
}

static void morse_mac_save_ecsa_chan_info(struct morse *mors, struct morse_vif *mors_if,
			struct ieee80211_ext_chansw_ie *ecsa_ie_info,
			const u8 *chswitch_wrapper_ie_data,
			u8 chswitch_wrapper_ie_datalen)
{
	const u8 *ie = NULL;

	if (chswitch_wrapper_ie_data)
		ie = cfg80211_find_ie(WLAN_EID_WIDE_BW_CHANNEL_SWITCH,
				chswitch_wrapper_ie_data,
				chswitch_wrapper_ie_datalen);

	mors_if->ecsa_channel_info.s1g_operating_class = ecsa_ie_info->new_operating_class;

	/* If wide bw channel switch wrapper IE is null then it is 1MHz Operating channel */
	if (!ie) {
		mors_if->ecsa_channel_info.op_chan_freq_hz =
					morse_dot11ah_s1g_chan_to_s1g_freq(ecsa_ie_info->new_ch_num);
		/*
		 * Assign the op bw by incrementing S1G_CHAN_1MHZ, as we always store actual bw
		 * in chan info whereas S1G_CHAN_1MHZ/S1G_CHAN_2MHZ etc macros are defined as per
		 * standard i.e, actual bw - 1.
		 */
		mors_if->ecsa_channel_info.op_bw_mhz = S1G_CHAN_1MHZ + 1;
	} else {
		struct ieee80211_wide_bw_chansw_ie *wbcsie =
							(struct ieee80211_wide_bw_chansw_ie *) (ie+2);

		mors_if->ecsa_channel_info.op_chan_freq_hz =
				morse_dot11ah_s1g_chan_to_s1g_freq(wbcsie->new_center_freq_seg0);
		/*
		 * Assign the op bw by incrementing new_channel_width, as new_channel_width
		 * is defined as per standard i.e, actual bw-1.
		 */
		mors_if->ecsa_channel_info.op_bw_mhz = wbcsie->new_channel_width + 1;
	}
	mors_if->ecsa_channel_info.pri_1mhz_chan_idx = morse_dot11ah_calculate_primary_s1g_channel_loc(
													HZ_TO_KHZ(morse_dot11ah_s1g_chan_to_s1g_freq(ecsa_ie_info->new_ch_num)),
													HZ_TO_KHZ(mors_if->ecsa_channel_info.op_chan_freq_hz),
													mors_if->ecsa_channel_info.op_bw_mhz);
	mors_if->ecsa_channel_info.pri_bw_mhz =
			(((morse_dot11ah_channel_get_flags(ecsa_ie_info->new_ch_num) > IEEE80211_CHAN_1MHZ) ?
										S1G_CHAN_2MHZ : S1G_CHAN_1MHZ) + 1);

	morse_info(mors, "ECSA:Chan Info:Prim_ch=%d, Op_ch=%d [%d-%d-%d], op_class=%d, count=%d, mode=%d\n",
						morse_dot11ah_s1g_chan_to_s1g_freq(ecsa_ie_info->new_ch_num),
						mors_if->ecsa_channel_info.op_chan_freq_hz,
						mors_if->ecsa_channel_info.op_bw_mhz,
						mors_if->ecsa_channel_info.pri_bw_mhz,
						mors_if->ecsa_channel_info.pri_1mhz_chan_idx,
						mors_if->ecsa_channel_info.s1g_operating_class,
						ecsa_ie_info->count,
						ecsa_ie_info->mode);
}

void morse_mac_process_ecsa_ie(struct morse *mors, struct ieee80211_vif *vif,
				struct sk_buff *skb)
{
	struct morse_vif *mors_if;
	struct ieee80211_ext *s1g_beacon = (struct ieee80211_ext *)skb->data;
	const u8 *ie;
	u8 *s1g_ies = s1g_beacon->u.s1g_beacon.variable;
	int header_length = s1g_ies - skb->data;
	int s1g_ies_len = skb->len - header_length;

	mors_if = ieee80211_vif_to_morse_vif(vif);

	if (s1g_beacon->frame_control & IEEE80211_FC_ANO) {
		s1g_ies += 1;
		s1g_ies_len -= 1;
	}

	ie = cfg80211_find_ie(WLAN_EID_EXT_CHANSWITCH_ANN,
						s1g_ies,
						s1g_ies_len);

	/* Process ECSA Info only once by checking operating channel */
	if (ie && !mors_if->ecsa_channel_info.op_chan_freq_hz) {
		struct ieee80211_ext_chansw_ie *ecsa_ie_info = (struct ieee80211_ext_chansw_ie *)(ie+2);

		ie = cfg80211_find_ie(WLAN_EID_CHANNEL_SWITCH_WRAPPER,
						s1g_ies,
						s1g_ies_len);

		if (ie)
			morse_mac_save_ecsa_chan_info(mors, mors_if, ecsa_ie_info, (ie+2), ie[1]);
		else
			morse_mac_save_ecsa_chan_info(mors, mors_if, ecsa_ie_info, NULL, 0);
	}
}

static bool morse_check_chan_info_after_scan(struct morse *mors, struct morse_vif *mors_vif)
{
	if (memcmp(&mors_vif->assoc_sta_channel_info, &mors->custom_configs.channel_info,
				sizeof(mors->custom_configs.channel_info)) == 0)
		return true;
	return false;
}

static inline bool morse_check_sta_associated(struct ieee80211_vif *vif, struct morse_vif *mors_vif)
{
	if ((vif->type == NL80211_IFTYPE_STATION) && mors_vif->is_sta_assoc)
		return true;
	else
		return false;
}

static inline bool morse_check_ibss_node_joined(struct ieee80211_vif *vif, struct morse_vif *mors_vif)
{
	if ((vif->type == NL80211_IFTYPE_ADHOC) && mors_vif->is_ibss_node_joined)
		return true;
	else
		return false;
}

static void morse_mac_ops_sw_scan_complete(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	int ret;
	struct morse *mors = hw->priv;
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;

	if (!mors->started) {
		morse_info(mors, "%s: Not started. Aborting\n", __func__);
		return;
	}

	mutex_lock(&mors->lock);
	if ((morse_check_sta_associated(vif, mors_vif) ||
		 morse_check_ibss_node_joined(vif, mors_vif)) &&
		!morse_check_chan_info_after_scan(mors, mors_vif)) {
		ret = morse_cmd_set_channel(mors,
				mors_vif->assoc_sta_channel_info.op_chan_freq_hz,
				mors_vif->assoc_sta_channel_info.pri_1mhz_chan_idx,
				mors_vif->assoc_sta_channel_info.op_bw_mhz,
				mors_vif->assoc_sta_channel_info.pri_bw_mhz);
		if (ret)
			morse_err(mors, "%s: morse_cmd_set_channel failed %d", __func__, ret);
	}

	mors->in_scan = false;
	ret = morse_cmd_cfg_scan(mors, false);
	if (ret)
		morse_err(mors, "%s: morse_cmd_cfg_scan failed %d", __func__, ret);

	mutex_unlock(&mors->lock);
}

static int
morse_mac_ops_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct morse *mors = hw->priv;
	struct morse_queue_params mqp;

	int ret;

	mutex_lock(&mors->lock);
	mqp.aci = map_mac80211q_2_morse_aci(queue);
	mqp.aifs = params->aifs;
	mqp.cw_max = params->cw_max;
	mqp.cw_min = params->cw_min;
	/* fw needs txop in units of usecs, not 32usecs - scale it */
	mqp.txop = params->txop << 5;

	morse_dbg(mors, "%s queue:%d txop:%d cw_min:%d cw_max:%d aifs:%d\n",
		__func__, mqp.aci, mqp.txop, mqp.cw_min, mqp.cw_max, mqp.aifs);

	ret = morse_cmd_cfg_qos(mors, &mqp);

	if (ret)
		morse_err(mors, "%s: morse_cmd_cfg_qos failed %d", __func__, ret);

	mutex_unlock(&mors->lock);

	return ret;
}

/* Helper function for getting last set bit on an extended bitmap
 *
 * Returns bit position with 0 being LSB, or -1 if bitmap is all 0's
 */
static s16 get_last_set_bit(const unsigned long *bitmap, u16 nlongs)
{
	s16 bit_pos = 0;
	u16 index = nlongs;

	do {
		bit_pos = fls(bitmap[--index]);
	} while (bit_pos == 0 && index > 0);

	return (bit_pos - 1) + (index * BITS_PER_LONG);
}

/**
 * Update values derived from the AID bitmap.
 * This function should be called on an AP every time the AID bitmap is updated
 */
static inline void morse_aid_bitmap_update(struct morse_ap *mors_ap)
{
	s16 largest_aid = get_last_set_bit(mors_ap->aid_bitmap, ARRAY_SIZE(mors_ap->aid_bitmap));

	if (largest_aid == -1)
		largest_aid = 0;

	mors_ap->largest_aid = largest_aid;
}

/*
 * This function updates remote peer capabilities using the custom config
 * based on the assumption that all nodes in the IBSS network have similar capabilties.
 */
static
void morse_mac_update_ibss_node_capabilities_using_defaults(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;

	rcu_read_lock();

	/* defaults - vif is IBSS creator or if no entry found in cssid list
	 * Update the STA capabilites using mors_vif->custom_configs
	 */
	mors_sta->ampdu_supported = mors_vif->custom_configs->enable_ampdu;
	mors_sta->trav_pilot_support = mors_vif->custom_configs->enable_trav_pilot;
	mors_sta->max_bw_mhz = mors_vif->custom_configs->channel_info.op_bw_mhz;

	/* mmrc is enabling all rates (MCS0-7 & 10) by default, assign rates to defaults */
	sta->ht_cap.mcs.rx_mask[0] = mcs_mask;

	/* Update VHT & SGI Capabilities */
	if (mors_vif->custom_configs->enable_sgi_rc) {
		sta->ht_cap.cap |= (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40);

		if (mors_vif->custom_configs->channel_info.op_bw_mhz >= 4) {
			sta->vht_cap.vht_supported = true;
			sta->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;

			if (mors_vif->custom_configs->channel_info.op_bw_mhz > 4) {
				sta->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
				sta->vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
			}
		}
	}

	rcu_read_unlock();
}

/*
 * This function updates remote peer capabilities using the beacon/probe response,
 * based on the assumption that all nodes in the network have similar capabilties.
 * The  main reason for this assumption is that every node in the IBSS network will
 * not have capabilities information of other nodes joining the network.
 * In IBSS network, nodes(ieee80211_sta/morse_sta) are created in mac80211 upon
 * reception of data frames with bssid same as this(receiving) node joined.
 */
static
void morse_mac_update_ibss_node_capabilities(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_s1g_cap *s1g_caps,
			struct morse_channel_info *info)
{

	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	bool sgi_enabled = 0;
	int sta_max_bw = 0;

	sgi_enabled = (s1g_caps->capab_info[0] & (S1G_CAP0_SGI_1MHZ | S1G_CAP0_SGI_2MHZ
						| S1G_CAP0_SGI_4MHZ | S1G_CAP0_SGI_8MHZ));
	sta_max_bw = (s1g_caps->capab_info[0] & S1G_CAP0_SUPP_CH_WIDTH);

	rcu_read_lock();

	if (s1g_caps->capab_info[7] & S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE)
		mors_vif->ctrl_resp_in_1mhz_en = true;

	/* AMPDU params info */
	if (s1g_caps->capab_info[5] & IEEE80211AH_AMPDU_SUPPORTED)
		mors_sta->ampdu_supported = true;
	else
		mors_sta->ampdu_supported = false;

	sta->ht_cap.ampdu_factor = (s1g_caps->capab_info[3] >> 3) & 0x3;
	sta->ht_cap.ampdu_density = (s1g_caps->capab_info[3] >> 5) & 0x7;

	mors_sta->trav_pilot_support = S1G_CAP2_GET_TRAV_PILOT(s1g_caps->capab_info[2]);

	mors_sta->max_bw_mhz = (sta_max_bw == S1G_CAP0_SUPP_16MHZ) ? 16 :
							 (sta_max_bw == S1G_CAP0_SUPP_8MHZ) ? 8 :
							 (sta_max_bw == S1G_CAP0_SUPP_4MHZ) ? 4 : 2;
	mors_vif->bss_color = S1G_CAP8_GET_COLOR(s1g_caps->capab_info[8]);

	/* mmrc is enabling all rates (MCS0-7 & 10) by default, assign rates to defaults */
	sta->ht_cap.mcs.rx_mask[0] = mcs_mask;

	if (sgi_enabled && mors_vif->custom_configs->enable_sgi_rc)
		sta->ht_cap.cap |= (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40);

	if (s1g_caps->capab_info[0] & S1G_CAP0_SGI_4MHZ)
		sta->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	if (info->op_bw_mhz >= 4) {
		if (sgi_enabled && mors_vif->custom_configs->enable_sgi_rc) {
			sta->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;

			if (info->op_bw_mhz > 4)
				sta->vht_cap.cap |=	IEEE80211_VHT_CAP_SHORT_GI_160;
		}
		sta->vht_cap.vht_supported = true;

		if (s1g_caps->capab_info[0] & S1G_CAP0_SGI_8MHZ)
			sta->vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
	}

	rcu_read_unlock();
}

void morse_ecsa_update_sta_caps(struct morse *mors, struct ieee80211_sta *sta)
{
	u8 op_bw = mors->custom_configs.channel_info.op_bw_mhz-1;

	rcu_read_lock();
	switch (op_bw) {
	case S1G_CHAN_1MHZ:
		if (mors->custom_configs.enable_sgi_rc)
			sta->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
		sta->ht_cap.cap &= ~IEEE80211_HT_CAP_SGI_40;
		sta->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		sta->vht_cap.vht_supported = false;
		break;
	case S1G_CHAN_2MHZ:
		if (mors->custom_configs.enable_sgi_rc)
			sta->ht_cap.cap |= (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40);
		sta->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		sta->vht_cap.vht_supported = false;
		break;
	case S1G_CHAN_4MHZ:
	case S1G_CHAN_8MHZ:
		/* configure vht caps */
		sta->vht_cap.cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_RXLDPC |
				IEEE80211_VHT_CAP_TXSTBC |
				IEEE80211_VHT_CAP_RXSTBC_1 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

		sta->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_7);
		sta->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_7);
		sta->vht_cap.vht_supported = true;

		if (mors->custom_configs.enable_sgi_rc)
			sta->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
		if (op_bw == S1G_CHAN_8MHZ) {
			if (mors->custom_configs.enable_sgi_rc)
				sta->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
			sta->vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		} else
			sta->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_160;
		break;
	default:
		morse_err(mors, "%s invalid op bw=%d\n", __func__,
			mors->custom_configs.channel_info.op_bw_mhz);
	}
	rcu_read_unlock();
}


/* API to process the bandwidth change notification from mac80211 */
static void morse_mac_ops_sta_rc_update(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta, u32 changed)
{
	struct morse *mors;
	struct morse_sta *mors_sta;
#ifdef CONFIG_MORSE_RC
	enum ieee80211_sta_state old_state;
	enum ieee80211_sta_state new_state;
#endif

	if (hw == NULL || vif == NULL || sta == NULL)
		return;

	mors = hw->priv;
	mors_sta = (struct morse_sta *)sta->drv_priv;

	morse_dbg(mors, "Rate control config updated (changed %u, peer address %pM)\n",
			   changed, sta->addr);

	if (!(changed & IEEE80211_RC_BW_CHANGED))
		return;

#ifdef CONFIG_MORSE_RC
	/* Simulate the disconnection and connection to reinitialize the sta in mmrc with new BW */
	old_state = IEEE80211_STA_ASSOC;
	new_state = IEEE80211_STA_NOTEXIST;

	morse_dbg(mors, "%s Remove sta, old_state=%d, new_state=%d, changed=0x%x, bw_changed=%d\n",
				__func__,
				old_state,
				new_state,
				changed,
				(changed & IEEE80211_RC_BW_CHANGED));
	mutex_lock(&mors->lock);

	morse_rc_sta_state_check(mors, sta, old_state, new_state);

	old_state = IEEE80211_STA_NOTEXIST;
	new_state = IEEE80211_STA_ASSOC;

	morse_ecsa_update_sta_caps(mors, sta);
	morse_dbg(mors, "%s Add sta, old_state=%d, new_state=%d\n",
				__func__,
				old_state,
				new_state);

	morse_rc_sta_state_check(mors, sta, old_state, new_state);

	mutex_unlock(&mors->lock);
#endif
}

static int
morse_mac_ops_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			enum ieee80211_sta_state old_state,
			enum ieee80211_sta_state new_state)
{
	struct morse *mors;
	struct morse_vif *mors_vif;
	struct morse_sta *mors_sta;
	u16 aid;
	int ret = 0;

	if (hw == NULL || vif == NULL || sta == NULL)
		return -EINVAL;

	mors = hw->priv;
	mors_vif = (struct morse_vif *)vif->drv_priv;
	mors_sta = (struct morse_sta *)sta->drv_priv;

	/* Ignore both NOTEXIST to NONE and NONE to NOTEXIST */
	if ((old_state == IEEE80211_STA_NOTEXIST && new_state == IEEE80211_STA_NONE) ||
	    (old_state == IEEE80211_STA_NONE && new_state == IEEE80211_STA_NOTEXIST))
		return 0;

	/* SW-5033: in IBSS mode, ignore any state transition originated by the network creator.
	 * Note: mac80211 will create two entries/peers/sta's for the network generator,
	 * one of them using the BSSID and the other using the actual peer MAC address. We
	 * can safely ignore the BSSID entry as it does not present an actual peer (and it
	 * will not have an IP anyway)
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC &&
		ether_addr_equal_unaligned(sta->addr, vif->bss_conf.bssid)) {
		return 0;
	}

	mutex_lock(&mors->lock);

	if ((old_state > IEEE80211_STA_NONE &&
		new_state <= IEEE80211_STA_NONE) &&
		mors_sta->already_assoc_req) {
		mors_sta->tx_ps_filter_en = false;
		morse_mac_save_sta_backup(mors, mors_vif, mors_sta);
		morse_vendor_reset_sta_transient_info(vif, mors_sta);
	}

	/* Always use WME (or QoS) for 802.11ah */
	rcu_read_lock();
	if (sta != NULL) {
		sta->wme = true;
		sta->ht_cap.ht_supported = true;
	}
	rcu_read_unlock();

	if (vif->type == NL80211_IFTYPE_STATION)
		aid = vif->bss_conf.aid;
	else if (vif->type == NL80211_IFTYPE_ADHOC)
		/* SW-4741: in IBSS mode, AID is always zero, and we can not use
		 * it as a unique ID. As a WAR, we overload the AID with the MAC
		 * address (lowest two octets) assuming those will always be unique
		 *
		 * TODO: make sure the AID passed to FW is never used as an index,
		 * but only used for lookup purposes (i.e., RAW will not work)
		 */
		aid = (((u16)sta->addr[4] << 8) | ((u16)sta->addr[5])) & 0x7FFF;
	else
		aid = sta->aid;

	if (vif->type == NL80211_IFTYPE_STATION && new_state > old_state &&
	    new_state == IEEE80211_STA_ASSOC)
		ret = morse_cmd_set_bss_color(mors, mors_vif,
					      mors_vif->bss_color);

	if (!ret)
		ret = morse_cmd_sta_state(mors, mors_vif, aid, sta, new_state);

	if (old_state < new_state && new_state == IEEE80211_STA_ASSOC)
		morse_mac_restore_sta_backup(mors, mors_vif, mors_sta, sta->addr);

	if (new_state == IEEE80211_STA_ASSOC) {
		int i;

		for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
			mors_sta->tid_start_tx[i] = false;
			mors_sta->tid_tx[i] = false;
		}

		/* Fetch beacon/probe resp using bssid for S1G caps and update the
		 * STA subbands (HT/VHT) Capabilities
		 */
		if (vif->type == NL80211_IFTYPE_ADHOC) {
			struct ieee80211_s1g_cap s1g_caps;
			u8 bssid[ETH_ALEN];

			memcpy(bssid, vif->bss_conf.bssid, ETH_ALEN);

			/* Apply sta capabilities using beacon/probe reposnse */
			if (morse_dot11ah_find_s1g_caps_for_bssid(bssid, &s1g_caps)) {
				struct morse_channel_info info;

				morse_info(mors, "Update RC of associated peer %pM using beacon\n", sta->addr);
				morse_mac_find_channel_info_for_bssid(bssid, &info);
				morse_mac_update_ibss_node_capabilities(hw, vif, sta, &s1g_caps, &info);
			} else {
				morse_info(mors, "Set defaults and update RC of associated peer %pM\n", sta->addr);
				morse_mac_update_ibss_node_capabilities_using_defaults(hw, vif, sta);
			}
		}
	}

#ifdef CONFIG_MORSE_RC
	morse_rc_sta_state_check(mors, sta, old_state, new_state);
#endif

	ether_addr_copy(mors_sta->addr, sta->addr);
	mors_sta->state = new_state;

	/* As per the mac80211 documentation, this callback must not fail
	 * for down transitions of state.
	 */
	if (new_state < old_state)
		ret = 0;

	if ((new_state > old_state) &&
		(new_state == IEEE80211_STA_ASSOC)) {
		morse_info(mors, "Station associated %pM\n", sta->addr);

		if (vif->type == NL80211_IFTYPE_AP) {
			if (test_and_set_bit(aid, mors_vif->ap->aid_bitmap))
				morse_warn(mors, "Station associated with duplicate AID %d\n", aid);
			else {
				mors_vif->ap->num_stas++;
				list_add(&mors_sta->list, &mors_vif->ap->stas);
			}


			morse_aid_bitmap_update(mors_vif->ap);
		}

		if (vif->type == NL80211_IFTYPE_STATION) {
			memcpy(&mors_vif->assoc_sta_channel_info, &mors->custom_configs.channel_info,
					sizeof(mors->custom_configs.channel_info));

			/* Reset channel info */
			memset(&mors_vif->ecsa_channel_info, 0, sizeof(mors_vif->ecsa_channel_info));

			mors_vif->ecsa_chan_configured = false;
		}
	}

	if ((new_state < old_state) && new_state == IEEE80211_STA_NONE) {
		morse_info(mors, "Station disassociated %pM\n", sta->addr);

		/* Reset channel info */
		if (vif->type == NL80211_IFTYPE_STATION) {
			memset(&mors_vif->ecsa_channel_info, 0, sizeof(mors_vif->ecsa_channel_info));
			mors_vif->ecsa_chan_configured = false;
		}

		if (vif->type == NL80211_IFTYPE_AP) {
			if (test_and_clear_bit(aid, mors_vif->ap->aid_bitmap)) {
				mors_vif->ap->num_stas--;
				list_del(&mors_sta->list);
			} else
				morse_warn(mors, "Non-existant station disassociated with AID %d\n", aid);

			morse_aid_bitmap_update(mors_vif->ap);
		}
	}

	if (enable_dhcpc_offload && (vif->type == NL80211_IFTYPE_STATION) &&
		(new_state > old_state) && (new_state == IEEE80211_STA_ASSOC)) {

		if (morse_cmd_dhcpc_enable(mors, mors_vif->id) < 0)
			morse_warn(mors, "Failed to enable in-chip DHCP client\n");
	}

	mutex_unlock(&mors->lock);

	mutex_lock(&mors_vif->twt.lock);
	if (new_state > old_state && (new_state == IEEE80211_STA_AUTHORIZED))
		morse_twt_install_pending_agreements(mors, mors_vif);

	/* Since agreements are negotiated in the (re)assoc frames, remove sta data if we become
	 * disassociated.
	 */
	if ((old_state >= IEEE80211_STA_ASSOC) && (new_state < IEEE80211_STA_ASSOC))
		morse_twt_sta_remove_addr(mors, mors_vif, sta->addr);

	/* If a STA disconnects remove pending TWT events. In the case where an association attempt
	 * fails, mac80211 on the next attempt will set the STA state to IEEE80211_STA_NONE before
	 * immediately setting it back to IEEE80211_STA_ASSOC. In this case we don't purge events
	 * from the queue.
	 */
	if ((new_state < old_state) && (new_state == IEEE80211_STA_NONE) && !mors_sta->already_assoc_req)
		morse_twt_event_queue_purge(mors, mors_vif, sta->addr);

	mutex_unlock(&mors_vif->twt.lock);

	if ((new_state > old_state) && (new_state >= IEEE80211_STA_ASSOC))
		morse_twt_handle_event(mors_vif, sta->addr);

	/* If a STA is added or removed from the AP while RAW is enabled update the RAW assignments. */
	if ((vif->type == NL80211_IFTYPE_AP) && mors->custom_configs.raw.enabled) {
		if (((new_state > old_state) && (new_state == IEEE80211_STA_ASSOC)) ||
			((new_state < old_state) && (new_state == IEEE80211_STA_NONE))) {
			morse_dbg(mors, "Schedule RAW AID refresh\n");
			schedule_work(&mors->custom_configs.raw.refresh_aids_work);
		}
	}

	return ret;
}

static int
morse_mac_ops_ampdu_action(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	struct morse *mors = hw->priv;
	struct ieee80211_sta *sta = params->sta;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	bool amsdu_supported = params->amsdu;
	u16 buf_size = min_t(u16, params->buf_size,
				DOT11AH_BA_MAX_MPDU_PER_AMPDU);
	int ret = 0;
	u16 aid;

	if (!mors->custom_configs.enable_ampdu) {
		morse_dbg(mors, "%s %pM.%d Denying AMPDU because not enabled\n",
				__func__, mors_sta->addr, tid);
		return -EINVAL;
	}

	if (!mors_sta->ampdu_supported) {
		morse_dbg(mors, "%s %pM.%d Denying AMPDU because STA doesn't support it\n",
				__func__, mors_sta->addr, tid);
		return -EINVAL;
	}

	if (vif->type == NL80211_IFTYPE_STATION)
		aid = vif->bss_conf.aid;
	else if (vif->type == NL80211_IFTYPE_ADHOC)
		/* SW-4741: in IBSS mode, AID is always zero, and we can not use
		 * it as a unique ID. As a WAR, we overload the AID with the MAC
		 * address (lowest two octets) assuming those will always be unique
		 *
		 * TODO: make sure the AID passed to FW is never used as an index,
		 * but only used for lookup purposes (i.e., RAW will not work)
		 */
		aid = (((u16)sta->addr[4] << 8) | ((u16)sta->addr[5])) & 0x7FFF;
	else
		aid = sta->aid;

	mutex_lock(&mors->lock);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		morse_info(mors, "%s %pM.%d A-MPDU RX start\n",
				__func__, mors_sta->addr, tid);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		morse_info(mors, "%s %pM.%d A-MPDU RX stop\n",
				__func__, mors_sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_START:
		morse_info(mors, "%s %pM.%d A-MPDU TX start\n",
				__func__, mors_sta->addr, tid);
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		morse_info(mors, "%s %pM.%d A-MPDU TX flush\n",
				__func__, mors_sta->addr, tid);
		mors_sta->tid_start_tx[tid] = false;
		mors_sta->tid_tx[tid] = false;
		mors_sta->tid_params[tid] = 0;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		morse_info(mors, "%s %pM.%d A-MPDU TX oper\n",
				__func__, mors_sta->addr, tid);
		mors_sta->tid_tx[tid] = true;
		/* Max reorder buffer is stored as little-Endian and 0-indexed */
		if (buf_size == 0) {
			morse_err(mors, "%s %pM.%d A-MPDU Invalid buf size\n",
				__func__, mors_sta->addr, tid);
			break;
		}
		mors_sta->tid_params[tid] =
			BMSET(buf_size - 1, TX_INFO_TID_PARAMS_MAX_REORDER_BUF) |
			BMSET(1, TX_INFO_TID_PARAMS_AMPDU_ENABLED) |
			BMSET(amsdu_supported, TX_INFO_TID_PARAMS_AMSDU_SUPPORTED);
		break;
	default:
		morse_err(mors,
			  "%s %pM.%d Invalid command %d, ignoring\n",
					__func__, mors_sta->addr, tid, action);
	}

	mutex_unlock(&mors->lock);
	return ret;
}

static int
morse_mac_ops_set_key(struct ieee80211_hw *hw,
			enum set_key_cmd cmd,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key)
{
	u16 aid;
	int ret = -EOPNOTSUPP;
	struct morse *mors = hw->priv;
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;

	mutex_lock(&mors->lock);

	if (vif->type == NL80211_IFTYPE_STATION) {
		aid = vif->bss_conf.aid;
	} else if (vif->type == NL80211_IFTYPE_ADHOC) {
		/* SW-4741: in IBSS mode, AID is always zero, and we can not use
		 * it as a unique ID. As a WAR, we overload the AID with the MAC
		 * address (lowest two octets) assuming those will always be unique
		 *
		 * TODO: make sure the AID passed to FW is never used as an index,
		 * but only used for lookup purposes (i.e., RAW will not work)
		 */
		if (sta != NULL)
			aid = (((u16)sta->addr[4] << 8) | ((u16)sta->addr[5])) & 0x7FFF;
		else
			aid = 0;
	} else if (sta != NULL) {
		aid = sta->aid;
	} else {
		/* Is a group key - AID is unused */
		MORSE_WARN_ON((key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
		aid = 0;
	}

	switch (cmd) {
	case SET_KEY:
	{
		enum morse_key_cipher cipher;
		enum morse_aes_key_length length;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		{
			cipher = MORSE_KEY_CIPHER_AES_CCM;
			break;
		}
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
		{
			cipher = MORSE_KEY_CIPHER_AES_GCM;
			break;
		}
		case WLAN_CIPHER_SUITE_AES_CMAC:
		{
			/* DEAD CODE, to latter be enabled */
			cipher = MORSE_KEY_CIPHER_AES_CMAC;
			/* Currently CMAC is not supported, avoid failed commands */
			ret = -EOPNOTSUPP;
			goto exit;
		}
		default:
		{
			/* Cipher suite currently not supported */
			ret = -EOPNOTSUPP;
			goto exit;
		}
		}

		switch (key->keylen) {
		case 16:
		{
			length = MORSE_AES_KEY_LENGTH_128;
			break;
		}
		case 32:
		{
			length = MORSE_AES_KEY_LENGTH_256;
			break;
		}
		default:
		{
			/* Key length not supported */
			ret = -EOPNOTSUPP;
			goto exit;
		}
		}

		ret = morse_cmd_install_key(mors, mors_vif,
			aid, key, cipher, length);
		break;
	}
	case DISABLE_KEY:
	{
		ret = morse_cmd_disable_key(mors, mors_vif, aid, key);
		if (ret) {
			/* Must return 0 */
			MORSE_WARN_ON(1);
			ret = 0;
		}
		break;
	}
	default:
		MORSE_WARN_ON(1);
	}

	if (ret) {
		morse_dbg(mors, "%s Falling back to software crypto\n",
			__func__);
		ret = 1;
	}

exit:
	mutex_unlock(&mors->lock);
	return ret;
}

static void morse_mac_ops_rfkill_poll(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;
	(void)mors;
}

static void morse_mac_ops_flush(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				u32 queues, bool drop)
{
	struct morse *mors = hw->priv;
	(void)mors;
}

static u64 morse_mac_ops_get_tsf(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct morse *mors = hw->priv;
	(void)mors;
	return 0;
}

static void morse_mac_ops_set_tsf(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, u64 tsf)
{
	struct morse *mors = hw->priv;
	(void)mors;
}

static int morse_mac_ops_tx_last_beacon(struct ieee80211_hw *hw)
{
	/* SW-4741: in IBSS mode, this should return TRUE only if this
	 * node is the one that generates beacons (for the current beacon
	 * interval). This will help host to decide if this node should
	 * reply probe requests or not. For now, as all nodes are acting
	 * as AP (sending beacons), then we can force this to TRUE
	 * TODO: decide when should we cancel beacon and return FALSE here
	 */
	return 1;
}

static int morse_mac_join_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct morse *mors = hw->priv;
	const struct morse_dot11ah_channel *chan_s1g =
		morse_dot11ah_channel_chandef_to_s1g(&vif->bss_conf.chandef);
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;
	u8 bssid[ETH_ALEN], fc_bss_bw_subfield = 0;

	/* Ensure chan_s1g != NULL, to protect against HT to S1G channel mismatch */
	int op_bw_mhz =
		(chan_s1g && (chan_s1g->ch.flags & IEEE80211_CHAN_8MHZ)) ? 8 :
		(chan_s1g && (chan_s1g->ch.flags & IEEE80211_CHAN_4MHZ)) ? 4 :
		(chan_s1g && (chan_s1g->ch.flags & IEEE80211_CHAN_2MHZ)) ? 2 :
		(chan_s1g && (chan_s1g->ch.flags & IEEE80211_CHAN_1MHZ)) ? 1 :
		-1;

	morse_info(mors, "Joined IBSS:\n"
		" * SSID           : %s\n"
		" * BSSID          : %pM\n"
		" * Address        : %pM\n"
		" * 5G Channel     : Ch %u, Freq %uKHz\n"
		" * S1G Channel    : Ch %d, Freq %dKHz, Width %dMHz\n"
		" * Regulatory     : %s\n"
		" * IBSS Creator?  : %s\n",
		vif->bss_conf.ssid,
		vif->bss_conf.bssid,
		vif->addr,
		vif->bss_conf.chandef.chan->hw_value,
		vif->bss_conf.chandef.chan->center_freq,
		chan_s1g ? chan_s1g->ch.hw_value : -1,
		chan_s1g ? ieee80211_channel_to_khz(&chan_s1g->ch) : -1,
		op_bw_mhz,
		morse_dot11ah_get_region_str(),
		vif->bss_conf.ibss_creator ? "Yes" : "No");


	/* Update channel only if it is not ibss creator*/
	if (vif->bss_conf.ibss_creator == false) {
		u32 changed = 0;

		/* mac80211 updating bssid after configuring the channel(morse_mac_ops_config) to driver.
		 * we have now bssid updated in vif->bss_conf, update(mors->custom_configs.channel_info)
		 * operating bw, prim chan bw and idx. This is required for selecting right sub band in
		 * transmission of mgmt and data packets.
		 */

		changed |= IEEE80211_CONF_CHANGE_CHANNEL;
		morse_mac_ops_config(hw, changed);
	}

	memcpy(bssid, vif->bss_conf.bssid, ETH_ALEN);

	mutex_lock(&mors->lock);
	mors_vif->is_ibss_node_joined = true;
	if (morse_dot11ah_find_bss_bw(bssid, &fc_bss_bw_subfield) &&
						 MORSE_IS_FC_BSS_BW_SUBFIELD_VALID(fc_bss_bw_subfield)) {
		mors_vif->custom_configs->channel_info.pri_bw_mhz = s1g_fc_bss_bw_lookup_min[fc_bss_bw_subfield];
	} else {
		struct morse_channel_info info;

		if (morse_mac_find_channel_info_for_bssid(bssid, &info))
			mors_vif->custom_configs->channel_info.pri_bw_mhz = info.pri_bw_mhz;
	}
	memcpy(&mors_vif->assoc_sta_channel_info, &mors->custom_configs.channel_info,
										sizeof(mors->custom_configs.channel_info));

	mutex_unlock(&mors->lock);
	return 0;
}

static void morse_mac_leave_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct morse *mors = hw->priv;
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;

	morse_info(mors, "Leaving IBSS:bssid=%pM\n",
		vif->bss_conf.bssid);

	mutex_lock(&mors->lock);
	mors_vif->is_ibss_node_joined = false;
	mutex_unlock(&mors->lock);
}

static int morse_mac_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	int ret = -EINVAL;
	struct morse *mors = hw->priv;

	mutex_lock(&mors->lock);

	ret = morse_cmd_set_frag_threshold(mors, value);

	if (ret)
		morse_err(mors, "morse_cmd_set_frag_treshold failed %d", ret);

	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_mac_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	/* When Minstrel is not used, Linux checks if .set_rts_threshold is registered.
	 * MMRC follows Minstrel to apply RTS on retry rates so does not use this function.
	 * So create this function to pass the check and may apply different algorithm later.
	 */
#ifdef CONFIG_MORSE_RC
	struct morse *mors = hw->priv;

	mors->rts_threshold = value;
#endif
	return 0;
}

#ifdef CONFIG_MORSE_RC
static void
morse_sta_tx_rate_stats(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
						struct ieee80211_sta *sta, struct station_info *sinfo)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct morse *mors = hw->priv;

	if (msta) {
		sinfo->txrate.mcs = msta->last_sta_tx_rate.rate;
		switch (msta->last_sta_tx_rate.bw) {
		case MMRC_BW_1MHZ:
			sinfo->txrate.flags = (RATE_INFO_FLAGS_MCS);
			break;
		case MMRC_BW_2MHZ:
			sinfo->txrate.flags = (RATE_INFO_FLAGS_MCS);
			sinfo->txrate.bw = RATE_INFO_BW_40;
			break;
		case MMRC_BW_4MHZ:
			sinfo->txrate.flags = (RATE_INFO_FLAGS_VHT_MCS);
			sinfo->txrate.bw = RATE_INFO_BW_80;
			sinfo->txrate.nss = 1;
			break;
		case MMRC_BW_8MHZ:
			sinfo->txrate.flags = (RATE_INFO_FLAGS_VHT_MCS);
			sinfo->txrate.bw = RATE_INFO_BW_160;
			sinfo->txrate.nss = 1;
			break;
		default:
			break;
		}
		if (msta->last_sta_tx_rate.guard == MMRC_GUARD_SHORT)
			sinfo->txrate.flags |= (RATE_INFO_FLAGS_SHORT_GI);

		morse_dbg(mors, "mcs: %d, bw: %d, flag: 0x%x\n", msta->last_sta_tx_rate.rate,
					msta->last_sta_tx_rate.bw, sinfo->txrate.flags);
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}
}
#endif

static struct ieee80211_ops mors_ops = {
	.tx = morse_mac_ops_tx,
	.start = morse_mac_ops_start,
	.stop = morse_mac_ops_stop,
	.add_interface = morse_mac_ops_add_interface,
	.remove_interface = morse_mac_ops_remove_interface,
	.config = morse_mac_ops_config,
	.bss_info_changed = morse_mac_ops_bss_info_changed,
	.configure_filter = morse_mac_ops_configure_filter,
	.sw_scan_start = morse_mac_ops_sw_scan_start,
	.sw_scan_complete = morse_mac_ops_sw_scan_complete,
	.conf_tx = morse_mac_ops_conf_tx,
	.sta_state = morse_mac_ops_sta_state,
	.ampdu_action = morse_mac_ops_ampdu_action,
	.rfkill_poll = morse_mac_ops_rfkill_poll,
	.flush = morse_mac_ops_flush,
	.get_tsf = morse_mac_ops_get_tsf,
	.set_tsf = morse_mac_ops_set_tsf,
	.get_survey = morse_mac_ops_get_survey,
	.set_key = morse_mac_ops_set_key,
	.tx_last_beacon = morse_mac_ops_tx_last_beacon,
	.join_ibss = morse_mac_join_ibss,
	.leave_ibss = morse_mac_leave_ibss,
	.sta_rc_update = morse_mac_ops_sta_rc_update,
	.set_frag_threshold = morse_mac_set_frag_threshold,
	.set_rts_threshold = morse_mac_set_rts_threshold,
#ifdef CONFIG_MORSE_RC
	.sta_statistics = morse_sta_tx_rate_stats,
#endif

};

int morse_mac_send_vendor_wake_action_frame(struct morse *mors,
	const u8 *dest_addr, const u8 *payload, int payload_len)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ieee80211_mgmt *action;
	struct ieee80211_sta *sta;
	const u8 subcategory = MORSE_VENDOR_SPECIFIC_FRAME_SUBCAT_WAKE;
	u8 *pos;

	int frame_len = IEEE80211_MIN_ACTION_SIZE + sizeof(morse_oui) +
		sizeof(subcategory) + payload_len;

	skb = dev_alloc_skb(frame_len + mors->hw->extra_tx_headroom);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, mors->hw->extra_tx_headroom);
	action = (struct ieee80211_mgmt *) skb_put(skb, IEEE80211_MIN_ACTION_SIZE);
	memset(action, 0, IEEE80211_MIN_ACTION_SIZE);

	/* It has been agreed that MM action frames get sent out at VO aci */
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);

	rcu_read_lock();
	sta = ieee80211_find_sta_by_ifaddr(mors->hw,
		dest_addr,
		morse_get_vif(mors)->addr);
	rcu_read_unlock();

	if (sta && sta->mfp) {
		if (no_hwcrypt) {
			morse_warn(mors, "Can't send protected action frame with soft encryption\n");
			goto error_free_skb;
		}

		action->u.action.category = WLAN_CATEGORY_VENDOR_SPECIFIC_PROTECTED;
		action->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
			IEEE80211_STYPE_ACTION | IEEE80211_FCTL_PROTECTED);
	} else {
		action->u.action.category = WLAN_CATEGORY_VENDOR_SPECIFIC;
		action->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
			IEEE80211_STYPE_ACTION);
	}

	memcpy(action->da, dest_addr, ETH_ALEN);
	memcpy(action->sa, morse_get_vif(mors)->addr, ETH_ALEN);
	memcpy(action->bssid, morse_get_vif(mors)->bss_conf.bssid, ETH_ALEN);

	pos = skb_put(skb, sizeof(morse_oui));
	memcpy(pos, morse_oui, sizeof(morse_oui));

	pos = skb_put(skb, sizeof(subcategory));
	memcpy(pos, &subcategory, sizeof(subcategory));

	pos = skb_put(skb, payload_len);
	memcpy(pos, payload, payload_len);

	/* Marking the packet as 'TX_FILTERED' will cause it
	 * to be rescheduled internal to mac80211. After this,
	 * the skb will go through the normal tx path.
	 */
	info = IEEE80211_SKB_CB(skb);
	info->control.vif = morse_get_vif(mors);
	info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	ieee80211_tx_status(mors->hw, skb);

	return 0;

error_free_skb:
	morse_mac_skb_free(mors, skb);
	return -1;
}

void
morse_mac_send_buffered_bc(struct morse *mors)
{
	int count = max_mc_frames;
	struct sk_buff *bc_frame;

	bc_frame = ieee80211_get_buffered_bc(mors->hw, morse_get_vif(mors));

	while (bc_frame != NULL) {
		morse_mac_ops_tx(mors->hw, NULL, bc_frame);

		if (count > 0)
			count--;
		if ((max_mc_frames > 0) && (count <= 0))
			break;
		bc_frame = ieee80211_get_buffered_bc(mors->hw, morse_get_vif(mors));
	}
}


static void
morse_mac_rx_status(struct morse *mors,
		    struct sk_buff *p,
		    struct morse_skb_rx_status *hdr_rx_status,
		    struct ieee80211_rx_status *rx_status)
{
	enum nl80211_chan_width chan_width = mors->hw->conf.chandef.width;

	/* fill in TSF and flag its presence */
#ifdef MORSE_MAC_CONFIG_RX_STATUS_MACTIME
	rx_status->mactime = brcms_c_recover_tsf64(wlc, rxh);
	rx_status->flag |= RX_FLAG_MACTIME_START;
#endif

	if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_DECRYPTED)
		rx_status->flag |= RX_FLAG_DECRYPTED;

	rx_status->band = NL80211_BAND_5GHZ;
	rx_status->freq =
		ieee80211_channel_to_frequency(
			mors->channel_num_80211n, rx_status->band);

#if KERNEL_VERSION(4, 12, 0) <= MAC80211_VERSION_CODE
	rx_status->nss = 1;
#else
	rx_status->vht_nss = 1;
#endif
	rx_status->antenna = 1;
	rx_status->signal = le16_to_cpu(hdr_rx_status->rssi);

	/* If MCS10, convert to MCS0 to keep rate control happy. */
	if (hdr_rx_status->rate == 10) {
		rx_status->rate_idx = 0;
		mors->debug.mcs_stats_tbl.mcs10.rx_count++;
	} else {
		rx_status->rate_idx = hdr_rx_status->rate;
		if (hdr_rx_status->rate == 0)
			mors->debug.mcs_stats_tbl.mcs0.rx_count++;
	}

	if (MORSE_RX_STATUS_FLAGS_SGI_GET(hdr_rx_status->flags))
#if KERNEL_VERSION(4, 12, 0) <= MAC80211_VERSION_CODE
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
#else
		rx_status->flag |= RX_FLAG_SHORT_GI;
#endif

	if ((chan_width != NL80211_CHAN_WIDTH_80) &&
	    (chan_width != NL80211_CHAN_WIDTH_160)) {
#if KERNEL_VERSION(4, 12, 0) <= MAC80211_VERSION_CODE
		rx_status->encoding = RX_ENC_HT;
		rx_status->bw = morse_mac_rx_bw_to_skb_ht(mors, hdr_rx_status->bw_mhz);
#endif
	} else {
#if KERNEL_VERSION(4, 12, 0) <= MAC80211_VERSION_CODE
		rx_status->encoding = RX_ENC_VHT;
		rx_status->bw = morse_mac_rx_bw_to_skb_vht(mors, hdr_rx_status->bw_mhz);
#else
		if (chan_width == NL80211_CHAN_WIDTH_160)
			rx_status->vht_flag |= RX_VHT_FLAG_160MHZ;
		else if (chan_width == NL80211_CHAN_WIDTH_80)
			rx_status->vht_flag |= RX_VHT_FLAG_80MHZ;
#endif
	}
}

static void morse_s1g_to_11n_rx_packet(struct morse *mors, struct sk_buff *skb, int length_11n,
						struct dot11ah_ies_mask *ies_mask)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211_vif *vif;

	hdr = (struct ieee80211_hdr *)skb->data;

	/**
	 * Needs to be revisited for handling other management frames
	 */
	if (ieee80211_is_assoc_resp(hdr->frame_control) ||
		ieee80211_is_reassoc_resp(hdr->frame_control) ||
		ieee80211_is_probe_resp(hdr->frame_control))
		vif = (is_multi_interface_mode() ? morse_get_sta_vif(mors) : morse_get_vif(mors));
	else
		vif = (is_multi_interface_mode() ? morse_get_ap_vif(mors) : morse_get_vif(mors));

	morse_dot11ah_s1g_to_11n_rx_packet(vif, skb, length_11n, ies_mask);
}

static void morse_mac_tx_probe_req_change_seq(struct morse *mors)
{
	int ret;
	struct morse_skbq *mq;
	struct sk_buff *skb = NULL;
	struct ieee80211_mgmt *probe_req;
	struct ieee80211_vif *vif = morse_get_sta_vif(mors);
	struct morse_vif *mors_if;
	struct morse_skb_tx_info tx_info = {0};
	int tx_bw_mhz = 1;

	if (!enable_bcn_change_seq_monitor ||
		(!vif) || (vif->type != NL80211_IFTYPE_STATION))
		return;

	mors_if = (struct morse_vif *)vif->drv_priv;

	if (!mors_if->is_sta_assoc)
		return;

	/**
	 * Template probe request buffer is expected to be populated in
	 * morse_mac_ops_bss_info_changed event handler and use it here. Below portion
	 * is required only for any handling any corner cases  like update of beacon
	 * change seq is detected, just immediately after the station is associated
	 * and bss_info handler is not invoked. If it's null, request from here & use it.
	 */
	if (!mors_if->probe_req_buf) {
		mors_if->probe_req_buf = ieee80211_ap_probereq_get(mors->hw, vif);

		if (mors_if->probe_req_buf == NULL) {
			morse_err(mors, "%s: ieee80211_ap_probereq_get failed\n", __func__);
			goto exit;
		}
	}

	skb = skb_copy(mors_if->probe_req_buf, GFP_ATOMIC);

	if (skb == NULL) {
		morse_err(mors, "%s: SKB for probereq failed\n", __func__);
		goto exit;
	}

	mq = mors->cfg->ops->skbq_mgmt_tc_q(mors);
	if (!mq) {
		morse_err(mors, "%s: mors->cfg->ops->skbq_mgmt_tc_q failed, no matching Q found\n",
			__func__);
		goto exit;
	}

	probe_req = (struct ieee80211_mgmt *) skb->data;

	/* Convert the packet to s1g format */
	if (morse_mac_pkt_to_s1g(mors, skb, &tx_bw_mhz) < 0) {
		morse_err(mors, "Failed to convert S1G probe req.. dropping\n");
		goto exit;
	}

	/* Always send back at 1mhz */
	morse_fill_tx_info(mors, &tx_info, skb, mors_if, tx_bw_mhz);

	morse_dbg(mors,
		"Generated Probe Req for Beacon change sequence\n");

	ret = morse_skbq_skb_tx(mq, skb, &tx_info, MORSE_SKB_CHAN_MGMT);
	if (ret) {
		morse_err(mors, "%s failed to send Unicast Probe req for Bcn change Seq\n", __func__);
		goto exit;
	}
	return;
exit:
	if (skb)
		dev_kfree_skb_any(skb);
}

static void morse_mac_send_probe_req_tasklet(unsigned long data)
{
	struct morse *mors = (struct morse *)data;

	morse_mac_tx_probe_req_change_seq(mors);
}

int morse_send_probe_req_enable(struct morse *mors, bool enable)
{
	if (enable)
		tasklet_enable(&mors->send_probe_req);
	else
		tasklet_disable(&mors->send_probe_req);
	return 0;
}

int morse_send_probe_req_init(struct morse *mors)
{
	tasklet_init(&mors->send_probe_req, morse_mac_send_probe_req_tasklet,
		     (unsigned long)mors);
	tasklet_disable(&mors->send_probe_req);
	return 0;
}

void morse_send_probe_req_finish(struct morse *mors)
{
	tasklet_kill(&mors->send_probe_req);
}

static void morse_mac_process_twt_ie(struct morse *mors,
				     struct morse_vif *mors_vif,
				     struct ie_element *element,
				     const u8 *src_addr)
{
	int ret;
	struct morse_twt_event *event = kmalloc(sizeof(*event), GFP_KERNEL);

	if (!event)
		return;

	ret = morse_twt_parse_ie(mors_vif, element, event, src_addr);

	if (!ret) {
		morse_twt_dump_event(mors, event);
		/* Add event to queue. */
		morse_twt_queue_event(mors, mors_vif, event);
	} else {
		morse_warn(mors, "Failed to parse TWT IE\n");
		kfree(event);
	}
}

/* Initiated the channel switch when beacon count down is completed */
void morse_mac_esca_beacon_tx_done(struct morse *mors, struct sk_buff *skb)
{
	struct ieee80211_vif *vif = morse_get_vif(mors);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data + sizeof(struct morse_buff_skb_header));
	struct morse_vif *mors_if;
	unsigned long timeout;

	if (!vif) {
		morse_err(mors, "ECSA: %s NULL vif\n", __func__);
		return;
	}

	mors_if = (struct morse_vif *)vif->drv_priv;
	if (vif->csa_active && ieee80211_is_s1g_beacon(hdr->frame_control)) {
#if KERNEL_VERSION(5, 10, 0) < MAC80211_VERSION_CODE
		if (ieee80211_beacon_cntdwn_is_complete(vif)) {
#else
		if (ieee80211_csa_is_complete(vif)) {
#endif
			timeout = jiffies + msecs_to_jiffies(BEACON_REQUEST_GRACE_PERIOD_MS);

			morse_info(mors, "ECSA:%s Countdown is comp, Trigger Chan Switch, ts=%ld, to=%ld\n",
							 __func__, jiffies, timeout);
			mod_timer(&mors_if->chswitch_timer, timeout);
		}
	} else if (mors_if->ecsa_chan_configured) {
		/* Add grace period + 1ms to make sure that the beacon is sent out */
		timeout = msecs_to_jiffies(BEACON_REQUEST_GRACE_PERIOD_MS+1);
		/*
		 * We will configure channel again after sending beacon in new channel
		 * to perform PHY calibration.
		 */
		morse_info(mors, "ECSA:%s Configure ECSA Chan ts=%ld, to=%ld\n",
							__func__, jiffies, timeout);
		schedule_delayed_work(&mors_if->ecsa_chswitch_work, timeout);
		mors_if->ecsa_chan_configured = false;
		/* Reset channel info */
		memset(&mors_if->ecsa_channel_info, 0, sizeof(mors_if->ecsa_channel_info));
		mors_if->mask_ecsa_info_in_beacon = false;
	}
}

void morse_mac_process_bcn_change_seq_tx_finish(struct morse *mors,
			struct sk_buff *skb)
{
	struct morse_vif *mors_if = NULL;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_vif *vif = morse_get_vif(mors);

	if (!vif) {
		morse_err(mors, "ECSA: %s NULL vif\n", __func__);
		return;
	}
	mors_if = (struct morse_vif *)vif->drv_priv;

	/*
	 * Check if probe req frame to be sent after STA detected
	 * update in beacon change sequence number and notified mac80211.
	 * mac80211 will send out QoS NULL with PM clear and on completion
	 * of QoS NULL data, here schedule to send unicast/directed probe req
	 */
	if (mors_if &&
		mors_if->waiting_for_probe_req_sched &&
		(vif->type == NL80211_IFTYPE_STATION) &&
		mors_if->is_sta_assoc &&
		(ieee80211_is_nullfunc(hdr->frame_control) ||
		ieee80211_is_qos_nullfunc(hdr->frame_control))) {
		morse_info(mors, "%s: Send probe req for updated beacon\n", __func__);
		morse_mac_schedule_probe_req(mors);
	}
}

int morse_mac_skb_recv(struct morse *mors, struct sk_buff *skb, u8 channel,
		       struct morse_skb_rx_status *hdr_rx_status)
{
	struct ieee80211_hw *hw = mors->hw;
	struct ieee80211_rx_status rx_status;
	const struct ieee80211_mgmt *hdr = NULL;
	struct dot11ah_ies_mask *ies_mask = NULL;
	struct ieee80211_vif *vif = morse_get_vif(mors);
	int length_11n;
	struct morse_vif *mors_if = vif ? ((struct morse_vif *)vif->drv_priv) : NULL;

	if (!mors->started) {
		morse_mac_skb_free(mors, skb);
		goto exit;
	}

	memset(&rx_status, 0, sizeof(rx_status));

	morse_watchdog_refresh(mors);

#ifdef CONFIG_MORSE_MONITOR
	if (mors->hw->conf.flags & IEEE80211_CONF_MONITOR) {
		morse_mon_rx(mors, skb, hdr_rx_status);
		/* If we have a monitor interface, don't bother doing any
		 * other work on the SKB as we only support a single interface
		 */
		morse_mac_skb_free(mors, skb);
		goto exit;
	}
#endif

	ies_mask = morse_dot11ah_ies_mask_alloc();
	if (ies_mask == NULL)
		goto exit;

	/* Check if the S1G frame is a different size, and
	 * if it is, ensure the space is correct
	 */
	length_11n = morse_dot11ah_s1g_to_11n_rx_packet_size(vif, skb, ies_mask);
	if (length_11n < 0) {
		morse_dbg(mors, "rx packet size < 0\n");
		morse_mac_skb_free(mors, skb);
		goto exit;
	}

	if (skb->len > 0)
		hdr = (const struct ieee80211_mgmt *) skb->data;

	if (hdr) {
		if (ieee80211_is_mgmt(hdr->frame_control)) {
			morse_vendor_rx_caps_ops_ie(mors, hdr, ies_mask);
			if (vif) {
				struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;

				if (mors_if->cac.enabled &&
						(vif->type == NL80211_IFTYPE_AP) &&
						ieee80211_is_auth(hdr->frame_control))
					morse_cac_count_auth(vif, hdr, length_11n);
			}
		}

		/* Deal with TWT messages. */
		if (ieee80211_is_assoc_resp(hdr->frame_control) ||
		    ieee80211_is_reassoc_resp(hdr->frame_control) ||
		    ieee80211_is_assoc_req(hdr->frame_control) ||
		    ieee80211_is_reassoc_req(hdr->frame_control)) {
			if (ies_mask->ies[WLAN_EID_S1G_TWT].ptr)
				morse_mac_process_twt_ie(mors,
							 mors_if,
							 &ies_mask->ies[WLAN_EID_S1G_TWT],
							 hdr->sa);
		}

		if (ieee80211_is_s1g_beacon(hdr->frame_control) && vif)
			morse_vendor_ie_process_rx_s1g_beacon(vif, skb);
	}

	if (skb->len + skb_tailroom(skb) < length_11n) {
		struct sk_buff *skb2;

		skb2 = skb_copy_expand(skb, skb_headroom(skb),
				       length_11n - skb->len,
				       GFP_KERNEL);
		morse_mac_skb_free(mors, skb);
		skb = skb2;
		/* Since we have freed the old skb, we must also clear the mask because now it will have
		 * references to invalid memory
		 */
	}

	morse_mac_rx_status(mors, skb, hdr_rx_status, &rx_status);
	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

	/*
	 * Check for Change Sequence number update in beacon and
	 * generate the probe request to get probe resp or wait for the full beacon.
	 * ECSA: Check for ECSA IE and save the channel info.
	 */
	if (mors_if && vif->type == NL80211_IFTYPE_STATION && hdr &&
		ieee80211_is_s1g_beacon(hdr->frame_control) &&
		mors_if->is_sta_assoc) {

		struct ieee80211_ext *s1g_beacon = (struct ieee80211_ext *)skb->data;
		u16 short_beacon;

		if (mors_if->s1g_bcn_change_seq == INVALID_BCN_CHANGE_SEQ_NUM) {
			/* Initialize the change seq number to track for STA */
			mors_if->s1g_bcn_change_seq = s1g_beacon->u.s1g_beacon.change_seq;
		} else if (mors_if->s1g_bcn_change_seq != s1g_beacon->u.s1g_beacon.change_seq) {
			/* Generate the probe Req */
			mors_if->s1g_bcn_change_seq = s1g_beacon->u.s1g_beacon.change_seq;

			/* Check if the feature is enabled to generate probe req on
			 * detection of update in beacon change seq number
			 */
			if (enable_bcn_change_seq_monitor) {
				/* Notify mac80211 to wakeup from power save to send probe req */
				morse_dbg(mors, "Beacon changed! Report Bcn loss,ps=%d, short_bcn=%d,seq_cnt=%d\n"
										, mors->config_ps
										, (s1g_beacon->frame_control & IEEE80211_FC_COMPRESS_SSID)
										, s1g_beacon->u.s1g_beacon.change_seq);
				ieee80211_beacon_loss(vif);

				if (!mors->config_ps) {
					/* Schedule the probe req, as we are already awake */
					tasklet_schedule(&mors->send_probe_req);
					morse_dbg(mors, "Scheduled to a send probe req\n");
				} else {
					/* Set a flag. with beacon_loss notification
					 * mac80211 will send a QoS NULL. On Tx complete
					 * of NULL data, probe req will be scheduled to be sent
					 */
					mors_if->waiting_for_probe_req_sched = true;
				}
			}
		}

		/* Check for ECSA IE and process it */
		short_beacon = (s1g_beacon->frame_control & IEEE80211_FC_COMPRESS_SSID);
		if (!short_beacon && ies_mask->ies[WLAN_EID_EXT_CHANSWITCH_ANN].ptr)
			morse_mac_process_ecsa_ie(mors, vif, skb);

		if (vif->csa_active && mors_if->ecsa_chan_configured) {
			/*
			 * We will configure channel again after receiving beacon in new channel
			 * to perform PHY calibration. This change is not required once the periodic
			 * PHY DC calibration is enabled in firmware. This first beacon in new channel
			 * is required in mac80211 to unblock traffic if it is blocked.
			 */
			morse_info(mors, "ECSA:%s Configure ECSA Chan ts=%ld,short_beacon=%d\n",
								__func__, jiffies, short_beacon);
			/* Schedule immediately */
			schedule_delayed_work(&mors_if->ecsa_chswitch_work, 0);
			mors_if->ecsa_chan_configured = false;
		}
	}
	morse_dot11ah_ies_mask_clear(ies_mask);
	morse_s1g_to_11n_rx_packet(mors, skb, length_11n, ies_mask);

	if (skb->len > 0)
		ieee80211_rx_irqsafe(hw, skb);
	else
		morse_mac_skb_free(mors, skb);

exit:
	morse_dot11ah_ies_mask_free(ies_mask);
	return 0;
}

static void morse_mac_config_ht_cap(struct ieee80211_hw *hw)
{
	struct morse *mors = (struct morse *)hw->priv;
	struct ieee80211_sta_ht_cap *morse_ht_cap = &(mors_band_5ghz.ht_cap);

	if (mors->custom_configs.enable_sgi_rc)
		morse_ht_cap->cap |= (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40);

	morse_ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
}

static void morse_mac_config_vht_80_cap(struct ieee80211_hw *hw)
{
	u16 mcs_map = 0, i;
	struct morse *mors = hw->priv;
	struct ieee80211_sta_vht_cap *morse_vht_cap = &(mors_band_5ghz.vht_cap);

	morse_vht_cap->vht_supported = true;
	morse_vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
			     IEEE80211_VHT_CAP_RXLDPC |
			     IEEE80211_VHT_CAP_TXSTBC |
			     IEEE80211_VHT_CAP_RXSTBC_1 |
			     IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

	/* Each 2 bits in mcs_cap corresponds to a spatial stream - we only support 1 */
	for (i = 0; i < 8; i++)	{
		if (i == 0) {
			mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
			continue;
		}
		mcs_map |= (IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}

	morse_vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	morse_vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);

	if (mors->custom_configs.enable_sgi_rc)
		morse_vht_cap->cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
}

static void morse_mac_config_vht_160_cap(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;

	struct ieee80211_sta_vht_cap *morse_vht_cap = &(mors_band_5ghz.vht_cap);

	morse_vht_cap->vht_supported = true;
	morse_vht_cap->cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;

	if (mors->custom_configs.enable_sgi_rc)
		morse_vht_cap->cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
}


static void morse_mac_config_wiphy(struct ieee80211_hw *hw)
{
	struct wiphy *wiphy = hw->wiphy;
	struct ieee80211_iface_combination *comb;
	struct ieee80211_iface_limit *if_limits;

	(void)wiphy;

	wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;

	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE;

	wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;

	wiphy->probe_resp_offload |=
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P;

	wiphy->features |= NL80211_FEATURE_TX_POWER_INSERTION;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	if (is_virtual_sta_test_mode()) {
		if_limits = kcalloc(1, sizeof(*if_limits), GFP_KERNEL);
		if_limits->max = virtual_sta_max;
		if_limits->types = BIT(NL80211_IFTYPE_STATION);

		comb = kcalloc(1, sizeof(*comb), GFP_KERNEL);
		comb->max_interfaces = virtual_sta_max;
		comb->n_limits = 1;
		comb->limits = if_limits;
		comb->num_different_channels = 1;

		wiphy->iface_combinations = comb;
		wiphy->n_iface_combinations = 1;
	} else if (is_multi_interface_mode()) {
		if_limits = kcalloc(1, sizeof(*if_limits), GFP_KERNEL);
		if_limits->max = MORSE_MAX_IF;
		if_limits->types = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);

		comb = kcalloc(1, sizeof(*comb), GFP_KERNEL);
		comb->max_interfaces = MORSE_MAX_IF;
		comb->n_limits = 1;
		comb->limits = if_limits;
		comb->num_different_channels = 1;

		wiphy->iface_combinations = comb;
		wiphy->n_iface_combinations = 1;
	}

#ifdef MORSE_MAC_CONFIG_WIPHY
	wiphy->available_antennas_rx = 0;
	wiphy->available_antennas_tx = 0;

	wiphy->features |= NL80211_FEATURE_STATIC_SMPS;
	wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS;

	wiphy->max_scan_ssids = WLAN_SCAN_PARAMS_MAX_SSID;
	wiphy->max_scan_ie_len = WLAN_SCAN_PARAMS_MAX_IE_LEN;

	wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wiphy->max_remain_on_channel_duration = 5000;
	wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
				   NL80211_FEATURE_AP_SCAN;

	wiphy->max_ap_assoc_sta = max_num_stations;
#endif
}

static void morse_mac_config_ieee80211_hw(struct morse *mors, struct ieee80211_hw *hw)
{
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);

	ieee80211_hw_set(hw, AMPDU_AGGREGATION);

#if KERNEL_VERSION(4, 10, 0) < MAC80211_VERSION_CODE
	if (MORSE_CAPAB_SUPPORTED(&mors->capabilities, HW_FRAGMENT))
		ieee80211_hw_set(hw, SUPPORTS_TX_FRAG);
#endif

	if (!enable_mac80211_connection_monitor)
		ieee80211_hw_set(hw, CONNECTION_MONITOR);

	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);

	if (enable_ps != POWERSAVE_MODE_DISABLED) {
		ieee80211_hw_set(hw, SUPPORTS_PS);
		/* Wait for a DTIM beacon - i.e in 802.11ah the long beacon, before associating */
		ieee80211_hw_set(hw, NEED_DTIM_BEFORE_ASSOC);
		if (enable_dynamic_ps_offload)
			ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
		else
			ieee80211_hw_set(hw, PS_NULLFUNC_STACK);
	}

#ifdef CONFIG_MORSE_RC
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
#endif

#ifdef MORSE_MAC_CONFIG_IEEE80211_HW
	ieee80211_hw_set(hw, AP_LINK_PS);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_PER_STA_GTK);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, QUEUE_CONTROL);

	ieee80211_hw_set(hw, SW_CRYPTO_CONTROL);
	ieee80211_hw_set(hw, TX_AMPDU_SETUP_IN_HW);
#endif
}

static void morse_reset_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse,
					      reset);

	morse_info(mors, "Resetting Bus...\n");
	morse_bus_reset(mors);
}

static void morse_ndr_work(struct work_struct *work)
{
	int ret = 0;
	struct morse *mors = container_of(work, struct morse,
					  soft_reset);

	ret = morse_firmware_exec_ndr(mors);
	if (ret)
		morse_err(mors, "%s: Failed to perform a soft reset (errno=%d)\n", __func__, ret);
	else
		morse_info(mors, "Soft Reset of FW COMPLETE\n");
}

static int morse_mac_restart(struct morse *mors)
{
	int ret;
	struct morse_vif *mors_if = NULL;
	struct ieee80211_vif *ap_vif = morse_get_ap_vif(mors);
	struct ieee80211_vif *sta_vif = morse_get_sta_vif(mors);
	u32 chip_id;

	if (enable_coredump) {
		ret = morse_coredump(mors);
		if (ret)
			morse_err(mors, "%s: Failed to perform a Core-Dump (errno=%d)\n", __func__, ret);
		else
			morse_info(mors, "Core-Dump generated");
	}

	morse_info(mors, "%s: Restarting HW\n", __func__);
	/* flag that we are no longer started
	 * to lingering/racey mac80211 call backs
	 * that they need to abort.
	 */
	mors->started = false;
	/* Stop rx */
	morse_bus_set_irq(mors, false);
	/* Stop Tx */
	ieee80211_stop_queues(mors->hw);

	/* Allow time for in-transit tx/rx packets to settle */
	mdelay(20);
	cancel_work_sync(&mors->chip_if_work);
	cancel_work_sync(&mors->tx_stale_work);

	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	morse_release_bus(mors);

	if (ret < 0) {
		morse_err(mors, "Morse FW chip access fail\n");
		goto exit;
	}

	/* Clear bus IRQ and reset */
	morse_hw_irq_clear(mors);

	mors->chip_if->event_flags = 0;

	if (ap_vif != NULL && mors_if != NULL) {
		if (ap_vif->type == NL80211_IFTYPE_AP ||
			ap_vif->type == NL80211_IFTYPE_ADHOC) {
			morse_beacon_finish(mors);
			morse_raw_finish(mors);
			morse_twt_finish(mors);
			morse_ndp_probe_req_resp_finish(mors);
		}
	}

	if (sta_vif != NULL && sta_vif->type == NL80211_IFTYPE_STATION)
		morse_send_probe_req_finish(mors);


	/* reload the firmware */
	ret = morse_firmware_exec_ndr(mors);
	if (ret < 0) {
		morse_err(mors, "Morse FW NDR fail\n");
		goto exit;
	}

	morse_bus_set_irq(mors, true);
	ieee80211_restart_hw(mors->hw);

	if ((ap_vif != NULL) &&
		(ap_vif->type == NL80211_IFTYPE_AP || ap_vif->type == NL80211_IFTYPE_ADHOC)) {
		morse_beacon_init(mors);
		morse_raw_init(mors, enable_raw);
		morse_twt_init(mors);
	}

exit:
	return ret;
}

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
static void morse_stale_tx_status_timer(unsigned long addr)
{
	struct morse *mors = (struct morse *) addr;
#else
static void morse_stale_tx_status_timer(struct timer_list *t)
{
	struct morse *mors = from_timer(mors, t, stale_status.timer);
#endif

	if (!mors || !mors->stale_status.enabled)
		return;

	spin_lock_bh(&mors->stale_status.lock);

	if (mors->cfg->ops->skbq_get_tx_status_pending_count(mors))
		queue_work(mors->net_wq, &mors->tx_stale_work);

	spin_unlock_bh(&mors->stale_status.lock);
}

static int morse_stale_tx_status_timer_init(struct morse *mors)
{
	MORSE_WARN_ON(mors->stale_status.enabled);

	spin_lock_init(&mors->stale_status.lock);

	mors->stale_status.enabled = 1;

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
	init_timer(&mors->stale_status.timer);
	mors->stale_status.timer.data = (unsigned long) &mors;
	mors->stale_status.timer.function = morse_stale_tx_status_timer;
	add_timer(&mors->stale_status.timer);
#else
	timer_setup(&mors->stale_status.timer,
		morse_stale_tx_status_timer, 0);
#endif

	return 0;
}

static int morse_stale_tx_status_timer_finish(struct morse *mors)
{
	if (!mors->stale_status.enabled)
		return 0;

	mors->stale_status.enabled = 0;

	spin_lock_bh(&mors->stale_status.lock);

	del_timer_sync(&mors->stale_status.timer);

	spin_unlock_bh(&mors->stale_status.lock);

	return 0;
}


/* Schedule the restart work from wherever a code restart
 * is deemed necessary. This can be triggered directly from the debugfs
 * or will be scheduled indirectly from a watchdog timeout.
 */
static void morse_mac_restart_work(struct work_struct *work)
{
	int ret;
	struct morse *mors = container_of(work, struct morse,
					  driver_restart);

	mors->restart_counter++;

	mutex_lock(&mors->lock);
	ret = morse_mac_restart(mors);

	if (ret >= 0)
		morse_info(mors, "Morse FW restart %d success", mors->restart_counter);
	else
		morse_err(mors, "Morse FW restart %d failed. Resetting..", mors->restart_counter);

	if (ret < 0) {
		/* FW restart failed, will need a reset */
		if (enable_watchdog_reset)
			/* Driver will request to reset the bus.
			 * This should remove/re-install the driver
			 */
			schedule_work(&mors->reset);
		else {
			/* Offload removing driver to user space */
			mors->reset_required = 1;
			morse_watchdog_cleanup(mors);
		}

		/* flag that we are no longer started
		 * to lingering/racey mac80211 call backs
		 * that they need to abort.
		 */
		mors->started = false;

		/* Stopping sched scan */
		ieee80211_sched_scan_stopped(mors->hw);
	}

	mutex_unlock(&mors->lock);
}

static int morse_mac_driver_restart(struct morse *mors)
{
	schedule_work(&mors->driver_restart);
	morse_info(mors, "Scheduled a driver reset ...\n");

	return 0;
}

static void morse_health_check_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse,
					  health_check);
	int ret;

	ret = morse_cmd_health_check(mors);
	if (ret) {
		morse_err(mors, "%s: Failed health check (errno=%d)\n", __func__, ret);
		/* Schedule a driver reset */
		schedule_work(&mors->driver_restart);
	} else
		morse_dbg(mors, "Health check complete\n");
}


static int morse_mac_ping_health_check(struct morse *mors)
{
	schedule_work(&mors->health_check);
	morse_dbg(mors, "Scheduled a health check\n");

	return 0;
}

int morse_mac_watchdog_create(struct morse *mors)
{
	return morse_watchdog_init(mors, watchdog_interval_secs,
								morse_mac_ping_health_check,
								morse_mac_driver_restart);
}

static int morse_mac_init(struct morse *mors)
{
	int ret;

	struct ieee80211_hw *hw = mors->hw;

	if (is_thin_lmac_mode()) {
		morse_info(mors, "%s: Enabling thin LMAC mode\n", __func__);
		if (is_virtual_sta_test_mode()) {
			morse_err(mors, "%s: Virtual STA test mode is set but ignored\n",
				__func__);
			virtual_sta_max = 0;
		}
	} else if (is_virtual_sta_test_mode()) {
		morse_info(mors, "%s: Enabling virtual STA test mode - max %d STAs\n",
			__func__, virtual_sta_max);
		if (enable_ps != POWERSAVE_MODE_DISABLED) {
			morse_err(mors, "%s: Disabling power save in virtual STA test mode\n",
				__func__);
			enable_ps = POWERSAVE_MODE_DISABLED;
		}
	} else if (is_multi_interface_mode()) {
		morse_info(mors, "%s: Enabling Multi Interface mode\n",
			__func__);
	}

	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = &mors_band_5ghz;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->extra_tx_headroom = sizeof(struct morse_buff_skb_header);
	hw->queues = 4;
	/* Limit the number of aggregations for SPI. May get overwhelmed by SDIO */
	if (max_aggregation_count)
		hw->max_rx_aggregation_subframes = max_aggregation_count;
	hw->max_rates = max_rates;        /* We support 4 rates */
	hw->max_report_rates = max_rates; /* We support 4 rates */
	hw->max_rate_tries = max_rate_tries;   /* Try each rate up to 7 times */
	hw->vif_data_size = sizeof(struct morse_vif);
	hw->sta_data_size = sizeof(struct morse_sta);
    /*
     * Avoid adding kernel version check for hw->tx_sk_pacing_shift
     * for kernel < linux-4.20.0. tx_sk_pacing_shift with tcp smaller
     * queues is required to achieve sufficient throughput in TCP. For
     * kernels < 4.20.0 apply TCP small queue patches to kernel and add
     * "tx_sk_pacing_shift" variable to "struct ieee80211_hw".
     * Ref: https://lwn.net/Articles/507065/
     *      https://lwn.net/Articles/757643/
     */
	hw->tx_sk_pacing_shift = 3;

	mors->enable_subbands = enable_subbands;

	if (enable_sgi_rc) {
		if (MORSE_CAPAB_SUPPORTED(&mors->capabilities, SGI))
			mors->custom_configs.enable_sgi_rc = true;
		else {
			enable_sgi_rc = false;
			mors->custom_configs.enable_sgi_rc = false;
			morse_err(mors,
			"%s: SGI has been configured but is not supported by this device. Ignoring.\n",
			__func__);
		}
	} else {
		mors->custom_configs.enable_sgi_rc = false;
	}

	if (enable_trav_pilot) {
		if (MORSE_CAPAB_SUPPORTED(&mors->capabilities, TRAVELING_PILOT_ONE_STREAM) ||
		    MORSE_CAPAB_SUPPORTED(&mors->capabilities, TRAVELING_PILOT_TWO_STREAM))
			mors->custom_configs.enable_trav_pilot = true;
		else {
			enable_trav_pilot = false;
			mors->custom_configs.enable_trav_pilot = false;
			morse_err(mors,
			"%s: Travelling pilots has been configured but is not supported by this device. Ignoring.\n",
			__func__);
		}
	} else {
		mors->custom_configs.enable_trav_pilot = false;
	}

#ifdef CONFIG_MORSE_RC
	/* Initial value for RTS threshold */
	mors->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
#endif

	/* Initial channel information when chip first boots */
	mors->custom_configs.default_bw_info.pri_bw_mhz = 2;
	mors->custom_configs.default_bw_info.pri_1mhz_chan_idx = 0;
	mors->custom_configs.default_bw_info.op_bw_mhz = 2;
	/* Frequency is special - we don't necessarily know what freq will be */
	/* Intial values for sta_type and enc_mode */
	mors->custom_configs.sta_type = STA_TYPE_NON_SENSOR;
	mors->custom_configs.enc_mode = ENC_MODE_BLOCK;

	/* Get supported MCS rates (TX/RX) from modparam */
	mors_band_5ghz.ht_cap.mcs.rx_mask[0] = mcs_mask;

	SET_IEEE80211_PERM_ADDR(hw, mors->macaddr);

	morse_mac_config_ieee80211_hw(mors, hw);
	morse_mac_config_wiphy(hw);
	morse_mac_config_ht_cap(hw);

	/* 4 and 8MHz parts use VHT 80 and 160 respectively */
	if (MORSE_CAPAB_SUPPORTED(&mors->capabilities, 4MHZ))
		morse_mac_config_vht_80_cap(hw);

	if (MORSE_CAPAB_SUPPORTED(&mors->capabilities, 8MHZ))
		morse_mac_config_vht_160_cap(hw);

	morse_beacon_init(mors);
	morse_ndp_probe_req_resp_init(mors);
	morse_stale_tx_status_timer_init(mors);

	ret = morse_ps_init(mors, (enable_ps != POWERSAVE_MODE_DISABLED),
		enable_dynamic_ps_offload);
	if (enable_ps != POWERSAVE_MODE_FULLY_ENABLED) {
		/* SW-2638:
		 * We do not have GPIO pins connected, let's disable the host-to-chip PS mechanism, that
		 * is by incrementing the number of wakers by one
		 */
		morse_ps_disable(mors);
	}

	MORSE_WARN_ON(ret);

#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
	if (enable_airtime_fairness)
		tasklet_setup(&mors->tasklet_txq, morse_txq_tasklet);
#endif

	ret = morse_raw_init(mors, enable_raw);
	MORSE_WARN_ON(ret);

	ret = morse_twt_init(mors);
	MORSE_WARN_ON(ret);

	/* Mark max_power_level as unread */
	mors->max_power_level = INT_MAX;

#ifdef CONFIG_MORSE_HW_TRACE
	morse_hw_trace_init();
#endif

#ifdef CONFIG_MORSE_VENDOR_COMMAND
	/* register vendor commands and events */
	morse_set_vendor_commands_and_events(hw->wiphy);
#endif

	return 0;
}

int morse_mac_register(struct morse *mors)
{
	int ret;
	struct ieee80211_hw *hw = mors->hw;
	const struct morse_regdomain *morse_regdom;
	struct ieee80211_regdomain *regdom;

	/* Pass debug_mask modparam to dot11ah module */
	morse_dot11ah_debug_init(debug_mask);

	ret = morse_mac_init(mors);
	if (ret) {
		morse_err(mors, "morse_mac_init failed %d\n", ret);
		goto err_init;
	}

	/* Set regulatory rules to support channels for the country=alpha */
	morse_regdom = morse_reg_set_alpha(country);
	if (!morse_regdom) {
		ret = -EINVAL;
		morse_err(mors, "Could not assign country code %s", country);
		goto err_init;
	}
	if (strncmp(morse_regdom->alpha2, country, strlen(morse_regdom->alpha2))) {
		morse_warn(mors, "Country code %s not recognised; using %s instead\n",
			country, morse_regdom->alpha2);
		/* Copy the country code we are actually using back into country so that
		 * /sys/modules/morse/parameters/country reflects the value we are using
		 */
		strncpy(country, morse_regdom->alpha2, ARRAY_SIZE(country) - 1);
	}
	/* for now, the driver is region-aware */
	morse_info(mors, "Setting Driver internal regulatory domain to %s", morse_regdom->alpha2);

	/* Set regulatory flag to avoid country ie processing in mac80211 */
	hw->wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;

	/* We need to override the alpha-2 code used internally to the
	 * user-assigned alpha2 - ZZ - for compatability with
	 * existing regdb rules in cfg80211
	 */
	hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
	regdom = morse_regdom_to_ieee80211(morse_regdom);
	strcpy(regdom->alpha2, USER_ASSIGNED_ALPHA);
	wiphy_apply_custom_regulatory(hw->wiphy, regdom);
	kfree(regdom);
	/* give the regulatory workqueue a chance to run */
	schedule_timeout_interruptible(1);

	/* Register with mac80211 */
	ret = ieee80211_register_hw(hw);
	if (ret) {
		morse_err(mors, "ieee80211_register_hw failed %d\n", ret);
		goto err_init;
	}

	INIT_WORK(&mors->reset, morse_reset_work);
	INIT_WORK(&mors->soft_reset, morse_ndr_work);
	INIT_WORK(&mors->driver_restart, morse_mac_restart_work);
	INIT_WORK(&mors->health_check, morse_health_check_work);

	morse_send_probe_req_init(mors);

	ret = morse_init_debug(mors);
	if (ret)
		morse_err(mors, "Unable to create debugfs files\n");

	ret = morse_mac_watchdog_create(mors);
	if (ret) {
		morse_err(mors, "Failed to create watchdog %d\n", ret);
		goto err_mon_init;
	}

	if (enable_watchdog) {
		ret = morse_watchdog_start(mors);
		if (ret) {
			morse_err(mors, "morse_watchdog_start failed %d\n", ret);
			goto err_mon_init;
		}
	}

#ifdef CONFIG_MORSE_MONITOR
	ret = morse_mon_init(mors);
	if (ret) {
		morse_err(mors, "morse_mon_init failed %d\n", ret);
		goto err_mon_init;
	}
#endif

#ifdef CONFIG_MORSE_RC
	ret = morse_rc_init(mors);
	if (ret) {
		morse_err(mors, "morse_rc_init failed %d\n", ret);
		goto err_rc_init;
	}
#endif

	return ret;

#ifdef CONFIG_MORSE_RC
err_rc_init:
#ifdef CONFIG_MORSE_MONITOR
	morse_mon_free(mors);
#endif
#endif

err_mon_init:
	ieee80211_unregister_hw(hw);
err_init:
	return ret;
}

struct morse *morse_mac_create(size_t priv_size, struct device *dev)
{
	struct ieee80211_hw *hw;
	struct morse *mors;

#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
	if (enable_airtime_fairness)
		mors_ops.wake_tx_queue = morse_mac_ops_wake_tx_queue;
#endif

	/* User disabled HW-crypto - fallback to software crypto */
	/* Encryption and decryption must be done on the host in Thin LMAC mode */
	if (no_hwcrypt || is_thin_lmac_mode())
		mors_ops.set_key = NULL;

	hw = ieee80211_alloc_hw(sizeof(*mors) + priv_size, &mors_ops);
	if (!hw) {
		dev_err(dev, "ieee80211_alloc_hw failed\r\n");
		return NULL;
	}

	SET_IEEE80211_DEV(hw, dev);
	memset(hw->priv, 0, sizeof(*mors));

	mors = hw->priv;
	mors->hw = hw;
	mors->dev = dev;
	mutex_init(&mors->lock);
	mutex_init(&mors->cmd_lock);
	mutex_init(&mors->cmd_wait);

#if KERNEL_VERSION(4, 10, 0) > MAC80211_VERSION_CODE
	/* Older kernels decide whether to do fragmentation based on the existence
	 * of this callback
	 */
	if (!MORSE_CAPAB_SUPPORTED(&mors->capabilities, HW_FRAGMENT))
		mors_ops.set_frag_threshold = NULL;
#endif

	mors->custom_configs.enable_ampdu = true;
	mors->custom_configs.enable_subbands = enable_subbands;
	mors->custom_configs.enable_arp_offload = enable_arp_offload;
	mors->custom_configs.enable_dhcpc_offload = enable_dhcpc_offload;
	mors->custom_configs.dhcpc_lease_update_script = dhcpc_lease_update_script;

	country[sizeof(country) - 1] = '\0';
	memcpy(mors->country, country, sizeof(mors->country));

#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
	mors->custom_configs.enable_airtime_fairness = enable_airtime_fairness;
#else
	mors->custom_configs.enable_airtime_fairness = false;
#endif
	/* TODO: Placeholder for legacy amsdu support */
	mors->custom_configs.enable_legacy_amsdu = (enable_airtime_fairness &&
		false);

	mors->watchdog.paused = 0;
	mors->watchdog.consumers = 0;
	mors->watchdog.ping = NULL;
	mors->watchdog.reset = NULL;

	return mors;
}

static void morse_mac_deinit(struct ieee80211_hw *hw)
{
	if (is_virtual_sta_test_mode() || is_multi_interface_mode()) {
		struct wiphy *wiphy = hw->wiphy;

		if (wiphy->iface_combinations) {
			kfree(wiphy->iface_combinations->limits);
			kfree(hw->wiphy->iface_combinations);
			hw->wiphy->iface_combinations = NULL;
			hw->wiphy->n_iface_combinations = 0;
		}
	}
}

void morse_mac_unregister(struct morse *mors)
{
	morse_deinit_debug(mors);
	morse_ps_disable(mors);

#ifdef CONFIG_MORSE_RC
	morse_rc_deinit(mors);
#endif
	ieee80211_stop_queues(mors->hw);
	mors->cfg->ops->flush_tx_data(mors);
	ieee80211_unregister_hw(mors->hw);
#if KERNEL_VERSION(5, 9, 0) <= MAC80211_VERSION_CODE
	if (enable_airtime_fairness)
		tasklet_kill(&mors->tasklet_txq);
#endif
	morse_mac_deinit(mors->hw);

	morse_raw_finish(mors);
	morse_beacon_finish(mors);
	morse_ndp_probe_req_resp_finish(mors);
	morse_send_probe_req_finish(mors);
	morse_stale_tx_status_timer_finish(mors);
#ifdef CONFIG_MORSE_MONITOR
	morse_mon_free(mors);
#endif
	morse_ps_finish(mors);

#ifdef CONFIG_MORSE_HW_TRACE
	morse_hw_trace_deinit();
#endif
}

void morse_mac_destroy(struct morse *mors)
{
	if (enable_watchdog)
		morse_watchdog_cleanup(mors);

	ieee80211_free_hw(mors->hw);
}

int morse_mac_get_watchdog_interval_secs(void)
{
	return watchdog_interval_secs;
}
