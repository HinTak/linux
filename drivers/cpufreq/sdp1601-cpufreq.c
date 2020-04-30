/* drivers/cpufreq/sdp1601-cpufreq.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1601 Kant-M CPU frequency scaling support
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
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/power/sdp_asv.h>
#include <linux/platform_data/sdp-cpufreq.h>
#include <linux/of.h>

#include <asm/cacheflush.h>

#define FREQ_LOCK_TIME		50
#define CPUFREQ_LEVEL_END	L17

#ifdef MAX_CPU_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_CPU_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif

extern bool sdp_cpufreq_print_on;

static DEFINE_SPINLOCK(freq_lock);

static struct clk *finclk;

static unsigned int max_support_idx;
static unsigned int min_support_idx = L14;
static unsigned int max_real_idx = L0;
static unsigned int min_real_idx = L16;	/* for thermal throttle */

static struct cpufreq_frequency_table sdp1601_freq_table[] = {
	{ 0, L0, 1700*1000},
	{ 0, L1, 1600*1000},	
	{ 0, L2, 1500*1000},	
	{ 0, L3, 1400*1000},
	{ 0, L4, 1300*1000},
	{ 0, L5, 1200*1000},	
	{ 0, L6, 1100*1000},	
	{ 0, L7, 1000*1000},
	{ 0, L8,  900*1000},
	{ 0, L9,  800*1000},
	{ 0,L10,  700*1000},
	{ 0,L11,  600*1000},
	{ 0,L12,  500*1000},
	{ 0,L13,  400*1000},
	{ 0,L14,  300*1000},
	{ 0,L15,  200*1000},
	{ 0,L16,  100*1000},
	{ 0,  0, CPUFREQ_TABLE_END},
};

#define PMU_PMS_ARM_100		
#define PMU_PMS_ARM_200		
#define PMU_PMS_ARM_300		
#define PMU_PMS_ARM_400		
#define PMU_PMS_ARM_500		
#define PMU_PMS_ARM_600		
#define PMU_PMS_ARM_700		
#define PMU_PMS_ARM_800		
#define PMU_PMS_ARM_900		
#define PMU_PMS_ARM_1000	
#define PMU_PMS_ARM_1100	
#define PMU_PMS_ARM_1200	
#define PMU_PMS_ARM_1300	
#define PMU_PMS_ARM_1400	
#define PMU_PMS_ARM_1500	
#define PMU_PMS_ARM_1600	
#define PMU_PMS_ARM_1700	

static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x00619f00, /* 1700 L0  */
	0x00528b01, /* 1600 L1  */
	0x00316e01, /* 1500 L2  */
	0x00315601, /* 1400 L3  */
	0x00521101, /* 1300 L4  */
	0x00312501, /* 1200 L5  */
	0x00416601, /* 1100 L6  */
	0x00519701, /* 1000 L7  */
	0x00412501, /*  900 L8  */
	0x00528b02, /*  800 L9  */
	0x00315602, /*  700 L10 */
	0x00312502, /*  600 L11 */
	0x00519702, /*  500 L12 */
	0x00528b03, /*	400 L13	*/
	0x00312503, /*	300 L14 */
	0x00528b04, /*	200 L15 */
	0x00528b05, /*	100 L16 */
	
};

/* voltage table (uV scale) */
static unsigned int sdp1601_volt_table[CPUFREQ_LEVEL_END];

static const unsigned int sdp1601_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*  ASV0,    ASV1,    ASV2,    ASV3,	ASV4,	 ASV5,	  ASV6,   ASV7,     ASV8,    ASV9 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1700 L0  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1600 L1  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1500 L2  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1400 L3  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1300 L4  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1200 L5  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1100 L6  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1000 L7  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  900 L8  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  800 L9  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  700 L10 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  600 L11 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  500 L12 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  400 L13  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  300 L14 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  200 L15 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  100 L16 */		
};

static void __iomem *g_clkbase;
static void __iomem *g_pmubase;

#define REG_SDP1601_CLKBASE 0x00F40000 
#define REG_SDP1601_PMUBASE 0x00400000

#define REG_SDP1601_PLL_CPU_PMS		(g_clkbase + 0x0000)
#define REG_SDP1601_PWM_CLK_CON		(g_pmubase + 0x0008)
#define USE_DVFS_CLOCKS			(1 << 16)
#define SEL_DVFSHALF			12
#define SEL_ARM_VS_DVFS			(1 << 8)
#define REG_SDP1601_PLL_CPU_CTRL	(g_clkbase + 0x000C)
#define PLL_CPU_LOCK_EN			(1 << 0)
#define REG_SDP1601_PLL_RESET_N_CTRL	(0x00F40190) /* must use lock, physical address */
#define PLL_CPU_RESET_N			(1 << 20)
#define REG_SDP1601_PLL_CPU_LOCK	(g_clkbase + 0x06D0)
#define CPU_PLL_LOCK			(1 << 0)
#define CPUFREQ_TEMP_FREQ		(250000);

static unsigned int sdp1601_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int pms;
	unsigned int infreq;

	if (finclk)
		infreq = clk_get_rate(finclk);
	else
		infreq = 24576000UL;

	pms = readl((void *)REG_SDP1601_PLL_CPU_PMS);

	ret = (infreq >> (pms & 0x7)) / ((pms >> 20) & 0x3F);
	ret *= ((pms >> 8) & 0x3FF); 
	
	/* convert to 10MHz scale */
	ret = ((ret + 5000000) / 10000000) * 10000000;

	return ret;
}

