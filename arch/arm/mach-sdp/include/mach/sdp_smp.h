#ifndef _SDP_SMP_H_
#define _SDP_SMP_H_

/* for smp_operations */
struct sdp_power_ops {
	int (*install_warp)(unsigned int cpu);
	int (*powerup_cpu)(unsigned int cpu);
	int (*powerdown_cpu)(unsigned int cpu);
	int (*cpu_die)(unsigned int cpu);
};
void sdp_set_power_ops(struct sdp_power_ops *ops) __init;

#ifdef CONFIG_MCPM
struct sdp_mcpm_ops {
	int (*write_resume_reg)(unsigned int cluster, unsigned int cpu, unsigned int value);
	int (*powerup)(unsigned int cluster, unsigned int cpu);
	int (*powerdown)(unsigned int cluster, unsigned int cpu);
};
void sdp_set_mcpm_ops(struct sdp_mcpm_ops *ops) __init;
#endif

void sdp_platsmp_init(struct sdp_power_ops *ops) __init;
void sdp_secondary_startup(void);
bool sdp_smp_init_ops(void);

#endif

