// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_panel.h>
#include <drm/drm_bridge.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "ma35_drm.h"

/**
 *               Active                 Front           Sync           Back
 *              Region                 Porch                          Porch
 *     <-----------------------><----------------><-------------><-------------->
 *       //////////////////////|
 *      ////////////////////// |
 *     //////////////////////  |..................               ................
 *                                                _______________
 *     <----- [hv]display ----->
 *     <------------- [hv]sync_start ------------>
 *     <--------------------- [hv]sync_end --------------------->
 *     <-------------------------------- [hv]total ----------------------------->
 */
// pixel rate = htotal * vtotal * frame_rate

#define ma35_crtc(c) \
	container_of(c, struct ma35_crtc, drm_crtc)

static const struct drm_prop_enum_list ma35_dpi_format[] = {
	{ MA35_DPI_D16CFG1, "D16CFG1" },
	{ MA35_DPI_D16CFG2, "D16CFG2" },
	{ MA35_DPI_D16CFG3, "D16CFG3" },
	{ MA35_DPI_D18CFG1, "D18CFG1" },
	{ MA35_DPI_D18CFG2, "D18CFG2" },
	{ MA35_DPI_D24, "D24" },
};

static enum drm_mode_status
ma35_crtc_mode_valid(struct drm_crtc *drm_crtc,
			const struct drm_display_mode *mode)
{
	struct drm_device *drm_dev = drm_crtc->dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;

	// check drm_mode_status for some limitations

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	if (mode->hdisplay > mode_config->max_width || mode->hdisplay < mode_config->min_width)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > mode_config->max_height || mode->vdisplay < mode_config->min_height)
		return MODE_BAD_VVALUE;

	if (mode->clock > MA35_MAX_PIXEL_CLK) {
		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static void ma35_reg_write_dbg(struct ma35_drm *priv, u32 reg, u32 val, const char *name)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	drm_info(drm_dev, "REG WRITE %-24s off=0x%04x val=0x%08x ret=%d\n",
			name, reg, val, ret);
}

static u32 ma35_reg_read_dbg(struct ma35_drm *priv, u32 reg, const char *name)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	u32 val = 0xdeadbeef;
	int ret;

	ret = regmap_read(priv->regmap, reg, &val);
	drm_info(drm_dev, "REG READ  %-24s off=0x%04x val=0x%08x ret=%d\n",
			name, reg, val, ret);

	return val;
}

