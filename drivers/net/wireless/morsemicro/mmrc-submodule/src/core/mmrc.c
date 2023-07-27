/*
 * Copyright 2022 Morse Micro.
 *
 */

#include "mmrc.h"
#include "mmrc_osal.h"


/**
 * The default packet size in bits used for calculated throughput of a given rate
 */
#define DEFAULT_PACKET_SIZE_BITS 9600

/**
 * The default packet size in bytes used for calculating retries for a given rate
 */
#define DEFAULT_PACKET_SIZE_BYTES 1200

/* The sample frequencies at different stages */
#define LOOKAROUND_RATE_INIT		5
#define LOOKAROUND_RATE_NORMAL		50
#define LOOKAROUND_RATE_STABLE		100

/* The thresholds for stability stages */
#define STABILITY_CNT_THRESHOLD_INIT	20
#define STABILITY_CNT_THRESHOLD_NORMAL	50
#define STABILITY_CNT_THRESHOLD_STABLE	100

/* The backoff step size for the counter */
#define STABILITY_BACKOFF_STEP		2

/**
 * The packet success threshold for attempting slower lookaround rates
 */
#define LOOKAROUND_THRESHOLD 85

/**
 * The packet success threshold for attempting forced lookaround rates
 * Lower values mean more forced_lookaround and so lower average throughput
 * due to extra time spent on not ideal rate
 */
#define FORCED_LOOKAROUND_THRESHOLD 96

/**
 * Limit the number of times we try to pick a theoretically better rate to sample.
 * Necessary so we don't stall the CPU, due to constantly picking worse rates.
 */
#define TP_FAIL_MAX 10

/**
 * Initial and reset probability per rate in the table
 * Changing this value will have a severe implication on the current heuristic
 * It could mean that some rates will have better probability throughput even
 * with no edivence and so will cause unexpected changes in the rate table
 */
#define RATE_INIT_PROBABILITY 0

/**
 * The time cap on rate allocation for multiple attempts. If a single attempt
 * exceeds this window, no additional attempts will be generated
 */
#define MAX_WINDOW_ATTEMPT_TIME 4000

/**
 * The time window for all rates in rate table
 */
#define RATE_WINDOW_MICROSECONDS 24000

/**
 * The overhead value of a single transmission.
 * This value is currently arbitrary
 */
#define OVERHEAD_MICROSECONDS 100

/**
 * The Congestion window time
 * This value is currently arbitrary
 */
#define CONGESTION_WINDOW_MICROSECONDS 32

/**
 * EWMA is the alpha coefficient in the exponential weighting moving average
 * filter used for probability updates.
 *
 * Y[n] = X[n] * (100 - EWMA) + (Y[n-1] * EWMA)
 *	  -------------------------------------
 *			   100
 *
 */
#define EWMA 75

/**
 * Evidence scaling to allow for one decimal place. Needed for low
 * throughput, otherwise the history decays in a single cycle.
 */
#define EVIDENCE_SCALE 5

/**
 * Evidence maximum to ensure history doesn't decay too slowly when
 * there is a lot of historical data.
 */
#define EVIDENCE_MAX 100

/**
 * This fixed point conversion multiplies a value by one and shifts it
 * accordingly to account for the fixed point shifting at the return of a
 * function
 */
#define FP_8_MULT_1 256

/**
 * Fixed point conversion for 2.1 * 2^8 used for 4MHz symbol multiplication
 */
#define FP_8_4MHZ 537

/**
 * Fixed point conversion for 4.5 * 2^8 used for 8MHz symbol multiplication
 */
#define FP_8_8MHZ 1152

/**
 * Fixed point conversion for 9.0 * 2^8 used for 16MHz symbol multiplication
 */
#define FP_8_16MHz 2301

/**
 * Fixed point conversion for 3.6 * 2^8 used for long guard symbol tx time
 * multiplication
 */
#define FP_8_LONG_GUARD_SYMBOL_TIME 1024

/**
 * Fixed point conversion for 4.0 * 2^8 used for short guard symbol tx time
 * multiplication
 */
#define FP_8_SHORT_GUARD_SYMBOL_TIME 921

/**
 * Shift value to shift back our FP conversions
 */
#define FP_8_SHIFT 8

/**
 * Checks if the desired gap in percentage is in range
 */
#define MAX_ALLOWED_GAP(ref, new, perc) ((ref - new) > (ref * perc / 100))

#if MMRC_MODE == MMRC_MODE_80211AH
#define MMRC_MAX_BW(bw_caps) \
		(((bw_caps) & MMRC_MASK(MMRC_BW_16MHZ)) ? MMRC_BW_16MHZ : \
		((bw_caps) & MMRC_MASK(MMRC_BW_8MHZ)) ? MMRC_BW_8MHZ : \
		((bw_caps) & MMRC_MASK(MMRC_BW_4MHZ)) ? MMRC_BW_4MHZ : \
		((bw_caps) & MMRC_MASK(MMRC_BW_2MHZ)) ? MMRC_BW_2MHZ : \
		MMRC_BW_1MHZ)
