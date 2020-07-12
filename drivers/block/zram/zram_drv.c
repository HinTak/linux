/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *               2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#define KMSG_COMPONENT "zram"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#ifdef CONFIG_ZRAM_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#ifdef CONFIG_BACKGROUND_RECOMPRESS
#include <linux/background_recompress.h>
#endif

#include "zram_drv.h"

/* Globals */
static int zram_major;
static struct zram *zram_devices;
static const char *default_compressor = 
#ifdef CONFIG_CRYPTO_ZSTD
"zstd";
#else
"lz4";
#endif
static const char *zram_allocator =
#ifndef CONFIG_ZRAM_Z4FOLD
"zsmalloc";
#else
"z4fold";
#endif

#ifdef CONFIG_ZRAM_DISKSIZE
#define ZRAM_DEFAULT_DISKSIZE CONFIG_ZRAM_DISKSIZE
#define ZRAM_ENTRY_MODEL_DISKSIZE "65%"
#else
#define ZRAM_DEFAULT_DISKSIZE "1G"
#endif

static bool is_entry;
/* Module params (documentation at end) */
static unsigned int num_devices = 1;

static inline void deprecated_attr_warn(const char *name)
{
	pr_warn_once("%d (%s) Attribute %s (and others) will be removed. %s\n",
			task_pid_nr(current),
			current->comm,
			name,
			"See zram documentation.");
}

#define ZRAM_ATTR_RO(name)						\
static ssize_t name##_show(struct device *d,		\
				struct device_attribute *attr, char *b)	\
{									\
	struct zram *zram = dev_to_zram(d);				\
									\
	deprecated_attr_warn(__stringify(name));			\
	return scnprintf(b, PAGE_SIZE, "%llu\n",			\
		(u64)atomic64_read(&zram->stats.name));			\
}									\
static DEVICE_ATTR_RO(name);

static inline bool init_done(struct zram *zram)
{
	return zram->disksize;
}

static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}
#ifndef CONFIG_ZRAM_Z4FOLD
static ssize_t compact_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	struct zram_meta *meta;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	meta = zram->meta;
	zs_compact(meta->mem_pool);
	up_read(&zram->init_lock);

	return len;
}
#endif

static ssize_t disksize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", zram->disksize);
}

static ssize_t initstate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 val;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	val = init_done(zram);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t orig_data_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	deprecated_attr_warn("orig_data_size");
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
		(u64)(atomic64_read(&zram->stats.pages_stored)) << PAGE_SHIFT);
}

static ssize_t mem_used_total_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u64 val = 0;
	struct zram *zram = dev_to_zram(dev);

	deprecated_attr_warn("mem_used_total");
	down_read(&zram->init_lock);
	if (init_done(zram)) {
		struct zram_meta *meta = zram->meta;
#ifndef CONFIG_ZRAM_Z4FOLD
		val = zs_get_total_pages(meta->mem_pool);
#else 
		val = z4fold_get_pool_size(meta->mem_pool);
#endif
	}
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", val << PAGE_SHIFT);
}

/*
 * We switched to per-cpu streams and this attr is not needed anymore.
 * However, we will keep it around for some time, because:
 * a) we may revert per-cpu streams in the future
 * b) it's visible to user space and we need to follow our 2 years
 *    retirement rule; but we already have a number of 'soon to be
 *    altered' attrs, so max_comp_streams need to wait for the next
 *    layoff cycle.
 */
static ssize_t max_comp_streams_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", num_online_cpus());
}

static ssize_t mem_limit_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u64 val;
	struct zram *zram = dev_to_zram(dev);

	deprecated_attr_warn("mem_limit");
	down_read(&zram->init_lock);
	val = zram->limit_pages;
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", val << PAGE_SHIFT);
}

static ssize_t mem_limit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 limit;
	char *tmp;
	struct zram *zram = dev_to_zram(dev);

	limit = memparse(buf, &tmp);
	if (buf == tmp) /* no chars parsed, invalid input */
		return -EINVAL;

	down_write(&zram->init_lock);
	zram->limit_pages = PAGE_ALIGN(limit) >> PAGE_SHIFT;
	up_write(&zram->init_lock);

	return len;
}

static ssize_t mem_used_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u64 val = 0;
	struct zram *zram = dev_to_zram(dev);

	deprecated_attr_warn("mem_used_max");
	down_read(&zram->init_lock);
	if (init_done(zram))
		val = atomic_long_read(&zram->stats.max_used_pages);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", val << PAGE_SHIFT);
}

static ssize_t mem_used_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	err = kstrtoul(buf, 10, &val);
	if (err || val != 0)
		return -EINVAL;

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		struct zram_meta *meta = zram->meta;
#ifndef CONFIG_ZRAM_Z4FOLD
		atomic_long_set(&zram->stats.max_used_pages,
				zs_get_total_pages(meta->mem_pool));
#else
		atomic_long_set(&zram->stats.max_used_pages,
				z4fold_get_pool_size(meta->mem_pool));
#endif
	}
	up_read(&zram->init_lock);

	return len;
}

static ssize_t max_comp_streams_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	return len;
}

static ssize_t comp_algorithm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t sz;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	sz = zcomp_available_show(zram->compressor, buf);
	up_read(&zram->init_lock);

	return sz;
}

static ssize_t comp_algorithm_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char compressor[CRYPTO_MAX_ALG_NAME];
    size_t sz;

	strlcpy(compressor, buf, sizeof(compressor));
	/* ignore trailing newline */
	sz = strlen(compressor);
	if (sz > 0 && compressor[sz - 1] == '\n')
		compressor[sz - 1] = 0x00;

	if (!zcomp_available_algorithm(compressor))
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		up_write(&zram->init_lock);
		pr_info("Can't change algorithm for initialized device\n");
		return -EBUSY;
	}

	strlcpy(zram->compressor, compressor, sizeof(compressor));
	up_write(&zram->init_lock);
	return len;
}

static ssize_t mem_allocator_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t sz = 0;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2, "%s", zram->mem_allocator);
	up_read(&zram->init_lock);

	sz += scnprintf(buf + sz, PAGE_SIZE - sz, "\n");
	return sz;
}

static ssize_t mem_allocator_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char allocator[ZRAM_MAX_ALLOCATOR_NAME];
    size_t sz;

	strlcpy(allocator, buf, sizeof(allocator));
	/* ignore trailing newline */
	sz = strlen(allocator);
	if (sz > 0 && allocator[sz - 1] == '\n')
		allocator[sz - 1] = 0x00;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		up_write(&zram->init_lock);
		pr_info("Can't change allocator for initialized device\n");
		return -EBUSY;
	}

	strlcpy(zram->mem_allocator, allocator, sizeof(allocator));
	up_write(&zram->init_lock);
	return len;
}

#ifdef CONFIG_BACKGROUND_RECOMPRESS
static ssize_t allocator_chunk_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t ret = 0;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		struct zram_meta *meta = zram->meta;
#ifdef CONFIG_ZRAM_Z4FOLD
		ret = z4fold_get_chunk_info(meta->mem_pool, buf);
#endif
	}
	up_read(&zram->init_lock);

	return ret;
}

