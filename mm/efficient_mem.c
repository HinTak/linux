/*
 * efficient_mem.c - Multiple purpose Memory management for efficient (called EMEM)
 *
 * EMEM implementation is based on GCMA written by SeongJae Park to use dmem interface.
 * EMEM aims for contiguous memory allocation with success and fast
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
 * withdrawing, EMEM restricts only easily discardable memory could be used for
 * second-class clients.
 *
 * To support various second-class clients, EMEM provides interface and
 * backend of discardable memory. Any candiates satisfying with discardable
 * memory could be second-class client of GCMA using the interface.
 *
 * Currently, EMEM uses cleancache and write-through mode frontswap as
 * second-class clients.
 *
 * Copyright (C) 2019  Samsung Electronics Inc.,
 * Copyright (C) 2019  Jusun song <jsunsong85@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/efficient_mem.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/migrate.h>

#define BITS_FOR_NOD	(BITS_PER_LONG - 1)

struct emem {
	unsigned long flags;
	spinlock_t lock;
	unsigned long *bitmap;
	union {
		unsigned long base_pfn;
		struct page** pages;
	};
	unsigned long size;
	struct list_head list;
};

struct emem_info {
	spinlock_t lock;	/* protect list */
	struct list_head head;
	struct list_head pend_list;
};

	

static struct emem_info einfo = {
	.head = LIST_HEAD_INIT(einfo.head),
	.pend_list = LIST_HEAD_INIT(einfo.pend_list),
	.lock = __SPIN_LOCK_UNLOCKED(einfo.lock),
};


/* For statistics */
static atomic_t emem_undiscard_nr_pages = ATOMIC_INIT(0);
static atomic_t emem_discard_nr_pages = ATOMIC_INIT(0);
static atomic_t emem_reserved_pages = ATOMIC_INIT(0);
static atomic_t emem_undiscard_alloc_nr = ATOMIC_INIT(0);
static atomic_t emem_discard_alloc_nr = ATOMIC_INIT(0);


int epage_flag(struct page *page, int flag)
{
	return page->private & flag;
}

static void set_epage_flag(struct page *page, int flag)
{
	page->private |= flag;
}

static void clear_epage_flag(struct page *page, int flag)
{
	page->private &= ~flag;
}

static void clear_epage_flagall(struct page *page)
{
	page->private = 0;
}

/*
 * UNCONTIGUOUS emem page should be freed by emem_free_page or 
 * emem_free_bitmap if page offset that know what index of store in bitmap
 */
void emem_free_page(struct page *page)
{
	struct emem *emem;
	unsigned long pfn, offset, *bitmap;
	unsigned long flags;
	BUG_ON(epage_flag(page, EF_CONTIGUOUS));

	offset = page->index;
	rcu_read_lock();
	list_for_each_entry_rcu(emem, &einfo.head, list) {
		local_irq_save(flags);
		spin_lock(&emem->lock);

		if (emem->flags & EMEM_NONCONTIGUOUS && offset < emem->size ) {
			if (page == emem->pages[offset]) {
				bitmap = &emem->bitmap[0] + offset / BITS_PER_LONG;
				if (test_bit(offset & BITS_FOR_NOD, bitmap)) { 
					bitmap_clear(&emem->bitmap[0], offset, 1);
					atomic_dec(&emem_undiscard_alloc_nr);
					clear_epage_flag(page, EF_ALLOC);
					spin_unlock(&emem->lock);
					local_irq_restore(flags);
					rcu_read_unlock();
					return;
				}
			}
		}
		spin_unlock(&emem->lock);
		local_irq_restore(flags);
	}
	rcu_read_unlock();
	/*there's no page in emem, if reach here*/
	WARN(1,"page:%p  pfn:%lx is invalid scope in emem\n",page, page_to_pfn(page));
	dump_page(page, "emem: invalid page is freeing");
	BUG_ON(1);
}

/*
 * emem_register - initializes a contiguous memory area
 *
 * @start_pfn	start pfn of contiguous memory area
 * @size	number of pages in the contiguous memory area
 * @res_emem	pointer to store the created emem region
 *
 * Returns 0 on success, error code on failure.
 */
