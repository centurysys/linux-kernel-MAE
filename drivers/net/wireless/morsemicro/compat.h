/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This header file is used to hide elements that must be used but would cause
 * the checkpatch utility to complain.
 */

#include "dot11ah/s1g_ieee80211.h"
#pragma once
#include <linux/version.h>
#include <linux/random.h>
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
#include <linux/refcount.h>
#else
#include <linux/atomic.h>
typedef atomic_t refcount_t;
static inline void refcount_set(refcount_t *r, int v)
{
	atomic_set(r, v);
}
static inline void refcount_inc(refcount_t *r)
{
	atomic_inc(r);
}
static inline bool refcount_dec_and_test(refcount_t *r)
{
	return atomic_dec_and_test(r);
}
#endif

static inline u32 morse_random_u32_max(u32 max)
{
#if KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE
	return prandom_u32_max(max);
#else
	return get_random_u32_below(max);
#endif
}

/* Checkpatch does not like Camel Case */
#define morse_elf_ehdr Elf32_Ehdr
#define morse_elf_shdr Elf32_Shdr
#define morse_elf_phdr Elf32_Phdr

#define morse_compat_desc_num_endpoints(_desc) ((_desc).bNumEndpoints)

/* Checkpatch does not like used of the keyword 'fallthrough' */

/**
 * Define fallthrough explicitly for kernels that don't have compiler_attributes.h.
 */
#if KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE
#ifndef fallthrough
#if __has_attribute(__fallthrough__)
#define fallthrough	__attribute__((__fallthrough__))
#else
#define fallthrough	do {} while (0)	/* fallthrough */
#endif
#endif
#endif

#if !defined(struct_size)
/* Older kernels dont support this. Just add a simple def for it */
#define struct_size(p, member, count)					\
	(sizeof(*(p)) + (sizeof(*(p)->member) * count))
#endif

#if !defined(EM_RISCV)
#define EM_RISCV 243
#endif

#if KERNEL_VERSION(4, 9, 61) < LINUX_VERSION_CODE
#define morse_in_interrupt() (!in_task())
#else
#define morse_in_interrupt() in_interrupt()
#endif

#if KERNEL_VERSION(6, 7, 0) <= MAC80211_VERSION_CODE
    #define MORSE_IEEE80211_TX_STATUS(_hw, _skb) ieee80211_tx_status_skb(_hw, _skb)
#else
    #define MORSE_IEEE80211_TX_STATUS(_hw, _skb) ieee80211_tx_status(_hw, _skb)
#endif

#if KERNEL_VERSION(6, 9, 0) <= MAC80211_VERSION_CODE
    #define MORSE_IEEE80211_CSA_FINISH(_vif) ieee80211_csa_finish(_vif, 0)
#else
    #define MORSE_IEEE80211_CSA_FINISH(_vif) ieee80211_csa_finish(_vif)
#endif

#if KERNEL_VERSION(6, 9, 0) <= MAC80211_VERSION_CODE
    #define MORSE_BSS_CONF_CHAN_DEF(_vif)   (&_vif->bss_conf.chanreq.oper)
#else
    #define MORSE_BSS_CONF_CHAN_DEF(_vif)   (&_vif->bss_conf.chandef)
#endif

#if KERNEL_VERSION(6, 9, 0) <= MAC80211_VERSION_CODE
    #define MORSE_IEEE80211_CSA_COMPLETE(_vif)  ieee80211_beacon_cntdwn_is_complete(_vif, 0)
#elif KERNEL_VERSION(5, 10, 0) < MAC80211_VERSION_CODE
    #define MORSE_IEEE80211_CSA_COMPLETE(_vif)  ieee80211_beacon_cntdwn_is_complete(_vif)
#else
    #define MORSE_IEEE80211_CSA_COMPLETE(_vif)  ieee80211_csa_is_complete(_vif)
#endif

