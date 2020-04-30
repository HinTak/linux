#ifndef __HWDECOMPRESS_H
#define __HWDECOMPRESS_H

#include <linux/blkdev.h>
#include <mach/sdp_unzip.h>

#define unzip_buf					sdp_unzip_req

static inline const struct hw_capability get_hw_capability(void)
{
	return (struct hw_capability) {
#if defined(CONFIG_ARCH_SDP1501)	// JazzM
		.comp_type = HW_IOVEC_COMP_GZIP | HW_IOVEC_COMP_ZLIB,
		.encryption = HW_IOVEC_ENCRYPT_NONE,	//HW AES ENCRYPT should be added.
		.hash_type = HW_IOVEC_HASH_SHA256,
		.min_size = 1,
		.max_size = 17,
#else
		.comp_type = HW_IOVEC_COMP_GZIP,
		.encryption = HW_IOVEC_ENCRYPT_NONE,
		.hash_type = HW_IOVEC_HASH_NONE,

		/* Don't care if input size is under 1<<17 (128KB)
		 * Need to update this info for Jazz */
		.min_size = 1,
		.max_size = 17,
#endif
	};
}

static inline void calculate_hw_hash(unsigned long vstart, unsigned long pstart,
		 unsigned long length)
{
    /* Do not support stand-alone H/W Hash.
       Support coupled H/W Hash with H/W Decompressor  */
}

static inline struct unzip_buf* unzip_alloc(size_t len)
{
	return sdp_unzip_alloc(len);
}

static inline void unzip_free(struct unzip_buf *buf)
{
	sdp_unzip_free(buf);
}

static inline int unzip_decompress_async(struct unzip_buf *buf, int off, struct page **opages, int npages,
	sdp_unzip_cb_t cb, void *arg, bool may_wait, enum hw_iovec_comp_type comp_type)
{
	struct hw_capability hw_cap = get_hw_capability();

	if(!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
		return -EINVAL;
	}
	//if(!(hw_cap.hash_type&hash_type)) {
	//	pr_err("[%s:%u] invalid hash_type 0x%x\n", __FUNCTION__, __LINE__, hash_type);
	//	return -EINVAL;
	//}
	//if(!(hw_cap.encryption&encryption)) {
	// pr_err("[%s:%u] invalid encryption 0x%x\n", __FUNCTION__, __LINE__, encryption);
	//	return -EINVAL;
	//}
	if(((0x1<<hw_cap.min_size) > buf->size) || ((0x1<<hw_cap.max_size) < buf->size)) {
		pr_err("[%s:%u] invalid size 0x%x\n", __FUNCTION__, __LINE__, buf->size);
		return -EINVAL;
	}

	buf->flags = 0;

	/* XXX: temp. true is normal mode, false is on the fly mode */
	if(!may_wait) {
		buf->flags |= GZIP_FLAG_OTF_MMCREAD;
	}

	switch(comp_type)
	{
		case HW_IOVEC_COMP_GZIP:	//GZIP
			break;
		case HW_IOVEC_COMP_ZLIB:	//ZLIB
			buf->flags |= GZIP_FLAG_ZLIB_FORMAT;
			break;
		default:
			pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
			return -EINVAL;
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	buf->flags |= GZIP_FLAG_ENABLE_AUTH;
	buf->auth.sha256_digest_out = buf->auth.__sha256_digest_out_buf;
#endif

	return sdp_unzip_decompress_async(buf, off, opages, npages, cb, arg, may_wait);
}

static inline int unzip_decompress_sync( struct unzip_buf *buf, int off, struct page **opages, int npages,
	bool may_wait, enum hw_iovec_comp_type comp_type)
{
	const struct hw_capability hw_cap = get_hw_capability();

	if(!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
		return -EINVAL;
	}
	//if(!(hw_cap.hash_type&hash_type)) {
	//	pr_err("[%s:%u] invalid hash_type 0x%x\n", __FUNCTION__, __LINE__, hash_type);
	//	return -EINVAL;
	//}
	//if(!(hw_cap.encryption&encryption)) {
	// pr_err("[%s:%u] invalid encryption 0x%x\n", __FUNCTION__, __LINE__, encryption);
	//	return -EINVAL;
	//}
	if(((0x1<<hw_cap.min_size) > buf->size) || ((0x1<<hw_cap.max_size) < buf->size)) {
		pr_err("[%s:%u] invalid size 0x%x\n", __FUNCTION__, __LINE__, buf->size);
		return -EINVAL;
	}

	buf->flags = 0;

	switch(comp_type)
	{
		case HW_IOVEC_COMP_GZIP:	//GZIP
			break;
		case HW_IOVEC_COMP_ZLIB:	//ZLIB
			buf->flags |= GZIP_FLAG_ZLIB_FORMAT;
			break;
		default:
			pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
			return -EINVAL;
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	buf->flags |= GZIP_FLAG_ENABLE_AUTH;
	buf->auth.sha256_digest_out = buf->auth.__sha256_digest_out_buf;
#endif

	return sdp_unzip_decompress_sync(buf, off, opages, npages, may_wait);
}

static inline void unzip_update_endpointer(void)
{
	sdp_unzip_update_endpointer();
}

static inline int unzip_decompress_wait(struct unzip_buf *buf,
		 int npage, struct page **opages, unsigned char *hash)
{
	int ret = 0;

	ret = sdp_unzip_decompress_wait(buf);

#if defined(CONFIG_SDP_UNZIP_AUTH)
	/* copy hash result */
	if (hash && buf->auth.sha256_digest_out)
		memcpy(hash, buf->auth.sha256_digest_out, sizeof(u8[32]));
#endif/*CONFIG_SDP_UNZIP_AUTH*/

	return ret;
}

/* same unzip_decompress_sync() */
/**
 * hw_decompress_sync - decompress gzip block synchronously
 * @ibuff : input buffer pointer. must be aligned 64bytes
 * @ilength : input buffer pointer. must be multiple of 64.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @may_wait: return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
static inline int hw_decompress_sync(void *ibuff, int ilength, struct page **opages, int npages, bool may_wait, enum hw_iovec_comp_type comp_type)
{
	struct sdp_unzip_req local_uzreq;

	memset(&local_uzreq, 0x0, sizeof(local_uzreq));
	local_uzreq = (struct sdp_unzip_req){
		.vaddr = ibuff,
		.paddr = 0,
		.size = ilength,
	};
	return sdp_unzip_decompress_sync(&local_uzreq, 0, opages, npages, may_wait);
}

#endif /* __HWDECOMPRESS_H */

