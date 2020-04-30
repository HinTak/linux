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

#define KMSG_COMPONENT "snapswap"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

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
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/wait.h>

#include <linux/circ_buf.h>
#include <linux/kthread.h>
#include "zram_drv.h"

static DEFINE_IDR(zram_index_idr);
/* idr index must be protected */
static DEFINE_MUTEX(zram_index_mutex);

static int zram_major;
static const char *default_compressor = "lz4";

/* Module params (documentation at end) */
static unsigned int num_devices = 1;

static void zram_free_page(struct zram *zram, size_t index);
static int zram_wb_flush_t(void *arg);

static inline bool init_done(struct zram *zram)
{
	return zram->disksize;
}

static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}

static unsigned long zram_get_handle(struct zram *zram, u32 index)
{
	return zram->table[index].handle;
}

static void zram_set_handle(struct zram *zram, u32 index, unsigned long handle)
{
	zram->table[index].handle = handle;
}

/* flag operations require table entry bit_spin_lock() being held */
static int zram_test_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	return zram->table[index].value & BIT(flag);
}

static void zram_set_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].value |= BIT(flag);
}

static void zram_clear_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].value &= ~BIT(flag);
}

static inline void zram_set_element(struct zram *zram, u32 index,
			unsigned long element)
{
	zram->table[index].element = element;
}

static unsigned long zram_get_element(struct zram *zram, u32 index)
{
	return zram->table[index].element;
}

static size_t zram_get_obj_size(struct zram *zram, u32 index)
{
	return zram->table[index].value & ZRAM_OBJ_SIZE_MASK;
}

static void zram_set_obj_size(struct zram *zram,
					u32 index, size_t size)
{
	zram->table[index].value &= ~ZRAM_OBJ_SIZE_MASK;
	zram->table[index].value |= (size & ZRAM_OBJ_SIZE_MASK);
}

static u32 zram_get_timestamp(struct zram *zram, u32 index)
{
	return (zram->table[index].value & ZRAM_TIMESTAMP_MASK)
		>> ZRAM_TIMESTAMP_SHIFT;
}

static void zram_set_timestamp(struct zram *zram, u32 index, u32 time)
{
	zram->table[index].value &= ~ZRAM_TIMESTAMP_MASK;
	zram->table[index].value |= ((time << ZRAM_TIMESTAMP_SHIFT)
				     & ZRAM_TIMESTAMP_MASK);
}

static int zram_wbq_enqueue(struct zram *zram, u32 index, u32 time)
{
	int ret = 0;
	unsigned long head, tail;

	spin_lock(&zram->producer_lock);
	head = zram->wbq.head;
	tail = ACCESS_ONCE(zram->wbq.tail);

	if (!CIRC_SPACE(head, tail, ZRAM_WBQ_WORDS)) {
		ret = -ENOSPC;
		goto out;
	}
	zram->wbq.buf[head] = GEN_WBQ_DATA(index, time);
	smp_wmb();
	zram->wbq.head = (head + 1) & (ZRAM_WBQ_WORDS - 1);
out:
	spin_unlock(&zram->producer_lock);
	return ret;
}

static int zram_wbq_dequeue(struct zram *zram)
{
	int ret = 0;
	unsigned long head, tail;

	spin_lock(&zram->consumer_lock);
	head = ACCESS_ONCE(zram->wbq.head);
	tail = zram->wbq.tail;

	if (!CIRC_CNT(head, tail, ZRAM_WBQ_WORDS)) {
		ret = -ENOENT;
		goto out;
	}

	smp_read_barrier_depends();
	zram->wbq.tail = (tail + 1) & (ZRAM_WBQ_WORDS - 1);
out:
	spin_unlock(&zram->consumer_lock);
	return ret;
}

static int zram_wbq_peak(struct zram *zram, u32 *index, u32 *time)
{
	int ret = 0;
	unsigned long head, tail;

	spin_lock(&zram->consumer_lock);
	head = ACCESS_ONCE(zram->wbq.head);
	tail = zram->wbq.tail;

	if (!CIRC_CNT(head, tail, ZRAM_WBQ_WORDS)) {
		ret = -ENOENT;
		goto out;
	}

	smp_read_barrier_depends();
	if (index)
		*index = GET_WBQ_IDX(zram->wbq.buf, tail);
	if (time)
		*time  = GET_WBQ_TIME(zram->wbq.buf, tail);
out:
	spin_unlock(&zram->consumer_lock);
	return ret;
}

#if PAGE_SIZE != 4096
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return bvec->bv_len != PAGE_SIZE;
}
#else
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return false;
}
#endif

/*
 * Check if request is within bounds and aligned on zram logical blocks.
 */
static inline bool valid_io_request(struct zram *zram,
		sector_t start, unsigned int size)
{
	u64 end, bound;

	/* unaligned request */
	if (unlikely(start & (ZRAM_SECTOR_PER_LOGICAL_BLOCK - 1)))
		return false;
	if (unlikely(size & (ZRAM_LOGICAL_BLOCK_SIZE - 1)))
		return false;

	end = start + (size >> SECTOR_SHIFT);
	bound = zram->disksize >> SECTOR_SHIFT;
	/* out of range range */
	if (unlikely(start >= bound || end > bound || start > end))
		return false;

	/* I/O request is valid */
	return true;
}

static void update_position(u32 *index, int *offset, struct bio_vec *bvec)
{
	*index  += (*offset + bvec->bv_len) / PAGE_SIZE;
	*offset = (*offset + bvec->bv_len) % PAGE_SIZE;
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
	unsigned long val;

	page = (unsigned long *)ptr;
	val = page[0];

	for (pos = 1; pos < PAGE_SIZE / sizeof(*page); pos++) {
		if (val != page[pos])
			return false;
	}

	*element = val;

	return true;
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

static ssize_t disksize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", zram->disksize);
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
		atomic_long_set(&zram->stats.max_used_pages,
				zs_get_total_pages(zram->mem_pool));
	}
	up_read(&zram->init_lock);

	return len;
}

static bool zram_wb_enabled(struct zram *zram)
{
	return zram->backing_dev;
}

static void reset_bdev(struct zram *zram)
{
	struct block_device *bdev;

	if (!zram_wb_enabled(zram))
		return;

	bdev = zram->bdev;
	if (zram->old_block_size)
		set_blocksize(bdev, zram->old_block_size);
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	/* hope filp_close flush all of IO */
	filp_close(zram->backing_dev, NULL);
	zram->backing_dev = NULL;
	zram->old_block_size = 0;
	zram->bdev = NULL;

	kfree(zram->bitmap);
	zram->bitmap = NULL;

	if (zram->wbq.buf)
		vfree(zram->wbq.buf);

	kthread_stop(zram->wb_thread);
}

static ssize_t backing_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct file *file = zram->backing_dev;
	char *p;
	ssize_t ret;

	down_read(&zram->init_lock);
	if (!zram_wb_enabled(zram)) {
		memcpy(buf, "none\n", 5);
		up_read(&zram->init_lock);
		return 5;
	}

	p = d_path(&file->f_path, buf, PAGE_SIZE -1);

	if (IS_ERR(p)) {
		ret = PTR_ERR(p);
		goto out;
	}

	ret = strlen(p);
	memmove(buf, p, ret);
	buf[ret++] = '\n';
out:
	up_read(&zram->init_lock);
	return ret;
}

static ssize_t backing_dev_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *file_name;
	struct file *backing_dev = NULL;
	struct inode *inode;
	struct address_space *mapping;
	unsigned int bitmap_sz, old_block_size = 0;
	unsigned long nr_pages, *bitmap = NULL;
	struct block_device *bdev = NULL;
	int err;
	unsigned int *wbq_buf = NULL;
	struct task_struct *wb_thread = NULL;
	struct zram *zram = dev_to_zram(dev);
	unsigned long nr_entries;

	file_name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!file_name)
		return -ENOMEM;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Can't setup backing device for initialized device\n");
		err = -EBUSY;
		goto out;
	}

	strlcpy(file_name, buf, len);

	backing_dev = filp_open(file_name, O_RDWR|O_LARGEFILE, 0);
	if (IS_ERR(backing_dev)) {
		err = PTR_ERR(backing_dev);
		backing_dev = NULL;
		goto out;
	}

	mapping = backing_dev->f_mapping;
	inode = mapping->host;

	/* Support only block device in this moment */
	if (!S_ISBLK(inode->i_mode)) {
		err = -ENOTBLK;
		goto out;
	}

	bdev = bdgrab(I_BDEV(inode));
	err = blkdev_get(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, zram);
	if (err < 0)
		goto out;

	nr_pages = i_size_read(inode) >> PAGE_SHIFT;
	nr_entries = nr_pages * ENTRY_PER_PAGE;
	bitmap_sz = BITS_TO_LONGS(nr_entries) * sizeof(long);
	bitmap = kzalloc(bitmap_sz, GFP_KERNEL);
	if (!bitmap) {
		err = -ENOMEM;
		goto out;
	}

	old_block_size = block_size(bdev);
	err = set_blocksize(bdev, PAGE_SIZE);
	if (err)
		goto out;

	wbq_buf = vmalloc(ZRAM_WBQ_SZ);
	if (!wbq_buf) {
		err = -ENOMEM;
		goto out;
	}

	wb_thread = kthread_run(zram_wb_flush_t, zram, "zram_wb_flush");
	if (IS_ERR(wb_thread)) {
		err = -EFAULT;
		goto out;
	}

	reset_bdev(zram);
	spin_lock_init(&zram->bitmap_lock);

	zram->old_block_size = old_block_size;
	zram->bdev = bdev;
	zram->backing_dev = backing_dev;
	zram->bitmap = bitmap;
	zram->nr_pages = nr_pages;
	zram->nr_entries = nr_entries;
	zram->wbq.buf = wbq_buf;
	zram->wbq.head = 0;
	zram->wbq.tail = 0;
	zram->wb_thread = wb_thread;
	atomic_set(&zram->wb_flush_cnt, 0);
	spin_lock_init(&zram->producer_lock);
	spin_lock_init(&zram->consumer_lock);
	init_waitqueue_head(&zram->wb_flush_wait);
	up_write(&zram->init_lock);

	pr_info("setup backing device %s\n", file_name);
	kfree(file_name);

	return len;
