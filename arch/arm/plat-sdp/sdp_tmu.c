/* drivers/tmu/sdp_tmu.c
*
* Copyright (c) 2013 Samsung Electronics Co., Ltd.
*      http://www.samsung.com
*
* SDP - Thermal Management support
*
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/platform_data/sdp_thermal.h>
#include <linux/power/sdp_asv.h>
#include <linux/notifier.h>
#include <linux/suspend.h>

#include <asm/irq.h>

#include <plat/cpufreq.h>

/* for factory mode */
#define CONFIG_TMU_SYSFS
#define CONFIG_TMU_DEBUG

/* flags that throttling or trippint is treated */
#define THROTTLE_ZERO_FLAG (0x1 << 0)
#define THROTTLE_1ST_FLAG  (0x1 << 1)
#define THROTTLE_2ND_FLAG  (0x1 << 2)
#define THROTTLE_3RD_FLAG  (0x1 << 3)

/* for log file */
#define LONGNAME_SIZE		50
#define LOG_BUF_LEN			255

struct sdp_tmu_info tmu_info;

char tmu_log_file_path[LONGNAME_SIZE]; /* log file path comming from dts */

/* work queue */
static struct workqueue_struct  *tmu_monitor_wq;
#ifdef CONFIG_PM
static struct workqueue_struct *tmu_resume_wq;
static struct delayed_work resume_avs;
#endif

/* lock */
static DEFINE_MUTEX(tmu_lock);

/* for sysfs */
static bool throt_on = true;

/* for log file */
static int tmu_1st_throttle_count = 0;
static int tmu_2nd_throttle_count = 0;
static int tmu_3rd_throttle_count = 0;

/* write log to filesystem */
static void write_log(unsigned int throttle)
{
	char tmu_longname[LONGNAME_SIZE];
	static char tmu_logbuf[LOG_BUF_LEN];
	int len;
	struct file *fp;
	
	if (throttle == TMU_1ST_THROTTLE)
		tmu_1st_throttle_count++;
	else if (throttle == TMU_2ND_THROTTLE)
		tmu_2nd_throttle_count++;
	else if (throttle == TMU_3RD_THROTTLE)
		tmu_3rd_throttle_count++;
	else
		printk(KERN_INFO "TMU: %s - throttle level is not valid. %u\n",
				__func__, throttle);

	printk(KERN_INFO "TMU: %s - 1st = %d, 2nd = %d, 3rd = %d\n", __func__,
			tmu_1st_throttle_count, tmu_2nd_throttle_count, tmu_3rd_throttle_count);

	snprintf(tmu_longname, LONGNAME_SIZE, tmu_log_file_path);
	printk("tmu_longname : %s\n", tmu_longname);

	fp = filp_open(tmu_longname, O_CREAT|O_WRONLY|O_TRUNC|O_LARGEFILE, 0644);
	if (IS_ERR(fp)) {
		printk(KERN_ERR "TMU: error in opening tmu log file.\n");
		return;
	}

	snprintf(tmu_logbuf, LOG_BUF_LEN,
		"1st throttle count = %d\n"
		"2nd throttle count = %d\n"
		"3rd throttle count = %d\n",
		tmu_1st_throttle_count, tmu_2nd_throttle_count, tmu_3rd_throttle_count);
	len = strlen(tmu_logbuf);
	
	fp->f_op->write(fp, tmu_logbuf, len, &fp->f_pos);

	filp_close(fp, NULL);	
}

/* sysfs */
static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	unsigned int temperature;

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	mutex_lock(&tmu_lock);

	temperature = info->get_curr_temp(info);

	mutex_unlock(&tmu_lock);

	return snprintf(buf, 5, "%u\n", temperature);
}

static ssize_t show_tmu_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	return snprintf(buf, 3, "%d\n", info->tmu_state);
}

static ssize_t show_throttle_on(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 3, "%d\n", throt_on);
}

static ssize_t store_throttle_on(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	unsigned int on;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (on == 1) {
		if (throt_on) {
			printk(KERN_ERR "TMU: throttle already ON.\n");
			goto out;
		}
		throt_on = true;
		dev_info(dev, "throttle ON\n");
	} else if (on == 0) {
		if (!throt_on) {
			printk(KERN_ERR "TMU: throttle already OFF\n");
			goto out;
		}
		throt_on = false;
		dev_info(dev, "throttle OFF\n");
	} else {
		dev_err(dev, "Invalid value!! %d\n", on);
		return -EINVAL;
	}

out:
	return (ssize_t)count;
}

