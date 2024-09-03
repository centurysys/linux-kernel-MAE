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

#include <linux/tty.h>

// for flip buffer
#include <linux/tty_flip.h>

// for modem info
#include <linux/serial.h>
#include <linux/serial_reg.h>

// for tasklet
#include <linux/interrupt.h>

#include "bt_ifusb.h"

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DPRINT printk
#else
#define DPRINT(fmt, args...)
#endif

/* #define BT_IFSPP_TTY_DEBUG_DUMP */

/*****************************************************************************/
/*                              Type definitions                             */
/*****************************************************************************/

#define BT_IFSPP_TTY_MAJOR 120	/* static major */
#define BT_IFSPP_TTY_MINOR 20

#define BT_IFSPP_TTY_NR_DEVS 3
#define BT_IFSPP_TTY_LOWER_MINOR_START BT_IFSPP_TTY_NR_DEVS

#define BT_IFSPP_TTY_DRIVER_MAGIC TTY_DRIVER_MAGIC

typedef struct PORT_CTRL {
	struct tty_struct *tty;
	int state;
	struct semaphore open_close_sem;
	struct semaphore write_sem;
	unsigned char *w_buf;
	struct file *fp;
} Port_ctrl;

typedef struct DEV_CTRL {
	unsigned char MCR;
	unsigned char MSR;
} Dev_ctrl;

enum bt_ifusb_tty_port_states {
	BT_IFSPP_TTY_INACTIVE,
	BT_IFSPP_TTY_ACTIVE
};

extern bt_ifusb_data *usbinstance;

#define GET_PORT(tty) (tty->index)

/*****************************************************************************/
/*                                 Prototypes                                */
/*****************************************************************************/

static int		bt_ifusb_tty_open(struct tty_struct *tty, struct file *filp);
static void 	bt_ifusb_tty_close(struct tty_struct *tty, struct file *filp);
static int		bt_ifusb_tty_write(struct tty_struct *tty,
							   const unsigned char *buf, int count);
static unsigned int		bt_ifusb_tty_write_room(struct tty_struct *tty);
static int	   	bt_ifusb_tty_put_char(struct tty_struct *tty, unsigned char ch);
static int		bt_ifusb_tty_port_init(void);

static struct	tty_operations bt_ifusb_tty_operations = {
	.open			= bt_ifusb_tty_open,
	.close			= bt_ifusb_tty_close,
	.write			= bt_ifusb_tty_write,
	.write_room 	= bt_ifusb_tty_write_room,
	.set_termios	= NULL,
	.put_char		= bt_ifusb_tty_put_char,
	.chars_in_buffer = NULL,
	.flush_buffer	= NULL,
	.ioctl			= NULL,
	.stop			= NULL,
	.start			= NULL,
	.hangup 		= NULL,
	.tiocmget		= NULL,
	.tiocmset		= NULL,
};

/*****************************************************************************/
/*                               To be delete                                */
/*****************************************************************************/

static Port_ctrl port_ctrl[BT_IFSPP_TTY_NR_DEVS];
static Dev_ctrl  dev_ctrl[BT_IFSPP_TTY_NR_DEVS];
static struct tty_driver *bt_ifusb_tty_driver = NULL;
static struct tty_port *bt_ifusb_ports = NULL;


/*****************************************************************************/
/*                                 Functions                                 */
/*****************************************************************************/

void bt_ifusb_tty_lock_port_ctrl(int port)
{
	down(&port_ctrl[port].open_close_sem);
	port_ctrl[port].state = BT_IFSPP_TTY_ACTIVE;
	up(&port_ctrl[port].open_close_sem);
}

void bt_ifusb_tty_unlock_port_ctrl(int port)
{
	down(&port_ctrl[port].open_close_sem);
	port_ctrl[port].state = BT_IFSPP_TTY_INACTIVE;
	up(&port_ctrl[port].open_close_sem);
}

