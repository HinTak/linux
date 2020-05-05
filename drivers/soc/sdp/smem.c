/*
 * SDP memory allocator(smem) ion-like allocator, reinvented wheel.
 * support no-kernel-mapping carveout memory + per-channel CMA allocations.
 *
 * Copyright (C) 2017 Samsung Electronics
 *
 * Ikjoon Jang <ij.jang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 */

#define SMEM_VERSION_STR	"20170704(add version string)"

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/cma.h>
#include <linux/dma-contiguous.h>
#include <asm/dma-contiguous.h>

/* XXX */
#include <asm/cacheflush.h>

#include "smem_priv.h"

static struct smem *smem_regions[SMEM_MAX_REGIONS_SYSTEM];

/* create a link between device and handle */
static void heap_attach_handle_device(struct device *dev, struct smem_handle *handle)
{
	BUG_ON(!dev || handle->dev);
	get_device(dev);
	handle->dev = dev;
}
static void heap_detach_handle_device(struct smem_handle *handle)
{
	BUG_ON(!handle->dev);
	put_device(handle->dev);
	handle->dev = NULL;
}

struct smem_handle* heap_create_handle(struct device *dev, struct smem_heap *heap,
		dma_addr_t addr, unsigned long buf_len,
		void *heap_priv, void *handle_priv)
{
	struct smem_handle *handle = NULL;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return NULL;

	kref_init(&handle->ref);
	handle->dma_addr = addr;
	handle->length = buf_len;
	handle->heap = heap;
	handle->heap_priv = heap_priv;
	handle->priv = handle_priv;
	mutex_init(&handle->lock);
	list_add_tail_rcu(&handle->link, &heap->handles);

	if (dev)
		heap_attach_handle_device(dev, handle);

	return handle;
}

int handle_get(struct smem_handle *handle)
{
	kref_get(&handle->ref);
	return 0;
}

static void _handle_destroy(struct kref *kref)
{
	struct smem_handle *handle = container_of(kref, struct smem_handle, ref);
	struct device *dev = handle->dev;

	handle->heap->free(handle);

	if (dev)
		heap_detach_handle_device(handle);

	list_del_rcu(&handle->link);

	kfree(handle);
}

int handle_put(struct smem_handle *handle)
{
	kref_put(&handle->ref, _handle_destroy);
	return 0;
}
static struct smem_handle* smem_alloc_on_heap(struct device *dev, struct smem_heap *heap,
				size_t size, size_t align, smem_flags_t flags,
				void *data)
{
	return heap->allocate(heap, dev, size, align, flags, data);
}

static struct smem_handle* smem_alloc_on_smem(struct device *dev,
				size_t size, size_t align,
				smem_flags_t flags, struct smem *smem,
				void *data)
{
	int heap_id;
	struct smem_handle *handle = NULL;
	struct smem_heap *heap = NULL;
	smem_flags_t want = SMEMFLAGS_HEAPTYPE(flags);
	int i;

	/* FIXME: sanity check of smem */
	if (!smem)
		return NULL;

	for (i = 0; i < SMEM_MAX_HEAPS; i++) {
		heap = smem->heaps[i];
		if (!heap)
			continue;
		if (want && !(heap->heapmask & want))
			continue;
		if (!heap->precheck(heap, size, align, flags)) {
			continue;
		}
		handle = smem_alloc_on_heap(dev, heap, size, align, flags, data);
		if (handle)
			return handle;
	}
	return NULL;
}

const char* smem_test_name(void *priv)
{
	static const char def_name[16] = {"unnamed"};
	int i;
	char *p;

	if (virt_addr_valid(priv))
		for (p = priv, i = 0; i < 16; i++)
			if (!p[i])
				return priv;
	return def_name;
}

