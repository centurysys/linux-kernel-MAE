/*
 * Copyright 2022 Morse Micro
 *
 */

#include "linux/crc7.h"

#include "yaps-hw.h"
#include "bus.h"
#include "debug.h"
#include "chip_if.h"
#include "utils.h"
#include "yaps.h"

#define YAPS_HW_WINDOW_SIZE_BYTES	32768
#define YAPS_MAX_PKT_SIZE_BYTES		16384
#define YAPS_DEFAULT_READ_SIZE_BYTES	512

#define YAPS_PAGE_SIZE  256
#define SDIO_BLOCKSIZE	512

/* Calculate padding required for yaps transaction */
#define YAPS_CALC_PADDING(_bytes) ((_bytes) & 0x3 ? (4 - ((_bytes) & 0x3)) : 0)

/*
 * Yaps data stream delimiter is a 32 bit word with the following fields:
 *
 * pkt_size (14 bits) - Packet size not including delimiter or padding
 * pool_id  (3  bits) - Pool that pages should be allocated from.
 *                      Pool IDs defined in enum yaps_alloc_pool
 * padding  (2  bits) - Padding required to bring packet to word (4 byte) boundary
 * irq      (1  bit ) - Raise a PKT_IRQ on the YDS this is sent to
 * reserved (5  bits) - Reserved, must write as 0
 * crc      (7  bits) - YAPS CRC
 */

/* Packet size not including delimiter or padding */
#define YAPS_DELIM_GET_PKT_SIZE(_delim)		(_delim & 0x3FFF)
#define YAPS_DELIM_SET_PKT_SIZE(_pkt_size)	(_pkt_size & 0x3FFF)
/* Pool that pages should be allocated from. Pool IDs defined in enum yaps_alloc_pool */
#define YAPS_DELIM_GET_POOL_ID(_delim)		((_delim >> 14) & 0x7)
#define YAPS_DELIM_SET_POOL_ID(_pool_id)	((_pool_id & 0x7) << 14)
/* Padding required to bring packet to word (4 byte) boundary */
#define YAPS_DELIM_GET_PADDING(_delim)		((_delim >> 17) & 0x3)
#define YAPS_DELIM_SET_PADDING(_padding)	((_padding & 0x3) << 17)
/* Raise a PKT_IRQ on the YDS this is sent to */
#define YAPS_DELIM_GET_IRQ(_delim)		((_delim >> 19) & 0x1)
#define YAPS_DELIM_SET_IRQ(_irq)		((_irq & 0x1) << 19)
/* Reserved, must write as 0 */
#define YAPS_DELIM_GET_RESERVED(_delim)		((_delim >> 20) & 0x1F)
#define YAPS_DELIM_SET_RESERVED(_reserved)	((_reserved & 0x1F) << 20)
/* YAPS CRC */
#define YAPS_DELIM_GET_CRC(_delim)		((_delim >> 25) & 0x7F)
#define YAPS_DELIM_SET_CRC(_crc)		((_crc & 0x7F) << 25)

/* This maps directly to the status window block in chip memory */
struct morse_yaps_status_registers {
	/* Allocation pools */
	u32 tc_tx_pool_num_pages;
	u32 tc_cmd_pool_num_pages;
	u32 tc_beacon_pool_num_pages;
	u32 tc_mgmt_pool_num_pages;
	u32 fc_rx_pool_num_pages;
	u32 fc_resp_pool_num_pages;
	u32 fc_tx_sts_pool_num_pages;
	u32 fc_aux_pool_num_pages;

	/* To chip/From chip queues for YDS/YSL */
	u32 tc_tx_num_pkts;
	u32 tc_cmd_num_pkts;
	u32 tc_beacon_num_pkts;
	u32 tc_mgmt_num_pkts;
	u32 fc_num_pkts;
	u32 fc_done_num_pkts;
	u32 fc_rx_bytes_in_queue;
	u32 tc_delim_crc_fail_detected;

