/* include/linux/power/sdp_asv.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * SDP - Adoptive Support Voltage Header file
 *
 */

#ifndef __POWER_SDP_ASV_H
#define __POWER_SDP_ASV_H

//#define LOOP_CNT			10
#define MAX_MP_COUNT		2
#define MAX_CPU_ASV_GROUP	10	/* must be same as CPUFREQ_ASV_COUNT in cpufreq code */
#define MAX_MP_ASV_GROUP	10
#define MAX_CORE_ASV_GROUP	10
#define MAX_US_ASV_GROUP	10

extern unsigned int sdp_result_of_asv;
extern unsigned int sdp_asv_stored_result;

struct asv_judge_table {
	unsigned int ids_limit; /* IDS value to decide group of target */
	unsigned int tmcb_limit; /* TMCB value to decide group of target */
};

struct asv_volt_table {
	unsigned int ids; /* ids value */
	unsigned int tmcb; /* tmcb value */
	int volt; /* micro volt */
};

struct asv_volt_dual_table {
	unsigned int ids_tmcb; /* ids or tmcb */
	int volt1;		/* original voltage */
	int volt2;		/* alternative voltage */
};

struct sdp_asv_info {
	void __iomem * base;
	
	int mp_cnt;		/* mp */
		
	unsigned long long ap_pkg_id;	/* fused value for ap package */
	unsigned long long mp_pkg_id[MAX_MP_COUNT];	/* fused value for mp package */

	unsigned int cpu_ids;			/* cpu ids value of chip */
	unsigned int cpu_tmcb;			/* cpu tmcb value of chip */

	unsigned int gpu_ids;			/* gpu ids value */

	unsigned int core_ids;			/* core ids value */

	unsigned int mp_ids[MAX_MP_COUNT];		/* mp ids value */
	unsigned int mp_tmcb[MAX_MP_COUNT];		/* mp tmcb value */

	unsigned int us_ids;			/* us ids value */
	unsigned int us_tmcb;			/* us tmcb value */

	struct asv_judge_table *cpu_ids_table;	/* cpu ids table */
	struct asv_volt_table *mp_table; 		/* mp ids, tmcb voltage table */
	
	int (*get_cpu_ids)(struct sdp_asv_info *asv_info);
	int (*get_cpu_tmcb)(struct sdp_asv_info *asv_info);
	int (*get_gpu_ids)(struct sdp_asv_info *asv_info);
	int (*get_mp_ids)(struct sdp_asv_info *asv_info);
	int (*store_result)(struct sdp_asv_info *asv_info);
};

struct sdp_asv_info * get_sdp_asv_info(void);

#endif /* __POWER_SDP_ASV_H */
