/* linux/drivers/cpufreq/sdp-cpufreq.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP - CPU frequency scaling support for SDP SoCs
 *
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/of.h>
#include <plat/cpufreq.h>
#include <mach/hardware.h>
#include <mach/platform.h>

#ifdef CONFIG_SDP_AVS
#include <linux/power/sdp_asv.h>
#endif

#ifdef CONFIG_SDP_THERMAL
#include <linux/platform_data/sdp_thermal.h>
#endif

struct sdp_dvfs_info *sdp_info;

static struct regulator *arm_regulator;
static struct cpufreq_freqs freqs;

static bool sdp_cpufreq_disable;
static bool sdp_cpufreq_on;
static bool sdp_user_cpufreq_on;
static bool sdp_asv_on;
static bool sdp_cpufreq_lock_disable;
static bool sdp_cpufreq_init_done;
static DEFINE_MUTEX(set_freq_lock);
static DEFINE_MUTEX(set_cpu_freq_lock);
static DEFINE_MUTEX(cpufreq_on_lock);
static DEFINE_MUTEX(set_volt_lock);

unsigned int g_cpufreq_limit_id;
unsigned int g_cpufreq_limit_val[DVFS_LOCK_ID_END];
unsigned int g_cpufreq_limit_level;

unsigned int g_cpufreq_lock_id;
unsigned int g_cpufreq_lock_val[DVFS_LOCK_ID_END];
unsigned int g_cpufreq_lock_level;

#ifndef CONFIG_SDP_AVS
unsigned int sdp_result_of_asv = 0;
unsigned int sdp_asv_stored_result = 0;
#endif

static int sdp_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      sdp_info->freq_table);
}

static unsigned int sdp_getspeed(unsigned int cpu)
{
	if (sdp_info->get_speed)
		return sdp_info->get_speed() / 1000;
	else
		return clk_get_rate(sdp_info->cpu_clk) / 1000;
}

static int sdp_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int index, old_index = UINT_MAX;
	unsigned int arm_volt;
	int ret = 0, i;
	struct cpufreq_frequency_table *freq_table = sdp_info->freq_table;
	unsigned int *volt_table = sdp_info->volt_table;

	mutex_lock(&set_freq_lock);

	if (sdp_cpufreq_disable)
		goto out;

	if (!sdp_cpufreq_on) {
		ret = -EAGAIN;
		goto out;
	}

	freqs.old = policy->cur;

	/*
	 * cpufreq_frequency_table_target() cannot be used for freqs.old
	 * because policy->min/max may have been changed. If changed, the
	 * resulting old_index may be inconsistent with freqs.old, which
	 * will lead to inconsistent voltage/frequency configurations later.
	 */
	for (i = 0; freq_table[i].frequency != (u32)CPUFREQ_TABLE_END; i++) {
		if (freq_table[i].frequency == freqs.old)
			old_index = freq_table[i].index;
	}
	if (old_index == UINT_MAX) {
		ret = -EINVAL;
		goto out;
	}

	if (cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index)) {
		ret = -EINVAL;
		goto out;
	}

	/* Need to set performance limitation */
	if (!sdp_cpufreq_lock_disable && (index > g_cpufreq_lock_level))
		index = g_cpufreq_lock_level;

	if (!sdp_cpufreq_lock_disable && (index < g_cpufreq_limit_level))
		index = g_cpufreq_limit_level;

	/*
	 * TODO:
	 * To overclock the cpu, user set cpu lock value to higher value than
	 * limit value. So we should set frequency index to lock level that
	 * setted by user.
	 * must be removed in next chip(hawk).
	 */
	if (g_cpufreq_lock_level < g_cpufreq_limit_level  &&
		!(g_cpufreq_limit_id & (1U << DVFS_LOCK_ID_TMU)))
		index = g_cpufreq_lock_level;

#if 0
	/* Do NOT step up max arm clock directly to reduce power consumption */
	if (index == sdp_info->max_support_idx && old_index > 3)
		index = 3;
#endif

	freqs.new = freq_table[index].frequency;
	freqs.cpu = policy->cpu;

	arm_volt = volt_table[index];

	/* When the new frequency is higher than current frequency */
	if ((freqs.new > freqs.old) && arm_regulator) {
		/* Firstly, voltage up to increase frequency */
		ret = sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
		if (ret < 0) {
			ret = -EIO;
			goto out;
		}
	}

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* call cpu frequency change function */
	if (freqs.new != freqs.old)
		sdp_info->set_freq(old_index, index);
	
	/* for apply changed clk to subsystem */
	clk_set_rate(sdp_info->cpu_clk, freqs.new * 1000);

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if ((freqs.new < freqs.old) && arm_regulator) {
		/* down the voltage after frequency change */
		sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
	}

out:
	mutex_unlock(&set_freq_lock);

	return ret;
}

/**
 * sdp_find_cpufreq_level_by_volt - find cpufreq_level by requested
 * arm voltage.
 *
 * This function finds the cpufreq_level to set for voltage above req_volt
 * and return its value.
 */
