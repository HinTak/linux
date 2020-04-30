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

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/bitmap.h>
#include <asm/bug.h>
#include <linux/delay.h>

#include <soc/sdp/smem.h>
#include "smem_priv.h"
#include "smem_cache.h"

#undef CO_DELAYED_FREE

typedef enum {
	CO_POS_LOW = 1,
	CO_POS_MID,
	CO_POS_HIGH,
} alloc_pos_t;

struct smem_heap_carveout {
	struct smem_heap	heap;
	struct gen_pool		*pool;
#ifdef CONFIG_SDP_SMEM_CACHE
	struct gcma *cache;
#endif

	int			order;
	int			nr_bits;
	unsigned long		flipmask;
	unsigned long		bits[0];
};

#define flip_is_top(hco, pos)		((hco)->flipmask & (1UL << (pos)))
#define flip_flip(hco, pos)		(hco)->flipmask ^= (1UL << (pos))
#define flip_set_top(hco, pos)		(hco)->flipmask |= (1UL << (pos))
#define flip_set_bottom(hco, pos)	(hco)->flipmask &= ~(1UL << (pos))

static int get_lowmark(struct smem_heap_carveout *hco)
{
	return (int)(hco->heap.low_area >> hco->order);
}
static int get_highmark(struct smem_heap_carveout *hco)
{
	return (int)((hco->heap.size - hco->heap.high_area) >> hco->order);
}
static int get_lowsize(struct smem_heap_carveout *hco)
{
	return (int)(hco->heap.low_filter >> hco->order);
}
static int get_highsize(struct smem_heap_carveout *hco)
{
	return (int)(hco->heap.high_filter >> hco->order);
}

static alloc_pos_t get_alloc_pos_by_size(struct smem_heap_carveout *hco, int nr)
{
	if (nr >= get_highsize(hco))
		return CO_POS_HIGH;
	else if (nr >= get_lowsize(hco))
		return CO_POS_MID;
	else
		return CO_POS_LOW;
}
static alloc_pos_t get_alloc_pos(struct smem_heap_carveout *hco, struct device *dev, smem_flags_t flags, int nr)
{
	alloc_pos_t pos_size = get_alloc_pos_by_size(hco, nr);	
	unsigned long mask = SMEMFLAGS_POS(flags);
	
	if (mask == SMEMFLAGS_POS_DEFAULT)
		return pos_size;
	
	switch (pos_size) {
	case CO_POS_HIGH:
		if (mask & SMEMFLAGS_POS_BOTTOM)
			return CO_POS_HIGH;
		if (mask & SMEMFLAGS_POS_MID)
			return CO_POS_MID;
		else
			return CO_POS_LOW;
		break;
	case CO_POS_MID:
		if (mask & SMEMFLAGS_POS_MID)
			return CO_POS_MID;
		if (mask & SMEMFLAGS_POS_BOTTOM)
			return CO_POS_HIGH;
		else
			return CO_POS_LOW;
		break;
	case CO_POS_LOW:
	default:
		if (mask & SMEMFLAGS_POS_TOP)
			return CO_POS_LOW;
		else if (mask & SMEMFLAGS_POS_MID)
			return CO_POS_MID;
		else
			return CO_POS_HIGH;
	}
}

static int get_head(struct smem_heap_carveout *hco, alloc_pos_t pos)
{
	switch(pos) {
	case CO_POS_MID:
		return get_lowmark(hco);
	case CO_POS_HIGH:
		return get_highmark(hco);
	case CO_POS_LOW:
	default:
		return 0;
	}
}
static int get_tail(struct smem_heap_carveout *hco, alloc_pos_t pos)
{
	switch(pos) {
	case CO_POS_LOW:
		return get_lowmark(hco);
	case CO_POS_MID:
		return get_highmark(hco);
	case CO_POS_HIGH:
	default:
		return hco->nr_bits;
	}
}

static bool limit_touched(struct smem_heap_carveout *hco, alloc_pos_t pos, int bit, int nr)
{
	int low  = get_lowmark(hco);
	int high = get_highmark(hco);

	switch(pos) {
	case CO_POS_LOW:
		return (bit + nr) > low;
	case CO_POS_MID:
		return (bit < low) || ((bit + nr) > high);
	case CO_POS_HIGH:
		return bit < high;
	default:
		return false;
	}
}

#ifdef CO_DELAYED_FREE
static unsigned long* get_free_bits(struct smem_heap_carveout *hco)
{
	return &hco->bits[BITS_TO_LONGS(hco->nr_bits)];
}

