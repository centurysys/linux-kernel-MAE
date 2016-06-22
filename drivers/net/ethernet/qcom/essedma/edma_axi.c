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

#include "edma.h"
#include "ess_edma.h"

char edma_axi_driver_name[] = "ess_edma";
static const u32 default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE |
	NETIF_MSG_LINK | NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP;

static unsigned long edma_hw_addr;

struct net_device *netdev[2];

void edma_write_reg(u16 reg_addr, u32 reg_value)
{
	writel(reg_value, (((void __iomem *)edma_hw_addr + reg_addr)));
}

void edma_read_reg(u16 reg_addr, volatile u32 *reg_value)
{
	*reg_value = readl((void __iomem *)edma_hw_addr + reg_addr);
}

/*
 * edma_axi_netdev_ops
 *	Describe the operations supported by registered netdevices
 *
 * static const struct net_device_ops edma_axi_netdev_ops = {
 *	.ndo_open               = edma_open,
 *	.ndo_stop               = edma_close,
 *	.ndo_start_xmit         = edma_xmit_frame,
 *	.ndo_set_mac_address    = edma_set_mac_addr,
 * }
 */
static const struct net_device_ops edma_axi_netdev_ops = {
	.ndo_open               = edma_open,
	.ndo_stop               = edma_close,
	.ndo_start_xmit         = edma_xmit,
	.ndo_set_mac_address    = edma_set_mac_addr,
};

/*
 * edma_axi_probe()
 *	Initialise an adapter identified by a platform_device structure.
 *
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur in the probe.
 */
static int edma_axi_probe(struct platform_device *pdev)
{
	struct edma_common_info *c_info;
	struct edma_hw *hw;
	struct edma_adapter *adapter[1];
	struct resource *res;
	int i, j, err = 0, ret = 0;

	netdev[0] = alloc_etherdev(sizeof(struct edma_adapter));
	if (!netdev[0]) {
		dev_err(&pdev->dev, "net device alloc fails=%p\n", netdev[0]);
		goto err_alloc;
	}

	SET_NETDEV_DEV(netdev[0], &pdev->dev);

	platform_set_drvdata(pdev, netdev[0]);

	c_info = vzalloc(sizeof(struct edma_common_info));
	if (!c_info) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	c_info->pdev = pdev;
	c_info->netdev[0] = netdev[0];

	/* Fill ring details */
	c_info->num_tx_queues = EDMA_MAX_TRANSMIT_QUEUE;
	c_info->tx_ring_count = EDMA_TX_RING_SIZE;
	c_info->num_rx_queues = EDMA_MAX_RECEIVE_QUEUE;
	c_info->rx_ring_count = EDMA_RX_RING_SIZE;

	hw = &c_info->hw;

	/* Fill HW defaults */
	hw->tx_intr_mask = EDMA_TX_IMR_NORMAL_MASK;
	hw->rx_intr_mask = EDMA_RX_IMR_NORMAL_MASK;
	hw->rx_buff_size = EDMA_RX_BUFF_SIZE;
	hw->misc_intr_mask = 0;
	hw->wol_intr_mask = 0;

	hw->intr_clear_type = EDMA_INTR_CLEAR_TYPE;
	hw->intr_sw_idx_w = EDMA_INTR_SW_IDX_W_TYPE;
	hw->rss_type = EDMA_RSS_TYPE;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	c_info->hw.hw_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(c_info->hw.hw_addr)) {
		ret = PTR_ERR(c_info->hw.hw_addr);
		goto err_hwaddr;
	}

	edma_hw_addr = (unsigned long)c_info->hw.hw_addr;

	/* Parse tx queue interrupt number from device tree */
	for (i = 0; i < c_info->num_tx_queues; i++)
		c_info->tx_irq[i] = platform_get_irq(pdev, i);

	/* Parse rx queue interrupt number from device tree
	 * Here we are setting j to point to the point where we
	 * left tx interrupt parsing(i.e 16) and run run the loop
	 * from 0 to 7 to parse rx interrupt number.
	 */
	for (i = 0, j = c_info->num_tx_queues; i < c_info->num_rx_queues;
			i++, j++)
		c_info->rx_irq[i] = platform_get_irq(pdev, j);

	c_info->rx_buffer_len = c_info->hw.rx_buff_size;

	err = edma_alloc_queues_tx(c_info);
	if (err) {
		dev_err(&pdev->dev, "Allocation of TX queue failed\n");
		goto err_tx_qinit;
	}

	err = edma_alloc_queues_rx(c_info);
	if (err) {
		dev_err(&pdev->dev, "Allocation of RX queue failed\n");
		goto err_rx_qinit;
	}

	err = edma_alloc_tx_rings(c_info);
	if (err) {
		dev_err(&pdev->dev, "Allocation of TX resources failed\n");
		goto err_tx_rinit;
	}

	err = edma_alloc_rx_rings(c_info);
	if (err) {
		dev_err(&pdev->dev, "Allocation of RX resources failed\n");
		goto err_rx_rinit;
	}

	/* Populate the adapter structure register the netdevice */
	adapter[0] = netdev_priv(netdev[0]);
	adapter[0]->netdev[0] = netdev[0];
	adapter[0]->pdev = pdev;
	adapter[0]->c_info = c_info;
	netdev[0]->netdev_ops = &edma_axi_netdev_ops;
	err = register_netdev(netdev[0]);
	if (err)
		goto err_register;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev[0]);

	/* Disable all 16 Tx and 8 rx irqs */
	edma_irq_disable(c_info);

	err = edma_reset(c_info);
	if (err) {
		err = -EIO;
		goto err_reset;
	}

	/* populate per_core_info, do a napi_Add, request 16 TX irqs,
	 * 8 RX irqs, do a napi enable
	 */
	for (i = 0; i < EDMA_NR_CPU; i++) {
		u16 tx_start;
		u8 rx_start;
		c_info->q_cinfo[i].napi.state = 0;
		netif_napi_add(netdev[0], &c_info->q_cinfo[i].napi,
					edma_poll, 64);
		napi_enable(&c_info->q_cinfo[i].napi);
		c_info->q_cinfo[i].tx_mask = EDMA_TX_PER_CPU_MASK <<
					(i << EDMA_PER_CPU_MASK_SHIFT);
		c_info->q_cinfo[i].rx_mask = EDMA_RX_PER_CPU_MASK <<
					(i << EDMA_PER_CPU_MASK_SHIFT);
		c_info->q_cinfo[i].tx_start = tx_start =
					i << EDMA_TX_CPU_START_SHIFT;
		c_info->q_cinfo[i].rx_start =  rx_start =
					i << EDMA_RX_CPU_START_SHIFT;
		c_info->q_cinfo[i].tx_status = 0;
		c_info->q_cinfo[i].rx_status = 0;
		c_info->q_cinfo[i].c_info = c_info;

		/* Request irq per core */
		for (j = c_info->q_cinfo[i].tx_start; j < (tx_start + 4); j++) {
			err = request_irq(c_info->tx_irq[j], edma_interrupt,
				0x0, "edma_eth_tx",
					&c_info->q_cinfo[i]);
		}

		for (j = c_info->q_cinfo[i].rx_start; j < rx_start + 2; j++) {
			err = request_irq(c_info->rx_irq[j], edma_interrupt,
				0x0, "edma_eth_rx",
					&c_info->q_cinfo[i]);
		}
	}

	/* Used to clear interrupt status, allocate rx buffer,
	 * configure edma descriptors registers
	 */
	err = edma_configure(c_info);
	if (err) {
		err = -EIO;
		goto err_configure;
	}

	/* Enable All 16 tx and 8 rx irq mask */
	edma_irq_enable(c_info);
	edma_enable_tx_ctrl(&c_info->hw);
	edma_enable_rx_ctrl(&c_info->hw);

	return 0;

