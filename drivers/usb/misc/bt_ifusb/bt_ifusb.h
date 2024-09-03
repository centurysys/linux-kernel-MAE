/***********************************************
 *	 Copyright (c) Toshiba Corporation 2024
 *	 All Rights Reserved
 ***********************************************/

/*
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __BT_IFUSB_H__
#define __BT_IFUSB_H__

// for usb_anchor
#include <linux/usb.h>

// for skb
#include <linux/skbuff.h>

/*****************************************************************************/
/*                              Type definitions                             */
/*****************************************************************************/

typedef struct {
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct usb_interface *isoc;

	unsigned long flags;

	struct work_struct work;
	struct work_struct waker;

	struct usb_anchor tx_anchor;
	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;
	struct usb_anchor isoc_anchor;
	struct usb_anchor deferred;
	int tx_in_flight;
	spinlock_t txlock;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
	struct usb_endpoint_descriptor *isoc_tx_ep;
	struct usb_endpoint_descriptor *isoc_rx_ep;

	__u8 cmdreq_type;

	unsigned int sco_num;
	int isoc_altsetting;
	int suspend_count;
	char name[12];
	unsigned long hciflags;
	__u16 voice_setting;

	int rtl_initializing;
	struct semaphore rtl_init_sem;
	struct workqueue_struct	*workqueue;
	wait_queue_head_t	hci_cmd_waitqueue;
	struct work_struct	init_rtl;
	__u8 hci_rcv_buffer[14];
} bt_ifusb_data;

/*****************************************************************************/
/*                                 Prototypes                                */
/*****************************************************************************/

// core
extern int bt_ifusb_open(bt_ifusb_data *bt_ifusb_instance);
extern int bt_ifusb_close(bt_ifusb_data *bt_ifusb_instance);
extern void bt_ifusb_setsco(bt_ifusb_data *bt_ifusb_instance);
extern int bt_ifusb_internal_receive(int type, const unsigned char *buf, int count);
extern int bt_ifusb_send_frame(struct sk_buff *skb, __u8 pkt_type);
extern int bt_ifusb_hci_cmd(bt_ifusb_data *bt_ifusb_instance, u16 opcode, u32 plen, const void *param);
extern int bt_ifusb_internal_open(void);
extern void bt_ifusb_internal_close(void);

// tty
extern void bt_ifusb_tty_lock_port_ctrl(int port);
extern void bt_ifusb_tty_unlock_port_ctrl(int port);
extern int bt_ifusb_tty_receive(int type, const unsigned char *buf, int count);
extern void bt_ifusb_tty_cleanup(void);
extern int bt_ifusb_tty_init(void);

// rtk
void bt_ifusb_setup_realtek(struct work_struct *work);

#endif /* #ifndef __BT_IFUSB_H__ */
