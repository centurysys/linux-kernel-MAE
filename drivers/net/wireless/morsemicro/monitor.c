/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <net/xfrm.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>

#include "debug.h"
#include "mac.h"
#include "s1g_radiotap.h"
#include "skb_header.h"

#define RT_ZERO_LEN_PSDU_DATA			0x2
#define RT_ZERO_LEN_PSDU_VENDOR_SPECIFIC	0xff

#define HZ_TO_MHZ(x)	(x / 1000000)

#ifndef IEEE80211_RADIOTAP_AMPDU_EOF
#define IEEE80211_RADIOTAP_AMPDU_EOF 0x0040
#endif

/* This should later be supported by Linux */
enum morse_ieee80211_radiotap_type {
	IEEE80211_RADIOTAP_HALOW = 22,
};

struct morse_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le64 rt_tsft;
	u8 rt_flags;
	/* Rate for PSDU with length, type for zero len PSDU */
	u8 rt_rate_or_zl_psdu;
	__le16 rt_channel;
	__le16 rt_chbitmask;
	s8 rt_dbm_antsignal;
} __packed;

/** Radiotap header with optional timestamp field used for signal field errors. */
struct morse_collision_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le64 rt_tsft;
	/* Timestamp field */
	struct {
		__le64 timestamp;
		__le16 accuracy;
		u8 unit_position;
		u8 flags;
	} timestamp;
} __packed;

struct zero_length_psdu {
	u8 psdu_type;
	u8 ndp_type;
	__le64 ndp[0];
} __packed;

struct ampdu_header {
	__le32 ref_num;
	__le16 flags;
	u8 eof_value;
	u8 reserved;
} __packed;

struct padding {
	u8 padding[1];
} __packed;

static struct net_device *morse_mon; /* global monitor netdev */

