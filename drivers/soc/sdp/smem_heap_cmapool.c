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
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/dma-mapping.h>
#include <linux/cma.h>
#include <linux/workqueue.h>
#include <linux/genalloc.h>
#include <linux/ktime.h>
#include <linux/delay.h>

#include <soc/sdp/smem.h>

#include "smem_priv.h"

struct smem_heap_cma;
struct smem_chunk {
	struct list_head	link;
	phys_addr_t		paddr;
	struct smem_heap_cma	*parent;

#define CHUNK_INUSE		(1 << 0)	/* on active queue */
#define CHUNK_EMPTY		(1 << 1)
	unsigned long		flags;
	ktime_t			rm_time;
	unsigned long		bits[0];
};

struct smem_heap_cma {
	struct smem_heap	heap;

	struct cma		*cma;
	struct mutex		lock;

	struct delayed_work	alloc_work;
	struct delayed_work	free_work;	
	int			delay_alloc;		/* retry-alloc intervals in msecs */
	
#define WORK_ALLOC_PROGRESS	(1 << 0)
	unsigned long		work_flags;

	/* 'in-use' chunk queue */
	int			nr_chunks;
	struct list_head	chunks;

	/* 'wait for free' chunk queue,  most recent chunk on the head */
	int			nr_chunks_freeq;
	struct list_head	chunks_freeq;

};

#define to_cmaheap(h)		((struct smem_heap_cma *)h)

/* a chunk's bitmap */
#define BITMAP_SIZE(hc)			(1 << ((hc)->heap.max_order - (hc)->heap.min_order))

/* one chunk's size */
#define CHUNK_PAGES(hc)			(1 << (hc)->heap.max_order)
#define CHUNK_BYTES(hc)			(CHUNK_PAGES(hc) << PAGE_SHIFT)
#define CHUNK_UNITS(hc)			(1 << ((hc)->heap.max_order - (hc)->heap.min_order))

/* a unit = minimal allocation size in internal chunk */
#define UNIT_SHIFT(hc)			((hc)->heap.min_order + PAGE_SHIFT)
#define UNIT_BYTES(hc)			(1 << UNIT_SHIFT(hc))
#define UNIT_PAGES(hc)			(1 << (hc)->heap.min_order)
#define BYTES_TO_UNITS(hc, size)	(((size) + UNIT_BYTES(hc) - 1) >> UNIT_SHIFT(hc))

#define MIN_ALLOC_SIZE(hc)		(UNIT_BYTES(hc) / 2)

/* total number of chunks can be allocated from a heap */
#define TOTAL_CHUNKS(hc)		((hc)->heap.size >> (PAGE_SHIFT + (hc)->heap.max_order))

/* if the usage of a chunk is under FREE_RATIO/10.24 percent, do not allocate on this chunk,
   (move this chunk out of in-use chunk list) */
#define FREE_RATIO		(256)
#define FREEQ_THRESHOLD(hc)	((BITMAP_SIZE(hc) * FREE_RATIO) >> 10)

/* delay before retrying cma_alloc on allocation fails in msec. */
#define ALLOC_RETRY_DELAY_MIN	(100)
#define ALLOC_RETRY_DELAY_MAX	(5000)
#define ALLOC_RETRY_INC_RATIO	(1408)	/* delay *= (ALLOC_RETRY_INC_RATIO / 1024) */
#define ALLOC_RETRY_DEC_RATIO	(640)	/* delay *= (ALLOC_RETRY_DEC_RATIO / 1024) */

#define FREE_DELAY		(4000)

static int get_delay_alloc(struct smem_heap_cma *hc)
{
	return hc->delay_alloc;
}

static int get_delay_free(struct smem_heap_cma *hc)
{
	return FREE_DELAY;
}

static void increase_delay_alloc(struct smem_heap_cma *hc)
{
	hc->delay_alloc = min_t(int,
		(hc->delay_alloc * ALLOC_RETRY_INC_RATIO) >> 10,
		ALLOC_RETRY_DELAY_MAX);
}

static void reduce_delay_alloc(struct smem_heap_cma *hc)
{
	hc->delay_alloc = max_t(int,
		(hc->delay_alloc * ALLOC_RETRY_DEC_RATIO) >> 10,
		ALLOC_RETRY_DELAY_MIN);
}

