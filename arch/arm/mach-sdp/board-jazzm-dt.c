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

#include <asm/processor.h>
#include <asm/idmap.h>
#include <asm/cputype.h>
#include <asm/pgalloc.h>

#include <mach/sdp_smp.h>
#include <mach/sdp_cpuidle.h>
#include "common.h"

#include <mach/map.h>

#undef SFR0_BASE
#define SFR0_BASE		(0x00100000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x00f00000)

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
	"samsung,sdp1511",	/* Jazz-Mt */
	"samsung,sdp1521",	/* Jazz-ML */
	"samsung,sdp1531",	/* Jazz-L */
	NULL
};

static int _sdp1500_set_entry(unsigned int cpu, u32 entry)
{
	void __iomem *base = (void*)SFR_VA + (SDP1500_MISC_BASE - SFR0_BASE);

	writel_relaxed(entry, base + SDP1500_MISC_BOOTUP + cpu * 4);
	dsb();
	return 0;
}

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

static int sdp1500_powerdown_cpu(unsigned int cpu)
{
	u32 val;
	void __iomem *base = (void*)SFR_VA + SDP1500_MISC_BASE - SFR0_BASE;
	int timeout = 10;

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
	/* prevent supurious wakeup */

	/* Jazz-M undocumented register 0x790088
	 *  - bit[19:16]: cpu[3-0]'s wake-up by irq enable.
	 *  - bit[23:20]: cpu[3-0]'s wake-up by fiq enable. */
	val = readl(base + SDP1500_MISC_POWER_CTL + 0x88);
	val &= ~(0x11 << (cpu + 16));
	writel(val, base + SDP1500_MISC_POWER_CTL + 0x88);

	/* set cpu power down */
	val = readl_relaxed((void *)(base + SDP1500_MISC_POWER_CTL + cpu*4));
	val |= 1 << 4;	/* pwrdn_req */
	writel_relaxed(val, (void *)(base + SDP1500_MISC_POWER_CTL + cpu*4));
	dsb();

	return 0;
}

static int __cpuinit sdp1500_install_warp(unsigned int cpu)
{
	return _sdp1500_set_entry(cpu, virt_to_phys(sdp_secondary_startup));
}

struct sdp_power_ops sdp1500_smp_power_ops = {
	.powerup_cpu	= sdp1500_powerup_cpu,
	.powerdown_cpu	= sdp1500_powerdown_cpu,
	.install_warp	= sdp1500_install_warp,
};
#endif	/* CONFIG_SMP */

#undef DEBUG_NO_POWERDOWN

/* This is not proven to work, see cpu_suspend()'s error handlings. */
#undef RESTORE_DIRECT