static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);
static DEVICE_ATTR(tmu_state, 0444, show_tmu_state, NULL);
static DEVICE_ATTR(throttle_on, 0644, show_throttle_on, store_throttle_on);

static void print_temperature_params(struct sdp_tmu_info *info)
{
	pr_info("TMU - temperature set value\n");
	pr_info("zero throttling start_temp = %u, stop_temp      = %u\n",
		info->ts.start_zero_throttle, info->ts.stop_zero_throttle);
	pr_info("1st  throttling stop_temp  = %u, start_temp     = %u\n",
		info->ts.stop_1st_throttle, info->ts.start_1st_throttle);
	pr_info("2nd  throttling stop_temp  = %u, start_temp     = %u\n",
		info->ts.stop_2nd_throttle, info->ts.start_2nd_throttle);
	pr_info("3rd  throttling stop_temp  = %u, start_temp     = %u\n",
		info->ts.stop_3rd_throttle, info->ts.start_3rd_throttle);
	pr_info("3rd  hotplug temp = %u\n", info->ts.start_3rd_hotplug);
}

#ifdef CONFIG_TMU_DEBUG
static ssize_t tmu_show_print_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (info == NULL)
		return -1;

	ret = snprintf(buf, 30, "[TMU] info->temp_print_on=%d\n"
					, info->temp_print_on);

	return ret;
}

static ssize_t tmu_store_print_state(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	unsigned int on;
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (info == NULL)
		goto err;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (on)
		info->temp_print_on = true;
	else
		info->temp_print_on = false;

err:
	return (ssize_t)count;
}
static DEVICE_ATTR(print_state, S_IRUGO | S_IWUSR,\
	tmu_show_print_state, tmu_store_print_state);
#endif
/* end of sysfs */

/* limit cpu and gpu frequency */
static void sdp_tmu_limit(struct sdp_tmu_info* info, unsigned int throttle)
{
	if (!throt_on) {
		printk(KERN_INFO "TMU: thermal throttle is OFF now.");
		return;
	}

#if defined(CONFIG_CPU_FREQ)
	/* 1st throttle */
	if (throttle == TMU_1ST_THROTTLE) {
		pr_info("TMU: 1st limit\n");
		info->throttle_print_on = true;
		
		/* cpu */
		if (info->cpufreq_level_1st_throttle == TMU_INV_FREQ_LEVEL) {
			sdp_cpufreq_get_level(info->cpufreq.limit_1st_throttle,
								&info->cpufreq_level_1st_throttle);
			pr_info("TMU: CPU 1st throttle level = %d\n", info->cpufreq_level_1st_throttle);
		}
		sdp_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				info->cpufreq_level_1st_throttle);

		/* gpu */
		if (info->gpu_freq_limit) {
			info->gpu_freq_limit((int)info->gpufreq_level_1st_throttle);
			pr_info("TMU: GPU 1st throttle freq = %d\n",
					info->gpufreq_level_1st_throttle);
		}
	}
	/* 2nd throttle */
	else if (throttle == TMU_2ND_THROTTLE) {
		pr_info("TMU: 2nd limit\n");
		info->throttle_print_on = true;
		
		/* cpu */
		if (info->cpufreq_level_2nd_throttle == TMU_INV_FREQ_LEVEL) {
			sdp_cpufreq_get_level(info->cpufreq.limit_2nd_throttle,
								&info->cpufreq_level_2nd_throttle);
			pr_info("TMU: CPU 2nd throttle level = %d\n", info->cpufreq_level_2nd_throttle);
		}
		sdp_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				info->cpufreq_level_2nd_throttle);
		
		/* gpu */
		if (info->gpu_freq_limit) {
			info->gpu_freq_limit((int)info->gpufreq_level_2nd_throttle);
			pr_info("TMU: GPU 2nd throttle freq = %d\n",
					info->gpufreq_level_2nd_throttle);
		}
	}
	/* 3rd throttle */
	else if (throttle == TMU_3RD_THROTTLE) {
		pr_info("TMU: 3rd limit.\n");
		info->throttle_print_on = true;
		
		/* cpu */
		if (info->cpufreq_level_3rd_throttle == TMU_INV_FREQ_LEVEL) {
			sdp_cpufreq_get_level(info->cpufreq.limit_3rd_throttle,
								&info->cpufreq_level_3rd_throttle);
			pr_info("TMU: CPU 3rd throttle level = %d\n", info->cpufreq_level_3rd_throttle);
		}
		sdp_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				info->cpufreq_level_3rd_throttle);
			
		/* gpu */
		if (info->gpu_freq_limit) {
			info->gpu_freq_limit((int)info->gpufreq_level_3rd_throttle);
			pr_info("TMU: GPU 3rd throttle freq = %d\n",
					info->gpufreq_level_3rd_throttle);
		}
	}
	else {
		pr_err("TMU: %s - throttle level error. %d\n", __func__, throttle);
	}
