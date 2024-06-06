/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include "morse.h"
#include "debug.h"
#include "trace.h"
#include "mac.h"
#include "watchdog.h"
#include "bus.h"
#include "firmware.h"
#include "ipmon.h"
#include "vendor_ie.h"
#include "twt.h"
#include "linux/semaphore.h"
#include "linux/wait.h"
#include <linux/ratelimit.h>

/*
 * Array of configured LOG levels, indexed by the ID of the feature / module.
 * Initialised at run-time from the 'debug_mask' module parameter.
 */
static u8 log_mask[NUM_FEATURE_IDS];

/*
 * Mapping between feature name and ID. Used to populate debugFS.
 * The order must match the defintions in enum morse_feature_id!
 */
static const char * const morse_log_features[] = {
	[FEATURE_ID_DEFAULT] = "default",
	[FEATURE_ID_TWT] = "twt",
	[FEATURE_ID_RAW] = "raw",
	[FEATURE_ID_RATECONTROL] = "ratecontrol",
	[FEATURE_ID_SKB] = "skb",
	[FEATURE_ID_SDIO] = "sdio",
	[FEATURE_ID_PAGER] = "pager",
	[FEATURE_ID_POWERSAVE] = "powersave",
	[FEATURE_ID_MESH] = "mesh",
	[FEATURE_ID_ECSA] = "ecsa",
	[FEATURE_ID_CAC] = "cac",
	[FEATURE_ID_SPI] = "spi",
};

/*
 * Generator macro for the various logging functions.
 *
 * Only call kernel logging function if the required verbosity level is enabled. However,
 * always call the matching trace_morse_xyz() function as this is much less expensive and
 * is already filtered by the Linux kernel trace mechanisms.
 *
 * Note: %pV is used for printing a struct va_format structure.
 */
#define __generate_log_fn(fn, lvl)							\
void morse_ ## fn(u32 id, struct morse *mors, const char *fmt, ...)			\
{											\
	struct va_format vaf = {							\
		.fmt = fmt,								\
	};										\
	va_list args;									\
											\
	va_start(args, fmt);								\
	vaf.va = &args;									\
	if (log_mask[id] >= (lvl))							\
		dev_ ## fn(mors->dev, "%pV", &vaf);					\
	trace_morse_ ## fn(mors, &vaf);							\
	va_end(args);									\
}											\

__generate_log_fn(dbg, MORSE_MSG_DEBUG)
__generate_log_fn(dbg_ratelimited, MORSE_MSG_DEBUG)
__generate_log_fn(info, MORSE_MSG_INFO)
__generate_log_fn(info_ratelimited, MORSE_MSG_INFO)
__generate_log_fn(warn, MORSE_MSG_WARN)
__generate_log_fn(warn_ratelimited, MORSE_MSG_WARN)
__generate_log_fn(err, MORSE_MSG_ERR)
__generate_log_fn(err_ratelimited, MORSE_MSG_ERR)

#undef __generate_log_fn

void morse_init_log_levels(u8 lvl)
{
	int id;

	for (id = 0; id < NUM_FEATURE_IDS; id++)
		log_mask[id] = lvl;
}

bool morse_log_is_enabled(enum morse_feature_id id, u8 level)
{
	return (bool)(log_mask[id] >= level);
}

static int morse_log_add_debugfs(struct morse *mors)
{
	enum morse_feature_id id;

	if (!mors->debug.debugfs_phy)
		return -ENODEV;

	mors->debug.debugfs_logging = debugfs_create_dir("logging", mors->debug.debugfs_phy);
	if (!mors->debug.debugfs_logging)
		return -ENODEV;

	for (id = 0; id < ARRAY_SIZE(morse_log_features); id++) {
		debugfs_create_u8(morse_log_features[id], 0600,
				  mors->debug.debugfs_logging, &log_mask[id]);
	}

	return 0;
}

static void morse_log_remove_debugfs(struct morse *mors)
{
	debugfs_remove_recursive(mors->debug.debugfs_logging);
	mors->debug.debugfs_logging = NULL;
}

static void print_stat(struct seq_file *file, const char *desc, u32 val)
{
	seq_printf(file, "%s: %u\n", desc, val);
}

