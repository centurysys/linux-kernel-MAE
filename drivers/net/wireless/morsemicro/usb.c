/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/module.h>
#include <linux/usb.h>

#include "morse.h"
#include "mac.h"
#include "debug.h"
#include "bus.h"

#define MORSE_USB_INTERRUPT_INTERVAL       8           /* High speed USB 8 * 125usec = 2msec */
#define USB_MAX_TRANSFER_SIZE              (16 * 1024) /* Maximum number of bytes per USB read/write */
#define MORSE_EP_INT_BUFFER_SIZE           8

/* Define these values to match your devices */
#define MORSE_VENDOR_ID                    0x325b
#define MORSE_MM610X_PRODUCT_ID            0x6100

enum morse_usb_endpoints {
	MORSE_EP_CMD = 0,			/* Commands endpoint */
	MORSE_EP_INT,				/* IRQ interrupt endpoint */
	MORSE_EP_MEM_RD,			/* Memory read endpoint */
	MORSE_EP_MEM_WR,			/* Memory write endpoint */
	MORSE_EP_REG_RD,			/* Register read endpoint */
	MORSE_EP_REG_WR,			/* Register write endpoint */
	MORSE_EP_EP_MAX,
};

struct morse_usb_endpoint {
	unsigned char *buffer;     /* the buffer to send/receive data */
	struct urb    *urb;        /* the urb to read/write data with */
	__u8          addr;        /* Address of endpoint */
	int           size;        /* Size of endpoint */
};

struct morse_usb {
	struct usb_device	*udev;		/* the usb device for this device */
	struct usb_interface	*interface;	/* the interface for this device */

	/* Morse USB endpoints */
	struct morse_usb_endpoint	endpoints[MORSE_EP_EP_MAX];

	/* Track errors in USB callbacks */
	int			errors;

	/* protects concurent access */
	struct mutex lock;

	/* for claim and release bus */
	struct mutex bus_lock;

	/* for synchronisation between commands and transfers */
	bool			ongoing_rw;	/* a command is going on */
	wait_queue_head_t	rw_in_wait;	/* to wait for an ongoing command */
};

/*
 * Morse USB Read/Write command format
 */
enum morse_usb_command_direction {
	MORSE_USB_WRITE	= 0x00,
	MORSE_USB_READ	= 0x80,
};

struct morse_usb_command {
	__le32	dir;				/* Next BULK direction */
	__le32	address;			/* Next BULK address */
	__le32	length;				/* Next BULK size */
} __packet;

/* table of devices that work with this driver */
static const struct usb_device_id morse_usb_table[] = {
	{
		USB_DEVICE(MORSE_VENDOR_ID, MORSE_MM610X_PRODUCT_ID),
		.driver_info = (unsigned long) &mm6108c_cfg
	},
	{ } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, morse_usb_table);

#ifdef CONFIG_MORSE_USER_ACCESS
struct uaccess *morse_usb_uaccess;
#endif

static void morse_usb_buff_log(struct morse *mors, const char *buf,
								int length, const char *prefix)
{
	int i, n = 0;
	u8 *hex_buf = NULL;

	if (!(debug_mask & MORSE_MSG_DEBUG_USB))
		return;

	hex_buf = kzalloc(length * 3, GFP_KERNEL);

	for (i = 0; i < length; i++) {
		sprintf(&hex_buf[n], "%02X ", buf[i]);
		n += 3;
	}

	if (prefix)
		morse_dbg_usb(mors, "%s (%d) %s\n", prefix, length, hex_buf);
	else
		morse_dbg_usb(mors, "%s\n", hex_buf);

	kfree(hex_buf);
}

static void morse_usb_irq_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse, usb_irq_work);

	morse_usb_buff_log(mors, ((struct morse_usb *)mors->drv_priv)->endpoints[MORSE_EP_INT].buffer,
			MORSE_EP_INT_BUFFER_SIZE, "YAPS STAT: ");

	morse_hw_irq_handle(mors);
}

