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

// for flip buffer
#include <linux/module.h>

#include "bt_ifusb.h"

#define VERSION_BT_IFUSB "0.6"

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DPRINT printk
#else
#define DPRINT(fmt, args...)
#endif

/* #define TEST_UBUNTU */
/* #define BT_IFSPP_DEBUG_DUMP */

/* HCI data types */
#define HCI_COMMAND_PKT		0x01
#define HCI_ACLDATA_PKT		0x02
#define HCI_SCODATA_PKT		0x03
#define HCI_EVENT_PKT		0x04

#define HCI_MAX_ACL_SIZE	1024
#define HCI_MAX_SCO_SIZE	255
#define HCI_MAX_EVENT_SIZE	260
#define HCI_MAX_FRAME_SIZE	(HCI_MAX_ACL_SIZE + 4)

// 現バージョンでは複数のBluetoothモジュールをサポートしない
bt_ifusb_data *usbinstance = NULL;

static struct usb_driver bt_ifusb_driver;
	
#define BTUSB_IGNORE			BIT(0)
#define BTUSB_DIGIANSWER		BIT(1)
#define BTUSB_CSR			BIT(2)
#define BTUSB_SNIFFER			BIT(3)
#define BTUSB_BCM92035			BIT(4)
#define BTUSB_BROKEN_ISOC		BIT(5)
#define BTUSB_WRONG_SCO_MTU		BIT(6)
#define BTUSB_ATH3012			BIT(7)
#define BTUSB_INTEL_COMBINED		BIT(8)
#define BTUSB_INTEL_BOOT		BIT(9)
#define BTUSB_BCM_PATCHRAM		BIT(10)
#define BTUSB_MARVELL			BIT(11)
#define BTUSB_SWAVE			BIT(12)
#define BTUSB_AMP			BIT(13)
#define BTUSB_QCA_ROME			BIT(14)
#define BTUSB_BCM_APPLE			BIT(15)
#define BTUSB_REALTEK			BIT(16)
#define BTUSB_BCM2045			BIT(17)
#define BTUSB_IFNUM_2			BIT(18)
#define BTUSB_CW6622			BIT(19)
#define BTUSB_MEDIATEK			BIT(20)
#define BTUSB_WIDEBAND_SPEECH		BIT(21)
#define BTUSB_VALID_LE_STATES		BIT(22)
#define BTUSB_QCA_WCN6855		BIT(23)
#define BTUSB_INTEL_BROKEN_SHUTDOWN_LED	BIT(24)
#define BTUSB_INTEL_BROKEN_INITIAL_NCMD BIT(25)
#define BTUSB_INTEL_NO_WBS_SUPPORT	BIT(26)
#define BTUSB_ACTIONS_SEMI		BIT(27)
	
static const struct usb_device_id bt_ifusb_table[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },
	
	/* Generic Bluetooth USB interface */
	{ USB_INTERFACE_INFO(0xe0, 0x01, 0x01) },

	/* Apple-specific (Broadcom) devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x05ac, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_APPLE | BTUSB_IFNUM_2 },

	/* MediaTek MT76x0E */
	{ USB_DEVICE(0x0e8d, 0x763f) },

	/* Broadcom SoftSailing reporting vendor specific */
	{ USB_DEVICE(0x0a5c, 0x21e1) },

	/* Apple MacBookPro 7,1 */
	{ USB_DEVICE(0x05ac, 0x8213) },

	/* Apple iMac11,1 */
	{ USB_DEVICE(0x05ac, 0x8215) },

	/* Apple MacBookPro6,2 */
	{ USB_DEVICE(0x05ac, 0x8218) },

	/* Apple MacBookAir3,1, MacBookAir3,2 */
	{ USB_DEVICE(0x05ac, 0x821b) },

	/* Apple MacBookAir4,1 */
	{ USB_DEVICE(0x05ac, 0x821f) },

	/* Apple MacBookPro8,2 */
	{ USB_DEVICE(0x05ac, 0x821a) },
	
	/* Apple MacMini5,1 */
	{ USB_DEVICE(0x05ac, 0x8281) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800), .driver_info = BTUSB_SWAVE },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },
	
	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_DEVICE(0x0c10, 0x0000) },

	/* Broadcom BCM20702A0 */
	{ USB_DEVICE(0x0b05, 0x17b5) },
	{ USB_DEVICE(0x0b05, 0x17cb) },
	{ USB_DEVICE(0x04ca, 0x2003) },
	{ USB_DEVICE(0x0489, 0xe042) },
	{ USB_DEVICE(0x413c, 0x8197) },

	/* Broadcom BCM20702B0 (Dynex/Insignia) */
	{ USB_DEVICE(0x19ff, 0x0239), .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom BCM43142A0 (Foxconn/Lenovo) */
	{ USB_DEVICE(0x105b, 0xe065), .driver_info = BTUSB_BCM_PATCHRAM },

	/* Foxconn - Hon Hai */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0489, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Lite-On Technology - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x04ca, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom devices with vendor specific id */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0a5c, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* ASUSTek Computer - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0b05, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Belkin F8065bf - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x050d, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* IMC Networks - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x13d3, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Dell Computer - Broadcom based  */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x413c, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Toshiba Corp - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0930, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Intel Bluetooth USB Bootloader (RAM module) */
	{ USB_DEVICE(0x8087, 0x0a5a),
	  .driver_info = BTUSB_INTEL_BOOT | BTUSB_BROKEN_ISOC },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, bt_ifusb_table);

