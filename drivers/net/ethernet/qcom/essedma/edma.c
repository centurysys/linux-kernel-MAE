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

#include <linux/platform_device.h>
#include "ess_edma.h"
#include "edma.h"

/* The array values are the tx queue number supported by the core */
u8 edma_skb_priority_tbl[8] = {0, 0, 1, 1, 2, 2, 3, 3};

/*
 * edma_alloc_tx_ring()
 *	Allocate Tx descriptors ring
 */
static int edma_alloc_tx_ring(struct edma_common_info *c_info,
		struct edma_tx_desc_ring *etdr)
{
	struct platform_device *pdev = c_info->pdev;

	/* Initialize ring */
	etdr->size = sizeof(struct edma_sw_desc) * etdr->count;
	etdr->sw_next_to_fill = 0;
	etdr->sw_next_to_clean = 0;


	/* Allocate SW descriptors */
	etdr->sw_desc = vzalloc(etdr->size);
	if (!etdr->sw_desc) {
		dev_err(&pdev->dev, "buffer alloc of tx ring failed=%p", etdr);
		return -ENOMEM;
	}

	/* Allocate HW descriptors */
	etdr->hw_desc = dma_alloc_coherent(&pdev->dev, etdr->size, &etdr->dma,
				GFP_KERNEL);
	if (!etdr->hw_desc) {
		dev_err(&pdev->dev, "descriptor allocation for tx ring failed");
		vfree(etdr->sw_desc);
		return -ENOMEM;
	}

	return 0;
}

/*
 * edma_free_tx_ring()
 *	Free tx rings allocated by edma_alloc_tx_rings
 */
static void edma_free_tx_ring(struct edma_common_info *c_info,
		struct edma_tx_desc_ring *etdr)
{
	struct platform_device *pdev = c_info->pdev;

	if (etdr->dma)
		dma_free_coherent(&pdev->dev, etdr->size, &etdr->hw_desc,
			etdr->dma);

	vfree(etdr->sw_desc);
	etdr->sw_desc = NULL;
}

/*
 * edma_alloc_rx_ring()
 *	allocate rx descriptor ring
 */
static int edma_alloc_rx_ring(struct edma_common_info *c_info,
		struct edma_rfd_desc_ring *erxd)
{
	struct platform_device *pdev = c_info->pdev;
	int size;

	erxd->size = sizeof(struct edma_sw_desc) * erxd->count;
	erxd->sw_next_to_fill = 0;
	erxd->sw_next_to_clean = 0;

	/* Allocate SW descriptors */
	erxd->sw_desc = vzalloc(erxd->size);
	if (!erxd->sw_desc)
		return -ENOMEM;

	/* Alloc HW descriptors */
	erxd->hw_desc = dma_alloc_coherent(&pdev->dev, erxd->size, &erxd->dma,
			GFP_KERNEL);
	if (!erxd->hw_desc) {
		vfree(erxd->sw_desc);
		return -ENOMEM;
	}

	return 0;
}

/*
 * edma_free_rx_ring()
 *	Free rx ring allocated by alloc_rx_ring
 */
static void edma_free_rx_ring(struct edma_common_info *c_info,
		struct edma_rfd_desc_ring *rxdr)
{
	struct platform_device *pdev = c_info->pdev;

	if (rxdr->dma)
		dma_free_coherent(&pdev->dev, rxdr->size, &rxdr->hw_desc,
			rxdr->dma);

	kfree(rxdr->sw_desc);
	rxdr->sw_desc = NULL;
}

/*
 * edma_configure_tx()
 *	Configure transmission control data
 */
static void edma_configure_tx(struct edma_common_info *c_info)
{
	u32 txq_ctrl_data;

	txq_ctrl_data = (EDMA_TPD_BURST << EDMA_TXQ_NUM_TPD_BURST_SHIFT);
	txq_ctrl_data |= EDMA_TXQ_CTRL_TPD_BURST_EN;
	txq_ctrl_data |= (EDMA_TXF_BURST << EDMA_TXQ_TXF_BURST_NUM_SHIFT);
	edma_write_reg(REG_TXQ_CTRL, txq_ctrl_data);
}

/*
 * edma_configure_rx()
 *	configure reception control data
 */
