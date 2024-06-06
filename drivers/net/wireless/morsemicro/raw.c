/*
 * Copyright 2017-2023 Morse Micro
 */
#include <linux/sort.h>
#include "debug.h"
#include "raw.h"
#include "command.h"

#define INVALID_AID_VALUE				(-1)
#define INVALID_AID_IDX_VALUE				(-1)

/* Masks for RAW assignment */
#define IEEE80211_S1G_RPS_RAW_CONTROL_TYPE_SHIFT	(0)
#define IEEE80211_S1G_RPS_RAW_CONTROL_TYPE		GENMASK(1, 0)

/**
 * enum ieee80211_s1g_rps_raw_control_generic_flags - generic RAW flags
 *
 * @IEEE80211_S1G_RPS_RAW_CONTROL_GENERIC_PSTA: paged STA flag
 * @IEEE80211_S1G_RPS_RAW_CONTROL_GENERIC_RAFRAME: RA frame flag
 */
enum ieee80211_s1g_rps_raw_control_generic_flags {
	IEEE80211_S1G_RPS_RAW_CONTROL_GENERIC_PSTA = BIT(0),
	IEEE80211_S1G_RPS_RAW_CONTROL_GENERIC_RAFRAME = BIT(1),
};

#define IEEE80211_S1G_RPS_RAW_CONTROL_TYPE_OPTION_SHIFT	(2)
#define IEEE80211_S1G_RPS_RAW_CONTROL_TYPE_OPTION	GENMASK(3, 2)

enum ieee80211_s1g_rps_raw_control_ind_flags {
	IEEE80211_S1G_RPS_RAW_CONTROL_START_IND = BIT(4),
	IEEE80211_S1G_RPS_RAW_CONTROL_GROUP_IND = BIT(5),
	IEEE80211_S1G_RPS_RAW_CONTROL_CHAN_IND = BIT(6),
	IEEE80211_S1G_RPS_RAW_CONTROL_PERIODIC_IND = BIT(7),
};

/**
 * enum ieee80211_s1g_rps_raw_slot_flags - RAW slot flags
 *
 * @IEEE80211_S1G_RPS_RAW_SLOT_DEF_FORMAT: slot format determining bits for duration
 * @IEEE80211_S1G_RPS_RAW_SLOT_CROSS_BOUNDARY: cross slot boundary bleed over
 */
enum ieee80211_s1g_rps_raw_slot_flags {
	IEEE80211_S1G_RPS_RAW_SLOT_DEF_FORMAT = BIT(0),
	IEEE80211_S1G_RPS_RAW_SLOT_CROSS_BOUNDARY = BIT(1),
};

#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_3BITS		(3)
#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_6BITS		(6)

#define IEEE80211_S1G_RPS_RAW_SLOT_DUR_8BITS		(8)
#define IEEE80211_S1G_RPS_RAW_SLOT_DUR_11BITS		(11)

#define IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_SHIFT		(2)
#define IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_8		GENMASK(9, 2)
#define IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_11		GENMASK(12, 2)

#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_6_SHIFT		(10)
#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_3_SHIFT		(13)
#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_6		GENMASK(16, 10)
#define IEEE80211_S1G_RPS_RAW_SLOT_NUM_3		GENMASK(15, 13)

/* Masks for RAW groups */
#define IEEE80211_S1G_RPS_RAW_GROUP_PAGE_IDX_SHIFT	(0)
#define IEEE80211_S1G_RPS_RAW_GROUP_PAGE_IDX		GENMASK(1, 0)

#define AID_BITS					(11)
#define AID_END_BITS_SHIFT				(16 - 2 - AID_BITS)

#define IEEE80211_S1G_RPS_RAW_GROUP_START_AID_SHIFT	(2)
#define IEEE80211_S1G_RPS_RAW_GROUP_START_AID		GENMASK(AID_BITS + 1, 2)

#define IEEE80211_S1G_RPS_RAW_GROUP_END_AID_SHIFT	(13)
#define IEEE80211_S1G_RPS_RAW_GROUP_END_AID		GENMASK(AID_END_BITS_SHIFT + 12, 13)

/* Masks for RAW channel indication */
#define IEEE80211_S1G_RPS_RAW_CHAN_MAX_TRAN_WIDTH_SHIFT	(0)
#define IEEE80211_S1G_RPS_RAW_CHAN_MAX_TRAN_WIDTH_BITS	(2)
#define IEEE80211_S1G_RPS_RAW_CHAN_MAX_TRAN_WIDTH	GENMASK(1, 0)

/**
 * enum ieee80211_s1g_rps_raw_chan_activity_flags - RAW SST activity flags
 *
 * @IEEE80211_S1G_RPS_RAW_CHAN_UL_ACTIVITY: UL Activity subfield flag
 * @IEEE80211_S1G_RPS_RAW_CHAN_DL_ACTIVITY: DL Activity subfield flag
 */
enum ieee80211_s1g_rps_raw_chan_activity_flags {
	IEEE80211_S1G_RPS_RAW_CHAN_UL_ACTIVITY = BIT(3),
	IEEE80211_S1G_RPS_RAW_CHAN_DL_ACTIVITY = BIT(4),
};

/** Minimum slot duration in us. (Corresponds to a cslot value of 0) */
#define MORSE_RAW_MIN_SLOT_DURATION_US	(500)