static const struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Broadcom BCM2045 devices */
	{ USB_DEVICE(0x0a5c, 0x2045), .driver_info = BTUSB_BCM2045 },

	/* Atheros 3011 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe027), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe03d), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x04f2, 0xaff1), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0930, 0x0215), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0x3002), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0xe019), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x13d3, 0x3304), .driver_info = BTUSB_IGNORE },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_DEVICE(0x03f0, 0x311d), .driver_info = BTUSB_IGNORE },

	/* Atheros 3012 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe04d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe04e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe056), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe057), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe05f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe076), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe078), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe095), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04c5, 0x1330), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3007), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3010), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3014), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3018), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0219), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x021c), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0220), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0227), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0b05, 0x17d0), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x0036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3121), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817a), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe003), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3362), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3375), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3393), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3395), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3402), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3408), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3423), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3432), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3472), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3474), .driver_info = BTUSB_ATH3012 },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe02c), .driver_info = BTUSB_IGNORE },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe03c), .driver_info = BTUSB_ATH3012 },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = BTUSB_BCM92035 },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 and 410 devices */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Asus WL-BTD202 device */
	{ USB_DEVICE(0x0b05, 0x1715), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622), .driver_info = BTUSB_BROKEN_ISOC },

	/* Roper Class 1 Bluetooth Dongle (Silicon Wave based) */
	{ USB_DEVICE(0x1310, 0x0001), .driver_info = BTUSB_SWAVE },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Marvell Bluetooth devices */
	{ USB_DEVICE(0x1286, 0x2044), .driver_info = BTUSB_MARVELL },
	{ USB_DEVICE(0x1286, 0x2046), .driver_info = BTUSB_MARVELL },

	/* Intel Bluetooth devices */
	{ USB_DEVICE(0x8087, 0x07da), .driver_info = BTUSB_CSR },
	{ USB_DEVICE(0x8087, 0x07dc), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0a2a), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0a2b), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0aa7), .driver_info = BTUSB_INTEL_COMBINED },

	/* Other Intel Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x8087, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_IGNORE },

	/* Realtek 8821CE Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3529), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8822CE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0xb00c), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc822), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8822CU Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3549), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8852AE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0x2852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x385a), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x4852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x165c), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x4006), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb8, 0xc549), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8852CE Bluetooth devices */
	{ USB_DEVICE(0x04ca, 0x4007), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x1675), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb8, 0xc558), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3587), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3586), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3592), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8852BE Bluetooth devices */
	{ USB_DEVICE(0x0cb8, 0xc559), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x887b), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3571), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0bda, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BU Bluetooth devices */
	{ USB_DEVICE(0x0411, 0x0374), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723AE Bluetooth devices */
	{ USB_DEVICE(0x0930, 0x021d), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3394), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BE Bluetooth devices */
	{ USB_DEVICE(0x0489, 0xe085), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x0489, 0xe08b), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x04f2, 0xb49f), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3410), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3416), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3459), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3494), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BU Bluetooth devices */
	{ USB_DEVICE(0x7392, 0xa611), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723DE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0xb009), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x2ff8, 0xb011), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8761BUV Bluetooth devices */
	{ USB_DEVICE(0x2357, 0x0604), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0b05, 0x190e), .driver_info = BTUSB_REALTEK |
	  					     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x2550, 0x8761), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x8771), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x6655, 0x8771), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x7392, 0xc611), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x2b89, 0x8761), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Additional Realtek 8821AE Bluetooth devices */
	{ USB_DEVICE(0x0b05, 0x17dc), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3414), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3458), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3461), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3462), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8822BE Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3526), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x0b05, 0x185c), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8822CE Bluetooth devices */
	{ USB_DEVICE(0x04ca, 0x4005), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x161f), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0b05, 0x18ef), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3548), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3549), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3553), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3555), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x2ff8, 0x3051), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x1358, 0xc123), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc123), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb5, 0xc547), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Silicon Wave based devices */
	{ USB_DEVICE(0x0c10, 0x0000), .driver_info = BTUSB_SWAVE },

	{ }	/* Terminating entry */
};

#define BTUSB_MAX_ISOC_FRAMES	10

#define BTUSB_INTR_RUNNING		0
#define BTUSB_BULK_RUNNING		1
#define BTUSB_ISOC_RUNNING		2
#define BTUSB_SUSPENDING		3
#define BTUSB_DID_ISO_RESUME	4
#define BTUSB_BOOTLOADER		5
#define BTUSB_DOWNLOADING		6
#define BTUSB_FIRMWARE_LOADED	7
#define BTUSB_FIRMWARE_FAILED	8
#define BTUSB_BOOTING			9
#define BTUSB_RESET_RESUME		10
#define BTUSB_DIAG_RUNNING		11

enum {
	HCI_RUNNING,
};

