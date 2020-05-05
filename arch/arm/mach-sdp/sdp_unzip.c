/*********************************************************************************************
 *
 *	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
 *
 *	author : seungjun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 *
 ********************************************************************************************/

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
#define GZIP_VER_STR	"20160212(add irq process time check)"

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
#include <mach/sdp_unzip.h>
#include <linux/of.h>
#include <mach/soc.h>
#include <linux/debugfs.h>
#ifdef CONFIG_SDP_HW_CLOCK
#include <mach/sdp_hwclock.h>
#endif

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
#define GZIP_ALIGNSIZE	64
#define GZIP_CIPHER_ALIGNSIZE	16

#define GZIP_NR_PAGE	33
#define GZIP_PAGESIZE	4096

/* jazz for zlib mode 32kb window size */
#define UNZIP_LZBUF_SIZE 0x8000//32kb

//#define GZIP_INPUT_PAR

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

struct sdp_unzip_t
{
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
	phys_addr_t pLzBuf;
	sdp_unzip_cb_t isrfp;
	void *israrg;
	void *sdp1202buf;
	dma_addr_t sdp1202phybuf;
	void *vbuff;
	struct clk *rst;
	struct clk *clk;

	int decompressed;
	int sha_result;
	struct sdp_unzip_auth_t *auth;
	struct sdp_mempool *mempool;

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
};

static struct sdp_unzip_t *sdp_unzip = NULL;