#else
#define MMRC_MAX_BW(bw_caps) \
		(((bw_caps) & MMRC_MASK(MMRC_BW_16MHZ)) ? MMRC_BW_16MHZ : \
		((bw_caps) & MMRC_MASK(MMRC_BW_8MHZ)) ? MMRC_BW_8MHZ : \
		((bw_caps) & MMRC_MASK(MMRC_BW_4MHZ)) ? MMRC_BW_4MHZ : \
		MMRC_BW_2MHZ)
#endif

/**
 * This table stores the number of bits per symbols used for MCS0-MCS9 based
 * on 20MHz and 1SS
 */
static const uint32_t sym_table[10] = {
	24, 36, 48, 72, 96, 144, 192, 216, 256, 288
};


/**
 * Calculate which bit is the nth bit set in an integer based flag.
 */
static uint8_t nth_bit(uint16_t in, uint16_t index)
{
	uint32_t i;
	uint8_t count = 0;

	for (i = 0; count != index + 1; i++) {
		if (((1u << i) & in) != 0)
			count++;
	}
	return i-1;
}

/**
 * Calculate the input bit's index among all the set bits in an integer
 * based flag.
 */
static uint8_t bit_index(uint16_t in, uint32_t bit_pos)
{
	uint8_t i;
	uint8_t index = 0;

	for (i = 0; i != bit_pos + 1; i++) {
		if (((1u << i) & in) != 0)
			index++;
	}
	if (index == 0) {
		/* Could not match bit pos to caps */
		return 0;
	}
	return index - 1;
}

void rate_update_index(struct mmrc_table *tb, struct mmrc_rate *rate)
{
	uint16_t index = 0;
	/* Information about our rates */
	uint8_t bw = BIT_COUNT(tb->caps.bandwidth);
	uint8_t streams = BIT_COUNT(tb->caps.spatial_streams);
	uint8_t guard = BIT_COUNT(tb->caps.guard);

	index = bit_index(tb->caps.guard, rate->guard) +
		bit_index(tb->caps.bandwidth, rate->bw) * guard +
		bit_index(tb->caps.spatial_streams, rate->ss) * guard * bw +
		bit_index(tb->caps.rates, rate->rate) * bw * streams * guard;

	rate->index = index;
}

struct mmrc_rate get_rate_row(struct mmrc_table *tb, uint16_t index)
{
	struct mmrc_rate rate;
	uint8_t ss_index;

	/* Information about our rates */
	uint8_t mcs = BIT_COUNT(tb->caps.rates);
	uint8_t bw = BIT_COUNT(tb->caps.bandwidth);
	uint8_t streams = BIT_COUNT(tb->caps.spatial_streams);
	uint8_t guard = BIT_COUNT(tb->caps.guard);
	uint16_t total_caps = mcs*bw*streams*guard;

	/* Find our MCS */
	uint8_t rows = total_caps / mcs;
	uint8_t mcs_index = index / rows;
	uint8_t mcs_modulo = index % rows;

	mcs = nth_bit(tb->caps.rates, mcs_index);

	/* Find our spatial stream */
	rows = rows / streams;
	streams = nth_bit(tb->caps.spatial_streams,
			  mcs_modulo / rows);

	/* Find our bandwidth */
	ss_index = index % rows;
	rows = rows / bw;
	bw = nth_bit(tb->caps.bandwidth,
		     ss_index / rows);

	/* Find our guard */
	guard = nth_bit(tb->caps.guard, index % guard);

	/**
	 * TODO: (MOR-32) We need to set any flags. To confirm with Morse,
	 * e.g. RTS/CTS - Dynamic SMPS & Streams > 1
	 */
	rate.flags = 0;

	/* Create our rate row and send it */
	rate.bw = (enum mmrc_bw)bw;
	rate.ss = (enum mmrc_spatial_stream)streams;
	rate.rate = (enum mmrc_mcs_rate)mcs;

	if (guard > MMRC_GUARD_MAX)
		guard = MMRC_GUARD_MAX;

	if (tb->caps.guard_per_bw[bw] & MMRC_MASK(guard)) {
		rate.guard = (enum mmrc_guard)guard;
		rate.index = index;
	} else {
		rate.guard = MMRC_GUARD_LONG;
		rate_update_index(tb, &rate);
	}

	return rate;
}


uint16_t rows_from_sta_caps(struct mmrc_sta_capabilities *caps)
{
	return BIT_COUNT(caps->bandwidth) *
	       BIT_COUNT(caps->rates) *
	       BIT_COUNT(caps->guard) *
	       BIT_COUNT(caps->spatial_streams);
}

size_t mmrc_memory_required_for_caps(struct mmrc_sta_capabilities *caps)
{
	return sizeof(struct mmrc_table) -
		sizeof(((struct mmrc_table *)0)->table) +
		rows_from_sta_caps(caps) * sizeof(struct mmrc_stats_table);
}