static int read_page_stats(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);

	print_stat(file, "Command Tx", mors->debug.page_stats.cmd_tx);
	print_stat(file, "Beacon Tx", mors->debug.page_stats.bcn_tx);
	print_stat(file, "Management Tx", mors->debug.page_stats.mgmt_tx);
	print_stat(file, "Data Tx", mors->debug.page_stats.data_tx);
	print_stat(file, "Page write fail", mors->debug.page_stats.write_fail);
	print_stat(file, "No page", mors->debug.page_stats.no_page);
	print_stat(file, "No command page", mors->debug.page_stats.cmd_no_page);
	print_stat(file, "Command page retry", mors->debug.page_stats.cmd_rsv_page_retry);
	print_stat(file, "No beacon page", mors->debug.page_stats.bcn_no_page);
	print_stat(file, "Excessive beacon loss", mors->debug.page_stats.excessive_bcn_loss);
	print_stat(file, "Queue stop", mors->debug.page_stats.queue_stop);
	print_stat(file, "Popped page owned by chip", mors->debug.page_stats.page_owned_by_chip);
	print_stat(file, "TX ps filtered", mors->debug.page_stats.tx_ps_filtered);
	print_stat(file, "Stale tx status flushed", mors->debug.page_stats.tx_status_flushed);
	print_stat(file, "TX status invalid", mors->debug.page_stats.tx_status_page_invalid);
	print_stat(file, "TX dropped due to duty cycle",
		   mors->debug.page_stats.tx_status_duty_cycle_cant_send);
	print_stat(file, "TX status dropped", mors->debug.page_stats.tx_status_dropped);
	print_stat(file, "RX empty queue", mors->debug.page_stats.rx_empty);
	print_stat(file, "RX packet split across window", mors->debug.page_stats.rx_split);
	print_stat(file, "Invalid checksum", mors->debug.page_stats.invalid_checksum);
	print_stat(file, "Invalid TX status checksum",
		mors->debug.page_stats.invalid_tx_staus_ckecksum);

	return 0;
}

#if defined(CONFIG_MORSE_DEBUG_IRQ)
static int read_hostsync_stats(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	int i;

	print_stat(file, "IRQs", mors->debug.hostsync_stats.irq);

	seq_puts(file, "IRQ bit histogram:");
	for (i = ARRAY_SIZE(mors->debug.hostsync_stats.irq_bits) - 1; i >= 0; i--)
		seq_printf(file, " %u", mors->debug.hostsync_stats.irq_bits[i]);
	seq_putc(file, '\n');

	return 0;
}
#endif

static int read_firmware_path(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	char *fw_path = morse_firmware_build_fw_path(mors);

	seq_printf(file, "%s\n", fw_path);

	kfree(fw_path);
	return 0;
}

static void read_vendor_operations(struct seq_file *file, struct morse_ops *ops)
{
	seq_puts(file, "    Features in operation\n");
	seq_printf(file, "      [%c] DTIM CTS-To-Self\n",
		   MORSE_OPS_IN_USE(ops, DTIM_CTS_TO_SELF) ? '*' : ' ');
	seq_printf(file, "      [%c] Legacy AMSDU\n",
		   MORSE_OPS_IN_USE(ops, LEGACY_AMSDU) ? '*' : ' ');
}

static void read_sta_vendor_info_iter(void *data, struct ieee80211_sta *sta)
{
	struct seq_file *file = (struct seq_file *)data;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;

	if (!mors_sta->vendor_info.valid)
		return;

	seq_printf(file, "STA [%pM]:\n", sta->addr);
	seq_printf(file, "    SW version: %d.%d.%d\n",
		   mors_sta->vendor_info.sw_ver.major, mors_sta->vendor_info.sw_ver.minor,
		   mors_sta->vendor_info.sw_ver.patch);
	seq_printf(file, "    HW version: 0x%08x\n", mors_sta->vendor_info.chip_id);
	seq_puts(file, "    Capabilities\n");
	seq_printf(file, "      MMSS: %u\n", mors_sta->ampdu_mmss);
	seq_printf(file, "      MMSS offset: %u\n", mors_sta->vendor_info.morse_mmss_offset);
	seq_printf(file, "      [%c] Supports short ack timeout\n",
		   mors_sta->vendor_info.supports_short_ack_timeout ? '*' : ' ');
	read_vendor_operations(file, &mors_sta->vendor_info.operations);
}

static int read_vendor_info_tbl(struct seq_file *file, void *data)
{
	int vif_id;
	struct morse *mors = dev_get_drvdata(file->private);

	seq_puts(file, "MM vendor-specific information\n");
	seq_printf(file, "    SW version: %d.%d.%d\n", mors->sw_ver.major,
		   mors->sw_ver.minor, mors->sw_ver.patch);
	seq_printf(file, "    HW version: 0x%08x\n", mors->chip_id);

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_if;

		if (!vif)
			continue;

		mors_if = ieee80211_vif_to_morse_vif(vif);

		seq_printf(file, "%s: VIF [%d]:\n", morse_vif_name(vif), mors_if->id);
		seq_puts(file, "    Capabilities\n");
		seq_printf(file, "      MMSS: %u\n", mors_if->capabilities.ampdu_mss);
		seq_printf(file, "      MMSS offset: %u\n",
			   mors_if->capabilities.morse_mmss_offset);
		/* Is unconditionally set */
		seq_puts(file, "      [*] Supports short ack timeout\n");

		read_vendor_operations(file, &mors_if->operations);

		if (morse_mac_is_iface_ap_type(vif)) {
			ieee80211_iterate_stations_atomic(mors->hw,
							  read_sta_vendor_info_iter, file);
		} else if (vif->type == NL80211_IFTYPE_STATION &&
			   vif->bss_conf.assoc && mors_if->bss_vendor_info.valid) {
			seq_printf(file, "AP [%pM]:\n", vif->bss_conf.bssid);
			seq_printf(file, "    SW version: %d.%d.%d\n",
				   mors_if->bss_vendor_info.sw_ver.major,
				   mors_if->bss_vendor_info.sw_ver.minor,
				   mors_if->bss_vendor_info.sw_ver.patch);
			seq_printf(file, "    HW version: 0x%08x\n",
				   mors_if->bss_vendor_info.chip_id);
			seq_puts(file, "    Capabilities\n");
			seq_printf(file, "      MMSS: %u\n", mors_if->bss_ampdu_mmss);
			seq_printf(file, "      MMSS offset: %u\n",
				   mors_if->bss_vendor_info.morse_mmss_offset);
			seq_printf(file, "      [%c] Supports short ack timeout\n",
				   mors_if->bss_vendor_info.supports_short_ack_timeout ? '*' : ' ');
			read_vendor_operations(file, &mors_if->bss_vendor_info.operations);
		}
	}

	return 0;
}

