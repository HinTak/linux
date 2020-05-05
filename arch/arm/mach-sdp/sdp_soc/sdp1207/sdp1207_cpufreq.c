/* linux/arch/arm/mach-ccep/ccep_soc/sdp1207/sdp1207_cpufreq.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1202 - CPU frequency scaling support
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
//#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <plat/cpufreq.h>
//#include <plat/asv.h>

unsigned int sdp_result_of_asv;
unsigned int sdp_asv_stored_result;

#define CPUFREQ_LEVEL_END	L9
#define CPUFREQ_ASV_COUNT	12

static unsigned int max_support_idx;
static unsigned int min_support_idx = L8;
static int min_real_idx = L8;

//static spinlock_t cpufreq_lock;

static struct clk *cpu_clk;

static struct cpufreq_frequency_table sdp1207_freq_table[] = {
	{ L0, 912*1000},
	{ L1, 792*1000},
	{ L2, 696*1000},
	{ L3, 600*1000},
	{ L4, 504*1000},
	{ L5, 408*1000},
	{ L6, 288*1000},
	{ L7, 192*1000},
	{ L8, 96*1000},
	{0, CPUFREQ_TABLE_END},
};

/*
@===========================================================================
@PLL PMS Table
@===========================================================================
@CPU
@---------------------------------------------------------------------------
@FIN FOUT Value			
@---------------------------------------------------------------------------
@24  100  0x
@24  200  0x
@24  300  0x
@24  400  0x
@24  500  0x
@24  600  0x1814
@24  696  0x1C14
@24  792  0x2014
@24  912  0x2514
@===========================================================================
*/
static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x2514, /* 912 L0 */
	0x2014, /* 792 L1 */
	0x1C14, /* 696 L2 */
	0x1814, /* 600 L3 */
	0x1414, /* 504 L4? */
	0x1014, /* 408 L5? */
	0x0b14, /* 288 L6? */
	0x0714, /* 192 L7? */
	0x0314, /* 96 L8? */
};

static struct cpufreq_timerdiv_table sdp1207_timerdiv_table[CPUFREQ_LEVEL_END] = {
	{256, 8}, /* L0 */
	{256, 8}, /* L1 */
	{256, 8}, /* L2 */
	{256, 8}, /* L3 */
	{256, 8}, /* L4 */
	{256, 8}, /* L5 */
	{256, 8}, /* L6 */
	{256, 8}, /* L7 */
	{256, 8}, /* L8 */
};

/* voltage table */
/* uV scale */
static unsigned int sdp1207_volt_table[CPUFREQ_LEVEL_END];

/* voltage table */
static const unsigned int sdp1207_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L0 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L1 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L2 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L3 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L4 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L5 */
	{ 1170000, 1170000, 1150000, 1120000, 1110000, 1080000, 1070000, 1060000, 1060000, 1060000, 1060000, 1060000}, /* L6 */
	{ 1170000, 1130000, 1130000, 1080000, 1060000, 1040000, 1040000, 1020000, 1020000, 1020000, 1020000, 1020000}, /* L7 */
	{ 1170000, 1070000, 1060000, 1060000, 1010000, 1000000, 1000000,  980000,  980000,  980000,  980000,  980000}, /* L8 */
};