static struct smem_chunk* smem_chunk_alloc(struct smem_heap_cma *hc)
{
	struct smem_chunk *chunk;
	struct page *pages;
	const int alloc_size = sizeof(struct smem_chunk) +
		sizeof(chunk->bits[0]) * BITS_TO_LONGS(BITMAP_SIZE(hc));
	ktime_t t0, t1;

	/* FIXME: consider alignment */

	BUG_ON(!hc || !hc->cma || !UNIT_PAGES(hc));

	t0 = ktime_get();
	pages = cma_alloc(hc->cma, CHUNK_PAGES(hc), hc->heap.min_order);
	t1 = ktime_get();

	if (!pages) {
		heap_trace(&hc->heap, "chunk-alloc fail %09lldusec\n", ktime_us_delta(t1, t0));
		return NULL;
	}

	chunk = kzalloc(alloc_size, GFP_KERNEL);
	if (unlikely(!chunk)) {
		cma_release(hc->cma, pages, CHUNK_PAGES(hc));
		return NULL;
	}

	INIT_LIST_HEAD(&chunk->link);
	chunk->paddr = page_to_phys(pages);
	chunk->parent = hc;
	heap_trace(&hc->heap, "chunk-alloc %pa page %p %09lldusec\n",
		&chunk->paddr, pages, ktime_us_delta(t1, t0));

	return chunk;
}

static void smem_chunk_free(struct smem_chunk *chunk)
{
	struct page *pages = phys_to_page(chunk->paddr);
	struct smem_heap_cma *hc = chunk->parent;

	heap_trace(&hc->heap, "chunk-free %p(%pa) page %p\n", chunk, &chunk->paddr, pages);

	cma_release(hc->cma, pages, CHUNK_PAGES(hc));
	list_del(&chunk->link);
	kfree(chunk);
}
/* returns true if allocated chunks are over total limit */
static bool check_limit(struct smem_heap_cma *hc)
{
	return TOTAL_CHUNKS(hc) <= (hc->nr_chunks + hc->nr_chunks_freeq);
}

/* check the limit and trigger the allocator work */
static void trigger_alloc_work(struct smem_heap_cma *hc, int msecs)
{
	const unsigned long jiffies = msecs_to_jiffies((msecs < 0) ? get_delay_alloc(hc) : msecs);

	if (check_limit(hc))
		return;
	hc->work_flags |= WORK_ALLOC_PROGRESS;
	schedule_delayed_work(&hc->alloc_work, jiffies);
}

static void trigger_free_work(struct smem_heap_cma *hc, int msecs)
{
	const unsigned long jiffies = msecs_to_jiffies(msecs);
	heap_trace(&hc->heap, "trigger free in %d msecs\n", msecs);
	schedule_delayed_work(&hc->free_work, jiffies);
}

/* chunk should be empty */
static void reserve_free_chunk(struct smem_chunk *chunk)
{
	struct smem_heap_cma *hc = chunk->parent;
	const int msecs = get_delay_free(hc);

	chunk->flags = CHUNK_EMPTY;
	chunk->rm_time = ktime_add_ms(ktime_get(), msecs);

	trigger_free_work(hc, msecs);
}

/* allocator workqueue worker */
static void cma_alloc_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct smem_heap_cma *hc = container_of(dwork, struct smem_heap_cma, alloc_work);
	struct smem_chunk *chunk = NULL;
	bool alloc_limited;

	mutex_lock(&hc->lock);
	alloc_limited = check_limit(hc);
	mutex_unlock(&hc->lock);

	if (alloc_limited)
		return;

	chunk = smem_chunk_alloc(hc);

	mutex_lock(&hc->lock);

	if (chunk) {
		list_add_tail(&chunk->link, &hc->chunks_freeq);
		hc->nr_chunks_freeq++;
		reserve_free_chunk(chunk);
		hc->work_flags &= ~WORK_ALLOC_PROGRESS;
		reduce_delay_alloc(hc);
	} else {
		increase_delay_alloc(hc);
		trigger_alloc_work(hc, -1);	/* retry later */
	}

	mutex_unlock(&hc->lock);
}

