/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/crc32.h>
#include <linux/ieee80211.h>
#include "s1g_ieee80211.h"

/*
 * This should include all modified/missing bits from ieee80211
 */

#if KERNEL_VERSION(5, 10, 11) > MAC80211_VERSION_CODE

u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band_s1g band)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	 * there are overlapping channel numbers in 5GHz and 2GHz bands
	 */
	if (chan <= 0)
		return 0; /* not supported */
	switch (band) {
	case NL80211_BAND_S1GHZ:
		return 902000 + chan * 500;
	default:
		break;
	}
	return 0; /* not supported */
}
EXPORT_SYMBOL(ieee80211_channel_to_freq_khz);

#endif

/*
 * This is patched by Morse to support S1G
 */
int __ieee80211_freq_khz_to_channel(u32 freq)
{
	/* S1G first */
	if (freq < MHZ_TO_KHZ(1000)) {
		/*
		 * S1G channels are region-dependent,
		 * so resolving channel index from frequency
		 * requires investigating the frequency
		 * to determine region
		 */
		if (freq > 902000) {
			/* Check for US freq offset */
			if (!(freq % 500))
				return (freq - 902000) / 500;
			/* Otherwise use the EU freq offset */
			return (freq - 901400) / 500;
			}
		else
			return (freq - 863000) / 500;
	}

	/* TODO: just handle MHz for now */
	freq = KHZ_TO_MHZ(freq);

	/* see 802.11 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq < 5925)
		return (freq - 5000) / 5;
	else if (freq == 5935)
		return 2;
	else if (freq <= 45000) /* DMG band lower limit */
		/* see 802.11ax D6.1 27.3.22.2 */
		return (freq - 5950) / 5;
	else if (freq >= 58320 && freq <= 70200)
		return (freq - 56160) / 2160;
	else
		return 0;
}