static bool bt_ifusb_tty_chk_opened(void)
{
	int port;
	for(port = 0;  port<BT_IFSPP_TTY_NR_DEVS;  port++)
	{
		if( port_ctrl[port].state == BT_IFSPP_TTY_ACTIVE )
		{
			return true;
		}
	}
	return false;
}

static int bt_ifusb_tty_open(struct tty_struct *tty, struct file *filp)
{
	int port = GET_PORT(tty);

	DPRINT("\"%s\"[%d] %s port(%d) tty(%p)\n", current->comm, current->pid,
		__func__, port, tty);
	
	if(!usbinstance)
	{
		return -ENODEV;
	}
	
	down(&usbinstance->rtl_init_sem);
	if(usbinstance->rtl_initializing)
	{
		up(&usbinstance->rtl_init_sem);
		DPRINT("\"%s\"[%d]     rtl initializing\n",
			current->comm,current->pid);
		return -EBUSY;
	}
	up(&usbinstance->rtl_init_sem);
	
	down(&port_ctrl[port].open_close_sem);
	if(port_ctrl[port].fp)
	{
		up(&port_ctrl[port].open_close_sem);
		DPRINT("\"%s\"[%d]     tty port is used by another user\n",
			current->comm,current->pid);
		return -EBUSY;
	}

	port_ctrl[port].fp = filp;

	if(!bt_ifusb_tty_chk_opened())
	{
		bt_ifusb_open(usbinstance);
	}
	up(&port_ctrl[port].open_close_sem);
	
	if(port_ctrl[port].state == BT_IFSPP_TTY_INACTIVE){
		down(&port_ctrl[port].open_close_sem);
		tty->driver_data = NULL;
		port_ctrl[port].tty = tty;
		port_ctrl[port].state = BT_IFSPP_TTY_ACTIVE;
		if(port == 2){
			usbinstance->sco_num = 1;
			bt_ifusb_setsco(usbinstance);
		}
		up(&port_ctrl[port].open_close_sem);
	}
	else{
		DPRINT("\"%s\"[%d]     port busy\n",
			current->comm,current->pid);
		return -EBUSY;
	}
	return 0;
}

static void bt_ifusb_tty_close(struct tty_struct *tty, struct file *filp)
{
	int port = GET_PORT(tty);

	DPRINT("\"%s\"[%d] %s port(%d)\n", current->comm, current->pid,
		__func__, port);

	if(!usbinstance)
	{
		return;
	}
	
	down(&usbinstance->rtl_init_sem);
	if(usbinstance->rtl_initializing)
	{
		up(&usbinstance->rtl_init_sem);
		DPRINT("\"%s\"[%d]     rtl initializing\n",
			current->comm,current->pid);
		return;
	}
	up(&usbinstance->rtl_init_sem);
	
	down(&port_ctrl[port].open_close_sem);
	if(port_ctrl[port].fp == NULL)
	{
		up(&port_ctrl[port].open_close_sem);
		DPRINT("\"%s\"[%d]     port is not open\n",
			current->comm,current->pid);
		return;
	}
	if(port_ctrl[port].fp != filp)
	{
		up(&port_ctrl[port].open_close_sem);
		DPRINT("\"%s\"[%d]     tty port is used by another user\n",
			current->comm,current->pid);
		return;
	}
	up(&port_ctrl[port].open_close_sem);
	
	down(&port_ctrl[port].open_close_sem);
	port_ctrl[port].state = BT_IFSPP_TTY_INACTIVE;
	port_ctrl[port].tty = NULL;
	port_ctrl[port].fp = NULL;
	if(port == 2){
		usbinstance->sco_num = 0;
		bt_ifusb_setsco(usbinstance);
	}
	if(!bt_ifusb_tty_chk_opened())
	{
		bt_ifusb_close(usbinstance);
	}
	up(&port_ctrl[port].open_close_sem);
}