	/* Scratch registers */
	union {
		u32 scratch_0;
		u32 metadata_count;
	};

	u32 scratch_1;
	u32 scratch_2;
	u32 scratch_3;
} __packed;

struct morse_yaps_hw_aux_data {
	unsigned long access_lock;

	u32 yds_addr;
	u32 ysl_addr;
	u32 status_regs_addr;

	/* Alloc pool sizes */
	int tc_tx_pool_size;
	int tc_cmd_pool_size;
	int tc_beacon_pool_size;
	int tc_mgmt_pool_size;
	int fc_rx_pool_size;
	int fc_resp_pool_size;
	int fc_tx_sts_pool_size;
	int fc_aux_pool_size;

	/* To chip/from chip queue sizes */
	int tc_tx_q_size;
	int tc_cmd_q_size;
	int tc_beacon_q_size;
	int tc_mgmt_q_size;
	int fc_q_size;
	int fc_done_q_size;

	/* Buffers to/from chip to support large contiguous reads/writes */
	char *to_chip_buffer;
	char *from_chip_buffer;

	/* Status registers for queues and aloc pools on chip */
	struct morse_yaps_status_registers status_regs;
};

static int yaps_hw_lock(struct morse_yaps *yaps)
{
	if (test_and_set_bit_lock(0, &yaps->aux_data->access_lock))
		return -1;
	return 0;
}

void yaps_hw_unlock(struct morse_yaps *yaps)
{
	clear_bit_unlock(0, &yaps->aux_data->access_lock);
}

static void morse_yaps_fill_aux_data_from_hw_tbl(
					struct morse_yaps_hw_aux_data *aux_data,
					struct morse_yaps_hw_table *tbl_ptr)
{
	aux_data->ysl_addr = __le32_to_cpu(tbl_ptr->ysl_addr);
	aux_data->yds_addr = __le32_to_cpu(tbl_ptr->yds_addr);
	aux_data->status_regs_addr = __le32_to_cpu(tbl_ptr->status_regs_addr);

	aux_data->tc_tx_pool_size = __le16_to_cpu(tbl_ptr->tc_tx_pool_size);
	aux_data->fc_rx_pool_size = __le16_to_cpu(tbl_ptr->fc_rx_pool_size);
	aux_data->tc_cmd_pool_size = tbl_ptr->tc_cmd_pool_size;
	aux_data->tc_beacon_pool_size = tbl_ptr->tc_beacon_pool_size;
	aux_data->tc_mgmt_pool_size = tbl_ptr->tc_mgmt_pool_size;
	aux_data->fc_resp_pool_size = tbl_ptr->fc_resp_pool_size;
	aux_data->fc_tx_sts_pool_size = tbl_ptr->fc_tx_sts_pool_size;
	aux_data->fc_aux_pool_size = tbl_ptr->fc_aux_pool_size;
	aux_data->tc_tx_q_size = tbl_ptr->tc_tx_q_size;
	aux_data->tc_cmd_q_size = tbl_ptr->tc_cmd_q_size;
	aux_data->tc_beacon_q_size = tbl_ptr->tc_beacon_q_size;
	aux_data->tc_mgmt_q_size = tbl_ptr->tc_mgmt_q_size;
	aux_data->fc_q_size = tbl_ptr->fc_q_size;
	aux_data->fc_done_q_size = tbl_ptr->fc_done_q_size;
}

static inline u8 morse_yaps_crc(u32 word)
{
	u8 crc = 0;
	int len = sizeof(word);

	/* Mask to look at only non-crc bits in both metadata word and delimiters */
	word = cpu_to_be32(word & 0x1ffffff);
	while (len--) {
		crc = crc7_be_byte(crc, word & 0xff);
		word >>= 8;
	}
	return crc >> 1;
}