#ifdef CONFIG_MORSE_DEBUGFS
static int read_file_pagesets(struct seq_file *file, void *data)
{
	int i;
	struct morse *mors = dev_get_drvdata(file->private);

	for (i = 0; i < mors->chip_if->pageset_count; i++) {
		seq_printf(file, "[%d]:\n", i);
		morse_pageset_show(mors, &mors->chip_if->pagesets[i], file);
	}

	return 0;
}


static int read_skbq_mon_tbl(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);

	morse_skbq_mon_dump(mors, file);

	return 0;
}

static int read_mcs_stats_tbl(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);

	seq_puts(file, "MCS Statistics\n");
	seq_puts(file, "MCS0 TX Beacons\n");
	seq_printf(file, "%-10u\n", mors->debug.mcs_stats_tbl.mcs0.tx_beacons);
	seq_puts(file, "MCS0 TX NDP Probes\n");
	seq_printf(file, "%-10u\n", mors->debug.mcs_stats_tbl.mcs0.tx_ndpprobes);
	seq_puts(file, "MCS0 TX Count       MCS10 TX Count\n");
	seq_printf(file, "%-10u          %-10u\n",
		   mors->debug.mcs_stats_tbl.mcs0.tx_count,
		   mors->debug.mcs_stats_tbl.mcs10.tx_count);
	seq_puts(file, "MCS0 TX Success     MCS10 TX Success\n");
	seq_printf(file, "%-10u          %-10u\n",
		   mors->debug.mcs_stats_tbl.mcs0.tx_success,
		   mors->debug.mcs_stats_tbl.mcs10.tx_success);
	seq_puts(file, "MCS0 TX Fail        MCS10 TX Fail\n");
	seq_printf(file, "%-10u          %-10u\n",
		   mors->debug.mcs_stats_tbl.mcs0.tx_fail, mors->debug.mcs_stats_tbl.mcs10.tx_fail);
	seq_puts(file, "MCS0 RX             MCS10 RX\n");
	seq_printf(file, "%-10u          %-10u\n",
		   mors->debug.mcs_stats_tbl.mcs0.rx_count,
		   mors->debug.mcs_stats_tbl.mcs10.rx_count);

	/* Resetting this should make it easier to debug for now. */
	memset(&mors->debug.mcs_stats_tbl, 0, sizeof(mors->debug.mcs_stats_tbl));

	return 0;
}

static int read_vendor_ies(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct vendor_ie_list_item *item;
	u8 *ie;
	int i;
	int vif_id;

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_if;

		if (!vif)
			continue;

		mors_if = ieee80211_vif_to_morse_vif(vif);

		seq_printf(file, "%s: VIF [%d]:\n", morse_vif_name(vif), mors_if->id);
		spin_lock_bh(&mors_if->vendor_ie.lock);
		list_for_each_entry(item, &mors_if->vendor_ie.ie_list, list) {
			ie = (u8 *)item->ie.oui;
			seq_printf(file, "Vendor IE: (mask 0x%04x)", item->mgmt_type_mask);

			for (i = 0; i < item->ie.len; i++) {
				if ((i % 32) == 0)
					seq_puts(file, "\n\t");
				seq_printf(file, "%02X ", ie[i]);
			}
			seq_puts(file, "\n");
		}
		spin_unlock_bh(&mors_if->vendor_ie.lock);
	}
	return 0;
}

