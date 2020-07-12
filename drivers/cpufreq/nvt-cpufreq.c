
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <mach/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/moduleparam.h>

#define CPUFREQ_ON	1
#define CPUFREQ_OFF	0
#define CPUFREQ_TOLERANCE	4000 // 4Mhz

static int cpufreq_on = CPUFREQ_OFF;
bool cpufreq_state = CPUFREQ_OFF;
static unsigned int boot_freq;
static unsigned int limit_freq;
static unsigned int nvt_debug = 0;
static int nvt_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation);
int nvt_change_freq(unsigned int cpu, unsigned int target_freq);

#define NVT_CPUCLK_DBG(fmt, args...)	\
do {					\
	if (nvt_debug)			\
		pr_info(fmt, ##args);	\
} while(0)

static DEFINE_MUTEX(nvt_cpufreq_lock);

struct nvt_cpufreq_struct_data {
	struct cpufreq_frequency_table *freq_table;
} nvt_cpufreq_data;

static struct freq_attr *nvt_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static inline unsigned int obtain_cpu(void)
{
	return cpumask_first(cpu_online_mask);
}

void nvt_cpufreq_turn_on(bool en)
{
	if (en)
	/* CPU freq ON */
		cpufreq_on = CPUFREQ_ON;
	else
	/* CPU freq OFF */
		cpufreq_on = CPUFREQ_OFF;

	NVT_CPUCLK_DBG("%s cpufreq_on: %d\r\n", __func__, cpufreq_on);
}
EXPORT_SYMBOL(nvt_cpufreq_turn_on);

bool nvt_get_cpufreq_onoff(void)
{
	return	cpufreq_on;
}
EXPORT_SYMBOL(nvt_get_cpufreq_onoff);

static ssize_t show_nvt_cpufreq_on(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", cpufreq_on);

	return ret;
}

static ssize_t store_nvt_cpufreq_on(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	int on;
	int ret;

	ret = sscanf(buf, "%d", &on);

	if (ret != 1) {
		pr_err("Invalid arg\n");
		return -EINVAL;
	}

	if (on) { /* CPU freq ON */
		nvt_cpufreq_turn_on(true);
	} else if (on == 0) { /* CPU freq OFF */
		nvt_cpufreq_turn_on(false);
	}

	return (ssize_t)count;
}

define_one_global_rw(nvt_cpufreq_on);

static ssize_t show_freq_limit(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = snprintf(buf, PAGE_SIZE, "%u\n", limit_freq);

	return ret;
}

static int nvt_get_freqtable_idx(unsigned int freq, unsigned int cpu)
{
	struct cpufreq_frequency_table *pos, *table;

	table = cpufreq_frequency_get_table(cpu);
	if (unlikely(!table)) {
		pr_debug("%s: Unable to find frequency table\n", __func__);
		return -ENOENT;
	}

	cpufreq_for_each_valid_entry(pos, table)
		if (pos->frequency == freq)
			return pos - table;

	return -EINVAL;

}

static ssize_t store_freq_limit(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int freq;
	unsigned int cpu;
	int ret;

	ret = sscanf(buf, "%u", &freq);

	if (ret != 1) {
		pr_err("Invalid arg\n");
		return -EINVAL;
	}

	if (freq != 0) {
		ret = nvt_get_freqtable_idx(freq, obtain_cpu());
		if (ret < 0) {
			/* No match freq in table, go out */
			pr_err("Invalid arg\n");
			return -EINVAL;
		}
	}

	if (freq > 0) {
		cpufreq_state = nvt_get_cpufreq_onoff();
		/* Turn off freq adjustment from kernel */
		nvt_cpufreq_turn_on(false);
		/* Set freq */
		nvt_change_freq(obtain_cpu(), freq);
		limit_freq = freq;
	} else {
		/* Restore freq */
		nvt_change_freq(obtain_cpu(), boot_freq);
		limit_freq = 0;
		/* Allow freq adjustment from kernel */
		nvt_cpufreq_turn_on(cpufreq_state);
	}

	return (ssize_t)count;
}

define_one_global_rw(freq_limit);

static int nvt_cpufreq_sysfs_register(void)
{
	int ret;

	ret = cpufreq_sysfs_create_file(&nvt_cpufreq_on.attr);

	if (ret < 0)
		goto out;

	ret = cpufreq_sysfs_create_file(&freq_limit.attr);

out:
	return ret;
}

static void nvt_cpufreq_sysfs_remove(void)
{
	cpufreq_sysfs_remove_file(&nvt_cpufreq_on.attr);

	cpufreq_sysfs_remove_file(&freq_limit.attr);
}

static int _nvt_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	int cluster_id = topology_physical_package_id(policy->cpu);
	struct cpufreq_freqs freqs;
	unsigned int idx;
	int ret = 0;

	mutex_lock(&nvt_cpufreq_lock);

	NVT_CPUCLK_DBG("%s Tar:%d Rel:%d CPU:%d MIN:%d MAX:%d CUR:%d\r\n",
		__func__, target_freq, relation, policy->cpu,
		policy->min, policy->max, policy->cur);

	/* scale the target frequency to one of the extremes supported */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;
	if (target_freq > policy->cpuinfo.max_freq)
		target_freq = policy->cpuinfo.max_freq;

	/* Lookup the next frequency */
	ret = cpufreq_frequency_table_target(policy,
		nvt_cpufreq_data.freq_table, target_freq, relation, &idx);
	if (ret) {
		pr_err("[CPUFREQ] %s give invalid freq %u relation %u\n",
			__func__, target_freq, relation);
		goto out;
	}

	freqs.old = policy->cur;
	freqs.new = nvt_cpufreq_data.freq_table[idx].frequency;
	freqs.cpu = policy->cpu;

	if (freqs.old == freqs.new)
		goto out;

	/* pre-change notification */
	cpufreq_freq_transition_begin(policy, &freqs);
	NVT_CPUCLK_DBG("[CPUFREQ] target final freq %u\n", freqs.new);
	if(cluster_id == 0)
		change_arm_clock((freqs.new) / 1000);
	else
		change_a73_clock((freqs.new) / 1000);

	/* post change notification */
	cpufreq_freq_transition_end(policy, &freqs, 0);

out:
	mutex_unlock(&nvt_cpufreq_lock);
	return ret;
}

