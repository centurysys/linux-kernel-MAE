/*
 * An I2C driver for the DAISHINKU DSK324SR RTC
 *
 * Copyright (c) 2015 Century Systems
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/log2.h>

#define DRV_VERSION "1.0"

#define DSK324SR_REG_SC	0x00 /* Second in BCD */
#define DSK324SR_REG_MN	0x01 /* Minute in BCD */
#define DSK324SR_REG_HR	0x02 /* Hour in BCD */
#define DSK324SR_REG_DW	0x03 /* Day of Week */
#define DSK324SR_REG_DM	0x04 /* Day of Month in BCD */
#define DSK324SR_REG_MO	0x05 /* Month in BCD */
#define DSK324SR_REG_YR	0x06 /* Year in BCD */
#define DSK324SR_REG_AMN	0x07 /* Alarm Min in BCD */
#define DSK324SR_REG_AHR	0x08 /* Alarm Hour in BCD */
#define DSK324SR_REG_ADM	0x09 /* Alarm Week in BCD */
#define DSK324SR_REG_ADW	0x09 /* Alarm Day in BCD */
#define DSK324SR_REG_TMR	0x0A /* Timer Counter */
#define DSK324SR_REG_SEL	0x0B /* Select Register */
#define DSK324SR_REG_FLAG	0x0C /* Flag Register */
#define DSK324SR_REG_CTRL	0x0D /* Control Register */

/* Select Register bit definitions */
#define DSK324SR_SEL_TCS1	0x80 /* Temperature Compensation 1 */
#define DSK324SR_SEL_TCS0	0x40 /* Temperature Compensation 0 */
#define DSK324SR_SEL_TCS_MASK	(DSK324SR_SEL_TCS1 | DSK324SR_SEL_TCS0)
#define DSK324SR_SEL_TCS_30S	(DSK324SR_SEL_TCS1 | DSK324SR_SEL_TCS0)
#define DSK324SR_SEL_TCS_10S	(DSK324SR_SEL_TCS1 | 0)
#define DSK324SR_SEL_TCS_2S	(                0 | DSK324SR_SEL_TCS0)
#define DSK324SR_SEL_TCS_0_5S	(                0 | 0)

/* Flag Register bit definitions */
#define DSK324SR_FLAG_VDHF	0x20 /* Voltage Detect High */
#define DSK324SR_FLAG_VDLF	0x10 /* Voltage Detect Low */
#define DSK324SR_FLAG_TF	0x04 /* Timer */
#define DSK324SR_FLAG_AF	0x02 /* Alarm */
#define DSK324SR_FLAG_UTF	0x01 /* Update */
#define DSK324SR_FLAG_VDF	(DSK324SR_FLAG_VDHF | DSK324SR_FLAG_VDLF)

/* Control Register bit definitions */
#define DSK324SR_CTRL_RESET	0x80 /* RESET bit */
#define DSK324SR_CTRL_TEST	0x40 /* TEST bit */
#define DSK324SR_CTRL_RAM	0x20 /* RAM bit */
#define DSK324SR_CTRL_FIE	0x10 /* Frequency Interrupt Enable */
#define DSK324SR_CTRL_TE	0x08 /* Timer Enable */
#define DSK324SR_CTRL_TIE	0x04 /* Timer Interrupt Enable */
#define DSK324SR_CTRL_AIE	0x02 /* Alarm Interrupt Enable */
#define DSK324SR_CTRL_UTIE	0x01 /* Update Time Interrupt Enable */

static struct i2c_driver dsk324sr_driver;