static ssize_t recompress_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t ret = 0;
	struct zram *zram = dev_to_zram(dev);
	u64 org, recomp;

	if (dynamic_bgrecomp_enable) {
		down_read(&zram->init_lock);
		org = (u64)atomic64_read(&zram->stats.recomp_org_data_size);
		recomp = (u64)atomic64_read(&zram->stats.recomp_data_size);

		ret = scnprintf(buf, PAGE_SIZE,
				"%12s %12s %12s\n"
				"%12llu %12llu %12llu\n",
				"origin", "recompress", "saved",
				org, recomp, org-recomp);
		up_read(&zram->init_lock);
	} else {
		ret = scnprintf(buf, PAGE_SIZE,	"disabled\n");
	}

	return ret;
}
#endif

/* flag operations needs meta->tb_lock */
static int zram_test_flag(struct zram_meta *meta, u32 index,
			enum zram_pageflags flag)
{
	return meta->table[index].value & BIT(flag);
}

static void zram_set_flag(struct zram_meta *meta, u32 index,
			enum zram_pageflags flag)
{
	meta->table[index].value |= BIT(flag);
}

static void zram_clear_flag(struct zram_meta *meta, u32 index,
			enum zram_pageflags flag)
{
	meta->table[index].value &= ~BIT(flag);
}

static inline void zram_set_element(struct zram_meta *meta, u32 index,
			unsigned long element)
{
	meta->table[index].element = element;
}

static inline void zram_clear_element(struct zram_meta *meta, u32 index)
{
	meta->table[index].element = 0;
}

static size_t zram_get_obj_size(struct zram_meta *meta, u32 index)
{
	return meta->table[index].value & (BIT(ZRAM_FLAG_SHIFT) - 1);
}

static void zram_set_obj_size(struct zram_meta *meta,
					u32 index, size_t size)
{
	unsigned long flags = meta->table[index].value >> ZRAM_FLAG_SHIFT;

	meta->table[index].value = (flags << ZRAM_FLAG_SHIFT) | size;
}

static inline int is_partial_io(struct bio_vec *bvec)
{
	return bvec->bv_len != PAGE_SIZE;
}

/* Hook point for background recompress. */
#ifdef CONFIG_BACKGROUND_RECOMPRESS
void zram_bgrecompress_store(unsigned long handle, u32 index, unsigned int clen)
{
	struct bgrecomp_handle *recomp_handle = (struct bgrecomp_handle *)handle;

	INIT_LIST_HEAD(&recomp_handle->bgrecomp_lru);

	if (clen < get_bgrecomp_min() || clen > get_bgrecomp_max())
		return;

	recomp_handle->index = index;
	recomp_handle->data = bgrecompd_store_data(clen);
	bgrecompress_list_store(&recomp_handle->bgrecomp_lru);
}

void zram_bgrecompress_invalidate(unsigned long handle)
{
	struct bgrecomp_handle *recomp_handle = (struct bgrecomp_handle *)handle;

	if (recomp_handle->index && recomp_handle->data)
		bgrecompress_invalidate(&recomp_handle->bgrecomp_lru);
}
#endif

/*
 * Check if request is within bounds and aligned on zram logical blocks.
 */
static inline int valid_io_request(struct zram *zram,
		sector_t start, unsigned int size)
{
	u64 end, bound;

	/* unaligned request */
	if (unlikely(start & (ZRAM_SECTOR_PER_LOGICAL_BLOCK - 1)))
		return 0;
	if (unlikely(size & (ZRAM_LOGICAL_BLOCK_SIZE - 1)))
		return 0;

	end = start + (size >> SECTOR_SHIFT);
	bound = zram->disksize >> SECTOR_SHIFT;
	/* out of range range */
	if (unlikely(start >= bound || end > bound || start > end))
		return 0;

	/* I/O request is valid */
	return 1;
}

static void zram_meta_free(struct zram_meta *meta, u64 disksize)
{
	size_t num_pages = disksize >> PAGE_SHIFT;
	size_t index;

	/* Free all pages that are still in this zram device */
	for (index = 0; index < num_pages; index++) {
		unsigned long handle = meta->table[index].handle;
		/*
		 * No memory is allocated for same element filled pages.
		 * Simply clear same page flag.
		 */
		if (!handle || zram_test_flag(meta, index, ZRAM_SAME))
			continue;

#ifndef CONFIG_ZRAM_Z4FOLD
		zs_free(meta->mem_pool, handle);
#else
		z4fold_free(meta->mem_pool, handle);
#endif
	}
#ifndef CONFIG_ZRAM_Z4FOLD
	zs_destroy_pool(meta->mem_pool);
#else
	z4fold_destroy_pool(meta->mem_pool);
#endif
	vfree(meta->table);
	kfree(meta);
}

static struct zram_meta *zram_meta_alloc(int device_id, u64 disksize)
{
	size_t num_pages;
	char pool_name[8];
	struct zram_meta *meta = kmalloc(sizeof(*meta), GFP_KERNEL);

	if (!meta)
		return NULL;

	num_pages = disksize >> PAGE_SHIFT;
	meta->table = vzalloc(num_pages * sizeof(*meta->table));
	if (!meta->table) {
		pr_err("Error allocating zram address table\n");
		goto out_error;
	}

	snprintf(pool_name, sizeof(pool_name), "zram%d", device_id);
#ifndef CONFIG_ZRAM_Z4FOLD
	meta->mem_pool = zs_create_pool(pool_name);
#else
	meta->mem_pool = z4fold_create_pool(pool_name, 
					__GFP_NORETRY | __GFP_NOWARN, NULL);
#endif
	if (!meta->mem_pool) {
		pr_err("Error creating memory pool\n");
		goto out_error;
	}

	return meta;

out_error:
	vfree(meta->table);
	kfree(meta);
	return NULL;
}

static inline bool zram_meta_get(struct zram *zram)
{
	if (atomic_inc_not_zero(&zram->refcount))
		return true;
	return false;
}

static inline void zram_meta_put(struct zram *zram)
{
	atomic_dec(&zram->refcount);
}

static void update_position(u32 *index, int *offset, struct bio_vec *bvec)
{
	if (*offset + bvec->bv_len >= PAGE_SIZE)
		(*index)++;
	*offset = (*offset + bvec->bv_len) % PAGE_SIZE;
}

static inline void zram_fill_page(char *ptr, unsigned long len,
                   unsigned long value)
{
   int i;
   unsigned long *page = (unsigned long *)ptr;

   WARN_ON_ONCE(!IS_ALIGNED(len, sizeof(unsigned long)));

   if (likely(value == 0)) {
       memset(ptr, 0, len);
   } else {
       for (i = 0; i < len / sizeof(*page); i++)
           page[i] = value;
   }
}

static bool page_same_filled(void *ptr, unsigned long *element)
{
	unsigned int pos;
	unsigned long *page;

	page = (unsigned long *)ptr;

   for (pos = 0; pos < PAGE_SIZE / sizeof(*page) - 1; pos++) {
       if (page[pos] != page[pos + 1])
			return false;
	}

   *element = page[pos];

	return true;
}

static void handle_same_page(struct bio_vec *bvec, unsigned long element)
{
	struct page *page = bvec->bv_page;
	unsigned char *user_mem;

	user_mem = kmap_atomic(page);
	zram_fill_page(user_mem + bvec->bv_offset, bvec->bv_len, element);
	kunmap_atomic(user_mem);

	flush_dcache_page(page);
}


/*
 * To protect concurrent access to the same index entry,
 * caller should hold this table index entry's bit_spinlock to
 * indicate this index entry is accessing.
 */
