/*
 * gcma.c - Guaranteed Contiguous Memory Allocator
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
 * With this idea, gcma withdraw pages being used for second-class clients and
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
 * Copyright (C) 2014  LG Electronics Inc.,
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>
 * Copyright (C) 2014-2015  SeongJae Park <sj38.park@gmail.com>
 *
 * smem_cache.c
 * drain.lee: Modified not to use kernel page structure in dmem area
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleancache.h>
#include <linux/frontswap.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rwlock.h>
#include <asm/mach/map.h>//arm_ioremap

#define BITS_FS_DMEM_HASH	8
#define NR_FS_DMEM_HASH_BUCKS	(1 << BITS_FS_DMEM_HASH)
#define BYTES_FS_DMEM_KEY	(sizeof(struct frontswap_dmem_key))

#define BITS_CC_DMEM_HASH	8
#define NR_CC_DMEM_HASH_BUCKS	(1 << BITS_CC_DMEM_HASH)
#define BYTES_CC_DMEM_KEY	(sizeof(struct cleancache_dmem_key))
#define MAX_CLEANCACHE_FS	16

/* XXX: What's the ideal? */
#define NR_EVICT_BATCH	32

#define IS_IN_RANGE(val, start, size) (((start) <= (val)) && (((start) + (size)) > (val)))

#define IOREMAP_WINDOW_PFN_BITS		8/* 2^12 * PAGESIZE = 1MB */
//#define IOREMAP_WINDOW_PFN_BITS		12/* 2^12 * PAGESIZE = 16MB */
#define IOREMAP_WINDOW_PFN_SIZE		(1UL<<IOREMAP_WINDOW_PFN_BITS)
#define IOREMAP_WINDOW_PFN_MASK		(~(IOREMAP_WINDOW_PFN_SIZE-1))

struct dmem_entry;
struct dmem_hashbucket;

struct gpage_t {
	struct dmem_hashbucket	*hashbucket;
	struct dmem_entry		*entry;
	unsigned long			flags;
	struct list_head		lru;
};

struct gcma {
	spinlock_t lock;
	unsigned long *bitmap;
	unsigned long base_pfn, size;
	struct gpage_t *gpages;

	rwlock_t			maping_rwlock;
	unsigned long mapped_gpfn;
	void __iomem * mapped_virt;
	size_t mapped_gpfn_cnt;

	struct list_head list;
};

struct gcma_info {
	spinlock_t			lock;	/* protect list */
	struct list_head	head;
};

static struct gcma_info ginfo = {
	.head = LIST_HEAD_INIT(ginfo.head),
	.lock = __SPIN_LOCK_UNLOCKED(ginfo.lock),
};

/*
 * Discardable memory(dmem) store and load easily discardable pages inside
 * gcma area. Because it's discardable memory, loading stored page could fail
 * anytime.
 */

/* entry for a discardable page */
struct dmem_entry {
	struct rb_node rbnode;
	struct gcma *gcma;
	void *key;
	struct gpage_t *gpage;
	atomic_t refcount;
};

/* dmem hash bucket */
struct dmem_hashbucket {
	struct dmem *dmem;
	struct rb_root rbroot;
	spinlock_t lock;
};

/* dmem pool */
struct dmem_pool {
	struct dmem_hashbucket *hashbuckets;
};

struct dmem {
	struct dmem_pool **pools;
	unsigned nr_pools;
	unsigned nr_hash;
	struct kmem_cache *key_cache;
	size_t bytes_key;
	struct list_head lru_list;
	spinlock_t lru_lock;

	unsigned (*hash_key)(void *key);
	int (*compare)(void *lkey, void *rkey);
};

struct frontswap_dmem_key {
	pgoff_t key;
};

struct cleancache_dmem_key {
	u8 key[sizeof(pgoff_t) + sizeof(struct cleancache_filekey)];
};

static struct kmem_cache *dmem_entry_cache;

static struct dmem fs_dmem;	/* dmem for frontswap backend */

static struct dmem cc_dmem;	/* dmem for cleancache backend */
static atomic_t nr_cleancache_fses = ATOMIC_INIT(0);

/* configs from kernel parameter */
static bool fs_disabled __read_mostly;
module_param_named(fs_disabled, fs_disabled, bool, 0444);

static bool cc_disabled __read_mostly;
module_param_named(cc_disabled, cc_disabled, bool, 0444);

/* For statistics */
static atomic_t gcma_fs_inits = ATOMIC_INIT(0);
static atomic_t gcma_fs_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_evicted_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_reclaimed_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_invalidated_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_invalidated_areas = ATOMIC_INIT(0);

static atomic_t gcma_cc_inits = ATOMIC_INIT(0);
static atomic_t gcma_cc_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_load_failed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_evicted_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_reclaimed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_inodes = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_fses = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_failed_fses = ATOMIC_INIT(0);

static atomic_t gcma_cc_time_loaded_pages_total = ATOMIC_INIT(0);
static atomic_t gcma_cc_time_load_failed_pages_total = ATOMIC_INIT(0);
static atomic_t gcma_cc_time_loaded_pages_map = ATOMIC_INIT(0);
static atomic_t gcma_cc_time_loaded_pages_copy = ATOMIC_INIT(0);

#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
#include <mach/sdp_hwclock.h>
#endif
static unsigned long long __get_nsecs(void)
{
#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	return sched_clock();
#endif
}

static unsigned long dmem_evict_lru(struct dmem *dmem, unsigned long nr_pages);

static struct dmem_hashbucket *dmem_hashbuck(struct gpage_t *gpage)
{
	return (struct dmem_hashbucket *)gpage->hashbucket;
}

