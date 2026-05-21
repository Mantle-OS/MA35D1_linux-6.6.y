# SPDX-License-Identifier: GPL-2.0-only

# MA35D1 I2S CPU DAI + DMAengine PCM helper
snd-soc-ma35d1-i2s-objs := \
	ma35d1-i2s.o \
	ma35d1-pcm.o

obj-$(CONFIG_SND_SOC_MA35D1_I2S) += snd-soc-ma35d1-i2s.o


# MA35D1 graph-backed card core
snd-soc-ma35d1-card-objs := \
	ma35d1-card.o

snd-soc-ma35d1-card-$(CONFIG_SND_SOC_MA35D1_CARD_SGTL5000) += \
	ma35d1-card-sgtl5000.o

snd-soc-ma35d1-card-$(CONFIG_SND_SOC_MA35D1_CARD_NAU8822) += \
	ma35d1-card-nau8822.o

obj-$(CONFIG_SND_SOC_MA35D1_AUDIO_CARD) += snd-soc-ma35d1-card.o
