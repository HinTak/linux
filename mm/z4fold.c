/*
 * z4fold.c
 *
 * Author: Jusun song <jsun.song@samsung.com>
 * Copyright (C) 2018, Samsung electronic Visual Display Inc.
 *
 * This implementation is based on z3fold written by Vitaly Wool.
 *
 * z4fold is an special purpose allocator for storing compressed pages. It
 * can store up to three compressed pages per page which improves the
 * compression ratio of z3fold while retaining its main concepts (e. g. always
 * storing an integral number of objects per page) and simplicity.
 * It still has simple and deterministic reclaim properties that make it
 * preferable to a higher density approach (with no requirement on integral
 * number of object per page) when reclaim is used.
 *
 * As in zbud, pages are divided into "chunks".  The size of the chunks is
 * fixed at compile time and is determined by NCHUNKS_ORDER below.
 *
 * z4fold doesn't export any API and is meant to be used via zpool API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/preempt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#ifdef CONFIG_ZPOOL
#include <linux/zpool.h>
#else
#include <linux/z4fold.h>
#endif
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/highmem.h>
#include <asm/fixmap.h>

#define Z4FOLD_MAX 4
#define NCHUNKS_ORDER	8	

#define CHUNK_SHIFT	(PAGE_SHIFT - NCHUNKS_ORDER)
#define CHUNK_SIZE	(1 << CHUNK_SHIFT)
#define ZHDR_SIZE_ALIGNED round_up(sizeof(struct z4fold_header), CHUNK_SIZE)
#define ZHDR_CHUNKS	(ZHDR_SIZE_ALIGNED >> CHUNK_SHIFT)
#define TOTAL_CHUNKS	(PAGE_SIZE >> CHUNK_SHIFT)
#define NCHUNKS		((PAGE_SIZE - ZHDR_SIZE_ALIGNED) >> CHUNK_SHIFT)

#define BIG_CHUNK_GAP	3

#define BUDDY_MASK	(0x3)
#define ORPHAN_CHUNK 0
#define Z4FOLD_PHYS_MASK (1 << (PAGE_SHIFT -1))
#define HANDLE_SHIFT (PAGE_SHIFT - 1)
#define START_OFFSET(bud) (bud - 1)
#define IS_PRIOR_CHUNK(hdr) (!hdr->chunks[FIRST] || !hdr->chunks[LAST])
#define IS_MIDDLE_CHUNK(hdr) (hdr->chunks[SECOND] != 0 || hdr->chunks[THIRD] != 0)
#define IS_LOWMEM_AREA(kaddr) (((unsigned long)(kaddr) >= PAGE_OFFSET && (unsigned long)(kaddr) < (unsigned long)high_memory) \
								&& pfn_valid(virt_to_pfn(kaddr)))

/*****************
 * Structures
*****************/
struct z4fold_pool;
struct z4fold_ops {
	int (*evict)(struct z4fold_pool *pool, unsigned long handle);
};

enum buddy {
	FIRST = 0,
	SECOND = 1,
	THIRD = 2,
	LAST = 3,
	HEADLESS = 4,
	BUDDIES_MAX
};
enum z4fold_tag {
	HANDLE_FREE = BUDDIES_MAX - 1,
	HANDLE_PIN,
	HANDLE_CONTEND,
	HANDLE_TAG_MAX,
};

/*
 * struct z4fold_header - z4fold page metadata occupying first chunks of each
 *			z4fold page, except for HEADLESS pages
 * @buddy:		links the z4fold page into the relevant list in the
 *			pool
 * @page_lock:		per-page lock
 * @refcount:		reference count for the z4fold page
 * @work:		work_struct for page layout optimization
 * @pool:		pointer to the pool which this page belongs to
 * @cpu:		CPU which this page "belongs" to
 * @chunks[FIRST]:	the size of the first buddy in chunks, 0 if free
 * @middle_chunks:	the size of the middle buddy in chunks, 0 if free
 * @chunks[LAST]:	the size of the last buddy in chunks, 0 if free
 * @first_num:		the starting number (for the first handle)
 */
struct z4fold_header {
#ifdef CONFIG_Z4FOLD_SHRINK
	struct list_head buddy;
#endif
	struct kref refcount;
	struct z4fold_pool *pool;
	u8 chunks[Z4FOLD_MAX];
#if Z4FOLD_MAX > 2
	u8 start_off[Z4FOLD_MAX-2];
#endif
	unsigned short nr_alloc:3;
	unsigned short chunk_num:9;
	unsigned long handle[Z4FOLD_MAX];
#ifdef CONFIG_Z4FOLD_SHRINK
	short cpu;
#endif
};

/*
 * NCHUNKS_ORDER determines the internal allocation granularity, effectively
 * adjusting internal fragmentation.  It also determines the number of
 * freelists maintained in each pool. NCHUNKS_ORDER of 6 means that the
 * allocation granularity will be in chunks of size PAGE_SIZE/64. Some chunks
 * in the beginning of an allocated page are occupied by z4fold header, so
 * NCHUNKS will be calculated to 63 (or 62 in case CONFIG_DEBUG_SPINLOCK=y),
 * which shows the max number of free chunks in z4fold page, also there will
 * be 63, or 62, respectively, freelists per pool.
 */

struct z4fold_class {
	struct list_head list;
	spinlock_t lock;
};

/**
 * struct z4fold_pool - stores metadata for each z4fold pool
 * @name:	pool name
 * @lock:	protects pool unbuddied/lru lists
 * @stale_lock:	protects pool stale page list
 * @unbuddied:	per-cpu array of lists tracking z4fold pages that contain 2-
 *		buddies; the list each z4fold page is added to depends on
 *		the size of its free region.
 * @lru:	list tracking the z4fold pages in LRU order by most recently
 *		added buddy.
 * @stale:	list of pages marked for freeing
 * @pages_nr:	number of z4fold pages in the pool.
 * @ops:	pointer to a structure of user defined operations specified at
 *		pool creation time.
 * @compact_wq:	workqueue for page layout background optimization
 * @release_wq:	workqueue for safe page release
 * @work:	work_struct for safe page release
 *
 * This structure is allocated at pool creation time and maintains metadata
 * pertaining to a particular z4fold pool.
 */
struct z4fold_pool {
	const char *name;
	spinlock_t stale_lock;
#ifdef CONFIG_Z4FOLD_SHRINK
	spinlock_t lru_lock;
#endif
	spinlock_t compact_lock;
	struct z4fold_class *unbuddied;
	struct list_head lru;
	struct list_head stale;
	struct list_head compact_list;
	atomic64_t pages_nr;
	atomic64_t orphan_nr;
	atomic64_t stale_nr;
	atomic64_t headless_nr;
	const struct z4fold_ops *ops;
#ifdef CONFIG_ZPOOL
	struct zpool *zpool;
	const struct zpool_ops *zpool_ops;
#endif
	struct kmem_cache *handle_cachep;
	struct workqueue_struct *release_wq;
	struct task_struct* compactd;
	wait_queue_head_t compact_wait;
	struct work_struct work;
	int need_wakeup;
};

/*
 * Internal z4fold page flags
 */
enum z4fold_page_flags {
	PAGE_HEADLESS = 0,
	PAGE_LOCK,
	NEEDS_COMPACTING,
	PAGE_STALE,
	PAGE_LAZY_FREE,
	PAGE_RECLAIM,
	PAGE_COMPACT_WORK,
};

struct z4fold_mapping_area {
    char *vm_addr; /* address of kmap_atomic()'ed pages */
};

static DEFINE_PER_CPU(struct z4fold_mapping_area, z4fold_map_area);

static int z4fold_cpu_notifier(struct notifier_block *nb, unsigned long action,
                void *pcpu)
{
    int ret, cpu = (long)pcpu;
    struct z4fold_mapping_area *area;

    switch (action) {
    case CPU_UP_PREPARE:
        area = &per_cpu(z4fold_map_area, cpu);
        break;
    case CPU_DEAD:
    case CPU_UP_CANCELED:
        area = &per_cpu(z4fold_map_area, cpu);
        break;
    }

    return NOTIFY_OK;
}

static struct notifier_block z4fold_cpu_nb = {
     .notifier_call = z4fold_cpu_notifier
 };
 
 static int z4fold_register_cpu_notifier(void)
 {
     int cpu, uninitialized_var(ret);
 
     cpu_notifier_register_begin();
 
     __register_cpu_notifier(&z4fold_cpu_nb);
     for_each_online_cpu(cpu) {
         ret = z4fold_cpu_notifier(NULL, CPU_UP_PREPARE, (void *)(long)cpu);
         if (notifier_to_errno(ret))
             break;
     }
 
     cpu_notifier_register_done();
     return notifier_to_errno(ret);
 }
 
 static void z4fold_unregister_cpu_notifier(void)
 {
     int cpu;
 
     cpu_notifier_register_begin();
 
     for_each_online_cpu(cpu)
         z4fold_cpu_notifier(NULL, CPU_DEAD, (void *)(long)cpu);
     __unregister_cpu_notifier(&z4fold_cpu_nb);
 
     cpu_notifier_register_done();
 }

/*****************
 * Helpers
*****************/
static int create_handle_cache(struct z4fold_pool *pool)
{
    pool->handle_cachep = kmem_cache_create("z4fold_handle", sizeof(unsigned long),
                    0, 0, NULL);
    return pool->handle_cachep ? 0 : 1;
}

static void destroy_handle_cache(struct z4fold_pool *pool)
{
    if (pool->handle_cachep)
        kmem_cache_destroy(pool->handle_cachep);
}

static unsigned long alloc_handle(struct z4fold_pool *pool, gfp_t gfp)
{
	return (unsigned long)kmem_cache_alloc(pool->handle_cachep,
			gfp & ~__GFP_HIGHMEM);
}

static void free_handle(struct z4fold_pool *pool, unsigned long handle)
{
    unsigned long *ptr = (unsigned long *)handle;
    kmem_cache_free(pool->handle_cachep, (void *)handle);
}

