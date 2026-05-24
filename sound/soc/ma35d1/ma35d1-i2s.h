/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Emac Inc.
 */

#ifndef _MA35D1_I2S_H
#define _MA35D1_I2S_H

#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/dmaengine.h>
#include <sound/dmaengine_pcm.h>
#include <linux/platform_data/dma-ma35d1.h>

struct clk;
struct device;


struct ma35d1_i2s {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;

	dma_addr_t phys_base;

	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;

	struct ma35d1_peripheral playback_pcfg;
	struct ma35d1_peripheral capture_pcfg;

	spinlock_t lock;

	unsigned int fmt;
	unsigned int rate;
	unsigned int channels;
	unsigned int sample_width;
	unsigned int slot_width;
	unsigned int mclk_rate;
	unsigned int bclk_rate;
	unsigned int early_mclk_rate;


	bool playback_active;
	bool capture_active;
	bool keep_mclk;
};

int devm_ma35d1_pcm_register(struct device *dev);

#endif /* _MA35D1_I2S_H */
