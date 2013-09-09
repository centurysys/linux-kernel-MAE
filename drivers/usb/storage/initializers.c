/* Special Initializers for certain USB Mass Storage devices
 *
 * Current development and maintenance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/errno.h>

#include "usb.h"
#include "initializers.h"
#include "debug.h"
#include "transport.h"

/* This places the Shuttle/SCM USB<->SCSI bridge devices in multi-target
 * mode */
int usb_stor_euscsi_init(struct us_data *us)
{
	int result;

	US_DEBUGP("Attempting to init eUSCSI bridge...\n");
	us->iobuf[0] = 0x1;
	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
			0x0C, USB_RECIP_INTERFACE | USB_TYPE_VENDOR,
			0x01, 0x0, us->iobuf, 0x1, 5000);
	US_DEBUGP("-- result is %d\n", result);

	return 0;
}

/* This function is required to activate all four slots on the UCR-61S2B
 * flash reader */
int usb_stor_ucr61s2b_init(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap*) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap*) us->iobuf;
	int res;
	unsigned int partial;
	static char init_string[] = "\xec\x0a\x06\x00$PCCHIPS";

	US_DEBUGP("Sending UCR-61S2B initialization packet...\n");

	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->Tag = 0;
	bcb->DataTransferLength = cpu_to_le32(0);
	bcb->Flags = bcb->Lun = 0;
	bcb->Length = sizeof(init_string) - 1;
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, init_string, sizeof(init_string) - 1);

	res = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcb,
			US_BULK_CB_WRAP_LEN, &partial);
	if (res)
		return -EIO;

	US_DEBUGP("Getting status packet...\n");
	res = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
			US_BULK_CS_WRAP_LEN, &partial);
	if (res)
		return -EIO;

	return 0;
}

/* This places the HUAWEI E220 devices in multi-port mode */
int usb_stor_huawei_e220_init(struct us_data *us)
{
	int result;

	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
				      USB_REQ_SET_FEATURE,
				      USB_TYPE_STANDARD | USB_RECIP_DEVICE,
				      0x01, 0x0, NULL, 0x0, 1000);
	US_DEBUGP("Huawei mode set result is %d\n", result);
	return 0;
}

#ifdef CONFIG_MACH_MAGNOLIA2
/* This places the LG L-02A devices in multi-port mode */
int usb_stor_lg_l02a_init(struct us_data *us)
{
	int result, actlen;
	char buf[] = {'U',  'S',  'B',	'C',  '@',  0x96, 0x95, 0x87,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		      0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	printk("NTT docomo L-02A(storage mode) found, ejecting...\n");
	mdelay(100);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    buf, 31, &actlen);
	US_DEBUGP("usb_bulk_transfer performing result is %d\n", result);
	return (result ? 0 : -1);
}

/* This places the LG L-05A devices in multi-port mode */
int usb_stor_lg_l05a_init(struct us_data *us)
{
	int result, actlen, tmp;
	char buf[] = {0x55, 0x53, 0x42, 0x43, 0x90, 0xe2, 0x2e, 0x86,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	char buf2[0x200];

	printk("NTT docomo L-05A(storage mode) found, ejecting...\n");
	mdelay(100);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    buf, 31, &actlen);
	tmp = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
					 buf2, 0x200, &actlen);
	mdelay(100);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    buf, 31, &actlen);
	US_DEBUGP("usb_bulk_transfer performing result is %d\n", result);
	//return (result ? 0 : -1);
	return 0;
}

/* This places the LG L-02C devices in multi-port mode */
int usb_stor_lg_l02c_init(struct us_data *us)
{
	int result, actlen;
	char buf[] = {0x55, 0x53, 0x42, 0x43, 0x68, 0xc2, 0x08, 0x89,
		      0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0xf1,
		      0x01, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	printk("NTT docomo L-02C(storage mode) found, ejecting...\n");
	mdelay(100);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    buf, 31, &actlen);
	US_DEBUGP("usb_bulk_transfer performing result is %d\n", result);
	return (result ? 0 : -1);
}

/* This places the ZTE devices in multi-port mode */
int usb_stor_zte_init(struct us_data *us)
{
	int result, actlen;
	char buf[] = {0x55, 0x53, 0x42, 0x43, 0xe0, 0xab, 0x36, 0x86,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		      0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	printk("ZTE MF6x6 found, ejecting...\n");

	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    buf, 31, &actlen);
	US_DEBUGP("usb_bulk_msg performing result is %d\n", result);
	return (result ? 0 : -1);
}

/* This places the Fujitsu F-06C devices in multi-port mode */
int usb_stor_f06c_init(struct us_data *us)
{
	int result;

	printk("NTT Docomo F-06C found, ejecting...\n");

#define F06C_USB_REQUEST_Mode	 0x70
#define F06C_SET_MODE		 0x0000

	mdelay(1000);
	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
				      F06C_USB_REQUEST_Mode, USB_RECIP_INTERFACE | USB_TYPE_VENDOR,
				      F06C_SET_MODE, 0x0, NULL, 0, USB_CTRL_SET_TIMEOUT);

	return result;
}
#endif
