/* cdc-wdm.c

This driver supports USB CDC WCM Device Management.

Copyright (c) 2007 Oliver Neukum
Copyright (c) 2007 Takeyoshi Kikuchi

Some code taken from cdc-acm.c
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <asm/byteorder.h>
#include <asm/bitops.h>
#include <asm/unaligned.h>

#include "cdc-wdm_tty.h"
//#include "cdc-acm.h" /* for request types */

//#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(fmt, ## args)
#else
#define DPRINTK(fmt, args...) do {} while (0)
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.02"
#define DRIVER_AUTHOR "Oliver Neukum / Takeyoshi Kikuchi"
#define DRIVER_DESC "USB driver for USB WCM Device Management"

static struct usb_driver wdm_driver;
static struct tty_driver *wdm_tty_driver;
static struct wdm_device *wdm_table[WDM_TTY_MINORS];

static DEFINE_MUTEX(wdm_mutex);

#define WDM_READY(wdm)	(wdm && wdm->dev && wdm->used)

static int wdm_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void wdm_disconnect(struct usb_interface *intf);

static void kill_urbs(struct wdm_device *wdm);

/*
 * Write buffer management.
 * All of these assume proper locks taken by the caller.
 */

static int wdm_wb_alloc(struct wdm_device *wdm)
{
	int i, wbn;
	struct wdm_wb *wb;

	wbn = wdm->write_current;
	i = 0;

	for (;;) {
		wb = &wdm->wb[wbn];
		if (!wb->use) {
			wb->use = 1;
			return wbn;
		}
		wbn = (wbn + 1) % WDM_NW;
		if (++i >= WDM_NW)
			return -1;
	}
}

static void wdm_wb_free(struct wdm_device *wdm, int wbn)
{
	wdm->wb[wbn].use = 0;
}

static int wdm_wb_is_avail(struct wdm_device *wdm)
{
	int i, n;

	n = WDM_NW;

	for (i = 0; i < WDM_NW; i++) {
//              DPRINTK("%s : wdm->wb[%d].use = %d\n", __FUNCTION__, i,
//                      wdm->wb[i].use);

		n -= wdm->wb[i].use;
	}

//      DPRINTK("%s : result is %d\n", __FUNCTION__, n);

	return n;
}

static inline int wdm_wb_is_used(struct wdm_device *wdm, int wbn)
{
        int inuse;

	inuse = wdm->wb[wbn].use;
        DPRINTK("%s : wdm->wb[%d].use = %d\n", __FUNCTION__, wbn, inuse);

        return inuse;
}

/*
 * Finish write.
 */
static void wdm_write_done(struct wdm_device *wdm)
{
	unsigned long flags;
	int wbn;

	spin_lock_irqsave(&wdm->write_lock, flags);
        wdm->write_ready = 1;
	wbn = wdm->write_current;
	wdm_wb_free(wdm, wbn);
	wdm->write_current = (wbn + 1) % WDM_NW;
	spin_unlock_irqrestore(&wdm->write_lock, flags);
}

/*
 * Poke write.
 */
static int wdm_write_start(struct wdm_device *wdm)
{
        struct urb *command;
	struct usb_ctrlrequest *req;
        struct wdm_wb *wb;
        int wbn;
	unsigned long flags;
	int rc;

        DPRINTK("%s : start...\n", __FUNCTION__);

	spin_lock_irqsave(&wdm->write_lock, flags);

	if (!wdm->dev) {
                DPRINTK("%s : !(wdm->dev)\n", __FUNCTION__);
		spin_unlock_irqrestore(&wdm->write_lock, flags);
		return -ENODEV;
	}

	if (!wdm->write_ready) {
                DPRINTK("%s : !(wdm->write_ready)\n", __FUNCTION__);
		spin_unlock_irqrestore(&wdm->write_lock, flags);
		return 0;	/* A white lie */
	}

	wbn = wdm->write_current;
        DPRINTK("%s : wbn = %d\n", __FUNCTION__, wbn);

	if (!wdm_wb_is_used(wdm, wbn)) {
                DPRINTK("%s : !wdm_wb_is_used()\n", __FUNCTION__);
		spin_unlock_irqrestore(&wdm->write_lock, flags);
		return 0;
	}

	wb = &wdm->wb[wbn];

	wdm->write_ready = 0;
	spin_unlock_irqrestore(&wdm->write_lock, flags);

        command = wdm->command;
	req = wdm->out_req;

	command->transfer_buffer        = wb->buf;
	command->transfer_dma           = wb->dmah;
	command->transfer_buffer_length = wb->len;
	command->dev                    = wdm->dev;

        req->wLength = cpu_to_le16(wb->len);

	if ((rc = usb_submit_urb(command, GFP_ATOMIC)) < 0) {
		err("%s : usb_submit_urb(write encapsulated) failed: %d",
                    __FUNCTION__, rc);
		wdm_write_done(wdm);
	}

        DPRINTK("%s : usb_submit_urb() results %d\n", __FUNCTION__, rc);

	return rc;
}

