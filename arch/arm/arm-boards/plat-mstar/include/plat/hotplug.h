#ifndef PLAT_HOTPLUG_H
#define PLAT_HOTPLUG_H

void platform_do_lowpower(unsigned int cpu);
int smp_platform_cpu_kill(unsigned int cpu);
void __ref smp_platform_cpu_die(unsigned int cpu);
int smp_platform_cpu_disable(unsigned int cpu);

#endif
