/*
 * ma2xx_umfxs.h: Definitions for Magnolia2 UM01-HW/FXS interface board
 *
 * Copyright
 * Author: 2011 Century Systems Co.,Ltd.
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

#ifndef _MAE2XX_UMFXS_H
#define _MAE2XX_UMFXS_H

/* Offset 0x00: FOMA_CNT */
#define REG_FOMA_CNT 0x00
union reg_foma_cnt {
	struct {
		u8 wake_up_sleep_in:1;
		u8 reserved:6;
		u8 pwrkey:1;	/* 0: GND, 1: Open */
	} bit;
	u8 byte;
};

/* Offset 0x01: Board_ID */
#define REG_BOARD_ID 0x01
union reg_board_id {
	struct {
		u8 revision:4;	/* Revision */
		u8 id:4;		/* Board ID (=0x04) */
	} bit;
	u8 byte;
};

/* Offset 0x02: LED_CNT1 */
#define REG_LED_CNT1 0x02
union reg_led_cnt1 {
	struct {
		u8 led_g3:1;
		u8 reserved:1;
		u8 led_g1:1;
		u8 reserved2:1;
		u8 led_r3:1;
		u8 reserved3:1;
		u8 led_r1:1;
		u8 reserved4:1;
	} bit;
	u8 byte;
};

/* Offset 0x03: FOMA_ST1 */
#define REG_FOMA_ST1 0x03
union reg_foma_st1 {
	struct {
		u8 reserved:3;
		u8 sim_cd:1;
		u8 reserved2:4;
	} bit;
	u8 byte;
};

/* Offset 0x04: FOMA_ST2 */
#define REG_FOMA_ST2 0x04
union reg_foma_st2 {
	struct {
		u8 fota_n:1;
		u8 mode_led:1;
		u8 status_led:1;
		u8 wake_up_sleep_out:1;
		u8 reserved:4;
	} bit;
	u8 byte;
};
	
/* Offset 0x05: UM01_POW_CNT */
#define REG_UM01_POW_CNT 0x05
union reg_um01_pow_cnt {
	struct {
		u8 pow_en:1;
		u8 reserved:7;
	} bit;
	u8 byte;
};

/* Offset 0x10: SLIC_CNT */
#define REG_SLIC_CNT 0x10
union reg_slic_cnt {
	struct {
		u8 forward_reverse:1;
		u8 reserved:3;
		u8 ir:1;
		u8 reserved2:3;
	} bit;
	u8 byte;
};

/* Offset 0x11: SLIC_ST */
#define REG_SLIC_ST 0x11
union reg_slic_st {
	struct {
		u8 hook:1;
		u8 reserved:7;
	} bit;
	u8 byte;
};

/* Offset 0x12: CODEC_AD */
#define REG_CODEC_AD 0x12

/* Offset 0x13: CODEC_DT */
#define REG_CODEC_DT 0x13

/* Offset 0x14: LED_CNT2 */
#define REG_LED_CNT2 0x14
union reg_led_cnt2 {
	struct {
		u8 level1:1;
		u8 level2:1;
		u8 level3:1;
		u8 level4:1;
		u8 com:1;
		u8 reserved:3;
	} bit;
	u8 byte;
};

/* ML7204 */
/* Offset 0x00: CR0 */
#define ML_CR0 0x00
union ml_cr0 {
	struct {
		u8 ope_stat:1;
		u8 sync_sel:1;
		u8 reserved:3;
		u8 afea_en:1;
		u8 afeb_en:1;
		u8 spdn:1;
	} bit;
	u8 byte;
};

/* Offset 0x01: CR1 */
#define ML_CR1 0x01
union ml_cr1 {
	struct {
		u8 reserved:3;
		u8 xdmwr_2:1;
		u8 reserved2:2;
		u8 xdmrd:1;
		u8 xdmwr:1;
	} bit;
	u8 byte;
};

/* Offset 0x02: CR2 */
#define ML_CR2 0x02
union ml_cr2 {
	struct {
		u8 tgen0_cnt0:1;
		u8 tgen0_cnt1:1;
		u8 tgen0_cnt2:1;
		u8 tgen0_cnt3:1;
		u8 tgen0_cnt4:1;
		u8 tgen0_cnt5:1;
		u8 tgen0_rx:1;
		u8 tgen0_rxab:1;
	} bit;
	u8 byte;
};

/* Offset 0x03: CR3 */
#define ML_CR3 0x03
union ml_cr3 {
	struct {
		u8 tgen1_cnt0:1;
		u8 tgen1_cnt1:1;
		u8 tgen1_cnt2:1;
		u8 tgen1_cnt3:1;
		u8 tgen1_cnt4:1;
		u8 tgen1_cnt5:1;
		u8 tgen1_rx:1;
		u8 tgen1_rxab:1;
	} bit;
	u8 byte;
};

/* Offset 0x05: CR5 */
#define ML_CR5 0x05
union ml_cr5 {
	struct {
		u8 rxflag_ch1:1;
		u8 rxflag_ch2:1;
		u8 reserved:5;
		u8 ready: 1;
	} bit;
	u8 byte;
};

/* data memory access registers */
#define ML_CR6 0x06
#define ML_CR7 0x07
#define ML_CR8 0x08
#define ML_CR9 0x09

/* Offset 0x0a: CR10 */
#define ML_CR10 0x0a
union ml_cr10 {
	struct {
		u8 clkout_en:1;
		u8 vfro0_sel:1;
		u8 vfro1_sel:1;
		u8 reserved:5;
	} bit;
	u8 byte;
};

/* Offset 0x11: CR17 */
#define ML_CR17 0x11
union ml_cr17 {
	struct {
		u8 fgen_flag:1;
		u8 reserved:7;
	} bit;
	u8 byte;
};

