#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/bootmem.h>
#include <linux/stacktrace.h>
#include <linux/page_owner.h>
#include <linux/stackdepot.h>

#include "internal.h"

static bool page_owner_disabled;
bool page_owner_inited __read_mostly;

static depot_stack_handle_t dummy_handle;
static depot_stack_handle_t failure_handle;
static depot_stack_handle_t early_handle;
#ifdef CONFIG_KERNEL_STACK_SMALL
static depot_stack_handle_t stackoverflow_handle;
#endif

static void init_early_allocated_pages(void);

static int early_page_owner_param(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (strcmp(buf, "on") == 0)
		page_owner_disabled = false;

	return 0;
}
early_param("page_owner", early_page_owner_param);

static bool need_page_owner(void)
{
	if (page_owner_disabled)
		return false;

	return true;
}

static __always_inline depot_stack_handle_t create_dummy_stack(void)
{
	unsigned long entries[5];
	struct stack_trace dummy;

	dummy.nr_entries = 0;
	dummy.max_entries = ARRAY_SIZE(entries) - 1;
	dummy.entries = &entries[0];
	dummy.skip = 0;

	save_stack_trace(&dummy);
	dummy.entries[dummy.nr_entries] = 0;
	dummy.nr_entries++;
	return depot_save_stack(&dummy, GFP_KERNEL);
}

static noinline void register_dummy_stack(void)
{
	dummy_handle = create_dummy_stack();
}

static noinline void register_failure_stack(void)
{
	failure_handle = create_dummy_stack();
}

static noinline void register_early_stack(void)
{
	early_handle = create_dummy_stack();
}

#ifdef CONFIG_KERNEL_STACK_SMALL
static noinline void register_overflow_stack(void)
{
	stackoverflow_handle = create_dummy_stack();
}
#else
static void register_overflow_stack(void) {}
#endif

static void init_page_owner(void)
{
	if (page_owner_disabled)
		return;

	register_dummy_stack();
	register_failure_stack();
	register_early_stack();
	register_overflow_stack();
	page_owner_inited = true;
	init_early_allocated_pages();
}

struct page_ext_operations page_owner_ops = {
	.need = need_page_owner,
	.init = init_page_owner,
};

void __reset_page_owner(struct page *page, unsigned int order)
{
	int i;
	struct page_ext *page_ext;

	for (i = 0; i < (1 << order); i++) {
		page_ext = lookup_page_ext(page + i);
		__clear_bit(PAGE_EXT_OWNER, &page_ext->flags);
		if (test_bit(PAGE_EXT_FALLBACK, &page_ext->flags)) {
			count_vm_events(PG_LOWMEM_FALLBACK, -1);
			__clear_bit(PAGE_EXT_FALLBACK, &page_ext->flags);
		}
#ifdef CONFIG_CMA_DEBUG_REFTRACE
		if (!i && test_bit(PAGE_EXT_MIGRATE_FAIL, &page_ext->flags)){
			__clear_bit(PAGE_EXT_MIGRATE_FAIL, &page_ext->flags);
		}	
#endif
	}

}

static inline bool check_recursive_alloc(struct stack_trace *trace,
					unsigned long ip)
{
	int i;

	if (!trace->nr_entries)
		return false;

	for (i = 0; i < trace->nr_entries; i++) {
		if (trace->entries[i] == ip)
			return true;
	}

	return false;
}

static noinline depot_stack_handle_t save_stack(gfp_t flags)
{
	unsigned long entries[PAGE_OWNER_STACK_DEPTH + 1];
	struct stack_trace trace = {
		.nr_entries = 0,
		.entries = entries,
		.max_entries = PAGE_OWNER_STACK_DEPTH,
		.skip = 2
	};
	depot_stack_handle_t handle;

#ifdef CONFIG_KERNEL_STACK_SMALL
	if (unlikely(current_thread_info()->flags & _TIF_VMAP_STACK_ALLOC))
		return stackoverflow_handle;
#endif

	save_stack_trace(&trace);

	/*
	 * We need to check recursion here because our request to stackdepot
	 * could trigger memory allocation to save new entry. New memory
	 * allocation would reach here and call depot_save_stack() again
	 * if we don't catch it. There is still not enough memory in stackdepot
	 * so it would try to allocate memory again and loop forever.
	 */
	if (check_recursive_alloc(&trace, _RET_IP_))
		return dummy_handle;

	filter_irq_stacks(&trace);
	if (trace.nr_entries != 0 &&
	    trace.entries[trace.nr_entries-1] == ULONG_MAX)
		trace.nr_entries--;

	trace.entries[trace.nr_entries] = flags;
	trace.nr_entries++;

	handle = depot_save_stack(&trace, flags);
	if (!handle)
		handle = failure_handle;

	return handle;
}

