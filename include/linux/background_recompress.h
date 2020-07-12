#ifdef CONFIG_BACKGROUND_RECOMPRESS
#include <linux/crypto.h>
#include <linux/cputime.h>

#define BGRECOMP_TIME_BIT 19
#define BGRECOMP_TIME_BIT_MASK ((1 << BGRECOMP_TIME_BIT) - 1)
#define BGRECOMP_TIME_SHIFT (PAGE_SHIFT + 1)

enum bgrecomp_error_type {
	RECOMP_SKIP = 1,
	RECOMP_INVALID_HANDLE,
	RECOMP_INVALID_DATA,
	RECOMP_META_ERR,
	RECOMP_GOTO_SLEEP,
	RECOMP_TRY_NEXT,
};
 
enum bgrecomp_flag_type {
	RECOMP_SET = 1,
	RECOMP_CLEAR,
};

/*
 * handle : handle value which get from allocator
 * index : zram_meta->table index
 * data : [31-13] stored time / [12-0] compressed size
 * bgrecomp_lru : manage for recomp_list
 */
struct bgrecomp_handle {
	unsigned long handle;
	u32 index;
	unsigned int data;
	struct list_head bgrecomp_lru;
};

extern bool dynamic_bgrecomp_enable;
extern void __bgrecompress_list_store(struct list_head*);
extern void __bgrecompress_invalidate(struct list_head*);
extern int __bgrecompress_zram_update(unsigned char*, u32, unsigned int, unsigned int);
extern int __bgrecompress_decompress(struct page*, u32, unsigned int);
extern int __bgrecompd_decompress_page(unsigned char*, unsigned char*, unsigned int);
extern int __zram_handle_recomp_flag(u32, enum bgrecomp_flag_type);
extern unsigned int __get_bgrecomp_min(void);
extern unsigned int __get_bgrecomp_max(void);

/*
 * Returns seconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^30ns == 1.074s.
 */
static unsigned int bgrecompd_get_timestamp(void)
{
    return (unsigned int)(running_clock() >> 30LL);  /* 2^30 ~= 10^9 */
}

static unsigned int bgrecompd_store_data(unsigned int clen)
{
	unsigned int data = (bgrecompd_get_timestamp() & BGRECOMP_TIME_BIT_MASK) << BGRECOMP_TIME_SHIFT;
	data |= clen;
	return data;
}

static unsigned int bgrecompd_get_clen(unsigned int data)
{
	return data & (BIT(BGRECOMP_TIME_SHIFT) - 1);
}

static unsigned int bgrecompd_get_time(unsigned int data)
{
	return data >> BGRECOMP_TIME_SHIFT;
}

static inline void bgrecompress_list_store(struct list_head *recompd)
{
	__bgrecompress_list_store(recompd);
}

static inline void bgrecompress_invalidate(struct list_head *recompd)
{
	__bgrecompress_invalidate(recompd);
}

static inline int bgrecompress_zram_update(unsigned char *src, u32 index, unsigned int clen, unsigned int rclen)
{
	int ret = -1;

	ret = __bgrecompress_zram_update(src, index, clen, rclen);

	return ret;
}

static inline int bgrecompress_decompress(struct page *page, u32 index, unsigned int clen)
{
	int ret = -1;

	ret = __bgrecompress_decompress(page, index, clen);

	return ret;
}

static inline int bgrecompd_decompress_page(unsigned char *cmem, unsigned char *mem, unsigned int size)
{
	int ret = -1;

	ret = __bgrecompd_decompress_page(cmem, mem, size);

	return ret;
}

static inline int zram_handle_recomp_flag(u32 index, enum bgrecomp_flag_type type)
{
	int ret = -1;

	ret = __zram_handle_recomp_flag(index, type);

	return ret;
}

static inline unsigned int get_bgrecomp_min(void)
{
	return __get_bgrecomp_min();
}

static inline unsigned int get_bgrecomp_max(void)
{
	return __get_bgrecomp_max();
}
#endif

