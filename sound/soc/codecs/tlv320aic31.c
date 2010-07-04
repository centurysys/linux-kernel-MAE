/*
 * ALSA SoC TLV320AIC31 codec driver
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * Based on sound/soc/codecs/wm8753.c by Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Hence the machine layer should disable unsupported inputs/outputs by
 *  snd_soc_dapm_disable_pin(codec, "MONO_LOUT"), etc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "tlv320aic31.h"

#define AIC31_VERSION "0.2"

/* codec private data */
struct aic31_priv {
	unsigned int sysclk;
	int master;
};

/*
 * AIC31 register cache
 * We can't read the AIC31 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const u8 aic31_reg[AIC31_CACHEREGNUM] = {
	0x00, 0x00, 0x00, 0x10,	/* 0 */
	0x04, 0x00, 0x00, 0x00,	/* 4 */
	0x00, 0x00, 0x00, 0x01,	/* 8 */
	0x00, 0x00, 0x00, 0x80,	/* 12 */
	0x80, 0xff, 0xff, 0x78,	/* 16 */
	0x78, 0x78, 0x78, 0x78,	/* 20 */
	0x78, 0x00, 0x00, 0xfe,	/* 24 */
	0x00, 0x00, 0xfe, 0x00,	/* 28 */
	0x18, 0x18, 0x00, 0x00,	/* 32 */
	0x00, 0x00, 0x00, 0x00,	/* 36 */
	0x00, 0x00, 0x00, 0x80,	/* 40 */
	0x80, 0x00, 0x00, 0x00,	/* 44 */
	0x00, 0x00, 0x00, 0x04,	/* 48 */
	0x00, 0x00, 0x00, 0x00,	/* 52 */
	0x00, 0x00, 0x04, 0x00,	/* 56 */
	0x00, 0x00, 0x00, 0x00,	/* 60 */
	0x00, 0x04, 0x00, 0x00,	/* 64 */
	0x00, 0x00, 0x00, 0x00,	/* 68 */
	0x04, 0x00, 0x00, 0x00,	/* 72 */
	0x00, 0x00, 0x00, 0x00,	/* 76 */
	0x00, 0x00, 0x00, 0x00,	/* 80 */
	0x00, 0x00, 0x00, 0x00,	/* 84 */
	0x00, 0x00, 0x00, 0x00,	/* 88 */
	0x00, 0x00, 0x00, 0x00,	/* 92 */
	0x00, 0x00, 0x00, 0x00,	/* 96 */
	0x00, 0x00, 0x02,	/* 100 */
};

/*
 * read aic31 register cache
 */
