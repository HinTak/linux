
/****************************************************************************
*
* nvt_ungzipc.c (Novateck Soc unzip device driver for Samsung Platform 4.1)
*
*	author : jianhao.su@novatek.com.tw
*
* 2016/04/23, jianhao.su: novatek implementation using Samsung interface.
*
***************************************************************************/
/*      base on*/
/****************************************************************************
*
* nvt_ungzipc.c (Novateck Soc unzip device driver for Samsung Platform)
*
*	author : jianhao.su@novatek.com.tw
*
* 2015/01/8, jianhao.su: novatek impleimplentation using Samsung interface.
*
***************************************************************************/
/*	base on  */
/****************************************************************************
*
*	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
*
*	author : seungjun.heo@samsung.com
*
* 2014/03/6, roman.pen: sync/async decompression and refactoring
*
***************************************************************************/

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
/*#include <asm/uaccess.h>*/
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <linux/highmem.h>
#include <linux/mmc/core.h>
#include <linux/mempool.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <mach/nvt_ungzip.h>
#include <linux/buffer_head.h>
#include "timelog.h"
#include <linux/blkdev.h>
#include <linux/console.h>

#ifdef CONFIG_NVT_UNZIP_AUTH
#include <mach/nvt_auth.h>
#endif

#ifdef CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#elif defined(CONFIG_NVT_CA53_HW_CLOCK)
#include <mach/nvt_hwclock_ca53.h>
#endif

#ifdef CONFIG_NVT_UNZIP_FIFO
#include "atf.h"
#endif

#if defined(CONFIG_SEPARATE_PRINTK_FROM_USER)
void _sep_printk_start(void);
void _sep_printk_end(void);
#else
#define _sep_printk_start() {}
#define _sep_printk_end() {}
#endif
/*implement define*/
/*#define NVT_UNZIP_INPUT_ISGL_NON_ALIGN*/
/*#define NVT_UNZIP_COHERENT_DATABUF*/
/*#define NVT_UNZIP_DESC_KALLOC*/
#define IGNORE_ALLOC_BUGON
#if !defined(CONFIG_VD_RELEASE)
#define NVT_UNZIP_DBG_LOG_EVENT_TIME
#endif

#define TO_ALIGNED(value, align)	(((value)+(align)-1) & ~((align)-1))
#define GZIP_ALIGNSIZE	64
#define GZIP_ALIGNADDR	8
/**/
#define ATTRI_DESC_EFFECTIVE		0x0001
/* indicates this line of descriptor is effective */

#define ATTRI_DESC_END			0x0002
/* indicates to end of descriptor */
#define ATTRI_DESC_INTERRUPT		0x0004
/* generates DMA Interrupt when the operation of
	the descriptor line is completed */

#define ATTRI_DESC_NOP			0x0000
/* do not execute current line and go to next line */

#define ATTRI_DESC_RSV			0x0010
/* same as Nop */

#define ATTRI_DESC_TRAN			0x0020
/* transfer data of one descriptor line */

#define ATTRI_DESC_LINK			0x0030
/* link to another descriptor */


#define HAL_GZIP_HW_SETTING		(0x00)
#define HAL_GZIP_SRC_ADMA_ADDR		(0x04)
#define HAL_GZIP_SRC_ADMA_ADDR_CURR	(0x08)
#define HAL_GZIP_SRC_ADMA_STATUS	(0x0c)
#define HAL_GZIP_DES_ADMA_ADDR		(0x10)
#define HAL_GZIP_DES_ADMA_ADDR_CURR	(0x14)
#define HAL_GZIP_DES_ADMA_STATUS	(0x18)
#define HAL_GZIP_HW_CONFIG		(0x1c)
#define HAL_GZIP_HW_DEBUG		(0x20)
#define HAL_GZIP_SRC_DCOUNT		(0x24)
#define HAL_GZIP_DES_DCOUNT		(0x28)
#define HAL_GZIP_TIMEOUT		(0x2c)
#define HAL_GZIP_INTERRUPT_EN		(0x30)
#define HAL_GZIP_INTERRUPT		(0x34)

#define HAL_GZIP_SRC_ADMA_ADDR_FINAL	(0x38)
#define HAL_GZIP_DES_ADMA_ADDR_FINAL	(0x3c)
#define HAL_GZIP_IFF_DATA_BYTE_CNT	(0x68)
#define HAL_GZIP_IFF_DEBUG_DATA		(0x6c)
#define HAL_GZIP_DES_ADMA_DBG_SRC_SEL	(0x40)	/*for hw debug only*/
#define HAL_GZIP_DES_ADMA_DBG_VAL	(0x44)	/*for hw debug only*/
#define HAL_GZIP_SRC_ADMA_DBG_SRC_SEL	(0x48) /*for hw debug only*/
#define HAL_GZIP_SRC_ADMA_DBG_VAL	(0X4c)	/*for hw debug only*/
#define HAL_GZIP_IFF_FIFO_DEBUG_CFG	(0x108)
#define HAL_GZIP_IFF_FIFO_DEBUG_DATA	(0x10C)

/* bit mapping of gzip setting register */
#define HAL_GZIP_CLR_DST_CNT		 (BIT(6))
#define HAL_GZIP_CLR_SRC_CNT		 (BIT(5))
#define HAL_GZIP_HW_RESET		 0x00000010UL
#define HAL_GZIP_HW_RESET		 0x00000010UL
#define HAL_GZIP_SRC_ADMA_START		 0x00000001UL
#define HAL_GZIP_SRC_ADMA_CONTINUE	 0x00000002UL
#define HAL_GZIP_DES_ADMA_START		 0x00000004UL
#define HAL_GZIP_DES_ADMA_CONTINUE	 0x00000008UL

/*#define HAL_GZIP_HW_DEBUG                      (HAL_GZIP_REG_BASE + 0x20)*/
#define HAL_GZIP_IFF_INPUT_RDY		(BIT(4))
#define HAL_GZIP_CRC_OK			(BIT(3))
#define HAL_GZIP_FINIAL_BLOCK		(BIT(2))
#define HAL_GZIP_DST_HAVE_DATA		(BIT(1))
#define HAL_GZIP_SRC_HAVE_DATA          (BIT(0))

/*#define HAL_GZIP_HW_CONFIG                     (HAL_GZIP_REG_BASE + 0x1c)*/
#define HAL_GZIP_IFF_CAP		(BIT(15))	/*read onely)*/
#define HAL_GZIP_IFF_INPUT_SELECT	(BIT(14))	/*0 card, 1 aes*/
#define HAL_GZIP_IFF_INPUT_AES		(BIT(14))	/*0 card, 1 aes*/
#define HAL_GZIP_DEBUG_MODE_EN		(BIT(13))
#define HAL_GZIP_IFF_MODE_EN		(BIT(12))
#define DUMMY_READ_AXI_BUS_ID		(3)
/*for read, write, dummy read, there is a id for each of they*/
#define HAL_GZIP_ADMA_STOP_DUMMY_READ   (BIT(11))
#define HAL_GZIP_DUMMY_READ_AXI_BUS_ID  (BIT(6)) /*4 bits*/
#define HAL_GZIP_DUMMY_READ_AXI_BUS_ID_BITSHIFT	(6)
#define HAL_GZIP_ADMA_CRC		(BIT(5))
#define HAL_GZIP_DST_ADMA_ENDIAN	(BIT(4))
/*0 : little endian 1:big endian*/

#define HAL_GZIP_SRC_ADMA_ENDIAN	(BIT(3))
/*0 : little endian 1:big endian*/

#define HAL_GZIP_BUS_SYNC               (BIT(2))
#define HAL_GZIP_RFC_1951               (BIT(1))
#define HAL_GZIP_BYPASS                 (BIT(0))

/* bit mapping of gzip interrupt register */
#define HAL_GZIP_COMMAND_CONFLICT	(BIT(21))
#define HAL_GZIP_DUMMY_READ_STOP	(BIT(20))
#define HAL_GZIP_INVAILD_DST_DSC        (BIT(19))
#define HAL_GZIP_INVAILD_SRC_DSC        (BIT(18))
#define HAL_GZIP_HEADER_RFC1951         (BIT(17))
#define HAL_GZIP_ERR_FILE_ABORTED       (BIT(16))
#define HAL_GZIP_ERR_TABLE_GEN          (BIT(15))
#define HAL_GZIP_ERR_HEADER             (BIT(14))
#define HAL_GZIP_ERR_EOF                (BIT(13))
#define HAL_GZIP_ERR_MEMORY             (BIT(12))
#define HAL_GZIP_ERR_CODE               (BIT(11))
#define HAL_GZIP_ERR_DISTANCE           (BIT(10))
#define HAL_GZIP_ERR_SIZE               (BIT(9))
#define HAL_GZIP_ERR_CRC                (BIT(8))
#define HAL_GZIP_RESERVED               (BIT(7))
#define HAL_GZIP_ERR_TIMEOUT            (BIT(6))
#define HAL_GZIP_DST_ADMA_DATA_INSIDE   (BIT(5))
#define HAL_GZIP_DST_ADMA_STOP          (BIT(4))
#define HAL_GZIP_DST_ADMA_EOF           (BIT(3))
#define HAL_GZIP_SRC_ADMA_DATA_INSIDE   (BIT(2))
#define HAL_GZIP_SRC_ADMA_STOP          (BIT(1))
#define HAL_GZIP_SRC_ADMA_EOF           (BIT(0))

#define ERROR_BTIS							\
(HAL_GZIP_ERR_FILE_ABORTED|HAL_GZIP_ERR_TABLE_GEN|HAL_GZIP_ERR_HEADER	\
|HAL_GZIP_ERR_EOF|HAL_GZIP_ERR_MEMORY|HAL_GZIP_ERR_CODE|		\
HAL_GZIP_ERR_DISTANCE|HAL_GZIP_ERR_SIZE|HAL_GZIP_ERR_CRC|		\
HAL_GZIP_ERR_TIMEOUT|HAL_GZIP_INVAILD_SRC_DSC|HAL_GZIP_INVAILD_DST_DSC)


#define CONTROL_BITS					\
(HAL_GZIP_DST_ADMA_STOP|HAL_GZIP_DST_ADMA_EOF		\
|HAL_GZIP_SRC_ADMA_STOP|HAL_GZIP_SRC_ADMA_EOF)

#define CONTROL_BITS_DST		\
(HAL_GZIP_DST_ADMA_STOP|HAL_GZIP_DST_ADMA_EOF|HAL_GZIP_SRC_ADMA_STOP)


/*#define HAL_GZIP_IFF_FIFO_DEBUG_CFG	( 0x108)*/
#define HAL_GZIP_IFF_DEBUG_EN		(BIT(8))
#define HAL_GZIP_IFF_DEBUG_EN_OFFSET	(8)
#define HAL_GZIP_IFF_DEBUG_EN_LEN	(1)
#define HAL_GZIP_IFF_DBG_RADDR_OFFSET	(16)
#define HAL_GZIP_IFF_DBG_RADDR_LEN	(21-16+1)
#define HAL_GZIP_IFF_DBG_SEL_OFFSET	(0)
#define HAL_GZIP_IFF_DBG_SEL_LEN	(4-0+1)

/*debug mode*/
#define HAL_GZIP_DEBUG_LOW_WORD		(2)
#define HAL_GZIP_DEBUG_HIGH_WORD	(3)
#define DMA_MAX_LENGTH ((1<<16)-1)


#define FDBG(...) printk(__VA_ARGS__)
#define TDBG(...) printk(__VA_ARGS__)
#define LDBG() /*printk("%s :%d\n", __func__, __LINE__)	*/
/*#define DBG_DESCRIPTOR*/
#define TRACE_FLOW 0
#define TRACE(...) \
	do {						\
		if (TRACE_FLOW)				\
			printk(__VA_ARGS__);		\
	} while (0)