static void ma35_crtc_dump_regs(struct ma35_drm *priv, const char *tag)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	u32 panel_config;
	u32 hdisplay;
	u32 hsync;
	u32 vdisplay;
	u32 vsync;
	u32 current_location;
	u32 int_state;
	u32 intr_enable;
	u32 dpi_config;
	u32 fb_config;
	u32 fb_addr;
	u32 fb_size;
	u32 fb_stride;

	// information
	u32 chip_id;
	u32 chip_rev;
	u32 chip_date;
	u32 chip_time;
	u32 customer_id;
	u32 product_id;

	panel_config = ma35_reg_read_dbg(priv, MA35_PANEL_CONFIG, "PANEL_CONFIG");
	hdisplay = ma35_reg_read_dbg(priv, MA35_HDISPLAY, "HDISPLAY");
	hsync = ma35_reg_read_dbg(priv, MA35_HSYNC, "HSYNC");
	vdisplay = ma35_reg_read_dbg(priv, MA35_VDISPLAY, "VDISPLAY");
	vsync = ma35_reg_read_dbg(priv, MA35_VSYNC, "VSYNC");
	current_location = ma35_reg_read_dbg(priv, MA35_DISPLAY_CURRENT_LOCATION, "CURRENT_LOCATION");
	int_state = ma35_reg_read_dbg(priv, MA35_INT_STATE, "INT_STATE");
	intr_enable = ma35_reg_read_dbg(priv, MA35_DISPLAY_INTRENABLE, "DISPLAY_INTRENABLE");
	dpi_config = ma35_reg_read_dbg(priv, MA35_DPI_CONFIG, "DPI_CONFIG");
	fb_config = ma35_reg_read_dbg(priv, MA35_FRAMEBUFFER_CONFIG, "FB_CONFIG");
	fb_addr = ma35_reg_read_dbg(priv, MA35_FRAMEBUFFER_ADDRESS, "FB_ADDR");
	fb_size = ma35_reg_read_dbg(priv, MA35_FRAMEBUFFER_SIZE, "FB_SIZE");
	fb_stride = ma35_reg_read_dbg(priv, MA35_FRAMEBUFFER_STRIDE, "FB_STRIDE");

	// information
	chip_id =  ma35_reg_read_dbg(priv, MA35_GC_CHIP_ID, "GC_CHIP_ID");
	chip_rev = ma35_reg_read_dbg(priv, MA35_GC_CHIP_REV, "GC_CHIP_REV");
	chip_date = ma35_reg_read_dbg(priv, MA35_GC_CHIP_DATE, "GC_CHIP_DATE");
	chip_time = ma35_reg_read_dbg(priv, MA35_GC_CHIP_TIME, "GC_CHIP_TIME");
	customer_id = ma35_reg_read_dbg(priv, MA35_GC_CUSTOMER_ID, "GC_CUSTOMER_ID");
	product_id = ma35_reg_read_dbg(priv, MA35_GC_PRODUCT_ID, "GC_PRODUCT_ID");

	//  debug
	drm_dbg(drm_dev,
		"%s: GC_CHIP_ID=0x%08x GC_CHIP_REV=0x%08x GC_CHIP_DATE=0x%08x GC_CHIP_TIME=0x%08x GC_CUSTOMER_ID=0x%08x GC_PRODUCT_ID=0x%08x\n",
		tag, chip_id, chip_rev, chip_date, chip_time, customer_id, product_id);

	drm_dbg(drm_dev,
		"%s: PANEL_CONFIG=0x%08x DPI_CONFIG=0x%08x\n",
		tag, panel_config, dpi_config);

	drm_dbg(drm_dev,
		"%s: HDISPLAY=0x%08x HSYNC=0x%08x VDISPLAY=0x%08x VSYNC=0x%08x\n",
		tag, hdisplay, hsync, vdisplay, vsync);

	drm_dbg(drm_dev,
		"%s: CURRENT_LOCATION=0x%08x INT_STATE=0x%08x INTRENABLE=0x%08x\n",
		tag, current_location, int_state, intr_enable);

	drm_dbg(drm_dev,
		"%s: FB_CONFIG=0x%08x FB_ADDR=0x%08x FB_SIZE=0x%08x FB_STRIDE=0x%08x\n",
		tag, fb_config, fb_addr, fb_size, fb_stride);

}

static int ma35_crtc_atomic_check(struct drm_crtc *drm_crtc,
									struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	struct drm_display_mode *adjusted_mode;
	int clk_rate;

	crtc_state = drm_atomic_get_new_crtc_state(state, drm_crtc);
	mode = &crtc_state->mode;
	adjusted_mode = &crtc_state->adjusted_mode;

	if (mode->clock > MA35_MAX_PIXEL_CLK){
		return MODE_CLOCK_HIGH;
	}

	clk_rate = clk_round_rate(priv->dcupclk, mode->clock * 1000);
	if (clk_rate <= 0){
		return MODE_CLOCK_RANGE;
	}

	adjusted_mode->clock = DIV_ROUND_UP(clk_rate, 1000);

	drm_dbg(drm_crtc->dev,
		"CRTC CHECK: mode %s clock request %d kHz rounded to %d kHz\n",
		mode->name, mode->clock, adjusted_mode->clock);

	return 0;
}

static u32 ma35_crtc_get_bus_flags(struct ma35_drm *priv,
									struct drm_connector *connector)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	u32 bus_flags = 0;

	if (connector)
		bus_flags = connector->display_info.bus_flags;

	if (!bus_flags) {
		drm_info(drm_dev,
				 "No connector bus_flags; using temporary fallback\n");
		bus_flags = DRM_BUS_FLAG_DE_HIGH |
			DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
	}

	drm_dbg(drm_dev,
		"Effective Bus Flags: 0x%x, DE_HIGH: %s, NEG_EDGE: %s\n",
		bus_flags,
		(bus_flags & DRM_BUS_FLAG_DE_HIGH) ? "yes" : "no",
		(bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE) ? "yes" : "no");

	return bus_flags;
}