static void morse_usb_int_handler(struct urb *urb)
{
	int ret;
	struct morse *mors = urb->context;

	if (urb->status)
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			morse_err(mors,
				"%s - nonzero read status received: %d\n",
				__func__, urb->status);

	ret = usb_submit_urb(urb, GFP_ATOMIC);

	/* usb_kill_urb has been called */
	if (ret == -EPERM)
		return;
	else if (ret)
		morse_err(mors, "error: resubmit urb %p err code %d\n",
			  urb, ret);

	queue_work(mors->chip_wq, &mors->usb_irq_work);
}

static int morse_usb_enable_int(struct morse *mors)
{
	int ret = 0;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;
	struct urb *urb;


	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto out;
	}
	musb->endpoints[MORSE_EP_INT].urb = urb;

	musb->endpoints[MORSE_EP_INT].buffer = usb_alloc_coherent(musb->udev, MORSE_EP_INT_BUFFER_SIZE,
					  GFP_KERNEL, &urb->transfer_dma);
	if (!musb->endpoints[MORSE_EP_INT].buffer) {
		morse_err(mors,
			"couldn't allocate transfer_buffer\n");
		ret = -ENOMEM;
		goto error_set_urb_null;
	}

	usb_fill_int_urb(musb->endpoints[MORSE_EP_INT].urb, musb->udev,
			 usb_rcvintpipe(musb->udev, musb->endpoints[MORSE_EP_INT].addr),
			 musb->endpoints[MORSE_EP_INT].buffer, MORSE_EP_INT_BUFFER_SIZE,
			 morse_usb_int_handler, mors,
			 MORSE_USB_INTERRUPT_INTERVAL);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		morse_err(mors,
			 "Couldn't submit urb. Error number %d\n", ret);
		goto error;
	}

	return 0;

error:
	usb_free_coherent(musb->udev, MORSE_EP_INT_BUFFER_SIZE,
			  musb->endpoints[MORSE_EP_INT].buffer, urb->transfer_dma);
error_set_urb_null:
	musb->endpoints[MORSE_EP_INT].urb = NULL;
	usb_free_urb(urb);
out:
	return ret;
}

static void morse_usb_cmd_callback(struct urb *urb)
{
	struct morse *mors = urb->context;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	morse_dbg(mors, "%s status: %d\n", __func__, urb->status);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			morse_err(mors,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		musb->errors = urb->status;
	}
}

static int morse_usb_cmd(struct morse_usb *musb, const char *user_buffer, size_t writesize)
{

	int retval = 0;
	struct morse *mors = usb_get_intfdata(musb->interface);
	struct morse_usb_endpoint *ep = &musb->endpoints[MORSE_EP_CMD];

	memcpy(ep->buffer, user_buffer, writesize);

	/* initialize the urb properly */
	usb_fill_bulk_urb(ep->urb, musb->udev, usb_sndbulkpipe(musb->udev, ep->addr),
			  ep->buffer, writesize, morse_usb_cmd_callback, mors);
	ep->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send the data out the bulk port */
	retval = usb_submit_urb(ep->urb, GFP_KERNEL);
	if (retval) {
		morse_err(mors,
			"%s - failed submitting write urb, error %d\n",
			__func__, retval);

		goto error;
	}

	return writesize;

error:
	return retval;
}

static void morse_usb_mem_rw_callback(struct urb *urb)
{
	struct morse *mors = urb->context;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	morse_dbg(mors, "%s status: %d\n", __func__, urb->status);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			morse_err(mors,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
		musb->errors = urb->status;
	}

	musb->ongoing_rw = 0;
	wake_up(&musb->rw_in_wait);
}

