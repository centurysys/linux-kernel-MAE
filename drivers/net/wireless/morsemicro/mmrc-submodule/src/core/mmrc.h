/*
 * Copyright 2022 Morse Micro.
 *
 */

#ifndef _MMRC_H_
#define _MMRC_H_

#include "mmrc_osal.h"

/**
 * Define used to specify 802.11 specification to build MMRC for
 */
#define MMRC_MODE_80211AH 1
#define MMRC_MODE_80211AC 2
/**
 * Define the specification to build MMRC. MMRC_MODE_80211AH is used
 * for the purpose built use case of the MMRC Module. In contrary
 * MMRC_MODE_80211AC is used for testing purposes in ns-3 as 802.11ah
 * is not supported in this environment.
 */
#define MMRC_MODE MMRC_MODE_80211AH

/**
 * The max length of a retry chain for a single packet transmission
 */
#define MMRC_MAX_CHAIN_LENGTH 4

/**
 * Rate minimum allowed attempts
 */
#define MMRC_MIN_CHAIN_ATTEMPTS 1

/**
 * Rate upper limit for attempts
 */
#define MMRC_MAX_CHAIN_ATTEMPTS 1

/**
 * The max size of a probability table for a STA
 *
 * Max 8 MHz and 1 SS supported for now
 */
#define MMRC_MAX_TABLE_SIZE (MMRC_GUARD_MAX * (MMRC_BW_8MHZ + 1) * \
				(MMRC_SPATIAL_STREAM_1 + 1) * MMRC_MCS_MAX)

/**
 * The frequency of MMRC stat table updates
 */
#define MMRC_UPDATE_FREQUENCY_MS 100

/**
 * Used to specify supported features when initialising a STA
 */
#define MMRC_MASK(x) (1u << (x))


/**
 * Flags to be used with a mmrc_rate
 */
enum mmrc_flags {
	/** CTS/RTS flag */
	MMRC_FLAGS_CTS_RTS,
};


/**
 * Rates supported by the MMRC module
 */
enum mmrc_mcs_rate {
	MMRC_MCS_UNUSED = -1,
	MMRC_MCS0 = 0,
	MMRC_MCS1,
	MMRC_MCS2,
	MMRC_MCS3,
	MMRC_MCS4,
	MMRC_MCS5,
	MMRC_MCS6,
	MMRC_MCS7,
	MMRC_MCS8,
	MMRC_MCS9,
#if MMRC_MODE == MMRC_MODE_80211AH
	MMRC_MCS10,
#endif
	MMRC_MCS_MAX,
};


/**
 * Bandwidths supported by the MMRC module
 */
enum mmrc_bw {
#if MMRC_MODE == MMRC_MODE_80211AH
	MMRC_BW_1MHZ,
#endif
	MMRC_BW_2MHZ,
	MMRC_BW_4MHZ,
	MMRC_BW_8MHZ,
	MMRC_BW_16MHZ,
	MMRC_BW_MAX,
};


/**
 * Spatial streams supported by the MMRC module
 */
enum mmrc_spatial_stream {
	MMRC_SPATIAL_STREAM_1,
	MMRC_SPATIAL_STREAM_2,
	MMRC_SPATIAL_STREAM_3,
	MMRC_SPATIAL_STREAM_4,
	MMRC_SPATIAL_STREAM_MAX,
};


/**
 * Guards supported by the MMRC module
 */
enum mmrc_guard {
	MMRC_GUARD_LONG,
	MMRC_GUARD_SHORT,
	MMRC_GUARD_MAX,
};


/**
 * A single rate chain
 */
struct mmrc_rate {
	/** The MCS for this entry in the rate table */
	enum mmrc_mcs_rate rate;

	/** The number of attempts at this rate */
	uint32_t attempts;

	/** The Guard to be used for this rate */
	enum mmrc_guard guard;

	/** The spatial streams to be used for this rate */
	enum mmrc_spatial_stream ss;

	/** The bandwidth for this rate */
	enum mmrc_bw bw;

	/** The flags for this rate */
	uint16_t flags;

	/** The index in the mmrc_table */
	uint16_t index;
};


