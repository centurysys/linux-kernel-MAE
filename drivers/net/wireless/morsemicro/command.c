/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "debug.h"
#include "command.h"
#include "skbq.h"
#include "mac.h"
#include "skb_header.h"
#include "watchdog.h"
#include "ps.h"
#include "raw.h"
#include "twt.h"
#include "operations.h"
#include "vendor_ie.h"

#define MM_BA_TIMEOUT (5000)
#define MM_MAX_COMMAND_RETRY 2

/*
 * These timeouts (in msecs) must be kept in sync with the same definitions in the driver.
 */
#define MM_CMD_TIMEOUT_DEFAULT 600
#define MM_CMD_TIMEOUT_PS 2000
#define MM_CMD_TIMEOUT_HEALTH_CHECK 2000

enum morse_interface_type {
	MORSE_INTERFACE_TYPE_INVALID = 0,
	MORSE_INTERFACE_TYPE_STA = 1,
	MORSE_INTERFACE_TYPE_AP = 2,
	MORSE_INTERFACE_TYPE_MON = 3,
	MORSE_INTERFACE_TYPE_ADHOC = 4,

	MORSE_INTERFACE_TYPE_LAST = MORSE_INTERFACE_TYPE_ADHOC,
	MORSE_INTERFACE_TYPE_MAX = INT_MAX,
};

struct morse_cmd_resp_cb {
	int ret;
	u32 length;
	struct morse_resp *dest_resp;
};

static int morse_cmd_tx(struct morse *mors, struct morse_resp *resp,
			struct morse_cmd *cmd, u32 length,
			u32 timeout, const char *func)
{
	int cmd_len, ret = 0;
	uint16_t iid;
	int retry = 0;
	unsigned long wait_ret = 0;
	struct sk_buff *skb;
	struct morse_skbq *cmd_q = mors->cfg->ops->skbq_cmd_tc_q(mors);
	struct morse_cmd_resp_cb *resp_cb;
	DECLARE_COMPLETION_ONSTACK(cmd_comp);

	BUILD_BUG_ON(sizeof(struct morse_cmd_resp_cb) > IEEE80211_TX_INFO_DRIVER_DATA_SIZE);

	if (!cmd_q)
		/* No control pageset, not supported by FW */
		return -EPERM;

	cmd_len = sizeof(*cmd) + le16_to_cpu(cmd->hdr.len);
	cmd->hdr.flags = cpu_to_le16(MORSE_CMD_REQ);

	mutex_lock(&mors->cmd_wait);
	mors->cmd_seq++;
	if (mors->cmd_seq > MORSE_CMD_IID_SEQ_MAX)
		mors->cmd_seq = 1;
	iid = mors->cmd_seq << MORSE_CMD_IID_SEQ_SHIFT;

	/* Make sure no one enables PS until the command is responded to or timed out */
	morse_ps_disable(mors);

	do {
		cmd->hdr.iid = cpu_to_le16(iid | retry);

		skb = morse_skbq_alloc_skb(cmd_q, cmd_len);
		if (!skb) {
			ret = -ENOMEM;
			break;
		}

		memcpy(skb->data, cmd, cmd_len);
		resp_cb = (struct morse_cmd_resp_cb *)IEEE80211_SKB_CB(skb)->driver_data;
		resp_cb->length = length;
		resp_cb->dest_resp = resp;

		morse_dbg(mors, "CMD 0x%04x:%04x\n", le16_to_cpu(cmd->hdr.id),
			 le16_to_cpu(cmd->hdr.iid));

		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = &cmd_comp;
		if (retry > 0)
			reinit_completion(&cmd_comp);
		timeout = timeout ? timeout : MM_CMD_TIMEOUT_DEFAULT;
		ret = morse_skbq_skb_tx(cmd_q, skb, NULL, MORSE_SKB_CHAN_COMMAND);
		mutex_unlock(&mors->cmd_lock);

		if (ret) {
			morse_err(mors, "morse_skbq_tx fail: %d\n", ret);
			break;
		}

		wait_ret = wait_for_completion_timeout(&cmd_comp,
							   msecs_to_jiffies(timeout));
		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = NULL;

		if (!wait_ret) {
			morse_info(mors, "Try:%d Command %04x:%04x timeout after %u ms\n",
				  retry, le16_to_cpu(cmd->hdr.id),
				  le16_to_cpu(cmd->hdr.iid), timeout);
			ret = -ETIMEDOUT;
		} else {
			ret = (length && resp) ? resp->status :
						resp_cb->ret;

			morse_dbg(mors, "Command 0x%04x:%04x status 0x%08x\n",
				  le16_to_cpu(cmd->hdr.id), le16_to_cpu(cmd->hdr.iid), ret);
			if (ret)
				morse_err(mors, "Command 0x%04x:%04x error %d\n",
					  le16_to_cpu(cmd->hdr.id),
					  le16_to_cpu(cmd->hdr.iid), ret);
		}
		/* Free the command request */
		spin_lock_bh(&cmd_q->lock);
		morse_skbq_skb_finish(cmd_q, skb, NULL);
		spin_unlock_bh(&cmd_q->lock);
		mutex_unlock(&mors->cmd_lock);

		retry++;
	} while ((ret == -ETIMEDOUT) && retry < MM_MAX_COMMAND_RETRY);

	morse_ps_enable(mors);
	mutex_unlock(&mors->cmd_wait);

	if (ret == -ETIMEDOUT)
		morse_err(mors, "Command %s %02x:%02x timed out\n",
			func, le16_to_cpu(cmd->hdr.id), le16_to_cpu(cmd->hdr.iid));
	else if (ret != 0)
		morse_err(mors, "Command %s %02x:%02x failed with rc %d (0x%x)\n",
			func, le16_to_cpu(cmd->hdr.id), le16_to_cpu(cmd->hdr.iid), ret, ret);

	return ret;
}

