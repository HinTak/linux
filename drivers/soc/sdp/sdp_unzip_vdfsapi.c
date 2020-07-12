#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>

/************************* XXX for soc eval kernel XXX ************************/
#ifndef CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM
enum hw_iovec_comp_type {
           HW_IOVEC_COMP_UNCOMPRESSED=1<<1,
           HW_IOVEC_COMP_GZIP=1<<2,
           HW_IOVEC_COMP_ZLIB=1<<3,
           HW_IOVEC_COMP_LZO=1<<4,
           HW_IOVEC_COMP_MAX=1<<5,
};

enum hw_iovec_hash_type {
           HW_IOVEC_HASH_NONE=1<<1,
           HW_IOVEC_HASH_SHA1=1<<2,
           HW_IOVEC_HASH_SHA256=1<<3,
           HW_IOVEC_HASH_MD5=1<<4,
};

struct req_hw {
	enum hw_iovec_hash_type hash_type;
	u8 *hashdata;	// calculated hash data is saved
};

struct hw_iovec {
	unsigned long long  phys_off;
	unsigned int        len;
};

struct hw_capability {
	unsigned int comp_type;
	unsigned int hash_type;
	int min_size;		// (1 << min_size) compressed size support
	int max_size;		// (1 << max_size) compressed size support
};
#endif/*CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM*/
/******************************************************************************/

#include <mach/hw_decompress.h>

 #define GZIP_VER_STR	"20160721(initial release)"



/* defines */
#define NAME	"sdp-unzip-vdfsapi"
#define PREFIX	NAME": "

#define INPUT_ALIGNSIZE	64
#define INPUT_ALIGNADDR	8
#define INPUT_CIPHER_ALIGNSIZE	16
#define SHA256_SIZE	32

/* prototypes */
extern int sdp_unzip_sw_zlib_comp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_zlib_decomp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_gzip_comp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_gzip_decomp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_aes_ctr128_crypt(const void *key, int key_len, const void *iv, const char *clear_text, char *cipher_text, size_t size);
extern int sdp_unzip_sw_sha256_digest(const u8 *input, u32 ilen, u8 *out_digest);

extern int sdp_unzip_standalone_hash_async(struct sdp_unzip_desc *uzdesc, 
	struct scatterlist *input_sgl,
	sdp_unzip_cb_t cb, void *arg,
	bool may_wait);

/* global variable */
static struct dentry *debugfs = NULL;






/***************************** Internal Functions *****************************/
static int buf_to_sglist(void *vaddr, unsigned int length, struct scatterlist *sg, int max_nents)
{
		u32 offset = offset_in_page(vaddr);
		void *base = vaddr - offset;
		int i, nents = 0;

		nents = PAGE_ALIGN(offset + length) >> PAGE_SHIFT;
		BUG_ON(nents > max_nents);

		if(nents == 1) {
				sg_init_one(sg, vaddr, length);
		} else {
				sg_init_table(sg, nents);

				sg_set_buf(sg, (void *) ((uintptr_t) base + (uintptr_t) offset), PAGE_SIZE - offset);
				for(i = 1; i < nents-1; i++) {
						sg_set_buf(sg + i, (void *) ((uintptr_t) base + (uintptr_t) (PAGE_SIZE * i)), PAGE_SIZE);
				}
				sg_set_buf(sg + i, (void *) ((uintptr_t) base + (uintptr_t) (PAGE_SIZE * i)),
						((offset + length - 1)&~PAGE_MASK) + 1);
		}

		return nents;
}






















/****************************** VDFS API Selftest *****************************/
#ifdef CONFIG_DEBUG_FS
#define INFILL 0x5A
#define OUTFILL 0xA5

struct sdp_unzip_test_vector_t {
	const char *name;
	const u8 *input;
	u8 *result;/* output */
	u32 ilen;
	u32 rlen;

	const u8 *expect;
	u32 elen;

	enum hw_iovec_comp_type comp_type;
	struct req_hw rq_hw;

	bool is_zlib;
	bool is_auth_standalone;
	u32 fail;
};

enum vdfsapi_fail_type {
	FAIL_RETURN_ERROR,
	FAIL_DATA_MISSMATCH,
	FAIL_DATA_CORRUPTED,
	FAIL_HASH_MISSMATCH,
	FAIL_HASH_MISSMATCH_HW,
};


static struct scatterlist in_sgl[HW_MAX_IBUFF_SG_LEN];
static struct page *opages[32];
static const char sample_text[] = "The quick brown fox jumps over the lazy dog";

#if 0
static struct page *iSeqPages[32];
static struct page *iSeqPages[32];
static struct page *iRndPages[32];

static int vdfsapi_selftest_run(void)
{
}






static int vdfsapi_selftest_prepare_data(void)
{
}

#endif




