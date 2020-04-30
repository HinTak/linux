/*
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
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
#include <mach/map.h>

#if defined(CONFIG_SPARSEMEM)
phys_addr_t __sdp_virt_to_phys(unsigned long x)
{
	struct memblock_type *mt = &memblock.memory;
	unsigned long local_offset = x - PAGE_OFFSET;
	phys_addr_t base;
	int i;

	if (unlikely(mt->cnt <= 1))
		return PHYS_OFFSET + local_offset;

	for (i = 0; i < mt->cnt; i++) {
		base = mt->regions[i].base;
		if (likely(local_offset < mt->regions[i].size))
			break;
		local_offset -= mt->regions[i].size;
	}
	return base + local_offset;
}
EXPORT_SYMBOL(__sdp_virt_to_phys);

unsigned long __sdp_phys_to_virt(phys_addr_t x)
{
	struct memblock_type *mt = &memblock.memory;
	unsigned long channel_offset = 0;
	unsigned long local_offset = (unsigned long)(x - PHYS_OFFSET);
	int i;

	if (unlikely(mt->cnt <= 1))
		return PAGE_OFFSET + local_offset;

	for (i = 0; i < mt->cnt; i++) {
		local_offset = (unsigned long)(x - mt->regions[i].base);
		if (likely(local_offset < mt->regions[i].size))
			break;
		channel_offset += (unsigned long)mt->regions[i].size;
	}
	return PAGE_OFFSET + channel_offset + local_offset;
}
EXPORT_SYMBOL(__sdp_phys_to_virt);

#endif

