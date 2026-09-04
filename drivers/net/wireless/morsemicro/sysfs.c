/*
 * Copyright 2025 Morse Micro
 */
#include "sysfs.h"
#include "debug.h"
#include "mac.h"
#include "scan_result_cache.h"
#include "morse.h"

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#define MORSE_SYSFS_EMIT(_buf, _fmt, ...) sysfs_emit(_buf, _fmt, ##__VA_ARGS__)
#define MORSE_SYSFS_EMIT_AT(_buf, _at, _fmt, ...) sysfs_emit_at(_buf, _at, _fmt, ##__VA_ARGS__)
#else
#define MORSE_SYSFS_EMIT(_buf, _fmt, ...) snprintf(_buf, PAGE_SIZE, _fmt, ##__VA_ARGS__)
#define MORSE_SYSFS_EMIT_AT(_buf, _at, _fmt, ...) \
	snprintf((_buf) + (_at), PAGE_SIZE - (_at), _fmt, ##__VA_ARGS__)
#endif

static ssize_t boot_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct morse *mors = dev_get_drvdata(dev);

	if (!mors)
		return -EINVAL;

	return MORSE_SYSFS_EMIT(buf, "%d - %s\n",
		mors->boot_code,
		morse_firmware_boot_code_to_str(mors->boot_code));
}

static ssize_t board_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct morse *mors = dev_get_drvdata(dev);

	if (!mors)
		return -EINVAL;

	if (mors->board_id < 0)
		return mors->board_id;

	return MORSE_SYSFS_EMIT(buf, "%d\n", mors->board_id);
}

static ssize_t countries_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct morse *mors = dev_get_drvdata(dev);
	int i, len = 0;

	if (!mors || !mors->regdoms.list || mors->regdoms.count <= 0)
		return -EINVAL;

	for (i = 0; i < mors->regdoms.count; i++) {
		len += MORSE_SYSFS_EMIT_AT(buf, len, "%s ", mors->regdoms.list[i]);
		if (len >= PAGE_SIZE)
			break;
	}

	if (len > 0 && buf[len - 1] == ' ')
		buf[len - 1] = '\n';

	return len;
}

static ssize_t firmware_path_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len;
	char *fw_path;
	struct morse *mors;

	mors = dev_get_drvdata(dev);
	if (!mors)
		return -ENODEV;

	fw_path = morse_firmware_build_fw_path(mors);
	if (!fw_path)
		return -ENOMEM;

	len = MORSE_SYSFS_EMIT(buf, "%s\n", fw_path);

	kfree(fw_path);
	return len;
}

static DEVICE_ATTR_RO(boot_code);
static DEVICE_ATTR_RO(board_type);
static DEVICE_ATTR_RO(countries);
static DEVICE_ATTR_RO(firmware_path);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_boot_code.attr,
	&dev_attr_board_type.attr,
	&dev_attr_countries.attr,
	&dev_attr_firmware_path.attr,
	NULL,
};

static const struct attribute_group sysfs_attr_group = {
	.attrs = sysfs_attrs,
};

static ssize_t aid_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct morse_vif_sysfs *entry = container_of(attr, struct morse_vif_sysfs, aid);
	struct morse_vif *mors_vif = container_of(entry, struct morse_vif, sysfs);
	struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_vif);
	unsigned int aid = 0;

	if (morse_mac_is_sta_vif_associated(vif))
		aid = morse_mac_sta_aid(vif);

	return MORSE_SYSFS_EMIT(buf, "%u\n", aid);
}

int morse_sysfs_vif_add(struct morse *mors, struct morse_vif *mors_vif)
{
	int ret;
	struct morse_vif_sysfs *entry;
	char *str;

	if (WARN_ON(!mors || !mors->dev || !mors_vif)) {
		ret = -EINVAL;
		goto exit;
	}

	if (is_fullmac_mode()) {
		/* not supported - do not register */
		ret = 0;
		goto exit;
	}

	entry = &mors_vif->sysfs;
	if (entry->registered) {
		/* Allow already registered entries if mlme is in reconfig */
		ret = morse_mlme_in_reconfig(mors) ? 0 : -EEXIST;
		goto exit;
	}

	str = kasprintf(GFP_KERNEL, "vif%d", mors_vif->id);
	if (!str) {
		ret = -ENOMEM;
		goto exit;
	}

	/* Initialise group attributes */
	memset(entry->attrs, 0, sizeof(entry->attrs));

	switch (morse_vif_to_ieee80211_vif(mors_vif)->type) {
	case NL80211_IFTYPE_STATION:
		/* Initialise AID attribute */
		sysfs_attr_init(&entry->aid.attr);
		entry->aid.attr.name = "aid";
		entry->aid.attr.mode = 0444;
		entry->aid.show = aid_show;
		entry->attrs[0] = &entry->aid.attr;
		break;
	default:
		/* Attributes for other VIF types are yet to be exposed */
		break;
	}

	/* Set group information */
	entry->attr_g.name = str;
	entry->attr_g.attrs = entry->attrs;

	/* Create group */
	ret = sysfs_create_group(&mors->dev->kobj, &entry->attr_g);
	if (ret) {
		kfree(str);
		entry->attr_g.name = NULL;
		goto exit;
	}

	entry->registered = true;
exit:
	if (ret)
		MORSE_ERR(mors, "%s: failed (ret:%d)\n", __func__, ret);

	return ret;
}

int morse_sysfs_vif_remove(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_vif_sysfs *entry;

	if (WARN_ON(!mors || !mors->dev || !mors_vif))
		return -EINVAL;

	entry = &mors_vif->sysfs;
	if (!entry->registered)
		return 0;

	sysfs_remove_group(&mors->dev->kobj, &entry->attr_g);
	kfree(entry->attr_g.name);
	entry->attr_g.name = NULL;
	entry->registered = false;

	return 0;
}

int morse_sysfs_init(struct morse *mors)
{
	int ret;

	ret = scan_result_cache_sysfs_init(mors);
	if (ret)
		goto err;

	ret = sysfs_create_group(&mors->dev->kobj, &sysfs_attr_group);
	if (ret < 0)
		goto err_finish_scan_cache;

	return 0;

err_finish_scan_cache:
	scan_result_cache_sysfs_finish(mors);
err:
	MORSE_ERR(mors, "%s: failed (ret:%d)\n", __func__, ret);
	return ret;
}

void morse_sysfs_finish(struct morse *mors)
{
	sysfs_remove_group(&mors->dev->kobj, &sysfs_attr_group);
	scan_result_cache_sysfs_finish(mors);
}