#ifdef SMEM_DEBUG
void dump_heap_handles(struct smem_heap *heap)
{
	struct smem_handle *handle;
	heap_err(heap, "dump handles (heap region: 0x%lx@%pad)\n", heap->size, &heap->base);
	list_for_each_entry_rcu(handle, &heap->handles, link) {
		phys_addr_t addr0, addr1;
		unsigned long len = smem_size(handle);
		addr0 = smem_phys(handle);
		addr1 = addr0 + len;
		heap_err(heap, "%pa -- %pa : %08lx(%3ldMb) %p %s\n", &addr0, &addr1,
			len, (len + 0x80000) >> 20, handle, smem_test_name(handle->priv));
	}
}
static void dump_smem(struct smem *smem)
{
	int i;
	struct smem_heap *heap;
	struct smem_handle *handle;

	pr_err("dump smem %p\n", smem);

	for (i = 0; i < SMEM_MAX_HEAPS; i++) {
		heap = smem->heaps[i];
		if (!heap)
			continue;
		if (heap->dump)
			heap->dump(heap);
		else
			dump_heap_handles(heap);
	}
}
#else
static inline void dump_smem(struct smem *smem) {}
void dump_heap_handles(struct smem_heap *heap) {}
#endif

struct smem_handle* smem_alloc_on_region_mask_data(struct device *dev,
			size_t size, size_t align,
			smem_flags_t flags, u32 region_mask,
			void *data)
{
	int i;
	struct smem_handle *handle = NULL;

	struct smem_device_data *smem_data = dev_get_smem_data(dev);
	BUG_ON(!smem_data);

	flags |= smem_data->alloc_flags;

	// TODO: pick _best_ smem region to allocate on
	for (i = 0; i < SMEM_MAX_REGIONS_DEVICE; i++) {
		if ((1 << i) & region_mask) {
			struct smem *smem = dev_get_smem(dev, i);
			if (!smem) {
				dev_warn(dev, "invalid smem region mask 0x%x\n", region_mask);
				continue;
			}
			handle = smem_alloc_on_smem(dev, size, align, flags, smem, data);
			if (handle) {
				dev_dbg(dev, "smem_alloc handle %p (0x%lx@%pad)",
					handle, handle->length, &handle->dma_addr);
				break;
			}
		}
	}
	if (!handle) {
		/* XXX: debug */
		dev_err(dev, "smem_alloc failed (0x%x) on  mask 0x%x",
				size, region_mask);
		for (i = 0; i < SMEM_MAX_REGIONS_DEVICE; i++) {
			if ((1 << i) & region_mask) {
				struct smem *smem = dev_get_smem(dev, i);
				if (smem)
					dump_smem(smem);
			}
		}
	}
	return handle;
}
EXPORT_SYMBOL(smem_alloc_on_region_mask_data);

int smem_free(struct smem_handle *handle)
{
	BUG_ON(!handle || !handle->heap || !handle->heap->smem);

	smem_dbg(handle->heap->smem, "%s free handle %p (0x%lx@%pad)\n",
			dev_name(handle->dev), handle,
			handle->length, &handle->dma_addr);

	handle_put(handle);
	return 0;
}
EXPORT_SYMBOL(smem_free);

/* TODO: use memremap() or go custom way */
static void* smem_remap(phys_addr_t addr, size_t len, pgprot_t pgprot)
{
	void *ret;

	if (pgprot == PAGE_KERNEL)
		ret = ioremap_cache(addr, len);
	else if (pgprot == pgprot_writecombine(PAGE_KERNEL))
		ret = ioremap_wc(addr, len);
	else
		ret = ioremap(addr, len);
	return ret;
}

static void* smem_vmap(phys_addr_t paddr, unsigned long len, pgprot_t pgprot)
{
	struct page **pages;
	unsigned long i;
	const unsigned long pfn0 = __phys_to_pfn(paddr);
	const unsigned long pfn1 = __phys_to_pfn(paddr + len - 1);
	const unsigned long nr = pfn1 - pfn0 + 1;
	void *ret;

	pages = kmalloc(sizeof(struct page *) * nr, GFP_KERNEL);
	if (!pages)
		return NULL;

	for (i = 0; i < nr; i++)
		pages[i] = pfn_to_page(pfn0 + i);

	ret = vmap(pages, nr, VM_MAP, pgprot);
	kfree(pages);
	return ret;
}

#define SMEM_PGPROT_NONCONSISTENT	PAGE_KERNEL
#define SMEM_PGPROT_WC			pgprot_writecombine(PAGE_KERNEL)
#define SMEM_PGPROT_COHERENT		pgprot_dmacoherent(PAGE_KERNEL)