static inline unsigned int aic31_read_reg_cache(struct snd_soc_codec *codec,
						unsigned int reg)
{
	u8 *cache = codec->reg_cache;
	if (reg >= AIC31_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write aic31 register cache
 */
static inline void aic31_write_reg_cache(struct snd_soc_codec *codec,
					 u8 reg, u8 value)
{
	u8 *cache = codec->reg_cache;
	if (reg >= AIC31_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the aic31 register space
 */
static int aic31_write(struct snd_soc_codec *codec, unsigned int reg,
		       unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D8 aic31 register offset
	 *   D7...D0 register data
	 */
	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	aic31_write_reg_cache(codec, data[0], data[1]);
	if (codec->hw_write(codec->control_data, data, 2) == 2) {
#ifdef CONFIG_MACH_MAGNOLIA2
//              printk("# AIC31: write reg %3d = 0x%02x\n", data[0], data[1]);
#endif
		return 0;
        } else
		return -EIO;
}

/*
 * read from the aic31 register space
 */
static int aic31_read(struct snd_soc_codec *codec, unsigned int reg,
		      u8 *value)
{
	*value = reg & 0xff;
	if (codec->hw_read(codec->control_data, value, 1) != 1)
		return -EIO;

#ifdef CONFIG_MACH_MAGNOLIA2
//      printk("# AIC31: read  reg %3d = 0x%02x\n", reg, *value);
#endif
	aic31_write_reg_cache(codec, reg, *value);
	return 0;
}

#define SOC_DAPM_SINGLE_AIC31(xname, reg, shift, mask, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw_aic31, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, mask, invert) }

/*
 * All input lines are connected when !0xf and disconnected with 0xf bit field,
 * so we have to use specific dapm_put call for input mixer
 */
static int snd_soc_dapm_put_volsw_aic31(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
        struct soc_mixer_control *mc = 
                (struct soc_mixer_control *) kcontrol->private_value;
	int reg = mc->reg;
	int shift = mc->shift;
	int mask = mc->max;
	int invert = mc->invert;
	unsigned short val, val_mask;
	int ret;
	struct snd_soc_dapm_path *path;
	int found = 0;

        printk("* %s: called, reg: %d, shift: %d, mask: 0x%02x, invert: %d\n",
               __FUNCTION__, reg, shift, mask, invert);

	val = (ucontrol->value.integer.value[0] & mask);

	mask = 0xf;
	if (val)
		val = mask;

	if (invert)
		val = mask - val;
	val_mask = mask << shift;
	val = val << shift;

	mutex_lock(&widget->codec->mutex);

	if (snd_soc_test_bits(widget->codec, reg, val_mask, val)) {
		/* find dapm widget path assoc with kcontrol */
		list_for_each_entry(path, &widget->codec->dapm_paths, list) {
			if (path->kcontrol != kcontrol)
				continue;

			/* found, now check type */
			found = 1;
			if (val)
				/* new connection */
				path->connect = invert ? 0 : 1;
			else
				/* old connection must be powered down */
				path->connect = invert ? 1 : 0;
			break;
		}

		if (found)
			snd_soc_dapm_sync(widget->codec);
	}

	ret = snd_soc_update_bits(widget->codec, reg, val_mask, val);

	mutex_unlock(&widget->codec->mutex);
	return ret;
}

static const char *aic31_left_dac_mux[] = { "DAC_L1", "DAC_L3", "DAC_L2" };
static const char *aic31_right_dac_mux[] = { "DAC_R1", "DAC_R3", "DAC_R2" };
static const char *aic31_left_hpcom_mux[] =
    { "differential of HPLOUT", "constant VCM", "single-ended" };
static const char *aic31_right_hpcom_mux[] =
    { "differential of HPROUT", "constant VCM", "single-ended",
      "differential of HPLCOM", "external feedback" };
static const char *aic31_adc_hpf[] =
    { "Disabled", "0.0045xFs", "0.0125xFs", "0.025xFs" };

#define LDAC_ENUM	0
#define RDAC_ENUM	1
#define LHPCOM_ENUM	2
#define RHPCOM_ENUM	3
#define ADC_HPF_ENUM	4

static const struct soc_enum aic31_enum[] = {
	SOC_ENUM_SINGLE(DAC_LINE_MUX, 6, 3, aic31_left_dac_mux),
	SOC_ENUM_SINGLE(DAC_LINE_MUX, 4, 3, aic31_right_dac_mux),
	SOC_ENUM_SINGLE(HPLCOM_CFG, 4, 3, aic31_left_hpcom_mux),
	SOC_ENUM_SINGLE(HPRCOM_CFG, 3, 5, aic31_right_hpcom_mux),
	SOC_ENUM_DOUBLE(AIC31_CODEC_DFILT_CTRL, 6, 4, 4, aic31_adc_hpf),
};

static const struct snd_kcontrol_new aic31_snd_controls[] = {
	/* Output */
	SOC_DOUBLE_R("PCM Playback Volume", LDAC_VOL, RDAC_VOL, 0, 0x7f, 1),

	SOC_DOUBLE_R("Line DAC Playback Volume", DACL1_2_LLOPM_VOL,
		     DACR1_2_RLOPM_VOL, 0, 0x7f, 1),
	SOC_DOUBLE_R("Line DAC Playback Switch", LLOPM_CTRL, RLOPM_CTRL, 3,
		     0x01, 0),
	SOC_DOUBLE_R("Line PGA Bypass Playback Volume", PGAL_2_LLOPM_VOL,
		     PGAR_2_RLOPM_VOL, 0, 0x7f, 1),

	SOC_DOUBLE_R("HP DAC Playback Volume", DACL1_2_HPLOUT_VOL,
		     DACR1_2_HPROUT_VOL, 0, 0x7f, 1),
	SOC_DOUBLE_R("HP DAC Playback Switch", HPLOUT_CTRL, HPROUT_CTRL, 3,
		     0x01, 0),
	SOC_DOUBLE_R("HP PGA Bypass Playback Volume", PGAL_2_HPLOUT_VOL,
		     PGAR_2_HPROUT_VOL, 0, 0x7f, 1),

	SOC_DOUBLE_R("HPCOM DAC Playback Volume", DACL1_2_HPLCOM_VOL,
		     DACR1_2_HPRCOM_VOL, 0, 0x7f, 1),
	SOC_DOUBLE_R("HPCOM DAC Playback Switch", HPLCOM_CTRL, HPRCOM_CTRL, 3,
		     0x01, 0),
	SOC_DOUBLE_R("HPCOM PGA Bypass Playback Volume", PGAL_2_HPLCOM_VOL,
		     PGAR_2_HPRCOM_VOL, 0, 0x7f, 1),

	/*
	 * Note: enable Automatic input Gain Controller with care. It can
	 * adjust PGA to max value when ADC is on and will never go back.
	*/
	SOC_DOUBLE_R("AGC Switch", LAGC_CTRL_A, RAGC_CTRL_A, 7, 0x01, 0),

	/* Input */
	SOC_DOUBLE_R("PGA Capture Volume", LADC_VOL, RADC_VOL, 0, 0x7f, 0),
	SOC_DOUBLE_R("PGA Capture Switch", LADC_VOL, RADC_VOL, 7, 0x01, 1),

	SOC_ENUM("ADC HPF Cut-off", aic31_enum[ADC_HPF_ENUM]),
};

/* add non dapm controls */
static int aic31_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(aic31_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				  snd_soc_cnew(&aic31_snd_controls[i],
					       codec, NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

/* Left DAC Mux */
static const struct snd_kcontrol_new aic31_left_dac_mux_controls =
SOC_DAPM_ENUM("Route", aic31_enum[LDAC_ENUM]);

/* Right DAC Mux */
static const struct snd_kcontrol_new aic31_right_dac_mux_controls =
SOC_DAPM_ENUM("Route", aic31_enum[RDAC_ENUM]);

/* Left HPCOM Mux */
static const struct snd_kcontrol_new aic31_left_hpcom_mux_controls =
SOC_DAPM_ENUM("Route", aic31_enum[LHPCOM_ENUM]);

/* Right HPCOM Mux */
static const struct snd_kcontrol_new aic31_right_hpcom_mux_controls =
SOC_DAPM_ENUM("Route", aic31_enum[RHPCOM_ENUM]);

/* Left DAC_L1 Mixer */
static const struct snd_kcontrol_new aic31_left_dac_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Switch", DACL1_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", DACL1_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", DACL1_2_HPLCOM_VOL, 7, 1, 0),
};

/* Right DAC_R1 Mixer */
static const struct snd_kcontrol_new aic31_right_dac_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Switch", DACR1_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", DACR1_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", DACR1_2_HPRCOM_VOL, 7, 1, 0),
};

/* Left PGA Mixer */
static const struct snd_kcontrol_new aic31_left_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE_AIC31("Line2L Switch", LINE2LR_2_LADC_CTRL, 4, 1, 1),
};

/* Right PGA Mixer */
static const struct snd_kcontrol_new aic31_right_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE_AIC31("Line2R Switch", LINE2LR_2_RADC_CTRL, 4, 1, 1),
};

/* Left PGA Bypass Mixer */
static const struct snd_kcontrol_new aic31_left_pga_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Switch", PGAL_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", PGAL_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", PGAL_2_HPLCOM_VOL, 7, 1, 0),
};

/* Right PGA Bypass Mixer */
static const struct snd_kcontrol_new aic31_right_pga_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Switch", PGAR_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", PGAR_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", PGAR_2_HPRCOM_VOL, 7, 1, 0),
};