static unsigned long long sdp_unzip_get_nsecs(void)
{
#ifdef CONFIG_SDP_HW_CLOCK
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

	pr_info("sdp-unzip: -------------DUMP GZIP registers------------\n");
#ifdef CONFIG_SDP_UNZIP_AUTH
	for(i = 0; i < 0x200/4; i++) reg[i] = readl(sdp_unzip->base + (i*4));
	print_hex_dump(KERN_INFO, "sdp-unzip: ", DUMP_PREFIX_OFFSET, 16, 4,
		reg, 0x200, false);
#else
	for(i = 0; i < 0x100/4; i++) reg[i] = readl(sdp_unzip->base + (i*4));
	print_hex_dump(KERN_INFO, "sdp-unzip: ", DUMP_PREFIX_OFFSET, 16, 4,
		reg, 0x100, false);
#endif

	pr_info("sdp-unzip: Version info: %s\n", GZIP_VER_STR);

	pr_info("sdp-unzip: req#%u flags: 0x%08x, format %s, input partial:%s, output partial:%s, MMC OTF:%s\n",
		sdp_unzip->req_idx, sdp_unzip->req_flags,
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_ZLIB_FORMAT?"zlib":"gzip",
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_IN_PAR?"yes":"no",
		reg[R_GZIP_DEC_CTRL/4]&V_GZIP_CTL_OUT_PAR?"yes":"no",
		reg[R_GZIP_IN_BUF_WRITE_CTRL/4]&0x1?"yes":"no");

	pr_info("sdp-unzip: error code(0x%08x) Buf(%s%s%s) ZLIBDec(%s%s%s) Header(%s%s) HufDec(%s%s%s%s%s) Syntax(%s%s)\n",
		reg[R_GZIP_ERR_CODE/4],
		reg[R_GZIP_ERR_CODE/4]&(0x1<<28)?"Timeout ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<25)?"OutBufFull ":"",
		reg[R_GZIP_ERR_CODE/4]&(0x1<<24)?"InBufFull ":"",

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

#ifdef GZIP_INPUT_PAR
	pr_info("sdp-unzip: input : buffer 0x%08x, size: 0x%08x(%2d), pointer: 0x%08x\n", (u32)sdp_unzip->req_ipaddrs[0], sdp_unzip->req_isize,
		sdp_unzip->req_icnt, reg[R_GZIP_IN_BUF_ADDR/4]?reg[R_GZIP_IN_BUF_ADDR/4]-(u32)sdp_unzip->req_ipaddrs[0]+reg[R_GZIP_IN_BUF_POINTER/4]:0x0);
#else
	pr_info("sdp-unzip: input : buffer 0x%08x, size: 0x%08x(%2d), pointer: 0x%08x\n", (u32)sdp_unzip->req_ipaddrs[0], sdp_unzip->req_isize,
		sdp_unzip->req_icnt, reg[R_GZIP_IN_BUF_POINTER/4]);
#endif

	pr_info("sdp-unzip: output: buffer 0x%08x, size: 0x%08x(%2d), pointer: 0x%08x\n", (u32)sdp_unzip->req_opaddrs[0], sdp_unzip->req_osize,
		sdp_unzip->req_ocnt, reg[R_GZIP_OUT_BUF_ADDR/4]?reg[R_GZIP_OUT_BUF_ADDR/4]-(u32)sdp_unzip->req_opaddrs[0]+reg[R_GZIP_OUT_BUF_POINTER/4]:0x0);

#ifdef CONFIG_SDP_UNZIP_AUTH
	if(reg[R_CP_CTRL/4] & V_CP_CRTL_AES_EN) {
		pr_info("sdp-unzip: Enabled AES-CTR using %s\n", reg[R_CP_CTRL/4]&V_CP_CRTL_AES_ROM_KEY?"RomKey":"UserKey");
	}

	if(reg[R_CP_CTRL/4] & V_CP_CRTL_SHA_EN) {
		pr_info("sdp-unzip: Enabled SHA256, MsgSize 0x%08x, %s%s%s\n",
			reg[R_SHA_MSG_SIZE/4],
			reg[R_SHA_AUTH_RESULT/4]&0x13?"Done. ":"Running... ",
			reg[R_SHA_AUTH_RESULT/4]&0x1?"Result Pass ":"",
			reg[R_SHA_AUTH_RESULT/4]&0x2?"Result Fail!! ":"");
	}
#endif

	pr_info("sdp-unzip: --------------------------------------------\n");

	return;
}

static irqreturn_t sdp_unzip_isr(int irq, void* devId)
{
	int i;
	u32 pend;
	int decompressed = readl(sdp_unzip->base + R_GZIP_ISIZE_VALUE_HDL);
	int errcode = 0, dump = 0;
	unsigned long long	isr_total_time_ns = 0;
	unsigned long long	isr_callback_time_ns = 0;

	isr_total_time_ns = sdp_unzip_get_nsecs();

	pend = readl(sdp_unzip->base + R_GZIP_IRQ);
	writel(pend, sdp_unzip->base + R_GZIP_IRQ);

	if (!pend) {
		return IRQ_NONE;
	}

	if (pend & V_GZIP_IRQ_DECODE_ERROR)
	{
		errcode = readl(sdp_unzip->base + R_GZIP_ERR_CODE);
		dump++;
		if (!sdp_unzip->quiet) {
			pr_err("sdp-unzip: req#%u unzip decode error pend=0x%08x errorcode=0x%08x\n", sdp_unzip->req_idx, pend, errcode);
		}
	}

	if (sdp_unzip->sdp1202buf) {
		dmac_unmap_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
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

	if (errcode || (pend & V_GZIP_IRQ_DECODE_DONE)) {
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
			if(sdp_unzip->cur_uzreq->flags&GZIP_FLAG_OTF_MMCREAD) {
				sdp_unzip->req_toral_overhead_time_ns += time_ns - sdp_unzip->update_endpointer_ns;
			} else {
				sdp_unzip->req_toral_overhead_time_ns += time_ns - sdp_unzip->req_start_ns;
			}
			sdp_unzip->req_toral_input_bytes += sdp_unzip->req_isize;
			sdp_unzip->req_toral_output_bytes += sdp_unzip->req_osize;
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

		sdp_unzip->decompressed = errcode ? -abs(errcode) : decompressed;

		smp_wmb();

		complete(&sdp_unzip->wait);
	}

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

	return IRQ_HANDLED;
}

struct sdp_unzip_req *sdp_unzip_alloc(size_t len)
{
	struct sdp_unzip_req *uzreq;

	BUG_ON(!sdp_unzip);

	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);

	uzreq = sdp_mempool_alloc(sdp_unzip->mempool, GFP_NOWAIT);
	if (!uzreq)
		return ERR_PTR(-ENOMEM);

	uzreq->size = len;
	return uzreq;
}

void sdp_unzip_free(struct sdp_unzip_req *uzreq)
{
	BUG_ON(!sdp_unzip);
	if (IS_ERR_OR_NULL(uzreq))
		return;

	uzreq->request_idx = 0;
	uzreq->flags = 0;

#ifdef CONFIG_SDP_UNZIP_AUTH
	memset(&uzreq->auth, 0x0, sizeof(uzreq->auth));
#endif

	sdp_mempool_free(uzreq, sdp_unzip->mempool);
}

static void *sdp_unzip_mempool_alloc(gfp_t gfp_mask, void *pool)
{
	struct sdp_unzip_req *uzreq;
	struct device *dev = pool;

	uzreq = kzalloc(sizeof(*uzreq), GFP_NOWAIT);
	if (!uzreq) {
		dev_err(dev, "failed to allocate sdp_unzip_uzreq\n");
		return NULL;
	}

	uzreq->__sz = HW_MAX_IBUFF_SZ;
	uzreq->vaddr = (void *)__get_free_pages(gfp_mask, get_order(uzreq->__sz));
	if (!uzreq->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}

	uzreq->paddr = dma_map_single(dev, uzreq->vaddr,
		uzreq->__sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, uzreq->paddr)) {
		dev_err(dev, "unable to map input buffer\n");
		goto err;
	}

	return uzreq;

err:
	free_pages((unsigned long)uzreq->vaddr, get_order(uzreq->__sz));
	kfree(uzreq);

	return NULL;
}

