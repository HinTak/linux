
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regulator/consumer.h>

#define SDP_AVS_STEP_MAX 10

struct sdp_avs_obj {
	char *name;
	void __iomem *mem;
	u32 shift;
	u32 mask;
	u32 n_steps;
	u32 steps[SDP_AVS_STEP_MAX];
	u32 mvolts[SDP_AVS_STEP_MAX];
	struct regulator *regulator;
	u32 step;
};

struct sdp_avs_data {
	int nr_avs;
	struct sdp_avs_obj *avs_objs;
	bool enabled;
};

static struct sdp_avs_data sdp_avs;


int sdp_avs_get_step(char *name)
{
	int i;
	
	if (!sdp_avs.enabled)
		return 0;
		
	for (i = 0; i < sdp_avs.nr_avs; i++) {
		if (!strcmp(sdp_avs.avs_objs[i].name, name))
			return sdp_avs.avs_objs[i].step;
	}
	return 0;
}
EXPORT_SYMBOL(sdp_avs_get_step);


static int sdp_avs_init_obj(struct device_node *avs_node, struct sdp_avs_obj *avs_obj)
{
	int ret;
	u32 data[2];
	char *regulator;

	ret = of_property_read_string(avs_node, "name", (const char **)&avs_obj->name);
	if (ret) {
		pr_err("[sdp-avs] failed to get name.\n");
		return ret;
	}

	ret = of_property_read_u32_array(avs_node, "reg", data, 2);
	if (ret) {
		pr_err("[sdp-avs] %s: failed to get reg.\n", avs_obj->name);
		return ret;
	}

	avs_obj->mem = ioremap(data[0], data[1]);
	if (!avs_obj->mem) {
		pr_err("[sdp-avs] %s: failed to ioremap.\n", avs_obj->name);
		return -ENOMEM;
	}

	ret = of_property_read_u32_array(avs_node, "bitmask", data, 2);
	if (ret) {
		pr_err("[sdp-avs] %s: failed to get bitmask.\n", avs_obj->name);
		return ret;
	}

	avs_obj->shift = data[0];
	avs_obj->mask = data[1];

	ret = of_property_read_u32(avs_node, "n_steps", &avs_obj->n_steps);
	if (ret) {
		pr_err("[sdp-avs] %s: failed to get n_steps.\n", avs_obj->name);
		return ret;
	}

	if (avs_obj->n_steps > SDP_AVS_STEP_MAX)
		avs_obj->n_steps = SDP_AVS_STEP_MAX;

	ret = of_property_read_u32_array(avs_node, "steps", avs_obj->steps, avs_obj->n_steps);
	if (ret) {
		pr_err("[sdp-avs] %s: failed to get steps.\n", avs_obj->name);
		return ret;
	}

	ret = of_property_read_string(avs_node, "regulator", (const char **)&regulator);
	if (!ret) {
		ret = of_property_read_u32_array(avs_node, "mvolts", avs_obj->mvolts, avs_obj->n_steps);
		if (ret) {
			pr_err("[sdp-avs] %s: failed to get mvolts.\n", avs_obj->name);
			return ret;
		}

		avs_obj->regulator = regulator_get_optional(NULL, regulator);
	}
	
	return 0;
}

static int sdp_avs_exit_obj(struct sdp_avs_obj *avs_obj)
{
	if (avs_obj->mem)
		iounmap(avs_obj->mem);

	if (avs_obj->regulator && !IS_ERR(avs_obj->regulator))
		regulator_put(avs_obj->regulator);

	return 0;
}

static int sdp_avs_exec_obj(struct sdp_avs_obj *avs_obj)
{
	int i;
	u32 chip_val;
	int uvolt;

	chip_val = (readl(avs_obj->mem) >> avs_obj->shift) & avs_obj->mask;
	for (i = 0; i < avs_obj->n_steps; i++) {
		if (chip_val <= avs_obj->steps[i]) {
			avs_obj->step = i;
			break;
		}
	}

	if (avs_obj->regulator && !IS_ERR(avs_obj->regulator)) {
		uvolt = avs_obj->mvolts[avs_obj->step] * 1000;
		regulator_set_voltage(avs_obj->regulator, uvolt, uvolt);
	}

	pr_err("[sdp-avs] name=%s, steps=(%d %d %d %d), step=%d\n",
			avs_obj->name, avs_obj->steps[0], avs_obj->steps[1], avs_obj->steps[2], avs_obj->steps[3], avs_obj->step);
			
	return 0;
}

static int __init sdp_avs_init(void)
{
	struct device_node *avs_root, *avs_node;
	int nr_avs;
	int ret, i;

	avs_root = of_find_compatible_node(NULL, NULL, "samsung,sdp-avs");
	if (!avs_root)
		return 0;

	nr_avs = of_get_available_child_count(avs_root);

	sdp_avs.nr_avs = nr_avs;
	sdp_avs.avs_objs = kzalloc(nr_avs * sizeof(struct sdp_avs_obj), GFP_KERNEL);
	if (!sdp_avs.avs_objs) {
		pr_err("[sdp-avs] failed to allocate memory.\n");
		ret = -ENOMEM;
		goto err;
	}

	i = 0;
	for_each_available_child_of_node(avs_root, avs_node) {
		ret = sdp_avs_init_obj(avs_node, &sdp_avs.avs_objs[i++]);
		if (ret)
			goto err;
	}

	for (i = 0; i < nr_avs; i++) {
		sdp_avs_exec_obj(&sdp_avs.avs_objs[i]);
	}

	sdp_avs.enabled = true;
	
	return 0;

err:
	for (i = 0; i < nr_avs; i++)
		sdp_avs_exit_obj(&sdp_avs.avs_objs[i]);

	if (sdp_avs.avs_objs)
		kfree(sdp_avs.avs_objs);

	pr_err("[sdp-avs] failed to initialize (%d).\n", ret);
	return ret;
}

device_initcall_sync(sdp_avs_init);