static int read_vendor_ie_oui_filter(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct vendor_ie_oui_filter_list_item *item;
	int vif_id;

	seq_puts(file, "OUI Filters:\n");

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_if;

		if (!vif)
			continue;

		mors_if = ieee80211_vif_to_morse_vif(vif);

		seq_printf(file, "%s: VIF [%d]:\n", morse_vif_name(vif), mors_if->id);
		spin_lock_bh(&mors_if->vendor_ie.lock);
		list_for_each_entry(item, &mors_if->vendor_ie.oui_filter_list, list) {
			seq_printf(file, "\t%02X:%02X:%02X - mask: 0x%04x\n",
				   item->oui[0], item->oui[1], item->oui[2], item->mgmt_type_mask);
		}
		spin_unlock_bh(&mors_if->vendor_ie.lock);
	}

	return 0;
}

#ifdef CONFIG_MORSE_DEBUG_TXSTATUS
static int read_tx_status_info(struct seq_file *file, void *data)
{
	struct morse *mors = dev_get_drvdata(file->private);
	struct morse_skb_tx_status entry;
	int i, count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);

	while (kfifo_get(&mors->debug.tx_status_entries, &entry)) {
		seq_printf(file, "%d,%u,%u", entry.tid, entry.flags, le16_to_cpu(entry.ampdu_info));
		for (i = 0; i < count; i++) {
			enum dot11_bandwidth bw_idx;
			u8 mcs_index;
			enum morse_rate_preamble pream;

			if (entry.rates[i].count <= 0)
				break;

			bw_idx = morse_ratecode_bw_index_get(entry.rates[i].morse_ratecode);
			mcs_index = morse_ratecode_mcs_index_get(entry.rates[i].morse_ratecode);
			pream = morse_ratecode_preamble_get(entry.rates[i].morse_ratecode);

			seq_printf(file, ",mcs:%d, bw:%d, preamble:%d count:%d",
				   mcs_index, bw_idx, pream, entry.rates[i].count);
		}

		seq_puts(file, "\n");

		if (seq_has_overflowed(file))
			break;
	}

	return 0;
}

int morse_debug_log_tx_status(struct morse *mors, struct morse_skb_tx_status *tx_sts)
{
	int ret;

	/* If full then pop off the oldest entry */
	if (kfifo_is_full(&mors->debug.tx_status_entries)) {
		struct morse_skb_tx_status entry;

		ret = kfifo_get(&mors->debug.tx_status_entries, &entry);
	}

	ret = kfifo_put(&mors->debug.tx_status_entries, *tx_sts);

	return ret;
}
#endif

static ssize_t morse_debug_bus_reset_write(struct file *file, const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	u8 value;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;

	if (value != 1)
		return -EINVAL;

	schedule_work(&mors->reset);

	return count;
}

static const struct file_operations bus_reset_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.write = morse_debug_bus_reset_write,
};

static ssize_t morse_debug_fw_reset_write(struct file *file, const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	u8 value;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;
	if (value != 1)
		return -EINVAL;
	schedule_work(&mors->soft_reset);
	return count;
}

static const struct file_operations fw_reset_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.write = morse_debug_fw_reset_write,
};

static ssize_t morse_debug_driver_restart_write(struct file *file, const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	u8 value;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;
	if (value != 1)
		return -EINVAL;
	schedule_work(&mors->driver_restart);
	return count;
}

static const struct file_operations driver_restart_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.write = morse_debug_driver_restart_write,
};

