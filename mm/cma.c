/*
 * Contiguous Memory Allocator
 *
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Copyright IBM Corporation, 2013
 * Copyright LG Electronics Inc., 2014
 * Written by:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *	Michal Nazarewicz <mina86@mina86.com>
 *	Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *	Joonsoo Kim <iamjoonsoo.kim@lge.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 */

#define pr_fmt(fmt) "cma: " fmt

#ifdef CONFIG_CMA_DEBUG
#ifndef DEBUG
#  define DEBUG
#endif
#endif
#define CREATE_TRACE_POINTS

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/cma.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <trace/events/cma.h>
#include <linux/ratelimit.h>

#include "cma.h"
#include "internal.h"

#ifdef CONFIG_CMA_DEBUG
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#endif

#ifdef CONFIG_STACKTRACE
extern void seq_print_stack_trace(struct seq_file *m,
				struct stack_trace *trace, int spaces);
#endif

struct cma cma_areas[MAX_CMA_AREAS];
unsigned cma_area_count;
static DEFINE_MUTEX(cma_mutex);

#ifdef CONFIG_CMA_DEBUG

/**
 * cma_buffer_list_add() - add a new entry to a list of allocated buffers
 * @dev:   Pointer to the device structure.
 * @pfn:   Base PFN of the allocated buffer.
 * @count: Number of allocated pages.
 *
 * This function adds a new entry to the list of allocated contiguous memory
 * buffers in a CMA area. It uses the CMA area specificated by the device
 * if available or the default global one otherwise.
 */
static int cma_buffer_list_add(struct cma *cma, unsigned long pfn,
		int count, int64_t latency)
{
	struct cma_buffer *cmabuf = NULL;
	struct stack_trace trace;

	cmabuf = kmalloc(sizeof(struct cma_buffer), GFP_KERNEL);
	if (!cmabuf)
		return -ENOMEM;

	trace.nr_entries = 0;
	trace.max_entries = ARRAY_SIZE(cmabuf->trace_entries);
	trace.entries = &cmabuf->trace_entries[0];
	trace.skip = 0;
	save_stack_trace(&trace);

	cmabuf->pfn = pfn;
	cmabuf->count = count;
	cmabuf->pid = task_pid_nr(current);
	cmabuf->nr_entries = trace.nr_entries;
	get_task_comm(cmabuf->comm, current);
	cmabuf->latency = latency;
	cmabuf->tick = jiffies;

	mutex_lock(&cma->list_lock);
	list_add_tail(&cmabuf->list, &cma->buffers_list);
	mutex_unlock(&cma->list_lock);

	return 0;
}

/**
 * cma_buffer_list_del() - delete an entry from a list of allocated buffers
 * @dev:   Pointer to device for which the pages were allocated.
 * @pfn:   Base PFN of the released buffer.
 *
 * This function deletes a list entry added by cma_buffer_list_add().
 */
static void cma_buffer_list_del(struct cma *cma, unsigned long pfn)
{
	struct cma_buffer *cmabuf;

	mutex_lock(&cma->list_lock);

	list_for_each_entry(cmabuf, &cma->buffers_list, list)
		if (cmabuf->pfn == pfn) {
			list_del(&cmabuf->list);
			kfree(cmabuf);
			goto out;
		}

	pr_err("%s(pfn %lu): couldn't find buffers list entry\n",
			__func__, pfn);

out:
	mutex_unlock(&cma->list_lock);
}
#endif /* CONFIG_CMA_DEBUG */

phys_addr_t cma_get_base(const struct cma *cma)
{
	return PFN_PHYS(cma->base_pfn);
}

unsigned long cma_get_size(const struct cma *cma)
{
	return cma->count << PAGE_SHIFT;
}

unsigned long cma_get_free(void)
{
	struct zone *zone;
	unsigned long freecma = 0;

	for_each_populated_zone(zone) {
		if (!is_zone_cma(zone))
			continue;

		freecma += zone_page_state(zone, NR_FREE_PAGES);
	}

	return freecma;
}

static unsigned long cma_bitmap_aligned_mask(const struct cma *cma,
					     int align_order)
{
	if (align_order <= cma->order_per_bit)
		return 0;
	return (1UL << (align_order - cma->order_per_bit)) - 1;
}

