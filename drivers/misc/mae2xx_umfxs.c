/*
 * ma2xx_umfxs.c: Magnolia2 UM01-HW/FXS interface board driver
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/crc-itu-t.h>
#include <linux/bitrev.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include <linux/mae2xx_umfxs.h>

#define DRIVER_NAME "mae2xx_umfxs"
#define PROC_DIR "driver/umfxs"
#define UMFXS_PROC_STATUS_NAME "status"
#define UMFXS_PROC_PWRKEY_NAME "pwrkey"
#define UMFXS_PROC_WSIN_NAME "wsin"
#define UMFXS_PROC_WSOUT_NAME "wsout"
#define UMFXS_PROC_FOTAN_NAME "fota_n"
#define UMFXS_PROC_LEDLEVEL_NAME "led_level"
#define UMFXS_PROC_LEDCOM_NAME "led_com"
#define UMFXS_PROC_IR_NAME "ir"
#define UMFXS_PROC_FR_NAME "fr"
#define UMFXS_PROC_EC_NAME "ec"
#define UMFXS_PROC_ECCR_NAME "eccr"
#define UMFXS_PROC_ECGLPAD_NAME "ecglpad"
#define UMFXS_PROC_DTMF_NAME "dtmf"
#define UMFXS_PROC_TXGAINA_NAME "txgaina"
#define UMFXS_PROC_TXGAINB_NAME "txgainb"
#define UMFXS_PROC_RXGAINA_NAME "rxgaina"
#define UMFXS_PROC_RXGAINB_NAME "rxgainb"
#define UMFXS_PROC_TGEN_NAME "tgen"
#define UMFXS_PROC_NUMBER_NAME "number"
#define UMFXS_PROC_FGENGAIN_NAME "fgen_gain"
#define UMFXS_PROC_POWEN_NAME "pow_en"
#define UMFXS_PROC_SENDCAT_NAME "send_cat"
#define UMFXS_PROC_HOOKING_NAME "hooking"
#define PROC_READ_RETURN			\
        if (len <= off + count)			\
                *eof = 1;			\
        *start = page + off;			\
        len -= off;				\
						\
        if (len > count)			\
                len = count;			\
						\
        if (len < 0)				\
                len = 0;			\
						\
        return len

struct mae2xx_umfxs {
	struct resource *res;
	u8 *ioaddr;
	struct input_dev *idev;

	u8 dtmf_code;
#define INV_CODE 255

	u8 hook;
	u8 hook_reported;
#define ONHOOK 1
#define OFFHOOK 0

	int hook_cnt;
	int hooking_permitted;

	struct timer_list timer;

	int ir_cnt;
	int car_cnt;
};

#define TIMER_INTERVAL (50 / (1000 / HZ))
#define CNT_HOOKING_MIN 2	/* 0.1sec */
#define CNT_HOOKING_MAX 42	/* 2.1sec */
#define CNT_ONHOOK 6		/* 0.3sec */
#define CNT_OFFHOOK 2		/* 0.1sec */
#define CNT_STOP (-1)

#define CNT_IR_ON 20	/* 1sec */
#define CNT_IR_OFF 40	/* 2sec */
#define CNT_CAR_ON 10	/* 0.5sec */
#define CNT_CAR_OFF 10	/* 0.5sec */

static DEFINE_SPINLOCK(devlock);
static struct proc_dir_entry *proc_umfxs = NULL;
static struct mae2xx_umfxs *umfxs;

static int umfxs_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);

static struct file_operations umfxs_fops = {
	.owner = THIS_MODULE,
	.compat_ioctl = umfxs_ioctl,
};

static struct miscdevice umfxs_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRIVER_NAME,
	.fops = &umfxs_fops,
};

static int umfxs_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	return ret;
}

static u8 _par7even[] = {
	0x00, 0x81, 0x82, 0x03, 0x84, 0x05, 0x06, 0x87, 
	0x88, 0x09, 0x0a, 0x8b, 0x0c, 0x8d, 0x8e, 0x0f, 
	0x90, 0x11, 0x12, 0x93, 0x14, 0x95, 0x96, 0x17, 
	0x18, 0x99, 0x9a, 0x1b, 0x9c, 0x1d, 0x1e, 0x9f, 
	0xa0, 0x21, 0x22, 0xa3, 0x24, 0xa5, 0xa6, 0x27, 
	0x28, 0xa9, 0xaa, 0x2b, 0xac, 0x2d, 0x2e, 0xaf, 
	0x30, 0xb1, 0xb2, 0x33, 0xb4, 0x35, 0x36, 0xb7, 
	0xb8, 0x39, 0x3a, 0xbb, 0x3c, 0xbd, 0xbe, 0x3f, 
	0xc0, 0x41, 0x42, 0xc3, 0x44, 0xc5, 0xc6, 0x47, 
	0x48, 0xc9, 0xca, 0x4b, 0xcc, 0x4d, 0x4e, 0xcf, 
	0x50, 0xd1, 0xd2, 0x53, 0xd4, 0x55, 0x56, 0xd7, 
	0xd8, 0x59, 0x5a, 0xdb, 0x5c, 0xdd, 0xde, 0x5f, 
	0x60, 0xe1, 0xe2, 0x63, 0xe4, 0x65, 0x66, 0xe7, 
	0xe8, 0x69, 0x6a, 0xeb, 0x6c, 0xed, 0xee, 0x6f, 
	0xf0, 0x71, 0x72, 0xf3, 0x74, 0xf5, 0xf6, 0x77, 
	0x78, 0xf9, 0xfa, 0x7b, 0xfc, 0x7d, 0x7e, 0xff
};

static u8 par7even(u8 ch)
{
	if (ch < 0 || ch > 127) {
		return 0x80;
	}
	return _par7even[ch];
}

static inline int umfxs_read_reg(int offset)
{
	u8 *base;
	base = umfxs->ioaddr;
	return __raw_readb(base + offset);
}

static inline void umfxs_write_reg(int offset, u8 val)
{
	u8 *base;
	base = umfxs->ioaddr;
	__raw_writeb(val, base + offset);
}

static inline int codec_read_reg(int offset)
{
	umfxs_write_reg(REG_CODEC_AD, offset);
	return umfxs_read_reg(REG_CODEC_DT);
}

static inline void codec_write_reg(int offset, u8 val)
{
	umfxs_write_reg(REG_CODEC_AD, offset);
	umfxs_write_reg(REG_CODEC_DT, val);
}

static inline int codec_wait_fgen(void)
{
	int i;
	union ml_cr17 cr17;
	int retry = 10000;

	for (i = 0; i < retry; i++) {
		cr17.byte = codec_read_reg(ML_CR17);
		if (cr17.bit.fgen_flag != 0) {
			udelay(10);
			continue;
		}
		break;
	}
	if (i >= retry) {
		return -1;
	}
	return 0;
}

static inline int codec_wait_mem(void)
{
	int i;
	union ml_cr1 cr1;
	int retry = 100;

	for (i = 0; i < retry; i++) {
		cr1.byte = codec_read_reg(ML_CR1);
		if (cr1.byte != 0) {
			udelay(10);
			continue;
		}
		break;
	}
	if (i >= retry) {
		return -1;
	}
	return 0;
}

static inline int codec_read_mem(int address)
{
	int ret;
	u8 lower;
	u8 upper;
	union ml_cr1 cr1;

	if (codec_wait_mem() < 0) {
		return -1;
	}

	lower = address & 0xff;
	upper = (address & 0xff00) >> 8;

	codec_write_reg(ML_CR6, upper);
	codec_write_reg(ML_CR7, lower);
	cr1.byte = 0;
	cr1.bit.xdmrd = 1;
	codec_write_reg(ML_CR1, cr1.byte);

	if (codec_wait_mem() < 0) {
		return -1;
	}

	upper = codec_read_reg(ML_CR8);
	lower = codec_read_reg(ML_CR9);

	ret = (upper << 8) + lower;

	return ret;
}