static void zram_free_page(struct zram *zram, size_t index)
{
	struct zram_meta *meta = zram->meta;
	unsigned long handle = meta->table[index].handle;

	/*
	 * No memory is allocated for same element filled pages.
	 * Simply clear same page flag.
	 */
	if (zram_test_flag(meta, index, ZRAM_SAME)) {
		zram_clear_flag(meta, index, ZRAM_SAME);
		zram_clear_element(meta, index);
		atomic64_dec(&zram->stats.same_pages);
		return;
	}

	if (!handle)
		return;

#ifdef CONFIG_BACKGROUND_RECOMPRESS
	if (dynamic_bgrecomp_enable) {
		zram_bgrecompress_invalidate(handle);
		if (zram_test_flag(meta, index, ZRAM_RECOMP))
			zram_clear_flag(meta, index, ZRAM_RECOMP);

		if (zram_test_flag(meta, index, ZRAM_RECOMP_FINISH)) {
			struct bgrecomp_handle *recompd = (struct bgrecomp_handle *)handle;
			unsigned int org_size = bgrecompd_get_clen(recompd->data);
			unsigned int recomp_size = zram_get_obj_size(meta,index);

			atomic64_sub(org_size, &zram->stats.recomp_org_data_size);
			atomic64_sub(recomp_size, &zram->stats.recomp_data_size);
			zram_clear_flag(meta, index, ZRAM_RECOMP_FINISH);
		}
	}
#endif

#ifndef CONFIG_ZRAM_Z4FOLD
	zs_free(meta->mem_pool, handle);
#else
	z4fold_free(meta->mem_pool, handle);
#endif

	atomic64_sub(zram_get_obj_size(meta, index),
			&zram->stats.compr_data_size);
	atomic64_dec(&zram->stats.pages_stored);

	meta->table[index].handle = 0;
	zram_set_obj_size(meta, index, 0);
}

/* This function is for memory corruption debugging */
static DEFINE_PER_CPU(unsigned long, prev_dcomp_err_handle);

static void zram_dump_page(unsigned long handle)
{
	struct page *dump_page;
	unsigned long pfn;
	unsigned long *obj = (unsigned long *)handle;
	void *vaddr;
	unsigned long *prev_err_handle = &get_cpu_var(prev_dcomp_err_handle);

	if(*prev_err_handle == handle) {
		put_cpu_var(prev_dcomp_err_handle);
		return;
	}

	*prev_err_handle = handle;

	/* decompress failed : print page dump */
#ifdef CONFIG_ZRAM_Z4FOLD
	pfn = z4fold_handle_to_pfn(*obj);
#else
	pfn = zs_handle_to_pfn(*obj);
#endif
	if (pfn_valid(pfn)) {
		dump_page = pfn_to_page(pfn);
		pr_err( "[%s(%d)] page:%p refcount:%d mapcount:%d mapping:%p index:%#lx handle:%lx obj:%lx\n",
				current->comm, current->pid, dump_page, page_ref_count(dump_page),
				page_mapcount(dump_page),dump_page->mapping, dump_page->index, handle, *obj);
		vaddr = kmap_atomic(dump_page);
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 4, vaddr, PAGE_SIZE, true);
		kunmap_atomic(vaddr);
	} else {	/* if zram_meta was broken, it print handle info */
		pr_err("[%s(%d)] invalid handle:%lx obj:%lx\n",
				current->comm, current->pid, handle, *obj);
	}
	put_cpu_var(prev_dcomp_err_handle);
}

static int zram_decompress_page(struct zram *zram, char *mem, u32 index)
{
	int ret = 0;
	unsigned char *cmem;
	struct zram_meta *meta = zram->meta;
	unsigned long handle;
	unsigned int size;

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	handle = meta->table[index].handle;
	size = zram_get_obj_size(meta, index);

	if (!handle || zram_test_flag(meta, index, ZRAM_SAME)) {
		bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
		zram_fill_page(mem, PAGE_SIZE, meta->table[index].element);
		return 0;
	}
#ifndef CONFIG_ZRAM_Z4FOLD 
	cmem = zs_map_object(meta->mem_pool, handle, ZS_MM_RO);
#else
	cmem = z4fold_map(meta->mem_pool, handle, Z4_MM_RO);
#endif
	if (size == PAGE_SIZE) {
		memcpy(mem, cmem, PAGE_SIZE);
	} else {
		struct zcomp_strm *zstrm = zcomp_stream_get(zram->comp);

		ret = zcomp_decompress(zstrm, cmem, size, mem);
		zcomp_stream_put(zram->comp);
	}

#ifndef CONFIG_ZRAM_Z4FOLD
	zs_unmap_object(meta->mem_pool, handle);
#else
	z4fold_unmap(meta->mem_pool, handle);
#endif
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	/* Should NEVER happen. Return bio error if it does. */
	if (unlikely(ret)) {
		pr_err("Decompression failed! err=%d, page=%u\n", ret, index);
		zram_dump_page(handle);
		return ret;
	}

	return 0;
}

static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
			  u32 index, int offset)
{
	int ret;
	struct page *page;
	unsigned char *user_mem, *uncmem = NULL;
	struct zram_meta *meta = zram->meta;
	page = bvec->bv_page;

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	if (unlikely(!meta->table[index].handle) ||
			zram_test_flag(meta, index, ZRAM_SAME)) {
		bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
		handle_same_page(bvec, meta->table[index].element);
		return 0;
	}
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	if (is_partial_io(bvec))
		/* Use  a temporary buffer to decompress the page */
		uncmem = kmalloc(PAGE_SIZE, GFP_NOIO);

	user_mem = kmap_atomic(page);
	if (!is_partial_io(bvec))
		uncmem = user_mem;

	if (!uncmem) {
		pr_info("Unable to allocate temp memory\n");
		ret = -ENOMEM;
		goto out_cleanup;
	}

	ret = zram_decompress_page(zram, uncmem, index);
	/* Should NEVER happen. Return bio error if it does. */
	if (unlikely(ret))
		goto out_cleanup;

	if (is_partial_io(bvec))
		memcpy(user_mem + bvec->bv_offset, uncmem + offset,
				bvec->bv_len);

	flush_dcache_page(page);
	ret = 0;
out_cleanup:
	kunmap_atomic(user_mem);
	if (is_partial_io(bvec))
		kfree(uncmem);
	return ret;
}

static inline void update_used_max(struct zram *zram,
					const unsigned long pages)
{
	unsigned long old_max, cur_max;

	old_max = atomic_long_read(&zram->stats.max_used_pages);

	do {
		cur_max = old_max;
		if (pages > cur_max)
			old_max = atomic_long_cmpxchg(
				&zram->stats.max_used_pages, cur_max, pages);
	} while (old_max != cur_max);
}

#ifdef CONFIG_BACKGROUND_RECOMPRESS
bool can_recomp(struct zram_meta *meta, u32 index)
{
	bool ret;

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	ret = zram_test_flag(meta, index, ZRAM_RECOMP);
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	return ret;
}

int __zram_handle_recomp_flag(u32 index, enum bgrecomp_flag_type type)
{
	int ret = 0;
	struct zram *zram = &zram_devices[0];
	struct zram_meta *meta;

	if (unlikely(!zram_meta_get(zram))) {
		ret = RECOMP_META_ERR;
		goto out;
	}

	meta = zram->meta;
	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	switch(type) {
		case RECOMP_SET:
			if (meta->table[index].handle && !zram_test_flag(meta, index, ZRAM_RECOMP))
				zram_set_flag(meta, index, ZRAM_RECOMP);
			break;
		case RECOMP_CLEAR:
			if (zram_test_flag(meta, index, ZRAM_RECOMP))
				zram_clear_flag(meta, index, ZRAM_RECOMP);
			break;
		default:
			ret = RECOMP_INVALID_DATA;
			pr_info("[background_recompress] Invalid type(%d)",type);
			break;
	}
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
	zram_meta_put(zram);
out:
	return ret;
}
EXPORT_SYMBOL(__zram_handle_recomp_flag);