static pgprot_t smem_pgprot(struct dma_attrs *attrs)
{
	pgprot_t pgprot;

	if (dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs))
		pgprot = SMEM_PGPROT_NONCONSISTENT;
	else if (dma_get_attr(DMA_ATTR_WRITE_COMBINE, attrs))
		pgprot = SMEM_PGPROT_WC;
	else
		pgprot = SMEM_PGPROT_COHERENT;

	return pgprot;
}

void* smem_remap_buffer(phys_addr_t paddr, unsigned long len, struct dma_attrs *attrs)
{
	unsigned long pfn0 = __phys_to_pfn(paddr);
	unsigned long pfn1 = __phys_to_pfn(paddr + len - 1);
	unsigned long pfn;
	void *ret;
	pgprot_t pgprot = smem_pgprot(attrs);

	bool paged = pfn_valid(pfn0);

	if (WARN_ON(paged && !memblock_is_region_memory(paddr, len)))
		return NULL;

	if (!paged) {
		return smem_remap(paddr, len, pgprot);
	} else {
		/* map pages */
		return smem_vmap(paddr, len, pgprot);
	}
}
EXPORT_SYMBOL(smem_remap_buffer);

void smem_unmap_buffer(void *ptr)
{
	struct vm_struct *vm = find_vm_area((void*)(PAGE_MASK & (unsigned long)ptr));
	if (vm && (vm->flags & VM_MAP) && !(vm->flags & VM_IOREMAP))
		vunmap(ptr);
	else
		iounmap(ptr);
}
EXPORT_SYMBOL(smem_unmap_buffer);

static void* _smem_map_kernel_partial(struct smem_handle *handle, struct dma_attrs *attrs,
				unsigned long offset, unsigned long len)
{
	struct smem_heap *heap = handle->heap;
	void *ret = NULL;
	pgprot_t pgprot = smem_pgprot(attrs);
	phys_addr_t addr = dma_to_phys(heap->smem->dev, handle->dma_addr + offset);

	if (offset >= handle->length || (offset + len) > handle->length)
		goto err_map_kernel_partial;

	if (heap->heapmask & SMEM_HEAP_FLAGS_NOMAP) {
		ret = smem_remap(addr, len, pgprot);
	} else {
		/* map pages */
		ret = smem_vmap(addr, len, pgprot);
	}

	smem_dbg(handle->heap->smem, "%s %p map kernel %p --> (0x%lx@%pa)",
			dev_name(handle->dev), handle, ret, len, &addr);

err_map_kernel_partial:
	return ret;
}

void* smem_map_kernel_partial(struct smem_handle *handle, struct dma_attrs *attrs,
				unsigned long offset, unsigned long len)
{
	/* to avoid aliased mappings, */
	if (dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs)) {
		smem_err(handle->heap->smem, "partial mapping cannot have non-consistent attributes.\n");
		return NULL;
	}
	return _smem_map_kernel_partial(handle, attrs, offset, len);
}
EXPORT_SYMBOL(smem_map_kernel_partial);

void* smem_map_kernel_attrs(struct smem_handle *handle, struct dma_attrs *attrs)
{
	void *ret = NULL;
	pgprot_t pgprot;

	mutex_lock(&handle->lock);

	pgprot = smem_pgprot(attrs);

	if (handle->kmap_cnt > 0) {
		BUG_ON(!handle->virtual);
		if (pgprot == handle->kmap_prot)
			ret = handle->virtual;
	} else {
		ret = _smem_map_kernel_partial(handle, attrs, 0, handle->length);
	}

	if (ret) {
		handle->kmap_cnt++;
		handle_get(handle);

		handle->kmap_prot = pgprot;
		handle->virtual = ret;
	}

	mutex_unlock(&handle->lock);
	return ret;
}
EXPORT_SYMBOL(smem_map_kernel_attrs);

static void _smem_unmap_kernel_partial(struct smem_handle *handle, void *ptr)
{
	struct smem_heap *heap = handle->heap;

	if (heap->heapmask & SMEM_HEAP_FLAGS_NOMAP) {
		iounmap(ptr);
	} else {
		vunmap(ptr);
	}
}
void smem_unmap_kernel_partial(struct smem_handle *handle, void *ptr)
{
	smem_dbg(handle->heap->smem, "%s %p unmap kernel partial (%p)\n",
			dev_name(handle->dev), handle, ptr);
	return _smem_unmap_kernel_partial(handle, ptr);
}
EXPORT_SYMBOL(smem_unmap_kernel_partial);