static int morse_usb_mem_read(struct morse_usb *musb, u32 address, u8 *data, ssize_t size)
{
	int ret;
	struct morse_usb_command cmd;
	struct morse *mors = usb_get_intfdata(musb->interface);

	mutex_lock(&musb->lock);

	musb->ongoing_rw = 1;
	musb->errors = 0;

	/* Send command ahead to prepare for Tokens */
	cmd.dir = cpu_to_le32(MORSE_USB_READ);
	cmd.address = cpu_to_le32(address);
	cmd.length = cpu_to_le32(size);

	morse_usb_buff_log(mors, (const char *)&cmd, sizeof(cmd), "CMDBUF: ");

	ret = morse_usb_cmd(musb, (const char *)&cmd, sizeof(cmd));
	if (ret < 0) {
		morse_err(mors, "morse_usb_cmd error %d\n", ret);
		goto error;
	}

	/* Let's be fast push the next URB, don't wait until command is done */
	usb_fill_bulk_urb(musb->endpoints[MORSE_EP_MEM_RD].urb,
			musb->udev,
			usb_rcvbulkpipe(musb->udev,
				musb->endpoints[MORSE_EP_MEM_RD].addr),
			musb->endpoints[MORSE_EP_MEM_RD].buffer,
			size,
			morse_usb_mem_rw_callback,
			mors);

	/* do it */
	ret = usb_submit_urb(musb->endpoints[MORSE_EP_MEM_RD].urb, GFP_ATOMIC);
	if (ret < 0) {
		morse_err(mors,
			"%s - failed submitting read urb, error %d\n",
			__func__, ret);
		ret = (ret == -ENOMEM) ? ret : -EIO;
		goto error;
	}

	ret = wait_event_interruptible(musb->rw_in_wait, (!musb->ongoing_rw));
	if (ret < 0) {
		morse_err(mors, "%s: wait_event_interruptible: error %d\n", __func__, ret);
		goto error;
	}

	if (musb->errors) {
		ret = musb->errors;
		morse_err(mors, "%s error %d\n", __func__, ret);
		goto error;
	}

	memcpy(data, musb->endpoints[MORSE_EP_MEM_RD].buffer, size);

	morse_usb_buff_log(mors, (const char *)data, size, "RD-DATA: ");

	ret = size;

error:
	musb->ongoing_rw = 0;
	mutex_unlock(&musb->lock);

	return ret;
}

static int morse_usb_mem_write(struct morse_usb *musb, u32 address, u8 *data, ssize_t size)
{
	int ret;
	struct morse_usb_command cmd;
	struct morse *mors = usb_get_intfdata(musb->interface);

	mutex_lock(&musb->lock);

	musb->ongoing_rw = 1;
	musb->errors = 0;

	/* Send command ahead to prepare for Tokens */
	cmd.dir = cpu_to_le32(MORSE_USB_WRITE);
	cmd.address = cpu_to_le32(address);
	cmd.length = cpu_to_le32(size);
	ret = morse_usb_cmd(musb, (const char *)&cmd, sizeof(cmd));
	if (ret < 0) {
		morse_err(mors, "morse_usb_mem_read error %d\n", ret);
		goto error;
	}

	morse_usb_buff_log(mors, (const char *)data, size, "WR-DATA: ");

	memcpy(musb->endpoints[MORSE_EP_MEM_WR].buffer, data, size);

	/* prepare a read */
	usb_fill_bulk_urb(musb->endpoints[MORSE_EP_MEM_WR].urb,
			musb->udev,
			usb_sndbulkpipe(musb->udev,
				musb->endpoints[MORSE_EP_MEM_WR].addr),
			musb->endpoints[MORSE_EP_MEM_WR].buffer,
			size,
			morse_usb_mem_rw_callback,
			mors);

	/* do it */
	ret = usb_submit_urb(musb->endpoints[MORSE_EP_MEM_WR].urb, GFP_ATOMIC);
	if (ret < 0) {
		morse_err(mors,
			"%s - failed submitting write urb, error %d\n",
			__func__, ret);
		ret = (ret == -ENOMEM) ? ret : -EIO;
		goto error;
	}

	ret = wait_event_interruptible(musb->rw_in_wait, (!musb->ongoing_rw));
	if (ret < 0) {
		morse_err(mors, "%s error %d\n", __func__, ret);
		goto error;
	}

	if (musb->errors) {
		ret = musb->errors;
		morse_err(mors, "%s error %d\n", __func__, ret);
		goto error;
	}

	ret = size;

error:
	musb->ongoing_rw = 0;
	mutex_unlock(&musb->lock);
	return ret;
}

static int morse_usb_dm_write(struct morse *mors, u32 address,
					const u8 *data, u32 len)
{
	ssize_t offset = 0, ret;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	while (offset < len) {
		ret = morse_usb_mem_write(musb, address + offset,
					   (u8 *)(data + offset),
					   min((ssize_t)(len - offset), (ssize_t)USB_MAX_TRANSFER_SIZE)); /* cast to ssize_t on both sides so the build on x64 doesn't complain. */
		if (ret < 0) {
			morse_err(mors, "%s failed (errno=%zd)\n", __func__, ret);
			return -EIO;
		}
		offset += ret;
	}

	return 0;
}