/* Converts an allocation size in bytes to size in z4fold chunks */
static int size_to_chunks(size_t size)
{
	return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
}

#define for_each_unbuddied_list(_iter, _begin) \
	for ((_iter) = (_begin); (_iter) < NCHUNKS; (_iter)++)

static void compact_page_work(struct work_struct *w);

/* Initializes the z4fold header of a newly allocated z4fold page */
static void init_z4fold_page(struct z4fold_header *zhdr,
							struct page *page, struct z4fold_pool *pool)
{
	int i = 0;

	INIT_LIST_HEAD(&page->lru);
	page->private = 0;

	kref_init(&zhdr->refcount);
	for (i = 0; i < Z4FOLD_MAX; i++)
	{
		zhdr->chunks[i] = 0;
		zhdr->handle[i] = 0;
	}
	zhdr->nr_alloc = 1;
	zhdr->chunk_num = NCHUNKS;
	zhdr->start_off[0] = zhdr->start_off[1] = 0;
	zhdr->pool = pool;

#ifdef CONFIG_Z4FOLD_SHRINK
	INIT_LIST_HEAD(&zhdr->buddy);
	zhdr->cpu = -1;
#endif
}

static inline void z4fold_unbuddy_add(struct z4fold_header* zhdr, struct page *page, unsigned short size)
{
	unsigned long flags;
	struct z4fold_class *sz_class;
	struct list_head *buddy;
#ifndef CONFIG_Z4FOLD_SHRINK
	buddy = &page->lru;
#else
	buddy = &zhdr->buddy;
#endif
	if (!list_empty(buddy)) {
		WARN(1,"%s page list is not empty[page:%p(n:%p|p:%p)|priv:%lx|zhdr:%p|nchunk:%d(f:%hd|s:%hd|t:%hd|l:%hd|nr_alloc:%hd)]\n",
                __func__, page, page->lru.next, page->lru.prev,  page->private, zhdr, zhdr->chunk_num, zhdr->chunks[FIRST], 
                zhdr->chunks[SECOND], zhdr->chunks[THIRD], zhdr->chunks[LAST], zhdr->nr_alloc);
		BUG_ON(1);
	}

	if (size >= NCHUNKS) {
		WARN(1,"%s sz_class size(%d) is bigger than NCHUNK\n",__func__, size);
		return;
	}

	sz_class = &zhdr->pool->unbuddied[size];
	/* the page's not completely free and it's unbuddied */
	spin_lock_irqsave(&sz_class->lock, flags);
	list_add_tail(buddy, &sz_class->list);
	zhdr->chunk_num = size;
	spin_unlock_irqrestore(&sz_class->lock, flags);
}

static inline void z4fold_unbuddy_del(struct z4fold_header* zhdr, struct page *page, unsigned short size)
{
	unsigned long flags;
	struct z4fold_class *sz_class;
	struct list_head* buddy;
#ifndef CONFIG_Z4FOLD_SHRINK
	buddy = &page->lru;
#else
	buddy = &zhdr->buddy;
#endif

	if (size >= NCHUNKS && 
		!list_empty(buddy)) {
		WARN(1,"%s sz_class chunk_num:%d but not emtpy buddy:%p",__func__, size, buddy);
		return;
	}

	sz_class = &zhdr->pool->unbuddied[size];

	if (list_empty(buddy))
		return;

	spin_lock_irqsave(&sz_class->lock, flags);
	list_del_init(buddy);
	zhdr->chunk_num = NCHUNKS;
	spin_unlock_irqrestore(&sz_class->lock, flags);
}

static inline void z4fold_compact_add(struct z4fold_header* zhdr, struct page *page)
{
	struct z4fold_pool *pool = zhdr->pool;
	unsigned long flags;
	struct list_head* buddy;
#ifndef CONFIG_Z4FOLD_SHRINK
	buddy = &page->lru;
#else
	buddy = &zhdr->buddy;
#endif

	if (WARN(!list_empty(buddy),"%s zhdr(%p) is not empty for compact\n",__func__, zhdr))
		return;

	spin_lock_irqsave(&pool->compact_lock, flags);
	list_add_tail(buddy, &pool->compact_list);
	spin_unlock_irqrestore(&pool->compact_lock, flags);
}

/* Resets the struct page fields and frees the page */
static void free_z4fold_page(struct page *page)
{
	__free_page(page);
}

static int free_headless_page(struct page *page)
{
	if (put_page_testzero(page)) {
#ifdef CONFIG_Z4FOLD_SHRINK
		if (!list_empty(&page->lru))
			list_del_init(&page->lru);
#endif
		free_hot_cold_page(page, false);
		return 1;
	}
	return 0;
}

static inline int trypin_tag(unsigned long handle)
{
    unsigned long *ptr = (unsigned long *)handle;
	return ptr && !test_and_set_bit(HANDLE_PIN, ptr);
}

static inline void pin_tag(unsigned long handle)
{
    while (!trypin_tag(handle));
}

static inline void unpin_tag(unsigned long handle)
{
    unsigned long *ptr = (unsigned long *)handle;
	if (handle)
	    clear_bit(HANDLE_PIN, ptr);
}
static inline void clear_handle_contents(unsigned long handle)
{
    unsigned long *ptr = (unsigned long *)handle;
	*ptr &= (1 << HANDLE_PIN);
}

static inline void mark_free(unsigned long handle)
{
	unsigned long *ptr = (unsigned long *)handle;
	set_bit(HANDLE_FREE, ptr);
}
static inline void mark_contend(unsigned long handle)
{
	unsigned long *ptr = (unsigned long *)handle;
	set_bit(HANDLE_CONTEND, ptr);
}

static inline void clear_contend(unsigned long handle)
{
	unsigned long *ptr = (unsigned long *)handle;
	test_and_clear_bit(HANDLE_CONTEND, ptr);
}

static inline int handle_is_free(unsigned long handle) 
{
	unsigned long *ptr = (unsigned long *)handle;
	if (handle) {
		return test_bit(HANDLE_FREE, ptr);
	}
	return 1;
}

static inline int handle_is_compactable(unsigned long handle) 
{
	unsigned long *ptr = (unsigned long *)handle;
	if (handle) {
		return !handle_is_free(handle) && !test_bit(HANDLE_CONTEND, ptr);
	}
	return 0;
}

static inline int handle_try_pin(struct z4fold_header* zhdr, enum buddy bud) 
{
	return trypin_tag(READ_ONCE(zhdr->handle[bud]));
}

/* Lock a z4fold page */
static inline void z4fold_page_lock(struct page *page)
{
	bit_spin_lock(PAGE_LOCK, &page->private);
}

/* Try to lock a z4fold page */
static inline int z4fold_page_trylock(struct page *page)
{
	return bit_spin_trylock(PAGE_LOCK, &page->private);
}

/* Unlock a z4fold page */
static inline void z4fold_page_unlock(struct page *page)
{
	bit_spin_unlock(PAGE_LOCK, &page->private);
}

/*
 * Encodes the handle of a particular buddy within a z4fold page
 * Pool lock should be held as this function accesses first_num
 */
static unsigned long encode_handle(struct page *page, enum buddy bud)
{
	unsigned long handle;

	handle = page_to_pfn(page) << HANDLE_SHIFT;
	if (bud != HEADLESS)
		handle |= bud & BUDDY_MASK;
	return handle;
}

/* Returns the z4fold page where a given handle is stored */
static unsigned long handle_to_pfn(unsigned long handle)
{
	return (handle >> HANDLE_SHIFT);
}

/*
 * (handle & BUDDY_MASK) < zhdr->first_num is possible in encode_handle
 *  but that doesn't matter. because the masking will result in the
 *  correct buddy number.
 */
static enum buddy handle_to_buddy(unsigned long handle)
{
	return handle & BUDDY_MASK;
}

static enum buddy closest_buddy(struct z4fold_header *zhdr, enum buddy target_bud, int direct) 
{
	int i = target_bud;

	while (1) {
		if (zhdr->chunks[i])
			return i;

		(direct == 0) ?	i-- : i++;

		if (i < FIRST || i > LAST)
			break;
	} 
	return BUDDIES_MAX;
}

static u8 bud_end_offset(struct z4fold_header* zhdr, enum buddy bud) 
{
	switch (bud) {
	case FIRST:
		return ZHDR_CHUNKS + zhdr->chunks[bud];
	case SECOND:
	case THIRD:
		return zhdr->start_off[START_OFFSET(bud)] + zhdr->chunks[bud];
	case LAST:
		return TOTAL_CHUNKS - zhdr->chunks[bud];
	default:
		WARN_ON(1);
		return 0;
	}
}

static u8 bud_start_offset(struct z4fold_header* zhdr, enum buddy bud) 
{
	switch (bud) {
	case FIRST:
		return ZHDR_CHUNKS;
	case SECOND:
	case THIRD:
		return zhdr->start_off[START_OFFSET(bud)];
	case LAST:
		return TOTAL_CHUNKS - zhdr->chunks[bud];
	default:
		WARN_ON(1);
		return 0;
	}

}

static enum buddy prior_fit_chunk(struct z4fold_header *zhdr, u8 chunk_sz) 
{
	unsigned short free = 0;
	if (!zhdr->chunks[LAST]) {
		enum buddy closest_bud = closest_buddy(zhdr, LAST - 1, 0);
		free = TOTAL_CHUNKS - bud_end_offset(zhdr, closest_bud);
		if (chunk_sz <= free)
			return LAST;
	}

	if (!zhdr->chunks[FIRST]) {
		enum buddy closest_bud = closest_buddy(zhdr, FIRST + 1, 1);
		free = bud_start_offset(zhdr, closest_bud) - ZHDR_CHUNKS;
		if (chunk_sz <= free)
			return FIRST;
	 }
	return BUDDIES_MAX;
}

