/*
 * Definitions for the MA-E2xx UM03-KO Ext-IO device.
 *
 * Copyright (C) 2010-2013 Century Systems Co.,Ltd.
 *
 * Author: Takeyoshi Kikuchi  <kikuchi@centurysys.co.jp>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE  IS PROVIDED ``AS  IS'' AND ANY  EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO  EVENT  SHALL   THE AUTHOR  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN	CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MAE2XX_UM03KO_EXTIO_H
#define _MAE2XX_UM03KO_EXTIO_H

#include <linux/ioctl.h>

#ifdef __KERNEL__

/* Registers */
#define POWER_CTRL   0x00
#define BOARD_STATUS 0x01
#define FOMA_CTRL    0x02
#define FOMA_STATUS  0x04
#define FOMA_MONITOR 0x05

/* FOMA Control register */
#define FOMA_CTRL_PWRKEY_OFF	1
#define FOMA_CTRL_PWRKEY_ON	0

struct mae2xx_um03ko_extio {
	struct resource *res;
	u8 *base;
	u8 *ioaddr;
};

/* Offset 0x00: Power Control register */
union power_ctrl {
	struct {
		u8 pow_off:1;
		u8 reserved:7;
	} bit;
	u8 byte;
};

/* Offset 0x01: Board Status register */
union board_status {
	struct {
		u8 revision:4;
		u8 id:4;
	} bit;
	u8 byte;
};

/* Offset 0x02: FOMA Control register */
union foma_ctrl {
	struct {
		u8 pwrkey_on:1;
		u8 pwrkey_off:1;
		u8 reserved2:2;
		u8 sysrst:1;
		u8 reserved1:3;
	} bit;
	u8 byte;
};

/* Offset 0x04: FOMA Status register */
union foma_status {
	struct {
		u8 sim_cd:1;
		u8 reserved2:3;
		u8 xdr:1;
		u8 xcs:1;
		u8 reserved1:2;
	} bit;
	u8 byte;
};

/* Offset 0x05: FOMA Monitor Status register */
union foma_monitor {
	struct {
		u8 cbs_etws:1;
		u8 sms:1;
		u8 adl:1;
		u8 ledgcmd:1;
		u8 ledg:1;
		u8 trx:1;
		u8 ant2:1;
		u8 ant1:1;
	} bit;
	u8 byte;
};

#endif /* __KERNEL__ */

#define MAE2XX_EXTIO_IOC_MAGIC	'u'
#define MAE2XX_EXTIO_IOCSPWRKEY _IOW(MAE2XX_EXTIO_IOC_MAGIC, 0, __u8)
#define MAE2XX_EXTIO_IOCGPWRKEY _IOR(MAE2XX_EXTIO_IOC_MAGIC, 1, __u8)
#define MAE2XX_EXTIO_IOCRESET	_IO(MAE2XX_EXTIO_IOC_MAGIC, 2)
#define MAE2XX_EXTIO_IOCGSTATUS _IOW(MAE2XX_EXTIO_IOC_MAGIC, 3, __u8)
#define MAE2XX_EXTIO_IOC_MAXNR	3


#endif /* _MAE2XX_UM03KO_EXTIO_H */
