/*
 * Serial-802.15.4 TTY line discipline.
 *
 * Provides interface between ZigBee stack and IEEE 802.15.4 compatible
 * firmware over serial line. Communication protocol is described below.
 *
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 * Sergey Lapin <slapin@ossfans.org>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/tty.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/ieee802154.h>

#include <net/mac802154.h>
#include <net/cfg802154.h>


/* NOTE: be sure to use here the same values as in the firmware */
#define START_BYTE1	's'
#define START_BYTE2	'2'
#define MAX_DATA_SIZE	127

#define TIMEOUT (200 + 255 * 4)

#define IDLE_MODE	0x00
#define RX_MODE		0x02
#define TX_MODE		0x03
#define FORCE_TRX_OFF	0xF0

#define STATUS_SUCCESS	0
#define STATUS_RX_ON	1
#define STATUS_TX_ON	2
#define STATUS_TRX_OFF	3
#define STATUS_IDLE	4
#define STATUS_BUSY	5
#define STATUS_BUSY_RX	6
#define STATUS_BUSY_TX	7
#define STATUS_ERR	8

#define STATUS_WAIT	((u8) -1) /* waiting for the answer */

/* We re-use PPP ioctl for our purposes */
#define	PPPIOCGUNIT	_IOR('t', 86, int)	/* get ppp unit number */

/*
 * The following messages are used to control ZigBee firmware.
 * All communication has request/response format,
 * except of asynchronous incoming data stream (DATA_RECV_* messages).
 */
enum {
	/* Driver to Firmware */
	CMD_NOOP		= 0x00, /* u8 id */
	CMD_OPEN		= 0x01, /* u8 id */
	CMD_CLOSE		= 0x02, /* u8 id */
	CMD_SET_CHANNEL	= 0x03, /* u8 id, u8 page, u8 channel */
	DATA_XMIT_BLOCK	= 0x04, /* u8 id, u8 len, u8 data[len] */
	DATA_RECV_BLOCK	= 0x05, /* u8 id, u8 lqi, u8 len, u8 data[len] */
	CMD_GET_LONG_ADDRESS	= 0x06, /* u8 id */
	CMD_SET_LONG_ADDRESS	= 0x08, /* u8 id, u8 address[8]  (MSB first) */
	CMD_SET_SHORT_ADDRESS	= 0x09, /* u8 id, u8 address[2]  (MSB first) */
	CMD_SET_PAN_ID		= 0x0a, /* u8 id, u8 panid[2]    (MSB first) */
	CMD_SET_PROMISC_MODE	= 0x0b, /* u8 id, u8 mode */

	/* Firmware to Driver */
	RESP_NOOP		= 0x80, /* u8 id, u8 status */
	RESP_OPEN		= 0x81, /* u8 id, u8 status */
	RESP_CLOSE		= 0x82, /* u8 id, u8 status */
	RESP_SET_CHANNEL	= 0x83, /* u8 id, u8 status */
	RESP_XMIT_BLOCK	= 0x84, /* u8 id, u8 status */
	RESP_RECV_BLOCK	= 0x85, /* u8 id, u8 status */
	RESP_GET_LONG_ADDRESS	= 0x06, /* u8 id, u8 address[8] (MSB first)  */
	RESP_SET_LONG_ADDRESS	= 0x88, /* u8 id, u8 status */
	RESP_SET_SHORT_ADDRESS	= 0x89, /* u8 id, u8 status */
	RESP_SET_PAN_ID	= 0x8a, /* u8 id, u8 status */
	RESP_SET_PROMISC_MODE	= 0x8b, /* u8 id, u8 mode */
};

/*
 * Status code from firmware
 */
enum {
	RESP_STATUS_SUCCESS		= 0x00,
	RESP_STATUS_FAILURE		= 0x01,
	RESP_STATUS_SUCCESS_WITH_EXTRA	= 0x02,
};