int __bgrecompress_get_new_handle(struct zram *zram, struct zram_meta *meta,
		unsigned char *src, u32 index, unsigned int rclen, unsigned long *handle)
{
	int ret = 0;
	unsigned char *cmem;
	unsigned long new_handle = 0;
	unsigned long alloced_pages;

	if (can_recomp(meta, index) == false) {
		ret = RECOMP_SKIP;
		goto out;
	}

#ifndef CONFIG_ZRAM_Z4FOLD
	new_handle = zs_malloc(meta->mem_pool, rclen,
			GFP_NOIO |__GFP_HIGHMEM);
#else
	ret = z4fold_alloc(meta->mem_pool, rclen,
			GFP_NOIO | __GFP_HIGHMEM, &new_handle);
#endif

	if (!new_handle) {
		pr_err("Error allocating memory for compressed page: %u, size=%u\n",
				index, rclen);
		ret = -ENOMEM;
		goto out;
	}

#ifndef CONFIG_ZRAM_Z4FOLD
	alloced_pages = zs_get_total_pages(meta->mem_pool);
#else
	alloced_pages = z4fold_get_pool_size(meta->mem_pool);
#endif

	if (zram->limit_pages && alloced_pages > zram->limit_pages) {
#ifndef CONFIG_ZRAM_Z4FOLD
		zs_free(meta->mem_pool, new_handle);
#else
		z4fold_free(meta->mem_pool, new_handle);
#endif
		ret = -ENOMEM;
		goto out;
	}

    update_used_max(zram, alloced_pages);
	
	preempt_disable();
#ifndef CONFIG_ZRAM_Z4FOLD
	cmem = zs_map_object(meta->mem_pool, new_handle, ZS_MM_WO);
#else
	cmem = z4fold_map(meta->mem_pool, new_handle, Z4_MM_WO);
#endif

	memcpy(cmem, src, rclen);

#ifndef CONFIG_ZRAM_Z4FOLD
    zs_unmap_object(meta->mem_pool, new_handle);
#else
    z4fold_unmap(meta->mem_pool, new_handle);
#endif
	preempt_enable();

	*handle = new_handle;

out:
	return ret;
}

int __bgrecompress_zram_update(unsigned char *src, u32 index, unsigned int clen, unsigned int rclen)
{
	struct zram *zram = &zram_devices[0];
	struct zram_meta *meta;
	unsigned long new_handle = 0;
	struct bgrecomp_handle *recomp_handle;
	int ret = 0;

	if (unlikely(!zram_meta_get(zram))) {
		ret = RECOMP_META_ERR;
		goto out;
	}

	meta = zram->meta;

	ret = __bgrecompress_get_new_handle(zram, meta, src, index, rclen, &new_handle);
	if (ret) {
		pr_err("[background_recompress] fail to get new handle(%d)", ret);
		goto meta_put;
	}

	recomp_handle = (struct bgrecomp_handle *)new_handle;
	INIT_LIST_HEAD(&recomp_handle->bgrecomp_lru);
	recomp_handle->index = index;
	recomp_handle->data = bgrecompd_store_data(clen);

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	if (!zram_test_flag(meta, index, ZRAM_RECOMP)) {
		ret = RECOMP_SKIP;
		goto bit_unlock;
	}
	/* free origin handle data */
	zram_free_page(zram, index);

	meta->table[index].handle = new_handle;
	zram_set_obj_size(meta, index, rclen);
	zram_clear_flag(meta, index, ZRAM_RECOMP);
	zram_set_flag(meta, index, ZRAM_RECOMP_FINISH);
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	atomic64_add(rclen,	&zram->stats.compr_data_size);
	atomic64_add(clen, &zram->stats.recomp_org_data_size);
	atomic64_add(rclen, &zram->stats.recomp_data_size);
	atomic64_inc(&zram->stats.pages_stored);

	zram_meta_put(zram);

	return ret;

bit_unlock:
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
	z4fold_free(meta->mem_pool, new_handle);
meta_put:
	zram_meta_put(zram);
out:
	return ret;
}
EXPORT_SYMBOL(__bgrecompress_zram_update);

int __bgrecompress_decompress(struct page *page, u32 index, unsigned int clen)
{
	int ret = 0;
	struct zram *zram = &zram_devices[0];
	struct zram_meta *meta;
	unsigned char *cmem, *mem, *tmp;
	unsigned long handle;
	unsigned int size;

	tmp = kmalloc(clen, GFP_KERNEL | __GFP_RECLAIMABLE);
	if (!tmp) {
		ret = RECOMP_SKIP;
		pr_err("[background_recompress] kalloc fail!");
		goto out;
	}

	if (unlikely(!zram_meta_get(zram))) {
		ret = RECOMP_META_ERR;
		goto free;
	}
	meta = zram->meta;

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	if (!zram_test_flag(meta, index, ZRAM_RECOMP)) {
		ret = RECOMP_SKIP;
		goto bit_unlock;
	}

	if (zram_test_flag(meta, index, ZRAM_SAME)) {
		pr_err("[background_recompress] skip same page\n");
		WARN_ON(1);
		ret = RECOMP_META_ERR;
		goto bit_unlock;
	}

	handle = meta->table[index].handle;
	if (!handle) {
		pr_info("[background_recompress] handle freed");
		ret = RECOMP_INVALID_HANDLE;
		goto bit_unlock;
	}

	size = zram_get_obj_size(meta, index);
	if (clen != size) {
		pr_info("[background_recompress] Data was changed(stored:%d|cur:%d)",
				clen, size);
		ret = RECOMP_INVALID_DATA;
		goto bit_unlock;
	}

#ifndef CONFIG_ZRAM_Z4FOLD
    cmem = zs_map_object(meta->mem_pool, handle, ZS_MM_RO);
#else
    cmem = z4fold_map(meta->mem_pool, handle, Z4_MM_RO);
#endif

	memcpy(tmp, cmem, size);

#ifndef CONFIG_ZRAM_Z4FOLD
    zs_unmap_object(meta->mem_pool, handle);
#else
    z4fold_unmap(meta->mem_pool, handle);
#endif
    bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	mem = page_address(page);
	if (!mem) {
		pr_err("Unable to allocate temp memory\n");
		ret = -ENOMEM;
		goto meta_put;
	}

	ret = bgrecompd_decompress_page(tmp, mem, size);

    /* Should NEVER happen. Return bio error if it does. */
    if (unlikely(ret)) {
        pr_err("BGDecompression failed! err=%d, page=%u\n", ret, index);
		zram_dump_page(handle);
        goto meta_put;
    }

	if (can_recomp(meta, index) == false) {
		ret = RECOMP_SKIP;
		pr_err("[background_recompress] free after decompress");
		goto meta_put;
	}

	zram_meta_put(zram);
	kfree(tmp);

	return ret;

bit_unlock:
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
meta_put:
	zram_meta_put(zram);
free:
	kfree(tmp);
out:
	return ret;
}
EXPORT_SYMBOL(__bgrecompress_decompress);
#endif

