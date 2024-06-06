#ifndef _MORSE_MORSE_H_
#define _MORSE_MORSE_H_

/*
 * Copyright 2017-2023 Morse Micro
 *
 */
#include <net/mac80211.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/types.h>
#include <linux/version.h>
#if KERNEL_VERSION(4, 9, 81) < LINUX_VERSION_CODE
#include <linux/nospec.h>
#endif
#include "hw.h"
#include "skbq.h"
#include "skb_header.h"
#include "firmware.h"
#include "dot11ah/dot11ah.h"
#include "dot11ah/tim.h"
#include "s1g_ies.h"
#include "watchdog.h"
#include "raw.h"
#include "chip_if.h"
#include "operations.h"
#include "utils.h"
#ifdef CONFIG_MORSE_RC
#include "rc.h"
#endif
#include "cac.h"
#include "pv1.h"

#ifdef CONFIG_MORSE_USER_ACCESS
#include "uaccess.h"
#endif
#include "page_slicing.h"

#ifdef MAC80211_BACKPORT_VERSION_CODE
#define MAC80211_VERSION_CODE MAC80211_BACKPORT_VERSION_CODE
#else
#define MAC80211_VERSION_CODE LINUX_VERSION_CODE
#endif

#define MORSE_DRIVER_SEMVER_MAJOR 52
#define MORSE_DRIVER_SEMVER_MINOR 0
#define MORSE_DRIVER_SEMVER_PATCH 0

#define MORSE_SEMVER_GET_MAJOR(x) (((x) >> 22) & 0x3FF)
#define MORSE_SEMVER_GET_MINOR(x) (((x) >> 10) & 0xFFF)
#define MORSE_SEMVER_GET_PATCH(x) ((x) & 0x3FF)

#define DRV_VERSION __stringify(MORSE_VERSION)

#define TOTAL_HALOW_CHANNELS 52

#define STA_PRIV_BACKUP_NUM (10)

#define SERIAL_SIZE_MAX  32
#define BCF_SIZE_MAX     48

/** Size in bytes of an OUI */
#define OUI_SIZE				(3)

/** Max number of OUIs supported in vendor IE OUI filter. Must match the define in the firmware */
#define MAX_NUM_OUI_FILTERS			(5)

/**
 * AID limit, currently limited to non-s1g for compatibility.
 *
 * TODO: Increase limit to 8192 (S1G) and support pages. See 802.11-2020 Section
 *	 9.4.1.8 AID Field.
 */
#define AID_LIMIT	(2007)

#define INVALID_BCN_CHANGE_SEQ_NUM 0xFFFF

/**
 * From firmware: Time to trigger chswitch_timer in AP mode after sending
 * last beacon data to firmware in the current channel.
 */
#define BEACON_REQUEST_GRACE_PERIOD_MS (5)

/* Generate a device ID from chip ID, revision and chip type */
#define MORSE_DEVICE_ID(chip_id, chip_rev, chip_type) \
	((chip_id) | ((chip_rev) << 8) | ((chip_type) << 12))

#define HZ_TO_KHZ(x) ((x) / 1000)
#define KHZ_TO_HZ(x) ((x) * 1000)
#define MHZ_TO_HZ(x) ((x) * 1000000)
#define HZ_TO_MHZ(x) ((x) / 1000000)

#define QDBM_TO_MBM(gain) (((gain) * 100) >> 2)
#define MBM_TO_QDBM(gain) (((gain) << 2) / 100)

#define BPS_TO_KBPS(x) ((x) / 1000)

/**
 * enum morse_config_test_mode - test mode
 * @MORSE_CONFIG_TEST_MODE_DISABLED: normal operation
 * @MORSE_CONFIG_TEST_MODE_DOWNLOAD_ONLY: download only (no verification)
 * @MORSE_CONFIG_TEST_MODE_DOWNLOAD_AND_GET_HOST_TBL_PTR: download and get host ptr only
 * @MORSE_CONFIG_TEST_MODE_GET_HOST_TBL_PTR_ONLY: get host ptr only (no download or verification)
 * @MORSE_CONFIG_TEST_MODE_RESET: reset only (no download or verification)
 * @MORSE_CONFIG_TEST_MODE_BUS: write/read block via the bus
 */
