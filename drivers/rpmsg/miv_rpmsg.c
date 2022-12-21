// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Mi-V RPMSG driver
 *
 * Copyright (c) 2021 - 2022 Microchip Technology Inc. All rights reserved.
 *
 * Author: Valentina Fernandez <valentina.fernandezalanis@microchip.com>
 *
 * Derived from the imx_rpmsg implementation:
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include <linux/mailbox_client.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/mailbox/miv_ihc.h>

/* Remote hart supports name service notifications */
#define MIV_RPMSG_F_NS		0

#define NUM_VIRTQUEUES		2

/* 32KB of memory for a unidirectional vring struct */
#define VRING_SIZE		0x8000

/* 64KB of memory for a bi-directional communication (Two vrings) */
#define TOTAL_VRING_SIZE	(VRING_SIZE * 2)

/*
 * Allocate 256 buffers of 512 bytes for each side. each buffer
 * will then have 16B for the msg header and 496B for the payload.
 * This will require a total space of 256KB for the buffers themselves, and
 * 3 pages for every vring (the size of the vring depends on the number of
 * buffers it supports).
 */
#define RPMSG_NUM_BUFS		(512)
#define RPMSG_BUF_SIZE		(512)
#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

/* The alignment between the consumer and producer parts of the vring */
#define RPMSG_VRING_ALIGN	(4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RPMSG_RING_SIZE	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, \
				RPMSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

struct miv_virdev {
	struct virtqueue *vq[NUM_VIRTQUEUES];
	struct mbox_chan *mbox;
	struct virtio_device vdev;
	struct notifier_block nb;
	u64 vring[NUM_VIRTQUEUES];
	int base_vq_id;
	int num_of_vqs;
	u64 features;
	u8 status;
};

struct miv_rpmsg_vproc {
	char *rproc_name;
	/* To prevent multiple mailbox sends run concurrently. */
	struct mutex lock;
	int vdev_nums;
#define MAX_VDEV_NUMS	1
	struct miv_virdev ivdev[MAX_VDEV_NUMS];
	struct work_struct rpmsg_work;
	struct blocking_notifier_head notifier;
	struct miv_ihc_msg miv_ihc_message;
	struct completion c;
	struct mbox_client mbox_client;
	bool initialized;
};

#define to_miv_virdev(vd) container_of(vd, struct miv_virdev, vdev)
#define to_miv_rpdev(vd, id) container_of(vd, struct miv_rpmsg_vproc, ivdev[id])

struct miv_rpmsg_vq_info {
	struct miv_rpmsg_vproc *rpdev;
	struct miv_virdev *virdev;
	void *addr;
	u16 index;
	u16 vq_id;
};

static struct miv_rpmsg_vproc miv_rpmsg_vproc[] = {
	{
		.rproc_name = "miv-rpmsg",
	},
};

static bool miv_rpmsg_notify(struct virtqueue *vq)
{
	struct miv_rpmsg_vq_info *rpvq = vq->priv;
	struct virtio_device *vdev = &rpvq->virdev->vdev;
	struct mbox_chan *mbox_chan = rpvq->virdev->mbox;
	struct miv_ihc_msg mbox_msg;
	int ret;

	/*
	 * If already initialized, do not notify each time rx buffers
	 * are consumed, since we are already sending an ACK using the
	 * Inter-Hart Communication (IHC) driver. The RPMsg-lite on the
	 * remote side has the RL_ALLOW_CONSUMED_BUFFERS_NOTIFICATION
	 * macro set to 0.
	 */
	if (rpvq->vq_id == rpvq->virdev->base_vq_id) {
		if (!rpvq->rpdev->initialized)
			rpvq->rpdev->initialized = true;
		else
			return true;
	}

	mbox_msg.msg[0] = rpvq->vq_id;

	mutex_lock(&rpvq->rpdev->lock);

	ret = mbox_send_message(mbox_chan, &mbox_msg);
	if (ret < 0) {
		dev_err(&vdev->dev, "failed to send message via mbox: %d\n", ret);
		goto fail_notify;
	}

	ret = wait_for_completion_timeout(&rpvq->rpdev->c,
					  msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(&vdev->dev, "timeout waiting for ack\n");
		goto fail_notify;
	}

	mutex_unlock(&rpvq->rpdev->lock);
	return true;

fail_notify:
	mutex_unlock(&rpvq->rpdev->lock);
	return false;
}

static int miv_rpmsg_callback(struct notifier_block *this,
			      unsigned long index, void *data)
{
	struct miv_virdev *virdev;
	struct virtio_device *vdev;
	u32 msg = *(u32 *)data;

	virdev = container_of(this, struct miv_virdev, nb);
	vdev = &virdev->vdev;

	/*
	 * A message may be sent from another context with a non-existent
	 * virtqueue id or one that is not present in this rpmsg virtio
	 * device. If the vq index does not match ours, we can safely
	 * ignore the message and return NOTIFY_DONE
	 */
	if (msg < virdev->base_vq_id || msg > virdev->base_vq_id + 1) {
		dev_info(&vdev->dev, "msg: 0x%x is invalid\n", msg);
		return NOTIFY_DONE;
	}
	msg -= virdev->base_vq_id;

	/*
	 * At this point, 'msg' contains the index of the vring which
	 * was just triggered.
	 */
	if (msg < virdev->num_of_vqs)
		vring_interrupt(msg, virdev->vq[msg]);

	return NOTIFY_DONE;
}