#if KERNEL_VERSION(7, 0, 0) <= MAC80211_VERSION_CODE
#define MORSE_ADDBA_ACTION_CODE(_mgmt) ((_mgmt)->u.action.action_code)
#define MORSE_ADDBA_REQ_CAPAB(_mgmt) ((_mgmt)->u.action.addba_req.capab)
#define MORSE_ADDBA_RESP_CAPAB(_mgmt) ((_mgmt)->u.action.addba_resp.capab)
#define MORSE_IEEE80211_MIN_ACTION_SIZE_CATEGORY IEEE80211_MIN_ACTION_SIZE(category)
#define MORSE_IEEE80211_MIN_ACTION_SIZE_ACTION_CODE IEEE80211_MIN_ACTION_SIZE(action_code)
#else
#define MORSE_ADDBA_ACTION_CODE(_mgmt) ((_mgmt)->u.action.u.addba_req.action_code)
#define MORSE_ADDBA_REQ_CAPAB(_mgmt) ((_mgmt)->u.action.u.addba_req.capab)
#define MORSE_ADDBA_RESP_CAPAB(_mgmt) ((_mgmt)->u.action.u.addba_resp.capab)
#define MORSE_IEEE80211_MIN_ACTION_SIZE_CATEGORY IEEE80211_MIN_ACTION_SIZE
#define MORSE_IEEE80211_MIN_ACTION_SIZE_ACTION_CODE (IEEE80211_MIN_ACTION_SIZE + 1)
#endif

#if KERNEL_VERSION(4, 17, 0) > MAC80211_VERSION_CODE
/**
 * ieee80211_get_tid - get the QoS TID for a frame
 *
 * @hdr: Frame header
 *
 * @returns: TID
 */
static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}
#endif

#if KERNEL_VERSION(6, 18, 0) <= LINUX_VERSION_CODE
typedef const struct bin_attribute morse_bin_attr_t;
#else
typedef struct bin_attribute morse_bin_attr_t;
#endif

#define MORSE_IEEE80211_GET_TID(_hdr) ieee80211_get_tid(_hdr)

#if KERNEL_VERSION(5, 3, 0) > MAC80211_VERSION_CODE
#define SAE_PASSWORD_MAX_LEN 128
#endif

/**
 * morse_wiphy_radio_init - describe this device's single physical radio to the wiphy
 *
 * On more recent kernels, some features are only enabled once wiphy->n_radio is set.
 * Setup radio and n_radio in order to have support for more such features introduced
 * by the kernel in the future
 *
 * @wiphy: wiphy to configure
 * @comb: interface combination already built for wiphy->iface_combinations,
 *	reused here since there is only one radio
 *
 * @returns: true on success, false if the radio descriptor could not be allocated
 */
static inline bool morse_wiphy_radio_init(struct wiphy *wiphy,
					   struct ieee80211_iface_combination *comb)
{
#if KERNEL_VERSION(6, 17, 0) <= MAC80211_VERSION_CODE
	static const struct wiphy_radio_freq_range mors_radio_freq_range[] = {
		{
			.start_freq = MHZ_TO_KHZ(5170),
			.end_freq = MHZ_TO_KHZ(5895),
		},
	};
	struct wiphy_radio *radio = kcalloc(1, sizeof(*radio), GFP_KERNEL);

	if (!radio)
		return false;

	radio->freq_range = mors_radio_freq_range;
	radio->n_freq_range = ARRAY_SIZE(mors_radio_freq_range);
	radio->iface_combinations = comb;
	radio->n_iface_combinations = 1;
	wiphy->radio = radio;
	wiphy->n_radio = 1;
#endif
	return true;
}

/**
 * morse_wiphy_radio_deinit - free the radio descriptor set up by morse_wiphy_radio_init()
 *
 * @wiphy: wiphy to clean up
 */
static inline void morse_wiphy_radio_deinit(struct wiphy *wiphy)
{
#if KERNEL_VERSION(6, 17, 0) <= MAC80211_VERSION_CODE
	kfree(wiphy->radio);
	wiphy->radio = NULL;
	wiphy->n_radio = 0;
#endif
}