static void ma35_crtc_program_panel_config(struct ma35_drm *priv,
						u32 bus_flags)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	u32 reg;

	reg = MA35_PANEL_DATA_ENABLE_ENABLE |
	MA35_PANEL_DATA_ENABLE |
	MA35_PANEL_DATA_CLOCK_ENABLE;

	/*
	 * MA35_PANEL_DATA_ENABLE_POLARITY appears to invert DE polarity.
	 * For DRM_BUS_FLAG_DE_HIGH, leave it clear.
	 * NOTE: this is emac specific the fema panel is high but the soc is low so ....
	 */
	if (!(bus_flags & DRM_BUS_FLAG_DE_HIGH)){
		reg |= MA35_PANEL_DATA_ENABLE_POLARITY;
	}

	/*
	 * SN65LVDS84AQ loads input data on CLKIN falling edge.
	 * During bring-up, SOM-255 may force PIXDATA_DRIVE_NEGEDGE until
	 * bridge/panel bus flag propagation is verified.
	 */
	if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE){
		reg |= MA35_PANEL_DATA_POLARITY;
	}

	regmap_write(priv->regmap, MA35_PANEL_CONFIG, reg);
	// ma35_reg_write_dbg(priv, MA35_PANEL_CONFIG, reg, "PANEL_CONFIG");
	// ma35_reg_read_dbg(priv, MA35_PANEL_CONFIG, "PANEL_CONFIG readback");


	drm_dbg(drm_dev, "MA35_PANEL_CONFIG programmed: 0x%08x\n", reg);
}

