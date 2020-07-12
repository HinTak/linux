#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/cpe.h>

struct thermal_zone_device *nvt_thermal = NULL;

static struct workqueue_struct *nvt_thermal_wq;
static DEFINE_MUTEX(nvt_thermal_lock);

#define BUF_SIZE	1024
#define PRINT_RATE	(1*1000*1000)  /* usecs */

#define BOOT_TEMP_TIME	(1*1000*1000*1000ull)	/* nsec */
#define EXEC_TEMP_TIME	(10*1000*1000*1000ull)	/* nsec */

#define TEMP_INVALID	(0x80000000)

#define RETRY_COUNTER	(0x4000)

static unsigned int nvt_thermal_debug = 0;
#define NVT_THERMAL_DBG(fmt, args...)	\
do {					\
	if (nvt_thermal_debug)		\
		pr_debug(fmt, ##args);	\
} while (0)

/* RESET register offset */
#define RESET_REG_OFF		(0x00)
#define M_RESET_PWRDOWN		0x1

/* AVERAGE TEMPERATURE register offset */
#define TEMP_REG_OFF		(0x04)
#define M_TEMP_AVE_TEMP		0x1ff

#define TRIM_VALUE_POSITIVE	(0x80)
#define TRIM_VALUE_MASK		(0x7F)

struct nvt_tsensor {
	/* IO base address */
	void __iomem *base;
	/* Temperature trim value from OTP */
	int trim_val;
	/*
	 * code base @ base temperature,
	 * used for code to temperature conversion
	 */
	int code_base;
	/* base temperature (in milli-celsius) */
	int base_temp;
	/* milli-celsius per code */
	int mc_per_code;
	/*
	 * trim_data = (0 - trim_data) if reverse_sign_bit is 1
	 *
	 * For KantS2, reverse_sign_bit is 1
	 * Formula is X = (Y + T - A) * mc_per_code + base_temp
	 *
	 * For KantSU2, reverse_sign_bit is 0
	 * Formula is X = (Y - T - A) * mc_per_code + base_temp
	 */
	int reverse_sign_bit;

	int print_on;
	int user_print_on;
	struct delayed_work polling;

	unsigned long boot_temp;
	unsigned long exec_temp;

	u32 sampling_rate;
	u64 ready_time;
};

/*
 * Reset thermal sesnor
 */
static void nvt_thermal_dev_reset(struct nvt_tsensor *sensor)
{
	u32 reg;

	reg = readl(sensor->base + RESET_REG_OFF);

	reg |= M_RESET_PWRDOWN;
	writel(reg, sensor->base + RESET_REG_OFF);

	reg &= ~M_RESET_PWRDOWN;
	writel(reg, sensor->base + RESET_REG_OFF);
}

/*
 * Get current (temperature) code
 */
static int nvt_thermal_dev_get_code(struct nvt_tsensor *sensor)
{
	u32 val = 0;
	int code = 0;
	u32 counter = 0;

	do {
		val = readl(sensor->base + TEMP_REG_OFF);
		code = (val & M_TEMP_AVE_TEMP);
		counter++;
	} while ((code == 0) && (counter < RETRY_COUNTER));

	/*
	 * To simplify driver flow,
	 * We calculate real thermal_code by SW for all chips
	 */
	code = code - sensor->trim_val;

	NVT_THERMAL_DBG("%s(): code = %d - %d (trim_data) = %d\n",
						 __func__,
						 val, sensor->trim_val, code);

	return code;
}

/*
 * Get temperature for passive thermal sensor
 */
static int nvt_thermal_dev_get_temp_passive(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	struct nvt_tsensor *sensor;
	int code = 0, real_temp;

	if (!thermal) {
		pr_err("ERROR: thermal_zone_device is not defined.\n");
		return 0;
	}

	sensor = thermal->devdata;

	if (!sensor) {
		pr_err("ERROR: thermal sensor is not defined.\n");
		return 0;
	}

	code = nvt_thermal_dev_get_code(sensor);

	/*
	 * Kernel 4.1.10's thermal framework only supports
	 * positive temperature
	 */
	real_temp = ((code - sensor->code_base) * sensor->mc_per_code)
			+ sensor->base_temp;
	if (real_temp < 0)
		*temp = 0;
	else
		*temp = (unsigned long)real_temp;

	NVT_THERMAL_DBG("%s(): thermal_temp = %ld\n", __func__, *temp);

	return 0;
}

static void nvt_thermal_get_trim_from_cpe(struct nvt_tsensor *sensor)
{
	int trim_data = 0;

	trim_data = ntcpe_get_thermal_trim();

	if (trim_data == -1) {
		NVT_THERMAL_DBG("%s(): No valid trim data\n", __func__);
		trim_data = 0;
	} else if (trim_data & TRIM_VALUE_POSITIVE) {
		trim_data = (trim_data & TRIM_VALUE_MASK);
	} else {
		trim_data = (0 - (trim_data & TRIM_VALUE_MASK));
	}

	if (sensor->reverse_sign_bit)
		sensor->trim_val = (0 - trim_data);
	else
		sensor->trim_val = trim_data;

	NVT_THERMAL_DBG("%s(): thermal trim_val is %d\n",
				__func__, sensor->trim_val);
}

static struct thermal_zone_device_ops ops = {
	.get_temp = nvt_thermal_dev_get_temp_passive,
};

#ifdef CONFIG_PM
static int nvt_thermal_suspend(struct device *dev)
{
	struct nvt_tsensor *sensor = nvt_thermal->devdata;

	cancel_delayed_work(&sensor->polling);

	return 0;
}

static int nvt_thermal_resume(struct device *dev)
{
	struct nvt_tsensor *sensor = nvt_thermal->devdata;

	/* Reset thermal sensor */
	nvt_thermal_dev_reset(sensor);

	/* Wake up */
	queue_delayed_work_on(0, nvt_thermal_wq, &sensor->polling, 0);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(nvt_thermal_pm_ops, nvt_thermal_suspend,
		nvt_thermal_resume);

/*
 * Get essential properties from DTS.
 */
static int nvt_thermal_of_get_property(struct device *dev,
		struct nvt_tsensor *sensor)
{
	struct device_node *node;
	int trim_type, pkg_type;
	char *otp_name;

	if (!dev->of_node) {
		dev_err(dev, "can't find thermal node\n");
		return -ENODEV;
	}

	otp_name = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!otp_name) {
		dev_err(dev, "kzalloc failed\n");
		return -ENODEV;
	}

	trim_type = ntcpe_get_thermal_trimtype();
	snprintf(otp_name, BUF_SIZE, "thermal_trim_%02d", trim_type);

	/* Find thermal formula */
	node = of_find_node_by_name(dev->of_node, otp_name);
	if (!node) {
		dev_err(dev, "can't find thermal formula\n");
		kfree(otp_name);
		return -ENODEV;
	}
	NVT_THERMAL_DBG("trim_type = %s\n", otp_name);

	if (node->child) {
		pkg_type = ntcpe_get_pkgtype();
		snprintf(otp_name, BUF_SIZE, "pkg_%02d", pkg_type);
		node = of_find_node_by_name(node, otp_name);

		if (!node) {
			dev_err(dev, "can't find thermal formula\n");
			kfree(otp_name);
			return -ENODEV;
		}
		NVT_THERMAL_DBG("pkg_type = %s\n", otp_name);
	}
	kfree(otp_name);

	/* code_base */
	if (of_property_read_u32(node,
				 "code_base",
				 &sensor->code_base)) {
		dev_err(dev, "missing \"code_base\" property!\n");
		return -ENODEV;
	}
	NVT_THERMAL_DBG("code_base = %d\n", sensor->code_base);

	/* base_temp */
	if (of_property_read_u32(node,
				 "base_temp",
				 &sensor->base_temp)) {
		dev_err(dev, "missing \"base_temp\" property!\n");
		return -ENODEV;
	}
	NVT_THERMAL_DBG("base_temp = %d\n", sensor->base_temp);

	/* mc_per_code */
	if (of_property_read_u32(node,
				 "mc_per_code",
				 &sensor->mc_per_code)) {
		dev_err(dev, "missing \"mc_per_code\" property!\n");
		return -ENODEV;
	}
	NVT_THERMAL_DBG("mc_per_code = %d\n", sensor->mc_per_code);

	/* reverse_sign_bit */
	if (of_property_read_u32(node,
				 "reverse_sign_bit",
				 &sensor->reverse_sign_bit)) {
		dev_err(dev, "missing \"reverse_sign_bit\" property!\n");
		return -ENODEV;
	}
	NVT_THERMAL_DBG("reverse_sign_bit = %d\n", sensor->reverse_sign_bit);

	return 0;
}
static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned long temp = 0;

	nvt_thermal_dev_get_temp_passive(nvt_thermal, &temp);

	/* Convert milli-celsius to celsius */
	temp = (temp / 1000);

	return snprintf(buf, 5, "%lu\n", temp);
}