enum morse_config_test_mode {
	MORSE_CONFIG_TEST_MODE_DISABLED,
	MORSE_CONFIG_TEST_MODE_DOWNLOAD_ONLY,
	MORSE_CONFIG_TEST_MODE_DOWNLOAD_AND_GET_HOST_TBL_PTR,
	MORSE_CONFIG_TEST_MODE_GET_HOST_TBL_PTR_ONLY,
	MORSE_CONFIG_TEST_MODE_RESET,
	MORSE_CONFIG_TEST_MODE_BUS,

	/* Add more test modes before this line */
	MORSE_CONFIG_TEST_MODE_INVALID,
};

struct morse_buff;
struct morse_bus_ops;

/**
 * modparam variables
 */
extern uint test_mode;
extern char serial[];
extern char board_config_file[];
extern u8 macaddr_octet;
extern u8 enable_otp_check;
extern u8 macaddr[ETH_ALEN];
extern bool enable_ibss_probe_filtering;
extern uint ocs_type;

/**
 * enum morse_mac_subbands_mode - flags to describe sub-bands handling
 *
 * @SUBBANDS_MODE_DISABLED : sub-bands disabled all packets are sent in operating bandwidth
 * @SUBBANDS_MODE_MANAGEMENT : sub-bands are set only for know management packets
 *	(e.g. beacons, probe requests/responses, etc.)
 * @SUBBANDS_MODE_ENABLED : [default] sub-bands fully enabled, data packets will follow sub-band
 *	signaling from the RC algorithm (ministrel)
 */
enum morse_mac_subbands_mode {
	SUBBANDS_MODE_DISABLED = 0x00,
	SUBBANDS_MODE_MANAGEMENT = 0x01,
	SUBBANDS_MODE_ENABLED = 0x02,
	SUBBANDS_MODE_UNKNOWN = 0xFF
};

/**
 * struct morse_raw - contains RAW state and configuration information
 *
 * @enabled		Whether RAW is currently enabled
 * @rps_ie		The currently generated RPS IE
 * @rps_ie_len		The size of the currently generated RPS IE
 * @sta_data		The latest station data, including a list of AIDs
 * @configs		RAW configurations (see %morse_raw_config)
 * @refresh_aids_work	Workqueue structure for refreshing AIDs
 * @lock		Mutex to synchronise access to this structure
 */
struct morse_raw {
	bool enabled;
	u8 *rps_ie;
	u8 rps_ie_len;
	struct morse_raw_station_data sta_data;
	struct morse_raw_config *configs[MAX_NUM_RAWS];
	struct work_struct refresh_aids_work;
	struct mutex lock;
};

/**
 * struct morse_twt - contains TWT state and configuration information
 *
 * @stas		List of structures containing a agreements for a STA.
 * @wake_intervals	List of structures used as heads for lists of agreements with the same
 *			wake interval. These are arranged in order from the smallest to largest wake
 *			intervals.
 * @events		A queue of TWT events to be processed.
 * @tx			A queue of TWT data to be sent.
 * @to_install		A queue of TWT agreements to install to the chip.
 * @req_event_tx	A TWT request event to be sent in the (re)assoc request frame.
 * @work		Work queue struct to defer processing of events.
 * @lock		Spinlock used to control access to lists/memory.
 * @requester		Whether or not the VIF is a TWT Requester.
 * @responder		Whether or not the VIF is a TWT Responder.
 */
struct morse_twt {
	struct list_head stas;
	struct list_head wake_intervals;
	struct list_head events;
	struct list_head tx;
	struct list_head to_install;
	u8 *req_event_tx;
	struct work_struct work;
	spinlock_t lock;
	bool requester;
	bool responder;
};

struct morse_mbssid_info {
	u8 max_bssid_indicator;
	u8 transmitter_vif_id;
};

struct morse_custom_configs {
	u8 sta_type;
	u8 enc_mode;
	bool enable_ampdu;
	bool enable_trav_pilot;
	bool enable_airtime_fairness;
	bool enable_sgi_rc;
	struct morse_raw raw;
	bool listen_interval_ovr;
	u16 listen_interval;
	enum morse_mac_subbands_mode enable_subbands;
	struct morse_channel_info channel_info;
	struct morse_channel_info default_bw_info;
	bool enable_arp_offload;
	bool enable_legacy_amsdu;
	bool enable_dhcpc_offload;
	bool enable_sta_cac;
	char *dhcpc_lease_update_script;
	u32 duty_cycle;
};