void smem_unmap_kernel(struct smem_handle *handle)
{
	struct smem_heap *heap = handle->heap;

	mutex_lock(&handle->lock);

	BUG_ON(handle->kmap_cnt < 1);
	BUG_ON(!handle->virtual);

	handle->kmap_cnt--;

	if (handle->kmap_cnt < 1) {
		_smem_unmap_kernel_partial(handle, handle->virtual);
		handle->virtual = NULL;
	}
	handle_put(handle);

	mutex_unlock(&handle->lock);

	smem_dbg(handle->heap->smem, "%s %p unmap kernel (0x%lx@%pad)",
				dev_name(handle->dev), handle,
				handle->length, &handle->dma_addr);
}
EXPORT_SYMBOL(smem_unmap_kernel);

/* do dirty jobs instead of our customers */
void smem_sync_for_cpu(struct smem_handle *handle, enum dma_data_direction dir)
{
	struct smem_heap *heap = handle->heap;
	unsigned long size = handle->length;
	phys_addr_t paddr = dma_to_phys(heap->smem->dev, handle->dma_addr);

	if (handle->kmap_prot != SMEM_PGPROT_NONCONSISTENT ||
		ACCESS_ONCE(handle->kmap_cnt) < 1) {
		rmb();
		return;
	}

	if (dir != DMA_TO_DEVICE)
		outer_inv_range(paddr, paddr + size);
	dmac_unmap_area(handle->virtual, size, dir);
}
EXPORT_SYMBOL(smem_sync_for_cpu);

void smem_sync_for_device(struct smem_handle *handle, enum dma_data_direction dir)
{
	struct smem_heap *heap = handle->heap;
	unsigned long size = handle->length;
	phys_addr_t paddr = dma_to_phys(heap->smem->dev, handle->dma_addr);

	if (handle->kmap_prot != SMEM_PGPROT_NONCONSISTENT ||
		ACCESS_ONCE(handle->kmap_cnt) < 1) {
		wmb();
		return;
	}

	dmac_map_area(handle->virtual, size, dir);
	if (dir == DMA_FROM_DEVICE)
		outer_inv_range(paddr, paddr + size);
	else
		outer_clean_range(paddr, paddr + size);
}
EXPORT_SYMBOL(smem_sync_for_device);

dma_addr_t smem_phys(struct smem_handle *handle)
{
	return handle->dma_addr;
}
EXPORT_SYMBOL(smem_phys);

unsigned long smem_size(struct smem_handle *handle)
{
	return handle->length;
}
EXPORT_SYMBOL(smem_size);

int smem_regionid(struct smem_handle *handle)
{
	return handle->heap->smem->id;
}
EXPORT_SYMBOL(smem_regionid);

void smem_set_private(struct smem_handle *handle, void *ptr)
{
	handle->priv = ptr;
}
EXPORT_SYMBOL(smem_set_private);

void* smem_get_private(struct smem_handle *handle)
{
	return handle->priv;
}
EXPORT_SYMBOL(smem_get_private);

static void smem_get(struct smem *smem)
{
	get_device(smem->dev);
}

static void smem_put(struct smem *smem)
{
	put_device(smem->dev);
}
int smem_device_nr_regions(struct device *dev)
{
	int ret = 0, i;
	for (i = 0; i < SMEM_MAX_REGIONS_DEVICE; i++) {
		if (dev_get_smem(dev, i))
			ret++;
		else
			break;
	}
	return ret;
}
EXPORT_SYMBOL(smem_device_nr_regions);

int smem_device_regionid(struct device *dev, int idx)
{
	struct smem *smem = dev_get_smem(dev, idx);
	return smem ? smem->id : -1;
}
EXPORT_SYMBOL(smem_device_regionid);

