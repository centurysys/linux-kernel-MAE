/*
 * Copyright 2025 Morse Micro
 */
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/byteorder/generic.h>
#include "debug.h"
#include "scan_result_cache.h"

/* We limit the cache to a typical page size - avoids complications in partial writes */
#define SCAN_RES_CACHE_MAX_LEN  (4 * 1024)

#define MORSE_SCAN_RES_CACHE_DBG(_m, _f, _a...) morse_dbg(FEATURE_ID_SCAN_RES_CACHE, _m, _f, ##_a)
#define MORSE_SCAN_RES_CACHE_INFO(_m, _f, _a...) morse_info(FEATURE_ID_SCAN_RES_CACHE, _m, _f, ##_a)
#define MORSE_SCAN_RES_CACHE_WARN(_m, _f, _a...) morse_warn(FEATURE_ID_SCAN_RES_CACHE, _m, _f, ##_a)
#define MORSE_SCAN_RES_CACHE_ERR(_m, _f, _a...) morse_err(FEATURE_ID_SCAN_RES_CACHE, _m, _f, ##_a)

static bool enable_scan_result_cache __read_mostly;
module_param(enable_scan_result_cache, bool, 0644);
MODULE_PARM_DESC(enable_scan_result_cache, "Enable caching of scan results during scan");

static ssize_t scan_result_cache_sysfs_read(struct file *filp, struct kobject *kobj,
						morse_bin_attr_t *attr, char *buf,
						loff_t off, size_t count)
{
	struct sk_buff *skb;
	size_t copied = 0;
	size_t pos = 0;
	struct morse *mors;

	mors = dev_get_drvdata(kobj_to_dev(kobj));
	if (!mors->scan_res_cache.is_enabled) {
		MORSE_SCAN_RES_CACHE_WARN(mors,
					  "%s: sysfs present but cache not enabled\n",
					  __func__);
		return 0;
	}

	/* sink the entirety of the scan result cache to disk */
	spin_lock_bh(&mors->scan_res_cache.cache.lock);
	skb_queue_walk(&mors->scan_res_cache.cache, skb) {
		size_t skb_offset;
		size_t available;
		size_t to_copy;
		size_t skb_len = skb->len;

		/* Skip SKBs that are entirely before 'off' */
		if (off >= pos + skb_len) {
			pos += skb_len;
			continue;
		}

		/* Calculate how far into this skb we start reading */
		skb_offset = off > pos ? off - pos : 0;
		available = skb_len - skb_offset;
		to_copy = min_t(size_t, available, count - copied);

		memcpy(buf + copied, skb->data + skb_offset, to_copy);
		copied += to_copy;
		pos += skb_len;

		if (copied >= count)
			break;
	}
	spin_unlock_bh(&mors->scan_res_cache.cache.lock);

	MORSE_SCAN_RES_CACHE_DBG(mors, "%s: off=%lld count=%zu copied=%zu\n",
				 __func__, off, count, copied);
	return copied;
}

static ssize_t scan_result_cache_sysfs_write(struct file *filp, struct kobject *kobj,
						morse_bin_attr_t *attr, char *buf,
						loff_t off, size_t count)
{
	struct morse *mors;
	size_t pos = 0;
	unsigned int entries = 0;

	mors = dev_get_drvdata(kobj_to_dev(kobj));
	if (!mors->scan_res_cache.is_enabled) {
		MORSE_SCAN_RES_CACHE_WARN(mors,
					  "%s: sysfs present but cache not enabled\n",
					  __func__);
		return 0;
	}

	if (off) {
		MORSE_SCAN_RES_CACHE_ERR(mors, "%s: partial writes not supported\n", __func__);
		return 0;
	}

	/* writing to the sysfs entry clears all existing entries */
	scan_result_cache_flush(mors);

	while (pos < count) {
		struct morse_buff_skb_header header;
		size_t expected = sizeof(header);
		size_t len = min_t(size_t, expected, count - pos);
		struct sk_buff *skb;
		void *tmp;

		if (len < expected || ((count - pos) == expected))
			break; /* Not enough memory remains for an entry's header + contents */

		memcpy(&header, buf + pos, len);
		if (header.sync != MORSE_SKB_HEADER_SYNC) {
			MORSE_SCAN_RES_CACHE_ERR(mors, "%s: corrupt entry\n", __func__);
			break;
		}

		expected = sizeof(header) + le16_to_cpu(header.len);
		len = min_t(size_t, expected, count - pos);
		if (len < expected) {
			MORSE_SCAN_RES_CACHE_ERR(mors, "%s: corrupt entry\n", __func__);
			break; /* Not enough memory remaining for the entire entry */
		}

		skb = dev_alloc_skb(expected);
		if (!skb) {
			MORSE_SCAN_RES_CACHE_WARN(mors, "%s: failed to allocate skb\n", __func__);
			break;
		}

		tmp = skb_put(skb, expected);
		memcpy(tmp, buf + pos, expected);
		skb_queue_tail(&mors->scan_res_cache.cache, skb);
		entries++;
		pos += expected;
	}

	MORSE_SCAN_RES_CACHE_DBG(mors, "%s: %d entries queued, %zu byte(s)\n",
				 __func__, entries, pos);
	return count;
}

static ssize_t scan_result_cache_clear_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct morse *mors = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "1"))
		scan_result_cache_flush(mors);

	return count;
}

static struct bin_attribute scan_result_cache_sysfs_attr = {
	.attr = { .name = "scan_result_cache", .mode = 0664 },
	.read  = scan_result_cache_sysfs_read,
	.write = scan_result_cache_sysfs_write,
	.size  = SCAN_RES_CACHE_MAX_LEN,
};

