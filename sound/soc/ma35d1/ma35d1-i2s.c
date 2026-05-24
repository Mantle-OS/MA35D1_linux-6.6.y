// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Emac Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "ma35d1-i2s.h"
#include "ma35d1-i2s-regs.h"


static inline u32 ma35d1_i2s_read(struct ma35d1_i2s *i2s, unsigned int reg)
{
	return readl(i2s->regs + reg);
}

static inline void ma35d1_i2s_write(struct ma35d1_i2s *i2s,
				    unsigned int reg,
				    u32 val)
{
	writel(val, i2s->regs + reg);
}

static inline void ma35d1_i2s_update_bits(struct ma35d1_i2s *i2s,
					  unsigned int reg,
					  u32 mask,
					  u32 val)
{
	u32 tmp;

	tmp = ma35d1_i2s_read(i2s, reg);
	tmp &= ~mask;
	tmp |= val & mask;
	ma35d1_i2s_write(i2s, reg, tmp);
}


static void ma35d1_i2s_update_bits_locked(struct ma35d1_i2s *i2s,
					  unsigned int reg,
					  u32 mask,
					  u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&i2s->lock, flags);
	ma35d1_i2s_update_bits(i2s, reg, mask, val);
	spin_unlock_irqrestore(&i2s->lock, flags);
}


static int ma35d1_i2s_parse_endpoint_clock(struct platform_device *pdev,
					   struct ma35d1_i2s *i2s)
{
	struct device_node *ep;
	u32 rate;
	int ret = 0;

	ep = of_graph_get_next_endpoint(pdev->dev.of_node, NULL);
	if (!ep)
		return 0;

	if (!of_property_read_bool(ep, "system-clock-direction-out"))
		goto out_put;

	ret = of_property_read_u32(ep, "system-clock-frequency", &rate);
	if (ret) {
		ret = dev_err_probe(&pdev->dev, ret,
				"system-clock-direction-out requires system-clock-frequency\n");
		goto out_put;
	}

	i2s->early_mclk_rate = rate;
	i2s->mclk_rate = rate;
	i2s->keep_mclk = true;

	dev_info(&pdev->dev, "enabling early MCLK at %u Hz\n", rate);

out_put:
	of_node_put(ep);
	return ret;
}

static int ma35d1_i2s_program_mclk(struct ma35d1_i2s *i2s)
{
	unsigned long parent_rate;
	u32 mclkdiv;
	u32 val;

	if (!i2s->mclk_rate) {
		return 0;
	}

	parent_rate = clk_get_rate(i2s->clk);
	if (!parent_rate) {
		return -EINVAL;
	}
	mclkdiv = DIV_ROUND_CLOSEST(parent_rate, i2s->mclk_rate * 2) - 1;

	if (mclkdiv > FIELD_MAX(MA35D1_I2S_CLKDIV_MCLKDIV)) {
		return -EINVAL;
	}

	val = FIELD_PREP(MA35D1_I2S_CLKDIV_MCLKDIV, mclkdiv);

	ma35d1_i2s_update_bits(i2s,
				MA35D1_I2S_CLKDIV,
				MA35D1_I2S_CLKDIV_MCLKDIV,
				val);

	return 0;
}


//
static int ma35d1_i2s_dai_probe(struct snd_soc_dai *dai);

static int ma35d1_i2s_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai);

static void ma35d1_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai);

static int ma35d1_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai);

static int ma35d1_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt);

static int ma35d1_i2s_set_sysclk(struct snd_soc_dai *dai,
				 int clk_id,
				 unsigned int freq,
				 int dir);

static int ma35d1_i2s_set_bclk_ratio(struct snd_soc_dai *dai,
				     unsigned int ratio);

static int ma35d1_i2s_trigger(struct snd_pcm_substream *substream,
			      int cmd,
			      struct snd_soc_dai *dai);

static const struct snd_soc_dai_ops ma35d1_i2s_dai_ops = {
	.probe		= ma35d1_i2s_dai_probe,
	.startup	= ma35d1_i2s_startup,
	.shutdown	= ma35d1_i2s_shutdown,
	.hw_params	= ma35d1_i2s_hw_params,
	.set_fmt	= ma35d1_i2s_set_fmt,
	.set_sysclk	= ma35d1_i2s_set_sysclk,
	.set_bclk_ratio	= ma35d1_i2s_set_bclk_ratio,
	.trigger	= ma35d1_i2s_trigger,
};

