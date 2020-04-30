/*********************************************************************************************
 *
 *	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
 *
 *	author : seungjun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 *
 ********************************************************************************************/
//#define SDP_UNZIP_DEBUG
//#define GZIP_VER_STR	"20150820(Support JazzM Auth Funtion)"
//#define GZIP_VER_STR	"20150901(fix Hawk-M/P boot fail)"
//#define GZIP_VER_STR	"20150914(fix GZIP mode window size)"
//#define GZIP_VER_STR	"20150923(bug fix opages[] overflow. change when digest_out is not NULL, enable SHA256)"
//#define GZIP_VER_STR	"20151013(add debug property)"
//#define GZIP_VER_STR	"20151019(support new vdfs interface)"
//#define GZIP_VER_STR	"20151028(bugfix, dma_unmap_page is not called.)"
//#define GZIP_VER_STR	"20151110(bugfix, global variable resource protect and code refactoring)"
//#define GZIP_VER_STR	"20151120(add unzip statistics)"
//#define GZIP_VER_STR	"20151216(add use_clockgating debugfs)"
//#define GZIP_VER_STR	"20151217(disable clockgating)"
//#define GZIP_VER_STR	"20160212(add irq process time check)"
//#define GZIP_VER_STR	"20160317(support new interface using sglist, support linux4.1)"
//#define GZIP_VER_STR	"20160419(add unzip selftest)"
//#define GZIP_VER_STR	"20160715(support to hash/decrypt standalone function)"
//#define GZIP_VER_STR	"20160721(support HW_IOVEC_COMP_UNCOMPRESSED flag)"
//#define GZIP_VER_STR	"20160729(fix NULL pointer in sglist)"
//#define GZIP_VER_STR	"20170105(add invalid buf address check for debug)"
#define GZIP_VER_STR	"20170208(add abnormal register check)"



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <linux/highmem.h>
#include <linux/mmc/core.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>

#include <linux/delay.h>
#include <soc/sdp/sdp_unzip.h>
#include <linux/of.h>
#include <soc/sdp/soc.h>
#include <linux/debugfs.h>
#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
#include <mach/sdp_hwclock.h>
#endif

/* for selftest */
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/compress.h>
#include <crypto/sha.h>
#include <linux/zlib.h>
#include <linux/crc32.h>
#include <net/netlink.h>

#define R_GZIP_IRQ				0x00
#define R_GZIP_IRQ_MASK			0x04
#define R_GZIP_CMD				0x08
#define R_GZIP_IN_BUF_ADDR		0x00C
#define R_GZIP_IN_BUF_SIZE		0x010
#define R_GZIP_IN_BUF_POINTER	0x014
#define R_GZIP_OUT_BUF_ADDR		0x018
#define R_GZIP_OUT_BUF_SIZE		0x01C
#define R_GZIP_OUT_BUF_POINTER	0x020
#define R_GZIP_LZ_ADDR		0x024
#define R_GZIP_DEC_CTRL		0x28
#define R_GZIP_PROC_DELAY	0x2C
#define R_GZIP_TIMEOUT		0x30
#define R_GZIP_IRQ_DELAY	0x34
#define R_GZIP_FILE_INFO	0x38
#define R_GZIP_ERR_CODE		0x3C
#define R_GZIP_PROC_STATE	0x40
#define R_GZIP_ENC_DATA_END_DELAY	0x44
#define R_GZIP_CRC32_VALUE_HDL		0x48
#define R_GZIP_CRC32_VALUE_SW		0x4C
#define R_GZIP_ISIZE_VALUE_HDL		0x50
#define R_GZIP_ISIZE_VALUE_SW		0x54
#define R_GZIP_ADDR_LIST1			0x58
#define R_GZIP_IN_BUF_WRITE_CTRL	(0xD8)
#define R_GZIP_IN_BUF_WRITE_POINTER	(0xDC)

#define R_CP_CTRL			0xE0
#define R_AES_KEY3			0xE4
#define R_AES_KEY2			0xE8
#define R_AES_KEY1			0xEC
#define R_AES_KEY0			0xF0
#define R_AES_IV3			0xF4
#define R_AES_IV2			0xF8
#define R_AES_IV1			0xFC
#define R_AES_IV0			0x100
#define R_SHA_MSG_SIZE			0x104
#define R_SHA_RSA7			0x108
#define R_SHA_RSA6			0x10C
#define R_SHA_RSA5			0x110
#define R_SHA_RSA4			0x114
#define R_SHA_RSA3			0x118
#define R_SHA_RSA2			0x11c
#define R_SHA_RSA1			0x120
#define R_SHA_RSA0			0x124
#define R_SHA_HASH_VAL7			0x128
#define R_SHA_HASH_VAL6			0x12C
#define R_SHA_HASH_VAL5			0x130
#define R_SHA_HASH_VAL4			0x134
#define R_SHA_HASH_VAL3			0x138
#define R_SHA_HASH_VAL2			0x13C
#define R_SHA_HASH_VAL1			0x140
#define R_SHA_HASH_VAL0			0x144
#define R_SHA_AUTH_RESULT		0x148

#ifdef CONFIG_ARCH_SDP1601/* after KantM */
#define R_GZIP_CLK_EN			0x14C
#define R_DEC_EN				0x150
#define R_IN_CURDMA_SIZE		0x154
#define R_OUT_CURDMA_SIZE		0x158

#define V_GZIP_CLK_EN_DECOMP_CLK_EN			(0x1UL<<0)
#define V_DEC_EN_DECOMP_ENABLE				(0x1UL<<0)
#endif

#define R_GZIP_IN_ADDR_LIST_0		0x160

#define V_GZIP_IRQ_AUTH_DONE		(0x1UL<<4)
#define V_GZIP_IRQ_DECODE_ERROR		(0x1UL<<3)
#define V_GZIP_IRQ_OUTPUT_PAR_DONE	(0x1UL<<2)
#define V_GZIP_IRQ_INPUT_PAR_DONE	(0x1UL<<1)
#define V_GZIP_IRQ_DECODE_DONE		(0x1UL<<0)

#define V_GZIP_CTL_ZLIB_FORMAT	(0x1 << 28)
#define V_GZIP_CTL_ADVMODE	(0x1 << 24)
#define V_GZIP_CTL_ISIZE	(0x0 << 21)
#define V_GZIP_CTL_CRC32	(0x0 << 20)
#define V_GZIP_CTL_OUT_PAR	(0x1 << 12)
#define V_GZIP_CTL_IN_PAR	(0x1 << 8)
#define V_GZIP_CTL_OUT_ENDIAN_LITTLE	(0x1 << 4)
#define V_GZIP_CTL_IN_ENDIAN_LITTLE		(0x1 << 0)

#define V_CP_CRTL_AES_EN	(1<<0)
#define V_CP_CRTL_AES_CTR_INIT	(1<<2)
#define V_CP_CRTL_AES_ROM_KEY	(1<<4)

#define V_CP_CRTL_SHA_EN	(1<<1)
#define V_CP_CRTL_SHA_MSG_CLEAR	(1<<3)


#define GZIP_WINDOWSIZE	4		//(0:256, 1:512, 2:1024, 3:2048, 4:4096... 7: 32k

#define GZIP_ALIGNADDR	8
#define GZIP_ALIGNSIZE	64
#define GZIP_CIPHER_ALIGNSIZE	16
#define GZIP_SHA256_SIZE	32

#define GZIP_NR_PAGE	33
#define GZIP_PAGESIZE	4096

/* jazz for zlib mode 32kb window size */
#define UNZIP_LZBUF_SIZE 0x8000//32kb

#if defined(CONFIG_ARCH_SDP1501)||defined(CONFIG_ARCH_SDP1601)/* after Jazz */
#define GZIP_INPUT_PAR
#endif

#define TO_ALIGNED(value, align)	(  ((value)+(align)-1) & ~((align)-1)  )

typedef void * (sdp_mempool_alloc_t)(gfp_t gfp_mask, void *pool_data);
typedef void (sdp_mempool_free_t)(void *element, void *pool_data);

struct sdp_mempool {
	spinlock_t   lock;
	unsigned int max_nr;
	unsigned int remain_nr;
	void **elements;

	void *pool_data;
	sdp_mempool_alloc_t *alloc;
	sdp_mempool_free_t  *free;
};
static struct sdp_mempool *g_mempool = NULL;

static inline void __sdp_mempool_destroy(struct sdp_mempool *pool)
{
	while (pool->max_nr) {
		void *element = pool->elements[--pool->max_nr];
		pool->free(element, pool->pool_data);
	}
	kfree(pool->elements);
	kfree(pool);
}

static struct sdp_mempool *sdp_mempool_create(unsigned int max_nr,
					      sdp_mempool_alloc_t *alloc_fn,
					      sdp_mempool_free_t *free_fn,
					      void *pool_data)
{
	struct sdp_mempool *pool;
	gfp_t gfp_mask = GFP_KERNEL;
	int node_id = NUMA_NO_NODE;

	pool = kmalloc_node(sizeof(*pool), gfp_mask | __GFP_ZERO, node_id);
	if (!pool)
		return NULL;
	pool->elements = kmalloc_node(max_nr * sizeof(void *),
				      gfp_mask, node_id);
	if (!pool->elements) {
		kfree(pool);
		return NULL;
	}

	spin_lock_init(&pool->lock);
	pool->pool_data = pool_data;
	pool->alloc = alloc_fn;
	pool->free = free_fn;
	pool->max_nr = 0;

	/*
	 * First pre-allocate the guaranteed number of buffers.
	 */
	while (pool->max_nr < max_nr) {
		void *element;

		element = pool->alloc(gfp_mask, pool->pool_data);
		if (unlikely(!element)) {
			__sdp_mempool_destroy(pool);
			return NULL;
		}
		pool->elements[pool->max_nr++] = element;
	}

	BUG_ON(pool->max_nr != max_nr);
	pool->remain_nr = max_nr;

	return pool;
}

static void sdp_mempool_destroy(struct sdp_mempool *pool)
{
	BUG_ON(pool->max_nr != pool->remain_nr);
	__sdp_mempool_destroy(pool);
}

static void *sdp_mempool_alloc(struct sdp_mempool *pool, gfp_t gfp_mask)
{
	unsigned long flags;
	void *element = NULL;

	spin_lock_irqsave(&pool->lock, flags);
	if (likely(pool->remain_nr > 0))
		element = pool->elements[--pool->remain_nr];
	spin_unlock_irqrestore(&pool->lock, flags);

	if (unlikely(element == NULL))
		pr_err("sdp-unzip: memory pool is empty\n");

	return element;
}

static void sdp_mempool_free(void *element, struct sdp_mempool *pool)
{
	unsigned long flags;

	if (unlikely(!element))
		return;

	spin_lock_irqsave(&pool->lock, flags);
	if (!WARN_ON(pool->remain_nr == pool->max_nr))
		pool->elements[pool->remain_nr++] = element;
	spin_unlock_irqrestore(&pool->lock, flags);
}

static void *sdp_unzip_mempool_alloc(gfp_t gfp_mask, void *pool)
{
	struct sdp_unzip_buf *uzbuf;
	struct device *dev = pool;

	uzbuf = kzalloc(sizeof(*uzbuf), GFP_NOWAIT);
	if (!uzbuf) {
		dev_err(dev, "failed to allocate sdp_unzip_uzbuf\n");
		return NULL;
	}

	uzbuf->__sz = HW_MAX_IBUFF_SZ;
	uzbuf->vaddr = (void *)__get_free_pages(gfp_mask, get_order(uzbuf->__sz));
	if (!uzbuf->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}

	uzbuf->paddr = dma_map_single(dev, uzbuf->vaddr,
		uzbuf->__sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, uzbuf->paddr)) {
		dev_err(dev, "unable to map input buffer\n");
		goto err;
	}

	return uzbuf;

err:
	free_pages((unsigned long)uzbuf->vaddr, get_order(uzbuf->__sz));
	kfree(uzbuf);

	return NULL;
}

static void sdp_unzip_mempool_free(void *mem_, void *pool)
{
	struct device *dev = pool;
	struct sdp_unzip_buf *uzbuf = mem_;
	if (!uzbuf || !uzbuf->__sz)
		return;

	dma_unmap_single(dev, uzbuf->paddr, uzbuf->__sz, DMA_TO_DEVICE);
	free_pages((unsigned long)uzbuf->vaddr, get_order(uzbuf->__sz));
	kfree(uzbuf);
}