static void edma_configure_rx(struct edma_common_info *c_info)
{
	struct edma_hw *hw = &c_info->hw;
	u32 rss_type, rx_desc1, rxq_ctrl_data;

	/* Set RSS type */
	rss_type = hw->rss_type;
	edma_write_reg(REG_RSS_TYPE, rss_type);

	/* Set RFD burst number */
	rx_desc1 = (EDMA_RFD_BURST << RXQ_RFD_BURST_NUM_SHIFT);

	/* Set RFD prefetch threshold */
	rx_desc1 |= (EDMA_RFD_THR << RXQ_RFD_PF_THRESH_SHIFT);

	/* Set RFD in host ring low threshold to generte interrupt */
	rx_desc1 |= (EDMA_RFD_LTHR << RXQ_RFD_LOW_THRESH_SHIFT);
	edma_write_reg(REG_RX_DESC1, rx_desc1);

	/* Set Rx FIFO threshold to start to DMA data to host */
	rxq_ctrl_data = FIFO_THRESH_128_BYTE;
	edma_write_reg(REG_RXQ_CTRL, rxq_ctrl_data);
}

/*
 * edma_alloc_rx_buf()
 *	does skb allocation for the received packets.
 */
static int edma_alloc_rx_buf(struct edma_common_info *c_info,
	struct edma_rfd_desc_ring *erdr, int cleaned_count, int queue_id)
{
	struct platform_device *pdev = c_info->pdev;
	struct edma_rx_free_desc *rx_desc;
	struct edma_sw_desc *sw_desc;
	struct sk_buff *skb;
	unsigned int i;
	u16 prod_idx, length;
	u32 reg_data;

	if (cleaned_count > erdr->count) {
		dev_err(&pdev->dev, "Incorrect cleaned_count %d",
				cleaned_count);
		return -1;
	}

	i = erdr->sw_next_to_fill;

	while (cleaned_count--) {
		sw_desc = &erdr->sw_desc[i];
		length = c_info->rx_buffer_len;

		/* alloc skb */
		skb = netdev_alloc_skb(c_info->netdev[0],
				length);
		if (unlikely(!skb)) {
			/* Better luck next round */
			break;
		}

		sw_desc->dma = dma_map_single(&pdev->dev, skb->data,
			length, DMA_FROM_DEVICE);
		if (dma_mapping_error(&pdev->dev, sw_desc->dma)) {
			dev_kfree_skb(skb);
			break; /* while !sw_desc->skb */
		}
		/* Update the buffer info */
		sw_desc->skb = skb;
		sw_desc->length = length;
		rx_desc = (&((struct edma_rx_free_desc *)(erdr->hw_desc))[i]);
		rx_desc->buffer_addr = cpu_to_le64(sw_desc->dma);
		if (unlikely(++i == erdr->count))
			i = 0;
	}

	erdr->sw_next_to_fill = i;

	if (unlikely(i == 0))
		prod_idx = erdr->count - 1;
	else
		prod_idx = i - 1;

	/* Update the producer index */
	edma_read_reg(REG_RFD_IDX_Q(queue_id), &reg_data);
	reg_data &= ~RFD_PROD_IDX_BITS;
	reg_data |= prod_idx;
	edma_write_reg(REG_RFD_IDX_Q(queue_id), reg_data);
	return 0;
}

/*
 * edma_init_desc()
 *	update descriptor ring size, buffer and producer/consumer index
 */
