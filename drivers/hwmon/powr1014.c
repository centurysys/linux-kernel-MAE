/*
 * powr1014.c - Driver for the Lattice POWR1014 programmable power supply
 * and monitor. Users can read all ADC inputs along with their labels
 * using the sysfs nodes.
 *
 * Copyright (C) 2017 Century Systems
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define ADC_STEP_MV			2
#define ADC_MAX_LOW_MEASUREMENT_MV	2000

enum powr1014_regs {
	VMON_STATUS0,
	VMON_STATUS1,
	VMON_STATUS2,
	OUTPUT_STATUS0,
	OUTPUT_STATUS1,
	RESERVED0,
	INPUT_STATUS,
	ADC_VALUE_LOW,
	ADC_VALUE_HIGH,
	ADC_MUX,
	UES_BYTE0,
	UES_BYTE1,
	UES_BYTE2,
	UES_BYTE3,
	GP_OUTPUT1,
	GP_OUTPUT2,
	RESERVED1,
	INPUT_VALUE,
	RESET,
	MAX_POWR1014_REGS
};

enum powr1014_adc_values {
	VMON1,
	VMON2,
	VMON3,
	VMON4,
	VMON5,
	VMON6,
	VMON7,
	VMON8,
	VMON9,
	VMON10,
	VCCA,
	VCCINP,
	MAX_POWR1014_ADC_VALUES
};

struct powr1014_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool adc_valid[MAX_POWR1014_ADC_VALUES];
	 /* the next value is in jiffies */
	unsigned long adc_last_updated[MAX_POWR1014_ADC_VALUES];

	/* values */
	int adc_maxes[MAX_POWR1014_ADC_VALUES];
	int adc_values[MAX_POWR1014_ADC_VALUES];
};

static const char * const input_names[] = {
	[VMON1]    = "vmon1",
	[VMON2]    = "vmon2",
	[VMON3]    = "vmon3",
	[VMON4]    = "vmon4",
	[VMON5]    = "vmon5",
	[VMON6]    = "vmon6",
	[VMON7]    = "vmon7",
	[VMON8]    = "vmon8",
	[VMON9]    = "vmon9",
	[VMON10]   = "vmon10",
	[VCCA]     = "vcca",
	[VCCINP]   = "vccinp",
};

/* Reads the specified ADC channel */
static int powr1014_read_adc(struct device *dev, int ch_num)
{
	struct powr1014_data *data = dev_get_drvdata(dev);
	int reading;
	int result;
	int adc_range = 0;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->adc_last_updated[ch_num] + HZ) ||
			!data->adc_valid[ch_num]) {
		/*
		 * figure out if we need to use the attenuator for
		 * high inputs or inputs that we don't yet have a measurement
		 * for. We dynamically set the attenuator depending on the
		 * max reading.
		 */
		if (data->adc_maxes[ch_num] > ADC_MAX_LOW_MEASUREMENT_MV ||
				data->adc_maxes[ch_num] == 0)
			adc_range = 1 << 4;

		/* set the attenuator and mux */
		result = i2c_smbus_write_byte_data(data->client, ADC_MUX,
				adc_range | ch_num);
		if (result)
			goto exit;

		/*
		 * wait at least Tconvert time (200 us) for the
		 * conversion to complete
		 */
		udelay(200);

		/* get the ADC reading */
		result = i2c_smbus_read_byte_data(data->client, ADC_VALUE_LOW);
		if (result < 0)
			goto exit;

		reading = result >> 4;

		/* get the upper half of the reading */
		result = i2c_smbus_read_byte_data(data->client, ADC_VALUE_HIGH);
		if (result < 0)
			goto exit;

		reading |= result << 4;

		/* now convert the reading to a voltage */
		reading *= ADC_STEP_MV;
		data->adc_values[ch_num] = reading;
		data->adc_valid[ch_num] = true;
		data->adc_last_updated[ch_num] = jiffies;
		result = reading;

		if (reading > data->adc_maxes[ch_num])
			data->adc_maxes[ch_num] = reading;
	} else {
		result = data->adc_values[ch_num];
	}

exit:
	mutex_unlock(&data->update_lock);

	return result;
}

/* Shows the voltage associated with the specified ADC channel */
static ssize_t powr1014_show_voltage(struct device *dev,
	struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	int adc_val = powr1014_read_adc(dev, attr->index);

	if (adc_val < 0)
		return adc_val;

	return sprintf(buf, "%d\n", adc_val);
}

/* Shows the maximum setting associated with the specified ADC channel */
static ssize_t powr1014_show_max(struct device *dev,
	struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct powr1014_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->adc_maxes[attr->index]);
}

/* Shows the label associated with the specified ADC channel */
static ssize_t powr1014_show_label(struct device *dev,
	struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%s\n", input_names[attr->index]);
}

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON1);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON2);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON3);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON4);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON5);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON6);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON7);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON8);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON9);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, powr1014_show_voltage, NULL,
	VMON10);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, powr1014_show_voltage, NULL,
	VCCA);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, powr1014_show_voltage, NULL,
	VCCINP);

