/* sdp1501_tmu.c
 *
 * Copyright (c) 2013-2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * SDP1501 - Thermal Management support
 *
 */
 
#include <linux/io.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <mach/map.h>
#include <mach/sdp_thermal.h>

#include <mach/soc.h>
#include <linux/of.h>

#define MAX_SENSOR_CNT	1

/* Register define */
#define SDP_PAD_CTRL50		0x8
#define SDP_PAD_CTRL51		0xC
#define TSC_TEM_T_EN		(1 << 0)
#define TSC_TEM_T_TS_8BIT_TS	12

#define CHIPID_BASE	(SFR_VA + 0x00180000 - 0x00100000)

static int diff_val;

static int get_fused_value(struct sdp_tmu_info * info)
{
	int timeout = 200;
	
	/* prepare to read */
	writel(0x1F, (void*)(CHIPID_BASE + 0x4));
	while (timeout--) {
		if (readl((void*)CHIPID_BASE) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("TMU: efuse read fail!\n");
		goto out_diff;
	}

	/* read efuse */
	diff_val = (readl((void*)(CHIPID_BASE + 0x10)) >> 17) & 0xFF;
	if (diff_val == 0) {
		printk(KERN_INFO "TMU: diff is not fused maybe, force set 46\n");
		diff_val = TSC_DEGREE_25;
	}		
	printk(KERN_INFO "TMU: diff val - 0: %d(%d'C)\n", 
				diff_val, TSC_DEGREE_25 - diff_val);

	diff_val = TSC_DEGREE_25 - diff_val;

	return 0;
	
out_diff:
	diff_val = 0;
	
	return 0;
}

static int sdp1501_enable_tmu(struct sdp_tmu_info * info)
{
	u32 val;

	/* read efuse value */
	get_fused_value(info);
	
	/* Temperature sensor enable */
	val = readl((void *)((u32)info->tmu_base + SDP_PAD_CTRL50)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_PAD_CTRL50));

	mdelay(1);
	
	return 0;
}

#define TMU_MAX_TEMP_DIFF	(125)
static u32 sdp1501_get_temp(struct sdp_tmu_info *info)
{
	int temp = 0;
	static int prev_temp = TSC_DEGREE_25; /* defualt temp is 46'C */
	static int print_delay = PRINT_RATE;

	if (info->sensor_id >= MAX_SENSOR_CNT) {
		printk(KERN_ERR "TMU ERROR - sensor id is not avaiable. %d\n", info->sensor_id);
		return 0;
	}

	/* get temperature from TSC register */
	temp = (readl((void *)((u32)info->tmu_base + SDP_PAD_CTRL51)) >> TSC_TEM_T_TS_8BIT_TS) & 0xFF;


	/* calibration */
	temp = temp + diff_val - (TSC_DEGREE_25 - 25);

	/* check boundary */
	if (temp < 0)
		temp = 0;

	/* sanity check */
	if (abs(temp - prev_temp) > TMU_MAX_TEMP_DIFF) {
		printk(KERN_INFO "TMU: warning - temp is insane, %d'C(force set to %d'C)\n", 
			temp, prev_temp);
		temp = prev_temp;
	}

	prev_temp = temp;

	/* Temperature is printed every PRINT_RATE. */ 
	if (info->print_on || info->user_print_on) {
		print_delay -= SAMPLING_RATE;
		if (print_delay <= 0) {
			printk(KERN_INFO "\033[1;7;33mT^%d'C\033[0m\n", temp);
			print_delay = PRINT_RATE;
		}
	}
	
	return (unsigned int)temp;
}

static int sdp1501_cpu_hotplug_down(struct sdp_tmu_info * info)
{
#ifdef CONFIG_HOTPLUG_CPU	
	struct device * dev;
	u32 cpu;
	ssize_t ret;

	/* cpu down (cpu1 ~ cpu3) */
	for (cpu = 1; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		
		dev = get_cpu_device(cpu);
		ret = cpu_down(cpu);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
	}
#endif
	
	return 0;
}

static int sdp1501_cpu_hotplug_up(struct sdp_tmu_info * info)
{
#ifdef CONFIG_HOTPLUG_CPU
	u32 cpu;
	struct device * dev;
	ssize_t ret;

	/* find offline cpu and power up */
	for (cpu = 1; cpu < NR_CPUS; cpu++) {
		if (cpu_online(cpu))
			continue;
		
		dev = get_cpu_device(cpu);
		ret = cpu_up(cpu);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
	}
#endif

	return 0;
}

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);

int sdp1501_tmu_extra(struct sdp_tmu_info * info)
{
#ifdef CONFIG_ARCH_SDP1501
	u32 temp, tmcb;

	tmcb = (readl((void *)(CHIPID_BASE + 0x14)) >> 10) & 0x3F;

	temp = sdp1501_get_temp(info);
	
	if (soc_is_jazzl() && tmcb <= 20) {
		sdp_set_clkrst_mux(0x005C10F0, 0xffffffff, 0x3);
	}
	else if (tmcb <= 30) {
		if (temp <= 50) {
			sdp_set_clkrst_mux(0x005C10F0, 0xffffffff, 0x3);
		}

		if (temp >= 55) {
			sdp_set_clkrst_mux(0x005C10F0, 0xffffffff, 0x18);
		}
	}
#endif
	return 0;
}

/* init */
int sdp1501_tmu_init(struct sdp_tmu_info * info)
{
	info->enable_tmu = sdp1501_enable_tmu;
	info->get_temp = sdp1501_get_temp;
	info->cpu_hotplug_down = sdp1501_cpu_hotplug_down;
	info->cpu_hotplug_up = sdp1501_cpu_hotplug_up;

	return 0;
}