enum {
	STATE_WAIT_START1,
	STATE_WAIT_START2,
	STATE_WAIT_COMMAND,
	STATE_WAIT_PARAM1,
	STATE_WAIT_PARAM2,
	STATE_WAIT_DATA
};

struct zb_device {
	/* Relative devices */
	struct tty_struct	*tty;
	struct ieee802154_hw	*hw;

	/* locks the ldisc for the command */
	struct mutex		mutex;

	spinlock_t lock;

	/* command completition */
	wait_queue_head_t	wq;
	u8			status;
	u8			ed;

	/* Internal state */
	struct completion	open_done;
	struct completion	close_done;
	unsigned char		opened;
	u8			pending_id;
	unsigned int		pending_size;
	u8			pending_data[MAX_DATA_SIZE + 4];
	/* FIXME: WE NEED LOCKING!!! */

	/* Command (rx) processing */
	int			state;
	unsigned char		id;
	unsigned char		param1;
	unsigned char		param2;
	unsigned char		index;
	int			datalen;
	unsigned char		data[MAX_DATA_SIZE];

	/* Command result */
	u8			result;
};

/*****************************************************************************
 * ZigBee serial device protocol handling
 *****************************************************************************/
static int _open_dev(struct zb_device *zbdev);
static int _close_dev(struct zb_device *zbdev);

static void cleanup(struct zb_device *zbdev)
{
	zbdev->state = STATE_WAIT_START1;
	zbdev->id = 0;
	zbdev->param1 = 0;
	zbdev->param2 = 0;
	zbdev->datalen = 0;
	zbdev->index = 0;
	zbdev->pending_id = 0;
	zbdev->pending_size = 0;
}

static int _send_pending_data(struct zb_device *zbdev)
{
	struct tty_struct *tty;

	//printk("%s: called.\n", __FUNCTION__);

	BUG_ON(!zbdev);
	tty = zbdev->tty;
	if (!tty) {
		printk("%s: ENODEV.\n", __FUNCTION__);
		return -ENODEV;
	}

	zbdev->status = STATUS_WAIT;

	/* Debug info */
	printk(KERN_DEBUG "%s, %d bytes\n", __func__,
	       zbdev->pending_size);

//#ifdef DEBUG
#if 0
	print_hex_dump_bytes("send_pending_data ", DUMP_PREFIX_NONE,
			     zbdev->pending_data, zbdev->pending_size);
#endif

	if (tty->driver->ops->write(tty, zbdev->pending_data,
				    zbdev->pending_size) != zbdev->pending_size) {
		printk(KERN_ERR "%s: device write failed\n", __func__);
		return -1;
	}

	//cleanup(zbdev);
	return 0;
}

static int __maybe_unused send_cmd(struct zb_device *zbdev, u8 id)
{
	int res;
	u8 len = 0;
	/* 4 because of 2 start bytes, id and optional extra */
	u8 buf[4];

	/* Check arguments */
	BUG_ON(!zbdev);

	if (!zbdev->opened) {
		if (_close_dev(zbdev) != 0 || _open_dev(zbdev) != 0)
			return -EAGAIN;
	}

#if 0
	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
		//BUG();
		cleanup(zbdev);
		return -EAGAIN;
	}
#endif

	/* Prepare a message */
	buf[len++] = START_BYTE1;
	buf[len++] = START_BYTE2;
	buf[len++] = id;

	zbdev->pending_id = id;
	zbdev->pending_size = len;
	memcpy(zbdev->pending_data, buf, len);

	res = _send_pending_data(zbdev);

	return res;
}

