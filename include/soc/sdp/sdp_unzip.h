/*
 * linux/arch/arm/mach-sdp/sdp_unzip.h
 *
 * Copyright (C) 2012 Samsung Electronics.co
 * Author : seunjgun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 * 2014/04/15,roman.pen: allocation/deallocation of decompression buffer
 *
 */
#ifndef __SDP_UNZIP_H
#define __SDP_UNZIP_H

#include <linux/blkdev.h>/* for VDFS enum type */
#include <linux/scatterlist.h>

/* Max HW input buffer size for compressed data
 *   128k + one page for alignment
 */
#define HW_MAX_IBUFF_SZ    (33 << PAGE_SHIFT)
#define HW_MAX_IBUFF_SG_LEN    (HW_MAX_IBUFF_SZ >> PAGE_SHIFT)
/* Max number of simultaneous decompression threads */
#define HW_MAX_SIMUL_THR    2

#define GZIP_FLAG_ZLIB_FORMAT	(0x1UL<<0)
#define GZIP_FLAG_OTF_MMCREAD	(0x1UL<<1)
#define GZIP_FLAG_ENABLE_AUTH	(0x1UL<<2)
#define GZIP_FLAG_AUTH_STANDALONE	(0x1UL<<3)/* not use decompresser. only use auth(hash/decript) */

/* Completion callback for async mode */
typedef void (*sdp_unzip_cb_t)(int err, int decompressed, void *arg);

enum sdp_unzip_sha_result_e {
	GZIP_HASH_OK,
	GZIP_ERR_HASH_INPROGRESS,
	GZIP_ERR_HASH_MISSMATCH,
	GZIP_ERR_HASH_TIMEOUT,
};

struct sdp_unzip_auth_t {
	u32 *aes_ctr_iv;//128 input
	u32 *aes_user_key;//128 input

	u32 sha256_length;
	u32 *sha256_digest;//256 input

	enum sdp_unzip_sha_result_e sha256_result;
	u32 *sha256_digest_out;//256 output
	u32 __sha256_digest_out_buf[8];//buffer
};

/**
 * sdp_unzip_buf - buffer for sdp needs
 */
struct sdp_unzip_buf {
	void       *vaddr;/* vir addr */
	dma_addr_t  paddr;/* phy addr */
	size_t      size;/* requested size*/
	size_t      __sz;/* actual alloc size */
};


/* sdp unzip descriptor */
struct sdp_unzip_desc {
	u32				request_idx;
	u32				decompressed_bytes;
	u32				errorcode;

#ifdef CONFIG_SDP_UNZIP_AUTH
	/* AES-CTR, SHA256 support(after Jazz) */
	struct sdp_unzip_auth_t auth;
#endif
};

/**
 * sdp_unzip_alloc - allocates buffer for input compressed data
 *
 * Note:
 *   this buffer should be used only for sync/async decompression
 */
struct sdp_unzip_buf *sdp_unzip_alloc(size_t len);

/**
 * sdp_unzip_alloc - frees previously allocated buffer
 */
void sdp_unzip_free(struct sdp_unzip_buf *uzbuf);


/* request alloc/free */
struct sdp_unzip_desc* sdp_unzip_alloc_descriptor(u32 flags);
void sdp_unzip_free_descriptor(struct sdp_unzip_desc *uzdesc);

/**
 * sdp_unzip_decompress_async - decompress gzip block asynchronously
 * @buff : input buffer pointer. must be aligned 64bytes
 * @off : offset inside buffer to start decompression from
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @cb: completion callback
 * @arg: completion argument
 * @may_wait : return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
int sdp_unzip_decompress_async(struct sdp_unzip_desc *uzdesc, 
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	sdp_unzip_cb_t cb, void *arg,
	bool may_wait);

/**
 * sdp_unzip_decompress_sync - decompress gzip block synchronously
 * @ibuff : input buffer pointer. must be aligned 64bytes
 * @ilength : input buffer pointer. must be multiple of 64.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @may_wait: return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
int sdp_unzip_decompress_sync(struct sdp_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	bool may_wait);

/* Kick async decompressor to finish */
void sdp_unzip_update_endpointer(void);

/**
 * sdp_unzip_decompress_wait - waits for decompressor
 *
 * Return:
 *   < 0   - error
 *   other - decompressed bytes
 */	
int sdp_unzip_decompress_wait(struct sdp_unzip_desc *uzdesc);


#endif
