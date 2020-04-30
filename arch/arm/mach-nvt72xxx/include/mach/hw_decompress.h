#ifndef __HWDECOMPRESS_H
#define __HWDECOMPRESS_H


#include <linux/blkdev.h>
#include <mach/nvt_ungzip.h>

#define unzip_buf nvt_unzip_buf
#define DBG_ENTRY()	/*pr_err("%s entry %d\n", __func__, __LINE__);*/
static inline const struct hw_capability get_hw_capability(void)
{
	DBG_ENTRY();
	return (struct hw_capability) {

		.comp_type = HW_IOVEC_COMP_GZIP | HW_IOVEC_COMP_ZLIB,
		.enc_type = HW_IOVEC_ENCRYPT_NONE,
		.hash_type = HW_IOVEC_HASH_SHA256,

		/* Don't care if input size is under 1<<17 (128KB) */
		.min_size = 1,
		.max_size = 17,
	};
}

static inline void calculate_hw_hash(unsigned long vstart, unsigned long pstart,
		 unsigned long length)
{
	DBG_ENTRY();
    /* Do not support stand-alone H/W Hash.
       Support coupled H/W Hash with H/W Decompressor  */
}

static inline struct unzip_buf *unzip_alloc(size_t len)
{
	DBG_ENTRY();
	return nvt_unzip_alloc(len);
}

static inline void unzip_free(struct unzip_buf *buf)
{
	DBG_ENTRY();
	nvt_unzip_free(buf);
}

static inline int unzip_decompress_async(
	struct scatterlist *sg, unsigned int comp_bytes,
	struct page **opages, int npages, void **uzpriv, nvt_unzip_cb_t cb,
	void *arg, bool may_wait,
	 enum hw_iovec_comp_type comp_type, struct req_hw *rq_hw)
{
	struct hw_capability hw_cap = get_hw_capability();
	struct nvt_unzip_desc *uzdesc = NULL;
	struct scatterlist out_sgl[HW_MAX_IBUFF_SG_LEN];
	int i, ret = 0;
	u32 flags = 0;

	DBG_ENTRY();

	if (!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n",
			 __func__, __LINE__, comp_type);
		return -EINVAL;
	}

	if (((0x1<<hw_cap.min_size) > comp_bytes)
		|| ((0x1<<hw_cap.max_size) < comp_bytes)) {
		pr_err("[%s:%u] invalid size 0x%x\n",
			__func__, __LINE__, comp_bytes);
		return -EINVAL;
	}

	/* XXX: temp. true is normal mode, false is mmc on the fly mode */
	if (!may_wait) {
		/*todo check the flag otf_mmcread*/
		flags |= GZIP_FLAG_OTF_MMCREAD;
	}

	switch (comp_type) {
	case HW_IOVEC_COMP_GZIP:	/*GZIP*/
		break;
	case HW_IOVEC_COMP_ZLIB:	/*ZLIB*/
		flags |= GZIP_FLAG_ZLIB_FORMAT;
		break;
	default:
		pr_err("[%s:%u] invalid comp_type 0x%x\n",
			__func__, __LINE__, comp_type);
		return -EINVAL;
	}

#ifdef CONFIG_NVT_UNZIP_AUTH
	if (rq_hw) {
		switch (rq_hw->hash_type) {
		case HW_IOVEC_HASH_SHA256:
			flags |= GZIP_FLAG_ENABLE_AUTH;
			break;
        case HW_IOVEC_HASH_NONE:
			break;
		default:
			pr_err("[%s:%u] invalid hash_type 0x%x\n",
				__func__, __LINE__, rq_hw->hash_type);
			return -EINVAL;
		}
	}
#endif

	uzdesc = nvt_unzip_alloc_descriptor(flags);
	if (!uzdesc) {
		/*make checkpatch happy*/
		return -ENOMEM;
	}

#ifdef CONFIG_NVT_UNZIP_AUTH
	if (rq_hw && rq_hw->hash_type == HW_IOVEC_HASH_SHA256) {
		uzdesc->auth.sha256_length = comp_bytes;
		uzdesc->auth.sha256_digest_out = (void *)rq_hw->hashdata;
	}
#endif

	sg_init_table(out_sgl, npages);
	for (i = 0; i < npages; i++) {
		/*make checkpatch happy here*/
		sg_set_page(&out_sgl[i], opages[i], PAGE_SIZE, 0x0);
	}

	ret = nvt_unzip_decompress_async(
		uzdesc, sg, out_sgl, cb, arg, may_wait);
	if (ret < 0) {
		/* return error */
		nvt_unzip_free_descriptor(uzdesc);
		*uzpriv = NULL;
	} else {
		*uzpriv = uzdesc;
	}

	return ret;
}

static inline void unzip_update_endpointer(void *uzpriv)
{
	DBG_ENTRY();
	nvt_unzip_update_endpointer();
}

static inline int unzip_decompress_wait(void *uzpriv)
{
	struct nvt_unzip_desc *uzdesc = uzpriv;
	int ret = 0;

	DBG_ENTRY();
	ret = nvt_unzip_decompress_wait(uzdesc);

	nvt_unzip_free_descriptor(uzdesc);
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
static inline int hw_decompress_sync(
	void *ibuff, int ilength, struct page **opages,
	int npages, bool may_wait, enum hw_iovec_comp_type comp_type)
{
	struct hw_capability hw_cap = get_hw_capability();
	struct nvt_unzip_desc *uzdesc = NULL;
	struct scatterlist in_sgl[1];
	struct scatterlist out_sgl[HW_MAX_IBUFF_SG_LEN];
	int i, ret = 0;
	u32 flags = 0;

	DBG_ENTRY();
	if (!(hw_cap.comp_type&comp_type)) {
		pr_err("[%s:%u] invalid comp_type 0x%x\n",
			__func__, __LINE__, comp_type);
		return -EINVAL;
	}

	switch (comp_type) {
	case HW_IOVEC_COMP_GZIP:	/*GZIP*/
		break;
	case HW_IOVEC_COMP_ZLIB:	/*ZLIB*/
		flags |= GZIP_FLAG_ZLIB_FORMAT;
		break;
	default:
		pr_err("[%s:%u] invalid comp_type 0x%x\n",
			__func__, __LINE__, comp_type);
		return -EINVAL;
	}
	/*pr_err("%s : ibuff %p ilength %x  npages %d\n",
		__func__, ibuff, ilength, npages);*/
	uzdesc = nvt_unzip_alloc_descriptor(flags);
	if (!uzdesc)
		return -ENOMEM;

	sg_init_one(in_sgl, ibuff, ilength);
	sg_init_table(out_sgl, npages);
	for (i = 0; i < npages; i++) {
		/*make checkpatch happy here*/
		sg_set_page(&out_sgl[i], opages[i], PAGE_SIZE, 0x0);
	}

	ret = nvt_unzip_decompress_sync(uzdesc, in_sgl, out_sgl, may_wait);
	nvt_unzip_free_descriptor(uzdesc);

	return ret;
}
#endif /* __HWDECOMPRESS_H */