/**
 * Calculates the numbers of bits per symbol for a given rate
 *
 * @param rate The rate we want to calculate bits per symbol for
 * @return int32_t The number of bits per symbol for this rate
 */
static uint32_t calculate_bits_per_symbol(struct mmrc_rate *rate)
{
	uint32_t bps;
/* If MCS10 is selected we return 2*MCS0 Symbols */
#if MMRC_MODE == MMRC_MODE_80211AH
	if (rate->rate == MMRC_MCS10)
		return 6;
#endif

	/* Confirm that the rate is valid for the sym_table lookup */
	if (rate->rate >= MMRC_MCS_MAX) {
		/* This is a very unlikely scenario but this will just result in slow TPUT */
		return 1;
	}

	/**
	 * Coversion from 20MHz as in sym_table to:
	 * 40MHz   ==	x 2.1
	 * 80MHz   ==	x 4.5
	 * 160MHz  ==	x 9.0
	 */
	bps = sym_table[rate->rate];
	switch (rate->bw) {
	case (MMRC_BW_4MHZ):
		bps *= FP_8_4MHZ;
		break;
	case (MMRC_BW_8MHZ):
		bps *= FP_8_8MHZ;
		break;
	case (MMRC_BW_16MHZ):
		bps *= FP_8_16MHz;
		break;
#if MMRC_MODE == MMRC_MODE_80211AH
	case(MMRC_BW_1MHZ):
		bps = sym_table[rate->rate] * 24 / 52;
		bps *= FP_8_MULT_1;
		break;
#endif
	case(MMRC_BW_2MHZ):
	case(MMRC_BW_MAX):
	default:
		bps *= FP_8_MULT_1;
		break;
	}
	/* SS + 1 because mmrc_spatial_stream starts at 0 */
	return ((rate->ss + 1) * bps) >> FP_8_SHIFT;
}

uint32_t get_tx_time(struct mmrc_rate *rate)
{
	uint32_t tx = 0;
	uint32_t n_sym;
	uint32_t avg_bits;

	/* Calculate tx time based on a default packet size */
	avg_bits = DEFAULT_PACKET_SIZE_BITS;

	/* Number of bits per symbol for this rate */
	n_sym = calculate_bits_per_symbol(rate);

	/* In case of bad calcuation/parameter use lowest value */
	n_sym = n_sym == 0 ? sym_table[0] : n_sym;

	/* number of symbols in default packet size */
	n_sym = avg_bits / n_sym;

	/* tx is time to transmit average packet in us */
	switch (rate->guard) {
	case (MMRC_GUARD_LONG):
		tx = n_sym * FP_8_LONG_GUARD_SYMBOL_TIME;
		break;
	case (MMRC_GUARD_SHORT):
		tx = n_sym * FP_8_SHORT_GUARD_SYMBOL_TIME;
		break;
	default:
		return 0;
	}

	/* AH rate is 10x slower than AC */
#if MMRC_MODE == MMRC_MODE_80211AH
	tx = (tx * 10);
#endif
	return tx >> FP_8_SHIFT;
}

/**
 * Calculate the theoretical thoughput of a given rate
 *
 * @param rate The rate with updated index, bandwidth, spatial-streams and guard
 * @returns uint32_t The standard data rate in Kbps
 */
static uint32_t calculate_theoretical_throughput(struct mmrc_rate rate)
{
	static const uint32_t s1g_tpt_lgi[4][11] = {
		{300, 600, 900, 1200, 1800, 2400, 2700, 3000, 3600, 4000, 150},
		{650, 1300, 1950, 2600, 3900, 5200, 5850, 6500, 7800, 0, 0},
		{1350, 2700, 4050, 5400, 8100, 10800, 12150, 13500, 16200, 18000, 0},
		{2925, 5850, 8775, 11700, 17550, 23400, 26325, 29250, 35100, 39000, 0},
	};

	static const uint32_t s1g_tpt_sgi[4][11] = {
		{333, 666, 1000, 1333, 2000, 2666, 3000, 3333, 4000, 4444, 166},
		{722, 1444, 2166, 2888, 4333, 5777, 6500, 7222, 8666, 0, 0},
		{1500, 3000, 4500, 6000, 9000, 12000, 13500, 15000, 18000, 20000, 0},
		{3250, 6500, 9750, 13000, 19500, 26000, 29250, 32500, 39000, 43333, 0},
	};

	if (rate.guard)
		return s1g_tpt_sgi[rate.bw][rate.rate] * 1000;

	return s1g_tpt_lgi[rate.bw][rate.rate] * 1000;
}

/**
 * Calculate the thoughput of a rate designated by its index in the mmrc_table
 *
 * @param tb The mmrc table
 * @param index The index in the table of the rate to calculate throughput for
 * @return uint32_t The expected throughput for the given rate
 */
static uint32_t calculate_throughput(struct mmrc_table *tb, uint16_t index)
{
	struct mmrc_rate rate = get_rate_row(tb, index);

	if (tb->table[rate.index].prob < 10)
		return 0;
	else
		return calculate_theoretical_throughput(rate) *	tb->table[rate.index].prob / 100;
}

