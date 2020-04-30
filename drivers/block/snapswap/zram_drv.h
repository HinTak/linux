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

#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/rwsem.h>
#include <linux/zsmalloc.h>
#include <linux/crypto.h>

#include "zcomp.h"

/*-- Configurable parameters */

/*
 * Pages that compress to size greater than this are stored
 * uncompressed in memory.
 */
static const size_t max_zpage_size = PAGE_SIZE / 4 * 3;

/*
 * Pages that compress to size greater than this could be stored
 * in flash. (by writeback to flash)
 */
static const size_t max_nowb_zpage_size = PAGE_SIZE / 8 * 1;

/*
 * NOTE: max_zpage_size must be less than or equal to:
 *   ZS_MAX_ALLOC_SIZE. Otherwise, zs_malloc() would
 * always return failure.
 */
/*-- End of configurable params */

extern char saved_root_name[64];

#define SECTOR_SHIFT		9
#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))
#define ENTRY_SHIFT		11
#define ENTRY_SIZE		(1 << ENTRY_SHIFT)
#define SECTORS_PER_ENTRY_SHIFT	(ENTRY_SHIFT - SECTOR_SHIFT)
#define ENTRY_PER_PAGE		(PAGE_SIZE / ENTRY_SIZE)

/*
 * writeback queue entry structure
 *  31                 12 11          0
 * +---------------------+-------------+
 * |      index id       |    time     |
 * +---------------------+-------------+
 */

#define ZRAM_WBQ_SZ		4096 * 1024	/* should be power of 2 */
#define ZRAM_WBQ_WORDS		(ZRAM_WBQ_SZ / sizeof(u32))
#define ZRAM_WBQ_TIME_MASK	(0xFFF)
#define GEN_WBQ_DATA(idx, time)	(((idx) << 12) | ((time) & ZRAM_WBQ_TIME_MASK))
#define GET_WBQ_IDX(data, idx)	((data)[idx] >> 12)
#define GET_WBQ_TIME(data, idx) ((data)[idx] & ZRAM_WBQ_TIME_MASK)

/*
 * zram is mainly used for memory efficiency so we want to keep memory
 * footprint small so we can squeeze size and flags into a field.
 *
 * - value field
 * +----------------------------------+
 * |31 25|24          13|12          0|
 * +-----+--------------+-------------+
 * |flags|  timestamp   |     size    |
 * +----------------------------------+
 * flags     : for zram_pageflags
 * timestamp : insertion time (jiffies >> 10)
 * size      : object size (excluding header)
 *
 * - compare jiffies field
 *
 *  31             22 21             10 9               0
 * +-----------------+----------------+------------------+
 * |        -        |     time       |        -         |
 * +-----------------+----------------+------------------+
 */

#define ZRAM_FLAG_MASK		(0xFE000000)
#define ZRAM_FLAG_SHIFT		(25)
#define ZRAM_TIMESTAMP_MASK	(0x01FFE000)
#define ZRAM_TIMESTAMP_SHIFT	(13)
#define ZRAM_OBJ_SIZE_MASK	(0x00001FFF)
#define ZRAM_JIFFIES_SHIFT	(10)		/* 4ms x 1024 */
#define ZRAM_WB_AGE		(8)		/* 8 x [4ms x 1024] */
#define ZRAM_WB_FLUSH_INTERVAL	(30)		/* sec */
#define ZRAM_WB_FLUSH_THR_CNT	(5000)
#define ZRAM_WB_FLUSH_THR_INTERVAL	(2)	/* sec */
#define ZRAM_WB_FLUSH_TIMEOUT	((HZ * ZRAM_WB_FLUSH_INTERVAL) >> 3)

/* Flags for zram pages (table[page_no].value) */
enum zram_pageflags {
	/* Page consists the same element */
	ZRAM_SAME = ZRAM_FLAG_SHIFT,
	ZRAM_ACCESS,	/* page is now accessed */
	ZRAM_WB,	/* page is stored on backing_device */
	ZRAM_PRE_WB,	/* page will be stored on backing_device */

	__NR_ZRAM_PAGEFLAGS,
};

/*-- Data structures */

/* Allocated for each disk page */
struct zram_table_entry {
	union {
		unsigned long handle;
		unsigned long element;
	};
	unsigned long value;
#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
	unsigned long read:1;
	unsigned long in_time:31; /* unit:sec */
#endif
};

