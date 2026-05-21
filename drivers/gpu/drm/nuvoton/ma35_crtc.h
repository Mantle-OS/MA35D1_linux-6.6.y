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

#ifndef _MA35_CRTC_H_
#define _MA35_CRTC_H_

#include <linux/bits.h>
#include <linux/types.h>

#include <drm/drm_crtc.h>
#include <drm/drm_property.h>

struct ma35_drm;

/*
 * MA35 DPI output format.
 *
 * These values are written directly into MA35_DPI_CONFIG[2:0].
 *
 * Important:
 *   This is the SoC-side DPI/LCM output packing format. It is not the
 *   same thing as the downstream panel's MEDIA_BUS_FMT_* value.
 *
 * For SOM-35D1F + SOM-255, the DT currently selects D18CFG1 because the
 * carrier routes the upper six RGB bits into the SN65LVDS84AQ encoder.
 */
enum ma35_dpi_format_enum {
	MA35_DPI_D16CFG1,
	MA35_DPI_D16CFG2,
	MA35_DPI_D16CFG3,
	MA35_DPI_D18CFG1,
	MA35_DPI_D18CFG2,
	MA35_DPI_D24,
};

#define MA35_DPI_FORMAT_MASK		GENMASK(2, 0)

/*
 * CRTC-owned display-controller state.
 *
 * The CRTC owns:
 *   - MA35 timing register programming
 *   - vblank accounting
 *   - SoC DPI format selection
 *   - gamma/dither properties
 *
 * The CRTC does not own:
 *   - connector
 *   - bridge chain
 *   - panel
 *
 * Those are owned/referenced through struct ma35_interface.
 */
struct ma35_crtc {
	struct drm_crtc drm_crtc;

	/* MA35-specific CRTC properties */
	struct drm_property *dpi_format_prop;
	struct drm_property *dither_depth_prop;
	struct drm_property *dither_enable_prop;

	/* Vblank accounting */
	spinlock_t vblank_lock;
	u32 vblank_counter;

	/* MA35 DPI/LCM output configuration */
	u32 dpi_format;

	/* Optional display dither configuration */
	u16 dither_depth;
	bool dither_enable;
};

#define MA35_DEFAULT_CRTC_ID		0

#define MA35_MAX_PIXEL_CLK		150000

#define MA35_GAMMA_TABLE_SIZE		256
#define MA35_GAMMA_RED_MASK		GENMASK(29, 20)
#define MA35_GAMMA_GREEN_MASK		GENMASK(19, 10)
#define MA35_GAMMA_BLUE_MASK		GENMASK(9, 0)

#define MA35_DITHER_TABLE_ENTRY		16
#define MA35_DITHER_ENABLE		BIT(31)
#define MA35_DITHER_TABLE_MASK		GENMASK(3, 0)

#define MA35_CRTC_VBLANK		BIT(0)

#define MA35_DEBUG_COUNTER_MASK		GENMASK(31, 0)

/*
 * MA35_PANEL_CONFIG bits.
 *
 * These are programmed from the effective output bus flags in CRTC enable.
 * The source of those flags should be the connector/bridge/panel chain,
 * with board-specific forcing kept as temporary debug scaffolding only.
 */
#define MA35_PANEL_DATA_ENABLE_ENABLE	BIT(0)
#define MA35_PANEL_DATA_ENABLE_POLARITY	BIT(1)
#define MA35_PANEL_DATA_ENABLE		BIT(4)
#define MA35_PANEL_DATA_DISABLE		0
#define MA35_PANEL_DATA_POLARITY	BIT(5)
#define MA35_PANEL_DATA_CLOCK_ENABLE	BIT(8)
#define MA35_PANEL_DATA_CLOCK_POLARITY	BIT(9)

/*
 * MA35 timing register fields.
 *
 * These currently receive DRM mode timing values from adjusted_mode.
 * The source file still needs to verify whether the hardware expects
 * absolute DRM positions, widths, or minus-one encoded values.
 */
#define MA35_DISPLAY_TOTAL_MASK		GENMASK(30, 16)
#define MA35_DISPLAY_ACTIVE_MASK	GENMASK(14, 0)

#define MA35_SYNC_POLARITY_BIT		BIT(31)
#define MA35_SYNC_PULSE_ENABLE		BIT(30)
#define MA35_SYNC_END_MASK		GENMASK(29, 15)
#define MA35_SYNC_START_MASK		GENMASK(14, 0)

#define MA35_DISPLAY_CURRENT_X		GENMASK(15, 0)
#define MA35_DISPLAY_CURRENT_Y		GENMASK(31, 16)

void ma35_crtc_vblank_handler(struct ma35_drm *priv);
int ma35_crtc_init(struct ma35_drm *priv);

#endif /* _MA35_CRTC_H_ */