void smem_region_range(struct device *dev, int idx, phys_addr_t *min, phys_addr_t *max)
{
	phys_addr_t minaddr = ~(0ULL);
	phys_addr_t maxaddr = 0;
	struct smem *smem = dev_get_smem(dev, idx);
	int i;

	BUG_ON(!smem);

	for (i = 0; i < SMEM_MAX_HEAPS; i++) {
		struct smem_heap *heap = smem->heaps[i];
		if (!heap)
			continue;
		if (minaddr > heap->base)
			minaddr = heap->base;
		if (maxaddr < (heap->base + heap->size - 1))
			maxaddr = (heap->base + heap->size - 1);
	}
	if (min)
		*min = minaddr;
	if (max)
		*max = maxaddr;
}
EXPORT_SYMBOL(smem_region_range);

/* debug purpose */
static int scan_device_handles(struct smem *smem, struct device *dev)
{
	int i, nr = 0;

	for (i = 0; i < SMEM_MAX_HEAPS; i++) {
		struct smem_handle *handle;
		struct smem_heap *heap = smem->heaps[i];

		if (!heap)
			continue;

		list_for_each_entry_rcu(handle, &heap->handles, link) {
			if (handle->dev == dev) {
				smem_err(smem, "zombie handle %p found\n", handle);
				nr++;
			}
		}
	}
	return nr;
}

static void _smem_device_detach(struct device *dev, void *res)
{
	int i;

	for (i = 0; i < SMEM_MAX_REGIONS_DEVICE; i++) {
		struct smem *smem = dev_get_smem(dev, i);
		if (smem) {
			WARN_ON(scan_device_handles(smem, dev) > 0);
			smem_put(smem);
			smem_info(smem, "detach device %s.\n", dev_name(dev));
		}
	}
	dev->archdata.smem_priv = NULL;
}

void smem_device_detach(struct device *dev)
{
	put_device(dev);
}
EXPORT_SYMBOL(smem_device_detach);