struct morse_ps {
	/* Number of clients requesting to talk to chip */
	u32 wakers;
	bool enable;
	bool suspended;
	bool dynamic_ps_en;
	unsigned long bus_ps_timeout;
	struct mutex lock;
	struct work_struct async_wake_work;
	struct delayed_work delayed_eval_work;
};

/* Morse ACI map for page metadata */
enum morse_page_aci {
	MORSE_ACI_BE = 0,
	MORSE_ACI_BK = 1,
	MORSE_ACI_VI = 2,
	MORSE_ACI_VO = 3,
};

/* Taken from 802.11me Table 10-1.
 * Encodes the user priority (UP) section of a TID
 */
enum qos_tid_up_index {
	MORSE_QOS_TID_UP_BK = 1,
	MORSE_QOS_TID_UP_xx = 2,	/* Not specified */
	MORSE_QOS_TID_UP_BE = 0,
	MORSE_QOS_TID_UP_EE = 3,
	MORSE_QOS_TID_UP_CL = 4,
	MORSE_QOS_TID_UP_VI = 5,
	MORSE_QOS_TID_UP_VO = 6,
	MORSE_QOS_TID_UP_NC = 7,

	MORSE_QOS_TID_UP_LOWEST = MORSE_QOS_TID_UP_BK,
	MORSE_QOS_TID_UP_HIGHEST = MORSE_QOS_TID_UP_NC
};

struct morse_sw_version {
	u8 major;
	u8 minor;
	u8 patch;
};

/**
 * struct morse_vendor_info - filled from mm vendor IE
 */
struct morse_vendor_info {
	/** Indicates if vendor info is valid (and has been filled) */
	bool valid;
	/** Vendor capability mmss offset signalled by Morse devices */
	u8 morse_mmss_offset;
	/** Identifies underlying hardware of device */
	u32 chip_id;
	/** Identifies underlying software ver of device */
	struct morse_sw_version sw_ver;
	/** Operational features in use on device */
	struct morse_ops operations;
	/** Device supports short 'additional ack timeout delay' */
	bool supports_short_ack_timeout;
	/** Device supports PV1 Data Frames only */
	bool pv1_data_frame_only_support;
	/**
	 * Device support exclusive page slicing (all stations in the BSS should support
	 * page slicing)
	 */
	bool page_slicing_exclusive_support;
};

/** Morse Private STA record */
struct morse_sta {
	/** pointer to next morse_sta's and used only in AP mode */
	struct list_head list;
	/** Whether we saw an assoc request when already associated */
	bool already_assoc_req;
	/** When to timeout this record (used in backup) */
	unsigned long timeout;
	/** The address of this sta */
	u8 addr[ETH_ALEN];
	/** Current state of the station (this is not part of the ieee80211_sta struct) */
	enum ieee80211_sta_state state;
	/** Whether A-MPDU is supported on this STA */
	bool ampdu_supported;
	/** STA's required Minimum MPDU start spacing as reported by s1g capabs */
	u8 ampdu_mmss;
	/** Whether we have a TX A-MPDU on this TID */
	bool tid_tx[IEEE80211_NUM_TIDS];
	/** Whether we have tried to start a TX A-MPDU on this TID */
	bool tid_start_tx[IEEE80211_NUM_TIDS];
	/** Whether travelling pilots is supported */
	enum trav_pilot_support trav_pilot_support;
	/** Per-TID parameters */
	u8 tid_params[IEEE80211_NUM_TIDS];
	/** STA's max tx bw as reported in s1g capabilities */
	int max_bw_mhz;
	/** Vendor information - filled from mm vendor IE */
	struct morse_vendor_info vendor_info;
#ifdef CONFIG_MORSE_RC
	/** Morse Micro RC info*/
	struct morse_rc_sta rc;
	struct mmrc_rate last_sta_tx_rate;
	struct mmrc_rate last_sta_rx_rate;
#endif
	/** RX status of last rx skb */
	struct morse_skb_rx_status last_rx_status;
	/** average rssi of rx packets */
	s16 avg_rssi;
	/** When set, frames destined for this STA must be returned to mac80211
	 *  for rescheduling. Cleared after frame destined for STA has
	 *  IEEE80211_TX_CTL_CLEAR_PS_FILT set.
	 */
	bool tx_ps_filter_en;

