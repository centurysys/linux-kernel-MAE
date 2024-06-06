/*
 * Copyright 2017-2022 Morse Micro
 */

#ifndef _RAW_H_
#define _RAW_H_

#include <linux/types.h>
#include <linux/workqueue.h>

#define MAX_NUM_RAWS_USER_PRIO	(8)	/* Limited by QoS User Priority */
#define MAX_NUM_RAWS_INTERNAL	(1)	/* Internal (e.g. used by OCS) */
#define MAX_NUM_RAWS		(MAX_NUM_RAWS_USER_PRIO + MAX_NUM_RAWS_INTERNAL)

#define MORSE_RAW_DEFAULT_START_AID			(1)

/* AID mask used for creating the RAW priority groups */
#define MORSE_RAW_AID_PRIO_MASK				GENMASK(10, 8)
#define MORSE_RAW_AID_PRIO_SHIFT			(8)
#define MORSE_RAW_GET_PRIO(x) \
	(((x) & MORSE_RAW_AID_PRIO_MASK) >> MORSE_RAW_AID_PRIO_SHIFT)
#define MORSE_RAW_GET_SUB_AID(x) \
	((x) & ~MORSE_RAW_AID_PRIO_MASK)
#define MORSE_RAW_AID_DEVICE_MASK			GENMASK(7, 0)

struct morse;
struct morse_cmd_raw;

/**
 * enum ieee80211_s1g_rps_raw_type - types of RAW possible in the RPS IE
 *
 * @IEEE80211_S1G_RPS_RAW_TYPE_GENERIC: generic RAW type
 * @IEEE80211_S1G_RPS_RAW_TYPE_SOUNDING: sounding RAW type
 * @IEEE80211_S1G_RPS_RAW_TYPE_SIMPLEX: simplex RAW type
 * @IEEE80211_S1G_RPS_RAW_TYPE_TRIGGERING: triggering RAW type
 */
enum ieee80211_s1g_rps_raw_type {
	IEEE80211_S1G_RPS_RAW_TYPE_GENERIC = 0,
	IEEE80211_S1G_RPS_RAW_TYPE_SOUNDING = 1,
	IEEE80211_S1G_RPS_RAW_TYPE_SIMPLEX = 2,
	IEEE80211_S1G_RPS_RAW_TYPE_TRIGGERING = 3,
};

enum ieee80211_s1g_rps_raw_sounding_type {
	IEEE80211_S1G_RPS_RAW_TYPE_SST_SOUNDING = 0,
	IEEE80211_S1G_RPS_RAW_TYPE_SST_REPORT = 1,
	IEEE80211_S1G_RPS_RAW_TYPE_SECTOR_SOUNDING = 2,
	IEEE80211_S1G_RPS_RAW_TYPE_SECTOR_REPORT = 3,
};

enum ieee80211_s1g_rps_raw_simplex_type {
	IEEE80211_S1G_RPS_RAW_TYPE_AP_PM = 0,
	IEEE80211_S1G_RPS_RAW_TYPE_NON_TIM = 1,
	IEEE80211_S1G_RPS_RAW_TYPE_OMNI = 2,
};

/** Structure with configuration parameters specific to the Generic RAW */
struct morse_raw_config_generic_t {
	/** Whether or not only paged STAs may transmit. */
	bool paged_sta;

	/** Whether or not to transmit a resource allocation frame at the beginning of the raw. */
	bool ra_frame;

	/** Whether or not to use the last group definition or use a new one. */
	bool group_same_as_prev;

	/** Whether or not to allow cross slot boundary bleed over. */
	bool cross_slot_boundary;

	/** Number of slots in the RAW. */
	u16 num_slots;

	/** Duration of the slot in the RAW in microseconds, maximum duration is 246260us. */
	u32 slot_duration_us;
};

/** Structure with configuration parameters specific to the Sounding RAW */
struct morse_raw_config_sounding_t {
	/** Type of Sounding RAW. */
	enum ieee80211_s1g_rps_raw_sounding_type sounding_type;

	/** Whether or not to use the last group definition or use a new one. */
	bool group_same_as_prev;
};

/** Structure with configuration parameters specific to the Simplex RAW */
struct morse_raw_config_simplex_t {
	/** Type of Simplex RAW. */
	enum ieee80211_s1g_rps_raw_simplex_type simplex_type;

	/** Whether to exclude non-AP STAs. If not excluded then define group. */
	bool exclude_non_ap_sta;
};

/** Structure with configuration parameters specific to the Triggering RAW */
struct morse_raw_config_triggering_t {
	/** Whether or not to use the last group definition or use a new one. */
	bool group_same_as_prev;
};