static int morse_usb_dm_read(struct morse *mors, u32 address,
						  u8 *data, u32 len)
{
	ssize_t offset = 0, ret;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	while (offset < len) {
		ret = morse_usb_mem_read(musb, address + offset,
					  (u8 *)(data + offset),
					  min((ssize_t)(len - offset), (ssize_t)USB_MAX_TRANSFER_SIZE)); /* cast to ssize_t on both sides so the build on x64 doesn't complain. */

		if (ret < 0) {
			morse_err(mors, "%s failed (errno=%zd)\n", __func__, ret);
			return -EIO;
		}
		offset += ret;
	}

	return 0;
}

static int morse_usb_reg32_read(struct morse *mors, u32 address, u32 *val)
{
	ssize_t ret = 0;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	ret = morse_usb_mem_read(musb, address, (u8 *)val, sizeof(*val));
	if (ret == sizeof(*val)) {
		*val = le32_to_cpup((__le32 *)val);
		return 0;
	}

	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

static int morse_usb_reg32_write(struct morse *mors, u32 address, u32 val)
{
	ssize_t ret = 0;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;

	val = cpu_to_le32(val);
	ret = morse_usb_mem_write(musb, address, (u8 *)&val, sizeof(val));
	if (ret == sizeof(val))
		return 0;

	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

void morse_usb_claim_bus(struct morse *mors)
{
	struct morse_usb *musb;

	musb = (struct morse_usb *)mors->drv_priv;
	mutex_lock(&musb->bus_lock);
}

void morse_usb_release_bus(struct morse *mors)
{
	struct morse_usb *musb;

	musb = (struct morse_usb *)mors->drv_priv;
	mutex_unlock(&musb->bus_lock);
}

static int morse_usb_reset_bus(struct morse *mors)
{
	return 0;
}

static void morse_usb_bus_enable(struct morse *mors, bool enable)
{
	/* PS not currently supported with USB */
}

void morse_usb_set_irq(struct morse *mors, bool enable)
{
}

static const struct morse_bus_ops morse_usb_ops = {
	.dm_read = morse_usb_dm_read,
	.dm_write = morse_usb_dm_write,
	.reg32_read = morse_usb_reg32_read,
	.reg32_write = morse_usb_reg32_write,
	.set_bus_enable = morse_usb_bus_enable,
	.claim = morse_usb_claim_bus,
	.release = morse_usb_release_bus,
	.reset = morse_usb_reset_bus,
	.set_irq = morse_usb_set_irq,
};

static int morse_detect_endpoints(struct morse *mors,
			const struct usb_interface *intf)
{
	int ret;
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *intf_desc = intf->cur_altsetting;
	unsigned int i;

	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc)) {
			/* Assuming all the Endpoints are the same size, Pick Memory first */
			if (!musb->endpoints[MORSE_EP_MEM_RD].addr) {
				musb->endpoints[MORSE_EP_MEM_RD].addr = usb_endpoint_num(ep_desc);
				musb->endpoints[MORSE_EP_MEM_RD].size = usb_endpoint_maxp(ep_desc);
			} else if (!musb->endpoints[MORSE_EP_REG_RD].addr) {
				musb->endpoints[MORSE_EP_REG_RD].addr = usb_endpoint_num(ep_desc);
				musb->endpoints[MORSE_EP_REG_RD].size = usb_endpoint_maxp(ep_desc);
			}
		} else if (usb_endpoint_is_bulk_out(ep_desc)) {
			/* Assuming all the Endpoints are the same size, Pick Memory first */
			if (!musb->endpoints[MORSE_EP_MEM_WR].addr) {
				musb->endpoints[MORSE_EP_MEM_WR].addr = usb_endpoint_num(ep_desc);
				musb->endpoints[MORSE_EP_MEM_WR].size = usb_endpoint_maxp(ep_desc);
			} else if (!musb->endpoints[MORSE_EP_REG_WR].addr) {
				musb->endpoints[MORSE_EP_REG_WR].addr = usb_endpoint_num(ep_desc);
				musb->endpoints[MORSE_EP_REG_WR].size = usb_endpoint_maxp(ep_desc);
			}
		} else if (usb_endpoint_is_int_in(ep_desc)) {
			musb->endpoints[MORSE_EP_INT].addr = usb_endpoint_num(ep_desc);
			musb->endpoints[MORSE_EP_INT].size = usb_endpoint_maxp(ep_desc);
		}
	}

	morse_info(mors, "\n"
		"Memory Endpoint IN %s detected: %d size %d\n"
		"Memory Endpoint OUT %s detected: %d size %d\n"
		"Register Endpoint IN %s detected: %d\n"
		"Register Endpoint OUT %s detected: %d\n"
		"Stats IN endpoint %s detected: %d\n",
		musb->endpoints[MORSE_EP_MEM_RD].addr ? "" : "not", musb->endpoints[MORSE_EP_MEM_RD].addr, musb->endpoints[MORSE_EP_MEM_RD].size,
		musb->endpoints[MORSE_EP_MEM_WR].addr ? "" : "not", musb->endpoints[MORSE_EP_MEM_WR].addr, musb->endpoints[MORSE_EP_MEM_WR].size,
		musb->endpoints[MORSE_EP_REG_RD].addr ? "" : "not", musb->endpoints[MORSE_EP_REG_RD].addr,
		musb->endpoints[MORSE_EP_REG_WR].addr ? "" : "not", musb->endpoints[MORSE_EP_REG_WR].addr,
		musb->endpoints[MORSE_EP_INT].addr ? "" : "not", musb->endpoints[MORSE_EP_INT].addr);

	/* Verify we have an IN and OUT */
	if (!(musb->endpoints[MORSE_EP_MEM_RD].addr && musb->endpoints[MORSE_EP_MEM_WR].addr))
		return -ENODEV;

	/* Verify the stats MORSE_EP_INT is detected */
	if (!musb->endpoints[MORSE_EP_INT].addr)
		return -ENODEV;

	/* Verify minimum iterrupt status read */
	if (musb->endpoints[MORSE_EP_INT].size < 8)
		return -ENODEV;

	musb->endpoints[MORSE_EP_CMD].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MORSE_EP_CMD].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MORSE_EP_MEM_RD].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MORSE_EP_MEM_RD].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MORSE_EP_MEM_WR].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MORSE_EP_MEM_WR].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MORSE_EP_MEM_RD].buffer = kmalloc(USB_MAX_TRANSFER_SIZE, GFP_KERNEL);
	if (!musb->endpoints[MORSE_EP_MEM_RD].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MORSE_EP_MEM_WR].buffer = kmalloc(USB_MAX_TRANSFER_SIZE, GFP_KERNEL);
	if (!musb->endpoints[MORSE_EP_MEM_RD].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MORSE_EP_CMD].buffer = usb_alloc_coherent(musb->udev, sizeof(struct morse_usb_command), GFP_KERNEL,
				 &musb->endpoints[MORSE_EP_CMD].urb->transfer_dma);

	if (!musb->endpoints[MORSE_EP_CMD].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	/* Assign command to memory out end point */
	musb->endpoints[MORSE_EP_CMD].addr = musb->endpoints[MORSE_EP_MEM_WR].addr;
	musb->endpoints[MORSE_EP_CMD].size = musb->endpoints[MORSE_EP_MEM_WR].size;

	return 0;

err_ep:
	usb_free_coherent(musb->udev, sizeof(struct morse_usb_command),
				 musb->endpoints[MORSE_EP_CMD].buffer, musb->endpoints[MORSE_EP_CMD].urb->transfer_dma);
	usb_free_urb(musb->endpoints[MORSE_EP_MEM_RD].urb);
	usb_free_urb(musb->endpoints[MORSE_EP_CMD].urb);
	usb_free_urb(musb->endpoints[MORSE_EP_MEM_WR].urb);
	kfree(musb->endpoints[MORSE_EP_MEM_RD].buffer);
	kfree(musb->endpoints[MORSE_EP_MEM_WR].buffer);

	return ret;
}

