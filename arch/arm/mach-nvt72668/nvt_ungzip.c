/*********************************************************************************************
 *
 *	nvt_ungzipc.c ( Novateck Soc unzip device driver for Samsung Platform)
 *
 *	author : jianhao.su@novatek.com.tw
 *
 * 2015/01/8, jianhao.su: novatek implemention using Samsung interface.
 *
 ********************************************************************************************/
/*	base on  */
/*********************************************************************************************
 *
 *	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
 *
 *	author : seungjun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 *
 ********************************************************************************************/

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
#include <linux/mempool.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <mach/nvt_hwclock.h>

#include <linux/delay.h>
#include <linux/of.h>
//#include <mach/soc.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <mach/nvt_ungzip.h>
//#include <linux/mm.h>
#include <linux/buffer_head.h>
#include "timelog.h"

#if defined(CONFIG_NVT_HW_SHA256)
#include <mach/nvt_sha256.h>
#endif

#if defined(CONFIG_SEPARATE_PRINTK_FROM_USER) 
void _sep_printk_start(void);
void _sep_printk_end(void);
#else
#define _sep_printk_start() {}
#define _sep_printk_end() {}
#endif
/*implement define*/

/**/
#define ATTRI_DESC_EFFECTIVE         	0x0001	/* indicates this line of descriptor is effective */
#define ATTRI_DESC_END               	0x0002	/* indicates to end of descriptor */
#define ATTRI_DESC_INTERRUPT         	0x0004	/* generates DMA Interrupt when the operation of
						   the descriptor line is completed */

#define ATTRI_DESC_NOP               	0x0000	/* do not execute current line and go to next line */
#define ATTRI_DESC_RSV               	0x0010	/* same as Nop */
#define ATTRI_DESC_TRAN              	0x0020	/* transfer data of one descriptor line */
#define ATTRI_DESC_LINK              	0x0030	/* link to another descriptor */


#define HAL_GZIP_HW_SETTING		( 0x00)
#define HAL_GZIP_SRC_ADMA_ADDR		( 0x04)
#define HAL_GZIP_SRC_ADMA_ADDR_CURR	( 0x08)
#define HAL_GZIP_SRC_ADMA_STATUS	( 0x0c)
#define HAL_GZIP_DES_ADMA_ADDR		( 0x10)
#define HAL_GZIP_DES_ADMA_ADDR_CURR	( 0x14)
#define HAL_GZIP_DES_ADMA_STATUS	( 0x18)
#define HAL_GZIP_HW_CONFIG		( 0x1c)
#define HAL_GZIP_HW_DEBUG		( 0x20)
#define HAL_GZIP_SRC_DCOUNT		( 0x24)
#define HAL_GZIP_DES_DCOUNT		( 0x28)
#define HAL_GZIP_TIMEOUT		( 0x2c)
#define HAL_GZIP_INTERRUPT_EN		( 0x30)
#define HAL_GZIP_INTERRUPT		( 0x34)

#define HAL_GZIP_SRC_ADMA_ADDR_FINAL	( 0x38)
#define HAL_GZIP_DES_ADMA_ADDR_FINAL 	( 0x3c)
#define HAL_GZIP_DES_ADMA_DBG_SRC_SEL	( 0x40)	//for hw debug only
#define HAL_GZIP_DES_ADMA_DBG_VAL	( 0x44)	//for hw debug only
#define HAL_GZIP_SRC_ADMA_DBG_SRC_SEL	( 0x48) //for hw debug only
#define HAL_GZIP_SRC_ADMA_DBG_VAL	( 0X4c)	//for hw debug only

/* bit mapping of gzip setting register */
#define HAL_GZIP_CLR_DST_CNT		 (BIT(6))
#define HAL_GZIP_CLR_SRC_CNT		 (BIT(5))
#define HAL_GZIP_HW_RESET		 0x00000010UL
#define HAL_GZIP_HW_RESET		 0x00000010UL
#define HAL_GZIP_SRC_ADMA_START		 0x00000001UL
#define HAL_GZIP_SRC_ADMA_CONTINUE	 0x00000002UL
#define HAL_GZIP_DES_ADMA_START		 0x00000004UL
#define HAL_GZIP_DES_ADMA_CONTINUE	 0x00000008UL

//#define HAL_GZIP_HW_DEBUG                      (HAL_GZIP_REG_BASE + 0x20)
#define HAL_GZIP_DST_HAVE_DATA		(BIT(1))
#define HAL_GZIP_SRC_HAVE_DATA          (BIT(0))

//#define HAL_GZIP_HW_CONFIG                     (HAL_GZIP_REG_BASE + 0x1c)
#define HAL_GZIP_DST_ADMA_ENDIAN	(BIT(4)) // 0 : little endian 1: big endian
#define HAL_GZIP_SRC_ADMA_ENDIAN	(BIT(3)) // 0 : little endian 1: big endian
#define HAL_GZIP_BUS_SYNC               (BIT(2))
#define HAL_GZIP_RFC_1951               (BIT(1))
#define HAL_GZIP_BYPASS                 (BIT(0))

/* bit mapping of gzip interrupt register */
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

#define ERROR_BTIS                    	(HAL_GZIP_ERR_FILE_ABORTED|HAL_GZIP_ERR_TABLE_GEN|HAL_GZIP_ERR_HEADER|HAL_GZIP_ERR_EOF|\
	HAL_GZIP_ERR_MEMORY|HAL_GZIP_ERR_CODE|HAL_GZIP_ERR_DISTANCE|HAL_GZIP_ERR_SIZE|HAL_GZIP_ERR_CRC|HAL_GZIP_ERR_TIMEOUT \
	| HAL_GZIP_INVAILD_SRC_DSC | HAL_GZIP_INVAILD_DST_DSC)
