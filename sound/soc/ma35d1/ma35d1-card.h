/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Emac Inc.
 */

#ifndef _MA35D1_CARD_H
#define _MA35D1_CARD_H

#include <sound/soc.h>

struct device;
struct snd_pcm_hw_params;
struct snd_pcm_substream;

/**
 * struct ma35d1_card_profile - MA35D1 codec/card profile
 * @compatible: DT compatible string handled by this profile
 * @name: short profile name used for diagnostics
 * @codec_dai_name: expected codec DAI name, if the profile needs one
 * @default_mclk_fs: default MCLK-to-sample-rate ratio when DT omits mclk-fs
 * @init: optional runtime init hook
 * @hw_params: optional profile-specific hw_params hook
 *
 * Each supported codec/profile gets its own implementation file, for example:
 * ma35d1-card-sgtl5000.c or ma35d1-card-nau8822.c.
 */
struct ma35d1_card_profile {
	const char *compatible;
	const char *name;
	const char *codec_dai_name;

	unsigned int default_mclk_fs;

	int (*init)(struct snd_soc_pcm_runtime *rtd);
	int (*hw_params)(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params);
};

/**
 * struct ma35d1_card - MA35D1 graph-backed sound card state
 * @dev: device owning the card
 * @card: ASoC card
 * @link: DAI link for the current profile/link
 * @cpu: CPU DAI link component
 * @codec: codec DAI link component
 * @platform: platform component, if needed
 * @profile: selected codec/card profile
 * @dai_fmt: cached DAI format parsed from the graph endpoint
 * @mclk_fs: selected MCLK-to-sample-rate ratio
 */
struct ma35d1_card {
	struct device *dev;

	struct snd_soc_card card;
	struct snd_soc_dai_link link;

	struct snd_soc_dai_link_component cpu;
	struct snd_soc_dai_link_component codec;
	struct snd_soc_dai_link_component platform;

	const struct ma35d1_card_profile *profile;

	unsigned int dai_fmt;
	unsigned int mclk_fs;
};

const struct ma35d1_card_profile *ma35d1_card_get_profile(struct device *dev);

#endif /* _MA35D1_CARD_H */