/**
 * CSLOT_TO_US() - Convert a cslot value to a microseconds (us) duration.
 * @x: cslot value to convert
 *
 * Returns: a duration in microseconds (us)
 */
#define CSLOT_TO_US(x) (MORSE_RAW_MIN_SLOT_DURATION_US + ((x) * 120))

/**
 * US_TO_CSLOT() - Convert a microseconds (us) duration to a cslot value.
 * @x: microseconds duration to convert
 *
 * Returns: a cslot value
 *
 * It is worth noting that the cslot is not value checked here. Checking for
 * overflow or negative values should be done elsewhere.
 */
#define US_TO_CSLOT(x)	(((x) - MORSE_RAW_MIN_SLOT_DURATION_US) / 120)

/**
 * US_TO_TWO_TU() - Convert a microseconds (us) duration to a 2TU (2.048ms units) value.
 * @x: microseconds duration to convert
 *
 * Returns: a 2TU value
 */
#define US_TO_TWO_TU(x)	((x) / (1024 * 2))

/**
 * TWO_TU_TO_US() - Convert a 2TU (2.048ms units) value to a microseconds (us) duration.
 * @x: 2TU value to convert
 *
 * Returns: a duration in microseconds (us)
 */
#define TWO_TU_TO_US(x)	((x) * (1024 * 2))

#define MORSE_RAW_DBG(_m, _f, _a...)		morse_dbg(FEATURE_ID_RAW, _m, _f, ##_a)
#define MORSE_RAW_INFO(_m, _f, _a...)		morse_info(FEATURE_ID_RAW, _m, _f, ##_a)
#define MORSE_RAW_WARN(_m, _f, _a...)		morse_warn(FEATURE_ID_RAW, _m, _f, ##_a)
#define MORSE_RAW_ERR(_m, _f, _a...)		morse_err(FEATURE_ID_RAW, _m, _f, ##_a)

enum morse_cmd_raw_enable_type {
	RAW_CMD_ENABLE_TYPE_GLOBAL = 0,
	RAW_CMD_ENABLE_TYPE_SINGLE = 1,
};

/**
 * struct ieee80211_s1g_rps - RPS element, contains the RAW Assignment subfield
 * @raw_control:	RAW Control subfield - see Figure 9-670 (RAW Assignment
 *			subfield format(11ah)).
 * @slot_definition:	RAW Slot Definition - see Figure 9-671 (RAW Slot
 *			Definition Subfield format(11ah)).
 * @optional:		Contains various options defined in raw_control. These
 *			can include RAW Start Time, RAW Group, Channel
 *			Indication, Periodic Operation Parameters. Additional
 *			RAWs can also be specified which are added to the end.
 *
 * For details see 9.4.2.191 RPS element(11ah).
 */
struct ieee80211_s1g_rps {
	u8 raw_control;
	__le16 slot_definition;
	/* Optional 0,1,2,3,5,6,7,8 or 9 bytes: depends on @raw_control */
	u8 optional[];
} __packed;

/**
 * struct morse_raw_start_time_t - RAW start time definition
 * @start_time_2tu:	RAW start time from either the current beacon or the end of
 *			the last RAW.
 *
 * The RAW Start Time subfield indicates the duration, in units of 2 TUs, from the
 * end of the S1G Beacon, the Probe Response, or the PV1 Probe Response frame transmission
 * that includes the RPS element to the start time of the RAW.
 */
struct morse_raw_start_time_t {
	u8 start_time_2tu;
} __packed;

/**
 * struct morse_raw_group_t - RAW Group definition
 * @raw_group12:	First two octets of the group definition, includes Page
 *			Index, RAW Start AID, and 3 bits of RAW End AID
 * @raw_group3:		Contains the third octet of the group definition, includes
 *			8 bits of the RAW End AID
 *
 * The RAW Group subfield indicates the STA AIDs that are allowed restricted access within
 * the RAW period. The RAW Group subfield contains Page Index, RAW Start AID, and RAW End AID
 * subfields according to the hierarchical addressing method of AIDs
 * (see Figure 9-152 (Hierarchical structure of traffic-indication virtual bitmap carried in an
 * S1G PPDU(#2001)(11ah))).
 */
struct morse_raw_group_t {
	__le16 raw_group12;
	u8 raw_group3;
} __packed;

/**
 * struct morse_raw_channel_t - RAW Channel definition
 * @channel_activity_bitmap:	bitmap of allowed operating channels
 * @channel2:			contains the Max Transmission width, UL and DL
 *				activity indications
 *
 * The Channel Activity Bitmap subfield shows the allowed operating channels for the
 * STAs indicated in the RAW, as defined in 10.23.5.1 (General). Each bit in the bitmap
 * corresponds to one minimum width channel within the current BSS operating channels, with the
 * least significant bit corresponding to the lowest numbered operating channel of the BSS.
 */
struct morse_raw_channel_t {
	u8 channel_activity_bitmap;
	u8 channel2;
} __packed;

