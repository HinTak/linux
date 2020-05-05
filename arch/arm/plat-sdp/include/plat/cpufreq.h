/* linux/arch/arm/mach-sdp/include/plat/cpufreq.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP - CPUFreq support
 *
 */

/* CPU frequency level index for using cpufreq lock API
 * This should be same with cpufreq_frequency_table
*/

#include <linux/regulator/consumer.h>

#ifndef __CPUFREQ_H
#define __CPUFREQ_H

enum cpufreq_level_index {
	L0, L1,	L2,	L3,	L4,
	L5,	L6,	L7,	L8,	L9,
	L10, L11, L12, L13, L14,
	L15, L16, L17, L18, L19,
	L20, L21, L22, L23, L24,
	LMAX
};

enum cpufreq_lock_ID {
	DVFS_LOCK_ID_PM,	/* PM */
	DVFS_LOCK_ID_TMU,	/* TMU */
	DVFS_LOCK_ID_USER,	/* USER */
	DVFS_LOCK_ID_GPU,	/* GPU */
	DVFS_LOCK_ID_MFC,	/* MFC */
	DVFS_LOCK_ID_USB2,	/* USB2 host */
	DVFS_LOCK_ID_USB3,	/* USB3 host */
	DVFS_LOCK_ID_USBOTG,	/* USB otg */
	DVFS_LOCK_ID_CAM,	/* CAMERA */
	DVFS_LOCK_ID_G2D,	/* 2D GFX */
	DVFS_LOCK_ID_MIC,	/* MIC */
	DVFS_LOCK_ID_ROTATION,	/* UI ROTATION */
	
	DVFS_LOCK_ID_END,
};

enum cpufreq_volt_id {
	DVFS_VOLT_ID_CPU,
	DVFS_VOLT_ID_GPU,

	DVFS_VOLT_ID_END,
};

struct cpufreq_timerdiv_table {
	unsigned long mult;
	int shift;
};

struct cpufreq_ema_table {
	unsigned int volt;
	unsigned int cpuema;
	unsigned int l2ema;
};

int sdp_cpufreq_get_level(unsigned int freq,
			unsigned int *level);
int sdp_find_cpufreq_level_by_volt(unsigned int arm_volt,
			unsigned int *level);

int sdp_cpufreq_lock(unsigned int nId,
			enum cpufreq_level_index cpufreq_level);
void sdp_cpufreq_lock_free(unsigned int nId);
void sdp_cpufreq_emergency_limit(void);
void sdp_cpufreq_emergency_limit_free(void);
int sdp_cpufreq_upper_limit(unsigned int nId,
			enum cpufreq_level_index cpufreq_level);
void sdp_cpufreq_upper_limit_free(unsigned int nId);

/*
 * This level fix API set highset priority level lock.
 * Please use this carefully, with other lock API
 */
int sdp_cpufreq_level_fix(unsigned int freq);
void sdp_cpufreq_level_unfix(void);
int sdp_cpufreq_is_fixed(void);
bool sdp_cpufreq_is_cpufreq_on(void);
void sdp_cpufreq_set_tmu_control(bool on);
void sdp_cpufreq_set_cpufreq_on(bool on);
void sdp_cpufreq_set_asv_on(bool on);

int sdp_cpufreq_set_voltage(enum cpufreq_volt_id id, int uV);

#define MAX_INDEX	10

struct sdp_dvfs_info {
	unsigned int	pll_safe_idx;
	unsigned int	pm_lock_idx;
	unsigned int	max_real_idx;
	unsigned int	max_support_idx;
	unsigned int	min_support_idx;
	unsigned int	min_real_idx;
	unsigned int	gov_support_freq;
	struct clk	*cpu_clk;
	unsigned int	*volt_table;
	struct cpufreq_frequency_table	*freq_table;
	struct cpufreq_timerdiv_table	*div_table;
	void (*set_freq)(unsigned int, unsigned int);
	bool (*need_apll_change)(unsigned int, unsigned int);
	void (*update_volt_table)(void);
	unsigned int (*get_speed)(void);
};

extern struct sdp_dvfs_info *sdp_info;

#ifdef CONFIG_OF
extern int sdp1304_cpufreq_init(struct sdp_dvfs_info *info);
extern int sdp1302_cpufreq_init(struct sdp_dvfs_info *info);
extern int sdp1307_cpufreq_init(struct sdp_dvfs_info *info);
#else
int sdp_cpufreq_mach_init(struct sdp_dvfs_info *info);
#endif

#endif