static void cma_free_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct smem_heap_cma *hc = container_of(dwork, struct smem_heap_cma, free_work);
	struct smem_chunk *chunk = NULL, *tmp;
	LIST_HEAD(free_list);

	int nr_empty = 0, nr_freed = 0;
	ktime_t shortest_time = ktime_add_ms(ktime_get(), get_delay_free(hc));

	mutex_lock(&hc->lock);

	list_for_each_entry_safe(chunk, tmp, &hc->chunks_freeq, link) {
		unsigned long chunk_flags = chunk->flags;
		if (!(chunk_flags & CHUNK_EMPTY))
			continue;
		nr_empty++;		
		if (ktime_after(ktime_get(), chunk->rm_time)) {
			list_move(&chunk->link, &free_list);
			hc->nr_chunks_freeq--;
			nr_freed++;
		} else {
			if (ktime_before(chunk->rm_time, shortest_time)) {
				shortest_time = chunk->rm_time;
				heap_trace(&hc->heap, "mark remove chunk %p after %dmsec\n",
					chunk, (int)ktime_ms_delta(shortest_time, ktime_get()));
			}
		}
	}
	if (nr_freed != nr_empty)
		trigger_free_work(hc, ktime_ms_delta(shortest_time, ktime_get()));

	mutex_unlock(&hc->lock);

	list_for_each_entry_safe(chunk, tmp, &free_list, link) {
		smem_chunk_free(chunk);
	}
}

/**
 * find_and_alloc - find a chunk available to allocate
 *
 * @hc		: heap_cma
 * @chunks	: chunk list to search
 * @nr		: number of units to allocate
 * @alloc	: allocation executed on a chunk if true, (false to search only)
 * @chunk_found	: returns found chunk
 *
 * returns an index of bitmap or -1 for fail
 */
static int find_and_alloc(struct smem_heap_cma *hc, struct list_head *chunks, int nr, bool alloc,
			/*out*/ struct smem_chunk **chunk_found)
{
	struct smem_chunk *chunk;
	list_for_each_entry(chunk, chunks, link) {
		int bit = bitmap_find_next_zero_area(chunk->bits, BITMAP_SIZE(hc), 0, nr, 0);
		if (bit < BITMAP_SIZE(hc)) {
			if (alloc) {
				bitmap_set(chunk->bits, bit, nr);
				if (chunk_found)
					*chunk_found = chunk;
			}
			return bit;
		}
	}
	return -1;
}

static void reclaim_chunk(struct smem_heap_cma *hc, struct smem_chunk *chunk)
{
	chunk->flags = CHUNK_INUSE;
	list_move(&chunk->link, &hc->chunks);
	hc->nr_chunks_freeq--;
	hc->nr_chunks++;
	heap_trace(&hc->heap, "chunk %p(%pa) reclaimed from freeq.\n", chunk, &chunk->paddr);
}

static void move_to_freeq(struct smem_heap_cma *hc, struct smem_chunk *chunk, int cur_alloc)
{
	chunk->flags &= (~CHUNK_INUSE);
	list_move(&chunk->link, &hc->chunks_freeq);
	hc->nr_chunks--;
	hc->nr_chunks_freeq++;
	heap_trace(&hc->heap, "chunk %p(%pa) move to freeq (weight %d)\n", chunk, &chunk->paddr, cur_alloc);
}

static bool check_alloc_unit(struct smem_heap_cma *hc, size_t size, size_t align)
{
	const int nr = BYTES_TO_UNITS(hc, size);
	
	if (size < MIN_ALLOC_SIZE(hc))
		return false;
	else if (nr > CHUNK_UNITS(hc))
		return false;
	else
		return true;
}
static bool smem_cma_precheck(struct smem_heap *heap, size_t size, size_t align,
				smem_flags_t flags)
{
	struct smem_heap_cma *hc = to_cmaheap(heap);
	const int nr = BYTES_TO_UNITS(hc, size);
	int bit;
	bool need_wait;
	
	if (!check_alloc_unit(hc, size, align))
		return false;	
	
	mutex_lock(&hc->lock);

	/* find in in-use chunk list first */
	bit = find_and_alloc(hc, &hc->chunks, nr, false, NULL);
	if (bit < 0) {
		/* fallback, free queue */
		bit = find_and_alloc(hc, &hc->chunks_freeq, nr, false, NULL);
	}
	need_wait = (bit < 0) && !check_limit(hc);
	
	mutex_unlock(&hc->lock);
	
	if (bit >= 0)
		return true;
	
	if (need_wait && SMEMFLAGS_DELAY_ALLOWED(flags)) {
		trigger_alloc_work(hc, 0);
		heap_trace(&hc->heap, "delay alloc\n");
		msleep(ALLOC_RETRY_DELAY_MIN);
		return true;
	} else {
		heap_trace(&hc->heap, "precheck for %d failed\n", nr);
		dump_heap_handles(heap);
		return false;
	}
}