static void set_dmem_hashbuck(struct gpage_t *gpage, struct dmem_hashbucket *buck)
{
	gpage->hashbucket = buck;
}

static struct dmem_entry *dmem_entry(struct gpage_t *gpage)
{
	return gpage->entry;
}

static void set_dmem_entry(struct gpage_t *gpage, struct dmem_entry *entry)
{
	gpage->entry = entry;
}

static struct gpage_t * gpfn_to_gpage(struct gcma *gcma, unsigned long gpfn) {
	
	return gcma->gpages + (gpfn - gcma->base_pfn);
}

static unsigned long gpage_to_gpfn(struct gcma *gcma, struct gpage_t *gpage) {
	return gcma->base_pfn + ((unsigned long)gpage
		-(unsigned long)gcma->gpages)/sizeof(struct gpage_t);
}

static void __iomem * gpfn_iomap(struct gcma *gcma, unsigned long gpfn) {

	read_lock(&gcma->maping_rwlock);

	if(!IS_IN_RANGE(gpfn, gcma->mapped_gpfn, gcma->mapped_gpfn_cnt)) {

		read_unlock(&gcma->maping_rwlock);

		if(!IS_IN_RANGE(gpfn, gcma->base_pfn, gcma->size)) {
			pr_err("gpfn(0x%lx) is out of range! valid range %lx ~ %lx",
					gpfn, gcma->base_pfn, gcma->base_pfn + gcma->size - 1);
			return NULL;
		}

		write_lock(&gcma->maping_rwlock);

		if(gcma->mapped_virt) {
			__arm_iounmap(gcma->mapped_virt);
			gcma->mapped_virt = NULL;
		}

		gcma->mapped_gpfn = gpfn & IOREMAP_WINDOW_PFN_MASK;
		gcma->mapped_gpfn_cnt = IOREMAP_WINDOW_PFN_SIZE;

		if(gcma->base_pfn > gcma->mapped_gpfn) {
			gcma->mapped_gpfn = gcma->base_pfn;
			gcma->mapped_gpfn_cnt = IOREMAP_WINDOW_PFN_SIZE
				- (gcma->base_pfn - gcma->mapped_gpfn);
		}

		gcma->mapped_virt = __arm_ioremap_pfn(gcma->mapped_gpfn, 0,
				gcma->mapped_gpfn_cnt << PAGE_SHIFT, MT_MEMORY_RW);

		if(!gcma->mapped_virt) {
			pr_err("__arm_ioremap_pfn failed!");
			gcma->mapped_gpfn = 0;
			gcma->mapped_gpfn_cnt = 0;
			write_unlock(&gcma->maping_rwlock);
			return NULL;
		}
	}

	//pr_info("gpfn=%lx, mapped_gpfn=%lx, mapped_gpfn_cnt=%x, mapped_virt=0x%p, virt=0x%p\n",
	//	gpfn, gcma->mapped_gpfn, gcma->mapped_gpfn_cnt, gcma->mapped_virt,
	//	gcma->mapped_virt + (__pfn_to_phys(gpfn) - __pfn_to_phys(gcma->mapped_gpfn)));
	return gcma->mapped_virt + (__pfn_to_phys(gpfn) - __pfn_to_phys(gcma->mapped_gpfn));
}

static void gpfn_uniomap(struct gcma *gcma, void __iomem *virt) {
	if(read_can_lock(&gcma->maping_rwlock)) {
		read_unlock(&gcma->maping_rwlock);
	} else {
		write_unlock(&gcma->maping_rwlock);
	}
}

/*
 * Flags for status of a page in gcma
 *
 * GF_LRU
 * The page is being used for a dmem and hang on LRU list of the dmem.
 * It could be discarded for contiguous memory allocation easily.
 * Protected by dmem->lru_lock.
 *
 * GF_RECLAIMING
 * The page is being discarded for contiguous memory allocation.
 * It should not be used for dmem anymore.
 * Protected by dmem->lru_lock.
 *
 * GF_ISOLATED
 * The page is isolated from dmem.
 * GCMA clients can use the page safely while dmem should not.
 * Protected by gcma->lock.
 */
enum gpage_flags {
	GF_LRU = 0x1,
	GF_RECLAIMING = 0x2,
	GF_ISOLATED = 0x4,
};

static int gpage_flag(struct gpage_t *gpage, int flag)
{
	return gpage->flags & flag;
}

static void set_gpage_flag(struct gpage_t *gpage, int flag)
{
	gpage->flags |= flag;
}

static void clear_gpage_flag(struct gpage_t *gpage, int flag)
{
	gpage->flags &= ~flag;
}

static void clear_gpage_flagall(struct gpage_t *gpage)
{
	gpage->flags = 0;
}

/*
 * smem_cache_init - initializes a smem cache area
 *
 * @start_pfn	start pfn of contiguous memory area
 * @size	number of pages in the contiguous memory area
 * @res_gcma	pointer to store the created gcma region
 *
 * Returns 0 on success, error code on failure.
 */
int smem_cache_init(unsigned long start_pfn, unsigned long size,
		struct gcma **res_gcma)
{
	int bitmap_size = BITS_TO_LONGS(size) * sizeof(long);
	struct gcma *gcma;
	unsigned long flags;
	resource_size_t base = start_pfn << PAGE_SHIFT;

	gcma = kzalloc(sizeof(*gcma), GFP_KERNEL);
	if (!gcma)
		goto out;

	gcma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!gcma->bitmap)
		goto free_gcma;

	gcma->gpages = vzalloc(sizeof(struct gpage_t) * size);
	if(!gcma->gpages) {
		pr_err("gpages allocation failed\n");
		goto free_bitmap;
	}

	gcma->size = size;
	gcma->base_pfn = start_pfn;
	spin_lock_init(&gcma->lock);

	rwlock_init(&gcma->maping_rwlock);