/**
 * morse_cmd_send_wake_action_frame() - Execute command to send wake action frame
 *
 * @mors	Morse chip struct
 * @cmd		Command from morsectrl
 */
static int morse_cmd_send_wake_action_frame(struct morse *mors,
	struct morse_cmd *cmd)
{
	struct morse_cmd_send_wake_action_frame *cmd_action =
		(struct morse_cmd_send_wake_action_frame *)cmd;
	return morse_mac_send_vendor_wake_action_frame(mors, cmd_action->dest_addr,
		cmd_action->payload, cmd_action->payload_size);
}

static int morse_cmd_drv(struct morse *mors, struct morse_resp *resp,
			struct morse_cmd *cmd, u32 length,
			u32 timeout)
{
	int ret;
	struct morse_vif *mors_if = (struct morse_vif *)morse_get_vif(mors)->drv_priv;

	switch (cmd->hdr.id) {
	case MORSE_COMMAND_SET_STA_TYPE:
		if (mors_if != NULL) {
			mors->custom_configs.sta_type = cmd->data[0];
			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;

	case MORSE_COMMAND_SET_ENC_MODE:
		if (mors_if != NULL) {
			mors->custom_configs.enc_mode = cmd->data[0];
			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;

	case MORSE_COMMAND_SET_LISTEN_INTERVAL:
		if (mors_if != NULL) {
			struct morse_cmd_set_listen_interval *cmd_li =
				(struct morse_cmd_set_listen_interval *)cmd;

			mors->custom_configs.listen_interval =
					le16_to_cpu(cmd_li->listen_interval);

			mors->custom_configs.listen_interval_ovr = true;

			morse_dbg(mors, "Listen Interval %d\n",
					  mors->custom_configs.listen_interval);

			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;

	case MORSE_COMMAND_SET_AMPDU:
		mors->custom_configs.enable_ampdu = (cmd->data[0] == 0) ? false : true;
		ret = 0;

		resp->hdr.len = 4;
		resp->status = ret;
		break;

	case MORSE_COMMAND_SET_RAW:
		if (mors_if != NULL) {
			struct morse_cmd_raw *cmd_raw = (struct morse_cmd_raw *)cmd;

			morse_raw_process_cmd(mors, cmd_raw);

			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;

	case MORSE_COMMAND_TEST_BA:
		if (mors_if != NULL) {
			struct ieee80211_sta *sta;
			struct morse_cmd_test_ba *test_ba =
				(struct morse_cmd_test_ba *)cmd;

			ret = -EINVAL;
			if (test_ba->tx) {
				/* Need the RCU lock to find a station,
				 * and must hold
				 * it until we're done with sta
				 */
				rcu_read_lock();
				sta = ieee80211_find_sta(morse_get_vif(mors),
							 test_ba->addr);
				if (sta == NULL) {
					rcu_read_unlock();
					break;
				}

				if (test_ba->start)
					ret = ieee80211_start_tx_ba_session(sta,
						test_ba->tid, MM_BA_TIMEOUT);
				else
					ret = ieee80211_stop_tx_ba_session(sta,
						test_ba->tid);
				rcu_read_unlock();
			} else {
				if (test_ba->start)
					break;
				ieee80211_stop_rx_ba_session(morse_get_vif(mors),
								 test_ba->tid,
								 test_ba->addr);
				ret = 0;
			}
		} else
			ret = -EFAULT;
		break;

	case MORSE_COMMAND_COREDUMP:
		ret = morse_coredump(mors);

		resp->hdr.len = 4;
		resp->status = ret;
		break;

	case MORSE_COMMAND_SET_S1G_OP_CLASS:
		mors->custom_configs.channel_info.s1g_operating_class = cmd->data[0];
		mors->custom_configs.channel_info.pri_global_operating_class = cmd->data[1];
		ret = 0;

		resp->hdr.len = 4;
		resp->status = ret;
		break;

	case MORSE_COMMAND_SEND_WAKE_ACTION_FRAME:
		ret = morse_cmd_send_wake_action_frame(mors, cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;

	case MORSE_COMMAND_VENDOR_IE_CONFIG:
	{
		ret = morse_vendor_ie_handle_config_cmd(mors_if, (struct morse_cmd_vendor_ie_config *) cmd);

		resp->hdr.len = 4;
		resp->status = ret;
		break;
	}

	case MORSE_COMMAND_TWT_SET_CONF:
		ret = morse_process_twt_cmd(mors, mors_if, cmd);

		resp->hdr.len = 4;
		resp->status = ret;
		break;

	case MORSE_COMMAND_GET_AVAILABLE_CHANNELS:
	{
		ret = morse_cmd_get_available_channels(mors, resp);

		resp->status = ret;
		break;
	}
	case MORSE_COMMAND_SET_ECSA_S1G_INFO:
	{
		if (mors_if != NULL) {
			struct morse_cmd_ecsa *cmd_ecsa = (struct morse_cmd_ecsa *)cmd;

			morse_info(mors, "ECSA channel info   :\n"
				" * s1g_global_operating_class    : %d\n"
				" * s1g_primary_bandwidth         : %d\n"
				" * s1g_operating_frequency       : %u\n"
				" * s1g_operating_bandwidth       : %d\n"
				" * s1g_primary_1MHz_chan_idx     : %d\n"
				" * primary_global_op_class       : %d\n",
				cmd_ecsa->op_class,
				cmd_ecsa->prim_bw,
				cmd_ecsa->op_chan_freq_hz,
				cmd_ecsa->op_bw_mhz,
				cmd_ecsa->prim_chan_1MHz_idx,
				cmd_ecsa->prim_opclass);

			mors_if->ecsa_channel_info.op_chan_freq_hz = cmd_ecsa->op_chan_freq_hz;
			mors_if->ecsa_channel_info.op_bw_mhz = cmd_ecsa->op_bw_mhz;
			mors_if->ecsa_channel_info.pri_1mhz_chan_idx = cmd_ecsa->prim_chan_1MHz_idx;
			mors_if->ecsa_channel_info.pri_bw_mhz = cmd_ecsa->prim_bw;
			mors_if->ecsa_channel_info.s1g_operating_class = cmd_ecsa->op_class;
			mors_if->ecsa_channel_info.pri_global_operating_class = cmd_ecsa->prim_opclass;
			mors_if->mask_ecsa_info_in_beacon = false;
			ret = 0;
		} else {
			ret = -EFAULT;
		}
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	}

	default:
		ret = -EINVAL;
	}
	return ret;
}

int morse_cmd_resp_process(struct morse *mors, struct sk_buff *skb, u8 channel)
{
	int length, ret = -ESRCH;	/* No such process */
	struct morse_skbq *cmd_q = mors->cfg->ops->skbq_cmd_tc_q(mors);
	struct morse_resp *src_resp = (struct morse_resp *)(skb->data);
	struct sk_buff *cmd_skb = NULL;
	struct morse_cmd_resp_cb *resp_cb;
	struct morse_resp *dest_resp;
	struct morse_cmd *cmd;
	u16 cmd_id = 0;
	u16 cmd_iid = 0;
	u16 resp_id = le16_to_cpu(src_resp->hdr.id);
	u16 resp_iid = le16_to_cpu(src_resp->hdr.iid);
	bool is_late_response = false;

	morse_dbg(mors, "EVT 0x%04x:0x%04x\n", resp_id, resp_iid);

	mutex_lock(&mors->cmd_lock);

	if (!MORSE_CMD_IS_CFM(src_resp)) {
		ret = morse_mac_event_recv(mors, skb);
		goto exit;
	}

	cmd_skb = morse_skbq_tx_pending(cmd_q);
	if (cmd_skb) {
		cmd = (struct morse_cmd *) (cmd_skb->data + sizeof(struct morse_buff_skb_header));
		cmd_id = le16_to_cpu(cmd->hdr.id);
		cmd_iid = le16_to_cpu(cmd->hdr.iid);
	}

	/*
	 * If there is no pending command or the sequence ID does not match, this is a late response
	 * for a timed out command which has been cleaned up, so just free up the response.
	 * If a command was retried, the response may be from the retry or from the original
	 * command (late response) but not from both because the firmware will silently drop
	 * a retry if it received the initial request. So a mismatched retry counter is treated
	 * as a matched command and response.
	 */
	if (!cmd_skb
		|| (cmd_id != resp_id)
		|| ((cmd_iid & MORSE_CMD_IID_SEQ_MASK) != (resp_iid & MORSE_CMD_IID_SEQ_MASK))) {
		morse_err(mors,
			"Late response for timed out cmd 0x%04x:%04x have 0x%04x:%04x 0x%04x\n",
			resp_id, resp_iid, cmd_id, cmd_iid, mors->cmd_seq);
		is_late_response = true;
		goto exit;
	}
	if ((cmd_iid & MORSE_CMD_IID_RETRY_MASK) != (resp_iid & MORSE_CMD_IID_RETRY_MASK))
		morse_info(mors, "Command retry mismatch 0x%04x:%04x 0x%04x:%04x\n",
			cmd_id, cmd_iid, resp_id, resp_iid);

	resp_cb = (struct morse_cmd_resp_cb *)IEEE80211_SKB_CB(cmd_skb)->driver_data;
	length = resp_cb->length;
	dest_resp = resp_cb->dest_resp;
	if ((length >= sizeof(struct morse_resp)) && dest_resp) {
		ret = 0;
		length = min_t(int, length, le16_to_cpu(src_resp->hdr.len) +
					sizeof(struct morse_cmd_header));
		memcpy(dest_resp, src_resp, length);
	} else {
		ret = le32_to_cpu(src_resp->status);
	}

	resp_cb->ret = ret;

exit:
	if (cmd_skb && !is_late_response) {
		/* Complete if not already timed out */
		if (mors->cmd_comp)
			complete(mors->cmd_comp);
	}

	mutex_unlock(&mors->cmd_lock);

	/* The firmware is still responsive */
	morse_watchdog_refresh(mors);

	dev_kfree_skb(skb);

	return 0;
}

int morse_cmd_set_channel(struct morse *mors, u32 op_chan_freq_hz,
			u8 pri_1mhz_chan_idx, u8 op_bw_mhz, u8 pri_bw_mhz)
{
	int ret;
	struct morse_cmd_set_channel cmd;
	struct morse_resp_set_channel resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_CHANNEL);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	/* May be 0xFFFF/0xFFFFFFFF to indicate no change */
	cmd.op_chan_freq_hz = cpu_to_le32(op_chan_freq_hz);
	cmd.op_bw_mhz = op_bw_mhz;
	cmd.pri_bw_mhz = pri_bw_mhz;
	cmd.pri_1mhz_chan_idx = pri_1mhz_chan_idx;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);
	if (ret == 0)
		morse_info(mors,
			"%s channel change f:%d, o:%d, p:%d, i:%d\n",
			__func__, cmd.op_chan_freq_hz, cmd.op_bw_mhz,
			cmd.pri_bw_mhz, cmd.pri_1mhz_chan_idx);

	return ret;
}

int morse_cmd_set_txpower(struct morse *mors, s32 *out_power, int txpower)
{
	int ret;
	struct morse_cmd_set_txpower cmd;
	struct morse_resp_set_txpower resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_TXPOWER);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd.power_level));
	cmd.power_level = cpu_to_le32(txpower);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);
	if (ret == 0)
		*out_power = le32_to_cpu(resp.power_level);

	return ret;
}