static inline u32 morse_yaps_delimiter(unsigned int size, u8 pool_id, bool irq)
{
	u32 delim = 0;

	delim |= YAPS_DELIM_SET_PKT_SIZE(size);
	delim |= YAPS_DELIM_SET_PADDING(YAPS_CALC_PADDING(size));
	delim |= YAPS_DELIM_SET_POOL_ID(pool_id);
	delim |= YAPS_DELIM_SET_IRQ(irq);
	delim |= YAPS_DELIM_SET_CRC(morse_yaps_crc(delim));
	return delim;
}

int morse_yaps_hw_read_table(struct morse *mors,
				  struct morse_yaps_hw_table *tbl_ptr)
{
	int ret;

	const u32 yaps_addr = mors->cfg->host_table_ptr +
		offsetof(struct host_table, chip_if) +
		offsetof(struct morse_chip_if_host_table, yaps_info);

	ret = morse_dm_read(mors, yaps_addr, (char *) tbl_ptr, sizeof(*tbl_ptr));

	if (ret)
		goto exit;

	morse_yaps_fill_aux_data_from_hw_tbl(mors->chip_if->yaps->aux_data, tbl_ptr);

exit:
	return ret;
}

static unsigned int morse_yaps_pages_required(unsigned int size_bytes)
{
	/* TODO remove +1 to improve performance when yaps hw off-by-1 fix goes in,
	 * see MM-5969
	 */
	return MORSE_INT_CEIL(size_bytes, YAPS_PAGE_SIZE) + 2;
}

/* Checks if a single pkt will fit in the chip using the pool/alloc holding
 * information from the last status register read.
 */
static bool morse_yaps_will_fit(struct morse_yaps *yaps, struct morse_yaps_pkt *pkt, bool update)
{
	bool will_fit = true;
	const int pages_required = morse_yaps_pages_required(pkt->skb->len);
	int *pool_pages_avail = NULL;
	int *pkts_in_queue = NULL;
	int queue_pkts_avail = 0;


	if (yaps->aux_data->status_regs.metadata_count == 0) {
		morse_warn(yaps->mors, "No available metadata\n");
		return false;
	}


	switch (pkt->tc_queue) {
	case MORSE_YAPS_TX_Q:
		pool_pages_avail = &yaps->aux_data->status_regs.tc_tx_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_tx_num_pkts;
		queue_pkts_avail = yaps->aux_data->tc_tx_q_size - *pkts_in_queue;
		break;
	case MORSE_YAPS_CMD_Q:
		pool_pages_avail = &yaps->aux_data->status_regs.tc_cmd_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_cmd_num_pkts;
		queue_pkts_avail = yaps->aux_data->tc_cmd_q_size - *pkts_in_queue;
		break;
	case MORSE_YAPS_BEACON_Q:
		pool_pages_avail = &yaps->aux_data->status_regs.tc_beacon_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_beacon_num_pkts;
		queue_pkts_avail = yaps->aux_data->tc_beacon_q_size - *pkts_in_queue;
		break;
	case MORSE_YAPS_MGMT_Q:
		pool_pages_avail = &yaps->aux_data->status_regs.tc_mgmt_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_mgmt_num_pkts;
		queue_pkts_avail = yaps->aux_data->tc_mgmt_q_size - *pkts_in_queue;
		break;
	default:
		morse_err(yaps->mors, "yaps invalid tc queue\n");
	}

	BUG_ON(queue_pkts_avail < 0);

	if (pages_required > *pool_pages_avail)
		will_fit = false;

	if (queue_pkts_avail == 0)
		will_fit = false;

	if (will_fit && update) {
		*pool_pages_avail -= pages_required;
		*pkts_in_queue += 1;
		yaps->aux_data->status_regs.metadata_count -= 1;
	}

	return will_fit;
}

/* SW-7590:
 * This is a workaround for an SDIO interrupt lock up issue.
 * Once fixed on the silicon, this should only be called for
 * the revisions of the chip with the problem.
 */