static inline u8 z4fold_offset_location(struct z4fold_header *zhdr, enum buddy bud)
{
		switch (bud) 
		{
			case FIRST:
				return ZHDR_CHUNKS;
			case SECOND:
				return	ZHDR_CHUNKS + zhdr->chunks[bud-1]; 
			case THIRD:
				return zhdr->start_off[bud - 2] + zhdr->chunks[bud - 1];
			default:
				WARN(1,"%s invalid bud:%hd \n",__func__, bud);
				BUG_ON(1);
				return 0;
		}
}

static void z4fold_set_chunks(struct z4fold_header *zhdr, u8 chunks, enum buddy bud)
{
	zhdr->chunks[bud] = chunks;

	if (bud == SECOND || bud == THIRD) {
		zhdr->start_off[START_OFFSET(bud)] = (chunks) ? 
			z4fold_offset_location(zhdr, bud) : 0;
	} 
	if (WARN(zhdr->chunks[THIRD] && zhdr->chunks[LAST] && 
				zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD]	> TOTAL_CHUNKS - zhdr->chunks[LAST],
				"%s overwritten zhdr:%p bud:(%d - %hd) chunks:%hd\n", __func__, zhdr, bud, zhdr->chunks[bud], chunks)) BUG_ON(1);
}

/*
 * This function is for shirnker which will be added.
 */
/*
static void z4fold_migrate_header(struct z4fold_header *zhdr,
	 enum buddy src, enum buddy dst) 
{
	unsigned long *obj = (unsigned long*)zhdr->handle[src];
	zhdr->handle[src] = 0;
	zhdr->chunks[dst] = zhdr->chunks[src];
	clear_handle_contents((unsigned long)obj);
	*obj |= encode_handle(zhdr, dst);
	zhdr->handle[dst] = (unsigned long)obj;
    unpin_tag((unsigned long)obj);
    z4fold_set_chunks(zhdr, 0, src);
}
*/

static inline int z4fold_compact_gap(struct z4fold_header *zhdr, enum buddy dst, enum buddy src) 
{
	BUG_ON(dst == LAST);
	BUG_ON(dst >= src || src != dst + 1 );
	if (dst == FIRST)
		return zhdr->start_off[src - 1] - (zhdr->chunks[dst] + ZHDR_CHUNKS) >= BIG_CHUNK_GAP;
	else 
		return zhdr->start_off[src - 1] - ( zhdr->start_off[dst - 1] + zhdr->chunks[dst]) >= BIG_CHUNK_GAP;
}
static inline unsigned short z4fold_total_chunks(struct z4fold_header *zhdr)
{
	unsigned short sum = 0, i;
	for (i=0;i<Z4FOLD_MAX;i++)
		sum += zhdr->chunks[i];
	return sum;
}

static inline int z4fold_allocatable(struct z4fold_header *zhdr)
{
	int i = 0;
	for (i=0;i<Z4FOLD_MAX;i++)
		if (!zhdr->chunks[i])
			return 1;
	return 0;
}

static void* z4fold_address(struct z4fold_header* zhdr, enum buddy bud) 
{
	unsigned char *addr = (unsigned char *)zhdr;
	switch (bud) {
	case FIRST:
		addr += ZHDR_SIZE_ALIGNED;
		break;
	case SECOND:
	case THIRD:
		addr += zhdr->start_off[START_OFFSET(bud)] << CHUNK_SHIFT;
		break;
	case LAST:
		addr += PAGE_SIZE - (zhdr->chunks[bud] << CHUNK_SHIFT);
		break;
	default:
		pr_err("unknown buddy id %d\n", bud);
		pr_err("zhdr:%p [f(%hd) m(%hd) l(%hd) nr_alloc:%hd]\n",
			  zhdr, zhdr->chunks[FIRST], zhdr->chunks[SECOND],
			 zhdr->chunks[LAST], zhdr->nr_alloc);
		WARN_ON(1);
		addr = NULL;
		break;
	}

	return addr;
}

static void __release_z4fold_page(struct z4fold_header *zhdr, struct page *page, bool locked)
{
	struct z4fold_pool *pool = zhdr->pool;
	struct list_head* buddy;
#ifndef CONFIG_Z4FOLD_SHRINK
	buddy = &page->lru;
#else
	buddy = &zhdr->buddy;
#endif

	WARN_ON(!list_empty(buddy));

	set_bit(PAGE_STALE, &page->private);
	clear_bit(NEEDS_COMPACTING, &page->private);

#ifdef  CONFIG_Z4FOLD_SHRINK
	if (bit_spin_trylock(PAGE_RECLAIM, &page->private)) {
		spin_lock(&pool->lru_lock);
		if (!list_empty(&page->lru))
			list_del_init(&page->lru);
		spin_unlock(&pool->lru_lock);
		bit_spin_unlock(PAGE_RECLAIM, &page->private);
	} 
	else { 
		set_bit(PAGE_LAZY_FREE, &page->private);
	}
#endif

	if (locked)
		z4fold_page_unlock(page);

	atomic64_inc(&pool->stale_nr);
	spin_lock(&pool->stale_lock);
	list_add(buddy, &pool->stale);
	queue_work(pool->release_wq, &pool->work);
	spin_unlock(&pool->stale_lock);
}

#ifdef CONFIG_Z4FOLD_SHRINK
static void __attribute__((__unused__))
			release_z4fold_page(struct kref *ref)
{
	struct z4fold_header *zhdr = container_of(ref, struct z4fold_header,
						refcount);
	__release_z4fold_page(zhdr, false);
}
#endif

struct page *z4fold_zhdr_to_page(void *kvaddr)
{
	struct page *page = NULL;
	unsigned long vaddr = (unsigned long)kvaddr & PAGE_MASK;

	if (IS_LOWMEM_AREA(kvaddr)) {
		page = virt_to_page(kvaddr);
	} else {
		if (kvaddr >= (void *)FIXADDR_START) {
			page = kmap_atomic_to_page(kvaddr);
		} else if (vaddr >= PKMAP_ADDR(0) && vaddr < PKMAP_ADDR(LAST_PKMAP)) {
			/* this address was obtained through kmap_high_get() */
			page = pte_page(pkmap_page_table[PKMAP_NR(vaddr)]);
		}
	} 

	BUG_ON(page == NULL);

	return page;
}

static void release_z4fold_page_locked(struct kref *ref)
{
	struct z4fold_header *zhdr = container_of(ref, struct z4fold_header,
						refcount);
	__release_z4fold_page(zhdr, z4fold_zhdr_to_page(zhdr), true);
}

static void release_z4fold_page_locked_list(struct kref *ref)
{
	struct z4fold_header *zhdr = container_of(ref, struct z4fold_header,
					       refcount);

	z4fold_unbuddy_del(zhdr, z4fold_zhdr_to_page(zhdr), zhdr->chunk_num);
	__release_z4fold_page(zhdr, z4fold_zhdr_to_page(zhdr), true);
}

/*
 * Returns the number of free chunks in a z4fold page.
 * NB: can't be used with HEADLESS pages.
 */
static unsigned short num_free_chunks(struct z4fold_header *zhdr)
{
	unsigned short nfree;
	/*
	 * If there is a middle object, pick up the bigger free space
	 * either before or after it. Otherwise just subtract the number
	 * of chunks occupied by the first and the last objects.
	 */
	if (zhdr->chunks[SECOND] != 0 && zhdr->chunks[THIRD] != 0) {
		unsigned short  nfree_before = zhdr->chunks[FIRST] ?
			0 : zhdr->start_off[START_OFFSET(SECOND)] - ZHDR_CHUNKS;
		unsigned short nfree_after = zhdr->chunks[LAST] ?
			0 : TOTAL_CHUNKS -
				(zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD]);
		nfree = max(nfree_before, nfree_after);
	} else if (zhdr->chunks[SECOND] != 0) {
		unsigned short nfree_before = zhdr->chunks[FIRST] ?
			0 : zhdr->start_off[START_OFFSET(SECOND)] - ZHDR_CHUNKS;
		unsigned short nfree_after =  
			TOTAL_CHUNKS - (zhdr->start_off[START_OFFSET(SECOND)] + zhdr->chunks[SECOND]) - zhdr->chunks[LAST];
		nfree = max(nfree_before, nfree_after);

	} else if (zhdr->chunks[THIRD] != 0) {
		unsigned short nfree_before =
			zhdr->start_off[START_OFFSET(THIRD)] - (ZHDR_CHUNKS + zhdr->chunks[FIRST]);
		unsigned short nfree_after = zhdr->chunks[LAST] ?
			0 : TOTAL_CHUNKS -
				(zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD]);
		nfree = max(nfree_before, nfree_after);
	} else
		nfree = NCHUNKS - zhdr->chunks[FIRST] - zhdr->chunks[LAST];

	return nfree;
}

static enum buddy num_fit_chunk_idx(struct z4fold_header *zhdr,int chunk_size)
{
	enum buddy bud = BUDDIES_MAX;
	/*
	 * If there is a middle object, pick up the bigger free space
	 * either before or after it. Otherwise just subtract the number
	 * of chunks occupied by the first and the last objects.
	 */
	if (zhdr->chunks[SECOND] != 0 && zhdr->chunks[THIRD] != 0) {
		int nfree_before = zhdr->chunks[FIRST] ?
			0 : zhdr->start_off[START_OFFSET(SECOND)] - ZHDR_CHUNKS;
		int nfree_after = zhdr->chunks[LAST] ?
			0 : TOTAL_CHUNKS -
				(zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD]);
		