static const struct snd_soc_dapm_widget aic31_dapm_widgets[] = {
	/* Left DAC to Left Outputs */
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", DAC_PWR, 7, 0),
	SND_SOC_DAPM_MUX("Left DAC Mux", SND_SOC_NOPM, 0, 0,
			 &aic31_left_dac_mux_controls),
	SND_SOC_DAPM_MIXER("Left DAC_L1 Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_left_dac_mixer_controls[0],
			   ARRAY_SIZE(aic31_left_dac_mixer_controls)),
	SND_SOC_DAPM_MUX("Left HPCOM Mux", SND_SOC_NOPM, 0, 0,
			 &aic31_left_hpcom_mux_controls),
	SND_SOC_DAPM_PGA("Left Line Out", LLOPM_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left HP Out", HPLOUT_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left HP Com", HPLCOM_CTRL, 0, 0, NULL, 0),

	/* Right DAC to Right Outputs */
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", DAC_PWR, 6, 0),
	SND_SOC_DAPM_MUX("Right DAC Mux", SND_SOC_NOPM, 0, 0,
			 &aic31_right_dac_mux_controls),
	SND_SOC_DAPM_MIXER("Right DAC_R1 Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_right_dac_mixer_controls[0],
			   ARRAY_SIZE(aic31_right_dac_mixer_controls)),
	SND_SOC_DAPM_MUX("Right HPCOM Mux", SND_SOC_NOPM, 0, 0,
			 &aic31_right_hpcom_mux_controls),
	SND_SOC_DAPM_PGA("Right Line Out", RLOPM_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right HP Out", HPROUT_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right HP Com", HPRCOM_CTRL, 0, 0, NULL, 0),

	/* Left Inputs to Left ADC */
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", LINE1L_2_LADC_CTRL, 2, 0),
	SND_SOC_DAPM_MIXER("Left PGA Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_left_pga_mixer_controls[0],
			   ARRAY_SIZE(aic31_left_pga_mixer_controls)),

	/* Right Inputs to Right ADC */
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", LINE1R_2_RADC_CTRL, 2, 0),
	SND_SOC_DAPM_MIXER("Right PGA Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_right_pga_mixer_controls[0],
			   ARRAY_SIZE(aic31_right_pga_mixer_controls)),

	/*
	 * Also similar function like mic bias. Selects digital mic with
	 * configurable oversampling rate instead of ADC converter.
	 */
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 128",
			 AIC31_ASD_INTF_CTRLA, 0, 3, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 64",
			 AIC31_ASD_INTF_CTRLA, 0, 3, 2, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 32",
			 AIC31_ASD_INTF_CTRLA, 0, 3, 3, 0),

	/* Mic Bias */
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias 2V",
			 MICBIAS_CTRL, 6, 3, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias 2.5V",
			 MICBIAS_CTRL, 6, 3, 2, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias AVDD",
			 MICBIAS_CTRL, 6, 3, 3, 0),

	/* Left PGA to Left Output bypass */
	SND_SOC_DAPM_MIXER("Left PGA Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_left_pga_bp_mixer_controls[0],
			   ARRAY_SIZE(aic31_left_pga_bp_mixer_controls)),

	/* Right PGA to Right Output bypass */
	SND_SOC_DAPM_MIXER("Right PGA Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic31_right_pga_bp_mixer_controls[0],
			   ARRAY_SIZE(aic31_right_pga_bp_mixer_controls)),

	SND_SOC_DAPM_OUTPUT("LLOUT"),
	SND_SOC_DAPM_OUTPUT("RLOUT"),
	SND_SOC_DAPM_OUTPUT("HPLOUT"),
	SND_SOC_DAPM_OUTPUT("HPROUT"),
	SND_SOC_DAPM_OUTPUT("HPLCOM"),
	SND_SOC_DAPM_OUTPUT("HPRCOM"),

	SND_SOC_DAPM_INPUT("LINE2L"),
	SND_SOC_DAPM_INPUT("LINE2R"),
};

