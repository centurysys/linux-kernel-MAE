/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <linux/interrupt.h>

#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "dot11ah/dot11ah.h"
#include "skb_header.h"

void morse_fill_tx_info(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct morse_vif *mors_if,
				int tx_bw_mhz)
{
	int bw_rate_flag_mhz =
		(tx_bw_mhz == 1) ? MORSE_SKB_RATE_FLAGS_1MHZ :
		(tx_bw_mhz == 2) ? MORSE_SKB_RATE_FLAGS_2MHZ :
		(tx_bw_mhz == 4) ? MORSE_SKB_RATE_FLAGS_4MHZ :
		(tx_bw_mhz == 8) ? MORSE_SKB_RATE_FLAGS_8MHZ :
		MORSE_SKB_RATE_FLAGS_2MHZ;

	(void) mors;
	(void) skb;

	tx_info->flags |=
		cpu_to_le32(MORSE_TX_CONF_FLAGS_VIF_ID_SET(mors_if->id));
	tx_info->rates[0].mcs = 0;
	tx_info->rates[0].count = 1;
	tx_info->rates[0].flags = cpu_to_le16(bw_rate_flag_mhz);
	mors->debug.mcs_stats_tbl.mcs0.tx_ndpprobes++;
	mors->debug.mcs_stats_tbl.mcs0.tx_success++;
	tx_info->rates[1].mcs = -1;
	tx_info->rates[1].count = 0;
	tx_info->rates[2].mcs = -1;
	tx_info->rates[2].count = 0;
	tx_info->rates[3].mcs = -1;
	tx_info->rates[3].count = 0;
}

static void morse_ndp_probe_req_resp_tasklet(unsigned long data)
{
	int ret;
	struct morse_skbq *mq;
	struct sk_buff *skb;
	struct ieee80211_mgmt *probe_resp;
	struct morse *mors = (struct morse *)data;
	struct ieee80211_vif *vif = morse_get_ap_vif(mors);
	struct morse_vif *mors_if;
	struct morse_skb_tx_info tx_info = {0};
	int tx_bw_mhz = 1;

	if ((!vif) || (vif->type != NL80211_IFTYPE_AP))
		return;

	mors_if = (struct morse_vif *)vif->drv_priv;

	skb = ieee80211_proberesp_get(mors->hw, vif);
	if (skb == NULL) {
		morse_err(mors,
			"%s: ieee80211_proberesp_get failed\n",
			__func__);
		return;
	}

	mq = mors->cfg->ops->skbq_mgmt_tc_q(mors);
	if (!mq) {
		morse_err(mors,
			"%s: mors->cfg->ops->skbq_mgmt_tc_q failed, no matching Q found\n",
			__func__);
		kfree_skb(skb);
		return;
	}

	probe_resp = (struct ieee80211_mgmt *) skb->data;

	/* Make it a broadcast probe request */
	eth_broadcast_addr(&probe_resp->da[0]);

	/* Convert the packet to s1g format */
	if (morse_mac_pkt_to_s1g(mors, skb, &tx_bw_mhz) < 0) {
		morse_dbg(mors, "Failed to convert ndp probe resp.. dropping\n");
		dev_kfree_skb_any(skb);
		return;
	}

	/* Always send back at 1mhz */
	morse_fill_tx_info(mors, &tx_info, skb, mors_if, 1);

	morse_dbg(mors,
		"Generated Probe Response for NDP probe request\n");
	ret = morse_skbq_skb_tx(mq, skb, &tx_info, MORSE_SKB_CHAN_MGMT);
	if (ret)
		morse_err(mors, "%s failed\n", __func__);
}

void morse_ndp_probe_req_resp_irq_handle(struct morse *mors)
{
	/* You are not safe, don't try to be smart */
	tasklet_schedule(&mors->ndp_probe_req_resp);
}

int morse_ndp_probe_req_resp_enable(struct morse *mors, bool enable)
{
	if (enable)
		tasklet_enable(&mors->ndp_probe_req_resp);
	else
		tasklet_disable(&mors->ndp_probe_req_resp);
	return 0;
}

int morse_ndp_probe_req_resp_init(struct morse *mors)
{
	morse_hw_irq_enable(mors, MORSE_INT_NDP_PROBE_REQ_PV0_NUM, true);
	tasklet_init(&mors->ndp_probe_req_resp,
		morse_ndp_probe_req_resp_tasklet,
		(unsigned long)mors);
	tasklet_disable(&mors->ndp_probe_req_resp);
	return 0;
}

void morse_ndp_probe_req_resp_finish(struct morse *mors)
{
	tasklet_kill(&mors->ndp_probe_req_resp);
}