/*
 * Find a PFN aligned to the specified order and return an offset represented in
 * order_per_bits.
 */
static unsigned long cma_bitmap_aligned_offset(const struct cma *cma,
					       int align_order)
{
	if (align_order <= cma->order_per_bit)
		return 0;

	return (ALIGN(cma->base_pfn, (1UL << align_order))
		- cma->base_pfn) >> cma->order_per_bit;
}

static unsigned long cma_bitmap_pages_to_bits(const struct cma *cma,
					      unsigned long pages)
{
	return ALIGN(pages, 1UL << cma->order_per_bit) >> cma->order_per_bit;
}

static void cma_clear_bitmap(struct cma *cma, unsigned long pfn,
			     unsigned int count)
{
	unsigned long bitmap_no, bitmap_count;

	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	mutex_lock(&cma->lock);
	bitmap_clear(cma->bitmap, bitmap_no, bitmap_count);
	mutex_unlock(&cma->lock);
}

bool is_zone_cma_page(const struct page *page)
{
	struct cma *cma;
	int i;
	unsigned long pfn = page_to_pfn(page);

	for (i = 0; i < cma_area_count; i++) {
		cma = &cma_areas[i];

		if (pfn < cma->base_pfn)
			continue;

		if (pfn < cma->base_pfn + cma->count)
			return true;
	}

	return false;
}

static int __init cma_activate_area(struct cma *cma)
{
	int bitmap_size = BITS_TO_LONGS(cma_bitmap_maxno(cma)) * sizeof(long);
	unsigned long base_pfn = cma->base_pfn, pfn = base_pfn;
	unsigned i = cma->count >> pageblock_order;
	struct zone *zone;

	cma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);

	if (!cma->bitmap)
		return -ENOMEM;

	WARN_ON_ONCE(!pfn_valid(pfn));

#ifdef CONFIG_GCMA
	if (cma->gcma == IS_GCMA) {
			if(gcma_init(cma->base_pfn, cma->count, &cma->gcma) != 0) {
				goto err;
			}
	} else
#endif
	{
		zone = page_zone(pfn_to_page(pfn));

		do {
			unsigned j;

			base_pfn = pfn;
			for (j = pageblock_nr_pages; j; --j, pfn++) {
				WARN_ON_ONCE(!pfn_valid(pfn));
				/*
				 * In init_cma_reserved_pageblock(), present_pages is
				 * adjusted with assumption that all pages come from
				 * a single zone. It could be fixed but not yet done.
				 */
				if (page_zone(pfn_to_page(pfn)) != zone)
					goto err;
			}
			init_cma_reserved_pageblock(pfn_to_page(base_pfn));
		} while (--i);
	}

	mutex_init(&cma->lock);

#ifdef CONFIG_CMA_DEBUGFS
	INIT_HLIST_HEAD(&cma->mem_head);
	spin_lock_init(&cma->mem_head_lock);
#endif
#ifdef CONFIG_CMA_DEBUG
	INIT_LIST_HEAD(&cma->buffers_list);
	mutex_init(&cma->list_lock);
#endif
	return 0;

err:
	kfree(cma->bitmap);
	cma->count = 0;
	return -EINVAL;
}

