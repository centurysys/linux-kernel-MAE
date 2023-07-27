#ifndef _MORSE_SKB_HEADER_H_
#define _MORSE_SKB_HEADER_H_

/*
 * Copyright 2021 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/skbuff.h>

#include "misc.h"

/** Sync value of skb header to indicate a valid skb */
#define MORSE_SKB_HEADER_SYNC            (0xAA)
/** Sync value indicating that the chip owns this skb */
#define MORSE_SKB_HEADER_CHIP_OWNED_SYNC (0xBB)

/**
 * enum morse_tx_status_and_conf_flags - TX status flags
 * @MORSE_TX_STATUS_FLAGS_NO_ACK: Whether the frame was acknowledged or not
 * @MORSE_TX_STATUS_FLAGS_NO_REPORT: Whether to generate no report
 * @MORSE_TX_CONF_FLAGS_CTL_AMPDU: This frame should be sent as part of an AMPDU
 * @MORSE_TX_CONF_FLAGS_HW_ENCRYPT: This frame should be encrypted with hardware
 * @MORSE_TX_CONF_FLAGS_VIF_ID: Virtual Interface ID
 * @MORSE_TX_CONF_FLAGS_KEY_IDX: If a group/multicast frame, use this key index
 * @MORSE_TX_STATUS_FLAGS_PS_FILTERED: Whether the frame was returned as the
 *                                     destination/sender is entering power save
 * @MORSE_TX_CONF_IGNORE_TWT: If the device is operating under TWT based
 *                            power-save, it should ignore service period rules
 *                            and unconditionally transmit this frame.
 * @MORSE_TX_STATUS_PAGE_INVALID: Page is in an unexpected state.
 * @MORSE_TX_CONF_FLAGS_IMMEDIATE_REPORT: TX status for this transmission
 *                                        should be reported immediately to the
 *                                        UMAC.
 * @MORSE_TX_CONF_NO_PS_BUFFER: This frame is a response to a poll frame
 *                              (PS-Poll or uAPSD) or a non-bufferable MMPDU
 *                              and must be sent although the station is in
 *                              powersave mode.
 *
 * NOTE: Because morse_skb_tx_rx_info is treated as a union the following
 *       bit fields cannot overlap.
 */
enum morse_tx_status_and_conf_flags {
	MORSE_TX_STATUS_FLAGS_NO_ACK      = BIT(0),
	MORSE_TX_STATUS_FLAGS_NO_REPORT   = BIT(1),
	MORSE_TX_CONF_FLAGS_CTL_AMPDU     = BIT(2),
	MORSE_TX_CONF_FLAGS_HW_ENCRYPT    = BIT(3),
	MORSE_TX_CONF_FLAGS_VIF_ID        =
		(BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11)),
	MORSE_TX_CONF_FLAGS_KEY_IDX       = (BIT(12) | BIT(13) | BIT(14)),
	MORSE_TX_STATUS_FLAGS_PS_FILTERED = (BIT(15)),
	MORSE_TX_CONF_IGNORE_TWT          = (BIT(16)),
	MORSE_TX_STATUS_PAGE_INVALID      = (BIT(17)),
	MORSE_TX_CONF_NO_PS_BUFFER        = (BIT(18)),
	MORSE_TX_CONF_FLAGS_IMMEDIATE_REPORT = (BIT(31))
};

/** Getter and setter macros for vif id */
#define MORSE_TX_CONF_FLAGS_VIF_ID_MASK (0xFF)
#define MORSE_TX_CONF_FLAGS_VIF_ID_SET(x) (((x) & MORSE_TX_CONF_FLAGS_VIF_ID_MASK) << 4)
#define MORSE_TX_CONF_FLAGS_VIF_ID_GET(x) (((x) & MORSE_TX_CONF_FLAGS_VIF_ID) >> 4)

/** Getter and setter macros for key index */
#define MORSE_TX_CONF_FLAGS_KEY_IDX_SET(x) (((x) & 0x07) << 12)
#define MORSE_TX_CONF_FLAGS_KEY_IDX_GET(x) (((x) & MORSE_TX_CONF_FLAGS_KEY_IDX) >> 12)