/**
 * morse_raw_periodic_t - Periodic RAW definition
 * @periodicity:	period of the current PRAW occurrance
 * @validity:		number of periods the PRAW repeats
 * @start_offset:	number of beacons until PRAW starts
 *
 * 9.4.2.191 RPS element(11ah)
 *
 * The PRAW Periodicity subfield indicates the period of current PRAW occurrance in the unit of
 * beacon interval if dot11ShortBeaconInterval is false and in the unit of short beacon interval if
 * dot11ShortBeaconInterval is true (see 11.1.3.10.2 (Generation of S1G Beacon frames)).
 */
struct morse_raw_periodic_t {
	u8 periodicity;
	u8 validity;
	u8 start_offset;
} __packed;

/**
 * @morse_raw_stations_count_iter() - Iterator to count connected STAs
 *
 * @data:		The data structure containing the STA count
 * @sta:		The data structure containing data for a connected STA
 */
static void morse_raw_stations_count_iter(void *data, struct ieee80211_sta *sta)
{
	struct morse_raw_station_data *iter_data = data;

	if (sta->aid != 0)
		iter_data->num_stations++;
}

/**
 * @morse_raw_stations_aid_iter() - Iterator to obtain STA AIDs
 *
 * @data:		The data structure containing the STA count and AIDs
 * @sta:		The data structure containing data for a connected STA
 */
static void morse_raw_stations_aid_iter(void *data, struct ieee80211_sta *sta)
{
	struct morse_raw_station_data *iter_data = data;

	BUG_ON(iter_data->station_idx >= iter_data->num_stations);

	if (sta->aid != 0)
		iter_data->aids[iter_data->station_idx++] = sta->aid;
}

/**
 * @morse_raw_get_station_aid() - Get the list of AIDs from mac80211
 *
 * @mors:		Morse chip struct
 * @data:		The data structure to put the AIDs in
 *
 * Note that the data struct is expected to be initialised to zero and not contain pointers to
 * allocated memory.
 */
static void morse_raw_get_station_aid(struct morse *mors, struct morse_raw_station_data *data)
{
	BUG_ON(!data);
	BUG_ON(data->aids);

	/* Find out how many stations there are and allocate memory for the AID list. */
	ieee80211_iterate_stations_atomic(mors->hw, morse_raw_stations_count_iter, data);
	if (data->num_stations > 0) {
		data->aids = kmalloc_array(data->num_stations, sizeof(*data->aids), GFP_ATOMIC);
		/* Now we can get the list of AIDs. */
		ieee80211_iterate_stations_atomic(mors->hw, morse_raw_stations_aid_iter, data);
	}
}

/**
 * @morse_raw_aid_compare() - Function for comparing AIDs for sorting
 *
 * @a:		First AID to compare
 * @b:		Second AID to compare
 *
 * Return: 1 if (a > b), -1 if (a < b), 0 if (a == b).
 *
 * Use this with heap sort.
 */
static int morse_raw_aid_compare(const void *a, const void *b)
{
	u16 arg1 = *(const u16 *)a;
	u16 arg2 = *(const u16 *)b;

	if (arg1 < arg2)
		return -1;
	if (arg1 > arg2)
		return 1;

	return 0;
}

/**
 * morse_raw_get_slot_config() - Create the slot configuration for the packed struct.
 * @mors:		Morse chip struct
 * @type:		RAW type (e.g. Generic, Sounding etc.)
 * @num_slots:		Number of slots for the RAW
 * @slot_duration_us:	Duration of each slot in the RAW in us (values above
 *			246260us will be capped)
 * @slot_definition:	Slot definition subfield to populate
 *
 * Because the number of bits allocated to duration and the number of slots is
 * variable, preference is given to the duration. I.e. a long duration is more
 * likely to result in the number of slots being capped.
 *
 */
