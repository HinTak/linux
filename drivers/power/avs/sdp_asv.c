/* linux/arch/arm/plat-sdp/sdp_asv.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * SDP - ASV(Adaptive Supply Voltage) driver
 *
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/power/sdp_asv.h>
#include <linux/notifier.h>
#include <linux/suspend.h>

#include <mach/soc.h>

#ifdef CONFIG_SDP_THERMAL
#include <linux/platform_data/sdp_thermal.h>
#endif

#define DEFAULT_ASV_GROUP	1

struct regulator {
	struct device *dev;
	struct list_head list;
	unsigned int always_on:1;
	unsigned int bypass:1;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};

static struct sdp_asv_info *asv_info;
static unsigned int sdp_asv_revid;
unsigned int sdp_result_of_asv;
unsigned int sdp_asv_stored_result;

struct sdp_asv_info * get_sdp_asv_info(void)
{
	return asv_info;
}
EXPORT_SYMBOL(get_sdp_asv_info);

/******************/
/* SDP1304 asv    */
/******************/
/* Golf-AP select the group only using TMCB */
struct asv_judge_table sdp1304_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{    0, 16},	/* Reserved Group (typical default) */
	{ 1023, 21},
	{ 1023, 24},
	{ 1023, 32},
	{ 1023, 37},
	{ 1023, 44},
	{ 1023, 63},
	{ 1023, 63},		
	{ 1023, 63},	/* Reserved Group (MAX) */
};