#if 0
	/* pre mapping */
	if(!gpfn_iomap(gcma, start_pfn)) {
		pr_err("io remap failed! start_pfn=%lu, gcma->mapped_gpfn=%lu\n",
				start_pfn, gcma->mapped_gpfn);
		goto free_gpages;
	}
	gpfn_uniomap(gcma, dst);
#endif

	local_irq_save(flags);
	spin_lock(&ginfo.lock);
	list_add(&gcma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);
	local_irq_restore(flags);

	*res_gcma = gcma;
	pr_info("initialized smem cache area phys 0x%lx000 to 0x%lx000\n",
			start_pfn, start_pfn + size);
	return 0;

//free_gpages:
//	vfree(gcma->gpages);
free_bitmap:
	kfree(gcma->bitmap);
free_gcma:
	kfree(gcma);
out:
	return -ENOMEM;
}

static struct gpage_t *gcma_alloc_gpage(struct gcma *gcma)
{
	unsigned long bit;
	unsigned long *bitmap = gcma->bitmap;
	struct gpage_t *gpage = NULL;
	unsigned long flags;

	local_irq_save(flags);
	spin_lock(&gcma->lock);
	bit = bitmap_find_next_zero_area(bitmap, gcma->size, 0, 1, 0);
	if (bit >= gcma->size) {
		spin_unlock(&gcma->lock);
		goto out;
	}

	bitmap_set(bitmap, bit, 1);
	gpage = gpfn_to_gpage(gcma, gcma->base_pfn + bit);

	spin_unlock(&gcma->lock);
	clear_gpage_flagall(gpage);

out:
	local_irq_restore(flags);
	return gpage;
}

/* Caller should hold lru_lock */
static void gcma_free_gpage(struct gcma *gcma, struct gpage_t *gpage)
{
	unsigned long pfn, offset;
	unsigned long flags;

	pfn = gpage_to_gpfn(gcma, gpage);

	local_irq_save(flags);
	spin_lock(&gcma->lock);
	offset = pfn - gcma->base_pfn;

	if (likely(!gpage_flag(gpage, GF_RECLAIMING))) {
		bitmap_clear(gcma->bitmap, offset, 1);
	} else {
		/*
		 * The page should be safe to be used for a thread which
		 * reclaimed the page.
		 * To prevent further allocation from other thread,
		 * set bitmap and mark the page as isolated.
		 */
		bitmap_set(gcma->bitmap, offset, 1);
		set_gpage_flag(gpage, GF_ISOLATED);
	}
	spin_unlock(&gcma->lock);
	local_irq_restore(flags);
}

/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST.
 */
static int dmem_insert_entry(struct dmem_hashbucket *bucket,
		struct dmem_entry *entry,
		struct dmem_entry **dupentry)
{
	struct rb_node **link = &bucket->rbroot.rb_node, *parent = NULL;
	struct dmem_entry *iter;
	int cmp;

	while (*link) {
		parent = *link;
		iter = rb_entry(parent, struct dmem_entry, rbnode);
		cmp = bucket->dmem->compare(entry->key, iter->key);
		if (cmp < 0)
			link = &(*link)->rb_left;
		else if (cmp > 0)
			link = &(*link)->rb_right;
		else {
			*dupentry = iter;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, &bucket->rbroot);
	return 0;
}

static void dmem_erase_entry(struct dmem_hashbucket *bucket,
		struct dmem_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, &bucket->rbroot);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct dmem_entry *dmem_search_entry(struct dmem_hashbucket *bucket,
		void *key)
{
	struct rb_node *node = bucket->rbroot.rb_node;
	struct dmem_entry *iter;
	int cmp;

	while (node) {
		iter = rb_entry(node, struct dmem_entry, rbnode);
		cmp = bucket->dmem->compare(key, iter->key);
		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			return iter;
	}
	return NULL;
}

/* Allocates a page from gcma areas using round-robin way */
static struct gpage_t *dmem_alloc_gpage(struct dmem *dmem, struct gcma **res_gcma)
{
	struct gpage_t *gpage;
	struct gcma *gcma;
	unsigned long flags;

	local_irq_save(flags);
retry:
	spin_lock(&ginfo.lock);
	gcma = list_first_entry(&ginfo.head, struct gcma, list);
	list_move_tail(&gcma->list, &ginfo.head);

	list_for_each_entry(gcma, &ginfo.head, list) {

		gpage = gcma_alloc_gpage(gcma);
		if (gpage) {
			spin_unlock(&ginfo.lock);
			goto got;
		}
	}
	spin_unlock(&ginfo.lock);

	/*
	 * Failed to alloc a page from entire gcma. Evict adequate LRU
	 * discardable pages and try allocation again.
	 */
	if (dmem_evict_lru(dmem, NR_EVICT_BATCH))
		goto retry;

got:
	local_irq_restore(flags);
	*res_gcma = gcma;
	return gpage;
}

/* Should be called from dmem_put only */
static void dmem_free_entry(struct dmem *dmem, struct dmem_entry *entry)
{
	gcma_free_gpage(entry->gcma, entry->gpage);
	kmem_cache_free(dmem->key_cache, entry->key);
	kmem_cache_free(dmem_entry_cache, entry);
}

/* Caller should hold hashbucket spinlock */
static void dmem_get(struct dmem_entry *entry)
{
	atomic_inc(&entry->refcount);
}

/*
 * Caller should hold hashbucket spinlock and dmem lru_lock.
 * Remove from the bucket and free it, if nobody reference the entry.
 */
static void dmem_put(struct dmem_hashbucket *buck,
				struct dmem_entry *entry)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		struct gpage_t *gpage = entry->gpage;