static void morse_raw_get_slot_config(struct morse *mors, enum ieee80211_s1g_rps_raw_type type,
				      u8 num_slots, u32 slot_duration_us, __le16 *slot_definition)
{
	u8 max_slots = 0;
	u16 cslot_max = 0;
	u32 cslot;

	if (slot_duration_us < MORSE_RAW_MIN_SLOT_DURATION_US) {
		cslot = US_TO_CSLOT(MORSE_RAW_MIN_SLOT_DURATION_US);
		MORSE_RAW_WARN(mors, "RAW Slot duration too short, setting to %u\n",
			       MORSE_RAW_MIN_SLOT_DURATION_US);
	} else {
		cslot = US_TO_CSLOT(slot_duration_us);
	}

	MORSE_RAW_DBG(mors, "Slot duration us, cslot: %u, %u\n", slot_duration_us, cslot);

	/* Clear all information except the cross slot boundary setting. */
	*slot_definition &= cpu_to_le16(IEEE80211_S1G_RPS_RAW_SLOT_CROSS_BOUNDARY);

	switch (type) {
	case IEEE80211_S1G_RPS_RAW_TYPE_SOUNDING:
		cslot_max = (1 << IEEE80211_S1G_RPS_RAW_SLOT_DUR_8BITS) - 1;
		max_slots = IEEE80211_S1G_RPS_RAW_SLOT_NUM_6BITS;
		break;
	case IEEE80211_S1G_RPS_RAW_TYPE_GENERIC:
	case IEEE80211_S1G_RPS_RAW_TYPE_SIMPLEX:
	case IEEE80211_S1G_RPS_RAW_TYPE_TRIGGERING:
		/* Give longer durations preference over greater number of slots. */
		if (cslot > __UINT8_MAX__) {
			*slot_definition |= cpu_to_le16(IEEE80211_S1G_RPS_RAW_SLOT_DEF_FORMAT);
			cslot_max = (1 << IEEE80211_S1G_RPS_RAW_SLOT_DUR_11BITS) - 1;
			max_slots = IEEE80211_S1G_RPS_RAW_SLOT_NUM_3BITS;
		} else {
			cslot_max = (1 << IEEE80211_S1G_RPS_RAW_SLOT_DUR_8BITS) - 1;
			max_slots = IEEE80211_S1G_RPS_RAW_SLOT_NUM_6BITS;
		}
		break;
	}

	if (num_slots > max_slots) {
		MORSE_RAW_WARN(mors, "Too many slots: %u, capping to %u\n", num_slots, max_slots);
		num_slots = max_slots;
	}

	if (cslot > cslot_max) {
		MORSE_RAW_WARN(mors, "Slot duration too long: %u (%uus), capping to %u (%uus)\n",
			       cslot, CSLOT_TO_US(cslot), cslot_max, CSLOT_TO_US(cslot_max));
		cslot = cslot_max;
	}

	if (le16_to_cpu(*slot_definition) & IEEE80211_S1G_RPS_RAW_SLOT_DEF_FORMAT) {
		*slot_definition |= cpu_to_le16((cslot << IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_SHIFT) &
						IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_11);

		*slot_definition |= cpu_to_le16((num_slots <<
						 IEEE80211_S1G_RPS_RAW_SLOT_NUM_3_SHIFT) &
						IEEE80211_S1G_RPS_RAW_SLOT_NUM_3);
	} else {
		*slot_definition |= cpu_to_le16((cslot << IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_SHIFT) &
						IEEE80211_S1G_RPS_RAW_SLOT_DCOUNT_8);

		*slot_definition |= cpu_to_le16((num_slots <<
						 IEEE80211_S1G_RPS_RAW_SLOT_NUM_6_SHIFT) &
						IEEE80211_S1G_RPS_RAW_SLOT_NUM_6);
	}
}

u8 morse_raw_get_rps_ie_size(struct morse *mors)
{
	if (!mors->custom_configs.raw.rps_ie)
		return 0;

	return mors->custom_configs.raw.rps_ie_len;
}

/**
 * morse_raw_calc_rps_ie_size() -	Calculates the RPS IE size required for the provided RAW
 *									configurations
 *
 * @mors		Morse chip struct
 * @config_list		List of RAW configurations
 * @num_configs		Number of RAW configurations in the list.
 *
 * Return size (>0) on success otherwise -EINVAL if a RAW configuration is invalid.
 */
static int morse_raw_calc_rps_ie_size(struct morse *mors,
				      const struct morse_raw_config *const *config_list,
				      u8 num_configs)
{
	u8 i;
	u16 size = 0;

	if (!num_configs) {
		MORSE_WARN_ON(FEATURE_ID_RAW, true);
		return 0;
	}

	BUG_ON(!config_list);

	for (i = 0; i < num_configs; i++) {
		/* Check for unsupported types. */
		switch (config_list[i]->type) {
		case IEEE80211_S1G_RPS_RAW_TYPE_SOUNDING:
		case IEEE80211_S1G_RPS_RAW_TYPE_SIMPLEX:
		case IEEE80211_S1G_RPS_RAW_TYPE_TRIGGERING:
			BUG_ON(true);
			break;
		case IEEE80211_S1G_RPS_RAW_TYPE_GENERIC:
			/* If the start time is 0 we can omit the start time field. */
			if (config_list[i]->start_time_us != 0)
				size += sizeof(struct morse_raw_start_time_t);

			/*
			 * While we could omit the RAW group configuration if the same as the last
			 * RAW we will include it for simplicity.
			 */
			size += sizeof(struct morse_raw_group_t);

			/* Channel indication not supported yet. */
			BUG_ON(config_list[i]->has_channel_indication);
			/* PRAW not supported yet. */
			BUG_ON(config_list[i]->is_periodic);
		}

		size += sizeof(struct ieee80211_s1g_rps);
	}

	return size;
}

u8 *morse_raw_get_rps_ie(struct morse *mors)
{
	return mors->custom_configs.raw.rps_ie;
}

/**
 * morse_raw_set_config() - Sets a single RAW configuration.
 *
 * @mors		Morse chip struct
 * @config		RAW configuration
 * @rps_ie_start	A pointer to the start of this configuration in the RPS IE
 *
 * Return offset of rps assignment in the rps_ie.
 */