static ssize_t morse_debug_watchdog_write(struct file *file, const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;

	if (strncmp(user_buf, "start", 5) == 0) {
		morse_watchdog_start(mors);
	} else if (strncmp(user_buf, "stop", 4) == 0) {
		morse_watchdog_stop(mors);
	} else if (strncmp(user_buf, "refresh", 7) == 0) {
		morse_watchdog_refresh(mors);
	} else if (strncmp(user_buf, "disable", 7) == 0) {
		morse_watchdog_cleanup(mors);
	} else {
		pr_info
		    ("[watchdog-debugfs] supported params: start, stop, refresh, disable\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations watchdog_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.write = morse_debug_watchdog_write,
};

static ssize_t morse_debug_reset_required_read(struct file *file,
					       char __user *user_buf, size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	char buf[5];
	size_t len;

	len = scnprintf(buf, sizeof(buf), "%u\n", mors->reset_required);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations reset_required_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.read = morse_debug_reset_required_read,
};

struct hostif_log_item {
	struct list_head list;
	u64 timestamp;
	int length;
	int to_chip;
	int channel;
	u8 data[];
};

static int morse_debug_fw_hostif_log_open(struct inode *inode, struct file *file)
{
	struct morse *mors = (struct morse *)inode->i_private;

	if (!mors)
		return -EINVAL;

	file->private_data = mors;

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return -EINVAL;

	/* For now only allow one client */
	if (mors->debug.hostif_log.active_clients >= 1) {
		mutex_unlock(&mors->debug.hostif_log.lock);
		return -ENOSPC;
	}

	mors->debug.hostif_log.active_clients++;
	mutex_unlock(&mors->debug.hostif_log.lock);

	return 0;
}

static void morse_debug_fw_hostif_log_flush(struct morse *mors)
{
	struct list_head *curr, *next;

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return;
	list_for_each_safe(curr, next, &mors->debug.hostif_log.items) {
		list_del(curr);
		kfree(curr);
	}
	mutex_unlock(&mors->debug.hostif_log.lock);
}

static int morse_debug_fw_hostif_log_release(struct inode *inode, struct file *file)
{
	struct morse *mors = (struct morse *)file->private_data;

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return -EINVAL;

	mors->debug.hostif_log.active_clients--;

	if (mors->debug.hostif_log.active_clients == 0)
		morse_debug_fw_hostif_log_flush((struct morse *)file->private_data);

	mutex_unlock(&mors->debug.hostif_log.lock);

	return 0;
}

static ssize_t morse_debug_fw_hostif_log_read(struct file *file,
					      char __user *user_buf, size_t count, loff_t *ppos)
{
	struct morse *mors = (struct morse *)file->private_data;
	struct list_head *curr;
	bool is_list_empty;
	struct hostif_log_item *item;
	u8 header_buf[sizeof(item->timestamp) + sizeof(item->to_chip)];
	int length = sizeof(header_buf);

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return -ERESTARTSYS;

	is_list_empty = list_empty(&mors->debug.hostif_log.items);
	mutex_unlock(&mors->debug.hostif_log.lock);

	if (is_list_empty && (file->f_flags & O_NONBLOCK))
		return -EWOULDBLOCK;

	if (is_list_empty &&
	    wait_event_interruptible(mors->debug.hostif_log.waitqueue,
				     !list_empty(&mors->debug.hostif_log.items) ||
				     mors->debug.hostif_log.active_clients == 0)) {
		return -ERESTARTSYS;
	}

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return -ERESTARTSYS;

	/* Active clients has gone to zero, we are probably tearing down,
	 * so return error.
	 */
	if (mors->debug.hostif_log.active_clients == 0) {
		mutex_unlock(&mors->debug.hostif_log.lock);
		return -EINVAL;
	}

	/* Because the list_empty check as part of the wait event isn't protected
	 * by the mutex, we check here again that it still has items. If not,
	 * return this error so the read is tried again.
	 */
	if (list_empty(&mors->debug.hostif_log.items)) {
		mutex_unlock(&mors->debug.hostif_log.lock);
		return -ERESTARTSYS;
	}

	curr = mors->debug.hostif_log.items.next;
	list_del(curr);
	mutex_unlock(&mors->debug.hostif_log.lock);

	item = list_entry(curr, struct hostif_log_item, list);
	length += item->length;

	if (count >= length) {
		/* We put the timestamp at the start, followed by the indication of to_chip */
		memcpy(header_buf, &item->timestamp, sizeof(item->timestamp));
		memcpy(&header_buf[sizeof(item->timestamp)], &item->to_chip, sizeof(item->to_chip));

		if (copy_to_user(user_buf, header_buf, sizeof(header_buf)) ||
		    copy_to_user(user_buf + sizeof(header_buf), item->data, item->length)) {
			return -EFAULT;
		}
	}

	kfree(curr);

	return length;
}

void morse_debug_fw_hostif_log_record(struct morse *mors, int to_chip,
				      struct sk_buff *skb, struct morse_buff_skb_header *hdr)
{
	struct hostif_log_item *item;
	struct timespec64 time_now;
	int hostif_log_mask = 0;

	/* The channel values don't lend themselves well to bitmasks, so we have a mapping */
	switch (hdr->channel) {
	case MORSE_SKB_CHAN_COMMAND:
		hostif_log_mask = MORSE_HOSTIF_LOG_COMMAND;
		break;
	case MORSE_SKB_CHAN_TX_STATUS:
		hostif_log_mask = MORSE_HOSTIF_LOG_TX_STATUS;
		break;
	default:
		hostif_log_mask = MORSE_HOSTIF_LOG_DATA;
		break;
	}

	/* If this channel isn't enabled in the mask, exit */
	if ((mors->debug.hostif_log.enabled_channel_mask & hostif_log_mask) == 0)
		return;

	if (mutex_lock_interruptible(&mors->debug.hostif_log.lock) != 0)
		return;
	if (mors->debug.hostif_log.active_clients == 0)
		goto exit;

	item = kmalloc(sizeof(*item) + skb->len, GFP_KERNEL);

	if (!item)
		goto exit;

	item->length = skb->len;
	item->to_chip = to_chip;
	item->channel = hdr->channel;
	jiffies_to_timespec64(get_jiffies_64(), &time_now);
	item->timestamp = (time_now.tv_sec * NSEC_PER_SEC) + time_now.tv_nsec;
	memcpy(item->data, skb->data, skb->len);

	list_add_tail(&item->list, &mors->debug.hostif_log.items);

	wake_up_interruptible_all(&mors->debug.hostif_log.waitqueue);
exit:
	mutex_unlock(&mors->debug.hostif_log.lock);
}

static void morse_debug_fw_hostif_log_destroy(struct morse *mors)
{
	/* Need to grab this lock, no interruptions */
	mutex_lock(&mors->debug.hostif_log.lock);
	mors->debug.hostif_log.active_clients = 0;
	mutex_unlock(&mors->debug.hostif_log.lock);
	wake_up_all(&mors->debug.hostif_log.waitqueue);
	morse_debug_fw_hostif_log_flush(mors);
}

static const struct file_operations fw_hostif_log_fops = {
	.open = morse_debug_fw_hostif_log_open,
	.release = morse_debug_fw_hostif_log_release,
	.llseek = no_llseek,
	.read = morse_debug_fw_hostif_log_read,
};

static ssize_t morse_debug_hostif_log_config_write(struct file *file, const char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	u8 value;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;
	mors->debug.hostif_log.enabled_channel_mask = value;
	return count;
}

static ssize_t morse_debug_hostif_log_config_read(struct file *file, char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct morse *mors = file->private_data;
	char buf[8];
	size_t len;

	len = scnprintf(buf, sizeof(buf), "0x%x\n", mors->debug.hostif_log.enabled_channel_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fw_hostif_log_config_fops = {
	.open = simple_open,
	.llseek = no_llseek,
	.write = morse_debug_hostif_log_config_write,
	.read = morse_debug_hostif_log_config_read
};
#endif

#ifndef CONFIG_MORSE_DEBUGFS
void morse_debug_fw_hostif_log_record(struct morse *mors, int to_chip,
				      struct sk_buff *skb, struct morse_buff_skb_header *hdr)
{
}
#endif

static int read_ap_info(struct seq_file *file, void *data)
{
	int i;
	int vif_id;
	struct morse *mors = dev_get_drvdata(file->private);

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_if;

		if (!vif)
			continue;

		mors_if = ieee80211_vif_to_morse_vif(vif);

		if (!mors_if->ap) {
			seq_printf(file, "%s not an AP\n", morse_vif_name(vif));
		} else {
			seq_printf(file, "%s Info\n", morse_vif_name(vif));
			seq_printf(file, "  Largest AID: %u\n", mors_if->ap->largest_aid);
			seq_printf(file, "  Num assoc STAs: %u\n", mors_if->ap->num_stas);
			seq_puts(file, "  AID bitmap (LSB first, bit 0 is AID 0):\n\t");

			/* Print bitmap as binary, e.g. 01101100 */
			for (i = 0; i < (mors_if->ap->largest_aid / 8) + 1; i++) {
				int j;
				u8 val = ((u8 *)mors_if->ap->aid_bitmap)[i];

				for (j = 0; j < 8; j++, val >>= 1)
					seq_printf(file, "%d", val & 0x1);

				/* New line every 8 bytes */
				seq_printf(file, "%s", ((i % 8) == 7) ? "\n\t" : " ");
			}
			seq_puts(file, "\n");
		}
	}

	return 0;
}

static int read_twt_sta_agreements(struct seq_file *file, void *data)
{
	int vif_id;
	struct morse *mors = dev_get_drvdata(file->private);

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_vif;

		if (!vif)
			continue;

		mors_vif = ieee80211_vif_to_morse_vif(vif);
		morse_twt_dump_sta_agreements(file, mors_vif);
	}

	return 0;
}

static int read_twt_wi_tree(struct seq_file *file, void *data)
{
	int vif_id;
	struct morse *mors = dev_get_drvdata(file->private);

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);
		struct morse_vif *mors_vif;

		if (!vif)
			continue;

		mors_vif = ieee80211_vif_to_morse_vif(vif);

		morse_twt_dump_wake_interval_tree(file, mors_vif);
	}

	return 0;
}