static int sdp1500_cpuidle_powerdown(unsigned int cpu)
{
	void __iomem *base = (void*)SFR_VA + SDP1500_MISC_BASE - SFR0_BASE;
	void __iomem *cpu_pwr_reg = base + (0x10 + cpu * 4);
	u32 val;

	/* this is essential for turning off SCTLR.C */
	setup_mm_for_reboot();

	/* Jazz-M undocumented register 0x790088 */
	val = readl(base + SDP1500_MISC_POWER_CTL + 0x88);
	val |= (0x11 << (cpu + 16));
	writel(val, base + SDP1500_MISC_POWER_CTL + 0x88);

	/* see v7_exit_coherency_flush() we can just flush at louis level */
	asm volatile(
		"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR \n\t"
		"bic	r0, r0, #0x0004		@ C \n\t"
		"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR \n\t"
		"isb	\n\t"
		"dsb	\n\t"
		
		"bl	v7_flush_dcache_louis	\n\t"
		
		"clrex	\n\t"

		"mrc	p15, 0, r0, c1, c0, 1	@ get ACTLR \n\t"
		"bic	r0, r0, #(1 << 6)	@ disable local coherency \n\t"
		"mcr	p15, 0, r0, c1, c0, 1	@ set ACTLR \n\t"
		"isb	\n\t"
		"dsb	\n\t"

#if !defined(CONFIG_SDP_DEBUG_OSSR_V7_1)
		/* TRACE32 support: do not power down after halt debug mode entered once.
		 *  sdp's TRACE32 script should set DBGPRCR/CORENPDRQ
		 *  to prevent cpu from power down. */
		"mov	r2, #0xff000000 @ dummy for debugging\n\t"
		"mrc    p14, 0, r1, c1, c4, 4	@ DBGPRCR	\n\t"
		"ands	r1, r1, #1		\n\t"
		"bne	skip_power_down		\n\t"
#else
		/* This is for ARMv7 standard debug over powerdown scheme. */
		"mov	r0, #1	\n\t"
		"mcr 	p14, 0, r0, c1, c3, 4	@ OS Double lock \n\t"
		"isb	\n\t"
#endif

#if !defined(DEBUG_NO_POWERDOWN)
		"ldr	r3, [%0]		\n\t"
		"orr	r3, r3, #(1 << 4)	\n\t"
		"str	r3, [%0]		\n\t"
#endif

		"skip_power_down:		\n\t"

		"dsb	\n\t"
		"wfi	\n\t"
		: : "r"(cpu_pwr_reg) : "r0","r1","r2","r3","r4","r5","r6","r7",
		      "r9","r10","lr","memory" );

	/* Disabling gic cpu interface done in cpuidle.c never wakes up the cpu. */
#if defined(RESTORE_DIRECT)
	asm volatile(
		"mrc	p15, 0, r0, c1, c0, 1	@ get ACTLR \n\t"
		"orr	r0, r0, #(1 << 6)	@ disable local coherency \n\t"
		"mcr	p15, 0, r0, c1, c0, 1	@ set ACTLR \n\t"
		"isb	\n\t"
		"dsb	\n\t"

		"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR \n\t"
		"orr	r0, r0, #(1 << 2) 	@ CR_C	\n\t"
		"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR \n\t"
		"isb	\n\t"
		"dsb	\n\t"
		"nop	\n\t"
		"nop	\n\t"
		"nop	\n\t"
		"nop	\n\t"
		: : : "r0", "memory" );
#endif		

#if defined(CONFIG_SDP_DEBUG_OSSR_V7_1)
	asm volatile(
		"mov	r0, #0	\n\t"
		"mcr    p14, 0, r0, c1, c3, 4	@ OS Double lock \n\t"
		"isb	\n\t"
		: : "r"(reg) : "r0", "memory" );
#endif

#if !defined(RESTORE_DIRECT)
	/* do soft restart */	
	{

		typedef void (*phys_reset_t)(unsigned long);
		phys_reset_t phys_reset =
			(phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
		
		phys_reset(0);
	}
#endif

	return 0;	/* meaningless */
}

struct sdp_cpuidle_ops sdp1500_cpuidle_ops = {
	.set_entry	= _sdp1500_set_entry,
	.powerdown_cpu	= sdp1500_cpuidle_powerdown,
};

static void __init sdp1500_map_io(void)
{
	iotable_init(sdp1500_io_desc, ARRAY_SIZE(sdp1500_io_desc));

#if defined(CONFIG_SMP)
	sdp_platsmp_init(&sdp1500_smp_power_ops);
#endif
	sdp_cpuidle_init(&sdp1500_cpuidle_ops);

#if !defined(CONFIG_SPARSEMEM)
	sdp_mem_size[0] = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_mem_size[0] += meminfo.bank[1].size;
#endif
}

static void __init sdp1500_init_time(void)
{
#ifdef CONFIG_OF
	of_clk_init(NULL);
	clocksource_of_init();
#endif
}

static void __init sdp1500_reserve(void)
{
}

DT_MACHINE_START(SDP1500_DT, "Samsung SDP1501(Flattened Device Tree)")
	/* Maintainer: */
	.init_irq	= sdp_init_irq,
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= sdp1500_map_io,
	.init_machine	= sdp_dt_init_machine,
	.init_time	= sdp1500_init_time,
	.dt_compat	= sdp1500_dt_compat,
	.restart	= sdp_restart,
	.reserve	= sdp1500_reserve,
MACHINE_END