struct sdp_unzip_buf *sdp_unzip_alloc(size_t len)
{
	struct sdp_unzip_buf *uzbuf;

	BUG_ON(!g_mempool);

	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);

	uzbuf = sdp_mempool_alloc(g_mempool, GFP_NOWAIT);
	if (!uzbuf)
		return ERR_PTR(-ENOMEM);

	uzbuf->size = len;
	return uzbuf;
}

void sdp_unzip_free(struct sdp_unzip_buf *uzbuf)
{
	BUG_ON(!g_mempool);
	if (IS_ERR_OR_NULL(uzbuf))
		return;

	sdp_mempool_free(uzbuf, g_mempool);
}




/********************* S/W Compress/Encrypt/Hash APIs *************************/
int sdp_unzip_sw_zlib_comp(u8 *input, u32 ilen, u8 *output, u32 olen)
{
	struct {
		struct nlattr nla;
		int val;
	} nla_zlib_param = {
		.nla = {
		.nla_len = nla_attr_size(sizeof(int)),
		.nla_type = ZLIB_COMP_WINDOWBITS,
		},
		.val = (GZIP_WINDOWSIZE + 8),
	};

	struct crypto_pcomp *tfm;
	struct comp_request req;
	int res;

	tfm = crypto_alloc_pcomp("zlib", CRYPTO_ALG_TYPE_PCOMPRESS, 0);

	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
			"zlib", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req.next_in = input;
	req.avail_in = ilen;
	req.next_out = output;
	req.avail_out = olen;

	res = crypto_compress_setup(tfm, &nla_zlib_param, sizeof(nla_zlib_param));

	res = crypto_compress_init(tfm);

	//print_comp_request(&req);
	res = crypto_compress_final(tfm, &req);
	//print_comp_request(&req);
	if(res < 0) {
			pr_err("crypto_compress_final return error %d\n", res);
			crypto_free_pcomp(tfm);
			return 0;
	}


	crypto_free_pcomp(tfm);

	//pr_err("zlib compressed size %d\n", olen - req.avail_out);
	return olen - req.avail_out;
}

int sdp_unzip_sw_zlib_decomp(u8 *input, u32 ilen, u8 *output, u32 olen)
{
	struct crypto_pcomp *tfm;
	//const char *algo = crypto_tfm_alg_driver_name(crypto_pcomp_tfm(tfm));
	struct comp_request req;
	int res;

	tfm = crypto_alloc_pcomp("zlib", CRYPTO_ALG_TYPE_PCOMPRESS, 0);

	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
			"zlib", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req.next_in = input;
	req.avail_in = ilen;
	req.next_out = output;
	req.avail_out = olen;

	res = crypto_decompress_setup(tfm, NULL/*params*/, 0/*paramsize*/);

	res = crypto_decompress_init(tfm);

	//print_comp_request(&req);
	res = crypto_decompress_final(tfm, &req);
	//print_comp_request(&req);

	crypto_free_pcomp(tfm);

	return res;
}

int sdp_unzip_sw_gzip_comp(u8 *input, u32 ilen, u8 *output, u32 olen)
{
	struct {
		struct nlattr nla;
		int val;
	} nla_gzip_param = {
		.nla = {
		.nla_len = nla_attr_size(sizeof(int)),
		.nla_type = ZLIB_COMP_WINDOWBITS,
		},
		.val = -(GZIP_WINDOWSIZE + 8),
	};
	struct crypto_pcomp *tfm;
	struct comp_request req;
	u32 comp_len = 0;
	/*crc32^0xFFFFFFFF: d874bde4: 39 a3 4f 41 */
	u32 gzip_crc32 = 0;
	int res = -1;

	//pr_info("sdp_unzip_test_sw_gzip 0x%08x %d 0x%08x %d %s\n", input, ilen, output, olen, is_comp?"Comp":"Decomp");

	tfm = crypto_alloc_pcomp("zlib", CRYPTO_ALG_TYPE_PCOMPRESS, 0);
	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
			"zlib", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	res = crypto_compress_setup(tfm, &nla_gzip_param, sizeof(nla_gzip_param));
	if(res < 0) pr_err("crypto_compress_setup return error %d\n", res);
	res = crypto_compress_init(tfm);
	if(res < 0) pr_err("crypto_compress_init return error %d\n", res);

	req.next_in = input;
	req.avail_in = ilen;
	req.next_out = output + 10;
	req.avail_out = olen - 10 - 8;

	res = crypto_compress_final(tfm, &req);
	if(res < 0) {
			pr_err("crypto_compress_final return error %d\n", res);
			crypto_free_pcomp(tfm);
			return 0;
	}
	comp_len = res;
	crypto_free_pcomp(tfm);

	/* make gzip header */
	output[0] = 0x1F;
	output[1] = 0x8B;
	output[2] = 0x08;
	output[3] = 0x00;
	output[4] = 0x00;
	output[5] = 0x00;
	output[6] = 0x00;
	output[7] = 0x00;
	output[8] = 0x00;
	output[9] = 0x03;
	comp_len += 10;

	gzip_crc32 = crc32(~0, input, ilen) ^ 0xFFFFFFFFUL;
	memcpy(output+comp_len, &gzip_crc32, 4);
	comp_len += 4;

	memcpy(output+comp_len, &ilen, 4);
	comp_len += 4;

	//pr_err("zlib compressed size %d\n", comp_len);
	return comp_len;
}

int sdp_unzip_sw_gzip_decomp(u8 *input, u32 ilen, u8 *output, u32 olen)
{
	struct {
		struct nlattr nla;
		int val;
	} nla_gzip_param = {
		.nla = {
		.nla_len = nla_attr_size(sizeof(int)),
		.nla_type = ZLIB_DECOMP_WINDOWBITS,
		},
		.val = -DEF_WBITS,
	};
	struct crypto_pcomp *tfm;
	struct comp_request req;
	u32 decomp_len = 0;
	u32 header_len = 0;
	/*crc32^0xFFFFFFFF: d874bde4: 39 a3 4f 41 */
	u32 gzip_crc32 = 0;
	int res = -1;

	tfm = crypto_alloc_pcomp("zlib", CRYPTO_ALG_TYPE_PCOMPRESS, 0);
	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
			"zlib", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	res = crypto_decompress_setup(tfm, &nla_gzip_param, sizeof(nla_gzip_param));
	if(res < 0) pr_err("crypto_decompress_setup return error %d\n", res);
	res = crypto_decompress_init(tfm);
	if(res < 0) pr_err("crypto_decompress_init return error %d\n", res);

	if(input[0] != 0x1F || input[1] != 0x8B) {
		pr_err("gzip not gzip file!\n");
	}

	req.next_in = input + 10;
	req.avail_in = ilen - 18;
	req.next_out = output;
	req.avail_out = olen;


	res = crypto_decompress_final(tfm, &req);
	if(res < 0) {
			pr_err("crypto_decompress_final return error %d\n", res);
			crypto_free_pcomp(tfm);
			return 0;
	}
	decomp_len = res;
	crypto_free_pcomp(tfm);



	memcpy(&header_len, input + ilen-4, 4);
	if(header_len != decomp_len) {
		pr_err("gzip decomp size error! gzip %d(0x%08x)bytes decomp %dbytes\n", header_len, header_len, decomp_len);
		return -EINVAL;
	}

	gzip_crc32 = crc32(~0, output, decomp_len) ^ 0xFFFFFFFFUL;
	res = memcmp(input + ilen-8, &gzip_crc32, 4);
	if(res) {
		pr_err("gzip decomp crc error! 0x%08x\n", (u32) (uintptr_t)(input + ilen-8));
		return -EINVAL;
	}

	return decomp_len;
}

int sdp_unzip_sw_aes_ctr128_crypt(const void *key, int key_len, const void *iv, const char *clear_text, char *cipher_text, size_t size)
{

	struct scatterlist sg_in[1], sg_out[1];
	struct crypto_blkcipher *tfm = crypto_alloc_blkcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	struct blkcipher_desc desc = {.tfm = tfm, .flags = 0};
	int rc;

	//pr_info("sdp_unzip_sw_aes_ctr128_crypt start\n");

	if(IS_ERR(tfm)) {
		printk("sdp_unzip_sw_aes_ctr128_crypt: cannot allocate cipher ctr(aes)\n");
		rc = PTR_ERR(tfm);
	goto out;
	}

	rc = crypto_blkcipher_setkey(tfm, key, key_len);
	if(rc) {
		printk("sdp_unzip_sw_aes_ctr128_crypt: cannot set key\n");
		goto out;
	}

	memcpy(crypto_blkcipher_crt(tfm)->iv, iv, crypto_blkcipher_ivsize(tfm));
	//print_hex_dump(KERN_INFO, "key: ", DUMP_PREFIX_ADDRESS, 16, 1, key, key_len, true);
	//print_hex_dump(KERN_INFO, "iv: ", DUMP_PREFIX_ADDRESS, 16, 1, crypto_blkcipher_crt(tfm)->iv, crypto_blkcipher_ivsize(tfm), true);
	//print_hex_dump(KERN_INFO, "clear_text: ", DUMP_PREFIX_ADDRESS, 16, 1, clear_text, size, true);

	sg_init_table(sg_in, 1);
	sg_set_buf(sg_in, clear_text, size);
	sg_init_table(sg_out, 1);
	sg_set_buf(sg_out, cipher_text, size);

	rc = crypto_blkcipher_encrypt(&desc, sg_out, sg_in, size);
	crypto_free_blkcipher(tfm);
	if(rc < 0) {
		pr_err("sdp_unzip_sw_aes_ctr128_crypt: encryption failed %d\n", rc);
		goto out;
	}

	//print_hex_dump(KERN_INFO, "cipher_text: ", DUMP_PREFIX_ADDRESS, 16, 1, cipher_text, size, true);

	rc=0;
out:
	return rc;
}

#define SHASH_DESC_ON_STACK(shash, ctx)                           \
	char __##shash##_desc[sizeof(struct shash_desc) +         \
		crypto_shash_descsize(ctx)] CRYPTO_MINALIGN_ATTR; \
	struct shash_desc *shash = (struct shash_desc *)__##shash##_desc

int sdp_unzip_sw_sha256_digest(const u8 *input, u32 ilen, u8 *out_digest)
{
	struct crypto_shash *tfm;
	int res;

	tfm = crypto_alloc_shash("sha256", CRYPTO_ALG_TYPE_SHASH, 0);
	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
			"sha256", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	{
		SHASH_DESC_ON_STACK(shash, tfm);
		shash->tfm = tfm;
		shash->flags = 0x0;

		res = crypto_shash_digest(shash, input, ilen, out_digest);
		if(res < 0) {
			pr_err("crypto_shash_digest return error %d\n", res);
		}
	}
	crypto_free_shash(tfm);
	return res;
}






/* below is sdp unzip private value */
enum sdp_unzip_req_mode {
	MODE_INVAL = 0,
	MODE_DECOMP,
	MODE_DECRYPT_DECOMP,
	MODE_HASH_DECOMP,
	MODE_HASH_DECRYPT_DECOMP,
	MODE_HASH,
	MODE_DECRYPT,
	MODE_HASH_DECRYPT
};

struct sdp_unzip_req {
	u32					request_idx;
	struct completion decomp_done;
	enum sdp_unzip_req_mode mode;

	u32					flags;
	unsigned int		comp_bytes;

	struct scatterlist	isgl[GZIP_NR_PAGE];
	unsigned int		inents;
	unsigned int		ilength;

	struct scatterlist	osgl[GZIP_NR_PAGE];
	unsigned int		onents;
	unsigned int		olength;

	sdp_unzip_cb_t	cb_func;
	void			*cb_arg;

	struct sdp_unzip_desc desc;
};

struct sdp_unzip_t
{
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
	phys_addr_t pLzBuf;
	sdp_unzip_cb_t isrfp;
	void *israrg;

	/* pagesize aligned buffer, unzip read/write 64byte unit */
	dma_addr_t padding_page_phys;

	void *sdp1202buf;
	dma_addr_t sdp1202phybuf;
	struct clk *rst;
	struct clk *clk;

	int sha_result;
	struct sdp_unzip_auth_t *auth;

	struct sdp_unzip_req *cur_uzreq;

	u32				quiet;
	u32				use_clockgating;

	u32				req_idx;
	u32				req_errors;
	u32				req_isize;
	u32				req_icnt;
	dma_addr_t		req_ipaddrs[GZIP_NR_PAGE];
	u32				req_osize;
	u32				req_ocnt;
	dma_addr_t		req_opaddrs[GZIP_NR_PAGE];
	u32				req_flags;