out:
	if (wbq_buf)
		vfree(wbq_buf);

	if (bitmap)
		kfree(bitmap);

	if (bdev)
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	if (backing_dev)
		filp_close(backing_dev, NULL);

	up_write(&zram->init_lock);

	kfree(file_name);

	return err;
}

static ssize_t force_wb_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	int rc;
	unsigned long timeout;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	/* trigger force writeback */
	zram->force_wb = true;
	timeout = jiffies + ZRAM_WB_FLUSH_TIMEOUT;

	/* wait for completion */
	do {
		msleep(100); //100ms
	} while (zram->force_wb == true
		 && time_is_after_jiffies(timeout));
	if (time_is_before_jiffies(timeout))
		pr_info("force wb flush operation timed out\n");

	/* reset force writeback flag */
	zram->force_wb = false;

	up_read(&zram->init_lock);

	return len;
}

#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
static void zram_perf_finish(struct zram *zram, ktime_t *start,
			     enum zram_perf_type type, int cnt)
{
	ktime_t finish;
	u32 diff_time;

	finish.tv64 = sched_clock();
	diff_time = (u32)ktime_us_delta(finish, *start);

	atomic_add(diff_time, &zram->stats.perf[type].time);
	atomic_add(cnt, &zram->stats.perf[type].count);
}

static void zram_perf_data_start(struct zram *zram,
				 struct zram_perf_data *data)
{
	data->zram = zram;
	data->start.tv64 = sched_clock();
}

static void zram_perf_data_finish(struct zram_perf_data *data,
				  enum zram_perf_type type, int cnt)
{
	zram_perf_finish(data->zram, &data->start, type, cnt);
}


#define scomp_compress(zstrm, src, comp_len) \
({ \
	int ret; \
	ktime_t start;\
	start.tv64 = sched_clock(); \
	ret = scomp_compress(zstrm, src, comp_len); \
	if (likely(!ret)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_COMPRESS, 1); \
	ret; \
});

#define zs_malloc(pool, size, gfp) \
({ \
	int ret; \
	ktime_t start;\
	start.tv64 = sched_clock(); \
	ret = zs_malloc(pool, size, gfp); \
	if (likely(ret)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_ZSMALLOC, 1); \
	ret; \
});

#define scomp_async_decompress(zstrm, src, src_len, dst) \
({ \
	int ret; \
	ktime_t start;\
	start.tv64 = sched_clock(); \
	ret = scomp_decompress(zstrm, src, src_len, dst); \
	if (likely(!ret)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_ASYNC_DECOMPRESS, 1); \
	ret; \
});

#define scomp_sync_decompress(zstrm, src, src_len, dst) \
({ \
	int ret; \
	ktime_t start;\
	start.tv64 = sched_clock(); \
	ret = scomp_decompress(zstrm, src, src_len, dst); \
	if (likely(!ret)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_SYNC_DECOMPRESS, 1); \
	ret; \
});

#else

static void zram_perf_data_start(struct zram *zram,
				struct zram_perf_data *data) {}
static void zram_perf_data_finish(struct zram_perf_data *data,
				enum zram_perf_type type, int cnt) {}

#define scomp_sync_decompress	scomp_decompress
#define scomp_async_decompress	scomp_decompress

#endif

/*
 * Allocate len entries.
 * one of entry(bit) means 2Kbyte
 */
static unsigned long get_entry_bdev(struct zram *zram, unsigned long len)
{
	unsigned long entry;

	spin_lock(&zram->bitmap_lock);
	/* skip 0 bit to confuse zram.handle = 0 */
	entry = bitmap_find_next_zero_area(zram->bitmap, zram->nr_entries,
					    1, len, 0);
	if (entry == zram->nr_entries) {
		spin_unlock(&zram->bitmap_lock);
		return 0;
	}
	bitmap_set(zram->bitmap, entry, len);
	spin_unlock(&zram->bitmap_lock);

	return entry;
}

static void put_entry_bdev(struct zram *zram, unsigned long entry,
			   unsigned long len)
{
	int was_set = 1, cnt = len;

	spin_lock(&zram->bitmap_lock);
	while (cnt--)
	       was_set &= test_bit(entry + cnt, zram->bitmap);
	bitmap_clear(zram->bitmap, entry, len);
	spin_unlock(&zram->bitmap_lock);
	WARN_ON_ONCE(!was_set);
}

static void zram_page_end_io(struct bio *bio, int error)
{
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (bio->bi_private) {
		zram_perf_data_finish(bio->bi_private, ZRAM_PERF_FLASH_READ,
				      ENTRY_PER_PAGE);
		kfree(bio->bi_private);
	}

	page_endio(page, bio_data_dir(bio), error);
	bio_put(bio);
}

/*
 * Returns 1 if the submission is successful.
 */
static int read_from_bdev_async(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent)
{
	struct bio *bio;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;

	bio->bi_iter.bi_sector = entry << SECTORS_PER_ENTRY_SHIFT;
	bio->bi_bdev = zram->bdev;
	if (!bio_add_page(bio, bvec->bv_page, bvec->bv_len, bvec->bv_offset)) {
		bio_put(bio);
		return -EIO;
	}

	if (!parent) {
#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
		bio->bi_private = kmalloc(sizeof(struct zram_perf_data),
					  GFP_ATOMIC);
		if (bio->bi_private)
			 zram_perf_data_start(zram, bio->bi_private);
#endif
		bio->bi_rw = READ;
		bio->bi_end_io = zram_page_end_io;
	} else {
		bio->bi_rw = parent->bi_rw;
		bio_chain(bio, parent);
	}

	submit_bio(bio->bi_rw, bio);
	atomic64_add(ENTRY_PER_PAGE, &zram->stats.num_wb_reads);

	return 1;
}

struct zram_work {
	struct work_struct work;
	struct zram *zram;
	unsigned long entry;
	struct bio *bio;
};

#if PAGE_SIZE != 4096
static void zram_sync_read(struct work_struct *work)
{
	struct bio_vec bvec;
	struct zram_work *zw = container_of(work, struct zram_work, work);
	struct zram *zram = zw->zram;
	unsigned long entry = zw->entry;
	struct bio *bio = zw->bio;

	read_from_bdev_async(zram, &bvec, entry, bio);
}

/*
 * Block layer want one ->make_request_fn to be active at a time
 * so if we use chained IO with parent IO in same context,
 * it's a deadlock. To avoid, it, it uses worker thread context.
 */
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	struct zram_work work;

	work.zram = zram;
	work.entry = entry;
	work.bio = bio;

	INIT_WORK_ONSTACK(&work.work, zram_sync_read);
	queue_work(system_unbound_wq, &work.work);
	flush_work(&work.work);
	destroy_work_on_stack(&work.work);

	return 1;
}
#else
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	WARN_ON(1);
	return -EIO;
}
#endif

static int read_from_bdev(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent, bool sync)
{
	if (sync)
		return read_from_bdev_sync(zram, bvec, entry, parent);
	else
		return read_from_bdev_async(zram, bvec, entry, parent);
}

static void zram_wb_clear(struct zram *zram, u32 index)
{
	unsigned long entry;
	size_t obj_size;

	zram_clear_flag(zram, index, ZRAM_WB);
	entry = zram_get_element(zram, index);
	obj_size = zram_get_obj_size(zram, index);
	zram_set_element(zram, index, 0);
	zram_set_timestamp(zram, index, 0);
	put_entry_bdev(zram, entry, DIV_ROUND_UP(obj_size, ENTRY_SIZE));
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
	sz = scomp_available_show(zram->compressor, buf);
	up_read(&zram->init_lock);

	return sz;
}

static ssize_t comp_algorithm_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char compressor[ARRAY_SIZE(zram->compressor)];
	size_t sz;

	strlcpy(compressor, buf, sizeof(compressor));
	/* ignore trailing newline */
	sz = strlen(compressor);
	if (sz > 0 && compressor[sz - 1] == '\n')
		compressor[sz - 1] = 0x00;

	if (!scomp_available_algorithm(compressor))
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		up_write(&zram->init_lock);
		pr_info("Can't change algorithm for initialized device\n");
		return -EBUSY;
	}

	strcpy(zram->compressor, compressor);
	up_write(&zram->init_lock);
	return len;
}