static SENSOR_DEVICE_ATTR(in0_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON1);
static SENSOR_DEVICE_ATTR(in1_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON2);
static SENSOR_DEVICE_ATTR(in2_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON3);
static SENSOR_DEVICE_ATTR(in3_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON4);
static SENSOR_DEVICE_ATTR(in4_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON5);
static SENSOR_DEVICE_ATTR(in5_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON6);
static SENSOR_DEVICE_ATTR(in6_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON7);
static SENSOR_DEVICE_ATTR(in7_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON8);
static SENSOR_DEVICE_ATTR(in8_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON9);
static SENSOR_DEVICE_ATTR(in9_highest, S_IRUGO, powr1014_show_max, NULL,
	VMON10);
static SENSOR_DEVICE_ATTR(in10_highest, S_IRUGO, powr1014_show_max, NULL,
	VCCA);
static SENSOR_DEVICE_ATTR(in11_highest, S_IRUGO, powr1014_show_max, NULL,
	VCCINP);

static SENSOR_DEVICE_ATTR(in0_label, S_IRUGO, powr1014_show_label, NULL,
	VMON1);
static SENSOR_DEVICE_ATTR(in1_label, S_IRUGO, powr1014_show_label, NULL,
	VMON2);
static SENSOR_DEVICE_ATTR(in2_label, S_IRUGO, powr1014_show_label, NULL,
	VMON3);
static SENSOR_DEVICE_ATTR(in3_label, S_IRUGO, powr1014_show_label, NULL,
	VMON4);
static SENSOR_DEVICE_ATTR(in4_label, S_IRUGO, powr1014_show_label, NULL,
	VMON5);
static SENSOR_DEVICE_ATTR(in5_label, S_IRUGO, powr1014_show_label, NULL,
	VMON6);
static SENSOR_DEVICE_ATTR(in6_label, S_IRUGO, powr1014_show_label, NULL,
	VMON7);
static SENSOR_DEVICE_ATTR(in7_label, S_IRUGO, powr1014_show_label, NULL,
	VMON8);
static SENSOR_DEVICE_ATTR(in8_label, S_IRUGO, powr1014_show_label, NULL,
	VMON9);
static SENSOR_DEVICE_ATTR(in9_label, S_IRUGO, powr1014_show_label, NULL,
	VMON10);
static SENSOR_DEVICE_ATTR(in10_label, S_IRUGO, powr1014_show_label, NULL,
	VCCA);
static SENSOR_DEVICE_ATTR(in11_label, S_IRUGO, powr1014_show_label, NULL,
	VCCINP);

static struct attribute *powr1014_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,

	&sensor_dev_attr_in0_highest.dev_attr.attr,
	&sensor_dev_attr_in1_highest.dev_attr.attr,
	&sensor_dev_attr_in2_highest.dev_attr.attr,
	&sensor_dev_attr_in3_highest.dev_attr.attr,
	&sensor_dev_attr_in4_highest.dev_attr.attr,
	&sensor_dev_attr_in5_highest.dev_attr.attr,
	&sensor_dev_attr_in6_highest.dev_attr.attr,
	&sensor_dev_attr_in7_highest.dev_attr.attr,
	&sensor_dev_attr_in8_highest.dev_attr.attr,
	&sensor_dev_attr_in9_highest.dev_attr.attr,
	&sensor_dev_attr_in10_highest.dev_attr.attr,
	&sensor_dev_attr_in11_highest.dev_attr.attr,

	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_in5_label.dev_attr.attr,
	&sensor_dev_attr_in6_label.dev_attr.attr,
	&sensor_dev_attr_in7_label.dev_attr.attr,
	&sensor_dev_attr_in8_label.dev_attr.attr,
	&sensor_dev_attr_in9_label.dev_attr.attr,
	&sensor_dev_attr_in10_label.dev_attr.attr,
	&sensor_dev_attr_in11_label.dev_attr.attr,

	NULL
};

ATTRIBUTE_GROUPS(powr1014);

struct powr1014_data *g_data = NULL;

int powr1014_get_input_value(u8 *val)
{
	int ret;

	if (!g_data) {
		printk("powr1014 driver: Hardware not present\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(g_data->client, INPUT_VALUE);
	if (ret < 0) {
		dev_err(&g_data->client->dev, "INPUT_VALUE: read error\n");
		return ret;
	}

	*val = ret;

	return 0;
}
EXPORT_SYMBOL(powr1014_get_input_value);

int powr1014_set_input_value(u8 val)
{
	int ret;

	if (!g_data) {
		printk("powr1014 driver: Hardware not present\n");
		return -ENODEV;
	}

	ret = i2c_smbus_write_byte_data(g_data->client, INPUT_VALUE, val);
	if (ret < 0) {
		dev_err(&g_data->client->dev, "INPUT_VALUE: write error\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(powr1014_set_input_value);

static int powr1014_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *hwmon_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	g_data = devm_kzalloc(&client->dev, sizeof(*g_data), GFP_KERNEL);
	if (!g_data)
		return -ENOMEM;

	mutex_init(&g_data->update_lock);
	g_data->client = client;

	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
			client->name, g_data, powr1014_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id powr1014_ids[] = {
	{ "powr1014", 0, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, powr1014_ids);

static struct i2c_driver powr1014_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "powr1014",
	},
	.probe		= powr1014_probe,
	.id_table	= powr1014_ids,
};

module_i2c_driver(powr1014_driver);

MODULE_AUTHOR("KASHIWAKURA Takashi");
MODULE_DESCRIPTION("POWR1014 driver");
MODULE_LICENSE("GPL");