#define SDP1207_CLK_MUX_SEL		(VA_PMU_BASE + 0xA4)
#define ARM_PLL_OUT_SEL			(1 << 5)
#define SDP1207_PLL1_CONT		(VA_PMU_BASE + 0x4)
#define SDP1207_PLL1_LOCKCNT	(VA_PMU_BASE + 0x34)
#define SDP1207_PLL_LOCK		(VA_PMU_BASE + 0x4C)
#define SDP1207_PLL_PWD			(VA_PMU_BASE + 0x50)
static DEFINE_SPINLOCK(freq_lock);
static void sdp1207_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&freq_lock, flags);

	/* select temp clock source (Audio 496MHz) */
	
	/* change CPU clock source to Temp clock (Audio 496MHz) */
	val = readl((void *)SDP1207_CLK_MUX_SEL) & ~ARM_PLL_OUT_SEL;
	writel(val, (void *)SDP1207_CLK_MUX_SEL);

	/* PLL Power off */
	val = readl((void *)SDP1207_PLL_PWD) & ~(1 << 1);
	writel(val, (void *)SDP1207_PLL_PWD);

	/* change CPU pll value */
	writel(clkdiv_cpu[new_index], (void *)SDP1207_PLL1_CONT);

	/* pll lock count */
	writel(0x7FFF, (void *)SDP1207_PLL1_LOCKCNT);

	/* PLL Power on */
	val = readl((void *)SDP1207_PLL_PWD) | (1 << 1);
	writel(val, (void *)SDP1207_PLL_PWD);
	
	/* wait PLL lock */
	while ((readl((void *)SDP1207_PLL_LOCK) & (1 << 1)) == 0);

	/* change CPU clock source to PLL clock */
	val = readl((void *)SDP1207_CLK_MUX_SEL) | ARM_PLL_OUT_SEL;
	writel(val, (void *)SDP1207_CLK_MUX_SEL);

	spin_unlock_irqrestore(&freq_lock, flags);
}

static void sdp1207_set_frequency(unsigned int old_index, unsigned int new_index)
{
	/* Change the system clock divider values */
#if defined(CONFIG_CPU_FREQ_SDP1207_DEBUG)
	printk("@$%u\n", sdp1207_freq_table[new_index].frequency/10000);
#endif

	/* change cpu frequnecy */
	sdp1207_set_clkdiv(old_index, new_index);
}

static void set_volt_table(void)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu's clock */
	freq = clk_get_rate(cpu_clk);
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1207_freq_table[i].frequency*1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;
	else
		max_support_idx = L0;

	pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp1207_freq_table[i].frequency/1000);
	
	for (i = L0; i < max_support_idx; i++)
		sdp1207_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", sdp_result_of_asv);

	if (sdp_result_of_asv < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1207_volt_table[i] = 
				sdp1207_asv_voltage[i][sdp_result_of_asv];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, sdp_result_of_asv);
	}
}

static void set_timerdiv_table(void)
{
	int i;

	for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
		if (sdp1207_freq_table[i].frequency == (unsigned int)CPUFREQ_ENTRY_INVALID)
			sdp1207_timerdiv_table[i].mult = 1 << 8;
		else
			sdp1207_timerdiv_table[i].mult = ((sdp1207_freq_table[i].frequency/1000)<<8) /
											(sdp1207_freq_table[max_support_idx].frequency/1000);
		sdp1207_timerdiv_table[i].shift = 8;
	}
}

static void update_volt_table(void)
{
	int i;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", sdp_result_of_asv);

	if (sdp_result_of_asv < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1207_volt_table[i] = 
				sdp1207_asv_voltage[i][sdp_result_of_asv];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, sdp_result_of_asv);
	}
}

static bool sdp1207_pms_change(unsigned int old_index, unsigned int new_index)
{
	unsigned int old_pm = clkdiv_cpu[old_index];
	unsigned int new_pm = clkdiv_cpu[new_index];

	return (old_pm == new_pm) ? 0 : 1;
}

int sdp_cpufreq_mach_init(struct sdp_dvfs_info *info)
{
	cpu_clk = clk_get(NULL, "fclk");
	if (IS_ERR(cpu_clk)) {
		/* error */
		printk(KERN_ERR "%s - clock get fail", __func__);
		return PTR_ERR(cpu_clk);
	}

	set_volt_table();
	set_timerdiv_table();
	
	info->pll_safe_idx = L5;
	info->pm_lock_idx = L5;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = sdp1207_volt_table;
	info->freq_table = sdp1207_freq_table;
	info->div_table = sdp1207_timerdiv_table;
	info->set_freq = sdp1207_set_frequency;
	info->need_apll_change = sdp1207_pms_change;
//	info->get_speed = sdp1207_getspeed;
	info->update_volt_table = update_volt_table;

	return 0;	
}
EXPORT_SYMBOL(sdp_cpufreq_mach_init);