#endif
}

/* free cpu and gpu frequency limitation */
static void sdp_tmu_limit_free(struct sdp_tmu_info* info, int check_handle)
{
	/* free limit */
	info->throttle_print_on = false;

#if defined(CONFIG_CPU_FREQ)
	/* cpu up */
	if (info->cpu_hotplug_up)
		info->cpu_hotplug_up(info);
	
	/* cpu */
	sdp_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
	pr_info("TMU: free cpu freq limit\n");
#endif
	
	/* gpu */
	if (info->gpu_freq_limit_free) {
		info->gpu_freq_limit_free();
		pr_info("TMU: free gpu freq limit\n");
	}
}

static void sdp_handler_tmu_state(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct sdp_tmu_info *info =
		container_of(delayed_work, struct sdp_tmu_info, polling);
	unsigned int cur_temp;
	static int check_handle;
	int trend = 0;
	static int count_down = PANIC_TIME_SEC;

	mutex_lock(&tmu_lock);

	cur_temp = info->get_curr_temp(info);
	trend = (int)cur_temp - (int)info->last_temperature;
	//pr_debug("curr_temp = %u, temp_diff = %d\n", cur_temp, trend);

	switch (info->tmu_state) {
	case TMU_STATUS_ZERO:
		/* 1. zero throttle. dvfs off */
		if (cur_temp <= info->ts.start_zero_throttle &&
			!(check_handle & THROTTLE_ZERO_FLAG)) {
			/* if limit is set, free */
			if (check_handle &
				(THROTTLE_1ST_FLAG | THROTTLE_2ND_FLAG | THROTTLE_3RD_FLAG)) {
				sdp_tmu_limit_free(info, check_handle);
				check_handle = 0;
				pr_info("TMU: zero throttle. all limit free.\n");
			}

			/* avs off */
			if (info->cpu_avs_on)
				info->cpu_avs_on(false);

			if (info->gpu_avs_on)
				info->gpu_avs_on(false);

			if (info->core_avs_on)
				info->core_avs_on(false);

			check_handle |= THROTTLE_ZERO_FLAG;

			/* store current state */
			info->tmu_prev_state = info->tmu_state;
		/* 2. change to NORMAL */
		} else if (cur_temp >= info->ts.stop_zero_throttle) {
			/* avs on */
			if (info->cpu_avs_on)
				info->cpu_avs_on(true);

			if (info->gpu_avs_on)
				info->gpu_avs_on(true);
			
			if (info->core_avs_on)
				info->core_avs_on(true);
			
			info->tmu_state = TMU_STATUS_NORMAL;
			pr_info("TMU: change state: zero -> normal. %d'C\n", cur_temp);
		/* 3. polling */
		} else {
			if (!(check_handle & THROTTLE_ZERO_FLAG))
				pr_info("TMU: polling. zero throttle is not activated yet. %d'C\n", cur_temp);
		}
		break;
		
	case TMU_STATUS_NORMAL:
		/* 1. change to ZERO THROTTLE */
		if (cur_temp <= info->ts.start_zero_throttle) {
			info->tmu_state = TMU_STATUS_ZERO;
			pr_info("TMU: change state: normal -> zero throttle. %d'C\n", cur_temp);
		/* 2. change to 1ST THROTTLE */
		} else if (cur_temp >= info->ts.start_1st_throttle) {
			info->tmu_state = TMU_STATUS_1ST;
			pr_info("TMU: change state: normal -> 1st throttle. %d'C\n", cur_temp);
		/* 3. all limit free or dvfs/avs on */
		} else if (cur_temp > info->ts.start_zero_throttle &&
					cur_temp <= info->ts.stop_1st_throttle &&
					check_handle) {
			if (check_handle & THROTTLE_ZERO_FLAG) {
				/* do nothing */
				
				check_handle &= ~THROTTLE_ZERO_FLAG;
			} else if (check_handle &
				(THROTTLE_1ST_FLAG | THROTTLE_2ND_FLAG | THROTTLE_3RD_FLAG)) {
				sdp_tmu_limit_free(info, check_handle);
				check_handle = 0;
			}

			/* store current state */
			info->tmu_prev_state = info->tmu_state;
			
			pr_info("TMU: current status is NORMAL. %d'C\n", cur_temp);
			pr_debug("TMU: check_handle = %d. %d'C\n", check_handle, cur_temp);
		} else {
			pr_debug("TMU: NORMAL polling. %d'C\n", cur_temp);
		}
		break;

	case TMU_STATUS_1ST:
		/* 1. change to NORMAL */
		if ((cur_temp <= info->ts.stop_1st_throttle) &&
			(trend < 0)) {
			info->tmu_state = TMU_STATUS_NORMAL;
			pr_info("TMU: change state: 1st throttle -> normal. %d'C\n", cur_temp);
		/* 2. change to 2ND THROTTLE */
		} else if (cur_temp >= info->ts.start_2nd_throttle) {
			info->tmu_state = TMU_STATUS_2ND;
			pr_info("TMU: change state: 1st throttle -> 2nd throttle. %d'C\n", cur_temp);
		/* 3. 1ST THROTTLE - cpu, gpu limitation */
		} else if ((cur_temp >= info->ts.start_1st_throttle) &&
				!(check_handle & THROTTLE_1ST_FLAG)) {
			if (check_handle & (THROTTLE_2ND_FLAG | THROTTLE_3RD_FLAG)) {
				sdp_tmu_limit_free(info, check_handle);
				check_handle = 0;
			}
			
			sdp_tmu_limit(info, TMU_1ST_THROTTLE);
			/* write tmu log */
			if (info->tmu_prev_state < info->tmu_state)
				write_log(TMU_1ST_THROTTLE);

			check_handle |= THROTTLE_1ST_FLAG;
			pr_debug("check_handle = %d\n", check_handle);
			
			/* store current state */
			info->tmu_prev_state = info->tmu_state;
		} else {
			pr_debug("TMU: 1ST THROTTLE polling. %d'C\n", cur_temp);
		}
		break;

	case TMU_STATUS_2ND:
		/* 1. change to 1ST THROTTLE */
		if ((cur_temp <= info->ts.stop_2nd_throttle) &&
			(trend < 0)) {
			info->tmu_state = TMU_STATUS_1ST;
			pr_info("TMU: change state: 2nd throttle -> 1st throttle. %d'C\n", cur_temp);
		/* 2. change to 3RD THROTTLE */
		} else if (cur_temp >= info->ts.start_3rd_throttle) {
			info->tmu_state = TMU_STATUS_3RD;
			pr_info("TMU: change state: 2nd throttle -> 3rd throttle. %d'C\n", cur_temp);
		/* 3. 2ND THROTTLE - cpu, gpu limitation */
		} else if ((cur_temp >= info->ts.start_2nd_throttle) &&
			!(check_handle & THROTTLE_2ND_FLAG)) {
			if (check_handle & (THROTTLE_1ST_FLAG | THROTTLE_3RD_FLAG)) {
				sdp_tmu_limit_free(info, check_handle);
				check_handle = 0;
			}

			sdp_tmu_limit(info, TMU_2ND_THROTTLE);
			/* write tmu log */
			if (info->tmu_prev_state < info->tmu_state)
				write_log(TMU_2ND_THROTTLE);

			check_handle |= THROTTLE_2ND_FLAG;
			pr_debug("check_handle = %d\n", check_handle);

			/* store current state */
			info->tmu_prev_state = info->tmu_state;
			
			pr_info("TMU: 2nd throttle: cpufreq is limited. level %d. %d'C\n",
					info->cpufreq_level_2nd_throttle, cur_temp);
		} else {
			pr_debug("TMU: 2ND THROTTLE polling. %d'C\n", cur_temp);
		}
		break;

	case TMU_STATUS_3RD:
		/* 1. change to 2ND THROTTLE */
		if ((cur_temp <= info->ts.stop_3rd_throttle) && (trend < 0)) {
			info->tmu_state = TMU_STATUS_2ND;
			pr_info("TMU: change state: 3rd throttle -> 2nd throttle. %d'C\n", cur_temp);
		/* 2. cpufreq limitation */
		} else if ((cur_temp >= info->ts.start_3rd_throttle) &&
			!(check_handle & THROTTLE_3RD_FLAG)){
			if (check_handle & ~(THROTTLE_3RD_FLAG)) {
				sdp_tmu_limit_free(info, check_handle);
				check_handle = 0;
			}

			/* call emergency limit */
			sdp_tmu_limit(info, TMU_3RD_THROTTLE);

			/* write tmu log */
			if (info->tmu_prev_state < info->tmu_state)
				write_log(TMU_3RD_THROTTLE);
			
			count_down = PANIC_TIME_SEC;
			
			check_handle |= THROTTLE_3RD_FLAG;
			pr_debug("check_handle = %d\n", check_handle);

			/* store current state */
			info->tmu_prev_state = info->tmu_state;
			
			pr_info("TMU: 3rd throttle: cpufreq is limited. level %d. %d'C\n",
					info->cpufreq_level_3rd_throttle, cur_temp);
		
		} else {
			pr_debug("TMU: 3RD THROTTLE polling. %d'C\n", cur_temp);

			/* cpu hoplug down when temp is over info->ts.start_3rd_hotplug */
			if (throt_on && cur_temp >= info->ts.start_3rd_hotplug) {
				/* cpu down */
				if (info->cpu_hotplug_down)
					info->cpu_hotplug_down(info);
			}
			
#if 0
			if (check_handle & THROTTLE_3RD_FLAG)
				printk("\033[1;7;41m>>>>> PANIC COUNT DOWN %d!!!\033[0m\n", count_down--);

			/* PANIC */
			if (count_down <= 0)
				panic("\033[1;7;41mTMU: CHIP is VERY HOT!\033[0m\n");
#endif			
		}
		break;

	case TMU_STATUS_INIT:
		/* send tmu initial status */
		if (cur_temp >= info->ts.start_3rd_throttle) {
			info->tmu_state = TMU_STATUS_3RD;
			info->tmu_prev_state = TMU_STATUS_3RD;
		} else if (cur_temp >= info->ts.start_2nd_throttle) {
			info->tmu_state = TMU_STATUS_2ND;
			info->tmu_prev_state = TMU_STATUS_2ND;
		} else if (cur_temp >= info->ts.start_1st_throttle) {
			info->tmu_state = TMU_STATUS_1ST;
			info->tmu_prev_state = TMU_STATUS_1ST;
		} else if (cur_temp > info->ts.start_zero_throttle) {
			info->tmu_state = TMU_STATUS_NORMAL;
			info->tmu_prev_state = TMU_STATUS_NORMAL;
		} else {
			info->tmu_state = TMU_STATUS_ZERO;
			info->tmu_prev_state = TMU_STATUS_ZERO;
		}

		pr_info("TMU: init state is %d(%d'C)\n", info->tmu_state, cur_temp);

		/* apply avs */
		if (cur_temp >= info->ts.stop_zero_throttle) {
			if (info->cpu_avs_on)
				info->cpu_avs_on(true);
			
			if (info->gpu_avs_on)
				info->gpu_avs_on(true);

			/* core avs is always on */
			if (info->core_avs_on)
				info->core_avs_on(true);
		}

		break;

	default:
		pr_warn("Bug: checked tmu_state. %d\n", info->tmu_state);
		info->tmu_state = TMU_STATUS_INIT;
		
		break;
	} /* end */

	info->last_temperature = cur_temp;

	/* reschedule the next work */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
			info->sampling_rate);

	mutex_unlock(&tmu_lock);

	return;
}