static const struct snd_soc_dapm_route intercon[] = {
	/* Left Output */
	{"Left DAC Mux", "DAC_L1", "Left DAC"},
	{"Left DAC Mux", "DAC_L2", "Left DAC"},
	{"Left DAC Mux", "DAC_L3", "Left DAC"},

	{"Left DAC_L1 Mixer", "Line Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "HP Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "HPCOM Switch", "Left DAC Mux"},
	{"Left Line Out", NULL, "Left DAC Mux"},
	{"Left HP Out", NULL, "Left DAC Mux"},

	{"Left HPCOM Mux", "differential of HPLOUT", "Left DAC_L1 Mixer"},
	{"Left HPCOM Mux", "constant VCM", "Left DAC_L1 Mixer"},
	{"Left HPCOM Mux", "single-ended", "Left DAC_L1 Mixer"},

	{"Left Line Out", NULL, "Left DAC_L1 Mixer"},
	{"Left HP Out", NULL, "Left DAC_L1 Mixer"},
	{"Left HP Com", NULL, "Left HPCOM Mux"},

	{"LLOUT", NULL, "Left Line Out"},
	{"LLOUT", NULL, "Left Line Out"},
	{"HPLOUT", NULL, "Left HP Out"},
	{"HPLCOM", NULL, "Left HP Com"},

	/* Right Output */
	{"Right DAC Mux", "DAC_R1", "Right DAC"},
	{"Right DAC Mux", "DAC_R2", "Right DAC"},
	{"Right DAC Mux", "DAC_R3", "Right DAC"},

	{"Right DAC_R1 Mixer", "Line Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "HP Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "HPCOM Switch", "Right DAC Mux"},
	{"Right Line Out", NULL, "Right DAC Mux"},
	{"Right HP Out", NULL, "Right DAC Mux"},

	{"Right HPCOM Mux", "differential of HPROUT", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "constant VCM", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "single-ended", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "differential of HPLCOM", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "external feedback", "Right DAC_R1 Mixer"},

	{"Right Line Out", NULL, "Right DAC_R1 Mixer"},
	{"Right HP Out", NULL, "Right DAC_R1 Mixer"},
	{"Right HP Com", NULL, "Right HPCOM Mux"},

	{"RLOUT", NULL, "Right Line Out"},
	{"RLOUT", NULL, "Right Line Out"},
	{"HPROUT", NULL, "Right HP Out"},
	{"HPRCOM", NULL, "Right HP Com"},

	/* Left Input */
	{"Left PGA Mixer", NULL, "LINE2L"},

	{"Left ADC", NULL, "Left PGA Mixer"},

	/* Right Input */
	{"Right PGA Mixer", NULL, "LINE2R"},

	{"Right ADC", NULL, "Right PGA Mixer"},

	/* Left PGA Bypass */
	{"Left PGA Bypass Mixer", "Line Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HP Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HPCOM Switch", "Left PGA Mixer"},

	{"Left HPCOM Mux", "differential of HPLOUT", "Left PGA Bypass Mixer"},
	{"Left HPCOM Mux", "constant VCM", "Left PGA Bypass Mixer"},
	{"Left HPCOM Mux", "single-ended", "Left PGA Bypass Mixer"},

	{"Left Line Out", NULL, "Left PGA Bypass Mixer"},
	{"Left HP Out", NULL, "Left PGA Bypass Mixer"},

	/* Right PGA Bypass */
	{"Right PGA Bypass Mixer", "Line Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HP Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HPCOM Switch", "Right PGA Mixer"},

	{"Right HPCOM Mux", "differential of HPROUT", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "constant VCM", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "single-ended", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "differential of HPLCOM", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "external feedback", "Right PGA Bypass Mixer"},

	{"Right Line Out", NULL, "Right PGA Bypass Mixer"},
	{"Right HP Out", NULL, "Right PGA Bypass Mixer"},
};

