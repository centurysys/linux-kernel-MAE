#ifndef _MORSE_SYSFS_H_
#define _MORSE_SYSFS_H_
/*
 * Copyright 2025 Morse Micro
 */
#include <linux/sysfs.h>
#include <linux/device.h>
#include "morse.h"

/**
 * morse_sysfs_init() - Initialise sysfs entries for morse device
 *
 * @mors Morse context
 *
 * Return: 0 on success
 */
int morse_sysfs_init(struct morse *mors);

/**
 * morse_sysfs_finish() - Remove sysfs entries from morse device
 *
 * @mors Morse context
 */
void morse_sysfs_finish(struct morse *mors);

/**
 * morse_sysfs_vif_add() - Add a sysfs entry for a newly created virtual interface
 *
 * @mors Morse context
 * @vif VIF context
 *
 * @Return 0 or error code
 */
int morse_sysfs_vif_add(struct morse *mors, struct morse_vif *mors_vif);

/**
 * morse_sysfs_vif_remove() - Remove a sysfs entry for a virtual interface
 *
 * @mors Morse context
 * @vif VIF context
 *
 * @Return 0 or error code
 */
int morse_sysfs_vif_remove(struct morse *mors, struct morse_vif *mors_vif);

#endif /* !_MORSE_SYSFS_H_ */