bool validate_rate(struct mmrc_rate *rate)
{
#if MMRC_MODE == MMRC_MODE_80211AH
	if (rate->rate == MMRC_MCS10 && rate->bw != MMRC_BW_1MHZ) {
		/* 802.11ah does not support MCS10 with BW that is not 1MHz */
		return false;
	}
	if (rate->rate == MMRC_MCS9 &&
	    rate->bw == MMRC_BW_16MHZ &&
	    rate->ss == MMRC_SPATIAL_STREAM_3) {
		/* 802.11ah does not support MCS9 at 16MHz for 3 spatial streams */
		return false;
	}
#endif
	if (rate->rate == MMRC_MCS9 &&
	    rate->bw == MMRC_BW_2MHZ &&
	    rate->ss != MMRC_SPATIAL_STREAM_3) {
		/* 802.11ah does not support MCS9 at 2MHz for 1 or 2 spatial streams */
		return false;
	}
	if (rate->rate == MMRC_MCS6 &&
	    rate->bw == MMRC_BW_8MHZ &&
	    rate->ss == MMRC_SPATIAL_STREAM_3) {
		/* 802.11ah does not support MCS6 at 8MHz for 3 spatial streams */
		return false;
	}
	return true;
}

static uint16_t find_baseline_index(struct mmrc_table *tb)
{
	uint32_t i, theoretical_tp, min_theoretical_tp;
	uint16_t row_count = rows_from_sta_caps(&tb->caps);
	uint16_t min_theoretical_tp_index = 0;
	struct mmrc_rate rate;

#if MMRC_MODE == MMRC_MODE_80211AH
	if (tb->caps.rates & MMRC_MASK(MMRC_MCS10))
		return 0;
#endif

	min_theoretical_tp = calculate_theoretical_throughput(get_rate_row(tb, 0));
	for (i = 0; i < row_count; i++)	{
		rate = get_rate_row(tb, i);
		if (!validate_rate(&rate))
			continue;

		theoretical_tp = calculate_theoretical_throughput(rate);
		if (min_theoretical_tp > theoretical_tp) {
			min_theoretical_tp = theoretical_tp;
			min_theoretical_tp_index = rate.index;
		}
	}

	return min_theoretical_tp_index;
}

/**
 * Retrieves the index of the rate in the mmrc_table_t with the best
 * likelihood of succeeding.
 */
static struct mmrc_rate get_best_prob(struct mmrc_table *tb)
{
	uint32_t i;
	uint16_t best_row = 0;
	struct mmrc_rate rate;
	struct mmrc_rate tmp;
	uint32_t best_prob = 0;
	uint32_t theoretical_tp, best_prob_throughput = 0;

	for (i = 0; i < rows_from_sta_caps(&tb->caps); i++) {
		tmp = get_rate_row(tb, i);
		if (!validate_rate(&tmp))
			continue;

		/**
		 *  Consider the rate only if its probability value is greater,
		 *  then check its throughput.
		 */
		if (tb->table[tmp.index].prob > best_prob) {
			theoretical_tp = calculate_theoretical_throughput(tmp);
			if (theoretical_tp > best_prob_throughput) {
				best_prob_throughput = theoretical_tp;
				best_prob = tb->table[tmp.index].prob;
				best_row = tmp.index;
			}
		}
	}

	rate = get_rate_row(tb, best_row);
	return rate;
}

/**
 * Updates the mmrc_table with the appropriate rate priority based on the
 * latest update statistics
 */