static void edma_init_desc(struct edma_common_info *c_info)
{
	struct edma_rfd_desc_ring *rfd_ring;
	struct edma_tx_desc_ring *etdr;
	int i = 0;
	volatile u32 data = 0;
	u16 hw_cons_idx = 0;

	/* Set the base address of every TPD ring. */
	for (i = 0; i < c_info->num_tx_queues; i++) {
		etdr = c_info->tpd_ring[i];

		/* Update descriptor ring base address */
		edma_write_reg(REG_TPD_BASE_ADDR_Q(i),
			(u32)(etdr->dma & 0xffffffff));
		edma_read_reg(REG_TPD_IDX_Q(i), &data);

		/* Calculate hardware consumer index */
		hw_cons_idx = (data >> TPD_CONS_IDX_SHIFT) & 0xffff;
		etdr->sw_next_to_fill = hw_cons_idx;
		etdr->sw_next_to_clean = hw_cons_idx;
		data &= ~(TPD_PROD_IDX_MASK << TPD_PROD_IDX_SHIFT);
		data |= hw_cons_idx;

		/* update producer index */
		edma_write_reg(REG_TPD_IDX_Q(i), data);

		/* update SW consumer index register */
		edma_write_reg(REG_TX_SW_CONS_IDX_Q(i), hw_cons_idx);

		/* Set TPD ring size */
		edma_write_reg(REG_TPD_RING_SIZE,
			(u32)(c_info->tx_ring_count & TPD_RING_SIZE_MASK));
	}

	for (i = 0; i < c_info->num_rx_queues; i++) {
		rfd_ring = c_info->rfd_ring[i];

		/* Update Receive Free descriptor ring base address */
		edma_write_reg(REG_RFD_BASE_ADDR_Q(i),
			(u32)(rfd_ring->dma & 0xffffffff));
		edma_read_reg(REG_RFD_BASE_ADDR_Q(i), &data);

		/* Update RFD ring size and RX buffer size */
		data = (c_info->rx_ring_count & RFD_RING_SIZE_MASK)
					<< RFD_RING_SIZE_SHIFT;
		data |= (c_info->rx_buffer_len & RX_BUF_SIZE_MASK)
					<< RX_BUF_SIZE_SHIFT;
		edma_write_reg(REG_RX_DESC0, data);
	}

	/* Disable TX FIFO low watermark and high watermark */
	edma_write_reg(REG_TXF_WATER_MARK, 0);

	/* Load all of base address above */
	edma_read_reg(REG_TX_SRAM_PART, &data);
	data |= 1 << LOAD_PTR_SHIFT;
	edma_write_reg(REG_TX_SRAM_PART, data);
}

/*
 * edma_rx_complete()
 *	Main api called from the poll function to process rx packets.
 */
static void edma_rx_complete(struct edma_common_info *c_info,
		int *work_done, int work_to_do, int queue_id)
{
	u16 cleaned_count = 0;
	u16 length = 0;
	int i = 0;
	u8 rrd[16];
	volatile u32 data = 0;
	u16 hw_next_to_clean = 0;
	u16 sw_next_to_clean;
	struct platform_device *pdev = c_info->pdev;
	struct edma_rfd_desc_ring *erdr = c_info->rfd_ring[queue_id];
	struct edma_sw_desc *sw_desc, *next_buffer;
	struct sk_buff *skb;
	struct edma_rx_free_desc *rfd_desc, *next_rfd_desc;
	sw_next_to_clean = erdr->sw_next_to_clean;

	while (1) {
		sw_desc = &erdr->sw_desc[sw_next_to_clean];
		rfd_desc = (&((struct edma_rx_free_desc *)(erdr->hw_desc))[sw_next_to_clean]);
		edma_read_reg(REG_RFD_IDX_Q(queue_id), &data);
		hw_next_to_clean = (data >> RFD_CONS_IDX_SHIFT) &
				RFD_CONS_IDX_MASK;

		if (hw_next_to_clean == sw_next_to_clean)
			break;

		if (*work_done >= work_to_do)
			break;

		(*work_done)++;
		skb = sw_desc->skb;

		/* Unmap the allocated buffer */
		dma_unmap_single(&pdev->dev, sw_desc->dma,
				sw_desc->length, DMA_FROM_DEVICE);

		/* Get RRD */
		for (i = 0; i < 16; i++)
			rrd[i] = skb->data[i];

		/* use next descriptor */
		sw_next_to_clean = (sw_next_to_clean + 1) % erdr->count;

		next_rfd_desc = (&((struct edma_rx_free_desc *)(erdr->hw_desc))[sw_next_to_clean]);
		next_buffer = &erdr->sw_desc[sw_next_to_clean];
		cleaned_count++;

		/* Check if RRD is valid */
		if (rrd[15] & 0x80) {

			/* Get the packet size and allocate buffer */
			length = ((rrd[13] & 0x3f) << 8) + rrd[12];

			/* Get the number of RFD from RRD */
		}

		skb_put(skb, length);

		/* Addition of 16 bytes is required, as in the packet
		 * first 16 bytes are rrd descriptors, so actual data
		 * starts from an offset of 16.
		 */
		skb->data += 16;
		skb->protocol = eth_type_trans(skb, c_info->netdev[0]);
		netif_receive_skb(skb);
	}

	erdr->sw_next_to_clean = sw_next_to_clean;

	/* alloc_rx_buf */
	if (cleaned_count) {
		edma_alloc_rx_buf(c_info, erdr, cleaned_count, queue_id);
		edma_write_reg(REG_RX_SW_CONS_IDX_Q(queue_id),
					erdr->sw_next_to_clean);
	}
}