static ssize_t nvt_show_print_temp(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct nvt_tsensor *sensor;
	int ret;

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		return -ENODEV;
	}

	sensor = nvt_thermal->devdata;

	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		return -1;
	}

	ret = snprintf(buf, 3, "%d\n", sensor->user_print_on);

	return ret;
}

static ssize_t nvt_store_print_temp(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	unsigned int on;
	struct nvt_tsensor *sensor;
	int cur_print_on;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		dev_err(dev, "%s(): invalid arg\n", __func__);
		return -EINVAL;
	}

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		goto err;
	}

	sensor = nvt_thermal->devdata;
	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		goto err;
	}

	cur_print_on = sensor->user_print_on || sensor->print_on;

	if ((cur_print_on == 0) && on) {
		sensor->user_print_on = true;

		/* Wake up */
		queue_delayed_work_on(0, nvt_thermal_wq, &sensor->polling,
					sensor->sampling_rate);
	} else if (on) {
		sensor->user_print_on = true;
	} else {
		sensor->user_print_on = false;
	}

err:
	return (ssize_t)count;
}

static ssize_t nvt_show_print_on(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct nvt_tsensor *sensor;
	int ret;

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		return -ENODEV;
	}

	sensor = nvt_thermal->devdata;

	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		return -1;
	}

	ret = snprintf(buf, 3, "%d\n", sensor->print_on);

	return ret;
}

