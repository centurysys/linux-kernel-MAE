/*
 * plum_backlight.c - PlumLCD backlight
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PLUM_BACKLIGHT_MAX 9


struct plum_backlight {
	struct device *dev;
	struct device *fbdev;

	void __iomem *base;

	int value;
};

static int plum_backlight_update_status(struct backlight_device *bl)
{
	struct plum_backlight *pbl = bl_get_data(bl);
	int brightness = bl->props.brightness;
	unsigned char reg;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	pbl->value = brightness;

	if (brightness > 0) {
		reg = (1 << 4) | (brightness - 1);
	} else {
		reg = 0;
	}

	*(volatile unsigned char *) pbl->base = reg;

	return 0;
}

static int plum_backlight_check_fb(struct backlight_device *bl,
				   struct fb_info *info)
{
	struct plum_backlight *pbl = bl_get_data(bl);

	return pbl->fbdev == NULL || pbl->fbdev == info->dev;
}

static const struct backlight_ops plum_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= plum_backlight_update_status,
	.check_fb	= plum_backlight_check_fb,
};

static int plum_backlight_probe_dt(struct platform_device *pdev,
				   struct plum_backlight *pbl)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bool on;

	pbl->base = devm_ioremap_nocache(&pdev->dev, regs->start,
					 resource_size(regs));
	if (!pbl->base)
		return -ENODEV;

	on = of_property_read_bool(np, "default-on");

	if (on) {
		pbl->value = PLUM_BACKLIGHT_MAX + 1;
	} else {
		pbl->value = 0;
	}

	return 0;
}

static int plum_backlight_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct plum_backlight *pbl;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev,
			"failed to find device tree node.\n");
		return -ENODEV;
	}

	pbl = devm_kzalloc(&pdev->dev, sizeof(*pbl), GFP_KERNEL);
	if (pbl == NULL)
		return -ENOMEM;

	pbl->dev = &pdev->dev;

	ret = plum_backlight_probe_dt(pdev, pbl);
	if (ret)
		return ret;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = PLUM_BACKLIGHT_MAX + 1;
	bl = devm_backlight_device_register(&pdev->dev, "plumLCD-Backlight",
					&pdev->dev, pbl, &plum_backlight_ops,
					&props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = pbl->value;
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id plum_backlight_of_match[] = {
	{ .compatible = "plum-backlight" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, plum_backlight_of_match);
#endif

static struct platform_driver plum_backlight_driver = {
	.driver		= {
		.name		= "plum-backlight",
		.of_match_table = of_match_ptr(plum_backlight_of_match),
	},
	.probe		= plum_backlight_probe,
};

module_platform_driver(plum_backlight_driver);

MODULE_AUTHOR("Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("PLUM-LCD Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:plum-backlight");
