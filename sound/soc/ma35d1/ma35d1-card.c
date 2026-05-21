// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Emac Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "ma35d1-card.h"

#ifdef CONFIG_SND_SOC_MA35D1_CARD_SGTL5000
extern const struct ma35d1_card_profile ma35d1_sgtl5000_profile;
#endif

#ifdef CONFIG_SND_SOC_MA35D1_CARD_NAU8822
extern const struct ma35d1_card_profile ma35d1_nau8822_profile;
#endif


static const struct of_device_id ma35d1_card_of_match[] = {
#ifdef CONFIG_SND_SOC_MA35D1_CARD_SGTL5000
	{
		.compatible = "nuvoton,ma35d1-audio-sgtl5000",
		.data = &ma35d1_sgtl5000_profile,
	},
#endif
#ifdef CONFIG_SND_SOC_MA35D1_CARD_NAU8822
	{
		.compatible = "nuvoton,ma35d1-audio-nau8822",
		.data = &ma35d1_nau8822_profile,
	},
#endif
	{ }
};

MODULE_DEVICE_TABLE(of, ma35d1_card_of_match);


static int ma35d1_card_parse_dai_format(struct device *dev,
					 struct device_node *ep,
					 unsigned int *fmt)
{
	const char *format;
	unsigned int dai_fmt = 0;
	bool bitclock_master;
	bool frame_master;
	bool bitclock_inv;
	bool frame_inv;
	int ret;

	ret = of_property_read_string(ep, "dai-format", &format);
	if (ret)
		return dev_err_probe(dev, ret,
				     "missing endpoint dai-format\n");

	if (!strcmp(format, "i2s"))
		dai_fmt |= SND_SOC_DAIFMT_I2S;
	else if (!strcmp(format, "left_j"))
		dai_fmt |= SND_SOC_DAIFMT_LEFT_J;
	else if (!strcmp(format, "right_j"))
		dai_fmt |= SND_SOC_DAIFMT_RIGHT_J;
	else if (!strcmp(format, "dsp_a"))
		dai_fmt |= SND_SOC_DAIFMT_DSP_A;
	else if (!strcmp(format, "dsp_b"))
		dai_fmt |= SND_SOC_DAIFMT_DSP_B;
	else
		return dev_err_probe(dev, -EINVAL,
				     "unsupported dai-format: %s\n", format);

	bitclock_inv = of_property_read_bool(ep, "bitclock-inversion");
	frame_inv = of_property_read_bool(ep, "frame-inversion");

	if (bitclock_inv && frame_inv)
		dai_fmt |= SND_SOC_DAIFMT_IB_IF;
	else if (bitclock_inv)
		dai_fmt |= SND_SOC_DAIFMT_IB_NF;
	else if (frame_inv)
		dai_fmt |= SND_SOC_DAIFMT_NB_IF;
	else
		dai_fmt |= SND_SOC_DAIFMT_NB_NF;

	bitclock_master = of_property_read_bool(ep, "bitclock-master");
	frame_master = of_property_read_bool(ep, "frame-master");

	if (bitclock_master && frame_master)
		dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
	else if (!bitclock_master && !frame_master)
		dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
	else
		return dev_err_probe(dev, -EINVAL,
				"mixed bitclock/frame master is unsupported\n");

	*fmt = dai_fmt;

	return 0;
}

static int ma35d1_card_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct ma35d1_card *priv = snd_soc_card_get_drvdata(card);

	if (priv->profile && priv->profile->hw_params)
		return priv->profile->hw_params(substream, params);

	return 0;
}

static const struct snd_soc_ops ma35d1_card_ops = {
	.hw_params = ma35d1_card_hw_params,
};

static int ma35d1_card_parse_routing(struct device *dev,
				     struct snd_soc_card *card)
{
	struct device_node *np = dev->of_node;
	int ret;

	ret = snd_soc_of_parse_audio_routing(card, "routing");
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to parse routing\n");

	ret = snd_soc_of_parse_aux_devs(card, "aux-devs");
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to parse aux-devs\n");

	ret = snd_soc_of_parse_pin_switches(card, "pin-switches");
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to parse pin-switches\n");

	/*
	 * widgets is graph-card naming. ASoC's common helper expects the
	 * property name to be supplied, so this keeps the card binding in graph
	 * vocabulary while reusing common parsing.
	 */
	ret = snd_soc_of_parse_audio_simple_widgets(card, "widgets");
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to parse widgets\n");

	if (of_property_present(np, "hp-det-gpios"))
		dev_dbg(dev, "hp-det-gpios present; jack handling not wired yet\n");

	if (of_property_present(np, "mic-det-gpios"))
		dev_dbg(dev, "mic-det-gpios present; jack handling not wired yet\n");

	return 0;
}

static int ma35d1_card_parse_endpoint_fmt(struct device *dev,
					  struct device_node *ep,
					  struct ma35d1_card *priv)
{
	unsigned int fmt;
	u32 mclk_fs;
	int ret;

