/* Copyright (c) 2016 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include "remoteproc_internal.h"
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#define WCSS_CRASH_REASON_SMEM 421

struct q6v5_rtable {
	struct resource_table rtable;
	struct fw_rsc_hdr last_hdr;
};

static struct q6v5_rtable q6v5_rtable = {
	.rtable = {
		.ver = 1,
		.num = 0,
	},
	.last_hdr = {
		.type = RSC_LAST,
	},
};

struct q6v5_rproc_pdata {
	void __iomem *q6_base;
	struct rproc *rproc;
	struct completion start_done;
	struct completion stop_done;
	struct qcom_smem_state *state;
	unsigned stop_bit;
	unsigned shutdown_bit;
	bool running;
	int emulation;
};

static struct q6v5_rproc_pdata *q6v5_rproc_pdata;

static struct resource_table *q6v5_find_loaded_rsc_table(struct rproc *rproc,
	const struct firmware *fw)
{
	q6v5_rtable.rtable.offset[0] = sizeof(struct resource_table);
	return &(q6v5_rtable.rtable);
}

static struct resource_table *q6v5_find_rsc_table(struct rproc *rproc,
	const struct firmware *fw, int *tablesz)
{
	*tablesz = sizeof(struct q6v5_rtable);

	q6v5_rtable.rtable.offset[0] = sizeof(struct resource_table);
	return &(q6v5_rtable.rtable);
}

static int q6_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct q6v5_rproc_pdata *pdata = platform_get_drvdata(pdev);
	int ret;

	pdata->running = false;

	qcom_smem_state_update_bits(pdata->state,
			BIT(pdata->stop_bit), BIT(pdata->stop_bit));

	ret = wait_for_completion_timeout(&pdata->stop_done,
			msecs_to_jiffies(10000));

	if (ret == 0)
		pr_err("Timedout waiting for stop-ack\n");

	qcom_smem_state_update_bits(pdata->state, BIT(pdata->stop_bit), 0);

	return 0;
}

static int q6_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct q6v5_rproc_pdata *pdata = platform_get_drvdata(pdev);
	int temp = 19;
	unsigned long val = 0;
	unsigned int nretry = 0;
	int ret = 0;

#define QDSP6SS_RST_EVB 0x10
#define QDSP6SS_RESET 0x14
#define QDSP6SS_DBG_CFG 0x18
#define QDSP6SS_GFMUX_CTL 0x20
#define QDSP6SS_XO_CBCR 0x38
#define QDSP6SS_PWR_CTL 0x30
#define QDSP6SS_MEM_PWR_CTL 0xb0
#define QDSP6SS_SLEEP_CBCR 0x3C
#define QDSP6SS_BHS_STATUS 0x78
#define BHS_EN_REST_ACK BIT(0)

	/* Write bootaddr to EVB so that Q6WCSS will jump there after reset */
	writel(rproc->bootaddr >> 4, pdata->q6_base + QDSP6SS_RST_EVB);
	/* Turn on XO clock. It is required for BHS and memory operation */
	writel(0x1, pdata->q6_base + QDSP6SS_XO_CBCR);
	/* Turn on BHS */
	writel(0x1700000, pdata->q6_base + QDSP6SS_PWR_CTL);
	/* Wait till BHS Reset is done */
	if (pdata->emulation == 1) {
		mdelay(100);
	} else {
		while (1) {
			val = readl(pdata->q6_base + QDSP6SS_BHS_STATUS);
			if (val & BHS_EN_REST_ACK)
				break;
			mdelay(1);
			nretry++;
			if (nretry >= 10) {
				pr_err("Can't bring q6 out of reset\n");
				return -EIO;
			}
		}
	}
	/* Put LDO in bypass mode */
	writel(0x3700000, pdata->q6_base + QDSP6SS_PWR_CTL);
	/* De-assert QDSP6 complier memory clamp */
	writel(0x3300000, pdata->q6_base + QDSP6SS_PWR_CTL);
	/* De-assert memory peripheral sleep and L2 memory standby */
	writel(0x33c0000, pdata->q6_base + QDSP6SS_PWR_CTL);

	/* turn on QDSP6 memory foot/head switch one bank at a time */
	while  (temp >= 0) {
		val = readl(pdata->q6_base + QDSP6SS_MEM_PWR_CTL);
		val = val | 1 << temp;
		writel(val, pdata->q6_base + QDSP6SS_MEM_PWR_CTL);
		val = readl(pdata->q6_base + QDSP6SS_MEM_PWR_CTL);
		udelay(2);
		temp -= 1;
	}
	/* Remove the QDSP6 core memory word line clamp */
	writel(0x31FFFFF, pdata->q6_base + QDSP6SS_PWR_CTL);
	/* Remove QDSP6 I/O clamp */
	writel(0x30FFFFF, pdata->q6_base + QDSP6SS_PWR_CTL);
	/* Bring Q6 out of reset and stop the core */
	writel(0x5, pdata->q6_base + QDSP6SS_RESET);
	mdelay(10);
	/* Retain debugger state during next QDSP6 reset */
	writel(0x0, pdata->q6_base + QDSP6SS_DBG_CFG);
	/* Turn on the QDSP6 core clock */
	writel(0x102, pdata->q6_base + QDSP6SS_GFMUX_CTL);
	/* Enable the core to run */
	writel(0x4, pdata->q6_base + QDSP6SS_RESET);
	/* Enable QDSP6SS Sleep clock */
	writel(0x1, pdata->q6_base + QDSP6SS_SLEEP_CBCR);

	ret = wait_for_completion_timeout(&pdata->start_done,
					msecs_to_jiffies(10000));
	if (ret == 0) {
		pr_err("Handover message not received\n");
		return -ETIMEDOUT;
	}

	pdata->running = true;

	return 0;
}