		if(nfree_before < chunk_size) {
			bud = LAST;
			WARN(chunk_size > nfree_after,"chunk_sz:%d is bigger"
			" than left chunk:%d", chunk_size, nfree_after);
		} else {
			bud = FIRST;
			WARN(chunk_size > nfree_before,"chunk_sz:%d is bigger"
			" than left chunk:%d", chunk_size, nfree_before);
		}
	} else if (zhdr->chunks[SECOND] != 0) {
		int nfree_before = zhdr->chunks[FIRST] ?
			0 : zhdr->start_off[START_OFFSET(SECOND)] - ZHDR_CHUNKS;
		int nfree_after = 
			TOTAL_CHUNKS - (zhdr->start_off[START_OFFSET(SECOND)] + zhdr->chunks[SECOND]) - zhdr->chunks[LAST];
		if (nfree_before >= chunk_size) {
			bud = FIRST;
		} else {
			if (zhdr->chunks[LAST])
				bud = THIRD;
			else
				bud = LAST;
			WARN(chunk_size > nfree_after,"chunk_sz:%d is bigger"
					" than left chunk:%d", chunk_size, nfree_after);
		}
		WARN(bud == BUDDIES_MAX, "There's no fitting chunk(%d)\n", chunk_size);
	} else if (zhdr->chunks[THIRD] != 0) {
		int nfree_before =
			zhdr->start_off[START_OFFSET(THIRD)] - ZHDR_CHUNKS - zhdr->chunks[FIRST];
		int nfree_after = zhdr->chunks[LAST] ?
			0 : TOTAL_CHUNKS -
				(zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD]);
		if (nfree_after >= chunk_size) {
			bud = LAST;
		} else {
			if(zhdr->chunks[FIRST]) {
				bud = SECOND;
			} else {
				bud = FIRST;
			}

			WARN(chunk_size > nfree_before,"chunk_sz:%d is bigger"
					" than left chunk:%d bud = %d", chunk_size, nfree_before, bud);
		}
	} else {
		if (zhdr->chunks[FIRST] && zhdr->chunks[LAST]) {
			bud = SECOND;
			WARN(zhdr->chunks[SECOND] || 
					chunk_size > ((zhdr->chunks[THIRD] ? 
							zhdr->start_off[THIRD] : TOTAL_CHUNKS - zhdr->chunks[LAST]) - (ZHDR_CHUNKS + zhdr->chunks[FIRST])),
					"chunk_sz:%d is bigger than left bud = %d", chunk_size, bud);

		} else if (zhdr->chunks[FIRST]) { 
			bud = LAST;	
			WARN(zhdr->chunks[LAST] || 
			chunk_size > (TOTAL_CHUNKS - (zhdr->start_off[START_OFFSET(THIRD)] + zhdr->chunks[THIRD])),
				"chunk_sz:%d is bigger than left bud = %d", chunk_size, bud);
		} else {
			bud = FIRST;
			WARN(chunk_size > (zhdr->start_off[START_OFFSET(SECOND)] - ZHDR_CHUNKS),
			   "chunk_sz:%d is bigger than left bud = %d", chunk_size, bud);
		}
	}

	WARN(bud > LAST,"chunk_sz:%d isn't fit bud = %d", chunk_size, bud);

	return bud;
}


static void free_pages_work(struct work_struct *w)
{
	struct z4fold_pool *pool = container_of(w, struct z4fold_pool, work);
	int stale_nr = atomic64_read(&pool->stale_nr);

	spin_lock(&pool->stale_lock);
	while (!list_empty(&pool->stale) && stale_nr-- > 0) {
		struct page * page;
		struct z4fold_header *zhdr;
		struct list_head* buddy;
#ifdef CONFIG_Z4FOLD_SHRINK
		zhdr = list_first_entry(&pool->stale,
						struct z4fold_header, buddy);
		page = virt_to_page(zhdr);
		buddy = &zhdr->buddy
#else
		page = list_first_entry(&pool->stale, struct page, lru);
		zhdr = kmap_atomic(page);
		buddy = &page->lru;
#endif
#ifdef CONFIG_Z4FOLD_SHRINK
		bool need_lru_lock = false;
	
		if (test_bit(PAGE_LAZY_FREE, &page->private)) 
			need_lru_lock = true;
#endif

		if(WARN_ON(!test_bit(PAGE_STALE, &page->private))) {
			kunmap_atomic(zhdr);
			continue;
		}

		if(WARN(zhdr->chunk_num != NCHUNKS,
			"Invalid page(%p) is freed[zhdr:%p chunk_num:%hd]\n", 
				page, zhdr, zhdr->chunk_num))
			BUG_ON(1);

		list_del(buddy);
		kunmap_atomic(zhdr);
		spin_unlock(&pool->stale_lock);
		
#ifdef CONFIG_Z4FOLD_SHRINK
		if (need_lru_lock) {
			if (!bit_spin_trylock(PAGE_RECLAIM, &page->private)) {
				spin_lock(&pool->stale_lock);
				list_add_tail(buddy, &pool->stale);
				continue;
			}
			spin_lock(&pool->lru_lock);
			list_del(&page->lru);
			spin_unlock(&pool->lru_lock);
			bit_spin_unlock(PAGE_RECLAIM, &page->private);
		}
#endif
		free_z4fold_page(page);

		atomic64_dec(&pool->stale_nr);

		cond_resched();
		spin_lock(&pool->stale_lock);
	}
	spin_unlock(&pool->stale_lock);
	
#ifdef CONFIG_Z4FOLD_SHRINK
	if (atomic64_read(&pool->orphan_nr)) {
		struct z4fold_class *size_class = &pool->unbuddied[ORPHAN_CHUNK];
		struct list_head *l = &size_class->list;
		spin_lock(&size_class->lock);
		while (!list_empty(l)) {
			int fchunks;
			struct z4fold_header *zhdr = list_first_entry(l,
					struct z4fold_header, buddy);
			struct page *page = virt_to_page(zhdr);

			if (test_bit(PAGE_RECLAIM, &page->private)) {
				WARN_ON(1);
				break;
			}
			list_del_init(&zhdr->buddy);
			spin_unlock(&size_class->lock);

			z4fold_page_lock(page);
			if (kref_put(&zhdr->refcount, release_z4fold_page_locked)) {
				atomic64_dec(&pool->pages_nr);
				continue;
			}
			if(!list_empty(&zhdr->buddy))
				WARN(1, "%s %d not empty buddy", __func__, __LINE__);

			fchunks = num_free_chunks(zhdr);
			size_class = &pool->unbuddied[fchunks];
			if (fchunks < NCHUNKS &&
					(!zhdr->chunks[FIRST] || !zhdr->chunks[SECOND] ||
					 !zhdr->chunks[THIRD] || !zhdr->chunks[LAST])) {
				spin_lock(&size_class->lock);
				list_add(&zhdr->buddy, &size_class->list);
				spin_unlock(&size_class->lock);
				zhdr->chunk_num = fchunks;
				zhdr->cpu = smp_processor_id();
			}
			z4fold_page_unlock(page);
			spin_lock(&size_class->lock);
		}

		spin_unlock(&size_class->lock);	
	}
#endif
}


static inline void *mchunk_memmove(struct z4fold_header *zhdr,
				unsigned short new_start, enum buddy src_bud)
{
	unsigned char *beg =(unsigned char*) zhdr;
	unsigned long off;	
	size_t sz = zhdr->chunks[src_bud];
	BUG_ON(src_bud == LAST || src_bud == FIRST);
	off = zhdr->start_off[START_OFFSET(src_bud)];

	return memmove(beg + (new_start << CHUNK_SHIFT),
		       beg + (off << CHUNK_SHIFT),
		       sz << CHUNK_SHIFT);
}

static struct page* z4fold_lookup_unbuddy(struct z4fold_pool* pool, unsigned short chunks) 
{
	struct z4fold_header *zhdr = NULL;
	struct z4fold_class* sz_class;
	struct list_head *l, *start;
	struct page* page = NULL;
	struct list_head* buddy;
	unsigned long flags;
	unsigned short i;

	BUG_ON(chunks >= NCHUNKS);

