#ifndef _SDP_CPUIDLE_H_
#define _SDP_CPUIDLE_H_

struct sdp_cpuidle_ops {
	int (*set_entry)(unsigned int cpu, u32 entry);
	int (*powerup_cpu)(unsigned int cpu);
	int (*powerdown_cpu)(unsigned int cpu);
};

#ifdef CONFIG_CPU_IDLE
int sdp_cpuidle_init(struct sdp_cpuidle_ops *ops);
#else
static inline int sdp_cpuidle_init(struct sdp_cpuidle_ops *ops) { return 0; }
#endif

#endif
