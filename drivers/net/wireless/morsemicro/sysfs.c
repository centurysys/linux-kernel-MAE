/*
 * Copyright 2024 Morse Micro
 *
 */

#include <linux/sysfs.h>
#include "morse.h"
#include "sysfs.h"
#include "debug.h"

static ssize_t board_type_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct morse *mors = dev_get_drvdata(dev);

	if (!mors)
		return -EINVAL;

	if (mors->board_id < 0)
		return mors->board_id;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	return sysfs_emit(buf, "%d\n", mors->board_id);
#else
	return snprintf(buf, PAGE_SIZE, "%d\n", mors->board_id);
#endif
}

static DEVICE_ATTR_RO(board_type);

int morse_sysfs_init(struct morse *mors)
{
	int ret;

	ret = device_create_file(mors->dev, &dev_attr_board_type);
	if (ret < 0)
		MORSE_ERR(mors, "failed to create sysfs file board_type");

	return ret;
}

void morse_sysfs_free(struct morse *mors)
{
	device_remove_file(mors->dev, &dev_attr_board_type);
}