int bt_ifusb_tty_receive(int type,
						  const unsigned char *buf, int count)
{
	int port = type - 1;
	struct tty_struct *tty = port_ctrl[port].tty;
	int send_c;
	int i;
	
	DPRINT("\"%s\"[%d] %s type(%d) start\n", current->comm, current->pid,
		__func__, type);

	if(!usbinstance)
	{
		return -ENODEV;
	}
	down(&usbinstance->rtl_init_sem);
	if(usbinstance->rtl_initializing)
	{
		up(&usbinstance->rtl_init_sem);
		return bt_ifusb_internal_receive(type, buf, count);
	}
	up(&usbinstance->rtl_init_sem);

	if(port_ctrl[port].state == BT_IFSPP_TTY_ACTIVE){
		send_c = tty_buffer_request_room(tty->port, count);
		send_c = min(count, send_c);
		
		for(i=0;i<send_c;i++){
			tty_insert_flip_char(tty->port, buf[i], 0);
		}
		tty_flip_buffer_push(tty->port);
#ifdef BT_IFSPP_TTY_DEBUG_DUMP
		printk(KERN_DEBUG);
		for(i=0;i<send_c;i++){
			if(!(i % 16)){
				printk("\n");
				printk(KERN_DEBUG);
			}
			printk("%02X ", buf[i]);
		}
		printk("\n");
#endif
	
	DPRINT("\"%s\"[%d] receive(%d) [%d]bytes\n",
					   current->comm,current->pid,port,
					   send_c);
	}
	else{
		DPRINT("\"%s\"[%d] port(%d) is not active\n",
					   current->comm,current->pid,port);
		return -EPIPE;
	}
	return send_c;
}

static int bt_ifusb_tty_write(struct tty_struct *tty,
						  const unsigned char *buf, int count)
{
	int port = GET_PORT(tty);
	int from_user = 0;
	struct sk_buff *skb;

	DPRINT("\"%s\"[%d] %s port(%d) data_size(%d) start\n", current->comm, current->pid,
		__func__, port, count);

	// ユーザー領域からの書き込み（⇔slipなど）
	from_user = access_ok(buf, count);
	
	DPRINT("\"%s\"[%d] %s from_user(%d)\n", current->comm, current->pid,
		__func__, from_user);
	skb = alloc_skb(count, GFP_ATOMIC);
	
	if(!skb)
	{
		return -ENOMEM;
		DPRINT("\"%s\"[%d] %s no skb\n", current->comm, current->pid,
			__func__);
	}
	
	DPRINT("\"%s\"[%d] %s skb(%p)\n", current->comm, current->pid,
		__func__, skb);
	if(from_user)
	{
		if(copy_from_user(skb_put(skb, count), buf, count))
		{
			return -EFAULT;
		}
		DPRINT("\"%s\"[%d] %s copy_from_user\n", current->comm, current->pid,
			__func__);
	}
	else
	{
		DPRINT("\"%s\"[%d] %s memcpy skb(%p) count(%d), buf(%p)\n", current->comm, current->pid,
			__func__, skb, count, buf);
		
		memcpy(skb_put(skb, count), buf, count);
	}
	
	down(&port_ctrl[port].write_sem);
	bt_ifusb_send_frame( skb, port+1 );
	up(&port_ctrl[port].write_sem);
	return count;
}

static int bt_ifusb_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
#ifdef DEBUG_PRINT
	int port = GET_PORT(tty);
#endif

	DPRINT("\"%s\"[%d] %s port(%d)\n", current->comm, current->pid,
		__func__, port);

	return bt_ifusb_tty_write(tty, &ch, 1);
}

static unsigned int bt_ifusb_tty_write_room(struct tty_struct *tty)
{
	return 1024;
}

