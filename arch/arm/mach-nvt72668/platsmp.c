/*
 *  linux/arch/arm/mach-nvt72668/platsmp.c
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
#include <linux/smp.h>
#include <linux/io.h>
#include <asm/smp_scu.h>
#include <asm/mach/map.h>
#include <mach/motherboard.h>
#include "plat/platsmp.h"
#include "core.h"

static void __iomem *scu_base;

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init nvt_smp_init_cpus(void)
{
	unsigned int ncores = 0, i;

	scu_base = ioremap(get_periph_base() + A9_MPCORE_SCU_OFFSET, 0x100);
	ncores = scu_get_core_count(scu_base);
	if (ncores < 2)
		return;

	if (ncores > (unsigned int) nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
				ncores, nr_cpu_ids);
		ncores = (unsigned int) nr_cpu_ids;
	}

	for (i = 0; i < ncores; ++i)
		set_cpu_possible(i, true);
}

static void __init nvt_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(scu_base);
}

struct smp_operations __initdata nvt_ca9_smp_ops = {
	.smp_init_cpus		= nvt_smp_init_cpus,
	.smp_prepare_cpus	= nvt_smp_prepare_cpus,
	.smp_secondary_init	= nvt_secondary_init,
	.smp_boot_secondary	= nvt_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= nvt_ca9_cpu_die,
#endif
};
