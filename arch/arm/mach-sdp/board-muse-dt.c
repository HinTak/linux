/*
 * Copyright (C) 2018 Samsung Electronics Co.Ltd
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

#if defined(CONFIG_SDP_KVALUE)
#include <soc/sdp/sdp_kvalue.h>
#define MICOM_SHARED_BASE_KEYSTR "micom_shared_base"
#endif

#undef SFR0_BASE
#define SFR0_BASE		(0x00400000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x00010000)

#define SDP18xx_MISC_BASE	(0x400000)
#define SDP18xx_MISC_BOOTUP	(0x3E0)

#define SDP18xx_MICOMGPR_BASE	(0x9C12C0)
#define SDP18xx_MICOMGPR_SIZE	(0x1000)/*page align*/
#define SDP18xx_MICOMGPR_IOMEM	((VA_SFR0_BASE+SFR0_SIZE)+(SDP18xx_MICOMGPR_BASE & (PAGE_SIZE-1)))

static struct map_desc sdp18xx_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
	[1] = {
		.virtual	= (unsigned long)VA_SFR0_BASE+SFR0_SIZE,
		.pfn		= __phys_to_pfn(SDP18xx_MICOMGPR_BASE),
		.length		= SDP18xx_MICOMGPR_SIZE,
		.type		= MT_DEVICE,
	},
};

static char const *sdp18xx_dt_compat[] __initdata = {
	"samsung,sdp1803",	/* Muse-M ES */
	"samsung,sdp1804",	/* Muse-L ES */
	NULL
};

static int sdp1800_mark_boot_progress(u32 value) {
	/* mark boot progress to Micom SRAM(0x9c12d8) */
	writel(value, (void *)((VA_SFR0_BASE+SFR0_SIZE)
		+ (0x9c12d8 - (SDP18xx_MICOMGPR_BASE & PAGE_MASK))));

	return 0;
}

#if defined(CONFIG_SMP)
static int sdp18xx_powerup_cpu(unsigned int cpu)
{
	u32 val;
	void __iomem *base = (void*)(SFR_VA + SDP18xx_MISC_BASE - SFR0_BASE);

	val = readl(base + 0xC);
	val |= (0x11 << cpu);
	writel(val, (void *)(base + 0xC));

	udelay(100);

	asm volatile("sev");

	return 0;
}

/* we cannot shutdown the cpu, just reset */
static int _sdp18xx_powerdown_cpu(unsigned int cpu, int kill)
{
	u32 val;
	void __iomem *base = (void*)SFR_VA + SDP18xx_MISC_BASE - SFR0_BASE;
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
		
		/* reset on  */
		val = readl(base + 0xC);
		val &= ~(0x11 << cpu);
		writel(val, (void *)(base + 0xC));

		udelay(10);
	} else {
		asm volatile(
			"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR \n\t"
			"bic	r0, r0, #(1 << 2) 	@ CR_C	\n\t"
			"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR \n\t"
			"isb	\n\t"

			/* Disable L2 prefetches */	
			"mrrc	p15, 1, r0, r1, c15	@ CPUECTLR\n\t"
			"bic	r1, r1, #0x1b		@ 36,35,33,32\n\t"
			"mcrr	p15, 1, r0, r1, c15	\n\t"
			"isb	\n\t"
			"dsb	\n\t"

			/* Disable Load-store hardware prefetcher */
			"mrrc	p15, 0, r0, r1, c15	@ CPUACTLR\n\t"
			"bic	r1, r1, #(1<<24)	@ 56\n\t"
			"mcrr	p15, 0, r0, r1, c15	@ CPUACTLR\n\t"
			"isb	\n\t"
			"dsb	\n\t"

			"bl	v7_flush_dcache_louis	\n\t"
			
			"clrex	\n\t"

			/* SMPEN off */
			"mrrc	p15, 1, r0, r1, c15	@ CPUECTLR\n\t"
			"bic	r0, r0, #(1 << 6)	\n\t"
			"mcrr	p15, 1, r0, r1, c15	\n\t"
			"isb	\n\t"
			"dsb	\n\t"

			"wfi"
			: : : "r0","r1","r2","r3","r4","r5","r6","r7",
	      			"r9","r10","lr","memory" );
		
		panic("CPU%u: spurious wakeup calls\n", cpu);
	}
	return 0;
}

static int sdp18xx_powerdown_cpu(unsigned int cpu)
{
	return _sdp18xx_powerdown_cpu(cpu, 1);
}
static int sdp18xx_cpu_die(unsigned int cpu)
{
	return _sdp18xx_powerdown_cpu(cpu, 0);
}

static int _sdp18xx_set_entry(unsigned int cpu, u32 entry)
{
	void __iomem *base = (void*)SFR_VA + (SDP18xx_MISC_BASE - SFR0_BASE);

	writel(entry, base + SDP18xx_MISC_BOOTUP + cpu * 4);
	dsb();	
	return 0;
}

static int __cpuinit sdp18xx_install_warp(unsigned int cpu)
{
	return _sdp18xx_set_entry(cpu, virt_to_phys(sdp_secondary_startup));
}

struct sdp_power_ops sdp18xx_power_ops = {
	.powerup_cpu	= sdp18xx_powerup_cpu,
	.powerdown_cpu	= sdp18xx_powerdown_cpu,
	.install_warp	= sdp18xx_install_warp,
	.cpu_die	= sdp18xx_cpu_die,
};
#endif

static void __init sdp18xx_map_io(void)
{
	iotable_init(sdp18xx_io_desc, ARRAY_SIZE(sdp18xx_io_desc));
}

static void __init sdp18xx_init_early(void)
{
	printk("log_buf info: physaddr %08llx, size %x\n", (uint64_t)virt_to_phys(log_buf_addr_get()), log_buf_len_get());
	writel(virt_to_phys(log_buf_addr_get()), (void *)((VA_SFR0_BASE+SFR0_SIZE)
		+ (0x9c12f8 - (SDP18xx_MICOMGPR_BASE & PAGE_MASK))));
	writel(log_buf_len_get(), (void *)((VA_SFR0_BASE+SFR0_SIZE)
		+ (0x9c12fc - (SDP18xx_MICOMGPR_BASE & PAGE_MASK))));

	register_boot_progress_fn(sdp1800_mark_boot_progress);
	mark_boot_progress(0xB0);

#if defined(CONFIG_SDP_KVALUE)
	sdp_kvalue_setptr_early(MICOM_SHARED_BASE_KEYSTR, (void*)SDP18xx_MICOMGPR_IOMEM);
#endif/*defined(CONFIG_SDP_KVALUE)*/

#if defined(CONFIG_SMP)
	sdp_platsmp_init(&sdp18xx_power_ops);
#endif
}

DT_MACHINE_START(SDP18xx_DT, "Samsung Muse Series(Flattened Device Tree)")
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= sdp18xx_map_io,
	.dt_compat	= sdp18xx_dt_compat,
	.init_early	= sdp18xx_init_early,
MACHINE_END