static void set_volt_table(int result)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu clock */
	freq = sdp1601_get_speed(0);
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1601_freq_table[i].frequency*1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;

	pr_info("DVFS: cur CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp1601_freq_table[i].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp1601_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT)
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1601_volt_table[i] = sdp1601_asv_voltage[i][result];
		}
	else
		pr_err("%s: asv table index error. %d\n", __func__, result);
}

static void update_volt_table(int result)
{
	int i;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1601_volt_table[i] = sdp1601_asv_voltage[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

static void sdp1601_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	u32 val;
	u8 tmp_div;
	
	/* calculate temp clock */
	if (old_index > new_index) {
		if (sdp1601_freq_table[new_index].frequency > 1000000)
			tmp_div = 1;
		else if (sdp1601_freq_table[new_index].frequency > 500000)
			tmp_div = 2;
		else
			tmp_div = 3;
	} else {
		/*
		 * when frequency down case,
		 * temp freq selection algorithm is
		 * 3 - MSB(old freq / 250MHz)
		 */
		tmp_div = (int)sdp1601_freq_table[old_index].frequency / CPUFREQ_TEMP_FREQ;
		tmp_div = 3 - (fls((int)tmp_div) - 1);
		if (tmp_div > 3)
			tmp_div = 3;
	}

	/* select temp clock to ams clock(sel_ftest_ams) */
	val = readl((void *)REG_SDP1601_PWM_CLK_CON) | USE_DVFS_CLOCKS;
	writel(val, (void *)REG_SDP1601_PWM_CLK_CON);

	/* set the mux to selected ams clock(sel_ams_half) */
	val = readl((void *)REG_SDP1601_PWM_CLK_CON) & (~(0x3 << SEL_DVFSHALF));
	val |= (u32)tmp_div << SEL_DVFSHALF; /* AMS clk div */
	writel(val, (void *)REG_SDP1601_PWM_CLK_CON);

	/* change CPU clock source to Temp clock(sel_arm_ams) */
	val = readl((void *)REG_SDP1601_PWM_CLK_CON);
	val &= ~SEL_ARM_VS_DVFS;
	writel(val, (void *)REG_SDP1601_PWM_CLK_CON);

	/* pll lock enable(pll_cpu_locken) */
	val = readl((void *)REG_SDP1601_PLL_CPU_CTRL) | PLL_CPU_LOCK_EN;
	writel(val, (void *)REG_SDP1601_PLL_CPU_CTRL);

	/* PWD off */
	sdp_set_clockgating(REG_SDP1601_PLL_RESET_N_CTRL, PLL_CPU_RESET_N, 0);

	/* change CPU pll value */
	writel(clkdiv_cpu[new_index], (void *)(REG_SDP1601_PLL_CPU_PMS));

	/* PWD on */
	sdp_set_clockgating(REG_SDP1601_PLL_RESET_N_CTRL, PLL_CPU_RESET_N, PLL_CPU_RESET_N);

	/* wait PLL lock */
	while (!(readl((void *)REG_SDP1601_PLL_CPU_LOCK) & CPU_PLL_LOCK));

	/* change CPU clock source to ARM PLL(sel_arm_ams) */
	val = readl((void *)REG_SDP1601_PWM_CLK_CON);
	val |= SEL_ARM_VS_DVFS;
	writel(val, (void *)REG_SDP1601_PWM_CLK_CON);	
}

static void sdp1601_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
	if (sdp_cpufreq_print_on)
		printk("@$%u\n", sdp1601_freq_table[new_index].frequency/10000);


	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp1601_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

int sdp1601_cpufreq_init(struct sdp_dvfs_info *info)
{
	u32 val;

	g_clkbase = ioremap(REG_SDP1601_CLKBASE, 0x1000);
	if (!g_clkbase) {
		return -ENOMEM;
	}
	g_pmubase = ioremap(REG_SDP1601_PMUBASE, 0x1000);
	if (!g_pmubase) {
		iounmap(g_clkbase);
		return -ENOMEM;
	}
	
	info->cur_group = 0;

	/* select temp clock to ams clock(sel_ftest_ams) */
	val = readl((void *)REG_SDP1601_PWM_CLK_CON) | USE_DVFS_CLOCKS;
	writel(val, (void *)REG_SDP1601_PWM_CLK_CON);

	/* pll lock enable(pll_cpu_locken) */
	val = readl((void *)REG_SDP1601_PLL_CPU_CTRL) | PLL_CPU_LOCK_EN;
	writel(val, (void *)REG_SDP1601_PLL_CPU_CTRL);

	/* get fin clock */
	finclk = clk_get(NULL, "fin_pll");
	if (!finclk)
		pr_err("finclk get fail\n");
	
	/* set default AVS off table */	
	set_volt_table(info->cur_group);

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->volt_table = sdp1601_volt_table;
	info->freq_table = sdp1601_freq_table;
	info->set_freq = sdp1601_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1601_get_speed;
	
	return 0;	
}
EXPORT_SYMBOL(sdp1601_cpufreq_init);