#ifdef CONFIG_PM
static void sdp_handler_resume_avs(struct work_struct *work)
{
	struct sdp_tmu_info *info = &tmu_info;
	unsigned int cur_temp = info->get_curr_temp(info);

	if (cur_temp >= info->ts.stop_zero_throttle) {
		pr_info("TMU: cur temp %u'C, AVS on\n", cur_temp);
		
		/* core avs */
		if (info->core_avs_on)
			info->core_avs_on(true);
	}
}
#endif

#ifdef CONFIG_TMU_SYSFS
static ssize_t sdp_tmu_show_curr_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	unsigned int curr_temp;

	if (info == NULL)
		return -1;

	curr_temp = info->get_curr_temp(info);
	pr_info("curr temp = %d\n", curr_temp);

	return snprintf(buf, 5, "%d\n", curr_temp);
}
static DEVICE_ATTR(curr_temp, S_IRUGO, sdp_tmu_show_curr_temp, NULL);
#endif

struct sdp_tmu_info* sdp_tmu_get_info(void)
{
	return &tmu_info;
}
EXPORT_SYMBOL(sdp_tmu_get_info);

int sdp_tmu_register_cpu_avs(void (*set_avs)(bool on))
{
	tmu_info.cpu_avs_on = set_avs;
	
	return 0;
}
EXPORT_SYMBOL(sdp_tmu_register_cpu_avs);