/*
 * Interrupt handlers for various WHM device responses
 */

/* control interface reports status changes with "interrupt" transfers */
static void wdm_ctrl_irq(struct urb *urb)
{
	struct wdm_device *wdm = urb->context;
	struct usb_ctrlrequest *req;
	struct usb_cdc_notification *dr = urb->transfer_buffer;
	int status;

	req = wdm->in_req;

        DPRINTK("%s : start, urb->status = %d\n",
                __FUNCTION__, urb->status);

	if (urb->status) {
		switch(urb->status) {
		case -ESHUTDOWN:
		case -ENOENT:
		case -ECONNRESET:
			return; /* unplug */
		case -EPIPE:
			err("Stall on int endpoint");
			break;
                        //goto sw; /* halt is cleared in work */
		default:
			err("nonzero urb status received: %d", urb->status);
			break;
		}
	}

        DPRINTK("%s : urb->actual_length = %d\n", __FUNCTION__,
                urb->actual_length);

	if (urb->actual_length < sizeof(struct usb_cdc_notification)) {
		err("%s: wdm_int_callback - %d bytes",
                    __FUNCTION__, urb->actual_length);

                goto exit;
	}
	
	switch (dr->bNotificationType) {
	case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:
//		info("%s : NOTIFY_RESPONSE_AVAILABLE received: index %d len %d",
//                   __FUNCTION__, dr->wIndex, dr->wLength);
		break;
	default:
		err("%s : unknown notification %d received: index %d len %d",
                    __FUNCTION__, dr->bNotificationType, dr->wIndex, dr->wLength);
                goto exit;
	}

//	req->bRequestType = USB_RT_ACM | USB_DIR_IN;
//	req->bRequest     = USB_CDC_GET_ENCAPSULATED_RESPONSE;
//	req->wValue       = 0;
//	req->wIndex       = cpu_to_le16(wdm->ifnum);
//	req->wLength      = cpu_to_le16(wdm->wMaxCommand);

        DPRINTK("%s : usb_submit_urb(wdm->response, GFP_ATOMIC)\n", __FUNCTION__);
        usb_submit_urb(wdm->response, GFP_ATOMIC);
        return;
exit:
        // Restart INT transfer
        DPRINTK("%s : re-submitting Notification URB.\n", __FUNCTION__);
	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, status);
}

static void wdm_read_callback(struct urb *urb)
{
	struct wdm_device *wdm;
	struct tty_struct *tty;

        if (unlikely(urb->status != 0))
                    info("%s : status: %d", __FUNCTION__, urb->status);

	wdm = (struct wdm_device *) urb->context;
        tty = wdm->tty;

//      info("%s : urb->actual_length = %d\n", __FUNCTION__, urb->actual_length);

        if (urb->actual_length > 0) {
                tty_buffer_request_room(tty, urb->actual_length);

                if (!wdm->throttle)
                        tty_insert_flip_string(tty, urb->transfer_buffer, urb->actual_length);
                tty_flip_buffer_push(tty);
        }

        // Restart INT transfer
	if (usb_submit_urb(wdm->notify, GFP_KERNEL))
		dbg("usb_submit_urb(interrupt) failed");
}

/* data interface wrote those outgoing bytes */
static void wdm_write_callback(struct urb *urb)
{
	struct wdm_device *wdm = (struct wdm_device *) urb->context;

	DPRINTK("%s : urb->status is %d\n", __FUNCTION__, urb->status);

	wdm_write_done(wdm);
	wdm_write_start(wdm);

	if (WDM_READY(wdm))
		schedule_work(&wdm->work);
}

static void wdm_softint(struct work_struct *work)
{
	struct wdm_device *wdm = container_of(work, struct wdm_device, work);

	dbg("Entering wdm_softint.");
	
	if (!WDM_READY(wdm))
		return;

	tty_wakeup(wdm->tty);
}

