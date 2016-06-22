/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>

struct edma_adapter;
struct edma_hw;

/* register definition */
#define REG_EDMA_MAS_CTRL 0x0
#define REG_TIMEOUT_CTRL 0x004
#define REG_EDMA_DBG0 0x008
#define REG_EDMA_DBG1 0x00C
#define REG_SW_CTRL0 0x100
#define REG_SW_CTRL1 0x104

/* Interrupt Status Register */
#define REG_RX_ISR 0x200
#define REG_TX_ISR 0x208
#define REG_MISC_ISR 0x210
#define REG_WOL_ISR 0x218

#define MISC_ISR_RX_URG_Q(x) (1 << x)

#define MISC_ISR_AXIR_TIMEOUT 0x00000100
#define MISC_ISR_AXIR_ERR 0x00000200
#define MISC_ISR_TXF_DEAD 0x00000400
#define MISC_ISR_AXIW_ERR 0x00000800
#define MISC_ISR_AXIW_TIMEOUT 0x00001000

#define WOL_ISR_WOL 0x00000001

#define ISR_RX_PKT(x) (1 << x)
#define ISR_TX_PKT(x) (1 << x)

/* Interrupt Mask Register */
#define REG_MISC_IMR 0x214
#define REG_WOL_IMR 0x218

#define EDMA_RX_IMR_NORMAL_MASK 0x1
#define EDMA_TX_IMR_NORMAL_MASK 0x1
#define MISC_IMR_NORMAL_MASK 0x80001FFF
#define WOL_IMR_NORMAL_MASK 0x1

/* Edma receive consumer index */
#define REG_RX_SW_CONS_IDX_Q(x) (0x220 + ((x) << 2)) /* x is the queue id */
/* Edma transmit consumer index */
#define REG_TX_SW_CONS_IDX_Q(x) (0x240 + ((x) << 2)) /* x is the queue id */

/* IRQ Moderator Initial Timer Register */
#define REG_IRQ_MODRT_TIMER_INIT 0x280
#define IRQ_MODRT_TIMER_MASK 0xFFFF
#define IRQ_MODRT_RX_TIMER_SHIFT 0
#define IRQ_MODRT_TX_TIMER_SHIFT 16

/* Interrupt Control Register */
#define REG_INTR_CTRL 0x284
#define INTR_CLR_TYP_SHIFT 0
#define INTR_SW_IDX_W_TYP_SHIFT 1
#define INTR_CLEAR_TYPE_W1 0
#define INTR_CLEAR_TYPE_R 1

/* RX Interrupt Mask Register */
#define REG_RX_INT_MASK_Q(x) (0x300 + ((x) << 2)) /* x = queue id */

/* TX Interrupt mask register */
#define REG_TX_INT_MASK_Q(x) (0x340 + ((x) << 2)) /* x = queue id */

/* Load Ptr Register
 * Software sets this bit after the initialization of the head and tail
 */
#define REG_TX_SRAM_PART 0x400
#define LOAD_PTR_SHIFT 16

/* TXQ Control Register */
#define REG_TXQ_CTRL 0x404
#define TXQ_CTRL_IP_OPTION_EN 0x10
#define TXQ_CTRL_TXQ_EN 0x20
#define TXQ_CTRL_ENH_MODE 0x40
#define TXQ_CTRL_LS_8023_EN 0x80
#define EDMA_TXQ_CTRL_TPD_BURST_EN 0x100
#define TXQ_CTRL_LSO_BREAK_EN 0x200
#define EDMA_TXQ_NUM_TPD_BURST_MASK 0xF
#define EDMA_TXQ_TXF_BURST_NUM_MASK 0xFFFF
#define EDMA_TXQ_NUM_TPD_BURST_SHIFT 0
#define EDMA_TXQ_TXF_BURST_NUM_SHIFT 16

#define	REG_TXF_WATER_MARK 0x408 /* In 8-bytes */
#define TXF_WATER_MARK_MASK 0x0FFF
#define TXF_LOW_WATER_MARK_SHIFT 0
#define TXF_HIGH_WATER_MARK_SHIFT 16
#define TXQ_CTRL_BURST_MODE_EN 0x80000000

/* WRR Control Register */
#define REG_WRR_CTRL(x) (0x40c + ((x) << 2)) /* x is the queue id */

