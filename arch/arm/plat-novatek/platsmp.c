/*
 *  linux/arch/arm/plat-novatek/platsmp.c
 *
 *  Copyright (C) Novatek Ltd.
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

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>

#include <linux/io.h>
#include "mach/motherboard.h"
#include "plat/platsmp.h"
extern void __iomem * clk_reg_base;
void __iomem * l2_base;

/*
 * Write the address of secondary startup into the
 * system register. The nvt_secondary_startup start to check
 * the pen_release value.
 */
static void __cpuinit set_secondary_entry(unsigned int cpu_id)
{
	switch (cpu_id) {
		case 1:
			writel(virt_to_phys(nvt_secondary_startup), (volatile unsigned int *)(clk_reg_base+NVTCORE1_JUMP_OFFSET));
			break;
		case 2:
			writel(virt_to_phys(nvt_secondary_startup), (volatile unsigned int *)(clk_reg_base+NVTCORE2_JUMP_OFFSET));
			break;
		case 3:
			writel(virt_to_phys(nvt_secondary_startup), (volatile unsigned int *)(clk_reg_base+NVTCORE3_JUMP_OFFSET));
			break;
		default:
			break;
	}
}

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 * The kernel L2 cache is ready. So call the outer_clean_range API.  
 */
static void __cpuinit write_pen_release(int val)
{
  	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	//outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

/* The kernel L2 cache hasn't ready yet. */
static void __cpuinit write_pen_release_without_kernel_l2(int val)
{
  	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	writel_relaxed(__pa(&pen_release), (l2_base + L2X0_CLEAN_LINE_PA));
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit nvt_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);

	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int __cpuinit nvt_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	int i;

	set_secondary_entry(cpu);
	for(i = 0; i < 10;i++)
	{
		l2_base = ioremap(0xffe00000, 0x1000);
		if(!l2_base)
		{
			printk("retry %d time\n", i);
		}
		else
		{
			printk("remap L2 page sucess\n");
			break;
		}
	}
	if(!l2_base)
	{
		printk("remap L2 page fail\n");
		while(1)
		{};
	}

	writel_relaxed(__pa(&secondary_data), (l2_base + L2X0_CLEAN_LINE_PA));
	writel_relaxed((__pa(&secondary_data) + 32), (l2_base + L2X0_CLEAN_LINE_PA));

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * This is really belt and braces; we hold unintended secondary
	 * CPUs in the holding pen until we're ready for them.  However,
	 * since we haven't sent them a soft interrupt, they shouldn't
	 * be there.
	 */
	write_pen_release_without_kernel_l2(cpu_logical_map(cpu));

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	gic_raise_softirq(cpumask_of(cpu), 0);

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();

		if (pen_release == -1)
			break;

		udelay(10);
	}
	printk("pen_release: %d\n\t", pen_release);

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	iounmap(l2_base);

	return pen_release != -1 ? -ENOSYS : 0;
}
