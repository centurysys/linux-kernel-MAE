/*
 * Copyright 2022 Morse Micro
 *
 */

#ifndef _TWT_H_
#define _TWT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <net/netlink.h>

#include "morse.h"
#include "dot11ah/dot11ah.h"

/* For now limit to maximum as defined in P802.11REVme_D1.1 Section 9.4.2.199 */
#define MORSE_TWT_AGREEMENTS_MAX_PER_STA		(8)

#define TWT_WAKE_DURATION_UNIT				(256)
#define TWT_WAKE_INTERVAL_EXPONENT_MAX_VAL		(31)
#define TWT_WAKE_DURATION_MAX_US			(__UINT8_MAX__ * TWT_WAKE_DURATION_UNIT)
#define TWT_AGREEMENT_REQUEST_TYPE_OFFSET		(1)
#define TWT_AGREEMENT_TARGET_WAKE_TIME_OFFSET		(3)
#define TWT_AGREEMENT_WAKE_DURATION_OFFSET		(11)
#define TWT_AGREEMENT_WAKE_INTERVAL_MANTISSA_OFFSET	(12)

enum morse_twt_state {
	MORSE_TWT_STATE_NO_AGREEMENT,
	MORSE_TWT_STATE_CONSIDER_REQUEST,
	MORSE_TWT_STATE_CONSIDER_SUGGEST,
	MORSE_TWT_STATE_CONSIDER_DEMAND,
	MORSE_TWT_STATE_CONSIDER_GROUPING,
	MORSE_TWT_STATE_AGREEMENT,
};

/* This structure is packed as control and params form the TWT IE. This allows the use of memcpy. */
struct morse_twt_agreement_data {
	/** First wakeup time in us with reference to the TSF */
	u64 wake_time_us;
	/** Interval between wake ups in us */
	u64 wake_interval_us;
	/** Wake nominal minimum duration in us */
	u32 wake_duration_us;
	/** TWT control field */
	u8 control;
	/** TWT agreement parameters */
	struct ieee80211_twt_params params;
} __packed;

struct morse_twt_agreement {
	struct list_head list;
	enum morse_twt_state state;
	struct morse_twt_agreement_data data;
};

enum morse_twt_event_type {
	MORSE_TWT_EVENT_SETUP,
	MORSE_TWT_EVENT_TEARDOWN
};

struct morse_twt_event {
	struct list_head list;
	enum morse_twt_event_type type;
	u8 addr[ETH_ALEN];
	u8 flow_id;
	union {
		struct {
			enum ieee80211_twt_setup_cmd cmd;
			struct morse_twt_agreement_data *agr_data;
		} setup;

		/* TODO Implement teardown. */
		struct {
			bool teardown;
		} teardown;
	};
};

struct morse_twt_sta {
	struct list_head list;
	u8 addr[ETH_ALEN];

	struct morse_twt_agreement agreements[MORSE_TWT_AGREEMENTS_MAX_PER_STA];
};

struct morse_twt_wake_interval {
	struct list_head list;
	struct list_head agreements;
};

static inline struct morse_vif *morse_twt_to_morse_vif(struct morse_twt *twt)
{
	return container_of(twt, struct morse_vif, twt);
}

/**
 * @morse_twt_event_queue_purge() - Remove all events for a STA addr from the event queue
 *
 * @mors	The morse chip struct
 * @mors_vif	The morse VIF struct
 * @addr	Address of the STA to remove events for
 */
void morse_twt_event_queue_purge(struct morse *mors, struct morse_vif *mors_vif, u8 *addr);

/**
 * morse_twt_sta_remove_addr() - Remove a station's TWT agreement.
 *
 * @mors	The morse chip struct
 * @mors_vif	The morse VIF struct
 * @skb		The sk_buff which should contain a (re)assoc frame
 *
 * Return:	0 on success or relevant error.
 */
int morse_twt_sta_remove_addr(struct morse *mors, struct morse_vif *mors_vif, u8 *addr);

/**
 * morse_twt_insert_ie() - Insert a TWT IE into an sk_buff
 *
 * @mors	The morse chip struct
 * @tx		The TWT data to send
 * @ies_mask	Array of information elements.
 * @size	Size of the TWT IE (can be found using morse_twt_get_ie_size())
 */
void morse_twt_insert_ie(struct morse *mors,
			 struct morse_twt_event *tx, struct dot11ah_ies_mask *ies_mask, u8 size);

/**
 * morse_twt_dequeue_tx() -	removes TX data from the queue it is in. It will also handle
 *				freeing the associated memory.
 *
 *
 * @mors	The morse chip struct
 * @event	The event or TX data to get the TWT IE size of
 * @tx		The transmitted event to remove from the queue.
 *
 * Return:	Positive size of the TWT on success or negative error code
 */
int morse_twt_dequeue_tx(struct morse *mors, struct morse_vif *mors_vif,
			 struct morse_twt_event *tx);

/**
 * morse_twt_get_ie_size() - Gets the size of a TWT IE for an event or TX data
 *
 * @mors	The morse chip struct
 * @event	The event or TX data to get the TWT IE size of
 *
 * Return:	Positive size of the TWT IE on success or negative error code
 */