int emem_register_contig(unsigned long start_pfn, unsigned long size, unsigned long eflags,
		struct emem **res_emem)
{
	int bitmap_size = BITS_TO_LONGS(size) * sizeof(long);
	struct emem *emem;
	unsigned long flags;
	struct page *page;
	int i;

	if (!start_pfn || !size)
		return -EINVAL;

	if (!slab_is_available()) {
		pr_err("%s is failure due to slab is unavailable (pfn:%lx sz:%lx)\n",
			 __func__, start_pfn, size);
		return -EINVAL;
	}

	pr_err("Register emem base:%lx size %lx\n",	start_pfn, size);

	emem = kmalloc(sizeof(*emem), GFP_KERNEL);
	if (!emem)
		goto out;

	emem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!emem->bitmap)
		goto free_emem;

	emem->flags = (EMEM_CONTIGUOUS | eflags);
	emem->size = size;
	emem->base_pfn = start_pfn;
	spin_lock_init(&emem->lock);
	for (i = 0;i < size;i++) {
		struct page *page = pfn_to_page(start_pfn + i);
		if (!PageReserved(page)) 
			SetPageReserved(page);
		if (!list_empty(&page->lru)) 
			list_del_init(&page->lru);
	
		clear_epage_flagall(page);
		set_epage_flag(page, EF_CONTIGUOUS);
	}

	atomic_set(&emem_undiscard_nr_pages, size);
	
	local_irq_save(flags);
	spin_lock(&einfo.lock);
	list_add_tail_rcu(&emem->list, &einfo.head);
	spin_unlock(&einfo.lock);
	local_irq_restore(flags);

	*res_emem = emem;
	pr_info("%s initialized emem area [%lx, %lx]\n",
			__func__, start_pfn, start_pfn + size);
	return 0;

free_emem:
	kfree(emem);
out:
	return -ENOMEM;
}

int emem_register_noncontig(struct page** pages, unsigned long count, unsigned long eflags,
		struct emem **res_emem)
{
	int bitmap_size = BITS_TO_LONGS(count) * sizeof(long), i;
	struct emem *emem;
	unsigned long flags;
	
	if (!pages || !count)
		return -EINVAL;

	if (!slab_is_available()) {
		pr_err("%s is failure due to slab is unavailable count:%lu\n",
			 __func__, count);
		return -EINVAL;
	}

	emem = kmalloc(sizeof(*emem), GFP_KERNEL);
	if (!emem)
		goto out;

	emem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!emem->bitmap)
		goto free_emem;

	emem->pages = kzalloc(sizeof(struct page *)*count, GFP_KERNEL);
	if (!emem->pages)
		goto free_bitmap;

	emem->flags = (EMEM_NONCONTIGUOUS | eflags);

	emem->size = count;
	for (i = 0;i < count;i++) {
		struct page *page = pages[i];
		if (page->index || page->private) { 
			pr_err("Register emem index:%d page:%p is not reserved\n",i, page);
			dump_page(page,"invalid index and private");
		}
		clear_epage_flagall(page);
		page->index = i;
		if (!list_empty(&page->lru)) {
			INIT_LIST_HEAD(&page->lru);
		}
	}

	spin_lock_init(&emem->lock);

	atomic_set(&emem_discard_nr_pages, count);
	local_irq_save(flags);
	spin_lock(&einfo.lock);
	list_add(&emem->list, &einfo.head);
	spin_unlock(&einfo.lock);
	local_irq_restore(flags);

	*res_emem = emem;
	pr_info("%s initialized emem area size:[%lu]\n",
			 __func__, count);
	return 0;
free_bitmap:
	kfree(emem->bitmap);
free_emem:
	kfree(emem);
out:
	return -ENOMEM;
}

/* allocate page from emem */
static struct page *emem_alloc_page(struct emem *emem, unsigned long start)
{
	unsigned long bit;
	unsigned long *bitmap = &emem->bitmap[0];
	struct page *page = NULL;

	spin_lock(&emem->lock);
	bit = bitmap_find_next_zero_area(bitmap, emem->size, start, 1, 0);
	if (bit >= emem->size) {
		spin_unlock(&emem->lock);
		goto out;
	}

