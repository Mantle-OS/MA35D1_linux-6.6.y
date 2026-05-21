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

	clk_set_rate(priv->dcupclk, adjusted_mode->clock * 1000);

	actual_rate = clk_get_rate(priv->dcupclk);
	result = DIV_ROUND_UP(actual_rate, 1000);

	// drm_dbg(drm_dev,
	// 	"Pixel clock: %d kHz; request: %d kHz\n",
	// 	result,
	// 	adjusted_mode->clock);
}

static const struct drm_encoder_helper_funcs ma35_encoder_helper_funcs = {
	.atomic_mode_set = ma35_encoder_mode_set,
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