static inline void __set_page_owner_handle(struct page_ext *page_ext,
	depot_stack_handle_t handle, unsigned int order)
{
	page_ext->handle = handle;
	page_ext->order = order;
	page_ext->pid = current->pid;

	__set_bit(PAGE_EXT_OWNER, &page_ext->flags);
}

noinline void __set_page_owner(struct page *page, unsigned int order,
					gfp_t gfp_mask)
{
	struct page_ext *page_ext = lookup_page_ext(page);
	depot_stack_handle_t handle;

	if (unlikely(!page_ext))
		return;

	handle = save_stack(gfp_mask);
	__set_page_owner_handle(page_ext, handle, order);
}

#ifdef CONFIG_CMA_DEBUG
#define can_be_read_fs(p) ((p)->mapping != NULL &&  \
	(p)->mapping->host != NULL && \
	(unsigned long)(p)->mapping->host > (unsigned long)PAGE_OFFSET && \
	(p)->mapping->host->i_sb != NULL && \
	(p)->mapping->host->i_sb->s_type != NULL)

#define BUF_SIZE	 512
void migrate_failed_page_dump(struct page *page, char* reason)
{
	unsigned long pfn = page_to_pfn(page);
	int pageblock_mt, page_mt;
	struct stack_trace trace;
	depot_stack_handle_t handle;
	int ret = 0;
	struct page_ext *page_ext;
	char kbuf[BUF_SIZE];
	gfp_t gfp_mask;

	if (!page_owner_inited)
		return;
	page_ext = lookup_page_ext(page);

	/*
	 * Some pages could be missed by concurrent allocation or free,
	 * because we don't hold the zone lock.
	 */
	if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
		return;

	handle = READ_ONCE(page_ext->handle);
	if (!handle) {
		pr_alert("page_owner info is not active (free page?)\n");
		return;
	}

	depot_fetch_stack(handle, &trace);
	trace.nr_entries--;
	gfp_mask = trace.entries[trace.nr_entries];

	/* Print information relevant to grouping pages by mobility */
	pageblock_mt = get_pfnblock_migratetype(page, pfn);
	page_mt  = gfpflags_to_migratetype(gfp_mask);

	ret = snprintf(kbuf, BUF_SIZE,
			"PFN %lx Block %lu type %d %s "
			"Flags %s%s%s%s%s%s%s%s%s%s%s%s%s%s(0x%lx)\n"
			"PID %d\n",
			pfn,
			pfn >> pageblock_order,
			pageblock_mt,
			pageblock_mt != page_mt ? "Fallback" : "        ",
			PageLocked(page)	? "K" : " ",
			PageError(page)		? "E" : " ",
			PageReferenced(page)	? "R" : " ",
			PageUptodate(page)	? "U" : " ",
			PageDirty(page)		? "D" : " ",
			PageLRU(page)		? "L" : " ",
			PageActive(page)	? "A" : " ",
			PageSlab(page)		? "S" : " ",
			PageWriteback(page)	? "W" : " ",
			PageCompound(page)	? "C" : " ",
			PageSwapCache(page)	? "B" : " ",
			PageMappedToDisk(page)	? "M" : " ",
			page_has_private(page)	? "P" : " ",
			PageReclaim(page) ? "W" : " ",
			page->flags,
			page_ext->pid);
	ret += snprint_stack_trace(kbuf + ret, BUF_SIZE - ret, &trace, 0);

	pr_alert("[cma] %s page:0x%p [count:%d, mapping:%p, mapcount:%d index:%lu private:%lu fs:%s]\n"
			"Page allocated via order %u, mask 0x%x\n %s\n",
			reason, page, page_ref_count(page), READ_ONCE(page->mapping),
			atomic_read(&page->_mapcount), page->index, page_private(page),
			can_be_read_fs(page) ? page->mapping->host->i_sb->s_type->name : "",
			page_ext->order, gfp_mask,  kbuf);
}
#endif

void __split_page_owner(struct page *page, unsigned int order)
{
	int i;
	struct page_ext *page_ext = lookup_page_ext(page);

	if (unlikely(!page_ext))
		return;

	page_ext->order = 0;
	for (i = 1; i < (1 << order); i++)
		__copy_page_owner(page, page + i);

}

