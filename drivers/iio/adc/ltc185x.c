/*
 * LTC185x SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 * Copyright 2014 Century Systems.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define LTC185X_MAX_CHAN	4

struct ltc185x_state {
	struct spi_device		*spi;
	struct regulator		*reg;

	struct {
		u8			uni;
		u8			gain;
	} chan_setting[4];

	struct spi_transfer		ring_xfer[6];
	struct spi_transfer		scan_single_xfer[2];
	struct spi_message		ring_msg;
	struct spi_message		scan_single_msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u16				rx_buf[8] ____cacheline_aligned;
	u16				tx_buf;
};

static int ltc185x_update_scan_mode(struct iio_dev *indio_dev,
				    const unsigned long *active_scan_mask)
{
	struct ltc185x_state *st = iio_priv(indio_dev);
	int i, nums = 0;
	unsigned short command;
	int scan_count;

	/* Now compute overall size */
	scan_count = bitmap_weight(active_scan_mask, indio_dev->masklength);

	spi_message_init(&st->ring_msg);

	for (i = 0; i < LTC185X_MAX_CHAN; i++) {
		if (test_bit(i, active_scan_mask)) {
			/* build spi ring message */
			command = ((i << 4) |
				   (st->chan_setting[i].uni << 3) |
				   (st->chan_setting[i].gain << 2)) << 8;
			st->tx_buf = command;

			st->ring_xfer[i].tx_buf = &st->tx_buf;
			st->ring_xfer[i].len = 2;
			st->ring_xfer[i].bits_per_word = 16;
			st->ring_xfer[i].cs_change = 1;
			st->ring_xfer[i].delay_usecs2 = 8;

			if (nums > 0) {
				st->ring_xfer[i].rx_buf = &st->rx_buf[nums - 1];
			}

			spi_message_add_tail(&st->ring_xfer[i], &st->ring_msg);

			nums++;
		}
	}

	if (nums > 0) {
		st->ring_xfer[nums].rx_buf = &st->rx_buf[nums - 1];
		st->ring_xfer[nums].len = 2;
	}

	/* make sure last transfer cs_change is not set */
	st->ring_xfer[nums + 1].cs_change = 0;

	return 0;
}

static irqreturn_t ltc185x_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ltc185x_state *st = iio_priv(indio_dev);
	int b_sent;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, st->rx_buf,
					   iio_get_time_ns());

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ltc185x_scan_direct(struct ltc185x_state *st, unsigned ch, int *val)
{
	int ret;
	unsigned short command;
	u8 uni, gain;

	ch = ch & 0x03;
	uni = st->chan_setting[ch].uni;
	gain = st->chan_setting[ch].gain;

	command = ((ch << 4) | (uni << 3) | (gain << 2)) << 8;
	st->tx_buf = command;

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	if (uni == 0)
		*val = (int) ((s16) st->rx_buf[0]);
	else
		*val = (int) ((u16) st->rx_buf[0]);

	return 0;
}

static int ltc185x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ltc185x_state *st = iio_priv(indio_dev);
	u8 uni, gain;
	int ret, raw_val = 0;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = ltc185x_scan_direct(st, chan->address, &raw_val);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;

		*val = raw_val;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		uni = st->chan_setting[chan->address].uni;
		gain = st->chan_setting[chan->address].gain;

		if (gain == 0)
			*val = 5;
		else
			*val = 10;

		if (uni == 0)
			*val2 = 15;
		else
			*val2 = 16;

		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static const char * const ltc185x_ranges[] = {
	"-5Vto+5V",	/* UNI: 0, GAIN: 0 */
	"-10Vto+10V",	/* UNI: 0, GAIN: 1 */
	"0Vto+5V",	/* UNI: 1, GAIN: 0 */
	"0Vto+10V",	/* UNI: 1, GAIN: 1 */
};

static int ltc185x_get_range(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan)
{
	struct ltc185x_state *st = iio_priv(indio_dev);
	u8 uni, gain;

	uni = st->chan_setting[chan->channel].uni;
	gain = st->chan_setting[chan->channel].gain;

	return (uni << 1 | gain);
}

