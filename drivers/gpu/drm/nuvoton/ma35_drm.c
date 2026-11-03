// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "ma35_drm.h"

/* Temporary serial bring-up tracing; zero disables it. No IRQ-path reads. */
static unsigned int bringup_debug_commits = 8;
module_param(bringup_debug_commits, uint, 0644);
MODULE_PARM_DESC(bringup_debug_commits, "Trace the first N display commits (default 8; 0 disables)");

bool ma35_bringup_debug(struct ma35_drm *priv)
{
	return bringup_debug_commits &&
		(unsigned int)atomic_read(&priv->bringup_commits) <= bringup_debug_commits;
}

void ma35_bringup_write(struct ma35_drm *priv, u32 reg, u32 val,
			const char *name)
{
	int ret = regmap_write(priv->regmap, reg, val);

	if (ma35_bringup_debug(priv))
		drm_info(&priv->drm_dev,
			 "MA35-BRINGUP write %s off=%#x value=%#010x ret=%d\n",
			 name, reg, val, ret);
}

void ma35_bringup_dump(struct ma35_drm *priv, const char *stage)
{
	static const struct {
		u32 reg;
		const char *name;
	} regs[] = {
		{ MA35_FRAMEBUFFER_CONFIG, "FB_CONFIG (UNDERFLOW clears on read)" },
		{ MA35_FRAMEBUFFER_ADDRESS, "FB_ADDRESS" },
		{ MA35_FRAMEBUFFER_SIZE, "FB_SIZE" },
		{ MA35_FRAMEBUFFER_STRIDE, "FB_STRIDE" },
		{ MA35_PANEL_CONFIG, "PANEL_CONFIG" },
		{ MA35_DPI_CONFIG, "DPI_CONFIG" },
		{ MA35_GENERAL_CONFIG, "GENERAL_CONFIG" },
		{ MA35_HDISPLAY, "HDISPLAY" },
		{ MA35_HSYNC, "HSYNC" },
		{ MA35_VDISPLAY, "VDISPLAY" },
		{ MA35_VSYNC, "VSYNC" },
		{ MA35_DISPLAY_CURRENT_LOCATION, "LOCATION" },
	};
	u32 vals[ARRAY_SIZE(regs)] = { 0 };
	int i, ret;

	if (!ma35_bringup_debug(priv))
		return;

	/* INT_STATE acknowledges IRQs on read: leave it to the IRQ handler. */
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = regmap_read(priv->regmap, regs[i].reg, &vals[i]);
		if (ret)
			drm_info(&priv->drm_dev, "MA35-BRINGUP %s read %s failed=%d\n",
				 stage, regs[i].name, ret);
	}
	drm_info(&priv->drm_dev,
		 "MA35-BRINGUP %s c=%d FB=%08x addr=%08x size=%08x stride=%u\n",
		 stage, atomic_read(&priv->bringup_commits), vals[0], vals[1], vals[2], vals[3]);
	drm_info(&priv->drm_dev,
		 "MA35-BRINGUP output=%u pending=%u underflow=%u flip=%u clear=%u (UNDERFLOW clears on read; bit4 not interpreted)\n",
		 !!(vals[0] & BIT(0)), !!(vals[0] & BIT(3)),
		 !!(vals[0] & BIT(5)), !!(vals[0] & BIT(6)), !!(vals[0] & BIT(8)));
	drm_info(&priv->drm_dev,
		 "MA35-BRINGUP panel=%08x dpi=%08x general=%08x location=%08x\n",
		 vals[4], vals[5], vals[6], vals[11]);
	drm_info(&priv->drm_dev,
		 "MA35-BRINGUP hdisplay=%08x hsync=%08x vdisplay=%08x vsync=%08x\n",
		 vals[7], vals[8], vals[9], vals[10]);
}

DEFINE_DRM_GEM_DMA_FOPS(ma35_drm_fops);

static int ma35_drm_gem_dma_dumb_create(struct drm_file *file_priv,
					   struct drm_device *drm_dev,
					   struct drm_mode_create_dumb *args)
{
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	u32 pixel_align;

	if (args->width < mode_config->min_width ||
		args->height < mode_config->min_height) {
		return -EINVAL;
	}

	// check for alignment
	pixel_align = MA35_DISPLAY_ALIGN_PIXELS * args->bpp / 8;
	args->pitch = ALIGN(args->width * args->bpp / 8, pixel_align);