static int aic31_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, aic31_dapm_widgets,
				  ARRAY_SIZE(aic31_dapm_widgets));

	/* set up audio path interconnects */
	snd_soc_dapm_add_routes(codec, intercon, ARRAY_SIZE(intercon));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

static int aic31_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->codec;
	struct aic31_priv *aic31 = codec->private_data;
	int codec_clk = 0, bypass_pll = 0, fsref, last_clk = 0;
	u8 data, r, p, pll_q, pll_p = 1, pll_r = 1, pll_j = 1;
	u16 pll_d = 1;

	/* select data word length */
	data =
	    aic31_read_reg_cache(codec, AIC31_ASD_INTF_CTRLB) & (~(0x3 << 4));
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		data |= (0x01 << 4);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data |= (0x02 << 4);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		data |= (0x03 << 4);
		break;
	}
	aic31_write(codec, AIC31_ASD_INTF_CTRLB, data);

	/* Fsref can be 44100 or 48000 */
	fsref = (params_rate(params) % 11025 == 0) ? 44100 : 48000;

	/* Try to find a value for Q which allows us to bypass the PLL and
	 * generate CODEC_CLK directly. */
	for (pll_q = 2; pll_q < 18; pll_q++)
		if (aic31->sysclk / (128 * pll_q) == fsref) {
			bypass_pll = 1;
			break;
		}

	if (bypass_pll) {
		pll_q &= 0xf;
		aic31_write(codec, AIC31_PLL_PROGA_REG, pll_q << PLLQ_SHIFT);
		aic31_write(codec, AIC31_GPIOB_REG, CODEC_CLKIN_CLKDIV);
	} else
		aic31_write(codec, AIC31_GPIOB_REG, CODEC_CLKIN_PLLDIV);

	/* Route Left DAC to left channel input and
	 * right DAC to right channel input */
	data = (LDAC2LCH | RDAC2RCH);
	data |= (fsref == 44100) ? FSREF_44100 : FSREF_48000;
	if (params_rate(params) >= 64000)
		data |= DUAL_RATE_MODE;
	aic31_write(codec, AIC31_CODEC_DATAPATH_REG, data);

	/* codec sample rate select */
	data = (fsref * 20) / params_rate(params);
	if (params_rate(params) < 64000)
		data /= 2;
	data /= 5;
	data -= 2;
	data |= (data << 4);
	aic31_write(codec, AIC31_SAMPLE_RATE_SEL_REG, data);

	if (bypass_pll)
		return 0;

	/* Use PLL
	 * find an apropriate setup for j, d, r and p by iterating over
	 * p and r - j and d are calculated for each fraction.
	 * Up to 128 values are probed, the closest one wins the game.
	 * The sysclk is divided by 1000 to prevent integer overflows.
	 */
#ifdef CONFIG_MACH_MAGNOLIA2
	codec_clk = (2048 * fsref) / (aic31->sysclk / 10000);
#else
	codec_clk = (2048 * fsref) / (aic31->sysclk / 1000);
#endif

	for (r = 1; r <= 16; r++) {
		for (p = 1; p <= 8; p++) {
#ifdef CONFIG_MACH_MAGNOLIA2
			int clk, tmp = (codec_clk * pll_r * 1) / pll_p;
#else
			int clk, tmp = (codec_clk * pll_r * 10) / pll_p;
#endif
			u8 j = tmp / 10000;
			u16 d = tmp % 10000;

			if (j > 63)
				continue;

			if (d != 0 && aic31->sysclk < 10000000)
				continue;

			/* This is actually 1000 * ((j + (d/10000)) * r) / p
			 * The term had to be converted to get rid of the
			 * division by 10000 */
#ifdef CONFIG_MACH_MAGNOLIA2
			clk = ((10000 * j * r) + (d * r)) / (1 * p);
#else
			clk = ((10000 * j * r) + (d * r)) / (10 * p);
#endif

			/* check whether this values get closer than the best
			 * ones we had before */
			if (abs(codec_clk - clk) < abs(codec_clk - last_clk)) {
				pll_j = j; pll_d = d; pll_r = r; pll_p = p;
				last_clk = clk;
			}

			/* Early exit for exact matches */
			if (clk == codec_clk)
				break;
		}
        }

#if 0
        printk("* %s: PLL (P, R, J, D) = (%d, %d, %d, %d)\n", __FUNCTION__,
               pll_p, pll_r, pll_j, pll_d);
#endif

	if (last_clk == 0) {
		printk(KERN_ERR "%s(): unable to setup PLL\n", __func__);
		return -EINVAL;
	}

	data = aic31_read_reg_cache(codec, AIC31_PLL_PROGA_REG);
	aic31_write(codec, AIC31_PLL_PROGA_REG, data | (pll_p << PLLP_SHIFT));
	aic31_write(codec, AIC31_OVRF_STATUS_AND_PLLR_REG, pll_r << PLLR_SHIFT);
	aic31_write(codec, AIC31_PLL_PROGB_REG, pll_j << PLLJ_SHIFT);
	aic31_write(codec, AIC31_PLL_PROGC_REG, (pll_d >> 6) << PLLD_MSB_SHIFT);
	aic31_write(codec, AIC31_PLL_PROGD_REG,
		    (pll_d & 0x3F) << PLLD_LSB_SHIFT);

	return 0;
}