int morse_cmd_get_max_txpower(struct morse *mors, s32 *out_power)
{
	int ret;
	struct morse_cmd_get_max_txpower cmd;
	struct morse_resp_get_max_txpower resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_GET_MAX_TXPOWER);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);
	if (ret == 0)
		*out_power = le32_to_cpu(resp.power_level);

	return ret;
}

int morse_cmd_set_ps(struct morse *mors, bool enabled,
		     bool enable_dynamic_ps_offload)
{
	int ret;
	struct morse_cmd_set_ps cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_PS);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.enabled = (u8)enabled;
	cmd.dynamic_ps_offload = (u8) enable_dynamic_ps_offload;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, MM_CMD_TIMEOUT_PS, __func__);

	return ret;
}

int morse_cmd_add_if(struct morse *mors, u16 *id, u8 *addr, u32 type)
{
	int ret;
	struct morse_cmd_add_if cmd;
	struct morse_resp_add_if resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_ADD_INTERFACE);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	switch (type) {
	case NL80211_IFTYPE_STATION:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_STA);
		break;
	case NL80211_IFTYPE_ADHOC:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_ADHOC);
		break;
	case NL80211_IFTYPE_AP:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_AP);
		break;
	case NL80211_IFTYPE_MONITOR:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_MON);
		break;
	default:
		return -EOPNOTSUPP;
	}

	memcpy(cmd.addr, addr, sizeof(cmd.addr));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);
	if (ret == 0)
		*id = le16_to_cpu(resp.id);

	return ret;
}