	/* speed */
	unsigned long long	req_start_ns;
	unsigned long long	update_endpointer_ns;
	unsigned long long	req_toral_input_bytes;
	unsigned long long	req_toral_output_bytes;
	unsigned long long	req_toral_process_time_ns;
	unsigned long long	req_toral_overhead_time_ns;

	unsigned long long	isr_total_max_time_ns;
	unsigned long long	isr_callback_max_time_ns;

	/* selftest */
	bool self_is_zlib;
};

static struct sdp_unzip_t *sdp_unzip = NULL;

static inline struct sdp_unzip_req *to_sdp_unzip_req(struct sdp_unzip_desc *uzdesc)
{
	return container_of(uzdesc, struct sdp_unzip_req, desc);
}

static unsigned long long sdp_unzip_get_nsecs(void)
{
#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	return sched_clock();
#endif
}

void sdp_unzip_update_endpointer(void)
{
	sdp_unzip->update_endpointer_ns = sdp_unzip_get_nsecs();

#ifdef CONFIG_ARCH_SDP1202
	/* Start decoding */
	writel(1, sdp_unzip->base + R_GZIP_CMD);
#else
	/* Kick decoder to finish */
	writel(0x40000, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
#endif
}
EXPORT_SYMBOL(sdp_unzip_update_endpointer);

static void sdp_unzip_clk_free(void)
{
#ifdef CONFIG_OF
	if (!IS_ERR_OR_NULL(sdp_unzip->clk)) {
		clk_unprepare(sdp_unzip->clk);
		clk_put(sdp_unzip->clk);
	}
	if (!IS_ERR_OR_NULL(sdp_unzip->rst)) {
		clk_unprepare(sdp_unzip->rst);
		clk_put(sdp_unzip->rst);
	}
#endif
}

static void __maybe_unused sdp_unzip_clockgating(int bOn)
{

	if(!sdp_unzip->rst || !sdp_unzip->clk)
		return;

	if(bOn) {
		clk_enable(sdp_unzip->clk);
		udelay(1);
		clk_enable(sdp_unzip->rst);
		udelay(1);
	} else {
		udelay(1);
		clk_disable(sdp_unzip->rst);
		udelay(1);
		clk_disable(sdp_unzip->clk);
	}
}

static void sdp_unzip_dump(void)
{
	int i;
	u32 reg[0x200/4];

	BUG_ON(!sdp_unzip);

	printk(KERN_ERR "sdp-unzip: -------------DUMP GZIP registers------------\n");
#ifdef CONFIG_SDP_UNZIP_AUTH
	for(i = 0; i < 0x200/4; i++) reg[i] = readl(sdp_unzip->base + (i*4));
	print_hex_dump(KERN_ERR, "sdp-unzip: ", DUMP_PREFIX_OFFSET, 16, 4,
		reg, 0x200, false);
#else
	for(i = 0; i < 0x100/4; i++) reg[i] = readl(sdp_unzip->base + (i*4));
	print_hex_dump(KERN_ERR, "sdp-unzip: ", DUMP_PREFIX_OFFSET, 16, 4,
		reg, 0x100, false);
#endif

	printk(KERN_ERR "sdp-unzip: Version info: %s\n", GZIP_VER_STR);

	printk(KERN_ERR "sdp-unzip: req#%u flags: 0x%08x, format %s, input partial:%s, output partial:%s, MMC OTF:%s\n",
		sdp_unzip->req_idx, sdp_unzip->req_flags,
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_ZLIB_FORMAT?"zlib":"gzip",
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_IN_PAR?"yes":"no",
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_OUT_PAR?"yes":"no",
		reg[R_GZIP_IN_BUF_WRITE_CTRL/4]&0x1?"yes":"no");

	printk(KERN_ERR "sdp-unzip: error code(0x%08x) Buf(%s%s%s) ZLIBDec(%s%s%s) Header(%s%s) HufDec(%s%s%s%s%s) Syntax(%s%s)\n",
		reg[R_GZIP_ERR_CODE/4],
		reg[R_GZIP_ERR_CODE/4]&(0x1<<28)?"Timeout ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<25)?"OutBufFull ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<24)?"InBufEmpty ":"",

		reg[R_GZIP_ERR_CODE/4]&(0x1<<22)?"Length ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<21)?"Byte ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<20)?"NotDelate ":"",

		reg[R_GZIP_ERR_CODE/4]&(0x1<<17)?"CRC32 ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<16)?"ISize ":"",

		reg[R_GZIP_ERR_CODE/4]&(0x1<<12)?"InvalData ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<11)?"LargerThenSWindow ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<10)?"DistanceIsZero ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<9)?"UnderSmallSWindow ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<8)?"NotDistanceValue ":"",

		reg[R_GZIP_ERR_CODE/4]&(0x1<<1)?"NotZLIB ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<0)?"NotGZIP ":"");

	printk(KERN_ERR "sdp-unzip: input : buffer 0x%08x, size: 0x%08x(%2d), pointer: 0x%08x\n",
		(u32)sdp_unzip->req_ipaddrs[0], sdp_unzip->req_isize,
		sdp_unzip->req_icnt, reg[R_GZIP_IN_BUF_ADDR/4]+reg[R_GZIP_IN_BUF_POINTER/4]);

	printk(KERN_ERR "sdp-unzip: output: buffer 0x%08x, size: 0x%08x(%2d), pointer: 0x%08x\n",
		(u32)sdp_unzip->req_opaddrs[0], sdp_unzip->req_osize,
		sdp_unzip->req_ocnt, reg[R_GZIP_OUT_BUF_ADDR/4]+reg[R_GZIP_OUT_BUF_POINTER/4]);

	if(reg[R_GZIP_OUT_BUF_ADDR/4]+reg[R_GZIP_OUT_BUF_POINTER/4] > sdp_unzip->padding_page_phys) {
		printk(KERN_ERR "sdp-unzip: Padding buffer dump at 0x%08llx\n", (u64)sdp_unzip->padding_page_phys);
#ifdef CONFIG_ARM64
		__dma_unmap_area (phys_to_virt(sdp_unzip->padding_page_phys), reg[R_GZIP_OUT_BUF_POINTER/4]&~PAGE_MASK, DMA_FROM_DEVICE);
#else
		dmac_unmap_area (phys_to_virt(sdp_unzip->padding_page_phys), reg[R_GZIP_OUT_BUF_POINTER/4]&~PAGE_MASK, DMA_FROM_DEVICE);
#endif
		print_hex_dump(KERN_INFO, "sdp-unzip: ", DUMP_PREFIX_OFFSET, 16, 4,
			phys_to_virt(sdp_unzip->padding_page_phys), reg[R_GZIP_OUT_BUF_POINTER/4]&~PAGE_MASK, false);
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	if(reg[R_CP_CTRL/4] & V_CP_CRTL_AES_EN) {
		printk(KERN_ERR "sdp-unzip: Enabled AES-CTR using %s\n", reg[R_CP_CTRL/4]&V_CP_CRTL_AES_ROM_KEY?"RomKey":"UserKey");
	}

	if(reg[R_CP_CTRL/4] & V_CP_CRTL_SHA_EN) {
		printk(KERN_ERR "sdp-unzip: Enabled SHA256, MsgSize 0x%08x, %s%s%s\n",
			reg[R_SHA_MSG_SIZE/4],
			reg[R_SHA_AUTH_RESULT/4]&0x13?"Done. ":"Running... ",
			reg[R_SHA_AUTH_RESULT/4]&0x1?"Result Pass ":"",
			reg[R_SHA_AUTH_RESULT/4]&0x2?"Result Fail!! ":"");
	}
#endif

	printk(KERN_ERR "sdp-unzip: --------------------------------------------\n");

	return;
}

static irqreturn_t sdp_unzip_isr(int irq, void* devId)
{
	int i;
	u32 pend;
	int decompressed = readl(sdp_unzip->base + R_GZIP_ISIZE_VALUE_HDL);
	int errcode = 0, dump = 0, is_done = false;
	struct sdp_unzip_req *uzreq = sdp_unzip->cur_uzreq;
	unsigned long long	isr_total_time_ns = 0;
	unsigned long long	isr_callback_time_ns = 0;

	BUG_ON(!uzreq);

	isr_total_time_ns = sdp_unzip_get_nsecs();

	pend = readl(sdp_unzip->base + R_GZIP_IRQ);
	writel(pend, sdp_unzip->base + R_GZIP_IRQ);

	//pr_info("sdp-unzip: req#%u irq pend=0x%08x errorcode=0x%08x\n", sdp_unzip->req_idx, pend, readl(sdp_unzip->base + R_GZIP_ERR_CODE));

	if (!pend) {
		return IRQ_NONE;
	}

	if (pend & V_GZIP_IRQ_DECODE_ERROR)
	{
		errcode = readl(sdp_unzip->base + R_GZIP_ERR_CODE);

#if 0/* not use */
		/* workaround: hash+decrypt mode occur input buffer empty error */
		if((sdp_unzip->req_flags & GZIP_FLAG_AUTH_STANDALONE)
			&& sdp_unzip->auth->aes_ctr_iv && (sdp_unzip->auth->sha256_digest || sdp_unzip->auth->sha256_digest_out)) {
			/* hash+decrypt mode */
			errcode &= ~(0x1<<24);/* clear InputBufferEmpty */
			if(!errcode) {
				pend &= ~V_GZIP_IRQ_DECODE_ERROR;
				goto l_errorcode_exit;
			}
		}
#endif

		dump++;
		if (!sdp_unzip->quiet) {
			pr_err("sdp-unzip: req#%u unzip decode error pend=0x%08x errorcode=0x%08x\n", sdp_unzip->req_idx, pend, errcode);
		}
	}

	if (sdp_unzip->sdp1202buf) {
#ifdef CONFIG_ARM64
		__dma_unmap_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
#else
		dmac_unmap_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
#endif
		for (i = 0; i < sdp_unzip->req_ocnt; ++i) {
			struct page *page = phys_to_page(sdp_unzip->req_opaddrs[i]);
			void *kaddr = kmap_atomic(page);
			memcpy(kaddr, sdp_unzip->sdp1202buf + i * PAGE_SIZE,
				PAGE_SIZE);
			kunmap_atomic(kaddr);
		}
	}

	if (sdp_unzip->auth && readl(sdp_unzip->base + R_CP_CTRL) & 2) {		//use SHA auth case
		unsigned long long timeout = 10 * 1000 * 1000;
		int is_timeout = false;

		timeout += sdp_unzip_get_nsecs();
		while (!(readl(sdp_unzip->base + R_SHA_AUTH_RESULT) & 0x13)) {	//wait for SHA End
			if(sdp_unzip_get_nsecs() > timeout) {
				is_timeout = true;
				dump++;
				if (!sdp_unzip->quiet) {
					pr_err("sdp-unzip: req#%u SHA Auth Done Timeout 10ms pend=0x%08x errorcode=0x%08X\n", sdp_unzip->req_idx, pend, errcode);
				}
				break;
			}
		}

		if(likely(!is_timeout)) {

			if(sdp_unzip->auth->sha256_digest_out) {
				/* copy SHA256 result*/
				sdp_unzip->auth->sha256_digest_out[0] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL7));
				sdp_unzip->auth->sha256_digest_out[1] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL6));
				sdp_unzip->auth->sha256_digest_out[2] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL5));
				sdp_unzip->auth->sha256_digest_out[3] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL4));
				sdp_unzip->auth->sha256_digest_out[4] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL3));
				sdp_unzip->auth->sha256_digest_out[5] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL2));
				sdp_unzip->auth->sha256_digest_out[6] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL1));
				sdp_unzip->auth->sha256_digest_out[7] = be32_to_cpu(readl(sdp_unzip->base + R_SHA_HASH_VAL0));
			}

			if(sdp_unzip->auth->sha256_digest) {
				sdp_unzip->auth->sha256_result = GZIP_HASH_OK;
				if ((readl(sdp_unzip->base + R_SHA_AUTH_RESULT) & 0x3) != 0x1) {
					dump++;
					if (!sdp_unzip->quiet) {
						pr_err("sdp-unzip: req#%u hash missmatch! pend=0x%08x errorcode=0x%08X\n", sdp_unzip->req_idx, pend, errcode);
					}
					sdp_unzip->auth->sha256_result = GZIP_ERR_HASH_MISSMATCH;
				}
			} else if(sdp_unzip->auth->sha256_digest_out) {
				/* not use hash match */
				sdp_unzip->auth->sha256_result = GZIP_HASH_OK;
			}
		} else {
			sdp_unzip->auth->sha256_result = GZIP_ERR_HASH_TIMEOUT;
		}

		if (sdp_unzip->auth && (sdp_unzip->auth->sha256_result != GZIP_HASH_OK))
		{
			dump++;
			if (!sdp_unzip->quiet) {
				pr_err("sdp-unzip: req#%u sha256_result error : %d\n", sdp_unzip->req_idx, (int)sdp_unzip->auth->sha256_result);
			}
		}
	}

	if (dump)
	{
		sdp_unzip->req_errors++;
		if (!sdp_unzip->quiet) {
			sdp_unzip_dump();
		}
	}

	if(errcode || (pend & V_GZIP_IRQ_DECODE_DONE)) {
		is_done = true;
	} else if((sdp_unzip->req_flags & GZIP_FLAG_AUTH_STANDALONE) && !sdp_unzip->auth->aes_ctr_iv) {
		/* only hash mode */
		if(pend & V_GZIP_IRQ_AUTH_DONE) {
			is_done = true;
		}
	}

	if (is_done) {
		u32 isize_value_hdl = readl(sdp_unzip->base + R_GZIP_ISIZE_VALUE_HDL);

#ifndef CONFIG_ARCH_SDP1202
		writel(0, sdp_unzip->base + R_GZIP_CMD);			//Gzip Reset
#endif

		if(sdp_unzip->use_clockgating) {
			sdp_unzip_clockgating(0);
		}

		/* update statatics */
		if(!dump) {
			unsigned long long time_ns = sdp_unzip_get_nsecs();

			sdp_unzip->req_toral_process_time_ns += time_ns - sdp_unzip->req_start_ns;
			if(uzreq->flags&GZIP_FLAG_OTF_MMCREAD) {
				sdp_unzip->req_toral_overhead_time_ns += time_ns - sdp_unzip->update_endpointer_ns;
			} else {
				sdp_unzip->req_toral_overhead_time_ns += time_ns - sdp_unzip->req_start_ns;
			}
			sdp_unzip->req_toral_input_bytes += sdp_unzip->req_isize;
			sdp_unzip->req_toral_output_bytes += isize_value_hdl;
		}

		//pr_err("unzip : Decoding Done..\n");
		isr_callback_time_ns = sdp_unzip_get_nsecs();
		if (sdp_unzip->isrfp) {
			sdp_unzip->isrfp(errcode, decompressed, sdp_unzip->israrg);
		}
		isr_callback_time_ns = sdp_unzip_get_nsecs() - isr_callback_time_ns;
		if(sdp_unzip->isr_callback_max_time_ns < isr_callback_time_ns) {
			sdp_unzip->isr_callback_max_time_ns = isr_callback_time_ns;
		}

		uzreq->desc.decompressed_bytes = decompressed;
		uzreq->desc.errorcode = errcode;
		complete(&uzreq->decomp_done);

		/* below is uzreq is invalid */
		uzreq = NULL;
		sdp_unzip->cur_uzreq = NULL;

		smp_wmb();

		sdp_unzip->req_flags = 0;
		sdp_unzip->auth = NULL;
		sdp_unzip->req_icnt = 0;
		sdp_unzip->req_isize = 0;
		memset(sdp_unzip->req_ipaddrs, 0x0, sizeof(sdp_unzip->req_ipaddrs));
		sdp_unzip->req_ocnt = 0;
		sdp_unzip->req_osize = 0;
		memset(sdp_unzip->req_opaddrs, 0x0, sizeof(sdp_unzip->req_opaddrs));

		isr_total_time_ns = sdp_unzip_get_nsecs() - isr_total_time_ns;
		if(sdp_unzip->isr_total_max_time_ns < isr_total_time_ns) {
			sdp_unzip->isr_total_max_time_ns = isr_total_time_ns;
		}

		if(isr_total_time_ns >= 1000000000UL) {
			if (!sdp_unzip->quiet) {
				pr_err("sdp-unzip: req#%u ISR Time over 1s!! total:%lluns, callback:%lluns\n",
					sdp_unzip->req_idx, isr_total_time_ns, isr_callback_time_ns);
			}
		}

		up(&sdp_unzip->sema);
	}
	return IRQ_HANDLED;
}