static ssize_t compact_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	zs_compact(zram->mem_pool);
	up_read(&zram->init_lock);

	return len;
}

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
	u64 orig_size, wb_write, wb_size, mem_used = 0;
	long max_used;
	ssize_t ret;

	down_read(&zram->init_lock);
	if (init_done(zram))
		mem_used = zs_get_total_pages(zram->mem_pool);

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);
	wb_write = atomic64_read(&zram->stats.num_wb_writes);
	wb_size = wb_write - atomic64_read(&zram->stats.num_wb_free);

	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8lu %8ld %8llu %8llu %8llu\n",
			orig_size << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.compr_data_size),
			mem_used << PAGE_SHIFT,
			zram->limit_pages << PAGE_SHIFT,
			max_used << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.same_pages),
			wb_size << ENTRY_SHIFT,
			wb_write << ENTRY_SHIFT);
	up_read(&zram->init_lock);

	return ret;
}

static ssize_t mm_stath_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	u64 orig_size, wb_write, wb_size, mem_used = 0;
	long max_used;
	ssize_t ret;

	down_read(&zram->init_lock);
	if (init_done(zram))
		mem_used = zs_get_total_pages(zram->mem_pool);

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);
	wb_write = atomic64_read(&zram->stats.num_wb_writes);
	wb_size = wb_write - atomic64_read(&zram->stats.num_wb_free);

	ret = scnprintf(buf, PAGE_SIZE,
		"%10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n"
		"%8lluKB %8lluKB %8lluKB %8luKB %8ldKB %8lluKB %8lluKB %8lluKB %8lluKB %8lluKB\n",
		"org_size", "comp_size", "mem_used", "limit", "max_used",
		"same", "wb_pages", "wb_size", "wb_write", "wb_read",
		orig_size << 2,
		(u64)atomic64_read(&zram->stats.compr_data_size) >> 10,
		mem_used << 2,
		zram->limit_pages << 2,
		max_used << 2,
		(u64)atomic64_read(&zram->stats.same_pages) << 2,
		(u64)atomic64_read(&zram->stats.num_wb_pages) << 2,
		wb_size << 1,
		wb_write << 1,
		atomic64_read(&zram->stats.num_wb_reads) << 1);
	up_read(&zram->init_lock);

	return ret;
}

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
static DEVICE_ATTR_RO(mm_stath);
static DEVICE_ATTR_RO(debug_stat);

#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
static void inc_etime_summary(struct zram *zram, u32 index,
			      enum zram_etime_type type)
{
	unsigned long diff;
	atomic_t *summary;

	summary = zram->stats.etime_summary[type];
	diff = (jiffies / HZ) - zram->table[index].in_time;

	if (diff < ZRAM_ETIME_INTERVAL_B_MIN)
		atomic_inc(&summary[0]);
	else if (diff < ZRAM_ETIME_INTERVAL_C_MIN)
		atomic_inc(&summary[1]);
	else
		atomic_inc(&summary[2]);
}

static void zram_etime_update(struct zram *zram, u32 index, int type)
{
	/* table[index].read == 0 : for only FIRST swap-in */
	if (type == ZRAM_ETIME_SWAP_IN && zram->table[index].read == 0) {
		inc_etime_summary(zram, index, ZRAM_ETIME_SWAP_IN);
		zram->table[index].read = 1;
	} else if (type == ZRAM_ETIME_SWAP_FREE) {
		inc_etime_summary(zram, index, ZRAM_ETIME_SWAP_FREE);
		zram->table[index].in_time = 0;
		zram->table[index].read = 0;
	} else if (type == ZRAM_ETIME_SWAP_OUT) {
		zram->table[index].in_time = jiffies / HZ;
		zram->table[index].read = 0;
	}
}

#else
static inline void zram_etime_update(struct zram *zram, u32 index, int type) {}
#endif

static void zram_slot_lock(struct zram *zram, u32 index)
{
	bit_spin_lock(ZRAM_ACCESS, &zram->table[index].value);
}

static void zram_slot_unlock(struct zram *zram, u32 index)
{
	bit_spin_unlock(ZRAM_ACCESS, &zram->table[index].value);
}

static void zram_meta_free(struct zram *zram, u64 disksize)
{
	size_t num_pages = disksize >> PAGE_SHIFT;
	size_t index;

	/* Free all pages that are still in this zram device */
	for (index = 0; index < num_pages; index++)
		zram_free_page(zram, index);

	zs_destroy_pool(zram->mem_pool);
	vfree(zram->table);
}

static bool zram_meta_alloc(struct zram *zram, u64 disksize)
{
	size_t num_pages;

	num_pages = disksize >> PAGE_SHIFT;
	zram->table = vzalloc(num_pages * sizeof(*zram->table));
	if (!zram->table)
		return false;

	zram->mem_pool = zs_create_pool(zram->disk->disk_name);
	if (!zram->mem_pool) {
		vfree(zram->table);
		return false;
	}

	return true;
}

/*
 * To protect concurrent access to the same index entry,
 * caller should hold this table index entry's bit_spinlock to
 * indicate this index entry is accessing.
 */
static void zram_free_page(struct zram *zram, size_t index)
{
	unsigned long handle;

	if (zram_wb_enabled(zram) && zram_test_flag(zram, index, ZRAM_WB)) {
		size_t obj_size = zram_get_obj_size(zram, index);

		zram_wb_clear(zram, index);
		atomic64_dec(&zram->stats.pages_stored);
		atomic64_dec(&zram->stats.num_wb_pages);
		atomic64_add(DIV_ROUND_UP(obj_size, ENTRY_SIZE),
			     &zram->stats.num_wb_free);
		return;
	}

	/*
	 * No memory is allocated for same element filled pages.
	 * Simply clear same page flag.
	 */
	if (zram_test_flag(zram, index, ZRAM_SAME)) {
		zram_clear_flag(zram, index, ZRAM_SAME);
		zram_set_element(zram, index, 0);
		atomic64_dec(&zram->stats.same_pages);
		atomic64_dec(&zram->stats.pages_stored);
		return;
	}

	handle = zram_get_handle(zram, index);
	if (!handle)
		return;

	zs_free(zram->mem_pool, handle);

	atomic64_sub(zram_get_obj_size(zram, index),
			&zram->stats.compr_data_size);
	atomic64_dec(&zram->stats.pages_stored);

	zram_set_handle(zram, index, 0);
	zram_set_obj_size(zram, index, 0);
	zram_set_timestamp(zram, index, 0);
	zram_clear_flag(zram, index, ZRAM_PRE_WB);
}

static void zram_comp_page_end_io(struct bio *bio, int err)
{
	struct zram *zram;
	struct scomp_strm *zstrm;
	struct zram_decomp_data *data;
	struct page *src_page, *dst_page;
	char *src, *dst;

	data = (struct zram_decomp_data*)bio->bi_private;
	zram = data->zram;
	src_page = bio->bi_io_vec[0].bv_page;
	dst_page = data->dst_page;

	if (err) {
		pr_err("failed to read from wb(err:%d)\n", err);
		goto out;
	}

	/* use uncompressed size for stat instead of compressed size */
	zram_perf_data_finish(&data->perf_data, ZRAM_PERF_FLASH_READ,
			      ENTRY_PER_PAGE);

	/* decompress */
	src = kmap_atomic(src_page);
	dst = kmap_atomic(dst_page);
	zstrm = scomp_stream_get(zram->comp);
	err = scomp_async_decompress(zstrm, src, data->obj_size, dst);
	scomp_stream_put(zram->comp);
	kunmap_atomic(dst);
	kunmap_atomic(src);

	if (err)
		pr_err("failed to decompress from wb(err:%d)\n", err);
out:
	if (data->parent)
		bio_endio(data->parent, err);
	else
		page_endio(dst_page, bio_data_dir(bio), err);
	kfree(data);
	__free_page(src_page);
	bio_put(bio);
}

/*
 * This function requires table entry bit_spin_lock() being held.
 * And this function will call table entry bit_spin_unlock() before return.
 */
static int read_comp_from_bdev(struct zram *zram, u32 index,
			       struct page *dst_page, struct bio *parent)
{
	int ret = 0;
	struct bio *bio = NULL;
	struct page *page = NULL;
	struct zram_decomp_data *data = NULL;
	size_t obj_size = zram_get_obj_size(zram, index);
	u32 entry_cnt;