	bitmap_set(bitmap, bit, 1);
	atomic_inc(&emem_undiscard_alloc_nr);
	if (emem->flags & EMEM_CONTIGUOUS)
		page = pfn_to_page(emem->base_pfn + bit);
	else {
		page = emem->pages[bit];
		BUG_ON(!page);
	}
	spin_unlock(&emem->lock);
	set_epage_flag(page, EF_ALLOC); 
out:
	return page;
}

static inline int is_emem_scope_range(struct emem *emem, unsigned long pfn) 
{
	return (emem->base_pfn <= pfn && pfn < (emem->base_pfn + emem->size));
}

int emem_alloc_bitmaps(struct emem *emem, unsigned long eflags, unsigned long base_offset, unsigned long count) 
{
	unsigned long *bitmap;
	struct page *page;
	unsigned long flags;
	unsigned long bitmap_no;

	if (!emem || !count) 
		return -EINVAL;

	if (emem->size < count ||
			emem->size < (base_offset + count))
		return -EINVAL;

	local_irq_save(flags);
	spin_lock(&emem->lock);

	bitmap_no = bitmap_find_next_zero_area_off(&emem->bitmap[0],
			emem->size, base_offset, count, 0, 0);
	if (bitmap_no >= emem->size) {
		pr_err("[error] %s allocation failed start_pfn:%lx count:%lx bitmap_no:%lx\n",
				__func__, emem->base_pfn + base_offset, count, bitmap_no);
		bitmap_no = -ENOMEM;
		goto unlock;
	}

	bitmap_set(&emem->bitmap[0], bitmap_no, count); 	
	if (eflags & EMEM_CONTIGUOUS)
		atomic_add(count, &emem_reserved_pages);
	else
		atomic_add(count, &emem_discard_alloc_nr);
unlock:
	spin_unlock(&emem->lock);
	local_irq_restore(flags);

	return bitmap_no;
}
/*
 * Return number of pages allocated.
 * Otherwise, return 0.
 * pages are set on pages arguments.
 */
int emem_alloc_pages(unsigned long count, unsigned long eflags, struct page **pages)
{
	struct page *page = NULL;
	struct emem *emem;
	unsigned long flags;
	unsigned long nr_pages = 0;

	local_irq_save(flags);
	rcu_read_lock();
	list_for_each_entry_rcu(emem, &einfo.head, list) {
		/* can allocate from type of CRASH_ON_USE emem for vmalloc */
		if (!(emem->flags & EMEM_CRASHONUSE))
			continue;
		do {
			page = emem_alloc_page(emem, 0);
			if (page) {
				pages[nr_pages++] = page;
				if (nr_pages == count) {
					goto got;
				}
			}
		} while (page != NULL);
	}
got:
	rcu_read_unlock();
	local_irq_restore(flags);
	return nr_pages;
}

/*
 * emem_reserve_contig - reserve contiguous pages
 *
 * @start_pfn	start pfn of requiring contiguous memory area
 * @size	size of the requiring contiguous memory area
 *
 * Returns 0 on success, error code  on failure.
 */

static int emem_alloc_nonbase(struct emem *emem, unsigned long count) 
{
	int pfn = -ENOMEM;
	if (!emem) 
		return -EINVAL; 

	spin_lock(&emem->lock);
	pfn = bitmap_find_next_zero_area_off(&emem->bitmap[0],
			emem->size, 0, count, 0, 0);

	if (pfn >= emem->size) {
		spin_unlock(&emem->lock);	
		return -ENOMEM;
	}

	atomic_add(count, &emem_reserved_pages);
	bitmap_set(&emem->bitmap[0], pfn, count);
	spin_unlock(&emem->lock);
	return pfn;
}

/*
 * emem_alloc_contig - allocates contiguous pages
 *
 * @start_pfn	start pfn of requiring contiguous memory area
 * @count	count of the requiring contiguous memory area
 *
 * Returns pfn number on success, 0 on failure.
 */