err_configure:
	edma_free_irqs(&adapter[0]);
	for (i = 0; i < EDMA_NR_CPU; i++)
		napi_disable(&c_info->q_cinfo[i].napi);
err_reset:
	unregister_netdev(netdev[0]);
err_register:
	edma_free_rx_rings(c_info);
err_rx_rinit:
	edma_free_tx_rings(c_info);
err_tx_rinit:
	edma_free_queues(c_info);
err_rx_qinit:
err_tx_qinit:
	iounmap(c_info->hw.hw_addr);
err_hwaddr:
	kfree(c_info);
err_ioremap:
	free_netdev(netdev[0]);
err_alloc:
	return err;
}

/*
 * edma_axi_remove - Device Removal Routine
 *	edma_axi_remove is called by the platform subsystem to alert the driver
 *	that it should release a platform device.
 */
static int edma_axi_remove(struct platform_device *pdev)
{
	struct edma_adapter *adapter = netdev_priv(netdev[0]);
	struct edma_common_info *c_info = adapter->c_info;
	struct edma_hw *hw = &c_info->hw;
	int id = smp_processor_id();

	edma_stop_rx_tx(hw);
	napi_disable(&c_info->q_cinfo[id].napi);
	edma_irq_disable(c_info);
	edma_free_irqs(&adapter[0]);
	edma_reset(c_info);
	edma_free_tx_rings(c_info);
	edma_free_rx_rings(c_info);
	edma_free_queues(c_info);
	unregister_netdev(adapter->netdev[0]);
	free_netdev(adapter->netdev[0]);

	return 0;
}

static void edma_axi_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id edma_of_mtable[] = {
	{.compatible = "qcom,ess-edma" },
	{}
};
MODULE_DEVICE_TABLE(of, edma_of_mtable);

static struct platform_driver edma_axi_driver = {
	.driver = {
		.name    = edma_axi_driver_name,
		.owner   = THIS_MODULE,
		.of_match_table = edma_of_mtable,
	},
	.probe    = edma_axi_probe,
	.remove   = edma_axi_remove,
	.shutdown = edma_axi_shutdown,
};

static int __init edma_axi_init_module(void)
{
	int ret;
	pr_info("edma module_init\n");
	ret = platform_driver_register(&edma_axi_driver);
	return ret;
}
module_init(edma_axi_init_module);

static void __exit edma_axi_exit_module(void)
{
	platform_driver_unregister(&edma_axi_driver);
	pr_info("edma module_exit\n");
}
module_exit(edma_axi_exit_module);

MODULE_AUTHOR("Qualcomm Atheros Inc");
MODULE_DESCRIPTION("QCA ESS EDMA driver");
MODULE_LICENSE("GPL");