/*
 * edma_tx_unmap_and_free()
 *	clean TX buffer
 */
static inline void edma_tx_unmap_and_free(struct platform_device *pdev,
		struct edma_sw_desc *sw_desc)
{
	struct sk_buff *skb = sw_desc->skb;

	/* unmap_single or unmap_page */
	if (sw_desc->dma) {
		dma_unmap_single(&pdev->dev, sw_desc->dma,
			sw_desc->length, DMA_TO_DEVICE);
	}

	dev_kfree_skb_any(skb);
	sw_desc->dma = 0;
	sw_desc->skb = NULL;
}

/*
 * edma_tx_complete()
 *	Used to clean tx queues and update hardware and consumer index
 */
static void edma_tx_complete(struct edma_common_info *c_info, int queue_id)
{
	struct edma_tx_desc_ring *etdr = c_info->tpd_ring[queue_id];
	struct edma_sw_desc *sw_desc;
	struct platform_device *pdev = c_info->pdev;

	u16 sw_next_to_clean = etdr->sw_next_to_clean;
	u16 hw_next_to_clean = 0;
	volatile u32 data = 0;
	edma_read_reg(REG_TPD_IDX_Q(queue_id), &data);
	hw_next_to_clean = (data >> TPD_CONS_IDX_SHIFT) & TPD_CONS_IDX_MASK;

	/* clean the buffer here */
	while (sw_next_to_clean != hw_next_to_clean) {
		sw_desc = &etdr->sw_desc[sw_next_to_clean];
		edma_tx_unmap_and_free(pdev, sw_desc);
		sw_next_to_clean = (sw_next_to_clean + 1) % etdr->count;
		etdr->sw_next_to_clean = sw_next_to_clean;
	}

	/* update the TPD consumer index register */
	edma_write_reg(REG_TX_SW_CONS_IDX_Q(queue_id), sw_next_to_clean);

	/* As of now, we are defaulting to use netdev[0],
	 * we will generalise this, once we decide whether
	 * we want a single port(with vlan differentiation for
	 * wan and lan) or not.
	 */
	if (netif_queue_stopped(c_info->netdev[0]) &&
			netif_carrier_ok(c_info->netdev[0])) {
		netif_wake_queue(c_info->netdev[0]);
	}
}

/*
 * edma_get_tx_buffer()
 *	Get sw_desc corresponding to the TPD
 */
static struct edma_sw_desc *edma_get_tx_buffer(struct edma_common_info *c_info,
		struct edma_tx_desc *tpd, int queue_id)
{
	struct edma_tx_desc_ring *etdr = c_info->tpd_ring[queue_id];
	return &etdr->sw_desc[tpd - (struct edma_tx_desc *)etdr->hw_desc];
}

/*
 * edma_get_next_tpd()
 *	Return a TPD descriptor for transfer
 */
static struct edma_tx_desc *edma_get_next_tpd(struct edma_common_info *c_info,
		int queue_id)
{
	struct edma_tx_desc_ring *etdr = c_info->tpd_ring[queue_id];
	u16 sw_next_to_fill = etdr->sw_next_to_fill;
	struct edma_tx_desc *tpd_desc =
		(&((struct edma_tx_desc *)(etdr->hw_desc))[sw_next_to_fill]);

	etdr->sw_next_to_fill++;
	if (unlikely(etdr->sw_next_to_fill == etdr->count))
		etdr->sw_next_to_fill = 0;

	return tpd_desc;
}

/*
 * edma_tpd_available()
 *	Check number of free TPDs
 */
static inline u16 edma_tpd_available(struct edma_common_info *c_info,
		int queue_id)
{
	struct edma_tx_desc_ring *etdr = c_info->tpd_ring[queue_id];

	u16 sw_next_to_fill = 0;
	u16 sw_next_to_clean = 0;
	u16 count = 0;

	sw_next_to_clean = etdr->sw_next_to_clean;
	sw_next_to_fill = etdr->sw_next_to_fill;

	if (likely(sw_next_to_clean <= sw_next_to_fill))
		count = etdr->count;

	return count + sw_next_to_clean - sw_next_to_fill - 1;
}

/*
 * edma_tx_queue_get()
 *	Get the starting number of  the queue
 */
static inline int edma_tx_queue_get(struct edma_adapter *adapter,
		struct sk_buff *skb)
{
	struct edma_common_info *c_info = adapter->c_info;
	int id = smp_processor_id();
	struct queue_per_cpu_info *q_cinfo = &c_info->q_cinfo[id];

