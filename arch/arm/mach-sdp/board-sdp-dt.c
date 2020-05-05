/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <linux/export.h>
#include <linux/proc_fs.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/arch.h>
#include <asm/setup.h>
#include <mach/sdp_smp.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include "common.h"


int sdp_mem_nr __read_mostly = 0;
phys_addr_t sdp_mem_base[NR_BANKS] __read_mostly = {0x0, };
phys_addr_t sdp_mem_size[NR_BANKS] __read_mostly = {0x0, };

phys_addr_t sdp_mach_mem0_size = (phys_addr_t) -1;
phys_addr_t sdp_mach_mem1_size = (phys_addr_t) -1;
phys_addr_t sdp_mach_mem2_size = (phys_addr_t) -1;

/* for __virt_to_phys(), __phys_to_virt() macros  */
#if defined(CONFIG_SPARSEMEM) && defined(CONFIG_NEED_MACH_MEMORY_H)
EXPORT_SYMBOL(sdp_mem_nr);
EXPORT_SYMBOL(sdp_mem_base);
EXPORT_SYMBOL(sdp_mem_size);
#endif

#define SDP1304_MISC_BASE	0x10F70000
#define SDP1304_MISC_POWER_CTL	0x10
#define SDP1304_MISC_BOOTUP	0x3F0

//extern void sdp_chipid_init(void __iomem *regs) __init;

static struct map_desc sdp_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)0xFE000000,
		.pfn		= __phys_to_pfn(0x10000000),
		.length		= SZ_16M,
		.type		= MT_DEVICE,
	}
};

#if defined(CONFIG_SMP)

static __cpuinit int golfap_install_powerup(unsigned int cpu)
{
	void __iomem *base;
	u32 mask, val;

	base = ioremap(SDP1304_MISC_BASE, 32);
	
	mask = 0x1UL << ((cpu - 1) * 4);
	val = readl((void *) ((u32) base + SDP1304_MISC_POWER_CTL)) & ~mask;
	val |= mask;
	writel(val, (void *) ((u32) base + SDP1304_MISC_POWER_CTL));
	val &= ~mask;
	udelay(1);
	writel(val, (void *) ((u32) base + SDP1304_MISC_POWER_CTL));
	dmb();
	while(readl(base) & (1UL << (cpu + 20)));	//wait for CPU power up
	iounmap(base);

	return 0;
}

static int golfap_install_powerdown(unsigned int cpu)
{
	void __iomem *base;
	u32 mask, val;

	base = ioremap(SDP1304_MISC_BASE, 32);
	while(!(readl(base) & (1UL << (cpu + 24))));	//wait for WFI enterance
	mask = 0x1UL << ((cpu - 1) * 4 + 1);
	val = readl((void *) ((u32) base + SDP1304_MISC_POWER_CTL)) & ~mask;
	val |= mask;
	writel(val, (void *) ((u32) base + SDP1304_MISC_POWER_CTL));
	val &= ~mask;
	udelay(1);
	writel(val, (void *) ((u32) base + SDP1304_MISC_POWER_CTL));
	dmb();
	while(!(readl((void *) base) & (1UL << (cpu + 20))));	//wait for CPU power down
	return 0;
}

static __cpuinit int golfap_install_warp(unsigned int cpu)
{
	void __iomem *base;

	base = ioremap(SDP1304_MISC_BASE, 0x400);
	writel_relaxed(virt_to_phys(sdp_secondary_startup)
		, (void *) ((u32) base + SDP1304_MISC_BOOTUP + cpu * 4));
	dmb();	
	udelay(1);	
	iounmap(base);
	return 0;
}

struct sdp_power_ops sdp1304_power_ops = {
	.powerup_cpu	= golfap_install_powerup,
	.powerdown_cpu	= golfap_install_powerdown,
	.install_warp = golfap_install_warp,
};

#endif

