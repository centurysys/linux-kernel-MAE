// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip Image Sensor Controller (ISC) Media Controller support
 *
 * Copyright (C) 2021 Microchip Technology, Inc.
 *
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "atmel-isc-regs.h"
#include "atmel-isc.h"

static const struct media_device_ops isc_media_ops = {
};

static int isc_scaler_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);
	struct v4l2_mbus_framefmt *v4l2_try_fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_get_try_format(sd, sd_state,
							  format->pad);
		format->format = *v4l2_try_fmt;

		return 0;
	}

	format->format = isc->scaler_format;

	return 0;
}

static int isc_scaler_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *req_fmt)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);
	struct v4l2_mbus_framefmt *v4l2_try_fmt;
	struct isc_format *fmt;
	unsigned int i;

	if (req_fmt->pad == ISC_SCALER_PAD_SOURCE)
		v4l_bound_align_image
			(&req_fmt->format.width, 16, isc->max_width, 0,
			 &req_fmt->format.height, 16, isc->max_height, 0, 0);
	else
		v4l_bound_align_image
			(&req_fmt->format.width, 16, 10000, 0,
			 &req_fmt->format.height, 16, 10000, 0, 0);

	req_fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
	req_fmt->format.field = V4L2_FIELD_NONE;
	req_fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	req_fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	req_fmt->format.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	fmt = isc_find_format_by_code(isc, req_fmt->format.code, &i);

	if (!fmt)
		fmt = &isc->formats_list[0];

	req_fmt->format.code = fmt->mbus_code;

	if (req_fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_get_try_format(sd, sd_state,
							  req_fmt->pad);
		*v4l2_try_fmt = req_fmt->format;
		/* Trying on the pad sink makes the source sink change too */
		if (req_fmt->pad == ISC_SCALER_PAD_SINK) {
			v4l2_try_fmt =
				v4l2_subdev_get_try_format(sd, sd_state,
							   ISC_SCALER_PAD_SOURCE);
			*v4l2_try_fmt = req_fmt->format;

			v4l_bound_align_image(&v4l2_try_fmt->width,
					      16, isc->max_width, 0,
					      &v4l2_try_fmt->height,
					      16, isc->max_height, 0, 0);
		}
		/* if we are just trying, we are done */
		return 0;
	}

	isc->scaler_format = req_fmt->format;

	return 0;
}

static int isc_scaler_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);
	int supported_index = 0;
	int i;

	for (i = 0; i < isc->formats_list_size; i++) {
		if (!isc->formats_list[i].sd_support)
			continue;
		if (supported_index == code->index) {
			code->code = isc->formats_list[i].mbus_code;
			return 0;
		}
		supported_index++;
	}

	return -EINVAL;
}

static int isc_scaler_g_sel(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_selection *sel)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);

	if (sel->pad == ISC_SCALER_PAD_SOURCE)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sel->r.height = isc->max_height;
	sel->r.width = isc->max_width;

	sel->r.left = 0;
	sel->r.top = 0;

	return 0;
}

static int isc_scaler_init_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *v4l2_try_fmt =
		v4l2_subdev_get_try_format(sd, sd_state, 0);
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);

	*v4l2_try_fmt = isc->scaler_format;

	return 0;
}

static const struct v4l2_subdev_pad_ops isc_scaler_pad_ops = {
	.enum_mbus_code = isc_scaler_enum_mbus_code,
	.set_fmt = isc_scaler_set_fmt,
	.get_fmt = isc_scaler_get_fmt,
	.get_selection = isc_scaler_g_sel,
	.init_cfg = isc_scaler_init_cfg,
};

static const struct v4l2_subdev_ops xisc_scaler_subdev_ops = {
	.pad = &isc_scaler_pad_ops,
};