#define MA35D1_I2S_RATES	(SNDRV_PCM_RATE_8000_48000)
#define MA35D1_I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver ma35d1_i2s_dai = {
	.name = "ma35d1-i2s",

	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= MA35D1_I2S_RATES,
		.formats	= MA35D1_I2S_FORMATS,
	},

	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= MA35D1_I2S_RATES,
		.formats	= MA35D1_I2S_FORMATS,
	},

	.ops = &ma35d1_i2s_dai_ops,
};

static const struct snd_soc_component_driver ma35d1_i2s_component = {
	.name = "ma35d1-i2s",
};


// ADD the rest.
static int ma35d1_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct ma35d1_i2s *i2s = dev_get_drvdata(dai->dev);

	snd_soc_dai_init_dma_data(dai,
				&i2s->playback_dma_data,
				&i2s->capture_dma_data);

	snd_soc_dai_set_drvdata(dai, i2s);

	return 0;
}

static int ma35d1_i2s_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = pm_runtime_resume_and_get(i2s->dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void ma35d1_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	pm_runtime_put(i2s->dev);
}

static int ma35d1_i2s_width_value(unsigned int width)
{
	switch (width) {
	case 16:
		return MA35D1_I2S_CTL0_DATWIDTH_16;
	case 24:
		return MA35D1_I2S_CTL0_DATWIDTH_24;
	case 32:
		return MA35D1_I2S_CTL0_DATWIDTH_32;
	default:
		return -EINVAL;
	}
}

static int ma35d1_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	unsigned int width = params_width(params);
	unsigned int slot_width;
	int width_val;
	u32 val;

	width_val = ma35d1_i2s_width_value(width);
	if (width_val < 0)
		return width_val;

	/*
	 * For now, use the sample width as the slot width. If the card/profile
	 * later sets a wider slot with TDM helpers, this can be overridden.
	 */
	slot_width = width;

	i2s->rate = rate;
	i2s->channels = channels;
	i2s->sample_width = width;
	i2s->slot_width = slot_width;
	i2s->bclk_rate = rate * channels * slot_width;

	val = FIELD_PREP(MA35D1_I2S_CTL0_DATWIDTH, width_val) |
	      FIELD_PREP(MA35D1_I2S_CTL0_CHWIDTH, width_val);

	if (channels == 1)
		val |= MA35D1_I2S_CTL0_MONO;

	ma35d1_i2s_update_bits_locked(i2s, MA35D1_I2S_CTL0,
				       MA35D1_I2S_CTL0_DATWIDTH |
				       MA35D1_I2S_CTL0_CHWIDTH |
				       MA35D1_I2S_CTL0_MONO,
				       val);

	return 0;
}

static int ma35d1_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 mask = 0;
	u32 val = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/*
		 * Preserve the vendor behavior for now: ORDER cleared for I2S.
		 * We still need to verify the exact FORMAT/ORDER meaning from
		 * the MA35D1 TRM before adding more formats.
		 */
		mask |= MA35D1_I2S_CTL0_ORDER;
		val &= ~MA35D1_I2S_CTL0_ORDER;
		break;

	case SND_SOC_DAIFMT_MSB:
		mask |= MA35D1_I2S_CTL0_ORDER;
		val |= MA35D1_I2S_CTL0_ORDER;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* CPU provides BCLK and LRCLK. */
		mask |= MA35D1_I2S_CTL0_SLAVE;
		val &= ~MA35D1_I2S_CTL0_SLAVE;
		break;

	case SND_SOC_DAIFMT_CBM_CFM:
		/* Codec provides BCLK and LRCLK. */
		mask |= MA35D1_I2S_CTL0_SLAVE;
		val |= MA35D1_I2S_CTL0_SLAVE;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * The old vendor driver ignored clock inversion. Keep that unsupported
	 * for now rather than silently lying.
	 */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	i2s->fmt = fmt;

	ma35d1_i2s_update_bits_locked(i2s, MA35D1_I2S_CTL0, mask, val);

	return 0;
}