int sdp_find_cpufreq_level_by_volt(unsigned int arm_volt,
					unsigned int *level)
{
	struct cpufreq_frequency_table *table;
	unsigned int *volt_table = sdp_info->volt_table;
	int i;

	if (!sdp_cpufreq_init_done)
		return -EINVAL;

	table = cpufreq_frequency_get_table(0);
	if (!table) {
		pr_err("%s: Failed to get the cpufreq table\n", __func__);
		return -EINVAL;
	}

	/* check if arm_volt has value or not */
	if (!arm_volt) {
		pr_err("%s: req_volt has no value.\n", __func__);
		return -EINVAL;
	}

	/* find cpufreq level in volt_table */
	for (i = (int)sdp_info->min_support_idx;
			i >= (int)sdp_info->max_support_idx; i--) {
		if (volt_table[i] >= arm_volt) {
			*level = (unsigned int)i;
			return 0;
		}
	}

	pr_err("%s: Failed to get level for %u uV\n", __func__, arm_volt);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sdp_find_cpufreq_level_by_volt);

/* This function finds the freq level by requested frequency */
int sdp_cpufreq_get_level(unsigned int freq, unsigned int *level)
{
	struct cpufreq_frequency_table *table;
	unsigned int i;

	if (!sdp_cpufreq_init_done)
		return -EINVAL;

	table = cpufreq_frequency_get_table(0);
	if (!table) {
		pr_err("%s: Failed to get the cpufreq table\n", __func__);
		return -EINVAL;
	}

	for (i = sdp_info->max_real_idx;
		(table[i].frequency != (u32)CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == freq) {
			*level = i;
			return 0;
		}
	}

	pr_err("%s: %u KHz is an unsupported cpufreq\n", __func__, freq);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sdp_cpufreq_get_level);

/* This function locks frequency lower level */
int sdp_cpufreq_lock(unsigned int nId,
			 enum cpufreq_level_index cpufreq_level)
{
	int ret = 0, i, old_idx = -EINVAL;
	unsigned int freq_old, freq_new, arm_volt;
	unsigned int *volt_table;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	unsigned int max_idx;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	if (!sdp_info)
		return -EPERM;

	if (sdp_cpufreq_disable && (nId != DVFS_LOCK_ID_TMU)) {
		pr_info("CPUFreq is already fixed\n");
		return -EPERM;
	}

	/* for unlimited control in USER (overclock) */
	if (nId == DVFS_LOCK_ID_USER)
		max_idx = sdp_info->max_real_idx;
	else
		max_idx = sdp_info->max_support_idx;

	if (cpufreq_level < max_idx
			|| cpufreq_level > sdp_info->min_support_idx) {
		pr_warn("%s: invalid cpufreq_level(%d:%d)\n", __func__, nId,
				cpufreq_level);
		return -EINVAL;
	}

	policy = cpufreq_cpu_get(0);
	if (!policy)
		return -EPERM;

	volt_table = sdp_info->volt_table;
	freq_table = sdp_info->freq_table;

	mutex_lock(&set_cpu_freq_lock);
	if (g_cpufreq_lock_id & (1U << nId)) {
		mutex_unlock(&set_cpu_freq_lock);
		return 0;
	}

	g_cpufreq_lock_id |= (1U << nId);
	g_cpufreq_lock_val[nId] = cpufreq_level;

	/* If the requested cpufreq is higher than current min frequency */
	if (cpufreq_level < g_cpufreq_lock_level)
		g_cpufreq_lock_level = cpufreq_level;

	mutex_unlock(&set_cpu_freq_lock);

	/* TODO:
	 * limit check code is blocked to overclock cpu frequency
	 * by using freq lock.
	 * must be unlocked in next chip(hawk).
	 */
#if 0
	if ((g_cpufreq_lock_level < g_cpufreq_limit_level)
				&& (nId != DVFS_LOCK_ID_PM)) {
		printk("%s-lock level must be lower than limit level! lock=%d, limit=%d\n",
				g_cpufreq_lock_level, g_cpufreq_limit_level);
		return 0;
	}
#endif

	/* Do not setting cpufreq lock frequency
	 * because current governor doesn't support dvfs level lock
	 * except DVFS_LOCK_ID_PM */
	if (sdp_cpufreq_lock_disable && (nId != DVFS_LOCK_ID_PM))
		return 0;

	/* If current frequency is lower than requested freq,
	 * it needs to update
	 */
	mutex_lock(&set_freq_lock);
	freq_old = policy->cur;
	freq_new = freq_table[cpufreq_level].frequency;