/**
 * Rate table generated on a per packet basis
 */
struct mmrc_rate_table {
	/** The rates to attempt within this chain */
	struct mmrc_rate rates[MMRC_MAX_CHAIN_LENGTH];
};


/**
 * Capabilities of an individual STA
 */
struct mmrc_sta_capabilities {
	/** The maximum number of output rates */
	uint8_t max_rates;

	/** The maximum retries */
	uint8_t max_retries;

	/** The supported bandwidths of the STA */
	uint16_t bandwidth;

	/** The supported spatial streams of the STA */
	uint16_t spatial_streams;

	/** The supported rates of the STA */
	uint16_t rates;

	/** The supported guards of the STA */
	uint8_t guard;
	uint8_t guard_per_bw[MMRC_BW_MAX];

	/** Flag of relevant features supported by the STA. e.g. dynamic SMPS... */
	uint16_t sta_flags;
};


/**
 * Statistics table of a STA
 */
struct mmrc_stats_table {
	/**
	 * The probability this rate will successfully transmit.
	 * This is updated based on the configured EWMA value and the
	 * period set in the configured timer
	 */
	uint32_t prob;

	/** The PHY theoretical calculated throughput of this rate */
	uint32_t throughput;

	/** The average calculated running throughput counter of this rate */
	uint32_t avg_throughput_counter;

	/** The average running throughput sum of this rate */
	uint32_t sum_throughput;

	/** The maximum observed calculated throughput of this rate */
	uint32_t max_throughput;

	/** The number of attempts at sending a packet at this rate since the last update */
	uint32_t sent;

	/** The number of successfully sent packets at this rate since the last	update */
	uint32_t sent_success;

	/** The total attempts of packets sent at this rate */
	uint32_t total_sent;

	/** The total successful attempts at sending at this rate */
	uint32_t total_success;

	/**
	 * A store of evidence as to how relevant a rate is based on how many
	 * times it has been attempted recently
	 */
	uint32_t evidence;
};


/**
 * Store of information the MMRC module requires for a STA
 */
struct mmrc_table {
	/**
	 * In case MMRC does not have up-to-date rate table, it should fall
	 * back to a set of pre-determind base rates to rapidly acquire that
	 * information
	 */
	bool is_initialised;

	/** The capabilities of the STA */
	struct mmrc_sta_capabilities caps;

	/** The index of the rate with the best tp */
	struct mmrc_rate best_tp;

	/** The index of the rate with the second best tp */
	struct mmrc_rate second_tp;

	/** The index of the rate with the best tp */
	struct mmrc_rate baseline;

	/** The index of the rate with the best probability */
	struct mmrc_rate best_prob;

	/** The index of the fixed rate */
	struct mmrc_rate fixed_rate;

	/** Used to decide when to do a lookaround */
	uint8_t lookaround_cnt;

	/** The ratio of using normal rate and sampling */
	uint8_t lookaround_wrap;

	/** Counter for analysis purposes */
	uint32_t total_lookaround;

	/**
	 * A counter that is used to determine when we should force a lookaround.
	 * Should be a portion of the above lookaround with less constraints
	 */
	uint32_t forced_lookaround;

	/**
	 * A counter to detect if the current best rate is optimal
	 * and may slow down sample frequency.
	 */
	uint32_t stability_cnt;

	/** Threshold for sample frequency switch. */
	uint32_t stability_cnt_threshold;

	/**
	 * The probability table for the STA. This MUST always be the last
	 * element in the struct.
	 *
	 * @note The table may not always be of length @ref MMRC_MAX_TABLE_SIZE as the
	 * @c mmrc_table may be allocated using @ref mmrc_memory_required_for_caps
	 */
	struct mmrc_stats_table table[MMRC_MAX_TABLE_SIZE];
};


/**
 * Initialise the mmrc table, based on the capabilities provided
 *
 * @param tb A pointer to an empty mmrc_sta_capabilities struct.
 * @param caps The capabilities of this STA.
 *
 * @note If the STA capabilities change this function will need to be called again
 * and the @c mmrc_table may need to be reallocated if allocated using
 * @ref mmrc_memory_required_for_caps
 */