#define TRACE_ENTRY() \
	do {								\
		if (TRACE_FLOW)						\
			dev_err(nvt_unzip->dev,				\
				"###nvtunzip hal: %s %d entry\n",	\
				__func__, __LINE__);			\
	} while (0)

#define TRACE_EXIT() \
	do {								\
		if (TRACE_FLOW)						\
			dev_err(nvt_unzip->dev,				\
				"###nvtunzip hal: %s %d exit\n",	\
				__func__, __LINE__);			\
	} while (0)

/*#define NVT_UNZIP_DEBUG*/
/*typedef*/enum gzip_attribute_e {
	EN_DESC_VALID		=	0x01,
	EN_DESC_END		=	0x02,
	EN_DESC_INT		=	0x04,
	EN_DESC_TRANSFER	=	0x20,
	EN_DESC_LINK		=	0x30
} /*gzip_attribute_t*/;

#define EN_DESC_ACT_NOP_MASK	(0x30)
#define EN_DESC_ACT_MASK	(0x30)

#define GZIP_NR_PAGE	33
#define GZIP_PAGESIZE	4096

/*typedef*/ struct gzip_desc_s {
	unsigned short attribute;
	unsigned short len;                     /* decode length */
	unsigned int   addr;            /* buffer address(physical) */
} /*gzip_desc_t*/;

#define MAX_ADMA_NUM (256)
struct st_src_adma {
	struct gzip_desc_s des[MAX_ADMA_NUM];
	dma_addr_t des_phys;
};

struct st_dst_adma {
	struct gzip_desc_s des[MAX_ADMA_NUM];
	dma_addr_t des_phys;
};

#ifdef CONFIG_NVT_UNZIP_FIFO
static int hw_max_simul_thr = HW_MAX_SIMUL_THR;
#endif


static unsigned int unzip_async_count = 0;
static unsigned int unzip_ddma_count = 0;
static unsigned int unzip_isr_count = 0;
static unsigned int unzip_isrclr_count = 0;
static unsigned int unzip_wait_count = 0;

struct nvt_unzip_debug_t {
	unsigned int last_intr_status;
	void *input_descriptor_list;
	void *output_descriptor_list;
	int last_input;
	unsigned int input_size;
	unsigned int output_size;
	int data_input_cnt;
	int data_output_cnt;
	struct page **output_pages;
	int output_pages_num;
	void *input_buf_adr;
	long long ts_wait_done;
	long long ts_free_des;
};

static struct nvt_unzip_debug_t nvt_unzip_dbg = {
	.last_intr_status = 0,
	.input_descriptor_list = NULL,
	.output_descriptor_list = NULL,
	.input_size = 0,
	.data_input_cnt = 0,
	.data_output_cnt = 0,
	.last_input = 0,
	.output_pages = NULL,
	.output_pages_num = 0,
	.input_buf_adr = NULL,
	.ts_wait_done = 0,
	.ts_free_des = 0,	/*this step run before free buff*/
};

/* below is nvt unzip private value */
struct nvt_unzip_req {
	u32					request_idx;
	struct completion decomp_done;

	u32					flags;
	unsigned int		comp_bytes;

	struct scatterlist	isgl[GZIP_NR_PAGE];
	unsigned int		inents;
	unsigned int		ilength;

	struct scatterlist	osgl[GZIP_NR_PAGE];
	unsigned int		onents;
	unsigned int		olength;

	nvt_unzip_cb_t	cb_func;
	void			*cb_arg;

	struct nvt_unzip_desc desc;
};

struct nvt_unzip_t {
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
	nvt_unzip_cb_t isrfp;       /*isr callback*/
	void *israrg;		/*isr callbak*/
	/*void *nvt1202buf;*/
	/*dma_addr_t nvt1202phybuf;*/
	dma_addr_t opages[MAX_ADMA_NUM];
	void *vbuff;
	/*struct clk *rst;	//for clk gating*/
	/*struct clk *clk;	//for clk gating*/
	u32 isize;
	u32  opages_cnt;
	int decompressed;
	struct nvt_unzip_buf *input_buf[HW_MAX_SIMUL_THR];
	spinlock_t input_buf_lock;

	int input_buf_status[HW_MAX_SIMUL_THR];
	#define BUF_FREE 0
	#define BUF_BUSY 1
	struct st_src_adma *data_input;
	int data_input_cnt;
	struct st_dst_adma *data_output;
	int data_output_cnt;
	int data_input_started;
	int data_output_started;
	int status;
#define HAL_STATUS_STOP (0x1)
	unsigned long long clock_ns;
	struct nvt_unzip_buf *cache_patch_buf;
	struct nvt_unzip_buf *input_cache_patch_buf;
	struct nvt_unzip_buf *src_desc_cache_patch_buf;
	enum hw_iovec_comp_type comp_type;

	/*new in 4.1 */
	struct nvt_unzip_auth_t *auth;
	struct nvt_unzip_req *cur_uzreq;
	u32				req_idx;
	u32				req_errors;
	u32				req_isize;
	u32				req_icnt;
	dma_addr_t		req_ipaddrs[GZIP_NR_PAGE];
	u32				req_osize;
	u32				req_ocnt;
	dma_addr_t		req_opaddrs[GZIP_NR_PAGE];
	u32				req_flags;
	unsigned long long	req_start_ns;
	u32				use_clockgating;
	u32				fifo_enable;
	u32				fast;
};
static struct nvt_unzip_t *nvt_unzip;/*= NULL;*/

static inline struct nvt_unzip_req *to_nvt_unzip_req(
	struct nvt_unzip_desc *uzdesc)
{
	return container_of(uzdesc, struct nvt_unzip_req, desc);
}

enum hw_err {
	NO_ERR    = 0,
	IO_ERR,
	UNZIP_ERR
};

struct hw_req {
	struct kref          kref;
	struct scatterlist   sg[HW_MAX_IBUFF_SG_LEN];
	struct unzip_buf     *buff;
	void                 *private;
	struct request       **reqs;
	struct completion    wait;
	atomic_t             req_cnt;
	struct page          **out_pages;
	unsigned int         out_cnt;
	sector_t             sector_off;
	unsigned int         off;
	unsigned int         compr_len;
	unsigned int         to_read_b;
	int                  result;
	enum hw_err          err;
	int                  fast;
	int                  preinited;
	enum hw_iovec_comp_type comp_type;
	struct req_hw      *rq_hw;
};

#ifdef CONFIG_NVT_UNZIP_FIFO
atomic_t unzip_busy = ATOMIC_INIT(0);
#endif

void debug_nvt_hw_req(struct hw_req *req)
{
	if (req == NULL) {
		dev_err(nvt_unzip->dev, "hw_req is null\n");
		return;
	}

	dev_err(nvt_unzip->dev, "hw req: %p\n", req);
	dev_err(nvt_unzip->dev, "sector off 0x%x off 0x%x\n",
		(unsigned int)req->sector_off, (unsigned int)req->off
	);
	dev_err(nvt_unzip->dev,
		"hw_req->buf : 0x%x\n", (unsigned int)req->buff);
}


void debug_nvt_unzip_status(void)
{
	if (nvt_unzip == NULL)
		return;

	dev_err(nvt_unzip->dev,
		"====dump nvt_unzip data struct====\n");
	dev_err(nvt_unzip->dev,
		"vbuf : 0x%x\n", (unsigned int)nvt_unzip->vbuff);
	dev_err(nvt_unzip->dev,
		"isize : 0x%x\n", nvt_unzip->isize);
	dev_err(nvt_unzip->dev,
		"opages_cnt : 0x%x\n", nvt_unzip->opages_cnt);
	dev_err(nvt_unzip->dev,
		"decompressed : 0x%x\n", nvt_unzip->decompressed);
	dev_err(nvt_unzip->dev,
		"input_buf : %p\n", (unsigned int *)nvt_unzip->input_buf);
	dev_err(nvt_unzip->dev,
		"st_src_adma : %p\n", nvt_unzip->data_input);
	dev_err(nvt_unzip->dev,
		"data_input_cnt : 0x%x\n", nvt_unzip->data_input_cnt);
	dev_err(nvt_unzip->dev,
		"st_dst_adma : %p\n", nvt_unzip->data_output);
	dev_err(nvt_unzip->dev,
		"data_output_cnt : 0x%x\n", nvt_unzip->data_output_cnt);
	dev_err(nvt_unzip->dev,
		"data_input_started : 0x%x\n", nvt_unzip->data_input_started);
	dev_err(nvt_unzip->dev,
		"data_output_started : 0x%x\n",
		nvt_unzip->data_output_started);
	dev_err(nvt_unzip->dev,
		"status : 0x%x\n", nvt_unzip->status);
	dev_err(nvt_unzip->dev,
		"clock_ns : 0x%llx\n", nvt_unzip->clock_ns);
	dev_err(nvt_unzip->dev,
		"cache_patch_buf : %p\n", nvt_unzip->cache_patch_buf);
	dev_err(nvt_unzip->dev,
		"input_cache_patch_buf : %p\n",
		nvt_unzip->input_cache_patch_buf);
	dev_err(nvt_unzip->dev,
		"src_desc_cache_patch_buf : %p\n",
		nvt_unzip->src_desc_cache_patch_buf);
	dev_err(nvt_unzip->dev,
		"fifo_enable : %d\n", nvt_unzip->fifo_enable);
	dev_err(nvt_unzip->dev,
		"fast : %d\n", nvt_unzip->fast);
	dev_err(nvt_unzip->dev, "===================================\n");
}

