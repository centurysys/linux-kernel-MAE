#ifndef _MORSE_MORSE_H_
#define _MORSE_MORSE_H_

/*
 * Copyright 2017-2022 Morse Micro
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
#include "watchdog.h"
#include "raw.h"
#include "chip_if.h"
#include "operations.h"
#ifdef CONFIG_MORSE_RC
#include "rc.h"
#endif
#include "cac.h"

#ifdef CONFIG_MORSE_USER_ACCESS
#include "uaccess.h"
#endif

#ifdef MAC80211_BACKPORT_VERSION_CODE
#define MAC80211_VERSION_CODE MAC80211_BACKPORT_VERSION_CODE
#else
#define MAC80211_VERSION_CODE LINUX_VERSION_CODE
#endif

#define MORSE_DRIVER_SEMVER_MAJOR 29
#define MORSE_DRIVER_SEMVER_MINOR 0
#define MORSE_DRIVER_SEMVER_PATCH 1

#define MORSE_SEMVER_GET_MAJOR(x) ((x >> 22) & 0x3FF)
#define MORSE_SEMVER_GET_MINOR(x) ((x >> 10) & 0xFFF)
#define MORSE_SEMVER_GET_PATCH(x) (x & 0x3FF)

#define DRV_VERSION __stringify(MORSE_VERSION)

#define TOTAL_HALOW_CHANNELS 52

#define STA_PRIV_BACKUP_NUM (10)

#define SERIAL_SIZE_MAX  32
#define BCF_SIZE_MAX     48

/** Maximum number of RAWs (limited by QoS User Priority) */
#define MAX_NUM_RAWS	(8)

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
#define MHZ_TO_HZ(x) (x * 1000000)
#define HZ_TO_MHZ(x) (x / 1000000)

/**
 * enum morse_config_test_mode - test mode
 * @MORSE_CONFIG_TEST_MODE_DISABLED: normal operation
 * @MORSE_CONFIG_TEST_MODE_DOWNLOAD: download only (no verification)
 * @MORSE_CONFIG_TEST_MODE_RESET: reset only (no download or verification)
 * @MORSE_CONFIG_TEST_MODE_BUS: write/read block via the bus
 */
enum morse_config_test_mode {
	MORSE_CONFIG_TEST_MODE_DISABLED = 0,
	MORSE_CONFIG_TEST_MODE_DOWNLOAD = 1,
	MORSE_CONFIG_TEST_MODE_RESET = 2,
	MORSE_CONFIG_TEST_MODE_BUS = 3,
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
 * @sta_data		The lastest station data including a list of AIDs
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
 * @wake_intervals	List of stuctures each used as heads for lists of agreements with the same
 *			wake interval. These are arranged in order from the smallest to largest wake
 *			intervals.
 * @events		A queue of TWT events to be processed.
 * @tx			A queue of TWT data to be sent.
 * @to_install		A queue of TWT agreements to install to the chip.
 * @req_event_tx	A TWT request event to be sent in the (re)assoc request frame.
 * @work		Work queue struct to defer processing of events.
 * @lock		Mutex used to control access to lists/memory.
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
	struct mutex lock;
	bool requester;
	bool responder;
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
	char *dhcpc_lease_update_script;
};

struct morse_ps {
	/* Number of clients requesting to talk to chip */
	u32	wakers;
	bool enable;
	bool suspended;
	bool dynamic_ps_en;
	unsigned long bus_ps_timeout;
	struct mutex lock;
	struct work_struct async_wake_work;
	struct delayed_work delayed_eval_work;
};

/* Morse ACI map for page metadata  */
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
	MORSE_QOS_TID_UP_xx = 2,   /* Not specified */
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
 * struct morse_vendor_info - filled from mm vendor ie
 */
struct morse_vendor_info {
	/** Indicates if vendor info is valid (and has been filled) */
	bool valid;
	/** Identifies underlying hardware of device */
	u32 chip_id;
	/** Identifies underlying software ver of device */
	struct morse_sw_version sw_ver;
	/** Operational features in use on device */
	struct morse_ops operations;
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
	/** Vendor information - filled from mm vendor ie */
	struct morse_vendor_info vendor_info;
#ifdef CONFIG_MORSE_RC
	/** Morse Micro RC info*/
	struct morse_rc_sta rc;
	struct mmrc_rate last_sta_tx_rate;
#endif
	/** When set, frames destined for this STA must be returned to mac80211
	 *  for rescheduling. Cleared after frame destined for STA has
	 *  IEEE80211_TX_CTL_CLEAR_PS_FILT set.
	 */
	bool tx_ps_filter_en;
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

struct morse_vif {
	u16 id;		/* interface ID from chip */
	u16 dtim_count;

	/**
	 * Used to keep track of time for beacons
	 * and probe responses. This is an approximation!
	 * Proper solution is to use timing from PHY.
	 *
	 * This field is in linux jiffies.
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
	 * to the firmware so that it can adjust timeouts as neccesary.
	 *
	 * To be strictly standards compliant with ack times and
	 * avoid uneccesary performance degredation this should be
	 * tracked per MAC address + vif as individual STAs may or may
	 * not opt into it.
	 */
	bool ctrl_resp_in_1mhz_en;

	/**
	 * CAC (Centralised Authentication Control)
	 */
	struct morse_cac cac;
	/**
	 * Configured BSS color.
	 * Only valid after association response is received for STAs
	 */
	u8 bss_color;

	/**
	 * TWT state information.
	 */
	struct morse_twt twt;

	/**
	 * AP mode specific information
	 * NULL if not an AP
	 */
	struct morse_ap *ap;

	struct {
		/** List of run-time configurable vendor IEs to insert into management frames */
		struct list_head ie_list;

