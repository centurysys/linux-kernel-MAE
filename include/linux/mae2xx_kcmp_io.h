/*
 * ma2xx_kcmp_io.h: Definitions for Magnolia2 KCMP-IO Control driver
 *
 * Copyright
 * Author: 2012 Century Systems Co.,Ltd.
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

#ifndef _MAE2XX_KCMP_IO_H
#define _MAE2XX_KCMP_IO_H

/* Offset 0x00: KCMP control register (R/W) */
#define REG_KCMP_CONTROL 0x00
union reg_kcmp_control {
	struct {
		u8 power_switch:1; /* 0:Power OFF, 1:Power ON */
		u8 reserved:4;
		u8 petp0:1;
		u8 w_disable:1; /* 0:Card disable, 1:Card enable */
		u8 perst:1; /* 0:Reset ON, 1:Reset Off */
	} bit;
	u8 byte;
};

/* Offset 0x01: board status register (Readonly) */
#define REG_BOARD_STATUS 0x01
union reg_board_status {
	u8 byte;
};

/* Offset 0x02: LED control register (R/W) */
#define REG_LED_CONTROL 0x02
union reg_led_control {
	struct {
		u8 led_g3:1; /* 0:off, 1:on */
		u8 led_g2:1; /* 0:off, 1:on */
		u8 led_g1:1; /* 0:off, 1:on */
		u8 reserved:1;
		u8 led_r3:1; /* 0:off, 1:on */
		u8 led_r2:1; /* 0:off, 1:on */
		u8 led_r1:1; /* 0:off, 1:on */
		u8 reserved2:1;
	} bit;
	u8 byte;
};
#define LED_G3 0x01
#define LED_G2 0x02
#define LED_G1 0x04
#define LED_R3 0x10
#define LED_R2 0x20
#define LED_R1 0x40

/* Offset 0x03: KCMP status register (Readonly) */
#define REG_KCMP_STATUS 0x03
union reg_kcmp_status {
	struct {
		u8 over_current:1; /* 0:normal, 1:over current */
		u8 reserved:6;
		u8 led_wwan:1; /* Not used */
	} bit;
	u8 byte;
};

#endif /* _MAE2XX_KCMP_IO_H */