void debug_nvt_unzip_dbg_status(void)
{

	dev_err(nvt_unzip->dev, "====dump nvt_unzip_dbg data struct====\n");
	dev_err(nvt_unzip->dev, "last_intr_status : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.last_intr_status);
	dev_err(nvt_unzip->dev, "input_descriptor_list : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.input_descriptor_list);
	dev_err(nvt_unzip->dev, "output_descriptor_list : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.output_descriptor_list);
	dev_err(nvt_unzip->dev, "input_size : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.input_size);
	dev_err(nvt_unzip->dev, "data_input_cnt : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.data_input_cnt);
	dev_err(nvt_unzip->dev, "data_output_cnt : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.data_output_cnt);
	dev_err(nvt_unzip->dev, "last_input : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.last_input);
	dev_err(nvt_unzip->dev, "output_pages : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.output_pages);
	dev_err(nvt_unzip->dev, "output_pages_num : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.output_pages_num);
	dev_err(nvt_unzip->dev, "input_buf_adr : 0x%x\n",
		(unsigned int)nvt_unzip_dbg.input_buf_adr);
	dev_err(nvt_unzip->dev, "ts_wait_done : 0x%llx\n",
		nvt_unzip_dbg.ts_wait_done);
	dev_err(nvt_unzip->dev, "ts_free_des : 0x%llx\n",
		nvt_unzip_dbg.ts_free_des);
	debug_nvt_hw_req((struct hw_req *)nvt_unzip->israrg);
	dev_err(nvt_unzip->dev, "===================================\n");
}

static unsigned long long nvt_unzip_calls;
static unsigned long long nvt_unzip_errors;
static unsigned long long nvt_unzip_nsecs;
static unsigned int       nvt_unzip_quiet;

static unsigned long long nvt_unzip_get_nsecs(void)
{
#if defined(CONFIG_NVT_HW_CLOCK) || defined(CONFIG_NVT_CA53_HW_CLOCK)
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	struct timespec ts;

	getrawmonotonic(&ts);
	return ts.tv_sec*1000000000+ts.tv_nsec;
#endif
}

/* interface function*/

static void unzip_finish_dst_adma(void);
static void unzip_issue_dst_adma(void);
static void unzip_finish_src_adma(void);
static void unzip_issue_src_adma(void);
static int unzip_prepare_src_adma_pages(dma_addr_t *ipages,
	int nr_ipages, unsigned int input_bytes);
static int unzip_prepare_dst_adma_pages(dma_addr_t *opages,
	int nr_opages, unsigned int output_bytes);


#ifdef CONFIG_NVT_UNZIP_FIFO
int unzip_hal_reset(enum hw_iovec_comp_type, bool may_wait, int from_aes);
#else
int unzip_hal_reset(enum hw_iovec_comp_type, bool may_wait);
#endif
static void nvt_bouncebuf_dst_desc_destroy(void);


void nvt_unzip_update_endpointer(void)
{
    struct nvt_unzip_req *uzreq = nvt_unzip->cur_uzreq;
#ifdef CONFIG_NVT_UNZIP_FIFO
	if (!(nvt_unzip->fifo_enable && nvt_unzip->fast)) {
#endif
	TRACE("%s %d\n", __func__, __LINE__);
	LOG_EVENT(DMA_ISSUE_START);
	unzip_issue_src_adma();
	unzip_issue_dst_adma();
#ifdef CONFIG_NVT_UNZIP_AUTH
    if (uzreq->flags&GZIP_FLAG_ENABLE_AUTH)
        hw_auth_update_endpointer();
#endif
	LOG_EVENT(DMA_ISSUE_DONE);
#ifdef CONFIG_NVT_UNZIP_FIFO
	} else {
		fifo_atf_done();
	}
#endif
}
EXPORT_SYMBOL(nvt_unzip_update_endpointer);

static void __maybe_unused nvt_unzip_clockgating(int bOn)
{
#if 0
	if (!nvt_unzip->rst || !nvt_unzip->clk)
		return;

	if (bOn) {
		clk_prepare_enable(nvt_unzip->clk);
		udelay(1);
		clk_prepare_enable(nvt_unzip->rst);
		udelay(1);
	} else {
		udelay(1);
		clk_disable_unprepare(nvt_unzip->rst);
		udelay(1);
		clk_disable_unprepare(nvt_unzip->clk);
	}
#endif
}

void nvt_unzip_dump_reg(void)
{
#define UNZIP_REG_INFO(REG)					\
	dev_err(nvt_unzip->dev, "	%-30s : 0x%08x\n",	\
		#REG, readl(nvt_unzip->base + REG))

	dev_err(nvt_unzip->dev,
		"-------------DUMP GZIP registers------------\n");
	UNZIP_REG_INFO(HAL_GZIP_HW_SETTING);
	UNZIP_REG_INFO(HAL_GZIP_SRC_ADMA_ADDR);
	UNZIP_REG_INFO(HAL_GZIP_SRC_ADMA_ADDR_CURR);
	UNZIP_REG_INFO(HAL_GZIP_SRC_ADMA_STATUS);
	UNZIP_REG_INFO(HAL_GZIP_DES_ADMA_ADDR);
	UNZIP_REG_INFO(HAL_GZIP_DES_ADMA_ADDR_CURR);
	UNZIP_REG_INFO(HAL_GZIP_DES_ADMA_STATUS);
	UNZIP_REG_INFO(HAL_GZIP_HW_CONFIG);
	UNZIP_REG_INFO(HAL_GZIP_HW_DEBUG);
	UNZIP_REG_INFO(HAL_GZIP_SRC_DCOUNT);
	UNZIP_REG_INFO(HAL_GZIP_DES_DCOUNT);
	UNZIP_REG_INFO(HAL_GZIP_TIMEOUT);
	UNZIP_REG_INFO(HAL_GZIP_INTERRUPT_EN);
	UNZIP_REG_INFO(HAL_GZIP_INTERRUPT);
	{
		unsigned int intr = readl(nvt_unzip->base + HAL_GZIP_INTERRUPT);

		if (intr & ERROR_BTIS) {
#define UNZIP_ERR_INTR(ERR)						\
			do {						\
				if ((intr) & (ERR)) {			\
					dev_err(nvt_unzip->dev,		\
		"		ERROR 0x%010lx %s\n", ERR, #ERR); }	\
			} while (0)

			UNZIP_ERR_INTR(HAL_GZIP_ERR_FILE_ABORTED);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_TABLE_GEN);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_HEADER);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_EOF);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_MEMORY);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_CODE);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_DISTANCE);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_SIZE);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_CRC);
			UNZIP_ERR_INTR(HAL_GZIP_ERR_TIMEOUT);
			UNZIP_ERR_INTR(HAL_GZIP_INVAILD_SRC_DSC);
			UNZIP_ERR_INTR(HAL_GZIP_INVAILD_DST_DSC);
		}
	}

	UNZIP_REG_INFO(HAL_GZIP_SRC_ADMA_ADDR_FINAL);
	UNZIP_REG_INFO(HAL_GZIP_DES_ADMA_ADDR_FINAL);
#ifdef CONFIG_NVT_UNZIP_FIFO	
	UNZIP_REG_INFO(HAL_GZIP_IFF_DATA_BYTE_CNT);
#endif
	dev_err(nvt_unzip->dev,
		"--------------------------------------------\n");
}

/* this is debug function, but need care of the caller situation*/
void nvt_unzip_dump_descriptor(void)
{
	int i = 0;
	struct st_dst_adma  *data_output = nvt_unzip->data_output;
	struct st_src_adma  *data_input = nvt_unzip->data_input;

	dev_err(nvt_unzip->dev,
		"---------NVT UNGZIP DUMP ADMA Descriptor-----------\n");
	unzip_finish_dst_adma();
	dev_err(nvt_unzip->dev,
		" DEST descriptor[%d]:\n", nvt_unzip->opages_cnt);

	for (i = 0; i < nvt_unzip->opages_cnt; i++) {
		dev_err(nvt_unzip->dev,
		"[%03d] attr: 0x%x length: 0x%8x addr :0x%08x vir_add :0x%08x\n"
			, i,
			data_output->des[i].attribute, data_output->des[i].len,
			data_output->des[i].addr, nvt_unzip->opages[i]);
	}

	unzip_finish_src_adma();
	dev_err(nvt_unzip->dev, " SRC descriptor:\n");
	for (i = 0; i < nvt_unzip->data_input_cnt; i++)
		dev_err(nvt_unzip->dev,
				"[%03d] attr: %8x length: 0x%8x addr :0x%08x\n",
				i, data_input->des[i].attribute,
				data_input->des[i].len,
				data_input->des[i].addr);
	dev_err(nvt_unzip->dev,
		"--------------------------------------------\n");

}
EXPORT_SYMBOL(nvt_unzip_dump_descriptor);

static void nvt_unzip_dump_input_buffer(void)
{
	struct nvt_unzip_req *uzreq = nvt_unzip->cur_uzreq;
#ifndef CONFIG_NVT_UNZIP_FIFO
	u32 ibuff = (nvt_unzip->data_input->des[0]).addr;
	unsigned int *buf = (unsigned int *) nvt_unzip->vbuff;
#endif
	struct scatterlist *sg = NULL;
	int i = 0;

	_sep_printk_start();
	console_forbid_async_printk();
	for (i = 0; i < uzreq->inents; i++) {
		sg = &(uzreq->isgl[i]);
		pr_err("Input buffer sg index %d of %d phy=0x%08X vir=0x%p datasize= 0x%x\n",
			i, uzreq->inents, sg_phys(sg), sg_virt(sg), sg->length);
#ifndef NVT_UNZIP_COHERENT_DATABUF
		dma_unmap_single(nvt_unzip->dev, sg_phys(sg),
			 sg->length, DMA_FROM_DEVICE);
#endif
		pr_err("-------------DUMP GZIP Input Buffers--------\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1,
				sg_virt(sg), sg->length, true);
		pr_err("--------------------------------------------\n");
	}
	console_permit_async_printk();
	_sep_printk_end();
}

static void nvt_unzip_dump_without_data(void)
{
	nvt_unzip_dump_reg();
	nvt_unzip_dump_descriptor();
	debug_nvt_unzip_status();
	debug_nvt_unzip_dbg_status();

#ifdef CONFIG_NVT_UNZIP_FIFO
	if (!(nvt_unzip->fifo_enable && nvt_unzip->fast))
		nvt_unzip_dump_input_buffer();
	else
		fifo_regdump();
#endif

}

void nvt_unzip_dump(void)
{
	pr_err("__UNZIP_ERRRR__: async: %u\n", unzip_async_count);
	pr_err("__UNZIP_ERRRR__: ddma: %u\n", unzip_ddma_count);
	pr_err("__UNZIP_ERRRR__: isr: %u\n", unzip_isr_count);
	pr_err("__UNZIP_ERRRR__: isrclr: %u\n", unzip_isrclr_count);
	pr_err("__UNZIP_ERRRR__: wait: %u\n", unzip_wait_count);

	nvt_unzip_dump_reg();
#ifndef CONFIG_NVT_UNZIP_FIFO	
	nvt_unzip_dump_input_buffer();
#endif	
	nvt_unzip_dump_reg();/*dump twice to avoid message skip*/
	nvt_unzip_dump_descriptor();
	debug_nvt_unzip_status();
	debug_nvt_unzip_dbg_status();

#ifdef CONFIG_NVT_UNZIP_FIFO
	if (!(nvt_unzip->fifo_enable && nvt_unzip->fast))
		nvt_unzip_dump_input_buffer();
	else
		fifo_regdump();
#endif

}

#ifdef CONFIG_NVT_UNZIP_FIFO
int nvt_unzip_dma_done(void)
{
	u32 status;

	status = readl(nvt_unzip->base + HAL_GZIP_INTERRUPT);

	status &= ~(HAL_GZIP_ERR_EOF);

	if (status & HAL_GZIP_DST_ADMA_EOF) {
		goto unzip_finished;
	} else {
		if (status & ERROR_BTIS) {
			pr_err("__UNZIP_DMA__\n");
			nvt_unzip_dump();
			goto unzip_finished;
		}
	}

	return 0;
unzip_finished:
	atomic_set(&unzip_busy, 0);
	return 1;
}
#endif

/* isr */

static irqreturn_t nvt_unzip_isr(int irq, void *devId)
{
	u32 value;
	/*int decompressed = 0;//readl(nvt_unzip->base + HAL_GZIP_DES_DCOUNT);*/
	int decompressed = readl(nvt_unzip->base + HAL_GZIP_DES_DCOUNT);
	int err = 0;
	int clr_intr = 0;
	struct nvt_unzip_req *uzreq = nvt_unzip->cur_uzreq;

	TRACE_ENTRY();
	value = readl(nvt_unzip->base + HAL_GZIP_INTERRUPT);

	/*nvt_unzip_dump();//just for test*/
	if (value & ERROR_BTIS) {
		err =  value & (ERROR_BTIS);
		clr_intr  |= err;

	/* because source length is not accurate,  HAL_GZIP_ERR_EOF will happen.
	   and dest length is correct so use use destination length
			to control and ignore the error .
	*/
		if (err & HAL_GZIP_ERR_EOF) {
			err &= ~(HAL_GZIP_ERR_EOF);
			/*clr_intr &= ~HAL_GZIP_ERR_EOF;*/
			/*dev_err(nvt_unzip->dev, "nvt gzip err eof\n");*/
		}
		if (err) {

			nvt_unzip_errors++;
			pr_err("__UNZIP_ERROR__\n");
			nvt_unzip_dump();
#ifndef CONFIG_VD_RELEASE
			BUG_ON(err);
#endif
			if (1 || !nvt_unzip_quiet) {
				pr_err("unzip: unzip error interrupt happened\n");
				nvt_unzip_dump();
#if !defined(CONFIG_VD_RELEASE)
				BUG_ON(err);
#endif
			}
		}
		if (err & HAL_GZIP_ERR_TIMEOUT) {
			/*to make checkpatch happy*/
			pr_err("unzip: unzip TIMEOUT\n");
		}
	}


	if (value & HAL_GZIP_DST_ADMA_EOF) {
		clr_intr |= HAL_GZIP_DST_ADMA_EOF;
		TRACE("NVT UNZIP DECODE DONE\n");
	}

	if (value & HAL_GZIP_SRC_ADMA_EOF) {
		/*no meaning usage now*/
		clr_intr |= HAL_GZIP_SRC_ADMA_EOF;
	}

	if (value & HAL_GZIP_SRC_ADMA_STOP) {
		pr_err("NVT UNZIP: SRC STOP!!!!\n");
		nvt_unzip->status |= HAL_STATUS_STOP;
	}

	if (value & HAL_GZIP_DST_ADMA_STOP) {
		/*check what happen?*/
		pr_err("NVT UNZIP: SHOULD NOT HAPPEN!!!!\n");
	}

	if (value != clr_intr)
		dev_err(nvt_unzip->dev, "NVT UNZIP warning: INTR: %x\n", value);


	/*process interrupt*/

	nvt_unzip_clockgating(0);

	if (nvt_unzip->isrfp)
		nvt_unzip->isrfp(err, decompressed, nvt_unzip->israrg);

	/*for nvt_unzip , one decompress will handle 1 interrupt.
	 so we disable interrupt in isr ....
	to prevent driver state turn to wrong state*/
	writel(0, nvt_unzip->base + HAL_GZIP_INTERRUPT_EN);

	nvt_unzip_calls += 1;
	nvt_unzip_nsecs += nvt_unzip_get_nsecs() - nvt_unzip->clock_ns;

	nvt_unzip->decompressed = err ? -abs(err) : decompressed;
	uzreq->desc.errorcode = err;

	TRACE(" nvt ungzip isr  decompressed:%x err %x intr_de %x\n",
		nvt_unzip->decompressed, err, decompressed);

	/*make everything ready*/
	smp_wmb();

	if (value & HAL_GZIP_ERR_EOF) {
		/*can't found eof?*/
		clr_intr &= ~HAL_GZIP_ERR_EOF;
	}


	if (clr_intr) {
		complete(&nvt_unzip->wait);
		complete(&uzreq->decomp_done);
#ifdef CONFIG_NVT_UNZIP_FIFO
		atomic_set(&unzip_busy, 0);
		unzip_isrclr_count++;
#endif
		nvt_unzip->req_flags = 0;
		nvt_unzip->auth = NULL;
		nvt_unzip->req_icnt = 0;
		nvt_unzip->req_isize = 0;
		/*memset(nvt_unzip->req_ipaddrs,
			 0x0, sizeof(nvt_unzip->req_ipaddrs));*/
		nvt_unzip->req_ocnt = 0;
		nvt_unzip->req_osize = 0;
		nvt_unzip->cur_uzreq = NULL;
		/*memset(nvt_unzip->req_opaddrs,
			0x0, sizeof(nvt_unzip->req_opaddrs));*/
	} else {
		dev_err(nvt_unzip->dev, "NVT UNZIP : going\n");
	}
/*isr_done:*/
	writel(value, nvt_unzip->base + HAL_GZIP_INTERRUPT);
	nvt_unzip_dbg.last_intr_status = value;
	nvt_unzip_dbg.output_size = decompressed;

	unzip_isr_count++;
	TRACE_EXIT();
	return IRQ_HANDLED;
}

struct nvt_unzip_desc *nvt_unzip_alloc_descriptor(u32 flags)
{
	struct nvt_unzip_req *uzreq = NULL;

	uzreq = kzalloc(sizeof(struct nvt_unzip_req), GFP_KERNEL);
	if (IS_ERR_OR_NULL(uzreq))
		return NULL;

#ifndef CONFIG_NVT_UNZIP_AUTH
	if (flags & (GZIP_FLAG_ENABLE_AUTH)) {
		dev_err(nvt_unzip->dev, "Not Supported Auth in this Platform!");
		return NULL;
	}
#endif
#if 0
	if (flags & (GZIP_FLAG_ZLIB_FORMAT)) {
		dev_err(nvt_unzip->dev, "Not Supported ZLIB Format in this Platform!");
		return NULL;
	}
#endif
	uzreq->flags = flags;
	init_completion(&uzreq->decomp_done);

	return &uzreq->desc;
}

void nvt_unzip_free_descriptor(struct nvt_unzip_desc *uzdesc)
{
	struct nvt_unzip_req *uzreq = to_nvt_unzip_req(uzdesc);
#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	struct hw_req *req = uzreq->cb_arg;
	struct nvt_unzip_buf *buf = 0;

	nvt_unzip_dbg.ts_free_des = nvt_unzip_get_nsecs();

	if (req) {
		buf = (struct nvt_unzip_buf *)(req->buff);
		if (buf)
			buf->ts_free_des = nvt_unzip_dbg.ts_free_des;
	} else {
		pr_err("[UNZIP]ERROR: check hw_req = 0\n");
		/*BUG_ON(1)*/ /*this should not happend.*/
	}
#endif
	uzreq->desc.request_idx = 0;
	uzreq->flags = 0;

	kfree(uzreq);

}

void debug_nvt_unzip_inputbuf(int i)
{
	struct nvt_unzip_buf *ibuf = nvt_unzip->input_buf[i];

	dev_err(nvt_unzip->dev, " buf(%p) %d - %d\n",
		ibuf, i, nvt_unzip->input_buf_status[i]);
	dev_err(nvt_unzip->dev,
		" buf %d - time alloc %llx free %llx\n",
		i, ibuf->ts_alloc, ibuf->ts_free);
	dev_err(nvt_unzip->dev,
		" buf %d - req time start %llx done %llx  free_des %llx\n",
		i, ibuf->req_start_ns, ibuf->ts_wait_done, ibuf->ts_free_des);
	dev_err(nvt_unzip->dev,
		" buf %d - auth time start %llx done %llx\n",
		i, ibuf->ts_auth_start, ibuf->ts_auth_done);
}

struct nvt_unzip_buf *nvt_unzip_alloc(size_t len)
{
	struct nvt_unzip_buf *buf = NULL;

	BUG_ON(!nvt_unzip);
	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);
	{
		int i = 0;
		unsigned long flags;
		long long ts = 0;

		spin_lock_irqsave(&nvt_unzip->input_buf_lock, flags);
#ifdef CONFIG_NVT_UNZIP_FIFO		
	    for (i = 0; i < hw_max_simul_thr; i++) {
#else
		for (i = 0; i < HW_MAX_SIMUL_THR; i++) {
#endif
			if (nvt_unzip->input_buf_status[i] == BUF_FREE)
				break;
		}
#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
		ts = nvt_unzip_get_nsecs();
#endif

#ifdef CONFIG_NVT_UNZIP_FIFO		
		if (i == hw_max_simul_thr) {
#else
		if (i == HW_MAX_SIMUL_THR) {
#endif
			int j = 0;

			dev_err(nvt_unzip->dev, "[ERR_DRV_UNZIP] all input buf is not avail!!!\n");
			for (j = 0; j < HW_MAX_SIMUL_THR; j++)
				debug_nvt_unzip_inputbuf(j);
			dev_err(nvt_unzip->dev, "error %d timestamp   %llx\n",
				i, ts);
			nvt_unzip_dump_without_data();
#ifdef IGNORE_ALLOC_BUGON
			spin_unlock_irqrestore(
				&nvt_unzip->input_buf_lock, flags);
			return ERR_PTR(-EBUSY);
#endif
			BUG_ON(1);
		}

		nvt_unzip->input_buf[i]->ts_alloc = ts;
		nvt_unzip->input_buf_status[i] = BUF_BUSY;
		buf = nvt_unzip->input_buf[i];
		spin_unlock_irqrestore(&nvt_unzip->input_buf_lock, flags);
	}
	buf->size = len;
	TRACE("NVT UNZIP :##check alloc # %s addr: %p paddr: %x len 0x%x\n",
		__func__, buf->vaddr, buf->paddr, len);

	return buf;
}

void nvt_unzip_free(struct nvt_unzip_buf *buf)
{
	BUG_ON(!nvt_unzip);
	TRACE("### %s %d\n", __func__, __LINE__);
	{
		int i = 0;
		int j = 0;
		unsigned long flags;
		long long ts = 0;

		spin_lock_irqsave(&nvt_unzip->input_buf_lock, flags);
#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
		ts = nvt_unzip_get_nsecs();
#endif
		for (i = 0; i < HW_MAX_SIMUL_THR; i++) {
			if (buf == nvt_unzip->input_buf[i])
				break;
		}

		if (i == HW_MAX_SIMUL_THR) {
			dev_err(nvt_unzip->dev,
			"[ERR_DRV_UNZIP] input_buffer %d can't free!!!\n", i);
			for (j = 0; j < HW_MAX_SIMUL_THR; j++)
				debug_nvt_unzip_inputbuf(j);
			dev_err(nvt_unzip->dev, "error %d timestamp   %llx\n",
				i, ts);
			nvt_unzip_dump_without_data();
			BUG_ON(1);
		}
		if (nvt_unzip->input_buf_status[i] != BUF_BUSY) {
			dev_err(nvt_unzip->dev,
			"[ERR_DRV_UNZIP] input_buf %d status is wrong!\n", i);
			for (j = 0; j < HW_MAX_SIMUL_THR; j++)
				debug_nvt_unzip_inputbuf(j);
			dev_err(nvt_unzip->dev, "error %d timestamp   %llx\n",
				i, ts);
			nvt_unzip_dump_without_data();
			BUG_ON(1);
		}

		nvt_unzip->input_buf[i]->ts_free = ts;
		nvt_unzip->input_buf_status[i] = BUF_FREE;
		spin_unlock_irqrestore(&nvt_unzip->input_buf_lock, flags);
	}
	if (IS_ERR_OR_NULL(buf))
		return;
	BUG_ON(!nvt_unzip->input_buf);
}

#ifdef NVT_UNZIP_COHERENT_DATABUF
struct nvt_unzip_buf *nvt_inputbuf_alloc(size_t len)
{
	struct nvt_unzip_buf *buf;
	dma_addr_t coh;
	struct device *dev = nvt_unzip->dev;

	BUG_ON(!nvt_unzip);
	spin_lock_init(&nvt_unzip->input_buf_lock);
	buf = kmalloc(sizeof(struct nvt_unzip_buf), GFP_KERNEL);
	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = dma_alloc_coherent(dev, HW_MAX_IBUFF_SZ,
		&buf->paddr, GFP_KERNEL|GFP_NOWAIT);
	coh = buf->paddr;

	if (buf->vaddr == NULL)
		return ERR_PTR(-ENOMEM);

	buf->size = len;
	buf->__sz = HW_MAX_IBUFF_SZ;

	if (!buf->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}
	buf->ts_alloc = 0;
	buf->ts_free = 0;
	TRACE("NVT_UNZIP %s  vaddr %p paddr %x\n",
		__func__, buf->vaddr, buf->paddr);

	return buf;
err:
	dma_free_coherent(dev, HW_MAX_IBUFF_SZ, buf->vaddr, buf->paddr);
	kfree(buf);

	return NULL;
}

static void nvt_inputbuf_free(struct nvt_unzip_buf *buf)
{
	struct device *dev = nvt_unzip->dev;

	if (!buf || !buf->__sz)
		return;
	dma_free_coherent(dev, HW_MAX_IBUFF_SZ, buf->vaddr, buf->paddr);
	kfree(buf);
}
#else
/* memory pool and memory */
struct nvt_unzip_buf *nvt_inputbuf_alloc(size_t len)
{
	struct device *dev = nvt_unzip->dev;
	struct nvt_unzip_buf *buf;

	BUG_ON(!nvt_unzip);
	spin_lock_init(&nvt_unzip->input_buf_lock);
	buf = kmalloc(sizeof(struct nvt_unzip_buf), GFP_KERNEL);
	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = (void *)__get_free_pages(GFP_NOWAIT,
		get_order(HW_MAX_IBUFF_SZ));

	if (buf->vaddr == NULL) {
		/*to make checkpatch happy*/
		return ERR_PTR(-ENOMEM);
	}

	buf->size = len;
	buf->__sz = HW_MAX_IBUFF_SZ;

	if (!buf->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}
	buf->paddr = dma_map_single(dev, buf->vaddr,
				    buf->__sz, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, buf->paddr)) {
		dev_err(dev, "unable to map input buffer\n");
		goto err;
	}

	TRACE("NVT_UNZIP %s  vaddr %p paddr %x page %x phys %x\n",
		__func__, buf->vaddr, buf->paddr,
		(unsigned int)virt_to_page(buf->vaddr),
		page_to_phys(virt_to_page(buf->vaddr)));

	buf->ts_alloc = 0;
	buf->ts_free = 0;

	return buf;
err:
	free_pages((unsigned long)buf->vaddr, get_order(buf->__sz));
	kfree(buf);

	return NULL;
}

static void nvt_inputbuf_free(struct nvt_unzip_buf *buf)
{
	struct device *dev = nvt_unzip->dev;

	if (!buf || !buf->__sz)
		return;
	dma_unmap_single(dev, buf->paddr, buf->__sz, DMA_FROM_DEVICE);
	/*we may skip this*/
	free_pages((unsigned long)buf->vaddr, get_order(buf->__sz));

	kfree(buf);
}
#endif
/* initial/destroy and interface function. */
int nvt_unzip_decompress_start(
	dma_addr_t *ipages,	int nr_ipages, unsigned int input_bytes,
	dma_addr_t *opages,	int nr_opages, unsigned int output_bytes,
	struct nvt_unzip_auth_t *auth, u32 flags,
	nvt_unzip_cb_t cb, void *arg)
{
	int i = 0;
#ifdef NVT_UNZIP_DEBUG
	dev_info(nvt_unzip->dev,
	"req#%u page input [0x%08llx, %d] 0x%xbytes, output [0x%08llx, %d] 0x%xbytes",
	nvt_unzip->req_idx, (u64)ipages[0], nr_ipages, input_bytes,
	(u64)opages[0], nr_opages, output_bytes);
#endif

	if (!(nr_ipages > 0 && nr_ipages <= GZIP_NR_PAGE)) {
		dev_err(nvt_unzip->dev, "req#%u Invalid Input page number %d",
			nvt_unzip->req_idx, nr_ipages);
		return -EINVAL;
	}

	if (!(nr_opages > 0 && nr_opages <= GZIP_NR_PAGE)) {
		dev_err(nvt_unzip->dev, "req#%u Invalid Output page number %d",
			 nvt_unzip->req_idx, nr_opages);
		return -EINVAL;
	}

	/* check input align*/
	if (input_bytes&(GZIP_ALIGNSIZE-1)) {
		dev_err(nvt_unzip->dev, "req#%u Invalid Input align bytes 0x%x",
			nvt_unzip->req_idx, input_bytes);
		return -EINVAL;
	}

#ifndef NVT_UNZIP_INPUT_ISGL_NON_ALIGN
	for (i = 0; i < nr_ipages; i++) {
		if (ipages[i]&
			((nr_ipages == 1 ? GZIP_ALIGNADDR-1:GZIP_PAGESIZE-1))) {
			dev_err(nvt_unzip->dev,
				"req#%u Invalid Input align address[%d] 0x%llx",
				nvt_unzip->req_idx, i, (u64)ipages[i]);
			return -EINVAL;
		}
	}

	/* check output align*/
	if (output_bytes&(GZIP_ALIGNSIZE-1)) {
		dev_err(nvt_unzip->dev,
			"req#%u Invalid Output align bytes 0x%x",
			nvt_unzip->req_idx, output_bytes);
		return -EINVAL;
	}
#endif

	for (i = 0; i < nr_opages; i++) {
		if (opages[i]&
			((nr_opages == 1 ? GZIP_ALIGNADDR-1:GZIP_PAGESIZE-1))) {
			dev_err(nvt_unzip->dev,
				"req#%u Invalid Output align address[%d] 0x%llx",
				nvt_unzip->req_idx, i, (u64)opages[i]);
			return -EINVAL;
		}
	}

	/* Set members */
	nvt_unzip->req_isize = input_bytes;
	nvt_unzip->req_osize = output_bytes;
	nvt_unzip->req_flags = flags;
	nvt_unzip->isrfp = cb;
	nvt_unzip->israrg = arg;
#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	{
		struct nvt_unzip_buf *buf =
			(struct nvt_unzip_buf *)((struct hw_req *)arg)->buff;

		nvt_unzip->req_start_ns = nvt_unzip_get_nsecs();
		if (buf)
			buf->req_start_ns = nvt_unzip->req_start_ns;
	}
#endif
	nvt_unzip_dbg.input_buf_adr = ((struct hw_req *)arg)->buff;

	if (nvt_unzip->use_clockgating)
		nvt_unzip_clockgating(1);

#ifdef CONFIG_NVT_UNZIP_FIFO
	if (nvt_unzip->fifo_enable && nvt_unzip->fast) {
		int offset = nvt_unzip->cur_uzreq->isgl->offset;

#ifdef CONFIG_NVT_UNZIP_AUTH        
        if (flags&GZIP_FLAG_ENABLE_AUTH)
		    nvt_atf_execute(input_bytes /*+ offset*/, to_AES, offset);
        else
#endif
            nvt_atf_execute(input_bytes /*+ offset*/, to_ZIP, offset);
		writel(input_bytes,
		nvt_unzip->base + HAL_GZIP_IFF_DATA_BYTE_CNT);
		/*nvt_atf_execute(input_bytes, to_ZIP,
			nvt_unzip->cur_uzreq->isgl->offset);*/
	} else
#endif
	{
		/*set source descriptor*/
		unzip_prepare_src_adma_pages(ipages, nr_ipages, input_bytes);
	}

	unzip_prepare_dst_adma_pages(opages, nr_opages, output_bytes);

	nvt_unzip->req_icnt = nr_ipages;
	nvt_unzip->req_ocnt++;
	nvt_unzip->req_idx++;

	nvt_unzip->cur_uzreq->request_idx = nvt_unzip->req_idx;
	nvt_unzip->decompressed = 0;
#ifdef CONFIG_NVT_UNZIP_FIFO
	if (nvt_unzip->fifo_enable && nvt_unzip->fast) {
		/*nvt_unzip_update_endpointer();*/
		atomic_inc(&unzip_busy);
		unzip_issue_dst_adma();
	} else {
		atomic_set(&unzip_busy, 0);
	}
#endif

	return 0;
}

int nvt_unzip_decompress_async(struct nvt_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	nvt_unzip_cb_t cb, void *arg,
	bool may_wait)
{

#ifdef CONFIG_NVT_UNZIP_FIFO		
	int   err = 0;
#else
	int   err = unzip_hal_reset(HW_IOVEC_COMP_GZIP, may_wait);
#endif

#if 0
	int i = 0;
	dma_addr_t pages_phys[npages];
#endif
	struct nvt_unzip_req *uzreq = to_nvt_unzip_req(uzdesc);
	u32 input_nr_pages = 0;
	struct scatterlist *sg = NULL;
	int i, ret = 0;

	dma_addr_t output_page_phys[GZIP_NR_PAGE];
	dma_addr_t input_page_phys[GZIP_NR_PAGE];

	TRACE("NVT UNGZIP %s entry\n", __func__);

#ifdef CONFIG_NVT_UNZIP_FIFO
	nvt_unzip->fast = uzdesc->fast;
	if (!nvt_unzip->fast) {
		fifo_atf_done();
	}
	unzip_async_count++;
#endif

//#ifdef CONFIG_NVT_UNZIP_AUTH
#ifdef CONFIG_NVT_UNZIP_FIFO
	if (atomic_read(&unzip_busy)) {
	    pr_err("__UNZIP_ERROR__: use unzip while busy\n");
	    nvt_unzip_dump();
#ifndef CONFIG_VD_RELEASE
	    BUG_ON(1);
#endif
	}
    if (uzreq->flags&GZIP_FLAG_ENABLE_AUTH)
        err = unzip_hal_reset(HW_IOVEC_COMP_GZIP, may_wait, 1);
    else
        err = unzip_hal_reset(HW_IOVEC_COMP_GZIP, may_wait, 0);
#endif
    
	if (err)
		goto err_init;

#if 0
	nvt_unzip_dbg.output_pages = opages;
	nvt_unzip_dbg.output_pages_num = npages;

	/* Prepare output pages */
	LOG_EVENT(OPAGE_DMA_PREPARE_START);
	for (i = 0; i < npages; i++) {
		dma_addr_t phys = dma_map_page(nvt_unzip->dev, opages[i],
				0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(nvt_unzip->dev, phys)) {
			dev_err(nvt_unzip->dev, "unable to map page %u\n", i);
			err = -EINVAL;
			goto err;
		}
		pages_phys[i] = phys;
	}
	LOG_EVENT(OPAGE_DMA_PREPARE_END);
#endif
	nvt_unzip->req_idx++;
	uzreq->desc.request_idx = nvt_unzip->req_idx;

	uzreq->inents = sg_nents(input_sgl);
	uzreq->onents = sg_nents(output_sgl);

	if (uzreq->inents > GZIP_NR_PAGE || uzreq->inents > GZIP_NR_PAGE) {
		dev_err(nvt_unzip->dev, "req#%u Invalid sg nents!! input %u, output %u",
			uzreq->desc.request_idx, uzreq->inents, uzreq->onents);
	}

#ifdef NVT_UNZIP_DEBUG
	dev_info(nvt_unzip->dev, "req#%u sg input %u, output %u",
		uzreq->desc.request_idx, uzreq->inents, uzreq->onents);
#endif

	/* copy arg sg to desc sg*/
	sg_init_table(uzreq->isgl, uzreq->inents);
	for_each_sg(input_sgl, sg, uzreq->inents, i) {
		sg_set_buf(&uzreq->isgl[i], sg_virt(sg), sg->length);

		/* copy dma_map_sg info */
		sg_dma_address(&uzreq->isgl[i]) = sg_dma_address(sg);
		sg_dma_len(&uzreq->isgl[i]) = sg_dma_len(sg);
#ifdef DBG_DESCRIPTOR
		dev_err(nvt_unzip->dev, "[check in sg ] vaddr %p phys : %x len:%x\n",
			sg_virt(sg), sg_phys(sg), sg_dma_len(sg));
#endif
	}
	sg_init_table(uzreq->osgl, uzreq->onents);
	for_each_sg(output_sgl, sg, uzreq->onents, i) {
		sg_set_page(&uzreq->osgl[i], sg_page(sg), sg->length, 0x0);
	}

	/* Prepare output pages */
	ret = dma_map_sg(nvt_unzip->dev, uzreq->osgl,
		uzreq->onents, DMA_FROM_DEVICE);
	if (ret == 0) {
		dev_err(nvt_unzip->dev,
			"req#%u unable to map output buffer\n",
			uzreq->desc.request_idx);
		return -EINVAL;
	}

	/* get phys address */
#ifndef NVT_UNZIP_INPUT_ISGL_NON_ALIGN
	if (uzreq->isgl->offset) {
		input_page_phys[0] = sg_phys(uzreq->isgl);
		input_nr_pages = 1;
#ifdef NVT_UNZIP_DEBUG
		dev_info(nvt_unzip->dev, "%s offset %d\n",
			__func__, uzreq->isgl->offset);
#endif
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
#if 0
			if ((input_page_phys[0] + uzreq->ilength)
				!= sg_phys(sg)) {
				dev_err(nvt_unzip->dev,
				"req#%u Not Supported sglist(not liner)\n",
				uzreq->desc.request_idx);
				goto _err_free;
			}
#endif
			uzreq->ilength += sg->length;
#ifdef NVT_UNZIP_DEBUG
			dev_info(nvt_unzip->dev, "req#%u sg input 0x%llx, 0x%x",
				uzreq->desc.request_idx,
				(u64)sg_phys(sg), sg->length);
#endif
		}
#ifdef NVT_UNZIP_DEBUG
		dev_info(nvt_unzip->dev, "req#%u sg input merge 0x%llx, 0x%x",
			uzreq->desc.request_idx,
			(u64)input_page_phys[0], uzreq->ilength);
#endif
		nvt_unzip->vbuff =  sg_virt(&(uzreq->isgl[0]));
	} else
#endif
	{
		input_nr_pages = uzreq->inents;
		for_each_sg(uzreq->isgl, sg, uzreq->inents, i) {
#ifndef NVT_UNZIP_INPUT_ISGL_NON_ALIGN
			if ((sg_phys(sg)&~PAGE_MASK) || ((i < uzreq->inents-1)
				&& (sg->length != PAGE_SIZE))) {
				dev_err(nvt_unzip->dev,
				"req#%u Not Support sglist(not page aligned)\n",
					uzreq->desc.request_idx);
				goto _err_free;
			}
#endif
			input_page_phys[i] = sg_phys(sg);
			uzreq->ilength += sg->length;

#ifdef NVT_UNZIP_DEBUG
		dev_info(nvt_unzip->dev, "req#%u sg input 0x%llx, 0x%x",
			uzreq->desc.request_idx, (u64)sg_phys(sg), sg->length);
#endif
		}
	}
	WARN_ON(uzreq->inents != i);

	for_each_sg(uzreq->osgl, sg, uzreq->onents, i) {
		output_page_phys[i] = sg_dma_address(sg);
		uzreq->olength += sg_dma_len(sg);

#ifdef NVT_UNZIP_DEBUG
	dev_info(nvt_unzip->dev, "req#%u sg output 0x%llx, 0x%x",
		uzreq->desc.request_idx, (u64)sg_dma_address(sg),
		sg_dma_len(sg));
#endif
	}
	WARN_ON(uzreq->onents != i);

	nvt_unzip->cur_uzreq = uzreq;
	uzreq->cb_func = cb;	/*for debug*/
	uzreq->cb_arg = arg;	/*for debug*/
#ifdef CONFIG_NVT_UNZIP_AUTH
    if (uzreq->flags&GZIP_FLAG_ENABLE_AUTH){
        ret = hw_auth_start(input_page_phys, 
                input_nr_pages, 
                TO_ALIGNED(uzreq->ilength, GZIP_ALIGNSIZE),
                &uzreq->desc.auth, 
                uzreq->isgl->offset);
        if (ret) {
		    /*free resource is needed*/
		    goto _err_free;
        }
    }
#endif

	ret = nvt_unzip_decompress_start(input_page_phys, input_nr_pages,
		TO_ALIGNED(uzreq->ilength, GZIP_ALIGNSIZE),
		output_page_phys, uzreq->onents, uzreq->olength,
		NULL,
		uzreq->flags, cb, arg);

	if (ret) {
		/*free resource is needed*/
		goto _err_free;
	}

	return 0;


_err_free:
		nvt_unzip->cur_uzreq = NULL;
		dma_unmap_sg(nvt_unzip->dev,
			uzreq->osgl, uzreq->onents, DMA_FROM_DEVICE);
		up(&nvt_unzip->sema);

err_init:
	return ret;
}
EXPORT_SYMBOL(nvt_unzip_decompress_async);

/**
* Must be called from the same task which has been started decompression
*/
int nvt_unzip_decompress_wait(struct nvt_unzip_desc *uzdesc)
{
    int retauth = 0;
	int ret = 0, patchbuf_out = 0;
	struct nvt_unzip_req *uzreq;

#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	struct nvt_unzip_buf *buf = nvt_unzip_dbg.input_buf_adr;
#endif
	TRACE_ENTRY();

	uzreq = to_nvt_unzip_req(uzdesc);

	BUG_ON(!nvt_unzip);

#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	if (buf)
		buf->ts_auth_start = nvt_unzip_get_nsecs();
#endif
#ifdef CONFIG_NVT_UNZIP_AUTH
	if (uzreq->flags&GZIP_FLAG_ENABLE_AUTH)
		retauth = hw_auth_wait();
#endif

#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	if (buf)
		buf->ts_auth_done = nvt_unzip_get_nsecs();
#endif
	wait_for_completion(&uzreq->decomp_done);
	/*	smp_rmb(); */
	ret = uzreq->desc.errorcode ? -EINVAL:uzreq->desc.decompressed_bytes;

#ifdef CONFIG_NVT_UNZIP_AUTH
	if (retauth < 0)
		ret = -EINVAL;
#endif

	TRACE(">>%s decompressd size: %x\n", __func__, ret);
	/*LOG_EVENT(OPAGE_DMA_FINISH_START);	*/
	{
		char str[32];
		snprintf(str, 32, "OPAGE_DMA_FINISH_START%d", ret);
		LOG_EVENT_STR(str);
	}
	patchbuf_out = ret;

	dma_unmap_sg(nvt_unzip->dev, uzreq->osgl,
		uzreq->onents, DMA_FROM_DEVICE);

	LOG_EVENT(OPAGE_DMA_FINISH_END);

	LOG_EVENT(DSCR_FINISH_START);
#ifdef CONFIG_NVT_UNZIP_FIFO
	if (nvt_unzip->fifo_enable && nvt_unzip->fast)
	{
		/*fifo_atf_done();*/
	} 
	else 
#endif			
	{	
		/*normal flow*/
		unzip_finish_src_adma();
	}

	unzip_finish_dst_adma();
	LOG_EVENT(DSCR_FINISH_END);
#if defined(NVT_UNZIP_DBG_LOG_EVENT_TIME)
	nvt_unzip_dbg.ts_wait_done = nvt_unzip_get_nsecs();
	if (buf)
		buf->ts_wait_done = nvt_unzip_dbg.ts_wait_done;
#endif
	unzip_wait_count++;
	up(&nvt_unzip->sema);
	TRACE_EXIT();
	return ret;
}
EXPORT_SYMBOL(nvt_unzip_decompress_wait);

/*hw1 support for gzip/zlib format only
since the HW_IOVEC_COMP_ZLIB make novatke hwunzip auto-detection */
int nvt_unzip_decompress_sync(struct nvt_unzip_desc *uzdesc,
	struct scatterlist *input_sgl,
	struct scatterlist *output_sgl,
	bool may_wait)
{
	int ret;
	unsigned int input_nents = 0;

	TRACE("UNZIP : %s start\n", __func__);
	if (!nvt_unzip) {
		pr_err("NVT Unzip Engine is not Initialized!\n");
		return -EINVAL;
	}

	input_nents = sg_nents(input_sgl);

	ret = dma_map_sg(nvt_unzip->dev, input_sgl, input_nents, DMA_TO_DEVICE);
	if (ret == 0) {
		dev_err(nvt_unzip->dev, "unable to map input bufferr\n");
		return -EINVAL;
	}
	/* Start decompression */
	ret = nvt_unzip_decompress_async(uzdesc, input_sgl,
		output_sgl, NULL, NULL, may_wait);

	if (!ret) {
		/* Kick decompressor to start right now */
		nvt_unzip_update_endpointer();

		/* Wait and drop lock */
		ret = nvt_unzip_decompress_wait(uzdesc);
	} else {
		dev_err(nvt_unzip->dev,
			"nvt gzip: nvt_unzip_decompress_async reset error\n");
	}
/*error_exit: */
	dma_unmap_sg(nvt_unzip->dev, input_sgl, input_nents, DMA_TO_DEVICE);
	return ret;
}
EXPORT_SYMBOL(nvt_unzip_decompress_sync);

static void nvt_unzip_clk_free(void)
{
#if 0
#ifdef CONFIG_OF
	if (!IS_ERR_OR_NULL(nvt_unzip->clk)) {
		clk_unprepare(nvt_unzip->clk);
		clk_put(nvt_unzip->clk);
	}
	if (!IS_ERR_OR_NULL(nvt_unzip->rst)) {
		clk_unprepare(nvt_unzip->rst);
		clk_put(nvt_unzip->rst);
	}
#endif
#endif
}


static u64 ungzip_dmamask = DMA_BIT_MASK(32);


void nvt_unzip_dbg_init(void)
{
	nvt_unzip_dbg.input_descriptor_list = nvt_unzip->data_input->des;
	nvt_unzip_dbg.output_descriptor_list = nvt_unzip->data_output->des;
	nvt_unzip_dbg.last_input = 0;
}

static int nvt_unzip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;
	int affinity = 0;
	/*void *buf;*/

#ifdef CONFIG_OF
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif

	nvt_unzip = devm_kzalloc(dev, sizeof(struct nvt_unzip_t), GFP_KERNEL);
	if (nvt_unzip == NULL) {
		/*dev_err(dev, "cannot allocate memory!!!\n");*/
		return -ENOMEM;
	}

	sema_init(&nvt_unzip->sema, 1);
	init_completion(&nvt_unzip->wait);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, nvt_unzip);
		return -ENODEV;
	}

	pr_err("[DEBUG]register base %x\n", res->start);
	nvt_unzip->base = devm_ioremap_resource(&pdev->dev, res);

	if (nvt_unzip->base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, nvt_unzip);
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	pr_err("[DEBUG] interregister base %x\n", res->start);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, nvt_unzip);
		return -ENODEV;
	}
	ret = request_irq(irq, nvt_unzip_isr, 0, dev_name(dev), nvt_unzip);

	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d err :%d\n", irq, ret);
		devm_kfree(dev, nvt_unzip);
		return -ENODEV;
	}

