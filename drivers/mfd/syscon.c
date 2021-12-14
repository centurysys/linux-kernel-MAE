// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * System Control Driver
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_data/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#define syscon_alloc(_dev, args...) ({					\
	void *_ptr;							\
	if (_dev)							\
		_ptr = devm_kzalloc((_dev), args);			\
	else								\
		_ptr = kzalloc(args);					\
	(_ptr);								\
})

#define syscon_get_resource(_pdev, _np, _idx, _res) ({			\
	int _ret = -EINVAL;						\
	if (_pdev) {							\
		struct resource *_r;					\
		_r = platform_get_resource((_pdev), IORESOURCE_MEM, (_idx));\
		if (!_r) {						\
			_ret = -ENOMEM;					\
		} else {						\
			(_res) = *_r;					\
			_ret = 0;					\
		}							\
	} else if (_np) {						\
		_ret = of_address_to_resource((_np), (_idx), &(_res));	\
	}								\
	(_ret);								\
})

static struct platform_driver syscon_driver;

static DEFINE_SPINLOCK(syscon_list_slock);
static LIST_HEAD(syscon_list);

struct syscon {
	struct device_node *np;
	struct regmap *regmap;
	struct list_head list;
};

static const struct regmap_config syscon_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const struct regmap_access_table *
syscon_prepare_regmap_access_table(struct platform_device *pdev,
				   struct device_node *np, u32 reg_io_width,
				   int entries)
{
	struct regmap_access_table *at;
	struct regmap_range *yes_ranges, *no_ranges = NULL;
	struct device *dev = pdev ? &pdev->dev : NULL;
	struct resource res;
	resource_size_t base_offset, offset;
	int i, ret;

	/* Allocate memory for access table. */
	at = syscon_alloc(dev, sizeof(*at), GFP_KERNEL);
	if (!at)
		return ERR_PTR(-ENOMEM);

	/* Allocate memory for allowed ranges. */
	yes_ranges = syscon_alloc(dev, entries * sizeof(*yes_ranges),
				  GFP_KERNEL);
	if (!yes_ranges) {
		ret = -ENOMEM;
		goto free;
	}

	/* Allocate memory for invalid ranges. */
	if (entries > 1) {
		no_ranges = syscon_alloc(dev,
					 (entries - 1) * sizeof(*no_ranges),
					 GFP_KERNEL);
		if (!no_ranges) {
			ret = -ENOMEM;
			goto free;
		}
	}

	/* Populate allowed and invalid ranges. */
	ret = syscon_get_resource(pdev, np, 0, res);
	if (ret)
		goto free;

	base_offset = res.start;
	yes_ranges[0].range_max = resource_size(&res) - reg_io_width;
	if (entries > 1)
		no_ranges[0].range_min = resource_size(&res);

	for (i = 1; i < entries; i++) {
		ret = syscon_get_resource(pdev, np, i, res);
		if (ret)
			goto free;

		offset = res.start - base_offset;
		yes_ranges[i].range_min = offset;
		yes_ranges[i].range_max = offset + resource_size(&res) -
					  reg_io_width;
		if (i != entries - 1)
			no_ranges[i].range_min = offset + resource_size(&res);
		no_ranges[i - 1].range_max = offset - reg_io_width;
	}

	/* Store them to access table. */
	at->yes_ranges = yes_ranges;
	at->n_yes_ranges = entries;
	at->no_ranges = no_ranges;
	at->n_no_ranges = entries > 1 ? entries - 1 : 0;

	return at;

free:
	if (!dev) {
		kfree(no_ranges);
		kfree(yes_ranges);
		kfree(at);
	}

	return ERR_PTR(ret);
}

static struct syscon *of_syscon_register(struct device_node *np, bool check_clk)
{
	struct clk *clk;
	struct syscon *syscon;
	struct regmap *regmap;
	void __iomem *base;
	const struct regmap_access_table *at;
	u32 reg_io_width;
	int ret, n_res = 0;
	struct regmap_config syscon_config = syscon_regmap_config;
	struct resource res;

	syscon = kzalloc(sizeof(*syscon), GFP_KERNEL);
	if (!syscon)
		return ERR_PTR(-ENOMEM);

	/* Count the number of resources. */
	while (of_address_to_resource(np, n_res, &res) == 0)
		n_res++;
	if (!n_res) {
		ret = -ENOMEM;
		goto err_map;
	}

	/*
	 * search for reg-io-width property in DT. If it is not provided,
	 * default to 4 bytes. regmap_init_mmio will return an error if values
	 * are invalid so there is no need to check them here.
	 */
	ret = of_property_read_u32(np, "reg-io-width", &reg_io_width);
	if (ret)
		reg_io_width = 4;

	at = syscon_prepare_regmap_access_table(NULL, np, reg_io_width, n_res);
	if (IS_ERR(at)) {
		ret = PTR_ERR(at);
		goto err_map;
	}

	base = of_iomap(np, 0);
	if (!base) {
		ret = -ENOMEM;
		goto err_map;
	}

	/* Parse the device's DT node for an endianness specification */
	if (of_property_read_bool(np, "big-endian"))
		syscon_config.val_format_endian = REGMAP_ENDIAN_BIG;
	else if (of_property_read_bool(np, "little-endian"))
		syscon_config.val_format_endian = REGMAP_ENDIAN_LITTLE;
	else if (of_property_read_bool(np, "native-endian"))
		syscon_config.val_format_endian = REGMAP_ENDIAN_NATIVE;

