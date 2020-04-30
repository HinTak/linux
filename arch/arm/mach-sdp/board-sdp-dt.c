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
#ifdef CONFIG_SPARSE_LOWMEM_EXT_MAP
extern unsigned int lowmem_ext_initialized;

phys_addr_t __sdp_dma_virt_to_phys(unsigned long x)
{
	phys_addr_t* phys_tbl = &memblock.phys_table[0];
	unsigned long local_offset = x - PAGE_OFFSET;
	if (unlikely(!lowmem_ext_initialized))
		return __sdp_virt_to_phys(x);
#ifdef CONFIG_DEBUG_LOWMEM_EXT_POISONING 
	if (PAGETBL_IDX(local_offset) >= MAX_VADDR_TABLE_SIZE || !phys_tbl[PAGETBL_IDX(local_offset)])
	{
		pr_err("[error] Caution invalid address convertion(0x%lx) while phys to virt"
				"[PAGETBL_IDX(%lx) MAX_VADDR_TABLE_SZ:%x virt_tbl:0x%llx]\n",
				x, PAGETBL_IDX(local_offset), MAX_PADDR_TABLE_SIZE, 
				local_offset < MAX_PADDR_TABLE_SIZE ? phys_tbl[PAGETBL_IDX(local_offset)] :
				0x0ULL);
		dump_stack();
	}
#endif
	return phys_tbl[PAGETBL_IDX(local_offset)] | (x & PAGETBL_ADDR_MASK);
}
EXPORT_SYMBOL(__sdp_dma_virt_to_phys);

unsigned long __sdp_dma_phys_to_virt(phys_addr_t x)
{
	unsigned long* virt_tbl = &memblock.virt_table[0];
	unsigned long local_offset = (unsigned long)(x - PHYS_OFFSET);

	if (unlikely(!lowmem_ext_initialized))
		return __sdp_phys_to_virt(x);

#ifdef CONFIG_DEBUG_LOWMEM_EXT_POISONING 
	if (PAGETBL_IDX(local_offset) >= MAX_VADDR_TABLE_SIZE || !virt_tbl[PAGETBL_IDX(local_offset)])
	{
		pr_err("[error] Caution invalid address(%pa) convertion while phys to virt"
				"[PAGETBL_IDX(0x%lx) MAX_VADDR_TABLE_SZ:0x%x virt_tbl:0x%lx]\n",
				&x, PAGETBL_IDX(local_offset), MAX_VADDR_TABLE_SIZE, 
				local_offset < MAX_VADDR_TABLE_SIZE ? virt_tbl[PAGETBL_IDX(local_offset)] :
				0x0UL);
		dump_stack();
	}
#endif
	return virt_tbl[PAGETBL_IDX(local_offset)] | (x & PAGETBL_ADDR_MASK);
}
EXPORT_SYMBOL(__sdp_dma_phys_to_virt);

phys_addr_t __sdp_virt_to_phys_ext(unsigned long x)
{
	struct memblock_type *mt = &memblock.puremem;
	unsigned long local_offset = x - PAGE_OFFSET;
	phys_addr_t total_size = mt->total_size;
	phys_addr_t base = 0;
	int i;

	if (unlikely(local_offset >= total_size)) {
		struct memblock_type *dma_mt = &memblock.dmamem;
		local_offset -= total_size;
		for (i = 0; i < dma_mt->cnt; i++) {
			base = dma_mt->regions[i].base;
			if (local_offset < dma_mt->regions[i].size)
				break;
			local_offset -= dma_mt->regions[i].size;
		}
	} else {
		for (i = 0; i < mt->cnt; i++) {
			base = mt->regions[i].base;
			if (likely(local_offset < mt->regions[i].size))
				break;
			local_offset -= mt->regions[i].size;
		}
	}
	return base + local_offset;
}

unsigned long __sdp_phys_to_virt_ext(phys_addr_t x)
{
	struct memblock_type *mt = &memblock.puremem;
	struct memblock_type *dma_mt;
	unsigned long channel_offset = 0;
	unsigned long local_offset = (unsigned long)(x - PHYS_OFFSET);
	int i;

	for (i = 0; i < mt->cnt; i++) {
		local_offset = (unsigned long)(x - mt->regions[i].base);
		if (likely(local_offset < mt->regions[i].size)) {
			if(mt->regions[i].base > x ||
			mt->regions[i].base + mt->regions[i].size <= x)
				goto dma_scope;

			goto out;
		}
		channel_offset += (unsigned long)mt->regions[i].size;
	}
dma_scope:
	dma_mt = &memblock.dmamem;
	channel_offset = (unsigned long)mt->total_size;
	for (i = 0; i < dma_mt->cnt; i++) {
		local_offset = (unsigned long)(x - dma_mt->regions[i].base);
		if (likely(local_offset) < dma_mt->regions[i].size)
			break;
		channel_offset += dma_mt->regions[i].size;
	}
out:
	return PAGE_OFFSET + channel_offset + local_offset;

}
#endif /* CONFIG_SPARSE_LOWMEM_MAP */


struct sdp_bootprogress_t {
	/* mark boot progress to Micom SRAM */
	int (*mark_fn)(u32 value);
};
/* this function can be used after map_io(). */
static struct sdp_bootprogress_t bootprog = {NULL,};

int register_boot_progress_fn(int (*mark_fn)(u32)) {
	bootprog.mark_fn = mark_fn;
	return 0;
}
EXPORT_SYMBOL(register_boot_progress_fn);

int mark_boot_progress(u32 value) {
	if(bootprog.mark_fn == NULL) {
		return -ENODEV;
	}

	bootprog.mark_fn(value);

	return 0;
}
EXPORT_SYMBOL(mark_boot_progress);


#endif

