/*
 * Copyright 2025 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _MORSE_WOWLAN_H_
#define _MORSE_WOWLAN_H_

#include <net/mac80211.h>

#include "morse.h"

/**
 * morse_mac_init_wowlan() - Initialize WoWLAN
 *
 * @morse: Morse structure
 */
void morse_init_wowlan(struct morse *mors);

/**
 * morse_wowlan_op_suspend() - suspend the device after configuring the WoWLAN triggers.
 * @hw: Pointer to hw structure
 * @wowlan: Contains the enabled wake-on-wireless triggers that are configured for this
 *          device, if any (NULL if none are configured).
 *
 * @returns: 0 on success or error code on failure.
 *           Non-zero return value indicates to mac80211 that all interfaces should be torn down
 *           and the driver removed
 *
 */
int morse_wowlan_op_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan);

/**
 * morse_wowlan_op_resume() - resume the device from suspend state.
 *
 * @hw: Pointer to hw structure
 *
 * @returns: 0 on success. Returning 1 from this function results in mac80211 restarting
 *           the hardware, any other error code will result in the device being unregistered.
 *
 */
int morse_wowlan_op_resume(struct ieee80211_hw *hw);

/**
 * morse_wowlan_op_set_wakeup() - Called when WoWLAN is enabled/disabled.
 *
 * @hw: Pointer to hw structure
 * @enabled: Indicate whether wakeup source needs to be enabled or disabled.
 */
void morse_wowlan_op_set_wakeup(struct ieee80211_hw *hw, bool enabled);
#endif