/*
 * TTY handlers
 */

static int wdm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct wdm_device *wdm;
	int rv = -EINVAL;

//	DPRINTK("%s : start...\n", __FUNCTION__);

	mutex_lock(&wdm_mutex);

	wdm = wdm_table[tty->index];

	if (!wdm || !wdm->dev)
		goto err_out;
	else
		rv = 0;

	tty->driver_data = wdm;
	wdm->tty = tty;

	/* force low_latency on so that our tty_push actually forces the data through,
	   otherwise it is scheduled, and with high data rates data can get lost. */
	tty->low_latency = 1;

	if (wdm->used) {
		goto done;
        }
	wdm->used++;

//	wdm->notify->dev = wdm->dev;
	if (usb_submit_urb(wdm->notify, GFP_KERNEL)) {
		err("usb_submit_urb(interrupt) failed");
		goto bail_out;
	}

        DPRINTK("%s : usb_submit_urb(wdm->notify) succeeded.\n", __FUNCTION__);
done:
err_out:
	mutex_unlock(&wdm_mutex);
	return rv;

bail_out:
	wdm->used--;
	mutex_unlock(&wdm_mutex);
	return -EIO;
}

static void wdm_tty_unregister(struct wdm_device *wdm)
{
//	DPRINTK("%s : start...\n", __FUNCTION__);

	tty_unregister_device(wdm_tty_driver, wdm->minor);
	usb_put_intf(wdm->intf);
	wdm_table[wdm->minor] = NULL;
	usb_free_urb(wdm->notify);
	usb_free_urb(wdm->response);
	usb_free_urb(wdm->command);
	kfree(wdm);
}

static void wdm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct wdm_device *wdm = tty->driver_data;

//	DPRINTK("%s : start...\n", __FUNCTION__);

	if (!wdm || !wdm->used)
		return;

	mutex_lock(&wdm_mutex);

	if (!--wdm->used) {
		if (wdm->dev)
                        kill_urbs(wdm);
                else
			wdm_tty_unregister(wdm);
	}

	mutex_unlock(&wdm_mutex);
}

static int wdm_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct wdm_device *wdm = tty->driver_data;
	struct wdm_wb *wb;
	int stat;
	unsigned long flags;
        int wbn;

	DPRINTK("%s : write %d bytes\n", __FUNCTION__, count);

	if (!WDM_READY(wdm))
		return -EINVAL;
	if (!count)
		return 0;

	spin_lock_irqsave(&wdm->write_lock, flags);
	if ((wbn = wdm_wb_alloc(wdm)) < 0) {
		spin_unlock_irqrestore(&wdm->write_lock, flags);
		wdm_write_start(wdm);
		return 0;
	}
	wb = &wdm->wb[wbn];

	count = (count > wdm->wMaxCommand) ? wdm->wMaxCommand : count;
	DPRINTK("%s : Get %d bytes...\n", __FUNCTION__, count);
	memcpy(wb->buf, buf, count);
	wb->len = count;
	spin_unlock_irqrestore(&wdm->write_lock, flags);

	if ((stat = wdm_write_start(wdm)) < 0)
		return stat;
	return count;
}

static int wdm_tty_write_room(struct tty_struct *tty)
{
	struct wdm_device *wdm = tty->driver_data;
        int result;

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return -EINVAL;
        }

	/*
	 * Do not let the line discipline to know that we have a reserve,
	 * or it might get too enthusiastic.
	 */

	result = (wdm->write_ready && wdm_wb_is_avail(wdm)) ? wdm->wMaxCommand : 0;

        return result;
}

static int wdm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct wdm_device *wdm = tty->driver_data;

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return -EINVAL;
        }

	/*
	 * This is inaccurate (overcounts), but it works.
	 */
	return (WDM_NW - wdm_wb_is_avail(wdm)) * wdm->wMaxCommand;
}

static void wdm_tty_throttle(struct tty_struct *tty)
{
	struct wdm_device *wdm = tty->driver_data;

        DPRINTK("%s : start...\n", __FUNCTION__);

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return;
        }

	spin_lock_bh(&wdm->throttle_lock);
	wdm->throttle = 1;
	spin_unlock_bh(&wdm->throttle_lock);
}