#define CONTROL_BITS                    (HAL_GZIP_DST_ADMA_STOP|HAL_GZIP_DST_ADMA_EOF|HAL_GZIP_SRC_ADMA_STOP|HAL_GZIP_SRC_ADMA_EOF)
#define CONTROL_BITS_DST                    (HAL_GZIP_DST_ADMA_STOP|HAL_GZIP_DST_ADMA_EOF|HAL_GZIP_SRC_ADMA_STOP)


#define DMA_MAX_LENGTH ((1<<16) -1)

#define FDBG(...) printk(__VA_ARGS__)	
#define TDBG(...) printk(__VA_ARGS__)	
#define LDBG() //printk("%s :%d \n", __FUNCTION__, __LINE__)	
//#define DBG_DESCRIPTOR
#define TRACE_FLOW 0
#define TRACE(...) \
	if(TRACE_FLOW) printk(__VA_ARGS__);

#define TRACE_ENTRY() \
	if(TRACE_FLOW) printk("###nvtunzip hal: %s %d entry\n", __FUNCTION__, __LINE__);

#define TRACE_EXIT() \
	if(TRACE_FLOW) printk("###nvtunzip hal: %s %d exit\n", __FUNCTION__, __LINE__);

typedef enum gzip_attribute_e {
        EN_DESC_VALID     = 0x01,
        EN_DESC_END       = 0x02,
        EN_DESC_INT       = 0x04,
        EN_DESC_TRANSFER  = 0x20,
        EN_DESC_LINK      = 0x30
} gzip_attribute_t;

#define EN_DESC_ACT_NOP_MASK	(0x30)
#define EN_DESC_ACT_MASK	(0x30)

typedef struct gzip_desc_s {
        unsigned short attribute;
        unsigned short len;                     /* decode length */
        unsigned int   addr;            /* buffer address(physical) */
} gzip_desc_t;

#define MAX_ADMA_NUM (256)
struct st_src_adma
{
	gzip_desc_t des[MAX_ADMA_NUM] ;
	dma_addr_t des_phys;
};

struct st_dst_adma
{
	gzip_desc_t des[MAX_ADMA_NUM] ;
	dma_addr_t des_phys;
};

static int hw_max_simul_thr = HW_MAX_SIMUL_THR;


struct nvt_unzip_debug_t
{
	unsigned int last_intr_status;
	void * input_descriptor_list;
	void * output_descriptor_list;
	int last_input ;
	unsigned int input_size;
	unsigned int output_size;
	int data_input_cnt;		//for hw2
	int data_output_cnt;		//for hw2
	struct page **output_pages ;
	int output_pages_num;
	void * input_buf_adr;	
};

static struct nvt_unzip_debug_t nvt_unzip_dbg = 
{
	.last_intr_status = 0,
	.input_descriptor_list = NULL,
	.output_descriptor_list = NULL,
	.input_size = 0,
	.data_input_cnt = 0,//for hw2
	.data_output_cnt = 0,
	.last_input = 0,
	.output_pages = NULL,
	.output_pages_num = 0,
	.input_buf_adr = NULL,
};
struct nvt_unzip_t
{
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
	//phys_addr_t pLzBuf; //samsung hw buf?  TOCHECK.
	unzip_cb_t isrfp;       //isr callback
	void *israrg;		//isr callbak
	//void *nvt1202buf;
	//dma_addr_t nvt1202phybuf;
	dma_addr_t opages[MAX_ADMA_NUM];
	void *vbuff;
	//struct clk *rst;	//for clk gating
	//struct clk *clk;	//for clk gating
	u32 isize;		//TOCHECK
	u32  opages_cnt;
	int decompressed;
	struct unzip_buf *input_buf[HW_MAX_SIMUL_THR] ;
	spinlock_t input_buf_lock;

	int input_buf_status[HW_MAX_SIMUL_THR];
	#define BUF_FREE 0
	#define BUF_BUSY 1
	struct st_src_adma * data_input;
	int data_input_cnt;		//for hw2
	struct st_dst_adma * data_output;
	int data_output_cnt;		//for hw2
	int data_input_started;
	int data_output_started;
	int status;
#define HAL_STATUS_STOP (0x1)
	unsigned long long clock_ns;
	struct unzip_buf * cache_patch_buf;
	struct unzip_buf * input_cache_patch_buf;
	struct unzip_buf * src_desc_cache_patch_buf;
	enum hw_iovec_comp_type comp_type;
};
static struct nvt_unzip_t *nvt_unzip = NULL;
void debug_nvt_unzip_status(void)
{
    if(nvt_unzip == NULL)
            return ;

    printk("====dump nvt_unzip data struct====\n");
    printk("vbuf : 0x%x\n", (unsigned int)nvt_unzip->vbuff);
    printk("isize : 0x%x\n", nvt_unzip->isize);
    printk("opages_cnt : 0x%x\n", nvt_unzip->opages_cnt);
    printk("decompressed : 0x%x\n", nvt_unzip->decompressed);
    printk("input_buf : %p\n", (unsigned int*)nvt_unzip->input_buf);
    printk("st_src_adma : %p\n", nvt_unzip->data_input);
    printk("data_input_cnt : 0x%x\n", nvt_unzip->data_input_cnt);
    printk("st_dst_adma : %p\n", nvt_unzip->data_output);
    printk("data_output_cnt : 0x%x\n", nvt_unzip->data_output_cnt);
    printk("data_input_started : 0x%x\n", nvt_unzip->data_input_started);
    printk("data_output_started : 0x%x\n", nvt_unzip->data_output_started);
    printk("status : 0x%x\n", nvt_unzip->status);
    printk("clock_ns : 0x%llx\n", nvt_unzip->clock_ns);
    printk("cache_patch_buf : %p\n", nvt_unzip->cache_patch_buf);
    printk("input_cache_patch_buf : %p\n", nvt_unzip->input_cache_patch_buf);
    printk("src_desc_cache_patch_buf : %p\n", nvt_unzip->src_desc_cache_patch_buf);
    printk("===================================\n");
}