const char *morse_iftype_to_str(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_STATION:
		return "sta";
	case NL80211_IFTYPE_AP:
		return "ap";
	case NL80211_IFTYPE_ADHOC:
		return "adhoc";
	case NL80211_IFTYPE_MESH_POINT:
		return "mesh";
	case NL80211_IFTYPE_MONITOR:
		return "monitor";
	default:
		return "unknown";
	}

	return NULL;
}

static void print_sta_tx_pkt_count_iter(void *data, struct ieee80211_sta *sta)
{
	struct seq_file *file = (struct seq_file *)data;
	struct morse_sta *s = (struct morse_sta *)sta->drv_priv;

	seq_printf(file, "%pM %llu\n", sta->addr, s->tx_pkt_count);

	s->tx_pkt_count = 0;
}

static int get_tx_sta_summary_tbl(struct seq_file *file, void *data)
{
	int ret = 0;
	int vif_id;
	struct morse *mors = dev_get_drvdata(file->private);

	seq_printf(file, "%-17s %s\n", "Station", "Packets");

	for (vif_id = 0; vif_id < mors->max_vifs; vif_id++) {
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);

		if (!vif)
			continue;

		if (morse_mac_is_iface_ap_type(vif)) {
			ieee80211_iterate_stations_atomic(mors->hw,
							  print_sta_tx_pkt_count_iter, file);
		}
	}
	return ret;
}