static int __init cma_init_reserved_areas(void)
{
	int i;
	struct zone *zone;
	pg_data_t *pgdat;

	if (!cma_area_count)
		return 0;
	for_each_online_pgdat(pgdat) {
		unsigned long start_pfn = UINT_MAX, end_pfn = 0;

		for (i = 0; i < cma_area_count; i++) {
			if (pfn_to_nid(cma_areas[i].base_pfn) !=
				pgdat->node_id)
				continue;

			start_pfn = min(start_pfn, cma_areas[i].base_pfn);
			end_pfn = max(end_pfn, cma_areas[i].base_pfn +
						cma_areas[i].count);
		}

		if (!end_pfn)
			continue;

		zone = &pgdat->node_zones[ZONE_CMA];

		/* ZONE_CMA doesn't need to exceed CMA region */
		zone->zone_start_pfn = start_pfn;
		zone->spanned_pages = end_pfn - start_pfn;
	}
	for (i = 0; i < cma_area_count; i++) {
		int ret = cma_activate_area(&cma_areas[i]);

		if (ret)
			return ret;
	}

	/*
	 * Reserved pages for ZONE_CMA are now activated and this would change
	 * ZONE_CMA's managed page counter and other zone's present counter.
	 * We need to re-calculate various zone information that depends on
	 * this initialization.
	 */
	build_all_zonelists(NULL, NULL);
	for_each_populated_zone(zone) {
		zone_pcp_update(zone);
	}

	/*
	 * We need to re-init per zone wmark by calling
	 * init_per_zone_wmark_min() but doesn't call here because it is
	 * registered on core_initcall and it will be called later than us.
	 */
	for_each_populated_zone(zone) {
		if (!is_zone_cma(zone))
			continue;

		setup_zone_pageset(zone);
	}



	return 0;
}
pure_initcall(cma_init_reserved_areas);

/**
 * cma_init_reserved_mem() - create custom contiguous area from reserved memory
 * @base: Base address of the reserved area
 * @size: Size of the reserved area (in bytes),
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @res_cma: Pointer to store the created cma region.
 *
 * This function creates custom contiguous area from already reserved memory.
 */
int __init cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
				 unsigned int order_per_bit,
				 struct cma **res_cma)
{
	struct cma *cma;
	phys_addr_t alignment;

	/* Sanity checks */
	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

	if (!size || !memblock_is_region_reserved(base, size))
		return -EINVAL;

	/* ensure minimal alignment requied by mm core */
	alignment = PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);

	/* alignment should be aligned with order_per_bit */
	if (!IS_ALIGNED(alignment >> PAGE_SHIFT, 1 << order_per_bit))
		return -EINVAL;

	if (ALIGN(base, alignment) != base || ALIGN(size, alignment) != size)
		return -EINVAL;

	/*
	 * Each reserved area must be initialised later, when more kernel
	 * subsystems (like slab allocator) are available.
	 */
	cma = &cma_areas[cma_area_count];
	cma->base_pfn = PFN_DOWN(base);
	cma->count = size >> PAGE_SHIFT;
	cma->order_per_bit = order_per_bit;
	INIT_LIST_HEAD(&cma->map_dev_list);
	*res_cma = cma;
	cma_area_count++;
	totalcma_pages += (size / PAGE_SIZE);

	return 0;
}

/**
 * cma_declare_contiguous() - reserve custom contiguous area
 * @base: Base address of the reserved area optional, use 0 for any
 * @size: Size of the reserved area (in bytes),
 * @limit: End address of the reserved memory (optional, 0 for any).
 * @alignment: Alignment for the CMA area, should be power of 2 or zero
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @fixed: hint about where to place the reserved area
 * @res_cma: Pointer to store the created cma region.
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory. This function allows to create custom reserved areas.
 *
 * If @fixed is true, reserve contiguous area at exactly @base.  If false,
 * reserve in range from @base to @limit.
 */
