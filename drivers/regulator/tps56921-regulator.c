/*
 * tps56921-regulator.c
 *
 * Copyright 2013 Samsung Electronics
 *
 * Author: Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#define TPS56921_BASE_VOLTAGE	720000
#define TPS56921_STEP_VOLTAGE	10000
#define TPS56921_N_VOLTAGES		77

#define TPS56921_DELAY			200		/* 200 us */

#define TPS56921_INVALID_VALUE	0xFFFFFFFF

struct tps56921_data {
	struct i2c_client *client;
	struct regulator_dev *rdev;
	unsigned int cur_val;
};

static int tps56921_write_reg(struct i2c_client *client, unsigned int val)
{
	u8 buf;
	u8 checksum = 0;
	int ret;
	int i;
	struct i2c_msg msg;

	buf = (u8)val & 0x7F;	/* register value */

	/* add checksum bit */
	for (i = 0; i < 7; i++) {
		if (val & 0x1)
			checksum++;
		val = val >> 1;
	}
	if (checksum % 2)
		buf |= 1 << 7;

	msg.addr = client->addr;
	msg.flags = 0;			/* write */
	msg.len = 1;			/* data size */
	msg.buf = &buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c send failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;			
}

static int tps56921_get_voltage_sel(struct regulator_dev *rdev)
{
	struct tps56921_data *tps56921 = rdev_get_drvdata(rdev);
	u8 val;

	if (tps56921 == NULL)
		return -1;

	if (tps56921->cur_val == TPS56921_INVALID_VALUE)
		return -1;

	val = tps56921->cur_val;

	return val;
}

static int tps56921_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct tps56921_data *tps56921 = rdev_get_drvdata(rdev);
	int ret;

	if (tps56921 == NULL)
		return -1;

	ret = tps56921_write_reg(tps56921->client, selector);
	if (ret < 0)
		return ret;

	tps56921->cur_val = selector;

	udelay(TPS56921_DELAY);

	return 0;
}

static int tps56921_set_default(struct i2c_client *client, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tps56921_data *tps56921 = dev_get_drvdata(dev);
	u32 def_volt, val, temp;
	int retry = 3;
	int ret;

	if (tps56921 == NULL)
		return -1;

	/* get default voltage */
	of_property_read_u32(np, "default_volt", &def_volt);

	/* find value from voltage */
	for (val = 0; val < TPS56921_N_VOLTAGES; val++) {
		temp = TPS56921_BASE_VOLTAGE + (val * TPS56921_STEP_VOLTAGE);
		if (def_volt == temp)
			break;
	}
	if (val == TPS56921_N_VOLTAGES) {
		dev_err(dev, "%s-default voltage finding failed!\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "default volt : %uuV(%u)\n", def_volt, val);
	while (retry) {
		ret = tps56921_write_reg(client, val);
		if (!ret)
			break;

		retry--;
	}
	
	if (retry <= 0) {
		dev_err(dev, "%s-set default value failed!\n", __func__);
		return -EIO;
	}

	tps56921->cur_val = val;
	
	return 0;
}

static struct regulator_ops tps56921_regulator_ops = {
	.get_voltage_sel = tps56921_get_voltage_sel,
	.set_voltage_sel = tps56921_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_desc tps56921_regulator_desc  = {
	.name		= "tps56921",
	.ops		= &tps56921_regulator_ops,
	.type		= REGULATOR_VOLTAGE,
	.id			= 0,
	.owner		= THIS_MODULE,
	.min_uV		= TPS56921_BASE_VOLTAGE,
	.uV_step	= TPS56921_STEP_VOLTAGE,
	.n_voltages	= TPS56921_N_VOLTAGES,
};

static int tps56921_probe(struct i2c_client *client,
							const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct tps56921_data *tps56921;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	tps56921 = devm_kzalloc(dev, sizeof(struct tps56921_data), GFP_KERNEL);
	if (!tps56921)
		return -ENOMEM;

	tps56921->cur_val = TPS56921_INVALID_VALUE;
	tps56921->client = client;
	i2c_set_clientdata(client, tps56921);

	reg_init_data = of_get_regulator_init_data(dev, dev->of_node);
	if (!reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return -EINVAL;
	}

	config.dev = dev;
	config.init_data = reg_init_data;
	config.driver_data = tps56921;
	config.of_node = dev->of_node;

	/* set HW defalt */
	ret = tps56921_set_default(client, dev);
	if (ret)
		return ret;

	rdev = regulator_register(&tps56921_regulator_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "regulator register failed for %s\n", id->name);
		return PTR_ERR(rdev);
	}

	tps56921->rdev = rdev;

	return 0;
}

static int tps56921_remove(struct i2c_client *client)
{
	struct tps56921_data *tps56921 = i2c_get_clientdata(client);

	if (tps56921 == NULL)
		return -1;

	regulator_unregister(tps56921->rdev);
	
	return 0;
}

static const struct of_device_id tps56921_dt_match[] = {
	{ .compatible = "ti,tps56921", },
	{},
};
MODULE_DEVICE_TABLE(of, tps56921_dt_match);;

static const struct i2c_device_id tps56921_id[] = {
	{ .name = "tps56921" },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps56921_id);

static struct i2c_driver tps56921_i2c_driver = {
	.probe		= tps56921_probe,
	.remove		= tps56921_remove,
	.driver = {
		.name	= "tps56921",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps56921_dt_match),
	},
	.id_table	= tps56921_id,
};

static int __init tps56921_init(void)
{
	return i2c_add_driver(&tps56921_i2c_driver);
}
subsys_initcall(tps56921_init);

static void __exit tps56921_exit(void)
{
	return i2c_del_driver(&tps56921_i2c_driver);
}
module_exit(tps56921_exit);

MODULE_DESCRIPTION("TPS56921 voltage regulator driver");
MODULE_LICENSE("GPL v2");