static int aic31_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 ldac_reg = aic31_read_reg_cache(codec, LDAC_VOL) & ~MUTE_ON;
	u8 rdac_reg = aic31_read_reg_cache(codec, RDAC_VOL) & ~MUTE_ON;

	if (mute) {
		aic31_write(codec, LDAC_VOL, ldac_reg | MUTE_ON);
		aic31_write(codec, RDAC_VOL, rdac_reg | MUTE_ON);
	} else {
		aic31_write(codec, LDAC_VOL, ldac_reg);
		aic31_write(codec, RDAC_VOL, rdac_reg);
	}

	return 0;
}

static int aic31_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct aic31_priv *aic31 = codec->private_data;

	aic31->sysclk = freq;
	return 0;
}

static int aic31_set_dai_fmt(struct snd_soc_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct aic31_priv *aic31 = codec->private_data;
	u8 iface_areg, iface_breg;

	iface_areg = aic31_read_reg_cache(codec, AIC31_ASD_INTF_CTRLA) & 0x3f;
	iface_breg = aic31_read_reg_cache(codec, AIC31_ASD_INTF_CTRLB) & 0x3f;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aic31->master = 1;
		iface_areg |= BIT_CLK_MASTER | WORD_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		aic31->master = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * match both interface format and signal polarities since they
	 * are fixed
	 */
	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_INV_MASK)) {
	case (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF):
		break;
	case (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_IB_NF):
		iface_breg |= (0x01 << 6);
		break;
	case (SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_NB_NF):
		iface_breg |= (0x02 << 6);
		break;
	case (SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF):
		iface_breg |= (0x03 << 6);
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	aic31_write(codec, AIC31_ASD_INTF_CTRLA, iface_areg);
	aic31_write(codec, AIC31_ASD_INTF_CTRLB, iface_breg);

	return 0;
}

static int aic31_set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level)
{
	struct aic31_priv *aic31 = codec->private_data;
	u8 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* all power is driven by DAPM system */
		if (aic31->master) {
			/* enable pll */
			reg = aic31_read_reg_cache(codec, AIC31_PLL_PROGA_REG);
			aic31_write(codec, AIC31_PLL_PROGA_REG,
				    reg | PLL_ENABLE);
		}
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/*
		 * all power is driven by DAPM system,
		 * so output power is safe if bypass was set
		 */
		if (aic31->master) {
			/* disable pll */
			reg = aic31_read_reg_cache(codec, AIC31_PLL_PROGA_REG);
			aic31_write(codec, AIC31_PLL_PROGA_REG,
				    reg & ~PLL_ENABLE);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* force all power off */
		reg = aic31_read_reg_cache(codec, DAC_PWR);
		aic31_write(codec, DAC_PWR, reg & ~(LDAC_PWR_ON | RDAC_PWR_ON));

		reg = aic31_read_reg_cache(codec, HPLOUT_CTRL);
		aic31_write(codec, HPLOUT_CTRL, reg & ~HPLOUT_PWR_ON);
		reg = aic31_read_reg_cache(codec, HPROUT_CTRL);
		aic31_write(codec, HPROUT_CTRL, reg & ~HPROUT_PWR_ON);

		reg = aic31_read_reg_cache(codec, HPLCOM_CTRL);
		aic31_write(codec, HPLCOM_CTRL, reg & ~HPLCOM_PWR_ON);
		reg = aic31_read_reg_cache(codec, HPRCOM_CTRL);
		aic31_write(codec, HPRCOM_CTRL, reg & ~HPRCOM_PWR_ON);

		reg = aic31_read_reg_cache(codec, LLOPM_CTRL);
		aic31_write(codec, LLOPM_CTRL, reg & ~LLOPM_PWR_ON);
		reg = aic31_read_reg_cache(codec, RLOPM_CTRL);
		aic31_write(codec, RLOPM_CTRL, reg & ~RLOPM_PWR_ON);

		if (aic31->master) {
			/* disable pll */
			reg = aic31_read_reg_cache(codec, AIC31_PLL_PROGA_REG);
			aic31_write(codec, AIC31_PLL_PROGA_REG,
				    reg & ~PLL_ENABLE);
		}
		break;
	}
	codec->bias_level = level;

	return 0;
}

void aic31_set_gpio(struct snd_soc_codec *codec, int gpio, int state)
{
	u8 reg = gpio ? AIC31_GPIO2_REG : AIC31_GPIO1_REG;
	u8 bit = gpio ? 3: 0;
	u8 val = aic31_read_reg_cache(codec, reg) & ~(1 << bit);
	aic31_write(codec, reg, val | (!!state << bit));
}
EXPORT_SYMBOL_GPL(aic31_set_gpio);