int __init cma_declare_contiguous(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, struct cma **res_cma)
{
	phys_addr_t memblock_end = memblock_end_of_DRAM();
	phys_addr_t highmem_start;
	int ret = 0;

#ifdef CONFIG_X86
	/*
	 * high_memory isn't direct mapped memory so retrieving its physical
	 * address isn't appropriate.  But it would be useful to check the
	 * physical address of the highmem boundary so it's justfiable to get
	 * the physical address from it.  On x86 there is a validation check for
	 * this case, so the following workaround is needed to avoid it.
	 */
	highmem_start = __pa_nodebug(high_memory);
#else
	highmem_start = __pa(high_memory);
#endif
	pr_debug("%s(size %pa, base %pa, limit %pa alignment %pa)\n",
		__func__, &size, &base, &limit, &alignment);

	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

	if (!size)
		return -EINVAL;

	if (alignment && !is_power_of_2(alignment))
		return -EINVAL;

	/*
	 * Sanitise input arguments.
	 * Pages both ends in CMA area could be merged into adjacent unmovable
	 * migratetype page by page allocator's buddy algorithm. In the case,
	 * you couldn't get a contiguous memory, which is not what we want.
	 */
	alignment = max(alignment,
		(phys_addr_t)PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order));
	base = ALIGN(base, alignment);
	size = ALIGN(size, alignment);
	limit &= ~(alignment - 1);

	if (!base)
		fixed = false;

	/* size should be aligned with order_per_bit */
	if (!IS_ALIGNED(size >> PAGE_SHIFT, 1 << order_per_bit))
		return -EINVAL;

	/*
	 * If allocating at a fixed base the request region must not cross the
	 * low/high memory boundary.
	 */
	if (fixed && base < highmem_start && base + size > highmem_start) {
		ret = -EINVAL;
		pr_err("Region at %pa defined on low/high memory boundary (%pa)\n",
			&base, &highmem_start);
		goto err;
	}

	/*
	 * If the limit is unspecified or above the memblock end, its effective
	 * value will be the memblock end. Set it explicitly to simplify further
	 * checks.
	 */
	if (limit == 0 || limit > memblock_end)
		limit = memblock_end;

	/* Reserve memory */
	if (fixed) {
		if (memblock_is_region_reserved(base, size) ||
		    memblock_reserve(base, size) < 0) {
			ret = -EBUSY;
			goto err;
		}
	} else {
		phys_addr_t addr = 0;

		/*
		 * All pages in the reserved area must come from the same zone.
		 * If the requested region crosses the low/high memory boundary,
		 * try allocating from high memory first and fall back to low
		 * memory in case of failure.
		 */
#ifdef CONFIG_CMA_SUPPORT_HIGHMEM
		if (base < highmem_start && limit > highmem_start) {
			addr = memblock_alloc_range(size, alignment,
						    highmem_start, limit);
			limit = highmem_start;
		}
#else
		limit = highmem_start;
#endif
		if (!addr) {
			addr = memblock_alloc_range(size, alignment, base,
						    limit);
			if (!addr) {
				ret = -ENOMEM;
				goto err;
			}
		}

		/*
		 * kmemleak scans/reads tracked objects for pointers to other
		 * objects but this address isn't mapped and accessible
		 */
		kmemleak_ignore(phys_to_virt(addr));
		base = addr;
	}

	ret = cma_init_reserved_mem(base, size, order_per_bit, res_cma);
	if (ret)
		goto err;

	pr_info("Reserved %ld MiB at %pa\n", (unsigned long)size / SZ_1M,
		&base);


#ifdef CONFIG_GCMA_DEFAULT
		(*res_cma)->gcma = IS_GCMA;
#endif

	return 0;

err:
	pr_err("Failed to reserve %ld MiB\n", (unsigned long)size / SZ_1M);
	return ret;
}

void cma_add_device_on_region(struct device *dev, struct cma *cma)
{
	struct cma_dev_map *dev_map = kmalloc(sizeof(*dev_map), GFP_KERNEL);

	if (dev_map == NULL)
		return;
	INIT_LIST_HEAD(&dev_map->list); 
	dev_map->dev = dev;
	VM_BUG_ON(in_atomic());
	mutex_lock(&cma->lock);
	list_add_tail(&dev_map->list, &cma->map_dev_list);
	mutex_unlock(&cma->lock);
}

void cma_remove_device_on_region(struct device *dev, struct cma *cma)
{
	struct cma_dev_map *dev_map = NULL;
	VM_BUG_ON(in_atomic());
	mutex_lock(&cma->lock);
	list_for_each_entry(dev_map, &cma->map_dev_list, list) {
		if (dev_map->dev == dev) {
			list_del(&dev_map->list);
			kfree(dev_map);
			break;
		}
	}
	mutex_unlock(&cma->lock);
}