static void sdp_unzip_mempool_free(void *mem_, void *pool)
{
	struct device *dev = pool;
	struct sdp_unzip_req *uzreq = mem_;
	if (!uzreq || !uzreq->__sz)
		return;

	dma_unmap_single(dev, uzreq->paddr, uzreq->__sz, DMA_TO_DEVICE);
	free_pages((unsigned long)uzreq->vaddr, get_order(uzreq->__sz));
	kfree(uzreq);
}



static int sdp_unzip_decompress_start(void *vbuff, dma_addr_t pbuff,
	int ilength, dma_addr_t *opages,
	int npages, sdp_unzip_cb_t cb,
	void *arg)
{
	int i;
	u32 value;

#ifdef CONFIG_SDP_UNZIP_AUTH
	sdp_unzip->auth = &sdp_unzip->cur_uzreq->auth;
#else
	sdp_unzip->auth = NULL;
#endif

	/* Set members */
	sdp_unzip->req_isize = ilength;
	sdp_unzip->req_osize = npages * GZIP_PAGESIZE;
	sdp_unzip->req_flags = sdp_unzip->cur_uzreq->flags;
	sdp_unzip->vbuff = vbuff;
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



	if ((sdp_unzip->cur_uzreq->flags&GZIP_FLAG_ENABLE_AUTH) && sdp_unzip->auth) {
		struct sdp_unzip_auth_t *auth = sdp_unzip->auth;
		u32 cp_ctrl_val = 0;

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

			writel(ilength, sdp_unzip->base + R_SHA_MSG_SIZE);

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

			writel(ilength, sdp_unzip->base + R_SHA_MSG_SIZE);

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



#ifdef GZIP_INPUT_PAR

	sdp_unzip->req_icnt = 0;

	/* Set page phys addr */
	for (i = 0; i < GZIP_NR_PAGE; i++) {
		unsigned int off =
			(i == 0 ? R_GZIP_IN_BUF_ADDR :
			R_GZIP_IN_ADDR_LIST_0);
		unsigned int ind = (i == 0 ? 0 : i - 1);

		if ((i * GZIP_PAGESIZE) < ilength + GZIP_ALIGNSIZE) {
			writel(pbuff + (i * GZIP_PAGESIZE), sdp_unzip->base + off + ind * 4);
			sdp_unzip->req_icnt++;
			sdp_unzip->req_ipaddrs[i] = pbuff + (i * GZIP_PAGESIZE);
		}
		else {
			writel(0x0, sdp_unzip->base + off + ind * 4);
		}
	}

	writel(GZIP_PAGESIZE, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
#else
	sdp_unzip->req_icnt = 1;
	sdp_unzip->req_ipaddrs[0] = pbuff;

	/* Set Source */
	writel(pbuff, sdp_unzip->base + R_GZIP_IN_BUF_ADDR);

	/* Set Src Size */
	writel(ALIGN(ilength, GZIP_ALIGNSIZE) + GZIP_ALIGNSIZE, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
#endif


	/* Set LZ Buf Address */
	writel(sdp_unzip->pLzBuf, sdp_unzip->base + R_GZIP_LZ_ADDR);

	sdp_unzip->req_ocnt = 0;

	if (sdp_unzip->sdp1202buf) {
		sdp_unzip->sdp1202phybuf = __pa(sdp_unzip->sdp1202buf);
		/* Set phys addr of page */
		for (i = 0; i < npages; ++i) {
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
		dmac_map_area (sdp_unzip->sdp1202buf, GZIP_NR_PAGE*GZIP_PAGESIZE, DMA_FROM_DEVICE);
	}
	else {
		/* Set page phys addr */
		for (i = 0; i < GZIP_NR_PAGE; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
				R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);

			if (i < npages) {
				writel(opages[i], sdp_unzip->base + off + ind * 4);
				sdp_unzip->req_ocnt++;
				sdp_unzip->req_opaddrs[i] = opages[i];
			} else {
				writel(0x0, sdp_unzip->base + off + ind * 4);
			}
		}
		writel(GZIP_PAGESIZE, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
	}

	value = GZIP_WINDOWSIZE << 16;
	value |= V_GZIP_CTL_OUT_PAR | V_GZIP_CTL_ADVMODE;
#ifdef GZIP_INPUT_PAR
	value |= V_GZIP_CTL_IN_PAR;
#endif
	value |= 0x11;

	if (sdp_unzip->cur_uzreq->flags&GZIP_FLAG_ZLIB_FORMAT) {
		value |= V_GZIP_CTL_ZLIB_FORMAT;
	}

	/* Set Decoding Control Register */
	writel(value, sdp_unzip->base + R_GZIP_DEC_CTRL);
	/* Set Timeout Value */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_TIMEOUT);
	/* Set IRQ Mask Register */
	writel(0xf, sdp_unzip->base + R_GZIP_IRQ_MASK);/* W/A not use AUTH_DONE IRQ */
	/* Set ECO value */
	writel(0x1E00, sdp_unzip->base + R_GZIP_PROC_DELAY);

	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
	if (sdp_unzip->cur_uzreq->flags&GZIP_FLAG_OTF_MMCREAD)
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

int sdp_unzip_decompress_async(struct sdp_unzip_req *uzreq, int off,
	struct page **opages, int npages, sdp_unzip_cb_t cb, void *arg, bool may_wait)
{
	dma_addr_t pages_phys[npages];
	int i, j, err = 0;

	if (!sdp_unzip) {
		pr_err("sdp-unzip: Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Check contention */
	if (!may_wait && down_trylock(&sdp_unzip->sema))
		return -EBUSY;
	else if (may_wait)
		down(&sdp_unzip->sema);

	/* Prepare output pages */
	for (i = 0; i < npages; i++) {
		dma_addr_t phys = dma_map_page(sdp_unzip->dev, opages[i],
			0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(sdp_unzip->dev, phys)) {
			dev_err(sdp_unzip->dev, "unable to map page %u\n", i);
			err = -EINVAL;
			goto err;
		}
		pages_phys[i] = phys;
	}

	sdp_unzip->cur_uzreq = uzreq;

	err = sdp_unzip_decompress_start(uzreq->vaddr + off, uzreq->paddr + off,
		uzreq->size, pages_phys, npages, cb, arg);
	if (err)
		goto err;

	return err;

err:
	up(&sdp_unzip->sema);

	for (j = 0; j < i; j++)
		dma_unmap_page(sdp_unzip->dev, pages_phys[j],
		PAGE_SIZE, DMA_FROM_DEVICE);

	return err;
}
EXPORT_SYMBOL(sdp_unzip_decompress_async);


int sdp_unzip_decompress_sync(struct sdp_unzip_req *uzreq, int off, struct page **opages,
	int npages, bool may_wait)
{
	dma_addr_t ibuff_phys;
	int ret;

	if (!sdp_unzip) {
		pr_err("sdp-unzip: Engine is not Initialized!\n");
		return -EINVAL;
	}


	/* Prepare input buffer */
	ibuff_phys = dma_map_single(sdp_unzip->dev, uzreq->vaddr, uzreq->size, DMA_TO_DEVICE);
	if (dma_mapping_error(sdp_unzip->dev, ibuff_phys)) {
		dev_err(sdp_unzip->dev, "unable to map input bufferr\n");
		return -EINVAL;
	}

	uzreq->paddr = ibuff_phys;

	/* Start decompression */
	ret = sdp_unzip_decompress_async(uzreq, 0, opages, npages,
		NULL, NULL, may_wait);

	if (!ret) {
		/* Kick decompressor to start right now */
		sdp_unzip_update_endpointer();

		/* Wait and drop lock */
		ret = sdp_unzip_decompress_wait(uzreq);
	}

	dma_unmap_single(sdp_unzip->dev, ibuff_phys, uzreq->size, DMA_TO_DEVICE);
	uzreq->paddr = 0;

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_sync);


/**
* Must be called from the same task which has been started decompression
*/
int sdp_unzip_decompress_wait(struct sdp_unzip_req *uzreq)
{
	int i, ret;

	wait_for_completion(&sdp_unzip->wait);
	smp_rmb();
	ret = xchg(&sdp_unzip->decompressed, 0);

	for (i = 0; i < sdp_unzip->req_ocnt; i++)
		dma_unmap_page(sdp_unzip->dev, sdp_unzip->req_opaddrs[i],
		PAGE_SIZE, DMA_FROM_DEVICE);

	sdp_unzip->req_flags = 0;
	sdp_unzip->auth = NULL;
	sdp_unzip->req_icnt = 0;
	sdp_unzip->req_isize = 0;
	memset(sdp_unzip->req_ipaddrs, 0x0, sizeof(sdp_unzip->req_ipaddrs));
	sdp_unzip->req_ocnt = 0;
	sdp_unzip->req_osize = 0;
	memset(sdp_unzip->req_opaddrs, 0x0, sizeof(sdp_unzip->req_opaddrs));
	sdp_unzip->cur_uzreq = NULL;

	up(&sdp_unzip->sema);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_wait);


/***************** dev attr *****************/
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

	sdp_unzip->base = devm_request_and_ioremap(&pdev->dev, res);

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
		dev_err(dev, "cannot find gzip_clk: %ld!\n",
			PTR_ERR(sdp_unzip->clk));
		sdp_unzip->clk = NULL;
	}
	else
		clk_prepare(sdp_unzip->clk);
	sdp_unzip->rst = clk_get(dev, "gzip_rst");
	if (IS_ERR(sdp_unzip->rst)) {
		dev_err(dev, "cannot find gzip_rst: %ld!\n",
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

	sdp_unzip->mempool = sdp_mempool_create(HW_MAX_SIMUL_THR,
						sdp_unzip_mempool_alloc,
						sdp_unzip_mempool_free, dev);
	if (!sdp_unzip->mempool) {
		dev_err(dev, "cannot allocate mempool for sdp_unzip\n");
		kfree(buf);
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, (void *) sdp_unzip);
	sdp_unzip->dev = dev;

	ret = device_create_file(dev, &dev_attr_statistics);

	dev_info(dev, "registered new unzip device!! (irq %d, affinity %d, clkgating %s, lzbuf size %dkb)\n",
		irq, affinity, sdp_unzip->use_clockgating?"on":"off", (UNZIP_LZBUF_SIZE)/1024);

	return 0;
}

static int sdp_unzip_remove(struct platform_device *pdev)
{
	sdp_mempool_destroy(sdp_unzip->mempool);
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








/*************** for debugfs ******************/
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
	struct sdp_unzip_req local_uzreq;

	if (count < max_out)
		return -EINVAL;
	if (!sdp_file->in_sz)
		return 0;

	memset(&local_uzreq, 0x0, sizeof(local_uzreq));
	local_uzreq = (struct sdp_unzip_req){
		.vaddr = sdp_file->in_buff,
		.paddr = 0,
		.size = ALIGN(sdp_file->in_sz, 8),
	};

	/* Yes, right, no synchronization here.
	 * sdp unzip sync has its own synchronization, so we do not care
	 * about corrupted data with simultaneous read/write on the same
	 * fd, we have to test different scenarious and data corruption
	 * is one of them */
	ret = sdp_unzip_decompress_sync(&local_uzreq, 0,
					sdp_file->dst_pages,
					ARRAY_SIZE(sdp_file->dst_pages),
					true);
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