	/** Counts the number of packets passed from the kernel to the driver */
	u64 tx_pkt_count;

	/** number of peerings established and valid only if it is mesh peer */
	u8 mesh_no_of_peerings;

	/** Set when PV1 capability is advertised in S1G capabilities of peer STA */
	bool pv1_frame_support;

	/** Save stored status of peer STA from Header Compression Response at TX */
	struct morse_sta_pv1 tx_pv1_ctx;

	/** Save stored status of peer STA from Header Compression Request on RX*/
	struct morse_sta_pv1 rx_pv1_ctx;
};

/** AP specific information */
struct morse_ap {
	/** Number of stas currently associated */
	u16 num_stas;
	/** Largest AID currently in use */
	u16 largest_aid;
	/** list of morse_sta's associated */
	struct list_head stas;

	/** Bitmap of AIDs currently in use. Bit position corresponds to the AID */
	 DECLARE_BITMAP(aid_bitmap, AID_LIMIT);
};

/** Mesh specific information */
struct morse_mesh {
	/** back pointer */
	struct morse_vif *mors_if;
	/** Mesh active */
	bool is_mesh_active;
	/** mesh beaconless mode */
	bool mesh_beaconless_mode;
	/** mesh id */
	u8 mesh_id[IEEE80211_MAX_SSID_LEN];
	/** mesh id length */
	u8 mesh_id_len;
	/** maximum number of peer links */
	u8 max_plinks;
	/** mesh periodic probe timer */
	struct timer_list mesh_probe_timer;
	/** rx status of probe req */
	struct ieee80211_rx_status probe_rx_status;
	/** dynamic peering mode */
	bool dynamic_peering;
	/** RSSI margin to consider while selecting a peer to kick out */
	u8 rssi_margin;
	/** Duration in seconds, a blacklisted peer is not allowed peering */
	u32 blacklist_timeout;
	/** address of the peer kicked out */
	u8 kickout_peer_addr[ETH_ALEN];
	/** Timestamp when peer is kicked out */
	u32 kickout_ts;

	/* Mesh Beacon Collision Avoidance state */
	struct {
		/**
		 * Configuration to enable or disable MBCA TBTT selection and adjustment.
		 */
		u8 config;

		/**
		 * Interval at which beacon timing elements are included in beacons.
		 */
		u8 beacon_timing_report_interval;

		/**
		 * To keep track number of beacons sent for beacon timing report interval.
		 */
		u8 beacon_count;

		/**
		 * Initial scan to find peers in the MBSS
		 */
		u16 mbss_start_scan_duration_ms;

		/**
		 * Minimum gap between our beacons and neighbor beacons for TBTT Selection.
		 */
		u8 min_beacon_gap_ms;

		/**
		 * TBTT adjustment timer interval in target LMAC firmware.
		 */
		u16 tbtt_adj_interval_ms;
	} mbca;
};


struct morse_vif {
	u16 id;			/* interface ID from chip */
	u16 dtim_count;

	/**
	 * Used to keep track of time for beacons
	 * and probe responses. This is an approximation!
	 * Proper solution is to use timing from PHY.
	 *
	 * This field is in Linux jiffies.
	 **/
	u64 epoch;

	/**
	 * Pointer to current custom configuration
	 * for the chip.
	 */
	struct morse_custom_configs *custom_configs;

	/**
	 * Signals that when we send control response frames we will
	 * send on 1MHz. Our S1G capabilities field must be updated accordingly.
	 */
	bool ctrl_resp_out_1mhz_en;

	/**
	 * Signals that for frames we send we can expect the control
	 * response frames (primarily ACKs) to be on 1MHz. This is sent
	 * to the firmware so that it can adjust timeouts as necessary.
	 *
	 * To be strictly standards compliant with ack times and
	 * avoid uneccesary performance degradation, this should be
	 * tracked per MAC address + vif as individual STAs may or may
	 * not opt into it.
	 */
	bool ctrl_resp_in_1mhz_en;

	/**
	 * CAC (Centralized Authentication Control)
	 */
	struct morse_cac cac;