static int vdfsapi_selftest_show(struct seq_file *s, void *data)
{
	const u32 alloc_size = 0x3000;
	struct req_hw rq_hw;
	u8 hash[SHA256_SIZE];
	u8 hash_ref[SHA256_SIZE];
	u8 *input_buf = NULL;
	size_t buf_len = 0;
	void *uzpriv = NULL;
	int i, ret, is_pass = false;

	seq_printf(s, PREFIX"***** VDFS API selftest *****\n");

	input_buf = kmalloc(alloc_size, GFP_KERNEL);

	/* fill buf to 0x5A */
	memset(input_buf, INFILL, alloc_size);
	memset(hash, 0x5A, SHA256_SIZE);
	memset(hash_ref, 0x5A, SHA256_SIZE);

	buf_len = ARRAY_SIZE(sample_text);
	memcpy(input_buf, sample_text, buf_len);

	/* S/W Hash calic */
	sdp_unzip_sw_sha256_digest(input_buf, buf_len, hash_ref);


	if((u32)input_buf & 0xFFFUL) {
		pr_err(PREFIX"buf is not page align! %p\n", input_buf);
		return -EINVAL;
	}
	opages[0] = virt_to_page(input_buf);

	ret = buf_to_sglist(input_buf, buf_len, in_sgl, ARRAY_SIZE(in_sgl));


	pr_info(PREFIX"[Hash] Test start.\n");
	rq_hw.hash_type = HW_IOVEC_HASH_SHA256;
	rq_hw.hashdata = hash;

	/* Prepare input buffer */
	ret = dma_map_sg(NULL, in_sgl, sg_nents(in_sgl), DMA_TO_DEVICE);
	if(ret == 0) {
		pr_err(PREFIX"unable to map input bufferr\n");
		return -EINVAL;
	}

	ret = unzip_decompress_async(in_sgl, buf_len, opages, 1, &uzpriv, NULL, NULL,
			true, HW_IOVEC_COMP_UNCOMPRESSED, &rq_hw);

	if (!ret) {
		/* Kick decompressor to start right now */
		unzip_update_endpointer(uzpriv);

		/* Wait and drop lock */
		ret = unzip_decompress_wait(uzpriv);
	} else {
		
	}

	/* Cleanup input buffer */
	dma_unmap_sg(NULL, in_sgl, sg_nents(in_sgl), DMA_TO_DEVICE);

	if(memcmp(hash_ref, hash, ARRAY_SIZE(hash_ref)) == 0) {
		is_pass = true;
	} else {
		pr_err(PREFIX"[Hash] Missmatch!\n");
		print_hex_dump(KERN_INFO, "SHA256 REF: ", DUMP_PREFIX_ADDRESS, 16, 1, hash_ref, SHA256_SIZE, true);
		print_hex_dump(KERN_INFO, "SHA256 CAL: ", DUMP_PREFIX_ADDRESS, 16, 1, hash, SHA256_SIZE, true);
	}

	for(i = buf_len; i < alloc_size; i++) {
		if(input_buf[i] != INFILL) {
			pr_err(PREFIX"[Hash] buffer corrupted 0x%02x at 0x%x\n", input_buf[i], i);
		}
	}
	pr_info(PREFIX"[Hash] Test done.\n");


	/* Print result! */
	if(is_pass) {
		seq_printf(s, PREFIX"[Hash] Test Pass.\n");
	} else {
		seq_printf(s, PREFIX"[Hash] Test Fail!\n");
	}


	kfree(input_buf);
	return 0;
}

static int vdfsapi_selftest_open(struct inode *inode, struct file *file)
{
	return single_open(file, vdfsapi_selftest_show, inode->i_private);
}

static const struct file_operations vdfsapi_selftest_fops = {
	.open           = vdfsapi_selftest_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};


static int vdfsapi_hwcap_show(struct seq_file *s, void *data)
{
	struct hw_capability hwcap;

	seq_printf(s, PREFIX"Show current H/W Capaility\n");

	hwcap = get_hw_capability();

	seq_printf(s, PREFIX"comp_type      = 0x%02x\n", hwcap.comp_type);
	seq_printf(s, PREFIX"hash_type      = 0x%02x\n", hwcap.hash_type);
	seq_printf(s, PREFIX"min_size       = %d\n", hwcap.min_size);
	seq_printf(s, PREFIX"max_size       = %d\n", hwcap.max_size);

	return 0;
}

static int vdfsapi_hwcap_open(struct inode *inode, struct file *file)
{
	return single_open(file, vdfsapi_hwcap_show, inode->i_private);
}

static const struct file_operations vdfsapi_hwcap_fops = {
	.open           = vdfsapi_hwcap_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};
#endif/*CONFIG_DEBUG_FS*/









static int __init sdp_unzip_vsfs_init(void)
{
	pr_info(PREFIX"module init. ver%s\n", GZIP_VER_STR);

	debugfs = debugfs_create_dir("sdp_unzip_vdfsapi", NULL);
	if (IS_ERR(debugfs)) {
		/* Don't complain -- debugfs just isn't enabled */
		return IS_ERR(debugfs);
	}
	if (!debugfs) {
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		pr_err(PREFIX"failed to initialize debugfs\n");
		return -EINVAL;
	}


	if (!debugfs_create_file("selftest", S_IRUSR, debugfs, NULL, &vdfsapi_selftest_fops)) {
		pr_err(PREFIX"can't create debugfs file - selftest");
	}

	if (!debugfs_create_file("hw_capaility", S_IRUSR, debugfs, NULL, &vdfsapi_hwcap_fops)) {
		pr_err(PREFIX"can't create debugfs file - hw_capaility");
	}

	return 0;
}
static void __exit sdp_unzip_vsfs_exit(void)
{
}

module_init(sdp_unzip_vsfs_init);
module_exit(sdp_unzip_vsfs_exit);

MODULE_DESCRIPTION("Samsung SDP SoC HW Decompress VDFS APIs");
MODULE_LICENSE("GPL v2");