static int zram_bvec_write(struct zram *zram, struct bio_vec *bvec, u32 index,
			   int offset)
{
	int ret = 0;
	unsigned int clen;
	unsigned long handle = 0;
	struct page *page;
	unsigned char *user_mem, *cmem, *src, *uncmem = NULL;
	struct zram_meta *meta = zram->meta;
	struct zcomp_strm *zstrm = NULL;
	unsigned long alloced_pages;
	unsigned long element;
	gfp_t gfp = __GFP_NO_KSWAPD | __GFP_NOWARN | __GFP_HIGHMEM | __GFP_MOVABLE;
#ifdef CONFIG_BACKGROUND_RECOMPRESS
	if (dynamic_bgrecomp_enable)
		gfp |= __GFP_ZERO;
#endif

	page = bvec->bv_page;
	if (is_partial_io(bvec)) {
		/*
		 * This is a partial IO. We need to read the full page
		 * before to write the changes.
		 */
		uncmem = kmalloc(PAGE_SIZE, GFP_NOIO);
		if (!uncmem) {
			ret = -ENOMEM;
			goto out;
		}
		ret = zram_decompress_page(zram, uncmem, index);
		if (ret)
			goto out;
	}

compress_again:
	zstrm = zcomp_stream_get(zram->comp);
	user_mem = kmap_atomic(page);

	if (is_partial_io(bvec)) {
		memcpy(uncmem + offset, user_mem + bvec->bv_offset,
		       bvec->bv_len);
		kunmap_atomic(user_mem);
		user_mem = NULL;
	} else {
		uncmem = user_mem;
	}

	if (page_same_filled(uncmem, &element)) {
		if (user_mem)
			kunmap_atomic(user_mem);
		/* Free memory associated with this sector now. */
		bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
		zram_free_page(zram, index);
		zram_set_flag(meta, index, ZRAM_SAME);
		zram_set_element(meta, index, element);
		bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

		atomic64_inc(&zram->stats.same_pages);
		ret = 0;
		goto out;
	}

	ret = zcomp_compress(zstrm, uncmem, &clen);
	if (!is_partial_io(bvec)) {
		kunmap_atomic(user_mem);
		user_mem = NULL;
		uncmem = NULL;
	}

	if (unlikely(ret)) {
		pr_err("Compression failed! err=%d\n", ret);
		goto out;
	}

	src = zstrm->buffer;
#ifndef CONFIG_ZRAM_Z4FOLD
	if (unlikely(clen > zs_max_zpage_size(meta->mem_pool))) {
#else
	if (unlikely(clen > z4fold_max_size(meta->mem_pool))) {
#endif
		clen = PAGE_SIZE;
		if (is_partial_io(bvec))
			src = uncmem;
	}

	/*
	 * handle allocation has 2 paths:
	 * a) fast path is executed with preemption disabled (for
	 *  per-cpu streams) and has __GFP_NO_KSWAPD bit set,
	 *  since we can't sleep;
	 * b) slow path enables preemption and attempts to allocate
	 *  the page with __GFP_NO_KSWAPD bit clear. we have to
	 *  put per-cpu compression stream and, thus, to re-do
	 *  the compression once handle is allocated.
	 *
	 * if we have a 'non-null' handle here then we are coming
	 * from the slow path and handle has already been allocated.
	 */
	if (!handle)
#ifndef CONFIG_ZRAM_Z4FOLD
		handle = zs_malloc(meta->mem_pool, clen,
				__GFP_NO_KSWAPD |
				__GFP_NOWARN |
				__GFP_HIGHMEM |
				__GFP_MOVABLE);
#else
		ret = z4fold_alloc(meta->mem_pool, clen, 
				gfp, &handle);
#endif
	if (!handle) {
		zcomp_stream_put(zram->comp);
		zstrm = NULL;

		atomic64_inc(&zram->stats.writestall);

		gfp = GFP_NOIO | __GFP_HIGHMEM | __GFP_MOVABLE;
#ifdef CONFIG_BACKGROUND_RECOMPRESS
		if (dynamic_bgrecomp_enable)
			gfp |= __GFP_ZERO;
#endif

#ifndef CONFIG_ZRAM_Z4FOLD
		handle = zs_malloc(meta->mem_pool, clen,
				GFP_NOIO | __GFP_HIGHMEM |
				__GFP_MOVABLE);
#else
		ret = z4fold_alloc(meta->mem_pool, clen,
				gfp, &handle);
#endif
		if (handle)
			goto compress_again;

		pr_err("Error allocating memory for compressed page: %u, size=%u\n",
			index, clen);
		ret = -ENOMEM;
		goto out;
	}

#ifndef CONFIG_ZRAM_Z4FOLD
	alloced_pages = zs_get_total_pages(meta->mem_pool);
#else
	alloced_pages = z4fold_get_pool_size(meta->mem_pool);
#endif
	if (zram->limit_pages && alloced_pages > zram->limit_pages) {
#ifndef CONFIG_ZRAM_Z4FOLD
		zs_free(meta->mem_pool, handle);
#else
		z4fold_free(meta->mem_pool, handle);
#endif
		ret = -ENOMEM;
		goto out;
	}

	update_used_max(zram, alloced_pages);

#ifndef CONFIG_ZRAM_Z4FOLD
	cmem = zs_map_object(meta->mem_pool, handle, ZS_MM_WO);
#else
	cmem = z4fold_map(meta->mem_pool, handle, Z4_MM_WO);
#endif

	if ((clen == PAGE_SIZE) && !is_partial_io(bvec)) {
		src = kmap_atomic(page);
		memcpy(cmem, src, PAGE_SIZE);
		kunmap_atomic(src);
	} else {
		memcpy(cmem, src, clen);
	}

	zcomp_stream_put(zram->comp);
	zstrm = NULL;
#ifndef CONFIG_ZRAM_Z4FOLD
	zs_unmap_object(meta->mem_pool, handle);
#else
	z4fold_unmap(meta->mem_pool, handle);
#endif

	/*
	 * Free memory associated with this sector
	 * before overwriting unused sectors.
	 */
	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	zram_free_page(zram, index);

#ifdef CONFIG_BACKGROUND_RECOMPRESS
	if (dynamic_bgrecomp_enable)
		zram_bgrecompress_store(handle, index, clen);
#endif
	meta->table[index].handle = handle;
	zram_set_obj_size(meta, index, clen);
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);

	/* Update stats */
	atomic64_add(clen, &zram->stats.compr_data_size);
	atomic64_inc(&zram->stats.pages_stored);
out:
	if (zstrm)
		zcomp_stream_put(zram->comp);
	if (is_partial_io(bvec))
		kfree(uncmem);
	return ret;
}

static int zram_bvec_rw(struct zram *zram, struct bio_vec *bvec, u32 index,
			int offset, int rw)
{
	unsigned long start_time = jiffies;
	int ret;

	generic_start_io_acct(rw, bvec->bv_len >> SECTOR_SHIFT,
			&zram->disk->part0);

	if (rw == READ) {
		atomic64_inc(&zram->stats.num_reads);
		ret = zram_bvec_read(zram, bvec, index, offset);
	} else {
		atomic64_inc(&zram->stats.num_writes);
		ret = zram_bvec_write(zram, bvec, index, offset);
	}

	generic_end_io_acct(rw, &zram->disk->part0, start_time);

	if (unlikely(ret)) {
		if (rw == READ)
			atomic64_inc(&zram->stats.failed_reads);
		else
			atomic64_inc(&zram->stats.failed_writes);
	}

	return ret;
}

