/*
 * ltc185x.c
 *
 * The ltc185x is an AD converter family from Linear Technology.
 *
 * Copyright (c) 2010 Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <asm/delay.h>

#define DRVNAME		"ltc185x"

static const char * const range2str[] = {
        [0] = "[UNI:0 GAIN:0]  -5V to  +5V",
        [1] = "[UNI:0 GAIN:1] -10V to +10V",
        [2] = "[UNI:1 GAIN:0]   0V to  +5V",
        [3] = "[UNI:1 GAIN:1]   0V to +10V"
};

static const char * const range2min[] = {
        [0] = "-5000",
        [1] = "-10000",
        [2] = "0",
        [3] = "0"
};

static const char * const range2max[] = {
        [0] = "5000",
        [1] = "10000",
        [2] = "5000",
        [3] = "10000"
};

static const int range2ref[] = {
        [0] = 5000,
        [1] = 10000,
        [2] = 5000,
        [3] = 10000
};

struct ltc185x_ch_info {
        int uni;
        int gain;
};

struct ltc185x {
	struct device *hwmon_dev;
	struct mutex lock;

        struct ltc185x_ch_info ch_info[4];
};

/* sysfs hook function */
static ssize_t ltc185x_read_val(struct device *dev,
                                struct device_attribute *devattr, int *val)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
	u16 tx_buf[2], rx_buf[2];
	int status, idx;

        idx = attr->index;
        if (idx < 0 || idx > 3)
                return -EFAULT;
        ch_info = &ltc->ch_info[idx];

	if (mutex_lock_interruptible(&ltc->lock))
		return -ERESTARTSYS;

        /* AD converter setup */
        tx_buf[0] = (idx << 4 | (ch_info->uni) << 3 | (ch_info->gain) << 2) << 8;
        tx_buf[1] = tx_buf[0];
#if 0
        status = spi_write(spi, (u8 *) tx_buf, 2);
	if (status < 0) {
		dev_warn(dev, "spi_write failed with status %d\n",
                         status);
		goto out;
	}

        /* read converted value */
        status = spi_read(spi, (u8 *) rx_buf, 2);
	if (status < 0) {
		dev_warn(dev, "spi_read failed with status %d\n",
                         status);
		goto out;
	}
#else
        status = spi_write_then_read(spi, (u8 *) tx_buf, 2, (u8 *) rx_buf, 2);

	if (unlikely(status < 0)) {
		dev_warn(dev, "spi_write_then_read failed with status %d\n",
                         status);
		goto out;
	}
#endif
	*val = rx_buf[0];
out:
	mutex_unlock(&ltc->lock);
	return status;
}

static ssize_t ltc185x_read_raw(struct device *dev,
                                struct device_attribute *devattr, char *buf)
{
        int value;
        ssize_t status;

        status = ltc185x_read_val(dev, devattr, &value);
        if (status == 0)
                status = sprintf(buf, "%d\n0x%04x\n", value, value);

        return status;
}

/* sysfs hook function */
static ssize_t ltc185x_read(struct device *dev,
                            struct device_attribute *devattr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
	ssize_t status, idx;
	int value, reference;

        idx = attr->index;
        if (idx < 0 || idx > 3)
                return -EFAULT;

        ch_info = &ltc->ch_info[idx];

	status = ltc185x_read_val(dev, devattr, &value);

        if (status == 0) {
                reference = range2ref[ch_info->uni << 1 | ch_info->gain];

                if (ch_info->uni == 0) {
                        value = (((s32) ((s16) value)) * reference);
                        value += 0x3fff + (((u32) value & (1 << 15)) ? 1 : 0);
                        value >>= 15;
                } else {
                        value = value * reference;
                        value += 0x7fff + (((u32) value & (1 << 16)) ? 1 : 0);
                        value >>= 16;
                }

                status = sprintf(buf, "%d\n", value);
        }

	return status;
}