#ifdef CONFIG_CMA_DEBUG
static void cma_region_alloc_info(struct cma *cma) {
	struct cma_buffer *cmabuf;
	struct stack_trace trace;
	unsigned long usec, sec;
	uint64_t latency;
	int ret = 0;
	char kbuf[512];

	mutex_lock(&cma->list_lock);

	list_for_each_entry(cmabuf, &cma->buffers_list, list) {
		sec = jiffies_to_usecs(cmabuf->tick);
		usec = do_div(sec, USEC_PER_SEC);
		latency = div_s64(cmabuf->latency, NSEC_PER_USEC);

		pr_err("0x%llx - 0x%llx (%lu kB), "
				"allocated by pid %u (%s), alloc time: %lu.%lu latency %llu us\n",
				(uint64_t)PFN_PHYS(cmabuf->pfn),
				(uint64_t)PFN_PHYS(cmabuf->pfn + cmabuf->count),
				(cmabuf->count * PAGE_SIZE) >> 10,
				cmabuf->pid, cmabuf->comm, sec, usec, latency);

		trace.nr_entries = cmabuf->nr_entries;
		trace.entries = &cmabuf->trace_entries[0];
		ret = snprint_stack_trace(kbuf, 512, &trace, 0);
		pr_err("%s \n", kbuf);
	}

	mutex_unlock(&cma->list_lock);
}
#else
static inline void cma_region_alloc_info(struct cma *cma) {}
#endif

/**
 * cma_alloc() - allocate pages from contiguous area
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 *
 * This function allocates part of contiguous memory on specific
 * contiguous memory area.
 */

#define K(x) ((x) << (PAGE_SHIFT - 10))

static DEFINE_RATELIMIT_STATE(cma_debug_ratelimit, HZ / 2, 30);
struct page *cma_alloc(struct cma *cma, unsigned int count, unsigned int align)
{
	unsigned long mask, offset, pfn, start = 0;
	unsigned long bitmap_maxno, bitmap_no, bitmap_count;
	struct page *page = NULL;
	int ret;
#ifdef CONFIG_CMA_DEBUG
	struct timespec ts1, ts2;
	int64_t latency;

	getnstimeofday(&ts1);
#endif

	if (!cma || !cma->count)
		return NULL;

	if (__ratelimit(&cma_debug_ratelimit)) {
		pr_debug("%s(cma %p, count %d, align %d)\n", __func__, (void *)cma,
			 count, align);
	}

	if (!count)
		return NULL;

	mask = cma_bitmap_aligned_mask(cma, align);
	offset = cma_bitmap_aligned_offset(cma, align);
	bitmap_maxno = cma_bitmap_maxno(cma);
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	for (;;) {
		mutex_lock(&cma->lock);
		bitmap_no = bitmap_find_next_zero_area_off(cma->bitmap,
				bitmap_maxno, start, bitmap_count, mask,
				offset);
		if (bitmap_no >= bitmap_maxno) {
			static DEFINE_RATELIMIT_STATE(cma_leak_ratelimit, HZ / 2, 2);
			mutex_unlock(&cma->lock);
			if (!start && __ratelimit(&cma_leak_ratelimit)) {
				unsigned long used = cma_get_used_pages(cma);
				unsigned long max_chunk = cma_get_maxchunk_pages(cma);
				pr_err("[cma total:%lu kB / used:%lu kB max_chunk :%lu kB]"
						" No more space to alloc for size:%u kB",
						K(cma->count), K(used), K(max_chunk), K(count));
				cma_region_alloc_info(cma);
			}
			break;
		}
		bitmap_set(cma->bitmap, bitmap_no, bitmap_count);
		/*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */
		mutex_unlock(&cma->lock);

		pfn = cma->base_pfn + (bitmap_no << cma->order_per_bit);
		/* 
		 * The reason that take cma global lock is to protect allocation 
		 * memory on same pageblock between tasks But, CMA memory map 
		 * between region couldn't be set on same pageblock due to PMD alignment
		 * That's why region lock covers instead of global cma lock.
		 * Region lock doesn't make bottleneck of cma allocation between different region.
 		 */
		mutex_lock(&cma->lock);

#ifdef CONFIG_GCMA
		if (cma->gcma)
			ret = gcma_alloc_contig(cma->gcma, pfn, count);
		else
#endif
			ret = alloc_contig_range(pfn, pfn + count);

		mutex_unlock(&cma->lock);
		if (ret == 0) {
			page = pfn_to_page(pfn);
			break;
		}

		cma_clear_bitmap(cma, pfn, count);
		if (ret != -EBUSY)
			break;

		if (__ratelimit(&cma_debug_ratelimit)) {
			pr_debug("%s(): memory range at %p(%lx) is busy, retrying\n",
				 __func__, pfn_to_page(pfn), pfn);
		}
		/* try again with a bit different memory target */
		start = bitmap_no + mask + 1;
	}
#ifdef CONFIG_CMA_DEBUG
	getnstimeofday(&ts2);
	latency = timespec_to_ns(&ts2) - timespec_to_ns(&ts1);

