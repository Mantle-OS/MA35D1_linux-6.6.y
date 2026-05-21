// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Emac Inc.
 */

#include <linux/module.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/nau8822.h"

#include "ma35d1-card.h"

#define MA35D1_NAU8822_DEFAULT_MCLK_FS	256

static int ma35d1_nau8822_hw_params(struct snd_pcm_substream *substream,
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
	unsigned int codec_sysclk;
	int ret;

	if (!mclk_fs)
		mclk_fs = MA35D1_NAU8822_DEFAULT_MCLK_FS;

	mclk_rate = rate * mclk_fs;

	/*
	 * Ask the MA35D1 CPU DAI to provide MCLK. The I2S controller driver
	 * owns divider programming and parent clock handling.
	 */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_rate,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	/*
	 * The old Nuvoton EVB path drove the NAU8822 from the SoC-side MCLK
	 * and then used the codec PLL to derive the codec system clock.
	 *
	 * Keep that policy isolated in the NAU8822 profile.
	 */
	codec_sysclk = rate * MA35D1_NAU8822_DEFAULT_MCLK_FS;

	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8822_CLK_PLL,
				     mclk_rate, SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, mclk_rate, codec_sysclk);
	if (ret)
		return ret;

	return 0;
}

const struct ma35d1_card_profile ma35d1_nau8822_profile = {
	.compatible		= "nuvoton,ma35d1-audio-nau8822",
	.name			= "MA35D1-NAU8822",
	.codec_dai_name		= "nau8822-hifi",
	.default_mclk_fs	= MA35D1_NAU8822_DEFAULT_MCLK_FS,
	.hw_params		= ma35d1_nau8822_hw_params,
};
