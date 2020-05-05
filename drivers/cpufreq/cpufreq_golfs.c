/*
 *  drivers/cpufreq/cpufreq_golfs.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/sysfs.h>
#include <linux/tick.h>
#include <linux/types.h>
#include <linux/slab.h>

#include "cpufreq_governor.h"

/* On-demand governor macors */
#define DEF_FREQUENCY_UP_THRESHOLD		(90)
#define DEF_FREQUENCY_DOWN_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_UP_THRESHOLD		(90)
#define MICRO_FREQUENCY_DOWN_THRESHOLD		(80)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(30000) /* 30ms */
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)
#define MIN_FREQUENCY_DOWN_THRESHOLD		(1)
#define MAX_FREQUENCY_DOWN_THRESHOLD		(100)

#define DEF_FREQUENCY_UP_STEP_ZONE		(400000)
#define DEF_FREQUENCY_DOWN_STEP_ZONE		(300000)
#define DEF_FREQUENCY_UP_THRESHOLD_L		(35)
#define DEF_FREQUENCY_UP_STEP_LEVEL_L		(800000)
#define DEF_FREQUENCY_DOWN_STEP_LEVEL_L		(300000)
#define DEF_FREQUENCY_DOWN_THRESHOLD_L		(25)

static struct dbs_data od_dbs_data;
static DEFINE_PER_CPU(struct od_cpu_dbs_info_s, od_cpu_dbs_info);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_GOLFS
static struct cpufreq_governor cpufreq_gov_golfs;
#endif

static struct od_dbs_tuners od_tuners = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.down_threshold = DEF_FREQUENCY_DOWN_THRESHOLD,
	.up_threshold_l = DEF_FREQUENCY_UP_THRESHOLD_L,
	.up_step_level_l = DEF_FREQUENCY_UP_STEP_LEVEL_L,
	.down_threshold_l = DEF_FREQUENCY_DOWN_THRESHOLD_L,
	
	.ignore_nice = 0,
	.powersave_bias = 0,
};

/* dvfs log */
static void add_to_log_buf(char *buf, int len);
#define dvfs_print(arg...) \
		do { \
			int len; \
			char buf[100]; \
			unsigned long long t; \
			unsigned long nanosec_rem; \
			t = cpu_clock(0); \
			nanosec_rem = do_div(t, 1000000000); \
			len = sprintf(buf, "[%5lu.%06lu] ",	(unsigned long) t, nanosec_rem / 1000); \
			add_to_log_buf(buf, len); \
			len = sprintf(buf, arg); \
			add_to_log_buf(buf, len); \
		} while(0)
static char *dvfs_log_buf;
static int log_buf_start;
static int log_buf_end;
static const int log_buf_size = SZ_16K;
static struct mutex log_mutex;
static void add_to_log_buf(char *buf, int len)
{
	int diff;

	mutex_lock(&log_mutex);

	//printk("len = %d\n", len);
	if (log_buf_end > log_buf_start)
		diff = log_buf_end - log_buf_start;
	else 
		diff = log_buf_size - log_buf_start + log_buf_end;

	/*printk("filled size = %d\n", diff);*/

	if (log_buf_size - diff < len) {
		log_buf_start = log_buf_end + len;
		if (log_buf_start >= log_buf_size)
			log_buf_start -= log_buf_size;
	}
	//printk("log_buf_start = %d\n", log_buf_start);
	
	/* copy to log buffer */
	if ((log_buf_end + len) < log_buf_size) {
		strncpy(&dvfs_log_buf[log_buf_end], buf, len);
		log_buf_end += len;
	} else {
		strncpy(&dvfs_log_buf[log_buf_end], buf, log_buf_size - log_buf_end);
		strncpy(dvfs_log_buf, &buf[log_buf_size - log_buf_end], len - (log_buf_size - log_buf_end));
		log_buf_end = (log_buf_end + len) - log_buf_size;
	}

	//printk("copied. end = %d\n", log_buf_end);

	mutex_unlock(&log_mutex);	
}