		/** List of vendor IE OUIs for which to generate a netlink event if seen in a mgmt frame */
		struct list_head oui_filter_list;

		/** Number of elementes on oui_filter_list */
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
	 *  vendor ie. Only valid after association response is received for STAs.
	 */
	struct morse_vendor_info bss_vendor_info;

	struct ieee80211_s1g_cap s1g_cap_ie;

	/**
	 *  Keeping track of beacon change sequence number for both AP and STA
	 */
	u16 s1g_bcn_change_seq;

	/**
	 * To keep track of chanel switch in progress and to restrict
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
	 * Flag to check if STA is in asscoiated state. This gets populated in
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
		unsigned int tx_status_dropped;
	} page_stats;
#ifdef CONFIG_MORSE_DEBUGFS
	struct {
		struct mutex lock;
		wait_queue_head_t waitqueue;
		int active_clients;
		struct list_head items;
		int enabled_channel_mask;
	} hostif_log;
#endif
};

/**
 * struct morse_channel_survey - RF/traffic characteristics of a channel
 * @time_listen: total time spent receiving, usecs
 * @time_rx: duration of time spent receiving, usecs
 * @noise: channel noise, dBm
 */
struct morse_channel_survey {
	u64 time_listen;
	u64 time_rx;
	s8 noise;
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

/** State flags for managing state of mors object */
enum morse_state_flags {
	/** Pushing/Pulling from the mac80211 DATA Qs have been stopped */
	MORSE_STATE_FLAG_DATA_QS_STOPPED,
	/** Sending TX data to the chip has been stopped */
	MORSE_STATE_FLAG_DATA_TX_STOPPED,
};

#define MORSE_MAX_IF (2)
#define MORSE_COUNTRY_LEN (3)

struct morse {
	u32 chip_id;

	/* Parsed from the release tag, which should be in the format
	 * 'rel_<major>_<minor>_<patch>'. If the tag is not in this format
	 * then corresponding version field will be 0.
	 */
	struct morse_sw_version sw_ver;
	u8 macaddr[ETH_ALEN];
	u8 country[MORSE_COUNTRY_LEN];

	struct morse_caps capabilities;

	bool started;
	bool in_scan;
	bool reset_required;

	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif[MORSE_MAX_IF];
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
	struct work_struct usb_irq_work;

	/* Used to periodically check for stale tx skbs */
	struct morse_stale_tx_status stale_status;

#ifdef CONFIG_MORSE_USER_ACCESS
	struct uaccess_device udev;
#endif

	/* power saving */
	bool config_ps;
	struct morse_ps ps;

	/* Tx Power in dBm received from the FW before association */
	s32 tx_power_dBm;
	s32 max_power_level;

#ifdef CONFIG_MORSE_RC
	struct morse_rc mrc;
	int rts_threshold;
#endif
	struct morse_vif mon_if; /* monitor interface */

	struct morse_hw_cfg *cfg;
	const struct morse_bus_ops *bus_ops;

	struct workqueue_struct *command_wq;
	/* Work queue used by code copying between linux and local buffers */
	struct workqueue_struct *net_wq;
	struct tasklet_struct bcon_tasklet;

	/* Work queues for resetting and restarting the system */
	struct work_struct reset;
	struct work_struct soft_reset;
	struct work_struct driver_restart;
	struct work_struct health_check;
	struct work_struct tx_stale_work;

	/** Tasklet for responding to NDP probe requests received by chip */
	struct tasklet_struct ndp_probe_req_resp;

	/**
	 *  Tasklet for sending unicast directed probe request from Morse driver
	 */
	struct tasklet_struct send_probe_req;

	struct morse_debug debug;

	char *board_serial;

	/* Stored Channel Information, sta_type, enc_mode, RAW */
	struct  morse_custom_configs custom_configs;

	/* watchdog */
	struct morse_watchdog watchdog;

	/* reset stats */
	u32 restart_counter;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));

};

/* Map from mac80211 queue to Morse ACI value for page metadata */
static inline u8 map_mac80211q_2_morse_aci(u16 mac80211queue)
{
	switch (mac80211queue) {
	case IEEE80211_AC_VO:
		return MORSE_ACI_VO;
	break;
	case IEEE80211_AC_VI:
		return MORSE_ACI_VI;
	break;
	case IEEE80211_AC_BK:
		return MORSE_ACI_BK;
	break;
	default:
		return MORSE_ACI_BE;
	}
}

/**
 * Convert dot11 traffic ID (TID) to WMM access category (AC)
 *
 * @param tid 4 bit TID value
 *
 * @return qos AC index
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

#ifdef CONFIG_MORSE_USB
int __init morse_usb_init(void);
void __exit morse_usb_exit(void);
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

int morse_beacon_enable(struct morse *mors, bool enable);
int morse_beacon_init(struct morse *mors);
void morse_beacon_finish(struct morse *mors);
void morse_beacon_irq_handle(struct morse *mors);

int morse_ndp_probe_req_resp_enable(struct morse *mors, bool enable);
int morse_ndp_probe_req_resp_init(struct morse *mors);
void morse_ndp_probe_req_resp_finish(struct morse *mors);
void morse_ndp_probe_req_resp_irq_handle(struct morse *mors);

void morse_sdio_set_irq(struct morse *mors, bool enable);

int morse_send_probe_req_enable(struct morse *mors, bool enable);
int morse_send_probe_req_init(struct morse *mors);
void morse_send_probe_req_finish(struct morse *mors);

/** Some kernels won't have spectre mitigations. To get around this, we just define this
 * as a macro to pass the value through
 */
#ifndef array_index_nospec
#define array_index_nospec(x, y) (x)
#endif

#endif	/* !_MORSE_MORSE_H_ */