#ifndef CONFIG_OF
	/*affinity = 1;*/ /*default cpu0*/
#else
	if (!of_property_read_u32(dev->of_node, "irq-affinity", &affinity))
#endif
		if (num_online_cpus() > affinity) {
			/*for performance? */
			irq_set_affinity(irq, cpumask_of(affinity));
		}
#if 0
#ifdef CONFIG_OF
	nvt_unzip->clk = clk_get(dev, "gzip_clk");
	if (IS_ERR(nvt_unzip->clk)) {
		dev_err(dev, "cannot find gzip_clk: %ld!\n",
			PTR_ERR(nvt_unzip->clk));
		nvt_unzip->clk = NULL;
	} else {
		/*make checkpatch happy*/
		clk_prepare(nvt_unzip->clk);
	}
	nvt_unzip->rst = clk_get(dev, "gzip_rst");
	if (IS_ERR(nvt_unzip->rst)) {
		dev_err(dev, "cannot find gzip_rst: %ld!\n",
			PTR_ERR(nvt_unzip->rst));
		nvt_unzip->rst = NULL;
	} else {
		/*make checkpatch happy*/
		clk_prepare(nvt_unzip->rst);
	}
#endif
#endif

	dev->dma_mask = &ungzip_dmamask;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	platform_set_drvdata(pdev, (void *) nvt_unzip);
	{
		int i = 0;

		for (i = 0; i < HW_MAX_SIMUL_THR; i++) {
			nvt_unzip->input_buf[i] =
				nvt_inputbuf_alloc(HW_MAX_IBUFF_SZ);

			if (IS_ERR_OR_NULL(nvt_unzip->input_buf[i])) {
				int j = 0;

				/*dev_err(dev, "can't alloc input buffer\n");*/
				for (j = (i-1); j >= 0; j--) {
					/*if fail , free all */
					nvt_inputbuf_free(
						nvt_unzip->input_buf[i]);
				}

				return -ENOMEM;
			}
			nvt_unzip->input_buf_status[i] = BUF_FREE;
		}
	}
