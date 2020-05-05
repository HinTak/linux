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
#include <mach/sdp_smp.h>

#include "common.h"

static inline void cpu_enter_lowpower_a9(void)
{
	unsigned int v;

	flush_cache_louis();

	asm volatile(
	"	mcr	p15, 0, %1, c7, c5, 0\n"
	"	mcr	p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %3\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static inline void cpu_enter_lowpower_a15(void)
{
	unsigned int v;

	asm volatile(
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "Ir" (CR_C)
	  : "cc");

	flush_cache_louis();

	asm volatile(
	/*
	* Turn off coherency
	*/
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	: "=&r" (v)
	: "Ir" (0x40)
	: "cc");

	isb();
	dsb();	
}

static inline void cpu_leave_lowpower(void)
{
	unsigned int v;

	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v)
	  : "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

#if defined(CONFIG_ARCH_SDP1207)
#	define sdp_kill_cpu(cpu)	sdp1207_kill_cpu(cpu)
#elif defined(CONFIG_ARCH_SDP1202)
#	define sdp_kill_cpu(cpu)	sdp1202_kill_cpu(cpu)
#else
#	define sdp_kill_cpu(cpu)	sdp_powerdown_cpu(cpu)
#endif

static inline void sdp_do_lowpower(unsigned int cpu, int *spurious)
{
	for (;;) {
		/* here's the WFI */
		asm(".word	0xe320f003\n" : : : "memory", "cc");

		if (pen_release == (int) cpu) {
			break;
		}
		(*spurious)++;
	}
}

static inline void sdp1207_kill_cpu(unsigned int cpu)
{
	BUG_ON(cpu == 0);
	writel(0, (u32*)0xFE0D0008);		//Set Stepping Stone
	writel(0xFFFFFFFF, (u32*)0xFEB70010);	//Set Power on/off delay
	writel(2, (u32*)0xFEB7000C);		//Set WFI_MODE
	writel(0, (u32*)0xFEB70008);		//Set CPU1 PowerDown Enable
	writel(1, (u32*)0xFEB70004);		//Set CPU1 PowerDown Enable
	wmb();
}

static inline void sdp1202_kill_cpu(unsigned int cpu)
{
	/* do not kill cpus, just stay in wfi. */
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
	int primary_part = 0;

	/*
	 * we're ready for shutdown now, so do it
	 */
	asm("mrc p15, 0, %0, c0, c0, 0" : "=r"(primary_part) : : "cc");
	if ((primary_part & 0xfff0) == 0xc0f0)
		cpu_enter_lowpower_a15();
	else
		cpu_enter_lowpower_a9();

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

