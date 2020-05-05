/* linux/arch/arm/mach-sdp/sdp_soc/sdp1302/sdp1302_cpufreq.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1302 - CPU frequency scaling support
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

#include <asm/cacheflush.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/soc.h>
#include <plat/cpufreq.h>

#define CPUFREQ_LEVEL_END	L12

#ifdef MAX_AP_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_AP_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif


static DEFINE_SPINLOCK(freq_lock);

static unsigned int max_support_idx;
static unsigned int min_support_idx = L10;
static unsigned int max_real_idx = L1;
static unsigned int min_real_idx = L11;

static struct clk *cpu_clk;
static struct clk *in_clk;
static u32 in_freq;

struct tmp_clk_div {
	int freq;
	int set_val;
};

static struct tmp_clk_div temp_div_table[4] = {
	{960000, 1},
	{480000, 0},
	{240000, 3},
	{120000, 2},
};

static struct cpufreq_frequency_table sdp1302_freq_table[] = {
	{ L0, 1200*1000},
	{ L1, 1100*1000},
	{ L2, 1000*1000},
	{ L3,  900*1000},
	{ L4,  800*1000},
	{ L5,  700*1000},
	{ L6,  600*1000},
	{ L7,  500*1000},	
	{ L8,  400*1000},	
	{ L9,  300*1000},	
	{L10,  200*1000},	
	{L11,  100*1000},
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
@24.576 1200  	0x612501
@24.576 1150  	0x611901
@24.576 1100  	0x610D01
@24.576 1050  	0x40AB01
@24.576 1001  	0xA19701
@24.576  900  	0x812501
@24.576  800  	0x912501
@24.576  700  	0x60AB01
@24.576  600  	0xC12501
@24.576  500	0xA19702
@24.576  400	0x912502
@24.576  300	0xC12502
@24.576  200	0x912503
@24.576  100	0x912504
@===========================================================================
*/
static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x612501, /* 1200 L0 */
	0x610D01, /* 1100 L1 */
	0xA19701, /* 1000 L2 */
	0x812501, /*  900 L3 */
	0x912501, /*  800 L4 */
	0x60AB01, /*  700 L5 */
	0xC12501, /*  600 L6 */
	0xA19702, /*  500 L7 */
	0x912502, /*  400 L8 */
	0xC12502, /*  300 L9 */
	0x912503, /*  200 L10 */
	0x912504, /*  100 L11 , only for emergency situation */
};

#if 0
/*
@===========================================================================
@PLL PMS Table
@===========================================================================
@CPU	
@---------------------------------------------------------------------------
@FIN FOUT Value		
@---------------------------------------------------------------------------
@24 1200  	0x612C01
@24 1100  	0x611301
@24 1000  	0x60FA01
@24  900  	0x609601
@24  800  	0x60C801
@24  700  	0x60AF01
@24  600  	0x507D01
@24  500	0x60FA02
@24  400	0x60C802
@24  300	0x507D02
@24  200	0x60C803
@24  100	0x60C804
@===========================================================================
*/
static unsigned int clkdiv_cpu_24[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x612C01, /* 1200 L0 */
	0x611301, /* 1100 L1 */
	0x60FA01, /* 1000 L2 */
	0x609601, /*  900 L3 */
	0x60C801, /*  800 L4 */
	0x60AF01, /*  700 L5 */
	0x507D01, /*  600 L6 */
	0x60FA02, /*  500 L7 */
	0x60C802, /*  400 L8 */
	0x507D02, /*  300 L9 */
	0x60C803, /*  200 L10 */
	0x60C804, /*  100 L11 , only for emergency situation */
};
#endif

/* voltage table */
/* uV scale */
static unsigned int sdp1302_volt_table[CPUFREQ_LEVEL_END];

/* voltage table */
static const unsigned int sdp1302_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000}, /* L0 */
	{ 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000}, /* L1 */
	{ 1350000, 1350000, 1350000, 1340000, 1320000, 1230000, 1200000, 1200000, 1200000, 1200000}, /* L2, 1000 */
	{ 1350000, 1340000, 1330000, 1310000, 1290000, 1200000, 1170000, 1170000, 1170000, 1170000}, /* L3 */
	{ 1350000, 1300000, 1290000, 1240000, 1230000, 1150000, 1120000, 1120000, 1120000, 1120000}, /* L4 */
	{ 1350000, 1280000, 1270000, 1220000, 1220000, 1110000, 1090000, 1090000, 1090000, 1090000}, /* L5 */
	{ 1350000, 1240000, 1240000, 1180000, 1170000, 1060000, 1040000, 1040000, 1040000, 1040000}, /* L6 */
	{ 1350000, 1230000, 1220000, 1160000, 1140000, 1030000, 1010000, 1010000, 1010000, 1010000}, /* L7 */
	{ 1350000, 1170000, 1160000, 1120000, 1100000, 1010000,  960000,  960000,  960000,  960000}, /* L8 */
	{ 1350000, 1130000, 1120000, 1070000, 1050000, 1010000,  960000,  960000,  960000,  960000}, /* L9 */
	{ 1350000, 1110000, 1100000, 1050000, 1030000, 1010000,  960000,  960000,  960000,  960000}, /* L10 */
	{ 1350000, 1110000, 1100000, 1050000, 1030000, 1010000,  960000,  960000,  960000,  960000}, /* L11 */
};