void debug_nvt_unzip_dbg_status(void) 
{ 

	printk("====dump nvt_unzip_dbg data struct====\n"); 
	printk("last_intr_status : 0x%x\n", (unsigned int)nvt_unzip_dbg.last_intr_status); 
	printk("input_descriptor_list : 0x%x\n", (unsigned int)nvt_unzip_dbg.input_descriptor_list); 
	printk("output_descriptor_list : 0x%x\n", (unsigned int)nvt_unzip_dbg.output_descriptor_list); 
	printk("input_size : 0x%x\n", (unsigned int)nvt_unzip_dbg.input_size); 
	printk("data_input_cnt : 0x%x\n", (unsigned int)nvt_unzip_dbg.data_input_cnt); 
	printk("data_output_cnt : 0x%x\n", (unsigned int)nvt_unzip_dbg.data_output_cnt); 
	printk("last_input : 0x%x\n", (unsigned int)nvt_unzip_dbg.last_input); 
	printk("output_pages : 0x%x\n", (unsigned int)nvt_unzip_dbg.output_pages); 
	printk("output_pages_num : 0x%x\n", (unsigned int)nvt_unzip_dbg.output_pages_num); 
	printk("input_buf_adr : 0x%x\n", (unsigned int)nvt_unzip_dbg.input_buf_adr); 
	printk("===================================\n"); 
}
	
static unsigned long long nvt_unzip_calls;
static unsigned long long nvt_unzip_errors;
static unsigned long long nvt_unzip_nsecs;
static unsigned int       nvt_unzip_quiet;

static unsigned long long nvt_unzip_get_nsecs(void)
{
#ifdef CONFIG_NVT_HW_CLOCK 
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	struct timespec ts;
	getrawmonotonic(&ts);
	return ts.tv_sec*1000000000 +ts.tv_nsec;
	//return sched_clock();
#endif
}



/* interface function*/

static void unzip_finish_dst_adma(void);
static int unzip_prepare_dst_adma(int pages, dma_addr_t *opages/*, unsigned short * attrs*/);
static void unzip_issue_dst_adma(void);
static void unzip_finish_src_adma(void);
static int unzip_prepare_src_adma( /*unsigned short attr,*/unsigned int len, unsigned int addr, bool last_input);
static void unzip_issue_src_adma(void);
int unzip_hal_reset(enum hw_iovec_comp_type, bool may_wait);
void nvt_bouncebuf_dst_desc_destroy(void);


void unzip_update_endpointer(void)
{
	TRACE("%s %d\n",__FUNCTION__, __LINE__);
	LOG_EVENT(DMA_ISSUE_START);
	unzip_issue_dst_adma();
	unzip_issue_src_adma();
	LOG_EVENT(DMA_ISSUE_DONE);
}
EXPORT_SYMBOL(unzip_update_endpointer);