static void generate_table_priority(struct mmrc_table *tb, uint32_t new_stats)
{
	uint16_t i;
	uint16_t best_row = tb->best_tp.index;
	uint16_t best_row_prev = best_row;
	uint16_t second_best_row = tb->second_tp.index;
	uint32_t best_tp = tb->table[best_row].throughput;
	uint32_t second_best_tp = tb->table[second_best_row].throughput;
	struct mmrc_rate tmp;

	/**
	 * Alway fall back to basic rate when state changed to uninitialised
	 * due to sharp drop in best rate probability or no feedback for long period of time.
	 */
	if (!tb->is_initialised) {
		tb->best_tp.bw = MMRC_MAX_BW(tb->caps.bandwidth);
		if (tb->caps.guard_per_bw[tb->best_tp.bw] & MMRC_MASK(MMRC_GUARD_SHORT))
			tb->best_tp.guard = MMRC_GUARD_SHORT;
		else
			tb->best_tp.guard = MMRC_GUARD_LONG;
		tb->best_tp.rate = MMRC_MCS0;
#if MMRC_MODE == MMRC_MODE_80211AH
		/**
		 * To compensate for slow feedback when running with 1 and 2 MHz
		 * bandwidth, we start from MCS3 which will correspond to a
		 * reasonable feedbacks and will avoid resetting the rate table evidence.
		 */
		if (tb->best_tp.bw == MMRC_BW_1MHZ || tb->best_tp.bw == MMRC_BW_2MHZ)
			tb->best_tp.rate = MMRC_MCS3;
#endif
		tb->best_tp.ss = MMRC_SPATIAL_STREAM_1;
		rate_update_index(tb, &tb->best_tp);
		tb->second_tp.rate = MMRC_MCS_UNUSED;
		tb->best_prob.rate = MMRC_MCS_UNUSED;
		tb->baseline.rate = MMRC_MCS_UNUSED;
		return;
	}

	/* Use fixed rate if set */
	if (tb->fixed_rate.rate != MMRC_MCS_UNUSED) {
		tb->best_tp = tb->fixed_rate;
		tb->second_tp = tb->fixed_rate;
		tb->best_prob = tb->fixed_rate;
		return;
	}

	for (i = 0; i < rows_from_sta_caps(&tb->caps); i++) {
		tmp = get_rate_row(tb, i);
		if (!validate_rate(&tmp))
			continue;

		if (tb->table[tmp.index].throughput > best_tp) {
			second_best_row = best_row;
			second_best_tp = best_tp;

			best_tp = tb->table[tmp.index].throughput;
			best_row = tmp.index;
		} else if (tb->table[tmp.index].throughput > second_best_tp && best_row != tmp.index) {
			second_best_tp = tb->table[tmp.index].throughput;
			second_best_row = tmp.index;
		}
	}

	tb->best_tp = get_rate_row(tb, best_row);
	if (tb->best_tp.rate != MMRC_MCS0) {
		tb->second_tp = tb->best_tp;
		tb->second_tp.rate--;
		rate_update_index(tb, &tb->second_tp);
	} else {
		tb->second_tp = get_rate_row(tb, second_best_row);
	}

	if (tb->second_tp.rate != MMRC_MCS0) {
		tb->best_prob = tb->second_tp;
		tb->best_prob.rate--;
	} else {
		tb->best_prob = get_best_prob(tb);
	}

	if (tb->best_prob.rate != MMRC_MCS0) {
		tb->baseline = tb->best_prob;
		tb->baseline.rate--;
	}

	/* Only update stability when there is traffic */
	if (!new_stats)
		return;

	if (tb->best_tp.rate > MMRC_MCS1 && best_row_prev == best_row) {
		/* Increase the counter when the best rate is not changed */
		tb->stability_cnt++;
	} else if (tb->stability_cnt > STABILITY_BACKOFF_STEP) {
		/* Back off the counter when there is a new best rate */
		tb->stability_cnt -= STABILITY_BACKOFF_STEP;
	} else {
		tb->stability_cnt = 0;
	}
}

/**
 * Calculates the appropriate amount of additional attempts to make based on
 * packet size and theoretical throughput.
 */
static void calculate_remaining_attempts(struct mmrc_table *tb,
					 struct mmrc_rate_table *rate,
					 int32_t *rem_time,
					 size_t size)
{
	size_t i;
	uint32_t max_attempts = tb->caps.max_retries;

	if (*rem_time < 0)
		return;

	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		uint32_t attempt;
		uint32_t time;

		if (rate->rates[i].rate == MMRC_MCS_UNUSED)
			break;

		time = get_tx_time(&rate->rates[i]);

		/* The attempts for these rates were calculated in the initial attempt allocation */
		if (tb->table[rate->rates[i].index].prob < 20)
			continue;

		if (i == 0 && (tb->table[rate->rates[i].index].throughput <
			tb->table[tb->best_prob.index].throughput))
			continue;

		if (size > DEFAULT_PACKET_SIZE_BYTES)
			attempt = (time * ((size * 1000) / DEFAULT_PACKET_SIZE_BYTES)) / 1000;
		else
			attempt = (time * 1000) / ((DEFAULT_PACKET_SIZE_BYTES * 1000) / size);

		if (!attempt)
			attempt = 1;

		attempt = (*rem_time / tb->caps.max_rates) / attempt;
		rate->rates[i].attempts = attempt > max_attempts ? max_attempts : attempt;
	}
}

/**
 * Allocate initial attempts to all rates in a rate table
 */
static void allocate_initial_attempts(struct mmrc_table *tb,
				      struct mmrc_rate_table *rate,
				      int32_t *rem_time,
				      size_t size)
{
	uint32_t i;

	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		uint32_t attempt;
		uint32_t time;

		if (rate->rates[i].rate == MMRC_MCS_UNUSED)
			break;

		time = get_tx_time(&rate->rates[i]);

		if (size > DEFAULT_PACKET_SIZE_BYTES)
			attempt = (time * ((size * 1000) / DEFAULT_PACKET_SIZE_BYTES)) / 1000;
		else
			attempt = (time * 1000) / ((DEFAULT_PACKET_SIZE_BYTES * 1000) / size);

		/**
		 * If the probability is low or the look around throughput is
		 * worse than our current throughput, then lets just try once.
		 */
		if (tb->table[rate->rates[i].index].prob < 20) {
			rate->rates[i].attempts = 1;
			continue;
		}
		if (i == 0 && (tb->table[rate->rates[i].index].throughput <
			tb->table[tb->best_prob.index].throughput)) {
			rate->rates[i].attempts = 1;
			continue;
		}

		/* if the time for a single attempt is very long, lets just try once */
		if (attempt > MAX_WINDOW_ATTEMPT_TIME) {
			*rem_time -= attempt;
			rate->rates[i].attempts += 1;
		} else {
			*rem_time -= attempt * 2;
			rate->rates[i].attempts += 2;
		}
	}
}

