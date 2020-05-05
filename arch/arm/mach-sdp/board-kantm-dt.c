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
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include "common.h"

#include <mach/map.h>

#undef SFR0_BASE
#define SFR0_BASE		(0x00100000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x01000000)

#define SDP1600_MISC_BASE	(0x400000)
#define SDP1600_MISC_BOOTUP	(0x3E0)

static struct map_desc sdp1600_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
};

static char const *sdp1600_dt_compat[] __initdata = {
	"samsung,sdp1600",	/* Kant-M FPGA */
	"samsung,sdp1601",	/* Kant-M */
	"samsung,sdp1701",	/* Kant-M2 */
	NULL
};

#if defined(CONFIG_SMP)
/* we cannot shutdown the cpu, just reset */
static int sdp1600_powerup_cpu(unsigned int cpu)
{
	u32 val;
	void __iomem *base = (void*)(SFR_VA + SDP1600_MISC_BASE - SFR0_BASE);

	/* reset off */
	val = readl(base + 0xC);
	val |= (0x11 << cpu);
	writel(val, (void *)(base + 0xC));

	udelay(100);

	asm volatile("sev");

	return 0;
}

static int sdp1600_powerdown_cpu(unsigned int cpu)
{
	u32 val;
	void __iomem *base = (void*)SFR_VA + SDP1600_MISC_BASE - SFR0_BASE;
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
	
	/* reset on  */
	val = readl(base + 0xC);
	val &= ~(0x11 << cpu);
	writel(val, (void *)(base + 0xC));

	udelay(10);
	
	return 0;
}

static int sdp1600_set_entry(unsigned int cpu, u32 entry)
{
	void __iomem *base = (void*)SFR_VA + (SDP1600_MISC_BASE - SFR0_BASE);

	writel(entry, base + SDP1600_MISC_BOOTUP + cpu * 4);
	dsb();	
	return 0;
}

static int __cpuinit sdp1600_install_warp(unsigned int cpu)
{
	return sdp1600_set_entry(cpu, virt_to_phys(sdp_secondary_startup));
}

struct sdp_power_ops sdp1600_power_ops = {
	.powerup_cpu	= sdp1600_powerup_cpu,
	.powerdown_cpu	= sdp1600_powerdown_cpu,
	.install_warp	= sdp1600_install_warp,
};
#endif

static void __init sdp1601_reserve(void)
{
	/* BUS Prevent Error Address */
	const phys_addr_t reserve_addr = (phys_addr_t) 0x100FF0000ULL;
	const phys_addr_t reserve_addr2 = (phys_addr_t) 0x84800000ULL;
	
	if (pfn_valid(reserve_addr >> PAGE_SHIFT)) {
		pr_info("sdp1601: reserve 1 pages for Prevent address 0x1_00FF_0000\n");
		memblock_reserve(reserve_addr, PAGE_SIZE);
	}

	if (pfn_valid(reserve_addr2 >> PAGE_SHIFT)) {
		pr_info("sdp1601: reserve 8MB for Prevent address 0x8480_0000\n");
		memblock_reserve(reserve_addr2, 0x800000);
	}	
}

static void __init sdp1600_map_io(void)
{
	iotable_init(sdp1600_io_desc, ARRAY_SIZE(sdp1600_io_desc));
}

static void __init sdp1600_init_early(void)
{
#if defined(CONFIG_SMP)
	sdp_platsmp_init(&sdp1600_power_ops);
#endif
}

DT_MACHINE_START(SDP1600_DT, "Samsung SDP1601(Flattened Device Tree)")
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= sdp1600_map_io,
	.dt_compat	= sdp1600_dt_compat,
	.init_early	= sdp1600_init_early,
	.reserve	= sdp1601_reserve,
MACHINE_END