	if (freq_old < freq_new) {
		/* Find out current level index */
		for (i = 0; freq_table[i].frequency != (u32)CPUFREQ_TABLE_END; i++) {
			if (freq_old == freq_table[i].frequency) {
				old_idx = (int)freq_table[i].index;
				break;
			}
		}
		if (old_idx == -EINVAL) {
			printk(KERN_ERR "%s: Level not found\n", __func__);
			mutex_unlock(&set_freq_lock);
			return -EINVAL;
		}

		freqs.old = freq_old;
		freqs.new = freq_new;

		/* get the voltage value */
		arm_volt = volt_table[cpufreq_level];
		if (arm_regulator) {
			ret = sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
			if (ret < 0) {
				ret = -EIO;
				goto out;
			}
		}

		for_each_cpu(freqs.cpu, policy->cpus)
			cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

		/* set cpu frequnecy */
		sdp_info->set_freq((u32)old_idx, (u32)cpufreq_level);

		/* for apply changed clk to subsystem */
		clk_set_rate(sdp_info->cpu_clk, freqs.new * 1000);

		for_each_cpu(freqs.cpu, policy->cpus)
			cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

out:
	mutex_unlock(&set_freq_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(sdp_cpufreq_lock);

/* This function frees locked frequency lower level */
void sdp_cpufreq_lock_free(unsigned int nId)
{
	unsigned int i;

	if (!sdp_cpufreq_init_done)
		return;

	mutex_lock(&set_cpu_freq_lock);
	g_cpufreq_lock_id &= ~(1U << nId);
	g_cpufreq_lock_val[nId] = sdp_info->min_support_idx;
	g_cpufreq_lock_level = sdp_info->min_support_idx;
	if (g_cpufreq_lock_id) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (g_cpufreq_lock_val[i] < g_cpufreq_lock_level)
				g_cpufreq_lock_level = g_cpufreq_lock_val[i];
		}
	}
	mutex_unlock(&set_cpu_freq_lock);
}
EXPORT_SYMBOL_GPL(sdp_cpufreq_lock_free);

/* This function limits frequnecy uppper level */
int sdp_cpufreq_upper_limit(unsigned int nId,
				enum cpufreq_level_index cpufreq_level)
{
	int ret = 0, old_idx = 0, i;
	unsigned int freq_old, freq_new, arm_volt;
	unsigned int *volt_table;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	unsigned int min_idx;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	if (!sdp_info)
		return -EPERM;

	if (sdp_cpufreq_disable) {
		pr_info("CPUFreq is already fixed\n");
		return -EPERM;
	}

	/* for unlimited control in TMU */
	if (nId == DVFS_LOCK_ID_TMU || nId == DVFS_LOCK_ID_USER)
		min_idx = sdp_info->min_real_idx;
	else
		min_idx = sdp_info->min_support_idx;

	if (cpufreq_level < sdp_info->max_support_idx
			|| cpufreq_level > min_idx) {
		pr_err("%s: invalid cpufreq_level(%d:%d)\n", __func__, nId,
				cpufreq_level);
		return -EINVAL;
	}

	policy = cpufreq_cpu_get(0);
	if (!policy)
		return -EPERM;

	volt_table = sdp_info->volt_table;
	freq_table = sdp_info->freq_table;

	mutex_lock(&set_cpu_freq_lock);
	if (g_cpufreq_limit_id & (1U << nId)) {
		pr_err("[CPUFREQ]This device [%d] already limited cpufreq\n", nId);
		mutex_unlock(&set_cpu_freq_lock);
		return 0;
	}

	g_cpufreq_limit_id |= (1U << nId);
	g_cpufreq_limit_val[nId] = cpufreq_level;

	/* If the requested limit level is lower than current value */
	if (cpufreq_level > g_cpufreq_limit_level)
		g_cpufreq_limit_level = cpufreq_level;

	mutex_unlock(&set_cpu_freq_lock);

	mutex_lock(&set_freq_lock);
	/* If cur frequency is higher than limit freq, it needs to update */
	freq_old = policy->cur;
	freq_new = freq_table[cpufreq_level].frequency;
	if (freq_old > freq_new) {
		/* Find out current level index */
		for (i = 0; i <= (int)min_idx; i++) {
			if (freq_old == freq_table[i].frequency) {
				old_idx = (int)freq_table[i].index;
				break;
			} else if (i == (int)min_idx) {
				printk(KERN_ERR "%s: Level is not found\n", __func__);
				mutex_unlock(&set_freq_lock);

				return -EINVAL;
			} else {
				continue;
			}
		}
		freqs.old = freq_old;
		freqs.new = freq_new;

		for_each_cpu(freqs.cpu, policy->cpus)
			cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

		/* set cpu frequency */
		sdp_info->set_freq((u32)old_idx, (u32)cpufreq_level);

		/* for apply changed clk to subsystem */
		clk_set_rate(sdp_info->cpu_clk, freqs.new * 1000);

		arm_volt = volt_table[cpufreq_level];
		if (arm_regulator)
			sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);

		for_each_cpu(freqs.cpu, policy->cpus)
			cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	mutex_unlock(&set_freq_lock);

