/*
 * mae2xx_flnet_driver.h: Magnolia2 an expansion FL-net card driver (FL-net)
 *
 * Copyright
 * Author: 2010 Century Systems Co.,Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MAE2XX_FLNET_DRIVER_H
#define _MAE2XX_FLNET_DRIVER_H

#include <linux/ioctl.h>

#ifdef __KERNEL__

/* memory map, base address is flnet_extio_resource */
/* reference to magnolia2.c                         */
#define FLNET_CR	0x00	/* FL-net card contorl, reset and download mode */
#define BOARD_ID	0x01	/* board id, revision */
#define DPM_HAD		0x08	/* dual-port ram access address (high) */
#define DPM_LAD		0x09	/* dual-port ram access address (low)  */
#define DPM_DAT		0x0a	/* dual-port ram read or write data    */
#define SEM_DAT		0x0b	/* dual-port ram semaphore register    */

/* FLNET_CR */
#define FLNET_RESET	0x02	/* 0:reset, 1:normal */
#define FLNET_MODE	0x01	/* 0:normal, 1:download */

/* semaphore */
#define SEM_ADDR	0x0000	/* semaphore 0 address */
#define SEM_MASK	0x01	/* semaphore mask */
#define SEM_REQ		0x00	/* request command */
#define SEM_REL		0x01	/* release command */
#define SEM_GOT		0x00	/* when semaphore was got, then bit 0 is 0 */

/* address macro */
#define ADDR_HIGH(addr)	((u8)((addr >> 8) & 0x000000ff))
#define ADDR_LOW(addr)	((u8)(addr & 0x000000ff))


/* device resource */
struct flnet_resource {
	struct resource *res;
	u8 *ioaddr;
};

#endif /* __KERNEL__ */


/* ioctl command */
#define CHARDEV_IOCTL_MAGIC  0xA4
#define FLNET_WRITE		_IOW(CHARDEV_IOCTL_MAGIC, 1, void*)
#define FLNET_WRITE_SEM _IOW(CHARDEV_IOCTL_MAGIC, 2, void*)
#define FLNET_READ		_IOR(CHARDEV_IOCTL_MAGIC, 3, void*)
#define FLNET_READ_SEM	_IOR(CHARDEV_IOCTL_MAGIC, 4, void*)
#define FLNET_SET_RESET	_IOW(CHARDEV_IOCTL_MAGIC, 5, void*)
#define FLNET_GET_RESET	_IOR(CHARDEV_IOCTL_MAGIC, 6, void*)
#define FLNET_SET_CS0	_IOW(CHARDEV_IOCTL_MAGIC, 7, void*)
#define FLNET_GET_CS0	_IOR(CHARDEV_IOCTL_MAGIC, 8, void*)


/* api parameters */
struct flwrite {
	int offset;
	int size;
	unsigned char *buff;
};

struct flread {
	int offset;
	int size;
	unsigned char *buff;
};

#define FLNET_MIN_OFFSET 	0x0000
#define FLNET_MAX_OFFSET	0xffff
#define FLNET_MIN_SIZE	 	0x0001
#define FLNET_MAX_SIZE		0x10000

#define FLNET_RESET_ON		0
#define FLNET_RESET_OFF		1
#define FLNET_CS0_NORMAL	0
#define FLNET_CS0_DOWNLOAD	1

#endif /* _MAE2XX_FLNET_DRIVER_H */