static int cpufreq_gov_log_init(void)
{
	dvfs_log_buf = kzalloc(log_buf_size + 16, GFP_ATOMIC);
	if (!dvfs_log_buf) {
		printk(KERN_ERR "cpufreq - %s : dvfs log buff allocation fail\n", __func__);
		return -ENOMEM;
	}

	log_buf_start = 0;
	log_buf_end = 0;

	mutex_init(&log_mutex);
		
	return 0;
}

static int cpufreq_gov_log_exit(void)
{
	mutex_destroy(&log_mutex);
	
	kfree(dvfs_log_buf);

	return 0;
}

static void golf_powersave_bias_init_cpu(int cpu)
{
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, cpu);

	dbs_info->freq_table = cpufreq_frequency_get_table(cpu);
	dbs_info->freq_lo = 0;
}

/*
 * Not all CPUs want IO time to be accounted as busy; this depends on how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (androidlcom) calis this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
#if defined(CONFIG_X86)
	/*
	 * For Intel, Core 2 (model 15) andl later have an efficient idle.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
			boot_cpu_data.x86 == 6 &&
			boot_cpu_data.x86_model >= 15)
		return 1;
#endif
	return 0;
}

/*
 * Find right freq to be set now with powersave_bias on.
 * Returns the freq_hi to be used right now and will set freq_hi_jiffies,
 * freq_lo, and freq_lo_jiffies in percpu area for averaging freqs.
 */
static unsigned int powersave_bias_target(struct cpufreq_policy *policy,
		unsigned int freq_next, unsigned int relation)
{
	unsigned int freq_req, freq_reduc, freq_avg;
	unsigned int freq_hi, freq_lo;
	unsigned int index = 0;
	unsigned int jiffies_total, jiffies_hi, jiffies_lo;
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info,
						   policy->cpu);

	if (!dbs_info->freq_table) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_next;
	}

	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_next,
			relation, &index);
	freq_req = dbs_info->freq_table[index].frequency;
	freq_reduc = freq_req * od_tuners.powersave_bias / 1000;
	freq_avg = freq_req - freq_reduc;

	/* Find freq bounds for freq_avg in freq_table */
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_H, &index);
	freq_lo = dbs_info->freq_table[index].frequency;
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_L, &index);
	freq_hi = dbs_info->freq_table[index].frequency;

	/* Find out how long we have to be in hi and lo freqs */
	if (freq_hi == freq_lo) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_lo;
	}
	jiffies_total = usecs_to_jiffies(od_tuners.sampling_rate);
	jiffies_hi = (freq_avg - freq_lo) * jiffies_total;
	jiffies_hi += ((freq_hi - freq_lo) / 2);
	jiffies_hi /= (freq_hi - freq_lo);
	jiffies_lo = jiffies_total - jiffies_hi;
	dbs_info->freq_lo = freq_lo;
	dbs_info->freq_lo_jiffies = jiffies_lo;
	dbs_info->freq_hi_jiffies = jiffies_hi;
	return freq_hi;
}

static void golf_powersave_bias_init(void)
{
	int i;
	for_each_online_cpu(i) {
		golf_powersave_bias_init_cpu(i);
	}
}

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
	int ret;
	
	if (od_tuners.powersave_bias)
		freq = powersave_bias_target(p, freq, CPUFREQ_RELATION_H);
	else if (p->cur == p->max)
		return;

	ret = __cpufreq_driver_target(p, freq, od_tuners.powersave_bias ?
			CPUFREQ_RELATION_L : CPUFREQ_RELATION_H);
	if (ret == -EIO)
		p->cur = cpufreq_get(0);
}

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency Every sampling_rate, we look for
 * a the lowest frequency which can sustain the load while keeping idle time
 * over 30%. If such a frequency exist, we try to decrease to this frequency.
 *
 * Any frequency increase takes it to the maximum frequency. Frequency reduction
 * happens at minimum steps of 5% (default) of current frequency
 */