static struct smem_handle* smem_cma_allocate(struct smem_heap *heap,
		struct device *dev,
		size_t size, size_t align, smem_flags_t flags,
		void *data)
{
	struct smem_heap_cma *hc = to_cmaheap(heap);
	struct smem_handle *handle;
	struct smem_chunk *chunk;
	int bit;
	const int nr = BYTES_TO_UNITS(hc, size);
	phys_addr_t alloc_addr;

	if (!check_alloc_unit(hc, size, align))
		return false;

	mutex_lock(&hc->lock);

	/* find in in-use chunk list first */
	bit = find_and_alloc(hc, &hc->chunks, nr, true, &chunk);
	if (bit < 0) {
		/* fallback, free queue */
		bit = find_and_alloc(hc, &hc->chunks_freeq, nr, true, &chunk);
	}

	if (bit >= 0) {
		/* reclaim a chunk back to in-use queue */
		if (!(chunk->flags & CHUNK_INUSE)) {
			reclaim_chunk(hc, chunk);
		}

		alloc_addr = chunk->paddr + (bit << UNIT_SHIFT(hc));
	} else {
		trigger_alloc_work(hc, 0);
	}

	mutex_unlock(&hc->lock);

	if (bit >= 0) {
		heap_trace(&hc->heap, "alloc %d/%d on %pa(0x%lx) %s\n", nr, bit, &chunk->paddr, chunk->flags, smem_test_name(data));
		return heap_create_handle(dev, heap,
			alloc_addr, nr << UNIT_SHIFT(hc),
			chunk, data);
	} else {
		heap_trace(&hc->heap, "alloc %d fails %d/%d\n %s", nr, hc->nr_chunks, hc->nr_chunks_freeq, smem_test_name(data));
		return NULL;
	}
}

static int smem_cma_free(struct smem_handle *handle)
{
	struct smem_heap_cma *hc = to_cmaheap(handle->heap);
	struct smem_chunk *chunk = handle->heap_priv;

	int pos = (handle->dma_addr - chunk->paddr) >> UNIT_SHIFT(hc);
	int nr = handle->length >> UNIT_SHIFT(hc);
	int cur, thres = FREEQ_THRESHOLD(hc);
	int cur_alloc;

	/* FIXME: check handle sanity */
	mutex_lock(&hc->lock);

	cur  = bitmap_weight(chunk->bits, BITMAP_SIZE(hc));
	bitmap_clear(chunk->bits, pos, nr);

	heap_trace(&hc->heap, "free %d@%d on %pa(0x%lx) weight=%d %s\n",
		nr, pos, &chunk->paddr, chunk->flags, cur - nr, smem_test_name(handle->priv));

	/* move a chunk to free queue if allocation ratio is under the threshold */
	cur_alloc = cur - nr;
	if (cur_alloc <= thres) {
		if (chunk->flags & CHUNK_INUSE)
			move_to_freeq(hc, chunk, cur_alloc);
		if (!cur_alloc)
			reserve_free_chunk(chunk);		
	}

	mutex_unlock(&hc->lock);

	return 0;
}

struct smem_heap* smem_heap_cmapool_create(struct smem *smem,
		const struct smem_platform_heap *pheap)
{
	struct smem_heap_cma *hc = NULL;
	static const char *heap_name = "cma";

	hc = devm_kzalloc(smem->dev, sizeof(*hc), GFP_KERNEL);
	if (!hc)
		return ERR_PTR(-ENOMEM);

	init_heap(smem, heap_name,  &hc->heap, pheap, SMEMFLAGS_SHARED_SYSTEM);
	hc->heap.allocate	= smem_cma_allocate;
	hc->heap.free		= smem_cma_free;
	hc->heap.precheck	= smem_cma_precheck;
	
	hc->cma = pheap->priv;
	mutex_init(&hc->lock);
	hc->delay_alloc = ALLOC_RETRY_DELAY_MIN;
	INIT_DELAYED_WORK(&hc->alloc_work, cma_alloc_worker);
	INIT_DELAYED_WORK(&hc->free_work, cma_free_worker);

	INIT_LIST_HEAD(&hc->chunks);
	INIT_LIST_HEAD(&hc->chunks_freeq);

	trigger_alloc_work(hc, 0);

	return &hc->heap;
}

void smem_heap_cmapool_destroy(struct smem_heap *sheap)
{
	// TODO
}