int morse_cmd_rm_if(struct morse *mors, u16 id)
{
	int ret;
	struct morse_cmd_rm_if cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_REMOVE_INTERFACE);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd.id));
	cmd.id = cpu_to_le16(id);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_cfg_bss(struct morse *mors, u16 id, u16 beacon_int, u16 dtim_period, u32 cssid)
{
	int ret;
	struct morse_cmd_cfg_bss cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_BSS_CONFIG);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.id = cpu_to_le16(id);
	cmd.beacon_int = cpu_to_le16(beacon_int);
	cmd.cssid = cpu_to_le32(cssid);
	cmd.dtim_period = cpu_to_le16(dtim_period);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}


int morse_cmd_sta_state(struct morse *mors, struct morse_vif *vif,
			u16 aid, struct ieee80211_sta *sta,
			enum ieee80211_sta_state state)
{
	int ret;
	struct morse_cmd_sta_state cmd;
	struct morse_resp_sta_state resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_STA_STATE);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.vif_id = cpu_to_le16(vif->id);
	memcpy(cmd.addr, sta->addr, sizeof(cmd.addr));
	cmd.aid = cpu_to_le16(aid);
	cmd.state = cpu_to_le16(state);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			(struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_disable_key(struct morse *mors, struct morse_vif *vif,
	u16 aid, struct ieee80211_key_conf *key)
{
	int ret;
	struct morse_cmd_disable_key cmd;

	morse_dbg(mors, "%s Disabling key for vif (%d):\n"
		"\tkey->hw_key_idx: %d\n"
		"\taid (optional): %d\n",
		__func__,
		vif->id,
		key->hw_key_idx,
		aid);

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_DISABLE_KEY);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.aid = cpu_to_le16(aid);
	cmd.vif_id = cpu_to_le16(vif->id);
	cmd.key_idx = key->hw_key_idx;
	cmd.key_type = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
		MORSE_KEY_TYPE_PTK : MORSE_KEY_TYPE_GTK;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_install_key(struct morse *mors, struct morse_vif *vif,
	u16 aid, struct ieee80211_key_conf *key, enum morse_key_cipher cipher,
	enum morse_aes_key_length length)
{
	int ret;
	struct morse_cmd_install_key cmd;
	struct morse_resp_install_key resp;

	morse_dbg(mors, "%s Installing key for vif (%d):\n"
		"\tkey->idx: %d\n"
		"\tkey->cipher: 0x%08x\n"
		"\tkey->pn: %lld\n"
		"\tkey->len: %d\n"
		"\tkey->flags: 0x%08x\n"
		"\taid (optional): %d\n",
		__func__,
		vif->id,
		key->keyidx,
		key->cipher,
		(u64)atomic64_read(&key->tx_pn),
		key->keylen,
		key->flags,
		aid);

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_INSTALL_KEY);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.pn = cpu_to_le64(atomic64_read(&key->tx_pn));
	cmd.aid = cpu_to_le32(aid);
	cmd.cipher = cipher;
	cmd.key_length = length;
	cmd.key_type = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
		MORSE_KEY_TYPE_PTK : MORSE_KEY_TYPE_GTK;

	cmd.vif_id = cpu_to_le16(vif->id);
	cmd.key_idx = key->keyidx;
	memcpy(&cmd.key[0], &key->key[0], sizeof(cmd.key));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
		(struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	if (ret == 0) {
		key->hw_key_idx = resp.key_idx;
		morse_dbg(mors, "%s Installed key @ hw index: %d\n",
			__func__, resp.key_idx);
	}

	return ret;
}


int morse_cmd_get_version(struct morse *mors)
{
	int ret;
	struct morse_cmd_get_version cmd;
	struct morse_resp_get_version *resp = NULL;

	memset(&mors->sw_ver, 0, sizeof(mors->sw_ver));

	/* we have to kmalloc otherwise we are bigger than stack allows */
	resp = kmalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return -ENOSPC;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_GET_VERSION);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
			  (struct morse_cmd *)&cmd, sizeof(*resp),
			  0, __func__);
	if (ret == 0) {
		int major;
		int minor;
		int patch;

		resp->version[resp->length] = '\0';
		morse_info(mors,
			"Morse Driver Version: %s, Morse FW Version: %s\n",
			DRV_VERSION, resp->version);

		if (sscanf(resp->version, "rel_%d_%d_%d", &major, &minor, &patch) == 3) {
			BUG_ON(major > __UINT8_MAX__);
			BUG_ON(minor > __UINT8_MAX__);
			BUG_ON(patch > __UINT8_MAX__);
			mors->sw_ver.major = major;
			mors->sw_ver.minor = minor;
			mors->sw_ver.patch = patch;
		}
	}

	kfree(resp);

	return ret;
}

