#ifndef __HWDECOMPRESS_H
#define __HWDECOMPRESS_H

#include <linux/blkdev.h>
#include <soc/sdp/sdp_unzip.h>

#define unzip_buf sdp_unzip_buf

extern int sdp_unzip_hw_hash_fn( unsigned char *buf, size_t buf_len, char *hash );

extern int sdp_unzip_hw_decrypt_fn(struct page **src, int src_offset,
							struct page **dst, int dst_offset,
							int length, enum hw_iovec_encrypt_type enc_type,
							const u8 *enc_key, const u8 *enc_ivec);

static inline const struct hw_capability get_hw_capability(void)
{
	return (struct hw_capability) {
#if defined(CONFIG_SDP_UNZIP_AUTH) && defined(CONFIG_SDP_UNZIP_AUTH_STANDALONE)	// KantM
		.comp_type = HW_IOVEC_COMP_GZIP | HW_IOVEC_COMP_ZLIB/* | HW_IOVEC_COMP_UNCOMPRESSED*/,
		.enc_type = HW_IOVEC_ENCRYPT_AES128_CTR,
		.hash_type = HW_IOVEC_HASH_SHA256,
		.min_size = 1,
		.max_size = 17,
		.hw_hash_fn = sdp_unzip_hw_hash_fn,
		.hw_decrypt_fn = sdp_unzip_hw_decrypt_fn,
#elif defined(CONFIG_SDP_UNZIP_AUTH)	// JazzM
		.comp_type = HW_IOVEC_COMP_GZIP | HW_IOVEC_COMP_ZLIB,
		.enc_type = HW_IOVEC_ENCRYPT_AES128_CTR,
		.hash_type = HW_IOVEC_HASH_SHA256,
		.min_size = 1,
		.max_size = 17,
#else
		.comp_type = HW_IOVEC_COMP_GZIP,
		.enc_type = HW_IOVEC_ENCRYPT_NONE,
		.hash_type = HW_IOVEC_HASH_NONE,

		/* Don't care if input size is under 1<<17 (128KB) */
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

static inline int unzip_decompress_async(struct scatterlist *sg, unsigned int comp_bytes,
	struct page **opages, int npages, void **uzpriv, sdp_unzip_cb_t cb, void *arg,
	bool may_wait, enum hw_iovec_comp_type comp_type, struct req_hw *rq_hw)
{
	struct hw_capability hw_cap = get_hw_capability();
	struct sdp_unzip_desc *uzdesc = NULL;
	struct scatterlist out_sgl[HW_MAX_IBUFF_SG_LEN];
	int i, ret = 0;
	u32 flags = 0;


	if(!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
		return -EINVAL;
	}

	if(((0x1<<hw_cap.min_size) > comp_bytes) || ((0x1<<hw_cap.max_size) < comp_bytes)) {
		pr_err("[%s:%u] invalid size 0x%x\n", __FUNCTION__, __LINE__, comp_bytes);
		return -EINVAL;
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	if(rq_hw) {
		if(rq_hw->hash_type != HW_IOVEC_HASH_NONE) {
			if(hw_cap.hash_type&rq_hw->hash_type) {
				flags |= GZIP_FLAG_ENABLE_AUTH;
			} else {
				pr_err("[%s:%u] invalid hash_type 0x%x\n", __FUNCTION__, __LINE__, rq_hw->hash_type);
			}
		}

		if(rq_hw->enc_type != HW_IOVEC_ENCRYPT_NONE) {
			if(hw_cap.enc_type&rq_hw->enc_type) {
				flags |= GZIP_FLAG_ENABLE_AUTH;
			} else {
				pr_err("[%s:%u] invalid enc_type 0x%x\n", __FUNCTION__, __LINE__, rq_hw->enc_type);
			}
		}
	}
#endif


	/* XXX: temp. true is normal mode, false is mmc on the fly mode */
	if(!may_wait) {
		flags |= GZIP_FLAG_OTF_MMCREAD;
	}

	switch(comp_type)
	{
		case HW_IOVEC_COMP_GZIP:	//GZIP
			break;
		case HW_IOVEC_COMP_ZLIB:	//ZLIB
			flags |= GZIP_FLAG_ZLIB_FORMAT;
			break;
		case HW_IOVEC_COMP_UNCOMPRESSED:
			if(flags&GZIP_FLAG_ENABLE_AUTH) {
				flags |= GZIP_FLAG_AUTH_STANDALONE;
			} else {
				pr_err("[%s:%u] can't use HW_IOVEC_COMP_UNCOMPRESSED type(comp_type 0x%x)\n", __FUNCTION__, __LINE__, comp_type);
				return -EINVAL;
			}
			break;
		default:
			pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
			return -EINVAL;
	}



	/* alloc new desc */
	uzdesc = sdp_unzip_alloc_descriptor(flags);
	if(!uzdesc) {
		return -ENOMEM;
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	if(rq_hw && rq_hw->hash_type == HW_IOVEC_HASH_SHA256) {
		uzdesc->auth.sha256_length = comp_bytes;
		uzdesc->auth.sha256_digest_out = (void *)rq_hw->hashdata;

		if(uzdesc->auth.sha256_digest_out == NULL) {
			pr_err("[%s:%u] sha256_digest_out is NULL(hash_type 0x%x)\n", __FUNCTION__, __LINE__, rq_hw->hash_type);

			/* return error */
			sdp_unzip_free_descriptor(uzdesc);
			*uzpriv = NULL;
			return -EINVAL;
		}
	}

	if(rq_hw && rq_hw->enc_type == HW_IOVEC_ENCRYPT_AES128_CTR) {
		uzdesc->auth.aes_ctr_iv = (u32 *)rq_hw->enc_ivec;
		uzdesc->auth.aes_user_key = (u32 *)rq_hw->enc_key;
	}
#endif

	sg_init_table(out_sgl, npages);
	for(i = 0; i < npages; i++) {
		sg_set_page(&out_sgl[i], opages[i], PAGE_SIZE, 0x0);
	}

	ret = sdp_unzip_decompress_async(uzdesc, sg, out_sgl, cb, arg, may_wait);

	if(ret < 0) {
		/* return error */
		sdp_unzip_free_descriptor(uzdesc);
		*uzpriv = NULL;
	} else {
		*uzpriv = uzdesc;
	}

	return ret;
}

static inline void unzip_update_endpointer(void *uzpriv)
{
	sdp_unzip_update_endpointer();
}

static inline int unzip_decompress_wait(void *uzpriv)
{
	struct sdp_unzip_desc *uzdesc = uzpriv;
	int ret = 0;

	ret = sdp_unzip_decompress_wait(uzdesc);

	sdp_unzip_free_descriptor(uzdesc);
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
	struct hw_capability hw_cap = get_hw_capability();
	struct sdp_unzip_desc *uzdesc = NULL;
	struct scatterlist in_sgl[1];
	struct scatterlist out_sgl[HW_MAX_IBUFF_SG_LEN];
	int i, ret = 0;
	u32 flags = 0;

	if(!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
		return -EINVAL;
	}

	switch(comp_type)
	{
		case HW_IOVEC_COMP_GZIP:	//GZIP
			break;
		case HW_IOVEC_COMP_ZLIB:	//ZLIB
			flags |= GZIP_FLAG_ZLIB_FORMAT;
			break;
		default:
			pr_err("[%s:%u] invalid comp_type 0x%x\n", __FUNCTION__, __LINE__, comp_type);
			return -EINVAL;
	}

	uzdesc = sdp_unzip_alloc_descriptor(flags);
	if(!uzdesc) {
		return -ENOMEM;
	}

	sg_init_one(in_sgl, ibuff, ilength);
	sg_init_table(out_sgl, npages);
	for(i = 0; i < npages; i++) {
		sg_set_page(&out_sgl[i], opages[i], PAGE_SIZE, 0x0);
	}

	ret = sdp_unzip_decompress_sync(uzdesc, in_sgl, out_sgl, may_wait);
	sdp_unzip_free_descriptor(uzdesc);
}

#endif /* __HWDECOMPRESS_H */

