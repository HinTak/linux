/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <linux/export.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include <mach/sdp_cpuidle.h>
#include "common.h"

#include <mach/map.h>

#undef SFR0_BASE
#define SFR0_BASE		(0x00100000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x01000000)

#define SDP1500_MISC_BASE	0x790000
#define SDP1500_MISC_POWER_CTL	0x10
#define SDP1500_MISC_BOOTUP	0x3E0

static struct map_desc sdp1500_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
};

static char const *sdp1500_dt_compat[] __initdata = {
	"samsung,sdp1500",	/* Jazz-M FPGA */
	"samsung,sdp1501",	/* Jazz-Mu */
	"samsung,sdp1521",	/* Jazz-ML */
	"samsung,sdp1531",	/* Jazz-L */
	NULL
};

#if defined(CONFIG_SMP)
static int sdp1500_powerup_cpu(unsigned int cpu)
{
	u32 val;
	void __iomem *base = (void*)(SFR_VA + SDP1500_MISC_BASE - SFR0_BASE);

	/* reset off */
	val = readl(base + 0xc);
	val |= (0x11 << cpu);
	writel(val, base + 0xc);

	/* set cpu power up */
	val = readl(base + 0x10 + cpu * 4);
	val |= 1;
	writel(val, base + 0x10 + cpu * 4);

	/* wait for power up */
//	while (((readl((void *)(base + 0x4)) >> (12 + (cpu * 4))) & 0xF) != 0);
	
	return 0;
}

static int _sdp1500_powerdown_cpu(unsigned int cpu, int kill)
{
	u32 val;
	void __iomem *base = (void*)SFR_VA + SDP1500_MISC_BASE - SFR0_BASE;
	int timeout = 10;

	if (kill) {
		/* wait for wfi */
		while (timeout--) {
			if (((readl(base) >> (cpu + 24)) & 0x1))
				break;
			pr_info("wait for wfi\n");
			udelay(1000);
		}
		if (!timeout) {
			pr_err("wait for wfi timeout\n");
			return -ETIMEDOUT;
		}
	}

	/* set cpu power down */
	val = readl((void *)(base + SDP1500_MISC_POWER_CTL + cpu*4));
	val |= 1 << 4;
	writel(val, (void *)(base + SDP1500_MISC_POWER_CTL + cpu*4));

	if (kill) {
		/* wait power down */	
		udelay(10);
		
		/* reset control */
		val = readl(base + 0xC);
		val &= ~(0x11 << cpu);
		writel(val, (void *)(base + 0xC));
	}
	return 0;
}

static int sdp1500_powerdown_cpu(unsigned int cpu)
{
	return _sdp1500_powerdown_cpu(cpu, 1);
}

static int _sdp1500_set_entry(unsigned int cpu, u32 entry);

static int __cpuinit sdp1500_install_warp(unsigned int cpu)
{
	return _sdp1500_set_entry(cpu, virt_to_phys(sdp_secondary_startup));
}

struct sdp_power_ops sdp1500_power_ops = {
	.powerup_cpu	= sdp1500_powerup_cpu,
	.powerdown_cpu	= sdp1500_powerdown_cpu,
	.install_warp	= sdp1500_install_warp,
};
#endif

#if defined(CONFIG_CPU_IDLE) && !defined(CONFIG_MCPM)
static int sdp1500_cpuidle_powerdown(unsigned int cpu)
{
	u32 reg = SFR_VA + SDP1500_MISC_BASE - SFR0_BASE + 0x10 + cpu * 4;

	/* see v7_exit_coherency_flush() we can just flush at louis level */
	asm volatile(
	"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR \n\t"
	"bic	r0, r0, #(1 << 2) 	@ CR_C	\n\t"
	"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR \n\t"
	"isb	\n\t"
	"bl	v7_flush_dcache_louis	\n\t"
	"clrex	\n\t"
	"mrc	p15, 0, r0, c1, c0, 1	@ get ACTLR \n\t"
	"bic	r0, r0, #(1 << 6)	@ disable local coherency \n\t"
	"mcr	p15, 0, r0, c1, c0, 1	@ set ACTLR \n\t"
	"isb	\n\t"
	"dsb	\n\t"
	"mov	r0, #1	\n\t"
	"mcr      p14, 0, r0, c1, c3, 4	@ OS Double lock \n\t"
	"isb	\n\t"
	"ldr	r0, [%0]		\n\t"
	"orr	r0, r0, #(1 << 4)	\n\t"
	"str	r0, [%0]		\n\t"
	"dsb	\n\t"
	"wfi"
	: : "r"(reg) : "r0","r1","r2","r3","r4","r5","r6","r7",
	      "r9","r10","lr","memory" );

	return 0;
}
#endif

#if defined(CONFIG_SMP)
static int _sdp1500_set_entry(unsigned int cpu, u32 entry)
{
	void __iomem *base = (void*)SFR_VA + (SDP1500_MISC_BASE - SFR0_BASE);

	writel(entry, base + SDP1500_MISC_BOOTUP + cpu * 4);
	dsb();	
	return 0;
}
#endif

#if defined(CONFIG_CPU_IDLE) && !defined(CONFIG_MCPM)
struct sdp_cpuidle_ops sdp1500_cpuidle_ops = {
	.set_entry	= _sdp1500_set_entry,
	.powerdown_cpu	= sdp1500_cpuidle_powerdown,
};
#endif

static void __init sdp1500_map_io(void)
{
	iotable_init(sdp1500_io_desc, ARRAY_SIZE(sdp1500_io_desc));
}

static void __init sdp1500_init_early(void)
{
#if defined(CONFIG_SMP)
	sdp_platsmp_init(&sdp1500_power_ops);
#endif

#if defined(CONFIG_CPU_IDLE) && !defined(CONFIG_MCPM)
	sdp_cpuidle_init(&sdp1500_cpuidle_ops);
#endif
}

DT_MACHINE_START(SDP1500_DT, "Samsung SDP1501(Flattened Device Tree)")
	/* Maintainer: */
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= sdp1500_map_io,
	.dt_compat	= sdp1500_dt_compat,
	.init_early	= sdp1500_init_early,
MACHINE_END