#define WRR_WEIGHT_Q_SHIFT(x) (((x) * 5) % 20)

/* Tx Descriptor Control Register */
#define REG_TPD_RING_SIZE 0x41C
#define TPD_RING_SIZE_SHIFT 0
#define TPD_RING_SIZE_MASK 0xFFFF

/* Transmit descriptor base address */
#define REG_TPD_BASE_ADDR_Q(x) (0x420 + ((x) << 2)) /* x = queue id */

/* TPD Index Register */
#define REG_TPD_IDX_Q(x) (0x460 + ((x) << 2)) /* x = queue id */

#define TPD_PROD_IDX_BITS 0x0000FFFF
#define TPD_CONS_IDX_BITS 0xFFFF0000
#define TPD_PROD_IDX_MASK 0xFFFF
#define TPD_CONS_IDX_MASK 0xFFFF
#define TPD_PROD_IDX_SHIFT 0
#define TPD_CONS_IDX_SHIFT 16

/* TX Virtual Queue Mapping Control Register */
#define REG_VQ_CTRL0 0x4A0
#define REG_VQ_CTRL1 0x4A4
#define VQ_ID_MASK 0x7
#define VQ0_ID_SHIFT 0
#define VQ1_ID_SHIFT 3
#define VQ2_ID_SHIFT 6
#define VQ3_ID_SHIFT 9
#define VQ4_ID_SHIFT 12
#define VQ5_ID_SHIFT 15
#define VQ6_ID_SHIFT 18
#define VQ7_ID_SHIFT 21
#define VQ8_ID_SHIFT 0
#define VQ9_ID_SHIFT 3
#define VQ10_ID_SHIFT 6
#define VQ11_ID_SHIFT 9
#define VQ12_ID_SHIFT 12
#define VQ13_ID_SHIFT 15
#define VQ14_ID_SHIFT 18
#define VQ15_ID_SHIFT 21

/* Tx side Port Interface Control Register */
#define REG_PORT_CTRL 0x4A8
#define PAD_EN_SHIFT 15

/* Tx side VLAN Configuration Register */
#define REG_VLAN_CFG 0x4AC
#define VLAN_TPID_MASK 0xffff
#define SVLAN_TPID_SHIFT 0
#define CVLAN_TPID_SHIFT 16

/* Tx Queue Packet Statistic Register */
#define REG_TX_STAT_PKT_Q(x) (0x700 + ((x) << 2)) /* x = queue id */

#define TX_STAT_PKT_MASK 0xFFFFFF

/* Tx Queue Byte Statistic Register */
#define REG_TX_STAT_BYTE_Q(x) (0x704 + ((x) << 2)) /* x = queue id */

/* Load Balance Based Ring Offset Register */
#define REG_LB_RING 0x800
#define LB_RING_ENTRY_MASK 0xff
#define LB_RING_ID_MASK 0x7
#define LB_RING_PROFILE_ID_MASK 0x3
#define LB_RING_ENTRY_BIT_OFFSET 8
#define LB_RING_ID_OFFSET 0
#define LB_RING_PROFILE_ID_OFFSET 3

/* Load Balance Priority Mapping Register */
#define REG_LB_PRI_START 0x804
#define REG_LB_PRI_END 0x810
#define LB_PRI_REG_INC 4
#define LB_PRI_ENTRY_BIT_OFFSET 4
#define LB_PRI_ENTRY_MASK 0xf

/* RSS Priority Mapping Register */
#define REG_RSS_PRI 0x820
#define RSS_PRI_ENTRY_MASK 0xf
#define RSS_RING_ID_MASK 0x7
#define RSS_PRI_ENTRY_BIT_OFFSET 4

/* RSS Indirection Register */
#define REG_RSS_IDT_START 0x840
#define REG_RSS_IDT_END 0x87C
#define RSS_IDT_REG_INC 4
#define RSS_IDT_ENTRY_BIT_OFFSET 4
#define RSS_IDT_ENTRY_MASK 0xf


/* Default RSS Ring Register */
#define REG_DEF_RSS 0x890
#define DEF_RSS_MASK 0x7

