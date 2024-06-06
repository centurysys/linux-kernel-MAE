/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <asm/div64.h>

#include "command.h"
#include "morse.h"
#include "mac.h"
#include "utils.h"
#include "wiphy.h"
#include "debug.h"


struct morse *morse_wiphy_to_morse(struct wiphy *wiphy)
{
	struct ieee80211_hw *hw;


	/* In softmac mode, mac80211 has installed struct ieee80211_hw as the priv structure
	 * in wiphy, ours is inside that.
	 */
	hw = wiphy_to_ieee80211_hw(wiphy);
	return hw->priv;
}