static int morse_raw_set_config(struct morse *mors, struct morse_raw_config *config,
				u8 *rps_ie_start)
{
	/* Pages aren't used yet so always use zero. */
	static const u8 page;
	/* Channel activity is not currently implemented so use zero. */
	static const u8 channel_activity_bitmap;
	static const u8 channel2;
	struct morse_raw_start_time_t *start_time_ptr;
	struct morse_raw_group_t *raw_group_ptr;
	struct morse_raw_channel_t *raw_channel_ptr;
	struct morse_raw_periodic_t *raw_prawn_ptr;
	u8 *end_ptr;
	struct ieee80211_s1g_rps *rps_ptr = (struct ieee80211_s1g_rps *)rps_ie_start;
	struct morse_raw_station_data *sta_data = &mors->custom_configs.raw.sta_data;
	u16 num_stas;
	int current_beacon_start_aid_idx = INVALID_AID_IDX_VALUE;
	int current_beacon_end_aid_idx = INVALID_AID_IDX_VALUE;
	u16 current_beacon_start_aid;
	u16 current_beacon_end_aid;
	u32 i;

	u16 sta_per_beacon;
	u16 sta_per_beacon_mod;

	/* If beacon spreading is enabled and there are connected STAs find the subgroup of STAs for
	 * this beacon.
	 */
	if (config->nominal_sta_per_beacon &&
	    !(config->start_aid_idx < 0) && !(config->end_aid_idx < 0)) {
		/* Calculate how many STAs in each RAW. */
		num_stas = config->end_aid_idx - config->start_aid_idx + 1;

		/* Increase the number of stations per RAW to avoid spreading over
		 * too many beacons if necessary.
		 */
		if (config->max_beacon_spread &&
		    (num_stas / config->nominal_sta_per_beacon) > config->max_beacon_spread) {
			sta_per_beacon = num_stas / config->max_beacon_spread;
			sta_per_beacon_mod = num_stas % config->max_beacon_spread;
		} else {
			u16 beacon_count = num_stas / config->nominal_sta_per_beacon;

			if (num_stas % config->nominal_sta_per_beacon)
				beacon_count++;

			sta_per_beacon = num_stas / beacon_count;
			sta_per_beacon_mod = num_stas % beacon_count;
		}

		MORSE_RAW_DBG(mors, "sta_per_beacon, mod: %u, %u\n",
			      sta_per_beacon, sta_per_beacon_mod);

		/* Find where we should start the AID range for this beacon from. */
		MORSE_RAW_DBG(mors, "Last spread AID: %u\n", config->last_spread_aid);
		for (i = config->start_aid_idx;
		     (i <= config->end_aid_idx) && (i < sta_data->num_stations); i++) {
			if (sta_data->aids[i] > config->last_spread_aid) {
				current_beacon_start_aid_idx = i;
				break;
			}
		}

		/* If the last end AID was the last of the connected STAs then start the cycle from
		 * the beginning.
		 */
		if (current_beacon_start_aid_idx == INVALID_AID_IDX_VALUE)
			current_beacon_start_aid_idx = config->start_aid_idx;

		/* If we are in one of the earlier RAWs add an additional STA to deal with the
		 * modulus calculated earlier.
		 */
		if (((current_beacon_start_aid_idx - config->start_aid_idx) / sta_per_beacon) <
		    sta_per_beacon_mod)
			sta_per_beacon++;

		/* Find the end AID for this beacon. */
		for (i = current_beacon_start_aid_idx;
		     (i <= config->end_aid_idx) &&
		     (i < (current_beacon_start_aid_idx + sta_per_beacon)) &&
		     (i < sta_data->num_stations); i++) {
			current_beacon_end_aid_idx = i;
		}

		BUG_ON(current_beacon_end_aid_idx < current_beacon_start_aid_idx);

		current_beacon_start_aid = sta_data->aids[current_beacon_start_aid_idx];
		current_beacon_end_aid = sta_data->aids[current_beacon_end_aid_idx];
		config->last_spread_aid = sta_data->aids[current_beacon_end_aid_idx];
		MORSE_RAW_DBG(mors, "Start, End AID idx: %u, %u\n",
			      current_beacon_start_aid_idx, current_beacon_end_aid_idx);
		MORSE_RAW_DBG(mors, "Start, End AID: %u, %u\n",
			      current_beacon_start_aid, current_beacon_end_aid);

		/* If not using beacon spreading or no connected STAs use the full AID range. */
	} else {
		current_beacon_start_aid = config->start_aid;
		current_beacon_end_aid = config->end_aid;
		config->last_spread_aid = config->end_aid;
	}

	/* Create a basic configuration (Generic RAW) with all devices in a single RAW */
	rps_ptr->raw_control =
	    ((config->type << IEEE80211_S1G_RPS_RAW_CONTROL_TYPE_SHIFT) &
	     IEEE80211_S1G_RPS_RAW_CONTROL_TYPE);

	if (config->generic.cross_slot_boundary) {
		MORSE_RAW_DBG(mors, "Cross slot bleed allowed\n");
		rps_ptr->slot_definition |= cpu_to_le16(IEEE80211_S1G_RPS_RAW_SLOT_CROSS_BOUNDARY);
	}

	MORSE_RAW_DBG(mors, "Slot duration us, number of slots: %u, %u\n",
		      config->generic.slot_duration_us, config->generic.num_slots);
	morse_raw_get_slot_config(mors, config->type,
				  config->generic.num_slots, config->generic.slot_duration_us,
				  &rps_ptr->slot_definition);

	start_time_ptr = (struct morse_raw_start_time_t *)(rps_ptr + 1);

	if (config->start_time_us != 0) {
		rps_ptr->raw_control |= IEEE80211_S1G_RPS_RAW_CONTROL_START_IND;
		start_time_ptr->start_time_2tu = US_TO_TWO_TU(config->start_time_us);
		raw_group_ptr = (struct morse_raw_group_t *)(start_time_ptr + 1);
	} else {
		raw_group_ptr = (struct morse_raw_group_t *)start_time_ptr;
	}

	rps_ptr->raw_control |= IEEE80211_S1G_RPS_RAW_CONTROL_GROUP_IND;
	raw_group_ptr->raw_group12 = 0;
	raw_group_ptr->raw_group3 = 0;

	raw_group_ptr->raw_group12 |= cpu_to_le16(page & IEEE80211_S1G_RPS_RAW_GROUP_PAGE_IDX);
	raw_group_ptr->raw_group12 |= cpu_to_le16((current_beacon_start_aid <<
						   IEEE80211_S1G_RPS_RAW_GROUP_START_AID_SHIFT) &
						  IEEE80211_S1G_RPS_RAW_GROUP_START_AID);

	raw_group_ptr->raw_group12 |= cpu_to_le16((current_beacon_end_aid <<
						   IEEE80211_S1G_RPS_RAW_GROUP_END_AID_SHIFT) &
						  IEEE80211_S1G_RPS_RAW_GROUP_END_AID);

	raw_group_ptr->raw_group3 = current_beacon_end_aid >> (AID_END_BITS_SHIFT);
	raw_channel_ptr = (struct morse_raw_channel_t *)(raw_group_ptr + 1);

	if (config->has_channel_indication) {
		rps_ptr->raw_control |= IEEE80211_S1G_RPS_RAW_CONTROL_CHAN_IND;
		raw_channel_ptr->channel_activity_bitmap = channel_activity_bitmap;
		raw_channel_ptr->channel2 = channel2;
		raw_prawn_ptr = (struct morse_raw_periodic_t *)(raw_channel_ptr + 1);

	} else {
		raw_prawn_ptr = (struct morse_raw_periodic_t *)raw_channel_ptr;
	}

	if (config->is_periodic) {
		rps_ptr->raw_control |= IEEE80211_S1G_RPS_RAW_CONTROL_PERIODIC_IND;
		raw_prawn_ptr->periodicity = config->periodicity;
		raw_prawn_ptr->validity = config->validity;
		raw_prawn_ptr->start_offset = config->period_start_offset;
		end_ptr = (u8 *)(raw_prawn_ptr + 1);
	} else {
		end_ptr = (u8 *)raw_prawn_ptr;
	}

	return (end_ptr - rps_ie_start);
}