	return ret;
}

/* This function frees upper limit */
void sdp_cpufreq_upper_limit_free(unsigned int nId)
{
	unsigned int i;
	struct cpufreq_frequency_table *freq_table = sdp_info->freq_table;
	unsigned int *volt_table = sdp_info->volt_table;
	struct cpufreq_policy *policy;
	unsigned int old_index = UINT_MAX;
	unsigned int arm_volt;
	int ret = 0;

	if (!sdp_cpufreq_init_done)
		return;

	mutex_lock(&set_cpu_freq_lock);
	
	g_cpufreq_limit_id &= ~(1U << nId);
	g_cpufreq_limit_val[nId] = sdp_info->max_support_idx;
	g_cpufreq_limit_level = sdp_info->max_support_idx;

	/* find lowest frequency */
	if (g_cpufreq_limit_id) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (g_cpufreq_limit_val[i] > g_cpufreq_limit_level)
				g_cpufreq_limit_level = g_cpufreq_limit_val[i];
		}
	}

	mutex_unlock(&set_cpu_freq_lock);

	mutex_lock(&set_freq_lock);

	/* set CPU frequency to lowest one */
	policy = cpufreq_cpu_get(0);
	if (policy == NULL) {
		printk(KERN_ERR "%s - policy is NULL\n", __func__);
		goto out;
	}
		
	freqs.old = policy->cur;
	freqs.new = freq_table[g_cpufreq_limit_level].frequency;
	freqs.cpu = policy->cpu;

	arm_volt = volt_table[g_cpufreq_limit_level];

	/* When the new frequency is higher than current frequency */
	if ((freqs.new > freqs.old) && arm_regulator) {
		/* Firstly, voltage up to increase frequency */
		ret = sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
		if (ret < 0)
			goto out;
	}

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	if (freqs.new != freqs.old)
		sdp_info->set_freq(old_index, g_cpufreq_limit_level);

	/* for apply changed clk to subsystem */
	clk_set_rate(sdp_info->cpu_clk, freqs.new * 1000);

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if ((freqs.new < freqs.old) && arm_regulator) {
		/* down the voltage after frequency change */
		sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
	}
out:	
	mutex_unlock(&set_freq_lock);
}

/* This API serve highest priority level locking */
int sdp_cpufreq_level_fix(unsigned int freq)
{
	struct cpufreq_policy *policy;
	int ret = 0;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	policy = cpufreq_cpu_get(0);
	if (!policy)
		return -EPERM;

	if (sdp_cpufreq_disable) {
		pr_info("CPUFreq is already fixed\n");
		return -EPERM;
	}

	/* convert to 10MHz scale */
	freq = (freq / 10000) * 10000;
	
	ret = sdp_target(policy, freq, CPUFREQ_RELATION_L);

	sdp_cpufreq_disable = true;
	return ret;

}
EXPORT_SYMBOL_GPL(sdp_cpufreq_level_fix);

void sdp_cpufreq_level_unfix(void)
{
	if (!sdp_cpufreq_init_done)
		return;

	sdp_cpufreq_disable = false;
}
EXPORT_SYMBOL_GPL(sdp_cpufreq_level_unfix);

int sdp_cpufreq_is_fixed(void)
{
	return sdp_cpufreq_disable;
}
EXPORT_SYMBOL_GPL(sdp_cpufreq_is_fixed);

bool sdp_cpufreq_is_cpufreq_on(void)
{
	return sdp_user_cpufreq_on;
}
/* This function sets dvfs on/off for sysfs interface */
void sdp_cpufreq_set_cpufreq_on(bool on)
{
	struct cpufreq_policy *policy;
	int i, timeout = 100;
	
	printk(KERN_DEBUG "cpufreq on = %u\n", on);

	mutex_lock(&cpufreq_on_lock);	

	if (on == true) { /* ON */
		if (sdp_cpufreq_on) {
			printk(KERN_INFO "%s - cpufreq already ON\n", __FUNCTION__);
			goto out;
		}

		sdp_cpufreq_on = true;
		printk(KERN_INFO "DVFS ON\n");
	} else { /* OFF */
		if (!sdp_cpufreq_on) {
			printk(KERN_INFO "%s - cpufreq already OFF\n", __FUNCTION__);
			goto out;
		}
		
		policy = cpufreq_cpu_get(0);
		if (policy == NULL) {
			printk(KERN_ERR "%s - policy is NULL\n", __func__);
			goto out;
		}
		
		/* frequency to max */
		for (i = 0; i < timeout; i++) {
			if (!sdp_target(policy, policy->max, CPUFREQ_RELATION_H))
				break;
			printk(KERN_WARNING "retry frequnecy setting.\n");
			msleep(10);
		}
		if (i == timeout)
			printk(KERN_WARNING "%s - frequnecy set time out!!\n", __FUNCTION__);
		
		sdp_cpufreq_on = false;
		printk(KERN_INFO "DVFS OFF\n");
	}

out:
	mutex_unlock(&cpufreq_on_lock);
	return;
}

