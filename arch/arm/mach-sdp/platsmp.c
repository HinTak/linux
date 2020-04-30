/*
 *  linux/arch/arm/mach-ccep/platsmp.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Cloned from linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/unified.h>
#include <asm/smp_plat.h>
#include <asm/mcpm.h>
#include <asm/cp15.h>

#include <mach/sdp_smp.h>
#include <soc/sdp/soc.h>

static DEFINE_SPINLOCK(boot_lock);

static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static void __cpuinit sdp_smp_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);

	set_cpu_online(cpu, true);
}

static struct sdp_power_ops *sdp_power_ops;

void __init sdp_platsmp_init(struct sdp_power_ops *ops)
{
	if (ops)
		sdp_power_ops = ops;
}

void __init sdp_set_power_ops(struct sdp_power_ops *ops)
{
	sdp_power_ops = ops;
}

static int __cpuinit sdp_install_warp(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->install_warp)
		return sdp_power_ops->install_warp(cpu);
	else
		return 0;
}

static int __cpuinit sdp_powerup_cpu(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->powerup_cpu)
		return sdp_power_ops->powerup_cpu(cpu);
	else
		return 0;
}

/* platform-specific code to shutdown a CPU
 * Called with IRQs disabled */
static int sdp_v7_cpu_die(unsigned int cpu)
{
	int spurious = 0;
	unsigned int v;

	v7_exit_coherency_flush(louis);
	
	for (;;) {
		wfi();
		spurious++;
	}

	/* back */
	asm volatile(
		"mrc	p15, 0, %0, c1, c0, 0\n"
		"	orr	%0, %0, %1\n"
		"	mcr	p15, 0, %0, c1, c0, 0\n"
		"	isb\n"
		"	mrc	p15, 0, %0, c1, c0, 1\n"
		"	orr	%0, %0, %2\n"
		"	mcr	p15, 0, %0, c1, c0, 1\n"
			: "=&r" (v)
			: "Ir" (CR_C), "Ir" (0x40)
			: "cc");

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);

	return 9;
}

static void sdp_cpu_die(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->cpu_die)
		sdp_power_ops->cpu_die(cpu);
	else
		sdp_v7_cpu_die(cpu);
}

static int sdp_cpu_kill(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->powerdown_cpu)
		return sdp_power_ops->powerdown_cpu(cpu) ? 0 : 1;
	else
		return 1;
}
static int sdp_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

static int __cpuinit sdp_smp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	printk("start boot_secondary....\r\n");

	sdp_install_warp(cpu);
	sdp_powerup_cpu(cpu);
	
	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/* enable cpu clock on cpu1 */

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */

	write_pen_release(cpu_logical_map((int) cpu));

	/*
	 * This is a later addition to the booting protocol: the
	 * bootMonitor now puts secondary cores into WFI, so
	 * poke_milo() no longer gets the cores moving; we need
	 * to send a soft interrupt to wake the secondary core.
	 * Use smp_cross_call() for this, since there's little
	 * point duplicating the code here
	 */

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		
		arch_send_wakeup_ipi_mask(cpumask_of(cpu));	

		smp_rmb();
		if (pen_release == -1)
		{
			printk("pen release ok!!!!!\n");
			break;
		}

		udelay(10);
	}


	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init sdp_smp_init_cpus(void)
{
	int i;

	for (i = 0; i < nr_cpu_ids; i++)
		set_cpu_possible((u32) i, true);

	//set_smp_cross_call(gic_raise_softirq);
}

static void __init sdp_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int i;

	for(i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);
}

struct smp_operations sdp_smp_ops __initdata = {
	.smp_init_cpus		= sdp_smp_init_cpus,
	.smp_prepare_cpus	= sdp_smp_prepare_cpus,
	.smp_secondary_init	= sdp_smp_secondary_init,
	.smp_boot_secondary	= sdp_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= sdp_cpu_kill,
	.cpu_die		= sdp_cpu_die,
	.cpu_disable		= sdp_cpu_disable,
#endif
};

bool __init sdp_smp_init_ops(void)
{
#if defined(CONFIG_MCPM)
	/*
	 * The best way to detect a multi-cluster configuration at the moment
	 * is to look for the presence of a CCI in the system.
	 * Override the default vexpress_smp_ops if so.
	 */
		mcpm_smp_set_ops();
		return true;
#else
	return false;
#endif
}