static void ma35_crtc_atomic_enable(struct drm_crtc *drm_crtc,
					struct drm_atomic_state *state)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_crtc_state *new_state;
	struct drm_display_mode *mode;
	struct drm_color_lut *lut;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	struct drm_display_info *display_info = NULL;
	struct drm_device *drm_dev = &priv->drm_dev;
	u32 bus_flags;
	u32 reg;
	int i, size;

	new_state = drm_atomic_get_new_crtc_state(state, drm_crtc);
	mode = &new_state->adjusted_mode;

	connector = priv->interface ? priv->interface->connector : NULL;
	bridge = priv->interface ? priv->interface->bridge : NULL;

	if (connector){
		display_info = &connector->display_info;
	}

	/* --- Timing / connector debug --- */
	drm_dbg(drm_dev, "CRTC ENABLE: Mode Name: %s, Clock: %d KHz\n",
			 mode->name, mode->clock);

	drm_dbg(drm_dev,
			"HTOTAL: %d, HACTIVE: %d, HSYNC_START: %d, HSYNC_END: %d\n",
			mode->htotal, mode->hdisplay, mode->hsync_start,
			mode->hsync_end);

	drm_dbg(drm_dev,
			"VTOTAL: %d, VACTIVE: %d, VSYNC_START: %d, VSYNC_END: %d\n",
			mode->vtotal, mode->vdisplay, mode->vsync_start,
			mode->vsync_end);

	drm_dbg(drm_dev,
			"Mode Flags: 0x%x (NHSYNC: %s, NVSYNC: %s)\n", mode->flags,
			(mode->flags & DRM_MODE_FLAG_NHSYNC) ? "yes" : "no",
			(mode->flags & DRM_MODE_FLAG_NVSYNC) ? "yes" : "no");

	if (connector) {
		drm_dbg(drm_dev, "Connector Name: %s, Connector Type: %d\n",
			connector->name, connector->connector_type);

		drm_dbg(drm_dev,
			"Connector display_info: bus_flags=0x%x, num_bus_formats=%u, bpc=%u\n",
			display_info->bus_flags, display_info->num_bus_formats, display_info->bpc);

		for (i = 0; i < display_info->num_bus_formats; i++){
			drm_dbg(drm_dev,
				"Connector bus_format[%d] = 0x%x\n",
				i, display_info->bus_formats[i]);
		}
	} else {
		drm_info(drm_dev, "Connector missing from MA35 interface\n");
	}

	if (bridge) {
		drm_dbg(drm_dev,
			"First Bridge: Name: %s, Type: %d\n",
			bridge->of_node ? bridge->of_node->name : "unknown", bridge->type);

		if (bridge->timings) {
			drm_dbg(drm_dev,
				"First Bridge timings: input_flags=0x%x, setup=%u ps, hold=%u ps\n",
				bridge->timings->input_bus_flags, bridge->timings->setup_time_ps, bridge->timings->hold_time_ps);
		} else {
			drm_dbg(drm_dev,
				"First Bridge: No static timings defined\n");
		}
	} else {
		drm_dbg(drm_dev,
			"First Bridge missing from MA35 interface\n");
	}

	/* Timings */
	reg = FIELD_PREP(MA35_DISPLAY_TOTAL_MASK,
			mode->htotal) | FIELD_PREP(MA35_DISPLAY_ACTIVE_MASK,
			mode->hdisplay);

	regmap_write(priv->regmap, MA35_HDISPLAY, reg); //FIXME
	// ma35_reg_write_dbg(priv, MA35_HDISPLAY, reg, "HDISPLAY");
	// ma35_reg_read_dbg(priv, MA35_HDISPLAY, "HDISPLAY readback");

	reg = MA35_SYNC_PULSE_ENABLE |
		FIELD_PREP(MA35_SYNC_START_MASK, mode->hsync_start) |
		FIELD_PREP(MA35_SYNC_END_MASK, mode->hsync_end);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC){
		reg |= MA35_SYNC_POLARITY_BIT;
	}

	regmap_write(priv->regmap, MA35_HSYNC, reg);
	// ma35_reg_write_dbg(priv, MA35_HSYNC, reg, "HSYNC");
	// ma35_reg_read_dbg(priv, MA35_HSYNC, "HSYNC readback");

	reg = FIELD_PREP(MA35_DISPLAY_TOTAL_MASK, mode->vtotal) |
		FIELD_PREP(MA35_DISPLAY_ACTIVE_MASK, mode->vdisplay);
	regmap_write(priv->regmap, MA35_VDISPLAY, reg);
	// ma35_reg_write_dbg(priv, MA35_VDISPLAY, reg, "MA35_VDISPLAY");
	// ma35_reg_read_dbg(priv, MA35_VDISPLAY, "MA35_VDISPLAY readback");

	reg = MA35_SYNC_PULSE_ENABLE |
		FIELD_PREP(MA35_SYNC_START_MASK, mode->vsync_start) |
		FIELD_PREP(MA35_SYNC_END_MASK, mode->vsync_end);

	if (mode->flags & DRM_MODE_FLAG_NVSYNC){
		reg |= MA35_SYNC_POLARITY_BIT;
	}
	regmap_write(priv->regmap, MA35_VSYNC, reg);
	// ma35_reg_write_dbg(priv, MA35_VSYNC, reg, "VSYNC");
	// ma35_reg_read_dbg(priv, MA35_VSYNC, "VSYNC readback");

	/* Signals */
	bus_flags = ma35_crtc_get_bus_flags(priv, connector);
	ma35_crtc_program_panel_config(priv, bus_flags);

	/* Gamma */
	if (new_state->gamma_lut) {
		if (new_state->color_mgmt_changed) {
			lut = new_state->gamma_lut->data;
			size = new_state->gamma_lut->length /
			sizeof(struct drm_color_lut);

			for (i = 0; i < size; i++) {
				regmap_write(priv->regmap, MA35_GAMMA_INDEX, i);
				/*
				 * Shift DRM gamma 16-bit values to the MA35
				 * 10-bit gamma fields.
				 */
				reg = 	FIELD_PREP(MA35_GAMMA_RED_MASK, lut[i].red >> 6) |
						FIELD_PREP(MA35_GAMMA_GREEN_MASK, lut[i].green >> 6) |
						FIELD_PREP(MA35_GAMMA_BLUE_MASK, lut[i].blue >> 6);

				regmap_write(priv->regmap, MA35_GAMMA_DATA, reg);
			}
		}

		/* Enable gamma */
		regmap_update_bits(priv->regmap,
				MA35_FRAMEBUFFER_CONFIG,
				MA35_PRIMARY_GAMMA,
				MA35_PRIMARY_GAMMA);

	} else {
		/* Disable gamma */
		regmap_update_bits(priv->regmap,
				MA35_FRAMEBUFFER_CONFIG,
				MA35_PRIMARY_GAMMA, 0);
	}

	/* DPI format */
	reg = FIELD_PREP(MA35_DPI_FORMAT_MASK, crtc->dpi_format);
	regmap_write(priv->regmap, MA35_DPI_CONFIG, reg);

	drm_info(drm_dev,
		"MA35_DPI_CONFIG programmed: 0x%08x, dpi_format=%u\n",
		reg, crtc->dpi_format);

	/* Dither */
	if (crtc->dither_enable) {
		for (i = 0, reg = 0; i < MA35_DITHER_TABLE_ENTRY / 2; i++){
			reg |= (crtc->dither_depth & MA35_DITHER_TABLE_MASK) << (i * 4);
		}

		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_TABLE_LOW, reg);
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_TABLE_HIGH, reg);
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG,
					 MA35_DITHER_ENABLE);
	} else {
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG, 0);
	}

	// DEBUG FOR NOW
	// ma35_crtc_dump_regs(priv, "BEFORE vblank_on"); // remove later or check debug flag
	drm_crtc_vblank_on(drm_crtc);
	// ma35_crtc_dump_regs(priv, "AFTER vblank_on"); // remove later or check debug flag

	// ma35_reg_read_dbg(priv, MA35_DISPLAY_CURRENT_LOCATION, "CUR_LOC t0");
	// udelay(1000);
	// ma35_reg_read_dbg(priv, MA35_DISPLAY_CURRENT_LOCATION, "CUR_LOC t1");
	// udelay(1000);
	// ma35_reg_read_dbg(priv, MA35_DISPLAY_CURRENT_LOCATION, "CUR_LOC t2");

	regmap_write(priv->regmap, MA35_DEBUG_COUNTER_SELECT, 5);
	// ma35_reg_write_dbg(priv,
	// 		MA35_DEBUG_COUNTER_SELECT, 5,
	// 		"DEBUG_COUNTER_SELECT");
	// ma35_reg_read_dbg(priv,
	// 		MA35_DEBUG_COUNTER_VALUE,
	// 		"DEBUG_COUNTER_VALUE");

}