int morse_twt_get_ie_size(struct morse *mors, struct morse_twt_event *event);

/**
 * morse_twt_peek_tx() - Get TWT TX data from the queue without removing it
 *
 * @mors	The morse chip struct
 * @mors_vif	The morse VIF struct
 * @addr	Destination address to get TX data for
 * @flow_id	Flow id to match if not NULL
 *
 * Return:	TX data for the destination address or NULL if none available or error
 */
struct morse_twt_event *morse_twt_peek_tx(struct morse *mors,
					  struct morse_vif *mors_vif,
					  const u8 *addr, const u8 *flow_id);

/**
 * morse_twt_parse_ie() - Parse a TWT IE and fills out an event
 *
 * @vif		The VIF the IE was received on.
 * @ie		The TWT IE to parse.
 * @event	The event to fill.
 * @src_addr	Address of the device sending the IE.
 *
 * Return:	0 on success or relevant error
 */
int morse_twt_parse_ie(struct morse_vif *vif, struct ie_element *ie,
		       struct morse_twt_event *event, const u8 *src_addr);

/**
 * morse_twt_dump_element() - Prints out the information for an event
 *
 * @mors	The morse chip struct
 * @event	The twt event to dump
 */
void morse_twt_dump_event(struct morse *mors, struct morse_twt_event *event);

/**
 * morse_twt_dump_wake_interval_tree() - Print the tree of wake intervals/agreements to debugfs
 *
 * @file	Seq file to print debug to.
 * @mors_vif	The morse VIF struct.
 */
void morse_twt_dump_wake_interval_tree(struct seq_file *file, struct morse_vif *mors_vif);

/**
 * morse_twt_dump_sta_agreements() - Print the tree of stations/agreements to debugfs
 *
 * @file	Seq file to print debug to.
 * @mors_vif	The morse VIF struct.
 */
void morse_twt_dump_sta_agreements(struct seq_file *file, struct morse_vif *mors_vif);

/**
 * morse_twt_install_pending_agreements() - Installs pending agreements to the firmware
 *
 * @mors	The morse chip struct
 * @mors_vif	The morse VIF struct
 */
void morse_twt_install_pending_agreements(struct morse *mors, struct morse_vif *mors_vif);

/**
 * @morse_twt_queue_event() - Adds a TWT event to the queue
 *
 * @mors	The morse chip struct
 * @mors_vif	The morse VIF struct
 * @event	The event to queue
 */
void morse_twt_queue_event(struct morse *mors,
			   struct morse_vif *mors_vif, struct morse_twt_event *event);

/**
 * morse_twt_handle_event() - Process a TWT event
 *
 * @mors_vif	The morse VIF struct
 * @addr	Address to filter on, if NULL proccess all events
 */
void morse_twt_handle_event(struct morse_vif *mors_vif, u8 *addr);

/**
 * morse_twt_handle_event_work() - Process TWT events that are queued (can also be called directly)
 *
 * @work	The work structure for the TWT event queue
 */
void morse_twt_handle_event_work(struct work_struct *work);

/**
 * morse_twt_init() - Initialise TWT
 *
 * @mors	The morse chip struct
 *
 * Return:	0 on success or relevant error
 */
int morse_twt_init(struct morse *mors);

/**
 * morse_twt_init_vif() - Initialise TWT for a VIF, this should be done after morse_twt_init()
 *
 * @mors	The morse chip struct
 * @mors_vif	The virtual interface
 *
 * Return:	0 on success or relevant error
 */
int morse_twt_init_vif(struct morse *mors, struct morse_vif *vif);

/**
 * morse_twt_finish_vif() - Initialise TWT for a VIF, this should be done before morse_twt_finish()
 *
 * @mors	The morse chip struct
 * @mors_vif	The virtual interface
 *
 * Return:	0 on success or relevant error
 */
int morse_twt_finish_vif(struct morse *mors, struct morse_vif *vif);

/**
 * morse_twt_finish() - Finish TWT
 *
 * @mors	The morse chip struct
 *
 * Return:	0 on success or relevant error
 */
int morse_twt_finish(struct morse *mors);

/**
 * morse_twt_initialise_agreement() - Initialises the twt agreement that needs to be sent to the FW
 * This is only used when we want to directly set the twt params in the FW bypassing the TWT IE
 * insertion
 *
 * @twt_conf	The twt configuration struct
 * @agreement	pointer to the agreement that need to be sent to the FW
 *
 * Return: The agreement length
 */
int morse_twt_initialise_agreement(struct morse_twt_agreement_data *twt_data, u8 *agreement);

/**
 * morse_process_twt_cmd() - Process TWT message triggered by morsectrl
 *
 * @mors	The morse chip struct
 * @morse_vif	The morse VIF struct
 * @cmd		Incoming twt conf command parameters
 *
 * Return	0 on success or relevant error
 */
int morse_process_twt_cmd(struct morse *mors, struct morse_vif *mors_vif, struct morse_cmd *cmd);

#endif