static int morse_usb_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	int ret;
	u32 chip_id = 0;
	struct morse *mors;
	struct morse_usb *musb;
	bool dl_fw = true, chk_fw = true;

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB Morse device now attached to Morse driver (minor=%d)",
		 interface->minor);

	mors = morse_mac_create(sizeof(*musb), &interface->dev);
	if (!mors) {
		dev_err(&interface->dev, "morse_mac_create failed\n");
		return -ENOMEM;
	}

	mors->cfg = (struct morse_hw_cfg *)id->driver_info;
	mors->bus_ops = &morse_usb_ops;

	musb = (struct morse_usb *)mors->drv_priv;
	musb->udev = usb_get_dev(interface_to_usbdev(interface));
	musb->interface = usb_get_intf(interface);

	/* save our data pointer in this interface device */
	mutex_init(&musb->lock);
	mutex_init(&musb->bus_lock);
	init_waitqueue_head(&musb->rw_in_wait);
	usb_set_intfdata(interface, mors);

	ret = morse_detect_endpoints(mors, interface);
	if (!mors) {
		morse_err(mors, "morse_detect_endpoints failed (%d)\n", ret);
		goto err_ep;
	}

	ret = morse_usb_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	if (ret < 0) {
		morse_err(mors, "Read CHIP ID failed (%d)\n", ret);
		goto err_ep;
	}
	morse_info(mors, "CHIP ID 0x%08x:0x%04x\n", MORSE_REG_CHIP_ID(mors), chip_id);
	mors->chip_id = chip_id;