/* RSS Hash Function Type Register */
#define REG_RSS_TYPE 0x894
#define RSS_NONE 0x00000001
#define RSS_IPV4_TCP_EN 0x00000002
#define RSS_IPV6_TCP_EN 0x00000004
#define RSS_IPV4_UDP_EN 0x00000008
#define RSS_IPV6_UDP_EN 0x00000010
#define RSS_IPV4_EN 0x00000020
#define RSS_IPV6_EN 0x00000040
#define RSS_HASH_MODE_MASK 0x7f

#define REG_RSS_HASH_VALUE 0x8C0

#define REG_RSS_TYPE_RESULT 0x8C4

/* RFD Base Address Register */
#define REG_RFD_BASE_ADDR_Q(x) (0x950 + ((x) << 2)) /* x = queue id */

/* RFD Index Register */
#define REG_RFD_IDX_Q(x) (0x9B0 + ((x) << 2))

#define RFD_PROD_IDX_BITS 0x00000FFF
#define RFD_CONS_IDX_BITS 0x0FFF0000
#define RFD_PROD_IDX_MASK 0xFFF
#define RFD_CONS_IDX_MASK 0xFFF
#define RFD_PROD_IDX_SHIFT 0
#define RFD_CONS_IDX_SHIFT 16

/* Rx Descriptor Control Register */
#define REG_RX_DESC0 0xA10
#define RFD_RING_SIZE_MASK 0xFFF
#define RX_BUF_SIZE_MASK 0xFFFF
#define RFD_RING_SIZE_SHIFT 0
#define RX_BUF_SIZE_SHIFT 16

#define REG_RX_DESC1 0xA14
#define RXQ_RFD_BURST_NUM_MASK 0x3F
#define RXQ_RFD_PF_THRESH_MASK 0x1F
#define RXQ_RFD_LOW_THRESH_MASK 0xFFF
#define RXQ_RFD_BURST_NUM_SHIFT 0
#define RXQ_RFD_PF_THRESH_SHIFT 8
#define RXQ_RFD_LOW_THRESH_SHIFT 16

/* RXQ Control Register */
#define REG_RXQ_CTRL 0xA18
#define FIFO_THRESH_TYPE_SHIF 0
#define FIFO_THRESH_128_BYTE 0x0
#define FIFO_THRESH_64_BYTE 0x1
#define RXQ_CTRL_RMV_VLAN 0x00000002
#define RXQ_CTRL_EN 0x0000FF00

/* Rx Statistics Register */
#define REG_RX_STAT_BYTE_Q(x) (0xA30 + ((x) << 2)) /* x = queue id */
#define REG_RX_STAT_PKT_Q(x) (0xA50 + ((x) << 2)) /* x = queue id */

/* WoL Pattern Length Register */
#define REG_WOL_PATTERN_LEN0 0xC00
#define WOL_PT_LEN_MASK 0xFF
#define WOL_PT0_LEN_SHIFT 0
#define WOL_PT1_LEN_SHIFT 8
#define WOL_PT2_LEN_SHIFT 16
#define WOL_PT3_LEN_SHIFT 24

#define REG_WOL_PATTERN_LEN1 0xC04
#define WOL_PT4_LEN_SHIFT 0
#define WOL_PT5_LEN_SHIFT 8
#define WOL_PT6_LEN_SHIFT 16

/* WoL Control Register */
#define REG_WOL_CTRL 0xC08
#define WOL_WK_EN 0x00000001
#define WOL_MG_EN 0x00000002
#define WOL_PT0_EN 0x00000004
#define WOL_PT1_EN 0x00000008
#define WOL_PT2_EN 0x00000010
#define WOL_PT3_EN 0x00000020
#define WOL_PT4_EN 0x00000040
#define WOL_PT5_EN 0x00000080
#define WOL_PT6_EN 0x00000100

/* MAC Control Register */
#define REG_MAC_CTRL0 0xC20
#define REG_MAC_CTRL1 0xC24

/* WoL Pattern Register */
#define REG_WOL_PATTERN_START 0x5000
#define PATTERN_PART_REG_OFFSET 0x40


/* TX descriptor checksum offload */
#define IP_CSUM_EN 0x1
#define IP_CSUM_SHIFT 9
#define TCP_CSUM_EN 0x1
#define TCP_CSUM_SHIFT 10
#define HDR_OFFSET 0
