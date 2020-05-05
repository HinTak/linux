/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include "common.h"

extern void sdp_dt_init_machine(void) __init;

static struct map_desc sdp1302_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
#if SFR_NR_BANKS == 2
	[1] = {
		.virtual	= VA_SFR1_BASE,
		.pfn		= __phys_to_pfn(SFR1_BASE),
		.length		= SFR1_SIZE,
		.type		= MT_DEVICE,
	},
#endif
};

static char const *sdp1302_dt_compat[] __initdata = {
	"samsung,sdp1302",
	NULL
};

struct sdp_power_ops sdp1302_power_ops = {
	.powerup_cpu	= NULL,
	.powerdown_cpu	= NULL,
	.install_warp	= NULL,
};

static void __init sdp1302_init_early(void)
{
	l2x0_of_init(0, 0xfdffffff);
}

#if !defined(CONFIG_SPARSEMEM)
extern phys_addr_t sdp_sys_mem0_size;
#endif

static void __init golfs_map_io(void)
{
	iotable_init(sdp1302_io_desc, ARRAY_SIZE(sdp1302_io_desc));
	sdp_platsmp_init(&sdp1302_power_ops);

#if !defined(CONFIG_SPARSEMEM)
	sdp_sys_mem0_size = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_sys_mem0_size += meminfo.bank[1].size;
#endif
}

#ifdef CONFIG_HDMA_DEVICE
#define MAX_OF_BANK 5
#define MAX_CMA_REGIONS 16

struct hdmaregion  {
        phys_addr_t start;
        phys_addr_t size;
        bool check_fatal_signals;
        struct {
                phys_addr_t size;
                phys_addr_t start;
        } aligned;
        /* page is returned/used by dma-contiguous API to allocate/release
           memory from contiguous pool */
        struct page *page;
        unsigned int count;
        struct device *dev;
        struct device dev2;
};

struct cmainfo  {
        int nr_regions;
        struct hdmaregion region[MAX_CMA_REGIONS];
};

extern struct cmainfo hdmainfo;
extern struct meminfo meminfo;
unsigned long hdma_size_of_bank[MAX_OF_BANK];

extern void hdma_regions_reserve(void);

static void __init golfs_reserve(void)
{
	int i,j;

	hdma_regions_reserve();

	for(i=0; i<meminfo.nr_banks; i++) {
	for(j=0; j<hdmainfo.nr_regions; j++) {
		if((meminfo.bank[i].start <= hdmainfo.region[j].start) &&
			((meminfo.bank[i].start + meminfo.bank[i].size) >=
			(hdmainfo.region[j].start + hdmainfo.region[j].size)))
			hdma_size_of_bank[i] += hdmainfo.region[j].size;
	}
	}
}
#else
static void __init golfs_reserve(void)
{
}
#endif

DT_MACHINE_START(SDP1302_DT, "Samsung SDP1302(Flattened Device Tree)")
	/* Maintainer: */
	.reserve	= golfs_reserve,
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= golfs_map_io,
	.init_early	= sdp1302_init_early,
	.init_irq	= sdp_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= sdp_dt_init_machine,
	.timer		= &sdp_timer,
	.dt_compat	= sdp1302_dt_compat,
	.restart	= sdp_restart,
MACHINE_END