static void od_check_cpu(int cpu, unsigned int load_freq)
{
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	int ret;
	
	dbs_info->freq_lo = 0;

	dvfs_print("[cpu] curfreq=%d, load=%d%%\n", policy->cur, load_freq / policy->cur);

	/* Check for frequency increase */
	if (policy->cur < DEF_FREQUENCY_UP_STEP_ZONE) {
		/*
		 * if cur freq < 400MHz, and load freq is bigger than
		 * up_threshold_l 35%, must visit 800MHz.
		 */
		if (load_freq > od_tuners.up_threshold_l * policy->cur) {
			dbs_freq_increase(policy, od_tuners.up_step_level_l);
			dvfs_print("increase step level (%dMHz)\n", od_tuners.up_step_level_l/1000);
			return;
		}
	} else if (policy->cur >= DEF_FREQUENCY_UP_STEP_ZONE &&
		policy->cur < od_tuners.up_step_level_l) {
		/*
		 * if cur freq >= 400MHz and < 800MHz, must visit 800MHz.
		 * up_threshold is 80%
		 */
		if (load_freq > od_tuners.up_threshold * policy->cur) {
			dbs_freq_increase(policy, od_tuners.up_step_level_l);
			dvfs_print("increase step level (%dMHz)\n", od_tuners.up_step_level_l/1000);
			return;
		}
	} else {
		/*
		 * if cur freq >= 800MHz, go to max freq.
		 * up_threshold is 80%
		 */
		if (load_freq > od_tuners.up_threshold * policy->cur) {
			/* If switching to max speed, apply sampling_down_factor */
			if (policy->cur < policy->max)
				dbs_info->rate_mult =
					od_tuners.sampling_down_factor;
			dbs_freq_increase(policy, policy->max);
			dvfs_print("increase max (%dMHz)\n", policy->max/1000);
			return;
		}
	}

	/* Check for frequency decrease */
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		return;

	/*
	 * The optimal frequency is the frequency that is the lowest that can
	 * support the current CPU usage without triggering the up policy. To be
	 * safe, we focus 10 points under the threshold.
	 */
	 if (policy->cur > DEF_FREQUENCY_DOWN_STEP_LEVEL_L) {
	 	/* if cur freq > 300MHz, apply down_threshold 80%. */
		if (load_freq < od_tuners.down_threshold * policy->cur) {
			unsigned int freq_next;

			freq_next = load_freq / od_tuners.down_threshold;
			
			/* if freq_next < 300, must visit 300MHz */
			if (freq_next < DEF_FREQUENCY_DOWN_STEP_ZONE)
				freq_next = DEF_FREQUENCY_DOWN_STEP_ZONE;
			
			if (freq_next < policy->min)
				freq_next = policy->min;
			dvfs_print("decrease freq(%dMHz)\n", freq_next/1000);

			if (!od_tuners.powersave_bias) {
				ret = __cpufreq_driver_target(policy, freq_next,
							CPUFREQ_RELATION_L);
				if (ret == -EIO)
					policy->cur = cpufreq_get(0);
			} else {
				int freq = powersave_bias_target(policy, freq_next,
						CPUFREQ_RELATION_L);
				ret = __cpufreq_driver_target(policy, freq,
							CPUFREQ_RELATION_L);
				if (ret == -EIO)
					policy->cur = cpufreq_get(0);
			}
		}
	 } else {
	 	/* if cur freq <= 300MHz, down threshold 25% */
		if (load_freq < od_tuners.down_threshold_l * policy->cur) {
			unsigned int freq_next;

			freq_next = load_freq / od_tuners.down_threshold_l;
						
			if (freq_next < policy->min)
				freq_next = policy->min;
			dvfs_print("decrease freq(%dMHz)\n", freq_next/1000);

			if (!od_tuners.powersave_bias) {
				ret = __cpufreq_driver_target(policy, freq_next,
							CPUFREQ_RELATION_L);
				if (ret == -EIO)
					policy->cur = cpufreq_get(0);
			} else {
				int freq = powersave_bias_target(policy, freq_next,
						CPUFREQ_RELATION_L);
				ret = __cpufreq_driver_target(policy, freq,
							CPUFREQ_RELATION_L);
				if (ret == -EIO)
					policy->cur = cpufreq_get(0);
			}
		}
	 }
}