/**
 * struct morse_raw - contains RAW state and configuration information
 *
 * @num_stations	Number of stations in the station list
 * @station_idx		Index that also counts the number of stations matching our criteria
 * @aids		Array of AIDs taken from the station list
 */
struct morse_raw_station_data {
	u16 num_stations;
	u16 station_idx;
	u16 *aids;
};

/** Structure containing configuration information creating for RAWs in an RPS IE. */
struct morse_raw_config {
	enum ieee80211_s1g_rps_raw_type type;

	/* Common parameters */
	/** Whether or not this RAW configuration is enabled or not. */
	bool enabled;

	/** Start time offset from the last RAW or beacon in microseconds. */
	u32 start_time_us;

	/*
	 * If the generic/sounding/triggering RAW isn't using the previous group definition then use
	 * these AID values.
	 */
	/** Starting AID for the RAW. */
	u16 start_aid;
	/** Ending AID for the RAW. */
	u16 end_aid;

	/**
	 * The index into the station data aid list for the first AID in the range of this
	 * config.
	 */
	s32 start_aid_idx;
	/**
	 * The index into the station data aid list for the last AID in the range of this
	 * config.
	 */
	s32 end_aid_idx;
	/**
	 * When spreading STAs over multiple beacons this is the maximum number of beacons to cycle
	 * over. If 0 then there is no maximum beacon spread.
	 */
	u16 max_beacon_spread;
	/**
	 * When spreading STAs over multiple beacons this is the number to place in a beacon before
	 * increasing the number of beacons to cycle over. If 0 then spreading over multiple beacons
	 * is disabled.
	 */
	u16 nominal_sta_per_beacon;
	/** Last AID that was used in a beacon with spreading. */
	u16 last_spread_aid;

	/** Whether or not the RAW uses channel indication */
	bool has_channel_indication;

	/* Optional periodic configuration information. */
	/** Whether or not the RAW is periodic. */
	bool is_periodic;
	/** Period of the RAW in number of beacons. */
	u8 periodicity;
	/** Validity of the RAW in number of beacons. */
	u8 validity;
	/** Start offset from the number of beacons. */
	u8 period_start_offset;

	/** RAW type specific configuration information. */
	union {
		/** Generic RAW specific configuration information. */
		struct morse_raw_config_generic_t generic;
		/** Sounding RAW specific configuration information. */
		struct morse_raw_config_sounding_t sounding;
		/** Simplex RAW specific configuration information. */
		struct morse_raw_config_simplex_t simplex;
		/** Triggering RAW specific configuration information. */
		struct morse_raw_config_triggering_t triggering;
	};
};

/**
 * morse_raw_get_rps_ie_size() - Gets the size of the RPS IE for current RAW settings.
 * @mors: Morse chip struct
 *
 * Return: Size of the RPS IE or 0 on error.
 */
u8 morse_raw_get_rps_ie_size(struct morse *mors);

/**
 * morse_raw_get_rps_ie() - Gets the size of the RPS IE for current RAW settings.
 * @mors:	Morse chip struct
 *
 * Use morse_raw_get_rps_ie_size() to calculate the size to allocate &rps_ie.
 *
 * Return: a pointer to the RPS IE or NULL on error.
 */
u8 *morse_raw_get_rps_ie(struct morse *mors);

/**
 * morse_raw_refresh_aids() - update the AID assignments in the current RAW configuration
 *
 * @work: Workqueue structure
 *
 * When STAs are added and removed the configuration will need to be updated
 */
void morse_raw_refresh_aids(struct work_struct *work);

/**
 * morse_raw_process_cmd() - Execute command to enable/disable/configure RAW
 *
 * @mors	Morse chip struct
 * @cmd		Command from morsectrl
 */
void morse_raw_process_cmd(struct morse *mors, struct morse_cmd_raw *cmd);

/**
 * morse_raw_enable() - Enable RAW functionality.
 * @mors: Morse chip struct
 *
 * Return: 0 - OK
 */
int morse_raw_enable(struct morse *mors);

/**
 * morse_raw_disable() - Disable RAW functionality.
 * @mors: Morse chip struct
 *
 * Return: 0 - OK
 */
int morse_raw_disable(struct morse *mors);

/**
 * morse_raw_init() - Initialise RAW.
 * @mors: Morse chip struct
 *
 * Return: 0 - OK
 */
int morse_raw_init(struct morse *mors, bool enable);

/**
 * morse_raw_finish() - Clean up RAW on finish.
 * @mors: Morse chip struct
 */
void morse_raw_finish(struct morse *mors);

#endif