static int isc_init_own_sd(struct isc_device *isc)
{
	int ret;

	v4l2_subdev_init(&isc->scaler_sd, &xisc_scaler_subdev_ops);

	isc->scaler_sd.owner = THIS_MODULE;
	isc->scaler_sd.dev = isc->dev;
	snprintf(isc->scaler_sd.name, sizeof(isc->scaler_sd.name),
		 "atmel_isc_scaler");

	isc->scaler_sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	isc->scaler_sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	isc->scaler_pads[ISC_SCALER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isc->scaler_pads[ISC_SCALER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	isc->scaler_format.height = isc->max_height;
	isc->scaler_format.width = isc->max_width;
	isc->scaler_format.code = isc->formats_list[0].mbus_code;
	isc->scaler_format.colorspace = V4L2_COLORSPACE_SRGB;
	isc->scaler_format.field = V4L2_FIELD_NONE;
	isc->scaler_format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	isc->scaler_format.quantization = V4L2_QUANTIZATION_DEFAULT;
	isc->scaler_format.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	ret = media_entity_pads_init(&isc->scaler_sd.entity,
				     ISC_SCALER_PADS_NUM,
				     isc->scaler_pads);
	if (ret < 0) {
		dev_err(isc->dev, "scaler sd media entity init failed\n");
		return ret;
	}
	ret = v4l2_device_register_subdev(&isc->v4l2_dev, &isc->scaler_sd);
	if (ret < 0) {
		dev_err(isc->dev, "scaler sd failed to register subdev\n");
		return ret;
	}

	return ret;
}

int isc_mc_init(struct isc_device *isc, u32 ver)
{
	const struct of_device_id *match;
	int ret;

	isc->video_dev.entity.function = MEDIA_ENT_F_IO_V4L;
	isc->video_dev.entity.flags = MEDIA_ENT_FL_DEFAULT;
	isc->pads[ISC_PAD_SINK].flags = MEDIA_PAD_FL_SINK;

	ret = media_entity_pads_init(&isc->video_dev.entity, ISC_PADS_NUM,
				     isc->pads);
	if (ret < 0) {
		dev_err(isc->dev, "media entity init failed\n");
		return ret;
	}

	isc->mdev.dev = isc->dev;
	isc->mdev.ops = &isc_media_ops;

	match = of_match_node(isc->dev->driver->of_match_table,
			      isc->dev->of_node);

	strscpy(isc->mdev.driver_name, KBUILD_MODNAME,
		sizeof(isc->mdev.driver_name));
	strscpy(isc->mdev.model, match->compatible, sizeof(isc->mdev.model));
	snprintf(isc->mdev.bus_info, sizeof(isc->mdev.bus_info), "platform:%s",
		 isc->v4l2_dev.name);
	isc->mdev.hw_revision = ver;

	media_device_init(&isc->mdev);

	isc->v4l2_dev.mdev = &isc->mdev;

	return isc_init_own_sd(isc);
}
EXPORT_SYMBOL_GPL(isc_mc_init);

int isc_mc_register(struct isc_device *isc)
{
	int ret;

	ret = media_create_pad_link(&isc->current_subdev->sd->entity,
				    isc->remote_pad, &isc->scaler_sd.entity,
				    ISC_SCALER_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);

	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "Failed to create pad link: %s to %s\n",
			 isc->current_subdev->sd->entity.name,
			 isc->scaler_sd.entity.name);
		return ret;
	}

	dev_dbg(isc->dev, "link with %s pad: %d\n",
		isc->current_subdev->sd->name, isc->remote_pad);

	ret = media_create_pad_link(&isc->scaler_sd.entity,
				    ISC_SCALER_PAD_SOURCE,
				    &isc->video_dev.entity, ISC_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);

	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "Failed to create pad link: %s to %s\n",
			 isc->scaler_sd.entity.name,
			 isc->video_dev.entity.name);
		return ret;
	}

	dev_dbg(isc->dev, "link with %s pad: %d\n", isc->scaler_sd.name,
		ISC_SCALER_PAD_SOURCE);

	return media_device_register(&isc->mdev);
}
EXPORT_SYMBOL_GPL(isc_mc_register);

void isc_mc_cleanup(struct isc_device *isc)
{
	media_entity_cleanup(&isc->video_dev.entity);
}
EXPORT_SYMBOL_GPL(isc_mc_cleanup);