static inline int codec_write_mem(int address, int val)
{
	u8 lower;
	u8 upper;
	union ml_cr1 cr1;

	if (codec_wait_mem() < 0) {
		return -1;
	}

	lower = address & 0xff;
	upper = (address & 0xff00) >> 8;
	codec_write_reg(ML_CR6, upper);
	codec_write_reg(ML_CR7, lower);

	lower = val & 0xff;
	upper = (val & 0xff00) >> 8;
	codec_write_reg(ML_CR8, upper);
	codec_write_reg(ML_CR9, lower);

	cr1.byte = 0;
	cr1.bit.xdmwr = 1;
	codec_write_reg(ML_CR1, cr1.byte);

	if (codec_wait_mem() < 0) {
		return -1;
	}

	return 0;
}

static int umfxs_get_status(char *buf)
{
	char *p = buf;
	u8 *ioaddr = umfxs->ioaddr;
	union reg_foma_cnt foma_cnt;
	union reg_board_id board_id;
	union reg_led_cnt1 led_cnt1;
	union reg_foma_st1 foma_st1;
	union reg_foma_st2 foma_st2;
	union reg_um01_pow_cnt um01_pow_cnt;
	union reg_slic_cnt slic_cnt;
	union reg_slic_st slic_st;
	union reg_led_cnt2 led_cnt2;
	union ml_cr0 ml_cr0;
	union ml_cr2 ml_cr2;
	union ml_cr3 ml_cr3;
	union ml_cr5 ml_cr5;
	union ml_cr10 ml_cr10;
	union ml_cr17 ml_cr17;
	union ml_cr19 ml_cr19;
	union ml_cr20 ml_cr20;
	union ml_cr27 ml_cr27;
	union ml_cr28 ml_cr28;
	union ml_cr30 ml_cr30;
	union ml_cr31 ml_cr31;
	union ml_cr32 ml_cr32;
	union ml_gpcr2 ml_gpcr2;
	int txgain_sc;
	int txgaina;
	int txgainb;
	int rxgain_sc;
	int rxgaina;
	int rxgainb;
	int stgaina;
	int stgainb;
	int fgen_gain;
	int dtmf_th;
	int dtmf_on_tm;
	int dtmf_off_tm;
	int dtmf_ndet_cont;
	int ec_cr;
	union mldm_ec_cr mldm_ec_cr;
	int glpad_cr;
	union mldm_glpad_cr mldm_glpad_cr;
	int cr20_intp_mskcnt;
	union mldm_cr20_intp_mskcnt mldm_cr20_intp_mskcnt;
	int cr20_intn_mskcnt;
	union mldm_cr20_intn_mskcnt mldm_cr20_intn_mskcnt;
	unsigned long flags;
	
	spin_lock_irqsave(&devlock, flags);
	foma_cnt.byte = __raw_readb(ioaddr + REG_FOMA_CNT);
	board_id.byte = __raw_readb(ioaddr + REG_BOARD_ID);
	led_cnt1.byte = __raw_readb(ioaddr + REG_LED_CNT1);
	foma_st1.byte = __raw_readb(ioaddr + REG_FOMA_ST1);
	foma_st2.byte = __raw_readb(ioaddr + REG_FOMA_ST2);
	um01_pow_cnt.byte = __raw_readb(ioaddr + REG_UM01_POW_CNT);
	slic_cnt.byte = __raw_readb(ioaddr + REG_SLIC_CNT);
	slic_st.byte = __raw_readb(ioaddr + REG_SLIC_ST);
	led_cnt2.byte = __raw_readb(ioaddr + REG_LED_CNT2);
	ml_cr0.byte = codec_read_reg(ML_CR0);
	ml_cr2.byte = codec_read_reg(ML_CR2);
	ml_cr3.byte = codec_read_reg(ML_CR3);
	ml_cr5.byte = codec_read_reg(ML_CR5);
	ml_cr10.byte = codec_read_reg(ML_CR10);
	ml_cr19.byte = codec_read_reg(ML_CR19);
	ml_cr20.byte = codec_read_reg(ML_CR20);
	ml_cr28.byte = codec_read_reg(ML_CR28);
	ml_cr30.byte = codec_read_reg(ML_CR30);
	ml_cr31.byte = codec_read_reg(ML_CR31);
	ml_cr32.byte = codec_read_reg(ML_CR32);
	ml_gpcr2.byte = codec_read_reg(ML_GPCR2);
	txgain_sc = codec_read_mem(MLDM_TXGAIN_SC);
	txgaina = codec_read_mem(MLDM_TXGAINA);
	txgainb = codec_read_mem(MLDM_TXGAINB);
	rxgain_sc = codec_read_mem(MLDM_RXGAIN_SC);
	rxgaina = codec_read_mem(MLDM_RXGAINA);
	rxgainb = codec_read_mem(MLDM_RXGAINB);
	stgaina = codec_read_mem(MLDM_STGAINA);
	stgainb = codec_read_mem(MLDM_STGAINB);
	fgen_gain = codec_read_mem(MLDM_FGEN_GAIN);
	dtmf_th = codec_read_mem(MLDM_DTMF_TH);
	dtmf_on_tm = codec_read_mem(MLDM_DTMF_ON_TM);
	dtmf_off_tm = codec_read_mem(MLDM_DTMF_OFF_TM);
	dtmf_ndet_cont = codec_read_mem(MLDM_DTMF_NDET_CONT);
	ec_cr = codec_read_mem(MLDM_EC_CR);
	glpad_cr = codec_read_mem(MLDM_GLPAD_CR);
	cr20_intp_mskcnt = codec_read_mem(MLDM_CR20_INTP_MSKCNT);
	cr20_intn_mskcnt = codec_read_mem(MLDM_CR20_INTN_MSKCNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "FOMA_CNT:           0x%02x\n", foma_cnt.byte);
	p += sprintf(p, "  PWRKEY:           %d\n", foma_cnt.bit.pwrkey);
	p += sprintf(p, "  WS_IN:            %d\n", foma_cnt.bit.wake_up_sleep_in);
	p += sprintf(p, "BOARD_ID:           0x%02x\n", board_id.byte);
	p += sprintf(p, "  ID:               %d\n", board_id.bit.id);
	p += sprintf(p, "  REVISION:         %d\n", board_id.bit.revision);
	p += sprintf(p, "LED_CNT1:           0x%02x\n", led_cnt1.byte);
	p += sprintf(p, "  LED_R1:           %d\n", led_cnt1.bit.led_r1);
	p += sprintf(p, "  LED_R3:           %d\n", led_cnt1.bit.led_r3);
	p += sprintf(p, "  LED_G1:           %d\n", led_cnt1.bit.led_g1);
	p += sprintf(p, "  LED_G3:           %d\n", led_cnt1.bit.led_g3);
	p += sprintf(p, "FOMA_ST1:           0x%02x\n", foma_st1.byte);
	p += sprintf(p, "  SIM_CD:           %d\n", foma_st1.bit.sim_cd);
	p += sprintf(p, "FOMA_ST2:           0x%02x\n", foma_st2.byte);
	p += sprintf(p, "  WS_OUT:           %d\n", foma_st2.bit.wake_up_sleep_out);
	p += sprintf(p, "  STATUS_LED:       %d\n", foma_st2.bit.status_led);
	p += sprintf(p, "  MODE_LED:         %d\n", foma_st2.bit.mode_led);
	p += sprintf(p, "  FOTA_N:           %d\n", foma_st2.bit.fota_n);
	p += sprintf(p, "UM01_POW:           0x%02x\n", um01_pow_cnt.byte);
	p += sprintf(p, "  POW_EN:           %d\n", um01_pow_cnt.bit.pow_en);
	p += sprintf(p, "SLIC_CNT:           0x%02x\n", slic_cnt.byte);
	p += sprintf(p, "  IR:               %d\n", slic_cnt.bit.ir);
	p += sprintf(p, "  F/R:              %d\n", slic_cnt.bit.forward_reverse);
	p += sprintf(p, "SLIC_ST :           0x%02x\n", slic_st.byte);
	p += sprintf(p, "  HOOK:             %d\n", slic_st.bit.hook);
	p += sprintf(p, "LED_CNT2:           0x%02x\n", led_cnt2.byte);
	p += sprintf(p, "  COM:              %d\n", led_cnt2.bit.com);
	p += sprintf(p, "  LEVEL4:           %d\n", led_cnt2.bit.level4);
	p += sprintf(p, "  LEVEL3:           %d\n", led_cnt2.bit.level3);
	p += sprintf(p, "  LEVEL2:           %d\n", led_cnt2.bit.level2);
	p += sprintf(p, "  LEVEL1:           %d\n", led_cnt2.bit.level1);
	p += sprintf(p, "CODEC CR0:          0x%02x\n", ml_cr0.byte);
	p += sprintf(p, "  SPDN:             %d\n", ml_cr0.bit.spdn);
	p += sprintf(p, "  AFEB_EN:          %d\n", ml_cr0.bit.afeb_en);
	p += sprintf(p, "  AFEA_EN:          %d\n", ml_cr0.bit.afea_en);
	p += sprintf(p, "  SYNC_SEL:         %d\n", ml_cr0.bit.sync_sel);
	p += sprintf(p, "  OPE_STAT:         %d\n", ml_cr0.bit.ope_stat);
	p += sprintf(p, "CODEC CR2:          0x%02x\n", ml_cr2.byte);
	p += sprintf(p, "  TGEN0_RXAB:       %d\n", ml_cr2.bit.tgen0_rxab);
	p += sprintf(p, "  TGEN0_RX:         %d\n", ml_cr2.bit.tgen0_rx);
	p += sprintf(p, "  TGEN0_CNT5:       %d\n", ml_cr2.bit.tgen0_cnt5);
	p += sprintf(p, "  TGEN0_CNT4:       %d\n", ml_cr2.bit.tgen0_cnt4);
	p += sprintf(p, "  TGEN0_CNT3:       %d\n", ml_cr2.bit.tgen0_cnt3);
	p += sprintf(p, "  TGEN0_CNT2:       %d\n", ml_cr2.bit.tgen0_cnt2);
	p += sprintf(p, "  TGEN0_CNT1:       %d\n", ml_cr2.bit.tgen0_cnt1);
	p += sprintf(p, "  TGEN0_CNT0:       %d\n", ml_cr2.bit.tgen0_cnt0);
	p += sprintf(p, "CODEC CR2:          0x%02x\n", ml_cr3.byte);
	p += sprintf(p, "  TGEN1_RXAB:       %d\n", ml_cr3.bit.tgen1_rxab);
	p += sprintf(p, "  TGEN1_RX:         %d\n", ml_cr3.bit.tgen1_rx);
	p += sprintf(p, "  TGEN1_CNT5:       %d\n", ml_cr3.bit.tgen1_cnt5);
	p += sprintf(p, "  TGEN1_CNT4:       %d\n", ml_cr3.bit.tgen1_cnt4);
	p += sprintf(p, "  TGEN1_CNT3:       %d\n", ml_cr3.bit.tgen1_cnt3);
	p += sprintf(p, "  TGEN1_CNT2:       %d\n", ml_cr3.bit.tgen1_cnt2);
	p += sprintf(p, "  TGEN1_CNT1:       %d\n", ml_cr3.bit.tgen1_cnt1);
	p += sprintf(p, "  TGEN1_CNT0:       %d\n", ml_cr3.bit.tgen1_cnt0);
	p += sprintf(p, "CODEC CR5:          0x%02x\n", ml_cr5.byte);
	p += sprintf(p, "  READY:            %d\n", ml_cr5.bit.ready);
	p += sprintf(p, "  RXFLAG_CH2:       %d\n", ml_cr5.bit.rxflag_ch2);
	p += sprintf(p, "  RXFLAG_CH1:       %d\n", ml_cr5.bit.rxflag_ch1);
	p += sprintf(p, "CODEC CR10:         0x%02x\n", ml_cr10.byte);
	p += sprintf(p, "  VFRO1_SEL:        %d\n", ml_cr10.bit.vfro1_sel);
	p += sprintf(p, "  VFRO0_SEL:        %d\n", ml_cr10.bit.vfro0_sel);
	p += sprintf(p, "  CLKOUT_EN:        %d\n", ml_cr10.bit.clkout_en);
	p += sprintf(p, "CODEC CR17:         0x%02x\n", ml_cr17.byte);
	p += sprintf(p, "  FGEN_FLAG:        %d\n", ml_cr17.bit.fgen_flag);
	p += sprintf(p, "CODEC CR19:         0x%02x\n", ml_cr19.byte);
	p += sprintf(p, "  DSP_ERR:          %d\n", ml_cr19.bit.dsp_err);
	p += sprintf(p, "  TONE1_DET:        %d\n", ml_cr19.bit.tone1_det);
	p += sprintf(p, "  TONE0_DET:        %d\n", ml_cr19.bit.tone0_det);
	p += sprintf(p, "  TXGEN1_EXFLAG:    %d\n", ml_cr19.bit.txgen1_exflag);
	p += sprintf(p, "  TXGEN0_EXFLAG:    %d\n", ml_cr19.bit.txgen0_exflag);
	p += sprintf(p, "CODEC CR20:         0x%02x\n", ml_cr20.byte);
	p += sprintf(p, "  INT:              %d\n", ml_cr20.bit.intr);
	p += sprintf(p, "  DP_DET:           %d\n", ml_cr20.bit.dp_det);
	p += sprintf(p, "  DTMF_DET:         %d\n", ml_cr20.bit.dtmf_det);
	p += sprintf(p, "  DTMF_CODE:        %d\n", ml_cr20.bit.dtmf_code);
	p += sprintf(p, "CODEC CR27:         0x%02x\n", ml_cr27.byte);
	p += sprintf(p, "  FGEN_D7:          %d\n", ml_cr27.bit.fgen_d7);
	p += sprintf(p, "  FGEN_D6:          %d\n", ml_cr27.bit.fgen_d6);
	p += sprintf(p, "  FGEN_D5:          %d\n", ml_cr27.bit.fgen_d5);
	p += sprintf(p, "  FGEN_D4:          %d\n", ml_cr27.bit.fgen_d4);
	p += sprintf(p, "  FGEN_D3:          %d\n", ml_cr27.bit.fgen_d3);
	p += sprintf(p, "  FGEN_D2:          %d\n", ml_cr27.bit.fgen_d2);
	p += sprintf(p, "  FGEN_D1:          %d\n", ml_cr27.bit.fgen_d1);
	p += sprintf(p, "  FGEN_D0:          %d\n", ml_cr27.bit.fgen_d0);
	p += sprintf(p, "CODEC CR28:         0x%02x\n", ml_cr28.byte);
	p += sprintf(p, "  FDET_EN:          %d\n", ml_cr28.bit.fdet_en);
	p += sprintf(p, "  FGEN_EN:          %d\n", ml_cr28.bit.fgen_en);
	p += sprintf(p, "  TIM_EN:           %d\n", ml_cr28.bit.tim_en);
	p += sprintf(p, "  TDET1_EN:         %d\n", ml_cr28.bit.tdet1_en);
	p += sprintf(p, "  TDET0_EN:         %d\n", ml_cr28.bit.tdet0_en);
	p += sprintf(p, "  DTMF_EN:          %d\n", ml_cr28.bit.dtmf_en);
	p += sprintf(p, "  EC_EN:            %d\n", ml_cr28.bit.ec_en);
	p += sprintf(p, "CODEC CR30:         0x%02x\n", ml_cr30.byte);
	p += sprintf(p, "  FDET_SEL:         %d\n", ml_cr30.bit.fdet_sel);
	p += sprintf(p, "  DTMF_SEL:         %d\n", ml_cr30.bit.dtmf_sel);
	p += sprintf(p, "  TDET1_SEL1:       %d\n", ml_cr30.bit.tdet1_sel1);
	p += sprintf(p, "  TDET1_SEL0:       %d\n", ml_cr30.bit.tdet1_sel0);
	p += sprintf(p, "  TDET0_SEL1:       %d\n", ml_cr30.bit.tdet0_sel1);
	p += sprintf(p, "  TDET0_SEL0:       %d\n", ml_cr30.bit.tdet0_sel0);
	p += sprintf(p, "CODEC CR31:         0x%02x\n", ml_cr31.byte);
	p += sprintf(p, "  LPEN1:            %d\n", ml_cr31.bit.lpen1);
	p += sprintf(p, "  LPEN0:            %d\n", ml_cr31.bit.lpen0);
	p += sprintf(p, "  CODECB_TXEN:      %d\n", ml_cr31.bit.codecb_txen);
	p += sprintf(p, "  CODECB_RXEN:      %d\n", ml_cr31.bit.codecb_rxen);
	p += sprintf(p, "  CODECA_TXEN:      %d\n", ml_cr31.bit.codeca_txen);
	p += sprintf(p, "  CODECA_RXEN:      %d\n", ml_cr31.bit.codeca_rxen);
	p += sprintf(p, "  SC_TXEN:          %d\n", ml_cr31.bit.sc_txen);
	p += sprintf(p, "  SC_RXEN:          %d\n", ml_cr31.bit.sc_rxen);
	p += sprintf(p, "CODEC CR32:         0x%02x\n", ml_cr32.byte);
	p += sprintf(p, "  RXGENA_EN:        %d\n", ml_cr32.bit.rxgena_en);
	p += sprintf(p, "  RXGENB_EN:        %d\n", ml_cr32.bit.rxgenb_en);
	p += sprintf(p, "  PCM_TXEN1:        %d\n", ml_cr32.bit.pcm_txen1);
	p += sprintf(p, "  PCM_TXEN0:        %d\n", ml_cr32.bit.pcm_txen0);
	p += sprintf(p, "  PCM_RXEN1:        %d\n", ml_cr32.bit.pcm_rxen1);
	p += sprintf(p, "  PCM_RXEN0:        %d\n", ml_cr32.bit.pcm_rxen0);
	p += sprintf(p, "CODEC GPCR2:        0x%02x\n", ml_gpcr2.byte);
	p += sprintf(p, "  GPFA6:            %d\n", ml_gpcr2.bit.gpfa6);
	p += sprintf(p, "  GPFA5:            %d\n", ml_gpcr2.bit.gpfa5);
	p += sprintf(p, "  GPFA4:            %d\n", ml_gpcr2.bit.gpfa4);
	p += sprintf(p, "  GPFA2:            %d\n", ml_gpcr2.bit.gpfa2);
	p += sprintf(p, "  GPFA0:            %d\n", ml_gpcr2.bit.gpfa0);
	p += sprintf(p, "DM TXGAIN_SC:       %d\n", txgain_sc);
	p += sprintf(p, "DM TXGAINA:         %d\n", txgaina);
	p += sprintf(p, "DM TXGAINB:         %d\n", txgainb);
	p += sprintf(p, "DM RXGAIN_SC:       %d\n", rxgain_sc);
	p += sprintf(p, "DM RXGAINA:         %d\n", rxgaina);
	p += sprintf(p, "DM RXGAINB:         %d\n", rxgainb);
	p += sprintf(p, "DM STGAINA:         %d\n", stgaina);
	p += sprintf(p, "DM STGAINB:         %d\n", stgainb);
	p += sprintf(p, "DM FGEN_GAIN:       %d\n", fgen_gain);
	p += sprintf(p, "DM DTMF_TH:         %d\n", dtmf_th);
	p += sprintf(p, "DM DTMF_ON_TM:      %d\n", dtmf_on_tm);
	p += sprintf(p, "DM DTMF_OFF_TM:     %d\n", dtmf_off_tm);
	p += sprintf(p, "DM DTMF_NDET_CONT:  %d\n", dtmf_ndet_cont);
	p += sprintf(p, "DM EC_CR:           0x%04x\n", ec_cr);
	if (ec_cr >= 0) {
		mldm_ec_cr.val = (u16)ec_cr;
		p += sprintf(p, "  THR:              %d\n", mldm_ec_cr.bit.thr);
		p += sprintf(p, "  HLD:              %d\n", mldm_ec_cr.bit.hld);
		p += sprintf(p, "  HDB:              %d\n", mldm_ec_cr.bit.hdb);
		p += sprintf(p, "  CLP:              %d\n", mldm_ec_cr.bit.clp);
		p += sprintf(p, "  ATTB:             %d\n", mldm_ec_cr.bit.attb);
	}
	p += sprintf(p, "DM GLPAD_CR:        0x%04x\n", glpad_cr);
	if (glpad_cr >= 0) {
		mldm_glpad_cr.val = (u16)glpad_cr;
		p += sprintf(p, "  GPAD:             %d\n", mldm_glpad_cr.bit.gpad);
		p += sprintf(p, "  LPAD:             %d\n", mldm_glpad_cr.bit.lpad);
	}
	p += sprintf(p, "DM CR20_INTP_MSKCNT 0x%04x\n", cr20_intp_mskcnt);
	if (cr20_intp_mskcnt >= 0) {
		mldm_cr20_intp_mskcnt.val = (u16)cr20_intp_mskcnt;
		p += sprintf(p, "  DP_DET_PMSK:      %d\n", mldm_cr20_intp_mskcnt.bit.dp_det_pmsk);
		p += sprintf(p, "  DTMF_DET_PMSK:    %d\n", mldm_cr20_intp_mskcnt.bit.dtmf_det_pmsk);
		p += sprintf(p, "  DTMF_CODE_PMSK:   %d\n", mldm_cr20_intp_mskcnt.bit.dtmf_code_pmsk);
	}
	p += sprintf(p, "DM CR20_INTN_MSKCNT 0x%04x\n", cr20_intn_mskcnt);
	if (cr20_intn_mskcnt >= 0) {
		mldm_cr20_intn_mskcnt.val = (u16)cr20_intn_mskcnt;
		p += sprintf(p, "  DP_DET_NMSK:      %d\n", mldm_cr20_intn_mskcnt.bit.dp_det_nmsk);
		p += sprintf(p, "  DTMF_DET_NMSK:    %d\n", mldm_cr20_intn_mskcnt.bit.dtmf_det_nmsk);
		p += sprintf(p, "  DTMF_CODE_NMSK:   %d\n", mldm_cr20_intn_mskcnt.bit.dtmf_code_nmsk);
	}
	return p - buf;
}