/*
 * In the routines that deal directly with the dsk324sr hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int dsk324sr_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned char date[7];
	int data, err;

	/* Now read time and date */
	err = i2c_smbus_read_i2c_block_data(client, DSK324SR_REG_SC,
		7, date);

	if (err < 0) {
		dev_err(&client->dev, "Unable to read date\n");
		return -EIO;
	}

	/* Check flag register */
	data = i2c_smbus_read_byte_data(client, DSK324SR_REG_FLAG);

	if (data < 0) {
		dev_err(&client->dev, "Unable to read device flags\n");
		return -EIO;
	}

	/* make sure VDHF bit cleared */
	if (data & DSK324SR_FLAG_VDHF)
		dev_info(&client->dev,
			"high voltage detected, date/time is not reliable.\n");

	/* make sure VDLF bit cleared */
	if (data & DSK324SR_FLAG_VDLF)
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");

	dev_dbg(&client->dev,
		"%s: raw data is sec=%02x, min=%02x, hr=%02x, "
		"wday=%02x, mday=%02x, mon=%02x, year=%02x\n",
		__func__,
		date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	tm->tm_sec = bcd2bin(date[DSK324SR_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(date[DSK324SR_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(date[DSK324SR_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_wday = bcd2bin(date[DSK324SR_REG_DW] & 0x07);
	tm->tm_mday = bcd2bin(date[DSK324SR_REG_DM] & 0x3F);
	tm->tm_mon = bcd2bin(date[DSK324SR_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(date[DSK324SR_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	err = rtc_valid_tm(tm);

	if (err < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return err;
}

static int dsk324sr_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	int data, err;
	unsigned char buf[7];

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[DSK324SR_REG_SC] = bin2bcd(tm->tm_sec);
	buf[DSK324SR_REG_MN] = bin2bcd(tm->tm_min);
	buf[DSK324SR_REG_HR] = bin2bcd(tm->tm_hour);

	buf[DSK324SR_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[DSK324SR_REG_MO] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[DSK324SR_REG_YR] = bin2bcd(tm->tm_year % 100);
	buf[DSK324SR_REG_DW] = tm->tm_wday & 0x07;

	/* set RESET bit */
	data = i2c_smbus_read_byte_data(client, DSK324SR_REG_CTRL);

	if (data < 0) {
		dev_err(&client->dev, "Unable to read control reg\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, DSK324SR_REG_CTRL,
		(data | DSK324SR_CTRL_RESET));

	if (err != 0) {
		dev_err(&client->dev, "Unable to write flag register\n");
		return -EIO;
	}

	/* write register's data */
	err = i2c_smbus_write_i2c_block_data(client, DSK324SR_REG_SC, 7, buf);

	if (err < 0) {
		dev_err(&client->dev, "Unable to write to date registers\n");
		return -EIO;
	}

	/* clear VDHF and VDLF */
	data = i2c_smbus_read_byte_data(client, DSK324SR_REG_FLAG);

	if (data < 0) {
		dev_err(&client->dev, "Unable to read flag register\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, DSK324SR_REG_FLAG,
					(data & ~(DSK324SR_FLAG_VDF)));

	if (err != 0) {
		dev_err(&client->dev, "Unable to write flag register\n");
		return -EIO;
	}

	return 0;
}

static int dsk324sr_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return dsk324sr_get_datetime(to_i2c_client(dev), tm);
}

static int dsk324sr_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return dsk324sr_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops dsk324sr_rtc_ops = {
	.read_time	= dsk324sr_rtc_read_time,
	.set_time	= dsk324sr_rtc_set_time,
};

static int dsk324sr_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct rtc_device *rtc;
        int err, data;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	rtc = devm_rtc_device_register(&client->dev, dsk324sr_driver.driver.name,
					&dsk324sr_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	data = i2c_smbus_read_byte_data(client, DSK324SR_REG_SEL);

	if (data >= 0 &&
	    (data & DSK324SR_SEL_TCS_MASK) != DSK324SR_SEL_TCS_30S) {
		data = data | DSK324SR_SEL_TCS_30S;
		err = i2c_smbus_write_byte_data(client, DSK324SR_REG_SEL,
						data);

		if (err == 0) {
			dev_info(&client->dev, "SEL Register updated to 30s.\n");
		} else {
			dev_info(&client->dev, "SEL Register update failed.\n");
		}
	}
	
	return 0;
}

static const struct i2c_device_id dsk324sr_id[] = {
	{"dsk324sr", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, dsk324sr_id);

#ifdef CONFIG_OF
static const struct of_device_id dsk324sr_of_match[] = {
	{
		.compatible = "dsk,dsk324sr",
	},
};
MODULE_DEVICE_TABLE(of, dsk324sr_of_match);
#endif

static struct i2c_driver dsk324sr_driver = {
	.driver		= {
		.name	= "rtc-dsk324sr",
		.of_match_table = of_match_ptr(dsk324sr_of_match),
	},
	.probe		= dsk324sr_probe,
	.id_table	= dsk324sr_id,
};

module_i2c_driver(dsk324sr_driver);

MODULE_AUTHOR("Century Systems ");
MODULE_DESCRIPTION("KDS DSK324SR RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
