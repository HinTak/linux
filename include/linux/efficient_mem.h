/*
 * efficient_mem.h - Effcient memory allocator that allocate from RAMDUMP memory
 *
 * GCMA aims for contiguous memory allocation with success and fast
 * latency guarantee.
 * It reserves large amount of memory and let it be allocated to
 * contiguous memory requests. Because system memory space efficiency could be
 * degraded if reserved area being idle, GCMA let the reserved area could be
 * used by other clients with lower priority.
 * We call those lower priority clients as second-class clients. In this
 * context, contiguous memory requests are first-class clients, of course.
 *
 * With this idea, emem withdraw pages being used for second-class clients and
 * gives them to first-class clients if they required. Because latency
 * and success of first-class clients depend on speed and availability of
 * withdrawing, GCMA restricts only easily discardable memory could be used for
 * second-class clients.
 *
 * To support various second-class clients, GCMA provides interface and
 * backend of discardable memory. Any candiates satisfying with discardable
 * memory could be second-class client of GCMA using the interface.
 *
 * Currently, GCMA uses cleancache and write-through mode frontswap as
 * second-class clients.
 *
 * Copyright (C) 2019  Samsung Electronics Inc.,
 * Copyright (C) 2019  Jusun Song <jsunsong85@gmail.com>
 */

#ifndef _LINUX_EMEM_H
#define _LINUX_EMEM_H

#include <linux/types.h>
#include <linux/mmzone.h>

struct emem;
enum emem_flags {
	__EMEM_CRASH_ON_USE,  /* This is special memory that's using on panic(crash) such as Ramdump */
	__EMEM_DISCARDABLE,	  /* This is discadable memory if memory allocation request invoke such as stackdepot, emergency pool */
	__EMEM_CONTIGUOUS,	 
	__EMEM_NONCONTIGUOUS,
};
#define EMEM_CRASHONUSE (1 << __EMEM_CRASH_ON_USE)
#define EMEM_DISCARDABLE (1 << __EMEM_DISCARDABLE)
#define EMEM_CONTIGUOUS (1 << __EMEM_CONTIGUOUS)
#define EMEM_NONCONTIGUOUS (1 << __EMEM_NONCONTIGUOUS)

#define EMEM_NONE_BASE_PFN (-1UL)

/*
 * Flags for status of a page in emem
 *
 * EF_LRU
 * The page is being used for a dmem and hang on LRU list of the dmem.
 * It could be discarded for contiguous memory allocation easily.
 * Protected by dmem->lru_lock.
 *
 * EF_RECLAIMING
 * The page is being discarded for contiguous memory allocation.
 * It should not be used for dmem anymore.
 * Protected by dmem->lru_lock.
 *
 * EF_ISOLATED
 * The page is isolated from dmem.
 * EMEM clients can use the page safely while dmem should not.
 * Protected by emem->lock.
 */
enum epage_flags {
	EF_LRU = 0x1,			/* LRU is indicate that page is use for frontswap */
	EF_RECLAIMING = 0x2,	/* Reclaiming is discard frontswap memory for allocation */
	EF_ISOLATED = 0x4,		/* if EF_ISOLATE set, page couldn't assign for frontswap */
	EF_ALLOC = 0x8,			/* if EF_ALLOC set, page is allocated by owner of registery */
	EF_CONTIGUOUS = 0x10,   /* if EF_CONTIGUOUS set, page is from contiguous emem. */
};


#define emem_free_one(p) \
	BUG_ON(!epage_flag(p, EF_ALLOC));	\
	if (epage_flag(p, EF_CONTIGUOUS))  { \
		emem_free_contig(page_to_pfn(p), 1); \
	} else { 	\
		emem_free_page(p); 	\
	}	

#ifndef CONFIG_EMEM
static inline int emem_alloc_pages(unsigned long pg_count,
		unsigned long eflags, struct page **pages) 
{
	return -ENOMEM; 
}

static inline int emem_alloc_bitmaps(struct emem *emem, unsigned long eflags,
		unsigned long base_offset, unsigned long count) 
{
	return 0;
} 
static inline unsigned long emem_alloc_contig(unsigned long start_pfn, unsigned long count) 
{
	return 0;
} 
static inline int epage_flag(struct page *page, int flag) 
{
	return 1;
}

static inline void emem_free_contig(unsigned long pfn, unsigned long pg_count) {}
static inline void emem_free_page(struct page *page) {}
static inline int emem_free_bitmap(struct emem* emem, unsigned long eflags, 
		unsigned long base_offset, unsigned long size) 
{ 
	return 0;
}
#else

/*
 * The APIs regiter memories range to emem system.
 */
int emem_register_contig(unsigned long start_pfn, unsigned long pg_count, unsigned long eflags,
	      struct emem **res_emem);
int emem_register_noncontig(struct page** pages, unsigned long count, unsigned long eflags,
		struct emem **res_emem);

//! allocate memory APIs from emem if regitery.
int emem_alloc_pages(unsigned long pg_count, unsigned long eflags, struct page **pages);
int emem_alloc_bitmaps(struct emem *emem, unsigned long eflags, unsigned long base_offset, unsigned long count); 
unsigned long emem_alloc_contig(unsigned long start_pfn, unsigned long count);

//! free emem APIs 
void emem_free_contig(unsigned long pfn, unsigned long pg_count);
void emem_free_page(struct page *page);
int emem_free_bitmap(struct emem* emem, unsigned long eflags, 
		unsigned long base_offset, unsigned long size); 


int epage_flag(struct page *page, int flag); 

#endif

#endif /* _LINUX_EMEM_H */
