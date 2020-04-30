#include <linux/regulator/consumer.h>

/******************/
/* SDP1501 asv    */
/******************/
#define CHIPID_BASE	(SFR_VA + 0x00180000 - 0x00100000)

/* Hawk-M select the group only using TMCB */
struct asv_judge_table sdp1501_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{ 1023, 63},	/* Reserved Group (typical default) */
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},		
	{ 1023, 63},	/* Reserved Group (MAX) */
};

static int sdp1501_get_cpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read cpu ids */
	info->cpu[0].ids = ((readl((void *)(CHIPID_BASE + 0xC)) >> 16) & 0x1FF) * 2;

	/* read cpu tmcb */
	info->cpu[0].tmcb = (readl((void *)(CHIPID_BASE + 0x14)) >> 10) & 0x3F;
	
	return 0;
}

static int sdp1501_get_gpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read gpu ids */
	info->gpu.ids = ((readl((void *)(CHIPID_BASE + 0xC)) >> 25) |
			((readl((void *)(CHIPID_BASE + 0x10)) & 0x3) << 7)) * 2;

	/* read gpu tmcb */
	info->gpu.tmcb = (readl((void *)(CHIPID_BASE + 0x14)) >> 16) & 0x3F;
	
	return 0;
}

static int sdp1501_get_core_ids_tmcb(struct sdp_asv_info *info)
{
	/* read ids */
	info->core.ids = ((readl((void *)(CHIPID_BASE + 0x10)) >> 2) & 0x1FF) * 2;

	/* read tmcb */
	info->core.tmcb = (readl((void *)(CHIPID_BASE + 0x10)) >> 11) & 0x3F;
	
	return 0;
}

static int sdp1501_store_result(struct sdp_asv_info *info)
{
	int i;
	
	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (info->cpu[0].tmcb <= info->cpu_table[0][i].tmcb_limit) {
			info->cpu[0].result = i;
			pr_info("AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (info->cpu[0].result < DEFAULT_ASV_GROUP ||
		info->cpu[0].result >= MAX_CPU_ASV_GROUP) {
		info->cpu[0].result = DEFAULT_ASV_GROUP;
	}
#if 0
	/* show all ids, tmcb */
	pr_info("AVS: cpu - tmcb: %d, ids: %dmA\n", info->cpu[0].tmcb, info->cpu[0].ids);
	pr_info("AVS: gpu - tmcb: %d, ids: %dmA\n", info->gpu.tmcb, info->gpu.ids);
	pr_info("ASV: core - tmcb: %d, ids: %dmA\n", info->core.tmcb, info->core.ids);
#endif		
	return 0;
}

static int sdp1501_suspend(struct sdp_asv_info *info)
{
	return 0;
}

static int sdp1501_resume(struct sdp_asv_info *info)
{
	return 0;
}

static int sdp1501_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->cpu_table[0] = sdp1501_ids_table;

	info->get_cpu_ids_tmcb = sdp1501_get_cpu_ids_tmcb;
	info->get_gpu_ids_tmcb = sdp1501_get_gpu_ids_tmcb;
	info->get_core_ids_tmcb = sdp1501_get_core_ids_tmcb;
	
	info->store_result = sdp1501_store_result;

	info->suspend = sdp1501_suspend;
	info->resume = sdp1501_resume;

	/* enable Jazz-M chip id register */
	writel(0x1F, (void*)(CHIPID_BASE + 0x4));
	while (timeout) {
		if (readl((void*)CHIPID_BASE) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		pr_err("AVS: jazz-m chip id enable failed!\n");
		return -EIO;
	}

	return 0;
}