static void od_dbs_timer(struct work_struct *work)
{
	struct od_cpu_dbs_info_s *dbs_info =
		container_of(work, struct od_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cpu;
	int delay, sample_type = dbs_info->sample_type;

	mutex_lock(&dbs_info->cdbs.timer_mutex);

	/* Common NORMAL_SAMPLE setup */
	dbs_info->sample_type = OD_NORMAL_SAMPLE;
	if (sample_type == OD_SUB_SAMPLE) {
		delay = dbs_info->freq_lo_jiffies;
		__cpufreq_driver_target(dbs_info->cdbs.cur_policy,
				dbs_info->freq_lo, CPUFREQ_RELATION_H);
	} else {
		dbs_check_cpu(&od_dbs_data, cpu);
		if (dbs_info->freq_lo) {
			/* Setup timer for SUB_SAMPLE */
			dbs_info->sample_type = OD_SUB_SAMPLE;
			delay = dbs_info->freq_hi_jiffies;
		} else {
			delay = usecs_to_jiffies(od_tuners.sampling_rate);
		}
	}

	schedule_delayed_work_on(cpu, &dbs_info->cdbs.work, delay);
	mutex_unlock(&dbs_info->cdbs.timer_mutex);
}

/************************** sysfs interface ************************/

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", od_dbs_data.min_sampling_rate);
}

/**
 * update_sampling_rate - update sampling rate effective immediately if needed.
 * @new_rate: new sampling rate
 *
 * If new rate is smaller than the old, simply updaing
 * dbs_tuners_int.sampling_rate might not be appropriate. For example, if the
 * original sampling_rate was 1 second and the requested new sampling rate is 10
 * ms because the user needs immediate reaction from ondemand governor, but not
 * sure if higher frequency will be required or not, then, the governor may
 * change the sampling rate too late; up to 1 second later. Thus, if we are
 * reducing the sampling rate, we need to make the new value effective
 * immediately.
 */
static void update_sampling_rate(unsigned int new_rate)
{
	int cpu;

	od_tuners.sampling_rate = new_rate = max(new_rate,
			od_dbs_data.min_sampling_rate);

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct od_cpu_dbs_info_s *dbs_info;
		unsigned long next_sampling, appointed_at;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (policy->governor != &cpufreq_gov_golfs) {
			cpufreq_cpu_put(policy);
			continue;
		}
		dbs_info = &per_cpu(od_cpu_dbs_info, policy->cpu);
		cpufreq_cpu_put(policy);

		mutex_lock(&dbs_info->cdbs.timer_mutex);

		if (!delayed_work_pending(&dbs_info->cdbs.work)) {
			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			continue;
		}

		next_sampling = jiffies + usecs_to_jiffies(new_rate);
		appointed_at = dbs_info->cdbs.work.timer.expires;

		if (time_before(next_sampling, appointed_at)) {

			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			cancel_delayed_work_sync(&dbs_info->cdbs.work);
			mutex_lock(&dbs_info->cdbs.timer_mutex);

			schedule_delayed_work_on(dbs_info->cdbs.cpu,
					&dbs_info->cdbs.work,
					usecs_to_jiffies(new_rate));

		}
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
	}
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	update_sampling_rate(input);
	return count;
}

static ssize_t store_io_is_busy(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	od_tuners.io_is_busy = !!input;
	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	od_tuners.up_threshold = input;
	return count;
}

static ssize_t store_down_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_DOWN_THRESHOLD ||
			input < MIN_FREQUENCY_DOWN_THRESHOLD) {
		return -EINVAL;
	}
	od_tuners.down_threshold = input;
	return count;
}

static ssize_t store_up_threshold_l(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	od_tuners.up_threshold_l = input;
	return count;
}

static ssize_t store_down_threshold_l(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_DOWN_THRESHOLD ||
			input < MIN_FREQUENCY_DOWN_THRESHOLD) {
		return -EINVAL;
	}
	od_tuners.down_threshold_l = input;
	return count;
}