		dmem_erase_entry(buck, entry);
		list_del(&gpage->lru);
		dmem_free_entry(buck->dmem, entry);
	}
}

/*
 * dmem_evict_lru - evict @nr_pages LRU dmem pages
 *
 * @dmem	dmem to evict LRU pages from
 * @nr_pages	number of LRU pages to be evicted
 *
 * Returns number of successfully evicted pages
 */
static unsigned long dmem_evict_lru(struct dmem *dmem, unsigned long nr_pages)
{
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct gpage_t *gpage, *n;
	unsigned long evicted = 0;
	u8 key[dmem->bytes_key];
	LIST_HEAD(free_gpages);

	spin_lock(&dmem->lru_lock);
	list_for_each_entry_safe_reverse(gpage, n, &dmem->lru_list, lru) {
		entry = dmem_entry(gpage);

		/*
		 * the entry could be free by other thread in the while.
		 * check whether the situation occurred and avoid others to
		 * free it by compare reference count and increase it
		 * atomically.
		 */
		if (!atomic_inc_not_zero(&entry->refcount))
			continue;

		clear_gpage_flag(gpage, GF_LRU);
		list_move(&gpage->lru, &free_gpages);
		if (++evicted >= nr_pages)
			break;
	}
	spin_unlock(&dmem->lru_lock);

	list_for_each_entry_safe(gpage, n, &free_gpages, lru) {
		buck = dmem_hashbuck(gpage);
		entry = dmem_entry(gpage);

		spin_lock(&buck->lock);
		spin_lock(&dmem->lru_lock);
		/* drop refcount increased by above loop */
		memcpy(&key, entry->key, dmem->bytes_key);
		dmem_put(buck, entry);
		/* free entry if the entry is still in tree */
		if (dmem_search_entry(buck, &key))
			dmem_put(buck, entry);
		spin_unlock(&dmem->lru_lock);
		spin_unlock(&buck->lock);
	}

	if (dmem == &fs_dmem)
		atomic_add(evicted, &gcma_fs_evicted_pages);
	else
		atomic_add(evicted, &gcma_cc_evicted_pages);
	return evicted;
}

/* Caller should hold bucket spinlock */
static struct dmem_entry *dmem_find_get_entry(struct dmem_hashbucket *buck,
						void *key)
{
	struct dmem_entry *entry;

	assert_spin_locked(&buck->lock);
	entry = dmem_search_entry(buck, key);
	if (entry)
		dmem_get(entry);

	return entry;
}

static struct dmem_hashbucket *dmem_find_hashbucket(struct dmem *dmem,
							struct dmem_pool *pool,
							void *key)
{
	return &pool->hashbuckets[dmem->hash_key(key)];
}

/*
 * dmem_init_pool - initialize a pool in dmem
 *
 * @dmem	dmem of a pool to be initialized
 * @pool_id	id of a pool to be initialized
 *
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int dmem_init_pool(struct dmem *dmem, unsigned pool_id)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	int i;

	pool = kzalloc(sizeof(struct dmem_pool), GFP_KERNEL);
	if (!pool) {
		pr_warn("%s: failed to alloc dmem pool %d\n",
				__func__, pool_id);
		return -ENOMEM;
	}

	pool->hashbuckets = kzalloc(
				sizeof(struct dmem_hashbucket) * dmem->nr_hash,
				GFP_KERNEL);
	if (!pool) {
		pr_warn("%s: failed to alloc hashbuckets\n", __func__);
		kfree(pool);
		return -ENOMEM;
	}

	for (i = 0; i < dmem->nr_hash; i++) {
		buck = &pool->hashbuckets[i];
		buck->dmem = dmem;
		buck->rbroot = RB_ROOT;

		/*
		 * Because lockdep recognizes lock class using lock
		 * initialization point, bucket lock of dmem for cleancache and
		 * frontswap be treated as same class.
		 * Because cleancache have dependency with softirq safe lock
		 * while frontswap doesn't, lockdep causes false irq lock
		 * inversion dependency report.
		 * Avoid the situation using this ugly, simple hack.
		 */
		if (dmem == &fs_dmem)
			spin_lock_init(&buck->lock);
		else
			spin_lock_init(&buck->lock);
	}

	dmem->pools[pool_id] = pool;
	return 0;
}

