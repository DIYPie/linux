#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nt36860_dcs_instr {
	const u8 *data;
	size_t len;
};

#define NT36860_DCS_INSTR(...)	\
	{	\
		.data = (const u8[]){__VA_ARGS__},	\
		.len = ARRAY_SIZE(((const u8[]){__VA_ARGS__})),	\
	}

struct nt36860_desc {
	struct drm_display_mode *mode;
	struct nt36860_dcs_instr *init;
	size_t init_len;
	unsigned long flags;
	enum drm_panel_orientation orientation;
};

struct nt36860 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct nt36860_desc *desc;
	struct gpio_desc *reset;
	struct regulator *vddi;
	struct regulator *avdd;
	struct regulator *avee;
};

static struct nt36860_dcs_instr jdi_lpm035m407b_init[] = {
	NT36860_DCS_INSTR(0xFFu, 0x10u),	/* Page select */
	NT36860_DCS_INSTR(0xFBu, 0x01u),	/* Reload */
	NT36860_DCS_INSTR(0x2Au, 0x00u, 0x00u, 0x05u, 0x9Fu),	/* SET_HORIZONTAL_ADDRESS */
	NT36860_DCS_INSTR(0x2Bu, 0x00u, 0x00u, 0x06u, 0x3Fu),	/* SET_VERTICAL_ADDRESS */
	NT36860_DCS_INSTR(0x35u, 0x00u),	/* SET_TEAR_ON */
	NT36860_DCS_INSTR(0xBAu, 0x07u),	/* SET_MIPI_LANE (4-lane x 1-port) */
	NT36860_DCS_INSTR(0xBBu, 0x13u),	/* SETDSIMODE (03: Video Mode bypass RAM, 10: Command Mode, 13: Video Mode with RAM) */
	NT36860_DCS_INSTR(0xE5u, 0x00u),	/* BK_EN (Random 00h, Black 01h) */
	NT36860_DCS_INSTR(0xFFu, 0x26u),	/* Page select (PWM adjustment for JDI reccomended video timing) */
	NT36860_DCS_INSTR(0xFBu, 0x01u),	/* Reload */
	NT36860_DCS_INSTR(0x02u, 0xC0u),	/* DELY_VID */
	NT36860_DCS_INSTR(0x03u, 0x00u),	/* DELY_VID */
	NT36860_DCS_INSTR(0xFFu, 0x25u),	/* Page select */
	NT36860_DCS_INSTR(0xFBu, 0x01u),	/* Reload */
	NT36860_DCS_INSTR(0x62u, 0x60u),	/* PIN_CTRL3 */
	NT36860_DCS_INSTR(0x65u, 0x00u),	/* VSOUTS_1 */
	NT36860_DCS_INSTR(0x66u, 0x07u),	/* VSOUTS_2 */
	NT36860_DCS_INSTR(0x67u, 0x56u),	/* VSOUTW */
	NT36860_DCS_INSTR(0xFFu, 0xD0u),	/* Page select */
	NT36860_DCS_INSTR(0xFBu, 0x01u),	/* Reload */
	NT36860_DCS_INSTR(0x05u, 0x88u),	/* Adjustment of timing */
	NT36860_DCS_INSTR(0xFFu, 0x10u),	/* Page select */
	NT36860_DCS_INSTR(0xFBu, 0x01u),	/* Reload */
	NT36860_DCS_INSTR(0xC0u, 0x80u),	/* Compression (80: No compression, 83: VESA_DSC) */
	NT36860_DCS_INSTR(0xBEu, 0x01u, 0x90u, 0x0Fu, 0x39u),	/* RGBMIPICTRL_HF */
};

static inline struct  nt36860 *panel_to_nt36860(struct drm_panel *panel)
{
	return container_of(panel, struct nt36860, panel);
}

static int nt36860_prepare(struct drm_panel *panel)
{
	struct nt36860 *ctx = panel_to_nt36860(panel);
	int ret, i;

	gpiod_set_value_cansleep(ctx->reset, 1);

	if (ctx->vddi) {
		ret = regulator_enable(ctx->vddi);
		if (ret) {
			return ret;
		}
	}

	msleep(1);

	ret = regulator_enable(ctx->avdd);
	if (ret) {
		return ret;
	}

	msleep(1);

	ret = regulator_enable(ctx->avee);
	if (ret) {
		return ret;
	}

	msleep(10);

	gpiod_set_value_cansleep(ctx->reset, 0);
	usleep_range(10, 20);
	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(10, 20);
	gpiod_set_value_cansleep(ctx->reset, 0);

	msleep(10);

	for (i = 0; i < ctx->desc->init_len; i++) {
		struct nt36860_dcs_instr *instr = &ctx->desc->init[i];
		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, instr->data, instr->len);
		if (ret) {
			return ret;
		}
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (ret) {
		return ret;
	}

	msleep(100);

	return 0;
}

static int nt36860_enable(struct drm_panel *panel)
{
	struct nt36860 *ctx = panel_to_nt36860(panel);

	mipi_dsi_dcs_set_display_on(ctx->dsi);
	msleep(40);

	return 0;
}