static int ma35d1_i2s_program_dividers(struct ma35d1_i2s *i2s)
{
	unsigned long parent_rate;
	unsigned int bclk_rate;
	unsigned int mclk_rate;
	u32 bclkdiv;
	u32 mclkdiv;
	u32 val;

	parent_rate = clk_get_rate(i2s->clk);
	if (!parent_rate) {
		return -EINVAL;
	}

	bclk_rate = i2s->bclk_rate;
	mclk_rate = i2s->mclk_rate;

	if (!bclk_rate)
		return 0;

	/*
	 * Divider formula needs TRM verification.
	 *
	 * Vendor code used:
	 *     bclkdiv = round((parent / bclk) / 2) - 1
	 *     mclkdiv = (parent / mclk) >> 1
	 *
	 * Keep the same broad formula for first bring-up, but program the
	 * fields by mask so BCLKDIV and MCLKDIV cannot be accidentally swapped.
	 */
	bclkdiv = DIV_ROUND_CLOSEST(parent_rate, bclk_rate * 2) - 1;

	if (mclk_rate)
		mclkdiv = DIV_ROUND_CLOSEST(parent_rate, mclk_rate * 2) - 1;
	else
		mclkdiv = 0;

	if (bclkdiv > FIELD_MAX(MA35D1_I2S_CLKDIV_BCLKDIV))
		return -EINVAL;

	if (mclkdiv > FIELD_MAX(MA35D1_I2S_CLKDIV_MCLKDIV))
		return -EINVAL;

	val = FIELD_PREP(MA35D1_I2S_CLKDIV_BCLKDIV, bclkdiv) |
	      FIELD_PREP(MA35D1_I2S_CLKDIV_MCLKDIV, mclkdiv);

	ma35d1_i2s_write(i2s, MA35D1_I2S_CLKDIV, val);

	return 0;
}

static int ma35d1_i2s_set_sysclk(struct snd_soc_dai *dai,
				 int clk_id,
				 unsigned int freq,
				 int dir)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (dir != SND_SOC_CLOCK_OUT && dir != SND_SOC_CLOCK_IN)
		return -EINVAL;

	/*
	 * For CPU-master operation, this is the generated or requested MCLK.
	 * For CPU-slave operation, this may describe the incoming clock rate.
	 */
	i2s->mclk_rate = freq;

	return ma35d1_i2s_program_dividers(i2s);
}

static int ma35d1_i2s_set_bclk_ratio(struct snd_soc_dai *dai,
				     unsigned int ratio)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (!i2s->rate)
		return -EINVAL;

	i2s->bclk_rate = i2s->rate * ratio;

	return ma35d1_i2s_program_dividers(i2s);
}

static int ma35d1_i2s_trigger(struct snd_pcm_substream *substream,
			      int cmd,
			      struct snd_soc_dai *dai)
{
	struct ma35d1_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	bool playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned long flags;
	u32 mask;
	u32 val;
	int ret = 0;

	spin_lock_irqsave(&i2s->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mask = MA35D1_I2S_CTL0_I2S_EN |
		       MA35D1_I2S_CTL0_MCLKEN;

		val = MA35D1_I2S_CTL0_I2S_EN |
		      MA35D1_I2S_CTL0_MCLKEN;

		if (playback) {
			mask |= MA35D1_I2S_CTL0_TX_EN |
				MA35D1_I2S_CTL0_TXPDMAEN;
			val |= MA35D1_I2S_CTL0_TX_EN |
			       MA35D1_I2S_CTL0_TXPDMAEN;
			i2s->playback_active = true;
		} else {
			mask |= MA35D1_I2S_CTL0_RX_EN |
				MA35D1_I2S_CTL0_RXPDMAEN;
			val |= MA35D1_I2S_CTL0_RX_EN |
			       MA35D1_I2S_CTL0_RXPDMAEN;
			i2s->capture_active = true;
		}

		ma35d1_i2s_update_bits(i2s, MA35D1_I2S_CTL0, mask, val);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback) {
			i2s->playback_active = false;

			mask = MA35D1_I2S_CTL0_TX_EN |
			       MA35D1_I2S_CTL0_TXPDMAEN;
		} else {
			i2s->capture_active = false;

			mask = MA35D1_I2S_CTL0_RX_EN |
			       MA35D1_I2S_CTL0_RXPDMAEN;
		}

		val = 0;

		/*
		 * Only shut down the global I2S block and MCLK when both
		 * streams are inactive.
		 *
		 * If keep_mclk is set, leave I2S_EN/MCLKEN enabled so codecs
		 * such as SGTL5000 keep their probe/runtime SYS_MCLK.
		 */
		if (!i2s->playback_active && !i2s->capture_active &&
		    !i2s->keep_mclk)
			mask |= MA35D1_I2S_CTL0_I2S_EN |
				MA35D1_I2S_CTL0_MCLKEN;

		ma35d1_i2s_update_bits(i2s, MA35D1_I2S_CTL0, mask, val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&i2s->lock, flags);

	return ret;
}

// *********************************
// PLATFORM SIDE
// *********************************

static int ma35d1_i2s_setup_dma(struct platform_device *pdev,
				struct ma35d1_i2s *i2s)
{
	u32 tx_reqsel;
	u32 rx_reqsel;
	int ret;