int nvt_change_freq(unsigned int cpu, unsigned int target_freq)
{
	struct cpufreq_policy *policy;
	int ret = 0;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_err("[CPUFREQ] %s error - CPU:%u policy is NULL %u\n",
		__func__, cpu, target_freq);
		goto out;
	}

	ret = _nvt_target(policy, target_freq, CPUFREQ_RELATION_C);

	cpufreq_cpu_put(policy);
out:
	return ret;
}
EXPORT_SYMBOL(nvt_change_freq);

static int nvt_verify_speed(struct cpufreq_policy *policy)
{
	NVT_CPUCLK_DBG("%s\r\n", __func__);
	return cpufreq_frequency_table_verify(policy,
		nvt_cpufreq_data.freq_table);
}

static int nvt_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	int ret = 0;
	if (cpufreq_on) {
		ret = _nvt_target(policy, target_freq, relation);
	} else {
		NVT_CPUCLK_DBG("[CPUFREQ] Can not set CPU frequency\n");
	}
	return ret;
}

static unsigned int nvt_getspeed(unsigned int cpu)
{
	int cluster_id = topology_physical_package_id(cpu);
	unsigned int speed;
	int index;

	/* request the prcm to get the current ARM opp */

	if(cluster_id == 0)
		speed = (get_cpu_clk()/1000);
	else
		speed = (get_a73_clk()/1000);

	NVT_CPUCLK_DBG("[CPUFREQ] %s CPU:%d raw speed %u\r\n",
		__func__, cpu, speed);

	for (index = 0;
	nvt_cpufreq_data.freq_table[index].frequency != CPUFREQ_TABLE_END;
		index++)
		if (abs(nvt_cpufreq_data.freq_table[index].frequency - speed)
			<= CPUFREQ_TOLERANCE)
			break;

	if (nvt_cpufreq_data.freq_table[index].frequency ==
		CPUFREQ_TABLE_END) {
		pr_err("[CPUFREQ] %s no match frequecy, speed %u\n",
			__func__, speed);
		WARN_ON(1);
		return nvt_cpufreq_data.freq_table[0].frequency;
	}

	return nvt_cpufreq_data.freq_table[index].frequency;
}

static int nvt_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret;

	boot_freq = policy->suspend_freq = policy->cur = nvt_getspeed(policy->cpu);
	NVT_CPUCLK_DBG("[CPUFREQ] %s cur freq %u\n", __func__, policy->cur);

	ret = cpufreq_table_validate_and_show(policy, nvt_cpufreq_data.freq_table);
	if (ret) {
		pr_err("%s: invalid frequency table: %d\n", __func__, ret);
		return ret;
	}

	policy->cpuinfo.transition_latency = 20 * 1000;

	cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));

	return 0;
}