int smem_device_attach(struct device *dev, const int *region_ids, unsigned long alloc_flags)
{
	int i, nr_regions = 0;
	struct smem_device_data *dev_data;

	BUG_ON(!region_ids);

	dev_data = devres_alloc(_smem_device_detach, sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	memset(dev_data, 0, sizeof(*dev_data));

	for (i = 0; i < SMEM_MAX_REGIONS_SYSTEM; i++) {
		int id = region_ids[i];
		if (id < 0)
			break;
		if(id  >= SMEM_MAX_REGIONS_SYSTEM || !smem_regions[id]) {
			dev_warn(dev, "invalid region ids provides - %d\n", id);
			continue;
		}
		if (dev_data->smem_regions[nr_regions]) {
			dev_warn(dev, "already attached to smem.\n");
			break;
		}
		dev_data->smem_regions[nr_regions] = smem_regions[id];

		smem_get(smem_regions[id]);

		/* XXX: change to dbg level */
		smem_dbg(smem_regions[id], "%s attached\n", dev_name(dev));

		nr_regions++;
	}

	if (nr_regions > 0) {
		devres_add(dev, dev_data);
		dev_data->alloc_flags = alloc_flags;
		dev->archdata.smem_priv = dev_data;
	} else {
		dev_err(dev, "no smem region to assign\n");
		devres_free(dev_data);
		return -EINVAL;
	}

	get_device(dev);
	return 0;
}
EXPORT_SYMBOL(smem_device_attach);

#if defined(CONFIG_OF)
static struct smem* smem_of_find_smem(struct device_node *node)
{
	int i;
	for (i = 0; i < SMEM_MAX_REGIONS_SYSTEM; i++)
		if (smem_regions[i] && smem_regions[i]->phandle == node->phandle)
			return smem_regions[i];
	return NULL;
}

int smem_device_of_init(struct device *dev)
{
	int i;
	int nr_regions = 0;
	int region_ids[SMEM_MAX_REGIONS_SYSTEM + 1];

	for (i = 0; ; i++) {
		struct device_node *node = of_parse_phandle(dev->of_node, "smem-region", i);
		struct smem *smem;
		if (!node)
			break;
		smem = smem_of_find_smem(node);

		if (!smem) {
			pr_err("smem: failed to find smem node phandle=%u\n", node->phandle);
		}
		region_ids[nr_regions++] = smem->id;
	}
	if (nr_regions > 0) {
		u32 val;
		unsigned long alloc_flags = 0;
		region_ids[nr_regions] = -1;
		if (!of_property_read_u32(dev->of_node, "smem-alloc-flags", &val))
			alloc_flags = val;
		return smem_device_attach(dev, region_ids, alloc_flags);
	} else {
		dev_err(dev, "couldn't find smem region defined.\n");
		return -EINVAL;
	}
}
#else
int smem_device_of_init(struct device *dev)
{
	return -ENOSYS;
}
#endif	/* CONFIG_OF */
EXPORT_SYMBOL(smem_device_of_init);

static void smem_heap_destroy(struct smem_heap *heap)
{
	// TODO
}

void init_heap(struct smem *smem, const char *heap_name, struct smem_heap *heap,
			const struct smem_platform_heap *pheap, const unsigned long heapmask)
{
	heap->heapmask		= heapmask;

	if (pheap->flags & SMEM_PHEAP_NOMAP)
		heap->heapmask |= SMEM_HEAP_FLAGS_NOMAP;
	heap->smem		= smem;
	heap->name		= heap_name;
	heap->base		= pheap->base;
	heap->size		= pheap->size;

	heap->min_order		= pheap->min_order;
	heap->max_order		= pheap->max_order;

	heap->low_area		= pheap->low_area;
	heap->high_area		= pheap->high_area;
	heap->low_filter	= pheap->low_filter;
	heap->high_filter	= pheap->high_filter;
}

/* create an heap instance for a region with platform heap information */
static struct smem_heap* create_heap(struct smem *smem, const struct smem_platform_heap *pheap)
{
	struct smem_heap *heap;

	switch(SMEM_PHEAP_TYPE(pheap->flags)) {
	case SMEM_PHEAP_CARVEOUT:
		heap = smem_heap_carveout_create(smem, pheap);
		break;
	case SMEM_PHEAP_CMA:
		heap = smem_heap_cma_create(smem, pheap);
		break;
	default:
		heap = NULL;
		break;
	}

	INIT_LIST_HEAD_RCU(&heap->handles);

	return heap;
}

static void smem_destroy(struct smem *smem)
{
	/* TODO */
	smem_info(smem, "removed from system.\n");
}

/* creates several heaps of smem device */
static int smem_register_device(struct platform_device *pdev)
{
	struct smem_platform_data *pdata = platform_get_drvdata(pdev);
	const int id = pdata->id;
	struct smem *smem;
	int i;

	BUG_ON(!pdata);
	if (id >= SMEM_MAX_REGIONS_SYSTEM) {
		dev_err(&pdev->dev, "too many regions defined in this system.\n");
		return -EINVAL;
	}
	if (smem_regions[id]) {
		dev_err(&pdev->dev, "region id %d is conflicted.\n", id);
		return -EINVAL;
	}

	smem = devm_kzalloc(&pdev->dev, sizeof(*smem), GFP_KERNEL);
	smem->id = id;
	smem->dev = &pdev->dev;
	smem->phandle = pdev->dev.of_node->phandle;

	for (i = 0; i < SMEM_MAX_HEAPS; i++) {
		struct smem_platform_heap *pheap = pdata->platheaps[i];
		struct smem_heap *heap;

		if (!pheap)
			continue;

	       	heap = create_heap(smem, pheap);
		if (IS_ERR(heap))
			dev_err(&pdev->dev, "failed to create heap%d\n", i);
		else
			smem->heaps[i] = heap;
	}

	smem_regions[id] = smem;
	pdata->smem = smem;
	smem_info(smem, "registered.\n");
	return 0;
}

#if defined(CONFIG_OF_RESERVED_MEM)
static int smem_of_init(struct platform_device *pdev)
{
	int i, nr_heaps;
	struct smem_platform_data *pdata = platform_get_drvdata(pdev);
	u32 val;

	if (pdata)
		return -EBUSY;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);

	/* attach OF heap nodes to smem platform device data */
	for (nr_heaps = 0, i = 0; ; i++) {
		if (of_reserved_mem_device_init_by_idx(&pdev->dev, pdev->dev.of_node, i))
			break;
		nr_heaps++;
	}
	if (nr_heaps < 1) {
		dev_err(&pdev->dev, "error: no memory regions registered\n");
		return -EINVAL;
	}

	of_property_read_u32(pdev->dev.of_node, "region-id", &val);
	pdata->id = (int)val;
	return 0;
}
#else
static int smem_of_init(struct platform_device *pdev) { return 0; }
#endif

