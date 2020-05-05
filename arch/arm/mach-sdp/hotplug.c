/* linux arch/arm/mach-ccep/hotplug.c
 *
 *  Cloned from linux/arch/arm/mach-realview/hotplug.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <mach/sdp_smp.h>

#include "common.h"

static inline void cpu_leave_lowpower(void)
{
	unsigned int v;

	asm volatile(
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	isb\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v)
	  : "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
	isb();
}

#define sdp_kill_cpu(cpu)	sdp_powerdown_cpu(cpu)

#ifdef CONFIG_SDP_DEBUG_OSSR_V7_1
static inline void os_double_lock_set(void)
{
	/* Debug v7.1: OS Double Lock */
	asm volatile ("mcr	p14, 0, %0, c1, c3, 4" : : "r"(1) : "memory");
	isb();
}
static inline void os_double_lock_clear(void)
{
	/* Debug v7.1: OS Double Lock */
	asm volatile ("mcr	p14, 0, %0, c1, c3, 4" : : "r"(0) : "memory");
	isb();
}
#else
static inline void os_double_lock_set(void) {}
static inline void os_double_lock_clear(void) {}
#endif

static inline void sdp_do_lowpower(unsigned int cpu, int *spurious)
{
	u32 primary_part;

	asm("mrc p15, 0, %0, c0, c0, 0" : "=r"(primary_part) : : "cc");
	if ((primary_part & 0xfff0) == 0xc090)
		v7_exit_coherency_flush(louis);
	else
		v7_exit_coherency_flush(all);

	os_double_lock_set();

	for (;;) {
		wfi();

		if ((u32)pen_release == cpu_logical_map(cpu)) {
			break;
		}
		(*spurious)++;
	}
	
	os_double_lock_clear();
}

int __ref sdp_cpu_kill(unsigned int cpu)
{
	/* per-soc hook */
	sdp_kill_cpu(cpu);
 	
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void __ref sdp_cpu_die(unsigned int cpu)
{
	int spurious = 0;

	/*
	 * we're ready for shutdown now, so do it
	 */
	sdp_do_lowpower(cpu, &spurious);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower();

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);
}

int __ref sdp_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

