
/*
 * samsung-vd-regulator.c
 *
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>

/* Samsung VD PMIC API */
int tztv_sys_6a_pmic_read(unsigned char *read_data); // 6A PMIC read
int tztv_sys_6a_pmic_write(unsigned char write_data); // 6A PMIC write
int tztv_sys_pmic_read(u8 *rdata)
{
	static int (*vd_pmic_read)(u8*);
	u8 buf = 0;

	if (!vd_pmic_read)
		vd_pmic_read = symbol_request(tztv_sys_6a_pmic_read);

	if (vd_pmic_read)
		vd_pmic_read(&buf);

	*rdata = buf;
	
	return 0;
}
int tztv_sys_pmic_write(u8 wdata)
{
	static int (*vd_pmic_write)(u8);

	if (!vd_pmic_write)
		vd_pmic_write = symbol_request(tztv_sys_6a_pmic_write);

	if (vd_pmic_write)
		vd_pmic_write((u8)wdata);

	return 0;
}

struct vd_regulator_data {
	struct regulator_dev *rdev;
	int (*pmic_read)(u8*);
	int (*pmic_write)(u8);
};

static int vd_regulator_if_setup(struct vd_regulator_data *data)
{
	data->pmic_read = tztv_sys_pmic_read;
	data->pmic_write = tztv_sys_pmic_write;
	
	return 0;
}

static int vd_regulator_get_voltage(struct regulator_dev *dev)
{
	struct vd_regulator_data *data = rdev_get_drvdata(dev);
	u8 val = 0;
	
	if (data && data->pmic_read)
		data->pmic_read(&val);
		
	return (val*10000);
}

static int vd_regulator_set_voltage(struct regulator_dev *dev, int min_uV, int max_uV, unsigned *selector)
{
	struct vd_regulator_data *data = rdev_get_drvdata(dev);
	
	if (data && data->pmic_write)
		data->pmic_write((u8)(min_uV/10000));
		
	return 0;
}

static struct regulator_ops vd_regulator_voltage_ops = {
	.get_voltage = vd_regulator_get_voltage,
	.set_voltage = vd_regulator_set_voltage,
};

static const struct regulator_desc vd_regulator_desc  = {
	.name		= "samsung-vd",
	.ops		= &vd_regulator_voltage_ops,
	.type		= REGULATOR_VOLTAGE,
	.id			= 0,
	.owner		= THIS_MODULE,
	.min_uV		= 600000,
	.uV_step	= 10000,
	.n_voltages	= 128,
};

static int vd_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct vd_regulator_data *vd_regulator;
	int ret;
	int retry = 3;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	vd_regulator = devm_kzalloc(dev, sizeof(struct vd_regulator_data), GFP_KERNEL);
	if (!vd_regulator)
		return -ENOMEM;

	dev_set_drvdata(dev, vd_regulator);

	vd_regulator_if_setup(vd_regulator);

	reg_init_data = of_get_regulator_init_data(dev, dev->of_node, &vd_regulator_desc);
	if (!reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return -EINVAL;
	}

	config.dev = dev;
	config.init_data = reg_init_data;
	config.driver_data = vd_regulator;
	config.of_node = dev->of_node;

	rdev = regulator_register(&vd_regulator_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "regulator register failed.\n");
		return PTR_ERR(rdev);
	}

	vd_regulator->rdev = rdev;

	return 0;
}

static int vd_regulator_remove(struct platform_device *pdev)
{
	struct vd_regulator_data *drvdata = platform_get_drvdata(pdev);
	regulator_unregister(drvdata->rdev);
	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id regulator_vd_of_match[] = {
	{ .compatible = "samsung,vd-regulator", },
	{},
};
#endif

static struct platform_driver vd_regulator_driver = {
	.probe		= vd_regulator_probe,
	.remove		= vd_regulator_remove,
	.driver		= {
		.name		= "vd-regulator",
		.of_match_table = of_match_ptr(regulator_vd_of_match),
	},
};

static int __init vd_regulator_init(void)
{
	return platform_driver_register(&vd_regulator_driver);
}
subsys_initcall(vd_regulator_init);

static void __exit vd_regulator_exit(void)
{
	platform_driver_unregister(&vd_regulator_driver);
}
module_exit(vd_regulator_exit);

MODULE_AUTHOR("Kim Yong Jin <yongjin79.kim@samsung.com>");
MODULE_DESCRIPTION("samsung,vd u-series voltage regulator");
MODULE_LICENSE("GPL");