/* This function set asv on/off for sysfs interface */
void sdp_cpufreq_set_asv_on(bool on)
{
	struct cpufreq_policy *policy;
	int arm_volt, index;
	struct cpufreq_frequency_table *freq_table = sdp_info->freq_table;
	unsigned int *volt_table = sdp_info->volt_table;
	
	printk(KERN_DEBUG "cpu avs on = %d\n", on);

	policy = cpufreq_cpu_get(0);
	if (!policy) {
		pr_err("Failed to get cpufreq_policy\n");
		return;
	}

	/* check input error */
	if (on == true) { /* ON */
		if (sdp_asv_on) {
			printk(KERN_INFO "CPU AVS already ON\n");
			goto out;
		}
	} else { /* OFF */
		if (!sdp_asv_on) {
			printk(KERN_INFO "CPU AVS already OFF\n");
			goto out;
		}
	}
			
	mutex_lock(&set_freq_lock);

	if (on == true) { /* ON */
		/* restore ASV index */
		printk(KERN_INFO "restore CPU ASV grp%d\n", sdp_asv_stored_result);
		sdp_result_of_asv = sdp_asv_stored_result;
	} else { /* OFF */
		/* select ASV1(typical voltage table) */
		printk(KERN_INFO "select CPU ASV grp0(typical fix)\n");
		sdp_result_of_asv = 0;
	}

	/* change voltage table */
	if (sdp_info->update_volt_table)
		sdp_info->update_volt_table();
	else
		printk(KERN_ERR "ERR: cpufreq update_volt_table is NULL\n");

	/* change voltage */
	/* get current freq index */
	if (cpufreq_frequency_table_target(policy, freq_table,
				   policy->cur, CPUFREQ_RELATION_L, &index)) {
		printk(KERN_ERR "AVS : get cpufreq table index err.\n");
		mutex_unlock(&set_freq_lock);
		goto out;
	}
	printk(KERN_INFO "cur freq=%d, idx=%d, volt=%duV", policy->cur, index, volt_table[index]);
	
	arm_volt = (int)volt_table[index];
	if (arm_regulator)
		sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)arm_volt);
	
	/* store result */
	if (on == 1) { /* ON*/
		sdp_asv_on = true;
		printk(KERN_INFO "CPU AVS ON\n");
	} else { /* OFF */
		sdp_asv_on = false;
		printk(KERN_INFO "CPU AVS OFF\n");
	}

	mutex_unlock(&set_freq_lock);

out:
	return;
}

/*
 * arm regulator voltage management function.
 * This function manages two different core(CPU & GPU)'s
 * request that regulator voltage setting.
 * id : voltage setter (CPU or GPU)
 * uV : voltage (micro volt)
 */
int sdp_cpufreq_set_voltage(enum cpufreq_volt_id id, int uV)
{
	static int prev_uV[DVFS_VOLT_ID_END] = {0, };
	int volt;
	int ret;

	if (!arm_regulator)
		return 0;

	mutex_lock(&set_volt_lock);
	
	/* store each buffer */
	prev_uV[id] = uV;

	/* select max voltage */
	volt = max(prev_uV[DVFS_VOLT_ID_CPU], prev_uV[DVFS_VOLT_ID_GPU]);
	
	ret = regulator_set_voltage(arm_regulator, volt, volt + 10000);

	mutex_unlock(&set_volt_lock);
	
	return ret;
}
EXPORT_SYMBOL(sdp_cpufreq_set_voltage);

#ifdef CONFIG_PM
static int sdp_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int sdp_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#else
#define sdp_cpufreq_suspend NULL
#define sdp_cpufreq_resume	NULL
#endif

/* For notifier */
static int sdp_cpufreq_policy_notifier_call(struct notifier_block *this,
				unsigned long code, void *data)
{
	struct cpufreq_policy *policy = data;

	switch (code) {
	case CPUFREQ_ADJUST:
		if ((!strnicmp(policy->governor->name, "powersave", CPUFREQ_NAME_LEN))
		|| (!strnicmp(policy->governor->name, "performance", CPUFREQ_NAME_LEN))
		|| (!strnicmp(policy->governor->name, "userspace", CPUFREQ_NAME_LEN))) {
			printk(KERN_DEBUG "cpufreq governor is changed to %s\n",
							policy->governor->name);
			sdp_cpufreq_lock_disable = true;
		} else
			sdp_cpufreq_lock_disable = false;

	case CPUFREQ_INCOMPATIBLE:
	case CPUFREQ_NOTIFY:
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block sdp_cpufreq_policy_notifier = {
	.notifier_call = sdp_cpufreq_policy_notifier_call,
};

static int sdp_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->cur = policy->min = policy->max = sdp_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(sdp_info->freq_table, policy->cpu);

	/* set the transition latency value (ns) */
	policy->cpuinfo.transition_latency = 100000; /* 100 us for pll change */

	/*
	 * SDP multi-core processors has 2 or 4 cores
	 * that the frequency cannot be set independently.
	 * Each cpu is bound to the same speed.
	 * So the affected cpu is all of the cpus.
	 */
	if (num_online_cpus() == 1) {
		cpumask_copy(policy->related_cpus, cpu_possible_mask);
		cpumask_copy(policy->cpus, cpu_online_mask);
	} else {
		cpumask_setall(policy->cpus);
	}

	return cpufreq_frequency_table_cpuinfo(policy, sdp_info->freq_table);
}

#ifndef CONFIG_SDP_THERMAL
/* This function applies AVS */
static int sdp_cpufreq_apply_avs(void)
{
	int ret = 0;
	int retry = 3;

	if (!arm_regulator) {
		printk(KERN_INFO "DVFS: can't apply avs. arm_regultor is NULL\n");
		return 0;
	}
	
	/* apply AVS */
	while (retry) {
		ret = sdp_cpufreq_set_voltage(DVFS_VOLT_ID_CPU, (int)sdp_info->volt_table[sdp_info->max_support_idx]);

		if (ret >= 0) {
			break;
		}
		retry--;
	}

	if (retry == 0) {
		printk(KERN_INFO "DVFS: ERROR - apply AVS - CPU voltage setting error.\n");
		ret = -EIO;
	} else {
		printk(KERN_INFO "DVFS: apply AVS - group%d %duV\n", sdp_result_of_asv,
						sdp_info->volt_table[sdp_info->max_support_idx]);
		ret = 0;
	}

	return ret;
}
#endif

/*******************/
/* SYSFS interface */

/* cpufreq fix */
static ssize_t sdp_cpufreq_freqfix_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	struct cpufreq_policy *policy;
	ssize_t ret;

	policy = cpufreq_cpu_get(0);
	if (!policy)
		return -EPERM;

	if (sdp_cpufreq_is_fixed())
		ret = sprintf(buf, "%d\n", policy->cur);
	else
		ret = sprintf(buf, "0\n");

	return ret;
}