/**
 * morse_raw_set_configs() - Sets the RAW configurations which can contain a mixture of types.
 *
 * @mors:		Morse chip struct
 * @config_list		List of RAW configurations
 * @num_configs		Number of RAW configurations in the list.
 *
 * Return 0 on success otherwise -EINVAL if a RAW configuration is invalid.
 */
static int morse_raw_set_configs(struct morse *mors, struct morse_raw_config *const *config_list,
				 u8 num_configs)
{
	u8 i;
	u8 offset = 0;
	u8 old_rps_ie_len;
	struct morse_raw *raw = &mors->custom_configs.raw;

	/* Calculate the size so we can allocate memory */
	int size =
	    morse_raw_calc_rps_ie_size(mors, (const struct morse_raw_config * const *)config_list,
				       num_configs);

	MORSE_RAW_DBG(mors, "Number of RAWs: %u\n", num_configs);
	MORSE_RAW_DBG(mors, "RPS IE size: %d\n", size);

	BUG_ON((size <= 0) || (size > __UINT8_MAX__));

	mutex_lock(&raw->lock);

	/* Invalidate current raw until we are finished by setting to 0. */
	old_rps_ie_len = raw->rps_ie_len;
	raw->rps_ie_len = 0;

	if (raw->rps_ie) {
		/* Adjust size of allocated memory if necessary. */
		if (old_rps_ie_len != size) {
			kfree(raw->rps_ie);
			raw->rps_ie = kmalloc(size, GFP_KERNEL);
		}
	} else {
		/* Allocate memory for the RPS IE. */
		MORSE_RAW_DBG(mors, "Allocate RAW RPS IE\n");
		raw->rps_ie = kmalloc(size, GFP_KERNEL);
	}
	/* Check we got our allocated memory. */
	if (!raw->rps_ie) {
		mutex_unlock(&raw->lock);
		MORSE_RAW_DBG(mors, "Failed to allocate RAW RPS IE\n");
		return -ENOMEM;
	}

	/* Keep everything neat and zero the memory. */
	memset(raw->rps_ie, 0, size);

	/* Populate RPS IE using config settings. */
	for (i = 0; i < num_configs; i++) {
		offset += morse_raw_set_config(mors, config_list[i], &raw->rps_ie[offset]);
		BUG_ON(offset > size);
	}

	/* Validate RPS IE by giving its size. */
	BUG_ON(offset != size);
	raw->rps_ie_len = size;
	mutex_unlock(&raw->lock);

	return 0;
}

/**
 * morse_raw_debug_print_aid_idx() - Print the end AID indices and values for RAWs
 *
 * @mors:	Morse chip struct
 * @sta_data:	Current station data (contains AIDs)
 */
static void morse_raw_debug_print_aid_idx(struct morse *mors,
					  struct morse_raw_station_data *sta_data)
{
	struct morse_raw *raw = &mors->custom_configs.raw;
	struct morse_raw_config *config_ptr;
	int i;

