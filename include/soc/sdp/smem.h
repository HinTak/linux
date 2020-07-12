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
#ifndef _SOC_SDP_SMEM_H_
#define _SOC_SDP_SMEM_H_

#include <linux/types.h>
#include <linux/dma-mapping.h>

/* smem_flags_t - smem allocation request flags
 *   bit [ 7: 0] heap-type
 *   bit [15: 8] device's preferred position within a heap
 */
#define SMEMFLAGS_HEAP_DEFAULT		(0x00)
#define SMEMFLAGS_SHARED_SYSTEM		(1)
#define SMEMFLAGS_CARVEOUT_DYNAMIC	(2)
#define SMEMFLAGS_CARVEOUT_STATIC	(4)
#define SMEMFLAGS_HEAPTYPE(flags)	((flags) & 0xff)

/* allocation hint - position, a heap could have three partitions */
#define SMEMFLAGS_POS_DEFAULT		(0x0000)
#define SMEMFLAGS_POS_TOP		(0x0100)
#define SMEMFLAGS_POS_MID		(0x0200)
#define SMEMFLAGS_POS_BOTTOM		(0x0400)
#define SMEMFLAGS_POS(flags)		((flags) & 0xff00)

/* allocation hint - flavor, a client can specify preferred allocation policy */
#define SMEMFLAGS_FLAVOR_DEFAULT	(0x000000)
#define SMEMFLAGS_FLAVOR_LOW		(0x010000)	/* client prefers low address */
#define SMEMFLAGS_FLAVOR_HIGH		(0x020000)
#define SMEMFLAGS_FLAVOR(flags)		((flags) & 0xff0000)

#define SMEMFLAGS_DELAY_SLEEPABLE	(0x010000)
#define SMEMFLAGS_DELAY_MASK(x)		((x) & 0xff0000)
#define SMEMFLAGS_DELAY_ALLOWED(x)	(SMEMFLAGS_DELAY_MASK(x) == SMEMFLAGS_DELAY_SLEEPABLE)
typedef unsigned long smem_flags_t;

#define SMEMFLAGS_NORMAL		(SMEMFLAGS_POS_DEFAULT | SMEMFLAGS_HEAP_DEFAULT)

struct smem_handle;

/**
 * smem_device_of_init - parse a device's OF properties and attach smem regions
 *
 * @dev:		client device
 */
int smem_device_of_init(struct device *dev);

/**
 * smem_device_attach - attach a client device to smem regions
 *
 * @dev:			client device
 * @region_ids:		an array of smem region ids to attach, the last entry should have -1
 * @alloc_flags:	default allocation flags applied to a device's calling smem_alloc()
 */
int smem_device_attach(struct device *dev, const int *region_ids, unsigned long alloc_flags);

/**
 * smem_device_detach - detach a client device from smem
 * @dev:	a client device
 */
void smem_device_detach(struct device *dev);

/**
 * smem_device_nr_regions - returns number of smem regions a device attached
 *
 * @dev:	clien device
 */
int smem_device_nr_regions(struct device *dev);

/**
 * smem_device_regionid - translate device local region index to global region id
 *
 * @dev:	clien device
 * @idx:	device local region index
 */
int smem_device_regionid(struct device *dev, int idx);

/**
 * smem_phys - returns the physical address of a handle
 *
 * @handle:	smem handle
 */
dma_addr_t smem_phys(struct smem_handle *handle);

/**
 * smem_size - returns the buffer size of a handle
 *
 * @handle:	smem handle
 */
unsigned long smem_size(struct smem_handle *handle);

/**
 * smem_regionid - returns global region id of a handle
 *
 * @handle:	smem handle
 */
int smem_regionid(struct smem_handle *handle);

/**
 * smem_region_range - get address range of a region
 *
 * @dev:	a client device
 * @idx:	device local region index
 * @min:	[out] lowest address
 * @max:	[out] highest address, (of the last byte address)
 */
void smem_region_range(struct device *dev, int idx, phys_addr_t *min, phys_addr_t *max);

/**
 * smem_alloc_on_region_mask_data - allocate a buffer
 *
 * @dev:		a client device
 * @size:		size to allocate
 * @align:		alignment
 * @flags:		allocation flags
 * @region_mask:	device's local regions to allocate on
 * @data:		handle private data
 *
 * allocate a buffer on a available region with client's private data.
 * returns allocated smem handle or NULL if failed
 */
struct smem_handle* smem_alloc_on_region_mask_data(struct device *dev,
			size_t size, size_t align,
			smem_flags_t flags, u32 region_mask,
			void *data);

/**
 * smem_alloc_on_region_mask - allocate a buffer
 *
 * @dev:		a client device
 * @size:		size to allocate
 * @align:		alignment
 * @flags:		allocation flags
 * @region_mask:	device's local regions to allocate on
 *
 * allocate a buffer on a available region.
 * returns allocated smem handle or NULL if failed
 */