static ssize_t nvt_store_print_on(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	unsigned int on;
	struct nvt_tsensor *sensor;
	int cur_print_on;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		dev_err(dev, "%s() invalid arg\n", __func__);
		return -EINVAL;
	}

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		goto err;
	}

	sensor = nvt_thermal->devdata;
	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		goto err;
	}

	cur_print_on = sensor->user_print_on || sensor->print_on;

	if ((cur_print_on == 0) && on) {
		sensor->print_on = true;

		/* Wake up */
		queue_delayed_work_on(0, nvt_thermal_wq, &sensor->polling,
					sensor->sampling_rate);
	} else if (on) {
		sensor->print_on = true;
	} else {
		sensor->print_on = false;
	}

err:
	return (ssize_t)count;
}
static ssize_t show_boot_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvt_tsensor *sensor;

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		return -ENODEV;
	}

	sensor = nvt_thermal->devdata;

	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		return -1;
	}

	return snprintf(buf, 5, "%lu\n", sensor->boot_temp);
}

static ssize_t show_exec_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvt_tsensor *sensor;

	if (!nvt_thermal) {
		dev_err(dev, "No device\n");
		return -ENODEV;
	}

	sensor = nvt_thermal->devdata;

	if (sensor == NULL) {
		dev_err(dev, "No sensor struct\n");
		return -1;
	}
	return snprintf(buf, 5, "%lu\n", sensor->exec_temp);
}

static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);
static DEVICE_ATTR(print_temp, 0644, nvt_show_print_temp, nvt_store_print_temp);
static DEVICE_ATTR(print_on, 0644, nvt_show_print_on, nvt_store_print_on);
static DEVICE_ATTR(boot_temp, 0444, show_boot_temp, NULL);
static DEVICE_ATTR(exec_temp, 0444, show_exec_temp, NULL);