static void wdm_tty_unthrottle(struct tty_struct *tty)
{
	struct wdm_device *wdm = tty->driver_data;

        DPRINTK("%s : start...\n", __FUNCTION__);

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return;
        }

	spin_lock_bh(&wdm->throttle_lock);
	wdm->throttle = 0;
	spin_unlock_bh(&wdm->throttle_lock);
}

#if 0
static void wdm_tty_break_ctl(struct tty_struct *tty, int state)
{
	struct acm *wdm = tty->driver_data;

	if (!WDM_READY(wdm))
		return;
}
#endif

static int wdm_tty_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct wdm_device *wdm = tty->driver_data;

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return -EINVAL;
        }

	return (wdm->ctrlout & ACM_CTRL_DTR ? TIOCM_DTR : 0) |
	       (wdm->ctrlout & ACM_CTRL_RTS ? TIOCM_RTS : 0) |
	       (wdm->ctrlin  & ACM_CTRL_DSR ? TIOCM_DSR : 0) |
	       (wdm->ctrlin  & ACM_CTRL_RI  ? TIOCM_RI  : 0) |
	       (wdm->ctrlin  & ACM_CTRL_DCD ? TIOCM_CD  : 0) |
	       TIOCM_CTS;
}

static int wdm_tty_tiocmset(struct tty_struct *tty, struct file *file,
			    unsigned int set, unsigned int clear)
{
	struct wdm_device *wdm = tty->driver_data;
	unsigned int newctrl;

        DPRINTK("%s : start...\n", __FUNCTION__);

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return -EINVAL;
        }

	newctrl = wdm->ctrlout;
	set = (set & TIOCM_DTR ? ACM_CTRL_DTR : 0) | (set & TIOCM_RTS ? ACM_CTRL_RTS : 0);
	clear = (clear & TIOCM_DTR ? ACM_CTRL_DTR : 0) | (clear & TIOCM_RTS ? ACM_CTRL_RTS : 0);

	newctrl = (newctrl & ~clear) | set;

	wdm->ctrlout = newctrl;
        return 0;
}

static int wdm_tty_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wdm_device *wdm = tty->driver_data;

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return -EINVAL;
        }

	return -ENOIOCTLCMD;
}

static const __u32 wdm_tty_speed[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600,
	1200, 1800, 2400, 4800, 9600, 19200, 38400,
	57600, 115200, 230400, 460800, 500000, 576000,
	921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
};

static const __u8 wdm_tty_size[] = {
	5, 6, 7, 8
};

static void wdm_tty_set_termios(struct tty_struct *tty, struct ktermios *termios_old)
{
	struct wdm_device *wdm = tty->driver_data;
	struct ktermios *termios = tty->termios;
	struct usb_cdc_line_coding newline;
	int newctrl = wdm->ctrlout;

	if (!WDM_READY(wdm)) {
                DPRINTK("%s : not ready\n", __FUNCTION__);
		return;
        }

	newline.dwDTERate = cpu_to_le32p(wdm_tty_speed
                                         + (termios->c_cflag & CBAUD & ~CBAUDEX)
                                         + (termios->c_cflag & CBAUDEX ? 15 : 0));
	newline.bCharFormat = termios->c_cflag & CSTOPB ? 2 : 0;
	newline.bParityType = termios->c_cflag & PARENB ?
		(termios->c_cflag & PARODD ? 1 : 2) + (termios->c_cflag & CMSPAR ? 2 : 0) : 0;
	newline.bDataBits = wdm_tty_size[(termios->c_cflag & CSIZE) >> 4];

	wdm->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (!newline.dwDTERate) {
		newline.dwDTERate = wdm->line.dwDTERate;
		newctrl &= ~ACM_CTRL_DTR;
	} else 
                newctrl |=  ACM_CTRL_DTR;

        wdm->ctrlout = newctrl;

	if (memcmp(&wdm->line, &newline, sizeof(newline))) {
		memcpy(&wdm->line, &newline, sizeof(newline));
		DPRINTK("%s : set line: %d %d %d %d", __FUNCTION__,
                       le32_to_cpu(newline.dwDTERate),
                       newline.bCharFormat, newline.bParityType,
                       newline.bDataBits);
	}
}

/*
 * USB probe and disconnect routines.
 */

/* Little helper: write buffers free */
static void wdm_write_buffers_free(struct wdm_device *wdm)
{
	int i;
	struct wdm_wb *wb;

	for (wb = &wdm->wb[0], i = 0; i < WDM_NW; i++, wb++)
		usb_free_coherent(wdm->dev, wdm->wMaxCommand, wb->buf, wb->dmah);
}