void mmrc_get_rates(struct mmrc_table *tb,
		    struct mmrc_rate_table *out,
		    size_t size)
{
	uint8_t i;
	uint16_t random_index;
	struct mmrc_rate random;
	struct mmrc_rate lookaround0 = tb->best_tp;
	struct mmrc_rate lookaround1 = tb->best_tp;
	bool is_lookaround;
	int lookaround_index = -1;
	int best_index = 0;
	int theoretical_random_tp = 0;
	int theoretical_best_tp = 0;
	int best_tp;
	int tp_fail_count = 0;

	int32_t rem_time = RATE_WINDOW_MICROSECONDS;

	memset(out, 0, sizeof(*out));

	tb->lookaround_cnt = (tb->lookaround_cnt + 1) % tb->lookaround_wrap;
	is_lookaround = tb->lookaround_cnt == 0 &&
				tb->fixed_rate.rate == MMRC_MCS_UNUSED;

	if (tb->lookaround_wrap != LOOKAROUND_RATE_STABLE) {
		if (tb->stability_cnt >= tb->stability_cnt_threshold) {
			tb->lookaround_wrap = LOOKAROUND_RATE_STABLE;
			tb->stability_cnt_threshold = STABILITY_CNT_THRESHOLD_STABLE;
			tb->stability_cnt = STABILITY_CNT_THRESHOLD_STABLE * 2;
			is_lookaround = false;
		}
	} else if (tb->stability_cnt < tb->stability_cnt_threshold) {
		tb->stability_cnt_threshold = STABILITY_CNT_THRESHOLD_NORMAL;
		tb->lookaround_wrap = LOOKAROUND_RATE_NORMAL;
		tb->stability_cnt = 0;
	}

	/* Look around only when the fixed rate is not set */
	if (is_lookaround) {
		tb->total_lookaround++;
		tb->forced_lookaround = (tb->forced_lookaround + 1) %
			LOOKAROUND_RATE_NORMAL;

		theoretical_best_tp =
			calculate_theoretical_throughput(tb->best_tp);
		best_tp = calculate_throughput(tb, tb->best_tp.index);

		/* Generate a lookaround */
		osal_mmrc_seed_random();

		while (1) {
			random_index = osal_mmrc_random_u32() % rows_from_sta_caps(&tb->caps);
			random = get_rate_row(tb, random_index);
			if (!validate_rate(&random))
				continue;

#if MMRC_MODE == MMRC_MODE_80211AH
			if (random.rate == MMRC_MCS10)
				continue;
#endif
			/**
			 * Add looking down the rates limitation in a form of percentage
			 * of the best throughput rate theoretical performance.
			 */
			theoretical_random_tp =
				calculate_theoretical_throughput(random);
			if (MAX_ALLOWED_GAP(theoretical_best_tp, theoretical_random_tp, 33))
				continue;

			/* Skip rates that can only be worse than the current best */
			if ((theoretical_random_tp < best_tp) && (tp_fail_count++ < TP_FAIL_MAX))
				continue;

			/**
			 * Force looking up the rate no more that one MCS.
			 * It will avoid looking for rates with very low success rate.
			 * In case of better environment conditions MMRC will collect
			 * enough statistics to climb up the rates one by one.
			 */
			if (random.rate > (tb->best_tp.rate + 1))
				continue;

			break;
		}

		lookaround0 = tb->best_tp;
		lookaround1 = random;
		lookaround_index = 1;
		/**
		 * Alway fall back to basic rate when state changed to uninitialised due to sharp
		 * drop in best rate probability or no feedback for long period of time.
		 */
		if (theoretical_best_tp > theoretical_random_tp &&
		    tb->table[tb->best_tp.index].prob > LOOKAROUND_THRESHOLD) {
			if (tb->table[random.index].evidence == 0 &&
			    tb->table[random.index].sent == 0 &&
			    tb->forced_lookaround == 0 &&
			    tb->table[tb->best_tp.index].prob < FORCED_LOOKAROUND_THRESHOLD) {
				lookaround0 = random;
				lookaround1 = tb->best_tp;
				lookaround_index = 0;
				best_index = 1;
			}
		} else {
			lookaround0 = random;
			lookaround1 = tb->best_tp;
			lookaround_index = 0;
			best_index = 1;
		}
	}

	if (tb->caps.max_rates == 1) {
		out->rates[0] = (is_lookaround) ? lookaround0 : tb->best_tp;
		out->rates[1].rate = MMRC_MCS_UNUSED;
		out->rates[2].rate = MMRC_MCS_UNUSED;
		out->rates[3].rate = MMRC_MCS_UNUSED;
	} else if (tb->caps.max_rates == 2) {
		out->rates[0] = (is_lookaround) ? lookaround0 : tb->best_tp;
		out->rates[1] = (is_lookaround) ? lookaround1 : tb->best_prob;
		out->rates[2].rate = MMRC_MCS_UNUSED;
		out->rates[3].rate = MMRC_MCS_UNUSED;
	} else if (tb->caps.max_rates == 3) {
		out->rates[0] = (is_lookaround) ? lookaround0 : tb->best_tp;
		out->rates[1] = (is_lookaround) ? lookaround1 : tb->second_tp;
		out->rates[2] = tb->best_prob;
		out->rates[3].rate = MMRC_MCS_UNUSED;
	} else {
		out->rates[0] = (is_lookaround) ? lookaround0 : tb->best_tp;
		out->rates[1] = (is_lookaround) ? lookaround1 : tb->second_tp;
		out->rates[2] = tb->best_prob;
		out->rates[3] = tb->baseline;
	}

	/* For fallback rates, set RTS/CTS */
	for (i = 1; i < MMRC_MAX_CHAIN_LENGTH; i++)
		out->rates[i].flags |= MMRC_MASK(MMRC_FLAGS_CTS_RTS);

	/* Allocate initial attempts for rate */
	allocate_initial_attempts(tb, out, &rem_time, size);

	/* Calculate and allocate remaining attempts */
	calculate_remaining_attempts(tb, out, &rem_time, size);

	/* Enforce limits on each attempts */
	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		if (out->rates[i].rate != MMRC_MCS_UNUSED) {
			out->rates[i].attempts =
				out->rates[i].attempts == 0 ?
				MMRC_MIN_CHAIN_ATTEMPTS :
				out->rates[i].attempts;
			out->rates[i].attempts =
				out->rates[i].attempts >
				MMRC_MAX_CHAIN_ATTEMPTS ?
				MMRC_MAX_CHAIN_ATTEMPTS :
				out->rates[i].attempts;
			if (i == lookaround_index)
				out->rates[i].attempts = 1;
		}
	}

	/* Give the best rate at least 2 attempts to keep peak throughput */
	if (out->rates[best_index].attempts == 1)
		out->rates[best_index].attempts = 2;
}

