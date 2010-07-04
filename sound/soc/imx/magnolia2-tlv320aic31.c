/*
 * magnolia2-tlv320aic31.c  --  i.MX Magnolia2 Driver for TI TLV320AIC31 Codec
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009 Century Systems Co., LTd. All Rights Reserved.
 *
 * Based on imx-3stack-wm8903 (c) 2007 Wolfson Microelectronics PLC.

 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <mach/clock.h>
#include <mach/mxc.h>
#include <linux/regulator/consumer.h>

#include "imx-ssi.h"
#include "imx-pcm.h"
#include "../codecs/tlv320aic31.h"

/* SSI BCLK and LRC master */
#define TLV320AIC31_SSI_MASTER	1

extern struct snd_soc_dai aic31_dai;
extern struct snd_soc_codec_device soc_codec_dev_aic31;

extern void gpio_activate_audio_port(void);

struct magnolia2_priv {
	struct platform_device *pdev;
};

static struct magnolia2_priv machine_priv;

/*
 * Magnolia2 tlv320aic31 HiFi DAI operations.
 */
static void magnolia2_init_dam(int ssi_port, int dai_port)
{
	/* TLV320AIC31 uses SSI1 via AUDMUX port dai_port for audio */
	unsigned int ssi_ptcr = 0;
	unsigned int dai_ptcr = 0;
	unsigned int ssi_pdcr = 0;
	unsigned int dai_pdcr = 0;

	__raw_writel(0, DAM_PTCR(ssi_port));
	__raw_writel(0, DAM_PTCR(dai_port));
	__raw_writel(0, DAM_PDCR(ssi_port));
	__raw_writel(0, DAM_PDCR(dai_port));

	/* set to synchronous */
	ssi_ptcr |= AUDMUX_PTCR_SYN;
	dai_ptcr |= AUDMUX_PTCR_SYN;

#if TLV320AIC31_SSI_MASTER
	/* set Rx sources ssi_port <--> dai_port */
	ssi_pdcr |= AUDMUX_PDCR_RXDSEL(dai_port);
        dai_pdcr |= AUDMUX_PDCR_RXDSEL(ssi_port);

	/* set Tx frame direction and source  dai_port--> ssi_port output */
	ssi_ptcr |= AUDMUX_PTCR_TFSDIR;
	ssi_ptcr |= AUDMUX_PTCR_TFSSEL(AUDMUX_FROM_TXFS, dai_port);

	/* set Tx Clock direction and source dai_port--> ssi_port output */
	ssi_ptcr |= AUDMUX_PTCR_TCLKDIR;
	ssi_ptcr |= AUDMUX_PTCR_TCSEL(AUDMUX_FROM_TXFS, dai_port);
#else
	/* set Rx sources ssi_port <--> dai_port */
	ssi_pdcr |= AUDMUX_PDCR_RXDSEL(dai_port);
	dai_pdcr |= AUDMUX_PDCR_RXDSEL(ssi_port);

	/* set Tx frame direction and source  ssi_port --> dai_port output */
	dai_ptcr |= AUDMUX_PTCR_TFSDIR;
	dai_ptcr |= AUDMUX_PTCR_TFSSEL(AUDMUX_FROM_TXFS, ssi_port);

	/* set Tx Clock direction and source ssi_port--> dai_port output */
	dai_ptcr |= AUDMUX_PTCR_TCLKDIR;
	dai_ptcr |= AUDMUX_PTCR_TCSEL(AUDMUX_FROM_TXFS, ssi_port);
#endif

	__raw_writel(ssi_ptcr, DAM_PTCR(ssi_port));
	__raw_writel(dai_ptcr, DAM_PTCR(dai_port));
	__raw_writel(ssi_pdcr, DAM_PDCR(ssi_port));
	__raw_writel(dai_pdcr, DAM_PDCR(dai_port));
}

static int magnolia2_hifi_hw_params(struct snd_pcm_substream *substream,
                                    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_link *pcm_link = rtd->dai;
	struct snd_soc_dai *cpu_dai = pcm_link->cpu_dai;
	struct snd_soc_dai *codec_dai = pcm_link->codec_dai;
	unsigned int channels = params_channels(params);
	int ret = 0;
	u32 dai_format;

#if TLV320AIC31_SSI_MASTER
	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
                SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_SYNC;
#else
	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
                SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_SYNC;
#endif
	if (channels == 2)
		dai_format |= SND_SOC_DAIFMT_TDM;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if (ret < 0) {
                printk("%s: snd_soc_dai_set_fmt(codec) failed.\n", __FUNCTION__);
		return ret;
        }

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
	if (ret < 0) {
                printk("%s: snd_soc_dai_set_fmt(cpu) failed.\n", __FUNCTION__);
		return ret;
        }

	/* set i.MX active slot mask */
	snd_soc_dai_set_tdm_slot(cpu_dai,
				 channels == 1 ? 0xfffffffe : 0xfffffffc, 2);

	/* set the SSI system clock as input (unused) */
	snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0, SND_SOC_CLOCK_IN);

	snd_soc_dai_set_sysclk(codec_dai, 0, 12000000, SND_SOC_CLOCK_IN);

	return 0;
}