static netdev_tx_t morse_mon_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	/* TODO: allow packet injection */
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops morse_mon_ops = {
	.ndo_start_xmit		= morse_mon_xmit,
#if KERNEL_VERSION(5, 10, 11) > LINUX_VERSION_CODE
	.ndo_change_mtu		= eth_change_mtu,
#endif
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void morse_mon_setup(struct net_device *dev)
{
	dev->netdev_ops = &morse_mon_ops;
	ether_setup(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->type = ARPHRD_IEEE80211_RADIOTAP;
	eth_zero_addr(dev->dev_addr);
	dev->dev_addr[0] = 0x12;
}

/* Convers an integer bandwidth (in MHz) to an S1G radiotap header bandwidth */
static enum dot11_rt_s1g_bandwidth int_bw_to_radiotap_bw_enum(int bw_mhz)
{
	int bw;

	switch (bw_mhz) {
	case 1:
		bw = DOT11_RT_S1G_BW_1MHZ;
		break;
	case 2:
		bw = DOT11_RT_S1G_BW_2MHZ;
		break;
	case 4:
		bw = DOT11_RT_S1G_BW_4MHZ;
		break;
	case 8:
		bw = DOT11_RT_S1G_BW_8MHZ;
		break;
	case 16:
		bw = DOT11_RT_S1G_BW_16MHZ;
		break;
	default:
		bw = DOT11_RT_S1G_BW_INVALID;
		break;
	}
	return bw;
}

void morse_mon_rx(struct morse *mors, struct sk_buff *rx_skb,
		  struct morse_skb_rx_status *hdr_rx_status)
{
	struct sk_buff *skb;
	u16 flags;
	struct morse_radiotap_hdr *hdr;
	struct zero_length_psdu *psdu;
	struct ampdu_header *ampdu_hdr = NULL;
	struct radiotap_s1g_tlv *s1g_info_hdr = NULL;
	struct padding *align_padding;
	enum dot11_rt_s1g_bandwidth bw;
	int ndp_sub_type;

	if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_NDP) {
		/* Null Data Packets contain no data, therefore no
		 * mcs encoding. The STF/LTF are usually BPSK, therefore
		 * the NDP mcs rate can always be considered as 0.
		 */
		hdr_rx_status->rate = 0;

		/**
		 * BSS COLOR is not present in NDP frames.
		 */
		hdr_rx_status->bss_color = 0;
	}

	if (!netif_running(morse_mon))
		return;

	/* There are specific radiotap fields we need
	 * to append to our skb depending on the packet type
	 */
	if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_NDP) {
		skb = skb_copy_expand(rx_skb,
				      sizeof(*hdr) +
				      sizeof(*psdu),
				      0,
				      GFP_KERNEL);
		if (skb == NULL)
			return;
		psdu = (struct zero_length_psdu *)skb_push(skb, sizeof(*psdu));

		/* Set bits for 0 length PSDU radiotap field*/
		psdu->psdu_type =
			IEEE80211_RADIOTAP_HALOW_FLAGS_S1G_NDP_CMAC;

		ndp_sub_type =
			MORSE_RX_STATUS_FLAGS_NDP_TYPE_GET(
				(hdr_rx_status->flags));
		psdu->ndp_type = (ndp_sub_type == IEEE80211_NDP_FTYPE_PREQ) ?
			IEEE80211_RADIOTAP_HALOW_FLAGS_S1G_NDP_MANAGEMENT :
			IEEE80211_RADIOTAP_HALOW_FLAGS_S1G_NDP_CONTROL;

		if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_NDP_2MHZ) {
			psdu->ndp[0] &= IEEE80211_RADIOTAP_HALOW_MASK_NDP_2MHZ;
			psdu->ndp[0] |=
				IEEE80211_RADIOTAP_HALOW_MASK_NDP_BW_2MHZ;
		} else {
			psdu->ndp[0] &= IEEE80211_RADIOTAP_HALOW_MASK_NDP_1MHZ;
		}
	} else {
		int len = sizeof(*hdr) +
			  sizeof(*ampdu_hdr) +
			  sizeof(*s1g_info_hdr) +
			  sizeof(*align_padding);
		skb = skb_copy_expand(rx_skb, len, 0, GFP_KERNEL);
		if (skb == NULL)
			return;

		s1g_info_hdr = (struct radiotap_s1g_tlv *)skb_push(skb,
						 sizeof(*s1g_info_hdr));

		if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_AMPDU) {
			ampdu_hdr = (struct ampdu_header *)skb_push(skb,
						      sizeof(*ampdu_hdr));
		}

		/* Add padding to keep the radiotap alignment.
		 * This is required for most packets, except ndps.
		 */
		align_padding = (struct padding *)skb_push(skb,
					 sizeof(*align_padding));
	}

	bw = int_bw_to_radiotap_bw_enum(hdr_rx_status->bw_mhz);
	if (bw == DOT11_RT_S1G_BW_INVALID) {
		morse_err(mors, "Packet with invalid BW '%d' received\n",
			hdr_rx_status->bw_mhz);
	}

	hdr = (struct morse_radiotap_hdr *) skb_push(skb, sizeof(*hdr));
	/* No flags set for now */
	hdr->rt_flags = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->rt_tsft = cpu_to_le64(hdr_rx_status->rx_timestamp_us);
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_present = cpu_to_le32(
		BIT(IEEE80211_RADIOTAP_FLAGS) |
		BIT(IEEE80211_RADIOTAP_CHANNEL) |
		BIT(IEEE80211_RADIOTAP_TSFT) |
		BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL));

	/* Size and flag rtp data conditionally */
	if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_NDP) {
		hdr->hdr.it_len += cpu_to_le16(sizeof(*psdu));
		hdr->hdr.it_present |= cpu_to_le32(
			BIT(IEEE80211_RADIOTAP_ZERO_LEN_PSDU));
		hdr->rt_rate_or_zl_psdu = RT_ZERO_LEN_PSDU_DATA;
	} else {
		hdr->hdr.it_present |= cpu_to_le32(
			BIT(IEEE80211_RADIOTAP_RATE) |
			BIT(IEEE80211_RADIOTAP_HALOW_TLV));

		/* Set MSB of rate so it is interpreted as an MCS index */
		hdr->rt_rate_or_zl_psdu = BIT(7) | hdr_rx_status->rate;

		hdr->hdr.it_len += cpu_to_le16(sizeof(*s1g_info_hdr) +
					       sizeof(*align_padding));

		if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_FCS_INCLUDED)
			hdr->rt_flags |= IEEE80211_RADIOTAP_F_FCS;

		/* Populate the s1g info rdtp header*/
		/* TODO Parse in RX status info e.g. MCS type */
		s1g_info_hdr->type = cpu_to_le16(DOT11_RT_TLV_S1G_TYPE);
		s1g_info_hdr->length = cpu_to_le16(DOT11_RT_TLV_S1G_LENGTH);

		s1g_info_hdr->known = cpu_to_le16(
			DOT11_RT_S1G_KNOWN_PPDU_FMT |
			DOT11_RT_S1G_KNOWN_GI |
			DOT11_RT_S1G_KNOWN_BW |
			DOT11_RT_S1G_KNOWN_MCS |
			DOT11_RT_S1G_KNOWN_RES_IND |
			DOT11_RT_S1G_KNOWN_COLOR |
			DOT11_RT_S1G_KNOWN_UPL_IND);

		s1g_info_hdr->data1 = cpu_to_le16(
			DOT11_RT_S1G_DAT1_PPDU_FMT_SET(
				MORSE_RX_STATUS_FLAGS_PPDU_FMT_GET(
							hdr_rx_status->flags)) |
			DOT11_RT_S1G_DAT1_GI_SET(
				MORSE_RX_STATUS_FLAGS_SGI_GET(hdr_rx_status->flags)) |
			DOT11_RT_S1G_DAT1_BW_SET(bw) |
			DOT11_RT_S1G_DAT1_MCS_SET(hdr_rx_status->rate) |
			DOT11_RT_S1G_DAT1_RES_IND_SET(
				MORSE_RX_STATUS_FLAGS_RI_GET(
					hdr_rx_status->flags)));

		s1g_info_hdr->data2 = cpu_to_le16(
			DOT11_RT_S1G_DAT2_RSSI_SET((s8) hdr_rx_status->rssi) |
			DOT11_RT_S1G_DAT2_COLOR_SET(hdr_rx_status->bss_color) |
			DOT11_RT_S1G_DAT2_UPL_IND_SET(
				MORSE_RX_STATUS_FLAGS_UPL_IND_GET(
							hdr_rx_status->flags)));
	}

	if (hdr_rx_status->flags & MORSE_RX_STATUS_FLAGS_AMPDU) {
		hdr->hdr.it_present |= cpu_to_le32(
			BIT(IEEE80211_RADIOTAP_AMPDU_STATUS));
		ampdu_hdr->flags = cpu_to_le16(IEEE80211_RADIOTAP_AMPDU_EOF |
				   IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN |
				   IEEE80211_RADIOTAP_AMPDU_IS_LAST);
		ampdu_hdr->ref_num = 1;
		hdr->hdr.it_len += cpu_to_le16(sizeof(*ampdu_hdr));
	}

	hdr->rt_dbm_antsignal = (s8) hdr_rx_status->rssi;

	/* Convert freq in Hz to MHz, half spaced channels are rounded down */
	hdr->rt_channel = cpu_to_le16(HZ_TO_MHZ(hdr_rx_status->freq_hz));
	if (hdr->rt_channel <= 700)
		flags = IEEE80211_CHAN_700MHZ;
	else if (hdr->rt_channel <= 800)
		flags = IEEE80211_CHAN_800MHZ;
	else
		flags = IEEE80211_CHAN_900MHZ;
	hdr->rt_chbitmask = cpu_to_le16(flags);

	/* Populate skb headers */
	skb->dev = morse_mon;
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	/* Push to network interface */
	netif_rx(skb);
}

