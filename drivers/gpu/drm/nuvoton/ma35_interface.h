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

#ifndef _MA35_INTERFACE_H_
#define _MA35_INTERFACE_H_

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>

struct ma35_drm;

/*
 * MA35 display interface ownership model:
 *
 *   MA35 CRTC
 *      |
 *      v
 *   MA35 encoder                 owned by this driver
 *      |
 *      v
 *   first downstream bridge       DT bridge or panel bridge wrapper
 *      |
 *      v
 *   remaining bridge chain        owned/managed by DRM bridge core
 *      |
 *      v
 *   bridge connector              created by drm_bridge_connector_init()
 *
 * The MA35 interface should not manually implement panel get_modes()
 * when a DRM bridge chain exists. The bridge connector should expose
 * the downstream panel/bridge modes to userspace.
 */
struct ma35_interface {
	/*
	 * SoC-side encoder object.
	 *
	 * This represents the MA35 DPI/LCM output feeding the first
	 * downstream bridge.
	 */
	struct drm_encoder drm_encoder;

	/*
	 * First downstream bridge attached to the MA35 encoder.
	 *
	 * For SOM-35D1F + SOM-255 this is expected to be the transparent
	 * LVDS encoder node:
	 *
	 *   /bridge-lvds
	 *
	 * For a direct-panel design, this may instead be a panel bridge
	 * wrapper created around the panel returned by the DT graph lookup.
	 */
	struct drm_bridge *bridge;

	/*
	 * Connector created for the bridge chain.
	 *
	 * This should be returned by drm_bridge_connector_init() and then
	 * attached to drm_encoder. It is a pointer, not an embedded connector,
	 * because the connector object is created by DRM bridge helpers.
	 */
	struct drm_connector *connector;
};

void ma35_interface_attach_crtc(struct ma35_drm *priv);
int ma35_interface_init(struct ma35_drm *priv);

#endif /* _MA35_INTERFACE_H_ */