unsigned long emem_alloc_contig(unsigned long start_pfn, unsigned long count)
{
	struct emem *emem;
	unsigned long pfn = 0;
	unsigned long flags = 0;
	int ret;

	if (!count)
		return 0;

	local_irq_save(flags);
	rcu_read_lock();
	list_for_each_entry_rcu(emem, &einfo.head, list) {
		if (emem->flags & EMEM_NONCONTIGUOUS)  
			continue;

		if (start_pfn == EMEM_NONE_BASE_PFN) {	/* allocate from any emem */
			ret = emem_alloc_nonbase(emem, count);
		} else { /*allocate from particular emem from start_pfn*/
			if (is_emem_scope_range(emem, start_pfn)) {
				ret = emem_alloc_bitmaps(emem, EMEM_CONTIGUOUS, start_pfn - emem->base_pfn, count); 
			}
		}
		if (ret >= 0) {
			pfn = emem->base_pfn + ret;
			break;
		}
	}
	rcu_read_unlock();
	local_irq_restore(flags);

	return pfn;
}

int emem_free_bitmap(struct emem* emem, unsigned long eflags, unsigned long base_offset, unsigned long size) 
{
	int ret = 0;
	unsigned long *bitmap;
	unsigned long flags, offset;
	local_irq_save(flags);
	spin_lock(&emem->lock);
	for (offset = base_offset; offset <( base_offset + size); offset++) {
		int set;

		bitmap = (unsigned long *)&emem->bitmap[0] + offset / BITS_PER_LONG;

		set = test_bit(offset & BITS_FOR_NOD, bitmap);
		if (!set) {
			ret = -EINVAL;
			break;
		}
	}
	if (!ret) {
		bitmap_clear(&emem->bitmap[0], base_offset, size); 	
		if (eflags & EMEM_CRASHONUSE) {
			if (size == 1)
				atomic_sub(size, &emem_undiscard_alloc_nr);
			else 
				atomic_sub(size, &emem_reserved_pages);
		}	else
			atomic_sub(size, &emem_discard_alloc_nr);
	}
	spin_unlock(&emem->lock);
	local_irq_restore(flags);

	return ret;
}

/*
 * emem_free_contig - free allocated contiguous pages
 *
 * @pfn	start pfn of freeing contiguous memory area
 * @size	number of pages in freeing contiguous memory area
 */
void emem_free_contig(unsigned long pfn, unsigned long size)
{
	struct emem *emem;
	unsigned long offset;

	if (!size)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(emem, &einfo.head, list) {
		if ((emem->flags & EMEM_CRASHONUSE) &&
			 is_emem_scope_range(emem, pfn)) {
			if (emem->flags & EMEM_CONTIGUOUS)
				offset = pfn - emem->base_pfn;
			else
				offset = pfn;

			if (emem_free_bitmap(emem, EMEM_CRASHONUSE, offset, size)) {
				WARN(1,"pfn:%lx size:%lu is not set in bitmap\n", pfn, size);
				dump_page(pfn_to_page(pfn), "emem: invalid page is freeing");
				BUG_ON(1);
			}
		} 
	}
	rcu_read_unlock();
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *emem_debugfs_root;

static int __init emem_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	emem_debugfs_root = debugfs_create_dir("emem", NULL);
	if (!emem_debugfs_root)
		return -ENOMEM;

	debugfs_create_atomic_t("undiscard_nr_pages", S_IRUGO,
			emem_debugfs_root, &emem_undiscard_nr_pages);
	debugfs_create_atomic_t("discard_nr_pages", S_IRUGO,
			emem_debugfs_root, &emem_discard_nr_pages);
	debugfs_create_atomic_t("undiscard_alloc_nr", S_IRUGO,
			emem_debugfs_root, &emem_undiscard_alloc_nr);
	debugfs_create_atomic_t("discard_alloc_nr", S_IRUGO,
			emem_debugfs_root, &emem_discard_alloc_nr);
	debugfs_create_atomic_t("reserved_pages", S_IRUGO,
			emem_debugfs_root, &emem_reserved_pages);

	pr_info("emem debufs init\n");
	return 0;
}
#else
static int __init emem_debugfs_init(void)
{
	return 0;
}
#endif

static int __init init_emem(void)
{
	pr_info("loading emem\n");
	emem_debugfs_init();
	return 0;
}

module_init(init_emem);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jusun Song <jsunsong85@gmail.com>");
MODULE_DESCRIPTION("Efficient Memory to use multiple purpose");