	data = kzalloc(sizeof(struct zram_decomp_data), GFP_ATOMIC);
	if (!data) {
		pr_err("Failed to allocate decomp data!!\n");
		ret = -ENOMEM;
		goto err;
	}

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio) {
		pr_err("failed to allocate bio in read!!\n");
		ret = -ENOMEM;
		goto err;
	}

	page = alloc_page(GFP_NOFS | __GFP_HIGH);
	if (!page) {
		pr_err("failed to allocate page in read..!!\n");
		ret = -ENOMEM;
		goto err;
	}

	data->zram = zram;
	data->dst_page = dst_page;
	data->obj_size = obj_size;
	data->parent = parent;

	bio->bi_bdev = zram->bdev;
	bio->bi_rw = READ;
	bio->bi_end_io = zram_comp_page_end_io;
	bio->bi_private = data;
	bio->bi_iter.bi_sector = zram_get_element(zram, index)
				<< SECTORS_PER_ENTRY_SHIFT;
	entry_cnt = DIV_ROUND_UP(obj_size, ENTRY_SIZE);
	ret = bio_add_page(bio, page, entry_cnt * ENTRY_SIZE, 0);
	if (!ret)
		goto err;

	zram_slot_unlock(zram, index);

	atomic64_add(entry_cnt, &zram->stats.num_wb_reads);
	zram_perf_data_start(zram, &data->perf_data);

	/* increment parent bio remain count */
	if (parent)
		atomic_inc(&parent->bi_remaining);
	submit_bio(bio->bi_rw, bio);

	return 1; /* async */

err:
	if (data)
		kfree(data);
	if (bio)
		bio_put(bio);
	if (page)
		__free_page(page);

	zram_slot_unlock(zram, index);
	return ret;
}

static int __zram_bvec_read(struct zram *zram, struct page *page, u32 index,
				struct bio *bio, bool partial_io)
{
	int ret;
	unsigned long handle;
	unsigned int size;
	void *src, *dst;

	zram_slot_lock(zram, index);
	if (zram_wb_enabled(zram) && zram_test_flag(zram, index, ZRAM_WB)) {
		if (zram_get_obj_size(zram, index) == PAGE_SIZE) {
			struct bio_vec bvec;

			zram_slot_unlock(zram, index);

			bvec.bv_page = page;
			bvec.bv_len = PAGE_SIZE;
			bvec.bv_offset = 0;
			return read_from_bdev(zram, &bvec,
					zram_get_element(zram, index),
					bio, partial_io);
		}
		/*
		 * read from bdev and decopmress it
		 * this operation call zram_slot_unlock() before return.
		 */
		return read_comp_from_bdev(zram, index, page, bio);
	}

	handle = zram_get_handle(zram, index);
	if (!handle || zram_test_flag(zram, index, ZRAM_SAME)) {
		unsigned long value;
		void *mem;

		value = handle ? zram_get_element(zram, index) : 0;
		mem = kmap_atomic(page);
		zram_fill_page(mem, PAGE_SIZE, value);
		kunmap_atomic(mem);
		zram_slot_unlock(zram, index);
		return 0;
	}

	size = zram_get_obj_size(zram, index);

	src = zs_map_object(zram->mem_pool, handle, ZS_MM_RO);
	if (size == PAGE_SIZE) {
		dst = kmap_atomic(page);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(dst);
		ret = 0;
	} else {
		struct scomp_strm *zstrm = scomp_stream_get(zram->comp);
		struct timeval start, finish;

		dst = kmap_atomic(page);
		ret = scomp_sync_decompress(zstrm, src, size, dst);
		kunmap_atomic(dst);
		scomp_stream_put(zram->comp);
	}
	zs_unmap_object(zram->mem_pool, handle);
	zram_slot_unlock(zram, index);

	/* Should NEVER happen. Return bio error if it does. */
	if (unlikely(ret))
		pr_err("Decompression failed! err=%d, page=%u\n", ret, index);

	return ret;
}

static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
	int ret;
	struct page *page;

	page = bvec->bv_page;
	if (is_partial_io(bvec)) {
		/* Use a temporary buffer to decompress the page */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;
	}

	ret = __zram_bvec_read(zram, page, index, bio, is_partial_io(bvec));
	if (unlikely(ret))
		goto out;

	if (is_partial_io(bvec)) {
		void *dst = kmap_atomic(bvec->bv_page);
		void *src = kmap_atomic(page);

		memcpy(dst + bvec->bv_offset, src + offset, bvec->bv_len);
		kunmap_atomic(src);
		kunmap_atomic(dst);
	}
out:
	if (is_partial_io(bvec))
		__free_page(page);

	return ret;
}

static void zram_wb_end_io(struct bio *bio, int error)
{
	struct zram_wb_data *data = (struct zram_wb_data*)(bio->bi_private);
	struct page *page = (struct page *)(bio->bi_io_vec[0].bv_page);
	struct zram *zram = data->zram;
	u32 index, element, entry_cnt;
	unsigned long handle;

	index = data->index;
	element = data->element;

	zram_slot_lock(zram, index);
	entry_cnt = DIV_ROUND_UP(zram_get_obj_size(zram, index),
				 ENTRY_SIZE);
	if (!error && zram_test_flag(zram, index, ZRAM_PRE_WB)) {
		/* free zs_malloc */
		handle = zram_get_handle(zram, index);
		zs_free(zram->mem_pool, handle);
		atomic64_sub(zram_get_obj_size(zram, index),
			     &zram->stats.compr_data_size);

		/* update table entry */
		zram_set_flag(zram, index, ZRAM_WB);
		zram_set_element(zram, index, element);

		/* update stat */
		atomic64_inc(&zram->stats.num_wb_pages);
		atomic64_add(entry_cnt, &zram->stats.num_wb_writes);
	} else {
		/* expired entry or error(insert wbq again? or not?) */
		put_entry_bdev(zram, element, entry_cnt);
		if (error)
			pr_err("failed to submit_bio(idx:%u, err:%d)\n",
			       index, error);
	}
	zram_clear_flag(zram, index, ZRAM_PRE_WB);
	zram_slot_unlock(zram, index);

	__free_page(page);
	bio_put(bio);
	kfree(data);

	if (atomic_dec_and_test(&zram->wb_flush_cnt))
		wake_up(&zram->wb_flush_wait);
}

/*
 * This function requires table entry bit_spin_lock() being held.
 * And this function will call table entry bit_spin_unlock() before return.
 */
static int write_to_bdev(struct zram *zram, u32 index)
{
	int ret = 0;
	struct zram_wb_data *wb_data = NULL;
	struct bio *bio = NULL;
	struct page *page = NULL;
	unsigned long entry;
	char *src, *dst;
	size_t obj_size = zram_get_obj_size(zram, index);
	size_t entry_cnt = DIV_ROUND_UP(obj_size, ENTRY_SIZE);

	if (!current->plug)
		blk_start_plug(&zram->wb_plug);

	wb_data = kzalloc(sizeof(struct zram_wb_data), GFP_ATOMIC);
	if (!wb_data) {
		pr_err("Failed to allocate wb data!!\n");
		ret = -ENOMEM;
		goto err;
	}

	bio = bio_alloc(GFP_NOFS | __GFP_HIGH, 1);
	if (!bio) {
		pr_err("Failed to allocate bio in write!!\n");
		ret = -ENOMEM;
		goto err;
	}

	page = alloc_page(GFP_NOFS | __GFP_HIGH);
	if (!page) {
		pr_err("failed to allocate page in write..!!\n");
		ret = -ENOMEM;
		goto err;
	}

	entry = get_entry_bdev(zram, entry_cnt);
	if (!entry) {
		pr_err("No space in writeback dev.!!\n");
		ret = -ENOSPC;
		goto err;
	}

	bio->bi_iter.bi_sector = entry << SECTORS_PER_ENTRY_SHIFT;
	bio->bi_bdev = zram->bdev;

	/* copy data */
	dst = kmap_atomic(page);
	src = zs_map_object(zram->mem_pool,
			    zram_get_handle(zram, index),
			    ZS_MM_RO);
	memcpy(dst, src, obj_size);
	zs_unmap_object(zram->mem_pool, zram_get_handle(zram, index));
	kunmap_atomic(dst);

	/* prepare bio */
	if (!bio_add_page(bio, page, entry_cnt * ENTRY_SIZE, 0)) {
		put_entry_bdev(zram, entry, entry_cnt);
		ret = -EIO;
		goto err;
	}
	bio->bi_rw = REQ_WRITE;
	bio->bi_end_io = zram_wb_end_io;
	bio->bi_private = wb_data;
	zram_set_flag(zram, index, ZRAM_PRE_WB);

	/* copy information of table entry for end_io() */
	wb_data->zram = zram;
	wb_data->index = index;
	wb_data->element = entry;

	zram_slot_unlock(zram, index);

	atomic_inc(&zram->wb_flush_cnt);
	submit_bio(bio->bi_rw, bio);

	return 0;

err:
	if (wb_data)
		kfree(wb_data);
	if (bio)
		bio_put(bio);
	if (page)
		__free_page(page);
	zram_slot_unlock(zram, index);
	return ret;
}

static void flush_to_bdev(struct zram *zram)
{
	int rc;

	if (atomic_read(&zram->wb_flush_cnt) == 0)
		return;

	blk_finish_plug(&zram->wb_plug);
	rc = wait_event_timeout(zram->wb_flush_wait,
			!atomic_read(&zram->wb_flush_cnt),
			ZRAM_WB_FLUSH_TIMEOUT);

	if (rc == 0)
		pr_info("wb flush operation timed out (remained:%d)\n",
			 atomic_read(&zram->wb_flush_cnt));
}