static void morse_yaps_hw_modify_status_pend_flag(struct morse *mors, u32 length)
{
	if ((length > SDIO_BLOCKSIZE) && ((length % SDIO_BLOCKSIZE) == 0))
		set_bit(MORSE_YAPS_STATUS_REG_READ_PEND, &mors->chip_if->event_flags);
	else
		clear_bit(MORSE_YAPS_STATUS_REG_READ_PEND, &mors->chip_if->event_flags);
}

static int morse_yaps_hw_write_pkts(struct morse_yaps *yaps,
				    struct morse_yaps_pkt pkts[], int num_pkts,
				    int *num_pkts_sent)
{
	int ret = 0;
	int i;
	u32 delim = 0;
	char *write_buf = yaps->aux_data->to_chip_buffer;
	int tx_len;
	int batch_txn_len = 0;
	int pkts_pending = 0;

	ret = yaps_hw_lock(yaps);
	if (ret) {
		morse_dbg(yaps->mors, "%s yaps lock failed %d\n", __func__, ret);
		return ret;
	}

	*num_pkts_sent = 0;

	/* Batch packets into larger transactions. Send as many as we have space for. */
	for (i = 0; i < num_pkts; ++i) {
		if (pkts[i].skb->len > YAPS_MAX_PKT_SIZE_BYTES) {
			ret = -EMSGSIZE;
			goto exit;
		}
		if (pkts[i].tc_queue > MORSE_YAPS_NUM_TC_Q) {
			ret = -EINVAL;
			goto exit;
		}
		if (!morse_yaps_will_fit(yaps, &pkts[i], true)) {
			ret = -EAGAIN;
			goto exit;
		}

		tx_len = pkts[i].skb->len + YAPS_CALC_PADDING(pkts[i].skb->len) + sizeof(delim);

		/* Send when we have reached window size, don't split pkt over boundary */
		if ((batch_txn_len + tx_len) > YAPS_HW_WINDOW_SIZE_BYTES) {
			ret = morse_dm_write(yaps->mors, yaps->aux_data->yds_addr,
					     yaps->aux_data->to_chip_buffer,
					     batch_txn_len);

			/*
			 * No need to check for SDIO interrupt lock up here.
			 * There is definitely more data to be sent
			 */

			batch_txn_len = 0;
			if (ret)
				goto exit;
			write_buf = yaps->aux_data->to_chip_buffer;
			*num_pkts_sent += pkts_pending;
			pkts_pending = 0;
		}

		/* Build stream header */
		/* Last data packet always set IRQ so the chip doesn't miss it */
		delim = morse_yaps_delimiter(pkts[i].skb->len,
			pkts[i].tc_queue,
			pkts[i].tc_queue == MORSE_YAPS_TX_Q && (i + 1) == num_pkts);
		*((u32 *) write_buf) = cpu_to_le32(delim);
		memcpy(write_buf + sizeof(delim), pkts[i].skb->data, pkts[i].skb->len);

		write_buf += tx_len;
		batch_txn_len += tx_len;
		pkts_pending++;
	}

exit:
	if (batch_txn_len > 0) {
		ret = morse_dm_write(yaps->mors, yaps->aux_data->yds_addr,
				     yaps->aux_data->to_chip_buffer,
				     batch_txn_len);
		*num_pkts_sent += pkts_pending;

		morse_yaps_hw_modify_status_pend_flag(yaps->mors, batch_txn_len);
	}

	yaps_hw_unlock(yaps);
	return ret;
}

static bool morse_yaps_is_valid_delimiter(u32 delim)
{
	u8 calc_crc = morse_yaps_crc(delim);
	int pkt_size = YAPS_DELIM_GET_PKT_SIZE(delim);
	int padding = YAPS_DELIM_GET_PADDING(delim);

	if (calc_crc != YAPS_DELIM_GET_CRC(delim))
		return false;

	if (pkt_size == 0)
		return false;

	if ((pkt_size + padding) > YAPS_MAX_PKT_SIZE_BYTES)
		return false;

	/* Pkt length + padding should not require more padding */
	if (YAPS_CALC_PADDING(pkt_size) != padding)
		return false;

	return true;
}