static ssize_t ltc185x_show_min(struct device *dev,
                                struct device_attribute *devattr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
        int range;
        ssize_t len;

	if (mutex_lock_interruptible(&ltc->lock))
		return -ERESTARTSYS;

        ch_info = &ltc->ch_info[attr->index];
        range = ch_info->uni << 1 | ch_info->gain;

	len = sprintf(buf, "%s\n", range2min[range]);

	mutex_unlock(&ltc->lock);

        return len;
}

static ssize_t ltc185x_show_max(struct device *dev,
                                struct device_attribute *devattr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
        int range;
        ssize_t len;

	if (mutex_lock_interruptible(&ltc->lock))
		return -ERESTARTSYS;

        ch_info = &ltc->ch_info[attr->index];
        range = ch_info->uni << 1 | ch_info->gain;

	len = sprintf(buf, "%s\n", range2max[range]);

	mutex_unlock(&ltc->lock);

        return len;
}

static ssize_t ltc185x_show_name(struct device *dev, struct device_attribute
                                 *devattr, char *buf)
{
	return sprintf(buf, "ltc185x\n");
}

static ssize_t ltc185x_show_range(struct device *dev,
                                  struct device_attribute *devattr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
        int range;
        ssize_t len;

	if (mutex_lock_interruptible(&ltc->lock))
		return -ERESTARTSYS;

        ch_info = &ltc->ch_info[attr->index];
        range = ch_info->uni << 1 | ch_info->gain;

	len = sprintf(buf, "%s\n", range2str[range]);

	mutex_unlock(&ltc->lock);

        return len;
}

static ssize_t ltc185x_show_ranges(struct device *dev,
                                   struct device_attribute *devattr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        int i;
        ssize_t tmp, len = 0;
        char *ptr = buf;

	if (mutex_lock_interruptible(&ltc->lock))
		return -ERESTARTSYS;

        for (i = 0; i < ARRAY_SIZE(range2str); i++) {
                tmp = sprintf(ptr, "%d: %s\n", i, range2str[i]);
                len += tmp;
                ptr += tmp;
        }

	mutex_unlock(&ltc->lock);

        return len;
}

static ssize_t ltc185x_set_range(struct device *dev,
                                 struct device_attribute *devattr,
                                 const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
        struct ltc185x_ch_info *ch_info;
        int range;

        ch_info = &ltc->ch_info[attr->index];
        range = simple_strtoul(buf, NULL, 10);

	mutex_lock(&ltc->lock);

        if (range < 4) {
                ch_info->uni = (range & 0x02) >> 1;
                ch_info->gain = range & 0x01;
        }

	mutex_unlock(&ltc->lock);

        return count;
}