/*
 * dmem_store_page - store a page in dmem
 *
 * Saves content of @page in gcma area and manages it using dmem. The content
 * could be loaded again from dmem using @key if it has not been discarded for
 * first-class clients.
 *
 * @dmem	dmem to store the page in
 * @pool_id	id of a dmem pool to store the page in
 * @key		key of the page to be stored in
 * @page	page to be stored in
 *
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int dmem_store_page(struct dmem *dmem, unsigned pool_id, void *key,
			struct page *page)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry, *dupentry;
	struct gcma *gcma;
	struct gpage_t *gcma_page = NULL;

	u8 *src, *dst;
	int ret;

	pool = dmem->pools[pool_id];
	if (!pool) {
		pr_warn("%s: dmem pool for id %d is not exist\n",
				__func__, pool_id);
		return -ENODEV;
	}
//pr_err("%s: before dmem_alloc_gpage page %p\n", __FUNCTION__, page);
	gcma_page = dmem_alloc_gpage(dmem, &gcma);
	if (!gcma_page)
		return -ENOMEM;
//pr_err("%s: after dmem_alloc_gpage gcma_page %p\n", __FUNCTION__, gcma_page);
	entry = kmem_cache_alloc(dmem_entry_cache, GFP_ATOMIC);
	if (!entry) {
		spin_lock(&dmem->lru_lock);
		gcma_free_gpage(gcma, gcma_page);
		spin_unlock(&dmem->lru_lock);
		return -ENOMEM;
	}

	entry->gcma = gcma;
	entry->gpage = gcma_page;
	entry->key = kmem_cache_alloc(dmem->key_cache, GFP_ATOMIC);
	if (!entry->key) {
		spin_lock(&dmem->lru_lock);
		gcma_free_gpage(gcma, gcma_page);
		spin_unlock(&dmem->lru_lock);
		kmem_cache_free(dmem_entry_cache, entry);
		return -ENOMEM;
	}
	memcpy(entry->key, key, dmem->bytes_key);
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);

	buck = dmem_find_hashbucket(dmem, pool, entry->key);
	set_dmem_hashbuck(gcma_page, buck);
	set_dmem_entry(gcma_page, entry);

	/* copy from orig data to gcma_page */
	src = kmap_atomic(page);
	dst = gpfn_iomap(gcma, gpage_to_gpfn(gcma, gcma_page));
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	gpfn_uniomap(gcma, dst);

	spin_lock(&buck->lock);
	do {
		/*
		 * Though this duplication scenario may happen rarely by
		 * race of dmem client layer, we handle this case here rather
		 * than fix the client layer because handling the possibility
		 * of duplicates is part of the tmem ABI.
		 */
		ret = dmem_insert_entry(buck, entry, &dupentry);
		if (ret == -EEXIST) {
			dmem_erase_entry(buck, dupentry);
			spin_lock(&dmem->lru_lock);
			dmem_put(buck, dupentry);
			spin_unlock(&dmem->lru_lock);
		}
	} while (ret == -EEXIST);

	spin_lock(&dmem->lru_lock);
	set_gpage_flag(gcma_page, GF_LRU);
	list_add(&gcma_page->lru, &dmem->lru_list);
	spin_unlock(&dmem->lru_lock);
	spin_unlock(&buck->lock);

	return ret;
}

/*
 * dmem_load_page - load a page stored in dmem using @key
 *
 * @dmem	dmem which the page stored in
 * @pool_id	id of a dmem pool the page stored in
 * @key		key of the page looking for
 * @page	page to store the loaded content
 *
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int dmem_load_page(struct dmem *dmem, unsigned pool_id, void *key,
			struct page *page)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct gcma *gcma = NULL;
	struct gpage_t *gcma_page;
	u8 *src, *dst;

	unsigned long long time_start, time_end, time_map_end, time_copy_end;

	pool = dmem->pools[pool_id];
	if (!pool) {
		pr_warn("dmem pool for id %d not exist\n", pool_id);
		return -1;
	}

	buck = dmem_find_hashbucket(dmem, pool, key);

	spin_lock(&buck->lock);
	entry = dmem_find_get_entry(buck, key);
	spin_unlock(&buck->lock);
	if (!entry)
		return -1;

	gcma = entry->gcma;
	gcma_page = entry->gpage;

time_start = __get_nsecs();
	src = gpfn_iomap(gcma, gpage_to_gpfn(gcma, gcma_page));
	dst = kmap_atomic(page);

time_map_end = __get_nsecs();
	memcpy(dst, src, PAGE_SIZE);
time_copy_end = __get_nsecs();
	gpfn_uniomap(gcma, src);
	kunmap_atomic(dst);
time_end = __get_nsecs();

	atomic_add((time_map_end - time_start + time_end - time_copy_end), &gcma_cc_time_loaded_pages_map);
	atomic_add((time_copy_end - time_map_end), &gcma_cc_time_loaded_pages_copy);

	spin_lock(&buck->lock);
	spin_lock(&dmem->lru_lock);
	if (likely(gpage_flag(gcma_page, GF_LRU)))
		list_move(&gcma_page->lru, &dmem->lru_list);
	dmem_put(buck, entry);
	spin_unlock(&dmem->lru_lock);
	spin_unlock(&buck->lock);

	return 0;
}

/*
 * dmem_invalidate_entry - invalidates an entry from dmem
 *
 * @dmem	dmem of entry to be invalidated
 * @pool_id	dmem pool id of entry to be invalidated
 * @key		key of entry to be invalidated
 *
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int dmem_invalidate_entry(struct dmem *dmem, unsigned pool_id, void *key)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;

	pool = dmem->pools[pool_id];
	buck = dmem_find_hashbucket(dmem, pool, key);

	spin_lock(&buck->lock);
	entry = dmem_search_entry(buck, key);
	if (!entry) {
		spin_unlock(&buck->lock);
		return -ENOENT;
	}

	spin_lock(&dmem->lru_lock);
	dmem_put(buck, entry);
	spin_unlock(&dmem->lru_lock);
	spin_unlock(&buck->lock);

	return 0;
}

/*
 * dmem_invalidate_pool - invalidates whole entries in a dmem pool
 *
 * @dmem	dmem of pool to be invalidated
 * @pool_id	id of pool to be invalidated
 *
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int dmem_invalidate_pool(struct dmem *dmem, unsigned pool_id)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry, *n;
	int i;

	pool = dmem->pools[pool_id];
	if (!pool)
		return -1;

	for (i = 0; i < dmem->nr_hash; i++) {
		buck = &pool->hashbuckets[i];
		spin_lock(&buck->lock);
		rbtree_postorder_for_each_entry_safe(entry, n, &buck->rbroot,
							rbnode) {
			spin_lock(&dmem->lru_lock);
			dmem_put(buck, entry);
			spin_unlock(&dmem->lru_lock);
		}
		buck->rbroot = RB_ROOT;
		spin_unlock(&buck->lock);
	}

	kfree(pool->hashbuckets);
	kfree(pool);
	dmem->pools[pool_id] = NULL;

	return 0;
}


static int frontswap_compare(void *lkey, void *rkey)
{
	return *(pgoff_t *)lkey - *(pgoff_t *)rkey;
}

static unsigned frontswap_hash_key(void *key)
{
	return *(pgoff_t *)key % fs_dmem.nr_hash;
}

void smem_frontswap_init(unsigned type)
{
	dmem_init_pool(&fs_dmem, type);
	atomic_inc(&gcma_fs_inits);
}

int smem_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	int ret;

	ret = dmem_store_page(&fs_dmem, type, (void *)&offset, page);
	if (ret == 0)
		atomic_inc(&gcma_fs_stored_pages);
	return ret;
}

/*
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int smem_frontswap_load(unsigned type, pgoff_t offset,
			       struct page *page)
{
	int ret;

	ret = dmem_load_page(&fs_dmem, type, (void *)&offset, page);
	if (ret == 0)
		atomic_inc(&gcma_fs_loaded_pages);
	return ret;
}

void smem_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	if (dmem_invalidate_entry(&fs_dmem, type, (void *)&offset) == 0)
		atomic_inc(&gcma_fs_invalidated_pages);
}

void smem_frontswap_invalidate_area(unsigned type)
{
	if (dmem_invalidate_pool(&fs_dmem, type) == 0)
		atomic_inc(&gcma_fs_invalidated_areas);
}

static struct frontswap_ops smem_frontswap_ops = {
	.init = smem_frontswap_init,
	.store = smem_frontswap_store,
	.load = smem_frontswap_load,
	.invalidate_page = smem_frontswap_invalidate_page,
	.invalidate_area = smem_frontswap_invalidate_area
};


static int cleancache_compare(void *lkey, void *rkey)
{
	/* Frontswap uses pgoff_t value as key */
	return memcmp(lkey, rkey, BYTES_CC_DMEM_KEY);
}