int aic31_get_gpio(struct snd_soc_codec *codec, int gpio)
{
	u8 reg = gpio ? AIC31_GPIO2_REG : AIC31_GPIO1_REG;
	u8 val, bit = gpio ? 2: 1;

	aic31_read(codec, reg, &val);
	return (val >> bit) & 1;
}
EXPORT_SYMBOL_GPL(aic31_get_gpio);

int aic31_headset_detected(struct snd_soc_codec *codec)
{
	u8 val;
	aic31_read(codec, AIC31_RT_IRQ_FLAGS_REG, &val);
	return (val >> 2) & 1;
}
EXPORT_SYMBOL_GPL(aic31_headset_detected);

#define AIC31_RATES	SNDRV_PCM_RATE_8000_96000
#define AIC31_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

struct snd_soc_dai aic31_dai = {
	.name = "tlv320aic31",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AIC31_RATES,
		.formats = AIC31_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AIC31_RATES,
		.formats = AIC31_FORMATS,},
	.ops = {
		.hw_params = aic31_hw_params,
	},
	.dai_ops = {
		.digital_mute = aic31_mute,
		.set_sysclk = aic31_set_dai_sysclk,
		.set_fmt = aic31_set_dai_fmt,
	}
};
EXPORT_SYMBOL_GPL(aic31_dai);

static int aic31_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	aic31_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int aic31_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;
	int i;
	u8 data[2];
	u8 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(aic31_reg); i++) {
		data[0] = i;
		data[1] = cache[i];
		codec->hw_write(codec->control_data, data, 2);
	}

	aic31_set_bias_level(codec, codec->suspend_bias_level);

	return 0;
}

/*
 * initialise the AIC31 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int aic31_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
	int reg, ret = 0;

	codec->name = "tlv320aic31";
	codec->owner = THIS_MODULE;
	codec->read = aic31_read_reg_cache;
	codec->write = aic31_write;
	codec->set_bias_level = aic31_set_bias_level;
	codec->dai = &aic31_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(aic31_reg);
	codec->reg_cache = kmemdup(aic31_reg, sizeof(aic31_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	aic31_write(codec, AIC31_PAGE_SELECT, PAGE0_SELECT);
	aic31_write(codec, AIC31_RESET, SOFT_RESET);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "aic31: failed to create pcms\n");
		goto pcm_err;
	}

	/* DAC default volume and mute */
	aic31_write(codec, LDAC_VOL, DEFAULT_VOL | MUTE_ON);
	aic31_write(codec, RDAC_VOL, DEFAULT_VOL | MUTE_ON);

	/* DAC to HP default volume and route to Output mixer */
	aic31_write(codec, DACL1_2_HPLOUT_VOL, DEFAULT_VOL | ROUTE_ON);
	aic31_write(codec, DACR1_2_HPROUT_VOL, DEFAULT_VOL | ROUTE_ON);
	aic31_write(codec, DACL1_2_HPLCOM_VOL, DEFAULT_VOL | ROUTE_ON);
	aic31_write(codec, DACR1_2_HPRCOM_VOL, DEFAULT_VOL | ROUTE_ON);
	/* DAC to Line Out default volume and route to Output mixer */
	aic31_write(codec, DACL1_2_LLOPM_VOL, DEFAULT_VOL | ROUTE_ON);
	aic31_write(codec, DACR1_2_RLOPM_VOL, DEFAULT_VOL | ROUTE_ON);

	/* unmute all outputs */
	reg = aic31_read_reg_cache(codec, LLOPM_CTRL);
	aic31_write(codec, LLOPM_CTRL, reg | UNMUTE);
	reg = aic31_read_reg_cache(codec, RLOPM_CTRL);
	aic31_write(codec, RLOPM_CTRL, reg | UNMUTE);
	reg = aic31_read_reg_cache(codec, HPLOUT_CTRL);
	aic31_write(codec, HPLOUT_CTRL, reg | UNMUTE);
	reg = aic31_read_reg_cache(codec, HPROUT_CTRL);
	aic31_write(codec, HPROUT_CTRL, reg | UNMUTE);
	reg = aic31_read_reg_cache(codec, HPLCOM_CTRL);
	aic31_write(codec, HPLCOM_CTRL, reg | UNMUTE);
	reg = aic31_read_reg_cache(codec, HPRCOM_CTRL);
	aic31_write(codec, HPRCOM_CTRL, reg | UNMUTE);

	/* ADC default volume and unmute */
	aic31_write(codec, LADC_VOL, DEFAULT_GAIN);
	aic31_write(codec, RADC_VOL, DEFAULT_GAIN);
	/* By default route Line2 to ADC PGA mixer */
	aic31_write(codec, LINE2LR_2_LADC_CTRL, 0x0f);
	aic31_write(codec, LINE2LR_2_RADC_CTRL, 0xf0);

	/* PGA to HP Bypass default volume, disconnect from Output Mixer */
	aic31_write(codec, PGAL_2_HPLOUT_VOL, DEFAULT_VOL);
	aic31_write(codec, PGAR_2_HPROUT_VOL, DEFAULT_VOL);
	aic31_write(codec, PGAL_2_HPLCOM_VOL, DEFAULT_VOL);
	aic31_write(codec, PGAR_2_HPRCOM_VOL, DEFAULT_VOL);
	/* PGA to Line Out default volume, disconnect from Output Mixer */
	aic31_write(codec, PGAL_2_LLOPM_VOL, DEFAULT_VOL);
	aic31_write(codec, PGAR_2_RLOPM_VOL, DEFAULT_VOL);

	/* Line2 to HP Bypass default volume, disconnect from Output Mixer */
	aic31_write(codec, LINE2L_2_HPLOUT_VOL, DEFAULT_VOL);
	aic31_write(codec, LINE2R_2_HPROUT_VOL, DEFAULT_VOL);
	aic31_write(codec, LINE2L_2_HPLCOM_VOL, DEFAULT_VOL);
	aic31_write(codec, LINE2R_2_HPRCOM_VOL, DEFAULT_VOL);
	/* Line2 Line Out default volume, disconnect from Output Mixer */
	aic31_write(codec, LINE2L_2_LLOPM_VOL, DEFAULT_VOL);
	aic31_write(codec, LINE2R_2_RLOPM_VOL, DEFAULT_VOL);

	/* off, with power on */
	aic31_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	aic31_add_controls(codec);
	aic31_add_widgets(codec);
	ret = snd_soc_register_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "aic31: failed to register card\n");
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