static const unsigned int sdp1302_asv_voltage_lfd[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000}, /* L0 */
	{ 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000, 1350000}, /* L1 */
	{ 1350000, 1350000, 1350000, 1340000, 1320000, 1230000, 1200000, 1200000, 1200000, 1200000}, /* L2, 1000 */
	{ 1350000, 1340000, 1330000, 1310000, 1290000, 1200000, 1170000, 1170000, 1170000, 1170000}, /* L3 */
	{ 1350000, 1300000, 1290000, 1240000, 1230000, 1150000, 1120000, 1120000, 1120000, 1120000}, /* L4 */
	{ 1350000, 1280000, 1270000, 1220000, 1220000, 1110000, 1090000, 1090000, 1090000, 1090000}, /* L5 */
	{ 1350000, 1240000, 1240000, 1180000, 1170000, 1060000, 1040000, 1040000, 1040000, 1040000}, /* L6 */
	{ 1350000, 1230000, 1220000, 1160000, 1140000, 1030000, 1010000, 1010000, 1010000, 1010000}, /* L7 */
	{ 1350000, 1170000, 1160000, 1120000, 1100000, 1010000,  960000,  960000,  960000,  960000}, /* L8 */
	{ 1350000, 1130000, 1120000, 1070000, 1050000, 1010000,  960000,  960000,  960000,  960000}, /* L9 */
	{ 1350000, 1110000, 1100000, 1050000, 1030000, 1010000,  960000,  960000,  960000,  960000}, /* L10 */
	{ 1350000, 1110000, 1100000, 1050000, 1030000, 1010000,  960000,  960000,  960000,  960000}, /* L11 */
};

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

extern ktime_t ktime_get(void);
static inline void cpufreq_udelay(u32 us)
{
	ktime_t stime = ktime_get();

	while ((ktime_us_delta(ktime_get(), stime)) < us);
}
#define SDP1302_REG_CLK_CTRL		(VA_SFR0_BASE + 0xB70030)
#define PLL_SEL				(1 << 8)
#define DVFS_PLL_SEL			(12)
#define PLL_FN_SEL			(1 << 16)
#define SDP1302_REG_CPU_PLL		(VA_SFR0_BASE + 0x90800)
#define SDP1302_REG_PLL_LOCK		(VA_SFR0_BASE + 0x908A0)
#define CPU_PLL_LOCK			(1 << 0)
#define SDP1302_REG_CPU_PLL_PWD		(0x100908A4)
#define CPU_PLL_PWD			(1 << 0)
#define SDP1302_REG_PLL_LOCK_CNT	(VA_SFR0_BASE + 0x90874)
#define SDP1302_AFE_ENB			(VA_SFR0_BASE + 0x90844)
static void sdp1302_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	unsigned int val;
	int tmp_div;
	
	/* freq up direction */
	if (old_index > new_index && new_index <= L2) {
		/*
		 * when frequency up case (xHz -> max freq)
		 * temp frequency need to be 480MHz
		 * temp freq = 960MHz / (2 ^ tmp_div)
		 */
		if (old_index <= L7) /* x >= 500MHz -> 960MHz */
			tmp_div = 0;
		else /* x < 500MHz -> 480MHz */
			tmp_div = 1;
	/* freq down direction */
	} else {
		/*
		 * when frequency down case,
		 * temp freq selection algorithm is
		 * 3 - MSB(old freq / 120MHz)
		 * temp freq = 960MHz / (2 ^ tmp_div)
		 */
		tmp_div = 3; /* 120MHz fix */
	}

	/* select temp clock source */
	val = __raw_readl((void *)SDP1302_REG_CLK_CTRL);
	val &= ~(0x3 << DVFS_PLL_SEL);
	val |= temp_div_table[tmp_div].set_val << DVFS_PLL_SEL;
	__raw_writel(val, (void *)SDP1302_REG_CLK_CTRL);
	
	/* change CPU clock source to Temp clock (OSC clk -> DVFS PLL) */
	val = __raw_readl((void *)SDP1302_REG_CLK_CTRL);
	val |= PLL_FN_SEL;
	__raw_writel(val, (void *)SDP1302_REG_CLK_CTRL);

	val = __raw_readl((void *)SDP1302_REG_CLK_CTRL);
	val &= ~PLL_SEL;
	__raw_writel(val, (void *)SDP1302_REG_CLK_CTRL);

	/* change CPU pll value */
	writel(0x4100, (void *)SDP1302_REG_PLL_LOCK_CNT); /* set count value to 100us */
	/* set AFE_ENB to 0 */
	val = readl((void *)SDP1302_AFE_ENB) & ~(1 << 12);
	writel(val, (void *)SDP1302_AFE_ENB);
	sdp_set_clockgating(SDP1302_REG_CPU_PLL_PWD, CPU_PLL_PWD, 0); /* pwd off */
	__raw_writel(clkdiv_cpu[new_index], (void *)SDP1302_REG_CPU_PLL);
	cpufreq_udelay(1);
	sdp_set_clockgating(SDP1302_REG_CPU_PLL_PWD, CPU_PLL_PWD, CPU_PLL_PWD); /* pwd on */

	/* wait PLL lock */
	while (CPU_PLL_LOCK != (__raw_readl((void *)SDP1302_REG_PLL_LOCK) & CPU_PLL_LOCK));

	/* change CPU clock source to PLL clock */
	val = __raw_readl((void *)SDP1302_REG_CLK_CTRL);
	val |= PLL_SEL;
	__raw_writel(val, (void *)SDP1302_REG_CLK_CTRL);
}

