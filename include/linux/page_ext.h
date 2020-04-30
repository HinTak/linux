#ifndef __LINUX_PAGE_EXT_H
#define __LINUX_PAGE_EXT_H

#include <linux/types.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>

struct pglist_data;
struct page_ext_operations {
	bool (*need)(void);
	void (*init)(void);
};

#ifdef CONFIG_PAGE_EXTENSION

/*
 * page_ext->flags bits:
 *
 * PAGE_EXT_DEBUG_POISON is set for poisoned pages. This is used to
 * implement generic debug pagealloc feature. The pages are filled with
 * poison patterns and set this flag after free_pages(). The poisoned
 * pages are verified whether the patterns are not corrupted and clear
 * the flag before alloc_pages().
 *
 * Maximum number of possible flags is 12. For more flags, the variable
 * reserved_flags needs to be increased.
 */

enum page_ext_flags {
	PAGE_EXT_DEBUG_POISON,		/* Page is poisoned */
	PAGE_EXT_DEBUG_GUARD,
	PAGE_EXT_OWNER,
	PAGE_EXT_FALLBACK,
#ifdef CONFIG_CMA_DEBUG_REFTRACE
	PAGE_EXT_MIGRATE_FAIL,
#endif
	PAGE_EXT_FLAGS_MAX = 12		/* Maximum flags = 12*/
};

/*
 * Page Extension can be considered as an extended mem_map.
 * A page_ext page is associated with every page descriptor. The
 * page_ext helps us add more information about the page.
 * All page_ext are allocated at boot or memory hotplug event,
 * then the page_ext for pfn always exists.
 */
struct page_ext {
#ifndef CONFIG_PAGE_OWNER
	unsigned long flags;
#else
	union {
		unsigned long flags;
		struct {
			/* First 12 bits reserved */
			unsigned int reserved_flags:12;
			unsigned int order:4;
			unsigned int pid:16;
		};
	};
	gfp_t gfp_mask;
	depot_stack_handle_t handle;
#endif
};

extern void pgdat_page_ext_init(struct pglist_data *pgdat);

#ifdef CONFIG_SPARSEMEM
static inline void page_ext_init_flatmem(void)
{
}
extern void page_ext_init(void);
#else
extern void page_ext_init_flatmem(void);
static inline void page_ext_init(void)
{
}
#endif

struct page_ext *lookup_page_ext(struct page *page);

#else /* !CONFIG_PAGE_EXTENSION */
struct page_ext;

static inline void pgdat_page_ext_init(struct pglist_data *pgdat)
{
}

static inline struct page_ext *lookup_page_ext(struct page *page)
{
	return NULL;
}

static inline void page_ext_init(void)
{
}

static inline void page_ext_init_flatmem(void)
{
}
#endif /* CONFIG_PAGE_EXTENSION */
#endif /* __LINUX_PAGE_EXT_H */
