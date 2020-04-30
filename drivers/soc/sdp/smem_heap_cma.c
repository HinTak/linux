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

#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>

#include <soc/sdp/smem.h>

#include "smem_priv.h"

struct smem_heap_cma {
	struct smem_heap	heap;
	struct cma		*cma;
	unsigned int		nr_alloc;
	unsigned int		nr_max;
};

#define to_cmaheap(h)		((struct smem_heap_cma *)h)

#define PATH_MAXLEN  64
#define PATH_MEMINFO "/proc/meminfo"
#define PATH_CMAINFO_FMT "/sys/kernel/debug/cma/cma-%d/cmainfo"
#define PRINT_FILE_MAXBUF 0x1000

static int print_line(char *buf, size_t size) {
	int pos = 0;
	char c;

	for(pos = 0; pos < size; pos++) {
		if(buf[pos] == '\n') {
			pos++;/* add '\n' char */
			c = buf[pos];
			buf[pos] = '\0';
			break;
		}
	}

	printk(buf);

	/* restore null */
	if(pos < size) {
		buf[pos] = c;
	}

	return pos;
}

static int print_file(const char *filename)
{
	int ret = 0;
	int fd;
	char *buf = NULL;
	mm_segment_t old_fs = get_fs();

	buf = kmalloc(PRINT_FILE_MAXBUF, GFP_KERNEL);
	if(buf == NULL) {
		return -ENOMEM;
	}

	set_fs(KERNEL_DS);

	fd = sys_open(filename, O_RDONLY, 0);
	if (fd >= 0) {
		int read_maxloop = 100;
		long count;

		pr_err("### print_file \"%s\"\n", filename);
		do {
			count = sys_read(fd, buf, PRINT_FILE_MAXBUF - 1);
			if(count > 0) {
				int printline_maxloop = PRINT_FILE_MAXBUF;
				int off = 0;

				buf[count] = '\0';
				do {
					off += print_line(buf + off, count - off);
				} while((off < count) && (--printline_maxloop > 0));
			} else {
				break;
			}
		} while(--read_maxloop > 0);
		pr_err("\n");
		sys_close(fd);
	} else {
		ret = fd;
		pr_err("### print_file open error(%d) \"%s\"\n", ret, filename);
	}

	set_fs(old_fs);
	kfree(buf);

	return ret;
}

static void print_meminfo_and_cmainfos(void) {
	int i;
	char path[PATH_MAXLEN + 1] = { 0, };

	/* Print meminfo */
	print_file(PATH_MEMINFO);

	/* Print cmdinfo 0 ~ n */
	for(i = 0; i < 100; i++) {
		snprintf(path, PATH_MAXLEN, PATH_CMAINFO_FMT, i);
		if(print_file(path) < 0) {
			break;
		}
	}
}

/* returns true if allocated chunks are over total limit */
static bool check_limit(struct smem_heap_cma *hc, int nr)
{
	return (hc->nr_max - hc->nr_alloc) >= nr;
}

static bool smem_cma_precheck(struct smem_heap *heap, size_t size, size_t align,
				smem_flags_t flags)
{
	struct smem_heap_cma *hc = to_cmaheap(heap);
	int nr = PFN_UP(size);
	
	return (nr <= (1 << heap->max_order)) && check_limit(hc, PFN_UP(size));
}

static struct page *vd_cma_alloc_retry(struct cma *cma, unsigned int count, unsigned int align) {
	struct page *pages;
	ktime_t t0, t1, tlatency;
	int try_count = 0;
	const u32 time_limit = 1500000;
	int try_per_time_limit = 0;
	s64 latency = 0;

	t0 = ktime_get();
	tlatency = t0;

retry:
	try_count++;
	pages = cma_alloc(cma, count, align);
	t1 = ktime_get();

	if (pages) {
		return pages;
	} else {
		if (fatal_signal_pending(current)) {
			pr_err("cma_alloc_retry: count %4u, align_order %u All %d retries failed!![fatal signal pending]  %09lldusec\n",
				count, align, try_count, ktime_us_delta(t1, t0));
			return NULL;
		}

		latency = ktime_us_delta(t1, tlatency);
		/* retry 5 times per 1.5 sec */
		if(try_per_time_limit <= 5) {
			if(unlikely(latency > time_limit)) {
				tlatency =  ktime_get();
				try_per_time_limit++;
			}
			goto retry;
		} else {
			phys_addr_t cma_base = cma_get_base(cma);

			pr_err("cma_alloc_retry: [Error]CMA_Alloc!! alloc count %4u, align_order %u All %d retries failed!!  %09lldusec\n",
				count, align, try_count, ktime_us_delta(t1, t0));
			pr_err("cma_alloc_retry: cma area: base=%pa, size=0x%lx\n",
				&cma_base, cma_get_size(cma));

			print_meminfo_and_cmainfos();

			panic("Critical CMA Alloc failure and couldn't recover system...");

			return NULL;
		}
	}
}

static struct smem_handle* smem_cma_allocate(struct smem_heap *heap,
		struct device *dev,
		size_t size, size_t align, smem_flags_t flags,
		void *data)
{
	struct smem_heap_cma *hc = to_cmaheap(heap);
	phys_addr_t alloc_addr;
	struct page *pages;
	unsigned int nr = PFN_UP(size);
	ktime_t t0, t1;

	t0 = ktime_get();
	pages = vd_cma_alloc_retry(hc->cma, nr, align?get_order(align):0);
	t1 = ktime_get();

	if (pages) {
		struct smem_handle* handle;
		
		alloc_addr = page_to_phys(pages);
		heap_trace(&hc->heap, "alloc %4d pages, %pa %09lldusec (%s)\n",
			nr, &alloc_addr, ktime_us_delta(t1, t0), smem_test_name(data));

		handle = heap_create_handle(dev, heap, alloc_addr, nr << PAGE_SHIFT, pages, data);
		if (!handle) {
			heap_err(&hc->heap, "create handle failed! %4d pages, %09lldusec (%s)\n",
				nr, ktime_us_delta(t1, t0), smem_test_name(data));
			cma_release(hc->cma, pages, nr);
		}
		else {
			hc->nr_alloc += nr;
		}

		return handle;
	} else {
		heap_err(&hc->heap, "alloc failed! %4d pages, %09lldusec (%s)\n",
			nr, ktime_us_delta(t1, t0), smem_test_name(data));

		return NULL;
	}
}

static int smem_cma_free(struct smem_handle *handle)
{
	struct smem_heap_cma *hc = to_cmaheap(handle->heap);
	struct page *pages = handle->heap_priv;
	unsigned int nr = PFN_DOWN(handle->length);
	
	heap_trace(&hc->heap, "free %4d(%d pages %s)", nr >> hc->heap.min_order, nr, smem_test_name(handle->priv));
	cma_release(hc->cma, pages, nr);
	BUG_ON(hc->nr_alloc < nr);
	hc->nr_alloc -= nr;	

	return 0;
}

struct smem_heap* smem_heap_cma_create(struct smem *smem,
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
	hc->nr_max = PFN_DOWN(hc->heap.size);

	return &hc->heap;
}

void smem_heap_cma_destroy(struct smem_heap *sheap)
{
	// TODO
}