static void ma35_crtc_atomic_disable(struct drm_crtc *drm_crtc,
					struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_device *drm_dev = drm_crtc->dev;

	drm_crtc_vblank_off(drm_crtc);

	/* Disable and clear CRTC bits. */
	regmap_update_bits(priv->regmap, MA35_PANEL_CONFIG,
						MA35_PANEL_DATA_ENABLE_ENABLE, 0);

	regmap_update_bits(priv->regmap, MA35_FRAMEBUFFER_CONFIG,
						MA35_PRIMARY_GAMMA, 0);

	regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG, 0);

	/* Consume any leftover event since vblank is now disabled. */
	if (drm_crtc->state->event && !drm_crtc->state->active) {
		spin_lock_irq(&drm_dev->event_lock);

		drm_crtc_send_vblank_event(drm_crtc, drm_crtc->state->event);
		drm_crtc->state->event = NULL;
		spin_unlock_irq(&drm_dev->event_lock);
	}
}

static void ma35_crtc_atomic_flush(struct drm_crtc *drm_crtc,
	struct drm_atomic_state *state)
{
	spin_lock_irq(&drm_crtc->dev->event_lock);
	if (drm_crtc->state->event) {
		if (drm_crtc_vblank_get(drm_crtc) == 0){
			drm_crtc_arm_vblank_event(drm_crtc, drm_crtc->state->event);
		} else {
			drm_crtc_send_vblank_event(drm_crtc, drm_crtc->state->event);
		}

		drm_crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm_crtc->dev->event_lock);
}

static bool ma35_crtc_get_scanout_position(struct drm_crtc *drm_crtc,
						bool in_vblank_irq,
						int *vpos,
						int *hpos,
						ktime_t *stime,
						ktime_t *etime,
						const struct drm_display_mode *mode)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 reg;

	if (stime)
		*stime = ktime_get();

	regmap_read(priv->regmap, MA35_DISPLAY_CURRENT_LOCATION, &reg);

	*hpos = FIELD_GET(MA35_DISPLAY_CURRENT_X, reg);
	*vpos = FIELD_GET(MA35_DISPLAY_CURRENT_Y, reg);

	if (etime){
		*etime = ktime_get();
	}

	return true;
}