/**
 * enum morse_rx_status_flags - RX status flags
 * @MORSE_RX_STATUS_FLAGS_ERROR: This frame had some error
 * @MORSE_RX_STATUS_FLAGS_DECRYPTED: This frame was decrypted in hardware
 *  (CCMP header / IV / MIC are still included)
 * @MORSE_RX_STATUS_FLAGS_FCS_INCLUDED: This frame includes
 *  the received 4 octet FCS
 * @MORSE_RX_STATUS_FLAGS_EOF: This frame was received as part of an AMPDU
 *  and had the EOF bit set (S-MPDU)
 * @MORSE_RX_STATUS_FLAGS_AMPDU: This frame was received as part of an AMPDU
 * @MORSE_RX_STATUS_FLAGS_NDP_2MHZ: This NDP frame is 2MHz BW
 * @MORSE_RX_STATUS_FLAGS_NDP_1MHZ: This NDP frame is 1MHz BW
 * @MORSE_RX_STATUS_FLAGS_NDP: This frame is a NDP
 * @MORSE_RX_STATUS_FLAGS_UPLINK: This frame had an uplink indication
 * @MORSE_RX_STATUS_FLAGS_RI: Response Indication Value Bits 9-10
 * @MORSE_RX_STATUS_FLAGS_NDP_TYPE: NDP type
 * @MORSE_RX_STATUS_FLAGS_PPDU_FMT: This frames PPDU format
 * @MORSE_RX_STATUS_FLAGS_SGI: If this frame uses SGI
 */
enum morse_rx_status_flags {
	MORSE_RX_STATUS_FLAGS_ERROR         = BIT(0),
	MORSE_RX_STATUS_FLAGS_DECRYPTED     = BIT(1),
	MORSE_RX_STATUS_FLAGS_FCS_INCLUDED  = BIT(2),
	MORSE_RX_STATUS_FLAGS_EOF           = BIT(3),
	MORSE_RX_STATUS_FLAGS_AMPDU         = BIT(4),
	MORSE_RX_STATUS_FLAGS_NDP_2MHZ      = BIT(5),
	MORSE_RX_STATUS_FLAGS_NDP_1MHZ      = BIT(6),
	MORSE_RX_STATUS_FLAGS_NDP           = BIT(7),
	MORSE_RX_STATUS_FLAGS_UPLINK        = BIT(8),
	MORSE_RX_STATUS_FLAGS_RI            = (BIT(9) | BIT(10)),
	MORSE_RX_STATUS_FLAGS_NDP_TYPE      = (BIT(11) | BIT(12) | BIT(13)),
	MORSE_RX_STATUS_FLAGS_PPDU_FMT      = (BIT(14) | BIT(15)),
	MORSE_RX_STATUS_FLAGS_SGI           = BIT(16),
};

/** Getter macro for guard interval */
#define MORSE_RX_STATUS_FLAGS_UPL_IND_GET(x) \
	(((x) & MORSE_RX_STATUS_FLAGS_UPLINK) >> 8)

/** Getter macro for response indication */
#define MORSE_RX_STATUS_FLAGS_RI_GET(x) (((x) & MORSE_RX_STATUS_FLAGS_RI) >> 9)

/** Getter macro for NDP type */
#define MORSE_RX_STATUS_FLAGS_NDP_TYPE_GET(x) \
	(((x) & MORSE_RX_STATUS_FLAGS_NDP_TYPE) >> 11)

/** Getter macro for PPDU format */
#define MORSE_RX_STATUS_FLAGS_PPDU_FMT_GET(x) \
	(((x) & MORSE_RX_STATUS_FLAGS_PPDU_FMT) >> 14)

/** Getter macro for guard interval */
#define MORSE_RX_STATUS_FLAGS_SGI_GET(x) \
	(((x) & MORSE_RX_STATUS_FLAGS_SGI) >> 16)

/**
 * enum morse_skb_rate_flags - tx_info rate control flags
 * @MORSE_SKB_RATE_FLAGS_RTS: Use RTS/CTS
 * @MORSE_SKB_RATE_FLAGS_CTS: Use CTS-to-self
 * @MORSE_SKB_RATE_FLAGS_SGI: Use Short GI
 * @MORSE_SKB_RATE_FLAGS_1MHZ: Use 1MHz
 * @MORSE_SKB_RATE_FLAGS_2MHZ: Use 2MHz
 * @MORSE_SKB_RATE_FLAGS_4MHZ: Use 4MHz
 * @MORSE_SKB_RATE_FLAGS_8MHZ: Use 8MHz
 * @MORSE_SKB_RATE_FLAGS_CTRL_RESP_1MHZ: Control response frames to this frame
 *                                       to this frame will use 1MHz BW
 */