struct sdp_unzip_desc* sdp_unzip_alloc_descriptor(u32 flags)
{
	struct sdp_unzip_req *uzreq = NULL;

	uzreq = kzalloc(sizeof(struct sdp_unzip_req), GFP_KERNEL);
	if(IS_ERR_OR_NULL(uzreq)) {
		return NULL;
	}

#ifndef CONFIG_SDP_UNZIP_AUTH
	if(flags & (GZIP_FLAG_ENABLE_AUTH)) {
		dev_err(sdp_unzip->dev, "Not Supported Auth in this Platform!");
		return NULL;
	}
	if(flags & (GZIP_FLAG_ZLIB_FORMAT)) {
		dev_err(sdp_unzip->dev, "Not Supported ZLIB Format in this Platform!");
		return NULL;
	}

#ifndef CONFIG_SDP_UNZIP_AUTH_STANDALONE
	if(flags & (GZIP_FLAG_AUTH_STANDALONE)) {
		dev_err(sdp_unzip->dev, "Not Supported Auth Standalone mode in this Platform!");
		return NULL;
	}
#endif
#endif

	if(!(flags & GZIP_FLAG_ENABLE_AUTH)) {
		if(flags & GZIP_FLAG_AUTH_STANDALONE) {
			dev_err(sdp_unzip->dev, "must be enabled auth, if using auth standalone mode!");
			return NULL;
		}
	}

	uzreq->flags = flags;
	init_completion(&uzreq->decomp_done);

	return &uzreq->desc;
}

void sdp_unzip_free_descriptor(struct sdp_unzip_desc *uzdesc)
{
	struct sdp_unzip_req *uzreq = to_sdp_unzip_req(uzdesc);

	uzreq->desc.request_idx = 0;
	uzreq->flags = 0;

	kfree(uzreq);
}

static int sdp_unzip_decompress_start(
	dma_addr_t *ipages,	int nr_ipages, unsigned int input_bytes,
	dma_addr_t *opages,	int nr_opages, unsigned int output_bytes,
	struct sdp_unzip_auth_t *auth, u32 flags,
	sdp_unzip_cb_t cb, void *arg)
{
	int i;
	u32 value;

	/* for debug */
	bool is_dump = false;
	for(i = 0; i < nr_ipages; i++) {
		if(ipages[i] < ((dma_addr_t)GZIP_ALIGNADDR)) {
			dev_err(sdp_unzip->dev, "req#%u phys input[%02d] [0x%08llx, %d] 0x%xbytes",
				sdp_unzip->req_idx, i, (u64)ipages[i], nr_ipages, input_bytes);
			is_dump = true;
		}
	}
	if(is_dump) {
		struct sdp_unzip_req *uzreq = sdp_unzip->cur_uzreq;

		dump_stack();

		dev_err(sdp_unzip->dev, "req#%u input 0x%xbytes, output 0x%xbytes, flags=0x%x",
			sdp_unzip->req_idx, input_bytes, output_bytes, flags);

		for(i = 0; i < nr_ipages; i++) {
			dev_err(sdp_unzip->dev, "iphys[%02d]=0x%08llx", i, (u64)ipages[i]);
		}
		for(i = 0; i < nr_opages; i++) {
			dev_err(sdp_unzip->dev, "ophys[%02d]=0x%08llx", i, (u64)opages[i]);
		}

		if(uzreq) {
			struct scatterlist *sg = NULL;

			for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
				dev_err(sdp_unzip->dev, "[%02d] in  page=0x%p, virt=0x%p, phys=0x%08llx len=0x%04x",
					i, sg_page(sg), sg_virt(sg), (u64)sg_phys(sg), sg_dma_len(sg));
			}
			for_each_sg(uzreq->osgl, sg, uzreq->onents, i) {
				dev_err(sdp_unzip->dev, "[%02d] out page=0x%p, virt=0x%p, phys=0x%08llx len=0x%04x",
					i, sg_page(sg), sg_virt(sg), (u64)sg_phys(sg), sg_dma_len(sg));
			}
		}
		dev_err(sdp_unzip->dev, "req#%u invalid address! dump done.",
			sdp_unzip->req_idx);
	}

	/* check register */
	if(readl(sdp_unzip->base + R_GZIP_CMD) || readl(sdp_unzip->base + R_GZIP_IRQ) || readl(sdp_unzip->base + R_GZIP_ERR_CODE)) {
		dev_err(sdp_unzip->dev, "unzip register is not clean! maybe another module accessed.\n");
		sdp_unzip_dump();
	}


#ifdef SDP_UNZIP_DEBUG
	dev_info(sdp_unzip->dev, "req#%u page input [0x%08llx, %d] 0x%xbytes, output [0x%08llx, %d] 0x%xbytes",
		sdp_unzip->req_idx, (u64)ipages[0], nr_ipages, input_bytes, opages?(u64)opages[0]:NULL, nr_opages, output_bytes);
#endif

#ifndef GZIP_INPUT_PAR
	if(nr_ipages != 1) {
		dev_err(sdp_unzip->dev, "req#%u Not Supported Input Scatter buffer! input pages# %d", sdp_unzip->req_idx, nr_ipages);
		return -EINVAL;
	}
#endif

	if(!(nr_ipages > 0 && nr_ipages <= GZIP_NR_PAGE)) {
		dev_err(sdp_unzip->dev, "req#%u Invalid Input page number %d", sdp_unzip->req_idx, nr_ipages);
		return -EINVAL;
	}

	if(!(nr_opages > 0 && nr_opages <= GZIP_NR_PAGE)) {
		dev_err(sdp_unzip->dev, "req#%u Invalid Output page number %d", sdp_unzip->req_idx, nr_opages);
		return -EINVAL;
	}

	/* check input align*/
	if(input_bytes&(GZIP_ALIGNSIZE-1)) {
		dev_err(sdp_unzip->dev, "req#%u Invalid Input align bytes 0x%x", sdp_unzip->req_idx, input_bytes);
		return -EINVAL;
	}
	for(i = 0; i < nr_ipages; i++) {
		if(ipages[i]&( (nr_ipages==1?GZIP_ALIGNADDR-1:GZIP_PAGESIZE-1) )) {
			dev_err(sdp_unzip->dev, "req#%u Invalid Input align address[%d] 0x%llx", sdp_unzip->req_idx, i, (u64)ipages[i]);
			return -EINVAL;
		}
	}

	/* check output align*/
	if(output_bytes&(GZIP_ALIGNSIZE-1)) {
		dev_err(sdp_unzip->dev, "req#%u Invalid Output align bytes 0x%x", sdp_unzip->req_idx, output_bytes);
		return -EINVAL;
	}
	for(i = 0; i < nr_opages; i++) {
		if(opages[i]&( (nr_opages==1?GZIP_ALIGNADDR-1:GZIP_PAGESIZE-1) )) {
			dev_err(sdp_unzip->dev, "req#%u Invalid Output align address[%d] 0x%llx", sdp_unzip->req_idx, i, (u64)opages[i]);
			return -EINVAL;
		}
	}




	/* Set members */
	sdp_unzip->req_isize = input_bytes;
	sdp_unzip->req_osize = output_bytes;
	sdp_unzip->req_flags = flags;
	sdp_unzip->isrfp = cb;
	sdp_unzip->israrg = arg;

	sdp_unzip->req_start_ns = sdp_unzip_get_nsecs();

	if(sdp_unzip->use_clockgating) {
		sdp_unzip_clockgating(1);
	}

	/* WTF is below? In case of not SDP1202 we do
	 * write, then read, then write again.
	 * Is it really expected? */

#ifndef CONFIG_ARCH_SDP1202
	/* Gzip reset */
	writel(0, sdp_unzip->base + R_GZIP_CMD);
#endif
	readl(sdp_unzip->base + R_GZIP_CMD);
	/* Gzip reset */
//XXX    	writel(0, sdp_unzip->base + R_GZIP_CMD);



#ifdef CONFIG_SDP_UNZIP_AUTH

#ifdef CONFIG_SDP_UNZIP_AUTH_STANDALONE
	if(flags&GZIP_FLAG_AUTH_STANDALONE) {
		/* decompress module off */
		value = readl(sdp_unzip->base + R_DEC_EN);
		writel(value&~(V_DEC_EN_DECOMP_ENABLE), sdp_unzip->base + R_DEC_EN);

		writel(input_bytes, sdp_unzip->base + R_SHA_MSG_SIZE);//defalut setting. overwrite sha256 setting
	}