static unsigned int cleancache_hash_key(void *key)
{
	unsigned long *k = (unsigned long *)key;

	return hash_long(k[0] ^ k[1] ^ k[2], BITS_CC_DMEM_HASH);
}

static void cleancache_set_key(struct cleancache_filekey *fkey, pgoff_t *offset,
				void *key)
{
	memcpy(key, offset, sizeof(pgoff_t));
	memcpy(key + sizeof(pgoff_t), fkey, sizeof(struct cleancache_filekey));
}


/* Returns positive pool id or negative error code */
int smem_cleancache_init_fs(size_t pagesize)
{
	int pool_id;
	int err;

	pool_id = atomic_inc_return(&nr_cleancache_fses) - 1;
	if (pool_id >= MAX_CLEANCACHE_FS) {
		pr_warn("%s: too many cleancache fs %d / %d\n",
				__func__, pool_id, MAX_CLEANCACHE_FS);
		return -1;
	}

	err = dmem_init_pool(&cc_dmem, pool_id);
	if (err != 0)
		return err;

	atomic_inc(&gcma_cc_inits);
	return pool_id;
}

int smem_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	return -1;
}

int smem_cleancache_get_page(int pool_id, struct cleancache_filekey fkey,
				pgoff_t offset, struct page *page)
{
	struct cleancache_dmem_key key;
	int ret;
	unsigned long flags;

	unsigned long long time;

	time = __get_nsecs();
	cleancache_set_key(&fkey, &offset, &key);

	local_irq_save(flags);
	ret = dmem_load_page(&cc_dmem, pool_id, &key, page);
	local_irq_restore(flags);
	if (ret == 0) {
		atomic_inc(&gcma_cc_loaded_pages);
		atomic_add(__get_nsecs() - time, &gcma_cc_time_loaded_pages_total);
	}
	else
		atomic_inc(&gcma_cc_load_failed_pages);
		atomic_add(__get_nsecs() - time, &gcma_cc_time_load_failed_pages_total);
	return ret;
}

void smem_cleancache_put_page(int pool_id, struct cleancache_filekey fkey,
				pgoff_t offset, struct page *page)
{
	struct cleancache_dmem_key key;
	unsigned long flags;

	cleancache_set_key(&fkey, &offset, &key);

	local_irq_save(flags);

	if (dmem_store_page(&cc_dmem, pool_id, &key, page) == 0)
		atomic_inc(&gcma_cc_stored_pages);
	local_irq_restore(flags);
}

void smem_cleancache_invalidate_page(int pool_id,
					struct cleancache_filekey fkey,
					pgoff_t offset)
{
	struct cleancache_dmem_key key;
	unsigned long flags;

	cleancache_set_key(&fkey, &offset, &key);

	local_irq_save(flags);
	if (dmem_invalidate_entry(&cc_dmem, pool_id, &key) == 0)
		atomic_inc(&gcma_cc_invalidated_pages);
	local_irq_restore(flags);
}

/*
 * Invalidating every entry of an filekey from a dmem pool requires iterating
 * and comparing key of every entry in the pool; it could be too expensive. To
 * alleviates the overhead, do nothing here. The entry will be evicted in LRU
 * order anyway.
 */
void smem_cleancache_invalidate_inode(int pool_id,
					struct cleancache_filekey key)
{
}

void smem_cleancache_invalidate_fs(int pool_id)
{
	unsigned long flags;

	if (pool_id < 0 || pool_id >= atomic_read(&nr_cleancache_fses)) {
		pr_warn("%s received wrong pool id %d\n",
				__func__, pool_id);
		atomic_inc(&gcma_cc_invalidate_failed_fses);
		return;
	}
	local_irq_save(flags);
	if (dmem_invalidate_pool(&cc_dmem, pool_id) == 0)
		atomic_inc(&gcma_cc_invalidated_fses);
	local_irq_restore(flags);
}