enum morse_skb_rate_flags {
	MORSE_SKB_RATE_FLAGS_RTS		= BIT(0),
	MORSE_SKB_RATE_FLAGS_CTS		= BIT(1),
	MORSE_SKB_RATE_FLAGS_SGI		= BIT(2),
	MORSE_SKB_RATE_FLAGS_1MHZ		= BIT(3),
	MORSE_SKB_RATE_FLAGS_2MHZ		= BIT(4),
	MORSE_SKB_RATE_FLAGS_4MHZ		= BIT(5),
	MORSE_SKB_RATE_FLAGS_8MHZ		= BIT(6),
	MORSE_SKB_RATE_FLAGS_USE_TRAV_PILOT	= BIT(9),
	MORSE_SKB_RATE_FLAGS_CTRL_RESP_1MHZ	= BIT(10),
};

#define MORSE_BW_TO_FLAGS(X)				\
	(((X) == 1) ? MORSE_SKB_RATE_FLAGS_1MHZ :	\
	((X) == 2) ? MORSE_SKB_RATE_FLAGS_2MHZ :	\
	((X) == 4) ? MORSE_SKB_RATE_FLAGS_4MHZ :	\
	((X) == 8) ? MORSE_SKB_RATE_FLAGS_8MHZ :	\
	MORSE_SKB_RATE_FLAGS_2MHZ)

#define MORSE_SKB_RATE_FLAGS_RTS_POS   (0)
#define MORSE_SKB_RATE_FLAGS_SGI_POS   (2)

/* Get tx guard interval */
#define MORSE_SKB_RATE_FLAGS_SGI_GET(x) \
	(((x) & MORSE_SKB_RATE_FLAGS_SGI) >> MORSE_SKB_RATE_FLAGS_SGI_POS)

/* Get tx RTS flag */
#define MORSE_SKB_RATE_FLAGS_RTS_GET(x) \
	(((x) & MORSE_SKB_RATE_FLAGS_RTS) >> MORSE_SKB_RATE_FLAGS_RTS_POS)

/**
 * enum morse_skb_channel - SKB header channel mapping
 * @MORSE_SKB_CHAN_DATA: Payload is normal data
 * @MORSE_SKB_CHAN_NDP_FRAMES: Payload is NDP frames (from chip only)
 * @MORSE_SKB_CHAN_DATA_NOACK: Data that does not generate an ack
 *                             (ie command response or tx status)
 * @MORSE_SKB_CHAN_BEACON: Payload is a beacon
 * @MORSE_SKB_CHAN_MGMT: Payload is a management frame
 * @MORSE_SKB_CHAN_LOOPBACK: Payload should be looped back untouched
 * @MORSE_SKB_CHAN_COMMAND: Payload is a command
 * @MORSE_SKB_CHAN_TX_STATUS: Payload is TX status (from chip only)
 */
enum morse_skb_channel {
	MORSE_SKB_CHAN_DATA = 0x0,
	MORSE_SKB_CHAN_NDP_FRAMES = 0x1,
	MORSE_SKB_CHAN_DATA_NOACK = 0x2,
	MORSE_SKB_CHAN_BEACON = 0x3,
	MORSE_SKB_CHAN_MGMT = 0x4,
	MORSE_SKB_CHAN_LOOPBACK = 0xEE,
	MORSE_SKB_CHAN_COMMAND = 0xFE,
	MORSE_SKB_CHAN_TX_STATUS = 0xFF
};

/** Maximum number of rates in the TX info
 *
 * @warning Do not change this unless you know what you're doing and have a
 * clear understanding of the implications of doing so
 *
 */
#define MORSE_SKB_MAX_RATES (4)

/**
 * struct morse_skb_rate_info: rate control information
 * @mcs: The MCS index to use. If -1 then no more are present
 * @count: The number of times to try this MCS rate
 * @flags: The flags for this frame
 */
struct morse_skb_rate_info {
	s8 mcs;
	u8 count;
	__le16 flags;
} __packed;