#endif

	if ((flags&GZIP_FLAG_ENABLE_AUTH) && auth) {
		u32 cp_ctrl_val = 0;

		sdp_unzip->auth = auth;

		writel(0x0, sdp_unzip->base + R_CP_CTRL);

		if (auth->aes_ctr_iv) {

			if (auth->aes_user_key) {
				writel(cpu_to_be32(auth->aes_user_key[0]), sdp_unzip->base + R_AES_KEY3);
				writel(cpu_to_be32(auth->aes_user_key[1]), sdp_unzip->base + R_AES_KEY2);
				writel(cpu_to_be32(auth->aes_user_key[2]), sdp_unzip->base + R_AES_KEY1);
				writel(cpu_to_be32(auth->aes_user_key[3]), sdp_unzip->base + R_AES_KEY0);
			}
			else {
				cp_ctrl_val |= V_CP_CRTL_AES_ROM_KEY;
				writel(cp_ctrl_val, sdp_unzip->base + R_CP_CTRL);
			}

			writel(cpu_to_be32(auth->aes_ctr_iv[0]), sdp_unzip->base + R_AES_IV3);
			writel(cpu_to_be32(auth->aes_ctr_iv[1]), sdp_unzip->base + R_AES_IV2);
			writel(cpu_to_be32(auth->aes_ctr_iv[2]), sdp_unzip->base + R_AES_IV1);
			writel(cpu_to_be32(auth->aes_ctr_iv[3]), sdp_unzip->base + R_AES_IV0);

			cp_ctrl_val |= V_CP_CRTL_AES_EN;
			writel(cp_ctrl_val, sdp_unzip->base + R_CP_CTRL);
			writel(cp_ctrl_val | V_CP_CRTL_AES_CTR_INIT, sdp_unzip->base + R_CP_CTRL);
		}


		auth->sha256_result = GZIP_HASH_OK;

		if (auth->sha256_digest) {
			auth->sha256_result = GZIP_ERR_HASH_INPROGRESS;

			writel(auth->sha256_length, sdp_unzip->base + R_SHA_MSG_SIZE);

			writel(cpu_to_be32(auth->sha256_digest[0]), sdp_unzip->base + R_SHA_RSA7);
			writel(cpu_to_be32(auth->sha256_digest[1]), sdp_unzip->base + R_SHA_RSA6);
			writel(cpu_to_be32(auth->sha256_digest[2]), sdp_unzip->base + R_SHA_RSA5);
			writel(cpu_to_be32(auth->sha256_digest[3]), sdp_unzip->base + R_SHA_RSA4);
			writel(cpu_to_be32(auth->sha256_digest[4]), sdp_unzip->base + R_SHA_RSA3);
			writel(cpu_to_be32(auth->sha256_digest[5]), sdp_unzip->base + R_SHA_RSA2);
			writel(cpu_to_be32(auth->sha256_digest[6]), sdp_unzip->base + R_SHA_RSA1);
			writel(cpu_to_be32(auth->sha256_digest[7]), sdp_unzip->base + R_SHA_RSA0);

			cp_ctrl_val |= V_CP_CRTL_SHA_EN;
			writel(cp_ctrl_val, sdp_unzip->base + R_CP_CTRL);
			writel(cp_ctrl_val | V_CP_CRTL_SHA_MSG_CLEAR, sdp_unzip->base + R_CP_CTRL);

		} else if(auth->sha256_digest_out) {/* only use SHA256 hw cal value, not use match result */
			auth->sha256_result = GZIP_ERR_HASH_INPROGRESS;

			writel(auth->sha256_length, sdp_unzip->base + R_SHA_MSG_SIZE);

			/* write dummy digest(all zero) */
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA7);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA6);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA5);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA4);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA3);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA2);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA1);
			writel(0xdeadbeef, sdp_unzip->base + R_SHA_RSA0);

			cp_ctrl_val |= V_CP_CRTL_SHA_EN;
			writel(cp_ctrl_val, sdp_unzip->base + R_CP_CTRL);
			writel(cp_ctrl_val | V_CP_CRTL_SHA_MSG_CLEAR, sdp_unzip->base + R_CP_CTRL);
		}
	}
#else
	sdp_unzip->auth = NULL;
#endif