int sdp_tmu_register_gpu_avs(void (*set_avs)(bool on))
{
	unsigned int temp;
	
	/*
	 * Once GPU avs function is registed,
	 * must apply GPU avs.
	 */

	/* regsister */
	tmu_info.gpu_avs_on = set_avs;

	/* apply GPU avs */
	temp = tmu_info.get_curr_temp(&tmu_info);
	
	if (temp >= tmu_info.ts.stop_zero_throttle) {
		pr_info("%s - cur temp = %d'C, GPU avs on\n", __func__, temp);
		tmu_info.gpu_avs_on(true);
	} else if (temp <= tmu_info.ts.start_zero_throttle) {
		pr_info("%s - cur temp = %d'C, GPU avs off\n", __func__, temp);
		tmu_info.gpu_avs_on(false);
	}
	
	return 0;;
}
EXPORT_SYMBOL(sdp_tmu_register_gpu_avs);

int sdp_tmu_register_core_avs(void (*set_avs)(bool on))
{
	tmu_info.core_avs_on = set_avs;
	
	return 0;
}
EXPORT_SYMBOL(sdp_tmu_register_core_avs);

static int sdp_tmu_pm_notifier(struct notifier_block *notifier,
			       unsigned long pm_event, void *v)
{
	unsigned int cur_temp;
	struct sdp_tmu_info *info = &tmu_info;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pr_info("PM_SUSPEND_PREPARE for TMU\n");
		break;
	case PM_POST_SUSPEND:
		pr_info("PM_POST_SUSPEND for TMU\n");

		cur_temp = info->get_curr_temp(info);
		pr_info("TMU: cur_temp=%d\n", cur_temp);

		if (cur_temp >= info->ts.stop_zero_throttle) {
			if (info->cpu_avs_on) {
				info->cpu_avs_on(false);
				info->cpu_avs_on(true);
			}
				
			if (info->gpu_avs_on)
				info->gpu_avs_on(true);
		}

		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block sdp_tmu_nb = {
	.notifier_call = sdp_tmu_pm_notifier,
};

static void get_tmu_info_from_of(struct sdp_tmu_info *info, struct device_node* np)
{
	const char * path;

	of_property_read_u32(np, "sensor_id", &info->sensor_id);
	
	of_property_read_u32(np, "start_zero_throttle", &info->ts.start_zero_throttle);
	of_property_read_u32(np, "stop_zero_throttle", &info->ts.stop_zero_throttle);
	of_property_read_u32(np, "start_1st_throttle", &info->ts.start_1st_throttle);
	of_property_read_u32(np, "stop_1st_throttle", &info->ts.stop_1st_throttle);
	of_property_read_u32(np, "start_2nd_throttle", &info->ts.start_2nd_throttle);
	of_property_read_u32(np, "stop_2nd_throttle", &info->ts.stop_2nd_throttle);
	of_property_read_u32(np, "start_3rd_throttle", &info->ts.start_3rd_throttle);
	of_property_read_u32(np, "stop_3rd_throttle", &info->ts.stop_3rd_throttle);
	of_property_read_u32(np, "start_3rd_hotplug", &info->ts.start_3rd_hotplug);
	if (!info->ts.start_3rd_hotplug)
		info->ts.start_3rd_hotplug = info->ts.start_3rd_throttle + 5;

	of_property_read_u32(np, "cpu_limit_1st_throttle", &info->cpufreq.limit_1st_throttle);
	of_property_read_u32(np, "cpu_limit_2nd_throttle", &info->cpufreq.limit_2nd_throttle);
	of_property_read_u32(np, "cpu_limit_3rd_throttle", &info->cpufreq.limit_3rd_throttle);

	of_property_read_u32(np, "gpu_limit_1st_throttle", &info->gpufreq.limit_1st_throttle);
	of_property_read_u32(np, "gpu_limit_2nd_throttle", &info->gpufreq.limit_2nd_throttle);
	of_property_read_u32(np, "gpu_limit_3rd_throttle", &info->gpufreq.limit_3rd_throttle);

	of_property_read_string(np, "log_file_path", &path);
	strncpy(tmu_log_file_path, path, strlen(path));
}

static int sdp_tmu_init(struct sdp_tmu_info * info)
{
	int ret = -EINVAL;

	pr_info("TMU: current sensor = #%d\n", info->sensor_id);
	
	/* set cpufreq limit level at 1st_throttle & 2nd throttle */
	pr_info("TMU: CPU 1st throttle %dMHz, 2nd throttle %dMHz, 3rd throttle %dMHz\n",
			info->cpufreq.limit_1st_throttle / 1000,
			info->cpufreq.limit_2nd_throttle / 1000,
			info->cpufreq.limit_3rd_throttle / 1000);
	pr_info("TMU: GPU 1st throttle %dMHz, 2nd throttle %dMHz, 3rd throttle %dMHz\n",
			info->gpufreq.limit_1st_throttle / 1000,
			info->gpufreq.limit_2nd_throttle / 1000,
			info->gpufreq.limit_3rd_throttle / 1000);
	info->cpufreq_level_1st_throttle = TMU_INV_FREQ_LEVEL;
	info->cpufreq_level_2nd_throttle = TMU_INV_FREQ_LEVEL;
	info->cpufreq_level_3rd_throttle = TMU_INV_FREQ_LEVEL;
	info->gpufreq_level_1st_throttle = info->gpufreq.limit_1st_throttle;
	info->gpufreq_level_2nd_throttle = info->gpufreq.limit_2nd_throttle;
	info->gpufreq_level_3rd_throttle = info->gpufreq.limit_3rd_throttle;

	print_temperature_params(info);
	printk("TMU: log file path = %s\n", tmu_log_file_path);

#ifdef CONFIG_SDP1304_THERMAL
	if (of_machine_is_compatible("samsung,sdp1304"))
		ret = sdp1304_tmu_init(info);
#endif
#ifdef CONFIG_SDP1302_THERMAL
	if (of_machine_is_compatible("samsung,sdp1302"))
		ret = sdp1302_tmu_init(info);
#endif
#ifdef CONFIG_SDP1307_THERMAL
	if (of_machine_is_compatible("samsung,sdp1307"))
		ret = sdp1307_tmu_init(info);
#endif	
	if (ret) {
		printk(KERN_ERR "%s error : %d\n", __func__, ret);
		return -EINVAL;
	}

	/* To poll current temp, set sampling rate to ONE second sampling */
	info->sampling_rate  = usecs_to_jiffies(SAMPLING_RATE);

	return 0;
}

static int sdp_tmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sdp_tmu_info *info = &tmu_info;
	struct resource *res;
	int ret = 0;

	pr_debug("%s: probe=%p\n", __func__, pdev);

	platform_set_drvdata(pdev, info);
	
	info->dev = dev;
	info->tmu_state = TMU_STATUS_INIT;

	/* get platform data from device tree */
	get_tmu_info_from_of(info, np);

	ret = sdp_tmu_init(info);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get memory region resource\n");
		ret = -ENODEV;
		goto err;
	}

	info->tmu_base = devm_ioremap(dev, res->start, (res->end - res->start) + 1);
	if (!(info->tmu_base)) {
		dev_err(dev, "failed ioremap()\n");
		ret = -ENOMEM;
		goto err;
	}
	
	tmu_monitor_wq = create_freezable_workqueue(dev_name(dev));
	if (!tmu_monitor_wq) {
		pr_info("Creation of tmu_monitor_wq failed\n");
		ret = -ENOMEM;
		goto err;
	}

	/* To support periodic temprature monitoring */
	INIT_DELAYED_WORK(&info->polling, sdp_handler_tmu_state);

