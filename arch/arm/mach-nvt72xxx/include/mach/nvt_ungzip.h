
#ifndef __NVT_HWDECOMP_HAL_H
#define __NVT_HWDECOMP_HAL_H

#include <linux/scatterlist.h>

/* Max HW input buffer size for compressed data
 *   128k + one page for alignment
 */
#define HW_MAX_IBUFF_PAGE_NUM	(33)
#define HW_MAX_IBUFF_SG_LEN    (HW_MAX_IBUFF_SZ >> PAGE_SHIFT)
#define HW_MAX_IBUFF_SZ    (33 << PAGE_SHIFT)
#define HW_MAX_DESC_BUFF_SZ    (1 << PAGE_SHIFT)

#ifdef CONFIG_NVT_UNZIP_FIFO
#define HW_MAX_SIMUL_THR    1
#else
#define HW_MAX_SIMUL_THR    2
#endif

#define GZIP_FLAG_ZLIB_FORMAT	(0x1UL<<0)
#define GZIP_FLAG_OTF_MMCREAD	(0x1UL<<1)
#define GZIP_FLAG_ENABLE_AUTH	(0x1UL<<2)

/* Completion callback for async mode */
typedef void (*nvt_unzip_cb_t)(int err, int decompressed, void *arg);

#if 0
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
#endif

enum nvt_unzip_sha_result_e {
	GZIP_HASH_OK,
	GZIP_ERR_HASH_INPROGRESS,
	GZIP_ERR_HASH_MISSMATCH,
	GZIP_ERR_HASH_TIMEOUT,
};

struct nvt_unzip_auth_t {
	u32 *aes_ctr_iv;
	u32 *aes_user_key;

	u32 sha256_length;
	u32 *sha256_digest;

	enum nvt_unzip_sha_result_e sha256_result;
	u32 *sha256_digest_out;
	u32 __sha256_digest_out_buf[8];
};

/**
 * unzip_buf - buffer for nvt needs
 */

struct nvt_unzip_buf {
	void       *vaddr;
	dma_addr_t  paddr;
	size_t      size;	/*what is the diff between size and __sz?*/
	size_t      __sz;
	long long	ts_alloc;/*add for nvt_unzip_alloc bugon*/
	long long	ts_free; /*add for nvt_unzip_alloc bugon*/
	long long	ts_free_des;
	long long	req_start_ns;
	long long	ts_wait_done;
	long long	ts_auth_start;
	long long	ts_auth_done;
};

/* nvt unzip descriptor */
struct nvt_unzip_desc {
	u32				request_idx;
	u32				decompressed_bytes;
	u32				errorcode;

#ifdef CONFIG_NVT_UNZIP_AUTH
	struct nvt_unzip_auth_t auth;
#endif
	u32				fast;
};


/**
 * nvt_unzip_alloc - allocates buffer for input compressed data
 *
 * Note:
 *   this buffer should be used only for sync/async decompression
 */
struct nvt_unzip_buf *nvt_unzip_alloc(size_t len);

/**
 * nvt_unzip_alloc - frees previously allocated buffer
 */
void nvt_unzip_free(struct nvt_unzip_buf *uzbuf);

/* request alloc/free */
struct nvt_unzip_desc *nvt_unzip_alloc_descriptor(u32 flags);
void nvt_unzip_free_descriptor(struct nvt_unzip_desc *uzdesc);
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
int nvt_unzip_decompress_async(struct nvt_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	nvt_unzip_cb_t cb, void *arg,
	bool may_wait);

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
int nvt_unzip_decompress_sync(struct nvt_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	bool may_wait);

void nvt_unzip_update_endpointer(void);

/**
 * nvt_unzip_decompress_wait - waits for decompressor
 *
 * Return:
 *   < 0   - error
 *   other - decompressed bytes
 */
int nvt_unzip_decompress_wait(struct nvt_unzip_desc *uzdesc);


#endif
