#ifndef _MORSE_DEBUG_H_
#define _MORSE_DEBUG_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include "morse.h"
#include "skb_header.h"

#define MORSE_MSG_DEBUG_USB 0x00000010
#define MORSE_MSG_ERR       0x00000008
#define MORSE_MSG_WARN      0x00000004
#define MORSE_MSG_INFO      0x00000002
#define MORSE_MSG_DEBUG     0x00000001

__printf(3, 4)
void __morse_dbg(u32 level, struct morse *mors, const char *fmt, ...);
__printf(3, 4)
void __morse_info(u32 level, struct morse *mors, const char *fmt, ...);
__printf(3, 4)
void __morse_warn(u32 level, struct morse *mors, const char *fmt, ...);
__printf(3, 4)
void __morse_warn_ratelimited(u32 level, struct morse *mors, const char *fmt, ...);
__printf(3, 4)
void __morse_err(u32 level, struct morse *mors, const char *fmt, ...);
__printf(3, 4)
void __morse_err_ratelimited(u32 level, struct morse *mors, const char *fmt, ...);


#ifndef morse_dbg_usb
#define morse_dbg_usb(core, f, a...)	\
		__morse_dbg((debug_mask & MORSE_MSG_DEBUG_USB), core, f, ##a)
#endif
#ifndef morse_dbg
#define morse_dbg(core, f, a...)	\
		__morse_dbg((debug_mask & MORSE_MSG_DEBUG), core, f, ##a)
#endif
#ifndef morse_info
#define morse_info(core, f, a...)	\
		__morse_info((debug_mask & MORSE_MSG_INFO), core, f, ##a)
#endif
#ifndef morse_warn
#define morse_warn(core, f, a...)	\
		__morse_warn((debug_mask & MORSE_MSG_WARN) ? 0xFFFFFFFF : 0, core, f, ##a)
#endif
#ifndef morse_warn_ratelimited
#define morse_warn_ratelimited(core, f, a...)	\
		__morse_warn_ratelimited((debug_mask & MORSE_MSG_WARN) ? 0xFFFFFFFF : 0, core, f, ##a)
#endif
#ifndef morse_err
#define morse_err(core, f, a...)	\
		__morse_err((debug_mask & MORSE_MSG_ERR) ? 0xFFFFFFFF : 0, core, f, ##a)
#endif
#ifndef morse_err_ratelimited
#define morse_err_ratelimited(core, f, a...)	\
		__morse_err_ratelimited((debug_mask & MORSE_MSG_ERR) ? 0xFFFFFFFF : 0, core, f, ##a)
#endif


#ifndef MORSE_WARN_ON
#define MORSE_WARN_ON(condition) \
	do { \
		if (debug_mask & MORSE_MSG_WARN) \
			WARN_ON(condition); \
		else if (debug_mask && (condition)) \
			pr_warn("%s:%d: WARN_ON ASSERTED\n", __func__, __LINE__); \
	} while (0)
#endif

#ifndef morse_pr_warn
#define morse_pr_warn(f, a...)	\
	do { \
		if (debug_mask & MORSE_MSG_WARN) \
			pr_warn(f, ##a); \
	} while (0)
#endif

#ifndef morse_pr_err
#define morse_pr_err(f, a...)	\
	do { \
		if (debug_mask & MORSE_MSG_ERR) \
			pr_err(f, ##a); \
	} while (0)
#endif

extern uint debug_mask;
int morse_init_debug(struct morse *mors);

void morse_deinit_debug(struct morse *mors);

int morse_debug_log_tx_status(struct morse *mors,
	struct morse_skb_tx_status *tx_sts);

enum morse_fw_hostif_log_channel_enable {
	MORSE_HOSTIF_LOG_DATA		= BIT(0),
	MORSE_HOSTIF_LOG_COMMAND	= BIT(1),
	MORSE_HOSTIF_LOG_TX_STATUS	= BIT(2)
};
void morse_debug_fw_hostif_log_record(struct morse *morse, int to_chip,
				      struct sk_buff *skb, struct morse_buff_skb_header *hdr);

int morse_coredump(struct morse *mors);

#ifdef CONFIG_MORSE_RC
void mmrc_s1g_add_sta_debugfs(struct morse *mors);
#endif

#endif  /* !_MORSE_DEBUG_H_ */