/*
 * zram_bio_discard - handler on discard request
 * @index: physical block index in PAGE_SIZE units
 * @offset: byte offset within physical block
 */
static void zram_bio_discard(struct zram *zram, u32 index,
			     int offset, struct bio *bio)
{
	size_t n = bio->bi_iter.bi_size;
	struct zram_meta *meta = zram->meta;

	/*
	 * zram manages data in physical block size units. Because logical block
	 * size isn't identical with physical block size on some arch, we
	 * could get a discard request pointing to a specific offset within a
	 * certain physical block.  Although we can handle this request by
	 * reading that physiclal block and decompressing and partially zeroing
	 * and re-compressing and then re-storing it, this isn't reasonable
	 * because our intent with a discard request is to save memory.  So
	 * skipping this logical block is appropriate here.
	 */
	if (offset) {
		if (n <= (PAGE_SIZE - offset))
			return;

		n -= (PAGE_SIZE - offset);
		index++;
	}

	while (n >= PAGE_SIZE) {
		bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
		zram_free_page(zram, index);
		bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
		atomic64_inc(&zram->stats.notify_free);
		index++;
		n -= PAGE_SIZE;
	}
}

static void zram_reset_device(struct zram *zram)
{
	struct zram_meta *meta;
	struct zcomp *comp;
	u64 disksize;

	down_write(&zram->init_lock);

	zram->limit_pages = 0;

	if (!init_done(zram)) {
		up_write(&zram->init_lock);
		return;
	}

	meta = zram->meta;
	comp = zram->comp;
	disksize = zram->disksize;
	/*
	 * Refcount will go down to 0 eventually and r/w handler
	 * cannot handle further I/O so it will bail out by
	 * check zram_meta_get.
	 */
	zram_meta_put(zram);
	/*
	 * We want to free zram_meta in process context to avoid
	 * deadlock between reclaim path and any other locks.
	 */
	wait_event(zram->io_done, atomic_read(&zram->refcount) == 0);

	/* Reset stats */
	memset(&zram->stats, 0, sizeof(zram->stats));
	zram->disksize = 0;

	set_capacity(zram->disk, 0);
	part_stat_set_all(&zram->disk->part0, 0);

	up_write(&zram->init_lock);
	/* I/O operation under all of CPU are done so let's free */
	zram_meta_free(meta, disksize);
	zcomp_destroy(comp);
}

static unsigned long long zram_memparse(const char *ptr)
{
	char *endptr;   /* local pointer to end of parsed string */

	unsigned long long ret = simple_strtoull(ptr, &endptr, 0);

	switch (*endptr) {
	case 'E':
	case 'e':
		ret <<= 10;
		/* fall through */
	case 'P':
	case 'p':
		ret <<= 10;
		/* fall through */
	case 'T':
	case 't':
		ret <<= 10;
		/* fall through */
	case 'G':
	case 'g':
		ret <<= 10;
		/* fall through */
	case 'M':
	case 'm':
		ret <<= 10;
		/* fall through */
	case 'K':
	case 'k':
		ret <<= 10;
		endptr++;
		break;
	case '%':
		ret = (totalram_pages * (unsigned long)ret)/100;
		ret <<= PAGE_SHIFT;
		break;
	default:
		break;
	}

	return ret;
}

static int __disksize_store(struct zram *zram, const char *buf)
{
	u64 disksize;
	struct zcomp *comp;
	struct zram_meta *meta;
	int err = 0;

	disksize = zram_memparse(buf);
	if (!disksize)
		return -EINVAL;

	disksize = PAGE_ALIGN(disksize);
	meta = zram_meta_alloc(zram->disk->first_minor, disksize);
	if (!meta)
		return -ENOMEM;

	comp = zcomp_create(zram->compressor);
	if (IS_ERR(comp)) {
		pr_info("Cannot initialise %s compressing backend\n",
				zram->compressor);
		err = PTR_ERR(comp);
		goto out_free_meta;
	}

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Cannot change disksize for initialized device\n");
		err = -EBUSY;
		goto out_destroy_comp;
	}

	init_waitqueue_head(&zram->io_done);
	atomic_set(&zram->refcount, 1);
	zram->meta = meta;
	zram->comp = comp;
	zram->disksize = disksize;
	set_capacity(zram->disk, zram->disksize >> SECTOR_SHIFT);
	up_write(&zram->init_lock);

	/*
	 * Revalidate disk out of the init_lock to avoid lockdep splat.
	 * It's okay because disk's capacity is protected by init_lock
	 * so that revalidate_disk always sees up-to-date capacity.
	 */
	revalidate_disk(zram->disk);

	return err;

out_destroy_comp:
	up_write(&zram->init_lock);
	zcomp_destroy(comp);
out_free_meta:
	zram_meta_free(meta, disksize);
	return err;
}

static ssize_t disksize_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	int err;

	err = __disksize_store(zram, buf);
	if (err)
		return err;
	return len;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned short do_reset;
	struct zram *zram;
	struct block_device *bdev;

	zram = dev_to_zram(dev);
	bdev = bdget_disk(zram->disk, 0);

	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	/* Do not reset an active device! */
	if (bdev->bd_openers) {
		ret = -EBUSY;
		goto out;
	}

	ret = kstrtou16(buf, 10, &do_reset);
	if (ret)
		goto out;

	if (!do_reset) {
		ret = -EINVAL;
		goto out;
	}

	/* Make sure all pending I/O is finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);

	mutex_unlock(&bdev->bd_mutex);
	revalidate_disk(zram->disk);
	bdput(bdev);

	return len;

out:
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	return ret;
}

static void __zram_make_request(struct zram *zram, struct bio *bio)
{
	int offset, rw;
	u32 index;
	struct bio_vec bvec;
	struct bvec_iter iter;

	index = bio->bi_iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (bio->bi_iter.bi_sector &
		  (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	if (unlikely(bio->bi_rw & REQ_DISCARD)) {
		zram_bio_discard(zram, index, offset, bio);
		bio_endio(bio, 0);
		return;
	}

	rw = bio_data_dir(bio);
	bio_for_each_segment(bvec, bio, iter) {
		int max_transfer_size = PAGE_SIZE - offset;

		if (bvec.bv_len > max_transfer_size) {
			/*
			 * zram_bvec_rw() can only make operation on a single
			 * zram page. Split the bio vector.
			 */
			struct bio_vec bv;

			bv.bv_page = bvec.bv_page;
			bv.bv_len = max_transfer_size;
			bv.bv_offset = bvec.bv_offset;

			if (zram_bvec_rw(zram, &bv, index, offset, rw) < 0)
				goto out;

			bv.bv_len = bvec.bv_len - max_transfer_size;
			bv.bv_offset += max_transfer_size;
			if (zram_bvec_rw(zram, &bv, index + 1, 0, rw) < 0)
				goto out;
		} else
			if (zram_bvec_rw(zram, &bvec, index, offset, rw) < 0)
				goto out;

		update_position(&index, &offset, &bvec);
	}

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return;

out:
	bio_io_error(bio);
}

/*
 * Handler function for all zram I/O requests.
 */
static void zram_make_request(struct request_queue *queue, struct bio *bio)
{
	struct zram *zram = queue->queuedata;

	if (unlikely(!zram_meta_get(zram)))
		goto error;

	if (!valid_io_request(zram, bio->bi_iter.bi_sector,
					bio->bi_iter.bi_size)) {
		atomic64_inc(&zram->stats.invalid_io);
		goto put_zram;
	}

	__zram_make_request(zram, bio);
	zram_meta_put(zram);
	return;
put_zram:
	zram_meta_put(zram);
error:
	bio_io_error(bio);
}