int morse_cmd_cfg_scan(struct morse *mors, bool enabled)
{
	int ret;
	struct morse_cmd_cfg_scan cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_CFG_SCAN);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.enabled = enabled;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_survey_channel(struct morse *mors, struct morse_channel_survey *survey, u32 frequency)
{
	int ret;
	struct morse_cmd_survey_channel cmd;
	struct morse_resp_survey_channel resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SURVEY_CHANNEL);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.frequency = frequency;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret == 0) {
		survey->noise = (s8)le32_to_cpu(resp.iq_power);
		survey->time_listen = le64_to_cpu(resp.time_listen);
		survey->time_rx = le64_to_cpu(resp.busy_time);
	}

	return ret;
}

static void morse_set_dtim_cts_to_self(bool enable, struct morse_vif *vif)
{
	if (enable)
		MORSE_OPS_SET(&vif->operations, DTIM_CTS_TO_SELF);
	else
		MORSE_OPS_CLEAR(&vif->operations, DTIM_CTS_TO_SELF);
}


int morse_cmd_update_beacon_vendor_ie_oui_filter(struct morse *mors, struct morse_vif *mors_if)
{
	struct morse_config_oui_filter_req req;
	struct vendor_ie_oui_filter_list_item *item;
	u8 idx = 0;

	req.hdr.id = cpu_to_le16(MORSE_COMMAND_UPDATE_OUI_FILTER);
	req.hdr.len = cpu_to_le16(sizeof(req) - sizeof(req.hdr));

	req.vif_id = cpu_to_le16(mors_if->id);

	spin_lock_bh(&mors_if->vendor_ie.lock);
	list_for_each_entry(item, &mors_if->vendor_ie.oui_filter_list, list) {
		if (item->mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON)
			memcpy(req.ouis[idx++], item->oui, sizeof(req.ouis[idx]));

		if (idx >= ARRAY_SIZE(req.ouis))
			break;
	}
	spin_unlock_bh(&mors_if->vendor_ie.lock);

	req.n_ouis = idx;

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&req, 0, 0, __func__);
}