#ifdef CONFIG_PM
	tmu_resume_wq = create_freezable_workqueue("sdptmu-pm");
	if (!tmu_resume_wq) {
		pr_info("Creation of tmu_resume_wq failed\n");
	}

	INIT_DELAYED_WORK(&resume_avs, sdp_handler_resume_avs);
#endif

	/* sysfs interface */
	ret = device_create_file(dev, &dev_attr_temperature);
	if (ret != 0) {
		pr_err("Failed to create temperatue file: %d\n", ret);
		goto err_sysfs_file1;
	}

	ret = device_create_file(dev, &dev_attr_tmu_state);
	if (ret != 0) {
		pr_err("Failed to create tmu_state file: %d\n", ret);
		goto err_sysfs_file2;
	}

	ret = device_create_file(dev, &dev_attr_throttle_on);
	if (ret != 0) {
		pr_err("Failed to create throttle on file: %d\n", ret);
		goto err_sysfs_file3;
	}

	ret = info->enable_tmu(info);
	if (ret)
		goto err_init;

#ifdef CONFIG_TMU_SYSFS
	ret = device_create_file(dev, &dev_attr_curr_temp);
	if (ret < 0) {
		dev_err(dev, "Failed to create sysfs group\n");
		goto err_init;
	}
#endif