static void *q6_da_to_va(struct rproc *rproc, u64 da, int len)
{
	unsigned long addr = (__force unsigned long)(da & 0xFFFFFFFF);

	return ioremap(addr, len);
}

static struct rproc_ops q6v5_rproc_ops = {
	.start		= q6_rproc_start,
	.da_to_va	= q6_da_to_va,
	.stop		= q6_rproc_stop,
};

static struct rproc_fw_ops q6_fw_ops;

static irqreturn_t wcss_err_fatal_intr_handler(int irq, void *dev_id)
{
	struct q6v5_rproc_pdata *pdata = dev_id;
	char *msg;
	size_t len;

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, WCSS_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		pr_err("Fatal error received from wcss software!: %s\n", msg);
	else
		pr_err("Fatal error received no message!\n");

	panic("%s: System ramdump requested.!\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t wcss_handover_intr_handler(int irq, void *dev_id)
{
	struct q6v5_rproc_pdata *pdata = dev_id;

	pr_info("Received handover interrupt from wcss\n");
	complete(&pdata->start_done);
	return IRQ_HANDLED;
}

static irqreturn_t wcss_stop_ack_intr_handler(int irq, void *dev_id)
{
	struct q6v5_rproc_pdata *pdata = dev_id;

	pr_info("Received stop ack interrupt from wcss\n");
	complete(&pdata->stop_done);
	return IRQ_HANDLED;
}

static irqreturn_t wcss_wdog_bite_intr_handler(int irq, void *dev_id)
{
	struct q6v5_rproc_pdata *pdata = dev_id;
	char *msg;
	size_t len;

	if (!pdata->running) {
		complete(&pdata->stop_done);
		return IRQ_HANDLED;
	}

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, WCSS_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		pr_err("Watchdog bite received from wcss software!: %s\n", msg);
	else
		pr_err("Watchdog bit received no message!\n");

	panic("%s: System ramdump requested.!\n", __func__);

	return IRQ_HANDLED;
}


int start_q6(void)
{
	struct rproc *rproc = q6v5_rproc_pdata->rproc;
	int ret = 0;

	ret = rproc_add(rproc);
	if (ret)
		return ret;

	wait_for_completion(&rproc->firmware_loading_complete);

	ret = rproc_boot(rproc);
	if (ret) {
		pr_err("couldn't boot q6v5: %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL(start_q6);

int stop_q6(void)
{
	struct rproc *rproc = q6v5_rproc_pdata->rproc;

	rproc_shutdown(rproc);
	rproc_del(rproc);

	return 0;
}
EXPORT_SYMBOL(stop_q6);

static int q6v5_request_irq(struct q6v5_rproc_pdata *pdata,
				struct platform_device *pdev,
				const char *name,
				irq_handler_t thread_fn)
{
	int ret;

	ret = platform_get_irq_byname(pdev, name);
	if (ret < 0) {
		dev_err(&pdev->dev, "no %s IRQ defined\n", name);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, ret,
			NULL, thread_fn,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"wcss", pdata);
	if (ret)
		dev_err(&pdev->dev, "request %s IRQ failed\n", name);

	return ret;
}

static int wcss_panic_handler(struct notifier_block *this,
			unsigned long event, void *ptr)
{
	struct q6v5_rproc_pdata *pdata = ptr;

	pdata->running = false;

	qcom_smem_state_update_bits(pdata->state, BIT(pdata->shutdown_bit),
			BIT(pdata->shutdown_bit));
	mdelay(1000);
	return NOTIFY_DONE;
}

static struct notifier_block panic_nb = {
	.notifier_call  = wcss_panic_handler,
};


static int q6_rproc_probe(struct platform_device *pdev)
{
	const char *firmware_name;
	struct rproc *rproc;
	int ret;
	struct resource *resource;

	ret = dma_set_coherent_mask(&pdev->dev,
			DMA_BIT_MASK(sizeof(dma_addr_t) * 8));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(pdev->dev.of_node, "firmware",
			&firmware_name);
	if (ret) {
		dev_err(&pdev->dev, "couldn't read firmware name: %d\n", ret);
		return ret;
	}

	rproc = rproc_alloc(&pdev->dev, "q6v5-wcss", &q6v5_rproc_ops,
				firmware_name,
				sizeof(*q6v5_rproc_pdata));
	if (unlikely(!rproc))
		return -ENOMEM;

	q6v5_rproc_pdata = rproc->priv;
	q6v5_rproc_pdata->rproc = rproc;
	q6v5_rproc_pdata->emulation = of_property_read_bool(pdev->dev.of_node,
					"qca,emulation");
	rproc->has_iommu = false;

	q6_fw_ops = *(rproc->fw_ops);
	q6_fw_ops.find_rsc_table = q6v5_find_rsc_table;
	q6_fw_ops.find_loaded_rsc_table = q6v5_find_loaded_rsc_table;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		goto free_rproc;

	q6v5_rproc_pdata->q6_base = ioremap(resource->start,
					resource_size(resource));
	if (!q6v5_rproc_pdata->q6_base)
		goto free_rproc;

	platform_set_drvdata(pdev, q6v5_rproc_pdata);

	rproc->fw_ops = &q6_fw_ops;

	ret = q6v5_request_irq(q6v5_rproc_pdata, pdev, "wdog",
				wcss_wdog_bite_intr_handler);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(q6v5_rproc_pdata, pdev, "fatal",
				wcss_err_fatal_intr_handler);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(q6v5_rproc_pdata, pdev, "handover",
			wcss_handover_intr_handler);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(q6v5_rproc_pdata, pdev, "stop-ack",
			wcss_stop_ack_intr_handler);
	if (ret < 0)
		goto free_rproc;

	q6v5_rproc_pdata->state = qcom_smem_state_get(&pdev->dev, "stop",
			&q6v5_rproc_pdata->stop_bit);
	if (IS_ERR(q6v5_rproc_pdata->state))
		goto free_rproc;

	q6v5_rproc_pdata->state = qcom_smem_state_get(&pdev->dev, "shutdown",
			&q6v5_rproc_pdata->shutdown_bit);
	if (IS_ERR(q6v5_rproc_pdata->state))
		goto free_rproc;

	init_completion(&q6v5_rproc_pdata->start_done);
	init_completion(&q6v5_rproc_pdata->stop_done);
	q6v5_rproc_pdata->running = false;

	ret = atomic_notifier_chain_register(&panic_notifier_list,
				&panic_nb);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_put(rproc);
	return -EIO;
}

static int q6_rproc_remove(struct platform_device *pdev)
{
	struct q6v5_rproc_pdata *pdata;
	struct rproc *rproc;

	pdata = platform_get_drvdata(pdev);
	rproc = q6v5_rproc_pdata->rproc;

	rproc_del(rproc);
	rproc_put(rproc);

	return 0;
}

static const struct of_device_id q6_match_table[] = {
	{ .compatible = "qca,q6v5-wcss-rproc" },
	{}
};

static struct platform_driver q6_rproc_driver = {
	.probe = q6_rproc_probe,
	.remove = q6_rproc_remove,
	.driver = {
		.name = "q6v5-wcss",
		.of_match_table = q6_match_table,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(q6_rproc_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("QCA Remote Processor control driver");