int morse_cmd_vendor(struct morse *mors, void *data_in, int data_in_len,
			 void *data_out, int *data_out_len)
{
	int ret;
	struct morse_resp_vendor *resp =
				(struct morse_resp_vendor *)data_out;
	const struct morse_cmd_vendor *cmd =
				(const struct morse_cmd_vendor *)data_in;

	resp->hdr.id = cmd->hdr.id;

	if ((cmd->hdr.id >= MORSE_COMMAND_DRIVER_START)
		&& (cmd->hdr.id <= MORSE_COMMAND_DRIVER_END)) {
		ret = morse_cmd_drv(mors, (struct morse_resp *)resp,
				(struct morse_cmd *)cmd, sizeof(*resp), 0);
		if (ret)
			morse_err(mors, "%s error %d\n", __func__, ret);
	} else {
		ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
				(struct morse_cmd *)cmd, sizeof(*resp), 0, __func__);
	}
	if (ret) {
		resp->hdr.iid = cmd->hdr.iid;
		resp->status = ret;
		*data_out_len = sizeof(struct morse_resp);
		goto exit;
	}
	*data_out_len = resp->hdr.len + sizeof(struct morse_cmd_header);

	/** Commands that were successful and need to be post processed */

	switch (cmd->hdr.id) {
	case MORSE_COMMAND_SET_CHANNEL:
	{
		struct morse_channel_info *stored_info =
			&mors->custom_configs.default_bw_info;
		struct morse_cmd_set_channel *channel_cmd =
				(struct morse_cmd_set_channel *) cmd;

		if (channel_cmd->op_chan_freq_hz != DEFAULT_FREQUENCY)
			stored_info->op_chan_freq_hz =
				channel_cmd->op_chan_freq_hz;

		if (channel_cmd->pri_1mhz_chan_idx
			!= DEFAULT_1MHZ_PRIMARY_CHANNEL_INDEX)
			stored_info->pri_1mhz_chan_idx =
				channel_cmd->pri_1mhz_chan_idx;

		if (channel_cmd->op_bw_mhz != DEFAULT_BANDWIDTH)
			stored_info->op_bw_mhz =
				channel_cmd->op_bw_mhz;

		if (channel_cmd->pri_bw_mhz != DEFAULT_BANDWIDTH)
			stored_info->pri_bw_mhz =
				channel_cmd->pri_bw_mhz;

		/* Validate that primary does not exceed operating */
		stored_info->pri_bw_mhz =
			(stored_info->op_bw_mhz == 1) ?
			1 : stored_info->pri_bw_mhz;

		morse_info(mors,
			"%s channel change f:%d, o:%d, p:%d, i:%d\n",
			__func__, channel_cmd->op_chan_freq_hz, channel_cmd->op_bw_mhz,
			channel_cmd->pri_bw_mhz, channel_cmd->pri_1mhz_chan_idx);

	}
	break;
	case MORSE_COMMAND_SET_CONTROL_RESPONSE:
	{
		struct morse_cmd_cr_bw *cr_cmd = (struct morse_cmd_cr_bw *) cmd;
		struct morse_vif *vif;

		if (cr_cmd->vif_id < MORSE_MAX_IF && mors->vif[cr_cmd->vif_id] != NULL) {
			vif = (struct morse_vif *)mors->vif[cr_cmd->vif_id]->drv_priv;
			if (cr_cmd->direction)
				vif->ctrl_resp_in_1mhz_en = cr_cmd->cr_1mhz_en;
			else
				vif->ctrl_resp_out_1mhz_en = cr_cmd->cr_1mhz_en;
		}
	}
	break;
	case MORSE_COMMAND_SET_BSS_COLOR:
	{
		struct morse_cmd_set_bss_color *color_cmd =
			(struct morse_cmd_set_bss_color *) cmd;
		struct morse_vif *vif;

		if (color_cmd->vif_id < MORSE_MAX_IF && mors->vif[color_cmd->vif_id] != NULL) {
			vif = (struct morse_vif *)mors->vif[color_cmd->vif_id]->drv_priv;
			vif->bss_color = color_cmd->color;
		}
	}
	break;
	case MORSE_COMMAND_SET_LONG_SLEEP_CONFIG:
	{
		struct morse_cmd_set_long_sleep_config *long_sleep_cmd =
			(struct morse_cmd_set_long_sleep_config *) cmd;

		if (long_sleep_cmd->enabled)
			(void)morse_watchdog_pause(mors);
		else
			(void)morse_watchdog_resume(mors);
	}
	break;
	case MORSE_COMMAND_SET_CTS_SELF_PS:
	{
		struct morse_cmd_cts_self_ps *cts_self_ps =
			(struct morse_cmd_cts_self_ps *) cmd;
		struct ieee80211_vif *vif = morse_get_ap_vif(mors);

		WARN_ON_ONCE(!vif);
		if (vif != NULL && vif->type == NL80211_IFTYPE_AP)
			morse_set_dtim_cts_to_self(cts_self_ps->enable,
					(struct morse_vif *)vif->drv_priv);
	}
	break;
	case MORSE_COMMAND_STANDBY_MODE:
	{
		struct morse_cmd_standby_mode_req *standby_mode =
			(struct morse_cmd_standby_mode_req *) cmd;

		if (standby_mode->cmd == STANDBY_MODE_CMD_ENTER)
			morse_watchdog_pause(mors);
		else if (standby_mode->cmd == STANDBY_MODE_CMD_EXIT)
			morse_watchdog_resume(mors);
	}
	break;
	}