void mmrc_update(struct mmrc_table *tb)
{
	uint32_t i;
	uint16_t this_success;
	uint32_t scale;
	uint32_t scaled_ewma;
	uint32_t new_stats = 0;

	for (i = 0; i < rows_from_sta_caps(&tb->caps); i++) {
		/* This algorithm is keeping track of the amount of evidence,
		 * being packets that have been recently sent at this rate.
		 * This value is smoothed with an EWMA function over time and
		 * used to update the probability of a rate succeeding
		 * dynamically. This method allows MMRC to react timely if a
		 * new rate is used that hasn't been used recently
		 */

		/* Neccesary to prevent a divide by 0 */
		if (tb->table[i].evidence == 0)
			scale = 0;
		else
			scale = ((tb->table[i].evidence * 2) * 100) /
				((tb->table[i].sent * EVIDENCE_SCALE) + tb->table[i].evidence);

		/* Restrict scale to appropriate values */
		if (scale > 100)
			scale = 100;

		scaled_ewma = scale * EWMA / 100;

		tb->table[i].evidence = ((tb->table[i].sent * EVIDENCE_SCALE *
					     (100 - scaled_ewma)) +
					 (tb->table[i].evidence * scaled_ewma)) / 100;

		if (tb->table[i].evidence > EVIDENCE_MAX)
			tb->table[i].evidence = EVIDENCE_MAX;

		if (tb->table[i].sent > 0) {
			this_success = (100 * tb->table[i].sent_success) / tb->table[i].sent;
			tb->table[i].prob = ((this_success * (100 - scaled_ewma)) +
					(tb->table[i].prob * scaled_ewma)) / 100;
			new_stats = 1;
		} else {
			if (tb->table[i].evidence == 0)
				tb->table[i].prob = RATE_INIT_PROBABILITY;
		}

		/* Clear our sent statistics and update totals */
		tb->table[i].total_sent += tb->table[i].sent;
		tb->table[i].sent = 0;

		tb->table[i].total_success += tb->table[i].sent_success;
		tb->table[i].sent_success = 0;

		tb->table[i].throughput = calculate_throughput(tb, i);
		if (tb->table[i].max_throughput < tb->table[i].throughput)
			tb->table[i].max_throughput = tb->table[i].throughput;

		/* Reset the running average windows if reached collector limits */
		if (tb->table[i].sum_throughput >
			(0xFFFFFFFF - tb->table[i].throughput)) {
			tb->table[i].sum_throughput /=
				tb->table[i].avg_throughput_counter;
			tb->table[i].avg_throughput_counter = 1;
		}
		/* Update the sum and counter so it will be possible later to
		 * calculate the running average throughput
		 */
		tb->table[i].sum_throughput += tb->table[i].throughput;
		tb->table[i].avg_throughput_counter++;
	}

	generate_table_priority(tb, new_stats);
}

