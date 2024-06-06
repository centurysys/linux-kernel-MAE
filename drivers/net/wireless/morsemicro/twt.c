/*
 * Copyright 2022-2023 Morse Micro
 *
 */

#include <linux/math64.h>

#include "command.h"
#include "twt.h"
#include "utils.h"
#include "mac.h"
#include "debug.h"

#define TWT_IE_MIN_LENGTH	(10)
#define TWT_IE_MAX_LENGTH	(20)
#define TWT_SETUP_CMD_MAX	(8)
#define TWT_SETUP_CMD_UNKNOWN	(8)
#define TWT_WAKE_DUR_UNIT_256	(256)

#define MORSE_TWT_DBG(_m, _f, _a...)		morse_dbg(FEATURE_ID_TWT, _m, _f, ##_a)
#define MORSE_TWT_INFO(_m, _f, _a...)		morse_info(FEATURE_ID_TWT, _m, _f, ##_a)
#define MORSE_TWT_WARN(_m, _f, _a...)		morse_warn(FEATURE_ID_TWT, _m, _f, ##_a)
#define MORSE_TWT_ERR(_m, _f, _a...)		morse_err(FEATURE_ID_TWT, _m, _f, ##_a)

#define MORSE_TWT_WARN_RATELIMITED(_m, _f, _a...)		\
	morse_warn_ratelimited(FEATURE_ID_TWT, _m, _f, ##_a)
#define MORSE_TWT_ERR_RATELIMITED(_m, _f, _a...)		\
	morse_err_ratelimited(FEATURE_ID_TWT, _m, _f, ##_a)

static const char *twt_cmd_strs[TWT_SETUP_CMD_MAX + 1] = {
	"Request",
	"Suggest",
	"Demand",
	"Grouping",
	"Accept",
	"Alternate",
	"Dictate",
	"Reject",
	"Unknown"
};

static int morse_twt_enter_state(struct morse *mors,
				 struct morse_twt *twt,
				 struct morse_twt_sta *sta,
				 struct morse_twt_event *event, enum morse_twt_state state);

/* Shorten the verbosity for referencing the twt control flags */
#define MORSE_TWT_CTRL_SUP(CONTROL, FLAG) \
	morse_twt_ctrl_flag_is_set(CONTROL, IEEE80211_TWT_CONTROL_##FLAG)

/* Shorten the verbosity for referencing the twt request flags */
#define MORSE_TWT_REQTYPE(REQ, FLAG) \
	morse_twt_req_flag_is_set(REQ, IEEE80211_TWT_REQTYPE_##FLAG)

static bool morse_twt_ctrl_flag_is_set(u8 flags, u8 flag)
{
	return !!(flags & flag);
}

static bool morse_twt_req_flag_is_set(__le16 le_flags, u16 flag)
{
	u16 flags = le16_to_cpu(le_flags);

	return !!(flags & flag);
}

static u64 morse_twt_calculate_wake_interval_us(struct ieee80211_twt_params *params)
{
	u16 exp = (le16_to_cpu(params->req_type) & IEEE80211_TWT_REQTYPE_WAKE_INT_EXP) >>
	    IEEE80211_TWT_REQTYPE_WAKE_INT_EXP_OFFSET;

	return (u64)le16_to_cpu(params->mantissa) * (1 << exp);
}

static bool morse_twt_cmd_is_req(enum ieee80211_twt_setup_cmd cmd)
{
	/* Since requester commands are between 0 and 3 and responder commands are between 4 and 7
	 * we can use bit 2 to tell if the command is a request.
	 */
	return ((u8)cmd & BIT(2)) == 0;
}

/**
 * @morse_twt_set_command() - sets the setup command type in the TWT Request Type (from TWT IE)
 *
 * @req_type_le		Request type field as little endian
 * @cmd			The setup command to set to.
 */
static void morse_twt_set_command(__le16 *req_type_le, enum ieee80211_twt_setup_cmd cmd)
{
	u16 req_type;

	req_type = le16_to_cpu(*req_type_le) & ~IEEE80211_TWT_REQTYPE_SETUP_CMD;
	if (morse_twt_cmd_is_req(cmd))
		req_type |= IEEE80211_TWT_REQTYPE_REQUEST;
	else
		req_type &= ~IEEE80211_TWT_REQTYPE_REQUEST;

	*req_type_le = cpu_to_le16(req_type | (cmd << IEEE80211_TWT_REQTYPE_SETUP_CMD_OFFSET));
}

/* Keep exponents and units the same for now. Review modification when sending alternate setup
 * command messages.
 */
static void morse_twt_update_params(struct ieee80211_twt_params *params,
				    u8 control,
				    u64 wake_time_us, u64 wake_interval_us, u32 wake_duration_us)
{
	u16 exp = (le16_to_cpu(params->req_type) & IEEE80211_TWT_REQTYPE_WAKE_INT_EXP) >>
	    IEEE80211_TWT_REQTYPE_WAKE_INT_EXP_OFFSET;
	u16 mantissa;

	params->twt = cpu_to_le64(wake_time_us);
	mantissa = div_u64(wake_interval_us, (1 << exp));
	params->mantissa = cpu_to_le16(mantissa);
	if (MORSE_TWT_CTRL_SUP(control, WAKE_DUR_UNIT))
		params->min_twt_dur = MORSE_US_TO_TU(wake_duration_us);
	else
		params->min_twt_dur = wake_duration_us / TWT_WAKE_DUR_UNIT_256;
}

static void morse_twt_purge_event(struct morse *mors, struct morse_twt_event *event)
{
	if (!event)
		return;

	MORSE_TWT_DBG(mors, "Purging event %u from %pM (Flow ID %u)\n",
		      event->type, event->addr, event->flow_id);

	list_del(&event->list);
	if (event->type == MORSE_TWT_EVENT_SETUP)
		kfree(event->setup.agr_data);

	kfree(event);
}

static void morse_twt_queue_purge(struct morse *mors,
				  struct list_head *qhead, u8 *addr, u8 *flow_id)
{
	struct morse_twt_event *temp;
	struct morse_twt_event *event;

	if (!qhead)
		return;

	list_for_each_entry_safe(event, temp, qhead, list) {
		if ((!addr || ether_addr_equal(event->addr, addr)) &&
		    (!flow_id || (*flow_id == event->flow_id)))
			morse_twt_purge_event(mors, event);
	}
}

void morse_twt_event_queue_purge(struct morse *mors, struct morse_vif *mors_vif, u8 *addr)
{
	spin_lock_bh(&mors_vif->twt.lock);
	MORSE_TWT_DBG(mors, "Purging event queue\n");
	morse_twt_queue_purge(mors, &mors_vif->twt.events, addr, NULL);
	spin_unlock_bh(&mors_vif->twt.lock);
}

static void morse_twt_tx_queue_purge(struct morse *mors, struct morse_twt *twt, u8 *addr)
{
	MORSE_TWT_DBG(mors, "Purging TX queue\n");
	morse_twt_queue_purge(mors, &twt->tx, addr, NULL);
}

static void morse_twt_to_install_queue_purge(struct morse *mors, struct morse_twt *twt, u8 *addr)
{
	MORSE_TWT_DBG(mors, "Purging install queue\n");
	morse_twt_queue_purge(mors, &twt->to_install, addr, NULL);
}

void morse_twt_dump_wake_interval_tree(struct seq_file *file, struct morse_vif *mors_vif)
{
	struct morse_twt *twt;
	struct morse_twt_wake_interval *wi;
	struct morse_twt_agreement *agr;

	if (!file || !mors_vif)
		return;

	seq_printf(file, "%s:\n", morse_vif_name(morse_vif_to_ieee80211_vif(mors_vif)));
	twt = &mors_vif->twt;
	spin_lock_bh(&twt->lock);
	list_for_each_entry(wi, &twt->wake_intervals, list) {
		agr = list_first_entry_or_null(&wi->agreements, struct morse_twt_agreement, list);
		if (!agr) {
			seq_puts(file, "Empty wake interval\n");
			continue;
		}

		seq_printf(file, "TWT Wake interval: %lluus\n", agr->data.wake_interval_us);

		list_for_each_entry(agr, &wi->agreements, list) {
			seq_printf(file,
				   "\tTWT Wake time: %llu us, Wake Duration: %u us, State: %u\n",
				   agr->data.wake_time_us, agr->data.wake_duration_us, agr->state);
		}
	}
	spin_unlock_bh(&twt->lock);
}

void morse_twt_dump_sta_agreements(struct seq_file *file, struct morse_vif *mors_vif)
{
	struct morse_twt *twt;
	struct morse_twt_sta *sta;
	struct morse_twt_agreement *agr;
	struct morse_twt_agreement_data *agr_data;
	struct ieee80211_vif *vif;
	struct morse_twt_event *event;
	int i;

	if (!file || !mors_vif)
		return;

	twt = &mors_vif->twt;
	vif = morse_vif_to_ieee80211_vif(mors_vif);

	seq_printf(file, "%s:\n", morse_vif_name(vif));
	spin_lock_bh(&twt->lock);
	/* Print out all TWT Responder agreements. */
	list_for_each_entry(sta, &twt->stas, list) {
		seq_printf(file, "TWT Agreements for Requester: %pM, Responder: %pM\n",
			   sta->addr, vif->addr);
		for (i = 0; i < MORSE_TWT_AGREEMENTS_MAX_PER_STA; i++) {
			agr = &sta->agreements[i];
			seq_printf(file,
				   "\tFlow ID: %u, Wake Interval: %llu us, Wake Time: %llu us, Wake Duration: %u us, State %u\n",
				   i,
				   agr->data.wake_interval_us,
				   agr->data.wake_time_us, agr->data.wake_duration_us, agr->state);
		}
	}

	/* Print out all TWT Requester agreements. */
	if (twt->req_event_tx) {
		event = (struct morse_twt_event *)mors_vif->twt.req_event_tx;

		seq_printf(file, "TWT Agreements for Requester: %pM, Responder: %pM\n",
			   vif->addr, vif->bss_conf.bssid);

		agr_data = event->setup.agr_data;
		seq_printf(file,
			   "\tFlow ID: %u, Wake Interval: %llu us, Wake Time: %llu us, Wake Duration: %u us\n",
			   0,
			   agr_data->wake_interval_us,
			   agr_data->wake_time_us, agr_data->wake_duration_us);
	}
	spin_unlock_bh(&twt->lock);
}

void morse_twt_dump_event(struct morse *mors, struct morse_twt_event *event)
{
	struct morse_twt_agreement_data *agr_data;
	const char *cmd = NULL;
	u16 req_type;

	if (event->type != MORSE_TWT_EVENT_SETUP)
		return;

	if (event->setup.cmd < TWT_SETUP_CMD_MAX)
		cmd = twt_cmd_strs[event->setup.cmd];
	else
		cmd = twt_cmd_strs[TWT_SETUP_CMD_UNKNOWN];

	MORSE_TWT_DBG(mors, "TWT Command: %s\n", cmd);
	MORSE_TWT_DBG(mors, "TWT from: %pM\n", event->addr);
	MORSE_TWT_DBG(mors, "TWT Flow ID: %u\n", event->flow_id);
	if (event->setup.agr_data) {
		agr_data = event->setup.agr_data;
		req_type = le16_to_cpu(agr_data->params.req_type);
		MORSE_TWT_DBG(mors, "TWT %s\n",
			      MORSE_TWT_REQTYPE(req_type, REQUEST) ? "Requester" : "Responder");

		if (MORSE_TWT_CTRL_SUP(agr_data->control, NDP))
			MORSE_TWT_DBG(mors, "TWT NDP paging indication");
		MORSE_TWT_DBG(mors, "TWT PM: %s\n",
			  MORSE_TWT_CTRL_SUP(agr_data->control, RESP_MODE) ? "Awake" : "Doze");
		if (MORSE_TWT_CTRL_SUP(agr_data->control, NEG_TYPE_BROADCAST))
			MORSE_TWT_DBG(mors, "TWT Broadcast negotiation\n");
		if (MORSE_TWT_CTRL_SUP(agr_data->control, RX_DISABLED))
			MORSE_TWT_DBG(mors, "TWT Info frame disabled\n");
		MORSE_TWT_DBG(mors, "TWT Wake duration unit: %s\n",
			      MORSE_TWT_CTRL_SUP(agr_data->control, WAKE_DUR_UNIT) ?
				"TU" : "256us");

		if (MORSE_TWT_REQTYPE(req_type, TRIGGER))
			MORSE_TWT_DBG(mors, "TWT IE includes triggering frames\n");
		MORSE_TWT_DBG(mors, "TWT request type: %s\n",
			      MORSE_TWT_REQTYPE(req_type, IMPLICIT) ? "implicit" : "explicit");
		MORSE_TWT_DBG(mors, "TWT flow type: %s\n",
			      MORSE_TWT_REQTYPE(req_type, FLOWTYPE) ? "unannounced" : "announced");
		if (MORSE_TWT_REQTYPE(req_type, PROTECTION))
			MORSE_TWT_DBG(mors, "TWT requires protection (RAW)\n");

		MORSE_TWT_DBG(mors, "TWT Wake Time (us): %llu\n", agr_data->wake_time_us);
		MORSE_TWT_DBG(mors, "TWT Wake Interval (us): %llu\n", agr_data->wake_interval_us);
		MORSE_TWT_DBG(mors, "TWT Wake Nominal Min Duration (us): %u\n",
			      agr_data->wake_duration_us);
	}
}

static struct morse_twt_sta *morse_twt_get_sta(struct morse *mors,
					       struct morse_vif *mors_vif, u8 *addr)
{
	struct ieee80211_vif *vif;
	struct morse_twt_sta *sta;
	struct morse_twt_sta *temp;

	if (!mors || !mors_vif)
		return NULL;

	vif = morse_vif_to_ieee80211_vif(mors_vif);

	list_for_each_entry_safe(sta, temp, &mors_vif->twt.stas, list) {
		MORSE_TWT_DBG(mors, "%s: STA addr %pM (want %pM)\n", __func__, sta->addr, addr);
		if (ether_addr_equal(sta->addr, addr))
			return sta;
	}

	return NULL;
}

static int morse_twt_dequeue_tx_response(struct morse *mors,
					 struct morse_vif *mors_vif, struct morse_twt_event *tx)
{
	struct morse_twt_sta *sta = morse_twt_get_sta(mors, mors_vif, tx->addr);
	int ret = 0;

	if (!sta) {
		MORSE_TWT_WARN_RATELIMITED(mors, "%s: Couldn't get STA\n", __func__);
		return -ENODEV;
	}

	switch (sta->agreements[tx->flow_id].state) {
	case MORSE_TWT_STATE_CONSIDER_REQUEST:
	case MORSE_TWT_STATE_CONSIDER_SUGGEST:
	case MORSE_TWT_STATE_CONSIDER_DEMAND:
	case MORSE_TWT_STATE_CONSIDER_GROUPING:
		if (tx->setup.cmd == TWT_SETUP_CMD_ACCEPT)
			ret = morse_twt_enter_state(mors, &mors_vif->twt, sta, tx,
						    MORSE_TWT_STATE_AGREEMENT);
		else if (tx->setup.cmd == TWT_SETUP_CMD_REJECT)
			ret = morse_twt_enter_state(mors, &mors_vif->twt, sta, tx,
						    MORSE_TWT_STATE_NO_AGREEMENT);
		else
			MORSE_TWT_WARN_RATELIMITED(mors, "%s: Dequeuing unsupported response %u",
						   __func__, tx->setup.cmd);
		break;

	case MORSE_TWT_STATE_NO_AGREEMENT:
	case MORSE_TWT_STATE_AGREEMENT:
		MORSE_TWT_WARN_RATELIMITED(mors, "%s: Tried to dequeue TX from invalid state %u\n",
					   __func__, sta->agreements[tx->flow_id].state);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int morse_twt_dequeue_tx(struct morse *mors, struct morse_vif *mors_vif, struct morse_twt_event *tx)
{
	struct ieee80211_vif *vif;
	struct morse_twt *twt;
	int ret = 0;

	if (!mors || !mors_vif || !tx)
		return -EINVAL;

	twt = &mors_vif->twt;
	vif = morse_vif_to_ieee80211_vif(mors_vif);
	spin_lock_bh(&twt->lock);
	MORSE_TWT_DBG(mors, "Dequeuing TX %u to %pM (Flow ID %u)\n",
		      tx->type, tx->addr, tx->flow_id);

	/* Handle responder state transitions. Requester responses skip this. */
	if (tx->type == MORSE_TWT_EVENT_SETUP) {
		switch (tx->setup.cmd) {
		case TWT_SETUP_CMD_REQUEST:
		case TWT_SETUP_CMD_SUGGEST:
		case TWT_SETUP_CMD_DEMAND:
		case TWT_SETUP_CMD_GROUPING:
			MORSE_TWT_DBG(mors, "%s: Dequeue request\n", __func__);
			break;

		case TWT_SETUP_CMD_ACCEPT:
		case TWT_SETUP_CMD_ALTERNATE:
		case TWT_SETUP_CMD_DICTATE:
		case TWT_SETUP_CMD_REJECT:
			ret = morse_twt_dequeue_tx_response(mors, mors_vif, tx);
			break;
		}
	}

	morse_twt_purge_event(mors, tx);
	spin_unlock_bh(&twt->lock);

	return ret;
}

int morse_twt_get_ie_size(struct morse *mors, struct morse_twt_event *event)
{
	struct morse_twt_agreement_data *agr_data;

	if (!mors || !event || event->type != MORSE_TWT_EVENT_SETUP || !event->setup.agr_data)
		return -EINVAL;

	agr_data = event->setup.agr_data;

	return sizeof(agr_data->control) + sizeof(agr_data->params);
}

void morse_twt_insert_ie(struct morse *mors,
			 struct morse_twt_event *event, struct dot11ah_ies_mask *ies_mask, u8 size)
{
	if (!event || !event->setup.agr_data || !ies_mask) {
		MORSE_TWT_WARN_RATELIMITED(mors, "%s: Invalid data to insert TWT IE\n", __func__);
		return;
	}

	if (size <= 0) {
		MORSE_TWT_WARN_RATELIMITED(mors, "%s: Invalid TWT IE size for insertion %u\n",
					   __func__, size);
		return;
	}

	morse_dot11ah_insert_element(ies_mask, WLAN_EID_S1G_TWT,
				     (u8 *)&event->setup.agr_data->control, size);
}

static struct morse_twt_event *morse_twt_peek_queue(struct morse *mors,
						    struct list_head *head,
						    struct morse_vif *mors_vif,
						    const u8 *addr, const u8 *flow_id)
{
	struct morse_twt_event *event;
	int i = 0;

	if (!mors || !mors_vif)
		return NULL;

	if (list_empty(head))
		MORSE_TWT_DBG(mors, "%s: Queue is empty", __func__);

	if (!addr) {
		MORSE_TWT_DBG(mors, "%s: Peek all addresses", __func__);
		return list_first_entry_or_null(head, struct morse_twt_event, list);
	}

	list_for_each_entry(event, head, list) {
		if (!flow_id)
			MORSE_TWT_DBG(mors, "%s: Peek %d - addr %pM (want %pM)\n",
				  __func__, i++, event->addr, addr);
		else
			MORSE_TWT_DBG(mors, "%s: Peek %d - addr %pM flow id %u (want %pM %u)\n",
				  __func__, i++, event->addr, event->flow_id, addr, *flow_id);

		/* TODO remove use of zero MAC address for STAs. In the future if we want the AP to
		 * be a requester we will need to populate the address before this point.
		 */
		if ((ether_addr_equal(event->addr, addr) ||
		     is_zero_ether_addr(event->addr)) && (!flow_id || *flow_id == event->flow_id))
			return event;
	}

	return NULL;
}

struct morse_twt_event *morse_twt_peek_tx(struct morse *mors,
					  struct morse_vif *mors_vif,
					  const u8 *addr, const u8 *flow_id)
{
	if (!addr)
		return NULL;

	MORSE_TWT_DBG(mors, "%s: Peek want addr %pM\n", __func__, addr);
	return morse_twt_peek_queue(mors, &mors_vif->twt.tx, mors_vif, addr, flow_id);
}

static struct morse_twt_event *morse_twt_peek_event(struct morse *mors,
						    struct morse_vif *mors_vif,
						    const u8 *addr, const u8 *flow_id)
{
	if (addr)
		MORSE_TWT_DBG(mors, "%s: Peek want addr %pM\n", __func__, addr);
	else
		MORSE_TWT_DBG(mors, "%s: Peek want any addr\n", __func__);
	return morse_twt_peek_queue(mors, &mors_vif->twt.events, mors_vif, addr, flow_id);
}

static enum ieee80211_twt_setup_cmd morse_twt_get_command(__le16 req_type)
{
	u16 req = le16_to_cpu(req_type);

	return (req & IEEE80211_TWT_REQTYPE_SETUP_CMD) >> IEEE80211_TWT_REQTYPE_SETUP_CMD_OFFSET;
}

static struct morse_twt_sta *morse_twt_add_sta(struct morse *mors, struct morse_twt *twt, u8 *addr)
{
	struct morse_twt_sta *sta = kzalloc(sizeof(*sta), GFP_ATOMIC);
	int i;

	if (!sta)
		return NULL;

	ether_addr_copy(sta->addr, addr);

	/* By initalising these agreements as list heads we can use list_empty() to see if they are
	 * in a wake interval list or not.
	 */
	for (i = 0; i < MORSE_TWT_AGREEMENTS_MAX_PER_STA; i++)
		INIT_LIST_HEAD(&sta->agreements[i].list);

	list_add_tail(&sta->list, &twt->stas);

	return sta;
}

/**
 * morse_twt_agreement_remove() -	Removes an agreement from a wake_interval list. Will also
 *					remove the wake interval list entry if the list becomes
 *					empty.
 *
 * @mors	The morse chip struct.
 * @agr		The TWT agreement.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_agreement_remove(struct morse *mors, struct morse_twt_agreement *agr)
{
	struct list_head *prev;
	struct morse_twt_wake_interval *wi;

	if (!mors || !agr)
		return -EINVAL;

	if (list_empty(&agr->list)) {
		MORSE_TWT_DBG(mors, "Agreement not in wake interval list - skipping\n");
		return 0;
	}

	prev = agr->list.prev;
	/* list_del_init() is used so we can use list_empty() afterwards since the STA may still
	 * exist.
	 */
	list_del_init(&agr->list);

	/* If the previous list entry is empty it must be the head. Remove wake interval head if it
	 * is now empty.
	 */
	if (list_empty(prev)) {
		wi = container_of(prev, struct morse_twt_wake_interval, agreements);
		list_del(&wi->list);
		kfree(wi);
	}

	return 0;
}

/**
 * morse_twt_sta_remove() - Removes a station from the TWT station list.
 *
 * @mors	The morse chip struct.
 * @twt		The TWT struct.
 * @sta		The TWT station list entry.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_sta_remove(struct morse *mors,
				struct morse_twt *twt, struct morse_twt_sta *sta)
{
	int i;

	if (!mors || !twt)
		return -EINVAL;

	if (!sta)
		return -ENODEV;

	MORSE_TWT_DBG(mors, "Removing TWT STA %pM\n", sta->addr);

	/* Remove each agreement from the wake interval linked list. */
	for (i = 0; i < MORSE_TWT_AGREEMENTS_MAX_PER_STA; i++) {
		MORSE_TWT_DBG(mors, "Remove TWT agreement %u\n", i);
		morse_twt_agreement_remove(mors, &sta->agreements[i]);
	}

	/* Remove the agreements from the sta list and purge queues. */
	morse_twt_tx_queue_purge(mors, twt, sta->addr);
	list_del(&sta->list);
	kfree(sta);
	return 0;
}

int morse_twt_sta_remove_addr(struct morse *mors, struct morse_vif *mors_vif, u8 *addr)
{
	int ret;
	struct morse_twt_sta *sta;

	spin_lock_bh(&mors_vif->twt.lock);
	sta = morse_twt_get_sta(mors, mors_vif, addr);
	ret = morse_twt_sta_remove(mors, &mors_vif->twt, sta);
	spin_unlock_bh(&mors_vif->twt.lock);

	return ret;
}

/**
 * morse_twt_sta_remove_all() - Removes all of the stations from the TWT station list.
 *
 * @mors	The morse chip struct.
 * @twt		The TWT struct.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_sta_remove_all(struct morse *mors, struct morse_twt *twt)
{
	struct morse_twt_sta *sta;
	struct morse_twt_sta *temp;
	int ret;

	list_for_each_entry_safe(sta, temp, &twt->stas, list) {
		ret = morse_twt_sta_remove(mors, twt, sta);

		if (ret)
			MORSE_TWT_WARN(mors, "Failed to remove STA: %d\n", ret);
	}

	return 0;
}

/**
 * morse_twt_sta_agreement_remove() -	Removes an agreement from a wake_interval list. Will also
 *					remove the wake interval list entry if the list becomes
 *					empty. Will remove the station from the TWT station entry
 *					list if empty.
 *
 * @mors	The morse chip struct.
 * @twt		The TWT struct.
 * @sta		The TWT station list entry containing the agreement.
 * @flow_id	The flow ID of the TWT agreement.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_sta_agreement_remove(struct morse *mors,
					  struct morse_twt *twt,
					  struct morse_twt_sta *sta, u8 flow_id)
{
	struct morse_twt_agreement *agr;
	struct morse_twt_agreement *test;
	int i;

	if (!sta || flow_id >= MORSE_TWT_AGREEMENTS_MAX_PER_STA)
		return -EINVAL;

	agr = &sta->agreements[flow_id];
	WARN_ON_ONCE(agr->state != MORSE_TWT_STATE_NO_AGREEMENT);
	morse_twt_agreement_remove(mors, agr);

	/* Check if there are any agreements and remove STA if there aren't any. */
	for (i = 0; i < MORSE_TWT_AGREEMENTS_MAX_PER_STA; i++) {
		test = &sta->agreements[i];
		if (test->state != MORSE_TWT_STATE_NO_AGREEMENT)
			return 0;
	}

	return morse_twt_sta_remove(mors, twt, sta);
}

static int morse_twt_sta_agreement_add(struct morse *mors,
				       struct morse_twt *twt,
				       struct morse_twt_sta *sta,
				       u8 flow_id, struct morse_twt_agreement_data *agr_data)
{
	struct morse_twt_agreement *agr;

	if (!sta || !agr_data || flow_id >= MORSE_TWT_AGREEMENTS_MAX_PER_STA)
		return -EINVAL;

	agr = &sta->agreements[flow_id];
	memcpy(&agr->data, agr_data, sizeof(*agr_data));

	return 0;
}

/**
 * morse_twt_agreement_wake_interval_get() -	Get the wake interval list head. Creates one if it
 *						doesn't exits already.
 *
 * @mors		The morse chip struct.
 * @twt			The TWT struct.
 * @wake_interval_us	The wake interval (us) to search for.
 *
 * Return:		A wake interval head struct on success otherwise NULL.
 */
static struct morse_twt_wake_interval *morse_twt_agreement_wake_interval_get(struct morse *mors,
									     struct morse_twt *twt,
									     u64 wake_interval_us)
{
	struct list_head *head;
	struct morse_twt_wake_interval *wi;
	struct morse_twt_wake_interval *temp;
	struct morse_twt_agreement *agr;

	if (!twt)
		return NULL;

	head = &twt->wake_intervals;

	if (list_empty(&twt->wake_intervals)) {
		wi = kmalloc(sizeof(*wi), GFP_ATOMIC);
		if (!wi)
			return NULL;
		INIT_LIST_HEAD(&wi->agreements);
		list_add(&wi->list, head);
		return wi;
	}

	list_for_each_entry(wi, head, list) {
		u64 wi_us;

		/* Find this wake interval by looking at the first agreement */
		agr = list_first_entry_or_null(&wi->agreements, struct morse_twt_agreement, list);
		if (!agr)
			return NULL;

		wi_us = agr->data.wake_interval_us;

		/* We found the exact value. */
		if (wi_us == wake_interval_us) {
			return wi;
			/* The exact value doesn't exist but a larger one is in the list. */
		} else if (wi_us > wake_interval_us) {
			temp = kmalloc(sizeof(*temp), GFP_ATOMIC);
			if (!temp)
				return NULL;
			INIT_LIST_HEAD(&temp->agreements);
			if (wi->list.prev == head)
				list_add(&temp->list, head);
			else
				list_add(&temp->list, &list_prev_entry(wi, list)->list);

			return temp;
			/* The exact value is not in the list and there are no larger ones. */
		} else if (wi->list.next == head) {
			temp = kmalloc(sizeof(*temp), GFP_ATOMIC);
			if (!temp)
				return NULL;
			INIT_LIST_HEAD(&temp->agreements);
			list_add_tail(&temp->list, head);
			return temp;
		}
	}

	return NULL;
}

/**
 * morse_twt_agreement_wake_interval_add() -	Adds an agreement to a wake interval list.
 *
 * @mors	The morse chip struct.
 * @twt		The TWT struct.
 * @agr		The agreement to insert.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_agreement_wake_interval_add(struct morse *mors,
						 struct morse_twt *twt,
						 struct morse_twt_agreement *agr)
{
	struct morse_twt_wake_interval *wi;
	struct morse_twt_agreement *ptr;
	struct morse_twt_agreement *ptr_next;

	/* Agreement is not accepted until the accept message is sent. */
	if (!agr ||
	    agr->state == MORSE_TWT_STATE_NO_AGREEMENT || agr->state == MORSE_TWT_STATE_AGREEMENT)
		return -EINVAL;

	MORSE_TWT_DBG(mors, "Get TWT wake interval head for %lluus\n", agr->data.wake_interval_us);

	/* Obtain the wake interval list for the specified wake interval. */
	wi = morse_twt_agreement_wake_interval_get(mors, twt, agr->data.wake_interval_us);
	if (!wi)
		return -EINVAL;

	if (list_empty(&wi->agreements)) {
		agr->data.wake_time_us = 0;
		list_add(&agr->list, &wi->agreements);
		MORSE_TWT_DBG(mors, "First TWT entry for wake interval %lluus\n",
			  agr->data.wake_interval_us);
		return 0;
	}

	/* For now just add accepted 'Demand' agreements to the end. */
	if (morse_twt_get_command(agr->data.params.req_type) == TWT_SETUP_CMD_DEMAND) {
		list_add_tail(&agr->list, &wi->agreements);
		MORSE_TWT_DBG(mors, "Demand TWT entry for wake time %lluus added to tail\n",
			  agr->data.wake_time_us);
		return 0;
	}

	/* Iterate through the list of agreements with the same wake interval. Either insert into a
	 * gap which is large enough or add to the end. The wake time of the first agreement is used
	 * as the reference. The firmware is left to calculate the next service period based on the
	 * wake time and wake interval.
	 */
	list_for_each_entry(ptr, &wi->agreements, list) {
		u64 cur_next_wake_offset_us;
		u64 next_next_wake_offset_us;
		u64 unalloc_dur_us;

		if (list_is_last(&ptr->list, &wi->agreements)) {
			agr->data.wake_time_us =
			    ptr->data.wake_time_us + ptr->data.wake_duration_us;

			list_add(&agr->list, &ptr->list);
			return 0;
		}

		/* Time may have elapsed from the initial wake time so wrap values. */
		ptr_next = list_next_entry(ptr, list);
		div64_u64_rem(ptr->data.wake_time_us,
			      ptr->data.wake_interval_us, &cur_next_wake_offset_us);
		div64_u64_rem(ptr_next->data.wake_time_us,
			      ptr_next->data.wake_interval_us, &next_next_wake_offset_us);

		/* If the next agreement offset is before the previous unwrap once to preserve
		 * order.
		 */
		if (cur_next_wake_offset_us > next_next_wake_offset_us)
			next_next_wake_offset_us += ptr->data.wake_interval_us;

		/* Calculate gap between consecutive TWT service periods. */
		unalloc_dur_us = next_next_wake_offset_us -
		    (cur_next_wake_offset_us + ptr->data.wake_duration_us);

		/* Service period fits between the current and next service periods. */
		if (unalloc_dur_us >= agr->data.wake_duration_us) {
			/* Adjust wake time to align with the end of the previous service period. */
			agr->data.wake_time_us =
			    ptr->data.wake_time_us + ptr->data.wake_duration_us;

			list_add(&agr->list, &ptr->list);
			MORSE_TWT_DBG(mors, "Added TWT entry for wake time %llu\n",
				  agr->data.wake_time_us);
			return 0;
		}
	}

	return -EBADSLT;
}

/**
 * morse_twt_send_accept() -	Adds an accept message to the tx queue. During this process the
 *				agreement from the event is copied to the TWT station list entry and
 *				added to the wake interval list. After the wake time has been
 *				adjusted to avoid overlapping other service periods, the updated
 *				parameters are copied back into the event message which is then used
 *				to fill the TWT IE with an accept.
 *
 * @mors	The morse chip struct.
 * @twt		The TWT struct.
 * @sta		STA TWT data.
 * @event	The originating event to be recycled into an accept message in the tx queue.
 *
 * Return:	0 on success otherwise relevant error.
 */
static int morse_twt_send_accept(struct morse *mors,
				 struct morse_twt *twt,
				 struct morse_twt_sta *sta, struct morse_twt_event *event)
{
	struct morse_twt_agreement_data *event_agr_data;
	struct morse_twt_agreement *sta_agr;
	struct morse_twt_agreement_data *sta_agr_data;

	if (!mors || !event || !event->setup.agr_data || !sta)
		return -EINVAL;

	event_agr_data = event->setup.agr_data;

	sta_agr = &sta->agreements[event->flow_id];
	sta_agr_data = &sta_agr->data;
	memcpy(sta_agr_data, event_agr_data, sizeof(*sta_agr_data));
	morse_twt_agreement_wake_interval_add(mors, twt, sta_agr);

	/* When accepting, the TWT parameters need updating and the setup command. */
	event->setup.cmd = TWT_SETUP_CMD_ACCEPT;
	morse_twt_set_command(&sta_agr_data->params.req_type, event->setup.cmd);
	morse_twt_update_params(&sta_agr_data->params,
				sta_agr_data->control,
				sta_agr_data->wake_time_us,
				sta_agr_data->wake_interval_us, sta_agr_data->wake_duration_us);
	/* Copy changes back to the accept message. */
	memcpy(event_agr_data, sta_agr_data, sizeof(*event_agr_data));
	list_add_tail(&event->list, &twt->tx);
	MORSE_TWT_DBG(mors, "TWT Accept added to queue for %pM (Flow ID %u)\n",
		  event->addr, event->flow_id);
	return 0;
}

/* Takes ownership of the event (this allows recycling of the event struct). */
static int morse_twt_send_reject(struct morse *mors,
				 struct morse_twt *twt, struct morse_twt_event *event)
{
	if (!mors || !event)
		return -EINVAL;

	/* When rejecting, the TWT parameters remain the same, except for the setup command. */
	event->setup.cmd = TWT_SETUP_CMD_REJECT;
	morse_twt_set_command(&event->setup.agr_data->params.req_type, event->setup.cmd);
	list_add_tail(&event->list, &twt->tx);
	MORSE_TWT_WARN_RATELIMITED(mors, "TWT Reject added to queue for %pM (Flow ID %u)\n",
				   event->addr, event->flow_id);
	return 0;
}

static int morse_twt_enter_state_no_agreement(struct morse *mors,
					      struct morse_twt *twt,
					      struct morse_twt_sta *sta,
					      struct morse_twt_event *event)
{
	return morse_twt_sta_agreement_remove(mors, twt, sta, event->flow_id);
}

static int morse_twt_enter_state_consider_request(struct morse *mors,
						  struct morse_twt *twt,
						  struct morse_twt_sta *sta,
						  struct morse_twt_event *event)
{
	/* Accept all requests for now. */
	return morse_twt_send_accept(mors, twt, sta, event);
}

static int morse_twt_enter_state_consider_suggest(struct morse *mors,
						  struct morse_twt *twt,
						  struct morse_twt_sta *sta,
						  struct morse_twt_event *event)
{
	/* Accept all suggests for now. */
	return morse_twt_send_accept(mors, twt, sta, event);
}

static int morse_twt_enter_state_consider_demand(struct morse *mors,
						 struct morse_twt *twt,
						 struct morse_twt_sta *sta,
						 struct morse_twt_event *event)
{
	/* Send reject. Don't negotiate with terrorists. */
	return morse_twt_send_reject(mors, twt, event);
}

static int morse_twt_enter_state_consider_grouping(struct morse *mors,
						   struct morse_twt *twt,
						   struct morse_twt_sta *sta,
						   struct morse_twt_event *event)
{
	/* Send reject. Don't negotiate with terrorists. */
	return morse_twt_send_reject(mors, twt, event);
}

static int morse_twt_enter_state_agreement(struct morse *mors,
					   struct morse_twt *twt,
					   struct morse_twt_sta *sta, struct morse_twt_event *event)
{
	return morse_twt_sta_agreement_add(mors, twt, sta, event->flow_id, event->setup.agr_data);
}

static int morse_twt_enter_state(struct morse *mors,
				 struct morse_twt *twt,
				 struct morse_twt_sta *sta,
				 struct morse_twt_event *event, enum morse_twt_state state)
{
	int ret;
	struct morse_twt_agreement *agr;

	if (!mors || !twt || !sta)
		return -EINVAL;

	MORSE_TWT_DBG(mors, "TWT STA %pM (Flow ID %u) state %u -> %u\n",
		      sta->addr, event->flow_id, sta->agreements[event->flow_id].state, state);

	agr = &sta->agreements[event->flow_id];
	agr->state = state;

	switch (agr->state) {
	case MORSE_TWT_STATE_NO_AGREEMENT:
		ret = morse_twt_enter_state_no_agreement(mors, twt, sta, event);
		break;
	case MORSE_TWT_STATE_CONSIDER_REQUEST:
		ret = morse_twt_enter_state_consider_request(mors, twt, sta, event);
		break;
	case MORSE_TWT_STATE_CONSIDER_SUGGEST:
		ret = morse_twt_enter_state_consider_suggest(mors, twt, sta, event);
		break;
	case MORSE_TWT_STATE_CONSIDER_DEMAND:
		ret = morse_twt_enter_state_consider_demand(mors, twt, sta, event);
		break;
	case MORSE_TWT_STATE_CONSIDER_GROUPING:
		ret = morse_twt_enter_state_consider_grouping(mors, twt, sta, event);
		break;
	case MORSE_TWT_STATE_AGREEMENT:
		ret = morse_twt_enter_state_agreement(mors, twt, sta, event);
		break;
	}

	return 0;
}

static int morse_twt_handle_event_in_no_agreement(struct morse *mors,
						  struct morse_twt *twt,
						  struct morse_twt_sta *sta,
						  struct morse_twt_event *event)
{
	if (!sta)
		goto error;

	if (event->type != MORSE_TWT_EVENT_SETUP)
		goto error;

	switch (event->setup.cmd) {
	case TWT_SETUP_CMD_REQUEST:
		morse_twt_enter_state(mors, twt, sta, event, MORSE_TWT_STATE_CONSIDER_REQUEST);
		break;
	case TWT_SETUP_CMD_SUGGEST:
		morse_twt_enter_state(mors, twt, sta, event, MORSE_TWT_STATE_CONSIDER_SUGGEST);
		break;
	case TWT_SETUP_CMD_DEMAND:
		morse_twt_enter_state(mors, twt, sta, event, MORSE_TWT_STATE_CONSIDER_DEMAND);
		break;
	case TWT_SETUP_CMD_GROUPING:
		morse_twt_enter_state(mors, twt, sta, event, MORSE_TWT_STATE_CONSIDER_DEMAND);
		break;
	case TWT_SETUP_CMD_ACCEPT:
	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
	case TWT_SETUP_CMD_REJECT:
		goto error;
	}

	return 0;

error:
	morse_twt_purge_event(mors, event);
	return -EINVAL;
}

static int morse_twt_handle_event_in_consider(struct morse *mors,
					      struct morse_twt *twt,
					      struct morse_twt_sta *sta,
					      struct morse_twt_event *event)
{
	if (event->type != MORSE_TWT_EVENT_SETUP)
		return -EINVAL;

	switch (event->setup.cmd) {
	case TWT_SETUP_CMD_REQUEST:
	case TWT_SETUP_CMD_SUGGEST:
	case TWT_SETUP_CMD_DEMAND:
	case TWT_SETUP_CMD_GROUPING:
		/* We've gotten an additional request in quick succession, drop request. */
		morse_twt_purge_event(mors, event);
		break;
	case TWT_SETUP_CMD_ACCEPT:
	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
	case TWT_SETUP_CMD_REJECT:
		return -EINVAL;
	}

	return 0;
}

static int morse_twt_handle_event_in_agreement(struct morse *mors,
					       struct morse_vif *mors_vif,
					       struct morse_twt *twt,
					       struct morse_twt_sta *sta,
					       struct morse_twt_event *event)
{
	if (event->type != MORSE_TWT_EVENT_SETUP)
		return -EINVAL;

	switch (event->setup.cmd) {
	case TWT_SETUP_CMD_REQUEST:
	case TWT_SETUP_CMD_SUGGEST:
	case TWT_SETUP_CMD_DEMAND:
	case TWT_SETUP_CMD_GROUPING:
		{
			struct morse_twt_event *existing_tx_event;
			/* We've already got an agreement, so reject. Teardown of agreement should
			 * be done first. If there's already a response to a previous event ready to
			 * be sent, drop the event instead.
			 */
			existing_tx_event =
			    morse_twt_peek_tx(mors, mors_vif, sta->addr, &event->flow_id);
			if (existing_tx_event)
				morse_twt_purge_event(mors, event);
			else
				morse_twt_send_reject(mors, twt, event);
			break;
		}
	case TWT_SETUP_CMD_ACCEPT:
	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
	case TWT_SETUP_CMD_REJECT:
		return -EINVAL;
	}

	return 0;
}

void morse_twt_install_pending_agreements(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_twt_event *event;
	struct morse_twt_event *temp;

	spin_lock_bh(&mors_vif->twt.lock);
	list_for_each_entry_safe(event, temp, &mors_vif->twt.to_install, list) {
		/* Must unlock as we are sending a command which blocks */
		spin_unlock_bh(&mors_vif->twt.lock);

		if (!morse_cmd_twt_agreement_install_req(mors, event->setup.agr_data, mors_vif->id))
			MORSE_TWT_INFO(mors,
				       "Installed TWT agreement (AP: %pM, VIF: %u, Flow ID: %u)\n",
				       event->addr, mors_vif->id, event->flow_id);
		else
			MORSE_TWT_WARN(mors, "Failed to install TWT agreement\n");

		spin_lock_bh(&mors_vif->twt.lock);

		/* Cleanup event. */
		morse_twt_purge_event(mors, event);
	}
	spin_unlock_bh(&mors_vif->twt.lock);
}

void morse_twt_queue_event(struct morse *mors,
			   struct morse_vif *mors_vif, struct morse_twt_event *event)
{
	struct morse_twt *twt = &mors_vif->twt;

	spin_lock_bh(&twt->lock);
	MORSE_TWT_DBG(mors, "%s: Queue event %pM", __func__, event->addr);
	/* Remove stale events (with the same addr/flow id). */
	morse_twt_queue_purge(mors, &twt->events, event->addr, &event->flow_id);
	list_add_tail(&event->list, &twt->events);
	spin_unlock_bh(&twt->lock);
}

/**
 * morse_twt_preprocess_event() - Assess whether events are valid, move to relevant queues if
				  required, purge if invalid. If further processing is not required
				  this function will consume the event.
 *
 * @mors	The morse chip struct.
 * @twt		The morse twt struct.
 * @event	The event to preprocess.
 *
 * Return true if further processing is required otherwise false if the event has been consumed.
 */
static bool morse_twt_preprocess_event(struct morse *mors,
				       struct morse_twt *twt, struct morse_twt_event *event)
{
	if (!event)
		return false;

	morse_twt_dump_event(mors, event);

	if (event->type != MORSE_TWT_EVENT_SETUP) {
		morse_twt_purge_event(mors, event);
		return false;
	}

	switch (event->setup.cmd) {
	case TWT_SETUP_CMD_REQUEST:
	case TWT_SETUP_CMD_SUGGEST:
	case TWT_SETUP_CMD_DEMAND:
	case TWT_SETUP_CMD_GROUPING:
		if (twt->responder) {
			MORSE_TWT_DBG(mors, "%s: Received a TWT request: %u\n",
				      __func__, event->setup.cmd);
			return true;
		}

		MORSE_TWT_WARN_RATELIMITED(mors, "Not a TWT responder but received a request: %u\n",
					   event->setup.cmd);
		break;

	case TWT_SETUP_CMD_ACCEPT:
		if (twt->requester) {
			MORSE_TWT_DBG(mors, "%s: Received a TWT response: %u\n",
				      __func__, event->setup.cmd);
			/* Queue installing TWT agreement to chip. Requires STA state change to
			 * associated first.
			 */
			list_add_tail(&event->list, &twt->to_install);
			return false;
		}

		MORSE_TWT_ERR_RATELIMITED(mors, "Not a TWT requester but received a response: %u\n",
					  event->setup.cmd);
		break;

	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
	case TWT_SETUP_CMD_REJECT:
		MORSE_TWT_ERR_RATELIMITED(mors, "%s: Unsupported TWT requester response: %u\n",
					  __func__, event->setup.cmd);
	}

	morse_twt_purge_event(mors, event);
	return false;
}

void morse_twt_handle_event(struct morse_vif *mors_vif, u8 *addr)
{
	struct morse *mors = morse_vif_to_morse(mors_vif);
	struct morse_twt *twt = &mors_vif->twt;
	struct morse_twt_sta *sta;
	struct morse_twt_event *event;

	if (addr)
		MORSE_TWT_DBG(mors, "%s %pM\n", __func__, addr);
	else
		MORSE_TWT_DBG(mors, "%s no addr filter\n", __func__);

	spin_lock_bh(&twt->lock);

	if (list_empty(&twt->events))
		MORSE_TWT_DBG(mors, "%s: No events to handle\n", __func__);

	event = morse_twt_peek_event(mors, mors_vif, addr, NULL);
	while (event) {
		/* Dequeue event while we process it */
		list_del_init(&event->list);

		if (morse_twt_preprocess_event(mors, twt, event)) {
			/* Deal with received requests. */
			sta = morse_twt_get_sta(mors, mors_vif, event->addr);

			if (!sta)
				sta = morse_twt_add_sta(mors, twt, event->addr);

			if (!sta) {
				MORSE_TWT_ERR(mors, "Unable to allocate TWT STA (%pM) for event\n",
					      event->addr);

				/* Perhaps we can try again later. */
				schedule_work(&twt->work);
				spin_unlock_bh(&twt->lock);
				return;
			}

			switch (sta->agreements[event->flow_id].state) {
			case MORSE_TWT_STATE_NO_AGREEMENT:
				morse_twt_handle_event_in_no_agreement(mors, twt, sta, event);
				break;
			case MORSE_TWT_STATE_CONSIDER_REQUEST:
			case MORSE_TWT_STATE_CONSIDER_SUGGEST:
			case MORSE_TWT_STATE_CONSIDER_DEMAND:
			case MORSE_TWT_STATE_CONSIDER_GROUPING:
				morse_twt_handle_event_in_consider(mors, twt, sta, event);
				break;
			case MORSE_TWT_STATE_AGREEMENT:
				morse_twt_handle_event_in_agreement(mors, mors_vif,
								    twt, sta, event);
				break;
			default:
				morse_twt_purge_event(mors, event);
			}
		}

		event = morse_twt_peek_event(mors, mors_vif, addr, NULL);
	}

	spin_unlock_bh(&twt->lock);
}

void morse_twt_handle_event_work(struct work_struct *work)
{
	struct morse_twt *twt = container_of(work, struct morse_twt, work);

	morse_twt_handle_event(morse_twt_to_morse_vif(twt), NULL);
}

/**
 * morse_twt_requester_send() - Sends a setup command as a requester
 *
 * @mors	The morse chip struct.
 * @mors_vif	The morse VIF.
 * @data	The data for the TWT IE.
 * @cmd		The setup command to be sent, must match the data.
 *
 * Return:	0 on success or relevant error.
 */
static int morse_twt_requester_send(struct morse *mors, struct morse_vif *mors_vif,
				    struct morse_twt_agreement_data *data,
				    enum ieee80211_twt_setup_cmd cmd)
{
	struct morse_twt *twt;
	struct morse_twt_event *req;

	if (!mors || !mors_vif || !data)
		return -EINVAL;

	twt = &mors_vif->twt;

	/* Check to see if TWT capable and enabled. */
	if (!twt->requester) {
		MORSE_TWT_ERR(mors, "TWT non-requester trying to send request: %u\n", cmd);
		return -EPERM;
	}

	req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req)
		return -ENOMEM;

	INIT_LIST_HEAD(&req->list);

	/* Double check we are trying to send a command as a requester. */
	switch (cmd) {
	case TWT_SETUP_CMD_REQUEST:
	case TWT_SETUP_CMD_SUGGEST:
	case TWT_SETUP_CMD_DEMAND:
	case TWT_SETUP_CMD_GROUPING:
		break;

	case TWT_SETUP_CMD_ACCEPT:
	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
	case TWT_SETUP_CMD_REJECT:
		MORSE_TWT_ERR(mors, "TWT requester trying to send response: %u\n", cmd);
		kfree(req);
		return -EINVAL;
	}

	/* We can safely omit the addr and flow ID in the request case. */
	req->type = MORSE_TWT_EVENT_SETUP;
	req->setup.agr_data = data;
	req->setup.cmd = cmd;

	kfree(twt->req_event_tx);

	twt->req_event_tx = (u8 *)req;

	return 0;
}

static int morse_twt_validate_params(struct morse *mors, struct ieee80211_vif *vif,
				     struct ieee80211_twt_params *params)
{
	u16 req_type = le16_to_cpu(params->req_type);
	u16 setup_cmd = morse_twt_get_command(params->req_type);

	switch (setup_cmd) {
	case TWT_SETUP_CMD_ACCEPT:
	case TWT_SETUP_CMD_REJECT:
		if (vif->type != NL80211_IFTYPE_STATION) {
			MORSE_TWT_WARN(mors, "Only STA as requester is supported\n");
			return -EINVAL;
		}
		break;

	case TWT_SETUP_CMD_REQUEST:
	case TWT_SETUP_CMD_DEMAND:
	case TWT_SETUP_CMD_SUGGEST:
		if (vif->type != NL80211_IFTYPE_AP) {
			MORSE_TWT_WARN(mors, "Only AP as responser is supported\n");
			return -EINVAL;
		}
		break;

	case TWT_SETUP_CMD_ALTERNATE:
	case TWT_SETUP_CMD_DICTATE:
		break;

	case TWT_SETUP_CMD_GROUPING:
		MORSE_TWT_WARN(mors, "TWT Grouping unsupported\n");
		return -EINVAL;
	}

	/* Leave TSF validation to chip when installing an agreement. */

	if (MORSE_TWT_REQTYPE(req_type, FLOWTYPE)) {
		MORSE_TWT_WARN(mors, "Unannounced TWT unsupported\n");
		return -EINVAL;
	}

	if (!MORSE_TWT_REQTYPE(req_type, IMPLICIT)) {
		MORSE_TWT_WARN(mors, "Explicit TWT unsupported\n");
		return -EINVAL;
	}

	if (MORSE_TWT_REQTYPE(req_type, PROTECTION)) {
		MORSE_TWT_WARN(mors, "TWT protection (RAW) currently unsupported\n");
		return -EINVAL;
	}

	if (params->channel > 0) {
		MORSE_TWT_WARN(mors, "TWT channel unsupported\n");
		return -EINVAL;
	}

	return 0;
}

int morse_twt_parse_ie(struct morse_vif *mors_vif, struct ie_element *ie,
		       struct morse_twt_event *event, const u8 *src_addr)
{
	struct ieee80211_vif *vif;
	struct morse *mors;
	u8 flow_id;
	struct ieee80211_twt_params *twt_params;
	struct morse_twt_agreement_data *agr_data;
	const u8 *ptr;
	u8 control;
	int ret;

	if (!ie || !ie->ptr)
		return -EINVAL;

	if (!mors_vif || !event)
		return -EINVAL;

	mors = morse_vif_to_morse(mors_vif);

	if (ie->len < TWT_IE_MIN_LENGTH || ie->len > TWT_IE_MAX_LENGTH) {
		MORSE_TWT_WARN(mors, "%s: Invalid TWT IE length: %u\n", __func__, ie->len);
		return -EINVAL;
	}

	event->type = MORSE_TWT_EVENT_SETUP;
	ptr = ie->ptr;
	control = *ptr;

	/* Return error for unsupported options. */
	if (MORSE_TWT_CTRL_SUP(control, NEG_TYPE_BROADCAST)) {
		MORSE_TWT_WARN(mors, "%s: TWT Broadcast not currently supported\n", __func__);
		return -EINVAL;
	} else if (MORSE_TWT_CTRL_SUP(control, NEG_TYPE)) {
		MORSE_TWT_WARN(mors, "%s: TWT TBTT interval negotiation not supported\n", __func__);
		return -EINVAL;
	}

	if (MORSE_TWT_CTRL_SUP(control, NDP)) {
		MORSE_TWT_WARN(mors, "%s: TWT NDP paging not currently supported\n", __func__);
		return -EINVAL;
	}

	twt_params = (struct ieee80211_twt_params *)(++ptr);
	vif = morse_vif_to_ieee80211_vif(mors_vif);
	ret = morse_twt_validate_params(mors, vif, twt_params);
	if (ret) {
		MORSE_TWT_WARN(mors, "%s: Invalid TWT Params\n", __func__);
		return ret;
	}

	flow_id = (twt_params->req_type & IEEE80211_TWT_REQTYPE_FLOWID) >>
	    IEEE80211_TWT_REQTYPE_FLOWID_OFFSET;

	/* Copy message into its own memory and fill friendly values. */
	event->setup.agr_data = kmalloc(sizeof(*event->setup.agr_data), GFP_ATOMIC);
	if (!event->setup.agr_data)
		return -ENOMEM;

	agr_data = event->setup.agr_data;
	agr_data->control = control;
	memcpy(&agr_data->params, twt_params, sizeof(agr_data->params));

	event->setup.cmd = morse_twt_get_command(agr_data->params.req_type);
	ether_addr_copy(event->addr, src_addr);
	event->flow_id = flow_id;
	agr_data->wake_time_us = le64_to_cpu(agr_data->params.twt);
	agr_data->wake_interval_us = morse_twt_calculate_wake_interval_us(&agr_data->params);

	if (MORSE_TWT_CTRL_SUP(control, WAKE_DUR_UNIT))
		agr_data->wake_duration_us = MORSE_TU_TO_US(agr_data->params.min_twt_dur);
	else
		agr_data->wake_duration_us = agr_data->params.min_twt_dur * TWT_WAKE_DUR_UNIT_256;

	return 0;
}

int morse_twt_init(struct morse *mors)
{
	return 0;
}

int morse_twt_init_vif(struct morse *mors, struct morse_vif *mors_vif)
{
	if (!mors || !mors_vif)
		return -EINVAL;

	spin_lock_init(&mors_vif->twt.lock);
	INIT_LIST_HEAD(&mors_vif->twt.stas);
	INIT_LIST_HEAD(&mors_vif->twt.wake_intervals);
	INIT_LIST_HEAD(&mors_vif->twt.events);
	INIT_LIST_HEAD(&mors_vif->twt.tx);
	INIT_LIST_HEAD(&mors_vif->twt.to_install);

	INIT_WORK(&mors_vif->twt.work, morse_twt_handle_event_work);

	return 0;
}

int morse_twt_finish_vif(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_twt *twt;

	if (!mors || !mors_vif)
		return -EINVAL;

	twt = &mors_vif->twt;

	cancel_work_sync(&twt->work);
	spin_lock_bh(&twt->lock);
	morse_twt_sta_remove_all(mors, twt);
	morse_twt_tx_queue_purge(mors, twt, NULL);
	morse_twt_to_install_queue_purge(mors, twt, NULL);
	spin_unlock_bh(&twt->lock);
	morse_twt_event_queue_purge(mors, mors_vif, NULL);
	return 0;
}

int morse_twt_finish(struct morse *mors)
{
	return 0;
}

static int twt_calculate_wake_duration(int wake_duration)
{
	return MORSE_INT_CEIL(wake_duration, TWT_WAKE_DURATION_UNIT);
}

static u64 twt_calculate_wake_interval(u16 mantissa, int exponent)
{
	return (u64)mantissa * (1UL << exponent);
}

static u64 twt_calculate_wake_interval_fields(u64 wake_interval, u16 *mantissa, int *exponent)
{
	/* Calculate wake interval fields; wake interval = mantissa * 2^exponent */
	if (wake_interval > __UINT16_MAX__) {
		/* Brute force our way to the closest approximation of wake_interval */
		int e = 0;
		int m = __UINT16_MAX__;
		u64 difference = __UINT64_MAX__;

		for (e = 0; e <= TWT_WAKE_INTERVAL_EXPONENT_MAX_VAL; e++) {
			u64 calculated = twt_calculate_wake_interval(m, e);

			if (calculated > wake_interval)
				break;
		}

		e = min(e, TWT_WAKE_INTERVAL_EXPONENT_MAX_VAL);

		for (m = 0; m < __UINT16_MAX__; m++) {
			u64 interval = twt_calculate_wake_interval(m, e);

			if (abs(wake_interval - interval) > difference) {
				/* let's take the mantissa that gave us the closest approximation */
				m -= 1;
				break;
			}

			difference = abs(wake_interval - interval);
		}

		/* Set wake interval so caller knows what it got rounded to */
		*mantissa = m;
		*exponent = e;
		wake_interval = twt_calculate_wake_interval(m, e);
	} else {
		*exponent = 0;
		*mantissa = (int)wake_interval;
	}

	return wake_interval;
}

static int morse_twt_process_set_cmd(struct morse *mors,
				     struct morse_vif *mors_vif,
				     struct command_twt_req *cmd_set_twt)
{
	int ret = 0;
	int exponent = 0;
	struct morse_twt_agreement_data *agreement = NULL;
	u64 *wake_interval_us = &cmd_set_twt->set_twt_conf.wake_interval_us;

	agreement = kzalloc(sizeof(*agreement), GFP_KERNEL);

	if (!agreement)
		return -EINVAL;

	if (cmd_set_twt->cmd == TWT_CONF_SUBCMD_CONFIGURE_EXPLICIT) {
		agreement->params.mantissa =
		    cmd_set_twt->set_twt_conf.explicit.wake_interval_mantissa;
		exponent = cmd_set_twt->set_twt_conf.explicit.wake_interval_exponent;
		agreement->wake_interval_us =
		    twt_calculate_wake_interval(agreement->params.mantissa, exponent);
	} else {
		agreement->wake_interval_us =
		    twt_calculate_wake_interval_fields(le64_to_cpu(*wake_interval_us),
						       &agreement->params.mantissa, &exponent);
	}

	agreement->wake_duration_us = cmd_set_twt->set_twt_conf.wake_duration;
	agreement->params.min_twt_dur = twt_calculate_wake_duration(agreement->wake_duration_us);
	agreement->params.req_type |=
	    cmd_set_twt->flow_id <<
	    (IEEE80211_TWT_REQTYPE_FLOWID_OFFSET) & IEEE80211_TWT_REQTYPE_FLOWID;
	agreement->params.req_type |= (exponent << IEEE80211_TWT_REQTYPE_WAKE_INT_EXP_OFFSET) &
	    IEEE80211_TWT_REQTYPE_WAKE_INT_EXP;

	MORSE_TWT_DBG(mors, "TWT config dur:%d mant:%d exp:%d wake_int:%lld req:0x%x\n",
		      agreement->params.min_twt_dur, agreement->params.mantissa,
		      exponent, agreement->wake_interval_us, agreement->params.req_type);

	if (cmd_set_twt->cmd == TWT_CONF_SUBCMD_FORCE_INSTALL_AGREEMENT) {
		/* Send cmd to fw */
		agreement->params.twt = cmd_set_twt->set_twt_conf.target_wake_time;
		ret = morse_cmd_twt_agreement_install_req(mors,
							  agreement,
							  le16_to_cpu(cmd_set_twt->hdr.vif_id));

		/* If we force installation of an agreement then we must be a requester. YMMV if
		 * something else is in conflict.
		 */
		if (!ret)
			mors_vif->twt.requester = true;

		goto exit;
	}

	/* Verify the values we are sending are compatible with the running firmware. */
	ret = morse_cmd_twt_agreement_validate_req(mors,
						   agreement, le16_to_cpu(cmd_set_twt->hdr.vif_id));

	agreement->params.req_type |= IEEE80211_TWT_REQTYPE_REQUEST;
	agreement->params.req_type |= (cmd_set_twt->set_twt_conf.twt_setup_command <<
				       IEEE80211_TWT_REQTYPE_SETUP_CMD_OFFSET) &
	    IEEE80211_TWT_REQTYPE_SETUP_CMD;

	if (ret) {
		MORSE_TWT_WARN(mors, "TWT request invalid\n");
		goto exit;
	}

	return morse_twt_requester_send(mors, mors_vif, agreement, cmd_set_twt->cmd);

exit:
	kfree(agreement);
	return ret;
}

static int morse_twt_process_remove_cmd(struct morse *mors,
					struct morse_vif *mors_vif,
					struct command_twt_req *cmd_remove_twt)
{
	struct morse_cmd_remove_twt_agreement remove_twt;

	remove_twt.flow_id = cmd_remove_twt->flow_id;
	/* Handle the remove command in the driver for removal of any twt config data */
	return morse_cmd_twt_remove_req(mors, &remove_twt, le16_to_cpu(cmd_remove_twt->hdr.vif_id));
}

int morse_twt_initialise_agreement(struct morse_twt_agreement_data *twt_data, u8 *agreement)
{
	int agreement_len = 0;

	memset(agreement, 0, TWT_MAX_AGREEMENT_LEN);

	memcpy((agreement + TWT_AGREEMENT_WAKE_DURATION_OFFSET),
	       &twt_data->params.min_twt_dur, sizeof(twt_data->params.min_twt_dur));
	agreement_len += sizeof(twt_data->params.min_twt_dur);

	memcpy((agreement + TWT_AGREEMENT_WAKE_INTERVAL_MANTISSA_OFFSET),
	       &twt_data->params.mantissa, sizeof(twt_data->params.mantissa));
	agreement_len += sizeof(twt_data->params.mantissa);

	/* Fill remainder of fields for the request and copy to agreement */
	twt_data->params.req_type |= IEEE80211_TWT_REQTYPE_IMPLICIT;
	memcpy((agreement + TWT_AGREEMENT_REQUEST_TYPE_OFFSET),
	       &twt_data->params.req_type, sizeof(twt_data->params.req_type));
	agreement_len += sizeof(twt_data->params.req_type);

	/* Copy the target wake time. Currently twt value is always present */
	memcpy((agreement + TWT_AGREEMENT_TARGET_WAKE_TIME_OFFSET),
	       &twt_data->params.twt, sizeof(twt_data->params.twt));
	agreement_len += sizeof(twt_data->params.twt);

	return agreement_len + sizeof(twt_data->control) + sizeof(twt_data->params.channel);
}

int morse_process_twt_cmd(struct morse *mors, struct morse_vif *mors_vif, struct morse_cmd *cmd)
{
	struct command_twt_req *cmd_twt = (struct command_twt_req *)cmd;

	if (!mors_vif)
		return -EFAULT;

	if (mors_vif->id != cmd_twt->hdr.vif_id)
		return -EINVAL;

	if (cmd_twt->cmd == TWT_CONF_SUBCMD_CONFIGURE ||
	    cmd_twt->cmd == TWT_CONF_SUBCMD_CONFIGURE_EXPLICIT ||
	    cmd_twt->cmd == TWT_CONF_SUBCMD_FORCE_INSTALL_AGREEMENT)
		return morse_twt_process_set_cmd(mors, mors_vif, cmd_twt);
	else if (cmd_twt->cmd == TWT_CONF_SUBCMD_REMOVE_AGREEMENT)
		return morse_twt_process_remove_cmd(mors, mors_vif, cmd_twt);
	return -EFAULT;
}