int morse_init_debug(struct morse *mors)
{
	mors->debug.debugfs_phy = debugfs_create_dir("morse", mors->wiphy->debugfsdir);
	if (!mors->debug.debugfs_phy)
		return -ENOMEM;

	debugfs_create_devm_seqfile(mors->dev, "page_stats",
				    mors->debug.debugfs_phy, read_page_stats);

#if defined(CONFIG_MORSE_DEBUG_IRQ)
	debugfs_create_devm_seqfile(mors->dev, "hostsync_stats",
				    mors->debug.debugfs_phy, read_hostsync_stats);
#endif

	debugfs_create_devm_seqfile(mors->dev, "firmware_path",
				    mors->debug.debugfs_phy, read_firmware_path);

	debugfs_create_devm_seqfile(mors->dev, "vendor_info",
				    mors->debug.debugfs_phy, read_vendor_info_tbl);

	debugfs_create_devm_seqfile(mors->dev, "ap_info", mors->debug.debugfs_phy, read_ap_info);

	debugfs_create_devm_seqfile(mors->dev, "twt_sta_agreements",
				    mors->debug.debugfs_phy, read_twt_sta_agreements);

	debugfs_create_devm_seqfile(mors->dev, "twt_wi_tree",
				    mors->debug.debugfs_phy, read_twt_wi_tree);

	debugfs_create_devm_seqfile(mors->dev, "sta_tx_count_table",
				    mors->debug.debugfs_phy, get_tx_sta_summary_tbl);

#ifdef CONFIG_MORSE_DEBUGFS
	if (mors->chip_if->active_chip_if == MORSE_CHIP_IF_PAGESET)
		debugfs_create_devm_seqfile(mors->dev, "pagesets",
					    mors->debug.debugfs_phy, read_file_pagesets);

	debugfs_create_devm_seqfile(mors->dev, "skbq_mon",
				    mors->debug.debugfs_phy, read_skbq_mon_tbl);

	debugfs_create_devm_seqfile(mors->dev, "mcs_stats",
				    mors->debug.debugfs_phy, read_mcs_stats_tbl);

	debugfs_create_devm_seqfile(mors->dev, "vendor_ies",
				    mors->debug.debugfs_phy, read_vendor_ies);

	debugfs_create_devm_seqfile(mors->dev, "vendor_ie_oui_filters",
				    mors->debug.debugfs_phy, read_vendor_ie_oui_filter);

#ifdef CONFIG_MORSE_DEBUG_TXSTATUS
	INIT_KFIFO(mors->debug.tx_status_entries);

	debugfs_create_devm_seqfile(mors->dev, "tx_status",
				    mors->debug.debugfs_phy, read_tx_status_info);
#endif
	mutex_init(&mors->debug.hostif_log.lock);
	init_waitqueue_head(&mors->debug.hostif_log.waitqueue);
	INIT_LIST_HEAD(&mors->debug.hostif_log.items);
	mors->debug.hostif_log.enabled_channel_mask = MORSE_HOSTIF_LOG_COMMAND;
	debugfs_create_file("fw_hostif_log", 0600, mors->debug.debugfs_phy, mors,
			    &fw_hostif_log_fops);
	debugfs_create_file("fw_hostif_log_enabled_channels", 0600, mors->debug.debugfs_phy, mors,
			    &fw_hostif_log_config_fops);

	/* populate debugfs */
	debugfs_create_file("reset", 0600, mors->debug.debugfs_phy, mors, &bus_reset_fops);

	debugfs_create_file("soft_reset", 0600, mors->debug.debugfs_phy, mors, &fw_reset_fops);

	debugfs_create_file("restart", 0600, mors->debug.debugfs_phy, mors, &driver_restart_fops);

	debugfs_create_file("watchdog", 0600, mors->debug.debugfs_phy, mors, &watchdog_fops);

	debugfs_create_file("reset_required", 0600, mors->debug.debugfs_phy, mors,
			    &reset_required_fops);

#endif

#ifdef CONFIG_MORSE_RC
	mmrc_s1g_add_sta_debugfs(mors);
	mmrc_s1g_add_mesh_debugfs(mors);
#endif

	return morse_log_add_debugfs(mors);
}

void morse_deinit_debug(struct morse *mors)
{
#ifdef CONFIG_MORSE_DEBUGFS
	morse_debug_fw_hostif_log_destroy(mors);
#endif

	morse_log_remove_debugfs(mors);
}