#include <mach/sdp_rewritable_const.h>
void __init sdp_dt_init_machine(void)
{
#ifdef CONFIG_USE_REWRITABLE_CONST
	static DEF_EDITED_VALUES();
	printk("Rewritable Const: Edited value# %d\n", REF_EDITED_VALUES());
#endif

	printk("[%d] %s\n", __LINE__, __func__);
	/* TODO */

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static char const *sdp1304_dt_compat[] __initdata = {
	"samsung,sdp1304",
	NULL
};

unsigned int sdp_get_mem_cfg(int nType)
{
	switch(nType)
	{
		case 0:
			return (u32) sdp_mem_size[0];
		case 1:
			return (u32) sdp_mach_mem0_size;
		case 2:
			return (u32) sdp_mem_size[1];
		case 3:
			return (u32) sdp_mach_mem1_size;
		case 4:
			return (u32) sdp_mem_size[2];
		case 5:
			return (u32) sdp_mach_mem2_size;
		default:
			return (u32) -1;
	}
}

#if 0
static void __init sdp1304_check_revision(void)
{
	
}
#endif

static void __init sdp1304_map_io(void)
{
	printk("[%d] %s\n", __LINE__, __func__);
	iotable_init(sdp_io_desc, ARRAY_SIZE(sdp_io_desc));
//	sdp_chipid_init(SDP_VA_CHIPID);
#if defined(CONFIG_SMP)
	sdp_set_power_ops(&sdp1304_power_ops);
#endif
	
	sdp_mem_base[0] = meminfo.bank[0].start;
	sdp_mem_size[0] = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_mem_size[0] += meminfo.bank[1].size;
}

#if defined(CONFIG_SPARSEMEM) && defined(CONFIG_NEED_MACH_MEMORY_H)
static int __init setup_sdp_meminfo(char *buf)
{
	int i;

	pr_info("sdp meminfo:\n");

	sdp_mem_nr = 0;

	for_each_bank(i, &meminfo) {
		struct membank *bank = &meminfo.bank[i];

		sdp_mem_nr++;
		sdp_mem_base[i] = bank_phys_start(bank);
		sdp_mem_size[i] = bank_phys_size(bank);

		pr_info("\t%llx@%llx\n", (unsigned long long)sdp_mem_size[i],
				(unsigned long long)sdp_mem_base[i]);
	}
	BUG_ON(!sdp_mem_nr || !sdp_mem_base[0] || !sdp_mem_size[0]);

	return 0;
}
early_param("sdp_sparsemem", setup_sdp_meminfo);
#endif

static void __init sdp1304_reserve(void)
{
	/* EHCI workaround */
	const phys_addr_t reserve_addr = (0xffffffff - PAGE_SIZE + 1);
	if (pfn_valid(reserve_addr >> PAGE_SHIFT)) {
		pr_info("sdp1304: reserve last page on 32bit boundary.\n");
		memblock_reserve(reserve_addr, PAGE_SIZE);
	}
}

void __init sdp_init_time(void)
{
#ifdef CONFIG_OF
	of_clk_init(NULL);
	clocksource_of_init();
#endif
}

static int _sdp_cpuidle_enable(int enable)
{
	extern void sdp_cpuidle_enable_state(int state, int enable);
#if 0
	if (IS_ENABLED(CONFIG_CPU_IDLE)) {
		sdp_cpuidle_enable_state(1, enable);
		return 0;
	}
#endif
	return -1;
}

void sdp_cpuidle_enable_c1(void)
{
	if (_sdp_cpuidle_enable(1))
		pr_warn("warning: sdp-cpuidle enable request ignored.\n");
}
EXPORT_SYMBOL(sdp_cpuidle_enable_c1);

void sdp_cpuidle_disable_c1(void)
{
	if (_sdp_cpuidle_enable(0))
		pr_warn("warning: sdp-cpuidle disable request ignored.\n");
}
EXPORT_SYMBOL(sdp_cpuidle_disable_c1);

DT_MACHINE_START(SDP1304_DT, "Samsung SDP1304(Flattened Device Tree)")
	/* Maintainer: */
	.init_irq	= sdp_init_irq,
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= sdp1304_map_io,
	.init_machine	= sdp_dt_init_machine,
	.init_time = sdp_init_time,
	.dt_compat	= sdp1304_dt_compat,
	.restart	= sdp_restart,
	.reserve	= sdp1304_reserve,
MACHINE_END