	/* skb->priority is used as an index to skb priority table
	 * and based on packet priority, correspong queue is assigned.
	 */
	return q_cinfo->tx_start + edma_skb_priority_tbl[skb->priority];
}

/*
 * edma_tx_update_hw_idx()
 *	update the producer index for the ring transmitted
 */
static void edma_tx_update_hw_idx(struct edma_common_info *c_info,
		struct sk_buff *skb, struct edma_tx_desc *tpd, int queue_id)
{
	struct edma_tx_desc_ring *etdr = c_info->tpd_ring[queue_id];
	volatile u32 tpd_idx_data;

	/* Read and update the producer index */
	edma_read_reg(REG_TPD_IDX_Q(queue_id), &tpd_idx_data);
	tpd_idx_data &= ~TPD_PROD_IDX_BITS;
	tpd_idx_data |= (etdr->sw_next_to_fill & TPD_PROD_IDX_MASK)
		<< TPD_PROD_IDX_SHIFT;

	edma_write_reg(REG_TPD_IDX_Q(queue_id), tpd_idx_data);
}

/*
 * edma_tx_map_and_fill()
 *	gets called from edma_xmit_frame
 *
 * This is where the dma of the buffer to be transmitted
 * gets mapped
 */
static int edma_tx_map_and_fill(struct edma_common_info *c_info,
		struct edma_adapter *adapter, struct sk_buff *skb,
		struct edma_tx_desc *tpd, int queue_id, unsigned long tx_flags)
{
	struct edma_sw_desc *sw_desc = NULL;
	struct platform_device *pdev = c_info->pdev;
	u16 buf_len = skb_headlen(skb);

	sw_desc = edma_get_tx_buffer(c_info, tpd, queue_id);
	sw_desc->dma = dma_map_single(&adapter->pdev->dev,
			skb->data, buf_len, DMA_TO_DEVICE);

	if (dma_mapping_error(&pdev->dev, sw_desc->dma))
		goto dma_error;

	tpd->addr = cpu_to_le32(sw_desc->dma);
	tpd->len  = cpu_to_le16(buf_len);

	tpd->word3 |= EDMA_PORT_ENABLE_ALL << EDMA_TPD_PORT_BITMAP_SHIFT;

	/* The last tpd */
	tpd->word1 |= 1 << EDMA_TPD_EOP_SHIFT;

	/* The last buffer info contain the skb address,
	 * so it will be free after unmap
	 */
	sw_desc->length = buf_len;
	sw_desc->skb = skb;

	return 0;

dma_error:
	dev_err(&pdev->dev, "TX DMA map failed\n");
	return -ENOMEM;
}

/*
 * edma_xmit()
 *	Main api to be called by the core for packet transmission
 */
netdev_tx_t edma_xmit(struct sk_buff *skb,
		struct net_device *netdev)
{
	struct edma_adapter *adapter = netdev_priv(netdev);
	struct edma_tx_desc *tpd;
	int queue_id = 0;
	struct edma_common_info *c_info = adapter->c_info;
	unsigned long tx_flags = 0;
	struct edma_tx_desc_ring *etdr;

	queue_id = edma_tx_queue_get(adapter, skb);
	etdr = c_info->tpd_ring[queue_id];

	/* Tx is not handled in bottom half context. Hence, we need to protect
	 * Tx from tasks and bottom half
	 */
	local_bh_disable();

	if (!edma_tpd_available(c_info, queue_id)) {

		/* not enough descriptor, just stop queue */
		netif_stop_queue(netdev);
		local_bh_enable();
		return NETDEV_TX_BUSY;
	}

	tpd = edma_get_next_tpd(c_info, queue_id);

	if (edma_tx_map_and_fill(c_info, adapter, skb, tpd,
		queue_id, tx_flags)) {
			dev_kfree_skb_any(skb);
			goto netdev_okay;
	}

	edma_tx_update_hw_idx(c_info, skb, tpd, queue_id);

netdev_okay:
	local_bh_enable();
	return NETDEV_TX_OK;
}

/*
 * edma_free_queues()
 *	Free the queues allocaated
 */