static DEVICE_ATTR_WO(scan_result_cache_clear);

void scan_result_cache(struct morse *mors,
		       const struct sk_buff *rx,
		       const struct morse_skb_rx_status *rx_status)
{
	struct ieee80211_hdr *rx_hdr;
	__le16 rx_sub_type;
	struct sk_buff *new;
	struct sk_buff *search;
	struct sk_buff *iter;
	struct morse_buff_skb_header header;
	void *tmp;

	if (!mors->scan_res_cache.is_enabled)
		return;

	if (WARN_ON(!rx || !rx_status))
		return;

	memset(&header, 0, sizeof(header));
	header.sync = MORSE_SKB_HEADER_SYNC;
	header.channel = MORSE_SKB_CHAN_MGMT;
	header.len = cpu_to_le16(rx->len);
	header.offset = 0;
	memcpy(&header.rx_status, rx_status, sizeof(header.rx_status));

	new = dev_alloc_skb(sizeof(header) + rx->len);
	if (!new) {
		MORSE_SCAN_RES_CACHE_WARN(mors, "%s: failed to allocate skb\n", __func__);
		return;
	}

	tmp = skb_put(new, sizeof(header));
	memcpy(tmp, &header, sizeof(header));
	tmp = skb_put(new, rx->len);
	memcpy(tmp, rx->data, rx->len);

	/* Derive information about new RX */
	rx_hdr = (struct ieee80211_hdr *)rx->data;
	rx_sub_type = rx_hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE);

	/* Prevent an accumulation of duplicate/old scan results */
	spin_lock_bh(&mors->scan_res_cache.cache.lock);
	skb_queue_walk_safe(&mors->scan_res_cache.cache, search, iter) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(search->data +
					    sizeof(struct morse_buff_skb_header));
		__le16 sub_type = hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE);

		if (memcmp(hdr->addr3, rx_hdr->addr3, sizeof(rx_hdr->addr3)))
			continue; /* bssid does not match */

		if (sub_type != rx_sub_type)
			continue; /* different mgmt frame type */

		MORSE_SCAN_RES_CACHE_DBG(mors, "%s: removing old result %u byte(s)",
					 __func__,
					 search->len);
		__skb_unlink(search, &mors->scan_res_cache.cache);
		kfree_skb(search);
	}
	spin_unlock_bh(&mors->scan_res_cache.cache.lock);

	/* Finally - queue the new scan result */
	skb_queue_tail(&mors->scan_res_cache.cache, new);
	MORSE_SCAN_RES_CACHE_DBG(mors, "%s: %zu byte(s)\n", __func__, sizeof(header) + rx->len);
}

void scan_result_cache_flush(struct morse *mors)
{
	if (!mors->scan_res_cache.is_enabled)
		return;

	skb_queue_purge(&mors->scan_res_cache.cache);
	MORSE_SCAN_RES_CACHE_DBG(mors, "%s: complete\n", __func__);
}

bool scan_result_cache_is_empty(struct morse *mors)
{
	if (!mors->scan_res_cache.is_enabled)
		return true;

	return skb_queue_len(&mors->scan_res_cache.cache) == 0;
}

int scan_result_cache_replay(struct morse *mors)
{
	struct sk_buff_head local;
	struct morse_skbq *mq;
	size_t entries;

	if (!mors->scan_res_cache.is_enabled)
		return 0;

	mq = mors->cfg->ops->skbq_rx_data_q(mors);
	if (!mq)
		return 0;

	__skb_queue_head_init(&local);

	/* Empty the cache and append entries into rx mq. Eventually these entries will come
	 * back through the RX path where they will be cached again (if enabled).
	 */
	spin_lock_bh(&mors->scan_res_cache.cache.lock);
	skb_queue_splice_tail_init(&mors->scan_res_cache.cache, &local);
	spin_unlock_bh(&mors->scan_res_cache.cache.lock);

	entries = skb_queue_len(&local);
	if (!entries)
		return 0; /* No entries to replay */

	morse_skbq_enq(mq, &local);
	MORSE_SCAN_RES_CACHE_DBG(mors, "%s: %zu entries replayed\n", __func__, entries);
	queue_work(mors->net_wq, &mq->dispatch_work);
	return entries;
}

void scan_result_cache_init(struct morse *mors)
{
	mors->scan_res_cache.is_enabled = enable_scan_result_cache;

	if (!mors->scan_res_cache.is_enabled)
		return;

	skb_queue_head_init(&mors->scan_res_cache.cache);
}

int scan_result_cache_sysfs_init(struct morse *mors)
{
	int ret;
	bool cache_created = false;

	if (!mors->scan_res_cache.is_enabled)
		return 0;

	ret = device_create_bin_file(mors->dev, &scan_result_cache_sysfs_attr);
	if (ret)
		goto err;
	cache_created = true;

	ret = device_create_file(mors->dev, &dev_attr_scan_result_cache_clear);
	if (ret)
		goto err;

	/* return before error path */
	return 0;
err:
	if (cache_created)
		device_remove_bin_file(mors->dev, &scan_result_cache_sysfs_attr);

	return ret;
}

void scan_result_cache_sysfs_finish(struct morse *mors)
{
	if (!mors->scan_res_cache.is_enabled)
		return;

	device_remove_bin_file(mors->dev, &scan_result_cache_sysfs_attr);
	device_remove_file(mors->dev, &dev_attr_scan_result_cache_clear);
}

void scan_result_cache_destroy(struct morse *mors)
{
	scan_result_cache_flush(mors);
}