static int morse_calc_bytes_remaining(struct morse_yaps *yaps)
{
	int bytes_remaining = yaps->aux_data->status_regs.fc_rx_bytes_in_queue;

	if (bytes_remaining > 0) {
		bytes_remaining += yaps->aux_data->status_regs.fc_num_pkts * sizeof(u32);
		bytes_remaining = min_t(int, YAPS_HW_WINDOW_SIZE_BYTES, bytes_remaining);
	}
	return bytes_remaining;
}

static int morse_yaps_hw_read_pkts(struct morse_yaps *yaps,
				   struct morse_yaps_pkt pkts[],
				   int num_pkts_max,
				   int *num_pkts_received)
{
	int ret;
	int i = 0;
	char *read_ptr = yaps->aux_data->from_chip_buffer;
	int bytes_remaining = morse_calc_bytes_remaining(yaps);

	*num_pkts_received = 0;

	if (num_pkts_max == 0 || bytes_remaining == 0)
		return 0;

	/* This is more coarse-grained than it needs to be -
	 * once the data is read into a local buffer the lock can be released,
	 * however access to from_chip_buffer will need to be protected with
	 * its own lock
	 */
	ret = yaps_hw_lock(yaps);
	if (ret) {
		morse_dbg(yaps->mors, "%s yaps lock failed %d\n", __func__, ret);
		return ret;
	}

	/* Read all available packets to the buffer */
	ret = morse_dm_read(yaps->mors, yaps->aux_data->ysl_addr,
			    yaps->aux_data->from_chip_buffer, bytes_remaining);

	morse_yaps_hw_modify_status_pend_flag(yaps->mors, bytes_remaining);

	if (ret)
		goto exit;

	/* Split serialised packets from buffer */
	while (i < num_pkts_max && bytes_remaining > 0) {
		u32 delim;
		int total_len;
		int pkt_size;

		delim = le32_to_cpu(*((uint32_t *) read_ptr));
		read_ptr += sizeof(delim);
		bytes_remaining -= sizeof(delim);

		/* End of stream */
		if (delim == 0x0)
			break;

		if (!morse_yaps_is_valid_delimiter(delim)) {
			/* This will start a hunt for a valid delimiter. Given
			 * the CRC is only 7 bit its possible to find an invalid
			 * block with a valid delimiter, leading
			 * to desynchronisation.
			 */
			morse_warn(yaps->mors, "yaps invalid delim\n");
			break;
		}

		/* Total length in chip */
		pkt_size = YAPS_DELIM_GET_PKT_SIZE(delim);
		total_len = pkt_size + YAPS_DELIM_GET_PADDING(delim);

		if (pkts[i].skb != NULL)
			morse_err(yaps->mors, "yaps packet leak\n");

		/* SKB doesn't want padding */
		pkts[i].skb = dev_alloc_skb(pkt_size);
		if (!pkts[i].skb) {
			ret = -ENOMEM;
			morse_err(yaps->mors, "yaps no mem for skb\n");
			goto exit;
		}
		skb_put(pkts[i].skb, pkt_size);

		if (total_len <= bytes_remaining) {
			/* Case where entire packet fits in the remaining window */
			memcpy(pkts[i].skb->data, read_ptr, pkt_size);
			read_ptr += total_len;
			bytes_remaining -= total_len;
		} else {
			/* Case where packet runs off the end of the window */
			const int read_overhang_len = total_len - bytes_remaining;
			const int pkt_overhang_len = pkt_size - bytes_remaining;

			morse_warn(yaps->mors, "yaps split pkt\n");
			memcpy(pkts[i].skb->data, read_ptr, bytes_remaining);
			read_ptr = yaps->aux_data->from_chip_buffer;

			ret = morse_dm_read(yaps->mors,
					    /* Offset by 4 to avoid retry logic */
					    yaps->aux_data->ysl_addr + 4,
					    read_ptr, read_overhang_len);

			morse_yaps_hw_modify_status_pend_flag(yaps->mors, read_overhang_len);

			if (ret)
				goto exit;

			memcpy(pkts[i].skb->data + bytes_remaining, read_ptr,
			       pkt_overhang_len);
			read_ptr += read_overhang_len;
			bytes_remaining = 0;
		}
		pkts[i].fc_queue = YAPS_DELIM_GET_POOL_ID(delim);
		*num_pkts_received += 1;
		i++;
	}

	if (bytes_remaining && i == num_pkts_max)
		ret = -EAGAIN;

exit:
	yaps_hw_unlock(yaps);
	return ret;
}