#ifdef GZIP_INPUT_PAR

	sdp_unzip->req_icnt = 0;

	/* Set page phys addr */
	for (i = 0; i < GZIP_NR_PAGE + 1; i++) {
		unsigned int off =
			(i == 0 ? R_GZIP_IN_BUF_ADDR :
			R_GZIP_IN_ADDR_LIST_0);
		unsigned int ind = (i == 0 ? 0 : i - 1);

		if (i < nr_ipages) {
			writel(ipages[i], sdp_unzip->base + off + ind * 4);
			sdp_unzip->req_icnt++;
			sdp_unzip->req_ipaddrs[i] = ipages[i];
		}
		else {
			writel(0xdead0000, sdp_unzip->base + off + ind * 4);
		}
	}
	if(nr_ipages > 1) {
		writel(GZIP_PAGESIZE, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	} else {
		writel(input_bytes, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	}
	/* last buffer padding */
	writel(sdp_unzip->padding_page_phys, sdp_unzip->base + R_GZIP_IN_ADDR_LIST_0 + (4 * (nr_ipages - 1)));
	
#else/*!GZIP_INPUT_PAR*/
	sdp_unzip->req_icnt = 1;
	sdp_unzip->req_ipaddrs[0] = ipages[0];

	/* Set Source */
	writel(ipages[0], sdp_unzip->base + R_GZIP_IN_BUF_ADDR);

	/* Set Src Size */
	if(flags&GZIP_FLAG_AUTH_STANDALONE) {
		writel(TO_ALIGNED(input_bytes, GZIP_ALIGNSIZE), sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	} else {
		writel(TO_ALIGNED(input_bytes, GZIP_ALIGNSIZE)+GZIP_ALIGNSIZE, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	}
#endif/*GZIP_INPUT_PAR*/


	/* Set LZ Buf Address */
	writel(sdp_unzip->pLzBuf, sdp_unzip->base + R_GZIP_LZ_ADDR);

	sdp_unzip->req_ocnt = 0;

	if (sdp_unzip->sdp1202buf) {
		sdp_unzip->sdp1202phybuf = __pa(sdp_unzip->sdp1202buf);
		/* Set phys addr of page */
		for (i = 0; i < nr_opages; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
				R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);

			writel(sdp_unzip->sdp1202phybuf + i * PAGE_SIZE,
				sdp_unzip->base + off + ind * 4);
			sdp_unzip->req_ocnt++;
			sdp_unzip->req_opaddrs[i] = opages[i];
		}
		writel(GZIP_PAGESIZE, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
#ifdef CONFIG_ARM64
		__dma_map_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
#else
		dmac_map_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
#endif
	}
	else {
		/* Set page phys addr */
		for (i = 0; i < GZIP_NR_PAGE + 1; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
				R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);

			if (i < nr_opages) {
				writel(opages[i], sdp_unzip->base + off + ind * 4);
				sdp_unzip->req_ocnt++;
				sdp_unzip->req_opaddrs[i] = opages[i];
			} else {
				writel(0xbeef0000, sdp_unzip->base + off + ind * 4);
			}
		}

		if(nr_opages == 0) {
			writel(0x0, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
		} else {
			if(nr_opages > 1) {
				writel(GZIP_PAGESIZE, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
			} else {
				writel(output_bytes, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
			}
			/* last buffer padding */
			writel(sdp_unzip->padding_page_phys, sdp_unzip->base + R_GZIP_ADDR_LIST1 + (4 * (nr_opages - 1)));
		}
	}

	value = GZIP_WINDOWSIZE << 16;
	value |= V_GZIP_CTL_OUT_PAR | V_GZIP_CTL_ADVMODE;

#ifdef GZIP_INPUT_PAR
	value |= V_GZIP_CTL_IN_PAR;
#endif
	value |= 0x11;

	if (flags&GZIP_FLAG_ZLIB_FORMAT) {
		value |= V_GZIP_CTL_ZLIB_FORMAT;
	}

	/* Set Decoding Control Register */
	writel(value, sdp_unzip->base + R_GZIP_DEC_CTRL);
	/* Set Timeout Value */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_TIMEOUT);

	/* Set IRQ Mask Register */
	if(flags&GZIP_FLAG_AUTH_STANDALONE && !auth->aes_ctr_iv) {
		writel(0x1F, sdp_unzip->base + R_GZIP_IRQ_MASK);/* use AUTH_DONE IRQ */
	} else {
		writel(0xF, sdp_unzip->base + R_GZIP_IRQ_MASK);/* W/A not use AUTH_DONE IRQ */
	}
	/* Set ECO value */
	writel(0x1E00, sdp_unzip->base + R_GZIP_PROC_DELAY);

	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
	if (flags&GZIP_FLAG_OTF_MMCREAD)
		writel((1 << 8) | (3 << 4) | 1,
		sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);

#ifndef CONFIG_ARCH_SDP1202
	/* Start Decoding */
	writel(1, sdp_unzip->base + R_GZIP_CMD);
#endif

	sdp_unzip->req_idx++;
	sdp_unzip->cur_uzreq->request_idx = sdp_unzip->req_idx;
	return 0;
}





int sdp_unzip_decompress_async(struct sdp_unzip_desc *uzdesc, 
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	sdp_unzip_cb_t cb, void *arg,
	bool may_wait)
{
	struct sdp_unzip_req *uzreq = to_sdp_unzip_req(uzdesc);
	dma_addr_t output_page_phys[GZIP_NR_PAGE];
	dma_addr_t input_page_phys[GZIP_NR_PAGE];
	u32 input_nr_pages = 0;
	struct scatterlist *sg = NULL;
	int i, ret = 0;

	if (!sdp_unzip) {
		pr_err("sdp-unzip: Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Check contention */
	if (!may_wait && down_trylock(&sdp_unzip->sema))
		return -EBUSY;
	else if (may_wait)
		down(&sdp_unzip->sema);

	uzreq->inents = sg_nents(input_sgl);
	uzreq->onents = sg_nents(output_sgl);

	if(uzreq->inents > GZIP_NR_PAGE || uzreq->inents > GZIP_NR_PAGE) {
		dev_err(sdp_unzip->dev, "req#%u Invalid sg nents!! input %u, output %u", uzreq->desc.request_idx, uzreq->inents, uzreq->onents);
	}

#ifdef SDP_UNZIP_DEBUG
	dev_info(sdp_unzip->dev, "req#%u sg input %u, output %u", uzreq->desc.request_idx, uzreq->inents, uzreq->onents);
#endif

	/* copy arg sg to desc sg*/
	sg_init_table(uzreq->isgl, uzreq->inents);
	for_each_sg(input_sgl, sg, uzreq->inents, i) {
		sg_set_page(&uzreq->isgl[i], sg_page(sg), sg->length, sg->offset);

		/* copy dma_map_sg info */
		sg_dma_address(&uzreq->isgl[i]) = sg_dma_address(sg);
		sg_dma_len(&uzreq->isgl[i]) = sg_dma_len(sg);
	}


	sg_init_table(uzreq->osgl, uzreq->onents);
	for_each_sg(output_sgl, sg, uzreq->onents, i) {
		sg_set_page(&uzreq->osgl[i], sg_page(sg), sg->length,0x0);
	}

	/* Prepare output pages */
	ret = dma_map_sg(sdp_unzip->dev, uzreq->osgl, uzreq->onents, DMA_FROM_DEVICE);
	if(ret == 0) {
		dev_err(sdp_unzip->dev, "req#%u unable to map output buffer\n", uzreq->desc.request_idx);
		return -EINVAL;
	}

	/* get phys address */
	if(uzreq->isgl->offset) {
		input_page_phys[0] = sg_phys(uzreq->isgl);
		input_nr_pages = 1;
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
			if((input_page_phys[0] + uzreq->ilength) != sg_phys(sg)) {
				dev_err(sdp_unzip->dev, "req#%u Not Supported sglist(not liner)\n", uzreq->desc.request_idx);
				goto _err_free;
			}
			uzreq->ilength += sg->length;
#ifdef SDP_UNZIP_DEBUG
			dev_info(sdp_unzip->dev, "req#%u sg input 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)sg_phys(sg), sg->length);
#endif
		}
#ifdef SDP_UNZIP_DEBUG
		dev_info(sdp_unzip->dev, "req#%u sg input merge 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)input_page_phys[0], uzreq->ilength);
#endif

	} else {
		input_nr_pages = uzreq->inents;
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
			if((sg_phys(sg)&~PAGE_MASK) || ((i < uzreq->inents-1) && (sg->length != PAGE_SIZE))) {
				dev_err(sdp_unzip->dev, "req#%u Not Supported sglist(not page aligned)\n", uzreq->desc.request_idx);
				goto _err_free;
			}
			input_page_phys[i] = sg_phys(sg);
			uzreq->ilength += sg->length;

#ifdef SDP_UNZIP_DEBUG
		dev_info(sdp_unzip->dev, "req#%u sg input 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)sg_phys(sg), sg->length);
#endif
		}
	}
	WARN_ON(uzreq->inents != i);

	for_each_sg(uzreq->osgl, sg, uzreq->onents, i) {
		output_page_phys[i] = sg_dma_address(sg);
		uzreq->olength += sg_dma_len(sg);

#ifdef SDP_UNZIP_DEBUG
	dev_info(sdp_unzip->dev, "req#%u sg output 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)sg_dma_address(sg), sg_dma_len(sg));
#endif
	}
	WARN_ON(uzreq->onents != i);

	sdp_unzip->cur_uzreq = uzreq;

	ret = sdp_unzip_decompress_start(input_page_phys, input_nr_pages, TO_ALIGNED(uzreq->ilength, GZIP_ALIGNSIZE),
		output_page_phys, uzreq->onents, uzreq->olength,
#ifdef CONFIG_SDP_UNZIP_AUTH
		&uzreq->desc.auth,
#else
		NULL,
#endif
		uzreq->flags, cb, arg);

	if(ret) {
		goto _err_free;
	}

	return 0;


_err_free:
		sdp_unzip->cur_uzreq = NULL;
		dma_unmap_sg(sdp_unzip->dev, uzreq->osgl, uzreq->onents, DMA_FROM_DEVICE);
		up(&sdp_unzip->sema);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_async);


int sdp_unzip_decompress_sync(struct sdp_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,	
	bool may_wait)
{
	unsigned int input_nents = 0;
	int ret;

	if (!sdp_unzip) {
		pr_err("sdp-unzip: Engine is not Initialized!\n");
		return -EINVAL;
	}


	/* Prepare input buffer */
	input_nents = sg_nents(input_sgl);

	ret = dma_map_sg(sdp_unzip->dev, input_sgl, input_nents, DMA_TO_DEVICE);
	if(ret == 0) {
		dev_err(sdp_unzip->dev, "unable to map input bufferr\n");
		return -EINVAL;
	}

	/* Start decompression */
	ret = sdp_unzip_decompress_async(uzdesc, input_sgl, output_sgl, NULL, NULL, may_wait);

	if (!ret) {
		/* Kick decompressor to start right now */
		sdp_unzip_update_endpointer();

		/* Wait and drop lock */
		ret = sdp_unzip_decompress_wait(uzdesc);
	}

	dma_unmap_sg(sdp_unzip->dev, input_sgl, input_nents, DMA_TO_DEVICE);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_sync);

#ifdef CONFIG_SDP_UNZIP_AUTH_STANDALONE

int sdp_unzip_standalone_hash_async(struct sdp_unzip_desc *uzdesc, 
	struct scatterlist *input_sgl,
	sdp_unzip_cb_t cb, void *arg,
	bool may_wait)
{
	struct sdp_unzip_req *uzreq = to_sdp_unzip_req(uzdesc);
	dma_addr_t input_page_phys[GZIP_NR_PAGE];
	u32 input_nr_pages = 0;
	struct scatterlist *sg = NULL;
	int i, ret = 0;

	if (!sdp_unzip) {
		pr_err("sdp-unzip: Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Check contention */
	if (!may_wait && down_trylock(&sdp_unzip->sema))
		return -EBUSY;
	else if (may_wait)
		down(&sdp_unzip->sema);

	uzreq->inents = sg_nents(input_sgl);

	if(uzreq->inents > GZIP_NR_PAGE || uzreq->inents > GZIP_NR_PAGE) {
		dev_err(sdp_unzip->dev, "req#%u Invalid sg nents!! input %u", uzreq->desc.request_idx, uzreq->inents);
	}

#ifdef SDP_UNZIP_DEBUG
	dev_info(sdp_unzip->dev, "req#%u hash sg input %u", uzreq->desc.request_idx, uzreq->inents);
#endif

	/* copy arg sg to desc sg*/
	sg_init_table(uzreq->isgl, uzreq->inents);
	for_each_sg(input_sgl, sg, uzreq->inents, i) {
		sg_set_buf(&uzreq->isgl[i], sg_virt(sg), sg->length);

		/* copy dma_map_sg info */
		sg_dma_address(&uzreq->isgl[i]) = sg_dma_address(sg);
		sg_dma_len(&uzreq->isgl[i]) = sg_dma_len(sg);
	}

	/* get phys address */
	if(uzreq->isgl->offset) {
		input_page_phys[0] = sg_phys(uzreq->isgl);
		input_nr_pages = 1;
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
			if((input_page_phys[0] + uzreq->ilength) != sg_phys(sg)) {
				dev_err(sdp_unzip->dev, "req#%u Not Supported sglist(not liner)\n", uzreq->desc.request_idx);
				goto _err_free;
			}
			uzreq->ilength += sg->length;
#ifdef SDP_UNZIP_DEBUG
			dev_info(sdp_unzip->dev, "req#%u sg input 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)sg_phys(sg), sg->length);
#endif
		}
#ifdef SDP_UNZIP_DEBUG
		dev_info(sdp_unzip->dev, "req#%u sg input merge 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)input_page_phys[0], uzreq->ilength);
#endif

	} else {
		input_nr_pages = uzreq->inents;
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
			if((sg_phys(sg)&~PAGE_MASK) || ((i < uzreq->inents-1) && (sg->length != PAGE_SIZE))) {
				dev_err(sdp_unzip->dev, "req#%u Not Supported sglist(not page aligned)\n", uzreq->desc.request_idx);
				goto _err_free;
			}
			input_page_phys[i] = sg_phys(sg);
			uzreq->ilength += sg->length;

#ifdef SDP_UNZIP_DEBUG
		dev_info(sdp_unzip->dev, "req#%u sg input 0x%llx, 0x%x", uzreq->desc.request_idx, (u64)sg_phys(sg), sg->length);
#endif
		}
	}
	WARN_ON(uzreq->inents != i);

	sdp_unzip->cur_uzreq = uzreq;

	ret = sdp_unzip_decompress_start(input_page_phys, input_nr_pages, TO_ALIGNED(uzreq->ilength, GZIP_ALIGNSIZE),
		NULL, 0, 0,
#ifdef CONFIG_SDP_UNZIP_AUTH
		&uzreq->desc.auth,
#else
		NULL,
#endif
		uzreq->flags, cb, arg);

	if(ret) {
		goto _err_free;
	}

	return 0;


_err_free:
		sdp_unzip->cur_uzreq = NULL;
		up(&sdp_unzip->sema);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_standalone_hash_async);
#endif/*CONFIG_SDP_UNZIP_AUTH_STANDALONE*/


/**
* Must be called from the same task which has been started decompression
*/
int sdp_unzip_decompress_wait(struct sdp_unzip_desc *uzdesc)
{
	struct sdp_unzip_req *uzreq = to_sdp_unzip_req(uzdesc);

	BUG_ON(!sdp_unzip);

	wait_for_completion(&uzreq->decomp_done);

	dma_unmap_sg(sdp_unzip->dev, uzreq->osgl, uzreq->onents, DMA_FROM_DEVICE);

	return uzreq->desc.errorcode?-EINVAL:uzreq->desc.decompressed_bytes;
}
EXPORT_SYMBOL(sdp_unzip_decompress_wait);





/*********************************** selftest *********************************/
struct sdp_unzip_test_vector_t {
	const char *name;
	const u8 *input;
	u8 *result;/* output */
	u32 ilen;
	u32 rlen;

	const u8 *expect;
	u32 elen;

	const u8 *aes_ctr_iv;
	const u8 *aes_ctr_key;
	const u8 *sha256_expect;
	u8 *sha256_result;/* output */

	bool is_zlib;
	bool is_auth_standalone;
	u32 fail;
};

enum sdp_unzip_test_fail_type {
	FAIL_DATA_MISSMATCH,
	FAIL_DATA_CORRUPTED,
	FAIL_HASH_MISSMATCH,
	FAIL_HASH_MISSMATCH_HW,
};

static const char sample_key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c, };
static const char sample_iv[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, };


static int _sdp_unzip_run_selftest(struct sdp_unzip_t *uz, struct sdp_unzip_test_vector_t *vec)
{
	struct sdp_unzip_desc *uzdesc = NULL;
	struct scatterlist in_sgl[1];
	struct scatterlist out_sgl[ALIGN(vec->rlen, PAGE_SIZE)/PAGE_SIZE];
	u32 flags = 0;
	int i, ret;
	const u8 FILL = 0x00;
	int req_mode = MODE_INVAL;

	BUG_ON(!uz);
	BUG_ON(!vec);

	dev_info(uz->dev, "%s: test start!(%s%s%s)\n", vec->name,
		vec->is_auth_standalone?"standalone":(vec->is_zlib?"zlib":"gzip"), vec->sha256_result?", sha256":"", vec->aes_ctr_iv?", aes128-ctr":"");

	if(vec->is_auth_standalone) {
		if(!vec->aes_ctr_iv) {
			req_mode = MODE_HASH;
		} else {
			req_mode = MODE_DECRYPT;
		}
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	flags |= GZIP_FLAG_ENABLE_AUTH;


#ifdef CONFIG_SDP_UNZIP_AUTH_STANDALONE
	if(vec->is_auth_standalone) {
		flags |= GZIP_FLAG_AUTH_STANDALONE;
	}
#endif
#endif

	if(vec->is_zlib) {
		flags |= GZIP_FLAG_ZLIB_FORMAT;
	}

	uzdesc = sdp_unzip_alloc_descriptor(flags);
	if(!uzdesc) {
		return -ENOMEM;
	}

	sg_init_one(in_sgl, (u8 *)vec->input, vec->ilen);

	if(req_mode != MODE_HASH) {
		memset(vec->result, FILL, vec->rlen);
		sg_init_table(out_sgl, ARRAY_SIZE(out_sgl));
		for(i = 0; i < ARRAY_SIZE(out_sgl); i++) {
			sg_set_page(&out_sgl[i], virt_to_page(vec->result + (PAGE_SIZE * i)), PAGE_SIZE, 0x0);
		}
	}

#ifdef CONFIG_SDP_UNZIP_AUTH
	uzdesc->auth.sha256_length = vec->ilen;
	uzdesc->auth.sha256_digest = (u32 *)vec->sha256_expect;
	uzdesc->auth.sha256_digest_out = (u32 *)vec->sha256_result;

	uzdesc->auth.aes_ctr_iv = (u32 *)vec->aes_ctr_iv;
	uzdesc->auth.aes_user_key = (u32 *)vec->aes_ctr_key;
#endif


	if(req_mode == MODE_HASH) {
		ret = sdp_unzip_decompress_async(uzdesc, in_sgl, in_sgl, NULL, NULL, true);
		if (!ret) {
			/* Kick decompressor to start right now */
			sdp_unzip_update_endpointer();

			/* Wait and drop lock */
			ret = sdp_unzip_decompress_wait(uzdesc);
		}
	} else {
		ret = sdp_unzip_decompress_sync(uzdesc, in_sgl, out_sgl, true);
	}

	vec->fail = 0;


	if(vec->result && vec->expect) {
		ret = memcmp(vec->result, vec->expect, vec->elen);
		if(ret != 0) {
			vec->fail |= BIT(FAIL_DATA_MISSMATCH);
			dev_err(uz->dev, "%s: data miss match\n", vec->name);
		}

		for(i = ALIGN(vec->elen, GZIP_ALIGNSIZE); i < vec->rlen; i++) {
			if(vec->result[i] != FILL) {
				vec->fail |= BIT(FAIL_DATA_CORRUPTED);
				dev_err(uz->dev, "%s: buffer corrupted at 0x%x\n", vec->name, i);
				break;
			}
		}
	}

	if(vec->sha256_result && vec->sha256_expect) {
		ret = memcmp(vec->sha256_result, vec->sha256_expect, GZIP_SHA256_SIZE);
		if(ret != 0) {
			vec->fail |= BIT(FAIL_HASH_MISSMATCH);
			dev_err(uz->dev, "%s: hash miss match\n", vec->name);
		}

#ifdef CONFIG_SDP_UNZIP_AUTH
		if(uzdesc->auth.sha256_result != GZIP_HASH_OK) {
			vec->fail |= BIT(FAIL_HASH_MISSMATCH_HW);
		}
#endif
	}

	sdp_unzip_free_descriptor(uzdesc);
	dev_info(uz->dev, "%s: test done!\n", vec->name);

	return 0;
}




/********************************* dev attr *********************************/
static ssize_t statistics_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_unzip_t *uz = platform_get_drvdata(pdev);
	ssize_t cur_pos = 0;

	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total requests       : %u\n", uz->req_idx);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total errors         : %u\n", uz->req_errors);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total input bytes    : %llubyte\n", uz->req_toral_input_bytes);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total output bytes   : %llubyte\n", uz->req_toral_output_bytes);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total process time   : %lluns\n", uz->req_toral_process_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total overhead time  : %lluns\n", uz->req_toral_overhead_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "compression ratio    : %llu%%\n", div64_u64(uz->req_toral_input_bytes * 100, uz->req_toral_output_bytes));
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "process speed input  : %lluMB/s\n", div64_u64(uz->req_toral_input_bytes * 1000, uz->req_toral_process_time_ns));
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "process speed output : %lluMB/s\n", div64_u64(uz->req_toral_output_bytes * 1000, uz->req_toral_process_time_ns));
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "overhead speed input : %lluMB/s\n", div64_u64(uz->req_toral_input_bytes * 1000, uz->req_toral_overhead_time_ns));
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "overhead speed output: %lluMB/s\n", div64_u64(uz->req_toral_output_bytes * 1000, uz->req_toral_overhead_time_ns));
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "isr total max time   : %lluns\n", uz->isr_total_max_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "isr callback max time: %lluns\n", uz->isr_callback_max_time_ns);

	return cur_pos;
}
static struct device_attribute dev_attr_statistics = __ATTR_RO(statistics);

static ssize_t selftest_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_unzip_t *uz = platform_get_drvdata(pdev);
	ssize_t cur_pos = 0;
	bool is_zlib = uz->self_is_zlib;

	u8 *plantext = NULL;
	u8 *data_buf = NULL;
	u8 hash_buf[GZIP_SHA256_SIZE];

	u8 *compressed = NULL;
	u8 *encrypted = NULL;
	u8 compressed_hash[GZIP_SHA256_SIZE];
	u8 encrypted_hash[GZIP_SHA256_SIZE];

	u8 *bitmap = NULL;

	const u32 bufsize = 0x3000;

	u32 planlen = 0, complen, complen_align;
	int ret, i;
	int result = -1;
	enum { CASE_DECOMP, CASE_DECRYPT_DECOMP, CASE_HASH_DECOMP, CASE_WHOLE, CASE_HASH, CASE_DECRYPT, CASE_HASH_DECRYPT, CASE_MAX };
	struct sdp_unzip_test_vector_t testvecs[CASE_MAX] = { {NULL,}, };

	plantext = kmalloc(bufsize, GFP_KERNEL);
	data_buf = kmalloc(bufsize, GFP_KERNEL);
	compressed = kmalloc(bufsize, GFP_KERNEL);
	encrypted = kmalloc(bufsize, GFP_KERNEL);
	bitmap = kmalloc(bufsize/8, GFP_KERNEL);

	if(!plantext || !data_buf || !compressed || !encrypted || !bitmap) {
		dev_err(uz->dev, "kmalloc is failed!\n");
		goto l_free;
	};

	get_random_bytes(&planlen, sizeof(planlen));
	planlen = (bufsize-PAGE_SIZE) + (planlen%PAGE_SIZE);

	/* make input data */
	memset(plantext, 0x00, bufsize);
	get_random_bytes(plantext, planlen);

	get_random_bytes(bitmap, bufsize/8);
	/* random inject 0x0 */
	for(i = 0; i < planlen; i++) {
		if(!(bitmap[i/8] & (0x1<<(i%8)))) {
			((u8 *)plantext)[i] = 0x0;
		}
	}

	get_random_bytes(bitmap, bufsize/8);
	/* random inject 0xFF */
	for(i = 0; i < planlen; i++) {
		if(!(bitmap[i/8] & (0x1<<(i%8)))) {
			((u8 *)plantext)[i] = 0xFF;
		}
	}

#if 0
	get_random_bytes(bitmap, bufsize/8);
	/* random inject 0x5A */
	for(i = 0; i < planlen; i++) {
		if(!(bitmap[i/8] & (0x1<<(i%8)))) {
			((u8 *)plantext)[i] = 0x5A;
		}
	}
#endif

	memset(compressed, 0xAA, bufsize);
	memset(encrypted, 0x55, bufsize);

	/* create test data */
	if(is_zlib) {
		complen = sdp_unzip_sw_zlib_comp(plantext, planlen, compressed, bufsize);
	} else {
		complen = sdp_unzip_sw_gzip_comp(plantext, planlen, compressed, bufsize);
	}
	complen_align = ALIGN(complen, GZIP_ALIGNSIZE);
	dev_info(uz->dev, "planlen 0x%x, complen org 0x%x, complen aln 0x%x\n",
		planlen, complen, complen_align);

	sdp_unzip_sw_aes_ctr128_crypt(sample_key, sizeof(sample_key), sample_iv, compressed, encrypted, complen_align);
	sdp_unzip_sw_sha256_digest(compressed, complen_align, compressed_hash);
	sdp_unzip_sw_sha256_digest(encrypted, complen_align, encrypted_hash);


	/* verify using sw aes-ctr */
	memset(data_buf, 0x00, bufsize);
	sdp_unzip_sw_aes_ctr128_crypt(sample_key, sizeof(sample_key), sample_iv, encrypted, data_buf, complen_align);
	ret = memcmp(data_buf, compressed, complen_align);
	if(ret != 0) {
		/* data error */
		dev_err(uz->dev, "encrypted data verify fail\n");
		goto l_result;
	}

	/* verify using sw decomp */
	memset(data_buf, 0x00, bufsize);
	if(is_zlib) {
		sdp_unzip_sw_zlib_decomp(compressed, complen, data_buf, bufsize);
	} else {
		sdp_unzip_sw_gzip_decomp(compressed, complen, data_buf, bufsize);
	}
	ret = memcmp(data_buf, plantext, planlen);
	if(ret != 0) {
		/* data error */
		dev_err(uz->dev, "compressed(%s) data verify fail\n", is_zlib?"zlib":"gzip");
		goto l_result;
	}

	result = 0;



	testvecs[CASE_DECOMP] = (struct sdp_unzip_test_vector_t){
		.name = "decompress",
		.input = compressed,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = plantext,
		.elen = planlen,
		.aes_ctr_iv = NULL,
		.aes_ctr_key = NULL,
		.sha256_expect = NULL,
		.sha256_result = NULL,
		.is_zlib = is_zlib,
	};

#ifdef CONFIG_SDP_UNZIP_AUTH
	testvecs[CASE_DECRYPT_DECOMP] = (struct sdp_unzip_test_vector_t){
		.name = "decrypt->decompress",
		.input = encrypted,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = plantext,
		.elen = planlen,
		.aes_ctr_iv = sample_iv,
		.aes_ctr_key = sample_key,
		.sha256_expect = NULL,
		.sha256_result = NULL,
		.is_zlib = is_zlib,
	};

	testvecs[CASE_HASH_DECOMP] = (struct sdp_unzip_test_vector_t){
		.name = "hash->decompress",
		.input = compressed,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = plantext,
		.elen = planlen,
		.aes_ctr_iv = NULL,
		.aes_ctr_key = NULL,
		.sha256_expect = compressed_hash,
		.sha256_result = hash_buf,
		.is_zlib = is_zlib,
	};

	testvecs[CASE_WHOLE] = (struct sdp_unzip_test_vector_t){
		.name = "hash+decrypt->decompress",
		.input = encrypted,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = plantext,
		.elen = planlen,
		.aes_ctr_iv = sample_iv,
		.aes_ctr_key = sample_key,
		.sha256_expect = encrypted_hash,
		.sha256_result = hash_buf,
		.is_zlib = is_zlib,
	};

#ifdef CONFIG_SDP_UNZIP_AUTH_STANDALONE
	testvecs[CASE_HASH] = (struct sdp_unzip_test_vector_t){
		.name = "hash",
		.input = encrypted,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = NULL,
		.elen = 0,
		.aes_ctr_iv = NULL,
		.aes_ctr_key = NULL,
		.sha256_expect = encrypted_hash,
		.sha256_result = hash_buf,
		.is_auth_standalone = true,
	};

	testvecs[CASE_DECRYPT] = (struct sdp_unzip_test_vector_t){
		.name = "decrypt",
		.input = encrypted,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = compressed,
		.elen = complen_align,
		.aes_ctr_iv = sample_iv,
		.aes_ctr_key = sample_key,
		.sha256_expect = NULL,
		.sha256_result = NULL,
		.is_auth_standalone = true,

	};

	testvecs[CASE_HASH_DECRYPT] = (struct sdp_unzip_test_vector_t){
		.name = "hash+decrypt",
		.input = encrypted,
		.result = data_buf,
		.ilen = complen_align,
		.rlen = bufsize,
		.expect = compressed,
		.elen = complen_align,
		.aes_ctr_iv = sample_iv,
		.aes_ctr_key = sample_key,
		.sha256_expect = encrypted_hash,
		.sha256_result = hash_buf,
		.is_auth_standalone = true,

	};
#endif
#endif


	for(i = 0; i < CASE_MAX; i++) {
		int chunk;

		if(testvecs[i].name == NULL) {
			/* invalid test case. skip. */
			continue;
		}

		_sdp_unzip_run_selftest(uz, &testvecs[i]);

		if(testvecs[i].fail != 0) {
			result = 1;
		}


		/* error dump */
		if(testvecs[i].fail&BIT(FAIL_DATA_MISSMATCH)) {
			for(chunk = 0; (testvecs[i].elen > GZIP_ALIGNSIZE) &&
				chunk < testvecs[i].elen-GZIP_ALIGNSIZE; chunk+=GZIP_ALIGNSIZE)
			{
				if(memcmp(testvecs[i].result + chunk, testvecs[i].expect + chunk, GZIP_ALIGNSIZE) != 0) {
					dev_err(uz->dev, "%s: data missmatch! dump at 0x%x\n", testvecs[i].name, chunk);

					print_hex_dump(KERN_ERR, "expect: ", DUMP_PREFIX_ADDRESS, 16, 1,
						testvecs[i].expect + chunk, GZIP_ALIGNSIZE, true);
					print_hex_dump(KERN_ERR, "result: ", DUMP_PREFIX_ADDRESS, 16, 1,
						testvecs[i].result + chunk, GZIP_ALIGNSIZE, true);
				}
			}

			if(memcmp(testvecs[i].result + chunk, testvecs[i].expect + chunk, testvecs[i].elen-chunk) != 0) {
				dev_err(uz->dev, "%s: data missmatch! dump at 0x%x\n", testvecs[i].name, chunk);

				print_hex_dump(KERN_ERR, "expect: ", DUMP_PREFIX_ADDRESS, 16, 1,
					testvecs[i].expect + chunk, testvecs[i].elen-chunk, true);
				print_hex_dump(KERN_ERR, "result: ", DUMP_PREFIX_ADDRESS, 16, 1,
					testvecs[i].result + chunk, testvecs[i].elen-chunk, true);
			}
		}

		if(testvecs[i].fail&BIT(FAIL_HASH_MISSMATCH)) {
			dev_err(uz->dev, "%s: hash missmatch! dump\n", testvecs[i].name);
			print_hex_dump(KERN_ERR, "expect: ", DUMP_PREFIX_ADDRESS, 16, 1,
				testvecs[i].sha256_expect, GZIP_SHA256_SIZE, true);
			print_hex_dump(KERN_ERR, "result: ", DUMP_PREFIX_ADDRESS, 16, 1,
				testvecs[i].sha256_result, GZIP_SHA256_SIZE, true);
		}

	}

l_result:
#define _PRINT_RESULT(b, p, testvecs) \
	if((testvecs).name) {	\
		(p) += scnprintf((b) + (p), PAGE_SIZE - (p), "%-30s:%s(%d)\n", (testvecs).name,	\
			(testvecs).fail==0?"pass":"fail", (testvecs).fail);	\
	}

	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "result:%s\n",
		result<0?"unable":(result==0?"pass":"fail"));
	if(result >= 0) {
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_DECOMP]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_DECRYPT_DECOMP]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_HASH_DECOMP]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_WHOLE]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_HASH]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_DECRYPT]);
		_PRINT_RESULT(buf, cur_pos, testvecs[CASE_HASH_DECRYPT]);
	}