/*****************************************************************************/
/*                                 Functions                                 */
/*****************************************************************************/

int bt_ifusb_internal_open(void)
{
	int port = 0;

	DPRINT("\"%s\"[%d] %s port(%d)\n", current->comm, current->pid,
		__func__, port);
	
	if(!usbinstance)
	{
		return -ENODEV;
	}
	
	bt_ifusb_tty_lock_port_ctrl(port);
	bt_ifusb_open(usbinstance);
	return 0;
}

void bt_ifusb_internal_close(void)
{
	int port = 0;

	DPRINT("\"%s\"[%d] %s port(%d)\n", current->comm, current->pid,
		__func__, port);

	bt_ifusb_close(usbinstance);
	bt_ifusb_tty_unlock_port_ctrl(port);
}


int bt_ifusb_internal_receive(int type, const unsigned char *buf, int count)
{
	int send_c;
	
	DPRINT("\"%s\"[%d] %s type(%d) start\n", current->comm, current->pid,
		__func__, type);
	
	if(count > sizeof(usbinstance->hci_rcv_buffer)) {
		send_c = sizeof(usbinstance->hci_rcv_buffer);
	} else {
		send_c = count;
	}

#ifdef BT_IFSPP_DEBUG_DUMP
	{
		int i;
		printk(KERN_DEBUG);
		printk("[IRCV]");
		for(i=0;i<count;i++){
			if(!(i % 16)){
				printk("\n");
				printk(KERN_DEBUG);
			}
			printk("%02X ", buf[i]);
		}
		printk("\n");
	}
#endif

	memcpy(usbinstance->hci_rcv_buffer, buf, send_c);
	wake_up_interruptible(&usbinstance->hci_cmd_waitqueue);

	return send_c;
}

int bt_ifusb_hci_cmd(bt_ifusb_data *bt_ifusb_instance, u16 opcode,
				u32 plen, const void *param)
{
	DECLARE_WAITQUEUE(wait, current);
	__u8 hci_snd_buffer[256];
	struct sk_buff *skb;
	int count = plen + 3;
	
	struct hci_command {
		__le16	opcode;		/* OCF & OGF */
		__u8	plen;
		__u8	param[253];
	} *hdr;
	
	skb = alloc_skb(count, GFP_ATOMIC);
	
	if(!skb)
	{
		DPRINT("\"%s\"[%d] %s no skb\n", current->comm, current->pid,
			__func__);
		return -ENOMEM;
	}

	hdr = (struct hci_command *)hci_snd_buffer;

	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;
	if (plen) {
		memcpy(hdr->param, param, plen);
	}

#ifdef BT_IFSPP_DEBUG_DUMP
	{
		int i;
		printk(KERN_DEBUG);
		printk("[SND] %04X %02X  ", hdr->opcode, hdr->plen);
		for(i=0;i<plen;i++){
			if(!(i % 16)){
				printk("\n");
				printk(KERN_DEBUG);
			}
			printk("%02X ", hdr->param[i]);
		}
		printk("\n");
	}
#endif

	memcpy(skb_put(skb, count), hci_snd_buffer, count);

	// タイムアウト時に割り込み
	add_wait_queue(&bt_ifusb_instance->hci_cmd_waitqueue, &wait);
	set_current_state( TASK_INTERRUPTIBLE );

	bt_ifusb_send_frame( skb, 1 );

	// タイマ監視 2秒
	schedule_timeout(msecs_to_jiffies(2000));

	remove_wait_queue(&bt_ifusb_instance->hci_cmd_waitqueue, &wait);

	// タイムアウト判定
	if (signal_pending(current)) {
		DPRINT("\"%s\"[%d] %s hci command timeout.\n", current->comm, current->pid,
			__func__);
		return EFAULT;
	} else {
		DPRINT("\"%s\"[%d] %s hci command complete.\n", current->comm, current->pid,
			__func__);
	}
	
	return 0;
}

// 送信番号インクリメント
static int inc_tx(bt_ifusb_data *bt_ifusb_instance)
{
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&bt_ifusb_instance->txlock, flags);
	rv = test_bit(BTUSB_SUSPENDING, &bt_ifusb_instance->flags);
	if (!rv)
		bt_ifusb_instance->tx_in_flight++;
	spin_unlock_irqrestore(&bt_ifusb_instance->txlock, flags);

	return rv;
}