static int __maybe_unused send_cmd2(struct zb_device *zbdev, u8 id, u8 extra)
{
	int res;
	u8 len = 0;
	/* 4 because of 2 start bytes, id and optional extra */
	u8 buf[4];

	/* Check arguments */
	BUG_ON(!zbdev);

	if (!zbdev->opened) {
		if (_close_dev(zbdev) != 0 || _open_dev(zbdev) != 0)
			return -EAGAIN;
	}

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
//		BUG();
		//cleanup(zbdev);
		return -EAGAIN;
	}

	/* Prepare a message */
	buf[len++] = START_BYTE1;
	buf[len++] = START_BYTE2;
	buf[len++] = id;
	buf[len++] = extra;

	zbdev->pending_id = id;
	zbdev->pending_size = len;
	memcpy(zbdev->pending_data, buf, len);

	res = _send_pending_data(zbdev);

	return res;
}

static int __maybe_unused send_cmd3(struct zb_device *zbdev, u8 id, u8 extra1, u8 extra2)
{
	u8 len = 0;
	/* 5 because of 2 start bytes, id, extra1, extra2 */
	u8 buf[5];

	/* Check arguments */
	BUG_ON(!zbdev);

	if (!zbdev->opened) {
		if (_close_dev(zbdev) != 0 || _open_dev(zbdev) != 0)
			return -EAGAIN;
	}

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
//		BUG();
		//cleanup(zbdev);
		return -EAGAIN;
	}

	/* Prepare a message */
	buf[len++] = START_BYTE1;
	buf[len++] = START_BYTE2;
	buf[len++] = id;
	buf[len++] = extra1;
	buf[len++] = extra2;

	zbdev->pending_id = id;
	zbdev->pending_size = len;
	memcpy(zbdev->pending_data, buf, len);

	return _send_pending_data(zbdev);
}

static int __maybe_unused send_cmd_n(struct zb_device *zbdev, u8 id,
				     u8 *extra, int extralen)
{
	int i;
	u8 len = 0;
	u8 buf[255];

	/* Check arguments */
	BUG_ON(!zbdev);

	if (!zbdev->opened) {
		if (_close_dev(zbdev) != 0 || _open_dev(zbdev) != 0)
			return -EAGAIN;
	}

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
//		BUG();
		//cleanup(zbdev);
		return -EAGAIN;
	}

	/* Prepare a message */
	buf[len++] = START_BYTE1;
	buf[len++] = START_BYTE2;
	buf[len++] = id;

	for (i = 0; i < extralen; i++)
		buf[len++] = extra[i];

	zbdev->pending_id = id;
	zbdev->pending_size = len;
	memcpy(zbdev->pending_data, buf, len);

	return _send_pending_data(zbdev);
}

static int send_block(struct zb_device *zbdev, u8 len, u8 *data)
{
	u8 i = 0, buf[4];	/* 4 because of 2 start bytes, id and len */

	/* Check arguments */
	BUG_ON(!zbdev);

	if (!zbdev->opened) {
		if (_close_dev(zbdev) != 0 || _open_dev(zbdev) != 0)
			return -EAGAIN;
	}

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
		//BUG();
		//cleanup(zbdev);
		return -EAGAIN;
	}

	/* Prepare a message */
	buf[i++] = START_BYTE1;
	buf[i++] = START_BYTE2;
	buf[i++] = DATA_XMIT_BLOCK;
	buf[i++] = len;

	zbdev->pending_id = DATA_XMIT_BLOCK;
	zbdev->pending_size = i + len;
	memcpy(zbdev->pending_data, buf, i);
	memcpy(zbdev->pending_data + i, data, len);

	return _send_pending_data(zbdev);
}

static int is_command(unsigned char c)
{
	//printk("%s: cmd: 0x%02x\n", __FUNCTION__, c);

	switch (c) {
	/* ids we can get here: */
	case RESP_NOOP:
	case RESP_OPEN:
	case RESP_CLOSE:
	case RESP_SET_CHANNEL:
	case RESP_XMIT_BLOCK:
	case RESP_GET_LONG_ADDRESS:
	case RESP_SET_LONG_ADDRESS:
	case RESP_SET_SHORT_ADDRESS:
	case RESP_SET_PAN_ID:
	case RESP_SET_PROMISC_MODE:
	case DATA_RECV_BLOCK:
		return 1;
	}
	return 0;
}