static int __zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, struct bio *bio)
{
	int ret = 0;
	unsigned long alloced_pages;
	unsigned long handle = 0;
	unsigned int comp_len = 0;
	void *src, *dst, *mem;
	struct scomp_strm *zstrm;
	struct page *page = bvec->bv_page;
	unsigned long element = 0;
	enum zram_pageflags flags = 0;

	mem = kmap_atomic(page);
	if (page_same_filled(mem, &element)) {
		kunmap_atomic(mem);
		/* Free memory associated with this sector now. */
		flags = ZRAM_SAME;
		atomic64_inc(&zram->stats.same_pages);
		goto out;
	}
	kunmap_atomic(mem);

compress_again:
	zstrm = scomp_stream_get(zram->comp);
	src = kmap_atomic(page);
	ret = scomp_compress(zstrm, src, &comp_len);
	kunmap_atomic(src);

	if (unlikely(ret)) {
		scomp_stream_put(zram->comp);
		pr_err("Compression failed! err=%d\n", ret);
		zs_free(zram->mem_pool, handle);
		return ret;
	}

	if (unlikely(comp_len > max_zpage_size))
		comp_len = PAGE_SIZE;

	/*
	 * handle allocation has 2 paths:
	 * a) fast path is executed with preemption disabled (for
	 *  per-cpu streams) and has __GFP_DIRECT_RECLAIM bit clear,
	 *  since we can't sleep;
	 * b) slow path enables preemption and attempts to allocate
	 *  the page with __GFP_DIRECT_RECLAIM bit set. we have to
	 *  put per-cpu compression stream and, thus, to re-do
	 *  the compression once handle is allocated.
	 *
	 * if we have a 'non-null' handle here then we are coming
	 * from the slow path and handle has already been allocated.
	 */
	if (!handle)
		handle = zs_malloc(zram->mem_pool, comp_len,
				__GFP_NO_KSWAPD |
				__GFP_NOWARN |
				__GFP_HIGHMEM);
	if (!handle) {
		scomp_stream_put(zram->comp);
		atomic64_inc(&zram->stats.writestall);
		handle = zs_malloc(zram->mem_pool, comp_len,
				GFP_NOIO | __GFP_HIGHMEM);
		if (handle)
			goto compress_again;
		return -ENOMEM;
	}

	alloced_pages = zs_get_total_pages(zram->mem_pool);
	update_used_max(zram, alloced_pages);

	if (zram->limit_pages && alloced_pages > zram->limit_pages) {
		scomp_stream_put(zram->comp);
		zs_free(zram->mem_pool, handle);
		return -ENOMEM;
	}

	dst = zs_map_object(zram->mem_pool, handle, ZS_MM_WO);

	src = zstrm->buffer;
	if (comp_len == PAGE_SIZE)
		src = kmap_atomic(page);
	memcpy(dst, src, comp_len);
	if (comp_len == PAGE_SIZE)
		kunmap_atomic(src);

	scomp_stream_put(zram->comp);
	zs_unmap_object(zram->mem_pool, handle);
	atomic64_add(comp_len, &zram->stats.compr_data_size);
out:
	/*
	 * Free memory associated with this sector
	 * before overwriting unused sectors.
	 */
	zram_slot_lock(zram, index);
	zram_free_page(zram, index);

	if (flags) {
		zram_set_flag(zram, index, flags);
		zram_set_element(zram, index, element);
	}  else {
		zram_set_handle(zram, index, handle);
		zram_set_obj_size(zram, index, comp_len);
		if (zram_wb_enabled(zram) && comp_len > max_nowb_zpage_size) {
			u32 time = jiffies >> ZRAM_JIFFIES_SHIFT;
			if (zram_wbq_enqueue(zram, index, time) == -ENOSPC)
				pr_err("The wbq is full...!!\n");
			zram_set_timestamp(zram, index, time);
		}
	}
	zram_slot_unlock(zram, index);

	/* Update stats */
	atomic64_inc(&zram->stats.pages_stored);
	return ret;
}

static int zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
	int ret;
	struct page *page = NULL;
	void *src;
	struct bio_vec vec;

	vec = *bvec;
	if (is_partial_io(bvec)) {
		void *dst;
		/*
		 * This is a partial IO. We need to read the full page
		 * before to write the changes.
		 */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;

		ret = __zram_bvec_read(zram, page, index, bio, true);
		if (ret)
			goto out;

		src = kmap_atomic(bvec->bv_page);
		dst = kmap_atomic(page);
		memcpy(dst + offset, src + bvec->bv_offset, bvec->bv_len);
		kunmap_atomic(dst);
		kunmap_atomic(src);

		vec.bv_page = page;
		vec.bv_len = PAGE_SIZE;
		vec.bv_offset = 0;
	}

	ret = __zram_bvec_write(zram, &vec, index, bio);
out:
	if (is_partial_io(bvec))
		__free_page(page);
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
		zram_slot_lock(zram, index);
		zram_free_page(zram, index);
		zram_slot_unlock(zram, index);
		atomic64_inc(&zram->stats.notify_free);
		index++;
		n -= PAGE_SIZE;
	}
}

#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
#define flush_to_bdev(zram) \
({ \
	ktime_t start; \
	start.tv64 = sched_clock(); \
	flush_to_bdev(zram); \
	zram_perf_finish(zram, &start, ZRAM_PERF_FLASH_WRITE, 1); \
});

#define zram_bvec_write(zram, bvec, index, offset, bio) \
({ \
	int ret; \
	ktime_t start; \
	start.tv64 = sched_clock(); \
	ret = zram_bvec_write(zram, bvec, index, offset, bio); \
	if (likely(ret >= 0)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_SWAPOUT, 1); \
	ret; \
});

#define zram_bvec_read(zram, bvec, index, offset, bio) \
({ \
	int ret; \
	ktime_t start; \
	start.tv64 = sched_clock(); \
	ret = zram_bvec_read(zram, bvec, index, offset, bio); \
	if (likely(ret >= 0)) \
		zram_perf_finish(zram, &start, ZRAM_PERF_SWAPIN, 1); \
	ret; \
});
#endif
/*
 * Returns errno if it has some problem. Otherwise return 0 or 1.
 * Returns 0 if IO request was done synchronously
 * Returns 1 if IO request was successfully submitted.
 */
static int zram_bvec_rw(struct zram *zram, struct bio_vec *bvec, u32 index,
			int offset, int rw, struct bio *bio)
{
	unsigned long start_time = jiffies;
	int ret;

	generic_start_io_acct(rw, bvec->bv_len >> SECTOR_SHIFT,
			&zram->disk->part0);

	if (rw == READ) {
		atomic64_inc(&zram->stats.num_reads);
		ret = zram_bvec_read(zram, bvec, index, offset, bio);
		flush_dcache_page(bvec->bv_page);
		zram_etime_update(zram, index, ZRAM_ETIME_SWAP_IN);
	} else {
		atomic64_inc(&zram->stats.num_writes);
		ret = zram_bvec_write(zram, bvec, index, offset, bio);
		zram_etime_update(zram, index, ZRAM_ETIME_SWAP_OUT);
	}

	generic_end_io_acct(rw, &zram->disk->part0, start_time);

	if (unlikely(ret < 0)) {
		if (rw == READ)
			atomic64_inc(&zram->stats.failed_reads);
		else
			atomic64_inc(&zram->stats.failed_writes);
	}

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

	if (bio->bi_rw & REQ_DISCARD || bio->bi_rw & REQ_OP_WRITE_ZEROES) {
		zram_bio_discard(zram, index, offset, bio);
		bio_endio(bio, 0);
		return;
	}

	rw = bio_data_dir(bio);
	bio_for_each_segment(bvec, bio, iter) {
		struct bio_vec bv = bvec;
		unsigned int unwritten = bvec.bv_len;

		do {
			bv.bv_len = min_t(unsigned int, PAGE_SIZE - offset,
							unwritten);
			if (zram_bvec_rw(zram, &bv, index, offset,
					rw, bio) < 0)
				goto out;

			bv.bv_offset += bv.bv_len;
			unwritten -= bv.bv_len;

			update_position(&index, &offset, &bv);
		} while (unwritten);
	}

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

	if (!valid_io_request(zram, bio->bi_iter.bi_sector,
					bio->bi_iter.bi_size)) {
		atomic64_inc(&zram->stats.invalid_io);
		goto error;
	}

	__zram_make_request(zram, bio);
	return;

error:
	bio_io_error(bio);
	return;
}

static void zram_slot_free_notify(struct block_device *bdev,
				unsigned long index)
{
	struct zram *zram;

	zram = bdev->bd_disk->private_data;

	zram_slot_lock(zram, index);
	zram_etime_update(zram, index, ZRAM_ETIME_SWAP_FREE);
	zram_free_page(zram, index);
	zram_slot_unlock(zram, index);
	atomic64_inc(&zram->stats.notify_free);
}