/*
 * magnolia2 tlv320aic31 HiFi DAI operations.
 */
static struct snd_soc_ops magnolia2_hifi_ops = {
	.hw_params = magnolia2_hifi_hw_params,
};

static int magnolia2_aic31_init(struct snd_soc_codec *codec)
{
	return 0;
}

static struct snd_soc_dai magnolia2_cpu_dai;

static struct snd_soc_dai_link magnolia2_dai = {
	.name = "tlv320aic31",
	.stream_name = "tlv320aic31",
	.cpu_dai = &magnolia2_cpu_dai,
	.codec_dai = &aic31_dai,
	.init = magnolia2_aic31_init,
	.ops = &magnolia2_hifi_ops,
};

static struct snd_soc_machine snd_soc_machine_magnolia2 = {
	.name = "magnolia2",
	.dai_link = &magnolia2_dai,
	.num_links = 1,
};

static struct aic31_setup_data magnolia2_aic31_setup = {
	.i2c_bus = 1,
	.i2c_address = 0x18,
	.gpio_func[0] = AIC31_GPIO1_FUNC_DISABLED,
	.gpio_func[1] = AIC31_GPIO1_FUNC_DISABLED,
};

static struct snd_soc_device magnolia2_snd_devdata = {
	.machine = &snd_soc_machine_magnolia2,
	.platform = &imx_soc_platform,
	.codec_dev = &soc_codec_dev_aic31,
        .codec_data = &magnolia2_aic31_setup,
};

/*
 * This function will register the snd_soc_pcm_link drivers.
 * It also registers devices for platform DMA, I2S, SSP and registers an
 * I2C driver to probe the codec.
 */
static int __init magnolia2_aic31_probe(struct platform_device *pdev)
{
	struct mxc_audio_platform_data *dev_data = pdev->dev.platform_data;
	struct magnolia2_priv *priv = &machine_priv;
	int ret = 0;

        dev_data->init();

	/* magnolia2 tlv320aic31 hifi interface */
	imx_ssi_dai_init(&magnolia2_cpu_dai);
	if (dev_data->src_port == 1)
		magnolia2_cpu_dai.name = "imx-ssi-1";
	else
		magnolia2_cpu_dai.name = "imx-ssi-3";

	/* Configure audio port 4 */
	gpio_activate_audio_ports();
	magnolia2_init_dam(dev_data->src_port, dev_data->ext_port);

	priv->pdev = pdev;
	return ret;
}

static int __devexit magnolia2_aic31_remove(struct platform_device *pdev)
{
	gpio_inactivate_audio_ports();

	return 0;
}

static struct platform_driver magnolia2_tlv320aic31_audio_driver = {
	.probe = magnolia2_aic31_probe,
	.remove = __devexit_p(magnolia2_aic31_remove),
	.driver = {
                .name = "magnolia2-aic31",
                .owner = THIS_MODULE,
        },
};

static struct platform_device *magnolia2_snd_device;

extern int magnolia2_is_audio_enable(void);
static int __init magnolia2_asoc_init(void)
{
        int ret;

        if (magnolia2_is_audio_enable() != 0)
                return -1;

	ret = platform_driver_register(&magnolia2_tlv320aic31_audio_driver);
	if (ret < 0)
		goto exit;

        magnolia2_snd_device = platform_device_alloc("soc-audio", 3);
        if (!magnolia2_snd_device)
		goto err_device_alloc;

	platform_set_drvdata(magnolia2_snd_device, &magnolia2_snd_devdata);
	magnolia2_snd_devdata.dev = &magnolia2_snd_device->dev;
	ret = platform_device_add(magnolia2_snd_device);
	if (ret == 0)
		goto exit;

	platform_device_put(magnolia2_snd_device);
err_device_alloc:
	platform_driver_unregister(&magnolia2_tlv320aic31_audio_driver);
exit:
	return ret;
}

static void __exit magnolia2_asoc_exit(void)
{
	platform_driver_unregister(&magnolia2_tlv320aic31_audio_driver);
	platform_device_unregister(magnolia2_snd_device);
}

module_init(magnolia2_asoc_init);
module_exit(magnolia2_asoc_exit);

MODULE_DESCRIPTION("ALSA SoC TLV320AIC31 Driver for Magnolia2");
MODULE_LICENSE("GPL");