struct asv_volt_table sdp1304_mp_table[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{    0,  0, 1200000},
	{ 1023, 20, 1200000},
	{ 1023, 26, 1170000},
	{ 1023, 31, 1140000},
	{ 1023, 47, 1110000},
	{ 1023, 54, 1040000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
};

struct asv_volt_table sdp1304_mp_evk_table[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{    0,  0, 1180000},
	{ 1023, 20, 1180000},
	{ 1023, 26, 1150000},
	{ 1023, 31, 1120000},
	{ 1023, 47, 1090000},
	{ 1023, 54, 1020000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
};

struct asv_volt_dual_table sdp1304_us_table[MAX_US_ASV_GROUP] = {
	/* ids, volt1, volt2 */
	{   20, 1180000, 1180000},
	{   40, 1140000, 1140000},
	{  150, 1100000, 1100000},
	{  260, 1050000, 1050000},
	{ 1023, 1030000, 1030000},
	{ 1023, 1030000, 1030000},
	{ 1023, 1030000, 1030000},
	{ 1023, 1030000, 1030000},
	{ 1023, 1030000, 1030000},
	{ 1023, 1030000, 1030000},
};

static int sdp1304_us_asv_result;
static int sdp1304_mp_asv_result[MAX_MP_COUNT];
static bool sdp1304_us_board_8500 = false;
static struct regulator *mp_regulator[MAX_MP_COUNT];
static struct regulator *us_regulator;
static bool us_altvolt = false;

static int sdp1304_get_cpu_ids(struct sdp_asv_info *asv_info)
{
	/* read cpu ids [56:48] */
	asv_info->cpu_ids = (readl((void*)(SFR_VA + 0x80008)) >> 16) & 0x1FF;	
	asv_info->cpu_ids *= 2; /* conver to 1mA scale */

	/* read cpu tmcb [63:58] */
	asv_info->cpu_tmcb = readl((void*)(SFR_VA + 0x80008)) >> 26;
	
	return 0;
}

static int sdp1304_get_gpu_ids(struct sdp_asv_info *asv_info)
{
	/* read gpu ids */
	asv_info->gpu_ids = readl((void*)(SFR_VA + 0x80010)) & 0xFF; /* [71:64] */
//	asv_info->gpu_ids = asv_info->gpu_ids | 
//						((readl((void*)(SFR_VA + 0x80008)) >> 25) & 0x1) << 8; /* [57] MSB */
	asv_info->gpu_ids *= 2; /* conver to 1mA scale */
	
	return 0;
}

static int sdp1304_get_mp_ids(struct sdp_asv_info *asv_info)
{
	int i, timeout;
	void __iomem * mp_base[MAX_MP_COUNT];
	u32 base[MAX_MP_COUNT] = {0x18080000, 0x1a080000};

	for (i = 0; i < asv_info->mp_cnt; i++) {
		/* ioremap */
		mp_base[i] = ioremap(base[i], 0x10);
		if (mp_base == NULL) {
			printk(KERN_ERR "AVS ERROR - MP%d ioremap fail\n", i);
			continue;
		}
			
		/* enable MP chip id */
		timeout = 200;
		writel(0x1F, mp_base[i] + 0x4);
		while (timeout) {
			if (readl(mp_base[i]) == 0)
				break;
		}
		if (!timeout) {
			printk(KERN_ERR "AVS ERROR - fail to enable MP%d chip id\n", i);
			iounmap(mp_base[i]);
			continue;
		}

		/* read mp ids */
		asv_info->mp_ids[i] = (readl(mp_base[i] + 0x8) >> 16) & 0x3FF;
		
		/* read mp tmcb */
		asv_info->mp_tmcb[i] = (readl(mp_base[i] + 0x8) >> 26) & 0x3F;

		iounmap(mp_base[i]);
	}

	return 0;
}

static int sdp1304_get_us_ids(struct sdp_asv_info *info)
{
	if (of_machine_is_compatible("samsung,golf-us")) {
		void __iomem *base;
		int timeout;

		base = ioremap(0x1d580000, 0x10);
		if (base == NULL) {
			printk(KERN_ERR "AVS ERROR - golf-us ioremap fail\n");
			return -1;
		}

		/* enable US chip id */
		timeout = 200;
		writel(0x1F, base + 0x4);
		while (timeout) {
			if (readl(base) == 0)
				break;
		}
		if (!timeout) {
			printk(KERN_ERR "AVS ERROR - fail to enable US chip id\n");
			iounmap(base);
			return -1;
		}

		/* read us ids */
		info->us_ids = (readl(base + 0x8) >> 16) & 0x3FF;
		
		/* read us tmcb */
		info->us_tmcb = (readl(base + 0x8) >> 26) & 0x3F;
	
		printk(KERN_INFO "AVS: us - tmcb: %u, ids: %umA\n", info->us_tmcb, info->us_ids);
		
		iounmap(base);
	}
	
	return 0;
}

static int sdp1304_apply_us_avs(struct sdp_asv_info *info)
{
	int i;
	int ret;
	
	if (of_machine_is_compatible("samsung,golf-us")) {
		/* find US group */
		for (i = 0; i < MAX_US_ASV_GROUP; i++) {
			if (info->us_ids <= sdp1304_us_table[i].ids_tmcb)
				break;
		}
		sdp1304_us_asv_result = i;
		printk(KERN_INFO "AVS: US gourp %d selected by using ids\n", sdp1304_us_asv_result);
	
		/* apply US voltage */
		us_regulator = regulator_get(NULL, "US_PW");
		if (IS_ERR(us_regulator)) {
			printk(KERN_ERR "AVS ERROR - failed to get US regulator\n");
			us_regulator = NULL;
			return -1;
		}

		/* set US voltage */
		printk(KERN_INFO "AVS: set US voltage to %duV\n", sdp1304_us_table[sdp1304_us_asv_result].volt1);
		ret = regulator_set_voltage(us_regulator,
							sdp1304_us_table[sdp1304_us_asv_result].volt1,
							sdp1304_us_table[sdp1304_us_asv_result].volt1 + 10000);
		if (ret < 0) {
			printk(KERN_ERR "AVS ERROR - fail to set US voltage\n");
			return -1;
		}
	}

	return 0;
}

static int sdp1304_store_result(struct sdp_asv_info *asv_info)
{
	int i, j, ret = 0;
	char vddmp_name[10];

	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (asv_info->cpu_tmcb <= asv_info->cpu_ids_table[i].tmcb_limit) {
			sdp_asv_stored_result = i;
			printk(KERN_INFO "AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (sdp_asv_stored_result < DEFAULT_ASV_GROUP ||
		sdp_asv_stored_result >= MAX_CPU_ASV_GROUP) {
		sdp_asv_stored_result = DEFAULT_ASV_GROUP;
	}
	sdp_result_of_asv = sdp_asv_stored_result;

	/* apply ABB at cpu ids < 70mA */
	if (asv_info->cpu_ids <= 70) {
		/* ABB x0.8 */
		printk(KERN_INFO "AVS: apply ABB x0.8\n");
		writel(0x9, (void *)(SFR_VA + 0xB00E3C));
	}

	/* find MP group */
	for (i = 0; i < asv_info->mp_cnt; i++) {
		for (j = 0; j < MAX_MP_ASV_GROUP; j++) {
			if (asv_info->mp_tmcb[i] <= asv_info->mp_table[j].tmcb)
				break;
		}
		sdp1304_mp_asv_result[i] = j;
		printk(KERN_INFO "AVS: MP%d gourp %d selected\n", i, sdp1304_mp_asv_result[i]);
	}
	
	/* apply MP voltage */
	for (i = 0; i < asv_info->mp_cnt; i++) {
		sprintf(vddmp_name, "MP%d_PW", i);
		mp_regulator[i] = regulator_get(NULL, vddmp_name);
		if (IS_ERR(mp_regulator[i])) {
			printk(KERN_ERR "AVS ERROR - failed to get MP%d regulator\n", i);
			mp_regulator[i] = NULL;
			continue;
		}

		/* set MP voltage */
		printk(KERN_INFO "AVS: set MP%d voltage to %duV\n", i, asv_info->mp_table[sdp1304_mp_asv_result[i]].volt);
		ret = regulator_set_voltage(mp_regulator[i],
							asv_info->mp_table[sdp1304_mp_asv_result[i]].volt,
							asv_info->mp_table[sdp1304_mp_asv_result[i]].volt + 10000);
		if (ret < 0)
			printk(KERN_ERR "AVS ERROR - fail to set MP%d voltage\n", i);
	}

	/* show all ids, tmcb */
	printk(KERN_INFO "AVS: cpu - tmcb: %d, ids: %dmA\n", asv_info->cpu_tmcb, asv_info->cpu_ids);
	printk(KERN_INFO "AVS: gpu - ids: %dmA\n", asv_info->gpu_ids);
	for (i = 0; i < asv_info->mp_cnt; i++)
		printk(KERN_INFO "AVS: mp%d - tmcb: %d, ids: %dmA\n", i, asv_info->mp_tmcb[i], asv_info->mp_ids[i]);

	/* apply US voltage */
	sdp1304_get_us_ids(asv_info);
	sdp1304_apply_us_avs(asv_info);
		
	return 0;
}

static int sdp1304_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->mp_cnt = 1;
	info->cpu_ids_table = sdp1304_ids_table;
	info->get_cpu_ids = sdp1304_get_cpu_ids;
	info->get_gpu_ids = sdp1304_get_gpu_ids;
	info->get_mp_ids = sdp1304_get_mp_ids;
	info->store_result = sdp1304_store_result;
	
	if (get_sdp_board_type() == SDP_BOARD_SBB)
		info->mp_table = sdp1304_mp_evk_table;
	else
		info->mp_table = sdp1304_mp_table;

	/* enable AP chip id register */
	writel(0x1F, (void*)(SFR_VA + 0x80004));
	while (timeout) {
		if (readl((void*)(SFR_VA + 0x80000)) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		printk(KERN_ERR "AVS: AP chip id enable failed!\n");
		return -EIO;
	}
		
	return 0;
}

int sdp1304_us_set_altvolt(bool on)
{
	int volt;
	int ret;
	
	/* set to alternative voltage */
	if (on == true)
		volt = sdp1304_us_table[sdp1304_us_asv_result].volt2;
	else /* set to original voltage */
		volt = sdp1304_us_table[sdp1304_us_asv_result].volt1;

	us_altvolt = on;

	if (us_regulator == NULL) {
		printk(KERN_ERR "AVS: us_regulator is NULL\n");
		return -1;
	}

	/* set voltage */
	printk(KERN_INFO "AVS: set US voltage to %duV\n", volt);
	ret = regulator_set_voltage(us_regulator, volt, volt + 10000);
	if (ret < 0) {
		printk(KERN_ERR "AVS ERROR - fail to set US voltage\n");
		return -1;
	}
	
	return 0;
}
EXPORT_SYMBOL(sdp1304_us_set_altvolt);

static __init int sdp1304_us_avs_board_8500(char *buf)
{
	printk(KERN_INFO "AVS: Golf-US 8500 board is found\n");
	sdp1304_us_board_8500 = true;
	
	return 0;
}
early_param("8500", sdp1304_us_avs_board_8500);

/******************/
/* SDP1302 asv    */
/******************/
struct asv_judge_table sdp1302_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{ 1023, 35},	/* Reserved Group (typical default) */
	{ 1023, 39},
	{ 1023, 45},
	{ 1023, 52},
	{ 1023, 57},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},	/* Reserved Group (MAX) */
};

struct asv_volt_table sdp1302_core_table[MAX_CORE_ASV_GROUP] = {
	/* IDS, TMCB, VOLT */
	{    0,  0, 1300000},
	{ 1023, 26, 1300000},
	{ 1023, 34, 1240000},
	{ 1023, 39, 1220000},
	{ 1023, 45, 1210000},
	{ 1023, 51, 1180000},
	{ 1023, 59, 1140000},
	{ 1023, 63, 1110000},
	{ 1023, 63, 1110000},
	{ 1023, 63, 1110000},	/* Reserved Group (MAX) */
};

static struct regulator *core_regulator;
static int sdp1302_core_asv_result = 0;

static int sdp1302_get_cpu_ids(struct sdp_asv_info *asv_info)
{
	/* cpu ids [79:74] * 4mA */
	asv_info->cpu_ids = ((__raw_readl((void*)(SFR_VA + 0x80010)) >> 10) & 0x3F) * 4;
	printk(KERN_DEBUG "cpu_ids = %u\n", asv_info->cpu_ids);

	/* tmcb [53:48] */
	asv_info->cpu_tmcb = (__raw_readl((void*)(SFR_VA + 0x80008)) >> 16) & 0x3F;
	printk(KERN_DEBUG "cpu_tmcb = %u\n", asv_info->cpu_tmcb);
	
	return 0;
}

static int sdp1302_get_gpu_ids(struct sdp_asv_info *asv_info)
{
	/* gpu ids [73:68] * 4mA */
	asv_info->gpu_ids = ((__raw_readl((void*)(SFR_VA + 0x80010)) >> 4) & 0x3F) * 4;
	printk(KERN_DEBUG "gpu_ids = %u\n", asv_info->gpu_ids);

	return 0;
}

static unsigned int sdp1302_get_logic_ids(struct sdp_asv_info *asv_info)
{
	unsigned int logic_ids;

	/* logic ids [67:62] * 8mA */
	logic_ids = (__raw_readl((void*)(SFR_VA + 0x80010)) & 0xF) << 2;
	logic_ids |= (__raw_readl((void*)(SFR_VA + 0x80008)) >> 30) & 0x3;
	logic_ids *= 8;

	return logic_ids;
}

static void sdp1302_apply_core_avs(bool on)
{
	int i;
	int ret;

	if (on == false) {
		printk(KERN_WARNING "AVS: golf-s core avs is always on\n");
		return;
	}
	
	/* find CORE group */
	for (i = 0; i < MAX_CORE_ASV_GROUP; i++) {
		if (asv_info->cpu_tmcb <= sdp1302_core_table[i].tmcb) {
			sdp1302_core_asv_result = i;
			printk(KERN_INFO "AVS: CORE gourp %d selected\n", sdp1302_core_asv_result);
			break;
		}
	}
	
	/* apply CORE voltage */
	core_regulator = regulator_get(NULL, "CORE_PW");
	if (IS_ERR(core_regulator)) {
		printk(KERN_ERR "AVS ERROR - failed to get CORE regulator\n");
		core_regulator = NULL;
	} else {
		/* set CORE voltage */
		printk(KERN_INFO "AVS: set CORE voltage to %duV\n", sdp1302_core_table[sdp1302_core_asv_result].volt);
		ret = regulator_set_voltage(core_regulator,
							sdp1302_core_table[sdp1302_core_asv_result].volt,
							sdp1302_core_table[sdp1302_core_asv_result].volt + 10000);
		if (ret < 0)
			printk(KERN_ERR "AVS ERROR - fail to set CORE voltage\n");
	}
}

static int sdp1302_store_result(struct sdp_asv_info *asv_info)
{
	int i;

	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (asv_info->cpu_tmcb <= asv_info->cpu_ids_table[i].tmcb_limit) {
			sdp_asv_stored_result = i;
			printk(KERN_INFO "AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (sdp_asv_stored_result < DEFAULT_ASV_GROUP ||
		sdp_asv_stored_result >= MAX_CPU_ASV_GROUP) {
		sdp_asv_stored_result = DEFAULT_ASV_GROUP;
	}
	sdp_result_of_asv = sdp_asv_stored_result;

#ifdef CONFIG_SDP_THERMAL
	sdp_tmu_register_core_avs(sdp1302_apply_core_avs);
#else
	sdp1302_apply_core_avs(true);
#endif

	printk(KERN_INFO "AVS: cpu - tmcb: %u, ids: %umA\n", asv_info->cpu_tmcb, asv_info->cpu_ids);
	printk(KERN_INFO "AVS: gpu - tmcb: %u, ids: %umA\n", asv_info->cpu_tmcb, asv_info->gpu_ids);
	printk(KERN_INFO "AVS: core, ddr - tmcb: %u, ids: %umA\n", asv_info->cpu_tmcb, sdp1302_get_logic_ids(asv_info));
	
	return 0;
}

static int sdp1302_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->cpu_ids_table = sdp1302_ids_table;
	info->get_cpu_ids = sdp1302_get_cpu_ids;
	info->get_gpu_ids = sdp1302_get_gpu_ids;
	info->store_result = sdp1302_store_result;

	/* enable AP chip id register */
	writel(0x1F, (void*)(SFR_VA + 0x80004));
	while (timeout) {
		if (readl((void*)(SFR_VA + 0x80000)) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		printk(KERN_ERR "AVS: chip id enable failed!\n");
		return -EIO;
	}
	
	return 0;
}

/******************/
/* SDP1307 asv    */
/******************/
struct asv_judge_table sdp1307_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{  100, 63},	/* Reserved Group (typical default) */
	{  300, 63},
	{  350, 63},
	{  500, 63},
	{  700, 63},
	{ 1890, 63},
	{ 1890, 63},
	{ 1890, 63},
	{ 1890, 63},	/* Reserved Group (MAX) */
};

struct asv_volt_table sdp1307_core_table[MAX_CORE_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{   0, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
};

static int sdp1307_get_cpu_ids(struct sdp_asv_info *asv_info)
{
	u32 val;
	
	/* cpu ids */
	if (sdp_asv_revid) {
		val = (readl(asv_info->base + 0x10) >> 8) & 0xFF;
		asv_info->cpu_ids = val * 3;
	} else {
		val = (readl(asv_info->base + 0x10) >> 10) & 0x3F;
		asv_info->cpu_ids = val * 30;
	}
	printk(KERN_DEBUG "cpu_ids = %u\n", asv_info->cpu_ids);

	return 0;
}

static int sdp1307_get_gpu_ids(struct sdp_asv_info *asv_info)
{
	u32 val;
	
	/* gpu ids */
	if (sdp_asv_revid) {
		val = readl(asv_info->base + 0x10) & 0xFF;
		asv_info->gpu_ids = val;
	} else {
		val = (readl(asv_info->base + 0x10) >> 10) & 0x3F;
		asv_info->gpu_ids = val * 30;
	}
	printk(KERN_DEBUG "gpu_ids = %u\n", asv_info->gpu_ids);

	return 0;
}

static int sdp1307_get_core_ids(struct sdp_asv_info *asv_info)
{
	/* core ids */
	if (sdp_asv_revid)
		asv_info->core_ids = ((readl(asv_info->base + 0x8) >> 24) & 0xFF) * 2;
	else 
		asv_info->core_ids = ((readl(asv_info->base + 0x8) >> 16) & 0xFF) * 2;

	printk(KERN_DEBUG "core_ids = %u\n", asv_info->core_ids);

	return 0;
}

static unsigned int sdp1307_get_total_ids(struct sdp_asv_info *asv_info)
{
	unsigned int ids;
	
	/* total ids (cpu + gpu + core) */
	ids = ((readl(asv_info->base + 0x8) >> 16) & 0x3F) * 30;

	return ids;
}

static int sdp1307_store_result(struct sdp_asv_info *asv_info)
{
	int i;
#if 0
	int ret;
	struct regulator * core_regulator;
#endif
	int core_asv_result = 0;

	/* get core ids */
	sdp1307_get_core_ids(asv_info);

	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (asv_info->cpu_ids <= asv_info->cpu_ids_table[i].ids_limit) {
			/* es1 */
			if (!sdp_asv_revid) {
				sdp_asv_stored_result = 0;
				printk(KERN_INFO "AVS: es1 is not grouped\n");
				break;
			}

			/* es2 */
			sdp_asv_stored_result = i;
			printk(KERN_INFO "AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (sdp_asv_stored_result < DEFAULT_ASV_GROUP ||
		sdp_asv_stored_result >= MAX_CPU_ASV_GROUP) {
		sdp_asv_stored_result = DEFAULT_ASV_GROUP;
	}
	sdp_result_of_asv = sdp_asv_stored_result;

	/* find CORE group */
	for (i = 0; i < MAX_CORE_ASV_GROUP; i++) {
		if (asv_info->core_ids <= sdp1307_core_table[i].ids) {
			core_asv_result = i;
			printk(KERN_INFO "AVS: CORE gourp %d selected\n", core_asv_result);
			break;
		}
	}

#if 0
	/* apply CORE voltage */
	core_regulator = regulator_get(NULL, "CORE_PW");
	if (IS_ERR(core_regulator)) {
		printk(KERN_ERR "AVS ERROR - failed to get CORE regulator\n");
	} else {
		/* set CORE voltage */
		printk(KERN_INFO "AVS: set CORE voltage to %duV\n", sdp1307_core_table[core_asv_result].volt);
		ret = regulator_set_voltage(core_regulator,
							sdp1307_core_table[core_asv_result].volt,
							sdp1307_core_table[core_asv_result].volt + 10000);
		if (ret < 0)
			printk(KERN_ERR "AVS ERROR - fail to set CORE voltage\n");
	}
#endif

	printk(KERN_INFO "AVS: cpu - ids: %umA\n", asv_info->cpu_ids);
	printk(KERN_INFO "AVS: gpu - ids: %umA\n", asv_info->gpu_ids);
	printk(KERN_INFO "AVS: core - ids: %umA\n", asv_info->core_ids);

	if (sdp_asv_revid)
		printk(KERN_INFO "AVS: total - ids: %umA\n", sdp1307_get_total_ids(asv_info));
	
	return 0;
}

#define SDP1307_CHIPID_BASE		0x18080000
static int sdp1307_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->cpu_ids_table = sdp1307_ids_table;
	info->get_cpu_ids = sdp1307_get_cpu_ids;
	info->get_gpu_ids = sdp1307_get_gpu_ids;
	info->store_result = sdp1307_store_result;

	/* enable chip id register */
	info->base = ioremap(SDP1307_CHIPID_BASE, 0x10);
	if (info->base == NULL) {
		printk(KERN_ERR "ASV Error: can't map chip id address\n");
		return -1;
	}

	writel(0xFF01, info->base + 0x4);
	while (timeout) {
		if (readl(info->base) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		printk(KERN_ERR "AVS: chip id enable failed!\n");
		return -EIO;
	}

	/* read revision id */
	sdp_asv_revid = (readl(info->base + 0x8) >> 23) & 0x1;
	if (sdp_asv_revid)
		printk(KERN_INFO "AVS: golf-v es2, rev num = %d\n", sdp_asv_revid);
		
	return 0;
}

/*******************/
/* SDP common code */
/*******************/
static int sdp_asv_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	int ret;
	int volt;
	int us_volt;
	int retry;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pr_info("PM_SUSPEND_PREPARE for AVS\n");
		
		if (of_machine_is_compatible("samsung,sdp1304")) {
			if (mp_regulator[0]) {
				mp_regulator[0]->min_uV = 0;
				mp_regulator[0]->max_uV = 0;
			}

			if (of_machine_is_compatible("samsung,golf-us")) {
				if (us_regulator) {
					us_regulator->min_uV = 0;
					us_regulator->max_uV = 0;
				}
			}
		} else if (of_machine_is_compatible("samsung,sdp1302")) {
			if (core_regulator) {
				core_regulator->min_uV = 0;
				core_regulator->max_uV = 0;
			}
		}
		
		break;
	case PM_POST_SUSPEND:
		pr_info("PM_POST_SUSPEND for AVS\n");

		if (of_machine_is_compatible("samsung,sdp1304")) {
			/* set MP voltage */
			retry = 3;
			do {
				if (mp_regulator[0] == NULL) {
					printk(KERN_ERR "AVS: mp_regulator is NULL\n");
					break;
				}
				
				printk(KERN_INFO "AVS: set MP0 voltage to %duV\n", asv_info->mp_table[sdp1304_mp_asv_result[0]].volt);
				ret = regulator_set_voltage(mp_regulator[0],
								asv_info->mp_table[sdp1304_mp_asv_result[0]].volt,
								asv_info->mp_table[sdp1304_mp_asv_result[0]].volt + 10000);
				if (ret < 0)
					printk(KERN_ERR "AVS ERROR - fail to set MP0 voltage\n");

				volt = regulator_get_voltage(mp_regulator[0]);
				if (volt != asv_info->mp_table[sdp1304_mp_asv_result[0]].volt)
					printk(KERN_INFO "AVS: set volt=%d, cur volt=%d\n",
						asv_info->mp_table[sdp1304_mp_asv_result[0]].volt, volt);

				retry--;
			} while (retry && volt != asv_info->mp_table[sdp1304_mp_asv_result[0]].volt);
					
			/* set US voltage */
			if (of_machine_is_compatible("samsung,golf-us")) {
				if (us_altvolt)
					us_volt = sdp1304_us_table[sdp1304_us_asv_result].volt2;
				else
					us_volt = sdp1304_us_table[sdp1304_us_asv_result].volt1;
					
				retry = 3;
				do {
					if (us_regulator == NULL) {
						printk(KERN_ERR "AVS: us_regulator is NULL\n");
						break;
					}
					
					printk(KERN_INFO "AVS: set US voltage to %duV\n", us_volt);
					ret = regulator_set_voltage(us_regulator, us_volt, us_volt + 10000);
					if (ret < 0) {
						printk(KERN_ERR "AVS ERROR - fail to set US voltage\n");
						return -1;
					}

					volt = regulator_get_voltage(us_regulator);
					if (volt != us_volt)
						printk(KERN_INFO "AVS: set volt=%d, cur volt=%d\n",
								us_volt, volt);

					retry--;
				} while (retry && volt != us_volt);
			}
		} else if (of_machine_is_compatible("samsung,sdp1302")) {
			/* set CORE voltage */
			retry = 3;

			do {
				if (core_regulator == NULL) {
					printk(KERN_ERR "AVS: core_regulator is NULL\n");
					break;
				}
				
				printk(KERN_INFO "AVS: set CORE voltage to %duV\n", sdp1302_core_table[sdp1302_core_asv_result].volt);
				ret = regulator_set_voltage(core_regulator,
								sdp1302_core_table[sdp1302_core_asv_result].volt,
								sdp1302_core_table[sdp1302_core_asv_result].volt + 10000);
				if (ret < 0)
					printk(KERN_ERR "AVS ERROR - fail to set CORE voltage\n");

				volt = regulator_get_voltage(core_regulator);
				if (volt != sdp1302_core_table[sdp1302_core_asv_result].volt)
					printk(KERN_INFO "AVS: set volt=%d, cur volt=%d\n",
							sdp1302_core_table[sdp1302_core_asv_result].volt, volt);

				retry--;
			} while (retry && volt != sdp1302_core_table[sdp1302_core_asv_result].volt);
		}

		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block sdp_asv_nb = {
	.notifier_call = sdp_asv_pm_notifier,
};

static int __init sdp_asv_init(void)
{
	int ret;

	asv_info = kzalloc(sizeof(struct sdp_asv_info), GFP_KERNEL);
	if (!asv_info)
		goto out1;

	sdp_result_of_asv = 0;
	sdp_asv_stored_result = 0;

	printk(KERN_INFO "AVS: Adaptive Support Voltage init\n");

	if (of_machine_is_compatible("samsung,sdp1304"))
		ret = sdp1304_asv_init(asv_info);
	else if (of_machine_is_compatible("samsung,sdp1302"))
		ret = sdp1302_asv_init(asv_info);
	else if (of_machine_is_compatible("samsung,sdp1307"))
		ret = sdp1307_asv_init(asv_info);
	else
		ret = -EIO;
	if (ret) {
		printk(KERN_ERR "%s error : %d\n", __func__, ret);
		goto out2;
	}

	/* Get CPU IDS Value */
	if (asv_info->get_cpu_ids) {
		if (asv_info->get_cpu_ids(asv_info))
			printk(KERN_ERR "AVS: Fail to get CPU IDS Value\n");
	}

	/* Get CPU TMCB Value */
	if (asv_info->get_cpu_tmcb) {
		if (asv_info->get_cpu_tmcb(asv_info))
			printk(KERN_INFO "AVS: Fail to get CPU TMCB Value\n");
	}

	/* Get GPU IDS Value */
	if (asv_info->get_gpu_ids) {
		if (asv_info->get_gpu_ids(asv_info))
			printk(KERN_INFO "AVS: Fail to get GPU IDS Value\n");
	}

	/* Get MP IDS Value */
	if (asv_info->get_mp_ids) {
		if (asv_info->get_mp_ids(asv_info))
			printk(KERN_ERR "AVS: Fail to get MP IDS value\n");
	}

	if (asv_info->store_result) {
		if (asv_info->store_result(asv_info)) {
			printk(KERN_INFO "AVS: Can not success to store result\n");
			goto out2;
		}
	} else {
		printk(KERN_INFO "AVS: No store_result function\n");
		goto out2;
	}

	register_pm_notifier(&sdp_asv_nb);

	return 0;
out2:
	kfree(asv_info);
out1:
	return -EINVAL;
}
device_initcall_sync(sdp_asv_init);

