/* arch/arm/plat-sdp/sdp1307_tmu.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * SDP1307 - Thermal Management support
 *
 */
 
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_data/sdp_thermal.h>

#define MAX_SENSOR_CNT			1

/* Register define */
/* TODO: check address */
#define SDP_TSC_CONTROL0		0x0
#define SDP_TSC_CONTROL1		0x4
#define SDP_TSC_CONTROL2		0x8
#define SDP_TSC_CONTROL3		0xC
#define SDP_TSC_CONTROL4		0x10
#define TSC_TEM_T_EN			(1 << 0)
#define SDP_TSC_CONTROL5		0x14
#define TSC_TEM_T_TS_8BIT_TS	12

static int diff_val;

static int get_fused_value(struct sdp_tmu_info * info)
{
	void __iomem * base;
	int timeout = 200;
	
	/* prepare to read */
	base = ioremap(0x18080000, 0x10);
	if (base == NULL) {
		pr_err("TMU: chip id enable fail\n");
		goto out_diff;
	}
	
	writel(0x1F, (void*)base + 0x4);
	while (timeout--) {
		if (readl((void*)base) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("TMU: efuse read fail!\n");
		goto out_diff;
	}

	/* read efuse */
	/* TODO: add register read */
	diff_val = TSC_DEGREE_25;
	printk(KERN_INFO "TMU: diff val - 0: %d(%d'C)\n", 
				diff_val, TSC_DEGREE_25 - diff_val);

	diff_val = TSC_DEGREE_25 - diff_val;

	iounmap(base);
	
out_diff:
	
	return 0;
}

static int sdp1307_enable_tmu(struct sdp_tmu_info * info)
{
	unsigned int val;

	/* read efuse value */
	get_fused_value(info);
	
	/* Temperature sensor enable */
	val = readl(info->tmu_base + SDP_TSC_CONTROL4) | TSC_TEM_T_EN;
	writel(val, info->tmu_base + SDP_TSC_CONTROL4);

	mdelay(1);
	
	return 0;
}

static unsigned int sdp1307_get_curr_temp(struct sdp_tmu_info *info)
{
	int temp = 0;
	static int print_delay = PRINT_RATE;

	if (info->sensor_id >= MAX_SENSOR_CNT) {
		printk(KERN_ERR "TMU ERROR - sensor id is not avaiable. %d\n", info->sensor_id);
		return 0;
	}

	/* get temperature from TSC register */
	temp = (readl(info->tmu_base + SDP_TSC_CONTROL5) >> TSC_TEM_T_TS_8BIT_TS) & 0xFF;
	temp = temp + diff_val - (TSC_DEGREE_25 - 25);
	if (temp < 0)
		temp = 0;

	/* Temperature is printed every PRINT_RATE. */ 
	if (info->temp_print_on || info->throttle_print_on) {
		print_delay -= SAMPLING_RATE;
		if (print_delay <= 0) {
			printk(KERN_INFO "\033[1;7;33mT^%d'C\033[0m\n", temp);
			print_delay = PRINT_RATE;
		}
	}
	
	return (unsigned int)temp;
}


int sdp1307_tmu_init(struct sdp_tmu_info * info)
{
	info->enable_tmu = sdp1307_enable_tmu;
	info->get_curr_temp = sdp1307_get_curr_temp;
	
	return 0;
}