#define LTC185X_INPUT_ATTR(_id)                                         \
        SENSOR_ATTR(in##_id##_input, S_IRUGO, ltc185x_read, NULL, _id)
#define LTC185X_INPUT_RAW_ATTR(_id)                                         \
        SENSOR_ATTR(in##_id##_input_raw, S_IRUGO, ltc185x_read_raw, NULL, _id)
#define LTC185X_RANGE_ATTR(_id)                                         \
        SENSOR_ATTR(in##_id##_range, S_IRUGO|S_IWUSR, ltc185x_show_range, \
                    ltc185x_set_range, _id)
#define LTC185X_MIN_ATTR(_id)                                           \
        SENSOR_ATTR(in##_id##_min, S_IRUGO, ltc185x_show_min, NULL, _id)
#define LTC185X_MAX_ATTR(_id)                                           \
        SENSOR_ATTR(in##_id##_max, S_IRUGO, ltc185x_show_max, NULL, _id)

static struct sensor_device_attribute ltc_input[] = {
	SENSOR_ATTR(name, S_IRUGO, ltc185x_show_name, NULL, 0),
	SENSOR_ATTR(ranges, S_IRUGO, ltc185x_show_ranges, NULL, 0),
        LTC185X_INPUT_ATTR(0),
        LTC185X_INPUT_ATTR(1),
        LTC185X_INPUT_ATTR(2),
        LTC185X_INPUT_ATTR(3),
        LTC185X_INPUT_RAW_ATTR(0),
        LTC185X_INPUT_RAW_ATTR(1),
        LTC185X_INPUT_RAW_ATTR(2),
        LTC185X_INPUT_RAW_ATTR(3),
        LTC185X_MIN_ATTR(0),
        LTC185X_MIN_ATTR(1),
        LTC185X_MIN_ATTR(2),
        LTC185X_MIN_ATTR(3),
        LTC185X_MAX_ATTR(0),
        LTC185X_MAX_ATTR(1),
        LTC185X_MAX_ATTR(2),
        LTC185X_MAX_ATTR(3),
        LTC185X_RANGE_ATTR(0),
        LTC185X_RANGE_ATTR(1),
        LTC185X_RANGE_ATTR(2),
        LTC185X_RANGE_ATTR(3),
};

/*----------------------------------------------------------------------*/

static inline void ltc185x_dummy_read(struct spi_device *spi)
{
	u16 tx_buf[2], rx_buf[2];

	tx_buf[0] = tx_buf[1] = 0;
        spi_write_then_read(spi, (u8 *) tx_buf, 2, (u8 *) rx_buf, 2);
}

static int __devinit ltc185x_probe(struct spi_device *spi)
{
	struct ltc185x *ltc;
	int status;
	int i;

	ltc = kzalloc(sizeof *ltc, GFP_KERNEL);
	if (!ltc)
		return -ENOMEM;

	mutex_init(&ltc->lock);

	mutex_lock(&ltc->lock);

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	dev_set_drvdata(&spi->dev, ltc);

	for (i = 0; i < ARRAY_SIZE(ltc_input); i++) {
		status = device_create_file(&spi->dev, &ltc_input[i].dev_attr);
		if (status) {
			dev_err(&spi->dev, "device_create_file failed.\n");
			goto out_err;
		}
	}

	ltc->hwmon_dev = hwmon_device_register(&spi->dev);
	if (IS_ERR(ltc->hwmon_dev)) {
		dev_err(&spi->dev, "hwmon_device_register failed.\n");
		status = PTR_ERR(ltc->hwmon_dev);
		goto out_err;
	}

	printk("LTC185x ADC (cs: %d) probed.\n", (int) spi->chip_select);
	ltc185x_dummy_read(spi);

	mutex_unlock(&ltc->lock);
	return 0;

out_err:
	for (i--; i >= 0; i--)
		device_remove_file(&spi->dev, &ltc_input[i].dev_attr);

	dev_set_drvdata(&spi->dev, NULL);
	mutex_unlock(&ltc->lock);
	kfree(ltc);
	return status;
}

static int __devexit ltc185x_remove(struct spi_device *spi)
{
	struct ltc185x *ltc = dev_get_drvdata(&spi->dev);
	int i;

	mutex_lock(&ltc->lock);
	hwmon_device_unregister(ltc->hwmon_dev);

	for (i = 0; i < ARRAY_SIZE(ltc_input); i++)
		device_remove_file(&spi->dev, &ltc_input[i].dev_attr);

	dev_set_drvdata(&spi->dev, NULL);
	mutex_unlock(&ltc->lock);
	kfree(ltc);

	return 0;
}

static struct spi_driver ltc185x_driver = {
	.driver = {
		.name	= "ltc185x",
		.owner	= THIS_MODULE,
	},
	.probe	= ltc185x_probe,
	.remove	= __devexit_p(ltc185x_remove),
};

static int __init init_ltc185x(void)
{
	int status;
	status = spi_register_driver(&ltc185x_driver);

	return status;
}

static void __exit exit_ltc185x(void)
{
	spi_unregister_driver(&ltc185x_driver);
}

module_init(init_ltc185x);
module_exit(exit_ltc185x);

MODULE_AUTHOR("Takeyoshi Kikuchi");
MODULE_DESCRIPTION("Linear Technology ltc185x Linux driver");
MODULE_LICENSE("GPL");
