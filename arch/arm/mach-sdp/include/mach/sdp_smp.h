#ifndef _SDP_SMP_H_
#define _SDP_SMP_H_

struct sdp_power_ops {
	int (*install_warp)(unsigned int cpu);
	int (*powerup_cpu)(unsigned int cpu);
	int (*powerdown_cpu)(unsigned int cpu);
};

void sdp_platsmp_init(struct sdp_power_ops *ops) __init;
void sdp_set_power_ops(struct sdp_power_ops *ops) __init;

void sdp_secondary_startup(void);

int sdp_powerdown_cpu(unsigned int cpu);
void sdp_scu_enable(void);

#ifdef CONFIG_HOTPLUG_CPU
int sdp_cpu_kill(unsigned int cpu);
void sdp_cpu_die(unsigned int cpu);
int sdp_cpu_disable(unsigned int cpu);
#endif

#endif