static int smem_probe(struct platform_device *pdev)
{
	int ret;
	ret = smem_of_init(pdev);
       	if (ret < 0) {
       		dev_err(&pdev->dev, "Failed to get smem device data.\n");
		return ret;
	}

	return smem_register_device(pdev);
}

static int smem_remove(struct platform_device *pdev)
{
	struct smem_platform_data *pdata = platform_get_drvdata(pdev);

	smem_destroy(pdata->smem);

	of_reserved_mem_device_release(&pdev->dev);

	return 0;
}

static const struct of_device_id smem_match_table[] = {
	{.compatible = "samsung,smem"},
	{},
};

static struct platform_driver smem_driver = {
	.probe = smem_probe,
	.remove = smem_remove,
	.driver = {
		.name = "smem-region",
		.of_match_table = smem_match_table,
	},
};

static int __init smem_init(void)
{
	pr_info("%s: Registered driver. version %s\n", smem_driver.driver.name, SMEM_VERSION_STR);

	return platform_driver_register(&smem_driver);
}
subsys_initcall(smem_init);

/*
 * reserved-mem "smem-heap"
 */
#ifdef CONFIG_OF_RESERVED_MEM

static struct smem_platform_heap rmem_heaps[SMEM_MAX_HEAPS_SYSTEM];
static int rmem_heaps_count;

/* attach reserved-memory to smem device node */
static int srmem_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct smem_platform_data *pdata = platform_get_drvdata(pdev);
	struct smem_platform_heap *pheap = rmem->priv;
	int i;

	BUG_ON(!pdata);

	/* a reserved-memory should be owned by only one smem region */
	if (pheap->flags & SMEM_PHEAP_USING) {
		dev_err(dev, "%p 0x%lx 0x%x heap assignment conflicts.\n", pheap, pheap->flags, SMEM_PHEAP_USING);
		return -EBUSY;
	}

	/* attach 'platform heaps' to smem device's platform data */
	for (i = 0; i < SMEM_MAX_HEAPS_SYSTEM; i++) {
		if (!pdata->platheaps[i]) {
			pdata->platheaps[i] = pheap;
			pheap->flags |= SMEM_PHEAP_USING;
			dev_dbg(dev, "assign reserved-mem to heap slot%d flags 0x%lx 0x%lx@%pa\n",
				i, pheap->flags, pheap->size, &pheap->base);
			return 0;
		}
	}
	dev_err(dev, "too much heaps assigned. max=%d\n", SMEM_MAX_HEAPS_SYSTEM);
	return -EBUSY;
}

static void srmem_device_release(struct reserved_mem *rmem, struct device *dev)
{
	/* TODO: but never happens */
}

static const struct reserved_mem_ops smem_rmem_ops = {
	.device_init	= srmem_device_init,
	.device_release	= srmem_device_release,
};

static int __init read_of_integer(const unsigned long node, const char *name)
{
	const __be32 *prop = of_get_flat_dt_prop(node, name, NULL);
	if (prop)
		return (int)of_read_ulong(prop, 1);
	else
		return -1;
}

static void  __init get_of_orders(struct reserved_mem *rmem,
	int *min_order, int *max_order)
{
	const unsigned long node = rmem->fdt_node;
	const int memblock_order = max_t(unsigned long, MAX_ORDER - 1, pageblock_order);
	int min, max;

	max = read_of_integer(node, "max-order");
	min = read_of_integer(node, "min-order");
	/*
	 * max range: 64K ~ (1 << (memblock-order + 6))
	 * min range: 4K ~ (1 << memblock_order)
	 */
	if (max < 4 || (max > (memblock_order << 6)))
		max = fls((int)rmem->size) - PAGE_SHIFT - 1;
	if (min > memblock_order || min > max || min < 0)
		min = max - 3;

	*min_order = min;
	*max_order = max;
}