static void morse_yaps_hw_update_status(struct morse_yaps *yaps)
{
	int ret;
	int tc_total_pkt_count;
	struct morse_yaps_status_registers *status_regs =
						&yaps->aux_data->status_regs;

	ret = yaps_hw_lock(yaps);
	if (ret) {
		morse_dbg(yaps->mors, "%s yaps lock failed %d\n", __func__, ret);
		return;
	}

	ret = morse_dm_read(yaps->mors, yaps->aux_data->status_regs_addr,
				(u8 *) status_regs, sizeof(*status_regs));

	/* We can't recover from this - all status info is messed up */
	BUG_ON(ret);

	status_regs->tc_tx_pool_num_pages = le32_to_cpu(status_regs->tc_tx_pool_num_pages);
	status_regs->tc_cmd_pool_num_pages = le32_to_cpu(status_regs->tc_cmd_pool_num_pages);
	status_regs->tc_beacon_pool_num_pages = le32_to_cpu(status_regs->tc_beacon_pool_num_pages);
	status_regs->tc_mgmt_pool_num_pages = le32_to_cpu(status_regs->tc_mgmt_pool_num_pages);
	status_regs->fc_rx_pool_num_pages = le32_to_cpu(status_regs->fc_rx_pool_num_pages);
	status_regs->fc_resp_pool_num_pages = le32_to_cpu(status_regs->fc_resp_pool_num_pages);
	status_regs->fc_tx_sts_pool_num_pages = le32_to_cpu(status_regs->fc_tx_sts_pool_num_pages);
	status_regs->fc_aux_pool_num_pages = le32_to_cpu(status_regs->fc_aux_pool_num_pages);
	status_regs->tc_tx_num_pkts = le32_to_cpu(status_regs->tc_tx_num_pkts);
	status_regs->tc_cmd_num_pkts = le32_to_cpu(status_regs->tc_cmd_num_pkts);
	status_regs->tc_beacon_num_pkts = le32_to_cpu(status_regs->tc_beacon_num_pkts);
	status_regs->tc_mgmt_num_pkts = le32_to_cpu(status_regs->tc_mgmt_num_pkts);
	status_regs->fc_num_pkts = le32_to_cpu(status_regs->fc_num_pkts);
	status_regs->fc_done_num_pkts = le32_to_cpu(status_regs->fc_done_num_pkts);
	status_regs->fc_rx_bytes_in_queue = le32_to_cpu(status_regs->fc_rx_bytes_in_queue);
	status_regs->tc_delim_crc_fail_detected = le32_to_cpu(status_regs->tc_delim_crc_fail_detected);
	status_regs->scratch_0 = le32_to_cpu(status_regs->scratch_0);
	status_regs->scratch_1 = le32_to_cpu(status_regs->scratch_1);
	status_regs->scratch_2 = le32_to_cpu(status_regs->scratch_2);
	status_regs->scratch_3 = le32_to_cpu(status_regs->scratch_3);

	/* SW-7464
	 * tc_total_pkt_count accounts for the packets that have been sent to the chip and
	 * haven’t been processed (or might have been processed but the status registers
	 * haven't been updated yet). These packets will get metadatas later, but that is
	 * not reflected in the current metadata count. So if we don’t consider them and push
	 * more packets, we will run out of metadatas.
	 */
	tc_total_pkt_count = status_regs->tc_tx_num_pkts +
						status_regs->tc_cmd_num_pkts +
						status_regs->tc_beacon_num_pkts +
						status_regs->tc_mgmt_num_pkts;

	/* Update the number of metadatas to the practical usable amount */
	status_regs->metadata_count = max((int)status_regs->metadata_count - tc_total_pkt_count, 0);

	/* Host and chip have become desynchronised somehow - this shouldn't happen */
	BUG_ON(status_regs->tc_delim_crc_fail_detected);

	yaps_hw_unlock(yaps);
}