static struct snd_soc_device *aic31_socdev;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
/*
 * AIC31 2 wire address can be up to 4 devices with device addresses
 * 0x18, 0x19, 0x1A, 0x1B
 */

/*
 * If the i2c layer weren't so broken, we could pass this kind of data
 * around
 */
static int aic31_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = aic31_socdev;
	struct snd_soc_codec *codec = socdev->codec;
	int ret;

	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = aic31_init(socdev);
	if (ret < 0)
		printk(KERN_ERR "aic31: failed to initialise AIC31\n");
	return ret;
}

static int aic31_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	kfree(codec->reg_cache);
	return 0;
}

static const struct i2c_device_id aic31_i2c_id[] = {
	{ "tlv320aic31", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aic31_i2c_id);

/* machine i2c codec control layer */
static struct i2c_driver aic31_i2c_driver = {
	.driver = {
		.name = "aic31 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe = aic31_i2c_probe,
	.remove = aic31_i2c_remove,
	.id_table = aic31_i2c_id,
};

static int aic31_i2c_read(struct i2c_client *client, u8 *value, int len)
{
	value[0] = i2c_smbus_read_byte_data(client, value[0]);
	return (len == 1);
}

static int aic31_add_i2c_device(struct platform_device *pdev,
				 const struct aic31_setup_data *setup)
{
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	int ret;

	ret = i2c_add_driver(&aic31_i2c_driver);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c driver\n");
		return ret;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = setup->i2c_address;
	strlcpy(info.type, "tlv320aic31", I2C_NAME_SIZE);

	adapter = i2c_get_adapter(setup->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "can't get i2c adapter %d\n",
			setup->i2c_bus);
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	i2c_put_adapter(adapter);
	if (!client) {
		dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
			(unsigned int)info.addr);
		goto err_driver;
	}

	return 0;

err_driver:
	i2c_del_driver(&aic31_i2c_driver);
	return -ENODEV;
}
#endif

static int aic31_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct aic31_setup_data *setup;
	struct snd_soc_codec *codec;
	struct aic31_priv *aic31;
	int ret = 0;

	printk(KERN_INFO "AIC31 Audio Codec %s\n", AIC31_VERSION);

	setup = socdev->codec_data;
	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	aic31 = kzalloc(sizeof(struct aic31_priv), GFP_KERNEL);
	if (aic31 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

	codec->private_data = aic31;
	socdev->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	aic31_socdev = socdev;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	if (setup->i2c_address) {
		codec->hw_write = (hw_write_t) i2c_master_send;
		codec->hw_read = (hw_read_t) aic31_i2c_read;
		ret = aic31_add_i2c_device(pdev, setup);
	}
#else
	/* Add other interfaces here */
#endif

	if (ret != 0) {
		kfree(codec->private_data);
		kfree(codec);
	}
	return ret;
}

static int aic31_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	/* power down chip */
	if (codec->control_data)
		aic31_set_bias_level(codec, SND_SOC_BIAS_OFF);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_unregister_device(codec->control_data);
	i2c_del_driver(&aic31_i2c_driver);
#endif
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_aic31 = {
	.probe = aic31_probe,
	.remove = aic31_remove,
	.suspend = aic31_suspend,
	.resume = aic31_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_aic31);

MODULE_DESCRIPTION("ASoC TLV320AIC31 codec driver");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