static const struct drm_crtc_helper_funcs ma35_crtc_helper_funcs = {
	.mode_valid		= ma35_crtc_mode_valid,
	.atomic_check		= ma35_crtc_atomic_check,
	.atomic_enable		= ma35_crtc_atomic_enable,
	.atomic_disable		= ma35_crtc_atomic_disable,
	.atomic_flush		= ma35_crtc_atomic_flush,
	.get_scanout_position	= ma35_crtc_get_scanout_position,
};

static int ma35_crtc_enable_vblank(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);

	// ma35_crtc_dump_regs(priv, "enable_vblank BEFORE");

	regmap_write(priv->regmap, MA35_DISPLAY_INTRENABLE, MA35_CRTC_VBLANK);
	// ma35_reg_write_dbg(priv, MA35_DISPLAY_INTRENABLE, MA35_CRTC_VBLANK, "DISPLAY_INTRENABLE");
	// ma35_reg_read_dbg(priv, MA35_DISPLAY_INTRENABLE, "DISPLAY_INTRENABLE readback");

	// ma35_crtc_dump_regs(priv, "enable_vblank AFTER");
	return 0;
}

static void ma35_crtc_disable_vblank(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);

	regmap_write(priv->regmap, MA35_DISPLAY_INTRENABLE, 0);
}

static u32 ma35_crtc_get_vblank_counter(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 val;

	spin_lock(&priv->crtc->vblank_lock);
	val = priv->crtc->vblank_counter;
	spin_unlock(&priv->crtc->vblank_lock);

	return val;
}

static int ma35_crtc_gamma_set(struct drm_crtc *drm_crtc,
				u16 *r, u16 *g, u16 *b,
				uint32_t size,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 reg;
	int i;

	if (size != MA35_GAMMA_TABLE_SIZE){
		return -EINVAL;
	}

	regmap_write(priv->regmap, MA35_GAMMA_INDEX, 0); // auto increment

	for (i = 0; i < size; i++) {

		reg = FIELD_PREP(MA35_GAMMA_RED_MASK, r[i]) |
				FIELD_PREP(MA35_GAMMA_GREEN_MASK, g[i]) |
				FIELD_PREP(MA35_GAMMA_BLUE_MASK, b[i]);

		regmap_write(priv->regmap, MA35_GAMMA_DATA, reg);
	}

	return 0;
}

static int ma35_crtc_atomic_set_property(struct drm_crtc *drm_crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t value)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);

	if (property == crtc->dpi_format_prop) {
		crtc->dpi_format = value;
	} else if (property == crtc->dither_enable_prop) {
		crtc->dither_enable = value;
	} else if (property == crtc->dither_depth_prop) {
		crtc->dither_depth = value;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ma35_crtc_atomic_get_property(struct drm_crtc *drm_crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *value)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);

	if (property == crtc->dpi_format_prop) {
		*value = crtc->dpi_format;
	} else if (property == crtc->dither_enable_prop) {
		*value = crtc->dither_enable;
	} else if (property == crtc->dither_depth_prop) {
		*value = crtc->dither_depth;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct drm_crtc_funcs ma35_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= ma35_crtc_enable_vblank,
	.disable_vblank		= ma35_crtc_disable_vblank,
	.get_vblank_counter 	= ma35_crtc_get_vblank_counter,
	.gamma_set      	= ma35_crtc_gamma_set,
	.atomic_set_property 	= ma35_crtc_atomic_set_property,
	.atomic_get_property 	= ma35_crtc_atomic_get_property,
};

void ma35_crtc_vblank_handler(struct ma35_drm *priv)
{
	struct ma35_crtc *crtc = priv->crtc;

	if (!crtc){
		return;
	}

	// ma35_crtc_dump_regs(priv, "vblank irq");

	spin_lock(&crtc->vblank_lock);
	crtc->vblank_counter++;
	spin_unlock(&crtc->vblank_lock);

	drm_crtc_handle_vblank(&crtc->drm_crtc);
}