struct cleancache_ops smem_cleancache_ops = {
	.init_fs = smem_cleancache_init_fs,
	.init_shared_fs = smem_cleancache_init_shared_fs,
	.get_page = smem_cleancache_get_page,
	.put_page = smem_cleancache_put_page,
	.invalidate_page = smem_cleancache_invalidate_page,
	.invalidate_inode = smem_cleancache_invalidate_inode,
	.invalidate_fs = smem_cleancache_invalidate_fs,
};


/*
 * Return 0 if [start_pfn, end_pfn] is isolated.
 * Otherwise, return first unisolated pfn from the start_pfn.
 */
static unsigned long isolate_interrupted(struct gcma *gcma,
		unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long offset;
	unsigned long *bitmap;
	unsigned long pfn, ret = 0;
	struct gpage_t *gpage;
	unsigned long flags;

	local_irq_save(flags);
	spin_lock(&gcma->lock);

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		int set;

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;

		set = test_bit(pfn % BITS_PER_LONG, bitmap);
		if (!set) {
			ret = pfn;
			break;
		}

		gpage = gpfn_to_gpage(gcma, pfn);
		if (!gpage_flag(gpage, GF_ISOLATED)) {
			ret = pfn;
			break;
		}

	}
	spin_unlock(&gcma->lock);
	local_irq_restore(flags);
	return ret;
}

/*
 * smem_cache_claim_area - claim mem area using for device.
 *
 * @start_pfn	start pfn of requiring contiguous memory area
 * @size	size of the requiring contiguous memory area
 *
 * Returns 0 on success, error code on failure.
 */
int smem_cache_claim_area(struct gcma *gcma, unsigned long start_pfn,
			unsigned long size)
{
	LIST_HEAD(free_gpages);
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct cleancache_dmem_key key;	/* cc key is larger than fs's */
	struct gpage_t *gpage, *n;
	unsigned long offset;
	unsigned long *bitmap;
	unsigned long pfn;
	unsigned long orig_start = start_pfn;
	spinlock_t *lru_lock;
	unsigned long flags = 0;

retry:
	for (pfn = start_pfn; pfn < start_pfn + size; pfn++) {
		local_irq_save(flags);
		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;
		gpage = gpfn_to_gpage(gcma, pfn);

		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			/* set a bit to prevent allocation for dmem */
			bitmap_set(gcma->bitmap, offset, 1);
			set_gpage_flag(gpage, GF_ISOLATED);
			spin_unlock(&gcma->lock);
			local_irq_restore(flags);
			continue;
		}
		if (gpage_flag(gpage, GF_ISOLATED)) {
			spin_unlock(&gcma->lock);
			local_irq_restore(flags);
			continue;
		}

		/* Someone is using the page so it's complicated :( */
		spin_unlock(&gcma->lock);
		local_irq_restore(flags);

		/* During dmem_store, hashbuck could not be set in page, yet */
		if (dmem_hashbuck(gpage) == NULL)
			continue;

		lru_lock = &dmem_hashbuck(gpage)->dmem->lru_lock;
		local_irq_save(flags);
		spin_lock(lru_lock);
		spin_lock(&gcma->lock);

		/* Avoid allocation from other threads */
		set_gpage_flag(gpage, GF_RECLAIMING);

		/*
		 * The page is in LRU and being used by someone. Discard it
		 * after removing from lru_list.
		 */
		if (gpage_flag(gpage, GF_LRU)) {
			entry = dmem_entry(gpage);
			if (atomic_inc_not_zero(&entry->refcount)) {
				clear_gpage_flag(gpage, GF_LRU);
				list_move(&gpage->lru, &free_gpages);
				goto next_page;
			}
		}

		/*
		 * The page is
		 * 1) allocated by others but not yet in LRU in case of
		 *    dmem_store or
		 * 2) deleted from LRU but not yet from gcma's bitmap in case
		 *    of dmem_invalidate or dmem_evict_lru.
		 * Anycase, the race is small so retry after a while will see
		 * success. Below isolate_interrupted handles it.
		 */
next_page:
		spin_unlock(&gcma->lock);
		spin_unlock(lru_lock);
		local_irq_restore(flags);
	}

	/*
	 * Since we increased refcount of the page above, we can access
	 * dmem_entry with safe.
	 */
	list_for_each_entry_safe(gpage, n, &free_gpages, lru) {
		buck = dmem_hashbuck(gpage);
		entry = dmem_entry(gpage);
		lru_lock = &dmem_hashbuck(gpage)->dmem->lru_lock;

		if (lru_lock == &cc_dmem.lru_lock)
			local_irq_save(flags);
		spin_lock(&buck->lock);
		spin_lock(lru_lock);
		/* drop refcount increased by above loop */
		memcpy(&key, entry->key, dmem_hashbuck(gpage)->dmem->bytes_key);
		dmem_put(buck, entry);
		/* free entry if the entry is still in tree */
		if (dmem_search_entry(buck, &key))
			dmem_put(buck, entry);
		spin_unlock(lru_lock);
		spin_unlock(&buck->lock);
		if (lru_lock == &cc_dmem.lru_lock) {
			local_irq_restore(flags);
			atomic_inc(&gcma_cc_reclaimed_pages);
		} else {
			atomic_inc(&gcma_fs_reclaimed_pages);
		}
	}

	start_pfn = isolate_interrupted(gcma, orig_start, orig_start + size);
	if (start_pfn)
		goto retry;

	return 0;
}