exit:
	return ret;
}

/* Sets the control response frame bandwidth for the given vif */
int morse_cmd_set_cr_bw(struct morse *mors, struct morse_vif *vif,
			u8 direction, u8 cr_1mhz_en)
{
	int ret;
	struct morse_cmd_cr_bw cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_CONTROL_RESPONSE);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.vif_id = cpu_to_le16(vif->id);
	cmd.cr_1mhz_en = cr_1mhz_en;
	cmd.direction = direction;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_cfg_qos(struct morse *mors,  struct morse_queue_params *params)
{
	int ret;
	struct morse_cmd_cfg_qos cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_QOS_PARAMS);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.aci = params->aci;
	cmd.aifs = params->aifs;
	cmd.cw_min = cpu_to_le16(params->cw_min);
	cmd.cw_max = cpu_to_le16(params->cw_max);
	cmd.txop =  cpu_to_le32(params->txop);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_set_bss_color(struct morse *mors, struct morse_vif *vif, u8 color)
{
	int ret;
	struct morse_cmd_set_bss_color cmd;
	struct morse_resp_set_bss_color resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_BSS_COLOR);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.vif_id = cpu_to_le16(vif->id);
	cmd.color = color;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			(struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_health_check(struct morse *mors)
{
	int ret;
	struct morse_cmd_health_check cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_HEALTH_CHECK);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, MM_CMD_TIMEOUT_HEALTH_CHECK,
			__func__);

	return ret;
}

int morse_cmd_arp_offload_update_ip_table(struct morse *mors, uint16_t vif_id,
						int arp_addr_count, u32 *arp_addr_list)
{
	int ret = 0;
	int i;
	struct morse_cmd_arp_offload cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_ARP_OFFLOAD);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.vif_id = vif_id;

	memset(cmd.ip_table, 0, sizeof(cmd.ip_table));

	for (i = 0; i < arp_addr_count && i < ARRAY_SIZE(cmd.ip_table); i++)
		cmd.ip_table[i] = arp_addr_list[i];

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_get_capabilities(struct morse *mors,
			       uint16_t vif_id,
			       struct morse_caps *capabilities)
{
	int ret = 0;
	int i;
	struct morse_get_capabilities_req cmd;
	struct morse_get_capabilities_cfm rsp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_GET_CAPABILITIES);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.interface_id = vif_id;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&rsp,
			  (struct morse_cmd *)&cmd, sizeof(rsp), 0, __func__);
	if (ret != 0)
		return ret;

	capabilities->ampdu_mss = rsp.capabilities.ampdu_mss;
	capabilities->beamformee_sts_capability = rsp.capabilities.beamformee_sts_capability;
	capabilities->maximum_ampdu_length_exponent = rsp.capabilities.maximum_ampdu_length_exponent;
	capabilities->number_sounding_dimensions = rsp.capabilities.number_sounding_dimensions;
	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
		capabilities->flags[i] = le32_to_cpu(rsp.capabilities.flags[i]);

	return ret;
}

int morse_cmd_dhcpc_enable(struct morse *mors, u16 vif_id)
{
	int ret;
	struct morse_cmd_dhcpc_req cmd;
	struct morse_cmd_dhcpc_cfm resp;

	if (vif_id == (u16)-1)
		return -ENODEV;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_DHCP_OFFLOAD);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.opcode = cpu_to_le32(MORSE_DHCP_CMD_ENABLE);
	cmd.vif_id = cpu_to_le16(vif_id);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);

	if (ret == 0) {
		if (le32_to_cpu(resp.retcode) == MORSE_DHCP_RET_SUCCESS) {
			morse_info(mors, "In chip DHCP client enabled\n");
		} else if (le32_to_cpu(resp.retcode) == MORSE_DHCP_RET_ALREADY_ENABLED) {
			/* Client is already enabled, trigger a lease update. */
			morse_info(mors, "DHCP client already enabled, forcing lease update\n");
			cmd.opcode = cpu_to_le32(MORSE_DHCP_CMD_SEND_LEASE_UPDATE);
			ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
					(struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
		} else {
			morse_warn(mors, "Error enabling in-chip DHCP client %d\n", le32_to_cpu(resp.retcode));
		}
	}

	return ret;
}