static int nvt_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	policy->freq_table = NULL;
	return 0;
}

#ifdef CONFIG_POWER_SAVING_MODE
static int nvt_freq_poweroff_resume(struct device *dev)
{
	nvt_change_freq(obtain_cpu(), boot_freq);
	limit_freq = 0;
	NVT_CPUCLK_DBG("%s limit_freq=%d.\n", __func__, limit_freq);
	return 0;
}
#endif /*CONFIG_POWER_SAVING_MODE*/

static struct cpufreq_driver nvt_cpufreq_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= nvt_verify_speed,
	.target		= nvt_target,
	.get		= nvt_getspeed,
	.init		= nvt_cpufreq_cpu_init,
	.exit		= nvt_cpufreq_cpu_exit,
	.attr		= nvt_cpufreq_attr,
	.name		= "nvt-cpufreq",
#ifdef CONFIG_PM
	.suspend	= cpufreq_generic_suspend,
	.resume		= cpufreq_generic_suspend, 
#endif
};

static int nvt_cpufreq_probe(struct platform_device *pdev)
{
	int ret = -EINVAL;

	ret = of_init_opp_table(&pdev->dev);
	if (ret) {
		pr_err("[CPUFREQ] can't get correct setting from device tree\n");
		goto err;
	}
	ret = dev_pm_opp_init_cpufreq_table(&pdev->dev,
		&nvt_cpufreq_data.freq_table);
	if (ret) {
		pr_err("[CPUFREQ] can't init cpufreq_frequency_table\n");
		goto err;
	}

	{
		int x;

		for (x = 0;; x++) {
			if (nvt_cpufreq_data.freq_table[x].frequency ==
				CPUFREQ_TABLE_END)
				break;
			pr_info("[CPUFREQ] freq table index %u freq %u\n",
				nvt_cpufreq_data.freq_table[x].driver_data,
				nvt_cpufreq_data.freq_table[x].frequency);
		}
	}

	ret = cpufreq_register_driver(&nvt_cpufreq_driver);
	if (ret) {
		pr_err("[CPUFREQ] failed to register cpufreq driver\n");
		goto err_free_table;
	}

	/* sysfs register*/
	nvt_cpufreq_sysfs_register();
	if (ret) {
		pr_err("%s: cannot register NVT cpufreq sysfs file\n", __func__);
		goto err_free_table;
	}
#ifdef CONFIG_POWER_SAVING_MODE
	pdev->dev.power.pwsv_magic = PWSV_MAGIC;
#endif

	return 0;
err_free_table:
	dev_pm_opp_free_cpufreq_table(&pdev->dev, &nvt_cpufreq_data.freq_table);
err:
	return ret;

}
static int nvt_cpufreq_remove(struct platform_device *pdev)
{
	nvt_cpufreq_sysfs_remove();
	cpufreq_unregister_driver(&nvt_cpufreq_driver);
	dev_pm_opp_free_cpufreq_table(&pdev->dev, &nvt_cpufreq_data.freq_table);
	return 0;
}

static const struct of_device_id nvt_cpufreq_match[] = {
	{
		.compatible = "nvt,nvt72xxx-cpufreq",
	},
	{},
};

static struct dev_pm_ops nvt_freq_pm_ops = {
#ifdef CONFIG_POWER_SAVING_MODE
	.pwsv_poweroff		= NULL,
	.pwsv_poweroff_late	= NULL,
	.pwsv_resume		= NULL,
	/*Set CPU frequency to normal when exit power saving */
	.pwsv_resume_early	= nvt_freq_poweroff_resume,
	.pwsv_suspend		= NULL,
	.pwsv_restore		= nvt_freq_poweroff_resume,
#endif
};

static struct platform_driver nvt_cpufreq_platdrv = {
	.driver = {
		.name	= "nvt-cpufreq",
		.owner	= THIS_MODULE,
		.of_match_table = nvt_cpufreq_match,
		.pm = &nvt_freq_pm_ops,
	},
	.probe		= nvt_cpufreq_probe,
	.remove		= nvt_cpufreq_remove,
};

static int __init nvt_cpufreq_register(void)
{
	return platform_driver_register(&nvt_cpufreq_platdrv);
}

module_param(nvt_debug, uint, 0644);
MODULE_PARM_DESC(nvt_debug, "Debug flag for debug message display");
device_initcall(nvt_cpufreq_register);