static int ma35_crtc_create_properties(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *np = dev->of_node;
	struct ma35_crtc *crtc = priv->crtc;
	struct drm_crtc *drm_crtc = &crtc->drm_crtc;
	u32 dpi_format_val;
	int ret;

	crtc->dpi_format_prop = drm_property_create_enum(drm_dev, 0, "dpi-format",
							ma35_dpi_format, ARRAY_SIZE(ma35_dpi_format));
	if (!crtc->dpi_format_prop) {
		drm_err(drm_dev, "Failed to create dpi format property\n");
		return -ENOMEM;
	}

	// drm_object_attach_property(&drm_crtc->base, crtc->dpi_format_prop, MA35_DPI_D24);
	// crtc->dpi_format = MA35_DPI_D24;
	/* Read 'nuvoton,dpi-format' from Device Tree display node */
	ret = of_property_read_u32(np, "nuvoton,dpi-format", &dpi_format_val);
	if (ret) {
		/* Fallback to D24 if property is missing in DT */
		dpi_format_val = MA35_DPI_D24;
		drm_dbg(drm_dev, "nuvoton,dpi-format missing in DT, using default D24\n");
	} else if (dpi_format_val >= ARRAY_SIZE(ma35_dpi_format)) {
		/* Safety check for out-of-bounds values */
		dpi_format_val = MA35_DPI_D24;
		drm_warn(drm_dev, "Invalid nuvoton,dpi-format in DT, using D24\n");
	}

	drm_object_attach_property(&drm_crtc->base, crtc->dpi_format_prop, dpi_format_val);
	crtc->dpi_format = dpi_format_val;
	drm_dbg(drm_dev, "CRTC DPI format initialized to: %u\n", dpi_format_val);

	/* Dither Enable  */
	crtc->dither_enable_prop = drm_property_create_bool(drm_dev, 0, "dither-enable");
	if (!crtc->dither_enable_prop) {
		drm_err(drm_dev, "Failed to create dither enable property\n");
		return -ENOMEM;
	}
	drm_object_attach_property(&drm_crtc->base, crtc->dither_enable_prop, false);
	crtc->dither_enable = false;

	/* Dither Depth  */
	crtc->dither_depth_prop = drm_property_create_range(drm_dev, 0, "dither-depth",
							0, 0xf);
	if (!crtc->dither_depth_prop) {
		drm_err(drm_dev, "Failed to create dither depth property\n");
		return -ENOMEM;
	}

	drm_object_attach_property(&drm_crtc->base, crtc->dither_depth_prop, 0);
	crtc->dither_depth = 0;

	return 0;
}

int ma35_crtc_init(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct ma35_crtc *crtc;
	struct ma35_layer *layer_primary, *layer_cursor;
	struct drm_plane *cursor_plane = NULL;
	int ret;

	crtc = devm_kzalloc(dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		return -ENOMEM;
	}

	priv->crtc = crtc;
	spin_lock_init(&crtc->vblank_lock);
	crtc->vblank_counter = 0;

	layer_primary = ma35_layer_get_from_type(priv, DRM_PLANE_TYPE_PRIMARY);
	if (!layer_primary) {
		drm_err(drm_dev, "Failed to get primary layer\n");
		return -EINVAL;
	}

	// optional
	layer_cursor = ma35_layer_get_from_type(priv, DRM_PLANE_TYPE_CURSOR);
	if (layer_cursor){
		cursor_plane = &layer_cursor->drm_plane;
	}
	// attach primary and cursor
	ret = drm_crtc_init_with_planes(drm_dev, &crtc->drm_crtc,
					&layer_primary->drm_plane, cursor_plane,
					&ma35_crtc_funcs, NULL);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize CRTC\n");
		return ret;
	}

	// attach overlay
	ma35_overlay_attach_crtc(priv);

	// dither & gamma
	ret = ma35_crtc_create_properties(priv);
	if (ret){
		return ret;
	}

	ret = drm_mode_crtc_set_gamma_size(&crtc->drm_crtc, MA35_GAMMA_TABLE_SIZE);
	if (ret){
		return ret;
	}

	drm_crtc_enable_color_mgmt(&crtc->drm_crtc, 0, false, MA35_GAMMA_TABLE_SIZE);
	drm_crtc_helper_add(&crtc->drm_crtc, &ma35_crtc_helper_funcs);

	return 0;
}