void edma_free_queues(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_tx_queues; i++) {
		if (c_info->tpd_ring[i])
			kfree(c_info->tpd_ring[i]);
		c_info->tpd_ring[i] = NULL;
	}

	for (i = 0; i < c_info->num_rx_queues; i++) {
		if (c_info->rfd_ring[i])
			kfree(c_info->rfd_ring[i]);
		c_info->rfd_ring[i] = NULL;
	}

	c_info->num_rx_queues = 0;
	c_info->num_tx_queues = 0;
	return;
}

/*
 * edma_alloc_tx_rings()
 *	Allocate rx rings
 */
int edma_alloc_tx_rings(struct edma_common_info *c_info)
{
	struct platform_device *pdev = c_info->pdev;
	int i, err = 0;

	for (i = 0; i < c_info->num_tx_queues; i++) {
		err = edma_alloc_tx_ring(c_info, c_info->tpd_ring[i]);
		if (err) {
			dev_err(&pdev->dev, "Tx Queue alloc %u failed\n", i);
			return err;
		}
	}

	return 0;
}

/*
 * edma_free_tx_rings()
 *	Free tx rings
 */
void edma_free_tx_rings(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_tx_queues; i++)
		edma_free_tx_ring(c_info, c_info->tpd_ring[i]);
}

/*
 * edma_alloc_rx_rings()
 *	Allocate rx rings
 */
int edma_alloc_rx_rings(struct edma_common_info *c_info)
{
	struct platform_device *pdev = c_info->pdev;
	int i, err = 0;

	for (i = 0; i < c_info->num_rx_queues; i++) {
		err = edma_alloc_rx_ring(c_info, c_info->rfd_ring[i]);
		if (err) {
			dev_err(&pdev->dev, "Rx Queue alloc%u failed\n", i);
			return err;
		}
	}

	return 0;
}

/*
 * edma_free_rx_rings()
 *	free rx rings
 */
void edma_free_rx_rings(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_rx_queues; i++)
		edma_free_rx_ring(c_info, c_info->rfd_ring[i]);
}

/*
 * edma_alloc_queues_tx()
 *	Allocate memory for all rings
 */
int edma_alloc_queues_tx(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_tx_queues; i++) {
		struct edma_tx_desc_ring *etdr;
		etdr = kzalloc(sizeof(struct edma_tx_desc_ring), GFP_KERNEL);
		if (!etdr)
			goto err;
		etdr->count = c_info->tx_ring_count;
		c_info->tpd_ring[i] = etdr;
	}

	return 0;
err:
	edma_free_queues(c_info);
	return -1;
}

/*
 * edma_alloc_queues_rx()
 *	Allocate memory for all rings
 */
int edma_alloc_queues_rx(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_rx_queues; i++) {
		struct edma_rfd_desc_ring *rfd_ring;
		rfd_ring = kzalloc(sizeof(struct edma_rfd_desc_ring),
				GFP_KERNEL);
		if (!rfd_ring)
			goto err;
		rfd_ring->count = c_info->rx_ring_count;
		rfd_ring->queue_index = i;
		c_info->rfd_ring[i] = rfd_ring;
	}
	return 0;
err:
	edma_free_queues(c_info);
	return -1;
}

/*
 * edma_configure()
 *	Configure skb, edma interrupts and control register.
 */
int edma_configure(struct edma_common_info *c_info)
{
	struct edma_hw *hw = &c_info->hw;
	u32 intr_modrt_data;
	u32 intr_ctrl_data = 0;
	int i = 0;

	edma_read_reg(REG_INTR_CTRL, &intr_ctrl_data);
	intr_ctrl_data &= ~(1 << INTR_SW_IDX_W_TYP_SHIFT);
	intr_ctrl_data |= hw->intr_sw_idx_w << INTR_SW_IDX_W_TYP_SHIFT;
	edma_write_reg(REG_INTR_CTRL, intr_ctrl_data);

	/* clear interrupt status */
	edma_write_reg(REG_RX_ISR, 0xff);
	edma_write_reg(REG_TX_ISR, 0xffff);
	edma_write_reg(REG_MISC_ISR, 0x1fff);
	edma_write_reg(REG_WOL_ISR, 0x1);

	/* Clear any WOL status */
	edma_write_reg(REG_WOL_CTRL, 0);
	intr_modrt_data = (EDMA_TX_IMT << IRQ_MODRT_TX_TIMER_SHIFT);
	intr_modrt_data |= (EDMA_RX_IMT << IRQ_MODRT_RX_TIMER_SHIFT);
	edma_write_reg(REG_IRQ_MODRT_TIMER_INIT, intr_modrt_data);
	edma_configure_tx(c_info);
	edma_configure_rx(c_info);

	/* Allocate the RX buffer */
	for (i = 0; i < c_info->num_rx_queues; i++) {
		struct edma_rfd_desc_ring *ring = c_info->rfd_ring[i];
		edma_alloc_rx_buf(c_info, ring, ring->count, i);
	}

	/* Configure descriptor Ring */
	edma_init_desc(c_info);
	return 0;
}