	ret = of_hwspin_lock_get_id(np, 0);
	if (ret > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && ret == 0)) {
		syscon_config.use_hwlock = true;
		syscon_config.hwlock_id = ret;
		syscon_config.hwlock_mode = HWLOCK_IRQSTATE;
	} else if (ret < 0) {
		switch (ret) {
		case -ENOENT:
			/* Ignore missing hwlock, it's optional. */
			break;
		default:
			pr_err("Failed to retrieve valid hwlock: %d\n", ret);
			fallthrough;
		case -EPROBE_DEFER:
			goto err_regmap;
		}
	}

	syscon_config.name = kasprintf(GFP_KERNEL, "%pOFn@%llx", np,
				       (u64)res.start);
	syscon_config.reg_stride = reg_io_width;
	syscon_config.val_bits = reg_io_width * 8;
	syscon_config.wr_table = at;
	syscon_config.rd_table = at;

	regmap = regmap_init_mmio(NULL, base, &syscon_config);
	kfree(syscon_config.name);
	if (IS_ERR(regmap)) {
		pr_err("regmap init failed\n");
		ret = PTR_ERR(regmap);
		goto err_regmap;
	}

	if (check_clk) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			/* clock is optional */
			if (ret != -ENOENT)
				goto err_clk;
		} else {
			ret = regmap_mmio_attach_clk(regmap, clk);
			if (ret)
				goto err_attach;
		}
	}

	syscon->regmap = regmap;
	syscon->np = np;

	spin_lock(&syscon_list_slock);
	list_add_tail(&syscon->list, &syscon_list);
	spin_unlock(&syscon_list_slock);

	return syscon;

err_attach:
	if (!IS_ERR(clk))
		clk_put(clk);
err_clk:
	regmap_exit(regmap);
err_regmap:
	iounmap(base);
err_map:
	kfree(syscon);
	kfree(at->no_ranges);
	kfree(at->yes_ranges);
	kfree(at);
	return ERR_PTR(ret);
}

static struct regmap *device_node_get_regmap(struct device_node *np,
					     bool check_clk)
{
	struct syscon *entry, *syscon = NULL;

	spin_lock(&syscon_list_slock);

	list_for_each_entry(entry, &syscon_list, list)
		if (entry->np == np) {
			syscon = entry;
			break;
		}

	spin_unlock(&syscon_list_slock);

	if (!syscon)
		syscon = of_syscon_register(np, check_clk);

	if (IS_ERR(syscon))
		return ERR_CAST(syscon);

	return syscon->regmap;
}

struct regmap *device_node_to_regmap(struct device_node *np)
{
	return device_node_get_regmap(np, false);
}
EXPORT_SYMBOL_GPL(device_node_to_regmap);

struct regmap *syscon_node_to_regmap(struct device_node *np)
{
	if (!of_device_is_compatible(np, "syscon"))
		return ERR_PTR(-EINVAL);

	return device_node_get_regmap(np, true);
}
EXPORT_SYMBOL_GPL(syscon_node_to_regmap);

struct regmap *syscon_regmap_lookup_by_compatible(const char *s)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	syscon_np = of_find_compatible_node(NULL, NULL, s);
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_compatible);

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
					const char *property)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	if (property)
		syscon_np = of_parse_phandle(np, property, 0);
	else
		syscon_np = np;

	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_phandle);

struct regmap *syscon_regmap_lookup_by_phandle_args(struct device_node *np,
					const char *property,
					int arg_count,
					unsigned int *out_args)
{
	struct device_node *syscon_np;
	struct of_phandle_args args;
	struct regmap *regmap;
	unsigned int index;
	int rc;

	rc = of_parse_phandle_with_fixed_args(np, property, arg_count,
			0, &args);
	if (rc)
		return ERR_PTR(rc);

	syscon_np = args.np;
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	for (index = 0; index < arg_count; index++)
		out_args[index] = args.args[index];
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_phandle_args);

/*
 * It behaves the same as syscon_regmap_lookup_by_phandle() except where
 * there is no regmap phandle. In this case, instead of returning -ENODEV,
 * the function returns NULL.
 */
struct regmap *syscon_regmap_lookup_by_phandle_optional(struct device_node *np,
					const char *property)
{
	struct regmap *regmap;

	regmap = syscon_regmap_lookup_by_phandle(np, property);
	if (IS_ERR(regmap) && PTR_ERR(regmap) == -ENODEV)
		return NULL;

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_phandle_optional);

static int syscon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct syscon_platform_data *pdata = dev_get_platdata(dev);
	struct syscon *syscon;
	struct regmap_config syscon_config = syscon_regmap_config;
	const struct regmap_access_table *at;
	struct resource *res;
	void __iomem *base;
	int n_res = 0;

	syscon = devm_kzalloc(dev, sizeof(*syscon), GFP_KERNEL);
	if (!syscon)
		return -ENOMEM;

	/* Count the number of resources. */
	while (platform_get_resource(pdev, IORESOURCE_MEM, n_res))
		n_res++;
	if (!n_res)
		return -ENOENT;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	at = syscon_prepare_regmap_access_table(pdev, NULL, 4, n_res);
	if (IS_ERR(at))
		return PTR_ERR(at);

	syscon_config.wr_table = at;
	syscon_config.rd_table = at;
	if (pdata)
		syscon_config.name = pdata->label;
	syscon->regmap = devm_regmap_init_mmio(dev, base, &syscon_config);
	if (IS_ERR(syscon->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(syscon->regmap);
	}

	platform_set_drvdata(pdev, syscon);

	dev_dbg(dev, "regmap %pR registered\n", res);

	return 0;
}

static const struct platform_device_id syscon_ids[] = {
	{ "syscon", },
	{ }
};

static struct platform_driver syscon_driver = {
	.driver = {
		.name = "syscon",
	},
	.probe		= syscon_probe,
	.id_table	= syscon_ids,
};

static int __init syscon_init(void)
{
	return platform_driver_register(&syscon_driver);
}
postcore_initcall(syscon_init);
