/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2026 Emac INC.
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 * Author: Joseph Mills
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "ma35_drm.h"

#define ma35_encoder(e) \
	container_of(e, struct ma35_interface, drm_encoder)

static void ma35_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct drm_device *drm_dev = encoder->dev;
	struct ma35_drm *priv = ma35_drm(drm_dev);
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	unsigned long actual_rate;
	int result;
	int ret;

	ret = clk_set_rate(priv->dcupclk, adjusted_mode->clock * 1000);

	actual_rate = clk_get_rate(priv->dcupclk);
	result = DIV_ROUND_UP(actual_rate, 1000);
	if (ma35_bringup_debug(priv))
		drm_info(drm_dev, "MA35-BRINGUP encoder pixel request=%d kHz actual=%lu Hz rounded=%d kHz ret=%d\n",
			 adjusted_mode->clock, actual_rate, result, ret);

	// drm_dbg(drm_dev,
	// 	"Pixel clock: %d kHz; request: %d kHz\n",
	// 	result,
	// 	adjusted_mode->clock);
}

/* The KMS helper calls this after bridge-chain pre-enable/panel prepare. */
static void ma35_encoder_atomic_enable(struct drm_encoder *encoder,
				      struct drm_atomic_state *state)
{
	struct drm_device *drm_dev = encoder->dev;
	struct ma35_drm *priv = ma35_drm(drm_dev);
	struct drm_crtc *drm_crtc = drm_atomic_get_new_crtc_for_encoder(state, encoder);
	static const u32 counter_select[] = { 0, 1, 4, 5 };
	u32 counter[2][ARRAY_SIZE(counter_select)] = { { 0 } };
	u32 irq_enable;
	bool counters_ok = true;
	int ret, pass, j;

	if (!drm_crtc || !priv->startup_prepared) {
		drm_err(drm_dev, "MA35-INIT encoder start skipped: no prepared CRTC\n");
		return;
	}

	ret = regmap_write(priv->regmap, MA35_DEBUG_COUNTER_SELECT, 5);
	if (ret)
		goto encoder_enable_failed;
	ret = regmap_write(priv->regmap, MA35_PANEL_CONFIG, priv->startup_panel_config);
	if (ret)
		goto encoder_enable_failed;
	ret = regmap_write(priv->regmap, MA35_FRAMEBUFFER_CONFIG, priv->startup_fb_config);
	drm_info(drm_dev,
		 "MA35-INIT encoder start after panel prepare PANEL_CONFIG=%#010x FB_CONFIG=%#010x ret=%d\n",
		 priv->startup_panel_config, priv->startup_fb_config, ret);
	if (ret)
		goto encoder_enable_failed;
	priv->startup_prepared = false;

	/*
	 * The IRQ path reads this selector as the hardware frame counter.
	 * Sample only before vblank_on, with the display IRQ verified masked,
	 * and restore selector 5 before DRM can consume it.
	 */
	if (!ma35_bringup_debug(priv))
		goto counters_done;
	ret = regmap_read(priv->regmap, MA35_DISPLAY_INTRENABLE, &irq_enable);
	if (!ret && !irq_enable) {
		/* Drain a handler already in flight before changing its selector. */
		synchronize_irq(priv->irq);
		for (pass = 0; pass < 2 && counters_ok; pass++) {
			for (j = 0; j < ARRAY_SIZE(counter_select); j++) {
				ret = regmap_write(priv->regmap, MA35_DEBUG_COUNTER_SELECT,
						   counter_select[j]);
				if (!ret)
					ret = regmap_read(priv->regmap, MA35_DEBUG_COUNTER_VALUE,
							  &counter[pass][j]);
				if (ret) {
					counters_ok = false;
					break;
				}
			}
			/* Always restore, including a failed sample. */
			ret = regmap_write(priv->regmap, MA35_DEBUG_COUNTER_SELECT, 5);
			if (ret) {
				drm_err(drm_dev, "MA35-COUNT restore frame selector failed=%d\n", ret);
				goto encoder_enable_failed;
			}
			if (pass == 0 && counters_ok)
				usleep_range(20000, 25000);
		}
		if (counters_ok)
			for (j = 0; j < ARRAY_SIZE(counter_select); j++)
				drm_info(drm_dev,
					 "MA35-COUNT selector=%u before=%u after=%u delta=%u (20-25ms window)\n",
					 counter_select[j], counter[0][j], counter[1][j],
					 counter[1][j] - counter[0][j]);
		else
			drm_info(drm_dev, "MA35-COUNT sample failed; frame selector restored\n");
	} else {
		drm_info(drm_dev, "MA35-COUNT skipped: display IRQ not masked or read failed ret=%d\n", ret);
	}
counters_done:
	ma35_bringup_dump(priv, "encoder-before-vblank-on");
	drm_crtc_vblank_on(drm_crtc);
	return;

encoder_enable_failed:
	drm_err(drm_dev, "MA35-INIT encoder start failed ret=%d\n", ret);
	/* Do not leave the physical output active after a partial enable. */
	regmap_write(priv->regmap, MA35_PANEL_CONFIG, 0);
	regmap_write(priv->regmap, MA35_FRAMEBUFFER_CONFIG, 0);
	priv->startup_prepared = false;
}

