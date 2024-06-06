/*
 * Copyright 2022-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "morse.h"
#include "debug.h"
#include "command.h"
#include "skbq.h"
#include "mac.h"
#include "monitor.h"
#include "skb_header.h"
#include "offload.h"
#include "vendor.h"
#include "ocs.h"
#include "wiphy.h"

int morse_mac_event_recv(struct morse *mors, struct sk_buff *skb)
{
	int ret;

	struct morse_event *event = (struct morse_event *)(skb->data);
	u16 event_id = le16_to_cpu(event->hdr.message_id);
	u16 event_iid = le16_to_cpu(event->hdr.host_id);
	u16 event_len = le16_to_cpu(event->hdr.len);

	lockdep_assert_held(&mors->lock);

	if (!MORSE_CMD_IS_EVT(event)) {
		ret = -EINVAL;
		goto exit;
	}

	/* For events, iid must be set to 0 */
	if (event_iid != 0) {
		ret = -EINVAL;
		goto exit;
	}

	MORSE_DBG(mors, "EVT 0x%04x LEN %u\n", event_id, event_len);

	switch (event_id) {
	case MORSE_COMMAND_EVT_STA_STATE:
		{
			struct morse_evt_sta_state *sta_state_evt =
			    (struct morse_evt_sta_state *)event;

			MORSE_DBG(mors, "State change event: addr %pM, aid %u, state %u\n",
				  sta_state_evt->addr, sta_state_evt->aid, sta_state_evt->state);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_BEACON_LOSS:
		{
			struct morse_evt_beacon_loss *bcn_loss_evt =
			    (struct morse_evt_beacon_loss *)event;
			struct ieee80211_vif *vif =
				morse_get_vif_from_vif_id(mors, bcn_loss_evt->hdr.vif_id);

			if (vif)
				ieee80211_beacon_loss(vif);

			MORSE_DBG(mors, "Beacon loss event: number of beacons %u, vif id %u\n",
				  bcn_loss_evt->num_bcns, bcn_loss_evt->hdr.vif_id);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_SIG_FIELD_ERROR:
		{
			struct morse_evt_sig_field_error_evt *sig_field_error_evt =
			    (struct morse_evt_sig_field_error_evt *)event;

#ifdef CONFIG_MORSE_MONITOR
			if (mors->hw->conf.flags & IEEE80211_CONF_MONITOR)
				morse_mon_sig_field_error(sig_field_error_evt);
#endif

			MORSE_DBG(mors, "Sig field error %llu - %llu\n",
				  sig_field_error_evt->start_timestamp,
				  sig_field_error_evt->end_timestamp);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_UMAC_TRAFFIC_CONTROL:
		{
			/* Event size from older firmware revisions */
			const int legacy_evt_len = 3;
			struct morse_evt_umac_traffic_control *umac_traffic_control =
			    (struct morse_evt_umac_traffic_control *)event;
			int sources = UMAC_TRAFFIC_CONTROL_SOURCE_TWT;

			if (event->hdr.len > legacy_evt_len)
				/* This event includes the source bitfield (potentially
				 * more sources than just twt).
				 */
				sources = le32_to_cpu(umac_traffic_control->sources);

			ret = morse_mac_traffic_control(mors,
						le16_to_cpu(umac_traffic_control->hdr.vif_id),
						umac_traffic_control->pause_data_traffic,
						sources);
			break;
		}
	case MORSE_COMMAND_EVT_DHCP_LEASE_UPDATE:
		{
			struct morse_evt_dhcp_lease_update *dhcp_lease_update =
			    (struct morse_evt_dhcp_lease_update *)event;

			if (mors->custom_configs.enable_dhcpc_offload)
				ret = morse_offload_dhcpc_set_address(mors, dhcp_lease_update);
			else
				ret = 0;
			break;
		}
	case MORSE_COMMAND_EVT_OCS_DONE:
		{
			ret = morse_evt_ocs_done(mors, event);
			break;
		}
	default:
		ret = -EINVAL;
	}

exit:
	return ret;
}