void morse_log_modparams(struct morse *mors)
{
	size_t i;
	const struct kernel_param *kp;
	char *buffer;
	const int bufflen = 4096;

	if (!try_module_get(THIS_MODULE))
		return;

	buffer = kmalloc(bufflen, GFP_KERNEL);
	if (!buffer)
		goto exit;

#ifdef CONFIG_SYSFS
	kernel_param_lock(THIS_MODULE);
#endif

	/* Unconditionally log mod-params despite debug level */
	kp = THIS_MODULE->kp;
	MORSE_INFO(mors, "Driver loaded with kernel module parameters");
	for (i = 0; i < THIS_MODULE->num_kp; i++)
		if (kp[i].ops->get(buffer, &kp[i]) > 0)
			MORSE_INFO(mors, "    %-40s: %s", kp[i].name, buffer);

#ifdef CONFIG_SYSFS
	kernel_param_unlock(THIS_MODULE);
#endif
	kfree(buffer);

exit:
	module_put(THIS_MODULE);
}

#ifdef CONFIG_MORSE_IPMON
void morse_ipmon(u64 *time_start, struct sk_buff *skb, char *data, int len,
		 enum ipmon_loc loc, int queue_stop)
{
	struct ieee80211_qos_hdr *d11 = (struct ieee80211_qos_hdr *)data;
	struct iphdr *iph;
	struct tcphdr *tcp = NULL;
	struct udphdr *udp = NULL;
	struct ipmon_hdr *hdr;
	unsigned int ccmp_hdr_len = 0;
	unsigned int tcplen;
	u64 time_now;
	u64 *p;
	u32 csum;

	if (loc == IPMON_LOC_SERVER_DRV && ieee80211_has_protected(d11->frame_control))
		ccmp_hdr_len = IEEE80211_CCMP_HDR_LEN;

	iph = (struct iphdr *)(data + ccmp_hdr_len + sizeof(*d11) + LLC_HDR_SIZE);

	if (len < (IPMON_HDRS_LEN + ccmp_hdr_len + sizeof(*tcp) + sizeof(struct ipmon_hdr)))
		return;

	if (iph->protocol == IPPROTO_TCP) {
		tcp = (struct tcphdr *)((char *)iph + sizeof(*iph));
		hdr = (struct ipmon_hdr *)((char *)iph + sizeof(*iph)
					   + (tcp->doff * 4) + IPMON_PAYLOAD_OFFSET);
	} else if (iph->protocol == IPPROTO_UDP) {
		udp = (struct udphdr *)((char *)iph + sizeof(*iph));
		hdr = (struct ipmon_hdr *)((char *)iph + sizeof(*iph)
					   + sizeof(*udp) + IPMON_PAYLOAD_OFFSET);
	} else {
		return;
	}

	if (hdr->check != IPMON_CHECK)
		return;

	switch (loc) {
	case IPMON_LOC_CLIENT_DRV1:
		p = &hdr->time_client_drv1;
		break;
	case IPMON_LOC_CLIENT_DRV2:
		p = &hdr->time_client_drv2;
		hdr->queue_stop = queue_stop;
		break;
	case IPMON_LOC_SERVER_DRV:
		p = &hdr->time_server_drv;
		break;
	default:
		return;
	}

	time_now = ktime_to_ms(ktime_get_real());
	if (hdr->pktnum == 1) {
		/* Start of a new stream */
		*time_start = time_now;
		*p = time_now;
	} else {
		*p = time_now - *time_start;
	}

	skb->ip_summed = CHECKSUM_NONE;	/* Prevent offloading */
	if (skb_is_nonlinear(skb))
		skb_linearize(skb);	/* very important */
	skb->csum_valid = 0;
	iph->check = 0;
	iph->check = ip_fast_csum((u8 *)iph, iph->ihl);

	/* Recalculate the UDP checksum */
	if (iph->protocol == IPPROTO_TCP) {
		tcp->check = 0;
		tcplen = ntohs(iph->tot_len) - (iph->ihl * 4);
		tcp->check = tcp_v4_check(tcplen, iph->saddr, iph->daddr,
					  csum_partial((char *)tcp, tcplen, 0));
	} else if (udp->check != 0) {
		udp->check = 0;
		csum = csum_partial(udp, ntohs(udp->len), 0);

		/* Add pseudo IP header checksum */
		udp->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
					       ntohs(udp->len), iph->protocol, csum);
		if (udp->check == 0)
			udp->check = CSUM_MANGLED_0;	/* 0 is converted to -1 */
	}
}
#endif

int morse_coredump(struct morse *mors)
{
	int ret = 0;
	static const char *const envp[] = { "HOME=/", NULL };
	static const char *const argv[] = {
		"/bin/bash", "-c", "/usr/sbin/morse-core-dump.sh -d", NULL };

	(void)morse_watchdog_pause(mors);
	morse_claim_bus(mors);
#if KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
	ret = call_usermodehelper(argv[0], (char **)argv, (char **)envp, UMH_WAIT_PROC);
#else
	ret = call_usermodehelper((char *)argv[0], (char **)argv, (char **)envp, UMH_WAIT_PROC);
#endif
	morse_release_bus(mors);
	(void)morse_watchdog_resume(mors);
	return ret;
}