static void nvt_thermal_handler(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct nvt_tsensor *sensor =
		container_of(delayed_work, struct nvt_tsensor, polling);
	unsigned long cur_temp;
	u64 cur_time;

	mutex_lock(&nvt_thermal_lock);

	nvt_thermal_dev_get_temp_passive(nvt_thermal, &cur_temp);
	cur_temp /= 1000;
	cur_time = local_clock();

	if ((sensor->boot_temp == TEMP_INVALID) &&
	    ((cur_time - sensor->ready_time) >= BOOT_TEMP_TIME))
		sensor->boot_temp = cur_temp;

	if ((sensor->exec_temp == TEMP_INVALID) &&
	    ((cur_time - sensor->ready_time) >= EXEC_TEMP_TIME))
		sensor->exec_temp = cur_temp;

	if (sensor->print_on)
		pr_warn("TMU: temperature = %lu \'C\n", cur_temp);

	if (sensor->user_print_on)
		pr_info("\033[1;7;33mT^%lu'C\033[0m\n", cur_temp);

	/* reschedule the next work */
	if ((sensor->print_on) || (sensor->user_print_on) ||
	    (sensor->boot_temp == TEMP_INVALID) ||
	    (sensor->exec_temp == TEMP_INVALID))
		queue_delayed_work_on(0, nvt_thermal_wq, &sensor->polling,
					sensor->sampling_rate);

	mutex_unlock(&nvt_thermal_lock);
}
static int nvt_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvt_tsensor *sensor;
	struct resource *res;
	struct platform_device *soc_pdev;
	struct device *soc_device;
	unsigned long cur_temp;
	int ret = 0;

	/* Init sensor data */
	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	/* Save ready time */
	sensor->ready_time = local_clock();

	/* IO remap */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sensor->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(sensor->base)) {
		dev_err(dev, "failed to get io address\n");
		return PTR_ERR(sensor->base);
	}

	/* Reset thermal sensor */
	nvt_thermal_dev_reset(sensor);

	dev_set_drvdata(dev, sensor);

	/* Parse property from DTS */
	ret = nvt_thermal_of_get_property(dev, sensor);
	if (ret)
		return ret;

	nvt_thermal_get_trim_from_cpe(sensor);

	nvt_thermal = thermal_zone_device_register("nvt_thermal", 0, 0,
				sensor, &ops, NULL, 0, 0);
	if (IS_ERR(nvt_thermal)) {
		dev_err(dev, "thermal zone device is NULL\n");
		ret = PTR_ERR(nvt_thermal);
		return ret;
	}

	platform_set_drvdata(pdev, nvt_thermal);

	NVT_THERMAL_DBG("Thermal Sensor Loaded at: 0x%p.\n", sensor->base);

	/*
	 * Init parameter for thermal node
	 */
	sensor->boot_temp = TEMP_INVALID;
	sensor->exec_temp = TEMP_INVALID;
	sensor->print_on = 0;
	sensor->user_print_on = 0;
	sensor->sampling_rate = usecs_to_jiffies(PRINT_RATE);

	nvt_thermal_wq = create_freezable_workqueue(dev_name(dev));
	if (!nvt_thermal_wq) {
		dev_err(dev, "Creation of nvt_thermal_wq failed\n");
		return -ENOMEM;
	}

	/* To support periodic temprature monitoring */
	INIT_DELAYED_WORK(&sensor->polling, nvt_thermal_handler);

	/* initialize work */
	queue_delayed_work_on(0, nvt_thermal_wq, &sensor->polling,
				sensor->sampling_rate);

	/* Create device file */
	ret = device_create_file(dev, &dev_attr_temperature);
	if (ret != 0)
		dev_err(dev, "Failed to create temperature file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_print_on);
	if (ret != 0)
		dev_err(dev, "Failed to create print_on file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_print_temp);
	if (ret != 0)
		dev_err(dev, "Failed to create print_temp file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_boot_temp);
	if (ret != 0)
		dev_err(dev, "Failed to create boot_temp file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_exec_temp);
	if (ret != 0)
		dev_err(dev, "Failed to create exec_temp file: %d\n", ret);

	/* Create soc device node */
	soc_device = bus_find_device_by_name(&platform_bus_type, NULL, "soc");
	if (!soc_device) {
		NVT_THERMAL_DBG("cannot find soc device, create a new one\n");
		soc_pdev = platform_device_register_simple("soc", -1, NULL, 0);
		soc_device = &soc_pdev->dev;
	}

	/* Add thermal node link below soc node */
	ret = sysfs_create_link(&(soc_device->kobj), &(dev->kobj), "thermal");
	if (ret < 0)
		dev_err(dev, "Failed to create sysfs link: %d\n", ret);

	return 0;
}

static int nvt_thermal_remove(struct platform_device *pdev)
{
	struct thermal_zone_device *nvt_thermal =
		platform_get_drvdata(pdev);

	thermal_zone_device_unregister(nvt_thermal);

	return 0;
}

static const struct of_device_id nvt_thermal_id_table[] = {
	{ .compatible = "nvt,tsensor" },
	{}
};
MODULE_DEVICE_TABLE(of, nvt_thermal_id_table);

static struct platform_driver nvt_thermal_driver = {
	.probe = nvt_thermal_probe,
	.remove = nvt_thermal_remove,
	.driver = {
		.name = "nvt_thermal",
		.of_match_table = nvt_thermal_id_table,
#ifdef CONFIG_PM
		.pm = &nvt_thermal_pm_ops,
#endif
	},
};

module_platform_driver(nvt_thermal_driver);
module_param(nvt_thermal_debug, uint, 0644);
MODULE_PARM_DESC(nvt_thermal_debug, "Debug flag for debug message display");
