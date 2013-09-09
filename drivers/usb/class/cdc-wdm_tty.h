#ifndef CDC_WDM_H
#define CDC_WDM_H

#include "cdc-acm.h"

/* --- device descriptor --- */

/*
 * The only reason to have several buffers is to accomodate assumptions
 * in line disciplines. They ask for empty space amount, receive our URB size,
 * and proceed to issue several 1-character writes, assuming they will fit.
 * The very first write takes a complete URB. Fortunately, this only happens
 * when processing onlcr, so we only need 2 buffers. These values must be
 * powers of 2.
 */
#define WDM_NW  2
#define WDM_NR  16

/*
 * Major and minor numbers.
 */

#define WDM_TTY_MAJOR		ACM_TTY_MAJOR
#define WDM_TTY_MINORS		32

struct wdm_wb {
	unsigned char *buf;
	dma_addr_t dmah;
	int len;
	int use;
};

struct wdm_rb {
	struct list_head	list;
	int			size;
	unsigned char		*base;
	dma_addr_t		dma;
};

struct wdm_ru {
	struct list_head	list;
	struct acm_rb		*buffer;
	struct urb		*urb;
	struct acm		*instance;
};

struct wdm_device {
	struct usb_device       *dev;		/* the corresponding usb device */
	struct tty_struct       *tty;           /* the corresponding tty */

	u8			*inbuf;         /* buffer for response */
	u8			*sbuf;          /* buffer for status */
	dma_addr_t		ihandle;
	dma_addr_t		shandle;

        struct wdm_wb           wb[WDM_NW];

	struct urb		*command;
	struct urb		*response;
	struct urb		*notify;

	struct usb_interface	*intf;
	struct work_struct	*rxwork;
	struct work_struct	*txwork;
	struct usb_ctrlrequest	*out_req;
	struct usb_ctrlrequest	*in_req;

	spinlock_t              write_lock;
	int                     write_current;		/* current write buffer */
	int                     write_used;		/* number of non-empty write buffers */
	int                     write_ready;            /* write urb is not running */

	struct usb_cdc_line_coding line;		/* bits, stop, parity */
	struct                  work_struct work;	/* work queue entry for line discipline waking up */
	spinlock_t              throttle_lock;		/* synchronize throtteling and read callback */
	unsigned int            ctrlin;			/* input control lines (DCD, DSR, RI, break, overruns) */
	unsigned int            ctrlout;		/* output control lines (DTR, RTS) */

	unsigned long		flags;
	u16			wMaxCommand;
	__le16			ifnum;

	unsigned int            minor;		/* whm minor number */
	unsigned int            used;		/* someone has this acm's device open */
	unsigned char           throttle;	/* throttled by tty layer */
	unsigned char           clocal; 	/* termios CLOCAL */
};

/* --- table of supported interfaces ---*/

#define WDM_MINOR_BASE 32

/* --- flags --- */
#define WDM_IN_USE		1
#define WDM_DISCONNECTING	2
#define WDM_RESULT		3
#define WDM_READ		4
#define WDM_INT_STALL		5
#define WDM_POLL_RUNNING	6

/* --- misc --- */
#define WDM_MAX 16

struct usb_cdc_dmm_header {
	__u8	bFunctionLength;
	__u8	bDescriptorType;
	__u8	bDescriptorSubtype;
	__u16	bcdVersion;
	__le16	wMaxCommand;
} __attribute__ ((packed));

#ifndef err
#define err(format, arg...) printk(KERN_ERR KBUILD_MODNAME ": " \
	format "\n" , ## arg)
#endif

#define info(format, arg...) printk(KERN_INFO KBUILD_MODNAME ": " \
	format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING KBUILD_MODNAME ": " \
	format "\n" , ## arg)

#endif
