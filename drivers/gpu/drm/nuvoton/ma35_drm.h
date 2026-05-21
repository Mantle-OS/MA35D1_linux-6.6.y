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

#ifndef _MA35_DRM_H_
#define _MA35_DRM_H_

#include <linux/clk.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/types.h>

#include <drm/drm_device.h>

#include "ma35_regs.h"
#include "ma35_plane.h"
#include "ma35_crtc.h"
#include "ma35_interface.h"

#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

/*
 * MA35_INT_STATE bits.
 *
 * The display controller interrupt status is read from MA35_INT_STATE.
 * DISP0 is the vblank/display interrupt source used by this DRM driver.
 */
#define MA35_INT_STATE_DISP0	BIT(0)

/*
 * Primary framebuffer alignment requirement.
 *
 * The MA35 display controller requires framebuffer pitch alignment based
 * on a 32-pixel boundary.
 */
#define MA35_DISPLAY_ALIGN_PIXELS	32
#define MA35_DISPLAY_PREFER_DEPTH	32

/*
 * Hardware cursor dimensions.
 *
 * The MA35 cursor plane is fixed at 32x32 pixels.
 */
#define MA35_CURSOR_WIDTH	32
#define MA35_CURSOR_HEIGHT	32

/*
 * Hardware planes:
 *
 *   layer 0: primary
 *   layer 1: overlay
 *   layer 2: cursor
 */
#define MA35_DISPLAY_MAX_ZPOS	3

#define ma35_drm(d) \
container_of(d, struct ma35_drm, drm_dev)

/*
 * Driver-private state for the MA35 display controller.
 *
 * Ownership model:
 *
 *   ma35_drm
 *      |
 *      +-- drm_device
 *      |
 *      +-- regmap
 *      |     MMIO access for the display controller register block.
 *      |
 *      +-- layers_list
 *      |     MA35 plane/layer objects parsed from the display node.
 *      |
 *      +-- crtc
 *      |     SoC display timing, DPI format, dither/gamma, and vblank state.
 *      |
 *      +-- interface
 *      |     Encoder, first downstream bridge, and bridge connector.
 *      |
 *      +-- clocks
 *      |     dcuclk:  display controller/core clock
 *      |     dcupclk: display pixel clock
 *      |
 *      +-- reset
 *            Display controller reset line from the DT "resets" property.
 *
 * The panel, bridge chain, and connector mode discovery are not owned by
 * this object directly. They are represented through struct ma35_interface.
 */
struct ma35_drm {
	/*
	 * DRM core device.
	 *
	 * This must remain embedded because ma35_drm() uses container_of()
	 * to recover the private driver state from struct drm_device.
	 */
	struct drm_device drm_dev;

	/*
	 * Display controller register map.
	 *
	 * This wraps the MMIO region from display@40260000 and is used by
	 * the CRTC, plane, and IRQ paths to access MA35_* registers.
	 */
	struct regmap *regmap;

	/*
	 * List of MA35 hardware layers.
	 *
	 * Populated by ma35_plane_init() from the display node's "layers"
	 * child nodes. Entries are struct ma35_layer.
	 */
	struct list_head layers_list;

	/*
	 * MA35 CRTC state.
	 *
	 * Owns timing programming, vblank accounting, gamma/dither state,
	 * and SoC-side DPI format selection.
	 */
	struct ma35_crtc *crtc;

	/*
	 * MA35 output interface state.
	 *
	 * Owns the SoC-side encoder and stores the first downstream bridge
	 * plus the DRM bridge connector.
	 */
	struct ma35_interface *interface;

	/*
	 * Saved atomic state for suspend/resume.
	 *
	 * Filled by drm_atomic_helper_suspend() and consumed by
	 * drm_atomic_helper_resume().
	 */
	struct drm_atomic_state *pm_atomic_state;

	/*
	 * Display controller/core clock.
	 *
	 * DT clock-name: "dcu_gate"
	 *
	 * This clocks the display controller register/block logic.
	 */
	struct clk *dcuclk;

	/*
	 * Display pixel clock.
	 *
	 * DT clock-name: "dcup_div"
	 *
	 * This is the pixel clock used for the DPI/LVDS output path. On the
	 * SOM-35D1F/SOM-255 7-inch panel path, this is derived from VPLL.
	 */
	struct clk *dcupclk;

	/*
	 * Display controller reset.
	 *
	 * DT property:
	 *
	 *   resets = <&reset MA35D1_RESET_DISP>;
	 *
	 * This must be deasserted before the display controller registers are
	 * expected to latch writes or generate interrupts.
	 */
	struct reset_control *reset;
};

#endif /* _MA35_DRM_H_ */
