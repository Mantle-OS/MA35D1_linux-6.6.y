// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Emac Inc.
 */

#include <linux/module.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "ma35d1-card.h"

#define MA35D1_SGTL5000_DEFAULT_MCLK_FS	256

static int ma35d1_sgtl5000_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct ma35d1_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs = priv->mclk_fs;
	unsigned int mclk_rate;
	int ret;

	if (!mclk_fs)
		mclk_fs = MA35D1_SGTL5000_DEFAULT_MCLK_FS;

	mclk_rate = rate * mclk_fs;

	/*
	 * Ask the MA35D1 CPU DAI to generate/provide MCLK for the codec.
	 * The CPU DAI owns the actual divider programming.
	 */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_rate, SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	/*
	 * SGTL5000 uses the external SYS_MCLK from the SoC side.
	 *
	 * Use clk_id 0 here intentionally. The SGTL5000 codec driver treats
	 * this as the external input clock path in common usage, and this keeps
	 * the MA35D1 card profile from depending on private codec constants.
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk_rate, SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	return 0;
}

const struct ma35d1_card_profile ma35d1_sgtl5000_profile = {
	.compatible		= "nuvoton,ma35d1-audio-sgtl5000",
	.name			= "MA35D1-SGTL5000",
	.codec_dai_name		= "sgtl5000",
	.default_mclk_fs	= MA35D1_SGTL5000_DEFAULT_MCLK_FS,
	.hw_params		= ma35d1_sgtl5000_hw_params,
};