static void rpmsg_work_handler(struct work_struct *work)
{
	struct miv_rpmsg_vproc *rpdev = container_of(work, struct miv_rpmsg_vproc, rpmsg_work);
	u32 vqid;

	vqid = rpdev->miv_ihc_message.msg[0];

	blocking_notifier_call_chain(&rpdev->notifier, 0, &vqid);
}

static int set_vring_phy_buf(struct platform_device *pdev,
			     struct miv_rpmsg_vproc *rpdev, int vdev_nums)
{
	struct resource *res;
	resource_size_t size;

	u64 start, end;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	size = resource_size(res);
	start = res->start;
	end = res->start + size;

	if (start + (vdev_nums * TOTAL_VRING_SIZE) > end) {
		dev_err(&pdev->dev, "Too small memory size %x!\n", (u32)size);
		return -EINVAL;
	}

	for (i = 0; i < vdev_nums; i++) {
		rpdev->ivdev[i].vring[0] = start + (i * TOTAL_VRING_SIZE);
		rpdev->ivdev[i].vring[1] = start + VRING_SIZE + (i * TOTAL_VRING_SIZE);
	}

	return 0;
}

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned int index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name, bool ctx)
{
	struct virtqueue *vq;
	struct miv_rpmsg_vq_info *rpvq;
	struct miv_virdev *virdev = to_miv_virdev(vdev);
	struct miv_rpmsg_vproc *rpdev = to_miv_rpdev(virdev, virdev->base_vq_id >> 1);
	int err;

	rpvq = devm_kzalloc(&vdev->dev, sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	rpvq->addr = (__force void *)ioremap(virdev->vring[index], RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	memset_io(rpvq->addr, 0, RPMSG_RING_SIZE);

	vq = vring_new_virtqueue(index, RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN,
				 vdev, false, ctx, rpvq->addr, miv_rpmsg_notify, callback, name);
	if (!vq) {
		dev_err(&vdev->dev, "vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	virdev->vq[index] = vq;
	vq->priv = rpvq;
	rpvq->virdev = virdev;
	rpvq->index = index;
	rpvq->vq_id = virdev->base_vq_id + index;
	rpvq->rpdev = rpdev;
	mutex_init(&rpdev->lock);

	return vq;

unmap_vring:
	iounmap((__force void __iomem *)rpvq->addr);
free_rpvq:
	return ERR_PTR(err);
}

static void miv_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct miv_virdev *virdev = to_miv_virdev(vdev);
	struct miv_rpmsg_vproc *rpdev = to_miv_rpdev(virdev, virdev->base_vq_id / 2);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct miv_rpmsg_vq_info *rpvq = vq->priv;

		iounmap(rpvq->addr);
		vring_del_virtqueue(vq);
	}

	blocking_notifier_chain_unregister(&rpdev->notifier, &virdev->nb);
}

static int miv_rpmsg_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char * const names[],
			      const bool *ctx,
			      struct irq_affinity *desc)
{
	struct miv_virdev *virdev = to_miv_virdev(vdev);
	struct miv_rpmsg_vproc *rpdev = to_miv_rpdev(virdev, virdev->base_vq_id / 2);
	int i;

	if (nvqs != NUM_VIRTQUEUES)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i], ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			miv_rpmsg_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	virdev->num_of_vqs = nvqs;
	virdev->nb.notifier_call = miv_rpmsg_callback;
	blocking_notifier_chain_register(&rpdev->notifier, &virdev->nb);

	return 0;
}

static u64 miv_rpmsg_get_features(struct virtio_device *vdev)
{
	struct miv_virdev *virdev = to_miv_virdev(vdev);

	return virdev->features;
}

static void miv_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static int miv_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
	return 0;
}

static void miv_rpmsg_reset(struct virtio_device *vdev)
{
	struct miv_virdev *virdev = to_miv_virdev(vdev);

	virdev->status = 0;
}

static u8 miv_rpmsg_get_status(struct virtio_device *vdev)
{
	struct miv_virdev *virdev = to_miv_virdev(vdev);

	return virdev->status;
}

static void miv_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	struct miv_virdev *virdev = to_miv_virdev(vdev);

	virdev->status = status;
}