	return drm_gem_dma_dumb_create_internal(file_priv, drm_dev, args);
}

static __maybe_unused void ma35_fbdev_backup_memory(struct ma35_drm *priv,
							struct resource *res)
{
	regmap_write(priv->regmap, MA35_FRAMEBUFFER_ADDRESS, res->start);
}

static struct drm_driver ma35_drm_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC |
					DRIVER_CURSOR_HOTSPOT, // this create offset property

	.fops				= &ma35_drm_fops,
	.name				= "ma35-drm",
	.desc				= "Nuvoton MA35 series DRM driver",
	.date				= "20250710",
	.major				= DRIVER_MAJOR,
	.minor				= DRIVER_MINOR,

	DRM_GEM_DMA_DRIVER_OPS_VMAP_WITH_DUMB_CREATE(ma35_drm_gem_dma_dumb_create),
};

static struct regmap_config ma35_drm_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register 	= 0x2000,
	.name		= "ma35-drm",
};

static irqreturn_t ma35_drm_irq_handler(int irq, void *data)
{
	struct ma35_drm *priv = data;
	struct drm_device *drm_dev = &priv->drm_dev;
	irqreturn_t ret = IRQ_NONE;
	u32 stat = 0;

	/* Get pending interrupt sources (RO). */
	regmap_read(priv->regmap, MA35_INT_STATE, &stat);

	// drm_info(drm_dev, "IRQ: MA35_INT_STATE=0x%08x\n", stat);

	if (stat & MA35_INT_STATE_DISP0) {
		ma35_crtc_vblank_handler(priv);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static const struct drm_mode_config_funcs ma35_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static void ma35_drm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *drm_dev = state->dev;
	struct ma35_drm *priv = ma35_drm(drm_dev);
	int seq = atomic_inc_return(&priv->bringup_commits);
	bool trace = ma35_bringup_debug(priv);

	if (trace)
		drm_info(drm_dev, "MA35-BRINGUP commit=%d state=%p begin\n", seq, state);
	ma35_bringup_dump(priv, "tail-before-disables");
	drm_atomic_helper_commit_modeset_disables(drm_dev, state);
	ma35_bringup_dump(priv, "tail-after-disables");
	drm_atomic_helper_commit_modeset_enables(drm_dev, state);
	ma35_bringup_dump(priv, "tail-after-enables");
	drm_atomic_helper_commit_planes(drm_dev, state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);
	ma35_bringup_dump(priv, "tail-after-planes");
	drm_atomic_helper_commit_hw_done(state);
	if (trace)
		drm_info(drm_dev, "MA35-BRINGUP commit=%d wait-for-vblanks begin\n", seq);
	drm_atomic_helper_wait_for_vblanks(drm_dev, state);
	if (trace)
		drm_info(drm_dev, "MA35-BRINGUP commit=%d wait-for-vblanks returned\n", seq);
	ma35_bringup_dump(priv, "tail-after-vblank-wait");
	drm_atomic_helper_cleanup_planes(drm_dev, state);
	if (trace)
		drm_info(drm_dev, "MA35-BRINGUP commit=%d end\n", seq);
}

static const struct drm_mode_config_helper_funcs ma35_mode_config_helper_funcs = {
	.atomic_commit_tail = ma35_drm_atomic_commit_tail,
};

static int ma35_mode_init(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	int ret;

	ret = drmm_mode_config_init(drm_dev);
	if (ret) {
		drm_err(drm_dev, "Failed to init mode config\n");
		return -EINVAL;
	}

	drm_dev->max_vblank_count = MA35_DEBUG_COUNTER_MASK;
	ret = drm_vblank_init(drm_dev, 1);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize vblank\n");
		return ret;
	}
	// TODO Revisit this
	mode_config->min_width = 32;
	mode_config->max_width = 1920;
	mode_config->min_height = 1;
	mode_config->max_height = 1080;
	mode_config->preferred_depth = 24;
	mode_config->cursor_width = MA35_CURSOR_WIDTH;
	mode_config->cursor_height = MA35_CURSOR_HEIGHT;
	mode_config->funcs = &ma35_mode_config_funcs;
	mode_config->helper_private = &ma35_mode_config_helper_funcs;

	return 0;
}

static void ma35_mode_fini(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	drm_kms_helper_poll_fini(drm_dev);
}