static int morse_cmd_twt_agreement_req(struct morse *mors,
				       struct morse_twt_agreement_data *agreement,
				       u16 iface_id,
				       bool validate_only)
{
	int ret = 0;
	struct morse_cmd_install_twt_agreement_req *cmd;

	cmd = kmalloc(sizeof(*cmd) + TWT_MAX_AGREEMENT_LEN, GFP_KERNEL);
	if (validate_only)
		cmd->hdr.id = cpu_to_le16(MORSE_COMMAND_VALIDATE_TWT_AGREEMENT);
	else
		cmd->hdr.id = cpu_to_le16(MORSE_COMMAND_INSTALL_TWT_AGREEMENT);

	cmd->hdr.len = cpu_to_le16((sizeof(*cmd) + TWT_MAX_AGREEMENT_LEN) - sizeof(cmd->hdr));
	cmd->interface_id = iface_id;
	cmd->flow_id = (agreement->params.req_type & IEEE80211_TWT_REQTYPE_FLOWID) >>
		IEEE80211_TWT_REQTYPE_FLOWID_OFFSET;
	cmd->agreement_len = morse_twt_initialise_agreement(agreement, cmd->agreement);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)cmd, 0, 0, __func__);
	kfree(cmd);
	return ret;
}

int morse_cmd_twt_agreement_validate_req(struct morse *mors,
					 struct morse_twt_agreement_data *agreement,
					 u16 iface_id)
{
	return morse_cmd_twt_agreement_req(mors, agreement, iface_id, true);
}

int morse_cmd_twt_agreement_install_req(struct morse *mors,
					struct morse_twt_agreement_data *agreement,
					u16 iface_id)
{
	return morse_cmd_twt_agreement_req(mors, agreement, iface_id, false);
}

int morse_cmd_twt_remove_req(struct morse *mors,
			     struct morse_cmd_remove_twt_agreement *twt_remove_cmd)
{
	twt_remove_cmd->hdr.id = cpu_to_le16(MORSE_COMMAND_REMOVE_TWT_AGREEMENT);
	twt_remove_cmd->hdr.len = cpu_to_le16(sizeof(*twt_remove_cmd) - sizeof(twt_remove_cmd->hdr));
	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)twt_remove_cmd, 0, 0, __func__);
}

int morse_cmd_cfg_ibss(struct morse *mors, u16 id,
						const u8 *bssid, bool ibss_creator, bool stop_ibss)
{
	int ret;
	struct morse_cmd_cfg_ibss cmd;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_IBSS_CONFIG);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));
	cmd.vif_id = cpu_to_le16(id);

	/* If stop_ibss is set, other parameters are ignored and cleared in target */
	memcpy(cmd.ibss_bssid_addr, bssid, sizeof(cmd.ibss_bssid_addr));

	if (stop_ibss) {
		cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_STOP;
	} else {
		if (ibss_creator)
			cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_CREATE;
		else
			cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_JOIN;
	}

	cmd.ibss_probe_filtering = enable_ibss_probe_filtering;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}


int morse_cmd_set_duty_cycle(struct morse *mors, int duty_cycle, bool omit_ctrl_resp)
{
	int ret;
	struct morse_cmd_set_duty_cycle_req cmd;
	struct morse_cmd_set_duty_cycle_cfm resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_DUTY_CYCLE);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.duty_cycle = cpu_to_le32(duty_cycle);
	cmd.omit_ctrl_resp = omit_ctrl_resp ? 1 : 0;
	cmd.set_configs = MORSE_DUTY_CYCLE_SET_CFG_DUTY_CYCLE |
		MORSE_DUTY_CYCLE_SET_CFG_OMIT_CTRL_RESP;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);

	return ret;
}

int morse_cmd_set_mpsw(struct morse *mors, int min, int max, int window)
{
	int ret;
	struct morse_cmd_set_mpsw_config_req cmd;
	struct morse_cmd_set_mpsw_config_cfm resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_MPSW_CONFIG);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.config.airtime_max_us = cpu_to_le32(max);
	cmd.config.airtime_min_us = cpu_to_le32(min);
	cmd.config.packet_space_window_length_us = cpu_to_le32(window);
	cmd.config.enable = (max > 0 && min > 0);
	cmd.set_configs = MORSE_MPSW_SET_CFG_AIRTIME_BOUNDS |
		MORSE_MPSW_SET_CFG_PKT_SPACE_WINDOW_LEN |
		MORSE_MPSW_SET_CFG_ENABLED;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);

	return ret;
}

int morse_cmd_get_available_channels(struct morse *mors, struct morse_resp *resp)
{
	struct morse_cmd_get_available_channels_cfm *cfm =
			(struct morse_cmd_get_available_channels_cfm *) resp;
	int num_entries;

	num_entries = morse_dot11ah_fill_channel_list(cfm->channels);

	if (num_entries < 0) {
		resp->hdr.len = 4;
		return num_entries;
	}

	cfm->num_channels = num_entries;

	resp->hdr.len = sizeof(*cfm) - sizeof(cfm->hdr) +
			(num_entries * sizeof(cfm->channels[0]));

	return 0;
}

int morse_cmd_set_frag_threshold(struct morse *mors, u32 frag_threshold)
{
	int ret;
	struct morse_cmd_set_frag_threshold_req cmd;
	struct morse_cmd_set_frag_threshold_cfm resp;

	cmd.hdr.id = cpu_to_le16(MORSE_COMMAND_SET_FRAG_THRESHOLD);
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	cmd.frag_threshold = cpu_to_le32(frag_threshold);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			  (struct morse_cmd *)&cmd, sizeof(resp),
			  0, __func__);

	return ret;

}