static int _match_pending_id(struct zb_device *zbdev)
{
	return ((zbdev->pending_id == DATA_XMIT_BLOCK &&
		 zbdev->id == RESP_XMIT_BLOCK) ||
		(zbdev->id == DATA_RECV_BLOCK) ||
		(zbdev->pending_id == CMD_NOOP &&
		 zbdev->id == RESP_NOOP) ||
		(zbdev->pending_id == CMD_OPEN &&
		 zbdev->id == RESP_OPEN) ||
		(zbdev->pending_id == CMD_CLOSE &&
		 zbdev->id == RESP_CLOSE) ||
		(zbdev->pending_id == CMD_SET_CHANNEL &&
		 zbdev->id == RESP_SET_CHANNEL) ||
		(zbdev->pending_id == CMD_GET_LONG_ADDRESS &&
		 zbdev->id == RESP_GET_LONG_ADDRESS) ||
		(zbdev->pending_id == CMD_SET_LONG_ADDRESS &&
		 zbdev->id == RESP_SET_LONG_ADDRESS) ||
		(zbdev->pending_id == CMD_SET_SHORT_ADDRESS &&
		 zbdev->id == RESP_SET_SHORT_ADDRESS) ||
		(zbdev->pending_id == CMD_SET_PAN_ID &&
		 zbdev->id == RESP_SET_PAN_ID) ||
		(zbdev->pending_id == CMD_SET_PROMISC_MODE &&
		 zbdev->id == RESP_SET_PROMISC_MODE));
}

static void serial_net_rx(struct zb_device *zbdev)
{
	/* zbdev->param1 is LQI
	 * zbdev->param2 is length of data
	 * zbdev->data is data itself
	 */
	struct sk_buff *skb;

	//printk("%s: datalen: %d\n", __FUNCTION__, zbdev->datalen);

	spin_lock(&zbdev->lock);

	skb = alloc_skb(zbdev->datalen + 2, GFP_ATOMIC);
	skb_put(skb, zbdev->datalen);
	skb_copy_to_linear_data(skb, zbdev->data, zbdev->datalen);

	//printk("%s: len: %d\n", __FUNCTION__, zbdev->param1);

#if 0
	print_hex_dump_bytes("serial_net_rx ", DUMP_PREFIX_NONE,
			     zbdev->data, zbdev->datalen);
#endif
	ieee802154_rx_irqsafe(zbdev->hw, skb, zbdev->param1);

	spin_unlock(&zbdev->lock);
}

static void process_command(struct zb_device *zbdev)
{
	printk("%s: %02x\n", __FUNCTION__, zbdev->id);

	/* Command processing */
	if (!_match_pending_id(zbdev)) {
		printk("%s: not match pending id (pending: 0x%02x, id: 0x%02x)\n",
		       __FUNCTION__, zbdev->pending_id, zbdev->id);
		cleanup(zbdev);
		return;
	}

	if (!zbdev->opened) {
		cleanup(zbdev);
		return;
	}

	switch (zbdev->id) {
	case DATA_RECV_BLOCK:
		/* zbdev->param1 is LQ, zbdev->param2 is length */
		serial_net_rx(zbdev);
		zbdev->state = STATE_WAIT_START1;
		break;

	case RESP_NOOP:
	case RESP_OPEN:
	case RESP_CLOSE:
	case RESP_SET_CHANNEL:
	case RESP_XMIT_BLOCK:
	case RESP_GET_LONG_ADDRESS:
	case RESP_SET_LONG_ADDRESS:
	case RESP_SET_SHORT_ADDRESS:
	case RESP_SET_PAN_ID:
	case RESP_SET_PROMISC_MODE:
		zbdev->pending_id = 0;
		zbdev->pending_size = 0;
		zbdev->status = STATUS_SUCCESS;
		zbdev->result = zbdev->param1;
		zbdev->state = STATE_WAIT_START1;
		wake_up(&zbdev->wq);
		break;

	default:
		break;
	}
}