/**
 * struct morse_skb_tx_status: TX status feedback
 * @flags: TX flags for this frame
 * @pkt_id: SKB packet id to match against tx_status
 * @ampdu_info: Info about which AMPDU this skb belonged to (if any).
 * @rates: rates an counts used
 */
struct morse_skb_tx_status {
	__le32    flags;
	__le32    pkt_id;
	u8 tid;
	/** The MORSE_SKB_CHAN_xxx that the frame being reported on belongs to */
	u8 channel;
	/**
	 * Set to 0 if not ampdu (including smpdu).
	 * Split into 3 fields:
	 * | tag (6b) | ampdu_len (5b) | success_len (5b) |
	 * tag: Identifier for this aggregation (wraps frequently)
	 * ampdu_len: Number of MPDUs in AMPDU as transmitted
	 * success_len: Number of MPDUs successfuly received
	 */
	__le16 ampdu_info;
	struct morse_skb_rate_info rates[MORSE_SKB_MAX_RATES];
} __packed;

/* Getter macro for ampdu_info field in struct morse_skb_tx_status */
#define MORSE_TXSTS_AMPDU_INFO_GET_TAG(x)   (((x) >> 10) & 0x3F)
#define MORSE_TXSTS_AMPDU_INFO_GET_LEN(x)   (((x) >> 5) & 0x1F)
#define MORSE_TXSTS_AMPDU_INFO_GET_SUC(x)  ((x) & 0x1F)

/**
 * struct morse_skb_tx_info: TX information
 * @flags: TX flags for this frame
 * @pkt_id: SKB packet id to match against tx_info
 * @tid: TID
 * @tid_params: TID parameters
 * @rates: rates an counts to use
 */
struct morse_skb_tx_info {
	__le32    flags;
	__le32    pkt_id;
	u8 tid;
	u8 tid_params;
	u8 padding[2];
	struct morse_skb_rate_info rates[MORSE_SKB_MAX_RATES];
} __packed;

/*
 * Bitmap for tid_params in struct morse_skb_tx_info
 * Max reorder buffer size is bound by max MSDUs per A-MPDU. Value is 0-indexed (ie 0b00000 = 1,
 * 0b11111 = 32). Be sure to add 1 before you use it!
 */
#define TX_INFO_TID_PARAMS_MAX_REORDER_BUF	0x1f
#define TX_INFO_TID_PARAMS_AMPDU_ENABLED	0x20
#define TX_INFO_TID_PARAMS_AMSDU_SUPPORTED	0x40
#define TX_INFO_TID_PARAMS_USE_LEGACY_BA	0x80

/**
 * struct morse_skb_rx_status: RX status feedback
 * @flags: RX flags for this frame
 * @mcs: The MCS index the frame was received at
 * @bw_mhz: Bandwidth in MHz
 * @rssi: The RSSI of the received frame
 * @freq_hz: The frequency the frame was received on in Hz
 * @rx_timestamp_us: When STA or AP, this is the value of the TSF timer.
 *                   In monitor mode this is the value of the chip's local timer
 *                   when the frame was first detected.
 *                   Note: currently TSF is not implemented so
 *                   when STA or AP the chip's local timer is used.
 * @bss_color: The BSS color of the received frame
 */
struct morse_skb_rx_status {
	__le32	flags;
	u8	rate;
	u8	bw_mhz;
	__le16	rssi;
	__le32	freq_hz;
	__le64	rx_timestamp_us;
	u8	bss_color;
	/** Padding for word alignment */
	u8  padding[3];
} __packed;

/**
 * struct morse_buff_skb_header - morse skb header
 *
 * structure size should be word aligned
 *
 * @sync: synchroniztion byte for verification
 * @channel: flags for the skb. Mapping from enum morse_skb_channel
 * @len: length of data section
 * @tail: padding in tail so skb can by word aligned
 * @pad: padding
 * @tx_info: TX information
 * @tx_status: TX status feedback
 * @rx_status: RX status feedback
 */
struct morse_buff_skb_header {
	u8 sync;
	u8 channel;
	__le16 len;
	u8 tail;
	u8 pad[3];
	union {
		struct morse_skb_tx_info tx_info;
		struct morse_skb_tx_status tx_status;
		struct morse_skb_rx_status rx_status;
	};
} __packed;

#endif  /* !_MORSE_SKB_HEADER_H_ */
