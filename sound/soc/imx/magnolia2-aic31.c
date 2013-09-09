/*
 * magnolia2-aic31.c  --  SoC audio for Magnolia2 in I2S mode
 *
 * Copyright 2010 Eric Bénard, Eukréa Electromatique <eric@eukrea.com>
 *
 * based on sound/soc/s3c24xx/s3c24xx_simtec_tlv320aic23.c
 * which is Copyright 2009 Simtec Electronics
 * and on sound/soc/imx/phycore-ac97.c which is
 * Copyright 2009 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * 
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-types.h>

#include "../codecs/tlv320aic3x.h"
#include "imx-ssi.h"

#define CODEC_CLOCK 12000000

static int magnolia2_aic31_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret) {
		pr_err("%s: failed set cpu dai format\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret) {
		pr_err("%s: failed set codec dai format\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     CODEC_CLOCK, SND_SOC_CLOCK_OUT);
	if (ret) {
		pr_err("%s: failed setting codec sysclk\n", __func__);
		return ret;
	}
	snd_soc_dai_set_tdm_slot(cpu_dai, 0xffffffc, 0xffffffc, 2, 0);

	ret = snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret) {
		pr_err("can't set CPU system clock IMX_SSP_SYS_CLK\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops magnolia2_aic31_snd_ops = {
	.hw_params	= magnolia2_aic31_hw_params,
};

static struct snd_soc_dai_link magnolia2_aic31_dai = {
	.name		= "tlv320aic3x",
	.stream_name	= "TLV320AIC3x",
	.codec_dai_name = "tlv320aic3x-hifi",
	.platform_name	= "imx-fiq-pcm-audio.0",
	.codec_name	= "tlv320aic3x-codec.1-0018",
	.cpu_dai_name	= "imx-ssi.0",
	.ops		= &magnolia2_aic31_snd_ops,
};

static struct snd_soc_card magnolia2_aic31 = {
	.name		= "cpuimx-audio",
	.dai_link	= &magnolia2_aic31_dai,
	.num_links	= 1,
};

static struct platform_device *magnolia2_aic31_snd_device;

static int __init magnolia2_aic31_init(void)
{
	int ret;

	if (!machine_is_magnolia2())
		/* return happy. We might run on a totally different machine */
		return 0;

	magnolia2_aic31_snd_device = platform_device_alloc("soc-audio", -1);
	if (!magnolia2_aic31_snd_device)
		return -ENOMEM;

	platform_set_drvdata(magnolia2_aic31_snd_device, &magnolia2_aic31);
	ret = platform_device_add(magnolia2_aic31_snd_device);

	if (ret) {
		printk(KERN_ERR "ASoC: Platform device allocation failed\n");
		platform_device_put(magnolia2_aic31_snd_device);
	}

	return ret;
}

static void __exit magnolia2_aic31_exit(void)
{
	platform_device_unregister(magnolia2_aic31_snd_device);
}

module_init(magnolia2_aic31_init);
module_exit(magnolia2_aic31_exit);

MODULE_AUTHOR("Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("CPUIMX ALSA SoC driver");
MODULE_LICENSE("GPL");