static void process_char(struct zb_device *zbdev, unsigned char c)
{
	static u64 last = 0;
	u64 now, timeout;

	now = get_jiffies_64();
#if 0
	if (last != 0) {
		timeout = last + msecs_to_jiffies(50);

		if (time_after64(now, timeout))
			cleanup(zbdev);
	}
#endif
	last = now;

	/* Data processing */
	switch (zbdev->state) {
	case STATE_WAIT_START1:
		if (c == START_BYTE1)
			zbdev->state = STATE_WAIT_START2;
		else
			cleanup(zbdev);
		break;

	case STATE_WAIT_START2:
		if (c == START_BYTE2)
			zbdev->state = STATE_WAIT_COMMAND;
		else
			cleanup(zbdev);
		break;

	case STATE_WAIT_COMMAND:
		if (is_command(c)) {
			zbdev->id = c;
			zbdev->state = STATE_WAIT_PARAM1;
		} else {
			cleanup(zbdev);
			printk(KERN_ERR "%s, unexpected command id: %x\n",
			       __func__, c);
		}
		break;

	case STATE_WAIT_PARAM1:
		zbdev->param1 = c;

		if (zbdev->id == DATA_RECV_BLOCK) {
			zbdev->state = STATE_WAIT_PARAM2;
		} else if (zbdev->id == RESP_GET_LONG_ADDRESS) {
			zbdev->state = STATE_WAIT_DATA;
			zbdev->datalen = 8;
		} else if (c == RESP_STATUS_SUCCESS) {
			process_command(zbdev);
		} else {
			zbdev->state = STATE_WAIT_PARAM2;
		}
		break;

	case STATE_WAIT_PARAM2:
		zbdev->param2 = c;

		if (zbdev->id == DATA_RECV_BLOCK) {
			zbdev->state = STATE_WAIT_DATA;
			zbdev->datalen = (int) zbdev->param2;
			zbdev->index = 0;
		} else {
			process_command(zbdev);
		}
		break;

	case STATE_WAIT_DATA:
		if ((zbdev->index < sizeof(zbdev->data)) &&
		    (zbdev->datalen <= sizeof(zbdev->data))) {
			zbdev->data[zbdev->index] = c;
			zbdev->index++;

			if (zbdev->index == zbdev->datalen) {
				zbdev->state = STATE_WAIT_START1;
				process_command(zbdev);
			}
		} else {
			printk(KERN_ERR "%s(): data size(%d) is greater "
			       "than buffer available\n", __func__, zbdev->index);
			cleanup(zbdev);
		}
		break;

	default:
		cleanup(zbdev);
	}
}

/*****************************************************************************
 * Device operations for IEEE 802.15.4 PHY side interface ZigBee stack
 *****************************************************************************/

static int _wait_response(struct zb_device *zbdev, int timeout)
{
	int ret;

	if (wait_event_interruptible_timeout(zbdev->wq,
					     zbdev->status != STATUS_WAIT,
					     msecs_to_jiffies(timeout)) > 0) {
		if (zbdev->status != STATUS_SUCCESS) {
			printk("%s: EBUSY\n", __FUNCTION__);
			ret = -EBUSY;
		} else {
			ret = 0;
		}
	} else {
		ret = -ETIMEDOUT;
		printk("%s: ETIMEDOUT\n", __FUNCTION__);
	}

	return ret;
}

static int _open_dev(struct zb_device *zbdev)
{
	/* Check arguments */
	BUG_ON(!zbdev);
	if (zbdev->opened)
		return 0;

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
		//BUG();
		cleanup(zbdev);
		return -EAGAIN;
	}

	zbdev->opened = 1;
	return 0;
}