static void free_delayed(struct smem_heap_carveout *hco)
{
	unsigned long *free_map = get_free_bits(hco);

	__bitmap_xor(hco->bits, hco->bits, free_map, hco->nr_bits);
	bitmap_zero(free_map, hco->nr_bits);
}
#endif

static int _alloc_top(unsigned long *map, int size, int nr, int start)
{
	return bitmap_find_next_zero_area(map, size, start, nr, 0);
}

static int _alloc_bottom(unsigned long *map, int size, int nr)
{
	int s, e = 0, last = -1;

	// TODO: reverse search
	while (e < size) {
		s = find_next_zero_bit(map, size, e);
		if (s >= size)
			break;
		e = find_next_bit(map, size, s);
		if (((e - s) >= nr) && (last < (e - nr))) {
			last = e - nr;
		}
	}

	return (last < 0)  ? size: last;
}

static int _alloc_autoflip(struct smem_heap_carveout *hco, int nr, alloc_pos_t pos)
{
	int bit;
	int head = get_head(hco, pos);
	int tail = get_tail(hco, pos);

	bool headzero = !test_bit(head, hco->bits);
	bool tailzero = !test_bit(tail - 1, hco->bits);
	bool top_alloc = flip_is_top(hco, pos);

	heap_trace(&hco->heap, " flip %3d@%d hz%d tz%d top%d\n",
			nr, pos, headzero, tailzero, top_alloc);

	/* check if there's space in the opposite side */
	if ( !top_alloc && headzero && !tailzero) {		
		bit = find_next_bit(hco->bits, hco->nr_bits, head);
		if ((bit - head) >= nr) {
			flip_set_top(hco, pos);
			heap_trace(&hco->heap, " fliptop\n");
		}
	} else if ( top_alloc && !headzero && tailzero) {
		bit = find_last_bit(hco->bits, tail);
		if ((bit == tail && (nr <= tail)) || ((tail - bit) > nr)) {
			flip_set_bottom(hco, pos);
			heap_trace(&hco->heap, " flipbottom\n");
		}
	}

	if (flip_is_top(hco, pos)) {
		do {
			bit = _alloc_top(hco->bits, hco->nr_bits, nr, get_head(hco, pos));
		} while (bit >= hco->nr_bits && pos-- > CO_POS_LOW);
	} else {
		int tail;
		do {
			tail = get_tail(hco, pos);
			bit = _alloc_bottom(hco->bits, tail, nr);
		} while (bit >= tail && pos++ < CO_POS_HIGH);
	}
	if (bit < hco->nr_bits)
		flip_flip(hco, pos);
	return bit;
}

static void smem_co_dump(struct smem_heap *heap)
{
	struct smem_heap_carveout *hco =
		container_of(heap, struct smem_heap_carveout, heap);
	struct smem_handle *handle;
	int total = 0;

	heap_err(heap, "dump lo %d hi %d end %d (%3d %3d)\n",
		get_lowmark(hco), get_highmark(hco), hco->nr_bits,
		get_lowsize(hco), get_highsize(hco));

	list_for_each_entry_rcu(handle, &heap->handles, link) {
		int addr0, addr1;
		unsigned long len = smem_size(handle);
		addr0 = (int)(smem_phys(handle) - heap->base);
		addr1 = addr0 + len;
		total += (int)(len >> hco->order);
		heap_err(heap, "%3d %3d (%3d) %s\n",
			addr0 >> hco->order,
			addr1 >> hco->order,
			(int)(len >> hco->order),
			smem_test_name(handle->priv));
	}
	heap_err(heap, "total %d allocated (%d remains)\n\n", total, hco->nr_bits - total);
}