static int zram_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, int rw)
{
	int offset, ret;
	u32 index;
	struct zram *zram;
	struct bio_vec bv;

	if (PageTransHuge(page))
		return -ENOTSUPP;
	zram = bdev->bd_disk->private_data;

	if (!valid_io_request(zram, sector, PAGE_SIZE)) {
		atomic64_inc(&zram->stats.invalid_io);
		ret = -EINVAL;
		goto out;
	}

	index = sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (sector & (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	bv.bv_page = page;
	bv.bv_len = PAGE_SIZE;
	bv.bv_offset = 0;

	ret = zram_bvec_rw(zram, &bv, index, offset, rw, NULL);
out:
	/*
	 * If I/O fails, just return error(ie, non-zero) without
	 * calling page_endio.
	 * It causes resubmit the I/O with bio request by upper functions
	 * of rw_page(e.g., swap_readpage, __swap_writepage) and
	 * bio->bi_end_io does things to handle the error
	 * (e.g., SetPageError, set_page_dirty and extra works).
	 */
	if (unlikely(ret < 0))
		return ret;

	switch (ret) {
	case 0:
		page_endio(page, rw, 0);
		break;
	case 1:
		ret = 0;
		break;
	default:
		WARN_ON(1);
	}
	return ret;
}

static void zram_reset_device(struct zram *zram)
{
	struct scomp *comp;
	u64 disksize;

	down_write(&zram->init_lock);

	zram->limit_pages = 0;

	if (!init_done(zram)) {
		up_write(&zram->init_lock);
		return;
	}

	comp = zram->comp;
	disksize = zram->disksize;
	zram->disksize = 0;

	set_capacity(zram->disk, 0);
	part_stat_set_all(&zram->disk->part0, 0);

	up_write(&zram->init_lock);
	/* I/O operation under all of CPU are done so let's free */
	zram_meta_free(zram, disksize);
	memset(&zram->stats, 0, sizeof(zram->stats));
	scomp_destroy(comp);
	reset_bdev(zram);
}

static ssize_t disksize_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 disksize;
	struct scomp *comp;
	struct zram *zram = dev_to_zram(dev);
	int err;

	disksize = memparse(buf, NULL);
	if (!disksize)
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Cannot change disksize for initialized device\n");
		err = -EBUSY;
		goto out_unlock;
	}

	disksize = PAGE_ALIGN(disksize);
	if (!zram_meta_alloc(zram, disksize)) {
		err = -ENOMEM;
		goto out_unlock;
	}

	comp = scomp_create(zram->compressor);
	if (IS_ERR(comp)) {
		pr_err("Cannot initialise %s compressing backend\n",
				zram->compressor);
		err = PTR_ERR(comp);
		goto out_free_meta;
	}

	zram->comp = comp;
	zram->disksize = disksize;
	set_capacity(zram->disk, zram->disksize >> SECTOR_SHIFT);

	revalidate_disk(zram->disk);
	up_write(&zram->init_lock);

	return len;

out_free_meta:
	zram_meta_free(zram, disksize);
out_unlock:
	up_write(&zram->init_lock);
	return err;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned short do_reset;
	struct zram *zram;
	struct block_device *bdev;

	ret = kstrtou16(buf, 10, &do_reset);
	if (ret)
		return ret;

	if (!do_reset)
		return -EINVAL;

	zram = dev_to_zram(dev);
	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	/* Do not reset an active device or claimed device */
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	/* From now on, anyone can't open /dev/zram[0-9] */
	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	revalidate_disk(zram->disk);
	bdput(bdev);

	mutex_lock(&bdev->bd_mutex);
	zram->claim = false;
	mutex_unlock(&bdev->bd_mutex);

	return len;
}

static int zram_open(struct block_device *bdev, fmode_t mode)
{
	int ret = 0;
	struct zram *zram;

	WARN_ON(!mutex_is_locked(&bdev->bd_mutex));

	zram = bdev->bd_disk->private_data;
	/* zram was claimed to reset so open request fails */
	if (zram->claim)
		ret = -EBUSY;

	return ret;
}

static const struct block_device_operations zram_devops = {
	.open = zram_open,
	.swap_slot_free_notify = zram_slot_free_notify,
	.rw_page = zram_rw_page,
	.owner = THIS_MODULE
};

static int zram_entry_exist(struct zram *zram, u32 index)
{
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return 1;

	if (zram_wb_enabled(zram) && zram_test_flag(zram, index, ZRAM_WB))
		return 1;

	if (zram_get_handle(zram, index))
		return 1;

	return 0;
}

#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
static ssize_t perf_stat_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret = 0;
	struct zram_perf *perf = zram->stats.perf;
	int i;
	u32 size[ZRAM_PERF_MAX];
	u32 time[ZRAM_PERF_MAX];
	u32 zs_avg_time;
	u32 orig_size, sum_obj_size = 0;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return 0;
	}

	for (i = 0; i < ZRAM_PERF_MAX ; i++) {
		size[i] = atomic_read(&perf[i].count) << 2;
		time[i] = atomic_read(&perf[i].time) / USEC_PER_MSEC;
	}

	/* correction for special stat case */
	size[ZRAM_PERF_FLASH_READ]  = atomic_read(&perf[ZRAM_PERF_FLASH_READ].count) << 1;
	size[ZRAM_PERF_FLASH_WRITE] = atomic_read(&zram->stats.num_wb_writes) << 1;

	zs_avg_time = atomic_read(&perf[ZRAM_PERF_ZSMALLOC].time) /
		atomic_read(&perf[ZRAM_PERF_ZSMALLOC].count);

	/* get orig_size and obj_size for compression ratio */
	orig_size = (u32) (atomic64_read(&zram->stats.pages_stored) << 2) * 10;

	for (i = 0; i < (zram->disksize >> PAGE_SHIFT); i++) {
		int obj_size;
		zram_slot_lock(zram, i);
		if (!zram_entry_exist(zram, i)) {
			zram_slot_unlock(zram, i);
			continue;
		}

		if (zram_test_flag(zram, i, ZRAM_SAME)) {
			zram_slot_unlock(zram, i);
			continue;
		}

		obj_size = zram_get_obj_size(zram, i);
		sum_obj_size += obj_size;
		zram_slot_unlock(zram, i);
	}
	sum_obj_size = sum_obj_size >> 10;

	ret = scnprintf(buf, PAGE_SIZE,
			"                %9s %9s  %4s\n"
			"swap-out        %9u %9u  %4u\n"
			" |- compress    %9u %9u  %4u \n"
			" `- zs_malloc   %9u %9u  %4u (%u us per alloc)\n"
			"swap-in (sync)  %9u %9u  %4u\n"
			" `- decompress  %9u %9u  %4u\n"
			"swap-in (async) \n"
			" |- decompress  %9u %9u  %4u\n"
			" `- flash read  %9u %9u  %4u\n"
			"wbq flush                 \n"
			" `- flash write %9u %9u  %4u\n\n"
			" * algorhtim : %s\n"
			" * compression ratio : x%1d.%1d\n",
			/* columns */
			"size(KB)", "time(ms)", "MB/s",
			/* swap_out */
			size[ZRAM_PERF_SWAPOUT] , time[ZRAM_PERF_SWAPOUT],
			size[ZRAM_PERF_SWAPOUT] / time[ZRAM_PERF_SWAPOUT],
			size[ZRAM_PERF_COMPRESS], time[ZRAM_PERF_COMPRESS],
			size[ZRAM_PERF_COMPRESS] / time[ZRAM_PERF_COMPRESS],
			size[ZRAM_PERF_ZSMALLOC] , time[ZRAM_PERF_ZSMALLOC],
			size[ZRAM_PERF_ZSMALLOC] / time[ZRAM_PERF_ZSMALLOC],
			zs_avg_time,
			/* swap_in (sync) */
			size[ZRAM_PERF_SWAPIN] , time[ZRAM_PERF_SWAPIN],
			size[ZRAM_PERF_SWAPIN] / time[ZRAM_PERF_SWAPIN],
			size[ZRAM_PERF_SYNC_DECOMPRESS] , time[ZRAM_PERF_SYNC_DECOMPRESS],
			size[ZRAM_PERF_SYNC_DECOMPRESS] / time[ZRAM_PERF_SYNC_DECOMPRESS],
			/* swap_in (async) */
			size[ZRAM_PERF_ASYNC_DECOMPRESS], time[ZRAM_PERF_ASYNC_DECOMPRESS],
			size[ZRAM_PERF_ASYNC_DECOMPRESS] / time[ZRAM_PERF_ASYNC_DECOMPRESS],
			size[ZRAM_PERF_FLASH_READ], time[ZRAM_PERF_FLASH_READ],
			size[ZRAM_PERF_FLASH_READ] / time[ZRAM_PERF_FLASH_READ],
			/* wbq_flush */
			size[ZRAM_PERF_FLASH_WRITE], time[ZRAM_PERF_FLASH_WRITE],
			size[ZRAM_PERF_FLASH_WRITE] / time[ZRAM_PERF_FLASH_WRITE],
			zram->compressor,
			(orig_size / sum_obj_size) / 10,
			(orig_size / sum_obj_size) % 10);

	up_read(&zram->init_lock);
	return ret;
}

