
#ifndef __NVT_HWDECOMP_HAL_H
#define __NVT_HWDECOMP_HAL_H

/* Max HW input buffer size for compressed data
 *   128k + one page for alignment
 */
#define HW_MAX_IBUFF_PAGE_NUM	(33)
#define HW_MAX_IBUFF_SZ    (33 << PAGE_SHIFT)
#define HW_MAX_DESC_BUFF_SZ    (1 << PAGE_SHIFT)

#define HW_MAX_SIMUL_THR    2 

#include <linux/blkdev.h>
#include "../../../../../drivers/mmc/card/hw_decompress_types.h"
/**
 * nvt_unzip_init - decompress gzip file
 * @ibuff : input buffer pointer. must be aligned QWORD(8bytes)
 * @ilength : input buffer pointer. must be multiple of 8.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 */
int nvt_unzip_init(void);

/**
 * unzip_buf - buffer for nvt needs
 */
struct unzip_buf {
	void       *vaddr;
	dma_addr_t  paddr;
	size_t      size;	//what is the difference between size and __sz?
	size_t      __sz;	//
};

/**
 * nvt_unzip_alloc - allocates buffer for input compressed data
 *
 * Note:
 *   this buffer should be used only for sync/async decompression
 */
struct unzip_buf *unzip_alloc(size_t len);

/**
 * nvt_unzip_alloc - frees previously allocated buffer
 */
void unzip_free(struct unzip_buf *buf);

/**
 * nvt_unzip_decompress_async - decompress gzip block asynchronously
 * @buff : input buffer pointer. must be aligned 64bytes
 * @off : offset inside buffer to start decompression from
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @cb: completion callback
 * @arg: completion argument
 * @may_wait : return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
#if 0
int nvt_unzip_decompress_async(struct nvt_unzip_buf *buf, int off,
			       struct page **opages, int npages,
			       nvt_unzip_cb_t cb, void *arg, bool may_wait, bool last_input);
#endif

int nvt_unzip_decompress_async_bouncebuf(struct unzip_buf *buf, int off,
			       struct page **opages, int npages,
			       unzip_cb_t cb, void *arg,
			       bool may_wait, bool last_input);
/**
 * nvt_unzip_decompress_async - decompress gzip block asynchronously
 * @buff : input buffer pointer. must be aligned 64bytes
 * @off : offset inside buffer to start decompression from
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @cb: completion callback
 * @arg: completion argument
 * @may_wait : return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
/* Kick async decompressor to finish */
void nvt_unzip_update_endpointer(void);

/**
 * nvt_unzip_decompress_wait - waits for decompressor
 *
 * Return:
 *   < 0   - error
 *   other - decompressed bytes
 */
//int nvt_unzip_decompress_wait(void);
int unzip_decompress_wait(struct unzip_buf *buf,
		int npage, struct page **opages, unsigned char *hash);

/**
 * nvt_unzip_decompress_sync - decompress gzip block synchronously
 * @ibuff : input buffer pointer. must be aligned 64bytes
 * @ilength : input buffer pointer. must be multiple of 64.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @may_wait: return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
int nvt_unzip_decompress_sync(void *ibuff, int ilength, struct page **opages,
			      int npages, bool may_wait, enum hw_iovec_comp_type);



#endif