void morse_mon_sig_field_error(const struct morse_evt_sig_field_error_evt *sig_field_error_evt)
{
	struct sk_buff *skb;
	struct morse_collision_radiotap_hdr *hdr;

	if (!netif_running(morse_mon))
		return;

	skb = dev_alloc_skb(sizeof(*hdr));
	if (skb == NULL)
		return;

	hdr = (struct morse_collision_radiotap_hdr *) skb_put(skb, sizeof(*hdr));
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->hdr.it_present =
		cpu_to_le32(BIT(IEEE80211_RADIOTAP_TSFT) | BIT(IEEE80211_RADIOTAP_TIMESTAMP));

	hdr->rt_tsft = cpu_to_le64(sig_field_error_evt->start_timestamp);
	hdr->timestamp.timestamp = cpu_to_le64(sig_field_error_evt->end_timestamp);
	hdr->timestamp.accuracy = 0;
	hdr->timestamp.unit_position =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US | IEEE80211_RADIOTAP_TIMESTAMP_SPOS_EO_PPDU;
	hdr->timestamp.flags = IEEE80211_RADIOTAP_TIMESTAMP_FLAG_64BIT;

	/* Populate skb headers */
	skb->dev = morse_mon;
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	/* Push to network interface */
	netif_rx(skb);
}

int morse_mon_init(struct morse *mors)
{
	int err = 0;

	morse_mon = alloc_netdev(0, "morse%d", NET_NAME_UNKNOWN,
				 morse_mon_setup);
	if (morse_mon == NULL) {
		err = -ENOMEM;
		goto alloc_netdev_err;
	}

	rtnl_lock();
	err = dev_alloc_name(morse_mon, morse_mon->name);
	if (err < 0) {
		rtnl_unlock();
		goto alloc_name_err;
	}

	err = register_netdevice(morse_mon);
	if (err < 0) {
		rtnl_unlock();
		goto alloc_name_err;
	}
	rtnl_unlock();

	return 0;

alloc_name_err:
	free_netdev(morse_mon);
alloc_netdev_err:
	return err;
}

void morse_mon_free(struct morse *mors)
{
	unregister_netdev(morse_mon);
}
