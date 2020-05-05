#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>

static inline int page_ref_count(struct page *page)
{
	return atomic_read(&page->_refcount);
}

static inline int page_count(struct page *page)
{
	return atomic_read(&compound_head(page)->_refcount);
}

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_refcount, v);
}

#ifdef CONFIG_CMA_DEBUG_REFTRACE
#define MAX_TRACE_REFCOUNT 8
extern void record_page_ref(struct page* page, int ref, unsigned long *ep, bool type);
extern bool is_cma_page(struct page* page);
#define REFTRACE_BT_ENTRIES 5

static inline void is_trace_page_count(struct page *page, bool inc)
{
	struct stack_trace trace;
	int i = 0;
	unsigned long ip;
	unsigned long trace_entries[REFTRACE_BT_ENTRIES];

	if(is_cma_page(page) &&
			page_ref_count(page) <= MAX_TRACE_REFCOUNT) {   
		trace.nr_entries = 0;
		trace.max_entries = REFTRACE_BT_ENTRIES;
		trace.entries = &trace_entries[0];
		trace.skip = 0;
		save_stack_trace(&trace);

		record_page_ref(page, page_ref_count(page), trace_entries, inc);

		return;
	}
	return;
}

static inline void tracing_page_count(struct page *page, bool inc)
{
	is_trace_page_count(page, inc);
}
#else
static inline void tracing_page_count(struct page *page, bool inc) {}
#endif /* CONFIG_CMA_DEBUG_REFTRACE */


/*
 * Setup the page count before being freed into the page allocator for
 * the first time (boot or memory hotplug)
 */
static inline void init_page_count(struct page *page)
{
	set_page_count(page, 1);
}

static inline void page_ref_add(struct page *page, int nr)
{
	atomic_add(nr, &page->_refcount);
	tracing_page_count(page, true); 

}

static inline void page_ref_sub(struct page *page, int nr)
{
	atomic_sub(nr, &page->_refcount);
	tracing_page_count(page, false); 
}

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_refcount);
	tracing_page_count(page, true);
}

static inline void page_ref_dec(struct page *page)
{
	atomic_dec(&page->_refcount);
	tracing_page_count(page, false);
}

static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	int ret = atomic_sub_and_test(nr, &page->_refcount);
	tracing_page_count(page, false);
	return ret;
}

static inline int page_ref_dec_and_test(struct page *page)
{
	int ret = atomic_dec_and_test(&page->_refcount);
	tracing_page_count(page, false);
	return ret;
}

static inline int page_ref_inc_return(struct page *page)
{
	int ret = atomic_inc_return(&page->_refcount);
	tracing_page_count(page, true);
	return ret;
}

static inline int page_ref_dec_return(struct page *page)
{
	int ret = atomic_dec_return(&page->_refcount);
	tracing_page_count(page, false);
	return ret;
}

static inline int page_ref_add_unless(struct page *page, int nr, int u)
{
	int ret = atomic_add_unless(&page->_refcount, nr, u);
	if (ret) {
		tracing_page_count(page, true); 
	}
	return ret;
}

static inline int page_ref_freeze(struct page *page, int count)
{
	int ret = likely(atomic_cmpxchg(&page->_refcount, count, 0) == count);
    if (ret) {
        tracing_page_count(page, false); 
	}
	return ret; 
}

static inline void page_ref_unfreeze(struct page *page, int count)
{
	VM_BUG_ON_PAGE(page_count(page) != 0, page);
	VM_BUG_ON(count == 0);

	atomic_set(&page->_refcount, count);
   if (count >= 2) {
       tracing_page_count(page, false); 
	}
}

#endif