static inline struct smem_handle* smem_alloc_on_region_mask(struct device *dev,
			size_t size, size_t align,
			smem_flags_t flags, u32 region_mask)
{
	return smem_alloc_on_region_mask_data(dev, size, align, flags, region_mask, NULL);
}

/**
 * smem_alloc_on_region - allocate a buffer
 *
 * @dev:		a client device
 * @size:		size to allocate
 * @align:		alignment
 * @flags:		allocation flags
 * @region_idx:		device's local region index to allocate on
 *
 * allocate a buffer on specific region.
 * returns allocated smem handle or NULL if failed
 */
static inline struct smem_handle* smem_alloc_on_region(struct device *dev,
			size_t size, size_t align,
			smem_flags_t flags, int region_idx)
{
	return smem_alloc_on_region_mask(dev, size, align, flags, 1 << region_idx);
}

/**
 * smem_alloc - allocate a buffer
 *
 * @dev:		a client device
 * @size:		size to allocate
 * @align:		alignment
 *
 * allocate a buffer on any available region with default allocation flags.
 * returns allocated smem handle or NULL if failed
 */
static inline struct smem_handle* smem_alloc(struct device *dev, size_t size, size_t align)
{
	return smem_alloc_on_region_mask(dev, size, align, SMEMFLAGS_NORMAL, -1);
}

/**
 * smem_free - free memory buffer
 *
 * @handle:	smem handle
 */
int smem_free(struct smem_handle *handle);

/**
 * smem_set_private - set handle's private data
 *
 * @handle:	smem handle
 * @ptr:	private data
 */
void smem_set_private(struct smem_handle *handle, void *ptr);

/**
 * smem_get_private - returns the handle's private data
 *
 * @handle:	smem handle
 */
void* smem_get_private(struct smem_handle *handle);

/**
 * smem_map_kernel_attrs - create a kernel mapping of a handle with dma atrribute
 *
 * @handle:	smem handle
 * @attrs:	dma attribute for mapping, NULL for default
 *
 * internal mapping count is managed when map/unmap is done by client.
 * to free actually the buffer, the numer of mappings should be zero.
 */
void* smem_map_kernel_attrs(struct smem_handle *handle, struct dma_attrs *attrs);

/**
 * smem_map_kernel_attrs - create a kernel mapping of a handle
 *
 * @handle:	smem handle
 *
 */
static inline void* smem_map_kernel(struct smem_handle *handle)
{
	return smem_map_kernel_attrs(handle, NULL);
}

void smem_unmap_kernel(struct smem_handle *handle);

/**
 * smem_sync_for_cpu - synchronize memory buffer before cpu use
 *
 * @handle:		smem handle
 * @dir:		direction between CPU and device
 *
 * when a handle is mapped cacheable DMA attributes,
 * this function synchronize the cpu caches befure use.
 * behaves exactly same as dma_sync_for_cpu()
 */
void smem_sync_for_cpu(struct smem_handle *handle, enum dma_data_direction dir);

/**
 * smem_sync_for_device - synchronize memory buffer before device access
 *
 * @handle:		smem handle
 * @dir:		direction between CPU and device
 *
 * when a handle is mapped cacheable DMA attributes,
 * this function synchronize the cpu caches before use.
 * behaves exactly same as dma_sync_for_cpu()
 */
void smem_sync_for_device(struct smem_handle *handle, enum dma_data_direction dir);

/**
 * smem_map_kernel_partial - create a kernel mapping partial range of a buffer
 *
 * @handle:	smem handle
 * @attrs:	dma attribute for mapping, NULL for default
 * @offset:	an offset within a buffer
 * @len:	buffer length to be mapped
 *
 * In contrast to smem_map_kernel(), calling this does not increase internal mapping count.
 * freeing a buffer with kernel mapping is undefined behavior.
 */
void* smem_map_kernel_partial(struct smem_handle *handle, struct dma_attrs *attrs,
	unsigned long offset, unsigned long len);

/**
 * smem_unmap_kernel_partial - unmap kernel mapping
 *
 * @handle:	smem handle
 * @ptr:	cpu address to be unmapped
 *
 */
void smem_unmap_kernel_partial(struct smem_handle *handle, void *ptr);


/**
 * smem_remap_buffer - do same as ioremap() or memremap() for reserved/carveout memory
 *
 * @paddr:	physical address to map, not have to be smem buffer
 * @len:	size to map
 * @attrs:	dma attribute for mapping, NULL for default (coherent mapping)
 *
 * this function can do what ioremap() cannot do with kernel reserved pages
 * 
 * given address range should be on (valid && reserved) || !valid pfns.
 */
void* smem_remap_buffer(phys_addr_t paddr, unsigned long len, struct dma_attrs *attrs);

/**
 * smem_unmap_kernel_partial - unmap kernel mapping
 *
 * @ptr:	cpu address to unmap
 *
 */
void smem_unmap_buffer(void *ptr);

/* dma-buf exports */

#endif

