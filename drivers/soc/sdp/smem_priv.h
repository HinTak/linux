/*
 * SDP memory allocator(smem) ion-like allocator,
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
#ifndef _SMEM_PRIV_H_
#define _SMEM_PRIV_H_

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <soc/sdp/smem.h>

#define SMEM_DEBUG
#define SMEM_TRACE

#ifdef SMEM_DEBUG
#define smem_dbg(smem, fmt, ...)	dev_dbg(smem->dev, fmt, ##__VA_ARGS__)
#else
#define smem_dbg(smem, fmt, ...)
#endif

#define smem_info(smem, fmt, ...)	dev_info(smem->dev, fmt, ##__VA_ARGS__)
#define smem_err(smem, fmt, ...)	dev_err(smem->dev, fmt, ##__VA_ARGS__)

#ifdef SMEM_TRACE
#define heap_trace(heap, fmt, ...)	dev_err((heap)->smem->dev, "%-8s" fmt, (heap)->name, ##__VA_ARGS__)
#endif

#define heap_err(heap, fmt, ...)	smem_err((heap)->smem, "%-8s" fmt, (heap)->name, ##__VA_ARGS__)

#define SMEM_MAX_REGIONS_SYSTEM	(12)	/* max # or regions exists in a system */
#define SMEM_MAX_HEAPS		(4)	/* max # of heaps in a smem region */
#define SMEM_MAX_HEAPS_SYSTEM	(SMEM_MAX_REGIONS_SYSTEM * SMEM_MAX_HEAPS)
#define SMEM_MAX_REGIONS_DEVICE	(4)	/* max # or regions defined in a device */

struct smem;
/**
 * struct smem_heap - internal heap, actual buffer provider
 *
 * @smem:		parent smem region
 * @heapmask:		bit 7:0=supported heap types(SMEMFLAGS_HEAPMASK) + bit 31:8=extra flags
 *
 * @allocate:		allocate buffer and returns handls (note that dev can be NULL)
 * @free
 * @precheck:		returns true if a buffer of given size is available currently.
 */
struct smem_heap {
	struct smem		*smem;
#define SMEM_HEAP_FLAGS_NOMAP	(1 << 8)
	unsigned long		heapmask;
	
	const char		*name;

	phys_addr_t		base;
	unsigned long		size;

	int			min_order;
	int			max_order;
	
	unsigned long		low_filter;
	unsigned long		low_area;
	unsigned long		high_filter;
	unsigned long		high_area;

	/* allocated handles */
	struct list_head	handles;

	struct smem_handle* (*allocate)(struct smem_heap *heap, struct device *dev,
			size_t size, size_t align, smem_flags_t flags,
			void *data);	// XXX: flags is needed for heap?
	int (*free)(struct smem_handle* handle);
	bool (*precheck)(struct smem_heap *heap, size_t size, size_t align, smem_flags_t flags);
	void (*dump)(struct smem_heap *heap);
};

/**
 * struct smem_handle - represents a buffer
 *
 * @kmap_cnt:		kernel mapping counts
 */
struct smem_handle {
	struct kref		ref;
	struct device		*dev;
	dma_addr_t		dma_addr;
	unsigned long		length;
	struct smem_heap	*heap;
	struct list_head	link;

	/* kernel mapping */
	struct mutex		lock;
	unsigned long		kmap_cnt;
	pgprot_t		kmap_prot;
	void			*virtual;	/* valid only for whole size mapping (smem_map_kernel_attrs) */

	void			*heap_priv;	/* heap private data */
	void			*priv;		/* user's service area */
};

/**
 * struct smem - represent a memory region (e.g. per channel)
 *
 * @id:			region id
 * @phandle:		of phandle
 * @dev:		smem device
 * @heaps:		registered heap array, priority base ordered
 */
struct smem {
	int			id;
	unsigned long		phandle;
	struct device		*dev;
	struct smem_heap	*heaps[SMEM_MAX_HEAPS];
};

/**
 * struct smem_device_data - user device's devarch data, mapping between device and smem
 *
 * @ alloc_flags:	default allocation flags (smem_flags_t)
 */
struct smem_device_data {
	struct smem	*smem_regions[SMEM_MAX_REGIONS_DEVICE];
	unsigned long	alloc_flags;
};

static inline struct smem_device_data* dev_get_smem_data(struct device *dev)
{
	return dev->archdata.smem_priv;
}
static inline struct smem* dev_get_smem(struct device *dev, int region_idx)
{
	struct smem_device_data *dev_data = dev_get_smem_data(dev);
	BUG_ON(!dev_data);
	return dev_data->smem_regions[region_idx];
}

/**
 * struct smem_platform_heap - platform-provided heap info
 *
 * @min_order:		minimum allocation size in PAGE_SIZE order
 * @max_order:		maximum "
 * @low_area:		'low' partition size in bytes
 * @low_filter:		maximum allocation size allocated in low partition
 * @high_area:		'high' partition size
 * @high_filter:	minimum allocation size allocated in high partition
 */
struct smem_platform_heap {
/* bit 7:0 heap type */
#define SMEM_PHEAP_NONE		(0)
#define SMEM_PHEAP_CARVEOUT	(1)
#define SMEM_PHEAP_CMA		(2)
#define SMEM_PHEAP_CMAPOOL	(3)
#define SMEM_PHEAP_TYPE(x)	((x) & 0xff)
#define SMEM_PHEAP_NOMAP	(0x100)
#define SMEM_PHEAP_USING	(0x200)

	unsigned long		flags;
	phys_addr_t		base;
	unsigned long		size;
	int			min_order;
	int			max_order;
	unsigned long		low_filter;
	unsigned long		low_area;
	unsigned long		high_filter;
	unsigned long		high_area;
	/* per-heap data */
	void			*priv;
};

struct smem_platform_data {
	int				id;
	struct smem			*smem;
	struct smem_platform_heap 	*platheaps[SMEM_MAX_HEAPS];
};

/* heap */
void init_heap(struct smem *smem, const char *name, struct smem_heap *heap,
			const struct smem_platform_heap *pheap, const unsigned long heapmask);

struct smem_heap* smem_heap_carveout_create(struct smem *smem, const struct smem_platform_heap *pheap);
void smem_heap_carveout_destroy(struct smem_heap *sheap);

struct smem_heap* smem_heap_cma_create(struct smem *smem, const struct smem_platform_heap *pheap);
void smem_heap_cma_destroy(struct smem_heap *sheap);

struct smem_heap* smem_heap_cmapool_create(struct smem *smem, const struct smem_platform_heap *pheap);
void smem_heap_cmapool_destroy(struct smem_heap *sheap);

struct smem_handle* heap_create_handle(struct device *dev, struct smem_heap *heap,
		dma_addr_t addr, unsigned long buf_len,
		void *priv, void *handle_priv);
int handle_get(struct smem_handle *handle);
int handle_put(struct smem_handle *handle);

/* debug features */
const char* smem_test_name(void *priv);
void dump_heap_handles(struct smem_heap *heap);

#endif