#ifdef NVT_UNZIP_DESC_KALLOC
	nvt_unzip->data_input = devm_kzalloc(dev,
		sizeof(struct st_src_adma), GFP_KERNEL);
	if (nvt_unzip->data_input == NULL) {
		dev_err(dev, "cannot allocate input description buffer\n");
		devm_kfree(dev, nvt_unzip);
		return (-ENOMEM);
	}
	nvt_unzip->data_output = devm_kzalloc(dev,
		sizeof(struct st_dst_adma), GFP_KERNEL);
	if (nvt_unzip->data_output == NULL) {
		dev_err(dev, "can't allocate output description buffer\n");
		devm_kfree(dev, nvt_unzip->data_input);
		devm_kfree(dev, nvt_unzip);
		return (-ENOMEM);
	}
#else
	{
	dma_addr_t phys_addr = 0;

	nvt_unzip->data_input = dma_alloc_coherent(dev,
			sizeof(struct st_src_adma), &phys_addr, GFP_KERNEL);
	if (nvt_unzip->data_input == NULL) {
		dev_err(dev, "cannot allocate input description buffer\n");
		devm_kfree(dev, nvt_unzip);
		return -ENOMEM;
	}
	nvt_unzip->data_input->des_phys = phys_addr;

	phys_addr = 0;
	nvt_unzip->data_output = dma_alloc_coherent(dev,
			sizeof(struct st_dst_adma), &phys_addr, GFP_KERNEL);
	if (nvt_unzip->data_output == NULL) {
		dev_err(dev, "can't allocate output description buffer\n");
		devm_kfree(dev, nvt_unzip);
		phys_addr = nvt_unzip->data_input->des_phys;
		dma_free_coherent(dev, sizeof(struct st_src_adma),
				nvt_unzip->data_input, phys_addr);
		return -ENOMEM;
	}
	nvt_unzip->data_output->des_phys = phys_addr;
	}
