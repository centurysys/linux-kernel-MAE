// SPDX-License-Identifier: GPL-2.0-only
/*
 * ltc2481.c - Driver for Analog Devices/Linear Technology LTC2481 ADC
 *
 * Copyright (C) 2017 Analog Devices Inc.
 * Copyright (C) 2023 Century Systems
 *
 * Datasheet: http://cds.linear.com/docs/en/datasheet/2481fd.pdf
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#define LTC2481_SGL			BIT(4)
#define LTC2481_DIFF			0
#define LTC2481_SIGN			BIT(3)
#define LTC2481_CONVERSION_TIME_MS	170ULL

struct ltc2481_st {
	struct i2c_client *client;
	struct regulator *ref;
	ktime_t	time_prev;
	bool started;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be32 buf ____cacheline_aligned;
};

static int ltc2481_wait_conv(struct ltc2481_st *st)
{
	s64 time_elapsed;

	time_elapsed = ktime_ms_delta(ktime_get(), st->time_prev);

	if (time_elapsed < LTC2481_CONVERSION_TIME_MS) {
		/* delay if conversion time not passed
		 * since last read or write
		 */
		if (msleep_interruptible(
		    LTC2481_CONVERSION_TIME_MS - time_elapsed))
			return -ERESTARTSYS;

		return 0;
	}

	if (time_elapsed - LTC2481_CONVERSION_TIME_MS <= 0) {
		/* We're in automatic mode -
		 * so the last reading is stil not outdated
		 */
		return 0;
	}

	return 1;
}

static int ltc2481_read(struct ltc2481_st *st, u8 address, int *val)
{
	struct i2c_client *client = st->client;
	int ret;
	char buf[1];

	ret = ltc2481_wait_conv(st);
	if (ret < 0)
		return ret;

	if (ret || !st->started) {
		buf[0] = 0;

		ret = i2c_master_send(client, buf, 1);
		if (ret < 0)
			return ret;
		st->started = true;
		if (msleep_interruptible(LTC2481_CONVERSION_TIME_MS))
			return -ERESTARTSYS;
	}
	ret = i2c_master_recv(client, (char *)&st->buf, 3);
	if (ret < 0)  {
		dev_err(&client->dev, "i2c_master_recv failed\n");
		return ret;
	}
	st->time_prev = ktime_get();

	/* convert and shift the result,
	 * and finally convert from offset binary to signed integer
	 */
	*val = (be32_to_cpu(st->buf) >> 14) - (1 << 17);

	return ret;
}

static int ltc2481_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2481_st *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = ltc2481_read(st, chan->address, val);
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(st->ref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 17;

		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

#define LTC2481_CHAN_DIFF(_chan, _addr) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_chan), \
	.address = (_chan), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
}

static const struct iio_chan_spec ltc2481_channel[] = {
	LTC2481_CHAN_DIFF(0, LTC2481_DIFF),
};

static const struct iio_info ltc2481_info = {
	.read_raw = ltc2481_read_raw,
};

static int ltc2481_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ltc2481_st *st;
	struct iio_map *plat_data;
	char buf[1];
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_WRITE_BYTE))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ltc2481_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2481_channel;
	indio_dev->num_channels = ARRAY_SIZE(ltc2481_channel);

	st->ref = devm_regulator_get(&client->dev, "vref");
	if (IS_ERR(st->ref))
		return PTR_ERR(st->ref);

	ret = regulator_enable(st->ref);
	if (ret < 0)
		return ret;

	if (client->dev.platform_data) {
		plat_data = ((struct iio_map *)client->dev.platform_data);
		ret = iio_map_array_register(indio_dev, plat_data);
		if (ret) {
			dev_err(&indio_dev->dev, "iio map err: %d\n", ret);
			goto err_regulator_disable;
		}
	}

	buf[0] = 0;

	ret = i2c_master_send(client, buf, 1);
	if (ret < 0)
		goto err_array_unregister;

	st->started = true;
	st->time_prev = ktime_get();

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto err_array_unregister;

	return 0;

err_array_unregister:
	iio_map_array_unregister(indio_dev);

err_regulator_disable:
	regulator_disable(st->ref);

	return ret;
}

static int ltc2481_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ltc2481_st *st = iio_priv(indio_dev);

	iio_map_array_unregister(indio_dev);
	iio_device_unregister(indio_dev);
	regulator_disable(st->ref);

	return 0;
}

static const struct i2c_device_id ltc2481_id[] = {
	{ "ltc2481", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc2481_id);

static const struct of_device_id ltc2481_of_match[] = {
	{ .compatible = "lltc,ltc2481", },
	{},
};
MODULE_DEVICE_TABLE(of, ltc2481_of_match);

static struct i2c_driver ltc2481_driver = {
	.driver = {
		.name = "ltc2481",
		.of_match_table = of_match_ptr(ltc2481_of_match),
	},
	.probe = ltc2481_probe,
	.remove = ltc2481_remove,
	.id_table = ltc2481_id,
};
module_i2c_driver(ltc2481_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Linear Technology LTC2481 ADC driver");
MODULE_LICENSE("GPL v2");