l_free:
	kfree(plantext);
	kfree(data_buf);
	kfree(compressed);
	kfree(encrypted);
	kfree(bitmap);

	return cur_pos;
}

static ssize_t selftest_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_unzip_t *uz = platform_get_drvdata(pdev);

	if(count == 1) {
		dev_info(uz->dev, "current mode\n");		
		dev_info(uz->dev, "format: %s\n", uz->self_is_zlib?"zlib":"gzip");
	} else if(strncasecmp("gzip", buf, count-1) == 0) {
		uz->self_is_zlib = false;
		dev_info(uz->dev, "set gzip format\n");
	} else if(strncasecmp("zlib", buf, count-1) == 0) {
		uz->self_is_zlib = true;
		dev_info(uz->dev, "set zlib format\n");
	} else {
		dev_info(uz->dev, "invalid input! count %u, \"%s\"\n", count, buf);
	}
	return count;
}

#ifndef __ATTR_RW
#define __ATTR_RW(attr) __ATTR(attr, 0644, attr##_show, attr##_store)
#endif
static struct device_attribute dev_attr_selftest = __ATTR_RW(selftest);




/***************************** platform device ********************************/
static int sdp_unzip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;
	int affinity = 0;
	void *buf;

#ifdef CONFIG_OF
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif

	sdp_unzip = devm_kzalloc(dev, sizeof(struct sdp_unzip_t), GFP_KERNEL);
	if(sdp_unzip == NULL)
	{
		dev_err(dev, "cannot allocate memory!!!\n");
		return -ENOMEM;
	}

	sema_init(&sdp_unzip->sema, 1);
	init_completion(&sdp_unzip->wait);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}

	sdp_unzip->base = devm_ioremap_resource(&pdev->dev, res);

	if (sdp_unzip->base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, sdp_unzip);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}

	ret = request_irq(irq , sdp_unzip_isr, 0, dev_name(dev), sdp_unzip);

	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d\n", irq);
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}

