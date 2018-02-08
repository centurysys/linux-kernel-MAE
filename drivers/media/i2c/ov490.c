// SPDX-License-Identifier: GPL-2.0
/*
 * OmniVision OV490 Camera Driver
 *
 * Copyright (C) 2018 Texas Instruments Incorporated -  http://www.ti.com/
 * Author: Nikhil Devshatwar <nikhil.nd@ti.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <media/soc_camera.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>

#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

/* Register definitions */
#define OV490_PID			0x300a
#define OV490_VER			0x300b
#define OV490_BANK_HIGH			0xfffd
#define OV490_BANK_LOW			0xfffe

#define OV490_MIPI_TX_LANE_CTRL2	0x8029202D
#define OV490_MIPI_TX_LANE_CTRL0	0x80292015

#define OV490_SC_RESET1			0x80800011

/* IDs */
#define OV490_VERSION_REG		0x0490
#define OV490_VERSION(pid, ver)	(((pid) << 8) | ((ver) & 0xff))

#define OV490_MAX_WIDTH			1280
#define OV490_MAX_HEIGHT		800

#define MAX_NUM_GPIOS			10

/*
 * = fvco / pixel_width * num_lanes
 * = 804,000,000 / 16 bits * 4 lanes
 */
#define OV490_PIXEL_RATE_PER_LANE	50250000

struct ov490_regval {
	u32 addr;
	u8 val;
} ov490_default_regs[] = {
	{ 0x80195000, 0x01, },
	{ 0x80195001, 0x01, },
	{ 0x80195002, 0x05, },
	{ 0x80195003, 0x08, },
	{ 0x80195004, 0x04, },
	{ 0x80195005, 0x40, },
	{ 0x80195006, 0x05, },
	{ 0x80195007, 0x08, },
	{ 0x80195008, 0x04, },
	{ 0x80195009, 0x40, },
	{ 0x8019500A, 0x00, },
	{ 0x80195000, 0x31, },

	{ 0x808000C0, 0x39, },
	{ 0x808000C0, 0xE2, },

	{ 0x8082000A, 0x92, },
};

struct ov490_color_format {
	u32 code;
	u32 colorspace;
};

struct ov490_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_async_subdev	asd;
	const struct ov490_color_format	*cfmt;
	int				width;
	int				height;
	int				num_lanes;

	struct regmap			*regmap;

	struct gpio_descs		*mux_gpios;

	struct v4l2_ctrl_handler	handler;
	struct v4l2_ctrl		*pixel_rate;
};

/* Main access control */
DEFINE_MUTEX(ov490_lock);
static int ov490_init_gpios(struct i2c_client *client);

/*
 * supported color format list
 */
static const struct ov490_color_format ov490_cfmts[] = {
	{
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_SMPTE170M,
	},
};

static struct ov490_priv *to_ov490(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov490_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov490_priv, handler)->subdev;
}

static int ov490_reg_write32(struct regmap *map, u32 reg, u8 val)
{
	u8 bank_high = (reg >> 24) & 0xff;
	u8 bank_low  = (reg >> 16) & 0xff;
	u16 reg_addr = reg & 0xffff;
	int ret = 0;

	/* For writing a register with 32 bit address, First set the bank
	 * address by writing to two BANK address registers. Then access
	 * the register using 16LSB bits.
	 */
	ret = regmap_write(map, OV490_BANK_HIGH, bank_high);
	if (!ret)
		ret = regmap_write(map, OV490_BANK_LOW, bank_low);
	if (!ret)
		ret = regmap_write(map, reg_addr, val);
	return ret;
}

/* Start/Stop streaming from the device */
static int ov490_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);
	struct regmap *map = priv->regmap;
	int ret = 0, i, val;

	mutex_lock(&ov490_lock);
	ret = ov490_init_gpios(client);
	if (ret) {
		dev_err(&client->dev, "Failed to request gpios");
		goto unlock;
	}

	if (enable) {
		/* Take MIPI_TX out of reset */
		ov490_reg_write32(map, OV490_SC_RESET1, 0x00);
		ov490_reg_write32(map, OV490_MIPI_TX_LANE_CTRL0, 0x80);
	} else {
		ov490_reg_write32(map, OV490_MIPI_TX_LANE_CTRL0, 0xa0);
		/* Put MIPI_TX in reset */
		ov490_reg_write32(map, OV490_SC_RESET1, 0x80);
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(ov490_default_regs); i++) {
		ret = ov490_reg_write32(map,
					ov490_default_regs[i].addr,
					ov490_default_regs[i].val);
		if (ret)
			goto unlock;
	}

	/* These register updates triggers a routine to configure ISP
	 * Wait for a while before any more changes are done
	 */
	mdelay(5);

	val = priv->num_lanes == 2 ? 0x03 : priv->num_lanes == 4 ? 0x0F : 0x0F;
	dev_info(&client->dev, "Using %d data lanes\n", priv->num_lanes);
	ov490_reg_write32(map, OV490_MIPI_TX_LANE_CTRL2, val);

unlock:
	mutex_unlock(&ov490_lock);
	return ret;
}

static int ov490_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	mf->width	= priv->width;
	mf->height	= priv->height;
	mf->code	= priv->cfmt->code;
	mf->colorspace	= priv->cfmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

/* Fixed format - no configurability */
static int ov490_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	mf->width	= priv->width;
	mf->height	= priv->height;
	mf->code	= priv->cfmt->code;
	mf->colorspace	= priv->cfmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov490_enum_code(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(ov490_cfmts))
		return -EINVAL;

	code->code = ov490_cfmts[code->index].code;

	return 0;
}