static int ma35_drm_of_parse(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *layers_node;
	int layers_count;

	layers_node = of_get_child_by_name(of_node, "layers");
	if (!layers_node) {
		drm_err(drm_dev, "Missing non-optional layers node\n");
		return -EINVAL;
	}

	layers_count = of_get_child_count(layers_node);
	if (!layers_count) {
		drm_err(drm_dev, "Missing a non-optional layers children node\n");
		return -EINVAL;
	}

	if (layers_count > MA35_DISPLAY_MAX_ZPOS) {
		drm_err(drm_dev, "Too many layers\n");
		return -EINVAL;
	}

	return 0;
}

static int ma35_clocks_prepare(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	int ret;

	priv->dcuclk = devm_clk_get(dev, "dcu_gate");
	if (IS_ERR(priv->dcuclk)) {
		dev_err(dev, "Failed to get display core clock dcu_gate\n");
		return PTR_ERR(priv->dcuclk);
	}

	ret = clk_prepare_enable(priv->dcuclk);
	if (ret) {
		dev_err(dev, "Failed to enable display core clock dcuclk\n");
		return ret;
	}

	// dev_info(dev, "dcu_clk = epll/2 = %ld Hz\n", clk_get_rate(priv->dcuclk));

	priv->dcupclk = devm_clk_get(dev, "dcup_div");
	if (IS_ERR(priv->dcupclk)) {
		dev_err(dev, "Failed to get display pixel clock dcup_div\n");
		return PTR_ERR(priv->dcupclk);
	}

	// dev_info(dev,
	// 	"dcuclk rate: %lu Hz\n",
	// 	clk_get_rate(priv->dcuclk));
 //
	// dev_info(dev,
	// 	"dcupclk rate before enable: %lu Hz\n",
	// 	clk_get_rate(priv->dcupclk));

	ret = clk_prepare_enable(priv->dcupclk);
	if (ret) {
		dev_err(dev, "Failed to enable display pixel clock dcupclk\n");
		return ret;
	}

	// dev_info(dev,
	// 	"dcupclk rate after enable: %lu Hz\n",
	// 	clk_get_rate(priv->dcupclk));

	return 0;
}

static int ma35_clocks_unprepare(struct ma35_drm *priv)
{
	struct clk **clocks[] = {
		&priv->dcuclk,
		&priv->dcupclk,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		if (!*clocks[i]){
			continue;
		}
		clk_disable_unprepare(*clocks[i]);
		*clocks[i] = NULL;
	}

	return 0;
}

static int ma35_reset_deassert(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	int ret;

	priv->reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		drm_err(drm_dev, "Failed to get display reset: %d\n", ret);
		return ret;
	}

	if (!priv->reset) {
		drm_info(drm_dev, "No display reset control found\n");
		return 0;
	}

	// drm_info(drm_dev, "Asserting display reset\n");

	ret = reset_control_assert(priv->reset);
	if (ma35_bringup_debug(priv))
		drm_info(drm_dev, "MA35-BRINGUP reset assert ret=%d\n", ret);
	if (ret) {
		drm_err(drm_dev, "Failed to assert display reset: %d\n", ret);
		return ret;
	}

	udelay(100);

	// drm_info(drm_dev, "Deasserting display reset\n");

	ret = reset_control_deassert(priv->reset);
	if (ma35_bringup_debug(priv))
		drm_info(drm_dev, "MA35-BRINGUP reset deassert ret=%d\n", ret);
	if (ret) {
		drm_err(drm_dev, "Failed to deassert display reset: %d\n", ret);
		return ret;
	}

	udelay(10);

	// drm_info(drm_dev, "Display reset pulse complete\n");

	return 0;
}


static void ma35_drm_apply_software_reset(struct ma35_drm *priv,
					void __iomem *base)
{
	struct drm_device *drm_dev = &priv->drm_dev;

	if (ma35_bringup_debug(priv))
		drm_info(drm_dev, "MA35-BRINGUP software reset: FB_CONFIG=0x10, 10us, FB_CONFIG=0\n");

	writel(MA35_FRAMEBUFFER_RESET_ENABLE,
		base + MA35_FRAMEBUFFER_CONFIG);

	udelay(10);

	writel(MA35_FRAMEBUFFER_RESET_DISABLE,
		base + MA35_FRAMEBUFFER_CONFIG);

	// drm_info(drm_dev,
	// 	"RAW MMIO after framebuffer software reset: 0000=%08x 0004=%08x 0020=%08x 00a8=%08x 1518=%08x 1418=%08x\n",
	// 	readl(base + 0x0000),
	// 	readl(base + 0x0004),
	// 	readl(base + 0x0020),
	// 	readl(base + 0x00a8),
	// 	readl(base + MA35_FRAMEBUFFER_CONFIG),
	// 	readl(base + MA35_PANEL_CONFIG));
}