	for_each_unbuddied_list(i, chunks) {
		if (i >= chunks + BIG_CHUNK_GAP) 
			return NULL;

		sz_class = &pool->unbuddied[i];
		l = &sz_class->list;

		if (list_empty(READ_ONCE(l)))
			continue;

		spin_lock_irqsave(&sz_class->lock, flags);
		if (list_empty(l)) {
			spin_unlock_irqrestore(&sz_class->lock, flags);
			continue;
		}
next:
#ifdef CONFIG_Z4FOLD_SHRINK
		if (unlikely(!(zhdr = list_last_entry(READ_ONCE(l),
				struct z4fold_header, buddy))) ||
				!(page = virt_to_page(zhdr)) ||
				test_bit(PAGE_RECLAIM, &page->private) ||
#else
		if (unlikely(!(page = list_last_entry(READ_ONCE(l),
						struct page, lru))) ||
#endif
			!z4fold_page_trylock(page)) {
			l = l->prev;

			if (l->prev != &sz_class->list)
				goto next;

			spin_unlock_irqrestore(&sz_class->lock, flags);
			page = NULL;
			continue;
		}

		zhdr = kmap_atomic(page);
#ifdef CONFIG_Z4FOLD_SHRINK
		list_del_init(&zhdr->buddy);
		zhdr->cpu = -1;	
#else
		list_del_init(&page->lru);
#endif
		zhdr->chunk_num = NCHUNKS;
		zhdr->nr_alloc++;
		kref_get(&zhdr->refcount);
		kunmap_atomic(zhdr);
		spin_unlock_irqrestore(&sz_class->lock, flags);

		break;
	}

	return page;
}

static int z4fold_migrate_page(struct page *src_page) 
{
	int ret = 0;
	unsigned long* ptr;
	struct z4fold_pool *pool; 
	struct z4fold_header *src, *dst = NULL;
	struct page *dst_page = NULL;
	enum buddy dst_bud, src_bud;
	struct z4fold_mapping_area *area;

	src = (struct z4fold_header *) kmap_atomic(src_page);
	pool = src->pool;

	if (src->nr_alloc > 2) 
		goto src_unmap;

	for (src_bud=0;src_bud < Z4FOLD_MAX && src->nr_alloc;src_bud++) {
		if (handle_try_pin(src, src_bud)) {
			dst_page = z4fold_lookup_unbuddy(pool, src->chunks[src_bud]);
			if (dst_page) {
				void* dst_addr, *src_addr;
				unsigned short free;
				dst = kmap_atomic(dst_page);

				/* migrate page cost is big so check handle is freeable*/
				if (!handle_is_compactable(READ_ONCE(src->handle[src_bud]))) {
					unpin_tag(READ_ONCE(src->handle[src_bud]));
					dst->nr_alloc--;

					if (kref_put(&dst->refcount, release_z4fold_page_locked)) {
						atomic64_dec(&pool->pages_nr);
						kunmap_atomic(dst);
						continue;
					}
					free = num_free_chunks(dst);
					if (free && z4fold_allocatable(dst)) {
						/* Add to unbuddied list */
						z4fold_unbuddy_add(dst, dst_page, free);
					}
					kunmap_atomic(dst);
					z4fold_page_unlock(dst_page);
					ret = -1;
					break;
				}

				dst_bud = num_fit_chunk_idx(dst, src->chunks[src_bud]);	
				z4fold_set_chunks(dst, src->chunks[src_bud], dst_bud);

				dst_addr = z4fold_address(dst, dst_bud);
				src_addr = z4fold_address(src, src_bud);

				memcpy(dst_addr, src_addr, src->chunks[src_bud] << CHUNK_SHIFT); 
				/* migrate handle to dst and encode handle changed value */
				ptr = (unsigned long *)src->handle[src_bud];
				dst->handle[dst_bud] = src->handle[src_bud];
				clear_handle_contents(dst->handle[dst_bud]);
				*ptr |= encode_handle(dst_page, dst_bud);

				unpin_tag(dst->handle[dst_bud]);

				free = num_free_chunks(dst);
				if (free && z4fold_allocatable(dst)) {
					/* Add to unbuddied list */
					z4fold_unbuddy_add(dst, dst_page, free);
				}

				kunmap_atomic(dst);
				z4fold_page_unlock(dst_page);

				src->handle[src_bud] = 0;
				z4fold_set_chunks(src, 0, src_bud);

				src->nr_alloc--;
				ret++;
				if (kref_put(&src->refcount, release_z4fold_page_locked_list)) {
					atomic64_dec(&pool->pages_nr);
					ret = Z4FOLD_MAX;
					goto src_unmap;
				}
				continue;
			}
			unpin_tag(READ_ONCE(src->handle[src_bud]));
		} 
	}

src_unmap:
	kunmap_atomic(src);
	return ret;
}

static unsigned short z4fold_mgchunk_offset(struct z4fold_header* zhdr,
		 enum buddy target_bud, enum buddy src_bud)
{
	switch (target_bud) {
	case FIRST:
		return ZHDR_CHUNKS;
	case LAST:
		return TOTAL_CHUNKS - zhdr->chunks[src_bud];
	default:
		WARN(1,"%s Invalid bud %d for mgchunk(%d)\n", __func__, target_bud, src_bud);
		return 0;
	}
}
static unsigned short z4fold_compactchunk_offset(struct z4fold_header* zhdr,
         enum buddy target_bud, enum buddy src_bud)
{
    switch (target_bud) {
    case FIRST:
        return  zhdr->chunks[target_bud] + ZHDR_CHUNKS;
	case LAST:
        return TOTAL_CHUNKS - zhdr->chunks[src_bud] - zhdr->chunks[target_bud];
    default:
        WARN(target_bud >= HEADLESS,"%s Invalid bud %d for mgchunk(%d)\n", __func__, target_bud, src_bud);
		return zhdr->start_off[target_bud-1] + zhdr->chunks[target_bud]; 
    }
}
static inline enum buddy compaction_target_buddy(enum buddy src) 
{
	BUG_ON(src == FIRST);
	return src - 1;
}

static int do_compact_chunks(struct z4fold_header *zhdr, enum buddy src, enum buddy dst) 
{
	WARN(!zhdr->chunks[dst], "%s Invalid compact chunks to bud(%d - %hd) \n",
		__func__, dst, zhdr->chunks[dst]);

	if (handle_is_compactable(READ_ONCE(zhdr->handle[src])))  {
		unsigned short new_start = z4fold_compactchunk_offset(zhdr, dst, src);
		mchunk_memmove(zhdr, new_start, src);
        if (src == SECOND || src == THIRD) {
			zhdr->start_off[START_OFFSET(src)] = new_start;
        }

		return 1;
	}

	return 0;
}

/*static int do_migrate_chunks(struct z4fold_header *zhdr, enum buddy src, enum buddy dst)
{
    WARN(zhdr->chunks[dst], "%s Invalid migrate chunks to bud(%d - %hd) \n",
        __func__, dst, zhdr->chunks[dst]);

    if (handle_is_compactable(READ_ONCE(zhdr->handle[src])))  {
        unsigned short new_start = z4fold_mgchunk_offset(zhdr, dst, src);
        mchunk_memmove(zhdr, new_start, src);
        z4fold_migrate_header(zhdr, src, dst);
        return 1;
    }
    unpin_tag(READ_ONCE(zhdr->handle[src]));
    return 0;
}*/

/* Has to be called with lock held */
static int z4fold_compact_page(struct page *page)
{
	int ret = 0;
	unsigned short total;
	enum buddy src_bud, dst_bud;
	struct z4fold_mapping_area *area;
	struct z4fold_header *zhdr = (struct z4fold_header *) kmap_atomic(page);

	total = z4fold_total_chunks(zhdr);

	/* all chunk is freed or used while compact*/
	if (!total || total == NCHUNKS
		  || zhdr->nr_alloc > 3) { 
		ret = NCHUNKS;
		goto unmap_page;
	}

	/* can't move middle chunk before write done used */
	if (!IS_MIDDLE_CHUNK(zhdr))
		goto unmap_page; /* nothing to compact */

	/*for(src_bud = FIRST + 1;IS_PRIOR_CHUNK(zhdr) && src_bud < LAST; src_bud++) {
		if (!zhdr->chunks[src_bud]) 
			continue;
		dst_bud = prior_fit_chunk(zhdr, zhdr->chunks[src_bud]);
		if (dst_bud != BUDDIES_MAX &&
				handle_try_pin(zhdr, src_bud)) {
			ret = do_migrate_chunks(zhdr, src_bud, dst_bud);
		} 
	}*/

	for(src_bud = FIRST + 1; IS_MIDDLE_CHUNK(zhdr) && src_bud < LAST; src_bud++) {
		if (!zhdr->chunks[src_bud]) 
			continue;
		dst_bud = compaction_target_buddy(src_bud);
		if (zhdr->chunks[dst_bud] != 0 && 
				z4fold_compact_gap(zhdr, dst_bud, src_bud) &&
				handle_try_pin(zhdr, src_bud)) {
			ret = do_compact_chunks(zhdr, src_bud, dst_bud);
			unpin_tag(READ_ONCE(zhdr->handle[src_bud]));
		}
	}

unmap_page:
	kunmap_atomic(zhdr);
	/*
	 * moving data is expensive, so let's only do that if
	 * there's substantial gain (at least BIG_CHUNK_GAP chunks)
	 */
	return ret;
}

static void do_compact_page(struct page *page)
{
	struct z4fold_pool *pool;
	struct z4fold_header *zhdr;
	unsigned short fchunks;
	int ret;

	z4fold_page_lock(page);

	zhdr = (struct z4fold_header *) kmap_atomic(page);
	pool = zhdr->pool;

	if (WARN_ON(!test_and_clear_bit(NEEDS_COMPACTING, &page->private))) {
		pr_err("[z4fold] Already compcting is done by someone\n");
		if(kref_put(&zhdr->refcount, release_z4fold_page_locked)) {
			atomic64_dec(&pool->pages_nr);
			kunmap_atomic(zhdr);
			return;
		}
		kunmap_atomic(zhdr);
		z4fold_page_unlock(page);
		return;
	}
#ifdef CONFIG_Z4FOLD_SHRINK
	if (test_bit(PAGE_RECLAIM, &page->private)) {
		if(kref_put(&zhdr->refcount, release_z4fold_page_locked)) {
			atomic64_dec(&pool->pages_nr);
			return;
		}
		z4fold_page_unlock(page);
		return;
	}
#endif

	if (kref_put(&zhdr->refcount, release_z4fold_page_locked)) {
		atomic64_dec(&pool->pages_nr);
		kunmap_atomic(zhdr);
		return;
	} 

	kunmap_atomic(zhdr);
	/* if return Z4FOLD_MAX, source zhdr is freed by migration after release lock */
	if ((ret = z4fold_migrate_page(page)) == Z4FOLD_MAX)
		return;

	if (ret >= 0)
		z4fold_compact_page(page);

	zhdr = kmap_atomic(page);
	fchunks = num_free_chunks(zhdr);

	WARN(fchunks >= NCHUNKS,"%s fchunks(%d) is equal to NCHUNK\n",__func__, fchunks);

	clear_bit(PAGE_COMPACT_WORK, &page->private);
	if (fchunks && fchunks < NCHUNKS &&
	    z4fold_allocatable(zhdr)) {
		z4fold_unbuddy_add(zhdr, page, fchunks);
	} 

	kunmap_atomic(zhdr);
	z4fold_page_unlock(page);
}

static inline int compact_should_run(struct z4fold_pool* pool) 
{
	return !list_empty(&pool->compact_list) && !(pool->need_wakeup = 0);
}

static void do_compactd_work(struct z4fold_pool* pool)
{
	struct z4fold_header *zhdr;
	unsigned long flags;
#ifndef CONFIG_Z4FOLD_SHRINK
	struct page *page = NULL;
#endif
	while (!list_empty(&pool->compact_list)) {
		spin_lock_irqsave(&pool->compact_lock, flags);
#ifdef CONFIG_Z4FOLD_SHRINK
		zhdr = list_first_entry(&pool->compact_list,
				struct z4fold_header, buddy);
		if(likely(zhdr)) {
			list_del_init(&zhdr->buddy);
			zhdr->chunk_num = NCHUNKS;
		}
#else
		zhdr = NULL;
		page = list_first_entry(&pool->compact_list,
				struct page, lru);
		if(likely(page)) {
			BUG_ON(test_bit(PAGE_STALE, &page->private));
			zhdr = (struct z4fold_header *)kmap_atomic(page);
			WARN(zhdr->chunk_num != NCHUNKS, "zhdr chunk_num is wrong(%u)\n", zhdr->chunk_num);
			set_bit(PAGE_COMPACT_WORK, &page->private);
			list_del_init(&page->lru);
			kunmap_atomic(zhdr);
		}

#endif
		spin_unlock_irqrestore(&pool->compact_lock, flags);

		if (likely(page))
			do_compact_page(page);
	}
	pool->need_wakeup = 1;
}

static int z4fold_compact_thread(void *data)
{
	struct z4fold_pool* pool = (struct z4fold_pool*)data;
	set_freezable();
	set_user_nice(current, 5);

	while (!kthread_should_stop()) {
		if (compact_should_run(pool))
			do_compactd_work(pool);

		try_to_freeze();

		wait_event_freezable(pool->compact_wait,
				compact_should_run(pool) || kthread_should_stop());
	}
	return 0;
}

/*
 * API Functions
 */

/**
 * z4fold_create_pool() - create a new z4fold pool
 * @name:	pool name
 * @gfp:	gfp flags when allocating the z4fold pool structure
 * @ops:	user-defined operations for the z4fold pool
 *
 * Return: pointer to the new z4fold pool or NULL if the metadata allocation
 * failed.
 */
struct z4fold_pool *z4fold_create_pool(const char *name, gfp_t gfp,
		const struct z4fold_ops *ops)
{
	struct z4fold_pool *pool = NULL;
	int i, err;

	pool = kzalloc(sizeof(struct z4fold_pool), gfp);
	if (!pool)
		return NULL;
	spin_lock_init(&pool->stale_lock);
	spin_lock_init(&pool->compact_lock);
#ifdef CONFIG_Z4FOLD_SHRINK
	spin_lock_init(&pool->lru_lock);
#endif

	pr_info("[z4fold] CHUNK_SHIFT:%d CHUNK_SIZE %d ZHDR_SIZE_ALIGNED:%u"
			" ZHDR_CHUNKS:%u TOTAL_CHUNKS:%lu NCHUNK:%lu\n",
			CHUNK_SHIFT, CHUNK_SIZE, ZHDR_SIZE_ALIGNED, ZHDR_CHUNKS,
			 TOTAL_CHUNKS, NCHUNKS);

	if (create_handle_cache(pool))
		goto out;

	pool->unbuddied = kmalloc(sizeof(struct z4fold_class)*NCHUNKS, gfp);

	for_each_unbuddied_list(i, 0) {
	struct z4fold_class *sz_class = &pool->unbuddied[i];
		INIT_LIST_HEAD(&sz_class->list);
		spin_lock_init(&sz_class->lock);
	}
	INIT_LIST_HEAD(&pool->lru);
	INIT_LIST_HEAD(&pool->stale);
	INIT_LIST_HEAD(&pool->compact_list);
	init_waitqueue_head(&pool->compact_wait);
	pool->need_wakeup = 1;
	atomic64_set(&pool->pages_nr, 0);
	atomic64_set(&pool->orphan_nr, 0);
	atomic64_set(&pool->stale_nr, 0);
	atomic64_set(&pool->headless_nr, 0);
	pool->name = name;

	pool->compactd = kthread_run(z4fold_compact_thread, pool, "z4fold_compactd");
	if (IS_ERR(pool->compactd)) {
		pr_err("z4fold: creating compact_kthread failed\n");
		err = PTR_ERR(pool->compactd);
		goto out;
	}


	pool->release_wq = create_singlethread_workqueue(pool->name);
	if (!pool->release_wq)
		goto release_wq;
	INIT_WORK(&pool->work, free_pages_work);
#ifdef CONFIG_ZPOOL
	pool->ops = ops;
#endif
	return pool;
release_wq:
	kthread_stop(pool->compactd);
out:
	kfree(pool);
	return NULL;
}

/**
 * z4fold_destroy_pool() - destroys an existing z4fold pool
 * @pool:	the z4fold pool to be destroyed
 *
 * The pool should be emptied before this function is called.
 */
void z4fold_destroy_pool(struct z4fold_pool *pool)
{
	destroy_workqueue(pool->release_wq);
	kthread_stop(pool->compactd);
    //destroy_workqueue(pool->compact_wq);
    destroy_handle_cache(pool);
	kfree(pool);
}

struct list_head_t {
    struct list_head *next, *prev;
};

static struct z4fold_header *get_vm_addr(struct page *page, bool can_sleep) 
{
	struct z4fold_header *zhdr = NULL;
	if (can_sleep)
		zhdr = kmap(page);
	else
		zhdr = kmap_atomic(page);