static int ov490_enum_size(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_frame_size_enum *fse)
{
	int cam_width[] =	{ OV490_MAX_WIDTH };
	int cam_height[] =	{ OV490_MAX_HEIGHT };

	if (fse->index >= ARRAY_SIZE(cam_width))
		return -EINVAL;

	fse->min_width  = cam_width[fse->index];
	fse->max_width  = fse->min_width;
	fse->min_height = cam_height[fse->index];
	fse->max_height = fse->min_height;
	return 0;
}

static int ov490_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* If the board has programmable pixel clock, set it here */
	if (ctrl->id == V4L2_CID_PIXEL_RATE)
		dev_info(&client->dev, "Pixel rate set to %d\n",
			 ctrl->val);
	return 0;
}

static int ov490_init_gpios(struct i2c_client *client)
{
	struct ov490_priv *priv = to_ov490(client);
	int ret = 0;

	/* Request the gpio lines and set the values
	 * then release them so that other drivers can use them
	 * This allows changing common board muxes which are
	 * controlled by multiple drivers
	 */
	priv->mux_gpios = gpiod_get_array(&client->dev, "mux", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->mux_gpios))
		goto done;
	gpiod_put_array(priv->mux_gpios);
done:
	return ret;
}

static int ov490_video_probe(struct i2c_client *client)
{
	struct ov490_priv *priv = i2c_get_clientdata(client);
	u32 pid, ver;
	int ret;

	/* check and show product ID and manufacturer ID */
	ret = regmap_read(priv->regmap, OV490_PID, &pid);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, OV490_VER, &ver);
	if (ret)
		return ret;

	if (OV490_VERSION(pid, ver) != OV490_VERSION_REG) {
		dev_err(&client->dev, "Product ID error %02x:%02x\n", pid, ver);
				return -ENODEV;
	}

	dev_info(&client->dev, "ov490 Product ID %02x Manufacturer ID %02x\n",
		 pid, ver);

	return 0;
}

static const struct v4l2_subdev_video_ops ov490_video_ops = {
	.s_stream		= ov490_s_stream,
};

static const struct v4l2_subdev_core_ops ov490_core_ops = {
	.log_status		= v4l2_ctrl_subdev_log_status,
	.subscribe_event	= v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,

};

static const struct v4l2_subdev_pad_ops ov490_pad_ops = {
	.enum_mbus_code		= ov490_enum_code,
	.enum_frame_size	= ov490_enum_size,
	.get_fmt		= ov490_get_fmt,
	.set_fmt		= ov490_set_fmt,
};

static const struct v4l2_subdev_ops ov490_subdev_ops = {
	.video	= &ov490_video_ops,
	.core	= &ov490_core_ops,
	.pad	= &ov490_pad_ops,
};

static const struct v4l2_ctrl_ops ov490_ctrl_ops = {
	.s_ctrl = ov490_s_ctrl,
};

static const struct regmap_config ov490_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

/*
 * i2c_driver function
 */

static int ov490_of_probe(struct i2c_client *client,
			  struct device_node *node)
{
	struct ov490_priv *priv = to_ov490(client);
	struct v4l2_fwnode_endpoint endpoint = {};
	struct device_node *ep;
	int num_lanes = 0;

	ep = of_graph_get_next_endpoint(node, NULL);
	if (ep) {
		v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &endpoint);
		if (endpoint.bus_type == V4L2_MBUS_CSI2) {
			num_lanes = endpoint.bus.mipi_csi2.num_data_lanes;
			if (num_lanes == 2 || num_lanes == 4)
				priv->num_lanes = num_lanes;
		} else {
			dev_err(&client->dev, "Endpoint bus is not CSI bus!");
		}
	}
	return 0;
}

static int ov490_probe(struct i2c_client *client,
		       const struct i2c_device_id *did)
{
	struct device_node *node = client->dev.of_node;
	struct v4l2_ctrl_handler *hdl;
	struct ov490_priv *priv;
	struct v4l2_subdev *sd;
	int ret = 0;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);
	priv->num_lanes = 4;
	priv->cfmt = &ov490_cfmts[0];
	priv->width = OV490_MAX_WIDTH;
	priv->height = OV490_MAX_HEIGHT;

	priv->regmap = devm_regmap_init_i2c(client, &ov490_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = ov490_video_probe(client);
	if (ret)
		goto err;

	ret = ov490_of_probe(client, node);
	if (ret)
		goto err;

	sd = &priv->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov490_subdev_ops);

	hdl = &priv->handler;
	sd->ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 1);
	priv->pixel_rate =
		v4l2_ctrl_new_std(hdl, &ov490_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1,
				  OV490_PIXEL_RATE_PER_LANE * priv->num_lanes);

	if (hdl->error) {
		dev_err(&client->dev, "Failed to add controls");
		ret = hdl->error;
		goto err;
	}

	ret = ov490_init_gpios(client);
	if (ret) {
		dev_err(&client->dev, "Failed to request gpios");
		goto err;
	}

	sd->dev = &client->dev;
	ret = v4l2_async_register_subdev(sd);

err:
	return ret;
}

static int ov490_remove(struct i2c_client *client)
{
	struct ov490_priv *priv = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->handler);

	return 0;
}

static const struct i2c_device_id ov490_id[] = {
	{ "ov490", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov490_id);

static const struct of_device_id ov490_dt_id[] = {
	{
		.compatible = "ovti,ov490",
	},
	{
	}
};

static struct i2c_driver ov490_i2c_driver = {
	.driver = {
		.name	= "ov490",
		.of_match_table = ov490_dt_id,
	},
	.probe = ov490_probe,
	.remove = ov490_remove,
	.id_table = ov490_id,
};

module_i2c_driver(ov490_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for OmniVision OV490");
MODULE_AUTHOR("Nikhil Devshatwar <nikhil.nd@ti.com>");
MODULE_LICENSE("GPL v2");