static void  __init get_of_bounds(struct reserved_mem *rmem,
		unsigned long *low_filter, unsigned long *high_filter,
		unsigned long *low_area, unsigned long *high_area)
{
	const unsigned long node = rmem->fdt_node;
	int lf, hf, la, ha;
	bool use_defs = false;

	lf = read_of_integer(node, "low-filter");
	hf = read_of_integer(node, "high-filter");
	la = read_of_integer(node, "low-area");
	ha = read_of_integer(node, "high-area");

	if (lf < 0 || hf < 0 || la < 0 || ha < 0 )
		use_defs = true;
	else if (lf > hf || ((la + ha) > rmem->size))
		use_defs = true;
	if (use_defs) {
		*low_filter = (ulong)0x800000;
		*high_filter = (ulong)0x1000000;
		*low_area = (ulong)(rmem->size >> 2);
		*high_area = (ulong)(rmem->size >> 1);
	} else {
		*low_filter = (ulong)lf;
		*high_filter = (ulong)hf;
		*low_area = (ulong)la;
		*high_area = (ulong)ha;
	}

}
static int __init smem_rmem_setup(struct reserved_mem *rmem)
{
	const unsigned long node = rmem->fdt_node;
	int ret  = -EINVAL;
	bool reusable, no_map, chunk_pool;
	struct smem_platform_heap *pheap = NULL;
	int min_order, max_order;

	if (rmem_heaps_count >= SMEM_MAX_HEAPS_SYSTEM)
		return -ENOMEM;

	pheap = &rmem_heaps[rmem_heaps_count];

	reusable = of_get_flat_dt_prop(node, "reusable", NULL) != NULL;
	no_map = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;
	chunk_pool = of_get_flat_dt_prop(node, "chunk-pool", NULL) != NULL;

	get_of_orders(rmem, &min_order, &max_order);
	get_of_bounds(rmem, &pheap->low_filter, &pheap->high_filter,
			&pheap->low_area, &pheap->high_area);

	pheap->base = rmem->base;
	pheap->size = rmem->size;
	pheap->min_order = min_order;
	pheap->max_order = max_order;

	/* FIXME: duplicated codes */
	if (!reusable) {
		int order = min_order + PAGE_SHIFT;
		/* carve out heap */
		pheap->flags = SMEM_PHEAP_CARVEOUT;
		if (no_map)
			pheap->flags |= SMEM_PHEAP_NOMAP;

		pr_info("Reserved memory: smem carveout heap created - %pa@%pa, orders=%d (%ld %ld %ld %ld).\n",
				&rmem->size, &rmem->base, order,
				pheap->low_filter >> order, pheap->high_filter >> order,
				pheap->low_area >> order, pheap->high_area >> order);
	} else {
		/* CMA heap */
		struct cma *cma;
		int ret;
		phys_addr_t memblock_align = PAGE_SIZE << max_t(unsigned long, MAX_ORDER - 1, pageblock_order);

		if (no_map)
			return -EINVAL;

		if (!ALIGN(rmem->base, memblock_align) || !ALIGN(rmem->size, memblock_align)) {
			pr_err("Reserved memory: incorrect alignment of CMA memory region: %pa@%pa < %pa\n",
				&rmem->size, &rmem->base, &memblock_align);
			return -EINVAL;
		}

		/* CMA 'order per bit' = min_order */
		ret = cma_init_reserved_mem(rmem->base, rmem->size, min_order, &cma);
		if (ret) {
			pr_err("Reserved memory: unable to setup smem CMA heap - %pa@%pa, cma order = %d\n",
				&rmem->size, &rmem->base, min_order);
			return ret;
		}
		/* Architecture specific contiguous memory fixup. */
		dma_contiguous_early_fixup(rmem->base, rmem->size);

		pheap->flags = chunk_pool ? SMEM_PHEAP_CMAPOOL : SMEM_PHEAP_CMA;
		pheap->priv = cma;

		pr_info("Reserved memory: smem CMA heap created - %pa@%pa, orders=%d/%d\n",
				&rmem->size, &rmem->base, min_order, max_order);
	}

	rmem_heaps_count++;

	rmem->ops = &smem_rmem_ops;
	rmem->priv = pheap;
	return 0;
}

RESERVEDMEM_OF_DECLARE(smem, "smem-heap", smem_rmem_setup);

#endif	/* CONFIG_OF_RESERVED_MEM */
