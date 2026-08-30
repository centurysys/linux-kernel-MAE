// SPDX-License-Identifier: GPL-2.0-only
/*
 * ltc2481.c - Driver for Analog Devices/Linear Technology LTC2481 ADC
 *
 * Copyright (C) 2017 Analog Devices Inc.
 * Copyright (C) 2023 Century Systems
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/2481fd.pdf
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#include <linux/iio/driver.h>
#include <linux/iio/iio.h>

#define LTC2481_CONFIG_DEFAULT		0x00
#define LTC2481_CONVERSION_TIME_MS	170ULL

struct ltc2481_st {
	struct i2c_client *client;
	struct regulator *ref;
	struct mutex lock;
	ktime_t time_prev;
	bool started;

	/*
	 * I2C controllers may use DMA, so keep the receive buffer on its own
	 * cache line and make it the last member of the structure.
	 */
	u8 data[3] __aligned(IIO_DMA_MINALIGN);
};

static void ltc2481_regulator_disable(void *data)
{
	regulator_disable(data);
}

static int ltc2481_wait_conv(struct ltc2481_st *st)
{
	s64 time_elapsed;

	time_elapsed = ktime_ms_delta(ktime_get(), st->time_prev);

	if (time_elapsed < LTC2481_CONVERSION_TIME_MS) {
		/* Delay until the conversion started by the last transfer ends. */
		if (msleep_interruptible(LTC2481_CONVERSION_TIME_MS - time_elapsed))
			return -ERESTARTSYS;

		return 0;
	}

	if (time_elapsed - LTC2481_CONVERSION_TIME_MS <= 0)
		return 0;

	return 1;
}

static int ltc2481_start_conversion(struct ltc2481_st *st)
{
	u8 command = LTC2481_CONFIG_DEFAULT;
	int ret;

	ret = i2c_master_send(st->client, &command, sizeof(command));
	if (ret != sizeof(command)) {
		if (ret >= 0)
			ret = -EIO;
		dev_err(&st->client->dev, "failed to start conversion: %d\n", ret);
		return ret;
	}

	st->started = true;

	return 0;
}

static int ltc2481_read(struct ltc2481_st *st, int *val)
{
	int ret;

	ret = ltc2481_wait_conv(st);
	if (ret < 0)
		return ret;

	if (ret || !st->started) {
		ret = ltc2481_start_conversion(st);
		if (ret)
			return ret;

		if (msleep_interruptible(LTC2481_CONVERSION_TIME_MS))
			return -ERESTARTSYS;
	}

	ret = i2c_master_recv(st->client, st->data, sizeof(st->data));
	if (ret != sizeof(st->data)) {
		if (ret >= 0)
			ret = -EIO;
		dev_err(&st->client->dev, "failed to read conversion result: %d\n", ret);
		return ret;
	}

	st->time_prev = ktime_get();

	/* Convert the result field from offset binary to a signed integer. */
	*val = (int)(get_unaligned_be24(st->data) >> 6) - (1 << 17);

	return 0;
}

static int ltc2481_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2481_st *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = ltc2481_read(st, val);
		mutex_unlock(&st->lock);
		if (ret)
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

/* The AD1244 Rev.2 uses one LTC2481 per logical pseudo-differential input. */
static const struct iio_chan_spec ltc2481_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info ltc2481_info = {
	.read_raw = ltc2481_read_raw,
};

static int ltc2481_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct iio_map *plat_data = dev_get_platdata(&client->dev);
	struct iio_dev *indio_dev;
	struct ltc2481_st *st;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->client = client;
	mutex_init(&st->lock);
	i2c_set_clientdata(client, indio_dev);

	indio_dev->name = id->name;
	indio_dev->info = &ltc2481_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2481_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltc2481_channels);

	st->ref = devm_regulator_get(&client->dev, "vref");
	if (IS_ERR(st->ref))
		return dev_err_probe(&client->dev, PTR_ERR(st->ref),
				     "failed to get vref regulator\n");

	ret = regulator_enable(st->ref);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to enable vref regulator\n");

	ret = devm_add_action_or_reset(&client->dev,
				       ltc2481_regulator_disable, st->ref);
	if (ret)
		return ret;

	if (plat_data) {
		ret = devm_iio_map_array_register(&client->dev, indio_dev,
						  plat_data);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "failed to register IIO map\n");
	}

	ret = ltc2481_start_conversion(st);
	if (ret)
		return ret;

	st->time_prev = ktime_get();

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to register IIO device\n");

	return 0;
}

static const struct i2c_device_id ltc2481_id[] = {
	{ "ltc2481", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc2481_id);

static const struct of_device_id ltc2481_of_match[] = {
	{ .compatible = "lltc,ltc2481" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2481_of_match);

static struct i2c_driver ltc2481_driver = {
	.driver = {
		.name = "ltc2481",
		.of_match_table = ltc2481_of_match,
	},
	.probe = ltc2481_probe,
	.id_table = ltc2481_id,
};
module_i2c_driver(ltc2481_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Linear Technology LTC2481 ADC driver");
MODULE_LICENSE("GPL v2");