static void zram_slot_free_notify(struct block_device *bdev,
				unsigned long index)
{
	struct zram *zram;
	struct zram_meta *meta;

	zram = bdev->bd_disk->private_data;
	meta = zram->meta;

	bit_spin_lock(ZRAM_ACCESS, &meta->table[index].value);
	zram_free_page(zram, index);
	bit_spin_unlock(ZRAM_ACCESS, &meta->table[index].value);
	atomic64_inc(&zram->stats.notify_free);
}

static int zram_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, int rw)
{
	int offset, err = -EIO;
	u32 index;
	struct zram *zram;
	struct bio_vec bv;

	zram = bdev->bd_disk->private_data;
	if (unlikely(!zram_meta_get(zram)))
		goto out;

	if (!valid_io_request(zram, sector, PAGE_SIZE)) {
		atomic64_inc(&zram->stats.invalid_io);
		err = -EINVAL;
		goto put_zram;
	}

	index = sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (sector & (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	bv.bv_page = page;
	bv.bv_len = PAGE_SIZE;
	bv.bv_offset = 0;

	err = zram_bvec_rw(zram, &bv, index, offset, rw);
put_zram:
	zram_meta_put(zram);
out:
	/*
	 * If I/O fails, just return error(ie, non-zero) without
	 * calling page_endio.
	 * It causes resubmit the I/O with bio request by upper functions
	 * of rw_page(e.g., swap_readpage, __swap_writepage) and
	 * bio->bi_end_io does things to handle the error
	 * (e.g., SetPageError, set_page_dirty and extra works).
	 */
	if (err == 0)
		page_endio(page, rw, 0);
	return err;
}

static const struct block_device_operations zram_devops = {
	.swap_slot_free_notify = zram_slot_free_notify,
	.rw_page = zram_rw_page,
	.owner = THIS_MODULE
};
#ifndef CONFIG_ZRAM_Z4FOLD
static DEVICE_ATTR_WO(compact);
#endif
static DEVICE_ATTR_RW(disksize);
static DEVICE_ATTR_RO(initstate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_RO(orig_data_size);
static DEVICE_ATTR_RO(mem_used_total);
static DEVICE_ATTR_RW(mem_limit);
static DEVICE_ATTR_RW(mem_used_max);
static DEVICE_ATTR_RW(max_comp_streams);
static DEVICE_ATTR_RW(comp_algorithm);
static DEVICE_ATTR_RW(mem_allocator);
#ifdef CONFIG_BACKGROUND_RECOMPRESS
static DEVICE_ATTR_RO(allocator_chunk_info);
static DEVICE_ATTR_RO(recompress_data);
#endif

static ssize_t io_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8llu\n",
			(u64)atomic64_read(&zram->stats.failed_reads),
			(u64)atomic64_read(&zram->stats.failed_writes),
			(u64)atomic64_read(&zram->stats.invalid_io),
			(u64)atomic64_read(&zram->stats.notify_free));
	up_read(&zram->init_lock);

	return ret;
}

static ssize_t mm_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
#ifndef CONFIG_ZRAM_Z4FOLD
	struct zs_pool_stats pool_stats;
#endif
	u64 orig_size, mem_used = 0;
	long max_used;
	ssize_t ret;


	down_read(&zram->init_lock);
	if (init_done(zram)) {
#ifndef CONFIG_ZRAM_Z4FOLD
		memset(&pool_stats, 0x00, sizeof(struct zs_pool_stats));
		mem_used = zs_get_total_pages(zram->meta->mem_pool);
		zs_pool_stats(zram->meta->mem_pool, &pool_stats);
#else 
		mem_used = z4fold_get_pool_size(zram->meta->mem_pool);
#endif
	}

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);

	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8lu %8ld %8llu %8lu\n",
			orig_size << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.compr_data_size),
			mem_used << PAGE_SHIFT,
			zram->limit_pages << PAGE_SHIFT,
			max_used << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.same_pages),
#ifndef CONFIG_ZRAM_Z4FOLD
			pool_stats.pages_compacted);
#else
			(unsigned long)0);
#endif
	up_read(&zram->init_lock);

	return ret;
}

void print_zram_info(struct seq_file *seq)
{
	size_t dev_id;
	size_t mem_used = 0;
	struct zram *rzs = NULL;
	struct zram_stats stats;
	u64 orig_size, comp_size = 0;
#ifdef CONFIG_ZRAM_Z4FOLD
	u64 headless = 0;
#endif
#ifdef CONFIG_BACKGROUND_RECOMPRESS_PROFILE_MODE
	u64 recomp_org_size, recomp_size;
#endif

	if (zram_devices == NULL) {
		seq_printk(seq, "zram driver has never been initialized\n");
		return;
	}
	for (dev_id = 0; dev_id < num_devices; dev_id++) {
		rzs = &zram_devices[dev_id];

		down_read(&rzs->init_lock);
		if (init_done(rzs)) {
			stats = rzs->stats;
			orig_size = atomic64_read(&stats.pages_stored);
			comp_size = (u64)atomic64_read(&stats.compr_data_size);
#ifndef CONFIG_ZRAM_Z4FOLD
			mem_used = zs_get_total_pages(rzs->meta->mem_pool);
#else 
			mem_used = z4fold_get_pool_size(rzs->meta->mem_pool);
			headless = z4fold_get_headless_pages(rzs->meta->mem_pool);
#endif

#ifdef CONFIG_BACKGROUND_RECOMPRESS_PROFILE_MODE
			if (dynamic_bgrecomp_enable) {
				recomp_org_size = (u64)atomic64_read(&stats.recomp_org_data_size) >> 10;
				recomp_size = (u64)atomic64_read(&stats.recomp_data_size) >> 10;
			}
#endif

			seq_printk(seq, "zram%u: %llu %s, %llu %s, %u %s",
					dev_id,
					(orig_size << PAGE_SHIFT) >> 10,
					"kB OrigDataSize",
					comp_size >> 10,
					"kB ComprDataSize",
					(mem_used << PAGE_SHIFT) >> 10,
					"kB MemUsedTotal");
#ifndef CONFIG_ZRAM_Z4FOLD
			seq_printk(seq, "\n");
#else 
			seq_printk(seq, ", %llu %s\n",
					(headless << PAGE_SHIFT) >> 10,
					"kB IncompressibleTotal");
#endif
#ifdef CONFIG_BACKGROUND_RECOMPRESS_PROFILE_MODE
			if (dynamic_bgrecomp_enable) {
				seq_printk(seq, "BG_Recompress: %llu %s, %llu %s, %llu %s\n",
						recomp_org_size,
						"kB OrigDataSize",
						recomp_size,
						"kB RecomprDataSize",
						(recomp_org_size - recomp_size),
						"kB SavedTotal");
			}	
#endif
		} else {
			seq_printk(seq, "zram%u: not initialized\n", dev_id);
		}
		up_read(&rzs->init_lock);
	}
}
EXPORT_SYMBOL(print_zram_info);


static ssize_t debug_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int version = 1;
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"version: %d\n%8llu\n",
			version,
			(u64)atomic64_read(&zram->stats.writestall));
	up_read(&zram->init_lock);

	return ret;
}