/* Little helper: write buffers allocate */
static int wdm_write_buffers_alloc(struct wdm_device *wdm)
{
	int i;
	struct wdm_wb *wb;

	for (wb = &wdm->wb[0], i = 0; i < WDM_NW; i++, wb++) {
		wb->buf = usb_alloc_coherent(wdm->dev, wdm->wMaxCommand, GFP_KERNEL,
					     &wb->dmah);
		if (!wb->buf) {
			while (i != 0) {
				--i;
				--wb;
				usb_free_coherent(wdm->dev, wdm->wMaxCommand,
						  wb->buf, wb->dmah);
			}
			return -ENOMEM;
		}
	}

	return 0;
}

static int wdm_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int rv = -EINVAL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct wdm_device *wdm;
	struct usb_host_interface *iface;
	struct usb_endpoint_descriptor *ep;
	struct usb_cdc_dmm_header *dmhd;
	struct usb_ctrlrequest *req;
	struct urb *urbs = NULL, *urbi = NULL, *urbo = NULL;
	u8 *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	int num_rx_buf;
        int minor;
	u16 maxcom = 0;

	if (!buffer)
		goto out;

        num_rx_buf = WDM_NR;

	while (buflen > 0) {
		if (buffer[1] != USB_DT_CS_INTERFACE) {
			err("skipping garbage");
			goto next_desc;
		}

		switch (buffer[2]) {
		case USB_CDC_HEADER_TYPE:
			break;
		case USB_CDC_DMM_TYPE:
			dmhd = (struct usb_cdc_dmm_header *) buffer;
			maxcom = le16_to_cpu(dmhd->wMaxCommand);
			info("Found maximum buffer length: %d", maxcom);	
		default:
			err("Ignoring extra header, type %d, length %d", buffer[2], buffer[0]);
			break;
		}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}
        for (minor = 0; minor < WDM_TTY_MINORS && wdm_table[minor]; minor++)
                ;

        if (minor == WDM_TTY_MINORS) {
                err("%s : no more free wdm devices", __FUNCTION__);
                return -ENODEV;
        }

	rv = -ENOMEM;
	wdm = kzalloc(sizeof(struct wdm_device), GFP_KERNEL);
	if (!wdm) {
                err("%s : kzalloc() failed.\n", __FUNCTION__);
		goto out;
	}
	spin_lock_init(&wdm->write_lock);
	spin_lock_init(&wdm->throttle_lock);
        wdm->write_ready = 1;

//	INIT_WORK(&wdm->work, wdm_softint, wdm);
	INIT_WORK(&wdm->work, wdm_softint);

	wdm->wMaxCommand = maxcom;
        wdm->dev = usb_dev;
	wdm->ifnum = cpu_to_le16((u16) intf->cur_altsetting->desc.bInterfaceNumber);
	wdm->intf = intf;
	//INIT_WORK(&wdm->rxwork, wdm_rxwork);
	//INIT_WORK(&wdm->txwork, wdm_txwork);

	iface = &intf->altsetting[0];
	ep = &iface->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(ep)) {
		rv = -EINVAL;
		goto err1;
	}

	if (!(wdm->out_req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL))) {
                err("%s : wdm->out_req kmalloc() failed.\n", __FUNCTION__);
		goto err2;
        }

	if (!(wdm->in_req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL))) {
                err("%s : wdm->in_req kmalloc() failed.\n", __FUNCTION__);
		goto err3;
        }

        /* allocate URB */
	if (!(urbs = usb_alloc_urb(0, GFP_KERNEL))) {
                err("%s : urbs usb_alloc_urb() failed.\n", __FUNCTION__);
		goto err4;
        }
	wdm->notify = urbs;

	if (!(urbi = usb_alloc_urb(0, GFP_KERNEL))) {
                err("%s : urbi usb_alloc_urb() failed.\n", __FUNCTION__);
		goto err5;
        }
	wdm->response = urbi;

	if (!(urbo = usb_alloc_urb(0, GFP_KERNEL))) {
                err("%s : urbo usb_alloc_urb() failed.\n", __FUNCTION__);
		goto err6;
        }
	wdm->command = urbo;

        /* allocate transfer_buffer */
	if (wdm_write_buffers_alloc(wdm) < 0) {
                err("%s : wdm_write_buffers_alloc() failed.\n", __FUNCTION__);
		goto err7;
	}