static ssize_t store_sampling_down_factor(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	od_tuners.sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info,
				j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == od_tuners.ignore_nice) { /* nothing to do */
		return count;
	}
	od_tuners.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct od_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->cdbs.prev_cpu_wall);
		if (od_tuners.ignore_nice)
			dbs_info->cdbs.prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];

	}
	return count;
}

static ssize_t store_powersave_bias(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 1000)
		input = 1000;

	od_tuners.powersave_bias = input;
	golf_powersave_bias_init();
	return count;
}

static ssize_t show_dvfs_log(struct kobject *kobj,
						struct attribute *attr, char *buf)
{
	int i;

	mutex_lock(&log_mutex);
	for (i = log_buf_start;;) {
		if (i >= log_buf_size)
			i = 0;
		printk("%c", dvfs_log_buf[i++]);
		if (i == log_buf_end)
			break;
	}
	mutex_unlock(&log_mutex);

	return sprintf(buf, " ");
}

show_one(od, sampling_rate, sampling_rate);
show_one(od, io_is_busy, io_is_busy);
show_one(od, up_threshold, up_threshold);
show_one(od, down_threshold, down_threshold);
show_one(od, up_threshold_l, up_threshold_l);
show_one(od, down_threshold_l, down_threshold_l);
show_one(od, sampling_down_factor, sampling_down_factor);
show_one(od, ignore_nice_load, ignore_nice);
show_one(od, powersave_bias, powersave_bias);

define_one_global_rw(sampling_rate);
define_one_global_rw(io_is_busy);
define_one_global_rw(up_threshold);
define_one_global_rw(down_threshold);
define_one_global_rw(up_threshold_l);
define_one_global_rw(down_threshold_l);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(powersave_bias);
define_one_global_ro(sampling_rate_min);
define_one_global_ro(dvfs_log);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&up_threshold_l.attr,
	&down_threshold_l.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&powersave_bias.attr,
	&io_is_busy.attr,
	&dvfs_log.attr,
	NULL
};

static struct attribute_group od_attr_group = {
	.attrs = dbs_attributes,
	.name = "golfs",
};

/************************** sysfs end ************************/

define_get_cpu_dbs_routines(od_cpu_dbs_info);

static struct od_ops od_ops = {
	.io_busy = should_io_be_busy,
	.powersave_bias_init_cpu = golf_powersave_bias_init_cpu,
	.powersave_bias_target = powersave_bias_target,
	.freq_increase = dbs_freq_increase,
};

static struct dbs_data od_dbs_data = {
	.governor = GOV_ONDEMAND,
	.attr_group = &od_attr_group,
	.tuners = &od_tuners,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = od_dbs_timer,
	.gov_check_cpu = od_check_cpu,
	.gov_ops = &od_ops,
};

static int od_cpufreq_governor_dbs(struct cpufreq_policy *policy,
		unsigned int event)
{
	return cpufreq_governor_dbs(&od_dbs_data, policy, event);
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_GOLFS
static
#endif
struct cpufreq_governor cpufreq_gov_golfs = {
	.name			= "golfs",
	.governor		= od_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	u64 idle_time;
	int cpu = get_cpu();

	mutex_init(&od_dbs_data.mutex);
	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		printk(KERN_INFO "DVFS: micro accounting\n");
		od_tuners.up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		od_tuners.down_threshold = MICRO_FREQUENCY_DOWN_THRESHOLD;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		od_dbs_data.min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		/* For correct statistics, we need 10 ticks for each measure */
		printk(KERN_INFO "DVFS: 10 ticks for each measure\n");
		od_dbs_data.min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
			jiffies_to_usecs(10);
	}

	cpufreq_gov_log_init();

	printk(KERN_INFO "DVFS: sample rate = %dms\n",
			od_dbs_data.min_sampling_rate/1000);

	return cpufreq_register_governor(&cpufreq_gov_golfs);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_gov_log_exit();
	
	cpufreq_unregister_governor(&cpufreq_gov_golfs);
}

MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_DESCRIPTION("'cpufreq_golf' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_GOLFS
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
