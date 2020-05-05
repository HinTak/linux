#ifndef __HWDECOMPRESS_H
#define __HWDECOMPRESS_H

#include <mach/nvt_sha256.h>
#include <mach/nvt_ungzip.h>
#include <linux/blkdev.h>

/**
 * hw_decompress_sync - decompress gzip block synchronously
 * @ibuff : input buffer pointer. must be aligned 64bytes
 * @ilength : input buffer pointer. must be multiple of 64.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @may_wait: return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
static inline int hw_decompress_sync(void *ibuff, int ilength,
		struct page **opages, int npages, bool may_wait, enum hw_iovec_comp_type comp_type)
{
	return nvt_unzip_decompress_sync(ibuff, ilength, opages,
			npages, may_wait, comp_type);
}

static inline const struct hw_capability get_hw_capability(void)
{
	return (struct hw_capability) {
		.comp_type = HW_IOVEC_COMP_ZLIB | HW_IOVEC_COMP_GZIP | HW_IOVEC_COMP_UNCOMPRESSED,
		.encryption = HW_IOVEC_ENCRYPT_NONE,
		.hash_type = HW_IOVEC_HASH_SHA256,
		.min_size = 1,
		.max_size = 17,
	};
}
#endif /* __HWDECOMPRESS_H */