static int _close_dev(struct zb_device *zbdev)
{
	/* Check arguments */
	BUG_ON(!zbdev);

	if (zbdev->pending_size) {
		printk(KERN_ERR "%s(): cmd is already pending, id = %u\n",
		       __func__, zbdev->pending_id);
		//BUG();
		cleanup(zbdev);
		return -EAGAIN;
	}

	zbdev->opened = 0;
	return 0;
}

static int ieee802154_serial_set_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct zb_device *zbdev;
	int ret = 0;
	u8 buf[2];

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return -EINVAL;
	}

	buf[0] = page;
	buf[1] = channel;

	ret = send_cmd_n(zbdev, CMD_SET_CHANNEL, buf, 2);

	if (ret < 0)
		return ret;

	ret = _wait_response(zbdev, 1 * 1000);

	if (ret < 0) {
		cleanup(zbdev);
		return ret;
	}

	if (zbdev->result == RESP_STATUS_FAILURE)
		ret = -EFAULT;

	return ret;
}

static int ieee802154_serial_set_long_addr(struct ieee802154_hw *hw, __le64 addr)
{
	struct zb_device *zbdev;
	int i, ret = 0;
	u8 buf[8];

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < 8; i++)
		buf[i] = (u8) ((addr >> (8 * i)) & 0xff);

	ret = send_cmd_n(zbdev, CMD_SET_LONG_ADDRESS, buf, 8);

	if (ret < 0)
		return ret;

	ret = _wait_response(zbdev, 1 * 1000);

	if (ret < 0) {
		cleanup(zbdev);
		return ret;
	}

	if (zbdev->result == RESP_STATUS_FAILURE)
		ret = -EFAULT;

	return ret;
}

static int ieee802154_serial_set_hw_addr_filt(struct ieee802154_hw *hw,
					      struct ieee802154_hw_addr_filt *filt,
					      unsigned long changed)
{
	struct zb_device *zbdev;
	int ret = 0;

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return -EINVAL;
	}

	if (changed & IEEE802154_AFILT_PANID_CHANGED) {
		u8 buf[2];
		u16 pan = le16_to_cpu(filt->pan_id);
		printk("%s: PANID: 0x%04x\n", __FUNCTION__, pan);

		buf[0] = (u8) ((pan >> 8) & 0xff);
		buf[1] = (u8) ((pan >> 0) & 0xff);

		ret = send_cmd_n(zbdev, CMD_SET_PAN_ID, buf, 2);

		if (ret < 0)
			return ret;

		ret = _wait_response(zbdev, 1 * 1000);

		if (ret < 0) {
			cleanup(zbdev);
			return ret;
		}

		if (zbdev->result == RESP_STATUS_FAILURE) {
			return -EFAULT;
		}
	}

	if (changed & IEEE802154_AFILT_IEEEADDR_CHANGED) {
		u8 i, addr[8];

		memcpy(addr, &filt->ieee_addr, 8);
		printk("%s: ADDR:", __FUNCTION__);
		for (i = 0; i < 8; i++) {
			printk(" %02x", addr[i]);
		}
		printk("\n");
	}

	return ret;
}

static int ieee802154_serial_ed(struct ieee802154_hw *hw, u8 *level)
{
	return 0;
}

static int ieee802154_serial_start(struct ieee802154_hw *hw)
{
	struct zb_device *zbdev;
	int ret = 0;

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return -EINVAL;
	}

	ieee802154_serial_set_long_addr(hw, hw->phy->perm_extended_addr);

	return ret;
}

static void ieee802154_serial_stop(struct ieee802154_hw *hw)
{
	struct zb_device *zbdev;

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return;
	}
}