	for (i = MAX_NUM_RAWS - 1; i >= 0; i--) {
		config_ptr = (struct morse_raw_config *)raw->configs[i];
		if (config_ptr && config_ptr->enabled) {
			MORSE_RAW_DBG(mors,
				      "Final Start/End AID indices (%d): %d, %d\n",
				      i, config_ptr->start_aid_idx, config_ptr->end_aid_idx);

			if (config_ptr->start_aid_idx >= 0 && config_ptr->end_aid_idx >= 0) {
				MORSE_RAW_DBG(mors,
					      "Final Start/End AID values (%d): %d, %d\n",
					      i,
					      sta_data->aids[config_ptr->start_aid_idx],
					      sta_data->aids[config_ptr->end_aid_idx]);
			}
		}
	}
}

/**
 * morse_raw_set_prio_raws() - Collects the enabled RAW configurations and compiles a list.
 *
 * @mors: Morse chip struct
 */
static void morse_raw_set_prio_raws(struct morse *mors)
{
	int i;
	struct morse_raw_config *configs_list[MAX_NUM_RAWS];
	struct morse_raw_config *config_ptr;
	u8 count = 0;
	struct morse_raw *raw = &mors->custom_configs.raw;
	struct morse_raw_station_data *sta_data = &raw->sta_data;

	/* RPS IE should only be regenerated if RAW is enabled. */
	if (!mors->custom_configs.raw.enabled) {
		MORSE_WARN_ON(FEATURE_ID_RAW, !mors->custom_configs.raw.enabled);
		goto cleanup;
	}

	memset(sta_data, 0, sizeof(*sta_data));

	/*
	 * Count how many RAWs exist and are enabled.
	 *
	 * Extract configs for enabled RAWs into a list. Start with highest
	 * priority.
	 */
	for (i = MAX_NUM_RAWS - 1; i >= 0; i--) {
		config_ptr = (struct morse_raw_config *)raw->configs[i];
		if (config_ptr && config_ptr->enabled) {
			configs_list[count++] = config_ptr;

			/* Reset indices */
			config_ptr->start_aid_idx = INVALID_AID_VALUE;
			config_ptr->end_aid_idx = INVALID_AID_VALUE;
		}
	}

	morse_raw_get_station_aid(mors, sta_data);
	MORSE_RAW_DBG(mors, "Number of stations: %u (%u)\n", sta_data->station_idx,
		      sta_data->num_stations);

	BUG_ON(sta_data->station_idx > 0 && !sta_data->aids);
	for (i = 0; i < sta_data->station_idx; i++)
		MORSE_RAW_DBG(mors, "Station AID: %u\n", sta_data->aids[i]);

	/* Sort AIDs, required for RAW group assignments. TODO optimise to avoid a large amount of
	 * computation every beacon (especially with large numbers of stations).
	 */
	sort(sta_data->aids, sta_data->station_idx, sizeof(*sta_data->aids),
	     &morse_raw_aid_compare, NULL);

	/* Find the start and end AIDs for each priority. */
	for (i = 0; i < sta_data->num_stations; i++) {
		u8 prio = MORSE_RAW_GET_PRIO(sta_data->aids[i]);

		/* Skip aids if there isn't a RAW config for this priority. */
		if (!raw->configs[prio])
			continue;

		/* Only set start AID index if it is the first for this priority. */
		if (raw->configs[prio]->start_aid_idx < 0)
			raw->configs[prio]->start_aid_idx = i;

		/* Always update the end AID to the last value we've seen. */
		raw->configs[prio]->end_aid_idx = i;
	}

	/* Print the AID indices and values if debug logging is enabled. */
	if (debug_mask & MORSE_MSG_DEBUG)
		morse_raw_debug_print_aid_idx(mors, sta_data);

	if (count == 0)
		goto cleanup;

	/* This cast looks strange but adds some protection. */
	morse_raw_set_configs(mors, (struct morse_raw_config * const *)configs_list, count);
	return;

cleanup:
	raw->rps_ie_len = 0;
	kfree(raw->rps_ie);
	raw->rps_ie = NULL;
}

void morse_raw_refresh_aids_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse, custom_configs.raw.refresh_aids_work);

	MORSE_RAW_DBG(mors, "Refresh RAW AIDs\n");
	morse_raw_set_prio_raws(mors);
}

/**
 * morse_raw_cmd_to_config() - Convert a RAW command into a RAW configuration
 *
 * @cmd		A pointer to a RAW command sent by morsectrl
 * @cfg		A pointer to the configuration to populate
 */