static int proc_read_status(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len = umfxs_get_status(page);

	PROC_READ_RETURN;
}

static int proc_write_powen(struct file *filp, const char __user *buf,
			    unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_um01_pow_cnt pow_cnt;

	if (count < 1)
		return -EFAULT;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	pow_cnt.byte = umfxs_read_reg(REG_UM01_POW_CNT);
	if (val)
		pow_cnt.bit.pow_en = 1;
	else
		pow_cnt.bit.pow_en = 0;
	umfxs_write_reg(REG_UM01_POW_CNT, pow_cnt.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_powen(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_um01_pow_cnt pow_cnt;

	spin_lock_irqsave(&devlock, flags);
	pow_cnt.byte = umfxs_read_reg(REG_UM01_POW_CNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", pow_cnt.bit.pow_en);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_pwrkey(struct file *filp, const char __user *buf,
			     unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_foma_cnt foma_cnt;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	foma_cnt.byte = umfxs_read_reg(REG_FOMA_CNT);
	if (val)
		foma_cnt.bit.pwrkey = 1;
	else
		foma_cnt.bit.pwrkey = 0;
	umfxs_write_reg(REG_FOMA_CNT, foma_cnt.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_pwrkey(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_foma_cnt foma_cnt;

	spin_lock_irqsave(&devlock, flags);
	foma_cnt.byte = umfxs_read_reg(REG_FOMA_CNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", foma_cnt.bit.pwrkey);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_wsin(struct file *filp, const char __user *buf,
			   unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_foma_cnt foma_cnt;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	foma_cnt.byte = umfxs_read_reg(REG_FOMA_CNT);
	if (val)
		foma_cnt.bit.wake_up_sleep_in = 1;
	else
		foma_cnt.bit.wake_up_sleep_in = 0;
	umfxs_write_reg(REG_FOMA_CNT, foma_cnt.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_wsin(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_foma_cnt foma_cnt;

	spin_lock_irqsave(&devlock, flags);
	foma_cnt.byte = umfxs_read_reg(REG_FOMA_CNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", foma_cnt.bit.wake_up_sleep_in);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_read_wsout(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_foma_st2 foma_st2;

	spin_lock_irqsave(&devlock, flags);
	foma_st2.byte = umfxs_read_reg(REG_FOMA_ST2);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", foma_st2.bit.wake_up_sleep_out);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_read_fotan(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_foma_st2 foma_st2;

	spin_lock_irqsave(&devlock, flags);
	foma_st2.byte = umfxs_read_reg(REG_FOMA_ST2);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", foma_st2.bit.fota_n);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_ledlevel(struct file *filp, const char __user *buf,
			       unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_led_cnt2 led_cnt2;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	led_cnt2.byte = umfxs_read_reg(REG_LED_CNT2);
	if (val & 0x08)
		led_cnt2.bit.level4 = 1;
	else
		led_cnt2.bit.level4 = 0;

	if (val & 0x04)
		led_cnt2.bit.level3 = 1;
	else
		led_cnt2.bit.level3 = 0;

	if (val & 0x02)
		led_cnt2.bit.level2 = 1;
	else
		led_cnt2.bit.level2 = 0;

	if (val & 0x01)
		led_cnt2.bit.level1 = 1;
	else
		led_cnt2.bit.level1 = 0;
	umfxs_write_reg(REG_LED_CNT2, led_cnt2.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_ledlevel(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_led_cnt2 led_cnt2;
	int val;

	spin_lock_irqsave(&devlock, flags);
	led_cnt2.byte = umfxs_read_reg(REG_LED_CNT2);
	spin_unlock_irqrestore(&devlock, flags);

	val = led_cnt2.bit.level1 + \
		(led_cnt2.bit.level2 << 1) +
		(led_cnt2.bit.level3 << 2) +
		(led_cnt2.bit.level4 << 3);

	p += sprintf(p, "%d\n", val);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_ledcom(struct file *filp, const char __user *buf,
			     unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_led_cnt2 led_cnt2;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	led_cnt2.byte = umfxs_read_reg(REG_LED_CNT2);
	if (val)
		led_cnt2.bit.com = 1;
	else
		led_cnt2.bit.com = 0;
	umfxs_write_reg(REG_LED_CNT2, led_cnt2.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_ledcom(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_led_cnt2 led_cnt2;

	spin_lock_irqsave(&devlock, flags);
	led_cnt2.byte = umfxs_read_reg(REG_LED_CNT2);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", led_cnt2.bit.com);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_ir(struct file *filp, const char __user *buf,
			 unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	//union reg_slic_cnt slic_cnt;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
#if 0
	{
		union reg_slic_cnt slic_cnt;

		slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);
		if (val)
			slic_cnt.bit.ir = 1;
		else
			slic_cnt.bit.ir = 0;
		umfxs_write_reg(REG_SLIC_CNT, slic_cnt.byte);
	}
#else
	if (val == 1) {
		umfxs->ir_cnt = 0;
		umfxs->car_cnt = CNT_STOP;
	} else if (val == 2) {
		umfxs->ir_cnt = CNT_STOP;
		umfxs->car_cnt = 0;
	} else {
		umfxs->ir_cnt = CNT_STOP;
		umfxs->car_cnt = CNT_STOP;
	}
#endif
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_ir(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_slic_cnt slic_cnt;

	spin_lock_irqsave(&devlock, flags);
	slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", slic_cnt.bit.ir);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_fr(struct file *filp, const char __user *buf,
			 unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union reg_slic_cnt slic_cnt;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);
	if (val)
		slic_cnt.bit.forward_reverse = 1;
	else
		slic_cnt.bit.forward_reverse = 0;
	umfxs_write_reg(REG_SLIC_CNT, slic_cnt.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_fr(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_slic_cnt slic_cnt;

	spin_lock_irqsave(&devlock, flags);
	slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", slic_cnt.bit.forward_reverse);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_ec(struct file *filp, const char __user *buf,
			 unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union ml_cr28 cr28;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	cr28.byte = codec_read_reg(ML_CR28);
	if (val)
		cr28.bit.ec_en = 1;
	else
		cr28.bit.ec_en = 0;
	codec_write_reg(ML_CR28, cr28.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_ec(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union ml_cr28 cr28;

	spin_lock_irqsave(&devlock, flags);
	cr28.byte = codec_read_reg(ML_CR28);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", cr28.bit.ec_en);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_dtmf(struct file *filp, const char __user *buf,
			   unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union ml_cr28 cr28;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	cr28.byte = codec_read_reg(ML_CR28);
	if (val)
		cr28.bit.dtmf_en = 1;
	else
		cr28.bit.dtmf_en = 0;
	codec_write_reg(ML_CR28, cr28.byte);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_dtmf(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union ml_cr28 cr28;

	spin_lock_irqsave(&devlock, flags);
	cr28.byte = codec_read_reg(ML_CR28);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", cr28.bit.dtmf_en);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_tgen(struct file *filp, const char __user *buf,
			   unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	//union ml_cr2 cr2;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	codec_write_reg(ML_CR2, val);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_tgen(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union ml_cr2 cr2;

	spin_lock_irqsave(&devlock, flags);
	cr2.byte = codec_read_reg(ML_CR2);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", cr2.byte);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_number(struct file *filp, const char __user *buf,
			     unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	unsigned long flags;
	union ml_cr17 cr17;
	union ml_cr28 cr28;
	u8 dtmf_en;
	int i;
	int param_len;
	int number_len;
	int frame_len;
	int crc_len;
	u8 frame[128];
	u8 revbuf[128];
	u8 *p;
	u8 *header;
	u8 *number;
	u8 digit;
	u8 cli;
	u16 crc;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	//printk("proc_write_number: count=%d\n", (int)count);

	if (count < 1 || count > 21) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	switch (*tmp) {
	case 'P':
	case 'O':
	case 'C':
	case 'S':
		cli = *tmp;
		number = NULL;
		number_len = 0;
		param_len = 3;
		break;
	default:
		cli = 0;
		number = tmp;
		for (i = 0, number_len = 0; i < count; i++, number_len++) {
			if ((*(tmp + i) < '0') || (*(tmp + i) > '9')) {
				break;
			}
		}
		param_len = number_len + 2;
		break;
	}

	p = frame;

	*p++ = par7even(0x10); /* DLE */
	*p++ = par7even(0x01); /* SOH */

	header = p;
	*p++ = par7even(0x07); /* header */
	*p++ = par7even(0x10); /* DLE */
	*p++ = par7even(0x02); /* STX */
	*p++ = par7even(0x40); /* service */
	
	if (param_len == 0x10)
		*p++ = par7even(0x10); /* escape */
	*p++ = par7even(param_len); /* message length */

	if (number_len > 0) {
		*p++ = par7even(0x02); /* parameter type: phone number */
		if (number_len == 0x10)
			*p++ = par7even(0x10); /* escape */
		*p++ = par7even(number_len); /* parameter length */
		for (i = 0; i < number_len; i++) {
			digit = *(number + i);
			if (digit == 0x10)
				*p++ = par7even(0x10); /* escape */
			*p++ = par7even(digit); /* phone number digit */
		}
	}

	if (cli) {
		*p++ = par7even(0x04); /* parameter type: cli */
		*p++ = par7even(0x01); /* parameter length */
		if (cli == 0x10)
			*p++ = par7even(0x10); /* escape */
		*p++ = par7even(cli); /* POCS */
	}

	*p++ = par7even(0x10); /* DLE */
	*p++ = par7even(0x03); /* ETX */
	crc_len = p - header;

	for (i = 0; i < crc_len; i++) {
		revbuf[i] = bitrev8(*(header + i));
	}
	crc = crc_itu_t(0, revbuf, crc_len);
	//printk("crc_len:%d, crc:%04x, s:%02x\n", crc_len, crc, *header);

	*p++ = bitrev8(crc >> 8);
	*p++ = bitrev8(crc & 0xff);

	frame_len = p - frame;

#if 0
	for (i = 0; i < frame_len; i++)
		printk("# frame[%d]: 0x%02x\n", i, frame[i]);
#endif

	spin_lock_irqsave(&devlock, flags);
	cr28.byte = codec_read_reg(ML_CR28);
	dtmf_en = cr28.bit.dtmf_en;	
	if (dtmf_en) {
		/* stop dtmf detector */
		cr28.bit.dtmf_en = 0;
	}
	/* start fsk generator */
	cr28.bit.fgen_en = 1;
	codec_write_reg(ML_CR28, cr28.byte);

	/* markbit (60ms) */
	mdelay(60);

	for (i = 0; i < frame_len; i++) {
		if (codec_wait_fgen() < 0) {
			printk("# wait error\n");
			ret = -EFAULT;
			goto unlock;
		}
		/* write a byte */
		//printk("frame[%d]: 0x%02x\n", i, frame[i]);
		codec_write_reg(ML_CR27, frame[i]);
		cr17.byte = codec_read_reg(ML_CR17);
		cr17.bit.fgen_flag = 1;
		codec_write_reg(ML_CR17, cr17.byte);
	}
	if (codec_wait_fgen() < 0) {
		ret = -EFAULT;
		goto unlock;
	}

	/* stop fsk generator */
	if (dtmf_en) {
		/* restart dtmf detector */
		cr28.bit.dtmf_en = 1;
	}
	cr28.bit.fgen_en = 0;
	codec_write_reg(ML_CR28, cr28.byte);

unlock:
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_number(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union ml_cr17 cr17;

	spin_lock_irqsave(&devlock, flags);
	cr17.byte = codec_read_reg(ML_CR17);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", cr17.bit.fgen_flag);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_mem(int address, struct file *filp, const char __user *buf,
			  unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u16 val;
	unsigned long flags;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u16)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	codec_write_mem(address, val);
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_mem(int address, char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	int val;

	spin_lock_irqsave(&devlock, flags);
	val = codec_read_mem(address);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", val);
	len = p - page;

	PROC_READ_RETURN;
}

static int proc_write_txgaina(struct file *filp, const char __user *buf,
			      unsigned long count, void *data)
{
	return proc_write_mem(MLDM_TXGAINA, filp, buf, count, data);
}
static int proc_read_txgaina(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_TXGAINA, page, start, off, count, eof, data);
}

static int proc_write_txgainb(struct file *filp, const char __user *buf,
			      unsigned long count, void *data)
{
	return proc_write_mem(MLDM_TXGAINB, filp, buf, count, data);
}
static int proc_read_txgainb(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_TXGAINB, page, start, off, count, eof, data);
}

static int proc_write_rxgaina(struct file *filp, const char __user *buf,
			      unsigned long count, void *data)
{
	return proc_write_mem(MLDM_RXGAINA, filp, buf, count, data);
}
static int proc_read_rxgaina(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_RXGAINA, page, start, off, count, eof, data);
}

static int proc_write_rxgainb(struct file *filp, const char __user *buf,
			      unsigned long count, void *data)
{
	return proc_write_mem(MLDM_RXGAINB, filp, buf, count, data);
}

static int proc_read_rxgainb(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_RXGAINB, page, start, off, count, eof, data);
}

static int proc_write_eccr(struct file *filp, const char __user *buf,
			   unsigned long count, void *data)
{
	return proc_write_mem(MLDM_EC_CR, filp, buf, count, data);
}

static int proc_read_eccr(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_EC_CR, page, start, off, count, eof, data);
}

static int proc_write_ecglpad(struct file *filp, const char __user *buf,
			      unsigned long count, void *data)
{
	return proc_write_mem(MLDM_GLPAD_CR, filp, buf, count, data);
}

static int proc_read_ecglpad(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_GLPAD_CR, page, start, off, count, eof, data);
}

static int proc_write_fgengain(struct file *filp, const char __user *buf,
			       unsigned long count, void *data)
{
	return proc_write_mem(MLDM_FGEN_GAIN, filp, buf, count, data);
}

static int proc_read_fgengain(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	return proc_read_mem(MLDM_FGEN_GAIN, page, start, off, count, eof, data);
}

static int proc_write_sendcat(struct file *filp, const char __user *buf,
			     unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;
	union ml_cr19 cr19;
	int i;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	codec_write_mem(MLDM_TGEN1_FREQ_C, 0x1b44); /* 852hz */
	codec_write_mem(MLDM_TGEN1_FREQ_D, 0x3442); /* 1633hz */
	codec_write_mem(MLDM_TGEN1_TIM_M0, 0x320); /* 100ms */
	codec_write_mem(MLDM_TGEN1_TIM_M1, 0x190); /* 50ms */
	codec_write_reg(ML_CR3, 0x8a); /* RXAB, C+D, single, M0:ON, M1:OFF */
	for (i = 0; i < 200; i++) {
		mdelay(1);
		cr19.byte = codec_read_reg(ML_CR19);
		if (cr19.bit.txgen1_exflag == 0) {
			break;
		}
	}
	codec_write_mem(MLDM_TGEN1_FREQ_C, 0x1e1d); /* 941hz */
	codec_write_mem(MLDM_TGEN1_FREQ_D, 0x3442); /* 1633hz */
	codec_write_reg(ML_CR3, 0x8a); /* RXAB, C+D, single, M0:ON, M1:OFF */
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_write_hooking(struct file *filp, const char __user *buf,
			   unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val;
	unsigned long flags;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8)simple_strtol(tmp, NULL, 10);

	spin_lock_irqsave(&devlock, flags);
	if (val)
		umfxs->hooking_permitted = 1;
	else
		umfxs->hooking_permitted = 0;
	spin_unlock_irqrestore(&devlock, flags);

out:
	kfree(tmp);
	return ret;
}

static int proc_read_hooking(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	int val;

	spin_lock_irqsave(&devlock, flags);
	val = umfxs->hooking_permitted;
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "%d\n", val);
	len = p - page;

	PROC_READ_RETURN;
}

static int map_dtmfcode_to_key(u8 code)
{
	switch (code) {
	case 0: return KEY_1;
	case 1: return KEY_4;
	case 2: return KEY_7;
	case 3: return KEY_KPASTERISK;
	case 4: return KEY_2;
	case 5: return KEY_5;
	case 6: return KEY_8;
	case 7: return KEY_0;
	case 8: return KEY_3;
	case 9: return KEY_6;
	case 10: return KEY_9;
	case 11: return KEY_LEFTSHIFT | KEY_3 << 8;
	}
	return -EINVAL;
}

static void report_key(struct input_dev *idev, u8 code, int flag) {
	int key;

	key = map_dtmfcode_to_key(code);
	if (key >= 0) {
		//printk("! %04x %d\n", key, flag);
		input_report_key(idev, key & 0xff, flag);
		if (key >> 8)
			input_report_key(idev, key >> 8, flag);
		input_sync(idev);
	}
}

static irqreturn_t umfxs_irq(int irq, void *devid)
{
	union ml_cr20 cr20;	
	u8 code;

	cr20.byte = codec_read_reg(ML_CR20);
	if (cr20.bit.dtmf_det) {
		code = cr20.bit.dtmf_code;
		if ((umfxs->dtmf_code != INV_CODE) && (umfxs->dtmf_code != code)) {
			report_key(umfxs->idev, umfxs->dtmf_code, 0);
		}
		report_key(umfxs->idev, code, 1);
		umfxs->dtmf_code = code;
	} else {
		if (umfxs->dtmf_code != INV_CODE) {
			report_key(umfxs->idev, umfxs->dtmf_code, 0);
			umfxs->dtmf_code = INV_CODE;
		}
	}

	return IRQ_HANDLED;
}

static void led_all_off(void)
{
	union reg_led_cnt2 led_cnt2;

	led_cnt2.byte = umfxs_read_reg(REG_LED_CNT2);
	led_cnt2.bit.level4 = 0;
	led_cnt2.bit.level3 = 0;
	led_cnt2.bit.level2 = 0;
	led_cnt2.bit.level1 = 0;
	led_cnt2.bit.com = 0;
	umfxs_write_reg(REG_LED_CNT2, led_cnt2.byte);
}

static int umfxs_dtmf_open(struct input_dev *idev)
{
	return 0;
}

static void umfxs_dtmf_close(struct input_dev *idev)
{

}

static void stopring(void)
{
	union reg_slic_cnt slic_cnt;

	umfxs->ir_cnt = CNT_STOP;
	umfxs->car_cnt = CNT_STOP;
	slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);
	slic_cnt.bit.ir = 0;
	umfxs_write_reg(REG_SLIC_CNT, slic_cnt.byte);
}

static int read_hook(void)
{
	union reg_slic_st slic_st;
	int i;
	int on_cnt = 0;
	int off_cnt = 0;

	for (i = 0; i < 50; i++) {
		slic_st.byte = umfxs_read_reg(REG_SLIC_ST);
		if (slic_st.bit.hook == 0) {
			on_cnt++;
		} else {
			off_cnt++;
		}
		udelay(1);
	}
	if (on_cnt < off_cnt) {
		/* off-hook */
		return OFFHOOK;
	} else {
		/* on-hook */
		return ONHOOK;
	}
}

static void poll_hook(void)
{
	int hook;
	int thresh;

	hook = read_hook();
	if (hook == ONHOOK) {
		/* on-hook */
		if (umfxs->hook == ONHOOK) {
			thresh = umfxs->hooking_permitted ? CNT_HOOKING_MAX : CNT_ONHOOK;
			if (umfxs->hook_cnt != CNT_STOP)
				umfxs->hook_cnt++;
			if (umfxs->hook_cnt > thresh) {
				if (umfxs->hook_reported == OFFHOOK) {
					/* report on-hook */
					printk("on-hook cnt=%d\n", umfxs->hook_cnt);
					input_report_key(umfxs->idev, KEY_ENTER, 0);
					input_sync(umfxs->idev);
					umfxs->hook_reported = ONHOOK;
				}
				umfxs->hook_cnt = CNT_STOP;
			}
		} else {
			printk("on-hook\n");
			umfxs->hook = ONHOOK;
			umfxs->hook_cnt = 0;
		}
	} else {
		/* off-hook */
		if (umfxs->hook == ONHOOK) {
			printk("off-hook\n");
			if (umfxs->hook_cnt >= 0) {
				if (umfxs->hook_cnt <= CNT_HOOKING_MIN) {
					/* short break */
					if (umfxs->hook_reported == OFFHOOK) {
						umfxs->hook_cnt = CNT_STOP;
					} else {
						umfxs->hook_cnt = 0;
					}
				} else if (umfxs->hook_cnt <= CNT_HOOKING_MAX) {
					if ((umfxs->hook_reported == OFFHOOK) &&
						(umfxs->hooking_permitted == 1)) {
						/* report hooking */
						printk("hooking cnt=%d\n", umfxs->hook_cnt);
						input_report_key(umfxs->idev, KEY_SPACE, 1);
						input_sync(umfxs->idev);
						input_report_key(umfxs->idev, KEY_SPACE, 0);
						input_sync(umfxs->idev);
					}
					umfxs->hook_cnt = CNT_STOP;
				} else {
					umfxs->hook_cnt = 0;
				}
			} else {
				umfxs->hook_cnt = 0;
			}
			umfxs->hook = OFFHOOK;
		} else {
			if (umfxs->hook_cnt != CNT_STOP)
				umfxs->hook_cnt++;
			if (umfxs->hook_cnt > CNT_OFFHOOK) {
				if (umfxs->hook_reported == ONHOOK) {
					/* report off-hook */
					printk("off-hook cnt=%d\n", umfxs->hook_cnt);
					input_report_key(umfxs->idev, KEY_ENTER, 1);
					input_sync(umfxs->idev);
					umfxs->hook_reported = OFFHOOK;
					stopring();
				}
				umfxs->hook_cnt = CNT_STOP;
			}
		}
	}
}

static void ringing(void)
{
	union reg_slic_cnt slic_cnt;

	slic_cnt.byte = umfxs_read_reg(REG_SLIC_CNT);

	if (umfxs->ir_cnt != CNT_STOP) {
		umfxs->ir_cnt++;
		if (umfxs->ir_cnt > CNT_IR_ON + CNT_IR_OFF) {
			umfxs->ir_cnt = 0;
		}
		if (umfxs->ir_cnt <= CNT_IR_ON) {
			slic_cnt.bit.ir = 1;
		} else {
			slic_cnt.bit.ir = 0;
		}
	} else if (umfxs->car_cnt != CNT_STOP) {
		umfxs->car_cnt++;
		if (umfxs->car_cnt > CNT_CAR_ON + CNT_CAR_OFF) {
			umfxs->car_cnt = 0;
		}
		if (umfxs->car_cnt <= CNT_CAR_ON) {
			slic_cnt.bit.ir = 1;
		} else {
			slic_cnt.bit.ir = 0;
		}
	} else {
		slic_cnt.bit.ir = 0;
	}

	umfxs_write_reg(REG_SLIC_CNT, slic_cnt.byte);
}

static void timer_handler(unsigned long data)
{
	unsigned long flags;

	spin_lock_irqsave(&devlock, flags);
	poll_hook();
	ringing();
	spin_unlock_irqrestore(&devlock, flags);

	umfxs->timer.expires = jiffies + TIMER_INTERVAL;
	add_timer(&umfxs->timer);
}

static int codec_reset(void)
{
	int i;
	int ret = -1;
	union ml_cr0 cr0;
	union ml_cr5 cr5;

	cr5.byte = codec_read_reg(ML_CR5);
	if (cr5.bit.ready == 0) {

		cr0.byte = codec_read_reg(ML_CR0);
		//printk ("cr0: 0x%02x\n", cr0.byte);
		cr0.bit.spdn = 1;
		codec_write_reg(ML_CR0, cr0.byte);
		udelay(1);
		cr0.byte = codec_read_reg(ML_CR0);
		//printk ("cr0: 0x%02x\n", cr0.byte);
		cr0.bit.spdn = 0;
		codec_write_reg(ML_CR0, cr0.byte);

		for (i = 0; i < 1000; i++) {
			cr5.byte = codec_read_reg(ML_CR5);
			//printk ("cr5: 0x%02x\n", cr5.byte);
			if (cr5.bit.ready == 0) {
				mdelay(1);
				continue;
			}
			ret = 0;
			break;
		}
	} else {
		ret = 0;
	}
	printk("codec_reset = %d\n", ret);
	return ret;
}

static int codec_init(void)
{
	union ml_cr0 cr0;
	union ml_cr10 cr10;
	union ml_cr28 cr28;
	union ml_cr30 cr30;
	union ml_cr31 cr31;
	union ml_cr32 cr32;

	if (codec_reset() < 0) {
		return -1;
	}
		
	/* setup path */
	cr31.byte = 0;
	cr31.bit.lpen0 = 1; 
	cr31.bit.lpen1 = 1; 
	cr31.bit.codecb_txen = 0; 
	cr31.bit.codecb_rxen = 0; 
	cr31.bit.codeca_txen = 1; 
	cr31.bit.codeca_rxen = 1; 
	cr31.bit.sc_txen = 0;
	cr31.bit.sc_rxen = 0;
	codec_write_reg(ML_CR31, cr31.byte);

	cr10.byte = codec_read_reg(ML_CR10);
	cr10.bit.vfro0_sel = 1; /* VFRO0 => SLIC */
	cr10.bit.vfro1_sel = 1; /* VFRO1 => UM01-HW */
	codec_write_reg(ML_CR10, cr10.byte);

	codec_write_mem(MLDM_TXGAIN_SC, 0);
	codec_write_mem(MLDM_RXGAIN_SC, 0);

	/* dtmf detector */
	cr30.byte = codec_read_reg(ML_CR30);
	cr30.bit.dtmf_sel = 0; /* TXDETA */
	codec_write_reg(ML_CR30, cr30.byte);
	//codec_write_mem(MLDM_DTMF_TH, 0x0600);
	codec_write_mem(MLDM_DTMF_NDET_CONT, 0x0000);
	cr28.byte = codec_read_reg(ML_CR28);
	cr28.bit.dtmf_en = 1; /* enable */
	codec_write_reg(ML_CR28, cr28.byte);

	/* tone generator */
	cr32.byte = codec_read_reg(ML_CR32);
	cr32.bit.rxgena_en = 1;
	codec_write_reg(ML_CR32, cr32.byte);

	/* start */
	cr0.byte = codec_read_reg(ML_CR0);
	cr0.bit.ope_stat = 1;
	codec_write_reg(ML_CR0, cr0.byte);

	return 0;
}

static int umfxs_create_proc_entries(void)
{
	struct proc_dir_entry *dir;
	struct proc_dir_entry *ent;

	dir = proc_mkdir(PROC_DIR, NULL);
	if (!dir)
		return -ENOMEM;
	create_proc_read_entry(UMFXS_PROC_STATUS_NAME, 0, dir, proc_read_status, 
			       NULL);
	ent = create_proc_entry(UMFXS_PROC_PWRKEY_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_pwrkey;
		ent->read_proc = proc_read_pwrkey;
	}
	ent = create_proc_entry(UMFXS_PROC_WSIN_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_wsin;
		ent->read_proc = proc_read_wsin;
	}
	create_proc_read_entry(UMFXS_PROC_WSOUT_NAME, 0, dir, proc_read_wsout,
			       NULL);
	create_proc_read_entry(UMFXS_PROC_FOTAN_NAME, 0, dir, proc_read_fotan,
			       NULL);
	ent = create_proc_entry(UMFXS_PROC_LEDLEVEL_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_ledlevel;
		ent->read_proc = proc_read_ledlevel;
	}
	ent = create_proc_entry(UMFXS_PROC_LEDCOM_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_ledcom;
		ent->read_proc = proc_read_ledcom;
	}
	ent = create_proc_entry(UMFXS_PROC_IR_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_ir;
		ent->read_proc = proc_read_ir;
	}
	ent = create_proc_entry(UMFXS_PROC_FR_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_fr;
		ent->read_proc = proc_read_fr;
	}
	ent = create_proc_entry(UMFXS_PROC_EC_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_ec;
		ent->read_proc = proc_read_ec;
	}
	ent = create_proc_entry(UMFXS_PROC_ECCR_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_eccr;
		ent->read_proc = proc_read_eccr;
	}
	ent = create_proc_entry(UMFXS_PROC_ECGLPAD_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_ecglpad;
		ent->read_proc = proc_read_ecglpad;
	}
	ent = create_proc_entry(UMFXS_PROC_DTMF_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_dtmf;
		ent->read_proc = proc_read_dtmf;
	}
	ent = create_proc_entry(UMFXS_PROC_TXGAINA_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_txgaina;
		ent->read_proc = proc_read_txgaina;
	}
	ent = create_proc_entry(UMFXS_PROC_TXGAINB_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_txgainb;
		ent->read_proc = proc_read_txgainb;
	}
	ent = create_proc_entry(UMFXS_PROC_RXGAINA_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_rxgaina;
		ent->read_proc = proc_read_rxgaina;
	}
	ent = create_proc_entry(UMFXS_PROC_RXGAINB_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_rxgainb;
		ent->read_proc = proc_read_rxgainb;
	}
	ent = create_proc_entry(UMFXS_PROC_TGEN_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_tgen;
		ent->read_proc = proc_read_tgen;
	}
	ent = create_proc_entry(UMFXS_PROC_NUMBER_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_number;
		ent->read_proc = proc_read_number;
	}
	ent = create_proc_entry(UMFXS_PROC_FGENGAIN_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_fgengain;
		ent->read_proc = proc_read_fgengain;
	}
	ent = create_proc_entry(UMFXS_PROC_POWEN_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_powen;
		ent->read_proc = proc_read_powen;
	}
	ent = create_proc_entry(UMFXS_PROC_SENDCAT_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_sendcat;
	}
	ent = create_proc_entry(UMFXS_PROC_HOOKING_NAME, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_hooking;
		ent->read_proc = proc_read_hooking;
	}
	proc_umfxs = dir;
	return 0;
}

static void umfxs_remove_proc_entries(void)
{
	if (proc_umfxs == NULL) {
		return;
	}
	remove_proc_entry(UMFXS_PROC_HOOKING_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_SENDCAT_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_POWEN_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_FGENGAIN_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_NUMBER_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_TGEN_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_RXGAINB_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_RXGAINA_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_TXGAINB_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_TXGAINA_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_DTMF_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_ECGLPAD_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_ECCR_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_EC_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_FR_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_IR_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_LEDCOM_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_LEDLEVEL_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_FOTAN_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_WSOUT_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_WSIN_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_PWRKEY_NAME, proc_umfxs);
	remove_proc_entry(UMFXS_PROC_STATUS_NAME, proc_umfxs);
	remove_proc_entry(PROC_DIR, NULL);
	proc_umfxs = NULL;
}

static int umfxs_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	int len;
	int irq;
	int r;

	printk("Magnolia2 UM01-HW/FXS interface board driver\n");

	umfxs = kzalloc(sizeof(struct mae2xx_umfxs), GFP_KERNEL);
	if (!umfxs)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto err1;
	}
	len = res->end - res->start + 1;
	//printk("%s res: %u - %u (len:%d)\n", pdev->name, res->start, res->end, len);

	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto err1;
	};

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		goto err2;
	}

	umfxs->idev = input_allocate_device();
	if (!umfxs->idev) {
		ret = -ENOMEM;
		goto err3;
	}

	if (umfxs_create_proc_entries() != 0) {
		ret = -EFAULT;
		goto err4;
	}

	umfxs->ioaddr = (void *)ioremap(res->start, len);
	umfxs->res = res;

	if (codec_init() < 0) {
		ret = -EFAULT;
		goto err5;
	}

	r = request_irq(irq, umfxs_irq, IRQF_TRIGGER_FALLING, pdev->name, NULL);
	if (r) {
		printk(KERN_ERR "request_irq() failed(%d).\n", r);
		goto err6;
	}

	/* init input device */
	umfxs->idev->name = pdev->name;
	umfxs->idev->phys = NULL;
	umfxs->idev->id.bustype = BUS_HOST;
	umfxs->idev->dev.parent = &pdev->dev;
	umfxs->idev->open = umfxs_dtmf_open;
	umfxs->idev->close = umfxs_dtmf_close;
	umfxs->idev->evbit[0] = BIT_MASK(EV_KEY);
	set_bit(KEY_0, umfxs->idev->keybit);
	set_bit(KEY_1, umfxs->idev->keybit);
	set_bit(KEY_2, umfxs->idev->keybit);
	set_bit(KEY_3, umfxs->idev->keybit);
	set_bit(KEY_4, umfxs->idev->keybit);
	set_bit(KEY_5, umfxs->idev->keybit);
	set_bit(KEY_6, umfxs->idev->keybit);
	set_bit(KEY_7, umfxs->idev->keybit);
	set_bit(KEY_8, umfxs->idev->keybit);
	set_bit(KEY_9, umfxs->idev->keybit);
	set_bit(KEY_KPASTERISK, umfxs->idev->keybit);
	set_bit(KEY_LEFTSHIFT, umfxs->idev->keybit);
	set_bit(KEY_ENTER, umfxs->idev->keybit);
	set_bit(KEY_SPACE, umfxs->idev->keybit);
	r = input_register_device(umfxs->idev);
	if (r < 0) {
		goto err7;
	}
	umfxs->dtmf_code = INV_CODE;

	umfxs->hook = ONHOOK;
	umfxs->hook_reported = ONHOOK;
	umfxs->hooking_permitted = 0;
	umfxs->hook_cnt = CNT_STOP;
	umfxs->ir_cnt = CNT_STOP;
	umfxs->car_cnt = CNT_STOP;

	/* init timer */
	init_timer(&umfxs->timer);
	umfxs->timer.function = timer_handler;
	umfxs->timer.expires = jiffies + TIMER_INTERVAL;
	add_timer(&umfxs->timer);

	led_all_off();
	r = misc_register(&umfxs_dev);
	if (r < 0) {
		goto err8;
	}

	return 0;

err8:
	del_timer(&umfxs->timer);
	input_unregister_device(umfxs->idev);
err7:
	free_irq(irq, NULL);
err6:
err5:
	iounmap(umfxs->ioaddr);
	umfxs_remove_proc_entries();
err4:
err3:
err2:
	release_mem_region(res->start, len);
err1:
	kfree(umfxs);
	return ret;
}

static int umfxs_remove(struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	led_all_off();
	res = umfxs->res;
	misc_deregister(&umfxs_dev);
	del_timer(&umfxs->timer);
	input_unregister_device(umfxs->idev);
	irq = platform_get_irq(pdev, 0);
	free_irq(irq, NULL);
	iounmap(umfxs->ioaddr);
	umfxs_remove_proc_entries();
	release_mem_region(res->start, res->end - res->start + 1);
	kfree(umfxs);
	return 0;
}

static struct platform_driver umfxs_driver = {
	.probe = umfxs_probe,
	.remove = __devexit_p(umfxs_remove),
	.driver = {
		.name = "umfxs",
	},
};

static int __init umfxs_init(void)
{
	return platform_driver_register(&umfxs_driver);
}

static void __exit umfxs_exit(void)
{
	platform_driver_unregister(&umfxs_driver);
}

module_init(umfxs_init);
module_exit(umfxs_exit);

MODULE_DESCRIPTION("Magnolia2 UM01-HW/FXS interface board driver");
MODULE_AUTHOR("Century Systems Co.,Ltd.");
MODULE_LICENSE("GPL");