	ret = device_property_read_u32(&pdev->dev, "nuvoton,tx-dma-reqsel",
				       &tx_reqsel);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "missing nuvoton,tx-dma-reqsel\n");

	ret = device_property_read_u32(&pdev->dev, "nuvoton,rx-dma-reqsel",
				       &rx_reqsel);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "missing nuvoton,rx-dma-reqsel\n");

	i2s->playback_pcfg.reqsel = tx_reqsel;
	i2s->capture_pcfg.reqsel = rx_reqsel;

	i2s->playback_dma_data.addr = i2s->phys_base + MA35D1_I2S_TXFIFO;
	i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma_data.peripheral_config = &i2s->playback_pcfg;
	i2s->playback_dma_data.peripheral_size = sizeof(i2s->playback_pcfg);

	i2s->capture_dma_data.addr = i2s->phys_base + MA35D1_I2S_RXFIFO;
	i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma_data.peripheral_config = &i2s->capture_pcfg;
	i2s->capture_dma_data.peripheral_size = sizeof(i2s->capture_pcfg);

	return 0;
}

static int ma35d1_i2s_runtime_suspend(struct device *dev)
{
	struct ma35d1_i2s *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk);

	return 0;
}

static int ma35d1_i2s_runtime_resume(struct device *dev)
{
	struct ma35d1_i2s *i2s = dev_get_drvdata(dev);

	return clk_prepare_enable(i2s->clk);
}

static const struct dev_pm_ops ma35d1_i2s_pm_ops = {
	RUNTIME_PM_OPS(ma35d1_i2s_runtime_suspend,
		       ma35d1_i2s_runtime_resume,
		       NULL)
};

static int ma35d1_i2s_drvprobe(struct platform_device *pdev)
{
	struct ma35d1_i2s *i2s;
	struct resource *res;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->dev = &pdev->dev;
	spin_lock_init(&i2s->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return dev_err_probe(&pdev->dev, -ENODEV,
					"missing memory resource\n");

	i2s->phys_base = res->start;

	i2s->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2s->regs))
		return PTR_ERR(i2s->regs);

	i2s->clk = devm_clk_get(&pdev->dev, "i2s");
	if (IS_ERR(i2s->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2s->clk),
					"failed to get i2s clock\n");

	platform_set_drvdata(pdev, i2s);

	ret = ma35d1_i2s_setup_dma(pdev, i2s);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);


	ret = ma35d1_i2s_parse_endpoint_clock(pdev, i2s);
	if (ret) {
		goto err_pm_disable;
	}

	if (i2s->keep_mclk) {
		ret = pm_runtime_resume_and_get(&pdev->dev);
		if (ret < 0) {
			goto err_pm_disable;
		}
		ret = ma35d1_i2s_program_mclk(i2s);

		if (ret) {
			goto err_pm_put;
		}
		ma35d1_i2s_update_bits(i2s,
				MA35D1_I2S_CTL0,
				MA35D1_I2S_CTL0_I2S_EN |
				MA35D1_I2S_CTL0_MCLKEN,
				MA35D1_I2S_CTL0_I2S_EN |
				MA35D1_I2S_CTL0_MCLKEN);
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					&ma35d1_i2s_component,
					&ma35d1_i2s_dai, 1);
	if (ret)
		goto err_pm_disable;

	ret = devm_ma35d1_pcm_register(&pdev->dev);
	if (ret)
		goto err_pm_disable;

	return 0;

err_pm_put:
	if (i2s->keep_mclk)
		pm_runtime_put_sync(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int ma35d1_i2s_drvremove(struct platform_device *pdev)
{
	struct ma35d1_i2s *i2s = platform_get_drvdata(pdev);

	if (i2s->keep_mclk) {
		ma35d1_i2s_update_bits(i2s,
				MA35D1_I2S_CTL0,
				MA35D1_I2S_CTL0_I2S_EN |
				MA35D1_I2S_CTL0_MCLKEN,
				0);
		pm_runtime_put_sync(&pdev->dev);
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id ma35d1_i2s_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-i2s" },
	{ }
};
MODULE_DEVICE_TABLE(of, ma35d1_i2s_of_match);

static struct platform_driver ma35d1_i2s_driver = {
	.driver = {
		.name = "ma35d1-i2s",
		.of_match_table = ma35d1_i2s_of_match,
		.pm = pm_ptr(&ma35d1_i2s_pm_ops),
	},
	.probe = ma35d1_i2s_drvprobe,
	.remove = ma35d1_i2s_drvremove,
};

module_platform_driver(ma35d1_i2s_driver);

MODULE_DESCRIPTION("MA35D1 IIS SoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ma35d1-i2s");