	/**
	 * Configured BSS color.
	 * Only valid after association response is received for STAs
	 */
	u8 bss_color;

	/**
	 * AP's required Minimum MPDU start spacing as communicated by S1G
	 * capabilities field. Only valid after association response
	 * is received for STAs.
	 */
	u8 bss_ampdu_mmss;

	/**
	 * TWT state information.
	 */
	struct morse_twt twt;

	/**
	 * AP mode specific information
	 * NULL if not an AP
	 */
	struct morse_ap *ap;

	/**
	 * Mesh mode specific information. NULL if vif is not a Mesh
	 */
	struct morse_mesh *mesh;

	struct {
		/** List of run-time configurable vendor IEs to insert into management frames */
		struct list_head ie_list;

		/** List of vendor IE OUIs for which to generate a netlink event if seen in a mgmt
		 * frame.
		 */
		struct list_head oui_filter_list;

		/** Number of elements in the OUI filter list */
		u8 n_oui_filters;

		/** Spinlock to protect access to these fields */
		spinlock_t lock;
	} vendor_ie;

	/** SW-3908 unveiled a race condition, so sometimes we have to store a backup
	 * of our private data when a device reassociates so that S1G information
	 * is persisted
	 */
	struct morse_sta sta_backups[STA_PRIV_BACKUP_NUM];

	struct morse_caps capabilities;
	struct morse_ops operations;

	/**
	 *  Custom features obtained from the associated AP, filled via
	 *  vendor IE. Only valid after association response is received for STAs.
	 */
	struct morse_vendor_info bss_vendor_info;

	struct ieee80211_s1g_cap s1g_cap_ie;

	/**
	 * Beacon Tasklet
	 */
	struct tasklet_struct beacon_tasklet;

	/**
	 *  Keeping track of beacon change sequence number for both AP and STA
	 */
	u16 s1g_bcn_change_seq;

	/**
	 * To keep track of channel switch in progress and to restrict
	 * the updating of s1g_bcn_change_seq to only once
	 */
	bool chan_switch_in_progress;

	/**
	 * CRC of EDCA parameter set either from EDCA parameter IE or WMM IE
	 */
	u32 edca_param_crc;

	/**
	 * CRC of S1G operation Parameter IE
	 */
	u32 s1g_oper_param_crc;

	/**
	 * Template Buffer of probe request unicast/directed packet for sending
	 * to connected AP only. This will be populated in bss_info_change event
	 * handler and used in other places, ex: to trigger probe on detecting
	 * update on change sequence number
	 */
	struct sk_buff *probe_req_buf;

	/**
	 * Buffer that saves beacon from mac80211 for this BSS
	 */
	struct sk_buff *beacon_buf;

	/**
	 * Pointer to SSID IE buffer got from beacon.
	 */
	const u8 *ssid_ie;

	/**
	 * Flag to check if STA is in asscociated state. This gets populated in
	 * bss_info_changed event handler. Any other place to access vif->bss_conf
	 * may not be safe. This flag saves the snapshot of connection status.
	 * valid only for STA mode.
	 */
	bool is_sta_assoc;

	/**
	 * Flag to check if ibss node joined/created a network. This flag gets
	 * updated in morse_mac_join_ibss and morse_mac_leave_ibss.
	 */
	bool is_ibss_node_joined;

	/**
	 * Flag to keep track if unicast/directed probe req needs to be sent.
	 */
	bool waiting_for_probe_req_sched;

	/**
	 * Flag to indicate if this VIF supports hw PS filtering
	 * (see IEEE80211_TX_STAT_TX_FILTERED). If support is not enabled,
	 * frames returned from the LMAC with this flag set will be dropped.
	 */
	bool supports_ps_filter;

	/**
	 * Stores the station channel info after association
	 */
	struct morse_channel_info assoc_sta_channel_info;

	/**
	 * Stores the ECSA channel info
	 */
	struct morse_channel_info ecsa_channel_info;

	/**
	 * Stores MBSSID IE info to select transmitting/non-transmitting BSS
	 */
	struct morse_mbssid_info mbssid_info;

	/**
	 * Channel Switch Timer for station mode
	 */
	struct timer_list chswitch_timer;