static int nt36860_disable(struct drm_panel *panel)
{
	struct nt36860 *ctx = panel_to_nt36860(panel);

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int nt36860_unprepare(struct drm_panel *panel)
{
	struct nt36860 *ctx = panel_to_nt36860(panel);

	mipi_dsi_dcs_set_tear_off(ctx->dsi);
	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);

	gpiod_set_value_cansleep(ctx->reset, 1);
	msleep(1);
	regulator_disable(ctx->avee);
	msleep(1);
	regulator_disable(ctx->avdd);

	if (ctx->vddi) {
		msleep(1);
		regulator_disable(ctx->vddi);
	}

	return 0;
}

static struct drm_display_mode jdi_lpm035m407b_mode = {
	.clock = 140070,
	.hdisplay = 1440,
	.hsync_start = 1440 + 30,
	.hsync_end = 1440 + 30 + 4,
	.htotal = 1440 + 30 + 4 + 30,
	.vdisplay = 1600,
	.vsync_start = 1600 + 30,
	.vsync_end = 1600 + 30 + 8,
	.vtotal = 1600 + 30 + 8 + 30,
	.width_mm = 60,
	.height_mm = 66,
	.flags = 0,
};

static int nt36860_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct nt36860 *ctx = panel_to_nt36860(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			ctx->desc->mode->hdisplay,
			ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	drm_connector_set_panel_orientation(connector, ctx->desc->orientation);

	return 1;
}

static struct drm_panel_funcs nt36860_funcs = {
	.prepare	= nt36860_prepare,
	.unprepare	= nt36860_unprepare,
	.enable		= nt36860_enable,
	.disable	= nt36860_disable,
	.get_modes	= nt36860_get_modes,
};