void __copy_page_owner(struct page *oldpage, struct page *newpage)
{
	struct page_ext *old_ext = lookup_page_ext(oldpage);
	struct page_ext *new_ext = lookup_page_ext(newpage);

	new_ext->order = old_ext->order;
	new_ext->pid = current->pid;
	new_ext->handle = old_ext->handle;

	/*
	 * We don't clear the bit on the oldpage as it's going to be freed
	 * after migration. Until then, the info can be useful in case of
	 * a bug, and the overal stats will be off a bit only temporarily.
	 * Also, migrate_misplaced_transhuge_page() can still fail the
	 * migration and then we want the oldpage to retain the info. But
	 * in that case we also don't need to explicitly clear the info from
	 * the new page, which will be freed.
	 */
	__set_bit(PAGE_EXT_OWNER, &new_ext->flags);

}
#ifdef CONFIG_DEBUG_PAGEALLOC_PRINT_OWNER
ssize_t
print_one_page_owner( size_t count, unsigned long pfn,
                struct page *page, struct page_ext *page_ext)
{
        int ret;
        int pageblock_mt, page_mt;
        char *kbuf;
	struct stack_trace trace;
	gfp_t gfp_mask;

        depot_stack_handle_t handle = READ_ONCE(page_ext->handle);
        if (!handle)
	{
		printk(KERN_ERR"%s handle is NULL!\n",__FUNCTION__);
		return -EFAULT;
	}

        kbuf = kmalloc(count, GFP_KERNEL);
        if (!kbuf)
                return -ENOMEM;

	depot_fetch_stack(handle, &trace);
	trace.nr_entries--;
	gfp_mask = trace.entries[trace.nr_entries];

        ret = snprintf(kbuf, count,
                        "Page allocated via order %u, mask 0x%x\n",
			page_ext->order, gfp_mask);

        if (ret >= count)
                goto err;

        /* Print information relevant to grouping pages by mobility */
        pageblock_mt = get_pfnblock_migratetype(page, pfn);
	page_mt  = gfpflags_to_migratetype(gfp_mask);
        ret += snprintf(kbuf + ret, count - ret,
                        "PFN %lu Block %lu type %d %s Flags %s%s%s%s%s%s%s%s%s%s%s%s\n"
                        "PID %d\n",
                        pfn,
                        pfn >> pageblock_order,
                        pageblock_mt,
                        pageblock_mt != page_mt ? "Fallback" : "        ",
                        PageLocked(page)        ? "K" : " ",
                        PageError(page)         ? "E" : " ",
                        PageReferenced(page)    ? "R" : " ",
                        PageUptodate(page)      ? "U" : " ",
                        PageDirty(page)         ? "D" : " ",
                        PageLRU(page)           ? "L" : " ",
                        PageActive(page)        ? "A" : " ",
                        PageSlab(page)          ? "S" : " ",
                        PageWriteback(page)     ? "W" : " ",
                        PageCompound(page)      ? "C" : " ",
                        PageSwapCache(page)     ? "B" : " ",
                        PageMappedToDisk(page)  ? "M" : " ",
                        page_ext->pid);

        if (ret >= count)
                goto err;

        ret += snprint_stack_trace(kbuf + ret, count - ret, &trace, 0);
        if (ret >= count)
                goto err;

        ret += snprintf(kbuf + ret, count - ret, "\n");
        if (ret >= count)
                goto err;

	printk(KERN_ERR"%s\n",kbuf);

        kfree(kbuf);
        return ret;

err:
        kfree(kbuf);
        return -ENOMEM;
}

#endif

static ssize_t
print_page_owner(char __user *buf, size_t count, unsigned long pfn,
		struct page *page, struct page_ext *page_ext,
		depot_stack_handle_t handle)
{
	int ret;
	int pageblock_mt, page_mt;
	char *kbuf;
	struct stack_trace trace;
	gfp_t gfp_mask;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	depot_fetch_stack(handle, &trace);
	trace.nr_entries--;
	gfp_mask = trace.entries[trace.nr_entries];

	ret = snprintf(kbuf, count,
			"Page allocated via order %u, mask 0x%x\n",
			page_ext->order, gfp_mask);

	if (ret >= count)
		goto err;

	/* Print information relevant to grouping pages by mobility */
	pageblock_mt = get_pfnblock_migratetype(page, pfn);
	page_mt  = gfpflags_to_migratetype(gfp_mask);
	ret += snprintf(kbuf + ret, count - ret,
			"PFN %lu Block %lu type %d %s Flags %s%s%s%s%s%s%s%s%s%s%s%s\n"
			"PID %d\n",
			pfn,
			pfn >> pageblock_order,
			pageblock_mt,
			pageblock_mt != page_mt ? "Fallback" : "        ",
			PageLocked(page)	? "K" : " ",
			PageError(page)		? "E" : " ",
			PageReferenced(page)	? "R" : " ",
			PageUptodate(page)	? "U" : " ",
			PageDirty(page)		? "D" : " ",
			PageLRU(page)		? "L" : " ",
			PageActive(page)	? "A" : " ",
			PageSlab(page)		? "S" : " ",
			PageWriteback(page)	? "W" : " ",
			PageCompound(page)	? "C" : " ",
			PageSwapCache(page)	? "B" : " ",
			PageMappedToDisk(page)	? "M" : " ",
			page_ext->pid);

	if (ret >= count)
		goto err;

	ret += snprint_stack_trace(kbuf + ret, count - ret, &trace, 0);
	if (ret >= count)
		goto err;

	ret += snprintf(kbuf + ret, count - ret, "\n");
	if (ret >= count)
		goto err;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;

err:
	kfree(kbuf);
	return -ENOMEM;
}