#ifdef CONFIG_TMU_DEBUG
	ret = device_create_file(dev, &dev_attr_print_state);
	if (ret) {
		dev_err(dev, "Failed to create tmu sysfs group\n\n");
		return ret;
	}
#endif

	/* initialize tmu_state */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
		info->sampling_rate);

	register_pm_notifier(&sdp_tmu_nb);

	return ret;

err_init:
	device_remove_file(dev, &dev_attr_throttle_on);
	
err_sysfs_file3:
	device_remove_file(dev, &dev_attr_tmu_state);

err_sysfs_file2:
	device_remove_file(dev, &dev_attr_temperature);

err_sysfs_file1:
	destroy_workqueue(tmu_monitor_wq);

#ifdef CONFIG_PM
	destroy_workqueue(tmu_resume_wq);
#endif

err:
	dev_err(dev, "initialization failed.\n");

	return ret;
}

static int sdp_tmu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdp_tmu_info *info = platform_get_drvdata(pdev);

	if (info)
		cancel_delayed_work(&info->polling);

	destroy_workqueue(tmu_monitor_wq);

#ifdef CONFIG_PM
	cancel_delayed_work(&resume_avs);
	destroy_workqueue(tmu_resume_wq);
#endif

	device_remove_file(dev, &dev_attr_temperature);
	device_remove_file(dev, &dev_attr_tmu_state);

	pr_info("%s is removed\n", dev_name(dev));
	return 0;
}