static void __maybe_unused nvt_unzip_clockgating(int bOn)
{
#if 0 	
	if(!nvt_unzip->rst || !nvt_unzip->clk)
		return;
	
	if(bOn) {
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

static void nvt_unzip_dump_reg(void)
{
#define UNZIP_REG_INFO(REG) \
	printk("	%-30s : 0x%08x\n", #REG, readl(nvt_unzip->base + REG));
	
	printk("-------------DUMP GZIP registers------------\n");
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
		if( intr & ERROR_BTIS){
#define UNZIP_ERR_INTR(ERR) \
			if(intr & ERR) {printk("		ERROR 0x%010lx %s\n", ERR, #ERR);}

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

	printk("--------------------------------------------\n");
}

/* this is debug function, but need care of the caller situation*/
void nvt_unzip_dump_descriptor(void)
{
	int i = 0;
	struct st_dst_adma  * data_output = nvt_unzip->data_output;
	struct st_src_adma  * data_input = nvt_unzip->data_input;

	printk("---------NVT UNGZIP DUMP ADMA Descriptor-----------\n");
	unzip_finish_dst_adma();
	printk(" DEST descriptor[%d]: \n",nvt_unzip->opages_cnt);
	
	for(i = 0; i < nvt_unzip->opages_cnt; i++ )
		printk("[%03d] attr: 0x%x length: 0x%8x addr :0x%08x vir_addr :0x%08x\n", i,
			data_output->des[i].attribute, data_output->des[i].len, data_output->des[i].addr, nvt_unzip->opages[i]);

	unzip_finish_src_adma();
	printk(" SRC descriptor: \n");
	for( i = 0 ; i < nvt_unzip->data_input_cnt ; i++)	
		printk("[%03d] attr: %8x length: 0x%8x addr :0x%08x\n", 
				i,data_input->des[i].attribute, data_input->des[i].len, data_input->des[i].addr);
	printk("--------------------------------------------\n");

}
EXPORT_SYMBOL(nvt_unzip_dump_descriptor);

static void nvt_unzip_dump_input_buffer(void)
{
	u32 ibuff = (nvt_unzip->data_input->des[0]).addr;
	unsigned int *buf = (unsigned int *) nvt_unzip->vbuff;
	_sep_printk_start();
	pr_err("Input buffer pointer phy=0x%08X vir=0x%p datasize= 0x%x\n",
	       ibuff, nvt_unzip->vbuff, nvt_unzip->isize);
	pr_err("-------------DUMP GZIP Input Buffers--------\n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1 , buf, nvt_unzip->isize, true  );
	pr_err("--------------------------------------------\n");
	_sep_printk_end();
}

static void nvt_unzip_dump(void)
{
	nvt_unzip_dump_reg();
	nvt_unzip_dump_descriptor();
	nvt_unzip_dump_input_buffer();
	debug_nvt_unzip_status();
	debug_nvt_unzip_dbg_status();
}


/* isr */

static irqreturn_t nvt_unzip_isr(int irq, void* devId)
{
	u32 value;
	//int decompressed = 0;//readl(nvt_unzip->base + HAL_GZIP_DES_DCOUNT);
	int decompressed = readl(nvt_unzip->base + HAL_GZIP_DES_DCOUNT);
	int err = 0;
	int clr_intr = 0;
	TRACE_ENTRY();
	value = readl(nvt_unzip->base + HAL_GZIP_INTERRUPT);

	if(value & ERROR_BTIS){
		err =  value &(ERROR_BTIS);
		clr_intr  |= err;
#if 1
	/* because source length is not accurate,  HAL_GZIP_ERR_EOF will happen.
	   and dest length is correct so use use destination length 
			to controll and ignore the error .
	*/
		if( err & HAL_GZIP_ERR_EOF){
			err &= ~(HAL_GZIP_ERR_EOF);
			//clr_intr &= ~HAL_GZIP_ERR_EOF;
			//printk("nvt gzip err eof\n");
		}
#endif
		if(err ){
		
			nvt_unzip_errors++;
			if( 1 || !nvt_unzip_quiet) /*currently novatek is just in develop stage.... */ 
			{
				pr_err(KERN_ERR "unzip: unzip error interrupt happened \n");
				nvt_unzip_dump();
				BUG_ON(err);	
			}
		}
		if( err & HAL_GZIP_ERR_TIMEOUT){
			pr_err(KERN_ERR "unzip: unzip TIMEOUT \n");
		}
	}
	

	if(value & HAL_GZIP_DST_ADMA_EOF){
		clr_intr |= HAL_GZIP_DST_ADMA_EOF;
		TRACE("NVT UNZIP DECODE DONE\n");
	}
	
	if(value & HAL_GZIP_SRC_ADMA_EOF){
		clr_intr |= HAL_GZIP_SRC_ADMA_EOF;
	}
	
	if(value & HAL_GZIP_SRC_ADMA_STOP){
		pr_err("NVT UNZIP: SRC STOP!!!!\n");
		nvt_unzip->status |= HAL_STATUS_STOP;
	}
	
	if(value & HAL_GZIP_DST_ADMA_STOP){
		pr_err("NVT UNZIP: SHOULD NOT HAPPEN!!!!\n");
	}

	if(value != clr_intr)
		printk("NVT UNZIP warning: INTR: %x\n", value);	


	//process interrupt

	nvt_unzip_clockgating(0);
	
	if (nvt_unzip->isrfp)
		nvt_unzip->isrfp(err, decompressed, nvt_unzip->israrg);

	//for nvt_unzip ,one decompress will handle 1 interrupt.
	// so we disable interrupt in isr .... to prevent driver state turn to wrong state
	writel(0, nvt_unzip->base + HAL_GZIP_INTERRUPT_EN);	

	nvt_unzip_calls += 1;
	nvt_unzip_nsecs += nvt_unzip_get_nsecs() - nvt_unzip->clock_ns;

	nvt_unzip->decompressed = err ? -abs(err) : decompressed;

	TRACE(" nvt ungzip isr  decompressed:%x err %x intr_de %x \n",nvt_unzip->decompressed, err,decompressed);
	smp_wmb();

	if( value & HAL_GZIP_ERR_EOF){
		clr_intr &= ~HAL_GZIP_ERR_EOF;
	}


	if(clr_intr  ){
		complete(&nvt_unzip->wait);
	}else{
		printk("NVT UNZIP : going\n");
	}
//isr_done:
	writel( value, nvt_unzip->base + HAL_GZIP_INTERRUPT);	
	nvt_unzip_dbg.last_intr_status = value;
	nvt_unzip_dbg.output_size = decompressed;

	TRACE_EXIT();
	return IRQ_HANDLED;
}

struct unzip_buf * unzip_alloc(size_t len)
{
	struct unzip_buf *buf = NULL;
	BUG_ON(!nvt_unzip);

	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);
	{
		int i = 0;
		unsigned long flags;
		spin_lock_irqsave(&nvt_unzip->input_buf_lock, flags);
		for(i = 0; i< HW_MAX_SIMUL_THR;i++){
			if(nvt_unzip->input_buf_status[i]== BUF_FREE)
				break;
		}
		if(i == HW_MAX_SIMUL_THR){
			printk("[ERR_DRV_UNZIP] all input buf is not avail!!!\n");
			for(i = 0; i< HW_MAX_SIMUL_THR;i++){
				printk(" buf %d - %d \n", i,
				nvt_unzip->input_buf_status[i]);
			}
			nvt_unzip_dump();
			BUG_ON(1);
		}
		nvt_unzip->input_buf_status[i] = BUF_BUSY;
		buf = nvt_unzip->input_buf[i ];
		spin_unlock_irqrestore(&nvt_unzip->input_buf_lock, flags);
	}
	buf->size = len;
	TRACE("NVT UNZIP :##check alloc # %s addr: %p len 0x%x \n", __FUNCTION__,buf->vaddr, len);

	return buf;
}
EXPORT_SYMBOL(unzip_alloc);

void unzip_free(struct unzip_buf *buf)
{
	BUG_ON(!nvt_unzip);
	TRACE("### %s %d \n", __FUNCTION__, __LINE__);
	{
		int i = 0;
		unsigned long flags;
		spin_lock_irqsave(&nvt_unzip->input_buf_lock, flags);

		for(i = 0; i < HW_MAX_SIMUL_THR;i++){
			if(buf == nvt_unzip->input_buf[i])
				break;
		}

		if(i == HW_MAX_SIMUL_THR){
			printk("[ERR_DRV_UNZIP] input_buffer %d can't free!!!\n",i);
			nvt_unzip_dump();
			BUG_ON(1);
		}
		if(nvt_unzip->input_buf_status[i] != BUF_BUSY){
			printk("[ERR_DRV_UNZIP] input_buffeer %d status is wrong!!!\n",i);
			nvt_unzip_dump();
			BUG_ON(1);
		}

		nvt_unzip->input_buf_status[i] = BUF_FREE;
		spin_unlock_irqrestore(&nvt_unzip->input_buf_lock, flags);
	}
	if (IS_ERR_OR_NULL(buf))
		return;
	BUG_ON(!nvt_unzip->input_buf);
}
EXPORT_SYMBOL(unzip_free);

/* memory pool and memory */
struct unzip_buf *nvt_inputbuf_alloc(size_t len, struct device *dev)
{
	struct unzip_buf *buf;
	dma_addr_t coh ;
	BUG_ON(!nvt_unzip);
	spin_lock_init(&nvt_unzip->input_buf_lock);
	buf = kmalloc(sizeof(struct unzip_buf), GFP_KERNEL);
	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = dma_alloc_coherent(dev, HW_MAX_IBUFF_SZ, &buf->paddr, GFP_KERNEL  );
	coh = buf->paddr;

	if(buf->vaddr == NULL){
		return ERR_PTR(-ENOMEM);
	}

	buf->size = len;
	buf->__sz = HW_MAX_IBUFF_SZ;

	if (!buf->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}
	TRACE("NVT_UNZIP %s  vaddr %p paddr %x\n", __FUNCTION__,buf->vaddr, buf->paddr );

	return buf;
err:
	dma_free_coherent(dev, HW_MAX_IBUFF_SZ, buf->vaddr, buf->paddr);
	kfree(buf);

	return NULL;
}

static void nvt_inputbuf_free(struct unzip_buf *buf,  struct device *dev)
{
	if (!buf || !buf->__sz)
		return;
	dma_free_coherent(dev, HW_MAX_IBUFF_SZ, buf->vaddr, buf->paddr);
	kfree(buf);
}

/* initial/destroy and interface function. */

int nvt_unzip_decompress_start(void *vbuff, dma_addr_t pbuff,
				      int ilength, dma_addr_t *opages,
				      int npages, unzip_cb_t cb,
				      void *arg, bool last_input)
{
#if 0
	/* Set Src Address
	   WTF? Why we always have extra GZIP_ALIGNSIZE? */
	ilength = ((ilength + GZIP_ALIGNSIZE) / GZIP_ALIGNSIZE)
		* GZIP_ALIGNSIZE;
#endif
	/* Set members */
	TRACE("NVT UNGZIP %s entry \n", __FUNCTION__);
	while(nvt_unzip->status != (HAL_STATUS_STOP)){
		//ndelay(10);
		printk("[NVT UNGZIP] WARNING :nvt_unzip satus should be stop but it is not!!!!!!!\n");
	}
	nvt_unzip->status &= ~(HAL_STATUS_STOP);

	nvt_unzip_dbg.input_size = nvt_unzip->isize = ilength;
	nvt_unzip_dbg.last_intr_status = 0;
	nvt_unzip->vbuff = vbuff;
	nvt_unzip->isrfp = cb;
	nvt_unzip->israrg = arg;
	nvt_unzip->opages_cnt = npages;

	nvt_unzip_clockgating(1);
	LOG_EVENT(PREPARE_DMA_START);
	unzip_prepare_src_adma(ilength, pbuff, last_input);
	{
		unzip_prepare_dst_adma(npages, opages);
	}
	LOG_EVENT(PREPARE_DMA_DONE)
	return 0;
}

int unzip_decompress_async(struct unzip_buf *buf, int off,
			       struct page **opages, int npages,
			       unzip_cb_t cb, void *arg,
			       bool may_wait, enum hw_iovec_comp_type comp_type)
{

	dma_addr_t pages_phys[npages];
	int   err = unzip_hal_reset(comp_type, may_wait);
	int i = 0;
	int last_input = 1;

	TRACE("NVT UNGZIP %s entry \n", __FUNCTION__);
	if (err)
		goto err_init;
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
	err = nvt_unzip_decompress_start(buf->vaddr + off, buf->paddr + off,
					 buf->size, pages_phys, npages,
					 cb, arg, last_input);

	if (err)
		goto err;

	return err;

err:
	up(&nvt_unzip->sema);
	{
		int j = 0;

		for (j = 0; j < i; j++)
			dma_unmap_page(nvt_unzip->dev, pages_phys[j],
					PAGE_SIZE, DMA_FROM_DEVICE);
	}
err_init:
	return err;
}
EXPORT_SYMBOL(unzip_decompress_async);

/**
 * Must be called from the same task which has been started decompression
 */
int unzip_decompress_wait(struct unzip_buf *buf,
		int npage, struct page **opages, unsigned char *hash)
{
	int i, ret = 0, patchbuf_out = 0;
	TRACE_ENTRY();
	wait_for_completion(&nvt_unzip->wait);
	
	smp_rmb();
	ret = xchg(&nvt_unzip->decompressed, 0);
	TRACE(">>%s decompressd size: %x\n", __FUNCTION__, ret);	
	//LOG_EVENT(OPAGE_DMA_FINISH_START);	
	{
		char str[32];
		sprintf(str, "OPAGE_DMA_FINISH_START%d", ret);
		LOG_EVENT_STR(str);
	}
	patchbuf_out = ret;

	for (i = 0; i < nvt_unzip->opages_cnt; i++){
		dma_unmap_page(nvt_unzip->dev, nvt_unzip->opages[i],
			       PAGE_SIZE, DMA_FROM_DEVICE);
	}

	LOG_EVENT(OPAGE_DMA_FINISH_END);	

	LOG_EVENT(DSCR_FINISH_START);
	unzip_finish_src_adma();
	unzip_finish_dst_adma();
	LOG_EVENT(DSCR_FINISH_END);

	up(&nvt_unzip->sema);

    #if defined(CONFIG_NVT_HW_SHA256)
    if(hash)
        hw_sha256_wait(hash);
    #endif
	TRACE_EXIT();
	return ret;
}
EXPORT_SYMBOL(unzip_decompress_wait);

/*hw1 support for gzip/zlib format only 
since the HW_IOVEC_COMP_ZLIB make novatke hwunzip auto-detection */
int nvt_unzip_decompress_sync(void *ibuff, int ilength, struct page **opages,
			      int npages, bool may_wait,  enum hw_iovec_comp_type comp_type)
{
	dma_addr_t ibuff_phys;
	struct unzip_buf buf;
	int ret;
	
	TRACE("UNZIP : %s start\n",__FUNCTION__);
	if (!nvt_unzip) {
		pr_err("NVT Unzip Engine is not Initialized!\n");
		return -EINVAL;
	}
	
	nvt_unzip_dbg.input_buf_adr = ibuff;
	/* Prepare input buffer */
	ibuff_phys = dma_map_single(nvt_unzip->dev, ibuff,
				    ilength, DMA_TO_DEVICE);

	if (dma_mapping_error(nvt_unzip->dev, ibuff_phys)) {
		dev_err(nvt_unzip->dev, "unable to map input bufferr\n");
		return -EINVAL;
	}

	buf = (struct unzip_buf) {
		.vaddr = ibuff,
		.paddr = ibuff_phys,
		.size  = ilength
	};

	/* Start decompression */
	ret = unzip_decompress_async(&buf, 0, opages, npages,
					 NULL, NULL, may_wait, true, comp_type);
	if (!ret) {
		/* Kick decompressor to start right now */
		unzip_update_endpointer();

		/* Wait and drop lock */
		ret = unzip_decompress_wait(NULL, npages, opages, NULL);
	//check error handling
	}else{
		dev_err(nvt_unzip->dev, "nvt gzip: nvt_unzip_decompress_async reset error\n");
	}
	dma_unmap_single(nvt_unzip->dev, ibuff_phys,
			 ilength, DMA_TO_DEVICE);
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


void nvt_unzip_dbg_init(void){
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
	//int affinity = 0;
	//void *buf;

#ifdef CONFIG_OF	
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif

	nvt_unzip = devm_kzalloc(dev, sizeof(struct nvt_unzip_t), GFP_KERNEL);
	if(nvt_unzip == NULL)
	{
		dev_err(dev, "cannot allocate memory!!!\n");
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
	
	nvt_unzip->base = devm_request_and_ioremap(&pdev->dev, res);

	if (nvt_unzip->base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, nvt_unzip);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, nvt_unzip);
		return -ENODEV;
	}

	ret = request_irq(irq , nvt_unzip_isr, 0, dev_name(dev), nvt_unzip);

	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d\n", irq);
		devm_kfree(dev, nvt_unzip);
		return -ENODEV;
	}

#if 0
#ifndef CONFIG_OF
	affinity = 1;
#else
	if(!of_property_read_u32(dev->of_node, "irq-affinity", &affinity))
#endif
		if(num_online_cpus() > affinity) {
			irq_set_affinity(irq, cpumask_of(affinity));
		}
#endif
#if 0
#ifdef CONFIG_OF
	nvt_unzip->clk = clk_get(dev, "gzip_clk");
	if (IS_ERR(nvt_unzip->clk)) {
		dev_err(dev, "cannot find gzip_clk: %ld!\n",
			PTR_ERR(nvt_unzip->clk));
		nvt_unzip->clk = NULL;
	}
	else
		clk_prepare(nvt_unzip->clk);
	nvt_unzip->rst = clk_get(dev, "gzip_rst");
	if (IS_ERR(nvt_unzip->rst)) {
		dev_err(dev, "cannot find gzip_rst: %ld!\n",
			PTR_ERR(nvt_unzip->rst));
		nvt_unzip->rst = NULL;
	}
	else
		clk_prepare(nvt_unzip->rst);
#endif
#endif

	dev->dma_mask = &ungzip_dmamask;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	platform_set_drvdata(pdev, (void *) nvt_unzip);
	{
		int i = 0;
		for(i = 0; i < HW_MAX_SIMUL_THR; i++){
			nvt_unzip->input_buf[i] = nvt_inputbuf_alloc( HW_MAX_IBUFF_SZ , dev );
			if ( IS_ERR_OR_NULL(nvt_unzip->input_buf[i])) {
				int j = 0;
				
				dev_err(dev, "cannot allocate input buffer for nvt_unzip\n");
				for(j = (i-1) ;j>=0;j--){
					nvt_inputbuf_free(nvt_unzip->input_buf[i],dev);
				}

				return -ENOMEM;
			}
			nvt_unzip->input_buf_status[i] = BUF_FREE;
		}
	}	
	nvt_unzip->data_input =devm_kzalloc(dev, sizeof(struct st_src_adma), GFP_KERNEL);
	if(nvt_unzip->data_input == NULL){
		dev_err(dev, "cannot allocate input description buffer\n" );
		devm_kfree(dev, nvt_unzip);
	}
	nvt_unzip->data_output =devm_kzalloc(dev, sizeof(struct st_src_adma), GFP_KERNEL);
	if(nvt_unzip->data_output == NULL){
		dev_err(dev, "cannot allocate output description buffer\n" );
		devm_kfree(dev, nvt_unzip->data_input);
		devm_kfree(dev, nvt_unzip);
	}

	/* set platform  device information*/
	nvt_unzip->dev = dev;
	dev_info(dev, "Registered Novatek unzip driver(%x) DBG: %p!! orig: %p hwthr:%d \n", nvt_unzip->base, &nvt_unzip_dbg, nvt_unzip, HW_MAX_SIMUL_THR);
	init_log();
	init_timedifflog();
	nvt_unzip_dbg_init();
	return 0;
}

static int nvt_unzip_remove(struct platform_device *pdev)
{
	int i = 0;
	for(i = 0; i< HW_MAX_SIMUL_THR;i++)
		nvt_inputbuf_free(nvt_unzip->input_buf[i], &pdev->dev);
#if 0
#ifdef CONFIG_OF
	if (!IS_ERR_OR_NULL(nvt_unzip->clk))
		clk_put(nvt_unzip->clk);
	if (!IS_ERR_OR_NULL(nvt_unzip->rst))
		clk_put(nvt_unzip->rst);
#endif
#endif
	nvt_unzip_clk_free();
	devm_kfree(&pdev->dev, nvt_unzip->data_input);
	devm_kfree(&pdev->dev, nvt_unzip->data_output);
	devm_kfree(&pdev->dev, nvt_unzip);
	
	return 0;
}

static const struct of_device_id nvt_unzip_dt_match[] = {
	{ .compatible = "novatek,nvt-unzip", },
	{},
};
MODULE_DEVICE_TABLE(of, nvt_unzip_dt_match);

static struct platform_driver nvt_unzip_driver = {
	.probe		= nvt_unzip_probe,
	.remove		= nvt_unzip_remove,
	.driver = {
		.name	= "nvt-unzip",
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

	if (count < max_out)
		return -EINVAL;
	if (!nvt_file->in_sz)
		return 0;

	/* Yes, right, no synchronization here.
	 * nvt unzip sync has its own synchronization, so we do not care
	 * about corrupted data with simultaneous read/write on the same
	 * fd, we have to test different scenarious and data corruption
	 * is one of them */
	ret = nvt_unzip_decompress_sync(nvt_file->in_buff,
					ALIGN(nvt_file->in_sz, 8),
					nvt_file->dst_pages,
					ARRAY_SIZE(nvt_file->dst_pages),
					true, HW_IOVEC_COMP_GZIP);
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
        volatile unsigned int val;
	if(nvt_unzip->data_input_started){
		val = HAL_GZIP_SRC_ADMA_CONTINUE;
	}else{
		val = HAL_GZIP_SRC_ADMA_START ;//| HAL_GZIP_DES_ADMA_START;//hal_gzip_src_adma_start ;
		nvt_unzip->clock_ns = nvt_unzip_get_nsecs();
	}

	nvt_unzip->data_input_started = 1;
        writel(val, nvt_unzip->base +HAL_GZIP_HW_SETTING);
}

/*generate source descriptor */
static int unzip_prepare_src_adma( unsigned int len, unsigned int addr, bool last_input)
{
	struct st_src_adma * data_input = nvt_unzip->data_input;
	dma_addr_t des_phys = 0;
	int err = 0;
	unsigned int calc_len = 0;
	int unit_num = 0;
	gzip_desc_t * des = NULL;
	int first_des_index = nvt_unzip->data_input_cnt;
	TRACE("%s entry:  len 0x%x  %s\n", __FUNCTION__, len,  last_input ?  "last": "");
	nvt_unzip_dbg.last_input = last_input;
	
#define UNIT_MAX_LEN (1<<15)
	while( (calc_len < len) ){
		bool last_unit = false;

		des = &data_input->des[nvt_unzip->data_input_cnt];
		BUG_ON(!des);
	
		if( (len <= UNIT_MAX_LEN)  || calc_len+UNIT_MAX_LEN>=len)
			last_unit = true;
		
		des->attribute = EN_DESC_VALID | EN_DESC_TRANSFER ;
		if(last_input && last_unit)
			des->attribute |= EN_DESC_END;
		des->len = (last_unit) ? len - calc_len : UNIT_MAX_LEN;
		des->addr = addr + calc_len;

		#ifdef DBG_DESCRIPTOR
		printk(">>>NVT_UNZIP : input des: [%2d]len: 0x%x addr: 0x%x %p attr: 0x%x\n", 
				nvt_unzip->data_input_cnt , des->len, des->addr,nvt_unzip->vbuff, des->attribute);
		#endif
		//writel(data_input->des_phys, nvt_unzip->base + HAL_GZIP_SRC_ADMA_ADDR);
		unit_num++;
		calc_len += UNIT_MAX_LEN;	
		nvt_unzip->data_input_cnt += 1;	
		nvt_unzip_dbg.data_input_cnt = nvt_unzip->data_input_cnt ;	
	}
	
	if(last_input ){
		if(!(des->attribute & EN_DESC_END)){
			dev_err(nvt_unzip->dev,"source descriptor end check fail...last_input: %d length : %d source :0x%x \n", last_input, len, addr );
			BUG_ON(!(des->attribute & EN_DESC_END));
		}
	}

	//des_phys = dma_map_single(nvt_unzip->dev, &data_input->des[first_des_index], sizeof(gzip_desc_t) * unit_num, DMA_TO_DEVICE);
	des_phys = dma_map_single(nvt_unzip->dev, data_input->des, sizeof(gzip_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
	if (dma_mapping_error(nvt_unzip->dev, des_phys)) {
		dev_err(nvt_unzip->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}

	if(first_des_index == 0){
		data_input->des_phys = des_phys;
		writel(data_input->des_phys, nvt_unzip->base + HAL_GZIP_SRC_ADMA_ADDR);
	}
	return err;
	
}

static void unzip_finish_src_adma(void)
{
	if(nvt_unzip->data_input->des_phys ){
		//dma_unmap_single(nvt_unzip->dev, nvt_unzip->data_input->des_phys,   sizeof(gzip_desc_t) *nvt_unzip->data_input_cnt, DMA_TO_DEVICE);
		dma_unmap_single(nvt_unzip->dev, nvt_unzip->data_input->des_phys,   sizeof(gzip_desc_t) *MAX_ADMA_NUM, DMA_TO_DEVICE);
	}
	//nvt_unzip->data_input->des_phys = 0;
	//nvt_unzip->data_input_cnt = 0;
}

static void unzip_issue_dst_adma(void)
{
        volatile unsigned int val;
	if(nvt_unzip->data_output_started)
		val =  HAL_GZIP_DES_ADMA_CONTINUE;
	else
		val =  HAL_GZIP_DES_ADMA_START;
	nvt_unzip->data_output_started = 1;
	writel(val, nvt_unzip->base + HAL_GZIP_HW_SETTING);
}


void nvt_bouncebuf_dst_desc_destroy(void)
{

	if(nvt_unzip->data_output->des_phys)
		//dma_unmap_single(nvt_unzip->dev, nvt_unzip->data_output->des_phys, sizeof(gzip_desc_t) *nvt_unzip->data_output_cnt, DMA_TO_DEVICE);
		dma_unmap_single(nvt_unzip->dev, nvt_unzip->data_output->des_phys, sizeof(gzip_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
			
}
static int unzip_prepare_dst_adma(int pages, dma_addr_t *opages/*, unsigned short * attrs*/)
{
	struct st_dst_adma * data_output = nvt_unzip->data_output;
	int pre_data_output_cnt = nvt_unzip->data_output_cnt;
	gzip_desc_t * dst_des = &data_output->des[pre_data_output_cnt];
	int i = 0, err = 0;

	for(i =  pre_data_output_cnt; i < pages + pre_data_output_cnt ; i++){
		int attr = EN_DESC_VALID | EN_DESC_TRANSFER;
		dst_des[i].attribute = attr;
		dst_des[i].len = PAGE_SIZE;
		nvt_unzip->opages[i] = dst_des[i].addr = opages[i];
		//already flush?
		#ifdef DBG_DESCRIPTOR
		printk("NVT_UNZIP : out des[%d]: len: %x addr: %x  , attr: %x\n", i, dst_des[i].len, dst_des[i].addr, dst_des[i].attribute);
		#endif
	}
	
	//data_output->des_phys = dma_map_single(nvt_unzip->dev, dst_des, sizeof(gzip_desc_t) *nvt_unzip->opages_cnt, DMA_TO_DEVICE);
	data_output->des_phys = dma_map_single(nvt_unzip->dev, data_output->des, sizeof(gzip_desc_t) *MAX_ADMA_NUM, DMA_TO_DEVICE);
	if (dma_mapping_error(nvt_unzip->dev, data_output->des_phys)) {
		dev_err(nvt_unzip->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}

	if(nvt_unzip->data_output_cnt == 0)
		writel(data_output->des_phys, nvt_unzip->base + HAL_GZIP_DES_ADMA_ADDR);
	nvt_unzip->data_output_cnt += pages;
	nvt_unzip_dbg.data_output_cnt = nvt_unzip->data_output_cnt ;

	return err;
}

static void unzip_finish_dst_adma(void)
{
	nvt_bouncebuf_dst_desc_destroy();
	//nvt_unzip->data_output->des_phys = 0;
	//nvt_unzip->data_output_cnt = 0;
	
}

int unzip_hal_reset(enum hw_iovec_comp_type comp_type, bool may_wait)
{
	switch(comp_type){
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
	if (!may_wait && down_trylock(&nvt_unzip->sema)){
		printk("NVT Unzip BUSY!\n");
		return -EBUSY;
	}
	else if (may_wait)
		down(&nvt_unzip->sema);
 
	TRACE("NVT UNGZIP : %s \n", __FUNCTION__);
	//reset
	INIT_COMPLETION(nvt_unzip->wait); 
	writel( HAL_GZIP_HW_RESET, nvt_unzip->base + HAL_GZIP_HW_SETTING);
	writel( HAL_GZIP_CLR_SRC_CNT, nvt_unzip->base + HAL_GZIP_HW_SETTING);
	writel( HAL_GZIP_CLR_DST_CNT, nvt_unzip->base + HAL_GZIP_HW_SETTING);
	
	//hw config
	switch(comp_type){
		case HW_IOVEC_COMP_GZIP:
		case HW_IOVEC_COMP_ZLIB:
		//hw can auto dection RFC_1950 zlib and RFC_1952 gzip
		//for other type HW_IOVEC_COMP_LZO/HW_IOVEC_COMP_MAX
		writel( 0, nvt_unzip->base + HAL_GZIP_HW_CONFIG);
			break;
		case HW_IOVEC_COMP_UNCOMPRESSED:
		default:
		//device use setting of auto-detect 
		writel( HAL_GZIP_BYPASS, nvt_unzip->base + HAL_GZIP_HW_CONFIG);
		break;
	}

	nvt_unzip->comp_type = comp_type;

	//clear intr
	writel( ERROR_BTIS| CONTROL_BITS_DST,nvt_unzip->base + HAL_GZIP_INTERRUPT_EN );
	//set timeout
	writel( -1,nvt_unzip->base + HAL_GZIP_TIMEOUT );
	nvt_unzip->data_input_started = nvt_unzip->data_output_started = 0;
	nvt_unzip->status = HAL_STATUS_STOP;
	nvt_unzip->data_input_cnt = nvt_unzip->data_output_cnt = 0;
	nvt_unzip_dbg.data_output_cnt = nvt_unzip_dbg.data_input_cnt = 0;
	
	memset(nvt_unzip->data_input->des, 0x0, sizeof(gzip_desc_t)*MAX_ADMA_NUM);
	memset(nvt_unzip->data_output->des, 0x0, sizeof(gzip_desc_t)*MAX_ADMA_NUM);
	return 0;
}