#ifdef CONFIG_MORSE_ENABLE_TEST_MODES
	if (test_mode == MORSE_CONFIG_TEST_MODE_BUS) {
		morse_bus_test(mors, "USB");
		goto usb_test_fin;
	}
#endif

	mors->board_serial = serial;
	morse_info(mors, "Board serial: %s", mors->board_serial);
	if (test_mode != MORSE_CONFIG_TEST_MODE_DISABLED)
		chk_fw = false;
	if (test_mode > MORSE_CONFIG_TEST_MODE_DOWNLOAD)
		dl_fw = false;
	ret = morse_firmware_init(mors, mors->cfg->fw_name,
				dl_fw, chk_fw);
	if (ret) {
		morse_err(mors, "morse_firmware_init failed: %d\n", ret);
		goto err_ep;
	}

	morse_info(mors, "Firmware initialized : %s\n", mors->cfg->fw_name);

	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		mors->chip_wq = create_singlethread_workqueue("MorseChipIfWorkQ");
		if (!mors->chip_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseChipIfWorkQ) failed\n");
			ret = -ENOMEM;
			goto err_ep;
		}

		mors->net_wq = create_singlethread_workqueue("MorseNetWorkQ");

		if (!mors->net_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseNetWorkQ) failed\n");
			ret = -ENOMEM;
			goto err_net_wq;
		}

		mors->command_wq =
			create_singlethread_workqueue("MorseCommandQ");

		if (!mors->command_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseCommandQ) failed\n");
			ret = -ENOMEM;
			goto err_command_wq;
		}

		ret = mors->cfg->ops->init(mors);
		if (ret) {
			morse_err(mors, "chip_if_init failed: %d\n", ret);
			goto err_buffs;
		}

		ret = morse_mac_register(mors);
		if (ret) {
			morse_err(mors, "morse_mac_register failed: %d\n", ret);
			goto err_mac;
		}
	}

#ifdef CONFIG_MORSE_USER_ACCESS
	morse_usb_uaccess = uaccess_alloc();
	if (IS_ERR(morse_usb_uaccess)) {
		morse_pr_err("uaccess_alloc() failed\n");
		return PTR_ERR(morse_usb_uaccess);
	}

	ret = uaccess_init(morse_usb_uaccess);
	if (ret) {
		morse_pr_err("uaccess_init() failed\n");
		goto err_uaccess;
	}

	if (uaccess_device_register(mors, morse_usb_uaccess, &musb->udev->dev)) {
		morse_err(mors, "uaccess_device_init() failed.\n");
		goto err_uaccess;
	}
#endif

	morse_usb_enable_int(mors);
	INIT_WORK(&mors->usb_irq_work, morse_usb_irq_work);

#ifdef CONFIG_MORSE_ENABLE_TEST_MODES
usb_test_fin:
#endif
	return 0;

