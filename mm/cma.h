#ifndef __MM_CMA_H__
#define __MM_CMA_H__

#ifdef CONFIG_GCMA
#include <linux/gcma.h>

#define IS_GCMA ((struct gcma *)(void *)0xFF)
#endif

struct cma {
	unsigned long   base_pfn;
	unsigned long   count;
	unsigned long   *bitmap;
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	struct mutex    lock;
	struct list_head map_dev_list;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
#endif
#ifdef CONFIG_CMA_DEBUG
	struct list_head buffers_list;
	struct mutex	list_lock;
#endif

#ifdef CONFIG_GCMA
	struct gcma	*gcma;
#endif
};

struct cma_dev_map {
	struct device *dev;
	struct list_head list;
};

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned cma_area_count;
#ifdef CONFIG_CMA_DEBUG
extern struct cma *dma_contiguous_default_area;
#endif

static unsigned long cma_bitmap_maxno(struct cma *cma)
{
	return cma->count >> cma->order_per_bit;
}

extern unsigned long cma_get_used_pages(struct cma *cma);
extern unsigned long cma_get_maxchunk_pages(struct cma *cma);
#endif