static ssize_t sdp_cpufreq_freqfix_store(struct kobject *a, struct attribute *b,
			 							const char *buf, size_t count)
{
	unsigned int freq;
	int ret;

	if (!sdp_cpufreq_on) {
		printk(KERN_ERR "%s : cpufreq_on must be turned on.\n", __func__);
		return -EPERM;
	}

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	/* must unfix before frequency fix */
	sdp_cpufreq_level_unfix();

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, cpufreq_fix\n", freq);
		sdp_cpufreq_level_fix(freq);
	} else {
		printk(KERN_DEBUG "freq=%u, cpufreq unfix\n", freq);
	}
	
	return (ssize_t)count;
}
static struct global_attr frequency = __ATTR(frequency, 0644, sdp_cpufreq_freqfix_show, sdp_cpufreq_freqfix_store);

/* cpufreq on/off */
static ssize_t sdp_cpufreq_on_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = sprintf(buf, "%d\n", sdp_cpufreq_on);

	return ret;
}

static ssize_t sdp_cpufreq_on_store(struct kobject *a, struct attribute *b,
			 							const char *buf, size_t count)
{
	unsigned int on;
	int ret;
	struct cpufreq_policy *policy;
	int i, timeout = 10;
	
	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	printk(KERN_DEBUG "cpufreq on = %u\n", on);

	mutex_lock(&cpufreq_on_lock);

	if (on == 1) { /* ON */
		if (sdp_cpufreq_on) {
			printk(KERN_INFO "cpufreq already ON\n");
			goto out;
		}

		/* store user setting */
		sdp_user_cpufreq_on = true;
		
		sdp_cpufreq_on = true;
	} else if (on == 0) { /* OFF */
		if (!sdp_cpufreq_on) {
			printk(KERN_INFO "cpufreq already OFF\n");
			goto out;
		}

		policy = cpufreq_cpu_get(0);
		if (policy == NULL) {
			printk(KERN_ERR "%s - policy is NULL\n", __func__);
			goto out;
		}
		
		/* frequency to max */
		for (i = 0; i < timeout; i++) {
			if (!sdp_target(policy, policy->max, CPUFREQ_RELATION_H))
				break;
			printk(KERN_WARNING "retry frequnecy setting.\n");
			msleep(10);
		}
		if (i == timeout)
			printk(KERN_WARNING "frequnecy set time out!!\n");
		
		sdp_cpufreq_on = false;
		sdp_user_cpufreq_on = false;
	} else {
		printk(KERN_ERR "%s: ERROR - input 0 or 1\n", __func__);
	}

out:
	mutex_unlock(&cpufreq_on_lock);
	return (ssize_t)count;
}
static struct global_attr cpufreq_on = __ATTR(cpufreq_on, 0644, sdp_cpufreq_on_show, sdp_cpufreq_on_store);

/* AVS on/off */
static ssize_t sdp_asv_on_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = sprintf(buf, "%d\n", sdp_asv_on);

	return ret;
}
static struct global_attr avs_on = __ATTR(avs_on, 0444, sdp_asv_on_show, NULL);

/* show AVS volt table */
static ssize_t sdp_asv_volt_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i;
	unsigned int * volt_table = sdp_info->volt_table;

	for (i = (int)sdp_info->max_support_idx; i <= (int)sdp_info->min_support_idx; i++)
		printk(KERN_INFO "group-%d, [%d] %duV\n", sdp_result_of_asv, i, volt_table[i]);		

	return ret;
}
static struct global_attr avs_table = __ATTR(avs_table, 0444, sdp_asv_volt_show, NULL);