	/**
	 * Flag to check if ECSA Info IEs needs to be masked in beacon & probe response.
	 */
	bool mask_ecsa_info_in_beacon;

	/**
	 * Flag to check if new channel configured to send full beacon in new channel
	 * irrespective of short beacon interval.
	 */
	bool ecsa_chan_configured;

	/**
	 * Work queue to configure ecsa channel
	 */
	struct delayed_work ecsa_chswitch_work;

	/**
	 * Save PV1 status for this VIF
	 */
	struct morse_pv1 pv1;

	/**
	 * Flag to check if PV1 support is allowed for this vif
	 */
	bool enable_pv1;

	/**
	 *  Tasklet for sending unicast directed probe request from Morse driver
	 */
	struct tasklet_struct send_probe_req;

	/**
	 * Holds page slicing information like page period, page slice control and bitmap.
	 */
	struct page_slicing page_slicing_info;

};

struct morse_debug {
	struct dentry *debugfs_phy;
#ifdef CONFIG_MORSE_DEBUG_TXSTATUS
	 DECLARE_KFIFO(tx_status_entries, struct morse_skb_tx_status, 1024);
#endif
	struct {
		struct {
			unsigned int tx_beacons;
			unsigned int tx_ndpprobes;
			unsigned int tx_count;
			unsigned int tx_success;
			unsigned int tx_fail;
			unsigned int rx_count;
		} mcs0;
		struct {
			unsigned int tx_count;
			unsigned int tx_success;
			unsigned int tx_fail;
			unsigned int rx_count;
		} mcs10;
	} mcs_stats_tbl;
	struct {
		unsigned int cmd_tx;
		unsigned int bcn_tx;
		unsigned int mgmt_tx;
		unsigned int data_tx;
		unsigned int write_fail;
		unsigned int no_page;
		unsigned int cmd_no_page;
		unsigned int cmd_rsv_page_retry;
		unsigned int bcn_no_page;
		unsigned int excessive_bcn_loss;
		unsigned int queue_stop;
		unsigned int page_owned_by_chip;
		unsigned int tx_ps_filtered;
		unsigned int tx_status_flushed;
		unsigned int tx_status_page_invalid;
		unsigned int tx_status_duty_cycle_cant_send;
		unsigned int tx_status_dropped;
		unsigned int rx_empty;
		unsigned int rx_split;
		unsigned int invalid_checksum;
		unsigned int invalid_tx_staus_ckecksum;
	} page_stats;
#if defined(CONFIG_MORSE_DEBUG_IRQ)
	struct {
		unsigned int irq;
		unsigned int irq_bits[32];
	} hostsync_stats;
#endif
#ifdef CONFIG_MORSE_DEBUGFS
	struct {
		struct mutex lock;
		wait_queue_head_t waitqueue;
		int active_clients;
		struct list_head items;
		int enabled_channel_mask;
	} hostif_log;
#endif
	struct dentry *debugfs_logging;
};

/**
 * struct morse_channel_survey - RF/traffic characteristics of a channel
 * @time_listen: total time spent receiving, usecs
 * @time_rx: duration of time spent receiving, usecs
 * @freq_hz: the center frequency of the channel
 * @bw_mhz: The bandwidth of the channel
 * @noise: channel noise, dBm
 */
struct morse_survey_rx_usage_record {
	u64 time_listen;
	u64 time_rx;
	u32 freq_hz;
	u8 bw_mhz;
	s8 noise;
};

struct morse_channel_survey {
	bool first_channel_in_scan;
	int num_records;
	struct morse_survey_rx_usage_record *records;
};

struct morse_watchdog {
	struct hrtimer timer;
	int interval_secs;
	watchdog_callback_t ping;
	watchdog_callback_t reset;
	int consumers;
	struct mutex lock;
	int paused;
};

struct morse_stale_tx_status {
	spinlock_t lock;
	struct timer_list timer;
	bool enabled;
};

struct mcast_filter {
	u8 count;
	/* Integer representation of the last four bytes of a multicast MAC address.
	 * The first two bytes are always 0x0100 (IPv4) or 0x3333 (IPv6).
	 */
	u32 addr_list[];
};