static void morse_raw_cmd_to_config(struct morse_cmd_raw *cmd, struct morse_raw_config *cfg)
{
	BUG_ON(!cmd || !cfg);

	/* Start with a clean slate. */
	memset(cfg, 0, sizeof(*cfg));

	/* Fill config data. */
	cfg->type = IEEE80211_S1G_RPS_RAW_TYPE_GENERIC;
	cfg->start_time_us = le32_to_cpu(cmd->start_time_us);
	if (cmd->prio == 0) {
		cfg->start_aid = MORSE_RAW_DEFAULT_START_AID;
		cfg->end_aid = (__UINT16_MAX__ & MORSE_RAW_AID_DEVICE_MASK);
	} else if ((cmd->prio > 0) && (cmd->prio < (MAX_NUM_RAWS_USER_PRIO - 1))) {
		cfg->start_aid = (cmd->prio << MORSE_RAW_AID_PRIO_SHIFT);
		cfg->end_aid = cfg->start_aid + (__UINT16_MAX__ & MORSE_RAW_AID_DEVICE_MASK);
	} else if (cmd->prio == (MAX_NUM_RAWS_USER_PRIO - 1)) {
		cfg->start_aid = (cmd->prio << MORSE_RAW_AID_PRIO_SHIFT);
		/* This is an existing limitation which can be removed with native s1g support. */
		cfg->end_aid = AID_LIMIT;
	} else {
		BUG();
	}

	BUG_ON(cfg->start_aid > cfg->end_aid);
	cfg->generic.cross_slot_boundary = !!cmd->cross_slot_boundary;
	cfg->generic.num_slots = cmd->num_slots;
	cfg->generic.slot_duration_us = le32_to_cpu(cmd->raw_duration_us) / cmd->num_slots;
	cfg->max_beacon_spread = le16_to_cpu(cmd->max_beacon_spread);
	cfg->nominal_sta_per_beacon = le16_to_cpu(cmd->nominal_sta_per_beacon);
	cfg->enabled = cmd->enable;
}

void morse_raw_process_cmd(struct morse *mors, struct morse_cmd_raw *cmd)
{
	struct morse_raw *raw = &mors->custom_configs.raw;
	struct morse_raw_config *config;
	u8 idx;

	if (cmd->enable_type == RAW_CMD_ENABLE_TYPE_GLOBAL) {
		MORSE_RAW_DBG(mors, "Morsectrl no update to RAW config: %s\n",
			      (cmd->enable) ? "enable" : "disable");

		if (cmd->enable)
			morse_raw_enable(mors);
		else
			morse_raw_disable(mors);

		return;
	}

	if (cmd->prio >= MAX_NUM_RAWS_USER_PRIO) {
		MORSE_RAW_WARN(mors, "RAW priority %u invalid (should be between 0 - %u)\n",
			       cmd->prio, (MAX_NUM_RAWS_USER_PRIO - 1));
		return;
	}

	/* Map user prio to idx (internal RAWs are assigned lower prio than user-specified ones */
	idx = cmd->prio + MAX_NUM_RAWS_INTERNAL;

	config = (struct morse_raw_config *)raw->configs[idx];

	if (cmd->config_type) {
		MORSE_RAW_DBG(mors, "Morsectrl update RAW config: %s %u %u %u %u %u %u %u\n",
			      (cmd->enable) ? "enable" : "disable",
			      cmd->prio,
			      le32_to_cpu(cmd->start_time_us),
			      le32_to_cpu(cmd->raw_duration_us),
			      cmd->num_slots,
			      cmd->cross_slot_boundary,
			      le16_to_cpu(cmd->max_beacon_spread),
			      le16_to_cpu(cmd->nominal_sta_per_beacon));

		mutex_lock(&raw->lock);
		if (!config) {
			raw->configs[idx] = kmalloc(sizeof(*config), GFP_KERNEL);
			config = (struct morse_raw_config *)raw->configs[idx];
		}

		morse_raw_cmd_to_config(cmd, config);
		mutex_unlock(&raw->lock);
	} else {
		if (!config) {
			if (cmd->enable)
				MORSE_RAW_WARN(mors,
					       "Trying to enable a RAW without configuration\n");
			return;
		}

		MORSE_RAW_DBG(mors, "Morsectrl enable/disable single RAW: %s %u\n",
			      (cmd->enable) ? "enable" : "disable", cmd->prio);

		mutex_lock(&raw->lock);
		config->enabled = cmd->enable;
		mutex_unlock(&raw->lock);
	}

	/* Update RPS IE with new configuration. */
	if (raw->enabled)
		schedule_work(&mors->custom_configs.raw.refresh_aids_work);
}

int morse_raw_enable(struct morse *mors)
{
	MORSE_RAW_INFO(mors, "Enabling RAW\n");
	schedule_work(&mors->custom_configs.raw.refresh_aids_work);
	mors->custom_configs.raw.enabled = true;
	return 0;
}

int morse_raw_disable(struct morse *mors)
{
	MORSE_RAW_INFO(mors, "Disabling RAW\n");
	mors->custom_configs.raw.enabled = false;
	return 0;
}

int morse_raw_init(struct morse *mors, bool enable)
{
	struct morse_raw *raw = &mors->custom_configs.raw;

	mutex_init(&raw->lock);
	INIT_WORK(&raw->refresh_aids_work, morse_raw_refresh_aids_work);

	if (enable)
		morse_raw_enable(mors);
	else
		morse_raw_disable(mors);

	return 0;
}

void morse_raw_finish(struct morse *mors)
{
	int i;
	struct morse_raw *raw = &mors->custom_configs.raw;

	morse_raw_disable(mors);
	cancel_work_sync(&raw->refresh_aids_work);

	/* Free RAW and clean up */
	raw->rps_ie_len = 0;
	kfree(raw->rps_ie);
	raw->rps_ie = NULL;

	for (i = 0; i < MAX_NUM_RAWS; i++) {
		kfree(raw->configs[i]);
		raw->configs[i] = NULL;
	}
}