/* frequency limitation */
static ssize_t sdp_cpufreq_freqlimit_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (g_cpufreq_limit_id & (1<<DVFS_LOCK_ID_USER)) {
		freq = sdp_info->freq_table[g_cpufreq_limit_val[DVFS_LOCK_ID_USER]].frequency;
		ret = sprintf(buf, "%d\n", freq);
	} else {
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t sdp_cpufreq_freqlimit_store(struct kobject *a, struct attribute *b,
			 							const char *buf, size_t count)
{
	unsigned int freq;
	int ret;
	unsigned int level;

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (freq != 0)
		sdp_cpufreq_get_level(freq, &level);
	//printk("freq=%u, level=%u\n", freq, level);

	if (g_cpufreq_limit_id & (1<<DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlimit\n", freq);
		sdp_cpufreq_upper_limit_free(DVFS_LOCK_ID_USER);
	}

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, freq limit\n", freq);
		sdp_cpufreq_upper_limit(DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "freq=%u, freq unlimit\n", freq);
		sdp_cpufreq_upper_limit_free(DVFS_LOCK_ID_USER);
	}
	
	return (ssize_t)count;
}
static struct global_attr freq_limit = __ATTR(freq_limit, 0644, sdp_cpufreq_freqlimit_show, sdp_cpufreq_freqlimit_store);

/* frequency lock */
static ssize_t sdp_cpufreq_freqlock_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (g_cpufreq_lock_id & (1<<DVFS_LOCK_ID_USER)) {
		freq = sdp_info->freq_table[g_cpufreq_lock_val[DVFS_LOCK_ID_USER]].frequency;
		ret = sprintf(buf, "%d\n", freq);
	} else {
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t sdp_cpufreq_freqlock_store(struct kobject *a, struct attribute *b,
			 							const char *buf, size_t count)
{
	unsigned int freq;
	int ret;
	unsigned int level;

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (freq != 0)
		sdp_cpufreq_get_level(freq, &level);
	//printk("freq=%u, level=%u\n", freq, level);

	if (g_cpufreq_lock_id & (1<<DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(DVFS_LOCK_ID_USER);
	}

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, freq lock\n", freq);
		sdp_cpufreq_lock(DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(DVFS_LOCK_ID_USER);
	}
	
	return (ssize_t)count;
}
static struct global_attr freq_lock = __ATTR(freq_lock, 0644, sdp_cpufreq_freqlock_show, sdp_cpufreq_freqlock_store);

/* DVFS voltage table update from Filesystem */
static ssize_t sdp_cpufreq_voltupdate_show(struct kobject *kobj,
								struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = sprintf(buf, "0\n");

	return ret;
}

#ifdef CONFIG_SDP_AVS
	unsigned int g_volt_table[LMAX][MAX_CPU_ASV_GROUP];
#else
	unsigned int g_volt_table[LMAX][10];
#endif
static ssize_t sdp_cpufreq_voltupdate_store(struct kobject *a, struct attribute *b,
			 							const char *buf, size_t count)
{
	int i, j;
	size_t size, read_cnt = 0;
	char atoi_buf[15];
	char temp;
	int line_cnt = 0, char_cnt;
	bool started = 0, loop = 1;

	/* store to memory */
	memset(g_volt_table, 0, sizeof(g_volt_table));
	
	size = count;//filp->f_dentry->d_inode->i_size;

	i = 0;
	j = 0;
	char_cnt = 0;
	while (size > read_cnt && loop) {
		/* get 1 byte */
		temp = buf[read_cnt++];
		
		/* find 'S' */
		if (started == 0 && temp == 'S') {
			/* find '\n' */
			while (size > read_cnt) {
				temp = buf[read_cnt++];
				if (temp == '\n') {
					started = 1;
					break;
				}
			}
			continue;
		}

		if (started == 0)
			continue;

		/* check volt table line count */
		if (i > (sdp_info->min_real_idx - sdp_info->max_support_idx + 1)) {
			printk(KERN_ERR "cpufreq ERR: volt table line count is more than %d, i = %d\n",
					sdp_info->min_real_idx - sdp_info->max_support_idx + 1, i);
			goto out;
		}

		/* check volt table column count */
#ifdef CONFIG_SDP_AVS
		if (j > MAX_CPU_ASV_GROUP) {
			printk(KERN_ERR "cpufreq ERR: volt table column count is more than %d, j = %d\n", MAX_CPU_ASV_GROUP, j);
#else
		if (j > 10) {
			printk(KERN_ERR "cpufreq ERR: volt table column count is more than 10, j = %d\n", j);
#endif
			goto out;
		}

		/* parsing */
		switch (temp) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				atoi_buf[char_cnt] = temp;
				char_cnt++;
				break;

			case ',':
				atoi_buf[char_cnt++] = 0;
#ifdef CONFIG_SDP_AVS
				if (j >= MAX_CPU_ASV_GROUP)
					break;
#else
				if (j >= 10)
					break;
#endif
				g_volt_table[i][j] = (unsigned int)simple_strtoul(atoi_buf, (char **)&atoi_buf, 0);
				//printk("g_volt_table[%d][%d]=%u\n", i, j, g_volt_table[i][j]);
				j++;
				char_cnt = 0;
				break;

			case '\n':
				//printk("meet LF\n");
				i++;
				j = 0;
				break;
			
			case 'E':
				loop = 0;
				line_cnt = i;
				//printk("meet END, line_cnt = %d\n", line_cnt);
				break;

			default:
				break;
		}
	}

	/* check line count */
	if (line_cnt != (sdp_info->min_real_idx - sdp_info->max_support_idx + 1)) {
		printk(KERN_ERR "cpufreq ERR: volt table line count is not %d\n",
			sdp_info->min_real_idx - sdp_info->max_support_idx + 1);
	
		goto out;
	}

	/* change current volt table */
	printk(KERN_INFO "> DVFS volt table change\n");
	for (i = sdp_info->max_support_idx, j = 0; i <= sdp_info->min_real_idx; i++, j++) {
		printk(KERN_INFO "group-%d, [%d] %uuV -> %uuV\n", 
			sdp_result_of_asv, i, sdp_info->volt_table[i], g_volt_table[j][sdp_result_of_asv]);
		sdp_info->volt_table[i] = g_volt_table[j][sdp_result_of_asv];
	}
	printk(KERN_INFO "> DONE\n");
	
out:

	return (ssize_t)count;
}
static struct global_attr volt_update = __ATTR(volt_update, 0644, sdp_cpufreq_voltupdate_show, sdp_cpufreq_voltupdate_store);

static struct attribute *dbs_attributes[] = {
	&frequency.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "freqfix",
};

static int sdp_cpufreq_dev_register(void)
{
	int err;

	err = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&cpufreq_on.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&avs_on.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&avs_table.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&freq_limit.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&freq_lock.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, 
						&volt_update.attr);
	
out:
	return err;
}
/* SYSFS interface end */
/***********************/

static struct cpufreq_driver sdp_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= sdp_verify_speed,
	.target		= sdp_target,
	.get		= sdp_getspeed,
	.init		= sdp_cpufreq_cpu_init,
	.name		= "sdp_cpufreq",
	.suspend	= sdp_cpufreq_suspend,
	.resume		= sdp_cpufreq_resume,
};

static int __init sdp_cpufreq_init(void)
{
	int ret = -EINVAL;
	int i;

	sdp_info = kzalloc(sizeof(struct sdp_dvfs_info), GFP_KERNEL);
	if (!sdp_info) {
		pr_err("DVFS: fail to allocate sdp_info\n");
		return -ENOMEM;
	}

#ifdef CONFIG_ARM_SDP1304_CPUFREQ
	if (of_machine_is_compatible("samsung,sdp1304"))
		ret = sdp1304_cpufreq_init(sdp_info);
#endif
#ifdef CONFIG_ARM_SDP1302_CPUFREQ
	if (of_machine_is_compatible("samsung,sdp1302"))
		ret = sdp1302_cpufreq_init(sdp_info);
#endif
#ifdef CONFIG_ARM_SDP1307_CPUFREQ
	if (of_machine_is_compatible("samsung,sdp1307"))
		ret = sdp1307_cpufreq_init(sdp_info);
#endif
	if (ret) {
		printk(KERN_ERR "cpufreq_init error : %d\n", ret);
		goto err;
	}

	if (sdp_info->set_freq == NULL) {
		printk(KERN_ERR "%s: No set_freq function (ERR)\n",	__func__);
		goto err;
	}

	if (sdp_info->update_volt_table == NULL) {
		printk(KERN_ERR "%s: No update_volt_table function (ERR)\n", __func__);
		goto err;
	}

	arm_regulator = regulator_get(NULL, "CPU_PW");
	if (IS_ERR(arm_regulator)) {
		printk(KERN_ERR "failed to get resource %s\n", "CPU_PW");
		arm_regulator = NULL;
	}

	/* default settings */
	/* dvfs off, freq fix off */
	sdp_cpufreq_on = false;
	sdp_cpufreq_disable = false;
	sdp_asv_on = true;

	cpufreq_register_notifier(&sdp_cpufreq_policy_notifier,
						CPUFREQ_POLICY_NOTIFIER);

	sdp_cpufreq_init_done = true;

	for (i = 0; i < DVFS_LOCK_ID_END; i++) {
		g_cpufreq_lock_val[i] = sdp_info->min_support_idx;
		g_cpufreq_limit_val[i] = sdp_info->max_support_idx;
	}

	g_cpufreq_lock_level = sdp_info->min_support_idx;
	g_cpufreq_limit_level = sdp_info->max_support_idx;

	if (cpufreq_register_driver(&sdp_driver)) {
		pr_err("failed to register cpufreq driver\n");
		goto err_cpufreq;
	}

	/* sysfs register*/
	ret = sdp_cpufreq_dev_register();
	if (ret < 0)
		pr_err("failed to register sysfs device\n");

	/* aplly avs */
#ifdef CONFIG_SDP_THERMAL
	/* TMU will on/off AVS */
	sdp_cpufreq_set_asv_on(false);
	sdp_tmu_register_cpu_avs(sdp_cpufreq_set_asv_on);
#else
	ret = sdp_cpufreq_apply_avs();
	if (ret < 0)
		pr_err("failed to apply avs\n");
#endif

	return 0;
	
err_cpufreq:
	if (!IS_ERR(arm_regulator))
		regulator_put(arm_regulator);
err:
	kfree(sdp_info);
	pr_err("%s: failed initialization\n", __func__);
	return -EINVAL;
}
late_initcall(sdp_cpufreq_init);