/*
 * edma_open()
 *	gets called when netdevice is up, start the queue.
 */
int edma_open(struct net_device *netdev)
{
	netif_carrier_on(netdev);
	netif_start_queue(netdev);
	return 0;
}

/*
 * edma_close()
 *	gets called when netdevice is down, stops the queue.
 */
int edma_close(struct net_device *netdev)
{
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	return 0;
}

/*
 * edma_irq_enable()
 *	Enable default interrupt generation settings
 */
void edma_irq_enable(struct edma_common_info *c_info)
{
	struct edma_hw *hw = &c_info->hw;
	int i;

	edma_write_reg(REG_RX_ISR, 0xFF);
	for (i = 0; i < c_info->num_rx_queues; i++)
		edma_write_reg(REG_RX_INT_MASK_Q(i), hw->rx_intr_mask);
	edma_write_reg(REG_TX_ISR, 0xFFFF);
	for (i = 0; i < c_info->num_tx_queues; i++)
		edma_write_reg(REG_TX_INT_MASK_Q(i), hw->tx_intr_mask);
}

/*
 * edma_irq_disable()
 *	Disable Interrupt
 */
void edma_irq_disable(struct edma_common_info *c_info)
{
	int i;

	for (i = 0; i < c_info->num_rx_queues; i++)
		edma_write_reg(REG_RX_INT_MASK_Q(i), 0x0);
	for (i = 0; i < c_info->num_tx_queues; i++)
		edma_write_reg(REG_TX_INT_MASK_Q(i), 0x0);
	edma_write_reg(REG_MISC_IMR, 0);
	edma_write_reg(REG_WOL_IMR, 0);
}

/*
 * edma_free_irqs()
 *	Free All IRQs
 */
void edma_free_irqs(struct edma_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev[0];
	struct edma_common_info *c_info = adapter->c_info;
	int i;

	for (i = 0; i < c_info->num_tx_queues; i++)
		free_irq(c_info->tx_irq[i], netdev);

	for (i = 0; i < c_info->num_rx_queues; i++)
		free_irq(c_info->rx_irq[i], netdev);
}

/*
 * edma_enable_rx_ctrl()
 *	Enable RX queue control
 */
void edma_enable_rx_ctrl(struct edma_hw *hw)
{
	volatile u32 data;

	edma_read_reg(REG_RXQ_CTRL, &data);
	data |= RXQ_CTRL_EN;
	edma_write_reg(REG_RXQ_CTRL, data);
}

/*
 * edma_emable_tx_ctrl()
 *	Enable TX queue control
 */
void edma_enable_tx_ctrl(struct edma_hw *hw)
{
	volatile u32 data;

	edma_read_reg(REG_TXQ_CTRL, &data);
	data |= TXQ_CTRL_TXQ_EN;
	edma_write_reg(REG_TXQ_CTRL, data);
}

/*
 * edma_stop_rx_tx()
 *	Disable RX/TQ Queue control
 */
void edma_stop_rx_tx(struct edma_hw *hw)
{
	volatile u32 data;

	edma_read_reg(REG_RXQ_CTRL, &data);
	data &= ~RXQ_CTRL_EN;
	edma_write_reg(REG_RXQ_CTRL, data);
	edma_read_reg(REG_TXQ_CTRL, &data);
	data &= ~TXQ_CTRL_TXQ_EN;
	edma_write_reg(REG_TXQ_CTRL, data);
}

/*
 * edma_reset()
 *	Reset the EDMA
 */
int edma_reset(struct edma_common_info *c_info)
{
	struct edma_hw *hw = &c_info->hw;
	int i;

	for (i = 0; i < c_info->num_rx_queues; i++)
		edma_write_reg(REG_RX_INT_MASK_Q(i), 0);
	for (i = 0; i < c_info->num_tx_queues; i++)
		edma_write_reg(REG_TX_INT_MASK_Q(i), 0);
	edma_write_reg(REG_MISC_IMR, 0);
	edma_write_reg(REG_WOL_IMR, 0);
	edma_write_reg(REG_RX_ISR, 0xff);
	edma_write_reg(REG_TX_ISR, 0xffff);
	edma_write_reg(REG_MISC_ISR, 0x1fff);
	edma_write_reg(REG_WOL_ISR, 0x1);

	edma_stop_rx_tx(hw);

	return 0;
}