#ifdef CONFIG_MORSE_USER_ACCESS
err_uaccess:
	uaccess_cleanup(morse_usb_uaccess);
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED)
		morse_mac_unregister(mors);
#endif
err_mac:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED)
		mors->cfg->ops->finish(mors);
err_buffs:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->command_wq);
		destroy_workqueue(mors->command_wq);
	}
err_command_wq:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->net_wq);
		destroy_workqueue(mors->net_wq);
	}
err_net_wq:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->chip_wq);
		destroy_workqueue(mors->chip_wq);
	}
err_ep:
	morse_mac_destroy(mors);
	return ret;
}

static void morse_urb_cleanup(struct morse *mors)
{
	struct morse_usb *musb = (struct morse_usb *)mors->drv_priv;
	struct morse_usb_endpoint *int_ep = &musb->endpoints[MORSE_EP_INT];
	struct morse_usb_endpoint *rd_ep = &musb->endpoints[MORSE_EP_MEM_RD];
	struct morse_usb_endpoint *wr_ep = &musb->endpoints[MORSE_EP_MEM_WR];
	struct morse_usb_endpoint *cmd_ep = &musb->endpoints[MORSE_EP_CMD];

	usb_kill_urb(int_ep->urb);

	usb_kill_urb(rd_ep->urb);

	usb_kill_urb(wr_ep->urb);

	usb_kill_urb(cmd_ep->urb);

	/* Locking the bus. No USB communication after this point */
	mutex_lock(&musb->lock);

	usb_free_urb(int_ep->urb);

	if (int_ep->urb)
		usb_free_coherent(musb->udev, MORSE_EP_INT_BUFFER_SIZE,
				int_ep->buffer,
				int_ep->urb->transfer_dma);

	usb_free_urb(rd_ep->urb);
	kfree(rd_ep->buffer);

	usb_free_urb(wr_ep->urb);
	kfree(wr_ep->buffer);

	usb_free_urb(cmd_ep->urb);

	if (cmd_ep->urb)
		usb_free_coherent(musb->udev, sizeof(struct morse_usb_command),
				cmd_ep->buffer,
				cmd_ep->urb->transfer_dma);
}

static void morse_usb_disconnect(struct usb_interface *interface)
{
	struct morse *mors = usb_get_intfdata(interface);
	int minor = interface->minor;

#ifdef CONFIG_MORSE_USER_ACCESS
		uaccess_device_unregister(mors);
		uaccess_cleanup(morse_usb_uaccess);
#endif

	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		morse_mac_unregister(mors);
		flush_workqueue(mors->chip_wq);
		destroy_workqueue(mors->chip_wq);
		flush_workqueue(mors->command_wq);
		destroy_workqueue(mors->command_wq);
		flush_workqueue(mors->net_wq);
		destroy_workqueue(mors->net_wq);
		mors->cfg->ops->finish(mors);
	}

	/* No USB communication after this point */
	morse_urb_cleanup(mors);

	if (mors)
		morse_mac_destroy(mors);

	usb_set_intfdata(interface, NULL);
	dev_info(&interface->dev, "USB Morse #%d now disconnected", minor);
	usb_put_dev(interface_to_usbdev(interface));
}

static int morse_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int morse_usb_resume(struct usb_interface *intf)
{
	return 0;
}

static int morse_usb_pre_reset(struct usb_interface *intf)
{
	return 0;
}

static int morse_usb_post_reset(struct usb_interface *intf)
{
	return 0;
}

static struct usb_driver morse_usb_driver = {
	.name =		"morse_usb",
	.probe =	morse_usb_probe,
	.disconnect =	morse_usb_disconnect,
	.suspend =	morse_usb_suspend,
	.resume =	morse_usb_resume,
	.pre_reset =	morse_usb_pre_reset,
	.post_reset =	morse_usb_post_reset,
	.id_table =	morse_usb_table,
	.supports_autosuspend = 1,
	.soft_unbind = 1,
};

int __init morse_usb_init(void)
{
	int ret;

	ret = usb_register(&morse_usb_driver);
	if (ret)
		morse_pr_err("usb_register_driver() failed: %d\n", ret);
	return ret;
}

void __exit morse_usb_exit(void)
{
	usb_deregister(&morse_usb_driver);
}
