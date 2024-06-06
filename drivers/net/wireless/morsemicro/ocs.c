/*
 * Copyright 2023 Morse Micro
 */

#include "ocs.h"
#include "raw.h"
#include "vendor.h"
#include "debug.h"

#define MORSE_OCS_DURATION	(32767)
#define MORSE_OCS_RAW_IDX	(0)

/* Does needed post processing after sending the OCS command to the FW. For now, this function
 * configures OCS-specific RAW assignment if enabled
 */
int morse_ocs_cmd_post_process(struct morse *mors, const struct morse_resp_ocs *resp,
			       const struct morse_cmd_ocs *cmd)
{
	struct morse_raw *raw = &mors->custom_configs.raw;
	struct morse_raw_config *config = NULL;

	if (ocs_type != OCS_TYPE_RAW ||
	    cmd->cmd.subcmd != OCS_SUBCMD_CONFIG || le32_to_cpu(resp->status))
		return 0;

	if (!raw->configs[MORSE_OCS_RAW_IDX]) {
		config = kmalloc(sizeof(*config), GFP_KERNEL);
		if (!config)
			return -ENOMEM;

		config->type = IEEE80211_S1G_RPS_RAW_TYPE_GENERIC;
		config->enabled = true;
		config->start_time_us = 0;
		config->start_aid = cmd->aid;
		config->end_aid = config->start_aid;
		config->start_aid_idx = -1;
		config->end_aid_idx = -1;
		config->nominal_sta_per_beacon = 0;
		config->has_channel_indication = false;
		config->is_periodic = false;
		config->generic.paged_sta = false;
		config->generic.ra_frame = false;
		config->generic.group_same_as_prev = false;
		config->generic.cross_slot_boundary = false;
		config->generic.num_slots = 1;
		config->generic.slot_duration_us = MORSE_OCS_DURATION;
	}

	mutex_lock(&raw->lock);

	/* Check again with lock held */
	if (raw->configs[MORSE_OCS_RAW_IDX]) {
		raw->configs[MORSE_OCS_RAW_IDX]->enabled = true;
		kfree(config);
	} else {
		raw->configs[MORSE_OCS_RAW_IDX] = config;
	}

	/* Enable RAW */
	raw->enabled = true;

	mutex_unlock(&raw->lock);

	MORSE_DBG(mors, "OCS: Added RAW\n");

	/* Update RPS IE with new configuration. */
	schedule_work(&raw->refresh_aids_work);

	return 0;
}

int morse_evt_ocs_done(struct morse *mors, struct morse_event *event)
{
	struct morse_raw *raw = &mors->custom_configs.raw;
	struct ieee80211_vif *vif;
	int ret;

	if (ocs_type == OCS_TYPE_RAW) {
		BUG_ON(!raw->configs[MORSE_OCS_RAW_IDX]);

		mutex_lock(&raw->lock);
		raw->configs[MORSE_OCS_RAW_IDX]->enabled = false;
		mutex_unlock(&raw->lock);

		MORSE_DBG(mors, "OCS: Removed RAW\n");

		/* Update RPS IE with new configuration. */
		schedule_work(&raw->refresh_aids_work);
	}

	vif = morse_get_ap_vif(mors);
	ret = morse_vendor_send_ocs_done_event(vif, event);

	return ret;
}