/*
 * edma_set_mac()
 *	Change the Ethernet Address of the NIC
 *
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
int edma_set_mac_addr(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	return 0;
}

/*
 * edma_poll
 *	polling function that gets called when the napi gets scheduled.
 *
 * Main sequence of task performed in this api
 * is clear irq status -> clear_tx_irq -> clean_rx_irq->
 * enable interrupts.
 */
int edma_poll(struct napi_struct *napi, int budget)
{
	struct queue_per_cpu_info *q_cinfo = container_of(napi,
		struct queue_per_cpu_info, napi);
	struct edma_common_info *c_info = q_cinfo->c_info;
	u32 reg_data;
	u32 shadow_rx_status, shadow_tx_status;
	int queue_id;
	int i, work_done = 0;

	/* Store the Rx/Tx status by ANDing it with
	 * appropriate CPU RX?TX mask
	 */
	edma_read_reg(REG_RX_ISR, &reg_data);
	q_cinfo->rx_status |= reg_data & q_cinfo->rx_mask;
	shadow_rx_status = q_cinfo->rx_status;
	edma_read_reg(REG_TX_ISR, &reg_data);
	q_cinfo->tx_status |= reg_data & q_cinfo->tx_mask;
	shadow_tx_status = q_cinfo->tx_status;

	/* Every core will have a start, which will be computed
	 * in probe and stored in q_cinfo->tx_start variable.
	 * We will shift the status bit by tx_start to obtain
	 * status bits for the core on which the current processing
	 * is happening. Since, there are 4 tx queues per core,
	 * we will run the loop till we get the correct queue to clear.
	 */
	while (q_cinfo->tx_status) {
		queue_id = ffs(q_cinfo->tx_status) - 1;
		edma_tx_complete(c_info, queue_id);
		q_cinfo->tx_status &= ~(1 << queue_id);
	}

	/* Every core will have a start, which will be computed
	 * in probe and stored in q_cinfo->tx_start variable.
	 * We will shift the status bit by tx_start to obtain
	 * status bits for the core on which the current processing
	 * is happening. Since, there are 4 tx queues per core, we
	 * will run the loop till we get the correct queue to clear.
	 */
	while (q_cinfo->rx_status) {
		queue_id = ffs(q_cinfo->rx_status) - 1;
		edma_rx_complete(c_info, &work_done,
			budget, queue_id);

		if (work_done < budget)
			q_cinfo->rx_status &= ~(1 << queue_id);
		else
			break;
	}

	/* Clear the status register, to avoid the interrupts to
	 * reoccur.This clearing of interrupt status register is
	 * done here as writing to status register only takes place
	 * once the  producer/consumer index has been updated to
	 * reflect that the packet transmission/reception went fine.
	 */
	edma_write_reg(REG_RX_ISR, shadow_rx_status);
	edma_write_reg(REG_TX_ISR, shadow_tx_status);

	/* If budget not fully consumed, exit the polling mode */
	if (work_done < budget) {
		napi_complete(napi);

		/* re-enable the interrupts */
		for (i = 0; i < 2; i++)
			edma_write_reg(REG_RX_INT_MASK_Q(q_cinfo->rx_start + i), 0x1);
		for (i = 0; i < 4; i++)
			edma_write_reg(REG_TX_INT_MASK_Q(q_cinfo->tx_start + i), 0x1);
	}

	return work_done;
}

/*
 * edma interrupt()
 *	interrupt handler
 */
irqreturn_t edma_interrupt(int irq, void *dev)
{
	struct queue_per_cpu_info *q_cinfo = (struct queue_per_cpu_info *) dev;
	int i;

	/* Unmask the TX/RX interrupt register */
	for (i = 0; i < 2; i++)
		edma_write_reg(REG_RX_INT_MASK_Q(q_cinfo->rx_start + i), 0x0);

	for (i = 0; i < 4; i++)
		edma_write_reg(REG_TX_INT_MASK_Q(q_cinfo->tx_start + i), 0x0);

	napi_schedule(&q_cinfo->napi);

	return IRQ_HANDLED;
}