	return zhdr;
}

static void put_vm_addr(struct z4fold_header *zhdr, struct page *page, bool can_sleep)
{
	if (can_sleep)
		kunmap(page);
	else
		kunmap_atomic(zhdr);
}
 
/**
 * z4fold_alloc() - allocates a region of a given size
 * @pool:	z4fold pool from which to allocate
 * @size:	size in bytes of the desired allocation
 * @gfp:	gfp flags used if the pool needs to grow
 * @handle:	handle of the new allocation
 *
 * This function will attempt to find a free region in the pool large enough to
 * satisfy the allocation request.  A search of the unbuddied lists is
 * performed first. If no suitable free region is found, then a new page is
 * allocated and added to the pool to satisfy the request.
 *
 * gfp should not set __GFP_HIGHMEM as highmem pages cannot be used
 * as z4fold pool pages.
 *
 * Return: 0 if success and handle is set, otherwise -EINVAL if the size or
 * gfp arguments are invalid or -ENOMEM if the pool was unable to allocate
 * a new page.
 */
int z4fold_alloc(struct z4fold_pool *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	unsigned short chunks = 0, i, freechunks;
	struct z4fold_header *zhdr = NULL;
	struct page *page = NULL;
	enum buddy bud;
	bool can_sleep = gfp & __GFP_WAIT;
	unsigned long* obj = NULL;
	struct z4fold_class *sz_class;
	struct list_head *buddy;
	unsigned long flags;

	if (!size)
		return -EINVAL;

	if (size > PAGE_SIZE)
		return -EINVAL;

	*handle = alloc_handle(pool, gfp);
	if (!*handle)
		return -ENOMEM;

	obj = (unsigned long *)*handle;
	*obj = 0;

	if (size > PAGE_SIZE - ZHDR_SIZE_ALIGNED) 
		bud = HEADLESS;
	else {
		struct list_head *l;
		chunks = size_to_chunks(size);

		/* First, try to find an unbuddied z4fold page. */
lookup:
		for_each_unbuddied_list(i, chunks) {
			sz_class = &pool->unbuddied[i];
			l = &sz_class->list;	
#ifdef CONFIG_Z4FOLD_SHRINK
			zhdr = list_first_entry_or_null(READ_ONCE(l),
						struct z4fold_header, buddy);
			if (!zhdr)
				continue;

#else
			page = list_first_entry_or_null(READ_ONCE(l),
						struct page, lru);
			if (!page)
				continue;
#endif

			/* Re-check under lock. */
			spin_lock_irqsave(&sz_class->lock, flags);
#ifdef CONFIG_Z4FOLD_SHRINK
			if (unlikely(zhdr != list_first_entry_or_null(READ_ONCE(l),
					struct z4fold_header, buddy)) ||
					!(page = virt_to_page(zhdr)) ||
					test_bit(PAGE_RECLAIM, &page->private) ||
#else
			if (unlikely(page != list_first_entry_or_null(READ_ONCE(l),
					struct page, lru)) ||
#endif
			    !z4fold_page_trylock(page)) {
				BUG_ON(i >= NCHUNKS);
				spin_unlock_irqrestore(&sz_class->lock, flags);
				goto lookup;
			}

			zhdr = get_vm_addr(page, can_sleep);
			
			BUG_ON(zhdr->chunk_num != i);

			if (test_bit(PAGE_STALE, &page->private))
				BUG_ON(1);
#ifdef CONFIG_Z4FOLD_SHRINK
			buddy = &zhdr->buddy;
#else 
			buddy = &page->lru;
#endif
			list_del_init(buddy);
			zhdr->chunk_num = NCHUNKS;

			BUG_ON(i >= NCHUNKS);
			spin_unlock_irqrestore(&sz_class->lock, flags);

			//zhdr->cpu = -1;

			/*
			 * this page could not be removed from its unbuddied
			 * list while pool lock was held, and then we've taken
			 * page lock so kref_put could not be called before
			 * we got here, so it's safe to just call kref_get()
			 */
			kref_get(&zhdr->refcount);
			break;
		}

		if (zhdr) {
			bud = num_fit_chunk_idx(zhdr, chunks);
			if(unlikely(bud == BUDDIES_MAX)) {
				z4fold_page_unlock(page);
				WARN(bud == BUDDIES_MAX, 
				"There's no free chunk to fit chunk size:%d zhdr:%p\n",
				 chunks, zhdr);

				put_vm_addr(zhdr, page, can_sleep);
				zhdr = NULL;
				goto lookup;
			}
			zhdr->nr_alloc++;
			goto found;
		}
		bud = FIRST;
	}

	page = NULL;
	if (can_sleep) {
	   int stale_nr = atomic64_read(&pool->stale_nr);
	   spin_lock(&pool->stale_lock);
#ifdef CONFIG_Z4FOLD_SHRINK
loop:
	   zhdr = list_first_entry_or_null(&pool->stale,
                       struct z4fold_header, buddy);
#else
	   page = list_first_entry_or_null(&pool->stale,
			   struct page, lru);
#endif
       /*
        * Before allocating a page, let's see if we can take one from
        * the stale pages list. cancel_work_sync() can sleep so we
        * limit this case to the contexts where we can sleep
        */

#ifdef CONFIG_Z4FOLD_SHRINK
	   if (zhdr && stale_nr-- > 0) {
		   page = virt_to_page(zhdr);
		   if(test_bit(PAGE_LAZY_FREE, &page->private)) {	
			   list_del_init(&zhdr->buddy);
			   list_add_tail(&zhdr->buddy, &pool->stale);
			   page = NULL;
			   goto loop;
		   }
		   buddy = &zhdr->buddy;
#else
	   if (page) {
		   buddy = &page->lru;
#endif
		   list_del_init(buddy);
       }
	   spin_unlock(&pool->stale_lock);
	}

   if (!page)
	   page = alloc_page(gfp);

	if (!page) {
		free_handle(pool, *handle);
		*handle = 0;
		return -ENOMEM;
	}

	zhdr = get_vm_addr(page, can_sleep);
	atomic64_inc(&pool->pages_nr);

	init_z4fold_page(zhdr, page, pool);
	if (bud == HEADLESS) {
		set_bit(PAGE_HEADLESS, &page->private);
		atomic64_inc(&pool->headless_nr);
		put_vm_addr(zhdr, page, can_sleep);
		goto headless;
	}

	z4fold_page_lock(page);

found:
	pin_tag(*handle);
	if (WARN(zhdr->handle[bud] != 0 ,
			"Invalid handle stat page:%p [f(%hd) s(%hd) t(%hd) l(%hd)] zhdr:%p bud:%d\n", page,
			zhdr->chunks[FIRST], zhdr->chunks[SECOND], zhdr->chunks[THIRD], zhdr->chunks[LAST], zhdr, bud)) {
		BUG_ON(1);
	}

	zhdr->handle[bud] = *handle;
	z4fold_set_chunks(zhdr, chunks, bud);

	if (WARN(NCHUNKS < z4fold_total_chunks(zhdr),
			"Invalid chunk stat page:%p [f(%hd) s(%hd) t(%hd) l(%hd)] zhdr:%p bud:%d\n", page,
			zhdr->chunks[FIRST], zhdr->chunks[SECOND], zhdr->chunks[THIRD], zhdr->chunks[LAST], zhdr, bud)) {
		BUG_ON(1);
	}

	freechunks = num_free_chunks(zhdr);
	if (freechunks && z4fold_allocatable(zhdr)) {
		/* Add to unbuddied list */
		z4fold_unbuddy_add(zhdr, page, freechunks);
	} 

	put_vm_addr(zhdr, page, can_sleep);
	z4fold_page_unlock(page);

headless:
#ifdef CONFIG_Z4FOLD_SHRINK
	spin_lock(&pool->lru_lock);
	/* Add/move z4fold page to beginning of LRU */
	if (!list_empty(&page->lru)) {
		list_del(&page->lru);
		list_add(&page->lru, &pool->lru);
	}
	spin_unlock(&pool->lru_lock);
#endif

	*obj |= encode_handle(page, bud);
	
	return 0;
}

/**
 * z4fold_free() - frees the allocation associated with the given handle
 * @pool:	pool in which the allocation resided
 * @handle:	handle associated with the allocation returned by z4fold_alloc()
 *
 * In the case that the z4fold page in which the allocation resides is under
 * reclaim, as indicated by the PG_reclaim flag being set, this function
 * only sets the first|chunks[LAST] to 0.  The page is actually freed
 * once both buddies are evicted (see z4fold_reclaim_page() below).
 */
void z4fold_free(struct z4fold_pool *pool, unsigned long handle)
{
	struct z4fold_header *zhdr;
	struct page *page;
	enum buddy bud;
	unsigned long* obj = (unsigned long *)handle;
	unsigned long zhdr_pfn;
	mark_free(handle);	
	pin_tag(handle);
	zhdr_pfn = handle_to_pfn(*obj);
	page = pfn_to_page(zhdr_pfn);

	if (test_bit(PAGE_HEADLESS, &page->private)) {
		/* HEADLESS page stored */
#ifdef CONFIG_Z4FOLD_SHRINK
		spin_lock(&pool->lru_lock);
#endif
		if(free_headless_page(page)) {
			atomic64_dec(&pool->pages_nr);
			atomic64_dec(&pool->headless_nr);
		}
#ifdef CONFIG_Z4FOLD_SHRINK
		spin_unlock(&pool->lru_lock);
#endif
		free_handle(pool, handle);

		return;
	} else {
		bud = handle_to_buddy(*obj);
		zhdr = (struct z4fold_header *)kmap(page);

		WARN(zhdr->handle[bud] != handle,
		"%s free corrupt handle:%lx != zhld:%lx  "
		"zhdr:%p bud:%hd ",__func__, handle, 
		zhdr->handle[bud], zhdr, bud);
		zhdr->handle[bud] = 0;

		z4fold_page_lock(page);
		z4fold_set_chunks(zhdr, 0, bud);

		free_handle(pool, handle);

	}

	zhdr->nr_alloc--;

	if (kref_put(&zhdr->refcount, release_z4fold_page_locked_list)) {
		atomic64_dec(&pool->pages_nr);
		kunmap(page);
		return;
	}


	if (test_and_set_bit(NEEDS_COMPACTING, &page->private)) {
		z4fold_page_unlock(page);
		kunmap(page);
		return;
	}

	if (!test_bit(PAGE_COMPACT_WORK, &page->private)) {
		kref_get(&zhdr->refcount);
		z4fold_unbuddy_del(zhdr, page, zhdr->chunk_num);
		z4fold_compact_add(zhdr, page);

		if (pool->need_wakeup) 
			wake_up_interruptible(&pool->compact_wait);
	} else {
		WARN(1,"%s page(%p-%lx) is cleared NEEDS_COMPACTING" 
			"but set PAGE_COMPACT_WORK\n",__func__, page, page->private);
	}
	kunmap(page);
	z4fold_page_unlock(page);
}
#ifdef CONFIG_ZPOOL
/**
 * z4fold_reclaim_page() - evicts allocations from a pool page and frees it
 * @pool:	pool from which a page will attempt to be evicted
 * @retires:	number of pages on the LRU list for which eviction will
 *		be attempted before failing
 *
 * z4fold reclaim is different from normal system reclaim in that it is done
 * from the bottom, up. This is because only the bottom layer, z4fold, has
 * information on how the allocations are organized within each z4fold page.
 * This has the potential to create interesting locking situations between
 * z4fold and the user, however.
 *
 * To avoid these, this is how z4fold_reclaim_page() should be called:

 * The user detects a page should be reclaimed and calls z4fold_reclaim_page().
 * z4fold_reclaim_page() will remove a z4fold page from the pool LRU list and
 * call the user-defined eviction handler with the pool and handle as
 * arguments.
 *
 * If the handle can not be evicted, the eviction handler should return
 * non-zero. z4fold_reclaim_page() will add the z4fold page back to the
 * appropriate list and try the next z4fold page on the LRU up to
 * a user defined number of retries.
 *
 * If the handle is successfully evicted, the eviction handler should
 * return 0 _and_ should have called z4fold_free() on the handle. z4fold_free()
 * contains logic to delay freeing the page if the page is under reclaim,
 * as indicated by the setting of the PG_reclaim flag on the underlying page.
 *
 * If all buddies in the z4fold page are successfully evicted, then the
 * z4fold page can be freed.
 *
 * Returns: 0 if page is successfully freed, otherwise -EINVAL if there are
 * no pages to evict or an eviction handler is not registered, -EAGAIN if
 * the retry limit was hit.
 */
static int z4fold_reclaim_page(struct z4fold_pool *pool, unsigned int retries)
{
	int i, ret = 0;
	struct z4fold_header *zhdr = NULL;
	struct page *page = NULL;
	struct list_head *pos, *n;
	unsigned long first_handle = 0, second_handle = 0, third_handle = 0, last_handle = 0;

	spin_lock(&pool->lru_lock);
	if (!pool->ops || !pool->ops->evict || retries == 0) {
		spin_unlock(&pool->lru_lock);
		return -EINVAL;
	}
	for (i = 0; i < retries; i++) {
		if (list_empty(&pool->lru)) {
			spin_unlock(&pool->lru_lock);
			return -EINVAL;
		}
		list_for_each_prev_safe(pos, n, &pool->lru) {
			page = list_entry(pos, struct page, lru);

			zhdr = page_address(page);

			if (test_bit(PAGE_HEADLESS, &page->private)) {
				/* candidate found */
				page_ref_inc(page);
				break;
			}

			if (test_bit(PAGE_STALE, &page->private)) { 
				pr_err("[z4fold] page(%p- %lx) is freeing so skip it",
				page,page_to_pfn(page));
				continue;
			}

			if (test_and_set_bit(PAGE_RECLAIM, &page->private) || 
				!z4fold_page_trylock(page)) 
				continue; /* can't evict at this point */
				
			kref_get(&zhdr->refcount);

			if (zhdr->chunk_num < NCHUNKS) {
				struct z4fold_class* size_class = &pool->unbuddied[zhdr->chunk_num];
				spin_lock(&size_class->lock);
				if (!list_empty(&zhdr->buddy))
					list_del_init(&zhdr->buddy);

				spin_unlock(&size_class->lock);
			} else {
				if(!list_empty(&zhdr->buddy)) {
					WARN(1,"%s size_class chunk_num:%d but not emtpy buddy:%p",__func__, zhdr->chunk_num, &zhdr->buddy);
				}

			}

			zhdr->cpu = -1;
			break;
		}

		list_del_init(&page->lru);
		spin_unlock(&pool->lru_lock);

		if (!test_bit(PAGE_HEADLESS, &page->private)) {
			/*
			 * We need encode the handles before unlocking, since
			 * we can race with free that will set
			 * (first|last)_chunks to 0
			 */
			first_handle = 0;
			last_handle = 0;
			second_handle = 0;
			third_handle = 0;
			if (zhdr->chunks[FIRST])
				first_handle = encode_handle(zhdr, FIRST);
			if (zhdr->chunks[SECOND])
				second_handle = encode_handle(zhdr, SECOND);
			if (zhdr->chunks[THIRD])
				third_handle = encode_handle(zhdr, THIRD);
			if (zhdr->chunks[LAST])
				last_handle = encode_handle(zhdr, LAST);
			/*
			 * it's safe to unlock here because we hold a
			 * reference to this page
			 */
			z4fold_page_unlock(page);
		} else {
			first_handle = encode_handle(zhdr, HEADLESS);
			last_handle = second_handle = third_handle = 0;
		}

		/* Issue the eviction callback(s) */
		if (second_handle) {
			ret = pool->ops->evict(pool, second_handle);
			if (ret) {
				goto next;
			}
		}
		if (third_handle) {
			ret = pool->ops->evict(pool, third_handle);
			if (ret) {
				goto next;
			}
		}

		if (first_handle) {
			ret = pool->ops->evict(pool, first_handle);
			if (ret) {
				goto next;
			}

		}
		if (last_handle) {
			ret = pool->ops->evict(pool, last_handle);
			if (ret) {
				goto next;
			}
		}
next:
		spin_lock(&pool->lru_lock);
		if(list_empty(&page->lru) && !test_bit(PAGE_STALE, &page->private))
			list_add(&page->lru, &pool->lru);
		spin_unlock(&pool->lru_lock);

		clear_bit(PAGE_RECLAIM, &page->private);

		if (test_bit(PAGE_HEADLESS, &page->private)) {
			spin_lock(&pool->lru_lock);
			if (free_headless_page(page)) {
				atomic64_dec(&pool->pages_nr);
				spin_unlock(&pool->lru_lock);
				return 0;
			}
			spin_unlock(&pool->lru_lock);
			if (ret == 0) { 
				return 0;
			}

		} else {
			/* evicted all pages*/
			if(ret == 0) {
				if (kref_put(&zhdr->refcount, release_z4fold_page)) { 
					atomic64_dec(&pool->pages_nr);
					return 0;
				}
			} else { /* evict failure*/
				struct z4fold_class *size_class;
				int fchunks = ORPHAN_CHUNK;
				bool locked = z4fold_page_trylock(page);

				if(locked) {
					if (kref_put(&zhdr->refcount, release_z4fold_page_locked)) {
						atomic64_dec(&pool->pages_nr);
						return 0;
					}
					if (!test_bit(NEEDS_COMPACTING, &page->private)) {
						if(!list_empty(&zhdr->buddy))
							WARN(1, "%s %d zhdr evict faild :%d is empty buddy", __func__, __LINE__, ret);

						fchunks = num_free_chunks(zhdr);
						size_class = &pool->unbuddied[fchunks];
						if (fchunks && fchunks < NCHUNKS &&
								(!zhdr->chunks[FIRST] || !zhdr->chunks[SECOND] ||
								 !zhdr->chunks[THIRD] || !zhdr->chunks[LAST])) {
							spin_lock(&size_class->lock);
							list_add(&zhdr->buddy, &size_class->list);
							spin_unlock(&size_class->lock);
							zhdr->chunk_num = fchunks;
						}
					} 
					z4fold_page_unlock(page);

				} else { /* not locked */
					if(!list_empty(&zhdr->buddy))
						WARN(1, "%s %d zhdr evict faild :%d is empty buddy", __func__, __LINE__, ret);

					size_class = &pool->unbuddied[fchunks];
					spin_lock(&size_class->lock);
					list_add(&zhdr->buddy, &size_class->list);
					spin_unlock(&size_class->lock);
					zhdr->chunk_num = fchunks;
					atomic64_inc(&pool->orphan_nr);
				}
			}
		}
		/*
		 * Add to the beginning of LRU.
		 * Pool lock has to be kept here to ensure the page has
		 * not already been released
		 */
		spin_lock(&pool->lru_lock);
	}
	spin_unlock(&pool->lru_lock);
	return -EAGAIN;
}
#endif
/**
 * z4fold_map() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be mapped
 *
 * Extracts the buddy number from handle and constructs the pointer to the
 * correct starting chunk within the page.
 *
 * Returns: a pointer to the mapped allocation
  = 3*/
#ifdef CONFIG_ZPOOL
void *z4fold_map(struct z4fold_pool *pool, unsigned long handle)
#else
void *z4fold_map(struct z4fold_pool *pool, unsigned long handle, enum z4fold_mapmode mm)
#endif
{
	struct z4fold_header *zhdr;
	struct page *page;
	struct z4fold_mapping_area *area;
	enum buddy buddy;
	unsigned long *obj = (unsigned long *)handle;
	unsigned long zhdr_pfn;

	if (mm != Z4_MM_WO) {
		if(!trypin_tag(handle)) {
			mark_contend(handle);
			pin_tag(handle);
			clear_contend(handle);
			
		}
	}

	zhdr_pfn = handle_to_pfn(*obj);
	page = pfn_to_page(zhdr_pfn);
	area = &get_cpu_var(z4fold_map_area); 
	area->vm_addr = kmap_atomic(page);
	zhdr = (struct z4fold_header *) area->vm_addr;

	if (test_bit(PAGE_HEADLESS, &page->private)) { 
		return (void*)zhdr;
	}

	buddy = handle_to_buddy(*obj);	
	return z4fold_address(zhdr, buddy);
}

/**
 * z4fold_unmap() - unmaps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be unmapped
 */
void z4fold_unmap(struct z4fold_pool *pool, unsigned long handle)
{
	struct z4fold_mapping_area *area;
	unsigned long *obj = (unsigned long *)handle;

	area = this_cpu_ptr(&z4fold_map_area);
	kunmap_atomic(area->vm_addr);
	put_cpu_var(z4fold_map_area);

	unpin_tag(handle);
}

/**
 * z4fold_get_pool_size() - gets the z4fold pool size in pages
 * @pool:	pool whose size is being queried
 *
 * Returns: size in pages of the given pool.
 */
u64 z4fold_get_pool_size(struct z4fold_pool *pool)
{
	return atomic64_read(&pool->pages_nr);
}
EXPORT_SYMBOL_GPL(z4fold_get_pool_size);

u64 z4fold_get_headless_pages(struct z4fold_pool *pool)
{
	return atomic64_read(&pool->headless_nr);
}
EXPORT_SYMBOL_GPL(z4fold_get_headless_pages);


#ifdef CONFIG_ZPOOL
/*****************
 * zpool
 ****************/

static int z4fold_zpool_evict(struct z4fold_pool *pool, unsigned long handle)
{
	if (pool->zpool && pool->zpool_ops && pool->zpool_ops->evict)
		return pool->zpool_ops->evict(pool->zpool, handle);
	else
		return -ENOENT;
}

static const struct z4fold_ops z4fold_zpool_ops = {
	.evict =	z4fold_zpool_evict
};

static void *z4fold_zpool_create(const char *name, gfp_t gfp,
			       const struct zpool_ops *zpool_ops,
			       struct zpool *zpool)
{
	struct z4fold_pool *pool;

	pool = z4fold_create_pool(name, gfp,
				zpool_ops ? &z4fold_zpool_ops : NULL);
	if (pool) {
		pool->zpool = zpool;
		pool->zpool_ops = zpool_ops;
	}
	return pool;
}

void z4fold_zpool_destroy(void *pool)
{
	z4fold_destroy_pool(pool);
}

static int z4fold_zpool_malloc(void *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	return z4fold_alloc(pool, size, gfp, handle);
}
static void z4fold_zpool_free(void *pool, unsigned long handle)
{
	z4fold_free(pool, handle);
}
#ifdef CONFIG_Z4FOLD_SHRINK
static int z4fold_zpool_shrink(void *pool, unsigned int pages,
			unsigned int *reclaimed)
{
	unsigned int total = 0;
	int ret = -EINVAL;

	while (total < pages) {
		ret = z4fold_reclaim_page(pool, 8);
		if (ret < 0)
			break;
		total++;
	}

	if (reclaimed)
		*reclaimed = total;

	return ret;
}
#endif

static void *z4fold_zpool_map(void *pool, unsigned long handle,
			enum zpool_mapmode mm)
{
	return z4fold_map(pool, handle);
}
static void z4fold_zpool_unmap(void *pool, unsigned long handle)
{
	z4fold_unmap(pool, handle);
}

static u64 z4fold_zpool_total_size(void *pool)
{
	return z4fold_get_pool_size(pool);
}

static struct zpool_driver z4fold_zpool_driver = {
	.type =		"z4fold",
	.owner =	THIS_MODULE,
	.create =	z4fold_zpool_create,
	.destroy =	z4fold_zpool_destroy,
	.malloc =	z4fold_zpool_malloc,
	.free =		z4fold_zpool_free,
#ifdef CONFIG_Z4FOLD_SHRINK
	.shrink =	z4fold_zpool_shrink,
#endif
	.map =		z4fold_zpool_map,
	.unmap =	z4fold_zpool_unmap,
	.total_size =	z4fold_zpool_total_size,
};

MODULE_ALIAS("zpool-z4fold");
static int __init init_z4fold(void)
{
	int ret = z4fold_register_cpu_notifier();
	
	if (ret);
		goto notifier_fail;
	/* Make sure the z4fold header is not larger than the page size */
	BUILD_BUG_ON(ZHDR_SIZE_ALIGNED > PAGE_SIZE);
	zpool_register_driver(&z4fold_zpool_driver);
	pr_err("[jsun.song] CHUNK_SHIFT:%d CHUNK_SIZE %d ZHDR_SIZE_ALIGNED:%u ZHDR_CHUNKS:%u TOTAL_CHUNKS:%lu NCHUNK:%lu\n",
		CHUNK_SHIFT, CHUNK_SIZE, ZHDR_SIZE_ALIGNED, ZHDR_CHUNKS, TOTAL_CHUNKS, NCHUNKS);

notifier_fail:
	z4fold_unregister_cpu_notifier();

	return 0;
}

static void __exit exit_z4fold(void)
{
	zpool_unregister_driver(&z4fold_zpool_driver);
	z4fold_unregister_cpu_notifier();
}

module_init(init_z4fold);
module_exit(exit_z4fold);
#endif
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jusun song <jsun.song@samsung.com>");
MODULE_DESCRIPTION("4-Fold Allocator for Compressed Pages");
