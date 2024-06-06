/*
 * Copyright 2017-2023 Morse Micro
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
			struct sk_buff *skb, struct morse_vif *mors_if, int tx_bw_mhz)
{
	enum dot11_bandwidth bw_idx = morse_ratecode_bw_mhz_to_bw_index(tx_bw_mhz);
	enum morse_rate_preamble pream = MORSE_RATE_PREAMBLE_S1G_SHORT;
	(void)mors;
	(void)skb;

	tx_info->flags |= cpu_to_le32(MORSE_TX_CONF_FLAGS_VIF_ID_SET(mors_if->id));
	morse_ratecode_mcs_index_set(&tx_info->rates[0].morse_ratecode, 0);
	morse_ratecode_nss_index_set(&tx_info->rates[0].morse_ratecode, NSS_TO_NSS_IDX(1));
	morse_ratecode_bw_index_set(&tx_info->rates[0].morse_ratecode, bw_idx);
	if (bw_idx == DOT11_BANDWIDTH_1MHZ)
		pream = MORSE_RATE_PREAMBLE_S1G_1M;
	morse_ratecode_preamble_set(&tx_info->rates[0].morse_ratecode, pream);
	tx_info->rates[0].count = 1;
	mors->debug.mcs_stats_tbl.mcs0.tx_ndpprobes++;
	mors->debug.mcs_stats_tbl.mcs0.tx_success++;
	tx_info->rates[1].count = 0;
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
	struct morse_skb_tx_info tx_info = { 0 };
	int tx_bw_mhz = 1;
	struct ieee80211_tx_info *info;

	if (!vif || vif->type != NL80211_IFTYPE_AP)
		return;

	mors_if = (struct morse_vif *)vif->drv_priv;

	skb = ieee80211_proberesp_get(mors->hw, vif);
	if (!skb) {
		MORSE_ERR(mors, "%s: ieee80211_proberesp_get failed\n", __func__);
		return;
	}

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;

	mq = mors->cfg->ops->skbq_mgmt_tc_q(mors);
	if (!mq) {
		MORSE_ERR(mors,
			  "%s: mors->cfg->ops->skbq_mgmt_tc_q failed, no matching Q found\n",
			  __func__);
		kfree_skb(skb);
		return;
	}

	probe_resp = (struct ieee80211_mgmt *)skb->data;

	/* Make it a broadcast probe request */
	eth_broadcast_addr(&probe_resp->da[0]);

	/* Convert the packet to s1g format */
	if (morse_mac_pkt_to_s1g(mors, &skb, &tx_bw_mhz) < 0) {
		MORSE_DBG(mors, "Failed to convert ndp probe resp.. dropping\n");
		dev_kfree_skb_any(skb);
		return;
	}

	/* Always send back at 1mhz */
	morse_fill_tx_info(mors, &tx_info, skb, mors_if, 1);

	MORSE_DBG(mors, "Generated Probe Response for NDP probe request\n");
	ret = morse_skbq_skb_tx(mq, &skb, &tx_info, MORSE_SKB_CHAN_MGMT);
	if (ret)
		MORSE_ERR(mors, "%s failed\n", __func__);
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
		     morse_ndp_probe_req_resp_tasklet, (unsigned long)mors);
	tasklet_disable(&mors->ndp_probe_req_resp);
	return 0;
}

void morse_ndp_probe_req_resp_finish(struct morse *mors)
{
	tasklet_kill(&mors->ndp_probe_req_resp);
}