static int ma35_drm_probe(struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct device_node *mem_node;
	struct resource res;
	struct ma35_drm *priv;
	struct drm_device *drm_dev;
	void __iomem *base;
	struct regmap *regmap = NULL;
	unsigned int preferred_bpp;
	int irq;
	int ret;

	// Check for reserved memory. Fall back to dynamic allocation if undefined
	mem_node = of_parse_phandle(of_node, "memory-region", 0);
	if (mem_node) {
		ret = of_address_to_resource(mem_node, 0, &res);
		if (ret) {
			dev_err(dev, "Failed to parse reserved memory resource: %d\n", ret);
			of_node_put(mem_node);
			return ret;
		}
		of_node_put(mem_node);
		// dev_info(dev, "registering reserved memory %pR\n", &res);

		/* This function assigns respective DMA-mapping operations
		 * to the device, so that it can use the reserved memory region
		 * with compatible = "shared-dma-pool" for DMA operations.
		 */
		ret = of_reserved_mem_device_init(dev);
		if (ret && ret != -ENODEV) {
			dev_err(dev, "Failed to init memory region\n");
			goto error_early;
		}
	}

	// determine regmap
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed to map I/O base\n");
		ret = PTR_ERR(base);
		goto error_reserved_mem;
	}

	regmap = devm_regmap_init_mmio(dev, base, &ma35_drm_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to create regmap for I/O\n");
		ret = PTR_ERR(regmap);
		goto error_reserved_mem;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		goto error_reserved_mem;
	}

	priv = devm_drm_dev_alloc(dev, &ma35_drm_driver,
				     struct ma35_drm, drm_dev);
	if (IS_ERR(priv)) {
		ret = PTR_ERR(priv);
		goto error_reserved_mem;
	}

	platform_set_drvdata(pdev, priv);
	drm_dev = &priv->drm_dev;
	priv->regmap = regmap;
	atomic_set(&priv->bringup_commits, 0);
	INIT_LIST_HEAD(&priv->layers_list);

	ret = ma35_clocks_prepare(priv);
	if (ret) {
		drm_err(drm_dev, "Failed to prepare clocks\n");
		goto error_reserved_mem;
	}

	ma35_bringup_dump(priv, "probe-before-reset");

	// NEW add reset controller
	ret = ma35_reset_deassert(priv);
	if (ret) {
		drm_err(drm_dev, "Failed to deassert display reset: %d\n", ret);
		goto error_clocks;
	}

	// NEW software reset
	ma35_drm_apply_software_reset(priv, base);
	ma35_bringup_dump(priv, "probe-after-resets");

	ret = devm_request_irq(dev, irq, ma35_drm_irq_handler, 0,
				dev_name(dev), priv);
	if (ret) {
		drm_err(drm_dev, "Failed to request IRQ\n");
		goto error_clocks;
	}

	ret = ma35_drm_of_parse(priv);
	if (ret && ret != -ENODEV) {
		drm_err(drm_dev, "Failed to parse config\n");
		goto error_clocks;
	}

	// modeset
	ret = ma35_mode_init(priv);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize KMS\n");
		goto error_clocks;
	}

	// plane
	ret = ma35_plane_init(priv);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize layers\n");
		goto error_clocks;
	}

	// crtc
	ret = ma35_crtc_init(priv);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize CRTC\n");
		goto error_clocks;
	}

	// interface
	ret = ma35_interface_init(priv);
	if (ret) {
		if (ret != -EPROBE_DEFER){
			drm_err(drm_dev, "Failed to initialize interface\n");
		}

		goto error_clocks;
	}

	drm_mode_config_reset(drm_dev);

	ret = drm_dev_register(drm_dev, 0);
	if (ret) {
		drm_err(drm_dev, "Failed to register DRM device\n");
		goto error_mode;
	}

	switch (drm_dev->mode_config.preferred_depth) {
	case 16:
		preferred_bpp = 16;
		break;
	case 24:
	case 32:
	default:
		preferred_bpp = 32;
		break;
	}

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (mem_node) {
		drm_fbdev_generic_setup(drm_dev, preferred_bpp);
		ma35_bringup_dump(priv, "probe-after-fbdev-before-backup");
		if (ma35_bringup_debug(priv))
			drm_info(drm_dev, "MA35-BRINGUP backup address=%pa\n", &res.start);
		ma35_fbdev_backup_memory(priv, &res);
		ma35_bringup_dump(priv, "probe-after-backup");
	} else {
		dev_dbg(dev, "Reserved memory node not present. Driver will try dynamic allocation\n");
	}
