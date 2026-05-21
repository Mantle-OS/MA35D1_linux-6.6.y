// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Nuvoton technology corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_data/dma-ma35d1.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "ma35d1-i2s.h"

#define MA35D1_DMABUF_SIZE	(64 * 1024)

static const struct snd_pcm_hardware ma35d1_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,

	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,

	// .rates = SNDRV_PCM_RATE_32000 |
	// 	SNDRV_PCM_RATE_44100 |
	// 	SNDRV_PCM_RATE_48000,
	.rates = SNDRV_PCM_RATE_8000_48000,


	.channels_min       = 1,
	.channels_max       = 2,

	.buffer_bytes_max   = MA35D1_DMABUF_SIZE,
	.period_bytes_min   = 4*1024,
	.period_bytes_max   = 8*1024,
	.periods_min        = 1,
	.periods_max        = 1024,
};

static const struct snd_dmaengine_pcm_config ma35d1_dmaengine_pcm_config = {
	.pcm_hardware = &ma35d1_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = 32 * 1024,
};

int devm_ma35d1_pcm_register(struct device *dev)
{
	return devm_snd_dmaengine_pcm_register(dev,
				&ma35d1_dmaengine_pcm_config,
				SND_DMAENGINE_PCM_FLAG_COMPAT);
}