#ifdef CONFIG_PM
static int sdp_tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sdp_tmu_info *info = platform_get_drvdata(pdev);

	if (!info)
		return -EAGAIN;

	return 0;
}

static int sdp_tmu_resume(struct platform_device *pdev)
{
	struct sdp_tmu_info *info = platform_get_drvdata(pdev);

	if (!info)
		return -EAGAIN;

	/* reinit TSC */
	info->enable_tmu(info);
	
	/* Find out tmu_state after wakeup */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling, 0);

	/* apply avs after 2 second */
	queue_delayed_work_on(0, tmu_resume_wq, &resume_avs, usecs_to_jiffies(SAMPLING_RATE));

	return 0;
}
#else
#define sdp_tmu_suspend	NULL
#define sdp_tmu_resume	NULL
#endif

static const struct of_device_id sdp_tmu_match[] = {
	{ .compatible = "samsung,sdp-tmu", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_tmu_match);

static struct platform_driver sdp_tmu_driver = {
	.probe		= sdp_tmu_probe,
	.remove		= sdp_tmu_remove,
	.suspend	= sdp_tmu_suspend,
	.resume		= sdp_tmu_resume,
	.driver		= {
		.name   = "sdp-tmu",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_tmu_match),
	},
};

static int __init sdp_tmu_driver_init(void)
{
	return platform_driver_register(&sdp_tmu_driver);
}

static void __exit sdp_tmu_driver_exit(void)
{
	platform_driver_unregister(&sdp_tmu_driver);
}
late_initcall_sync(sdp_tmu_driver_init);
module_exit(sdp_tmu_driver_exit);