static int ltc185x_set_range(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ltc185x_state *st = iio_priv(indio_dev);
	u8 uni, gain;

	mutex_lock(&indio_dev->mlock);
	uni = (mode & 0x02) >> 1;
	gain = mode & 0x01;

	st->chan_setting[chan->channel].uni = uni;
	st->chan_setting[chan->channel].gain = gain;

	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static const struct iio_enum ltc185x_range_enum = {
	.items = ltc185x_ranges,
	.num_items = ARRAY_SIZE(ltc185x_ranges),
	.get = ltc185x_get_range,
	.set = ltc185x_set_range,
};

static const struct iio_chan_spec_ext_info ltc185x_ext_info[] = {
	IIO_ENUM("range", IIO_SEPARATE, &ltc185x_range_enum),
	IIO_ENUM_AVAILABLE("ranges", &ltc185x_range_enum),
	{},
};

#define LTC185X_CHAN(index) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = index, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), \
	.address = index, \
	.scan_index = index, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.ext_info = ltc185x_ext_info, \
}

static const struct iio_chan_spec ltc185x_channels[] = {
	LTC185X_CHAN(0),
	LTC185X_CHAN(1),
	LTC185X_CHAN(2),
	LTC185X_CHAN(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_info ltc185x_info = {
	.read_raw = ltc185x_read_raw,
	.update_scan_mode = ltc185x_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static int ltc185x_probe(struct spi_device *spi)
{
	struct ltc185x_state *st;
	struct iio_dev *indio_dev;
	int i, ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get(&spi->dev, "vcc");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret)
		return ret;

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	/* set default range: 0V to 5V */
	for (i = 0; i < 4; i++) {
		st->chan_setting[i].uni = 1;
		st->chan_setting[i].gain = 0;
	}

	/* Establish that the iio_dev is a child of the spi device */
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc185x_channels;
	indio_dev->num_channels = 4;
	indio_dev->info = &ltc185x_info;
	/* Setup default message */

	st->scan_single_xfer[0].tx_buf = &st->tx_buf;
	st->scan_single_xfer[0].len = 2;
	st->scan_single_xfer[0].bits_per_word = 16;
	st->scan_single_xfer[0].delay_usecs2 = 8;
	st->scan_single_xfer[0].cs_change = 1;

	st->scan_single_xfer[1].tx_buf = &st->tx_buf;
	st->scan_single_xfer[1].rx_buf = &st->rx_buf[0];
	st->scan_single_xfer[1].len = 2;
	st->scan_single_xfer[1].bits_per_word = 16;

	spi_message_init(&st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[0], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[1], &st->scan_single_msg);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 &ltc185x_trigger_handler, NULL);
	if (ret)
		goto error_disable_reg;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_ring_unregister;
	return 0;

error_ring_unregister:
	iio_triggered_buffer_cleanup(indio_dev);
error_disable_reg:
	regulator_disable(st->reg);

	return ret;
}

static int ltc185x_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ltc185x_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	regulator_disable(st->reg);

	return 0;
}

#ifdef CONFIG_PM
static int ltc185x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ltc185x_state *st = iio_priv(indio_dev);

	regulator_disable(st->reg);

	return 0;
}

static int ltc185x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ltc185x_state *st = iio_priv(indio_dev);
	int res;

	res = regulator_enable(st->reg);

	return res;
}

static const struct dev_pm_ops ltc185x_pm_ops = {
	.suspend = ltc185x_suspend,
	.resume = ltc185x_resume,
};
#endif

static const struct spi_device_id ltc185x_id[] = {
	{"ltc1857", 0},
	{"ltc1858", 0},
	{"ltc1859", 0},
	{"ltc185x", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ltc185x_id);

static struct spi_driver ltc185x_driver = {
	.driver = {
		.name	= "ltc185x",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &ltc185x_pm_ops,
#endif
	},
	.probe		= ltc185x_probe,
	.remove	= ltc185x_remove,
	.id_table	= ltc185x_id,
};
module_spi_driver(ltc185x_driver);

MODULE_AUTHOR("Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_DESCRIPTION("Linear Technology LTC185X and similar 4-channel ADCs");
MODULE_LICENSE("GPL v2");