static struct smem_handle* smem_co_allocate(struct smem_heap *heap, struct device *dev,
		size_t size, size_t align, smem_flags_t flags, void *data)
{
	struct smem_heap_carveout *hco =
		container_of(heap, struct smem_heap_carveout, heap);
	struct smem_handle *handle;
	phys_addr_t pa;
	int bit;
	const int nr = (size + (1 << hco->order) - 1) >> hco->order;
	alloc_pos_t pos = get_alloc_pos(hco, dev, flags, nr);
	bool limited;
	ktime_t t0, t1;
	int try_count = 0;
	const u32 time_limit = 2000000;

	t0 = ktime_get();
	
retry:
	try_count++;

	switch (SMEMFLAGS_FLAVOR(flags)) {
	case SMEMFLAGS_FLAVOR_LOW:
		bit = _alloc_top(hco->bits, hco->nr_bits, nr, get_head(hco, pos));
		break;
	case SMEMFLAGS_FLAVOR_HIGH:
		bit = _alloc_bottom(hco->bits, get_tail(hco, pos), nr);
		break;
	case SMEMFLAGS_FLAVOR_DEFAULT:
	default:
		bit = _alloc_autoflip(hco, nr, pos);
		break;
	}
	
	limited = limit_touched(hco, pos, bit, nr);

#ifdef CO_DELAYED_FREE
	if (bit >= hco->nr_bits || limited) {
		heap_trace(heap, "free delayed for %3d - %c %c\n", nr,
			(bit >= hco->nr_bits) ? 'y' : 'n',
			limited ? 'y' : 'n');
		free_delayed(hco);
		bit = alloc_flip(hco, nr, pos);
		limited = limit_touched(hco, pos, bit, nr);
	}
#endif
	t1 = ktime_get();

	if (bit >= hco->nr_bits) {
		if(ktime_us_delta(t1, t0)< time_limit) {
			msleep(4);
			goto retry;
		} else {		
			heap_trace(heap, "alloc %3d failed %s (%d retry)\n", nr, smem_test_name(data), try_count);
			return NULL;
		}
	}

	bitmap_set(hco->bits, bit, nr);

	pa = heap->base + (bit << hco->order);
	handle = heap_create_handle(dev, heap, pa, nr << hco->order, NULL, data);

#ifdef CONFIG_SDP_SMEM_CACHE
	smem_cache_claim_area(hco->cache,
			__phys_to_pfn(heap->base + (bit << hco->order)), PFN_DOWN(nr << hco->order));
#endif

	heap_trace(heap, "alloc 0x%08x %3d - %3d (pos%d over%d %s)\n",
			(unsigned int)(pa & 0xffffffffUL),
			bit, bit + nr,
			pos, limited,
			smem_test_name(data));

	if (limited)
		smem_co_dump(&hco->heap);

	return handle;
}

static int smem_co_free(struct smem_handle *handle)
{
	struct smem_heap_carveout *hco =
		container_of(handle->heap, struct smem_heap_carveout, heap);
	int bit = (handle->dma_addr - hco->heap.base) >> hco->order;
	int nr = handle->length >> hco->order;

	heap_trace(handle->heap, "free %3d %3d (%s)\n", bit, bit + nr, smem_test_name(handle->priv));

#ifdef CONFIG_SDP_SMEM_CACHE
	smem_cache_release_area(hco->cache,
			__phys_to_pfn(handle->heap->base + (bit << hco->order)), PFN_DOWN(nr << hco->order));
#endif

#ifdef CO_DELAYED_FREE
	bitmap_set(get_free_bits(hco), bit, nr);
#else
	bitmap_clear(hco->bits, bit, nr);
#endif
	return 0;
}

bool smem_co_precheck(struct smem_heap *sheap, size_t size, size_t align, smem_flags_t flags)
{
	// FIXME: real check
	return true;
}

struct smem_heap* smem_heap_carveout_create(struct smem *smem, const struct smem_platform_heap *pheap)
{
	struct smem_heap_carveout *hco;
	static const char *heap_name = "co";

	const int order = pheap->min_order + PAGE_SHIFT;
	const int nr_bits = pheap->size >> order;

	int alloc_size = sizeof(struct smem_heap_carveout);

	alloc_size +=  sizeof(hco->bits[0]) * BITS_TO_LONGS(nr_bits);
#ifdef CO_DELAYED_FREE
	alloc_size +=  sizeof(hco->bits[0]) * BITS_TO_LONGS(nr_bits);
#endif

	hco = devm_kzalloc(smem->dev, alloc_size, GFP_KERNEL);
	if (!hco)
		return ERR_PTR(-ENOMEM);

	init_heap(smem, heap_name,  &hco->heap, pheap, SMEMFLAGS_CARVEOUT_DYNAMIC);

	hco->heap.allocate	= smem_co_allocate;
	hco->heap.free		= smem_co_free;
	hco->heap.precheck	= smem_co_precheck;
	hco->heap.dump		= smem_co_dump;

	hco->order		= order;
	hco->nr_bits		= nr_bits;

#ifdef CONFIG_SDP_SMEM_CACHE
	smem_cache_init(__phys_to_pfn(pheap->base), PFN_DOWN(pheap->size), &hco->cache);
#endif

	smem_info(hco->heap.smem, "%-8s created, order=%d bits=%d mid=%d high=%d\n",
		hco->heap.name, order, nr_bits, get_lowmark(hco), get_highmark(hco));
	return &hco->heap;
}

void smem_heap_carveout_destroy(struct smem_heap *heap)
{
}

