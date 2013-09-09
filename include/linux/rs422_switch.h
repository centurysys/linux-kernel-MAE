/*
 * Definitions for the Magnolia2 RS-422 Switch device.
 *
 * Copyright (C) 2011 Century Systems Co.,Ltd.
 *
 * Author: Takeyoshi Kikuchi  <kikuchi@centurysys.co.jp>
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

#ifndef _MAGNOLIA2_RS422_SWITCH_H
#define _MAGNOLIA2_RS422_SWITCH_H

#include <linux/ioctl.h>

#ifdef __KERNEL__

#endif /* __KERNEL__ */

struct rs422_port_status {
	int port_a_ok;
	int port_b_ok;
	int port_op;
};

#define RS422_SWITCH_IOC_MAGIC  'r'
#define RS422_IOC_GET_STATUS	_IOR(RS422_SWITCH_IOC_MAGIC, 0, struct rs422_port_status)
#define RS422_IOC_SELECT_PORT	_IOW(RS422_SWITCH_IOC_MAGIC, 1, int)
#define RS422_SWITCH_IOC_MAXNR	1

#endif /* _MAGNOLIA2_RS422_SWITCH_H */