static int nt36860_probe(struct mipi_dsi_device *dsi) 
{
	struct nt36860 *ctx;
	struct device *dev = &dsi->dev;
	struct device_node *np = dev->of_node;
	struct nt36860_desc *debug;
	int ret, iter, count, ind;
	static union { 
		u32 tu32[250];
		u8 tu8[1000];
	} temp;

	dev_warn(dev, "probe started for nt36860\n");

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		return -ENOMEM;
	}
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = of_device_get_match_data(&dsi->dev);

	/* Enter debug mode */
	if (of_property_read_bool(np, "debug-on")) {
		dev_info(dev, "debug mode engaged!\n");
		debug = devm_kzalloc(&dsi->dev, sizeof(struct nt36860_desc), GFP_KERNEL);

		if (!debug) {
			return -ENOMEM;
		}

		memcpy(debug, ctx->desc, sizeof(struct nt36860_desc));

		/* Orientation */
		ret = of_property_read_u32(np, "panel-orient", temp.tu32);
		if (ret) {
			dev_err(dev, "panel-orient not specified\n");
		} else {
			debug->orientation = (enum drm_panel_orientation)temp.tu32[0];
		}

		/* Video flags */
		ret = of_property_read_u32(np, "video-flags", temp.tu32);
		if (ret) {
			dev_err(dev, "video-flags not specified\n");
		} else {
			debug->flags = (unsigned long)temp.tu32[0];
		}

		/* Panel mode */
		ret = of_property_read_u32_array(np, "panel-mode", temp.tu32, 12);
		if (ret) {
			dev_err(dev, "panel-mode not specified\n");
		} else {
			debug->mode->clock = (int)temp.tu32[0];
			debug->mode->hdisplay = (u16)temp.tu32[1];
			debug->mode->hsync_start = (u16)temp.tu32[2];
			debug->mode->hsync_end = (u16)temp.tu32[3];
			debug->mode->htotal = (u16)temp.tu32[4];
			debug->mode->vdisplay = (u16)temp.tu32[5];
			debug->mode->vsync_start = (u16)temp.tu32[6];
			debug->mode->vsync_end = (u16)temp.tu32[7];
			debug->mode->vtotal = (u16)temp.tu32[8];
			debug->mode->width_mm = (u16)temp.tu32[9];
			debug->mode->height_mm = (u16)temp.tu32[10];
			debug->mode->flags = (u32)temp.tu32[11];
		}

		/* On commands */
		ret = of_property_read_variable_u8_array(np, "on-cmds", temp.tu8, 3, sizeof(temp.tu8));
		if (ret < 0) {
			dev_err(dev, "on-cmds not specified\n");
		} else {
			/* Count them first */
			count = 0;
			for (iter = 0; iter < ret;) {
				count++;
				iter += temp.tu8[iter] + 1;
			}

			/* Sanity */
			if (iter != ret) {
				dev_err(dev, "on-cmds contains invalid data\n");
			} else {
				debug->init_len = count;
				debug->init = devm_kzalloc(&dsi->dev, sizeof(struct nt36860_dcs_instr) * count, GFP_KERNEL);

				if (!debug->init) {
					return -ENOMEM;
				}
			}

			/* Update */
			count = 0;
			for (iter = 0; iter < ret;) {
				debug->init[count].len = (size_t)temp.tu8[iter];
				debug->init[count].data = &temp.tu8[iter + 1];
				iter += temp.tu8[iter] + 1;
				count++;
			}
		}

		/* Update */
		ctx->desc = debug;

		/* Show */
		dev_info(dev, "panel-orient: %u\n", (unsigned)debug->orientation);
		dev_info(dev, "video-flags: %u\n", (unsigned)debug->mode);
		dev_info(dev, "video-flags:clock: %u\n", (unsigned)debug->mode->clock);
		dev_info(dev, "video-flags:hdisplay: %u\n", (unsigned)debug->mode->hdisplay);
		dev_info(dev, "video-flags:hsync_start: %u\n", (unsigned)debug->mode->hsync_start);
		dev_info(dev, "video-flags:hsync_end: %u\n", (unsigned)debug->mode->hsync_end);
		dev_info(dev, "video-flags:htotal: %u\n", (unsigned)debug->mode->htotal);
		dev_info(dev, "video-flags:vdisplay: %u\n", (unsigned)debug->mode->vdisplay);
		dev_info(dev, "video-flags:vsync_start: %u\n", (unsigned)debug->mode->vsync_start);
		dev_info(dev, "video-flags:vsync_end: %u\n", (unsigned)debug->mode->vsync_end);
		dev_info(dev, "video-flags:width_mm: %u\n", (unsigned)debug->mode->width_mm);
		dev_info(dev, "video-flags:height_mm: %u\n", (unsigned)debug->mode->height_mm);
		dev_info(dev, "video-flags:flags: %u\n", (unsigned)debug->mode->flags);
		dev_info(dev, "video-flags:flags: %u\n", (unsigned)debug->mode->flags);
		dev_info(dev, "on-cmds:count: %u\n", (unsigned)debug->init_len);

		// for (iter = 0; iter < count; iter++) {
		// 	dev_info(dev, "on-cmds:%u: [", iter);
		// 	for (ind = 0; ind < debug->init[iter].len; ++ind) {
		// 		dev_info(dev, "%02x", debug->init[iter].data[ind]);
		// 	}
		// 	dev_info(dev, "]\n");
		// }
	}

	ctx->vddi = devm_regulator_get_optional(dev, "vddi");
	if (IS_ERR(ctx->vddi)) {
		ret = PTR_ERR(ctx->vddi);

		if (ret != -ENODEV) {
			if (ret != -EPROBE_DEFER) {
				dev_err(dev, "failed to get vddi regulator: %d\n", ret);
			}
			return ret;
		}

		ctx->vddi = NULL;
	}

	ctx->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ctx->avdd)) {
		ret = PTR_ERR(ctx->avdd);
		if (ret != -EPROBE_DEFER) {
			dev_err(dev, "failed to get avdd regulator %d\n", ret);
		}
		return ret;
	}

	ctx->avee = devm_regulator_get(dev, "avee");
	if (IS_ERR(ctx->avee)) {
		ret = PTR_ERR(ctx->avee);
		if (ret != -EPROBE_DEFER) {
			dev_err(dev, "failed to get avee regulator %d\n", ret);
		}
		return ret;
	}

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset)) {
		ret = PTR_ERR(ctx->reset);
		if (ret != -EPROBE_DEFER) {
			dev_err(dev, "failed to get reset gpio %d\n", ret);
		}
		return ret;
	}

	ctx->panel.prepare_upstream_first = true;
	drm_panel_init(&ctx->panel, dev, &nt36860_funcs, DRM_MODE_CONNECTOR_DSI);

	/* TODO Create backlight */

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = ctx->desc->flags;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		drm_panel_remove(&ctx->panel);
	}

	return ret;
}

static int nt36860_remove(struct mipi_dsi_device *dsi)
{
	struct nt36860 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static struct nt36860_desc jdi_lpm035m407b_desc = {
	.init = jdi_lpm035m407b_init,
	.init_len = ARRAY_SIZE(jdi_lpm035m407b_init),
	.mode = &jdi_lpm035m407b_mode,
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL
};

static const struct of_device_id nt36860_of_match[] = {
	{ .compatible = "jdi,lpm035m407b-video", .data = &jdi_lpm035m407b_desc },
	{ },
};
MODULE_DEVICE_TABLE(of, nt36860_of_match);

static struct mipi_dsi_driver nt36860_panel_driver = {
	.driver = {
		.name = "panel-novatek-nt36860",
		.of_match_table = nt36860_of_match,
	},
	.probe = nt36860_probe,
	.remove = nt36860_remove,
	.shutdown = NULL, /* TODO ?? */
};
module_mipi_dsi_driver(nt36860_panel_driver);

MODULE_AUTHOR("Ionut Catalin Pavel <iocapa@iocapa.com>");
MODULE_DESCRIPTION("NOVATEK NT36860 based MIPI-DSI LCD panel driver");
MODULE_LICENSE("GPL v2");