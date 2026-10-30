// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Morse Micro
 *
 * Per-VIF debugfs files under netdev:<iface>/morse/.
 * Registered via the .vif_add_debugfs mac80211 callback.
 * This file is only compiled when CONFIG_MAC80211_DEBUGFS=y.
 */

#include <linux/debugfs.h>
#include <linux/ieee80211.h>

#include "morse.h"
#include "debug.h"

#if KERNEL_VERSION(6, 7, 0) <= MAC80211_VERSION_CODE

static ssize_t morse_vif_listen_interval_read(struct file *file, char __user *userbuf,
					      size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);
	struct morse *mors = morse_vif_to_morse(mors_vif);
	struct morse_custom_configs *cfg = mors_vif->custom_configs;
	u16 s1g_li = cfg->listen_interval_ovr ? cfg->listen_interval :
		     morse_dot11ah_listen_interval_to_s1g(mors->hw->conf.listen_interval);
	u16 usf = (s1g_li & IEEE80211_S1G_LI_USF) >> IEEE80211_S1G_LI_USF_SHIFT;
	u16 unscaled = s1g_li & IEEE80211_S1G_LI_UNSCALED_INTERVAL;
	bool sleep_enabled = mors_vif->is_ps_enabled &&
			     cfg->listen_interval_ovr &&
			     cfg->listen_interval > vif->bss_conf.dtim_period;
	ssize_t bufsz = 256;
	ssize_t ret;
	char *buf, *p;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	/*
	 * Figure 9-144: Listen Interval field format carried in an S1G PPDU (802.11-2024
	 * 9.4.1.6). The decoded (Unscaled x scaling factor) value is in units of beacon
	 * intervals.
	 */
	p += scnprintf(p, bufsz + buf - p, "%-10s  %-4s  %-4s  %-8s  %s\n",
		       "S1G Raw", "UI", "USF", "BI", "Sleep");
	p += scnprintf(p, bufsz + buf - p, "%-10u  %-4u  %-4u  %-8u  %s\n",
		       s1g_li, unscaled, usf, morse_dot11ah_s1g_to_listen_interval(s1g_li),
		       sleep_enabled ? "enabled" : "disabled");
	p += scnprintf(p, bufsz + buf - p,
		       "\nUI = Unscaled Interval\nUSF = Unified Scaling Factor\nBI = Beacon Interval\n");

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}

static const struct file_operations morse_vif_listen_interval_fops = {
	.read   = morse_vif_listen_interval_read,
	.open   = simple_open,
	.llseek = generic_file_llseek,
};

void morse_vif_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);
	struct dentry *morse_dir;

	if (IS_ERR_OR_NULL(vif->debugfs_dir))
		return;

	morse_dir = debugfs_create_dir("morse", vif->debugfs_dir);
	if (IS_ERR_OR_NULL(morse_dir))
		return;

	debugfs_create_u16("id", 0400, morse_dir, &mors_vif->id);
	debugfs_create_bool("power_save", 0400, morse_dir, &mors_vif->is_ps_enabled);

	if (vif->type == NL80211_IFTYPE_STATION) {
		debugfs_create_file("listen_interval", 0400, morse_dir, vif,
				    &morse_vif_listen_interval_fops);
	} else if (vif->type == NL80211_IFTYPE_ADHOC) {
		debugfs_create_bool("ibss_joined", 0400, morse_dir, &mors_vif->is_ibss_node_joined);
	} else if (vif->type == NL80211_IFTYPE_MESH_POINT && mors_vif->ap) {
		debugfs_create_u16("mesh_points", 0400, morse_dir, &mors_vif->ap->num_stas);
	}

	debugfs_create_u16("s1g_bcn_change_seq", 0400, morse_dir, &mors_vif->s1g_bcn_change_seq);
}

#endif