static ssize_t
read_page_owner(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;
	depot_stack_handle_t handle;

	if (!page_owner_inited)
		return -EINVAL;

	page = NULL;
	pfn = min_low_pfn + *ppos;

	/* Find a valid PFN or the start of a MAX_ORDER_NR_PAGES area */
	while (!pfn_valid(pfn) && (pfn & (MAX_ORDER_NR_PAGES - 1)) != 0)
		pfn++;

	drain_all_pages(NULL);

	/* Find an allocated page */
	for (; pfn < max_pfn; pfn++) {
		/*
		 * If the new page is in a new MAX_ORDER_NR_PAGES area,
		 * validate the area as existing, skip it if not
		 */
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		/* Check for holes within a MAX_ORDER area */
		if (!pfn_valid_within(pfn))
			continue;

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = page_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = lookup_page_ext(page);

		/*
		 * Some pages could be missed by concurrent allocation or free,
		 * because we don't hold the zone lock.
		 */
		if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
			continue;

		/*
		 * Access to page_ext->handle isn't synchronous so we should
		 * be careful to access it.
		 */
		handle = READ_ONCE(page_ext->handle);
		if (!handle)
			continue;

		/* Record the next PFN to read in the file offset */
		*ppos = (pfn - min_low_pfn) + 1;
		return print_page_owner(buf, count, pfn, page,
				page_ext, handle);


	}

	return 0;
}

static void init_pages_in_zone(pg_data_t *pgdat, struct zone *zone)
{
	struct page *page;
	struct page_ext *page_ext;
	unsigned long pfn = zone->zone_start_pfn, block_end_pfn;
	unsigned long end_pfn = pfn + zone->spanned_pages;
	unsigned long count = 0;

	/* Scan block by block. First and last block may be incomplete */
	pfn = zone->zone_start_pfn;

	/*
	 * Walk the zone in pageblock_nr_pages steps. If a page block spans
	 * a zone boundary, it will be double counted between zones. This does
	 * not matter as the mixed block count will still be correct
	 */
	for (; pfn < end_pfn; ) {
		if (!pfn_valid(pfn)) {
			pfn = ALIGN(pfn + 1, MAX_ORDER_NR_PAGES);
			continue;
		}

		block_end_pfn = ALIGN(pfn + 1, pageblock_nr_pages);
		block_end_pfn = min(block_end_pfn, end_pfn);

		page = pfn_to_page(pfn);

		for (; pfn < block_end_pfn; pfn++) {
			if (!pfn_valid_within(pfn))
				continue;

			page = pfn_to_page(pfn);

			/*
			 * We are safe to check buddy flag and order, because
			 * this is init stage and only single thread runs.
			 */
			if (PageBuddy(page)) {
				pfn += (1UL << page_order(page)) - 1;
				continue;
			}

			if (PageReserved(page))
				continue;

			page_ext = lookup_page_ext(page);

			/* Maybe overlaping zone */
			if (test_bit(PAGE_EXT_OWNER, &page_ext->flags))
				continue;

			/* Found early allocated page */
			__set_page_owner_handle(page_ext, early_handle, 0);
			count++;
		}
	}

	pr_info("Node %d, zone %8s: page owner found early allocated %lu pages\n",
		pgdat->node_id, zone->name, count);
}

static void init_zones_in_node(pg_data_t *pgdat)
{
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		init_pages_in_zone(pgdat, zone);
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}

static void init_early_allocated_pages(void)
{
	pg_data_t *pgdat;

	drain_all_pages(NULL);
	for_each_online_pgdat(pgdat)
		init_zones_in_node(pgdat);
}

static const struct file_operations proc_page_owner_operations = {
	.read		= read_page_owner,
};

static int __init pageowner_init(void)
{
	struct dentry *dentry;

	if (!page_owner_inited) {
		pr_info("page_owner is disabled\n");
		return 0;
	}

	dentry = debugfs_create_file("page_owner", S_IRUSR, NULL,
			NULL, &proc_page_owner_operations);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	return 0;
}
module_init(pageowner_init)
