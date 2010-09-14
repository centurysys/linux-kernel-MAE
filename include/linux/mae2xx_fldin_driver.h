/*
 * mae2xx_fldin_driver.h: Magnolia2 an expansion FL-net card driver (FL-din)
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

#ifndef _MAE2XX_FLDIN_DRIVER_H
#define _MAE2XX_FLDIN_DRIVER_H

#ifdef __KERNEL__

/* proc entry name */
#define FLDIN_PROC_DIR	"driver/fldin"

/* memory map, base address is fldin_extio_resource */
/* reference to magnolia2.c                         */
#define DIN_IRQ_CR	0x00	/* din irq control */
#define DIN_ST		0x01	/* din status */

/* DIN_IRQ_CR */
#define FLDIN_IRQ_MASK		0x01
#define FLDIN_IRQ_ENABLE	0x01	/* 0:disable, 1:enable */
#define FLDIN_IRQ_DISABLE	0x00	/* 0:disable, 1:enable */

/* DIN_ST */
#define FLDIN_DIN_MASK		0x01
#define FLDIN_DIN_OFF		0x01	/* 0:on, 1:off */
#define FLDIN_DIN_ON		0x00	/* 0:on, 1:off */

/* address macro */
#define ADDR_HIGH(addr)	((u8)((addr >> 8) & 0x000000ff))
#define ADDR_LOW(addr)	((u8)(addr & 0x000000ff))

/* device resource */
struct fldin_resource {
	struct resource *res;
	u8 *ioaddr;
};

#endif /* __KERNEL__ */

#define CHARDEV_IOCTL_MAGIC  0xA5
#define FLDIN_READ		_IOR(CHARDEV_IOCTL_MAGIC, 1, void*)

#endif /* _MAE2XX_FLDIN_DRIVER_H */
