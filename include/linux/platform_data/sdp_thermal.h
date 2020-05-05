/* include/linux/platform_data/sdp_thermal.h
 *
 * Copyright 2013 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for sdp tmu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SDP_THERMAL_H
#define __SDP_THERMAL_H

#define TMU_SAVE_NUM   10

#define TMU_INV_FREQ_LEVEL	(0xFFFFFFFF)

#define TMU_1ST_THROTTLE	(1)
#define TMU_2ND_THROTTLE	(2)
#define TMU_3RD_THROTTLE	(3)

/* times */
#define SAMPLING_RATE		(2 * 1000 * 1000) /* u sec */
#define PANIC_TIME_SEC		(10 * 60 * 1000 * 1000 / SAMPLING_RATE) /* 10 minutes */
#define PRINT_RATE			(6 * 1000 * 1000) /* 6 sec */

#define TSC_DEGREE_25		46	/* 25'C is 46 */

enum tmu_state_t {
	TMU_STATUS_INIT = 0,
	TMU_STATUS_ZERO,
	TMU_STATUS_NORMAL,
	TMU_STATUS_1ST,
	TMU_STATUS_2ND,
	TMU_STATUS_3RD,
};

/*
 * struct temperature_params have values to manange throttling, tripping
 * and other software safety control
 */
struct temperature_params {
	unsigned int start_zero_throttle;
	unsigned int stop_zero_throttle;
	unsigned int stop_1st_throttle;
	unsigned int start_1st_throttle;
	unsigned int stop_2nd_throttle;
	unsigned int start_2nd_throttle;
	unsigned int stop_3rd_throttle;
	unsigned int start_3rd_throttle;
	unsigned int start_3rd_hotplug;
};

struct cpufreq_params {
	unsigned int limit_1st_throttle;
	unsigned int limit_2nd_throttle;
	unsigned int limit_3rd_throttle;
};

struct gpufreq_params {
	unsigned int limit_1st_throttle;
	unsigned int limit_2nd_throttle;
	unsigned int limit_3rd_throttle;
};

struct sdp_tmu_info {
	struct device   *dev;

	int id;
	char *sdp_name;

	void __iomem    *tmu_base;
	struct resource *ioarea;
	int irq;

	int	tmu_state;
	int tmu_prev_state;
	unsigned int last_temperature;

	struct temperature_params ts;
	struct cpufreq_params cpufreq;
	struct gpufreq_params gpufreq;

	unsigned int cpufreq_level_1st_throttle;
	unsigned int cpufreq_level_2nd_throttle;
	unsigned int cpufreq_level_3rd_throttle;
	
	unsigned int gpufreq_level_1st_throttle;
	unsigned int gpufreq_level_2nd_throttle;
	unsigned int gpufreq_level_3rd_throttle;

	struct delayed_work monitor;
	struct delayed_work polling;

	unsigned int monitor_period;
	unsigned int sampling_rate;
	unsigned int reg_save[TMU_SAVE_NUM];

	unsigned int sensor_id;

	int temp_print_on;
	int throttle_print_on;
		
	int (*gpu_freq_limit)(int freq); /* gpu frequency limit function */	
	int (*gpu_freq_limit_free)(void); /* gpu frequency limit free function */

	int (*enable_tmu)(struct sdp_tmu_info * info);
	unsigned int (*get_curr_temp)(struct sdp_tmu_info * info);
	int (*cpu_hotplug_down)(struct sdp_tmu_info * info);
	int (*cpu_hotplug_up)(struct sdp_tmu_info * info);

	/* AVS on/off */
	void (*cpu_avs_on)(bool on);
	void (*gpu_avs_on)(bool on);
	void (*core_avs_on)(bool on);
};

/* to send tmu_info pointer to GPU */
struct sdp_tmu_info* sdp_tmu_get_info(void);

int sdp_tmu_register_cpu_avs(void (*set_avs)(bool on));
int sdp_tmu_register_gpu_avs(void (*set_avs)(bool on));
int sdp_tmu_register_core_avs(void (*set_avs)(bool on));

extern int sdp1304_tmu_init(struct sdp_tmu_info * info);
extern int sdp1302_tmu_init(struct sdp_tmu_info * info);
extern int sdp1307_tmu_init(struct sdp_tmu_info * info);

#endif /* __SDP_THERMAL_H */