//	if (!(wdm->sbuf = usb_buffer_alloc(usb_dev, sizeof(struct usb_cdc_notification),
//                                         GFP_KERNEL, &wdm->shandle))) {
        if (!(wdm->sbuf = kmalloc(sizeof(struct usb_cdc_notification), GFP_KERNEL))) {
                err("%s : wdm->sbuf usb_buffer_alloc() failed.\n", __FUNCTION__);
		goto err8;
        }
        DPRINTK("%s : wdm->sbuf = %p\n", __FUNCTION__, wdm->sbuf);

//	if (!(wdm->inbuf = usb_buffer_alloc(usb_dev, maxcom, GFP_KERNEL, &wdm->ihandle))) {
        if (!(wdm->inbuf = kmalloc(maxcom, GFP_KERNEL))) {
                err("%s : wdm->inbuf usb_buffer_alloc() failed.\n", __FUNCTION__);
		goto err9;
        }
        DPRINTK("%s : wdm->inbuf = %p\n", __FUNCTION__, wdm->inbuf);

        /* Notification URB */
        DPRINTK("%s : Notification URB EndpointAddress = 0x%02x, Interval = %d\n",
                __FUNCTION__, ep->bEndpointAddress, ep->bInterval);
	usb_fill_int_urb(urbs, usb_dev, usb_rcvintpipe(usb_dev, ep->bEndpointAddress),
                         wdm->sbuf, sizeof(struct usb_cdc_notification), wdm_ctrl_irq,
                         wdm, ep->bInterval);
//	urbs->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

        /* Transmit URB */
	req = wdm->out_req;
	req->bRequestType = USB_RT_ACM | USB_DIR_OUT;
	req->bRequest     = USB_CDC_SEND_ENCAPSULATED_COMMAND;
	req->wValue       = 0;
	req->wIndex       = cpu_to_le16(wdm->ifnum);
	req->wLength      = 0;

	usb_fill_control_urb(wdm->command, usb_dev, usb_sndctrlpipe(usb_dev, 0), /* using common endpoint 0 */
                             (unsigned char *) req, NULL, 0, wdm_write_callback, wdm);
	wdm->command->transfer_flags |= URB_NO_FSBR | URB_NO_TRANSFER_DMA_MAP;

        /* Receive URB */
	req = wdm->in_req;
	req->bRequestType = USB_RT_ACM | USB_DIR_IN;
	req->bRequest     = USB_CDC_GET_ENCAPSULATED_RESPONSE;
	req->wValue       = 0;
	req->wIndex       = cpu_to_le16(wdm->ifnum);
	req->wLength      = cpu_to_le16(wdm->wMaxCommand);

	usb_fill_control_urb(wdm->response, usb_dev, usb_rcvctrlpipe(usb_dev, 0), /* using common endpoint 0 */
                             (unsigned char *) req, wdm->inbuf, wdm->wMaxCommand,
                             wdm_read_callback, wdm);
//	wdm->response->transfer_flags |= URB_NO_FSBR | URB_NO_TRANSFER_DMA_MAP;
	wdm->response->transfer_flags |= URB_NO_FSBR;

	usb_set_intfdata(intf, wdm);

	dev_info(&intf->dev, "ttyWDM%d: USB WDM device\n", minor);

//	wdm_set_control(wdm, wdm->intf);

	wdm->line.dwDTERate = cpu_to_le32(9600);
	wdm->line.bDataBits = 8;
//	wdm_set_line(wdm, &wdm->line);

	usb_driver_claim_interface(&wdm_driver, intf, wdm);

	usb_get_intf(intf);
	tty_register_device(wdm_tty_driver, minor, &intf->dev);

	wdm_table[minor] = wdm;
	usb_set_intfdata(intf, wdm);

        rv = 0;
out:
	return rv;

err9:
//	usb_buffer_free(interface_to_usbdev(wdm->intf), sizeof(struct usb_cdc_notification),
//                      wdm->sbuf, wdm->shandle);
        kfree(wdm->sbuf);
err8:
        wdm_write_buffers_free(wdm);
err7:
        usb_free_urb(wdm->command);
err6:
        usb_free_urb(wdm->response);
err5:
        usb_free_urb(wdm->notify);
err4:
	kfree(wdm->in_req);
err3:
	kfree(wdm->out_req);