#endif
	/* set platform  device information*/
#ifdef CONFIG_NVT_UNZIP_FIFO
	if (of_get_property(dev->of_node, "enable_fifo_mode", NULL)) {
		nvt_unzip->fifo_enable = 1;
		nvt_unzip->fast = 0;
		hw_max_simul_thr = 1;
		pr_err("gzip support fifo mode\n");
	}
#endif
	nvt_unzip->dev = dev;

#ifdef CONFIG_NVT_UNZIP_FIFO
	dev_info(dev, "Registered Novatek unzip driver(%p) DBG: %p!! orig: %p hwthr:%d\n",
		nvt_unzip->base, &nvt_unzip_dbg, nvt_unzip, hw_max_simul_thr);
#else
    dev_info(dev, "Registered Novatek unzip driver(%p) DBG: %p!! orig: %p hwthr:%d\n",
		nvt_unzip->base, &nvt_unzip_dbg, nvt_unzip, HW_MAX_SIMUL_THR);
#endif
	init_log();
	init_timedifflog();
	nvt_unzip_dbg_init();
	return 0;
}

static int nvt_unzip_remove(struct platform_device *pdev)
{
	int i = 0;

	for (i = 0; i < HW_MAX_SIMUL_THR; i++)
		nvt_inputbuf_free(nvt_unzip->input_buf[i]);
#if 0
#ifdef CONFIG_OF
	if (!IS_ERR_OR_NULL(nvt_unzip->clk))
		clk_put(nvt_unzip->clk);
	if (!IS_ERR_OR_NULL(nvt_unzip->rst))
		clk_put(nvt_unzip->rst);
#endif
#endif
	nvt_unzip_clk_free();
#ifdef NVT_UNZIP_DESC_KALLOC
	devm_kfree(&pdev->dev, nvt_unzip->data_input);
	devm_kfree(&pdev->dev, nvt_unzip->data_output);
#else
	{
		dma_addr_t phys_addr = nvt_unzip->data_input->des_phys;

		dma_free_coherent(nvt_unzip->dev, sizeof(struct st_src_adma),
			nvt_unzip->data_input, phys_addr);
		phys_addr = nvt_unzip->data_output->des_phys;
		dma_free_coherent(nvt_unzip->dev, sizeof(struct st_dst_adma),
			nvt_unzip->data_output, phys_addr);
	}
#endif
	devm_kfree(&pdev->dev, nvt_unzip);

	return 0;
}