// HCIイベント受信コールバック
static void bt_ifusb_intr_complete(struct urb *urb)
{
	bt_ifusb_data *bt_ifusb_instance = urb->context;
	int err;

	DPRINT("\"%s\"[%d] %s name(%s) urb %p status %d count %d\n", current->comm, current->pid, __func__, bt_ifusb_instance->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		return;

	if (urb->status == 0) {
		if( bt_ifusb_tty_receive(HCI_COMMAND_PKT/* HCI_EVENT_PKT */,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			DPRINT("%s corrupted event packet", bt_ifusb_instance->name);
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags))
		return;

	usb_mark_last_busy(bt_ifusb_instance->udev);
	usb_anchor_urb(urb, &bt_ifusb_instance->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p failed to resubmit (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

// HCIイベント受信コールバック設定
static int bt_ifusb_submit_intr_urb(bt_ifusb_data *bt_ifusb_instance, gfp_t mem_flags)
{
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	DPRINT("\"%s\"[%d] %s name(%s)\n", current->comm, current->pid, __func__, bt_ifusb_instance->name);

	if (!bt_ifusb_instance->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(bt_ifusb_instance->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(bt_ifusb_instance->udev, bt_ifusb_instance->intr_ep->bEndpointAddress);
	DPRINT("\"%s\"[%d] %s usb_rcvintpipe(%d)\n", current->comm, current->pid, __func__, pipe);

	usb_fill_int_urb(urb, bt_ifusb_instance->udev, pipe, buf, size,
						bt_ifusb_intr_complete, bt_ifusb_instance,
						bt_ifusb_instance->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &bt_ifusb_instance->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p submission failed (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

// ACLデータ受信コールバック
static void bt_ifusb_bulk_complete(struct urb *urb)
{
	bt_ifusb_data *bt_ifusb_instance = urb->context;
	int err;

	DPRINT("\"%s\"[%d] %s name(%s) urb %p status %d count %d\n", current->comm, current->pid, __func__, bt_ifusb_instance->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		return;

	if (urb->status == 0) {
		if( bt_ifusb_tty_receive(HCI_ACLDATA_PKT,
					urb->transfer_buffer,
						urb->actual_length) < 0) {
			DPRINT("%s corrupted ACL packet", bt_ifusb_instance->name);
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &bt_ifusb_instance->flags))
		return;

	usb_anchor_urb(urb, &bt_ifusb_instance->bulk_anchor);
	usb_mark_last_busy(bt_ifusb_instance->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p failed to resubmit (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

// ACLデータ受信コールバック設定
static int bt_ifusb_submit_bulk_urb(bt_ifusb_data *bt_ifusb_instance, gfp_t mem_flags)
{
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	DPRINT("\"%s\"[%d] %s name(%s)\n", current->comm, current->pid, __func__, bt_ifusb_instance->name);

	if (!bt_ifusb_instance->bulk_rx_ep) {
		DPRINT("\"%s\"[%d] %s ENODEV\n", current->comm, current->pid, __func__);
		return -ENODEV;
	}

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb) {
		DPRINT("\"%s\"[%d] %s ENOMEM\n", current->comm, current->pid, __func__);
		return -ENOMEM;
	}

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		DPRINT("\"%s\"[%d] %s buf ENOMEM\n", current->comm, current->pid, __func__);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(bt_ifusb_instance->udev, bt_ifusb_instance->bulk_rx_ep->bEndpointAddress);
	DPRINT("\"%s\"[%d] %s usb_rcvbulkpipe(%d)\n", current->comm, current->pid, __func__, pipe);

	usb_fill_bulk_urb(urb, bt_ifusb_instance->udev, pipe,
					buf, size, bt_ifusb_bulk_complete, bt_ifusb_instance);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(bt_ifusb_instance->udev);
	usb_anchor_urb(urb, &bt_ifusb_instance->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	DPRINT("\"%s\"[%d] %s usb_submit_urb err(%d)\n", current->comm, current->pid, __func__, err);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p submission failed (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

// SCOデータ受信コールバック
static void bt_ifusb_isoc_complete(struct urb *urb)
{
	bt_ifusb_data *bt_ifusb_instance = urb->context;
	int i, err;

	if (urb->actual_length > 0)
	{
		DPRINT("\"%s\"[%d] %s name(%s) status %d count %d length %dn", current->comm, current->pid, __func__, bt_ifusb_instance->name,
						urb->status, urb->number_of_packets, urb->actual_length);
	}

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		return;

	if ((urb->status == 0)  &&  (urb->actual_length > 0)) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

		if( bt_ifusb_tty_receive(HCI_SCODATA_PKT,
						urb->transfer_buffer + offset,
								length) < 0) {
				DPRINT("%s corrupted SCO packet", bt_ifusb_instance->name);
			}
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags))
		return;

	usb_anchor_urb(urb, &bt_ifusb_instance->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p failed to resubmit (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

// SCOデータ送信時に使用
static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	DPRINT("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

// SCOデータ受信コールバック設定
static int bt_ifusb_submit_isoc_urb(bt_ifusb_data *bt_ifusb_instance, gfp_t mem_flags)
{
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	DPRINT("\"%s\"[%d] %s name(%s)\n", current->comm, current->pid, __func__, bt_ifusb_instance->name);

	if (!bt_ifusb_instance->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(bt_ifusb_instance->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(bt_ifusb_instance->udev, bt_ifusb_instance->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bt_ifusb_instance->udev, pipe, buf, size, bt_ifusb_isoc_complete,
				bt_ifusb_instance, bt_ifusb_instance->isoc_rx_ep->bInterval);

	urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			le16_to_cpu(bt_ifusb_instance->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &bt_ifusb_instance->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p submission failed (%d)",
						bt_ifusb_instance->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

// データ送信で使用
static void bt_ifusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	bt_ifusb_data *bt_ifusb_instance = (bt_ifusb_data *) skb->dev;

	DPRINT("\"%s\"[%d] %s name(%s) urb %p status %d count %d\n", current->comm, current->pid, __func__, bt_ifusb_instance->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		goto done;

done:
	spin_lock(&bt_ifusb_instance->txlock);
	bt_ifusb_instance->tx_in_flight--;
	spin_unlock(&bt_ifusb_instance->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

// データ送信で使用
static void bt_ifusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	bt_ifusb_data *bt_ifusb_instance = (bt_ifusb_data *) skb->dev;

	DPRINT("\"%s\"[%d] %s name(%s) urb %p status %d count %d\n", current->comm, current->pid, __func__, bt_ifusb_instance->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		goto done;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

// 上位からのデバイス獲得要求
int bt_ifusb_open(bt_ifusb_data *bt_ifusb_instance)
{
	int err;

	bt_ifusb_instance->name[0] = 0;
	sprintf(bt_ifusb_instance->name, "bt_ifusb");
	
	DPRINT("\"%s\"[%d] %s %s\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->name);

	err = usb_autopm_get_interface(bt_ifusb_instance->intf);
	if (err < 0) {
		DPRINT("\"%s\"[%d] %s 00\n", current->comm, current->pid,
			__func__);
		return err;
	}

	bt_ifusb_instance->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags)) {
		DPRINT("\"%s\"[%d] %s 01\n", current->comm, current->pid,
			__func__);
		goto done;
	}

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags)) {
		DPRINT("\"%s\"[%d] %s 02\n", current->comm, current->pid,
			__func__);
		goto done;
	}

	err = bt_ifusb_submit_intr_urb(bt_ifusb_instance, GFP_KERNEL);
	if (err < 0) {
		DPRINT("\"%s\"[%d] %s 03\n", current->comm, current->pid,
			__func__);
		goto failed;
	}

	down(&bt_ifusb_instance->rtl_init_sem);
	if(!bt_ifusb_instance->rtl_initializing) {
		err = bt_ifusb_submit_bulk_urb(bt_ifusb_instance, GFP_KERNEL);
		if (err < 0) {
			usb_kill_anchored_urbs(&bt_ifusb_instance->intr_anchor);
			DPRINT("\"%s\"[%d] %s 04\n", current->comm, current->pid,
				__func__);
			goto failed;
		}

		set_bit(BTUSB_BULK_RUNNING, &bt_ifusb_instance->flags);
		bt_ifusb_submit_bulk_urb(bt_ifusb_instance, GFP_KERNEL);
	}
	up(&bt_ifusb_instance->rtl_init_sem);

done:
	usb_autopm_put_interface(bt_ifusb_instance->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags);
	clear_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags);
	usb_autopm_put_interface(bt_ifusb_instance->intf);
	return err;
}

// cancel transfer requests en masse
// デバイス解放とSuspendで使用
static void bt_ifusb_stop_traffic(bt_ifusb_data *bt_ifusb_instance)
{
	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->intf);
	
	usb_kill_anchored_urbs(&bt_ifusb_instance->intr_anchor);
	down(&bt_ifusb_instance->rtl_init_sem);
	if(!bt_ifusb_instance->rtl_initializing) {
		usb_kill_anchored_urbs(&bt_ifusb_instance->bulk_anchor);
		usb_kill_anchored_urbs(&bt_ifusb_instance->isoc_anchor);
	}
	up(&bt_ifusb_instance->rtl_init_sem);
}

// 上位からのデバイス解放要求
int bt_ifusb_close(bt_ifusb_data *bt_ifusb_instance)
{
	int err;

	DPRINT("\"%s\"[%d] %s %s\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->name);

	if (!test_and_clear_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		return 0;

	cancel_work_sync(&bt_ifusb_instance->waker);

	clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
	clear_bit(BTUSB_BULK_RUNNING, &bt_ifusb_instance->flags);
	clear_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags);

	bt_ifusb_stop_traffic(bt_ifusb_instance);
	err = usb_autopm_get_interface(bt_ifusb_instance->intf);
	if (err < 0)
		goto failed;

	bt_ifusb_instance->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(bt_ifusb_instance->intf);

failed:
//unanchor all an anchor's urbs
	usb_scuttle_anchored_urbs(&bt_ifusb_instance->deferred);
	return 0;
}

// 上位からのHCIコマンド/ACLデータ/SCOデータ送信要求
int bt_ifusb_send_frame(struct sk_buff *skb, __u8 pkt_type)
{
	bt_ifusb_data *bt_ifusb_instance = usbinstance;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	DPRINT("\"%s\"[%d] %s %s udev(%p)\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->name, bt_ifusb_instance->udev);

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		return -EBUSY;

	skb->dev = (struct net_device *)bt_ifusb_instance;
	
	switch (pkt_type) {
	case HCI_COMMAND_PKT:
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = bt_ifusb_instance->cmdreq_type;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(bt_ifusb_instance->udev, 0x00);

		usb_fill_control_urb(urb, bt_ifusb_instance->udev, pipe, (void *) dr,
				skb->data, skb->len, bt_ifusb_tx_complete, skb);

		break;

	case HCI_ACLDATA_PKT:
		if (!bt_ifusb_instance->bulk_tx_ep) {
			DPRINT("\"%s\"[%d] %s HCI_ACLDATA_PKT ENODEV\n", current->comm, current->pid,
				__func__);
			return -ENODEV;
		}

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb) {
			DPRINT("\"%s\"[%d] %s HCI_ACLDATA_PKT ENOMEM\n", current->comm, current->pid,
				__func__);
			return -ENOMEM;
		}

		pipe = usb_sndbulkpipe(bt_ifusb_instance->udev,
					bt_ifusb_instance->bulk_tx_ep->bEndpointAddress);

		usb_fill_bulk_urb(urb, bt_ifusb_instance->udev, pipe,
				skb->data, skb->len, bt_ifusb_tx_complete, skb);

		break;

	case HCI_SCODATA_PKT:
		if (!bt_ifusb_instance->isoc_tx_ep || bt_ifusb_instance->sco_num < 1)
			return -ENODEV;

		urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndisocpipe(bt_ifusb_instance->udev,
					bt_ifusb_instance->isoc_tx_ep->bEndpointAddress);

		usb_fill_int_urb(urb, bt_ifusb_instance->udev, pipe,
				skb->data, skb->len, bt_ifusb_isoc_tx_complete,
				skb, bt_ifusb_instance->isoc_tx_ep->bInterval);

		urb->transfer_flags  = URB_ISO_ASAP;

		__fill_isoc_descriptor(urb, skb->len,
				le16_to_cpu(bt_ifusb_instance->isoc_tx_ep->wMaxPacketSize));

		goto skip_waking;

	default:
		return -EILSEQ;
	}

	err = inc_tx(bt_ifusb_instance);
	if (err) {
		usb_anchor_urb(urb, &bt_ifusb_instance->deferred);
		schedule_work(&bt_ifusb_instance->waker);
		err = 0;
		goto done;
	}

skip_waking:
	usb_anchor_urb(urb, &bt_ifusb_instance->tx_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		DPRINT("\"%s\"[%d] %s usb_submit_urb err\n", current->comm, current->pid,
			__func__);
		if (err != -EPERM && err != -ENODEV)
			DPRINT("%s urb %p submission failed (%d)",
						bt_ifusb_instance->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(bt_ifusb_instance->udev);
	}

done:
	usb_free_urb(urb);
	return err;
}

// SCO接続処理で使用
static inline int __set_isoc_interface(bt_ifusb_data *bt_ifusb_instance, int altsetting)
{
	struct usb_interface *intf = bt_ifusb_instance->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!bt_ifusb_instance->isoc)
		return -ENODEV;

	err = usb_set_interface(bt_ifusb_instance->udev, 1, altsetting);
	if (err < 0) {
		DPRINT("%s setting interface failed (%d)", bt_ifusb_instance->name, -err);
		return err;
	}

	bt_ifusb_instance->isoc_altsetting = altsetting;

	bt_ifusb_instance->isoc_tx_ep = NULL;
	bt_ifusb_instance->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!bt_ifusb_instance->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			bt_ifusb_instance->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!bt_ifusb_instance->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			bt_ifusb_instance->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!bt_ifusb_instance->isoc_tx_ep || !bt_ifusb_instance->isoc_rx_ep) {
		DPRINT("%s invalid SCO descriptors", bt_ifusb_instance->name);
		return -ENODEV;
	}

	return 0;
}

// 上位からの制御
// SCO接続のあり・なしが変化したときにUSBにアンカーを設定・解除
void bt_ifusb_setsco(bt_ifusb_data *bt_ifusb_instance)
{
	int new_alts;
	int err;

	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->intf);
	
	if (bt_ifusb_instance->sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &bt_ifusb_instance->flags)) {
			err = usb_autopm_get_interface(bt_ifusb_instance->isoc ? bt_ifusb_instance->isoc : bt_ifusb_instance->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
				usb_kill_anchored_urbs(&bt_ifusb_instance->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &bt_ifusb_instance->flags);
		}

		if (bt_ifusb_instance->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };
			new_alts = alts[bt_ifusb_instance->sco_num - 1];
		} else {
			new_alts = bt_ifusb_instance->sco_num;
		}

		if (bt_ifusb_instance->isoc_altsetting != new_alts) {
			clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
			usb_kill_anchored_urbs(&bt_ifusb_instance->isoc_anchor);

			if (__set_isoc_interface(bt_ifusb_instance, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags)) {
			if (bt_ifusb_submit_isoc_urb(bt_ifusb_instance, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
			else
				bt_ifusb_submit_isoc_urb(bt_ifusb_instance, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
		usb_kill_anchored_urbs(&bt_ifusb_instance->isoc_anchor);

		__set_isoc_interface(bt_ifusb_instance, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &bt_ifusb_instance->flags))
			usb_autopm_put_interface(bt_ifusb_instance->isoc ? bt_ifusb_instance->isoc : bt_ifusb_instance->intf);
	}
}

// increment a USB interface's PM-usage counter
static void bt_ifusb_waker(struct work_struct *work)
{
	bt_ifusb_data *bt_ifusb_instance = container_of(work, bt_ifusb_data, waker);
	int err;

	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, bt_ifusb_instance->intf);
	
	err = usb_autopm_get_interface(bt_ifusb_instance->intf);
	if (err < 0)
		return;

	usb_autopm_put_interface(bt_ifusb_instance->intf);
}

static int bt_ifusb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	bt_ifusb_data *bt_ifusb_instance;
	int i, err;

	DPRINT("\"%s\"[%d] %s intf %p id %p\n", current->comm, current->pid,
		__func__, intf, id);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	if (!id->driver_info) {
		const struct usb_device_id *match;
		match = usb_match_id(intf, blacklist_table);
		if (match)
			id = match;
	}

	if (id->driver_info == BTUSB_IGNORE)
		return -ENODEV;

	if (id->driver_info & BTUSB_ATH3012) {
		struct usb_device *udev = interface_to_usbdev(intf);

		/* Old firmware would otherwise let ath3k driver load
		 * patch and sysconfig files */
		if (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x0001)
			return -ENODEV;
	}

	bt_ifusb_instance = kzalloc(sizeof(*bt_ifusb_instance), GFP_KERNEL);
	if (!bt_ifusb_instance)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!bt_ifusb_instance->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			bt_ifusb_instance->intr_ep = ep_desc;
			continue;
		}

		if (!bt_ifusb_instance->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			bt_ifusb_instance->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!bt_ifusb_instance->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			bt_ifusb_instance->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!bt_ifusb_instance->intr_ep || !bt_ifusb_instance->bulk_tx_ep || !bt_ifusb_instance->bulk_rx_ep) {
		kfree(bt_ifusb_instance);
		return -ENODEV;
	}

	bt_ifusb_instance->cmdreq_type = USB_TYPE_CLASS;

	bt_ifusb_instance->udev = interface_to_usbdev(intf);
	bt_ifusb_instance->intf = intf;

	INIT_WORK(&bt_ifusb_instance->waker, bt_ifusb_waker);
	spin_lock_init(&bt_ifusb_instance->txlock);

	init_usb_anchor(&bt_ifusb_instance->tx_anchor);
	init_usb_anchor(&bt_ifusb_instance->intr_anchor);
	init_usb_anchor(&bt_ifusb_instance->bulk_anchor);
	init_usb_anchor(&bt_ifusb_instance->isoc_anchor);
	init_usb_anchor(&bt_ifusb_instance->deferred);

	/* Interface numbers are hardcoded in the specification */
	bt_ifusb_instance->isoc = usb_ifnum_to_if(bt_ifusb_instance->udev, 1);

	if (id->driver_info & BTUSB_BROKEN_ISOC)
		bt_ifusb_instance->isoc = NULL;

	if (bt_ifusb_instance->isoc) {
		err = usb_driver_claim_interface(&bt_ifusb_driver,
							bt_ifusb_instance->isoc, bt_ifusb_instance);
		if (err < 0) {
			kfree(bt_ifusb_instance);
			return err;
		}
	}
	
	sema_init(&bt_ifusb_instance->rtl_init_sem, 1);
	
#ifdef CONFIG_REALTEK
	/*  Realtek */
	if(id->driver_info & BTUSB_REALTEK)
	{
		down(&bt_ifusb_instance->rtl_init_sem);
		bt_ifusb_instance->rtl_initializing = 1;
		up(&bt_ifusb_instance->rtl_init_sem);
		/* Realtek devices lose their updated firmware over suspend,
		 * but the USB hub doesn't notice any status change.
		 * Explicitly request a device reset on resume.
		 */

#ifdef TEST_UBUNTU
		interface_to_usbdev(intf)->quirks |= USB_QUIRK_RESET_RESUME;
#else
		set_bit(BTUSB_RESET_RESUME, &bt_ifusb_instance->flags);
#endif
	}
#endif //#ifdef CONFIG_REALTEK

	usbinstance = bt_ifusb_instance;
	bt_ifusb_tty_init();
	
	usb_set_intfdata(intf, bt_ifusb_instance);

#ifdef CONFIG_REALTEK
	/*  Realtek */
	if(id->driver_info & BTUSB_REALTEK)
	{
		INIT_WORK(&bt_ifusb_instance->init_rtl, bt_ifusb_setup_realtek);
		init_waitqueue_head(&bt_ifusb_instance->hci_cmd_waitqueue);

		do
		{
			bt_ifusb_instance->workqueue = alloc_workqueue("%s", WQ_HIGHPRI | WQ_UNBOUND |
							  WQ_MEM_RECLAIM | WQ_FREEZABLE, 1, bt_ifusb_driver.name);
			
			DPRINT("\"%s\"[%d] %s alloc_workqueue %p\n", current->comm, current->pid,
				__func__, bt_ifusb_instance->workqueue);
			if (!bt_ifusb_instance->workqueue) {
				break;
			}
			
			queue_work(bt_ifusb_instance->workqueue, &bt_ifusb_instance->init_rtl);
		}
		while(0);
	}
#endif //#ifdef CONFIG_REALTEK
	
	DPRINT("bt_ifusb driver rev. %s\n", VERSION_BT_IFUSB);

	return 0;
}

static void bt_ifusb_disconnect(struct usb_interface *intf)
{
	bt_ifusb_data *bt_ifusb_instance = usb_get_intfdata(intf);

	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, intf);
	
	bt_ifusb_tty_cleanup();
	
	if (!bt_ifusb_instance)
		return;

	usb_set_intfdata(bt_ifusb_instance->intf, NULL);

	if (bt_ifusb_instance->isoc)
		usb_set_intfdata(bt_ifusb_instance->isoc, NULL);

	usbinstance = NULL;
	
	if (intf == bt_ifusb_instance->isoc)
		usb_driver_release_interface(&bt_ifusb_driver, bt_ifusb_instance->intf);
	else if (bt_ifusb_instance->isoc)
		usb_driver_release_interface(&bt_ifusb_driver, bt_ifusb_instance->isoc);
	
	kfree(bt_ifusb_instance);
}

#ifdef CONFIG_PM
static int bt_ifusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	bt_ifusb_data *bt_ifusb_instance = usb_get_intfdata(intf);

	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, intf);

	if (bt_ifusb_instance->suspend_count++)
		return 0;

	spin_lock_irq(&bt_ifusb_instance->txlock);
	if (!(PMSG_IS_AUTO(message) && bt_ifusb_instance->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &bt_ifusb_instance->flags);
		spin_unlock_irq(&bt_ifusb_instance->txlock);
	} else {
		spin_unlock_irq(&bt_ifusb_instance->txlock);
		bt_ifusb_instance->suspend_count--;
		return -EBUSY;
	}

	bt_ifusb_stop_traffic(bt_ifusb_instance);
	usb_kill_anchored_urbs(&bt_ifusb_instance->tx_anchor);

#ifdef TEST_UBUNTU
	;
#else
	/* Optionally request a device reset on resume, but only when
	 * wakeups are disabled. If wakeups are enabled we assume the
	 * device will stay powered up throughout suspend.
	 */
	if (test_bit(BTUSB_RESET_RESUME, &bt_ifusb_instance->flags) &&
	    !device_may_wakeup(&bt_ifusb_instance->udev->dev))
		bt_ifusb_instance->udev->reset_resume = 1;
#endif

	return 0;
}

static void play_deferred(bt_ifusb_data *bt_ifusb_instance)
{
	struct urb *urb;
	int err;
	DPRINT("\"%s\"[%d] %s\n", current->comm, current->pid,
		__func__);

	while ((urb = usb_get_from_anchor(&bt_ifusb_instance->deferred))) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0)
			break;

		bt_ifusb_instance->tx_in_flight++;
	}
	usb_scuttle_anchored_urbs(&bt_ifusb_instance->deferred);
}

static int bt_ifusb_resume(struct usb_interface *intf)
{
	bt_ifusb_data *bt_ifusb_instance = usb_get_intfdata(intf);
	int err = 0;

	DPRINT("\"%s\"[%d] %s intf %p\n", current->comm, current->pid,
		__func__, intf);

	if (--bt_ifusb_instance->suspend_count)
		return 0;

	if (!test_bit(HCI_RUNNING, &bt_ifusb_instance->hciflags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags)) {
		err = bt_ifusb_submit_intr_urb(bt_ifusb_instance, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &bt_ifusb_instance->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &bt_ifusb_instance->flags)) {
		err = bt_ifusb_submit_bulk_urb(bt_ifusb_instance, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &bt_ifusb_instance->flags);
			goto failed;
		}

		bt_ifusb_submit_bulk_urb(bt_ifusb_instance, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags)) {
		if (bt_ifusb_submit_isoc_urb(bt_ifusb_instance, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &bt_ifusb_instance->flags);
		else
			bt_ifusb_submit_isoc_urb(bt_ifusb_instance, GFP_NOIO);
	}

	spin_lock_irq(&bt_ifusb_instance->txlock);
	play_deferred(bt_ifusb_instance);
	clear_bit(BTUSB_SUSPENDING, &bt_ifusb_instance->flags);
	spin_unlock_irq(&bt_ifusb_instance->txlock);

	return 0;

failed:
	usb_scuttle_anchored_urbs(&bt_ifusb_instance->deferred);
done:
	spin_lock_irq(&bt_ifusb_instance->txlock);
	clear_bit(BTUSB_SUSPENDING, &bt_ifusb_instance->flags);
	spin_unlock_irq(&bt_ifusb_instance->txlock);

	return err;
}
#endif

static struct usb_driver bt_ifusb_driver = {
	.name		= "bt_ifusb",
	.probe		= bt_ifusb_probe,
	.disconnect	= bt_ifusb_disconnect,
#ifdef CONFIG_PM
	.suspend	= bt_ifusb_suspend,
	.resume		= bt_ifusb_resume,
#endif
	.id_table	= bt_ifusb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(bt_ifusb_driver);

MODULE_DESCRIPTION("Toshiba Information System Bluetooth USB-serial driver");
MODULE_VERSION(VERSION_BT_IFUSB);
MODULE_LICENSE("GPL");