static ssize_t etime_entry_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i;
	struct zram *zram = dev_to_zram(dev);
	unsigned int cur_sec = jiffies / HZ;
	unsigned int cur[2][2] = {0,}, sum[2];
	ssize_t ret = 0;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return 0;
	}

	for (i = 0; i < (zram->disksize >> PAGE_SHIFT); i++) {
		int idx, etime;
		zram_slot_lock(zram, i);
		if (!zram_entry_exist(zram, i)) {
			zram_slot_unlock(zram, i);
			continue;
		}
		etime = cur_sec - zram->table[i].in_time;
		idx = (etime <= ZRAM_ETIME_INTERVAL_C_MIN) ? 0 : 1;

		if (zram_test_flag(zram, i, ZRAM_WB))
			cur[idx][1]++;
		else if (!zram_test_flag(zram, i, ZRAM_SAME))
			cur[idx][0]++;
		zram_slot_unlock(zram, i);
	}

	sum[0] = cur[0][0] + cur[0][1];
	sum[1] = cur[1][0] + cur[1][1];
	ret = scnprintf(buf, PAGE_SIZE,
			" [ Page count in zram by elapsed time ]\n\n"
			"                             Count    (  memory,      wb)\n"
			"          under %3d sec : %8u    (%8u,%8u)\n"
			"           over %3d sec : %8u    (%8u,%8u)\n"
			"                    sum : %8u\n",
			ZRAM_ETIME_INTERVAL_C_MIN, sum[0], cur[0][0], cur[0][1],
			ZRAM_ETIME_INTERVAL_C_MIN, sum[1], cur[1][0], cur[1][1],
			sum[0] + sum[1]);
	up_read(&zram->init_lock);
	return ret;
}

static ssize_t etime_summary_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret = 0;
	atomic_t *swap_in, *swap_free;
	unsigned int sum_in, sum_free;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return 0;
	}

	swap_in   = zram->stats.etime_summary[ZRAM_ETIME_SWAP_IN];
	swap_free = zram->stats.etime_summary[ZRAM_ETIME_SWAP_FREE];

	sum_in = atomic_read(&swap_in[0]) + atomic_read(&swap_in[1])
		+ atomic_read(&swap_in[2]);
	sum_free = atomic_read(&swap_free[0]) + atomic_read(&swap_free[1])
		+ atomic_read(&swap_free[2]);

	ret = scnprintf(buf, PAGE_SIZE,
			" [ Summary by elapsed time of pages ]\n\n"
			"                   swap-in   swap-free\n"
			"     0 ~ %3d :    %8u    %8u\n"
			"   %3d ~ %3d :    %8u    %8u\n"
			"   %3d ~     :    %8u    %8u\n"
			"      sum    :   %9u   %9u\n",
			ZRAM_ETIME_INTERVAL_B_MIN - 1,
			atomic_read(&swap_in[0]), atomic_read(&swap_free[0]),
			ZRAM_ETIME_INTERVAL_B_MIN, ZRAM_ETIME_INTERVAL_C_MIN - 1,
			atomic_read(&swap_in[1]), atomic_read(&swap_free[1]),
			ZRAM_ETIME_INTERVAL_C_MIN,
			atomic_read(&swap_in[2]), atomic_read(&swap_free[2]),
			sum_in, sum_free);
	up_read(&zram->init_lock);
	return ret;
}

static int bitcount(unsigned int v)
{
	unsigned c;

	c = (v & 0x55555555) + ((v >> 1) & 0x55555555);
	c = (c & 0x33333333) + ((c >> 2) & 0x33333333);
	c = (c & 0x0F0F0F0F) + ((c >> 4) & 0x0F0F0F0F);
	c = (c & 0x00FF00FF) + ((c >> 8) & 0x00FF00FF);
	c = (c & 0x0000FFFF) + ((c >> 16)& 0x0000FFFF);

	return c;
}

static ssize_t wb_bitmap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i = 0, c;
	char bit_char[2] = {0,};
	unsigned int bitmap_sz, char_cnt;
	unsigned long nr_pages, nr_entries;
	struct zram *zram = dev_to_zram(dev);

	nr_entries = zram->nr_pages * ENTRY_PER_PAGE;
	bitmap_sz = DIV_ROUND_UP(nr_entries, BITS_PER_BYTE);
	char_cnt = bitmap_sz / 8;

	down_read(&zram->init_lock);

	scnprintf(buf, PAGE_SIZE, "bitmap_sz : %d, char_cnt : %d, unit_size : 128K\n",
		  bitmap_sz, char_cnt);

	while (i < char_cnt * 2) {
		c = bitcount(zram->bitmap[i++]);
		c = c + bitcount(zram->bitmap[i++]);

		/* 0 ~ 63 */
		if (c)
			c = c / 6;

		bit_char[0] = '0' + c;
		strncat(buf, bit_char, PAGE_SIZE - 1 - strlen(buf));
	}

	up_read(&zram->init_lock);

	return strlen(buf);
}

static ssize_t entry_summary_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int i;
	struct zram *zram = dev_to_zram(dev);
	unsigned int same_cnt = 0;
	unsigned int cnt[8][2] = {0,};
	unsigned int size[8][2] = {0,};
	unsigned int sum_cnt[2] = {0,}, sum_size[2] = {0,};

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return 0;
	}

	for (i = 0; i < (zram->disksize >> PAGE_SHIFT); i++) {
		int obj_size, size_idx, type_idx;
		zram_slot_lock(zram, i);
		if (!zram_entry_exist(zram, i)) {
			zram_slot_unlock(zram, i);
			continue;
		}

		if (zram_test_flag(zram, i, ZRAM_SAME)) {
			same_cnt++;
			zram_slot_unlock(zram, i);
			continue;
		}

		obj_size = zram_get_obj_size(zram, i);
		type_idx = !!(zram_test_flag(zram, i, ZRAM_WB)
			      || zram_test_flag(zram, i, ZRAM_PRE_WB));

		if (obj_size <= 512)
			size_idx = 0;
		else if (obj_size <= 1024)
			size_idx = 1;
		else if (obj_size <= 2048)
			size_idx = 2;
		else if (obj_size <= 3072)
			size_idx = 3;
		else if (obj_size <= 4096)
			size_idx = 4;
		else
			pr_err("invalid obj size(%d)\n", obj_size);

		cnt[size_idx][type_idx]++;
		size[size_idx][type_idx] += obj_size;

		sum_cnt[type_idx]++;
		sum_size[type_idx] += obj_size;

		zram_slot_unlock(zram, i);
	}

	scnprintf(buf, PAGE_SIZE,
		  "           count (   ram /   bdev)     size (     ram /     bdev)\n"
		  "    same :%6u\n"
		  "  ~ 0.5K :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n"
		  "  ~ 1.0K :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n"
		  "  ~ 2.0K :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n"
		  "  ~ 3.0K :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n"
		  "    4.0K :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n"
		  "     Sum :%6u (%6u / %6u) %6uKB (%6uKB / %6uKB)\n",
		  same_cnt,
		  /* ~ 0.5K */
		  cnt[0][0] + cnt[0][1], cnt[0][0], cnt[0][1],
		  (size[0][0] + size[0][1]) >> 10,
		  (size[0][0] >> 10), (size[0][1] >> 10),
		  /* ~ 1.0K */
		  cnt[1][0] + cnt[1][1], cnt[1][0], cnt[1][1],
		  (size[1][0] + size[1][1]) >> 10,
		  (size[1][0] >> 10), (size[1][1] >> 10),
		  /* ~ 2.0K */
		  cnt[2][0] + cnt[2][1], cnt[2][0], cnt[2][1],
		  (size[2][0] + size[2][1]) >> 10,
		  (size[2][0] >> 10), (size[2][1] >> 10),
		  /* ~ 3.0K */
		  cnt[3][0] + cnt[3][1], cnt[3][0], cnt[3][1],
		  (size[3][0] + size[3][1]) >> 10,
		  (size[3][0] >> 10), (size[3][1] >> 10),
		  /* ~ 4.0K */
		  cnt[4][0] + cnt[4][1], cnt[4][0], cnt[4][1],
		  (size[4][0] + size[4][1]) >> 10,
		  (size[4][0] >> 10), (size[4][1] >> 10),
		  /* sum */
		  sum_cnt[0] + sum_cnt[1], sum_cnt[0], sum_cnt[1],
		  (sum_size[0] + sum_size[1]) >> 10,
		  (sum_size[0] >> 10), (sum_size[1] >> 10));
	up_read(&zram->init_lock);
	return strlen(buf);
}

static DEVICE_ATTR_RO(perf_stat);
static DEVICE_ATTR_RO(etime_entry);
static DEVICE_ATTR_RO(etime_summary);
static DEVICE_ATTR_RO(wb_bitmap);
static DEVICE_ATTR_RO(entry_summary);
#endif