static void morse_yaps_hw_show(struct morse_yaps *yaps, struct seq_file *file)
{
	struct morse_yaps_status_registers *status_regs =
					&yaps->aux_data->status_regs;

	seq_printf(file, "flags:0x%01x\n", yaps->flags);
	seq_printf(file, "YDS addr: %x\n", yaps->aux_data->yds_addr);
	seq_printf(file, "YSL addr: %x\n", yaps->aux_data->ysl_addr);
	seq_printf(file, "Status addr: %x\n", yaps->aux_data->status_regs_addr);

	seq_puts(file, "YAPS status registers\n");
	seq_printf(file, "\tp_tx %d\n", status_regs->tc_tx_pool_num_pages);
	seq_printf(file, "\tp_cmd %d\n", status_regs->tc_cmd_pool_num_pages);
	seq_printf(file, "\tp_bcn %d\n", status_regs->tc_beacon_pool_num_pages);
	seq_printf(file, "\tp_mgmt %d\n", status_regs->tc_mgmt_pool_num_pages);
	seq_printf(file, "\tp_rx %d\n", status_regs->fc_rx_pool_num_pages);
	seq_printf(file, "\tp_resp %d\n", status_regs->fc_resp_pool_num_pages);
	seq_printf(file, "\tp_sts %d\n", status_regs->fc_tx_sts_pool_num_pages);
	seq_printf(file, "\tp_aux %d\n", status_regs->fc_aux_pool_num_pages);
	seq_printf(file, "\tq_tx_n %d\n", status_regs->tc_tx_num_pkts);
	seq_printf(file, "\tq_cmd_n %d\n", status_regs->tc_cmd_num_pkts);
	seq_printf(file, "\tq_bcn_n %d\n", status_regs->tc_beacon_num_pkts);
	seq_printf(file, "\tq_mgmt_n %d\n", status_regs->tc_mgmt_num_pkts);
	seq_printf(file, "\tq_fc_n %d\n", status_regs->fc_num_pkts);
	seq_printf(file, "\tq_fc_bytes %d\n", status_regs->fc_rx_bytes_in_queue);
	seq_printf(file, "\tq_fc_done_n %d\n", status_regs->fc_done_num_pkts);
	seq_printf(file, "\tdelim_crc_fail %d\n", status_regs->tc_delim_crc_fail_detected);
	seq_printf(file, "\tscratch_0%d\n", status_regs->scratch_0);
	seq_printf(file, "\tscratch_1 %d\n", status_regs->scratch_1);
	seq_printf(file, "\tscratch_2 %d\n", status_regs->scratch_2);
	seq_printf(file, "\tscratch_3 %d\n", status_regs->scratch_3);

}


const struct morse_yaps_ops morse_yaps_hw_ops = {
	.write_pkts = morse_yaps_hw_write_pkts,
	.read_pkts = morse_yaps_hw_read_pkts,
	.update_status = morse_yaps_hw_update_status,
	.show = morse_yaps_hw_show
};

void morse_yaps_hw_enable_irqs(struct morse *mors, bool enable)
{
	morse_hw_irq_enable(mors, MORSE_INT_YAPS_FC_PKT_WAITING_IRQn, enable);
	morse_hw_irq_enable(mors, MORSE_INT_YAPS_FC_PACKET_FREED_UP_IRQn, enable);
}