static int ieee802154_serial_xmit(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct zb_device *zbdev;
	int ret;

	zbdev = hw->priv;
	if (!zbdev) {
		printk(KERN_ERR "%s: wrong phy\n", __func__);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&zbdev->mutex))
		return -EINTR;

	ret = send_block(zbdev, skb->len, skb->data);
	if (ret)
		goto out;

	ret = _wait_response(zbdev, TIMEOUT);
	if (ret < 0) {
		cleanup(zbdev);
	}

#if 0
	if (wait_event_interruptible_timeout(zbdev->wq,
					     zbdev->status != STATUS_WAIT,
					     msecs_to_jiffies(TIMEOUT)) > 0) {
		if (zbdev->status != STATUS_SUCCESS) {
			printk("%s: EBUSY\n", __FUNCTION__);
			ret = -EBUSY;
			cleanup(zbdev);
			goto out;
		}
	} else {
		ret = -ETIMEDOUT;
		printk("%s: ETIMEDOUT\n", __FUNCTION__);
		cleanup(zbdev);
		goto out;
	}
#endif

out:
	mutex_unlock(&zbdev->mutex);
	return ret;
}

static int ieee802154_serial_set_promiscuous_mode(struct ieee802154_hw *hw,
						  const bool on)
{

	return 0;
}

/*****************************************************************************
 * Line discipline interface for IEEE 802.15.4 serial device
 *****************************************************************************/

static struct ieee802154_ops serial_ops = {
	.owner = THIS_MODULE,
	.xmit_sync = ieee802154_serial_xmit,
	.ed = ieee802154_serial_ed,
	.set_channel = ieee802154_serial_set_channel,
	.start = ieee802154_serial_start,
	.stop = ieee802154_serial_stop,
	.set_hw_addr_filt = ieee802154_serial_set_hw_addr_filt,
	.set_promiscuous_mode = ieee802154_serial_set_promiscuous_mode,
};

/*
 * Called when a tty is put into ZB line discipline. Called in process context.
 * Returns 0 on success.
 */
static int ieee802154_tty_open(struct tty_struct *tty)
{
	struct zb_device *zbdev = tty->disc_data;
	struct ieee802154_hw *hw;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (tty->disc_data)
		return -EBUSY;

	hw = ieee802154_alloc_hw(sizeof(*zbdev), &serial_ops);
	if (!hw)
		return -ENOMEM;

	zbdev = hw->priv;
	zbdev->hw = hw;

	mutex_init(&zbdev->mutex);
	init_completion(&zbdev->open_done);
	init_completion(&zbdev->close_done);
	init_waitqueue_head(&zbdev->wq);

	hw->extra_tx_headroom = 0;
	/* 920MHz Japanese band (ch33 - 59) */
	hw->phy->channels_supported[9] = 0x0003fff0;

	//hw->flags = IEEE802154_HW_OMIT_CKSUM;
	hw->flags = IEEE802154_HW_RX_OMIT_CKSUM;

	hw->parent = tty->dev;

	zbdev->tty = tty_kref_get(tty);

	cleanup(zbdev);

	tty->disc_data = zbdev;
	tty->receive_room = MAX_DATA_SIZE;
	// TC: why was it removed ?
	// why does it crash one of my computer each times, and the other one is fine
	// tty->low_latency = 1;

	/* FIXME: why is this needed. Note don't use ldisc_ref here as the
	   open path is before the ldisc is referencable */

	if (tty->ldisc->ops->flush_buffer)
		tty->ldisc->ops->flush_buffer(tty);
	tty_driver_flush_buffer(tty);

	spin_lock_init(&zbdev->lock);
	
	ieee802154_random_extended_addr(&hw->phy->perm_extended_addr);
	
	err = ieee802154_register_hw(hw);
	if (err) {
		printk(KERN_ERR "%s: device register failed\n", __func__);
		goto out_free;
	}

	return 0;

out_free:
	tty->disc_data = NULL;
	tty_kref_put(tty);
	zbdev->tty = NULL;

	ieee802154_unregister_hw(zbdev->hw);
	ieee802154_free_hw(zbdev->hw);

	return err;
}