/* Runs after panel disable/backlight-off, before panel unprepare/power-off. */
static void ma35_encoder_atomic_disable(struct drm_encoder *encoder,
				       struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(encoder->dev);
	struct drm_crtc *drm_crtc = drm_atomic_get_old_crtc_for_encoder(state, encoder);
	int panel_ret, fb_ret;

	if (drm_crtc)
		drm_crtc_vblank_off(drm_crtc);
	panel_ret = regmap_write(priv->regmap, MA35_PANEL_CONFIG, 0);
	fb_ret = regmap_write(priv->regmap, MA35_FRAMEBUFFER_CONFIG, 0);
	priv->startup_prepared = false;
	drm_info(encoder->dev,
		 "MA35-INIT encoder stop before panel unprepare panel_ret=%d fb_ret=%d\n",
		 panel_ret, fb_ret);
}

static const struct drm_encoder_helper_funcs ma35_encoder_helper_funcs = {
	.atomic_mode_set = ma35_encoder_mode_set,
	.atomic_enable = ma35_encoder_atomic_enable,
	.atomic_disable = ma35_encoder_atomic_disable,
};

static void ma35_encoder_attach_crtc(struct ma35_drm *priv)
{
	u32 possible_crtcs = drm_crtc_mask(&priv->crtc->drm_crtc);

	priv->interface->drm_encoder.possible_crtcs = possible_crtcs;
}

static int ma35_interface_find_bridge(struct ma35_drm *priv,
					struct ma35_interface *interface)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *remote_node;
	struct drm_bridge *bridge = NULL;
	struct drm_panel *panel = NULL;
	int ret;

	// drm_info(drm_dev,
	// 	"Starting bridge/panel search on node: %pOF\n",
	// 	of_node);

	/*
	 * This is only debug/visibility. The real lookup below is
	 * drm_of_find_panel_or_bridge().
	 */
	remote_node = of_graph_get_remote_node(of_node, 0, 0);
	if (!remote_node) {
		drm_err(drm_dev,
			"Graph Error: No remote node found connected to port 0, ep 0\n");

	} else {
		// drm_info(drm_dev,
		// 	"Graph Success: Found remote node: %pOF, compatible: %pOFc\n",
		// 	remote_node, remote_node);

		of_node_put(remote_node);
	}

	ret = drm_of_find_panel_or_bridge(of_node, 0, 0, &panel, &bridge);
	if (ret) {
		drm_info(drm_dev, "drm_of_find_panel_or_bridge returned: %d%s\n",
				 ret, ret == -EPROBE_DEFER ? " (EPROBE_DEFER)" : "");
		return ret;
	}

	if (panel) {
		// drm_info(drm_dev, "Found direct panel, wrapping in panel bridge\n");

		/*
		 * For a direct-panel design, normalize the topology into:
		 *
		 *   encoder -> panel_bridge -> bridge_connector
		 *
		 * If devm_drm_panel_bridge_add() is not available in this
		 * vendor 6.6 tree, use drm_panel_bridge_add() and add the
		 * matching cleanup later.
		 */
		bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(bridge)) {
			ret = PTR_ERR(bridge);
			drm_err(drm_dev, "Failed to create panel bridge: %d\n",
					ret);
			return ret;
		}
	}

	if (!bridge) {
		drm_err(drm_dev,
			"Lookup succeeded but no bridge/panel was returned\n");

		return -ENODEV;
	}

	interface->bridge = bridge;

	// if (bridge->of_node){
	// 	drm_info(drm_dev, "Using first downstream bridge: %pOF, type: %d\n", bridge->of_node, bridge->type);
	// } else {
	// 	drm_info(drm_dev, "Using first downstream bridge: panel wrapper, type: %d\n",  bridge->type);
	// }
	return 0;
}

static int ma35_interface_attach_bridge_chain(struct ma35_drm *priv,
						struct ma35_interface *interface)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_encoder *encoder = &interface->drm_encoder;
	struct drm_connector *connector;
	int ret;

	ret = drm_bridge_attach(encoder,
				interface->bridge,
				NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		drm_err(drm_dev, "Failed to attach bridge chain: %d\n", ret);
		return ret;
	}

	connector = drm_bridge_connector_init(drm_dev, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		drm_err(drm_dev, "Failed to initialize bridge connector: %d\n",
				ret);
		return ret;
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		drm_err(drm_dev, "Failed to attach connector to encoder: %d\n", ret);
		return ret;
	}

	interface->connector = connector;

	// drm_info(drm_dev, "Bridge connector display_info: bus_flags=0x%x, num_bus_formats=%u, bpc=%u\n",
	// 		connector->display_info.bus_flags,
	// 		connector->display_info.num_bus_formats,
	// 		connector->display_info.bpc);

	// drm_info(drm_dev, "Bridge connector attached: %s, type: %d\n",
	// 		 connector->name, connector->connector_type);

	return 0;
}

/*
 * encoder -> bridge chain -> bridge connector
 */
int ma35_interface_init(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct ma35_interface *interface;
	struct drm_encoder *encoder;
	int ret;

	interface = drmm_simple_encoder_alloc(drm_dev,
						struct ma35_interface,
						drm_encoder,
						DRM_MODE_ENCODER_DPI);
	if (!interface) {
		drm_err(drm_dev, "Failed to allocate encoder\n");
		return -ENOMEM;
	}

	priv->interface = interface;
	encoder = &interface->drm_encoder;

	drm_encoder_helper_add(encoder, &ma35_encoder_helper_funcs);
	ma35_encoder_attach_crtc(priv);

	ret = ma35_interface_find_bridge(priv, interface);
	if (ret)
		return ret;

	ret = ma35_interface_attach_bridge_chain(priv, interface);
	if (ret)
		return ret;

	return 0;
}