static struct virtio_config_ops miv_rpmsg_config_ops = {
	.get_features = miv_rpmsg_get_features,
	.finalize_features = miv_rpmsg_finalize_features,
	.find_vqs = miv_rpmsg_find_vqs,
	.del_vqs = miv_rpmsg_del_vqs,
	.reset = miv_rpmsg_reset,
	.set_status = miv_rpmsg_set_status,
	.get_status = miv_rpmsg_get_status,
};

static void miv_mbox_rx_callback(struct mbox_client *cl, void *msg)
{
	struct miv_rpmsg_vproc *rpmsg_vproc;

	rpmsg_vproc = container_of(cl, struct miv_rpmsg_vproc, mbox_client);

	if (WARN_ON(!rpmsg_vproc))
		return;

	rpmsg_vproc->miv_ihc_message = *(struct miv_ihc_msg *)msg;

	schedule_work(&rpmsg_vproc->rpmsg_work);
}

static void tx_done_callback(struct mbox_client *cl, void *msg, int r)
{
	struct miv_rpmsg_vproc *rpmsg_vproc = container_of(cl, struct miv_rpmsg_vproc, mbox_client);

	complete(&rpmsg_vproc->c);
}

static int miv_rpmsg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct miv_rpmsg_vproc *rpdev;
	int i, ret;

	/*
	 * Assign a single virtual rpmsg container and set the number of
	 * virtual devices to one to represent communication with one
	 * remote AMP context
	 */
	rpdev = &miv_rpmsg_vproc[0];
	rpdev->vdev_nums = 1;

	ret = of_count_phandle_with_args(np, "mboxes", "#mbox-cells");
	if (ret <= 0) {
		ret = -ENODEV;
		return dev_err_probe(dev, ret, "no mboxes property in '%pOF'\n", np);
	}

	rpdev->mbox_client.dev = dev;
	rpdev->mbox_client.tx_done = tx_done_callback;
	rpdev->mbox_client.rx_callback = miv_mbox_rx_callback;
	rpdev->mbox_client.tx_block = false;
	rpdev->mbox_client.tx_tout = 0;
	rpdev->mbox_client.knows_txdone = false;

	init_completion(&rpdev->c);
	BLOCKING_INIT_NOTIFIER_HEAD(&rpdev->notifier);
	INIT_WORK(&rpdev->rpmsg_work, rpmsg_work_handler);

	for (i = 0; i < rpdev->vdev_nums; i++) {
		rpdev->ivdev[i].mbox = mbox_request_channel(&rpdev->mbox_client, i);

		if (IS_ERR(rpdev->ivdev[i].mbox)) {
			return dev_err_probe(dev, PTR_ERR(rpdev->ivdev[i].mbox),
					     "Failed to request mbox channel\n");
		}
	}

	ret = set_vring_phy_buf(pdev, rpdev, rpdev->vdev_nums);
	if (ret)
		return dev_err_probe(dev, ret, "No vring buffer.\n");

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		return dev_err_probe(dev, ret, "init reserved memory failed\n");

	for (i = 0; i < rpdev->vdev_nums; i++) {
		dev_dbg(dev, "%s vdev%d: vring0 0x%llx, vring1 0x%llx\n", __func__,
			rpdev->vdev_nums,
			rpdev->ivdev[i].vring[0],
			rpdev->ivdev[i].vring[1]);

		rpdev->ivdev[i].vdev.id.device = VIRTIO_ID_RPMSG;
		rpdev->ivdev[i].vdev.config = &miv_rpmsg_config_ops;
		rpdev->ivdev[i].vdev.dev.parent = &pdev->dev;
		rpdev->ivdev[i].vdev.dev.release = miv_rpmsg_vproc_release;
		rpdev->ivdev[i].base_vq_id = i * NUM_VIRTQUEUES;
		rpdev->ivdev[i].features = 1 << MIV_RPMSG_F_NS;

		ret = register_virtio_device(&rpdev->ivdev[i].vdev);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register rpdev\n");
	}

	dev_info(dev, "Registered Mi-V RPMsg driver\n");
	platform_set_drvdata(pdev, rpdev);

	return 0;
}

static int miv_rpmsg_remove(struct platform_device *pdev)
{
	struct miv_rpmsg_vproc *rpdev = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < rpdev->vdev_nums; i++) {
		mbox_free_channel(rpdev->ivdev[i].mbox);
		unregister_virtio_device(&rpdev->ivdev[i].vdev);
	}

	return 0;
}

static const struct of_device_id miv_rpmsg_of_match[] = {
	{ .compatible = "microchip,miv-rpmsg", },
	{}
};

MODULE_DEVICE_TABLE(of, miv_rpmsg_of_match);
static struct platform_driver miv_rpmsg_driver = {
	.driver = {
		.name = "miv_rpmsg",
		.of_match_table = miv_rpmsg_of_match
	},
	.probe = miv_rpmsg_probe,
	.remove = miv_rpmsg_remove,
};

module_platform_driver(miv_rpmsg_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valentina Fernandez <valentina.fernandezalanis@microchip.com>");
MODULE_DESCRIPTION("Mi-V rpmsg driver");