#ifndef CONFIG_OF
	affinity = 1;
#else
	if(!of_property_read_u32(dev->of_node, "irq-affinity", &affinity))
#endif
		if(num_online_cpus() > affinity) {
			irq_set_affinity(irq, cpumask_of(affinity));
		}

	if(soc_is_sdp1202())
	{
		sdp_unzip->sdp1202buf = kmalloc(GZIP_NR_PAGE*GZIP_PAGESIZE, GFP_KERNEL);
		if(sdp_unzip->sdp1202buf == NULL)
		{
			dev_err(dev, "output buffer allocation failed!!!\n");
			devm_kfree(dev, sdp_unzip);
			return -ENOMEM;
		}
	}

#ifdef CONFIG_OF
	sdp_unzip->clk = clk_get(dev, "gzip_clk");
	if (IS_ERR(sdp_unzip->clk)) {
		dev_info(dev, "cannot find gzip_clk: %ld!\n",
			PTR_ERR(sdp_unzip->clk));
		sdp_unzip->clk = NULL;
	}
	else
		clk_prepare(sdp_unzip->clk);
	sdp_unzip->rst = clk_get(dev, "gzip_rst");
	if (IS_ERR(sdp_unzip->rst)) {
		dev_info(dev, "cannot find gzip_rst: %ld!\n",
			PTR_ERR(sdp_unzip->rst));
		sdp_unzip->rst = NULL;
	}
	else
		clk_prepare(sdp_unzip->rst);
#endif

	sdp_unzip->use_clockgating = false;

	buf = kmalloc((UNZIP_LZBUF_SIZE), GFP_KERNEL);/* XXX: lz buffer 32kb */
	if(buf == NULL)	{
		dev_err(dev, "cannot allocate lzbuf memory!!!\n");
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}
	sdp_unzip->pLzBuf = __pa(buf);

	buf = (void *)get_zeroed_page(GFP_KERNEL);
	if(!buf) {
		dev_err(dev, "cannot allocate Padding page memory!!!\n");
		kfree(__va(sdp_unzip->pLzBuf));
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}
	sdp_unzip->padding_page_phys = virt_to_phys(buf);


	g_mempool = sdp_mempool_create(HW_MAX_SIMUL_THR,
						sdp_unzip_mempool_alloc,
						sdp_unzip_mempool_free, dev);
	if (!g_mempool) {
		dev_err(dev, "cannot allocate mempool for sdp_unzip\n");
		free_page((unsigned long)phys_to_virt(sdp_unzip->padding_page_phys));
		kfree(buf);
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, (void *) sdp_unzip);
	sdp_unzip->dev = dev;

	ret = device_create_file(dev, &dev_attr_statistics);
	ret = device_create_file(dev, &dev_attr_selftest);

	dev_info(dev, "registered new unzip device!! (irq %d, affinity %d, windowsize %dkb)\n", irq, affinity, (UNZIP_LZBUF_SIZE)/1024);

	return 0;
}

static int sdp_unzip_remove(struct platform_device *pdev)
{
	sdp_mempool_destroy(g_mempool);
	free_page((unsigned long)phys_to_virt(sdp_unzip->padding_page_phys));
	kfree(__va(sdp_unzip->pLzBuf));
	sdp_unzip_clk_free();
	devm_kfree(&pdev->dev, sdp_unzip);

	return 0;
}

static const struct of_device_id sdp_unzip_dt_match[] = {
	{ .compatible = "samsung,sdp-unzip", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_unzip_dt_match);

static struct platform_driver sdp_unzip_driver = {
	.probe		= sdp_unzip_probe,
	.remove		= sdp_unzip_remove,
	.driver = {
		.name	= "sdp-unzip",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_unzip_dt_match),
	},
};

int sdp_unzip_init(void)
{
	pr_info("%s: Registered driver. ver %s\n", sdp_unzip_driver.driver.name, GZIP_VER_STR);
	return platform_driver_register(&sdp_unzip_driver);
}
subsys_initcall(sdp_unzip_init);

static void __exit sdp_unzip_exit(void)
{
	platform_driver_unregister(&sdp_unzip_driver);
}
module_exit(sdp_unzip_exit);








/******************************** for debugfs *********************************/
#ifndef CONFIG_VD_RELEASE

struct sdp_unzip_file {
	void         *in_buff;
	void         *out_buff;
	size_t        in_sz;
	bool          up_to_date;
	struct page  *dst_pages[32];
};

static void __sdp_unzip_free(struct sdp_unzip_file *sdp_file, unsigned int sz)
{
	unsigned int i;

	vunmap(sdp_file->out_buff);
	for (i = 0; i < sz; i++)
		__free_page(sdp_file->dst_pages[i]);
	free_pages((unsigned long)sdp_file->in_buff,
		   get_order(HW_MAX_IBUFF_SZ));
	kfree(sdp_file);
}

static int sdp_unzip_open(struct inode *inode, struct file *file)
{
	unsigned int i = 0;
	struct sdp_unzip_file *sdp_file;

	sdp_file = kzalloc(sizeof(*sdp_file), GFP_KERNEL);
	if (!sdp_file)
		return -ENOMEM;

	sdp_file->in_buff = (void *)__get_free_pages(GFP_KERNEL,
						get_order(HW_MAX_IBUFF_SZ));
	if (!sdp_file->in_buff)
		goto err;

	for (i = 0; i < ARRAY_SIZE(sdp_file->dst_pages); ++i) {
		void *addr = (void *)__get_free_page(GFP_KERNEL);
		if (!addr)
			goto err;
		sdp_file->dst_pages[i] = virt_to_page(addr);
	}

	sdp_file->out_buff = vmap(sdp_file->dst_pages,
				  ARRAY_SIZE(sdp_file->dst_pages),
				  VM_MAP, PAGE_KERNEL);
	if (!sdp_file->out_buff)
		goto err;

	file->private_data = sdp_file;

	return nonseekable_open(inode, file);

err:
	__sdp_unzip_free(sdp_file, i);
	return -ENOMEM;
}

static int sdp_unzip_close(struct inode *inode, struct file *file)
{
	struct sdp_unzip_file *sdp_file = file->private_data;

	__sdp_unzip_free(sdp_file, ARRAY_SIZE(sdp_file->dst_pages));
	return 0;
}

static ssize_t sdp_unzip_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	ssize_t ret;
	struct sdp_unzip_file *sdp_file = file->private_data;
	size_t max_out = ARRAY_SIZE(sdp_file->dst_pages) * PAGE_SIZE;
	struct sdp_unzip_desc *uzdesc = NULL;
	struct scatterlist isg[1];
	struct sg_table out_sgt;

	if (count < max_out)
		return -EINVAL;
	if (!sdp_file->in_sz)
		return 0;

	uzdesc = sdp_unzip_alloc_descriptor(0x0);
	if(IS_ERR_OR_NULL(uzdesc)) {
		return PTR_ERR(uzdesc);
	}

	sg_init_one(isg, sdp_file->in_buff, ALIGN(sdp_file->in_sz, 8));
	sg_alloc_table_from_pages(&out_sgt, sdp_file->dst_pages, ARRAY_SIZE(sdp_file->dst_pages), 0x0, max_out, GFP_KERNEL);

	/* Yes, right, no synchronization here.
	 * sdp unzip sync has its own synchronization, so we do not care
	 * about corrupted data with simultaneous read/write on the same
	 * fd, we have to test different scenarious and data corruption
	 * is one of them */
	ret = sdp_unzip_decompress_sync(uzdesc, isg, out_sgt.sgl, true);

	sg_free_table(&out_sgt);
	sdp_unzip_free_descriptor(uzdesc);


	sdp_file->in_sz = 0;

	if (ret < 0 || !ret)
		return -EINVAL;

	if (copy_to_user(buf, sdp_file->out_buff, (unsigned long)ret))
		return -EFAULT;

	return ret;
}

static ssize_t sdp_unzip_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct sdp_unzip_file *sdp_file = file->private_data;

	/* Worry about synchronization? Please read comments in read function */

	if (count > HW_MAX_IBUFF_SZ)
		return -EINVAL;
	if (copy_from_user(sdp_file->in_buff, buf, count))
		return -EFAULT;

	sdp_file->in_sz = count;

	return (ssize_t)count;
}

static const struct file_operations sdp_unzip_fops = {
	.owner	= THIS_MODULE,
	.open    = sdp_unzip_open,
	.release = sdp_unzip_close,
	.llseek	 = no_llseek,
	.read	 = sdp_unzip_read,
	.write	 = sdp_unzip_write
};

static struct miscdevice sdp_unzip_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "sdp_unzip",
	.fops	= &sdp_unzip_fops,
	.mode = 0600,
#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	.lab_smk64 = "*",
#endif
};

static struct dentry *sdp_unzip_debugfs;

static void sdp_unzip_debugfs_create(void)
{
	sdp_unzip_debugfs = debugfs_create_dir("sdp_unzip", NULL);
	if (!IS_ERR_OR_NULL(sdp_unzip_debugfs)) {
		debugfs_create_u32("calls",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip->req_idx);
		debugfs_create_u32("errors",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip->req_errors);
		debugfs_create_u64("nsecs",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip->req_toral_overhead_time_ns);
		debugfs_create_bool("quiet",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    sdp_unzip_debugfs, &sdp_unzip->quiet);
		debugfs_create_bool("use_clockgating",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    sdp_unzip_debugfs, &sdp_unzip->use_clockgating);
	}
}

static void sdp_unzip_debugfs_destroy(void)
{
	debugfs_remove_recursive(sdp_unzip_debugfs);
}

static int __init sdp_unzip_file_init(void)
{
	if (!sdp_unzip)
		return -EINVAL;

	sdp_unzip_debugfs_create();
	return misc_register(&sdp_unzip_misc);
}

static void __exit sdp_unzip_file_cleanup(void)
{
	sdp_unzip_debugfs_destroy();
	misc_deregister(&sdp_unzip_misc);
}

module_init(sdp_unzip_file_init);
module_exit(sdp_unzip_file_cleanup);

#endif /* CONFIG_VD_RELEASE */

MODULE_DESCRIPTION("Samsung SDP SoCs HW Decompress driver");
MODULE_LICENSE("GPL v2");