err2:
err1:
	kfree(wdm);

	return rv;
}

static void kill_urbs(struct wdm_device *wdm)
{
	usb_kill_urb(wdm->command);
	usb_kill_urb(wdm->notify);
	usb_kill_urb(wdm->response);
}

static void wdm_disconnect(struct usb_interface *intf)
{
	struct wdm_device *wdm = usb_get_intfdata(intf);
//	struct usb_device *usb_dev = interface_to_usbdev(intf);

	if (!wdm || !wdm->dev) {
		DPRINTK("disconnect on nonexisting interface");
		return;
	}

	mutex_lock(&wdm_mutex);
	if (!usb_get_intfdata(intf)) {
		mutex_unlock(&wdm_mutex);
		return;
	}
	wdm->dev = NULL;
	usb_set_intfdata(wdm->intf, NULL);

        kill_urbs(wdm);

	flush_scheduled_work(); /* wait for acm_softint */

	wdm_write_buffers_free(wdm);
//	usb_buffer_free(usb_dev, sizeof(struct usb_cdc_notification), wdm->sbuf, wdm->shandle);
        kfree(wdm->sbuf);
//      usb_buffer_free(usb_dev, wdm->wMaxCommand, wdm->inbuf, wdm->ihandle);
        kfree(wdm->inbuf);

	usb_driver_release_interface(&wdm_driver, wdm->intf);

	if (!wdm->used) {
		wdm_tty_unregister(wdm);
		mutex_unlock(&wdm_mutex);
		return;
	}

	mutex_unlock(&wdm_mutex);

	if (wdm->tty)
		tty_hangup(wdm->tty);
}

static struct usb_device_id wdm_ids[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS,
		.bInterfaceClass = USB_CLASS_COMM,
		.bInterfaceSubClass = USB_CDC_SUBCLASS_DMM
	},
	{ }
};

MODULE_DEVICE_TABLE (usb, wdm_ids);

static struct usb_driver wdm_driver = {
	.name =		"cdc_wdm",
	.probe =	wdm_probe,
	.disconnect =	wdm_disconnect,
	.id_table =	wdm_ids,
};

/*
 * TTY driver structures.
 */

static struct tty_operations wdm_ops = {
	.open =			wdm_tty_open,
	.close =		wdm_tty_close,
	.write =		wdm_tty_write,
	.write_room =		wdm_tty_write_room,
	.ioctl =		wdm_tty_ioctl,
	.throttle =		wdm_tty_throttle,
	.unthrottle =		wdm_tty_unthrottle,
	.chars_in_buffer =	wdm_tty_chars_in_buffer,
//	.break_ctl =		wdm_tty_break_ctl,
	.set_termios =		wdm_tty_set_termios,
	.tiocmget =		wdm_tty_tiocmget,
	.tiocmset =		wdm_tty_tiocmset,
};

/*
 * Init / exit.
 */

static int __init wdm_init(void)
{
	int retval;

	wdm_tty_driver = alloc_tty_driver(WDM_TTY_MINORS);

	if (!wdm_tty_driver)
		return -ENOMEM;

	wdm_tty_driver->owner = THIS_MODULE,
	wdm_tty_driver->driver_name = "wdm",
	wdm_tty_driver->name = "ttyWDM",
	wdm_tty_driver->major = ACM_TTY_MAJOR,
	wdm_tty_driver->minor_start = ACM_TTY_MINORS,
	wdm_tty_driver->type = TTY_DRIVER_TYPE_SERIAL,
	wdm_tty_driver->subtype = SERIAL_TYPE_NORMAL,
	wdm_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	wdm_tty_driver->init_termios = tty_std_termios;
	wdm_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;

	tty_set_operations(wdm_tty_driver, &wdm_ops);

	retval = tty_register_driver(wdm_tty_driver);
	if (retval) {
		put_tty_driver(wdm_tty_driver);
		return retval;
	}

	retval = usb_register(&wdm_driver);
	if (retval) {
		tty_unregister_driver(wdm_tty_driver);
		put_tty_driver(wdm_tty_driver);
		return retval;
	}

	info(DRIVER_VERSION ":" DRIVER_DESC);

	return 0;
}

static void __exit wdm_exit(void)
{
	usb_deregister(&wdm_driver);
	tty_unregister_driver(wdm_tty_driver);
	put_tty_driver(wdm_tty_driver);
}

module_init(wdm_init);
module_exit(wdm_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");