/** State flags for managing state of mors object */
enum morse_state_flags {
	/** Pushing/Pulling from the mac80211 DATA Qs have been stopped */
	MORSE_STATE_FLAG_DATA_QS_STOPPED,
	/** Sending TX data to the chip has been stopped */
	MORSE_STATE_FLAG_DATA_TX_STOPPED,
	/** Regulatory domain has been set by the user using `iw reg set` */
	MORSE_STATE_FLAG_REGDOM_SET_BY_USER,
	/** An action on initialisation requires a FW reload. (eg. regdom change) */
	MORSE_STATE_FLAG_RELOAD_FW_AFTER_START,
	/** Perform a core dump on next mac restart */
	MORSE_STATE_FLAG_DO_COREDUMP,
};

/**
 * State flags are cleared on .start(). Bits specified here will not be cleared
 */
#define MORSE_STATE_FLAG_KEEP_ON_START_MASK	(BIT(MORSE_STATE_FLAG_REGDOM_SET_BY_USER))

#define MORSE_MAX_IF (2)
#define MORSE_COUNTRY_LEN (3)
#define INVALID_VIF_INDEX 0xFF

struct morse {
	u32 chip_id;

	/** Refer to enum morse_host_bus_type */
	u32 bus_type;

	/* Parsed from the release tag, which should be in the format
	 * 'rel_<major>_<minor>_<patch>'. If the tag is not in this format
	 * then corresponding version field will be 0.
	 */
	struct morse_sw_version sw_ver;
	u8 macaddr[ETH_ALEN];
	u8 country[MORSE_COUNTRY_LEN];

	/* mask of type \enum host_table_firmware_flags */
	u32 firmware_flags;
	struct morse_caps capabilities;

	bool started;
	/** @in_scan: whether the chip has been configured for scan mode (softmac only). */
	bool in_scan;
	bool reset_required;

	/* wiphy device registered with cfg80211 */
	struct wiphy *wiphy;

	struct morse_channel_survey *channel_survey;

	struct ieee80211_hw *hw;

	/* Array of vif pointers, indexed by vif ID. Allocated based on max interfaces supported.
	 * Do not access directly. Use morse_get_vif_* functions.
	 */
	struct ieee80211_vif **vif;
	/* Size of the above array */
	u16 max_vifs;

	/* spinlock to protect vif array */
	spinlock_t vif_list_lock;

	struct device *dev;
	/** See morse_state_flags */
	unsigned long state_flags;

	/* Command sequence counter */
	u16 cmd_seq;
	struct completion *cmd_comp;
	struct mutex cmd_lock;
	struct mutex cmd_wait;

	/** Address in hardware to write the BCF file */
	u32 bcf_address;

	struct tasklet_struct tasklet_txq;
	struct mutex lock;
	/**
	 * 80211n channel number - may or may not map to currently selected
	 * s1g channel.
	 */
	int channel_num_80211n;

	/* Deprecated, required for platform support */
	int rb_cnt;
	struct morse_rb *rb;

	int enable_subbands;

	/* Chip interface variables */
	struct morse_chip_if_state *chip_if;
	/* Work queue used by code directly talking to the chip */
	struct workqueue_struct *chip_wq;
	struct work_struct chip_if_work;

	/* Used to periodically check for stale tx skbs */
	struct morse_stale_tx_status stale_status;

#ifdef CONFIG_MORSE_USER_ACCESS
	struct uaccess_device udev;
#endif

	/* power saving */
	bool config_ps;
	struct morse_ps ps;

	/* Tx Power in mBm received from the FW before association */
	s32 tx_power_mbm;
	s32 tx_max_power_mbm;

	bool enable_mbssid_ie;
#ifdef CONFIG_MORSE_RC
	struct morse_rc mrc;
	int rts_threshold;
#endif
	struct morse_vif mon_if;	/* monitor interface */

	struct morse_hw_cfg *cfg;
	const struct morse_bus_ops *bus_ops;

	/* Work queue used by code copying between Linux and local buffers */
	struct workqueue_struct *net_wq;

	/* Work queues for resetting and restarting the system */
	struct work_struct reset;
	struct work_struct soft_reset;
	struct work_struct driver_restart;
	struct work_struct health_check;
	struct work_struct tx_stale_work;

	/** Tasklet for responding to NDP probe requests received by chip */
	struct tasklet_struct ndp_probe_req_resp;

	struct morse_debug debug;

	char *board_serial;