#define ZRAM_ETIME_INTERVAL_CNT 3
#define ZRAM_ETIME_INTERVAL_A_MIN 0
#define ZRAM_ETIME_INTERVAL_B_MIN 4
#define ZRAM_ETIME_INTERVAL_C_MIN 120

#define ZRAM_ETIME_SUMMARY_CNT 2

enum zram_etime_type {
	ZRAM_ETIME_SWAP_IN = 0,
	ZRAM_ETIME_SWAP_FREE,
	ZRAM_ETIME_SWAP_OUT
};

enum zram_perf_type {
	ZRAM_PERF_SWAPOUT = 0,
	ZRAM_PERF_COMPRESS,	/* stat of compress is done */
	ZRAM_PERF_ZSMALLOC,	/* stat of zs_malloc is done */
	ZRAM_PERF_SWAPIN,
	ZRAM_PERF_FLASH_READ,
	ZRAM_PERF_SYNC_DECOMPRESS,	/* stat of decompress is done */
	ZRAM_PERF_ASYNC_DECOMPRESS,	/* stat of decompress is done */
	ZRAM_PERF_FLASH_WRITE,
	ZRAM_PERF_MAX,
};

struct zram_perf {
	atomic_t count; /* total no. */
	atomic_t time;	/* total consumed time */
};

struct zram_stats {
	atomic64_t compr_data_size;	/* compressed size of pages stored */
	atomic64_t num_reads;	/* failed + successful */
	atomic64_t num_writes;	/* --do-- */
	atomic64_t failed_reads;	/* can happen when memory is too low */
	atomic64_t failed_writes;	/* can happen when memory is too low */
	atomic64_t invalid_io;	/* non-page-aligned I/O requests */
	atomic64_t notify_free;	/* no. of swap slot free notifications */
	atomic64_t same_pages;		/* no. of same element filled pages */
	atomic64_t num_wb_pages;	/* no. of pages currently in wb */
	atomic64_t num_wb_writes;	/* no. of wb write entries */
	atomic64_t num_wb_free;		/* no. of wb free entries */
	atomic64_t num_wb_reads;	/* no. of wb read entries */
	atomic64_t pages_stored;	/* no. of pages currently stored */
	atomic_long_t max_used_pages;	/* no. of maximum pages stored */
	atomic64_t writestall;		/* no. of write slow paths */
#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
	struct zram_perf perf[ZRAM_PERF_MAX];
	atomic_t etime_summary[ZRAM_ETIME_SUMMARY_CNT][ZRAM_ETIME_INTERVAL_CNT];
#endif
};

struct zram {
	struct zram_table_entry *table;
	struct zs_pool *mem_pool;
	struct scomp *comp;
	struct gendisk *disk;
	/* Prevent concurrent execution of device init */
	struct rw_semaphore init_lock;
	/*
	 * the number of pages zram can consume for storing compressed data
	 */
	unsigned long limit_pages;

	struct zram_stats stats;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */
	char compressor[CRYPTO_MAX_ALG_NAME];
	/*
	 * zram is claimed so open request will be failed
	 */
	bool claim; /* Protected by bdev->bd_mutex */
	struct file *backing_dev;
	struct block_device *bdev;
	unsigned int old_block_size;
	unsigned long *bitmap;
	unsigned long nr_pages;
	unsigned long nr_entries;
	spinlock_t bitmap_lock;

	/* snapswap */
	struct {
		u32 *buf;
		int head;
		int tail;
	} wbq;

	spinlock_t producer_lock;
	spinlock_t consumer_lock;

	struct task_struct *wb_thread;
	struct blk_plug wb_plug;
	wait_queue_head_t wb_flush_wait;
	atomic_t wb_flush_cnt;
	bool force_wb;
};

struct zram_perf_data {
#ifdef CONFIG_SNAPSWAP_ADVANCED_STAT
	struct zram *zram;
	ktime_t start;
#endif
};

struct zram_wb_data {
	struct zram *zram;
	u32 index;
	u32 element;
};

struct zram_decomp_data {
	struct zram *zram;
	struct page *dst_page;
	u32 obj_size;
	struct zram_perf_data perf_data;
	struct bio *parent;
};
#endif