	if (page) {
		ret = cma_buffer_list_add(cma, pfn, count, latency);
		if (ret) {
			pr_warn("%s(): cma_buffer_list_add() returned %d\n",
					__func__, ret);
			page = NULL;
		}
	}
#endif
	trace_cma_alloc(page ? pfn : -1UL, page, count, align);

	if (page) {
		pr_err("[%s(%d)] %s(): returned %p range %lx - %lx \n",
			current->comm, current->pid, __func__, page, pfn, pfn + count);
	}

	return page;
}

/**
 * cma_release() - release allocated pages
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @pages: Allocated pages.
 * @count: Number of allocated pages.
 *
 * This function releases memory allocated by alloc_cma().
 * It returns false when provided pages do not belong to contiguous area and
 * true otherwise.
 */
bool cma_release(struct cma *cma, const struct page *pages, unsigned int count)
{
	unsigned long pfn;

	if (!cma || !pages)
		return false;

	pr_debug("%s(page %p)\n", __func__, (void *)pages);

	pfn = page_to_pfn(pages);

	if (pfn < cma->base_pfn || pfn >= cma->base_pfn + cma->count) {
		pr_err("%s [Error](%s - %d) free invalid memory range"
		"base_pfn:%lx pfn:%lx - %lx\n", __func__, current->comm,
		 current->pid, cma->base_pfn, pfn, pfn + count);
		return false;
	}

	VM_BUG_ON(pfn + count > cma->base_pfn + cma->count);

#ifdef CONFIG_GCMA
	if (cma->gcma)
		gcma_free_contig(cma->gcma, pfn, count);
	else
#endif
		free_contig_range(pfn, count);

	cma_clear_bitmap(cma, pfn, count);
	trace_cma_release(pfn, pages, count);
#ifdef CONFIG_CMA_DEBUG
	cma_buffer_list_del(cma, pfn);
#endif
	return true;
}

unsigned long cma_get_device_used_pages(void)
{
    unsigned long ret = 0;
    int i = 0;

    if (!cma_area_count)
        return 0;

    for(i=0; i < cma_area_count;i++) {
        ret += cma_get_used_pages(&cma_areas[i]);
    }

    return ret;
}

/**
 * cma_get_used_pages() - get number of used pages
 * @cma: region to check
 *
 * This function returns number of used pages in cma region.
 */
unsigned long cma_get_used_pages(struct cma *cma)
{
	unsigned long ret = 0;

	if (!cma)
		return 0;

	mutex_lock(&cma->lock);
	ret = bitmap_weight(cma->bitmap,(int)cma_bitmap_maxno(cma));
	mutex_unlock(&cma->lock);

	return ret << cma->order_per_bit;
}

/**
 * cma_get_maxchunk_pages() - get the biggest free chunk
 * @cma: region to check
 *
 * This function returns number of pages inside biggest chunk in cma region.
 */
unsigned long cma_get_maxchunk_pages(struct cma *cma)
{
	unsigned long maxchunk = 0;
	unsigned long start, end = 0;
	unsigned long bitmap_maxno = cma_bitmap_maxno(cma);

	if (!cma)
		return 0;

	mutex_lock(&cma->lock);
	for (;;) {
		start = find_next_zero_bit(cma->bitmap, bitmap_maxno, end);
		if ((start << cma->order_per_bit) >= cma->count)
			break;
		end = find_next_bit(cma->bitmap, bitmap_maxno, start);
		maxchunk = max(((end-start) << cma->order_per_bit), maxchunk);
	}
	mutex_unlock(&cma->lock);

	return maxchunk;
}
