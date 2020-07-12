/*
 * HW decompression extension for MMC block device
 */

#include <linux/blkdev.h>
#include "hw_decompress_types.h"

/**
 * hw_decompress_fn - decompress block on MMC using hardware
 * @bdev:   block device
 * @vec:    scattered parts of compressed block
 * @vec_cnt:    number of scattered parts
 * @out_pages:  array of output pages
 * @out_cnt:    number of output pages
 * @rq_hash:    hash type and hash pointer(allocated from upper layer)
 * @comp_type:  compressed type
 * @enc_type:   encrytion type (not used)
 *
 * Description:
 *    If we have gzipped block on disk we can decompress it using
 *    hardware.
 *
 * Return:
 *     < 0   - error value
 *     other - decompressed size
 *
 * Note:
 *    All allocations here are being done with GFP_NOWAIT.
 *    We must be as fast as possible, so in case of memory
 *    preasure we return an -EBUSY and upper layer will do
 *    (probably) software decompression
 */
extern int hw_decompress_fn(struct block_device *bdev,
			    const struct hw_iovec *vec,
			    unsigned int vec_cnt,
			    struct page **out_pages,
				unsigned int out_cnt, struct req_hw *rq_hw);

/**
 * unzip_alloc - allocates buffer for input compressed data
 *
 * Note:
 *   this buffer should be used only for sync/async decompression
 */
extern struct unzip_buf *unzip_alloc(size_t len);

/**
 * unzip_free - frees previously allocated buffer
 */
extern void unzip_free(struct unzip_buf *buf);

/**
 * unzip_decompress_async
 * @sg:   scatterlist pointer for compressed data buffer
 * @comp_bytes:  compressed buffer bytes
 * @opages:  array of output pages
 * @npages:  number of output pages
 * @private: private filed on hw_req
 * @cb:   completion callback function for async mode
 * @arg:  argument passing to callback function
 * @may_wait: return -EBUSY if cannot wait
 * @comp_type:  compressed type
 * @hash  :  hash data pointer (for coupled H/W hash)
 *
 * Description:
 *  - Initalization / Setup to start decompressor
 *  - Start H/W hash calculator
 *    (if decompressor and hash calculator are coupled)
 *
 * Return:
 *     < 0   - error value
 *     other - decompressed size
 */
extern int unzip_decompress_async(struct scatterlist *sg,
			unsigned int comp_bytes,
			struct page **opages,
			int npages,
			void **private,
			unzip_cb_t cb,
			void *arg,
			bool may_wait,
			enum hw_iovec_comp_type comp_type,
			struct req_hw *rq_hw);

/**
 * unzip_update_endpointer - Kick decompressor to start
 * @private: private filed on hw_req
 */
extern void unzip_update_endpointer(void *private);

/**
 * calculate_hw_hash - start stand-alone H/W hash calculator
 * @vstart: virtual address of input buffer
 * @pstart: physical address of input buffer
 * @length: buffer length
 *
 * NOTE 1:
 * ONLY for "stand-alone" H/W hash caculator
 *
 * NOTE 2:
 * DO NOT return any hash data from this function.
 * Get the hash data when decompressor working is done.
 * See unzip_decompress_wait()
 */
extern void calculate_hw_hash(unsigned long vstart, unsigned long pstart,
			unsigned long length);

/**
 * unzip_decompress_wait - waits for decompressor
 * @private: private filed on hw_req
 * Return:
 *   < 0   - error
 *   other - decompressed bytes
 */
extern int unzip_decompress_wait(void *private);