static const struct of_device_id nvt_unzip_dt_match[] = {
	/*{ .compatible = "novatek,nvt-unzip", },*/
	{ .compatible = "nvt,unzip", },
	{},
};
MODULE_DEVICE_TABLE(of, nvt_unzip_dt_match);

static struct platform_driver nvt_unzip_driver = {
	.probe		= nvt_unzip_probe,
	.remove		= nvt_unzip_remove,
	.driver = {
		.name	= "nvt,unzip",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(nvt_unzip_dt_match),
	},
};

int nvt_unzip_init(void)
{
	return platform_driver_register(&nvt_unzip_driver);
}
subsys_initcall(nvt_unzip_init);

static void __exit nvt_unzip_exit(void)
{
	platform_driver_unregister(&nvt_unzip_driver);
}
module_exit(nvt_unzip_exit);

/*************** for debugfs ******************/
#ifndef CONFIG_VD_RELEASE
struct nvt_unzip_file {
	void         *in_buff;
	void         *out_buff;
	size_t        in_sz;
	bool          up_to_date;
	struct page  *dst_pages[32];
};

static void __nvt_unzip_free(struct nvt_unzip_file *nvt_file, int sz)
{
	int i;

	vunmap(nvt_file->out_buff);
	for (i = 0; i < sz; i++)
		__free_page(nvt_file->dst_pages[i]);
	free_pages((unsigned long)nvt_file->in_buff,
		   get_order(HW_MAX_IBUFF_SZ));
	kfree(nvt_file);
}

static int nvt_unzip_open(struct inode *inode, struct file *file)
{
	unsigned int i = 0;
	struct nvt_unzip_file *nvt_file;

	nvt_file = kzalloc(sizeof(*nvt_file), GFP_KERNEL);
	if (!nvt_file)
		return -ENOMEM;

	nvt_file->in_buff = (void *)__get_free_pages(GFP_KERNEL,
						get_order(HW_MAX_IBUFF_SZ));
	if (!nvt_file->in_buff)
		goto err;

	for (i = 0; i < ARRAY_SIZE(nvt_file->dst_pages); ++i) {
		void *addr = (void *)__get_free_page(GFP_KERNEL);

		if (!addr)
			goto err;
		nvt_file->dst_pages[i] = virt_to_page(addr);
	}

	nvt_file->out_buff = vmap(nvt_file->dst_pages,
				  ARRAY_SIZE(nvt_file->dst_pages),
				  VM_MAP, PAGE_KERNEL);
	if (!nvt_file->out_buff)
		goto err;

	file->private_data = nvt_file;

	return nonseekable_open(inode, file);

err:
	__nvt_unzip_free(nvt_file, i);
	return -ENOMEM;
}

static int nvt_unzip_close(struct inode *inode, struct file *file)
{
	struct nvt_unzip_file *nvt_file = file->private_data;

	__nvt_unzip_free(nvt_file, ARRAY_SIZE(nvt_file->dst_pages));
	return 0;
}

static ssize_t nvt_unzip_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	ssize_t ret;
	struct nvt_unzip_file *nvt_file = file->private_data;
	size_t max_out = ARRAY_SIZE(nvt_file->dst_pages) * PAGE_SIZE;
	struct nvt_unzip_desc *uzdesc = NULL;
	struct scatterlist isg[1];
	struct sg_table out_sgt;

	if (count < max_out)
		return -EINVAL;
	if (!nvt_file->in_sz)
		return 0;

	uzdesc = nvt_unzip_alloc_descriptor(0x0);
	if (IS_ERR_OR_NULL(uzdesc))
		return PTR_ERR(uzdesc);

	sg_init_one(isg, nvt_file->in_buff, ALIGN(nvt_file->in_sz, 8));
	sg_alloc_table_from_pages(&out_sgt, nvt_file->dst_pages,
		ARRAY_SIZE(nvt_file->dst_pages), 0x0, max_out, GFP_KERNEL);

	/* Yes, right, no synchronization here.
	 * nvt unzip sync has its own synchronization, so we do not care
	 * about corrupted data with simultaneous read/write on the same
	 * fd, we have to test different scenarious and data corruption
	 * is one of them */
	ret = nvt_unzip_decompress_sync(uzdesc, isg, out_sgt.sgl, true);

	sg_free_table(&out_sgt);
	nvt_unzip_free_descriptor(uzdesc);


	nvt_file->in_sz = 0;

	if (ret < 0 || !ret)
		return -EINVAL;

	if (copy_to_user(buf, nvt_file->out_buff, (unsigned long)ret))
		return -EFAULT;

	return ret;
}

static ssize_t nvt_unzip_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct nvt_unzip_file *nvt_file = file->private_data;

	/* Worry about synchronization? Please read comments in read function */

	if (count > HW_MAX_IBUFF_SZ)
		return -EINVAL;
	if (copy_from_user(nvt_file->in_buff, buf, count))
		return -EFAULT;

	nvt_file->in_sz = count;

	return (ssize_t)count;
}

static const struct file_operations nvt_unzip_fops = {
	.owner	= THIS_MODULE,
	.open    = nvt_unzip_open,
	.release = nvt_unzip_close,
	.llseek	 = no_llseek,
	.read	 = nvt_unzip_read,
	.write	 = nvt_unzip_write
};

static struct miscdevice nvt_unzip_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "nvt_unzip",
	.fops	= &nvt_unzip_fops,
};

static struct dentry *nvt_unzip_debugfs;

static void nvt_unzip_debugfs_create(void)
{
	nvt_unzip_debugfs = debugfs_create_dir("nvt_unzip", NULL);
	if (!IS_ERR_OR_NULL(nvt_unzip_debugfs)) {
		debugfs_create_u64("calls",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_unzip_debugfs, &nvt_unzip_calls);
		debugfs_create_u64("errors",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_unzip_debugfs, &nvt_unzip_errors);
		debugfs_create_u64("nsecs",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_unzip_debugfs, &nvt_unzip_nsecs);
		debugfs_create_bool("quiet",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    nvt_unzip_debugfs, &nvt_unzip_quiet);
	}
}

static void nvt_unzip_debugfs_destroy(void)
{
	debugfs_remove_recursive(nvt_unzip_debugfs);
}

static int __init nvt_unzip_file_init(void)
{
	if (!nvt_unzip)
		return -EINVAL;

	nvt_unzip_debugfs_create();
	return misc_register(&nvt_unzip_misc);
}

static void __exit nvt_unzip_file_cleanup(void)
{
	nvt_unzip_debugfs_destroy();
	misc_deregister(&nvt_unzip_misc);
}

module_init(nvt_unzip_file_init);
module_exit(nvt_unzip_file_cleanup);

#endif /* CONFIG_VD_RELEASE */

MODULE_DESCRIPTION("Novatek SOC HW Decompress for MMC driver");
MODULE_LICENSE("GPL v2");


/* hal function :
	ADMA related
	global HAL control
*/

static void unzip_issue_src_adma(void)
{
	unsigned int val;

	if (nvt_unzip->data_input_started) {
		/*special case: we don't use it normally*/
		val = HAL_GZIP_SRC_ADMA_CONTINUE;
	} else {
		val = HAL_GZIP_SRC_ADMA_START;
		nvt_unzip->clock_ns = nvt_unzip_get_nsecs();
	}

	nvt_unzip->data_input_started = 1;
	writel(val, nvt_unzip->base + HAL_GZIP_HW_SETTING);
}