void mmrc_feedback_agg(struct mmrc_table *tb,
		       struct mmrc_rate_table *rates,
		       int32_t retry_count,
		       uint32_t success,
		       uint32_t failure)
{
	int32_t ind = retry_count;
	uint32_t i;
	uint32_t packet_count = success + failure;

	/* Recover from unintialisated state upon the first feedback with success */
	if (!tb->is_initialised) {
		if (success)
			tb->is_initialised = true;
	}

	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		/* Calculate and update the index */
		rate_update_index(tb, &rates->rates[i]);
		if ((int32_t)rates->rates[i].attempts < ind) {
			ind = ind - rates->rates[i].attempts;
			tb->table[rates->rates[i].index].sent +=
				rates->rates[i].attempts * packet_count;

			/**
			 * Assumes that retry count is incremented if the last
			 * rate in the table fails to send
			 */
			if (ind == 0) {
				/* We have used up all retries with no success */
				return;
			}
		} else {
			tb->table[rates->rates[i].index].sent += packet_count * ind;
			tb->table[rates->rates[i].index].sent_success += success;
			return;
		}
	}
}

void mmrc_feedback(struct mmrc_table *tb,
		   struct mmrc_rate_table *rates,
		   int32_t retry_count)
{
	int32_t ind = retry_count;
	uint32_t i;

	for (i = 0; i < MMRC_MAX_CHAIN_LENGTH; i++) {
		/* Calculate and update the index */
		rate_update_index(tb, &rates->rates[i]);
		if (retry_count == -1) {
			tb->table[rates->rates[i].index].sent = rates->rates[i].attempts;
			continue;
		}

		if ((int32_t)rates->rates[i].attempts < ind) {
			tb->table[rates->rates[i].index].sent += rates->rates[i].attempts;
			ind = ind - rates->rates[i].attempts;

			/**
			 * Assumes that retry count is incremented if the last
			 * rate in the table fails to send.
			 */
			if (ind == 0) {
				/* We have used up all retries with no success */
				return;
			}
		} else {
			tb->table[rates->rates[i].index].sent += ind;
			tb->table[rates->rates[i].index].sent_success += 1;
			/**
			 * We are in any sort of success feedback, recover from
			 * unintialisated state.
			 */
			if (!tb->is_initialised)
				tb->is_initialised = true;
			return;
		}
	}
}

void mmrc_sta_init(struct mmrc_table *tb, struct mmrc_sta_capabilities *caps)
{
	uint32_t i;
	uint16_t row_count = rows_from_sta_caps(caps);

	/* This zeros the mmrc_table memory required for the given capabilities */
	memset(tb, 0, mmrc_memory_required_for_caps(caps));
	memcpy(&tb->caps, caps, sizeof(tb->caps));

	for (i = 0; i < row_count; i++) {
		tb->table[i].prob = RATE_INIT_PROBABILITY;
		tb->table[i].evidence = 0;
		tb->table[i].throughput = 0;
		tb->table[i].sum_throughput = tb->table[i].throughput;
		tb->table[i].avg_throughput_counter = 0;
		tb->table[i].max_throughput = tb->table[i].throughput;
	}

	tb->is_initialised = false;
	tb->fixed_rate.rate = MMRC_MCS_UNUSED;
	tb->lookaround_cnt = 0;
	tb->lookaround_wrap = LOOKAROUND_RATE_INIT;
	tb->stability_cnt_threshold = STABILITY_CNT_THRESHOLD_INIT;
	tb->baseline = get_rate_row(tb, find_baseline_index(tb));
	generate_table_priority(tb, 0);

	/**
	 * Verifying station parameters are being intialised: either by
	 * driver's Module Parameters or by default macros.
	 */
	tb->caps.max_rates = tb->caps.max_rates == 0 ?
		MMRC_MAX_CHAIN_LENGTH : tb->caps.max_rates;
	tb->caps.max_retries = tb->caps.max_retries == 0 ?
		MMRC_MAX_CHAIN_ATTEMPTS : tb->caps.max_retries;
}

bool mmrc_set_fixed_rate(struct mmrc_table *tb, struct mmrc_rate fixed_rate)
{
	bool caps_support_rate = true;

	/* Do not accept rate which does not support the STA capabilities */
	if (((MMRC_MASK(fixed_rate.rate) & tb->caps.rates) == 0) ||
		((MMRC_MASK(fixed_rate.bw) & tb->caps.bandwidth) == 0) ||
		((MMRC_MASK(fixed_rate.ss) & tb->caps.spatial_streams) == 0) ||
		((MMRC_MASK(fixed_rate.guard) & tb->caps.guard) == 0))
		caps_support_rate = false;

	if (validate_rate(&fixed_rate) && caps_support_rate) {
		tb->fixed_rate = fixed_rate;
		return true;
	}
	return false;
}
