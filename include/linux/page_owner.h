#ifndef __LINUX_PAGE_OWNER_H
#define __LINUX_PAGE_OWNER_H

#ifdef CONFIG_PAGE_OWNER
extern bool page_owner_inited;
extern struct page_ext_operations page_owner_ops;

extern void __reset_page_owner(struct page *page, unsigned int order);
extern void __set_page_owner(struct page *page,
			unsigned int order, gfp_t gfp_mask);
extern void __split_page_owner(struct page *page, unsigned int order);
extern void __copy_page_owner(struct page *oldpage, struct page *newpage);

static inline void reset_page_owner(struct page *page, unsigned int order)
{
	if (likely(!page_owner_inited))
		return;

	__reset_page_owner(page, order);
}

static inline void set_page_owner(struct page *page,
			unsigned int order, gfp_t gfp_mask)
{
	if (likely(!page_owner_inited))
		return;

	__set_page_owner(page, order, gfp_mask);
}

static inline void split_page_owner(struct page *page, unsigned int order)
{
	if (likely(!page_owner_inited))
		return;

	__split_page_owner(page, order);
}
static inline void copy_page_owner(struct page *oldpage, struct page *newpage)
{
	if (likely(!page_owner_inited))
		return;

	__copy_page_owner(oldpage, newpage);
}
#else
static inline void reset_page_owner(struct page *page, unsigned int order)
{
}
static inline void set_page_owner(struct page *page,
			unsigned int order, gfp_t gfp_mask)
{
}
static inline void split_page_owner(struct page *page,
			unsigned int order)
{
}
static inline void copy_page_owner(struct page *oldpage, struct page *newpage)
{
}
#endif /* CONFIG_PAGE_OWNER */
#endif /* __LINUX_PAGE_OWNER_H */