	/* Stored Channel Information, sta_type, enc_mode, RAW */
	struct morse_custom_configs custom_configs;

	/* watchdog */
	struct morse_watchdog watchdog;

	/** Multicast filter list */
	struct mcast_filter *mcast_filter;

	/* reset stats */
	u32 restart_counter;

	/** Extra timeout applied to wait for ctrl-resp frames */
	int extra_ack_timeout_us;

	/* must be last */
	u8 drv_priv[] __aligned(sizeof(void *));

};

/* Map from mac80211 queue to Morse ACI value for page metadata */
static inline u8 map_mac80211q_2_morse_aci(u16 mac80211queue)
{
	switch (mac80211queue) {
	case IEEE80211_AC_VO:
		return MORSE_ACI_VO;
	case IEEE80211_AC_VI:
		return MORSE_ACI_VI;
	case IEEE80211_AC_BK:
		return MORSE_ACI_BK;
	default:
		return MORSE_ACI_BE;
	}
}

/**
 * Convert dot11 traffic ID (TID) to WMM access category (AC)
 *
 * @param TID 4-bit TID value
 *
 * @return QoS AC index
 */
static inline enum morse_page_aci dot11_tid_to_ac(enum qos_tid_up_index tid)
{
	switch (tid) {
	case MORSE_QOS_TID_UP_BK:
	case MORSE_QOS_TID_UP_xx:
		return MORSE_ACI_BK;
	case MORSE_QOS_TID_UP_CL:
	case MORSE_QOS_TID_UP_VI:
		return MORSE_ACI_VI;
	case MORSE_QOS_TID_UP_VO:
	case MORSE_QOS_TID_UP_NC:
		return MORSE_ACI_VO;
	case MORSE_QOS_TID_UP_BE:
	case MORSE_QOS_TID_UP_EE:
	default:
		return MORSE_ACI_BE;
	}
}

#ifdef CONFIG_MORSE_SDIO
int __init morse_sdio_init(void);
void __exit morse_sdio_exit(void);
#endif

#ifdef CONFIG_MORSE_SPI
int __init morse_spi_init(void);
void __exit morse_spi_exit(void);
#endif


static inline bool morse_is_data_tx_allowed(struct morse *mors)
{
	return !test_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED, &mors->state_flags) &&
	    !test_bit(MORSE_DATA_TRAFFIC_PAUSE_PEND, &mors->chip_if->event_flags);
}

static inline struct ieee80211_vif *morse_vif_to_ieee80211_vif(struct morse_vif *mors_if)
{
	return container_of((void *)mors_if, struct ieee80211_vif, drv_priv);
}

static inline struct morse_vif *ieee80211_vif_to_morse_vif(struct ieee80211_vif *vif)
{
	return (struct morse_vif *)vif->drv_priv;
}

static inline struct morse *morse_vif_to_morse(struct morse_vif *mors_if)
{
	return container_of(mors_if->custom_configs, struct morse, custom_configs);
}

static inline bool morse_test_mode_is_interactive(uint test_mode)
{
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED ||
	    test_mode == MORSE_CONFIG_TEST_MODE_DOWNLOAD_AND_GET_HOST_TBL_PTR ||
	    test_mode == MORSE_CONFIG_TEST_MODE_GET_HOST_TBL_PTR_ONLY)
		return true;

	return false;
}

int morse_beacon_init(struct morse_vif *mors_if);
void morse_beacon_finish(struct morse_vif *mors_if);
void morse_beacon_irq_handle(struct morse *mors, u32 status);

int morse_ndp_probe_req_resp_enable(struct morse *mors, bool enable);
int morse_ndp_probe_req_resp_init(struct morse *mors);
void morse_ndp_probe_req_resp_finish(struct morse *mors);
void morse_ndp_probe_req_resp_irq_handle(struct morse *mors);

void morse_sdio_set_irq(struct morse *mors, bool enable);

int morse_send_probe_req_enable(struct ieee80211_vif *vif, bool enable);
int morse_send_probe_req_init(struct ieee80211_vif *vif);
void morse_send_probe_req_finish(struct ieee80211_vif *vif);
void morse_mac_schedule_probe_req(struct ieee80211_vif *vif);

#endif /* !_MORSE_MORSE_H_ */