#ifdef CONFIG_SNAPSWAP_DUMP_DATA
static ssize_t dump_data_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	ssize_t rtn;
	struct zram *zram = dev_to_zram(dev);
	struct file *filp = NULL;
	struct page *page = NULL;
	size_t num_entries, idx;
	loff_t pos = 0;
	void *mem = NULL;
	char *path;
	mm_segment_t fs;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!path) {
		up_read(&zram->init_lock);
		return -ENOMEM;
	}
	strlcpy(path, buf, len);
	filp = filp_open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
	kfree(path);
	if (IS_ERR(filp)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	page = alloc_page(GFP_NOIO | __GFP_HIGHMEM);
	if (!page) {
		rtn = -ENOMEM;
		goto err;
	}

	mem = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
	if (!mem) {
		rtn = -ENOMEM;
		goto err;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	num_entries = zram->disksize >> PAGE_SHIFT;
	for (idx = 0; idx < num_entries; idx++) {
		int is_async;
		unsigned long element;

		ClearPageUptodate(page);
		is_async = __zram_bvec_read(zram, page, idx, NULL, false);
		if (is_async == 1) {
			/* async read case */
			while (!PageUptodate(page))
				msleep(10);
		} else if (is_async) {
			/* error case */
			rtn = -EIO;
			break;
		}

		if (page_same_filled(mem, &element) == false) {
			rtn = vfs_write(filp, mem, PAGE_SIZE, &pos);
			if (rtn != PAGE_SIZE) {
				kunmap_atomic(mem);
				break;
			}
		}
	}
	set_fs(fs);

err:
	if (mem)
		vunmap(mem);
	if (page)
		__free_page(page);
	if (filp && !IS_ERR(filp))
		filp_close(filp, NULL);
	up_read(&zram->init_lock);
	return rtn;
}

static DEVICE_ATTR_WO(dump_data);
#endif

static DEVICE_ATTR_WO(compact);
static DEVICE_ATTR_RW(disksize);
static DEVICE_ATTR_RO(initstate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(mem_limit);
static DEVICE_ATTR_WO(mem_used_max);
static DEVICE_ATTR_RW(max_comp_streams);
static DEVICE_ATTR_RW(comp_algorithm);
static DEVICE_ATTR_RW(backing_dev);
static DEVICE_ATTR_WO(force_wb);

static struct attribute *zram_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_initstate.attr,
	&dev_attr_reset.attr,
	&dev_attr_compact.attr,
	&dev_attr_mem_limit.attr,
	&dev_attr_mem_used_max.attr,
	&dev_attr_max_comp_streams.attr,
	&dev_attr_comp_algorithm.attr,
	&dev_attr_backing_dev.attr,
	&dev_attr_force_wb.attr,
	&dev_attr_io_stat.attr,
	&dev_attr_mm_stat.attr,
	&dev_attr_mm_stath.attr,
	&dev_attr_debug_stat.attr,
#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
	&dev_attr_perf_stat.attr,
	&dev_attr_etime_entry.attr,
	&dev_attr_etime_summary.attr,
	&dev_attr_wb_bitmap.attr,
	&dev_attr_entry_summary.attr,
#endif
#ifdef CONFIG_SNAPSWAP_DUMP_DATA
	&dev_attr_dump_data.attr,
#endif
	NULL,
};

static const struct attribute_group zram_disk_attr_group = {
	.attrs = zram_disk_attrs,
};

/*
 * writeback flush thread
 */
static int is_wb_target_entry(struct zram *zram, u32 index,
			 u32 entry_time, u32 wbq_time)
{
	if (!zram_get_handle(zram, index))
		return 0;
	if (entry_time != wbq_time)
		return 0;
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return 0;
	if (zram_test_flag(zram, index, ZRAM_PRE_WB))
		return 0;
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		pr_err("the index(%u) is already ZRAM_WB\n", index);
		return 0;
	}
	return 1;
}

static int zram_wb_flush_t(void *arg)
{
	int ret = 0;
	struct zram *zram = arg;
	u32 write_cnt = 0;
	u32 index, wbq_time, entry_time;
	u32 interval = ZRAM_WB_FLUSH_INTERVAL;

loop:
	while (!init_done(zram) || interval) {
		if (kthread_should_stop())
			goto out;
		if (zram->force_wb)
			break;
		ssleep(1);
		interval--;
	}

	while (1) {
		u32 cur_time;

		if (!zram->force_wb && write_cnt >= ZRAM_WB_FLUSH_THR_CNT)
			break;

		ret = zram_wbq_peak(zram, &index, &wbq_time);
		if (ret == -ENOENT) {
			zram->force_wb = false;
			break;
		}

		zram_slot_lock(zram, index);
		entry_time = zram_get_timestamp(zram, index);

		/* check writeback target entry */
		if (!is_wb_target_entry(zram, index, entry_time, wbq_time)) {
			zram_slot_unlock(zram, index);
			zram_wbq_dequeue(zram);
			continue;
		}

		/* check elapsed time of oldest entry*/
		if (!zram->force_wb) {
			cur_time = (jiffies >> ZRAM_JIFFIES_SHIFT) & 0xFFF;

			if (entry_time > cur_time)
				cur_time = cur_time | 0x1000;
			if (entry_time + ZRAM_WB_AGE > cur_time) {
				zram_slot_unlock(zram, index);
				break;
			}
		}

		/*
		 * write data to bdev.
		 * This operation call zram_slot_unlock() before return.
		 */
		ret = write_to_bdev(zram, index);
		if (ret) {
			pr_err("Failed to write_to_bdev(ret:%d, idx:%u)\n",
			       ret, index);
			break;
		}
		zram_wbq_dequeue(zram);
		write_cnt++;
	}

	if (write_cnt >= ZRAM_WB_FLUSH_THR_CNT)
		interval = ZRAM_WB_FLUSH_THR_INTERVAL;
	else
		interval = ZRAM_WB_FLUSH_INTERVAL;

	if (!write_cnt)
		goto loop;

	/* flush to wb */
	flush_to_bdev(zram);
	pr_info("Flush wb %u cnt\n", write_cnt);

	write_cnt = 0;
	goto loop;
out:
	return ret;
}

/*
 * Allocate and initialize new zram device. the function returns
 * '>= 0' device_id upon success, and negative value otherwise.
 */
static int zram_add(void)
{
	struct zram *zram;
	struct request_queue *queue;
	int ret, device_id;

	zram = kzalloc(sizeof(struct zram), GFP_KERNEL);
	if (!zram)
		return -ENOMEM;

	ret = idr_alloc(&zram_index_idr, zram, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_free_dev;
	device_id = ret;

	init_rwsem(&zram->init_lock);

	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		pr_err("Error allocating disk queue for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_idr;
	}

	blk_queue_make_request(queue, zram_make_request);

	/* gendisk structure */
	zram->disk = alloc_disk(1);
	if (!zram->disk) {
		pr_err("Error allocating disk structure for device %d\n",
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
	snprintf(zram->disk->disk_name, 16, "snapswap%d", device_id);

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
	blk_queue_max_discard_sectors(zram->disk->queue, UINT_MAX);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, zram->disk->queue);

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

	zram->disk->queue->backing_dev_info->capabilities |=
			BDI_CAP_STABLE_WRITES;
	add_disk(zram->disk);

	ret = sysfs_create_group(&disk_to_dev(zram->disk)->kobj,
				&zram_disk_attr_group);
	if (ret < 0) {
		pr_err("Error creating sysfs group for device %d\n",
				device_id);
		goto out_free_disk;
	}
	strlcpy(zram->compressor, default_compressor, sizeof(zram->compressor));

	pr_info("Added device: %s\n", zram->disk->disk_name);

	return device_id;

out_free_disk:
	del_gendisk(zram->disk);
	put_disk(zram->disk);
out_free_queue:
	blk_cleanup_queue(queue);
out_free_idr:
	idr_remove(&zram_index_idr, device_id);
out_free_dev:
	kfree(zram);
	return ret;
}

static int zram_remove(struct zram *zram)
{
	struct block_device *bdev;

	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	/*
	 * Remove sysfs first, so no one will perform a disksize
	 * store while we destroy the devices. This also helps during
	 * hot_remove -- zram_reset_device() is the last holder of
	 * ->init_lock, no later/concurrent disksize_store() or any
	 * other sysfs handlers are possible.
	 */
	sysfs_remove_group(&disk_to_dev(zram->disk)->kobj,
			&zram_disk_attr_group);

	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	bdput(bdev);

	pr_info("Removed device: %s\n", zram->disk->disk_name);

	blk_cleanup_queue(zram->disk->queue);
	del_gendisk(zram->disk);
	put_disk(zram->disk);
	kfree(zram);
	return 0;
}

static int zram_remove_cb(int id, void *ptr, void *data)
{
	zram_remove(ptr);
	return 0;
}

static void destroy_devices(void)
{
	idr_for_each(&zram_index_idr, &zram_remove_cb, NULL);
	idr_destroy(&zram_index_idr);
	unregister_blkdev(zram_major, "snapswap");
}

static int __init zram_init(void)
{
	int ret;

	zram_major = register_blkdev(0, "snapswap");
	if (zram_major <= 0) {
		pr_err("Unable to get major number\n");
		return -EBUSY;
	}

	while (num_devices != 0) {
		mutex_lock(&zram_index_mutex);
		ret = zram_add();
		mutex_unlock(&zram_index_mutex);
		if (ret < 0)
			goto out_error;
		num_devices--;
	}

	return 0;

out_error:
	destroy_devices();
	return ret;
}

static void __exit zram_exit(void)
{
	destroy_devices();
}

module_init(zram_init);
module_exit(zram_exit);

module_param(num_devices, uint, 0);
MODULE_PARM_DESC(num_devices, "Number of pre-created zram devices");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Block Device");