/* Offset 0x13: CR19 */
#define ML_CR19 0x13
union ml_cr19 {
	struct {
		u8 reserved:1;
		u8 txgen0_exflag:1;
		u8 txgen1_exflag:1;
		u8 tone0_det:1;
		u8 tone1_det:1;
		u8 reserved2:2;
		u8 dsp_err:1;
	} bit;
	u8 byte;
};

/* Offset 0x14: CR20 */
#define ML_CR20 0x14
union ml_cr20 {
	struct {
		u8 dtmf_code:4;
		u8 dtmf_det:1;
		u8 reserved:1;
		u8 dp_det:1;
		u8 intr:1;
	} bit;
	u8 byte;
};

/* Offset 0x1b: CR27 */
#define ML_CR27 0x1b
union ml_cr27 {
	struct {
		u8 fgen_d0:1;
		u8 fgen_d1:1;
		u8 fgen_d2:1;
		u8 fgen_d3:1;
		u8 fgen_d4:1;
		u8 fgen_d5:1;
		u8 fgen_d6:1;
		u8 fgen_d7:1;
	} bit;
	u8 byte;
};

/* Offset 0x1c: CR28 */
#define ML_CR28 0x1c
union ml_cr28 {
	struct {
		u8 reserved:1;
		u8 ec_en:1;
		u8 dtmf_en:1;
		u8 tdet0_en:1;
		u8 tdet1_en:1;
		u8 tim_en:1;
		u8 fgen_en:1;
		u8 fdet_en:1;
	} bit;
	u8 byte;
};

/* Offset 0x1e: CR30 */
#define ML_CR30 0x1e
union ml_cr30 {
	struct {
		u8 tdet0_sel0:1;
		u8 tdet0_sel1:1;
		u8 tdet1_sel0:1;
		u8 tdet1_sel1:1;
		u8 dtmf_sel:1;
		u8 reserved:1;
		u8 fdet_sel:1;
		u8 reserved2:1;
	} bit;
	u8 byte;
};

/* Offset 0x1f: CR31 */
#define ML_CR31 0x1f
union ml_cr31 {
	struct {
		u8 sc_rxen:1;
		u8 sc_txen:1;
		u8 codeca_rxen:1;
		u8 codeca_txen:1;
		u8 codecb_rxen:1;
		u8 codecb_txen:1;
		u8 lpen0:1;
		u8 lpen1:1;
	} bit;
	u8 byte;
};

/* Offset 0x20: CR32 */
#define ML_CR32 0x20
union ml_cr32 {
	struct {
		u8 pcm_rxen0:1;
		u8 pcm_rxen1:1;
		u8 pcm_txen0:1;
		u8 pcm_txen1:1;
		u8 rxgenb_en:1;
		u8 rxgena_en:1;
		u8 reserved:2;
	} bit;
	u8 byte;
};

/* Offset 0x42: GPCR2 */
#define ML_GPCR2 0x42
union ml_gpcr2 {
	struct {
		u8 gpfa0:1;
		u8 reserved:1;
		u8 gpfa2:1;
		u8 reserved2:1;
		u8 gpfa4:1;
		u8 gpfa5:1;
		u8 gpfa6:1;
		u8 reserved3:1;
	} bit;
	u8 byte;
};

/* data memory address */
#define MLDM_TXGAIN_SC 0x05e7
#define MLDM_TXGAINA 0x05e3
#define MLDM_TXGAINB 0x05e4
#define MLDM_RXGAIN_SC 0x05e8
#define MLDM_RXGAINA 0x05e5
#define MLDM_RXGAINB 0x05e6
#define MLDM_STGAINA 0x05df
#define MLDM_STGAINB 0x05e0
#define MLDM_FGEN_GAIN 0x0230
#define MLDM_DTMF_TH 0x018d
#define MLDM_DTMF_ON_TM 0x01f2
#define MLDM_DTMF_OFF_TM 0x01f4
#define MLDM_DTMF_NDET_CONT 0x01f5
#define MLDM_TGEN1_FREQ_C 0x02f9
#define MLDM_TGEN1_FREQ_D 0x02fb
#define MLDM_TGEN1_TIM_M0 0x02ff
#define MLDM_TGEN1_TIM_M1 0x0302

#define MLDM_EC_CR 0x002c
union mldm_ec_cr {
	struct {
		u16 reserved:1;
		u16 attb:1;
		u16 reserved2:1;
		u16 clp:1;
		u16 hdb:1;
		u16 hld:1;
		u16 reserved3:1;
		u16 thr:1;
		u16 reserved4:8;
	} bit;
	u16 val;
};

#define MLDM_GLPAD_CR 0x002d
union mldm_glpad_cr {
	struct {
		u16 lpad:2;
		u16 gpad:2;
		u16 reserved:12;
	} bit;
	u16 val;
};

#define MLDM_CR20_INTP_MSKCNT 0x0034
union mldm_cr20_intp_mskcnt {
	struct {
		u16 dtmf_code_pmsk:4;
		u16 dtmf_det_pmsk:1;
		u16 reserved:1;
		u16 dp_det_pmsk:1;
		u16 reserved2:9;
	} bit;
	u16 val;
};

#define MLDM_CR20_INTN_MSKCNT 0x0035
union mldm_cr20_intn_mskcnt {
	struct {
		u16 dtmf_code_nmsk:4;
		u16 dtmf_det_nmsk:1;
		u16 reserved:1;
		u16 dp_det_nmsk:1;
		u16 reserved2:9;
	} bit;
	u16 val;
};
		
#endif /* _MAE2XX_UMFXS_H */