/*
 * Called when the tty is put into another line discipline or it hangs up. We
 * have to wait for any cpu currently executing in any of the other zb_tty_*
 * routines to finish before we can call zb_tty_close and free the
 * zb_serial_dev struct. This routine must be called from process context, not
 * interrupt or softirq context.
 */
static void ieee802154_tty_close(struct tty_struct *tty)
{
	struct zb_device *zbdev;

	zbdev = tty->disc_data;
	if (!zbdev) {
		printk(KERN_WARNING "%s: match is not found\n", __func__);
		return;
	}

	tty->disc_data = NULL;
	tty_kref_put(tty);
	zbdev->tty = NULL;
	mutex_destroy(&zbdev->mutex);

	ieee802154_unregister_hw(zbdev->hw);

	tty_ldisc_flush(tty);
	tty_driver_flush_buffer(tty);

	ieee802154_free_hw(zbdev->hw);
}

/*
 * Called on tty hangup in process context.
 */
static int ieee802154_tty_hangup(struct tty_struct *tty)
{
	ieee802154_tty_close(tty);
	return 0;
}

/*
 * Called in process context only. May be re-entered
 * by multiple ioctl calling threads.
 */
static int ieee802154_tty_ioctl(struct tty_struct *tty, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	struct zb_device *zbdev;

	zbdev = tty->disc_data;
	if (zbdev == NULL) {
		printk("dev is not found\n");
		return -EINVAL;
	}

	switch (cmd) {
	case TCFLSH:
		return tty_perform_flush(tty, arg);
	default:
		/* Try the mode commands */
		return tty_mode_ioctl(tty, file, cmd, arg);
	}
}


/*
 * This can now be called from hard interrupt level as well
 * as soft interrupt level or mainline.
 */
static void ieee802154_tty_receive(struct tty_struct *tty, const unsigned char *buf,
				   char *cflags, int count)
{
	struct zb_device *zbdev;
	int i;

#ifdef DEBUG
	print_hex_dump_bytes("ieee802154_tty_receive ", DUMP_PREFIX_NONE,
			     buf, count);
#endif

	/* Actual processing */
	zbdev = tty->disc_data;
	if (!zbdev) {
		printk(KERN_ERR "%s(): record for tty is not found\n",
		       __func__);
		return;
	}
	for (i = 0; i < count; ++i)
		process_char(zbdev, buf[i]);
#if 0
	if (tty->driver->flush_chars)
		tty->driver->flush_chars(tty);
#endif
	tty_unthrottle(tty);
}

/*
 * Line discipline device structure
 */
static struct tty_ldisc_ops ieee802154_ldisc = {
	.owner  = THIS_MODULE,
	.magic	= TTY_LDISC_MAGIC,
	.name	= "ieee802154-ldisc",
	.open	= ieee802154_tty_open,
	.close	= ieee802154_tty_close,
	.hangup	= ieee802154_tty_hangup,
	.receive_buf = ieee802154_tty_receive,
	.ioctl	= ieee802154_tty_ioctl,
};

/*****************************************************************************
 * Module service routinues
 *****************************************************************************/

static int __init ieee802154_serial_init(void)
{
	printk(KERN_INFO "Initializing serial802154 TTY interface\n");

	if (tty_register_ldisc(N_IEEE802154, &ieee802154_ldisc) != 0) {
		printk(KERN_ERR "%s: line discipline register failed\n",
		       __func__);
		return -EINVAL;
	}

	return 0;
}

static void __exit ieee802154_serial_cleanup(void)
{
	if (tty_unregister_ldisc(N_IEEE802154) != 0)
		printk(KERN_CRIT
		       "failed to unregister serial802154 line discipline.\n");
}

module_init(ieee802154_serial_init);
module_exit(ieee802154_serial_cleanup);

MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_IEEE802154);