int morse_yaps_hw_init(struct morse *mors)
{
	int ret = 0;
	int flags;
	struct morse_yaps *yaps = NULL;
	struct morse_yaps_hw_table tbl_ptr;

	morse_claim_bus(mors);

	mors->chip_if = kzalloc(sizeof(struct morse_chip_if_state), GFP_KERNEL);
	if (!mors->chip_if) {
		ret = -ENOMEM;
		goto err_exit;
	}

	mors->chip_if->yaps = kzalloc(sizeof(struct morse_yaps), GFP_KERNEL);
	if (!mors->chip_if->yaps) {
		ret = -ENOMEM;
		goto err_exit;
	}

	yaps = mors->chip_if->yaps;
	yaps->aux_data =
		kzalloc(sizeof(struct morse_yaps_hw_aux_data), GFP_KERNEL);
	if (!yaps->aux_data) {
		ret = -ENOMEM;
		goto err_exit;
	}

	ret = morse_yaps_hw_read_table(mors, &tbl_ptr);
	if (ret) {
		morse_err(mors, "morse_yaps_hw_read_table failed %d\n", ret);
		goto err_exit;
	}

	yaps->aux_data->to_chip_buffer =
		kzalloc(YAPS_HW_WINDOW_SIZE_BYTES, GFP_KERNEL);
	if (!yaps->aux_data->to_chip_buffer) {
		ret = -ENOMEM;
		goto err_exit;
	}

	yaps->aux_data->from_chip_buffer =
		kzalloc(YAPS_HW_WINDOW_SIZE_BYTES, GFP_KERNEL);
	if (!yaps->aux_data->from_chip_buffer) {
		ret = -ENOMEM;
		goto err_exit;
	}

	yaps->ops = &morse_yaps_hw_ops;

	/* This is mostly for compatability with pageset API.
	 * We just have one YAPS instance that does everything
	 */
	flags = MORSE_CHIP_IF_FLAGS_DATA | MORSE_CHIP_IF_FLAGS_COMMAND |
		MORSE_CHIP_IF_FLAGS_DIR_TO_HOST | MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP;

	ret = morse_yaps_init(mors, yaps, flags);
	if (ret) {
		morse_err(mors, "morse_yaps_init failed %d\n", ret);
		goto err_exit;
	}

	INIT_WORK(&mors->chip_if_work, morse_yaps_work);
	INIT_WORK(&mors->tx_stale_work, morse_yaps_stale_tx_work);

	/* yaps irq will claim and release the bus */
	morse_release_bus(mors);

	/* Enable interrupts */
	morse_yaps_hw_enable_irqs(mors, true);


	return ret;

err_exit:
	morse_yaps_finish(yaps);
	morse_yaps_hw_finish(mors);
	morse_release_bus(mors);
	return ret;
}

void morse_yaps_hw_yaps_flush_tx_data(struct morse *mors)
{
	struct morse_yaps *yaps = mors->chip_if->yaps;

	if ((yaps->flags & MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP) &&
		(yaps->flags &
			(MORSE_CHIP_IF_FLAGS_DATA | MORSE_CHIP_IF_FLAGS_BEACON)))
		morse_yaps_flush_tx_data(yaps);
}

void morse_yaps_hw_finish(struct morse *mors)
{
	struct morse_yaps *yaps;

	if (!mors->chip_if)
		return;

	yaps = mors->chip_if->yaps;
	morse_yaps_hw_enable_irqs(mors, false);
	cancel_work_sync(&mors->chip_if_work);
	morse_yaps_finish(yaps);
	cancel_work_sync(&mors->tx_stale_work);
	if (yaps->aux_data) {
		kfree(yaps->aux_data->from_chip_buffer);
		yaps->aux_data->from_chip_buffer = NULL;
		kfree(yaps->aux_data->to_chip_buffer);
		yaps->aux_data->to_chip_buffer = NULL;
		kfree(yaps->aux_data);
		yaps->aux_data = NULL;
	}
	yaps->ops = NULL;
	kfree(yaps);
	mors->chip_if->yaps = NULL;
	kfree(mors->chip_if);
	mors->chip_if = NULL;
}