	/*
	 * The graph endpoint carries dai-format, bitclock-master,
	 * frame-master, inversion flags, and mclk-fs.
	 *
	 * snd_soc_of_parse_daifmt() also follows the common simple-card style.
	 * For graph-style boolean master properties this may need adjustment
	 * after first compile/test. Keep it contained here.
	 */
	ret = ma35d1_card_parse_dai_format(dev, ep, &fmt);
	if (!ret)
		return ret;

	priv->dai_fmt = fmt;


	ret = of_property_read_u32(ep, "mclk-fs", &mclk_fs);
	if (ret)
		mclk_fs = priv->profile->default_mclk_fs;

	priv->mclk_fs = mclk_fs;

	return 0;
}

static int ma35d1_card_parse_graph_link(struct platform_device *pdev,
					struct ma35d1_card *priv)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *cpu_np;
	struct device_node *codec_np;
	struct snd_soc_dai_link *link = &priv->link;
	int ret;

	cpu_port = of_parse_phandle(np, "dais", 0);
	if (!cpu_port)
		return dev_err_probe(dev, -EINVAL, "missing dais property\n");

	cpu_ep = of_get_next_child(cpu_port, NULL);
	if (!cpu_ep) {
		ret = dev_err_probe(dev, -EINVAL,
				    "CPU DAI port has no endpoint\n");
		goto out_put_cpu_port;
	}

	codec_ep = of_graph_get_remote_endpoint(cpu_ep);
	if (!codec_ep) {
		ret = dev_err_probe(dev, -EINVAL,
				    "CPU endpoint has no remote endpoint\n");
		goto out_put_cpu_ep;
	}

	cpu_np = of_graph_get_port_parent(cpu_ep);
	if (!cpu_np) {
		ret = dev_err_probe(dev, -EINVAL,
				    "failed to get CPU DAI node\n");
		goto out_put_codec_ep;
	}

	codec_np = of_graph_get_port_parent(codec_ep);
	if (!codec_np) {
		ret = dev_err_probe(dev, -EINVAL,
				    "failed to get codec DAI node\n");
		goto out_put_cpu_np;
	}

	ret = ma35d1_card_parse_endpoint_fmt(dev, cpu_ep, priv);
	if (ret)
		goto out_put_codec_np;

	priv->cpu.of_node = cpu_np;
	priv->cpu.dai_name = NULL;

	priv->codec.of_node = codec_np;
	priv->codec.dai_name = priv->profile->codec_dai_name;

	/*
	 * For a CPU DAI with devm_snd_dmaengine_pcm_register() on the same
	 * device, modern cards can usually omit an explicit platform component.
	 * Keep platform set to the CPU node for compatibility with older ASoC
	 * paths if needed.
	 */
	priv->platform.of_node = cpu_np;
	priv->platform.name = NULL;
	priv->platform.dai_name = NULL;

	link->name = priv->profile->name;
	link->stream_name = priv->profile->name;
	link->cpus = &priv->cpu;
	link->num_cpus = 1;
	link->codecs = &priv->codec;
	link->num_codecs = 1;
	link->platforms = &priv->platform;
	link->num_platforms = 1;
	link->dai_fmt = priv->dai_fmt;
	link->ops = &ma35d1_card_ops;
	link->init = priv->profile->init;

out_put_codec_np:
	of_node_put(codec_np);
out_put_cpu_np:
	of_node_put(cpu_np);
out_put_codec_ep:
	of_node_put(codec_ep);
out_put_cpu_ep:
	of_node_put(cpu_ep);
out_put_cpu_port:
	of_node_put(cpu_port);

	return ret;
}

static int ma35d1_card_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct ma35d1_card *priv;
	const char *label;
	int ret;

	match = of_match_device(ma35d1_card_of_match, dev);
	if (!match || !match->data)
		return dev_err_probe(dev, -EINVAL,
				     "missing MA35D1 card profile\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->profile = match->data;

	ret = of_property_read_string(dev->of_node, "label", &label);
	if (ret)
		label = priv->profile->name;

	priv->card.dev = dev;
	priv->card.owner = THIS_MODULE;
	priv->card.name = label;
	priv->card.dai_link = &priv->link;
	priv->card.num_links = 1;

	snd_soc_card_set_drvdata(&priv->card, priv);

	ret = ma35d1_card_parse_graph_link(pdev, priv);
	if (ret)
		return ret;

	ret = ma35d1_card_parse_routing(dev, &priv->card);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register MA35D1 sound card\n");

	return 0;
}

static struct platform_driver ma35d1_card_driver = {
	.driver = {
		.name = "ma35d1-audio-card",
		.of_match_table = ma35d1_card_of_match,
	},
	.probe = ma35d1_card_probe,
};

module_platform_driver(ma35d1_card_driver);

MODULE_DESCRIPTION("MA35D1 graph-backed ASoC sound card");
MODULE_LICENSE("GPL");