void mmrc_sta_init(struct mmrc_table *tb, struct mmrc_sta_capabilities *caps);


/**
 * Calculate the size of the mmrc_table required for these capabilities.
 *
 * @param caps The capabilities of this STA.
 *
 * @returns size_t the size of an mmrc_table for this STA
 */
size_t mmrc_memory_required_for_caps(struct mmrc_sta_capabilities *caps);


/**
 * Get a retry chain from MMRC for a specific mmrc_table.
 *
 * @param tb A pointer to a mmrc table to generate rates from.
 * @param out A points to an empty rate table to be populated.
 * @param size The size of the packet to be sent
 */
void mmrc_get_rates(struct mmrc_table *tb,
		    struct mmrc_rate_table *out,
		    size_t size);


/**
 * Feedback to MMRC so the appropriate stats table can be updated.
 *
 * @param tb A pointer to a mmrc table to update
 * @param rates The rate table used to send the last packet
 * @param retry_count The amount of retries attempted using the last
 *	rate table
 */
void mmrc_feedback(struct mmrc_table *tb,
		   struct mmrc_rate_table *rates,
		   int32_t retry_count);



/**
 * Feedback to MMRC based on aggregated frames.
 *
 * @param tb Pointer to a mmrc table to update
 * @param rates The rate table used to send the last packet
 * @param retry_count The amount of retries attempted using the last
 *      rate table
 * @param success The amount of successfully sent frames in the A-MPDU
 * @param failure The amount of unsuccessfully sent frames in the A-MPDU
 */
void mmrc_feedback_agg(struct mmrc_table *tb,
		       struct mmrc_rate_table *rates,
		       int32_t retry_count,
		       uint32_t success,
		       uint32_t failure);


/**
 * Update an MMRC table from the most recent stats.
 * delta_time_ms is currently ignored.
 *
 * @param tb A pointer to a mmrc table to update.
 */
void mmrc_update(struct mmrc_table *tb);

/**
 * Set a fixed rate.
 *
 * @param tb A pointer to a mmrc table to update.
 * @param fixed_rate The fixed rate to be set
 *
 * @returns bool true if setting rate succeeds, otherwise false
 */
bool mmrc_set_fixed_rate(struct mmrc_table *tb, struct mmrc_rate fixed_rate);

/**
 * Calculate the amount of rows occupied by a stations capabilities
 *
 * @param caps A pointer to the desired station capabilities
 * @returns uint16_t the total number of rows to accommodate all
 *                   capabilities options
 */
uint16_t rows_from_sta_caps(struct mmrc_sta_capabilities *caps);


/**
 * Calculate the transmit time of a given rate in the mmrc_table based on a
 * default packet size in microseconds
 *
 * @param rate  The rate to calculate the tx time for
 * @returns uint32_t The tx time of the given rate
 */
uint32_t get_tx_time(struct mmrc_rate *rate);


/**
 * Validates that the combinations if a given rate is valid
 *
 * @param rate The rate to validate
 * @returns bool If the rate is valid
 */
bool validate_rate(struct mmrc_rate *rate);


/**
 * Calculate and update the index of the input rate
 *
 * @param rate The rate with all parameters except index to update
 */
void rate_update_index(struct mmrc_table *tb, struct mmrc_rate *rate);


/**
 * Takes an index in an mmrc_table and calculate the capabilities
 * of that rate
 *
 * @param tb A pointer to a mmrc table to update
 * @param index A valid index in the mmrc_stats_table (to find the
 *              upper index limit use rows_from_sta_caps())
 * @returns struct mmrc_rate with the updated rate parameters
 */
struct mmrc_rate get_rate_row(struct mmrc_table *tb, uint16_t index);


/**
 * Set a fixed rate.
 *
 * @param tb A pointer to a mmrc table to update
 * @param fixed_rate The fixed rate to be set
 *
 * @param tb A pointer to a mmrc table to update
 * @param rate The rate to be updated with the currect index
 */
void rate_update_index(struct mmrc_table *tb, struct mmrc_rate *rate);

#endif /* _MMRC_H_ */