static int unzip_prepare_src_adma_pages(dma_addr_t *ipages,
		int nr_ipages, unsigned int input_bytes)
{
	struct st_src_adma *data_input = nvt_unzip->data_input;
	dma_addr_t des_phys = 0;
	int err = 0;
	struct gzip_desc_s *des = NULL;
	int first_des_index = nvt_unzip->data_input_cnt;
	int i = 0;
	struct nvt_unzip_req *uzreq = nvt_unzip->cur_uzreq;

#ifdef DBG_DESCRIPTOR
	dev_err(nvt_unzip->dev, "%s: nr_ipages %d\n", __func__, nr_ipages);
	for (i = 0; i < nr_ipages; i++)
		dev_err(nvt_unzip->dev, "%d addr %x\n", i, ipages[i]);
#endif
	TRACE("%s entry:  len 0x%x\n", __func__, input_bytes);

	if (nr_ipages == 1) {
		/*ss special case............................*/
		unsigned int calc_len = 0;
		unsigned int len = input_bytes;
		unsigned int addr = ipages[0];

#define UNIT_MAX_LEN (1<<15)
		while ((calc_len < len)) {
			bool last_unit = false;

			des = &data_input->des[nvt_unzip->data_input_cnt];
			BUG_ON(!des);

			if ((len <= UNIT_MAX_LEN)
				|| (calc_len+UNIT_MAX_LEN >= len))
				last_unit = true;

			des->attribute = EN_DESC_VALID | EN_DESC_TRANSFER;
			if (last_unit)
				des->attribute |= EN_DESC_END;
			des->len = (last_unit) ? len - calc_len : UNIT_MAX_LEN;
			des->addr = addr + calc_len;

#ifdef DBG_DESCRIPTOR
			dev_err(nvt_unzip->dev, ">>>NVT_UNZIP ");
			dev_err(nvt_unzip->dev, ": input des: [%2d]len: 0x%x",
					nvt_unzip->data_input_cnt, des->len);
			dev_err(nvt_unzip->dev, " vaddr: 0x%x %p attr: 0x%x\n",
					des->addr,
					nvt_unzip->vbuff, des->attribute);
#endif
#if 0
			writel(data_input->des_phys,
				nvt_unzip->base + HAL_GZIP_SRC_ADMA_ADDR);
			unit_num++;
#endif
			calc_len += UNIT_MAX_LEN;
			nvt_unzip->data_input_cnt += 1;
			nvt_unzip_dbg.data_input_cnt =
				nvt_unzip->data_input_cnt;
		}
	} else {
		for (i = 0; i < nr_ipages; i++) {
			des = &data_input->des[nvt_unzip->data_input_cnt];
			BUG_ON(!des);

			des->attribute = EN_DESC_VALID | EN_DESC_TRANSFER;
			if (i == (nr_ipages-1))
				des->attribute |= EN_DESC_END;
			/*TOCHECK: :wit is ok to alwasy set 4K page size*/
#ifdef NVT_UNZIP_INPUT_ISGL_NON_ALIGN
			if (i == 0)
				des->len = PAGE_SIZE - uzreq->isgl->offset;
			else
				des->len = PAGE_SIZE;
#else
			des->len = PAGE_SIZE;
#endif
			/*(last_unit) ? len - calc_len : UNIT_MAX_LEN; */
			des->addr = ipages[i];

#ifdef DBG_DESCRIPTOR
			dev_err(nvt_unzip->dev,
				">>>NVT_UNZIP : input des: [%2d]len: 0x%x",
				nvt_unzip->data_input_cnt, des->len);
			dev_err(nvt_unzip->dev,
				" addr: 0x%x %p attr: 0x%x\n",
				des->addr, sg_virt(&(uzreq->isgl[i])),
				des->attribute);
#endif
			nvt_unzip->data_input_cnt += 1;
			nvt_unzip_dbg.data_input_cnt =
				nvt_unzip->data_input_cnt;
		}
	}
#ifdef NVT_UNZIP_DESC_KALLOC
	des_phys = dma_map_single(nvt_unzip->dev, data_input->des,
		sizeof(struct gzip_desc_s) * MAX_ADMA_NUM, DMA_TO_DEVICE);
	if (dma_mapping_error(nvt_unzip->dev, des_phys)) {
		dev_err(nvt_unzip->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}
#endif
	if (first_des_index == 0) {
#ifdef NVT_UNZIP_DESC_KALLOC
		data_input->des_phys = des_phys;
#endif
		writel(data_input->des_phys,
			nvt_unzip->base + HAL_GZIP_SRC_ADMA_ADDR);
	}

	return err;

}
static void unzip_finish_src_adma(void)
{
	if (nvt_unzip->data_input->des_phys) {
		dma_unmap_single(nvt_unzip->dev,
		nvt_unzip->data_input->des_phys,
		sizeof(struct gzip_desc_s) * MAX_ADMA_NUM, DMA_TO_DEVICE);
	}
}

static void unzip_issue_dst_adma(void)
{
	unsigned int val;

	if (nvt_unzip->data_output_started)
		val =  HAL_GZIP_DES_ADMA_CONTINUE;
	else
		val =  HAL_GZIP_DES_ADMA_START;
	unzip_ddma_count++;
	nvt_unzip->data_output_started = 1;
	writel(val, nvt_unzip->base + HAL_GZIP_HW_SETTING);
}


void nvt_bouncebuf_dst_desc_destroy(void)
{

	if (nvt_unzip->data_output->des_phys)
		dma_unmap_single(nvt_unzip->dev,
			nvt_unzip->data_output->des_phys,
		sizeof(struct gzip_desc_s) * MAX_ADMA_NUM, DMA_TO_DEVICE);

}

static int unzip_prepare_dst_adma_pages(
	dma_addr_t *opages, int nr_opages, unsigned int output_bytes)
{
	struct st_dst_adma *data_output = nvt_unzip->data_output;
	int pre_data_output_cnt = nvt_unzip->data_output_cnt;
	struct gzip_desc_s *dst_des = &data_output->des[pre_data_output_cnt];
	int i = 0, err = 0;

	for (i =  pre_data_output_cnt;
			i < nr_opages + pre_data_output_cnt; i++) {
		int attr = EN_DESC_VALID | EN_DESC_TRANSFER;

		dst_des[i].attribute = attr;
		dst_des[i].len = PAGE_SIZE;
		nvt_unzip->opages[i] = dst_des[i].addr = opages[i];
		/*already flush?*/
		#ifdef DBG_DESCRIPTOR
		dev_err(nvt_unzip->dev,
			"NVT_UNZIP: out des[%d]: len: %x addr: %x  ,attr: %x\n",
			i, dst_des[i].len, dst_des[i].addr,
			dst_des[i].attribute);
		#endif
	}
	i--;
	dst_des[i].attribute |= EN_DESC_END;
#ifdef DBG_DESCRIPTOR
	dev_err(nvt_unzip->dev,
			"NVT_UNZIP: last out des[%d]: len: %x addr: %x  ,attr: %x\n",
			i, dst_des[i].len, dst_des[i].addr,
			dst_des[i].attribute);
#endif

#ifdef NVT_UNZIP_DESC_KALLOC
	data_output->des_phys = dma_map_single(nvt_unzip->dev,
		data_output->des,
		sizeof(struct gzip_desc_s) * MAX_ADMA_NUM, DMA_TO_DEVICE);

	if (dma_mapping_error(nvt_unzip->dev, data_output->des_phys)) {
		dev_err(nvt_unzip->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}
#endif
	if (nvt_unzip->data_output_cnt == 0)
		writel(data_output->des_phys,
			nvt_unzip->base + HAL_GZIP_DES_ADMA_ADDR);

	nvt_unzip->data_output_cnt += output_bytes;
	nvt_unzip_dbg.data_output_cnt = nvt_unzip->data_output_cnt;
	nvt_unzip->opages_cnt = nr_opages;

	return err;
}


static void unzip_finish_dst_adma(void)
{
	nvt_bouncebuf_dst_desc_destroy();

}

#ifdef CONFIG_NVT_UNZIP_FIFO
void nvt_unzip_fifo_enable(int from_aes)
{
	u32 reg = readl(nvt_unzip->base + HAL_GZIP_HW_CONFIG);

	reg |= HAL_GZIP_IFF_MODE_EN;
	if (from_aes)
		reg |= HAL_GZIP_IFF_INPUT_AES;

	writel(reg, nvt_unzip->base + HAL_GZIP_HW_CONFIG);
}
#endif

#ifdef CONFIG_NVT_UNZIP_FIFO
int unzip_hal_reset(enum hw_iovec_comp_type comp_type, bool may_wait, int from_aes)
#else
int unzip_hal_reset(enum hw_iovec_comp_type comp_type, bool may_wait)
#endif
{
	switch (comp_type) {
	case HW_IOVEC_COMP_GZIP:
	case HW_IOVEC_COMP_ZLIB:
	case HW_IOVEC_COMP_UNCOMPRESSED:
		break;
	default:
		return -ENOMEM;
	}

	if (!nvt_unzip) {
		pr_err("NVT Unzip Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Check contention */
	if (!may_wait && down_trylock(&nvt_unzip->sema)) {
		dev_err(nvt_unzip->dev, "NVT Unzip BUSY!\n");
		return -EBUSY;
	} else if (may_wait) {
		down(&nvt_unzip->sema);
	} else {
		/*what is this condition...
			this is normal case ? */
	}
	TRACE("NVT UNGZIP : %s\n", __func__);

	reinit_completion(&nvt_unzip->wait);
	writel(HAL_GZIP_HW_RESET, nvt_unzip->base + HAL_GZIP_HW_SETTING);
	writel(HAL_GZIP_CLR_SRC_CNT, nvt_unzip->base + HAL_GZIP_HW_SETTING);
	writel(HAL_GZIP_CLR_DST_CNT, nvt_unzip->base + HAL_GZIP_HW_SETTING);

	/*hw config*/
	switch (comp_type) {
	case HW_IOVEC_COMP_GZIP:
	case HW_IOVEC_COMP_ZLIB:
	/*hw can auto dection RFC_1950 zlib and RFC_1952 gzip
	for other type HW_IOVEC_COMP_LZO/HW_IOVEC_COMP_MAX*/
	writel(0, nvt_unzip->base + HAL_GZIP_HW_CONFIG);
		break;
	case HW_IOVEC_COMP_UNCOMPRESSED:
	default:
	/*device use setting of auto-detect*/
	writel(HAL_GZIP_BYPASS, nvt_unzip->base + HAL_GZIP_HW_CONFIG);
	break;
	}

	nvt_unzip->comp_type = comp_type;

	/*clear intr*/
	writel(ERROR_BTIS|CONTROL_BITS_DST,
		nvt_unzip->base + HAL_GZIP_INTERRUPT_EN);
	/*set timeout*/
	writel(-1, nvt_unzip->base + HAL_GZIP_TIMEOUT);
	nvt_unzip->data_input_started = nvt_unzip->data_output_started = 0;
	nvt_unzip->status = HAL_STATUS_STOP;
	nvt_unzip->data_input_cnt = nvt_unzip->data_output_cnt = 0;
	nvt_unzip_dbg.data_output_cnt = nvt_unzip_dbg.data_input_cnt = 0;

	memset(nvt_unzip->data_input->des,
		0x0, sizeof(struct gzip_desc_s)*MAX_ADMA_NUM);
	memset(nvt_unzip->data_output->des,
		0x0, sizeof(struct gzip_desc_s)*MAX_ADMA_NUM);
#ifdef CONFIG_NVT_UNZIP_FIFO	
	if (nvt_unzip->fifo_enable && nvt_unzip->fast)
		nvt_unzip_fifo_enable(from_aes);
#endif	
	return 0;
}