#endif

	return 0;

error_mode:
	ma35_mode_fini(priv);

error_clocks:
	ma35_clocks_unprepare(priv);

error_reserved_mem:
	of_reserved_mem_device_release(dev);

error_early:
	return ret;
}

static void ma35_drm_remove(struct platform_device *pdev)
{
	struct ma35_drm *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct drm_device *drm_dev = &priv->drm_dev;

	drm_dev_unregister(drm_dev);
	drm_atomic_helper_shutdown(drm_dev);

	ma35_mode_fini(priv);

	ma35_clocks_unprepare(priv);

	of_reserved_mem_device_release(dev);
}

static void ma35_drm_shutdown(struct platform_device *pdev)
{
	struct ma35_drm *priv = platform_get_drvdata(pdev);
	struct drm_device *drm_dev = &priv->drm_dev;

	drm_atomic_helper_shutdown(drm_dev);
}

static __maybe_unused int ma35_drm_runtime_suspend(struct device *dev)
{
	struct ma35_drm *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->dcupclk);

	return 0;
}

static __maybe_unused int ma35_drm_suspend(struct device *dev)
{
	struct ma35_drm *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_atomic_state *state;

	WARN_ON(priv->pm_atomic_state);

	state = drm_atomic_helper_suspend(drm_dev);
	if (IS_ERR(state))
		return PTR_ERR(state);

	priv->pm_atomic_state = state;
	pm_runtime_force_suspend(dev);

	return 0;
}

static __maybe_unused int ma35_drm_runtime_resume(struct device *dev)
{
	struct ma35_drm *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->dcupclk);
	if (ret) {
		dev_err(dev, "Failed to enable display pixel clock\n");
		return ret;
	}

	return 0;
}

static __maybe_unused int ma35_drm_resume(struct device *dev)
{
	struct ma35_drm *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = &priv->drm_dev;
	int ret;

	if (WARN_ON(!priv->pm_atomic_state))
		return -ENOENT;

	pm_runtime_force_resume(dev);
	ret = drm_atomic_helper_resume(drm_dev, priv->pm_atomic_state);
	if (ret)
		pm_runtime_force_suspend(dev);

	priv->pm_atomic_state = NULL;

	return 0;
}

static const struct of_device_id ma35_drm_of_table[] = {
	{ .compatible = "nuvoton,ma35d1-drm" },
	{ .compatible = "nuvoton,ma35d0-drm" },
	{ .compatible = "nuvoton,ma35h0-drm" },
	{},
};
MODULE_DEVICE_TABLE(of, ma35_drm_of_table);

static const struct dev_pm_ops ma35_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ma35_drm_suspend,
				ma35_drm_resume)

	SET_RUNTIME_PM_OPS(ma35_drm_runtime_suspend,
				ma35_drm_runtime_resume,
				NULL)
};

static struct platform_driver ma35_drm_platform_driver = {
	.probe		= ma35_drm_probe,
	.remove_new	= ma35_drm_remove,
	.shutdown	= ma35_drm_shutdown,
	.driver		= {
		.name		= "ma35-drm",
		.of_match_table	= ma35_drm_of_table,
		.pm 		= &ma35_pm_ops,
	},
};

/* remove the platform call and use late_initcall */
// module_platform_driver(ma35_drm_platform_driver);
static int __init ma35_drm_init(void) {
	return platform_driver_register(&ma35_drm_platform_driver);
}

/* late_initcall "ensures" almost everything else (I2C, Regulators) is ready first kinda */
late_initcall(ma35_drm_init);

MODULE_AUTHOR("Joey Lu <yclu4@nuvoton.com>");
MODULE_DESCRIPTION("Nuvoton MA35 series DRM driver");
MODULE_LICENSE("GPL");