static void sdp1302_set_frequency(unsigned int old_index, unsigned int new_index)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
#if defined(CONFIG_ARM_SDP1302_CPUFREQ_DEBUG)
	printk("@$%u\n", sdp1302_freq_table[new_index].frequency/10000);
#endif

	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp1302_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

#define GET_P_VALUE(x)	((x >> 20) & 0x3F)
#define GET_M_VALUE(x)	((x >> 8) & 0x3FF)
#define GET_S_VALUE(x)	(x & 0x7)
static unsigned int sdp1302_get_speed(void)
{
	unsigned int ret;
	unsigned int pms;

	pms = readl((void *)SDP1302_REG_CPU_PLL);

	ret = (in_freq >> (GET_S_VALUE(pms) - 1));
	ret /= GET_P_VALUE(pms);
	ret *= GET_M_VALUE(pms);
	
	/* convert to 10MHz scale */
	ret = (ret / 10000000) * 10000000;

	return ret;
}

extern enum sdp_board get_sdp_board_type(void);

static void set_volt_table(void)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu's clock */
	freq = sdp1302_get_speed() / 1000000;
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1302_freq_table[i].frequency/1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END) {
		max_support_idx = i;
		pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
				freq, sdp1302_freq_table[i].frequency/1000);
	} else {
		max_support_idx = L2;
		pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
				freq, sdp1302_freq_table[max_support_idx].frequency/1000);
	}
	
	for (i = L0; i < max_real_idx; i++)
		sdp1302_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", sdp_result_of_asv);

	if (sdp_result_of_asv < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			if (get_sdp_board_type() == SDP_BOARD_LFD) {
				sdp1302_volt_table[i] = 
					sdp1302_asv_voltage_lfd[i][sdp_result_of_asv];
			} else {
				sdp1302_volt_table[i] = 
					sdp1302_asv_voltage[i][sdp_result_of_asv];
			}
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, sdp_result_of_asv);
	}
}

#if 0
static void set_clkdiv_table(void)
{
	int i;

	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if (in_freq == 24576000)
			clkdiv_cpu[i] = clkdiv_cpu_24_576[i];
		else
			clkdiv_cpu[i] = clkdiv_cpu_24[i];
	}
}
#endif

static void update_volt_table(void)
{
	int i;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", sdp_result_of_asv);

	if (sdp_result_of_asv < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			if (get_sdp_board_type() == SDP_BOARD_LFD) {
				sdp1302_volt_table[i] = 
					sdp1302_asv_voltage_lfd[i][sdp_result_of_asv];
			} else {
				sdp1302_volt_table[i] = 
					sdp1302_asv_voltage[i][sdp_result_of_asv];
			}
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, sdp_result_of_asv);
	}
}

int sdp1302_cpufreq_init(struct sdp_dvfs_info *info)
{
	printk(KERN_INFO "DVFS: %s\n", __func__);

	in_clk = clk_get(NULL, "fin_pll");
	if (IS_ERR(in_clk)) {
		printk(KERN_ERR "%s = fin_pll clock get fail", __func__);
		return PTR_ERR(in_clk);
	}
	
	in_freq = clk_get_rate(in_clk);
	printk(KERN_INFO "DVFS: input freq = %d\n", in_freq);
	
	cpu_clk = clk_get(NULL, "arm_clk");
	if (IS_ERR(cpu_clk)) {
		/* error */
		printk(KERN_ERR "%s - arm clock get fail", __func__);
		return PTR_ERR(cpu_clk);
	}

	set_volt_table();

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = sdp1302_volt_table;
	info->freq_table = sdp1302_freq_table;
	info->set_freq = sdp1302_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1302_get_speed;

	return 0;	
}
EXPORT_SYMBOL(sdp1302_cpufreq_init);