static int bt_ifusb_tty_port_init(void)
{
	int i;

	DPRINT("\"%s\"[%d] %s\n", current->comm, current->pid, __func__);

	for(i=0; i<BT_IFSPP_TTY_NR_DEVS; i++){
		port_ctrl[i].tty = NULL;
		port_ctrl[i].state = BT_IFSPP_TTY_INACTIVE;
		sema_init(&port_ctrl[i].open_close_sem, 1);
		sema_init(&port_ctrl[i].write_sem, 1);
		port_ctrl[i].fp = NULL;

		dev_ctrl[i].MCR = UART_MCR_DTR | UART_MCR_RTS;
		dev_ctrl[i].MSR = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
	}

	return 0;
}

void bt_ifusb_tty_cleanup(void)
{
	DPRINT("\"%s\"[%d] %s\n", current->comm, current->pid, __func__);

	if (bt_ifusb_tty_driver != NULL) {
		tty_unregister_driver(bt_ifusb_tty_driver);
		tty_driver_kref_put(bt_ifusb_tty_driver);
		bt_ifusb_tty_driver = NULL;
	}
	
	if(bt_ifusb_ports != NULL)
	{
		kfree(bt_ifusb_ports);
		bt_ifusb_ports = NULL;
	}
}

int bt_ifusb_tty_init(void)
{
	int result, i;

	DPRINT("\"%s\"[%d] %s\n", current->comm, current->pid, __func__);

	/* Initialise the tty_driver structure */
	if (bt_ifusb_tty_driver == NULL) {
		bt_ifusb_tty_driver = tty_alloc_driver(BT_IFSPP_TTY_NR_DEVS, 0);
		if (bt_ifusb_tty_driver == NULL) {
				return -ENOMEM;
		}
	}
	if(bt_ifusb_ports == NULL)
	{
		bt_ifusb_ports = kcalloc(BT_IFSPP_TTY_NR_DEVS, sizeof(struct tty_port), GFP_KERNEL);
	}

	bt_ifusb_tty_driver->owner			 = THIS_MODULE;
	bt_ifusb_tty_driver->magic			 = BT_IFSPP_TTY_DRIVER_MAGIC;
	bt_ifusb_tty_driver->driver_name	 = "ttyBTUSB";
	bt_ifusb_tty_driver->name			 = "ttyBTUSB";
	bt_ifusb_tty_driver->major			 = BT_IFSPP_TTY_MAJOR;
	bt_ifusb_tty_driver->minor_start	 = BT_IFSPP_TTY_MINOR;
	bt_ifusb_tty_driver->num			 = BT_IFSPP_TTY_NR_DEVS;
	bt_ifusb_tty_driver->type			 = TTY_DRIVER_TYPE_SERIAL;
	bt_ifusb_tty_driver->subtype		 = SERIAL_TYPE_NORMAL;
	bt_ifusb_tty_driver->init_termios	 = tty_std_termios;
	bt_ifusb_tty_driver->init_termios.c_iflag = 0;
	bt_ifusb_tty_driver->init_termios.c_cflag = B9600|CS8|CREAD|HUPCL|CLOCAL;
	bt_ifusb_tty_driver->init_termios.c_oflag = 0;
	bt_ifusb_tty_driver->init_termios.c_lflag = 0;
	bt_ifusb_tty_driver->flags			 = TTY_DRIVER_REAL_RAW;

	/* Manually assigns each port to tty_driver. */
	for (i = 0; i < BT_IFSPP_TTY_NR_DEVS; i++) {
		tty_port_init(bt_ifusb_ports + i);
		tty_port_link_device(bt_ifusb_ports + i, bt_ifusb_tty_driver, i);
	}

	tty_set_operations(bt_ifusb_tty_driver, &bt_ifusb_tty_operations);

	result = tty_register_driver(bt_ifusb_tty_driver);
	if(result){
		DPRINT("Can't register driver\n");
		tty_driver_kref_put(bt_ifusb_tty_driver);
				bt_ifusb_tty_driver = NULL;
		return result;
	}

	if((result = bt_ifusb_tty_port_init())){
		bt_ifusb_tty_cleanup();
		return result;
	}

	return 0;	/* succeed */
}