/*
 * smem_cache_release_area - release mem area used for device.
 *
 * @start_pfn	start pfn of freeing contiguous memory area
 * @size	number of pages in freeing contiguous memory area
 */
void smem_cache_release_area(struct gcma *gcma,
			unsigned long start_pfn, unsigned long size)
{
	unsigned long offset;
	unsigned long flags;

	local_irq_save(flags);
	spin_lock(&gcma->lock);
	offset = start_pfn - gcma->base_pfn;
	bitmap_clear(gcma->bitmap, offset, size);
	spin_unlock(&gcma->lock);
	local_irq_restore(flags);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *gcma_debugfs_root;

static int __init gcma_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	gcma_debugfs_root = debugfs_create_dir("smem_cache", NULL);
	if (!gcma_debugfs_root)
		return -ENOMEM;

	debugfs_create_atomic_t("fs_inits", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_inits);
	debugfs_create_atomic_t("fs_stored_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_stored_pages);
	debugfs_create_atomic_t("fs_loaded_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_loaded_pages);
	debugfs_create_atomic_t("fs_evicted_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_evicted_pages);
	debugfs_create_atomic_t("fs_reclaimed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_reclaimed_pages);
	debugfs_create_atomic_t("fs_invalidated_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_invalidated_pages);
	debugfs_create_atomic_t("fs_invalidated_areas", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_invalidated_areas);

	debugfs_create_atomic_t("cc_inits", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_inits);
	debugfs_create_atomic_t("cc_stored_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_stored_pages);
	debugfs_create_atomic_t("cc_loaded_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_loaded_pages);
	debugfs_create_atomic_t("cc_load_failed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_load_failed_pages);
	debugfs_create_atomic_t("cc_evicted_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_evicted_pages);
	debugfs_create_atomic_t("cc_reclaimed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_reclaimed_pages);
	debugfs_create_atomic_t("cc_invalidated_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidated_pages);
	debugfs_create_atomic_t("cc_invalidated_inodes", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidated_inodes);
	debugfs_create_atomic_t("cc_invalidated_fses", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidated_fses);
	debugfs_create_atomic_t("cc_invalidate_failed_fses", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_failed_fses);

	debugfs_create_atomic_t("cc_time_loaded_pages_total", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_time_loaded_pages_total);
	debugfs_create_atomic_t("cc_time_load_failed_pages_total", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_time_load_failed_pages_total);
	debugfs_create_atomic_t("cc_time_loaded_pages_map", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_time_loaded_pages_map);
	debugfs_create_atomic_t("cc_time_loaded_pages_copy", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_time_loaded_pages_copy);

	pr_info("smem cache debufs init\n");
	return 0;
}
#else
static int __init gcma_debugfs_init(void)
{
	return 0;
}
#endif

static int __init init_gcma(void)
{
	pr_err("WARNING! SMEM Cache is enabled. this is currently test version!!\n");
	pr_info("register cleancache/frontswap ops\n");

	dmem_entry_cache = KMEM_CACHE(dmem_entry, 0);
	if (dmem_entry_cache == NULL)
		return -ENOMEM;

	if (fs_disabled) {
		pr_info("gcma frontswap is disabled. skip it\n");
		goto init_cleancache;
	}
	fs_dmem.nr_pools = MAX_SWAPFILES;
	fs_dmem.pools = kzalloc(sizeof(struct dmem_pool *) * fs_dmem.nr_pools,
				GFP_KERNEL);
	if (!fs_dmem.pools) {
		pr_warn("failed to allocate frontswap dmem pools\n");
		return -ENOMEM;
	}

	fs_dmem.nr_hash = NR_FS_DMEM_HASH_BUCKS;
	fs_dmem.key_cache = KMEM_CACHE(frontswap_dmem_key, 0);
	if (!fs_dmem.key_cache)
		return -ENOMEM;
	fs_dmem.bytes_key = BYTES_FS_DMEM_KEY;

	INIT_LIST_HEAD(&fs_dmem.lru_list);
	spin_lock_init(&fs_dmem.lru_lock);

	fs_dmem.hash_key = frontswap_hash_key;
	fs_dmem.compare = frontswap_compare;

	/*
	 * By writethough mode, GCMA could discard all of pages in an instant
	 * instead of slow writing pages out to the swap device.
	 */
	frontswap_writethrough(true);
	frontswap_register_ops(&smem_frontswap_ops);

init_cleancache:
	if (cc_disabled) {
		pr_info("gcma cleancache is disabled. skip it\n");
		goto init_debugfs;
	}
	cc_dmem.nr_pools = MAX_CLEANCACHE_FS;
	cc_dmem.pools = kzalloc(sizeof(struct dmem_pool *) * cc_dmem.nr_pools,
				GFP_KERNEL);
	if (!cc_dmem.pools) {
		pr_warn("failed to allocate cleancache dmem pools\n");
		return -ENOMEM;
	}
	cc_dmem.nr_hash = NR_CC_DMEM_HASH_BUCKS;
	cc_dmem.key_cache = KMEM_CACHE(cleancache_dmem_key, 0);
	if (!cc_dmem.key_cache)
		return -ENOMEM;
	cc_dmem.bytes_key = BYTES_CC_DMEM_KEY;

	INIT_LIST_HEAD(&cc_dmem.lru_list);
	spin_lock_init(&cc_dmem.lru_lock);

	cc_dmem.hash_key = cleancache_hash_key;
	cc_dmem.compare = cleancache_compare;
	cleancache_register_ops(&smem_cleancache_ops);

init_debugfs:
	gcma_debugfs_init();
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