static DEVICE_ATTR_RO(io_stat);
static DEVICE_ATTR_RO(mm_stat);
static DEVICE_ATTR_RO(debug_stat);
ZRAM_ATTR_RO(num_reads);
ZRAM_ATTR_RO(num_writes);
ZRAM_ATTR_RO(failed_reads);
ZRAM_ATTR_RO(failed_writes);
ZRAM_ATTR_RO(invalid_io);
ZRAM_ATTR_RO(notify_free);
ZRAM_ATTR_RO(same_pages);
ZRAM_ATTR_RO(compr_data_size);

static struct attribute *zram_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_initstate.attr,
	&dev_attr_reset.attr,
	&dev_attr_num_reads.attr,
	&dev_attr_num_writes.attr,
	&dev_attr_failed_reads.attr,
	&dev_attr_failed_writes.attr,
#ifndef CONFIG_ZRAM_Z4FOLD
	&dev_attr_compact.attr,
#endif
	&dev_attr_invalid_io.attr,
	&dev_attr_notify_free.attr,
	&dev_attr_same_pages.attr,
	&dev_attr_orig_data_size.attr,
	&dev_attr_compr_data_size.attr,
	&dev_attr_mem_used_total.attr,
	&dev_attr_mem_limit.attr,
	&dev_attr_mem_used_max.attr,
	&dev_attr_max_comp_streams.attr,
	&dev_attr_comp_algorithm.attr,
	&dev_attr_io_stat.attr,
	&dev_attr_mm_stat.attr,
	&dev_attr_debug_stat.attr,
	&dev_attr_mem_allocator.attr,
#ifdef CONFIG_BACKGROUND_RECOMPRESS
	&dev_attr_allocator_chunk_info.attr,
	&dev_attr_recompress_data.attr,
#endif
	NULL,
};

static struct attribute_group zram_disk_attr_group = {
	.attrs = zram_disk_attrs,
};

static int create_device(struct zram *zram, int device_id)
{
	struct request_queue *queue;
	int ret = -ENOMEM;

	init_rwsem(&zram->init_lock);

	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		pr_err("Error allocating disk queue for device %d\n",
			device_id);
		goto out;
	}

	blk_queue_make_request(queue, zram_make_request);

	 /* gendisk structure */
	zram->disk = alloc_disk(1);
	if (!zram->disk) {
		pr_warn("Error allocating disk structure for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_queue;
	}

	zram->disk->major = zram_major;
	zram->disk->first_minor = device_id;
	zram->disk->fops = &zram_devops;
	zram->disk->queue = queue;
	zram->disk->queue->queuedata = zram;
	zram->disk->private_data = zram;
	snprintf(zram->disk->disk_name, 16, "zram%d", device_id);

	/* Actual capacity set using syfs (/sys/block/zram<id>/disksize */
	set_capacity(zram->disk, 0);
	/* zram devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, zram->disk->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, zram->disk->queue);
	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(zram->disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(zram->disk->queue,
					ZRAM_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(zram->disk->queue, PAGE_SIZE);
	blk_queue_io_opt(zram->disk->queue, PAGE_SIZE);
	zram->disk->queue->limits.discard_granularity = PAGE_SIZE;
	zram->disk->queue->limits.max_discard_sectors = UINT_MAX;
	/*
	 * zram_bio_discard() will clear all logical blocks if logical block
	 * size is identical with physical block size(PAGE_SIZE). But if it is
	 * different, we will skip discarding some parts of logical blocks in
	 * the part of the request range which isn't aligned to physical block
	 * size.  So we can't ensure that all discarded logical blocks are
	 * zeroed.
	 */
	if (ZRAM_LOGICAL_BLOCK_SIZE == PAGE_SIZE)
		zram->disk->queue->limits.discard_zeroes_data = 1;
	else
		zram->disk->queue->limits.discard_zeroes_data = 0;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, zram->disk->queue);

	zram->disk->queue->backing_dev_info->capabilities |=
		(BDI_CAP_STABLE_WRITES | BDI_CAP_SYNCHRONOUS_IO);
	add_disk(zram->disk);

	ret = sysfs_create_group(&disk_to_dev(zram->disk)->kobj,
				&zram_disk_attr_group);
	if (ret < 0) {
		pr_warn("Error creating sysfs group");
		goto out_free_disk;
	}
	strlcpy(zram->compressor, default_compressor, sizeof(zram->compressor));
	strlcpy(zram->mem_allocator, zram_allocator, sizeof(zram->mem_allocator)); 
	zram->meta = NULL;

	return 0;

out_free_disk:
	del_gendisk(zram->disk);
	put_disk(zram->disk);
out_free_queue:
	blk_cleanup_queue(queue);
out:
	return ret;
}

static void destroy_devices(unsigned int nr)
{
	struct zram *zram;
	unsigned int i;

	for (i = 0; i < nr; i++) {
		zram = &zram_devices[i];
		/*
		 * Remove sysfs first, so no one will perform a disksize
		 * store while we destroy the devices
		 */
		sysfs_remove_group(&disk_to_dev(zram->disk)->kobj,
				&zram_disk_attr_group);

		zram_reset_device(zram);

		blk_cleanup_queue(zram->disk->queue);
		del_gendisk(zram->disk);
		put_disk(zram->disk);
	}

	kfree(zram_devices);
	unregister_blkdev(zram_major, "zram");
	pr_info("Destroyed %u device(s)\n", nr);
}

static int __init zram_init(void)
{
	int ret, dev_id;

	if (num_devices > max_num_devices) {
		pr_warn("Invalid value for num_devices: %u\n",
				num_devices);
		return -EINVAL;
	}

	zram_major = register_blkdev(0, "zram");
	if (zram_major <= 0) {
		pr_warn("Unable to get major number\n");
		return -EBUSY;
	}

	/* Allocate the device array and initialize each one */
	zram_devices = kzalloc(num_devices * sizeof(struct zram), GFP_KERNEL);
	if (!zram_devices) {
		unregister_blkdev(zram_major, "zram");
		return -ENOMEM;
	}

	for (dev_id = 0; dev_id < num_devices; dev_id++) {
		ret = create_device(&zram_devices[dev_id], dev_id);
		if (ret)
			goto out_error;

		ret = __disksize_store(&zram_devices[dev_id],
				is_entry ? ZRAM_ENTRY_MODEL_DISKSIZE:ZRAM_DEFAULT_DISKSIZE);
		if (ret)
			pr_warn("Fail to set zram disksize(%s)\n",
					ZRAM_DEFAULT_DISKSIZE);
	}

	spin_lock(&pt_zram_struct.pt_zram_lock);
	pt_zram_struct.pt_zram_info = print_zram_info;
	spin_unlock(&pt_zram_struct.pt_zram_lock);

	pr_info("Created %u device(s)\n", num_devices);

	return 0;

out_error:
	destroy_devices(dev_id);
	return ret;
}

static void __exit zram_exit(void)
{
	spin_lock(&pt_zram_struct.pt_zram_lock);
	pt_zram_struct.pt_zram_info = NULL;
	spin_unlock(&pt_zram_struct.pt_zram_lock);

	destroy_devices(num_devices);
}

static int __init set_entry_zram_disksize(char *str)
{
    is_entry = true;
    return 0;
}
early_param("only_entry_model", set_entry_zram_disksize);
module_init(zram_init);
module_exit(zram_exit);

module_param(num_devices, uint, 0);
MODULE_PARM_DESC(num_devices, "Number of zram devices");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Block Device");
