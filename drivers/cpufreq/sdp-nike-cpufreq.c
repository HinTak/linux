/* drivers/cpufreq/sdp-nike-cpufreq.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * NIKE-M/L CPU frequency scaling support
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
#include <soc/sdp/soc.h>

#include <asm/cacheflush.h>

#ifdef CONFIG_SDP_THERMAL	
#include <soc/sdp/sdp_thermal.h>
#endif

#define CPUFREQ_LEVEL_END	L5
#define CPUFREQ_ASV_COUNT	5

extern bool sdp_cpufreq_print_on;

static DEFINE_SPINLOCK(freq_lock);

static struct clk *finclk;

static unsigned int max_support_idx;
static unsigned int min_support_idx = L3;
static unsigned int max_real_idx = L0;
static unsigned int min_real_idx = L4;	/* for thermal throttle */

static struct cpufreq_frequency_table sdp_nike_freq_table[] = {
	{ 0, L0, 1700*1000},
	{ 0, L1, 1000*1000},
	{ 0, L2,  500*1000},
	{ 0, L3,  250*1000},
	{ 0, L4,  100*1000},
	{ 0,  0, CPUFREQ_TABLE_END},
};

/* voltage table (uV scale) */
static unsigned int sdp_nike_volt_table[CPUFREQ_LEVEL_END];

static const unsigned int sdp_nike_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*  ASV0,    ASV1,    ASV2,    ASV3,     ASV4,*/
	{1020000,  980000,  940000,  910000,  870000,}, /* 1700 L0  */
	{ 940000,  940000,  920000,  870000,  870000,}, /* 1000 L1  */
	{ 940000,  940000,  920000,  870000,  870000,}, /*  500 L2 */
	{ 940000,  940000,  920000,  870000,  870000,}, /*  250 L3 */
	{ 940000,  940000,  920000,  870000,  870000,}, /*  125 L4 */
};

static void __iomem *g_clkbase;
static void __iomem *g_pmubase;

#define REG_SDP_NIKE_CLKBASE 0x00f50000
#define REG_SDP_NIKE_PMUBASE 0x00400000

#define REG_SDP_NIKE_PLL_CPU_PMS     (g_clkbase + 0x001c)
#define REG_SDP_NIKE_PLL_CPU_K       (g_clkbase + 0x0020)
#define REG_SDP_NIKE_PWM_CLK_CON     (g_pmubase + 0x0008)
#define USE_DVFS_CLOCKS         (1 << 16)
#define SEL_DVFSHALF            12
#define SEL_ARM_VS_DVFS         (1 << 8)
#define PLL_CPU_RESET_N         (1 << 24)
#define CPUFREQ_TEMP_FREQ       (250000);

#define ARM_DVFS_CLK_FREQ 1000000000
static unsigned int sdp_nike_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int pms;
	unsigned int infreq;
	unsigned int div;

	if (finclk)
		infreq = clk_get_rate(finclk);
	else
		infreq = 24576000UL;

	if (readl((void *)REG_SDP_NIKE_PWM_CLK_CON) & SEL_ARM_VS_DVFS) {
		pms = readl((void *)REG_SDP_NIKE_PLL_CPU_PMS);
		ret = (infreq >> (pms & 0x7)) / ((pms >> 20) & 0x3F);
		ret *= ((pms >> 8) & 0x3FF); 
		ret = ((ret + 50000000) / 100000000) * 100000000; /* convert to 100MHz scale */
	}
	else {
		div = (readl((void *)REG_SDP_NIKE_PWM_CLK_CON) >> SEL_DVFSHALF) & 0x3;
		ret = ARM_DVFS_CLK_FREQ / (1<<div);
	}

	return ret;
}

static void set_volt_table(int result)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu clock */
	freq = sdp_nike_get_speed(0);
	sdp_nike_freq_table[L0].frequency = freq/1000;

	max_support_idx = L0;

	pr_info("DVFS: cur CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp_nike_freq_table[L0].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp_nike_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT)
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			sdp_nike_volt_table[i] = sdp_nike_asv_voltage[i][result];
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
			sdp_nike_volt_table[i] = sdp_nike_asv_voltage[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

static void sdp_nike_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	u32 val;

	if (old_index == L0 && new_index == L0) {
		; /* do nothing : same frequency */
	}
	else if (old_index == L0 && new_index > L0) {
		val = readl((void *)REG_SDP_NIKE_PWM_CLK_CON) & (~(0x3 << SEL_DVFSHALF));
		val |= (u32)((new_index-1)&0x3) << SEL_DVFSHALF; 
		writel(val, (void *)REG_SDP_NIKE_PWM_CLK_CON);

		val = readl((void *)REG_SDP_NIKE_PWM_CLK_CON);
		val &= ~SEL_ARM_VS_DVFS;
		writel(val, (void *)REG_SDP_NIKE_PWM_CLK_CON);
	}
	else if (old_index > L0 && new_index == L0) {
		val = readl((void *)REG_SDP_NIKE_PWM_CLK_CON);
		val |= SEL_ARM_VS_DVFS;
		writel(val, (void *)REG_SDP_NIKE_PWM_CLK_CON);	
	}	
	else {
		; /* do nothing : because we have no tempclk for this case */
	}
}

static void sdp_nike_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
	if (sdp_cpufreq_print_on)
		printk("@$%u\n", sdp_nike_freq_table[new_index].frequency/10000);


	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp_nike_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

int sdp_nike_cpufreq_init(struct sdp_dvfs_info *info)
{
	u32 val;

	g_clkbase = ioremap(REG_SDP_NIKE_CLKBASE, 0x1000);
	if (!g_clkbase) {
		return -ENOMEM;
	}
	g_pmubase = ioremap(REG_SDP_NIKE_PMUBASE, 0x1000);
	if (!g_pmubase) {
		iounmap(g_clkbase);
		return -ENOMEM;
	}
	
	info->cur_group = 0;

	/* select temp clock to ams clock(sel_ftest_ams) */
	val = readl((void *)REG_SDP_NIKE_PWM_CLK_CON) | USE_DVFS_CLOCKS;
	writel(val, (void *)REG_SDP_NIKE_PWM_CLK_CON);

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
	info->volt_table = sdp_nike_volt_table;
	info->freq_table = sdp_nike_freq_table;
	info->set_freq = sdp_nike_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp_nike_get_speed;
	
	return 0;	
}
EXPORT_SYMBOL(sdp_nike_cpufreq_init);


