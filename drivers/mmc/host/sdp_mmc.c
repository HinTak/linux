/*****************************************************************************
 *
 *		MMC Host Controller Driver for Samsung DTV/BD Platform
 *		created by tukho.kim@samsung.com
 *
 ****************************************************************************/

/*
 * 2010/10/25, youngsoo0822.lee : bug fix, resume and HW locked error 101025
 * 2011/01/18, tukho.kim: bug fix, not return error status when cause error in data transfer, 110118
 * 2011/07/20, tukho.kim: bug fix, modify for gic interrupt controller
 * 2011/12/02, tukho.kim: bug fix, modify for mp core sync 111202
 * 2011/12/04, tukho.kim: bug fix, data stop command caused 2 interrupts 111204
 * 2011/12/07, tukho.kim: bug fix, DTO and CMD Done correlation error, data stop command caused 2 interrupts 111207
 * 2012/03/12, drain.lee: porting for kernel-3.0.20
 * 2012/03/20, drain.lee: move board specipic init code
 * 2012/04/17, drain.lee:	add IP ver.2.41a set CMD use_hold_reg
 							add 2.41a Card Threshold Control Register Setting
 							some error fix.
 * 2012/05/25, drain.lee:	change, only last desc be occur IDMAC RX/TX interrupt.
 							change, ISR sequence change.
 							add, print debug dump in error.
 * 2012/06/20, drain.lee:	use state machine(enum sdp_mmch_state).
 							recovery in error state.
 * 2012/06/22, drain.lee:	move clk delay restore code to sdpxxxx.c file.
 * 2012/07/16, drain.lee:	add PIO Mode. add state event field.
 * 2012/07/16, drain.lee:	add platdata fifo_depth fidld.
 * 2012/07/19, drain.lee:	bug fix, FIFO reset after PIO data xmit.
 * 2012/07/30, drain.lee:	fix, HIGHMEM buffer support.
 * 2012/07/31, drain.lee:	change to use memcpy to PIO copy function.
 * 2012/08/09, drain.lee:	fixed, PIO debug code error and edit debug dump msg.
 * 2012/09/10, drain.lee:	bug fix, lost DTO Interupt in DMA Mode.
 * 2012/09/13, drain.lee:	bug fix, data corrupted in DMA Mode.
 * 2012/09/21, drain.lee:	change, used to calculate mts value.
 * 2012/09/26, drain.lee:	bug fix, mmc NULL pointer dereference.
 * 2012/10/12, drain.lee:	add, try to recovery in HTO
 * 2012/10/22, drain.lee:	add Debug dump for dma desc
 * 2012/11/06, drain.lee:	export debug dump fumc for mmc subsystem.
 * 2012/11/19, drain.lee:	change selecting xfer mode(PIO/DMA) in prove
 * 2012/11/20, drain.lee:	move DMA code and change pio function name
 * 2012/11/21, drain.lee:	when dma is done checking owned bit '0' in descriptor.
 * 2012/11/23, drain.lee:	change warning msg level(check own bit)
 * 2012/11/27, drain.lee:	bug fix, NULL pointer dereference in mmch_dma_intr_dump
 * 2012/12/03, drain.lee:	bug fix, check busy signal in R1b
 * 2012/12/04, drain.lee:	fix prevent defect and compile warning
 * 2012/12/12, drain.lee:	move dma mask setting to sdpxxxx.c file and support DMA_ADDR_T 64bit
 * 2012/12/15, drain.lee:	fix, change jiffies to ktime_get() in timeout code.
 * 2012/12/17, drain.lee:	change init sequence in prove.
 * 2012/12/21, drain.lee:	fix compile warning.
 * 2013/01/21, drain.lee:	change print msg level. add udelay in busy check loop.
 * 2013/01/28, drain.lee:	bug fix, mmc request return error at not idle.
 							so remove temporary spin unlock in mmch_request_end()
 * 2013/02/01, drain.lee:	fix, prevent defect(Unintentional integer overflow, Uninitialized scalar variable)
 * 2013/02/27, drain.lee:	fix, PIO dcache flush error
 * 2013/05/03, drain.lee:	support pre/post request ops
 * 2013/05/07, drain.lee:	add auto stop cmd config
 * 2013/05/27, drain.lee:	support SDIF and MMC HS200
 * 2013/06/03, drain.lee:	support Pre-defined multiple read/write
 * 2013/06/24, drain.lee:	support OF.
 * 2013/06/26, drain.lee:	fix.. OF.
 * 2013/07/29, drain.lee:	change tuning code.
 * 2013/07/29, drain.lee:	bug fix restoreregs
 * 2013/07/30, drain.lee:	move restoreregs
 * 2013/08/01, drain.lee:	add sdp_unzip decompress
 							state machine run in tasklet
 							add irq affinity in dt
 * 2013/08/02, drain.lee:	revert.. state machine run in irq
 * 2013/08/05, drain.lee:	bugfix, add IDMAC reset and change reset seq
 * 2013/08/13, drain.lee:	bug fix, h/w squashFS2 support.
 * 2013/08/13, drain.lee:	change HS200 tuning code.
 * 2013/09/17, drain.lee:	add Golf-S fixup.
 * 2013/09/25, drain.lee:	add Golf-S fixup(drv delay tuning)
 * 2013/09/26, drain.lee:	add Fox-P HW SquashFS2 Mode
 * 2013/10/09, drain.lee:	add SDIF capabilities for DT
 * 2013/10/14, drain.lee:	fix IDMAC stop seq. add idmac interrupt accumulated
 * 2013/10/30, drain.lee:	SDIF cd, ro function
 * 2013/11/04, drain.lee:	fix Golf-S fixup(div 7) and add HW reset function
 * 2013/11/06, drain.lee:	add capabilities hw-reset
 * 2013/11/29, drain.lee:	add sw reset in tuning code and bugfix dma_unmap
 * 2013/12/16, drain.lee:	change Golf-S fixup for Rev1
 * 2013/12/20, drain.lee:	add sw timeout state
 * 2013/12/23, drain.lee:	add process pending interrupt in timeout.
 * 2013/12/23, drain.lee:	change Fox-B own by dma timeout value.
 * 2013/12/24, drain.lee:	bugfix for Golf-S fixup for Rev1
 * 2013/12/31, drain.lee:	bugfix sw timeout callback
 * 2014/01/04, drain.lee:	bugfix IRQ_NONE check
 * 2014/01/09, seungjun.heo:	bugfix execute tunning CSD error.
 * 2014/01/14, drain.lee:	add Golf-P UD fixup.
 * 2014/01/27, drain.lee:	add Golf-S drv retuning code.
 * 2014/01/28, drain.lee:	bugfix stop cmd wating.
 * 2014/02/11, drain.lee:	remove rev0 code.
 * 2014/02/19, drain.lee:	add Golf-S set low speed when read csd.
 * 2014/02/25, drain.lee:	change Golf-S drv tuning CMD(program CSD to program CID)
 * 2014/03/17, drain.lee:	change Fox-B Owned by dma dump msg
 * 2014/04/01, drain.lee:	porting Hawk Kernel(linux 3.10)
 * 2014/04/04, drain.lee:	support 64bit address for IDMAC
 * 2014/04/08, drain.lee:	remove DMA desc dump code
 * 2014/06/17, drain.lee:	support new mmc subsystem and add HS400 timing
 * 2014/06/18, roman.pen:	sdp_unzip refactoring:
 *				  pass unzip completion callback directly
 *				  to decompression call
 * 2014/06/16, roman.pen:       unify async decomopression logic for both
 *                              platforms: fox-b and golf
 * 2014/07/03, drain.lee:	fix PIO Mode, and fix IOS clock &timing and etc.
 * 2014/07/11, drain.lee:	update tuning logic for hs400.
 * 2014/08/01, drain.lee:	add debug fs.
 * 2014/08/06, drain.lee:	fix Hawk-P initreg fixup
 * 2014/08/13, drain.lee:	fix debug fs. bug fix.
 * 2014/08/13, drain.lee:	add mmc moniter dump.
 * 2014/08/22, drain.lee:	fix compile warning.
 * 2014/08/27, drain.lee:	reset bug fix, fix debug print, fix compile warning.
 * 2014/09/26, drain.lee:	fix compile warning(W=123).
 * 2014/10/07, drain.lee:	tuning only use CMD in hs400 Mode, fix debug info
 * 2014/10/08, drain.lee:	add Hawk-M evt0 fixup
 * 2014/10/10, drain.lee:	fix debugfs RDQS set, add Hawk-P EVT0 HS400 150MHz drv fixup
 * 2014/10/10, drain.lee:	add EW sam/drv delay debugfs
 * 2014/10/14, drain.lee:	bugfix EW mmc delay debugfs
 * 2014/10/17, drain.lee:	add debug msg for request busy error
 * 2014/10/23, drain.lee:	bugfix race in mmc state machine(by roman.pen)
 * 2014/10/27, drain.lee:	add fixup, vender specific rdqs setting for hawk-p
 * 2014/11/10, drain.lee:	support HS400 mode RDQS tune
 * 2014/11/17, drain.lee:	add fifo reset in debugfs rdqs set
 * 2014/11/25, drain.lee:	add hs400 cmd tuning property
 * 2014/12/08, drain.lee:	add PoN dts property
 * 2014/12/11, drain.lee:	fix possible stack overflow
 * 2014/12/12, drain.lee:	use tasklet, timeout timer
 * 2014/12/17, drain.lee:	increase sw timeout value
 * 2014/12/18, drain.lee:	fix debug dump, fix polling timer ctrl
 * 2014/12/22, drain.lee:	fix prevent waring
 * 2014/12/26, drain.lee:	fix timeout callback running in irq disabled
 * 2015/01/05, drain.lee:	support polling request func
 * 2015/01/07, drain.lee:	add polling timer debug print
 * 2015/01/08, drain.lee:	fix Hawk-A SW timeout value
 * 2015/01/09, drain.lee:	add irq_affinity_mask property, increase write polling time.
 * 2015/01/12, drain.lee:	fix W=123 compile warning.
 * 2015/01/20, drain.lee:	increase sw timeout value(data)
 * 2015/01/29, drain.lee:	remove reg dump in polling req
 * 2015/03/05, drain.lee:	fix prevent warning
 * 2015/03/17, drain.lee:	fix polling request fail
 * 2015/06/23, jk97.son:	Add functions for tuning(drv, sam, rdqs)
 * 2015/06/24, drain.lee:	remove CMD12(STOP) in tuning
 * 2015/06/29, jk97.son : 	add code to save successed rdqs value
 * 2015/07/02, jk97.son :	add functions for seding error 
 * 2015/07/17, jk97.son :   add read/write pattern functions & get/set strength value
 * 2015/11/27, jk97.son :	add writeboot fuction for ASB board test
 * 2015/12/24, jk97.son :	reduce log on request function, bugfix(rdqs, writeboot on debugfs), change log for checking isr_integer.
 * 2015/12/29, jk97.son :	fix mmch_timeout_callback function to prevent simultaneous access of timer and tasklet
 * 2016/04/21, jk97.son :	changed pointer form of host_cookie in form of integer. make macro to shift rdqs_value.
 * 2016/06/13, jk97.son :	added mmch_ew_dbg_clk_phase_fops to change clk phase in debugfs function.
 * 2016/06/24, jk97.son :	remove state_lock in request function.
 * 2016/07/02, jk97.son :	add sample_shift function to change sample_delay when response crc & error is gernerated for jazz
 * 2016/07/02, jk97.son :	changed pointer form of host_cookie in form of integer. make macro to shift rdqs_value.
 * 2016/07/26, jk97.son :	add MMC_CAP_ERASE for trim
 * 2016/07/26, jk97.son :	mask MMC_CAP_WAIT_WHILE_BUSY to ignore checking busy_state on kernel
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>


#include <linux/err.h>
#include <linux/cpufreq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/random.h>

#include <asm/dma-mapping.h>
#include <asm/dma.h>
#include <asm/cacheflush.h>
#include <asm/div64.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include "sdp_mmc.h"
#include <linux/seq_file.h>
//#include <mach/sdp_unzip.h>

#define DRIVER_MMCH_NAME "sdp-mmc"
#define DRIVER_MMCH_VER  "160726(add function for error)"

#define MMCH_CMD_MAX_TIMEOUT	(1000)/* ms. if cmd->cmd_timeout_ms == 0, using this value */

#define MMCH_USE_PRE_POST_REQ		// 130503, enable pre/post request
#define MMCH_USE_SW_TIMEOUT

//#define MMCH_TEST_SW_TIMEOUT//for debug

#define MMCH_PRINT_LEVEL	KERN_DEBUG
//#define MMCH_PRINT_ISR_TIME //for degug
//#define MMCH_PRINT_REQUEST_TIME //for degug
//#define MMCH_PRINT_STATE //for degug

//#define MMCH_DEBUG 	// Debug print on

#ifdef MMCH_DEBUG
#define MMCH_DBG(fmt, args...)		printk(KERN_DEBUG fmt,##args)
#define MMCH_PRINT_STATUS \
		printk( "[%s:%d] rintsts=%x, status=%x\n", \
				__FUNCTION__, __LINE__,\
				mmch_readl(sdpmmch->base+MMCH_RINTSTS), \
				mmch_readl(sdpmmch->base+MMCH_STATUS))
#define MMCH_FLOW(fmt,args...)		printk("[%s]" fmt,__FUNCTION__,##args)
#else
#define MMCH_DBG(fmt, args...)
#define MMCH_PRINT_STATUS
#define MMCH_FLOW(fmt,args...)
#endif

#define MMCH_INFO(fmt,args...)		printk(KERN_INFO"[MMCH_INFO]" fmt,##args)

#define TO_SRTING(x) #x

#define SG_NUM	(MMCH_DESC_NUM)

#define sdp_mmch_rdqs_shift(ret) (ret<<2)

#ifdef CONFIG_SDP_BUSERR
	int sdp_buserr_show(void);
	void sdp_busmon_show(void);
#endif

int sw_timeout_count;

static unsigned int request_warn_msg=1; 

static int sdp_mmch_host_reset(SDP_MMCH_T *sdpmmch);

static void mmch_state_machine(SDP_MMCH_T *sdpmmch,
			       unsigned long intr_status);
static int mmch_dbg_prepare_mrq(struct mmc_card *card, struct mmc_request *mrq, u32 blk_addr, u32 blk_count, int is_write);


static int sdp_mmch_regset(SDP_MMCH_T *sdpmmch, struct sdp_mmch_reg_set *regset);

static inline u32 mmch_readl(const volatile void __iomem *addr)
{
	return readl(addr);
}

static inline void mmch_writel(const u32 val, volatile void __iomem *addr)
{
	writel(val, addr);
}

static unsigned long
mmch_read_ip_version(SDP_MMCH_T *sdpmmch) {
	return mmch_readl(sdpmmch->base + MMCH_VERID);
}

#if defined(CONFIG_ARCH_SDP1803)
int sdp_mmch_pll_counter(SDP_MMCH_T *sdpmmch){
	int temp = 0;
	int clk_v = 0;

	writel(0x800, v_pll_clk_cnt_base);
	temp = (readl(v_pll_clk_cnt_base) & (~0xFF0F));
	writel(temp | 0x603, v_pll_clk_cnt_base+0x20);
	mdelay(1);

	temp = readl(v_pll_clk_cnt_base+0x4);
	dev_err(&sdpmmch->pdev->dev, "pll clock counter : 0x%x\n", temp);

	return 0;
}
#endif

inline static const char *
mmch_state_string(enum sdp_mmch_state state)
{
	switch(state)
	{
		case MMCH_STATE_IDLE:
			return "IDLE";

		case MMCH_STATE_SENDING_SBC:
			return "SENDING_SBC";

		case MMCH_STATE_SENDING_CMD:
			return "SENDING_CMD";

		case MMCH_STATE_SENDING_DATA:
			return "SENDING_DATA";

		case MMCH_STATE_PROCESSING_DATA:
			return "PROCESSING_DATA";

		case MMCH_STATE_SENDING_STOP:
			return "SENDING_STOP";

		case MMCH_STATE_DECOMPRESSING_DATA:
			return "DECOMPRESSING_DATA";

		case MMCH_STATE_REQUEST_ENDING:
			return "REQUEST_ENDING";

		case MMCH_STATE_ERROR_CMD:
			return "ERROR_CMD";

		case MMCH_STATE_ERROR_DATA:
			return "ERROR_DATA";

		case MMCH_STATE_SW_TIMEOUT:
			return "SW_TIMEOUT";

		default:
			return "Unknown";
	}
}


static void mmch_register_dump_notitle(SDP_MMCH_T * sdpmmch)
{
	MMCH_DMA_DESC_T* p_desc = sdpmmch->p_dmadesc_vbase;
	u32 idx=0;
	u32 reg_dump[0xa0/4];
	u32 reg_dump_ext[0x160/4];

	for(idx = 0; idx < ARRAY_SIZE(reg_dump); idx++) {
		reg_dump[idx] = mmch_readl(sdpmmch->base + (idx*4));
	}

	if( (mmch_read_ip_version(sdpmmch)&0xFFFF) >= 0x270a) {
		for(idx = 0; idx < ARRAY_SIZE(reg_dump_ext); idx++) {
			reg_dump_ext[idx] = mmch_readl(sdpmmch->base + 0xA0 + (idx*4));
		}
	}

	for(idx = 0; idx < ARRAY_SIZE(reg_dump); idx+=4) {
		dev_err(&sdpmmch->pdev->dev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)sdpmmch->mem_res->start)+(idx*4),
				reg_dump[idx+0],
				reg_dump[idx+1],
				reg_dump[idx+2],
				reg_dump[idx+3]
			  );
	}

	/* if ip version is 270a, extend regdump */
	if( (mmch_read_ip_version(sdpmmch)&0xFFFF) >= 0x270a) {

		idx = 0x0;/* 0xA0 */
		dev_err(&sdpmmch->pdev->dev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)sdpmmch->mem_res->start)+0xA0+(idx*4),
				reg_dump_ext[idx+0],
				reg_dump_ext[idx+1],
				reg_dump_ext[idx+2],
				reg_dump_ext[idx+3]
			  );
		idx = (0xB0-0xA0)/4;/* 0xB0 */
		dev_err(&sdpmmch->pdev->dev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)sdpmmch->mem_res->start)+0xA0+(idx*4),
				reg_dump_ext[idx+0],
				reg_dump_ext[idx+1],
				reg_dump_ext[idx+2],
				reg_dump_ext[idx+3]
			  );
		idx = (0x100-0xA0)/4;/* 0x100 */
		dev_err(&sdpmmch->pdev->dev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)sdpmmch->mem_res->start)+0xA0+(idx*4),
				reg_dump_ext[idx+0],
				reg_dump_ext[idx+1],
				reg_dump_ext[idx+2],
				reg_dump_ext[idx+3]
			  );
		idx = (0x180-0xA0)/4;/* 0x180 */
		dev_err(&sdpmmch->pdev->dev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)sdpmmch->mem_res->start)+0xA0+(idx*4),
				reg_dump_ext[idx+0],
				reg_dump_ext[idx+1],
				reg_dump_ext[idx+2],
				reg_dump_ext[idx+3]
			  );
	}

	if(sdpmmch->sg_len > 0) {
		dev_err(&sdpmmch->pdev->dev, "---------------------------------------------------------\n");
		dev_err(&sdpmmch->pdev->dev, "    desc index    status     length     buffer1    buffer2\n");
		for(idx=0;idx<sdpmmch->sg_len;idx++){
			dev_err(&sdpmmch->pdev->dev, "%p: [%3u] = 0x%08x 0x%08x 0x%08x 0x%08x\n",
				p_desc, idx, p_desc->status, p_desc->length,
				p_desc->buffer1_paddr, p_desc->buffer2_paddr);
			p_desc++;
		}
	}

	dev_err(&sdpmmch->pdev->dev, "---------------------------------------------------------\n");
	dev_err(&sdpmmch->pdev->dev, "driver version: %s\ndriver state: %s\n", DRIVER_MMCH_VER, mmch_state_string(sdpmmch->state));
	if(sdpmmch->mrq) {
		struct mmc_request *mrq = sdpmmch->mrq;

		dev_err(&sdpmmch->pdev->dev, "Request#%llu in progress(0x%p): CMD%-2d, DATA=%s, STOP=%s\n", sdpmmch->request_count, sdpmmch->mrq, mrq->cmd->opcode,
		mrq->data?(mrq->data->flags&MMC_DATA_READ?"READ":"WRITE"):"NoData",
		mrq->stop?"YES":"NO");
		if(sdpmmch->mrq->sbc) {
			struct mmc_command *cmd = mrq->sbc;

			dev_err(&sdpmmch->pdev->dev, "  SBC  in Request(0x%p): op=%02d, arg=0x%08x, fl=%#x, retries=%d, err=%u\n",
				cmd, cmd->opcode, cmd->arg, cmd->flags, cmd->retries, cmd->error);
		}
		if(sdpmmch->mrq->cmd) {
			struct mmc_command *cmd = mrq->cmd;

			dev_err(&sdpmmch->pdev->dev, "  CMD  in Request(0x%p): op=%02d, arg=0x%08x, fl=%#x, retries=%d, err=%u\n",
				cmd, cmd->opcode, cmd->arg, cmd->flags, cmd->retries, cmd->error);
			if(cmd->opcode == 6) {
				dev_err(&sdpmmch->pdev->dev, "    SWITCH CMD: set %u, index %u, value 0x%02x\n", 
					cmd->arg&0xFFU, (cmd->arg>>16)&0xFFU, (cmd->arg>>8)&0xFFU);
			}
		}
		if(sdpmmch->mrq->data) {
			struct mmc_data *data = mrq->data;

			dev_err(&sdpmmch->pdev->dev, "  DATA in Request(0x%p): bytes %#x, %s, err %u, fl %#x, xfered %#x\n",
				data, data->blksz*data->blocks, (mrq->data->flags&MMC_DATA_READ?"READ":"WRITE"),
				data->error, data->flags, data->bytes_xfered);
			dev_err(&sdpmmch->pdev->dev, "    sdpmmch->mrq_data_size = %#x\n",sdpmmch->mrq_data_size);
		}
		if(sdpmmch->mrq->stop) {
			struct mmc_command *cmd = mrq->stop;

			dev_err(&sdpmmch->pdev->dev, "  STOP in Request(0x%p): op=%02d, arg=0x%08x, fl=%#x, retries=%d, err=%u\n",
				cmd, cmd->opcode, cmd->arg, cmd->flags, cmd->retries, cmd->error);
		}
	}
	if(sdpmmch->cmd) {
		struct mmc_command *cmd = sdpmmch->cmd;

		dev_err(&sdpmmch->pdev->dev, "CMD in progress(0x%p): op=%02d, arg=0x%08x, fl=%#x, retries=%d, err=%u\n",
			cmd, cmd->opcode, cmd->arg, cmd->flags, cmd->retries, cmd->error);
	}
	if(sdpmmch->sg) {
		struct scatterlist *sg = sdpmmch->sg;

		dev_err(&sdpmmch->pdev->dev, "SG list in progress(0x%p): sglist_len=%d, sg offset=%#x, sg len=%#x, pio_offset=%#x\n",
			sg,sdpmmch->sg_len, sg->offset, sg->length, sdpmmch->pio_offset);
	}

	dev_err(&sdpmmch->pdev->dev, "last response for CMD%-2d:\n\t0x%08x 0x%08x 0x%08x 0x%08x\n", (reg_dump[MMCH_STATUS/4]>>11)&0x3F,
		reg_dump[0x30/4], reg_dump[0x34/4], reg_dump[0x38/4], reg_dump[0x3C/4]);

	dev_err(&sdpmmch->pdev->dev, "ISR Interval: %lldns, Req count: %llu\n", timeval_to_ns(&sdpmmch->isr_time_now)
		-timeval_to_ns(&sdpmmch->isr_time_pre), sdpmmch->request_count);

	dev_err(&sdpmmch->pdev->dev, "Int. panding    : MMCH(MINTSTS):0x%08x, IDMAC(IDSTS):0x%08x\n",
		sdpmmch->intr_status, sdpmmch->dma_status);

	dev_err(&sdpmmch->pdev->dev, "Int. accumulated: MMCH:0x%08x, IDMAC:0x%08x\n", sdpmmch->intr_accumulated, sdpmmch->idmac_accumulated);

	dev_err(&sdpmmch->pdev->dev, "host state event: MMCH:0x%08lx, ACCMU:0x%08lx\n", sdpmmch->event, sdpmmch->event_accumulated);

	dev_err(&sdpmmch->pdev->dev, "BIU %dbytes, FIFO %ldbytes, CIU %dbytes, DMA FSM %d\n", reg_dump[MMCH_TBBCNT/4], (MMCH_STATUS_GET_FIFO_CNT(reg_dump[MMCH_STATUS/4]) << sdpmmch->data_shift), reg_dump[MMCH_TCBCNT/4]*((reg_dump[MMCH_UHS_REG/4]&0x10000)?2:1), (reg_dump[MMCH_IDSTS/4]>>13)&0xF);
}

static void mmch_register_dump(SDP_MMCH_T * sdpmmch)
{
	dev_err(&sdpmmch->pdev->dev, "=========================================================\n");
	dev_err(&sdpmmch->pdev->dev, "====================SDP MMC Register DUMP================\n");

	mmch_register_dump_notitle(sdpmmch);

	dev_err(&sdpmmch->pdev->dev, "=================SDP MMC Register DUMP END===============\n");
	dev_err(&sdpmmch->pdev->dev, "=========================================================\n");
}

#ifdef CONFIG_ARCH_SDP1304
static int mmch_bus_moniter_dump(SDP_MMCH_T * sdpmmch)
{
	const u32 SDP_MMCH_MONITER_BASE = 0x10068200;
	u32 idx=0;
	u32 reg_dump[0x80/4];

	u8 * __iomem iomem;
	iomem = ioremap(SDP_MMCH_MONITER_BASE, ARRAY_SIZE(reg_dump));
	for(idx = 0; idx < ARRAY_SIZE(reg_dump); idx++) {
		reg_dump[idx] = mmch_readl(iomem + (idx*4));
	}
	iounmap(iomem);

	
	pr_info("====================SDP MMC Moniter DUMP================\n");
	for(idx = 0; idx < ARRAY_SIZE(reg_dump); idx+=4) {
		pr_info("0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				((u64)SDP_MMCH_MONITER_BASE)+(idx*4),
				reg_dump[idx+0],
				reg_dump[idx+1],
				reg_dump[idx+2],
				reg_dump[idx+3]
			  );
	}
	return 0;
}
#endif

static inline int bscnl_emit(char *buf, int buflen, int rbot, int rtop, int len)
{
	if (len > 0)
		len += scnprintf(buf + len, buflen - len, ",");
	if (rbot == rtop)
		len += scnprintf(buf + len, buflen - len, "%d", rbot);
	else
		len += scnprintf(buf + len, buflen - len, "%d-%d", rbot, rtop);
	return len;
}


static int bitmap_scnlistprintf(char *buf, unsigned int buflen,
        const unsigned long *maskp, int nmaskbits)
{
        int len = 0;
        /* current bit is 'cur', most recently seen range is [rbot, rtop] */
        int cur, rbot, rtop;

        if (buflen == 0)
                return 0;
        buf[0] = 0;

        rbot = cur = find_first_bit(maskp, nmaskbits);
       while (cur < nmaskbits) {
                rtop = cur;
                cur = find_next_bit(maskp, nmaskbits, cur+1);
                if (cur >= nmaskbits || cur > rtop + 1) {
                        len = bscnl_emit(buf, buflen, rbot, rtop, len);
                        rbot = cur;
                }
        }
        return len;
}

static void
mmch_initialization(SDP_MMCH_T *sdpmmch)
{
	u32 regval = 0;
	u32 bit_pos = 0;

	/* IDMAC SW reset */
	mmch_writel(MMCH_BMOD_SWR, sdpmmch->base+MMCH_BMOD);
	while( mmch_readl(sdpmmch->base+MMCH_BMOD) & MMCH_BMOD_SWR );

	/* Host DMA reset */
	mmch_writel(MMCH_CTRL_DMA_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_DMA_RESET );

	/* Host FIFO reset */
	mmch_writel(MMCH_CTRL_FIFO_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_FIFO_RESET );

	/* Host CONTROLLER reset */
	mmch_writel(MMCH_CTRL_CONTROLLER_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_CONTROLLER_RESET );



	/* Now make CTYPE to default i.e, all the cards connected will work in 1 bit mode initially*/
	mmch_writel(0x0, sdpmmch->base+MMCH_CTYPE);

	/* Power up only those cards/devices which are connected
		- Shut-down the card/device once wait for some time
		- Enable the power to the card/Device. wait for some time
	*/
	mmch_writel(0x0, sdpmmch->base+MMCH_PWREN);			// ?????
	mmch_writel(0x1, sdpmmch->base+MMCH_PWREN);

	/* Set up the interrupt mask.
		   - Clear the interrupts in any pending Wrinting 1's to RINTSTS
	   - Enable all the interrupts other than ACD in INTMSK register
	   - Enable global interrupt in control register
	*/
	mmch_writel(0xffffffff, sdpmmch->base+MMCH_RINTSTS);

	regval = MMCH_INTMSK_ALL_ENABLED & ~MMCH_INTMSK_ACD;
	mmch_writel(regval, sdpmmch->base+MMCH_INTMSK);

	/* disable IDMAC all interrupt, enable when using. */
	mmch_writel(0, sdpmmch->base+MMCH_IDINTEN);  				// dma interrupt disable
	mmch_writel(0xFFFFFFFF, sdpmmch->base+MMCH_IDSTS);		// status clear

	/* enable mmc master interrupt */
	mmch_writel(MMCH_CTRL_INT_ENABLE, sdpmmch->base+MMCH_CTRL);

	/* Set Data and Response timeout to Maximum Value*/
	mmch_writel(0xffffffff, sdpmmch->base+MMCH_TMOUT);

	/* Set the card Debounce to allow the CDETECT fluctuations to settle down*/
	mmch_writel(DEFAULT_DEBNCE_VAL, sdpmmch->base+MMCH_DEBNCE);

	if(sdpmmch->fifo_depth == 0) {
		/* read FIFO size */
		sdpmmch->fifo_depth = (u8)(((mmch_readl(sdpmmch->base+MMCH_FIFOTH)>>16)&0xFFF)+1);
	}

	/* Update the watermark levels to half the fifo depth
	   - while reset bitsp[27..16] contains FIFO Depth
	   - Setup Tx Watermark
	   - Setup Rx Watermark
		*/
	bit_pos = (u32) ffs(sdpmmch->fifo_depth/2);
	if( bit_pos == 0 || bit_pos != (u32)fls(sdpmmch->fifo_depth/2) ) {
		dev_err(&sdpmmch->pdev->dev, "fifo depth(%d) is not correct!!", sdpmmch->fifo_depth);
	}

	regval = MMCH_FIFOTH_DW_DMA_MTS(bit_pos-2U) |
			 MMCH_FIFOTH_RX_WMARK((sdpmmch->fifo_depth/2)-1U) |
			 MMCH_FIFOTH_TX_WMARK(sdpmmch->fifo_depth/2);

	mmch_writel(regval, sdpmmch->base+MMCH_FIFOTH);

	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
		regval = MMCH_BMOD_DE |
				 MMCH_BMOD_FB |
				 MMCH_BMOD_DSL(0);
		mmch_writel(regval, sdpmmch->base+MMCH_BMOD);
	}


	/* 2.41a Card Threshold Control Register Setting */
	if( (mmch_read_ip_version(sdpmmch)&0xFFFF) >= 0x240a) {
		mmch_writel(0x02000001, sdpmmch->base+MMCH_CARDTHRCTL);
	}
}


#ifdef CONFIG_OF
static int sdp_mmc_of_do_initregs(struct device *dev)
{
	int psize;
	const u32 *initregs;
	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -1;
	}

	/* Get "initregs" property */
	initregs = of_get_property(dev->of_node, "initregs", &psize);

	if (initregs != NULL) {
		int onesize;
		int i = 0;

		psize /= 4;/* each cell size 4byte */
		onesize = 3;
		for (i = 0; psize >= onesize; psize -= onesize, initregs += onesize, i++) {
			u32 addr, mask, val;
			u8 * __iomem iomem;

			addr = be32_to_cpu(initregs[0]);
			mask = be32_to_cpu(initregs[1]);
			val = be32_to_cpu(initregs[2]);

			iomem = ioremap(addr, sizeof(u32));
			if(iomem) {
#ifdef CONFIG_ARCH_SDP1404
				/* Hawk-P 0x100000A8[15] bit inversion fixup */
				if(addr == 0x100000A8)
					mmch_writel( ((readl(iomem)&~mask) | (val&mask))^0x8000, iomem );
				else
#endif
					mmch_writel( (readl(iomem)&~mask) | (val&mask), iomem );
				dev_printk(KERN_DEBUG, dev,
					"of initreg addr 0x%08x, mask 0x%08x, val 0x%08x, readl 0x%08x\n",
					addr, mask, val, readl(iomem));
				iounmap(iomem);
			} else {
				return -ENOMEM;
			}
		}
	}
	return 0;
}

static int sdp_mmc_of_do_restoreregs(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	SDP_MMCH_T *sdpmmch = NULL;
	int psize;
	const u32 *initregs;

	if(!dev || !pdev) {
		dev_err(dev, "device is null(dev %p, pdev %p)\n", dev, pdev);
		return -EINVAL;
	}

	sdpmmch = platform_get_drvdata(pdev);
	if(!sdpmmch) {
		dev_err(dev, "sdpmmch is null\n");
		return -EINVAL;
	}

	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -1;
	}

	/* Get "restoreregs" property */
	initregs = of_get_property(dev->of_node, "restoreregs", &psize);

	if (initregs != NULL) {
		int onesize;
		int i = 0;

		psize /= 4;/* each cell size 4byte */
		onesize = 2;

		/*check array size */
		if((int) ARRAY_SIZE(sdpmmch->pm_clk_delay) < (psize/onesize)) {
			return -E2BIG;
		}
		for (i = 0; psize >= onesize; psize -= onesize, initregs += onesize, i++) {
			u32 addr, mask;
			u8 * __iomem iomem;

			addr = be32_to_cpu(initregs[0]);
			mask = be32_to_cpu(initregs[1]);

			iomem = ioremap(addr, sizeof(u32));
			if(iomem) {
				if(sdpmmch->pm_is_valid_clk_delay == 0) {
					sdpmmch->pm_clk_delay[i] = readl(iomem) & mask;
					dev_printk(KERN_DEBUG, dev,
						"of restoreregs store addr 0x%08x, mask 0x%08x, val 0x%08x\n",
						addr, mask, sdpmmch->pm_clk_delay[i]);
				} else {
					mmch_writel( (readl(iomem)&~mask) | (sdpmmch->pm_clk_delay[i]&mask), iomem );
					dev_printk(KERN_DEBUG, dev,
						"of restoreregs restore addr 0x%08x, mask 0x%08x, val 0x%08x\n",
						addr, mask, sdpmmch->pm_clk_delay[i]);
				}
				iounmap(iomem);
			} else {
				return -ENOMEM;
			}
		}

		if(sdpmmch->pm_is_valid_clk_delay == 0) {
			sdpmmch->pm_is_valid_clk_delay = 1;
		}
	}
	return 0;
}
#endif

extern int sdp_get_revision_id(void);

static int mmch_platform_init(SDP_MMCH_T * sdpmmch)
{
	struct platform_device *pdev = to_platform_device(sdpmmch->host->parent);
	struct sdp_mmch_plat *platdata = dev_get_platdata(&pdev->dev);

	if(!platdata)
		return -ENOSYS;

#ifdef CONFIG_OF
	sdp_mmc_of_do_initregs(&pdev->dev);
	sdp_mmc_of_do_restoreregs(&pdev->dev);
#else
	if(platdata->init) {
		int init_ret;
		init_ret = platdata->init(sdpmmch);
		if(init_ret < 0) {
			dev_err(&pdev->dev, "failed to board initialization!!(%d)\n", init_ret);
			return init_ret;
		}
	} else {
		dev_warn(&pdev->dev, "Board initialization code is not available!\n");
	}
#endif

	if(sdpmmch->rdqs_tune != MMCH_RDQS_TUNE_DISABLED) {
		sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_ENABLING;
	}
	sdpmmch->is_hs400 = false;

#ifdef CONFIG_ARCH_SDP1404
	/* XXX: Hawk-P EVT0, 0.1 MMC fixup. overwrite RDQS delay */
	if(sdp_get_revision_id() <= 1) {
		u32 temp = 0;
		dev_info(&pdev->dev, "Hawk-P EVT0 MMC RDQS delay fixup.\n");
		temp = mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
		temp &= ~0x3FFU;
		mmch_writel(temp|(2<<2), sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
	}
#endif

#ifdef CONFIG_ARCH_SDP1406
	/* XXX: Hawk-M EVT0 MMC fixup. overwrite RDQS delay */
	if(sdp_get_revision_id() <= 0) {
		u32 temp = 0;
		dev_info(&pdev->dev, "Hawk-M EVT0 MMC RDQS delay fixup.\n");
		temp = mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
		temp &= ~0x3FFU;
		mmch_writel(temp|(20<<2), sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
	}
#endif

	return 0;
}

static int
mmch_cmd_ciu_status(SDP_MMCH_T *sdpmmch)
{
	int retval = 0;
	u32 regval;
	int	count = MMCH_CMD_MAX_RETRIES;

	do{
		regval = mmch_readl(sdpmmch->base + MMCH_CMD);
		if(regval & MMCH_CMD_START_CMD) count--;
		else break;

		if(count < 0) {
			u32 status = mmch_readl(sdpmmch->base+MMCH_STATUS);
			dev_err(&sdpmmch->pdev->dev, "cmd status not change to ready(CMD%-2d) status 0x%08x(%s). host reset!!\n", regval&0x3F, status, (status&MMCH_STATUS_DATA_BUSY)?"busy!":"not busy");
			sdp_mmch_host_reset(sdpmmch);
			retval = -1;
			break;
		}
		udelay(1);
	}while(1);

	return retval;
}

static int
mmch_cmd_send_to_ciu(SDP_MMCH_T* sdpmmch, u32 cmd, u32 arg)
{
	MMCH_FLOW("cmd: 0x%08x, arg: 0x%08x\n", cmd, arg);

	mmch_writel(arg, sdpmmch->base+MMCH_CMDARG); wmb();
	mmch_writel(cmd | MMCH_CMD_USE_HOLD_REG, sdpmmch->base+MMCH_CMD); wmb();

	return mmch_cmd_ciu_status(sdpmmch);
}

static int
mmch_clock_enable(SDP_MMCH_T* sdpmmch)
{
	int retval = 0;

	mmch_writel(MMCH_CLKENA_ALL_CCLK_ENABLE | MMCH_CLKENA_ALL_LOW_POWER_MODE,
		sdpmmch->base + MMCH_CLKENA);
	retval = mmch_cmd_send_to_ciu(sdpmmch, MMCH_UPDATE_CLOCK, 0);
	if (retval < 0) {
		dev_err(&sdpmmch->pdev->dev, "ERROR : mmch_clock_enable\n");
	}
	return retval;
}

static int
mmch_clock_disable(SDP_MMCH_T* sdpmmch)
{
	int retval = 0;

	mmch_writel(MMCH_CLKDIV_0(MMCH_CLKDIV_LIMIT), sdpmmch->base + MMCH_CLKDIV);
	mmch_writel(MMCH_CLKENA_ALL_LOW_POWER_MODE, sdpmmch->base + MMCH_CLKENA);
	retval = mmch_cmd_send_to_ciu(sdpmmch, MMCH_UPDATE_CLOCK, 0);
	if (retval < 0) {
		dev_err(&sdpmmch->pdev->dev, "ERROR : mmch_clock_disable\n");
	}
	return retval;
}

static int
mmch_set_op_clock (SDP_MMCH_T* sdpmmch, unsigned int clock)
{
	int retval = 0;
	u32 divider;
	u32 __maybe_unused req_clock = clock;
	u32 cclk_in = 0;
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	unsigned long flags;
	int is_ddr = !!(mmch_readl(sdpmmch->base + MMCH_UHS_REG) & 0x10000);

	WARN(in_interrupt(), "%s: called in interrupt!!", __FUNCTION__);

	cclk_in = platdata->processor_clk /
		(((mmch_readl(sdpmmch->base + MMCH_CLKSEL)>>24)&0x7)+1);

	/* op clock = input clock / (divider * 2) */
	if((clock < (cclk_in/(0xFF*2))) || (clock > cclk_in)) {
		unsigned int actual_clock = (clock > cclk_in)?
			cclk_in : (cclk_in/(0xFF*2));
		dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev,
			"request clock is %uHz but support clock is %uHz ~ %uHz. "
			"so actual clock is %uHz\n",
			clock, cclk_in/(0xFF*2),
			cclk_in, actual_clock);
		clock = actual_clock;
	}

	/* calculate the clock. */
	if(cclk_in <= clock) {
		if(is_ddr)
			divider = 1;
		else
			divider = 0;
	} else {
		for(divider = 1; cclk_in/(2*divider) > clock; divider++) {
			if(divider >= MMCH_CLKDIV_LIMIT)
				break;
		}
	}
	sdpmmch->actual_clock = cclk_in / (divider?(2*divider):1);
#ifdef CONFIG_MMC_DEBUG
	dev_info(&sdpmmch->pdev->dev,
		"cclk_in %uHz, request clock : %d, actual clock : %d, divider %d\n", cclk_in, req_clock, sdpmmch->actual_clock, divider);
#endif

	/* apply new clock divider value */
	spin_lock_irqsave(&sdpmmch->lock, flags);

	retval = mmch_clock_disable(sdpmmch);
	if (retval < 0) {
		dev_err(&sdpmmch->pdev->dev, "ERROR : set mmch disable clocks \n");
		goto set_op_clock_unlock;
	}

	// set divider value
	mmch_writel(divider, sdpmmch->base+MMCH_CLKDIV);

	retval = mmch_clock_enable(sdpmmch);
	if (retval < 0) {
		dev_err(&sdpmmch->pdev->dev, "ERROR : set mmch enable clocks \n");
		goto set_op_clock_unlock;
	}
	udelay(10);// need time for clock gen


set_op_clock_unlock:
	spin_unlock_irqrestore(&sdpmmch->lock, flags);

	return retval;

}

static int
sdp_mmch_host_reset(SDP_MMCH_T *sdpmmch) {// MMC all reset!!
	u32 ctrl = mmch_readl(sdpmmch->base+MMCH_CTRL);
	u32 clkdiv = mmch_readl(sdpmmch->base+MMCH_CLKDIV);

	/* IDMAC SW reset */
	mmch_writel(MMCH_BMOD_SWR, sdpmmch->base+MMCH_BMOD);
	while( mmch_readl(sdpmmch->base+MMCH_BMOD) & MMCH_BMOD_SWR );

	/* Host DMA reset */
	mmch_writel(ctrl|MMCH_CTRL_DMA_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_DMA_RESET );

	/* Host FIFO reset */
	mmch_writel(ctrl|MMCH_CTRL_FIFO_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_FIFO_RESET );

	/* Host CONTROLLER reset */
	mmch_writel(ctrl|MMCH_CTRL_CONTROLLER_RESET, sdpmmch->base+MMCH_CTRL);
	while( mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_CONTROLLER_RESET );

	mmch_clock_disable(sdpmmch);
	mmch_writel(clkdiv, sdpmmch->base+MMCH_CLKDIV);
	mmch_clock_enable(sdpmmch);

	return 0;
}

#ifdef CONFIG_WRITE_PROTECT
unsigned int check_protection_area(unsigned int address, struct mmc_host *host);
#endif

static int
mmch_start_mrq(SDP_MMCH_T *sdpmmch, struct mmc_command *cmd)	// 111204
{
	u32 regval = 0;
	u32 argval = 0;

#ifdef CONFIG_WRITE_PROTECT
        unsigned int part_type = 0;

        if(cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
        {
                part_type = check_protection_area(cmd->arg, sdpmmch->host);
        }
#endif

	argval = cmd->arg;
	regval = cmd->opcode;
	regval |= MMCH_CMD_START_CMD;

	if(cmd->flags & MMC_RSP_PRESENT){
		MMCH_SET_BITS(regval, MMCH_CMD_RESPONSE_EXPECT);

		if(cmd->flags & MMC_RSP_CRC)
			MMCH_SET_BITS(regval, MMCH_CMD_CHECK_RESPONSE_CRC);

		if(cmd->flags & MMC_RSP_136)
			MMCH_SET_BITS(regval, MMCH_CMD_RESPONSE_LENGTH);
	}

	if(cmd->data) {
		MMCH_FLOW("data transfer %s\n", (cmd->data->flags&MMC_DATA_WRITE)? "write":"read");

		MMCH_SET_BITS(regval, MMCH_CMD_DATA_EXPECTED);

#ifdef CONFIG_SDP_MMC_USE_AUTO_STOP_CMD	// 111204
		if(!cmd->mrq->sbc && cmd->data->stop)
			MMCH_SET_BITS(regval, MMCH_CMD_SEND_AUTO_STOP);
#endif

		if(cmd->data->flags & MMC_DATA_WRITE)
			MMCH_SET_BITS(regval, MMCH_CMD_READ_WRITE);
	} else {
		MMCH_FLOW("command response \n");
	}

	MMCH_SET_BITS(regval, MMCH_CMD_WARVDATA_COMPLETE);

	switch(cmd->opcode){
		case MMC_GO_IDLE_STATE :
			//MMCH_UNSET_BITS(regval, MMCH_CMD_WARVDATA_COMPLETE);
			MMCH_SET_BITS(regval, MMCH_CMD_SEND_INITIALIZATION);
			break;

		case MMC_STOP_TRANSMISSION :	// 111204
			MMCH_SET_BITS(regval, MMCH_CMD_STOP_ABORT_CMD);
			MMCH_UNSET_BITS(regval, MMCH_CMD_WARVDATA_COMPLETE);
			break;

		case MMC_SEND_STATUS :
			//MMCH_UNSET_BITS(regval, MMCH_CMD_WARVDATA_COMPLETE);
			break;

		default :
			break;
	}
//	printk("regval: 0x%08x opcode: 0x%d, arg: 0x%08x\n", regval, cmd->opcode, argval);
	MMCH_FLOW("regval: 0x%08x opcode: 0x%08x, arg: 0x%08x\n", regval, cmd->opcode, argval);

	sdpmmch->cmd = cmd;
	sdpmmch->cmd_start_time = jiffies;

	if(mmch_cmd_send_to_ciu(sdpmmch, regval, argval) < 0){
		dev_err(&sdpmmch->pdev->dev, "ERROR: Can't send CMD%-2d\n", cmd->opcode);
		return -100;
	}

	return 0;
}

static inline void
mmch_get_resp(SDP_MMCH_T *sdpmmch, struct mmc_command *cmd)
{
	if(cmd->flags & MMC_RSP_136){
		cmd->resp[3] = mmch_readl(sdpmmch->base + MMCH_RESP0);
		cmd->resp[2] = mmch_readl(sdpmmch->base + MMCH_RESP1);
		cmd->resp[1] = mmch_readl(sdpmmch->base + MMCH_RESP2);
		cmd->resp[0] = mmch_readl(sdpmmch->base + MMCH_RESP3);
	} else {
		cmd->resp[0] = mmch_readl(sdpmmch->base + MMCH_RESP0);
	}
}

static int sdp_mmch_set_tune_value(SDP_MMCH_T *sdpmmch, struct sdp_mmch_reg_list *table, int idx);

static int 
sdp_mmch_select_median(SDP_MMCH_T *sdpmmch, int nr_bit, unsigned long bitmap, int *out_maxlen)
{
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	int max_start = 0, max_len = 0, now_bit = 0, pre_bit = 0;
	int start = 0, len = 0, i;

	if(!bitmap) {
		return -EINVAL;
	}

	for( i = 0; i < nr_bit * 2; i++) {
		now_bit = !!(bitmap & (0x1UL<<(i % nr_bit)));

		if(!pre_bit && now_bit) {
			start = i;
			len = 1;
		} else if(pre_bit && now_bit) {
			len++;
		} else if(pre_bit && !now_bit) {
			if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
				if(len >= max_len) {
					max_start = start;
					max_len = len;
				}
			} else {
				if(len > max_len) {
					max_start = start;
					max_len = len;
				}
			}
			start = 0;
			len = 0;
		}

		pre_bit = now_bit;
	}

	/* check last */
	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
		if(len >= max_len) {
			max_start = start;
			max_len = len;
		}
	} else {
		if(len > max_len) {
			max_start = start;
			max_len = len;
		}
	}

	if(out_maxlen) {
		*out_maxlen = max_len;
	}
	//pr_info("sdp_mmch_select_median: start %d, len %d\n", max_start, max_len);
	return (max_start+((max_len-1)/2)) % nr_bit;
}

static int mmc_error_show(struct seq_file *s, void *data)
{
	struct mmc_host *host = s->private;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	sdpmmch->in_tuning = true;

	seq_printf(s, "%x\n", sdpmmch->intr_status);

	sdpmmch->in_tuning = false;

	mmc_release_host(host);
	return 0;
}

static int mmc_error_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_error_show, inode->i_private);
}

static const struct file_operations mmc_error_fops = {
	.open		= mmc_error_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void show_cardinfo(struct mmc_card *card)
{
	printk(KERN_ALERT "Product Name : %s, F/W : 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	       mmc_card_name(card),
	       card->ext_csd.fwrev[0],
	       card->ext_csd.fwrev[1],
	       card->ext_csd.fwrev[2],
	       card->ext_csd.fwrev[3],
	       card->ext_csd.fwrev[4],
	       card->ext_csd.fwrev[5],
	       card->ext_csd.fwrev[6],
	       card->ext_csd.fwrev[7]);
}

// 110118
static u32
mmch_mmc_data_error(SDP_MMCH_T * sdpmmch, u32 intr_status, const char *msg)
{
	struct mmc_host *host = sdpmmch->host;
	struct mmc_card *card = host->card;
	u32 retval = 0;
	if(sdpmmch->in_tuning) {
		return EILSEQ;
	}
	show_cardinfo(card);
	dev_err(&sdpmmch->pdev->dev, "%s(event 0x%08lx, accum 0x%08lx)\n", msg, sdpmmch->event, sdpmmch->event_accumulated);

/* Error: Status */
	if(intr_status & MMCH_MINTSTS_EBE) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: End bit/Write no CRC\n");
		retval = EILSEQ;
	}

 	if(intr_status & MMCH_MINTSTS_SBE) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: Start Bit\n");
		retval = EILSEQ;
	}

 	if(intr_status & MMCH_MINTSTS_FRUN) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: FIFO Underrun/Overrun\n");
		retval = EILSEQ;
	}

/* Error: Timeout */
 	if(intr_status & MMCH_MINTSTS_HTO) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: Data Starvation by host Timeout\n");
		retval = ETIMEDOUT;
#ifdef CONFIG_ARCH_SDP1304
		mmch_bus_moniter_dump(sdpmmch);
#endif
#ifdef CONFIG_SDP_BUSERR
#if defined(CONFIG_ARCH_SDP1803)
	sdp_mmch_pll_counter(sdpmmch);
#endif
	sdp_buserr_show();
	sdp_busmon_show();
#endif

	}

 	if(intr_status & MMCH_MINTSTS_DRTO) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: Data read Timeout\n");
#if defined(CONFIG_ARCH_SDP1803)
		sdp_mmch_pll_counter(sdpmmch);
#endif
		retval = ETIMEDOUT;
	}

/* Xfer error */
 	if(intr_status & MMCH_MINTSTS_DCRC) {		// Data
		dev_err(&sdpmmch->pdev->dev, "Error: Data CRC\n");
#if defined(CONFIG_ARCH_SDP1803)
		sdp_mmch_pll_counter(sdpmmch);
#endif
		retval = EILSEQ;
	}

	dev_err(&sdpmmch->pdev->dev, "MMC DATA ERROR!!! DUMP START!!(status 0x%08x dma 0x%08x)\n", sdpmmch->intr_status, sdpmmch->dma_status);
	mmch_register_dump(sdpmmch);
	dev_err(&sdpmmch->pdev->dev, "MMC DATA ERROR!!! DUMP END!!\n\n");
//	udelay(200); 	// TODO: check

	return retval;
}


static u32
mmch_mmc_cmd_error(SDP_MMCH_T * sdpmmch, u32 intr_status)
{
	u32 retval = 0;
	u32 opcode = 0;

	MMCH_FLOW("mrq: 0x%08x, intr_status 0x%08x\n", (u32)sdpmmch->mrq, intr_status);

	opcode = sdpmmch->cmd? sdpmmch->cmd->opcode:0xFF;

 	if(intr_status & MMCH_MINTSTS_HLE) {		// Command
		dev_err(&sdpmmch->pdev->dev, "[MMC_INTR] Error: H/W locked write(CMD%-2d)\n", opcode);
		mmch_register_dump(sdpmmch);
		retval = EILSEQ;
	}

	if(sdpmmch->in_tuning) {
		return EILSEQ;
	}

 	if(intr_status & MMCH_MINTSTS_RTO) {		// Command
		switch(opcode){
	//initialize command, find device sd, sdio
			case(5):
			case(8):
			case(52):
			case(55):
				/* if card type is not mmc */
				if(!(sdpmmch->host && sdpmmch->host->card))
					break;

			default:
				dev_err(&sdpmmch->pdev->dev, "[MMC_INTR] Error: Response Timeout(CMD%-2d)\n", opcode); // when initialize,
				//mmch_register_dump(sdpmmch);
				break;
		}
		retval = ETIMEDOUT;
	}

 	if(intr_status & MMCH_MINTSTS_RCRC) {		// Command
		dev_err(&sdpmmch->pdev->dev, "[MMC_INTR] Error: Response CRC(CMD%-2d)\n", opcode);
		//mmch_register_dump(sdpmmch);
#if defined(CONFIG_ARCH_SDP1803)
		sdp_mmch_pll_counter(sdpmmch);
#endif
		retval = EILSEQ;
	}

/* Response Error */
 	if(intr_status & MMCH_MINTSTS_RE) {			// Command
		dev_err(&sdpmmch->pdev->dev, "[MMC_INTR] Error: Response(CMD%-2d)\n", opcode);
		//mmch_register_dump(sdpmmch);
#if defined(CONFIG_ARCH_SDP1803)
		sdp_mmch_pll_counter(sdpmmch);
#endif
		retval = EILSEQ;
	}

//	udelay(200); 	// TODO: check

	return retval;
}
// 110118 end


/* DMA mode funcs */
static void mmch_dma_intr_dump(SDP_MMCH_T * sdpmmch)
{
#ifdef MMCH_DMA_DESC_DUMP

	struct scatterlist *data_sg = NULL;
	MMCH_DMA_DESC_T* p_desc = sdpmmch->p_dmadesc_vbase;
	u32 idx=0;

	dev_err(&sdpmmch->pdev->dev, "\n=============================================================\n");
	dev_err(&sdpmmch->pdev->dev, "=====================SDP MMC Descriptor DUMP=====================\n");
	dev_err(&sdpmmch->pdev->dev, "=============================================================\n");
	if(sdpmmch->mrq && sdpmmch->mrq->data) {
		for(data_sg = sdpmmch->mrq->data->sg; data_sg; data_sg = sg_next(data_sg)){
			printk("idx = %3u, sg: length=%4d, dmaaddr=0x%08llx\n",
				idx, data_sg ->length ,(u64)sg_phys(data_sg));
		}
	}

	dev_err(&sdpmmch->pdev->dev, "-------------------------------------------------------------\n");
	dev_err(&sdpmmch->pdev->dev, "    desc index    status     length     buffer1    buffer2\n");
	for(idx = 0; p_desc && (idx < sdpmmch->sg_len); idx++){
		dev_err(&sdpmmch->pdev->dev, "%p: [%3u] = 0x%08x 0x%08x 0x%08x 0x%08x\n",
			p_desc, idx, p_desc->status, p_desc->length,
			p_desc->buffer1_paddr, p_desc->buffer2_paddr);
		p_desc++;
	}

	dev_err(&sdpmmch->pdev->dev, "-------------------------------------------------------------\n");
	dev_err(&sdpmmch->pdev->dev, "sdpmmch->mrq_data_size = %d\n",sdpmmch->mrq_data_size);

	dev_err(&sdpmmch->pdev->dev, "\n=============================================================\n");
	dev_err(&sdpmmch->pdev->dev, "===================SDP MMC Descriptor DUMP END===================\n");
	dev_err(&sdpmmch->pdev->dev, "=============================================================\n");

#endif
}

static void
mmch_dma_sg_to_desc(SDP_MMCH_T* sdpmmch)
{
	struct scatterlist *sg = sdpmmch->sg;
	unsigned int nr_sg = sdpmmch->sg_len;
	MMCH_DMA_DESC_T* p_desc = sdpmmch->p_dmadesc_vbase;
	dma_addr_t phy_desc_addr = sdpmmch->dmadesc_pbase;
	u32 idx;

	BUG_ON(nr_sg > MMCH_DESC_NUM);
	BUG_ON(nr_sg == MMCH_DESC_ZERO);

	for(idx = 0; idx < nr_sg; idx++){

#ifdef CONFIG_SDP_MMC_64BIT_ADDR
		p_desc->reserved0 = 0;
		p_desc->reserved1 = 0;
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		p_desc->buffer1_paddr_u= (u32)((u64)sg_phys(sg)>>32);
		p_desc->buffer2_paddr_u= (u32)((u64)(phy_desc_addr + sizeof(MMCH_DMA_DESC_T))>>32);
#else
		p_desc->buffer1_paddr_u= 0;
		p_desc->buffer2_paddr_u= 0;
#endif
#endif

		p_desc->length = sg_dma_len(sg) & DescBuf1SizMsk;
		p_desc->buffer1_paddr = (u32) sg_phys(sg);
		p_desc->buffer2_paddr = (u32)(phy_desc_addr + sizeof(MMCH_DMA_DESC_T));
		/* 120518 dongseok.lee disable interrupt */
		p_desc->status = DescOwnByDma | DescSecAddrChained | DescDisInt;

		p_desc++;
		sg++;
		phy_desc_addr += sizeof(MMCH_DMA_DESC_T);
	}

	/* 1st descriptor */
	p_desc = sdpmmch->p_dmadesc_vbase;
	p_desc->status |= DescFirstDesc;

	/* last descriptor */
	p_desc += nr_sg - 1;
	p_desc->status |= DescLastDesc;

	/* 120518 dongseok.lee enable interrupt only last desc */
	p_desc->status &= ~(u32)DescDisInt;
	p_desc->buffer2_paddr = 0;

	wmb();
	p_desc->status |= DescLastDesc;/* for XMIF WRITE CONFORM */
}

static void mmch_dma_start(SDP_MMCH_T * sdpmmch)
{
	u32 regval;

	/* peanding clear */
	mmch_writel(MMCH_IDSTS_INTR_ALL, sdpmmch->base+MMCH_IDSTS);

	mmch_writel(MMCH_IDSTS_INTR_ALL, sdpmmch -> base+MMCH_IDINTEN);

	/* chack idmac reset bit */
	while(mmch_readl(sdpmmch->base+MMCH_CTRL) & MMCH_CTRL_DMA_RESET);

	/* Select IDMAC interface */
	regval = mmch_readl(sdpmmch->base+MMCH_CTRL);
	regval |= MMCH_CTRL_USE_INTERNAL_DMAC;
	mmch_writel(regval, sdpmmch->base+MMCH_CTRL);

	wmb();

	/* Enable the IDMAC */
	regval = mmch_readl(sdpmmch->base+MMCH_BMOD);
	regval |= MMCH_BMOD_DE | MMCH_BMOD_FB;
	mmch_writel(regval, sdpmmch->base+MMCH_BMOD);

	/* Start it running */
	mmch_writel(1, sdpmmch->base+MMCH_PLDMND);
}

static void mmch_dma_stop(SDP_MMCH_T * sdpmmch)
{
	u32 regval;

	/* Disable and reset the IDMAC interface */
	regval = mmch_readl(sdpmmch->base+MMCH_CTRL);
	regval &= ~MMCH_CTRL_USE_INTERNAL_DMAC;
	regval |= MMCH_CTRL_DMA_RESET;
	mmch_writel(regval, sdpmmch->base+MMCH_CTRL);

	wmb();

	/* Stop the IDMAC running */
	regval = mmch_readl(sdpmmch->base+MMCH_BMOD);
	regval &= ~(MMCH_BMOD_DE| MMCH_BMOD_FB);
	mmch_writel(regval, sdpmmch->base+MMCH_BMOD);

	mmch_writel(0x0, sdpmmch -> base+MMCH_IDINTEN);
}


/* Process seq
 * cbuf: compressed buffer
 * ucbuf: uncompressed buffer
 * map ucbuf -> MMCH DMA -> cbuf -> UNZIP -> ucbuf -> unmap ucbuf
 */

#define	MIN(a, b) (((a) < (b)) ? (a) : (b))

static int mmch_dma_prepare_data(SDP_MMCH_T * sdpmmch, struct mmc_data *data)
{
	int sg_num = 0;

#ifdef CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM
	if (!(data->flags & MMC_DATA_NOMAP))
#endif
		sg_num = dma_map_sg(&sdpmmch->pdev->dev, data->sg,
				    (int)data->sg_len,
				    ((data->flags & MMC_DATA_WRITE) ?
					     DMA_TO_DEVICE : DMA_FROM_DEVICE));

	return sg_num;
}

/* clean up desc buffer!! */
static void mmch_dma_cleanup_data(SDP_MMCH_T * sdpmmch, struct mmc_data *data)
{
#ifdef CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM
	if (!(data->flags & MMC_DATA_NOMAP))
#endif
		dma_unmap_sg(&sdpmmch->pdev->dev, data->sg,
			     (int)data->sg_len,
			     ((data->flags & MMC_DATA_WRITE) ?
				      DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void
mmch_dma_set_dma (SDP_MMCH_T* sdpmmch)
{
	struct mmc_data *data = sdpmmch->mrq->data;

	BUG_ON(!data);
	sdpmmch->sg = data->sg;
	sdpmmch->sg_len = data->sg_len;
	sdpmmch->mrq_data_size = data->blocks * data->blksz;

	/* FIFO reset! */
	mmch_writel(mmch_readl(sdpmmch->base+MMCH_CTRL)|MMCH_CTRL_FIFO_RESET, sdpmmch->base+MMCH_CTRL);
	while(mmch_readl(sdpmmch->base+MMCH_CTRL)&MMCH_CTRL_FIFO_RESET);

	mmch_writel(sdpmmch->mrq_data_size, sdpmmch->base+MMCH_BYTCNT);
	mmch_writel(data->blksz, sdpmmch->base+MMCH_BLKSIZ);

	if(!(sdpmmch->is_pre_post_req)){
		mmch_dma_prepare_data(sdpmmch, data);
	}

	mmch_dma_sg_to_desc(sdpmmch);
}

static void
mmch_dma_end_dma (SDP_MMCH_T* sdpmmch)
{
	struct mmc_data *data = sdpmmch->mrq->data;
	struct timespec nowtime, timeout;
#ifdef CONFIG_ARCH_SDP1207
	/* Fox-B bus is very busy... */
	const u32 timeout_us = 1000;
#else
	const u32 timeout_us = 100;
#endif
	u32 idx;

	BUG_ON(!data);

	/* chack OWN bit */
	for(idx = 0; idx < data->sg_len; idx++) {
		if(sdpmmch->p_dmadesc_vbase[idx].status & DescOwnByDma) {
			dev_dbg(&sdpmmch->pdev->dev,
				"DEBUG! DESC %02u is Owned by DMA!! waiting max %uus. (status 0x%08x)\n",
				idx, timeout_us, sdpmmch->p_dmadesc_vbase[idx].status);

			ktime_get_ts(&nowtime);
			timeout = nowtime;
			timespec_add_ns(&timeout, timeout_us*NSEC_PER_USEC);

			while(sdpmmch->p_dmadesc_vbase[idx].status & DescOwnByDma) {
				ktime_get_ts(&nowtime);

				if(timespec_compare(&timeout, &nowtime) < 0) {
					dev_err(&sdpmmch->pdev->dev,
						"ERROR! DESC %02u is Owned by DMA!! timeout %uus! (status 0x%08x)\n",
						idx, timeout_us, sdpmmch->p_dmadesc_vbase[idx].status);
#ifdef CONFIG_ARCH_SDP1207
					mmch_register_dump_notitle(sdpmmch);
#else
					mmch_register_dump(sdpmmch);
#endif
					break;
				}
				udelay(1);
			}

			if(timespec_compare(&timeout, &nowtime) >= 0) {
				nowtime = timespec_sub(timeout, nowtime);
				dev_dbg(&sdpmmch->pdev->dev,
					"DEBUG! DESC %02u is Owned by DMA!! waiting time is %lldus. (status 0x%08x)\n",
					idx, (timeout_us*NSEC_PER_USEC)-timespec_to_ns(&nowtime),
					sdpmmch->p_dmadesc_vbase[idx].status);
			}
		}
	}

	/* if not excuted pre_req */
	if(!(sdpmmch->is_pre_post_req)){
		mmch_dma_cleanup_data(sdpmmch, data);
	}
}

static int mmch_dma_desc_init(SDP_MMCH_T* sdpmmch)
{
	void *p_dmadesc_vbase;

	p_dmadesc_vbase =
			(MMCH_DMA_DESC_T*)dma_alloc_coherent(sdpmmch->host->parent,
							sizeof(MMCH_DMA_DESC_T) * MMCH_DESC_NUM,
							&sdpmmch->dmadesc_pbase,
							GFP_ATOMIC);


	if(!p_dmadesc_vbase) {
		return -1;
	}

	memset(p_dmadesc_vbase, 0x0, MMCH_DESC_SIZE);

	dev_info(&sdpmmch->pdev->dev, "dma desc alloc VA: 0x%pK, PA: 0x%08llx\n",
		p_dmadesc_vbase, (u64)sdpmmch->dmadesc_pbase);

	sdpmmch->p_dmadesc_vbase = p_dmadesc_vbase;
	mmch_writel((u32) sdpmmch->dmadesc_pbase, sdpmmch->base+MMCH_DBADDR);
#ifdef MMCH_DBADDRU
	mmch_writel((u32) sdpmmch->dmadesc_pbase>>32, sdpmmch->base+MMCH_DBADDRU);
#endif

	return 0;
}

/* PIO mode funcs */
#ifdef CONFIG_HIGHMEM
/*
	kmap_atomic().  This permits a very short duration mapping of a single
	page.  Since the mapping is restricted to the CPU that issued it, it
	performs well, but the issuing task is therefore required to stay on that
	CPU until it has finished, lest some other task displace its mappings.

	kmap_atomic() may also be used by interrupt contexts, since it is does not
	sleep and the caller may not sleep until after kunmap_atomic() is called.

	It may be assumed that k[un]map_atomic() won't fail.
*/
static void *mmch_pio_sg_kmap_virt(struct scatterlist *sg) {
	return (u8 *)kmap_atomic(sg_page(sg)) + sg->offset;
}
static void mmch_pio_sg_kunmap_virt(void *kvaddr) {
	kunmap_atomic(kvaddr);
}
#else
#define mmch_pio_sg_kmap_virt(sg)		sg_virt(sg)
#define mmch_pio_sg_kunmap_virt(kvaddr)	do {}while(0)
#endif/* CONFIG_HIGHMEM */

static void mmch_pio_pull_data64(SDP_MMCH_T *sdpmmch, void *buf, size_t cnt)
{
	size_t i = 0;
	WARN_ON(cnt % 8 != 0);

	for(i = 0; i < cnt; i+=8) {
		memcpy(((u8*)buf)+i, sdpmmch->base+MMCH_FIFODAT, 8);
	}
}

static void mmch_pio_push_data64(SDP_MMCH_T *sdpmmch, void *buf, size_t cnt)
{
	size_t i = 0;
	WARN_ON(cnt % 8 != 0);

	for(i = 0; i < cnt; i+=8) {
		memcpy(sdpmmch->base+MMCH_FIFODAT, ((u8*)buf)+i, 8);
	}
}

static void mmch_pio_read_data(SDP_MMCH_T *sdpmmch)
{
	struct scatterlist *sg = sdpmmch->sg;
	u8 *buf = NULL;
	unsigned int offset = sdpmmch->pio_offset;
	struct mmc_data	*data = sdpmmch->mrq->data;
	int shift = sdpmmch->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;

	do {
		len = MMCH_STATUS_GET_FIFO_CNT(mmch_readl(sdpmmch->base+MMCH_STATUS)) << shift;
		if (offset + len <= sg->length) {
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_pull(sdpmmch, (buf + offset), len);
			mmch_pio_sg_kunmap_virt(buf);

			offset += len;
			nbytes += len;

			if (offset == sg->length) {
				flush_kernel_dcache_page(sg_page(sg));
				sdpmmch->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
			}
		} else {
			unsigned int remaining = sg->length - offset;
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_pull(sdpmmch, (void *)(buf + offset),
					remaining);
			mmch_pio_sg_kunmap_virt(buf);
			nbytes += remaining;

			flush_kernel_dcache_page(sg_page(sg));
			sdpmmch->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_pull(sdpmmch, buf, offset);
			mmch_pio_sg_kunmap_virt(buf);
			nbytes += offset;
		}

		status = mmch_readl(sdpmmch->base+MMCH_MINTSTS);
		mmch_writel(MMCH_RINTSTS_RXDR, sdpmmch->base+MMCH_RINTSTS);
		if (status & MMCH_MINTSTS_ERROR_DATA) {
			sdpmmch->data_error_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();

			//set_bit(EVENT_DATA_ERROR, &sdpmmch->pending_events);
			//tasklet_schedule(&sdpmmch->tasklet);

			return;
		}
	} while (status & MMCH_MINTSTS_RXDR); /*if the RXDR is ready read again*/

	sdpmmch->pio_offset = offset;
	data->bytes_xfered += nbytes;
	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	set_bit(MMCH_EVENT_XFER_DONE, &sdpmmch->event);
}

static void mmch_pio_write_data(SDP_MMCH_T *sdpmmch)
{
	struct scatterlist *sg = sdpmmch->sg;
	u8 *buf = NULL;
	unsigned int offset = sdpmmch->pio_offset;
	struct mmc_data	*data = sdpmmch->mrq->data;
	int shift = sdpmmch->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;

	do {
		len = (sdpmmch->fifo_depth - MMCH_STATUS_GET_FIFO_CNT(mmch_readl(sdpmmch->base+MMCH_STATUS))) << shift;
		if (offset + len <= sg->length ) {
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_push(sdpmmch, (void *)(buf + offset), len);
			mmch_pio_sg_kunmap_virt(buf);

			offset += len;
			nbytes += len;
			if (offset == sg->length) {
				sdpmmch->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
			}
		} else {
			unsigned int remaining = sg->length - offset;
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_push(sdpmmch, (void *)(buf + offset),
					remaining);
			mmch_pio_sg_kunmap_virt(buf);
			nbytes += remaining;

			sdpmmch->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = mmch_pio_sg_kmap_virt(sg);
			sdpmmch->pio_push(sdpmmch, (void *)buf, offset);
			mmch_pio_sg_kunmap_virt(buf);
			nbytes += offset;
		}

		status = mmch_readl(sdpmmch->base+MMCH_MINTSTS);
		mmch_writel(MMCH_RINTSTS_TXDR, sdpmmch->base+MMCH_RINTSTS);
		if (status & MMCH_MINTSTS_ERROR_DATA) {
			sdpmmch->data_error_status = status;
			data->bytes_xfered += nbytes;

			smp_wmb();

			//set_bit(EVENT_DATA_ERROR, &sdpmmch->pending_events);
			//tasklet_schedule(&sdpmmch->tasklet);
			return;
		}
	} while (status & MMCH_MINTSTS_TXDR); /* if TXDR write again */

	sdpmmch->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	set_bit(MMCH_EVENT_XFER_DONE, &sdpmmch->event);
}

/* setting PIO */
static void
mmch_pio_set_pio(SDP_MMCH_T* sdpmmch)
{
	struct mmc_data *data = sdpmmch->mrq->data;

//	data->error = EINPROGRESS;

	WARN_ON(sdpmmch->sg);

	sdpmmch->sg = data->sg;
	sdpmmch->pio_offset = 0;

	sdpmmch->mrq_data_size = data->blocks * data->blksz;


	/* FIFO reset! */
	mmch_writel(mmch_readl(sdpmmch->base+MMCH_CTRL)|MMCH_CTRL_FIFO_RESET, sdpmmch->base+MMCH_CTRL);
	while(mmch_readl(sdpmmch->base+MMCH_CTRL)&MMCH_CTRL_FIFO_RESET);

	mmch_writel(sdpmmch->mrq_data_size, sdpmmch->base+MMCH_BYTCNT);
	mmch_writel(data->blksz, sdpmmch->base+MMCH_BLKSIZ);

	mmch_writel(mmch_readl(sdpmmch->base+MMCH_INTMSK)|(MMCH_INTMSK_RXDR|MMCH_INTMSK_TXDR),
		sdpmmch->base+MMCH_INTMSK);
}


static void mmch_request_end(SDP_MMCH_T * sdpmmch)
{
	int i;

	/* all inter disable */
	mmch_writel(0x0, sdpmmch->base+MMCH_INTMSK);

	if((sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) && sdpmmch->mrq->data) {

		mmch_dma_stop(sdpmmch);

		if(!sdpmmch->is_pre_post_req) {
			sdpmmch->mrq->data->host_cookie = 0;
		}
	}

	sdpmmch->mrq = NULL;
	sdpmmch->cmd = NULL;
	sdpmmch->cmd_start_time = 0;
	sdpmmch->sg = NULL;
	sdpmmch->sg_len= 0;
	sdpmmch->mrq_data_size = 0;
	sdpmmch->pio_offset = 0;

	sdpmmch->intr_accumulated = 0;
	sdpmmch->idmac_accumulated = 0;
	sdpmmch->event_accumulated = 0;

	for(i = 0; i < MMCH_EVENT_ENDMARK; i++) {
		if(test_and_clear_bit(i, &sdpmmch->event)) {
			if(!sdpmmch->in_tuning) {
				dev_err(&sdpmmch->pdev->dev, "mmch_request_end: event %d is pending!\n", i);
			}
		}
	}
}



/* FSM */
static void mmch_state_machine(SDP_MMCH_T * sdpmmch, unsigned long intr_status)
{
	enum sdp_mmch_state init_state, pre_state, state;
	unsigned long init_event= 0, init_event_accm = 0;
	unsigned long flags;
	struct mmc_request *mrq_done = NULL;
	int sw_timeout_handled = false;
	struct mmc_command *first_handled_cmd = NULL;
	unsigned long process_interval, polling_interval;

	/* delete timer */
	del_timer(&sdpmmch->timeout_timer);

	spin_lock(&sdpmmch->state_lock);

	process_interval = jiffies_to_msecs(jiffies - sdpmmch->cmd_start_time);
	polling_interval = jiffies_to_msecs(jiffies - sdpmmch->polling_timer_start);

	init_event= sdpmmch->event;
	init_event_accm = sdpmmch->event_accumulated;

	init_state = sdpmmch->state;
	state = init_state;



#ifdef MMCH_PRINT_STATE
	dev_printk(MMCH_PRINT_LEVEL, &sdpmmch->pdev->dev,
			"FSM ----- %s, event 0x%08lx, intr_status 0x%08x", mmch_state_string(state), sdpmmch->event, sdpmmch->intr_status);
#endif

	do {
		pre_state = state;


		switch(state) {
		case MMCH_STATE_IDLE:
			break;

		case MMCH_STATE_SENDING_SBC:
			if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_CMD, &sdpmmch->event)) {
				sdpmmch->mrq->sbc->error = mmch_mmc_cmd_error(sdpmmch, sdpmmch->intr_status);
				intr_status  &= ~MMCH_MINTSTS_ERROR_CMD;
				state = MMCH_STATE_ERROR_CMD;
			}

			if(test_and_clear_bit(MMCH_EVENT_HOST_CMDDONE_SBC, &sdpmmch->event)) {

				if(sdpmmch->cmd->flags & MMC_RSP_PRESENT){
					mmch_get_resp(sdpmmch, sdpmmch->mrq->sbc);
				}

				if (sdpmmch->mrq->sbc->error) {
					state = MMCH_STATE_ERROR_CMD;
					break;
				}

				/* start new cmd */
				mmch_start_mrq(sdpmmch, sdpmmch->mrq->cmd);
				state = MMCH_STATE_SENDING_CMD;
			}
			break;

		case MMCH_STATE_SENDING_CMD:
			if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_CMD, &sdpmmch->event)) {
				sdpmmch->mrq->cmd->error = mmch_mmc_cmd_error(sdpmmch, sdpmmch->intr_status);
				intr_status  &= ~MMCH_MINTSTS_ERROR_CMD;
				state = MMCH_STATE_ERROR_CMD;
			}

			if(test_and_clear_bit(MMCH_EVENT_HOST_CMDDONE, &sdpmmch->event)) {

				if(sdpmmch->mrq->cmd->flags & MMC_RSP_PRESENT){
					mmch_get_resp(sdpmmch, sdpmmch->mrq->cmd);
				}

				if (sdpmmch->mrq->cmd->error) {
					if(sdpmmch->mrq->stop) {
						mmch_start_mrq(sdpmmch, sdpmmch->mrq->stop);
						state = MMCH_STATE_SENDING_STOP;
					} else {
						state = MMCH_STATE_ERROR_CMD;
					}
					break;
				}

				if(sdpmmch->mrq->data) {
					state = MMCH_STATE_SENDING_DATA;
					break;
				}

				state = MMCH_STATE_REQUEST_ENDING;
			}
			break;

		case MMCH_STATE_SENDING_DATA:

			if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_DATA, &sdpmmch->event)) {
				struct mmc_data *data = sdpmmch->mrq->data;
				data->error = mmch_mmc_data_error(sdpmmch, sdpmmch->intr_status, "FSM Error in SENDING_DATA!");
				intr_status  &= ~MMCH_MINTSTS_ERROR_DATA;
			}

			if(test_and_clear_bit(MMCH_EVENT_XFER_ERROR, &sdpmmch->event)) {
				struct mmc_data *data = sdpmmch->mrq->data;
				data->error = ETIMEDOUT;
			}

			if(sdpmmch->mrq->data->error) {
				/* send stop for DTO occur */
				if(sdpmmch->mrq->stop) {
					mmch_start_mrq(sdpmmch, sdpmmch->mrq->stop);
				}

				state = MMCH_STATE_ERROR_DATA;
				break;
			}

			if(test_and_clear_bit(MMCH_EVENT_XFER_DONE, &sdpmmch->event)) {

				if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
					mmch_dma_end_dma(sdpmmch);
				}
				state = MMCH_STATE_PROCESSING_DATA;
			}
			break;

		case MMCH_STATE_PROCESSING_DATA:

			if(test_and_clear_bit(MMCH_EVENT_HOST_DTO, &sdpmmch->event)) {
				struct mmc_data *data = sdpmmch->mrq->data;

				if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_DATA, &sdpmmch->event)) {
					data->error = mmch_mmc_data_error(sdpmmch, sdpmmch->intr_status, "FSM Error in PROCESSING_DATA(DTO)!");
					intr_status  &= ~MMCH_MINTSTS_ERROR_DATA;
				}

				if(!data->error) {
					/*no data error*/
					data->bytes_xfered = data->blocks * data->blksz;
				}

				if(data->stop && !data->mrq->sbc) {
					/* no data error & use stop. send stop */
#ifndef CONFIG_SDP_MMC_USE_AUTO_STOP_CMD
					mmch_start_mrq(sdpmmch, sdpmmch->mrq->stop);
#else
					sdpmmch->cmd = sdpmmch->mrq->stop;
					sdpmmch->cmd_start_time = jiffies;
#endif
					state = MMCH_STATE_SENDING_STOP;
					break;
				}

				state = MMCH_STATE_REQUEST_ENDING;

			} else if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_DATA, &sdpmmch->event)) {
				/* Non DTO and error! */
				struct mmc_data *data = sdpmmch->mrq->data;

				data->error = mmch_mmc_data_error(sdpmmch, sdpmmch->intr_status, "FSM Error in PROCESSING_DATA(NonDTO)!");
				intr_status  &= ~MMCH_MINTSTS_ERROR_DATA;

				state = MMCH_STATE_ERROR_DATA;
			}
			break;

		case MMCH_STATE_SENDING_STOP:

			/* chack error */
			if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_CMD, &sdpmmch->event)) {
				sdpmmch->mrq->stop->error = mmch_mmc_cmd_error(sdpmmch, sdpmmch->intr_status);
				intr_status  &= ~MMCH_MINTSTS_ERROR_CMD;
				state = MMCH_STATE_ERROR_CMD;
			}


			if(test_and_clear_bit(MMCH_EVENT_HOST_CMDDONE_STOP, &sdpmmch->event)) {
				if(sdpmmch->mrq->stop->flags & MMC_RSP_PRESENT){
					mmch_get_resp(sdpmmch, sdpmmch->mrq->stop);
				}

				if (sdpmmch->mrq->stop->error) {
					state = MMCH_STATE_ERROR_CMD;
					break;
				}

				state = MMCH_STATE_REQUEST_ENDING;
				break;
			}
#ifdef CONFIG_SDP_MMC_USE_AUTO_STOP_CMD
			else if(test_and_clear_bit(MMCH_EVENT_HOST_ACD, &sdpmmch->event)) {
				/* read auto stop cmd response at RESP1 reg */
				if(sdpmmch->mrq->stop->flags & MMC_RSP_PRESENT){
					sdpmmch->mrq->stop->resp[0] = mmch_readl(sdpmmch->base + MMCH_RESP1);
				}

				if (sdpmmch->mrq->stop->error) {
					state = MMCH_STATE_ERROR_CMD;
					break;
				}

				state = MMCH_STATE_REQUEST_ENDING;
				break;
			}
#endif
			break;

		case MMCH_STATE_REQUEST_ENDING:

#ifndef CONFIG_ARCH_SDP1412
			/* waiting busy signal */
			if(sdpmmch->cmd->flags & MMC_RSP_BUSY) {
				u64 cmd_timeout =
					sdpmmch->cmd->busy_timeout?
						sdpmmch->cmd->busy_timeout:MMCH_CMD_MAX_TIMEOUT;

				if(mmch_readl(sdpmmch->base+MMCH_STATUS)&MMCH_STATUS_DATA_BUSY) {
					struct timespec nowtime, timeout;

					dev_dbg(&sdpmmch->pdev->dev,
						"DEBUG! CMD%-2d R1b busy waiting max %llums.\n",
						sdpmmch->cmd->opcode, cmd_timeout);

					ktime_get_ts(&nowtime);
					timeout = nowtime;
					timespec_add_ns(&timeout, cmd_timeout*NSEC_PER_MSEC);

					while(mmch_readl(sdpmmch->base+MMCH_STATUS)&MMCH_STATUS_DATA_BUSY) {
						ktime_get_ts(&nowtime);

						if(timespec_compare(&timeout, &nowtime) < 0) {
							dev_err(&sdpmmch->pdev->dev,
								"ERROR! CMD%-2d R1b busy waiting timeout!(%llums)\n",
								sdpmmch->cmd->opcode, cmd_timeout);
							mmch_register_dump(sdpmmch);
							break;
						}
						udelay(1);
					}

					if(timespec_compare(&timeout, &nowtime) >= 0) {
						nowtime = timespec_sub(timeout, nowtime);
						dev_dbg(&sdpmmch->pdev->dev,
							"DEBUG! CMD%-2d R1b busy waiting time is %lldns.\n",
							sdpmmch->cmd->opcode, (cmd_timeout*NSEC_PER_MSEC)-(u64) timespec_to_ns(&nowtime));
					}
				}
			}
#endif

			mrq_done = sdpmmch->mrq;
			state = MMCH_STATE_IDLE;
			break;


		case MMCH_STATE_ERROR_CMD:
			state = MMCH_STATE_REQUEST_ENDING;
			break;

		case MMCH_STATE_ERROR_DATA:
			state = MMCH_STATE_REQUEST_ENDING;
			break;

		case MMCH_STATE_SW_TIMEOUT:
			if(!sdpmmch->in_tuning) {
				if(sw_timeout_count > 2){
					mmch_register_dump(sdpmmch);
#ifdef CONFIG_SDP_BUSERR
					dev_err(&sdpmmch->pdev->dev, "bus error log in sw timeout\n");
#if defined(CONFIG_ARCH_SDP1803)
					sdp_mmch_pll_counter(sdpmmch);
#endif
					sdp_buserr_show();
					sdp_busmon_show();
#endif
				}
			}
			sdp_mmch_host_reset(sdpmmch);
			state = MMCH_STATE_REQUEST_ENDING;
			break;

		default:
			dev_err(&sdpmmch->pdev->dev, "Unknown State!!!(%d)\n", state);
			BUG();
			break;
		}

		/* check timeout event */
		if(test_and_clear_bit(MMCH_EVENT_SW_TIMEOUT, &sdpmmch->event)) {
			if(init_state == state && state != MMCH_STATE_IDLE) {
				char* kern_level = KERN_ERR;
				char dmacnt_str[100] = "";

				if(state == MMCH_STATE_SENDING_DATA || state == MMCH_STATE_PROCESSING_DATA) {
					snprintf(dmacnt_str, 100, ", ReqBytes %u, BIU %u, CIU %u",
						mmch_readl(sdpmmch->base+MMCH_BYTCNT),
						mmch_readl(sdpmmch->base+MMCH_TBBCNT),
						mmch_readl(sdpmmch->base+MMCH_TCBCNT)*((mmch_readl(sdpmmch->base+MMCH_UHS_REG)&0x10000)?2:1));
				}

				if(sdpmmch->in_tuning || process_interval > sdpmmch->timeout_ms) {
					if(pre_state == MMCH_STATE_SENDING_SBC ||pre_state == MMCH_STATE_SENDING_CMD || pre_state == MMCH_STATE_SENDING_STOP) {
						sdpmmch->cmd->error = ETIMEDOUT;
					} else if(sdpmmch->mrq->data) {
						sdpmmch->mrq->data->error = ETIMEDOUT;
					}
					state = MMCH_STATE_SW_TIMEOUT;
					sw_timeout_count++;

					if(sdpmmch->in_tuning) {
						kern_level = KERN_DEBUG;
					}
					dev_printk(kern_level, &sdpmmch->pdev->dev,
						"Request#%06llu(CMD%-2d) SW Timeout(%lums[%lums], event 0x%04lx accum 0x%04lx%s) in %s!!\n",
						sdpmmch->request_count, sdpmmch->cmd->opcode, sdpmmch->timeout_ms, process_interval, sdpmmch->event, sdpmmch->event_accumulated,
						dmacnt_str, mmch_state_string(state));
				} else {
					dev_printk(kern_level, &sdpmmch->pdev->dev,
						"request#%06llu(CMD%-2d) is not handled by polling timer!(%lums[%lums, %lums], event 0x%04lx, accum 0x%04lx, dma_status : 0x%x  %s%s)\n",
						sdpmmch->request_count, sdpmmch->cmd->opcode, sdpmmch->polling_ms, polling_interval, process_interval, sdpmmch->event, sdpmmch->event_accumulated, mmch_readl(sdpmmch->base+MMCH_STATUS),
						mmch_state_string(state), dmacnt_str);
				}
			} else if((init_state != state)) {
				sw_timeout_handled = true;
				if(sdpmmch->cmd) {
					first_handled_cmd = sdpmmch->cmd;
				}
			}
		}


#ifdef MMCH_PRINT_STATE
		if(pre_state != state) {
			dev_printk(MMCH_PRINT_LEVEL, &sdpmmch->pdev->dev,
				"FSM CMD%-2d %s->%s, event 0x%08lx, accum 0x%08lx, hoststaus 0x%08x",
				(mmch_readl(sdpmmch->base+MMCH_STATUS)>>11)&0x3F, mmch_state_string(pre_state), mmch_state_string(state),
				sdpmmch->event, sdpmmch->event_accumulated, sdpmmch->intr_status);
		}
#endif

	} while(pre_state != state);

	if(sw_timeout_handled) {
		unsigned long handled_event = init_event | (sdpmmch->event_accumulated&~init_event_accm);

		handled_event &= ~sdpmmch->event;

		dev_info(&sdpmmch->pdev->dev, "request#%06llu(CMD%-2d) is handled by polling timer.(%lums[%lums, %lums], event 0x%04lx, accum 0x%04lx, handled 0x%04lx, %s->%s)\n",
			sdpmmch->request_count, first_handled_cmd?first_handled_cmd->opcode:99, sdpmmch->polling_ms, polling_interval, process_interval, sdpmmch->event, sdpmmch->event_accumulated, handled_event,
			mmch_state_string(init_state), mmch_state_string(state));
	}

	sdpmmch->state = state;

	spin_unlock(&sdpmmch->state_lock);

	/* Finish request */
	if (mrq_done) {
		BUG_ON(state != MMCH_STATE_IDLE);

		tasklet_disable_nosync(&sdpmmch->tasklet);

		spin_lock_irqsave(&sdpmmch->lock, flags);
		mmch_request_end(sdpmmch);
		spin_unlock_irqrestore(&sdpmmch->lock, flags);

		if(mrq_done->done) {/* not polling request */
			mmc_request_done(sdpmmch->host, mrq_done);
		}
	} else {
		sdpmmch->polling_timer_start = jiffies;
		mod_timer(&sdpmmch->timeout_timer, sdpmmch->polling_timer_start + msecs_to_jiffies(sdpmmch->polling_ms));
	}

#ifdef MMCH_PRINT_STATE
	{
		unsigned long handled_event = init_event | (sdpmmch->event_accumulated&~init_event_accm);

		handled_event &= ~sdpmmch->event;
		dev_printk(MMCH_PRINT_LEVEL, &sdpmmch->pdev->dev,
			"FSM ===== %s->%s, event 0x%08lx, accum 0x%08lx, handled 0x%08lx, hoststaus 0x%08x",
			mmch_state_string(init_state), mmch_state_string(state), sdpmmch->event, sdpmmch->event_accumulated, handled_event, sdpmmch->intr_status);
	}
#endif
}

static void
mmch_tasklet(unsigned long data)
{
	SDP_MMCH_T *sdpmmch = (SDP_MMCH_T *)data;

	if(test_bit(MMCH_EVENT_XFER_ERROR, &sdpmmch->event)) {
		unsigned long flags;

		if(sdpmmch->dma_status & MMCH_IDSTS_DU) {
			mmch_dma_intr_dump(sdpmmch);

			dev_err(&sdpmmch->pdev->dev, "DMA Error: Descriptor Unavailable!\n");
			dev_err(&sdpmmch->pdev->dev, "DMA Desc Base address drv:0x%08llx, reg:0x%08x\n",
							(u64)sdpmmch->dmadesc_pbase,
							mmch_readl(sdpmmch->base+MMCH_DBADDR));
			dev_err(&sdpmmch->pdev->dev, "DMA Desc Current address 0x%08x\n",
							mmch_readl(sdpmmch->base+MMCH_DSCADDR));
		}

		if(sdpmmch->dma_status & MMCH_IDSTS_FBE) {
			dev_err(&sdpmmch->pdev->dev, "DMA Error: Fatal Bus Error (when %s)\n",
				(sdpmmch->dma_status&MMCH_IDSTS_EB)==MMCH_IDSTS_EB_TRANS?"TX":"RX");
		}
		mmch_register_dump_notitle(sdpmmch);
		pr_info("\n");

		spin_lock_irqsave(&sdpmmch->lock, flags);
		sdpmmch->dma_status &= ~(MMCH_IDSTS_AIS|MMCH_IDSTS_DU|MMCH_IDSTS_FBE);
		spin_unlock_irqrestore(&sdpmmch->lock, flags);
	}

	if(sdpmmch->event)
		mmch_state_machine(sdpmmch, sdpmmch->intr_status);
}


#define SDP_MMCH_IRQ_IN_TIMEOUT	(-1234)
#define SDP_MMCH_IRQ_IN_POLLING	(-5678)

static irqreturn_t
sdp_mmch_isr(int irq, void *dev_id)
{
	SDP_MMCH_T *sdpmmch = (SDP_MMCH_T*)dev_id;
	u32 intr_status = 0;
	u32 dma_status = 0;
	int i = 0;

	BUG_ON(!irqs_disabled());

#ifdef MMCH_PRINT_ISR_TIME
	struct timeval tv_start, tv_end;
	unsigned long us = 0;
	enum sdp_mmch_state old_state = sdpmmch->state;
	int cmd = mmch_readl(sdpmmch->base + MMCH_CMD)&0x3F;
	char type[10];
	int start_fifo = MMCH_STATUS_GET_FIFO_CNT(mmch_readl(sdpmmch->base+MMCH_STATUS));
	if(sdpmmch->cmd)
		sprintf(type, "%s", !sdpmmch->cmd->data?"NoData":(sdpmmch->cmd->data->flags&MMC_DATA_READ?"READ  ":"WRITE "));
	else
		sprintf(type, "NoCMD");

	do_gettimeofday(&tv_start);
#endif

	if(unlikely(!sdpmmch)) {
		dev_err(&sdpmmch->pdev->dev, "ISR host is not allocated!");
		return IRQ_NONE;
	}

	spin_lock(&sdpmmch->lock);

	/* mrq is NULL, not in request.. */
	if(!sdpmmch->mrq) {
		dev_err(&sdpmmch->pdev->dev, "ISR mrq is NULL, host state is IDLE! irq : %d", irq);
		mmch_register_dump_notitle(sdpmmch);
		mmch_writel(MMCH_RINTSTS_ALL_ENABLED, sdpmmch->base + MMCH_RINTSTS); wmb();
		mmch_writel(MMCH_IDSTS_INTR_ALL, sdpmmch->base + MMCH_IDSTS); wmb();
		spin_unlock(&sdpmmch->lock);
		return IRQ_HANDLED;
	}

#ifdef MMCH_TEST_SW_TIMEOUT
	/* test code for sw timeout! */
	if(irq != SDP_MMCH_IRQ_IN_TIMEOUT && !(sdpmmch->in_tuning) && sdpmmch->cmd && sdpmmch->cmd->opcode != 13) {
		if(get_random_int() % 100 == 0) {
			mmch_writel(readl(sdpmmch->base+MMCH_CTRL)&~MMCH_CTRL_INT_ENABLE, sdpmmch->base+MMCH_CTRL);
			spin_unlock(&sdpmmch->lock);
			return IRQ_HANDLED;
		}
	}
	mmch_writel(readl(sdpmmch->base+MMCH_CTRL)|MMCH_CTRL_INT_ENABLE, sdpmmch->base+MMCH_CTRL);
#endif

	//update time
	sdpmmch->isr_time_pre = sdpmmch->isr_time_now;
	do_gettimeofday(&sdpmmch->isr_time_now);

	intr_status = mmch_readl(sdpmmch->base + MMCH_MINTSTS);
	sdpmmch->intr_status |= intr_status;
	sdpmmch->intr_accumulated |= intr_status;

	dma_status = mmch_readl(sdpmmch->base + MMCH_IDSTS);
	/* overwrite int status */
	sdpmmch->dma_status |= dma_status & MMCH_IDSTS_INTR_ALL;
	sdpmmch->idmac_accumulated |= dma_status & MMCH_IDSTS_INTR_ALL;

	/* set dma new state */
	sdpmmch->dma_status &= MMCH_IDSTS_INTR_ALL;
	sdpmmch->dma_status |= dma_status & (~MMCH_IDSTS_INTR_ALL);

	if(irq != SDP_MMCH_IRQ_IN_TIMEOUT && irq != SDP_MMCH_IRQ_IN_POLLING && !intr_status && !(dma_status & MMCH_IDSTS_INTR_ALL)) {
		dev_err(&sdpmmch->pdev->dev, "ISR pending interrupt is zero!! irq : %d", irq);
		spin_unlock(&sdpmmch->lock);
		return IRQ_NONE;
	}


	mmch_writel(intr_status, sdpmmch->base + MMCH_RINTSTS); wmb();		// 111202
	mmch_writel(dma_status, sdpmmch->base + MMCH_IDSTS); wmb();	// 111202

	/* set sw timeout event */
	if(irq == SDP_MMCH_IRQ_IN_TIMEOUT) {
		set_bit(MMCH_EVENT_SW_TIMEOUT, &sdpmmch->event);
	}

	for(i = 0; i < 16; i++) {
		u32 now_intr = 0x1u<<i;

		if(intr_status & now_intr) {
			int event_bit = -1;

			if(now_intr & MMCH_MINTSTS_CMD_DONE) {
				if(sdpmmch->mrq) {
					if( sdpmmch->cmd == sdpmmch->mrq->sbc ) {
						event_bit = MMCH_EVENT_HOST_CMDDONE_SBC;
					} else if( sdpmmch->cmd == sdpmmch->mrq->stop ) {
						event_bit = MMCH_EVENT_HOST_CMDDONE_STOP;
					} else {
						event_bit = MMCH_EVENT_HOST_CMDDONE;
					}
				}
				sdpmmch->intr_status &= ~MMCH_MINTSTS_CMD_DONE;

			} else if(now_intr & MMCH_MINTSTS_CD) {
				event_bit = MMCH_EVENT_HOST_CD;
				sdpmmch->intr_status &= ~MMCH_MINTSTS_CD;
			} else if(now_intr & MMCH_MINTSTS_DTO) {
				event_bit = MMCH_EVENT_HOST_DTO;
				sdpmmch->intr_status &= ~MMCH_MINTSTS_DTO;
			} else if(now_intr & MMCH_MINTSTS_ACD) {
				event_bit = MMCH_EVENT_HOST_ACD;
				sdpmmch->intr_status &= ~MMCH_MINTSTS_ACD;
			} else if(now_intr & MMCH_MINTSTS_TXDR) {
				event_bit = MMCH_EVENT_HOST_TXDR;
				sdpmmch->intr_status &= ~MMCH_MINTSTS_TXDR;
			} else if(now_intr & MMCH_MINTSTS_RXDR) {
				event_bit = MMCH_EVENT_HOST_RXDR;
				sdpmmch->intr_status &= ~MMCH_MINTSTS_RXDR;
			} else if(now_intr & MMCH_MINTSTS_ERROR_CMD) {
				event_bit = MMCH_EVENT_HOST_ERROR_CMD;
				//sdpmmch->intr_status &= ~MMCH_MINTSTS_ERROR_CMD;
				//sdpmmch->data_error_status =| intr_status & MMCH_MINTSTS_ERROR_CMD;
			} else if(now_intr&MMCH_MINTSTS_ERROR_DATA) {
				event_bit = MMCH_EVENT_HOST_ERROR_DATA;
				//sdpmmch->intr_status &= ~MMCH_MINTSTS_ERROR_DATA;
				//sdpmmch->data_error_status =| intr_status & MMCH_MINTSTS_ERROR_DATA;
			}

			if(event_bit < 0) {
				dev_err(&sdpmmch->pdev->dev, "ISR can not parsing intrrupt bit%d(intr 0x%08x)\n", i, intr_status);
			} else if(test_and_set_bit(event_bit, &sdpmmch->event)) {
				if((event_bit != MMCH_EVENT_HOST_ERROR_CMD) && (event_bit != MMCH_EVENT_HOST_ERROR_DATA)) {
					dev_info(&sdpmmch->pdev->dev, "ISR already set event bit%d(intr bit%d)(event 0x%08lx, intr 0x%08x)\n", event_bit, i, sdpmmch->event, intr_status);
				}
			}
		}
	}

	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
		if(sdpmmch->dma_status & MMCH_IDSTS_CES){
			sdpmmch->dma_status &= ~(MMCH_IDSTS_CES);
		}

		if(sdpmmch->dma_status & MMCH_IDSTS_AIS) {
			set_bit(MMCH_EVENT_XFER_ERROR, &sdpmmch->event);
		}


		if(sdpmmch->dma_status & MMCH_IDSTS_NIS) {
			if(!sdpmmch->mrq && !sdpmmch->in_tuning) {
				sdpmmch->dma_status = 0;
			} else {
				sdpmmch->dma_status &= ~(MMCH_IDSTS_NIS|MMCH_IDSTS_RI|MMCH_IDSTS_TI);
				set_bit(MMCH_EVENT_XFER_DONE, &sdpmmch->event);
			}
		}

	} else {
		if(test_and_clear_bit(MMCH_EVENT_HOST_ERROR_DATA, &sdpmmch->event) && sdpmmch->intr_status&MMCH_MINTSTS_HTO) {
			//if only HTO error
			mmch_register_dump(sdpmmch);
			dev_info(&sdpmmch->pdev->dev, "ISR Try to recovery in HTO... (interupt %#x)\n", sdpmmch->intr_status);
			sdpmmch->intr_status &= ~MMCH_MINTSTS_HTO;
		}

		if(test_and_clear_bit(MMCH_EVENT_HOST_DTO, &sdpmmch->event) &&
			sdpmmch->mrq && sdpmmch->mrq->data && (sdpmmch->mrq->data->flags & MMC_DATA_READ))
		{
			if(sdpmmch->sg) {
				mmch_pio_read_data(sdpmmch);
			}

			/* disable RX/TX interrupt */
			mmch_writel(mmch_readl(sdpmmch->base+MMCH_INTMSK)&~(MMCH_INTMSK_RXDR|MMCH_INTMSK_TXDR), sdpmmch->base+MMCH_INTMSK);
			set_bit(MMCH_EVENT_XFER_DONE, &sdpmmch->event);

			/* for debug */
			if(sdpmmch->mrq->data->bytes_xfered < sdpmmch->mrq_data_size)
			{
				/* DTO... but buffer is remained */
				dev_err(&sdpmmch->pdev->dev, "ISR DTO... but buffer is remained.. %#x/%#xbytes\n",
					sdpmmch->mrq->data->bytes_xfered, sdpmmch->mrq_data_size);
				if(sdpmmch->sg) {
					dev_err(&sdpmmch->pdev->dev, "ISR Now SG(0x%p) = [len %#x, pio_off %#x]\n",
						sdpmmch->sg, sdpmmch->sg->length, sdpmmch->pio_offset);
				} else {
					dev_err(&sdpmmch->pdev->dev, "ISR Now SG is NULL\n");
				}
				set_bit(MMCH_EVENT_XFER_ERROR, &sdpmmch->event);
			}
		}

		if(test_and_clear_bit(MMCH_EVENT_HOST_RXDR, &sdpmmch->event))
		{
			if(sdpmmch->sg)
				mmch_pio_read_data(sdpmmch);
		}

		if(test_and_clear_bit(MMCH_EVENT_HOST_TXDR, &sdpmmch->event))
		{
			if(sdpmmch->sg)
				mmch_pio_write_data(sdpmmch);
		}
	}

	sdpmmch->event_accumulated |= sdpmmch->event;
	//mmch_state_machine(sdpmmch, sdpmmch->intr_status);

	if(irq != SDP_MMCH_IRQ_IN_TIMEOUT && irq != SDP_MMCH_IRQ_IN_POLLING) {
		tasklet_schedule(&sdpmmch->tasklet);
	}

#ifdef MMCH_PRINT_ISR_TIME
	{
		int end_fifo = MMCH_STATUS_GET_FIFO_CNT(mmch_readl(sdpmmch->base+MMCH_STATUS));

		do_gettimeofday(&tv_end);
		us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
			     (tv_end.tv_usec - tv_start.tv_usec);

		dev_printk(MMCH_PRINT_LEVEL, &sdpmmch->pdev->dev,
			"ISR %3luus %s(CMD%-2d, mmc 0x%08x, dma 0x%08x, event 0x%08lx, Type %s %s->%s), FIFO %d->%d, off %#x, ISR Interval: %lldns\n",
			us, (irq == SDP_MMCH_IRQ_IN_TIMEOUT)?"called timeout cb ":"", cmd, intr_status, dma_status, sdpmmch->event, type, mmch_state_string(old_state), mmch_state_string(sdpmmch->state), start_fifo, end_fifo, sdpmmch->pio_offset, timeval_to_ns(&sdpmmch->isr_time_now)-timeval_to_ns(&sdpmmch->isr_time_pre));
	}
#endif

	spin_unlock(&sdpmmch->lock);

	return IRQ_HANDLED;
}

static void
mmch_timeout_callback(unsigned long data)
{
	SDP_MMCH_T *sdpmmch = (SDP_MMCH_T*)data;
	unsigned long flags;
	enum sdp_mmch_state now_state;

//	dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "TOBC start");

	local_irq_save(flags);

	if(tasklet_trylock(&sdpmmch->tasklet)) {
		/* if tasklet is not running, direct call tasklet */
		now_state = sdpmmch->state;

		if(now_state != MMCH_STATE_IDLE) {
	//		unsigned long process_interval = jiffies_to_msecs(jiffies - sdpmmch->cmd_start_time);
	//		unsigned long polling_interval = jiffies_to_msecs(jiffies - sdpmmch->polling_timer_start);

			sdp_mmch_isr(SDP_MMCH_IRQ_IN_TIMEOUT, sdpmmch);/* in this case, set irq# is -1234 */

	//		if(!sdpmmch->in_tuning) {
	//			dev_info(&sdpmmch->pdev->dev, "TOCB request#%06llu(CMD%-2d) polling timer expired! (%lums[%lums, %lums], event 0x%08lx, accum 0x%08lx, %s)\n",
	//				sdpmmch->request_count, sdpmmch->cmd?sdpmmch->cmd->opcode:99, sdpmmch->polling_ms, polling_interval, process_interval,
	//				sdpmmch->event, sdpmmch->event_accumulated, mmch_state_string(now_state));
	//		}
		}
		mmch_tasklet((unsigned long)sdpmmch);
		tasklet_unlock(&sdpmmch->tasklet);
	} else {
		/* else schedule tasklet */
		dev_info(&sdpmmch->pdev->dev, "TOCB request#%06llu(CMD%-2d) tasklet is running. so schedule first.\n", sdpmmch->request_count, sdpmmch->cmd?sdpmmch->cmd->opcode:99);
		tasklet_hi_schedule_first(&sdpmmch->tasklet);
	}
	local_irq_restore(flags);

//	dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "TOBC end");
}

#define MAX_TUNING_LOOP        (20)
static const u8 tuning_blk_pattern_4bit[] = { 
	0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc, 
	0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef, 
	0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb, 
	0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef, 
	0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c, 
	0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee, 
	0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff, 
	0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde, 
}; 
 
static const u8 tuning_blk_pattern_8bit[] = { 
	0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 
	0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc, 
	0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff, 
	0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff, 
	0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd, 
	0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb, 
	0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff, 
	0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff, 
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 
	0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 
	0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 
	0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 
	0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 
	0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 
	0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 
	0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

static int sdp_mmch_regset(SDP_MMCH_T *sdpmmch, struct sdp_mmch_reg_set *regset)
{
	u8 * __iomem iomem;

	if(!regset) {
		dev_err(&sdpmmch->pdev->dev, "sdp_mmch_regset: regset is NULL!!\n");
		return -EINVAL;
	}

	iomem = ioremap_nocache((u32) regset->addr, sizeof(u32));
	if(iomem) {
#ifdef CONFIG_ARCH_SDP1404
		/* Hawk-P 0x100000A8[15] bit inversion fixup */
		if(regset->addr == 0x100000A8)
			mmch_writel( ((readl(iomem)&~((u32)regset->mask)) | (u32)(regset->value&(regset->mask)))^0x0008000U, iomem);
		else
#endif
		mmch_writel( (readl(iomem)&~((u32)regset->mask)) | (u32)(regset->value&regset->mask), iomem);

		wmb(); udelay(1);
		dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev,
			"sdp_mmch_regset 0x%p - 0x%08llx, mask 0x%08llx, val 0x%08x, readl 0x%08x\n",
			regset, (u64)regset->addr, (u64)regset->mask, regset->value, readl(iomem));
		iounmap(iomem);
		return 0;
	} else {
		dev_err(&sdpmmch->pdev->dev, "sdp_mmch_regset: ioremap failed!! addr:0x%08llx\n", (u64)regset->addr);
		return -ENOMEM;
	}
	return 0;
}

static int sdp_mmch_set_tune_value(SDP_MMCH_T *sdpmmch, struct sdp_mmch_reg_list *table, int idx) {
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	int setidx, ret;

	if( (platdata->tuning_set_size * (idx+1)) > table->list_num) {
		dev_err(&sdpmmch->pdev->dev, "sdp_mmch_set_tune_value: invalied index %d\n", idx);
		return -EINVAL;
	}

	for(setidx = 0; setidx < platdata->tuning_set_size; setidx++) {
		ret = sdp_mmch_regset(sdpmmch, &table->list[(idx*platdata->tuning_set_size) + setidx]);
		if(ret < 0) {
			return ret;
		}
	}
	return 0;
}
static int sdp_mmch_set_hs200_default(SDP_MMCH_T *sdpmmch)
{
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	int ret, i;

	for (i = 0; i < platdata->tuning_hs200_drv_default.list_num; i++) {
		ret = sdp_mmch_regset(sdpmmch, &platdata->tuning_hs200_drv_default.list[i]);
		if(ret < 0) {
			return ret;
		}
	}

	for (i = 0; i < platdata->tuning_hs200_sam_default.list_num; i++) {
		ret = sdp_mmch_regset(sdpmmch, &platdata->tuning_hs200_sam_default.list[i]);
		if(ret < 0) {
			return ret;
		}
	}
	return 0;
}

static int sdp_mmch_set_hs400_default(SDP_MMCH_T *sdpmmch)
{
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	int ret, i;

	for (i = 0; i < platdata->tuning_hs400_drv_default.list_num; i++) {
		ret = sdp_mmch_regset(sdpmmch, &platdata->tuning_hs400_drv_default.list[i]);
		if(ret < 0) {
			return ret;
		}
	}
#ifdef CONFIG_ARCH_SDP1404
	/* XXX hawk-p EVT0 HS400 150MHz drv fixup.... */
	if(platdata->processor_clk <= (600 * 1000000) ) {
		u32 temp = 0;
		dev_info(&sdpmmch->pdev->dev, "Hawk-P EVT0 MMC DRV delay fixup. input clk %uHz\n", platdata->processor_clk);
		temp = mmch_readl(sdpmmch->base + MMCH_CLKSEL);
		temp &= ~0x00C78000UL;
		mmch_writel((temp|0x00C08000)^0x00008000, sdpmmch->base + MMCH_CLKSEL);
	}
#endif
	if(sdpmmch->successed_rdqs >= 0){
			dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "rdqs_value was changed..\nprevious rdqs : %d\ncurrent rdqs : %d\n", 
						platdata->tuning_hs400_sam_default.list[1].value, sdpmmch->successed_rdqs);
			platdata->tuning_hs400_sam_default.list[1].value=(u32)sdpmmch->successed_rdqs;
	}

	for (i = 0; i < platdata->tuning_hs400_sam_default.list_num; i++) {
		ret = sdp_mmch_regset(sdpmmch, &platdata->tuning_hs400_sam_default.list[i]);
		if(ret < 0) {
			return ret;
		}
	}
	return 0;
}

static int
sdp_mmch_find_median_in_bitmap(SDP_MMCH_T *sdpmmch, const unsigned long *bitmap, int bits, int *window_size)
{
	int max_start = 0, max_len = 0, now_bit = 0, pre_bit = 0;
	int start = 0, len = 0, i;

	if(bitmap_empty(bitmap, bits)) {
		return -EINVAL;
	}

	for( i = 0; i < bits * 2; i++) {
		now_bit = test_bit(i % bits, bitmap);

		if(!pre_bit && now_bit) {
			start = i;
			len = 1;
		} else if(pre_bit && now_bit) {
			len++;
		} else if(pre_bit && !now_bit) {
			if(len > max_len) {
				max_start = start;
				max_len = len;
			}
			start = 0;
			len = 0;
		}

		pre_bit = now_bit;
	}

	/* check last */
	if(len > max_len) {
		max_start = start;
		max_len = len;
	}


	if(window_size) {
		*window_size = max_len;
	}

	return (max_start+((max_len-1)/2)) % bits;
}


/* in mmc_ops.c */
extern int mmc_send_status(struct mmc_card *card, u32 *status);
extern int mmc_switch(struct mmc_card *card, u8 set, u8 index, u8 value, unsigned int timeout_ms);

static int
sdp_mmch_send_status(SDP_MMCH_T *sdpmmch, u32 *status)
{
	int ret = 0;
	struct mmc_host *host = sdpmmch->host;
	struct mmc_card *dummy_card;

	dummy_card = kmalloc(GFP_KERNEL, sizeof(struct mmc_card));
	if(!dummy_card) {
		dev_err(&sdpmmch->pdev->dev, "sdp_mmch_send_status: kmalloc return NULL\n");
		BUG();
	}

	dummy_card->host = host;
	dummy_card->rca = 1;

	ret = mmc_send_status(dummy_card, status);
	kfree(dummy_card);
	return ret;
}

#define SAM_MAX_COUNT 32
static unsigned long
sdp_mmch_do_sam_tuning(SDP_MMCH_T *sdpmmch, u32 opcode, struct seq_file *s)
{
	struct mmc_host *host = sdpmmch->host;
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	struct sdp_mmch_reg_list *table = NULL;

	int tune_values = 0;
	const u8 *pattern = NULL;
	static u8 __aligned(128) tb[512];
	size_t tb_len;
	int i;
	char bit_str[100] = {0};
	int sam_median = 0, sam_maxlen;

	unsigned long sam_map = 0;

	static DECLARE_BITMAP(bitmap_sam, SAM_MAX_COUNT);
	bitmap_zero(bitmap_sam, SAM_MAX_COUNT);


	if(opcode == MMC_READ_SINGLE_BLOCK) {
		/* for Non HS200Mode(SDR/DDR50 Mode) tunning */
		tb_len = 512;
	} else {
		if(host->ios.bus_width == MMC_BUS_WIDTH_4) {
			pattern = tuning_blk_pattern_4bit;
			tb_len = 64;
		} else if(host->ios.bus_width == MMC_BUS_WIDTH_8) {
			pattern = tuning_blk_pattern_8bit;
			tb_len = 128;
		} else {
			return (unsigned long) -ENOTSUPP;
		}
	}

	if(sdpmmch->is_hs400) {
		table = &platdata->tuning_table_hs400;
	} else {
		table = &platdata->tuning_table;
	}
	tune_values = (table->list_num/platdata->tuning_set_size);

	for(sam_map = 0, i = 0; i < tune_values; i++) {
		struct scatterlist sg;
		struct mmc_request mrq = {0};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};

		sdp_mmch_set_tune_value(sdpmmch, table, i);


		sdp_mmch_host_reset(sdpmmch);

		cmd.opcode = opcode;
		cmd.arg = 0; 
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		memset(tb, 0x0, sizeof(tb));
		sg_init_one(&sg, tb, (u32) tb_len);

		dma_map_sg(&sdpmmch->pdev->dev, &sg, 1, DMA_TO_DEVICE);
		dma_unmap_sg(&sdpmmch->pdev->dev, &sg, 1, DMA_TO_DEVICE);

		data.blksz = (u32) tb_len;
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.sg = &sg;
		data.sg_len = 1;

		mrq.cmd = &cmd;
		//mrq.stop = &stop;//remove stop. hynix emmc stuck issue
		mrq.data = &data;

		mmc_wait_for_req(host, &mrq);

		if(opcode == MMC_READ_SINGLE_BLOCK) {
			if(!mrq.cmd->error && (sdpmmch->is_hs400 || !mrq.data->error)) {
				sam_map |= 1UL<<i;
				bitmap_set(bitmap_sam, i, 1);
				dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "%2d pattern match!\n", i);
			} else {
				dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "%2d pattern miss match!(req ret: CMD %d, DATA %d)\n", i, mrq.cmd->error, mrq.data->error);
			}
		}
		else
		{
			if(!mrq.cmd->error && (sdpmmch->is_hs400 || !mrq.data->error) /*XXX && !memcmp(tb, pattern, tb_len)*/) {
				sam_map |= 1UL<<i;
				bitmap_set(bitmap_sam, i, 1);
				dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "%2d pattern match!\n", i);
			} else {
				dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "%2d pattern miss match!(req ret: CMD %d, DATA %d, MEMCMP %d)\n", i, mrq.cmd->error, mrq.data->error, memcmp(tb, pattern, tb_len));
				print_hex_dump_bytes("Pattern Ref: ", DUMP_PREFIX_ADDRESS, pattern, tb_len);
				printk(KERN_DEBUG "----------------------------------------------------------------------------------------\n");
				print_hex_dump_bytes("Pattern Rcv: ", DUMP_PREFIX_ADDRESS, tb, tb_len);
			}
		}
	}
	sam_median = sdp_mmch_find_median_in_bitmap(sdpmmch, bitmap_sam, tune_values, &sam_maxlen);
	bitmap_scnlistprintf(bit_str, ARRAY_SIZE(bit_str), bitmap_sam, tune_values);
	
	if(s!=NULL){
		seq_printf(s, "%u / %s / %d\n", tune_values, bit_str, sam_median);
	}

	return sam_map;
}

static int mmc_sam_show(struct seq_file *s, void *data)
{
	struct mmc_host *host = s->private;

	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	sdpmmch->in_tuning = true;

	sdp_mmch_do_sam_tuning(sdpmmch, MMC_READ_SINGLE_BLOCK, s);

	sdpmmch->in_tuning = false;

	mmc_release_host(host);

	return 0;
}

static int mmc_sam_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_sam_show, inode->i_private);
}

static const struct file_operations mmc_sam_fops = {
	.open		= mmc_sam_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static unsigned long
sdp_mmch_do_golfs_drv_tuning(SDP_MMCH_T *sdpmmch)
{
	struct mmc_host *host = sdpmmch->host;
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	struct sdp_mmch_reg_list *table = &platdata->tuning_table;

	int tune_values = (table->list_num/platdata->tuning_set_size);

	u8 __aligned(128) tb[512];

	unsigned long drv_map = 0;

	struct sdp_mmch_reg_set drv_regset;
	u32 status = 0;
	int ret;
	int i;

	if(host->ios.bus_width == MMC_BUS_WIDTH_4) {
		memcpy(tb, tuning_blk_pattern_4bit, 64);
	} else if(host->ios.bus_width == MMC_BUS_WIDTH_8) {
		memcpy(tb, tuning_blk_pattern_8bit, 128);
	} else {
		return (unsigned long) -ENOTSUPP;
	}

	drv_regset.addr = platdata->tuning_hs200_drv_default.list[0].addr;
	drv_regset.mask = platdata->tuning_hs200_drv_default.list[0].mask;

	for(i = 0; i < tune_values; i++) {
		struct scatterlist sg;
		struct mmc_request mrq = {0};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};

		/* set drv value */
		drv_regset.value = platdata->tuning_table.list[i].value;
		sdp_mmch_regset(sdpmmch, &drv_regset);

		sdp_mmch_host_reset(sdpmmch);

		cmd.opcode = MMC_PROGRAM_CID;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		sg_init_one(&sg, tb, sizeof(u32)*4);
		data.blksz = sizeof(u32)*4;
		data.blocks = 1;
		data.flags = MMC_DATA_WRITE;
		data.sg = &sg;
		data.sg_len = 1;

		mrq.cmd = &cmd;
		mrq.data = &data;
		mmc_wait_for_req(host, &mrq);


		mrq.cmd = &stop;
		mrq.data = NULL;
		mrq.stop = NULL;
		mmc_wait_for_req(host, &mrq);

		if(!cmd.error) {
			do {
				ret = sdp_mmch_send_status(sdpmmch, &status);
				if(ret) {
					dev_err(&sdpmmch->pdev->dev, "tuning %2d send status fail!(%d)", i, ret);
					break;
				}
				udelay(100);
			} while(R1_CURRENT_STATE(status) == R1_STATE_PRG);
		}

		if(!cmd.error && !data.error) {
			drv_map |= 1UL<<i;
			dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "drv tuning result %2d OK!!(status 0x%08x)\n", i, status);
		} else {
			dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "drv tuning result %2d FAIL(%3d 0x%08x %3d)!!\n", i, cmd.error, cmd.resp[0], data.error);
		}
	}

	return drv_map;
}

static int sdp_mmch_make_bit_srting(unsigned long bitmap, int bits, int median, char *out_str)
{
	int i = 0;
	for(i = 0; i < bits; i++) {
		out_str[bits-1-i] = (bitmap&(0x1UL<<i))?'O':'x';
	}
	out_str[bits-1-median] = 'M';
	out_str[bits] = '\0';
	return 0;
}

static int
sdp_mmch_execute_tuning(struct mmc_host *host, u32 opcode)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	struct sdp_mmch_reg_list *table = NULL;
	int tune_values = 0;

	char bit_str[sizeof(unsigned long)*8 + 1] = {0};
	unsigned long sam_map = 0;
	int median = 0;
	int max_len = 0;
	int error, retry = 0;

	do	{
	sam_map = 0;
	median = 0;
	max_len = 0;
	error = 0;

	if(sdpmmch->is_hs400) {
		sdp_mmch_set_hs400_default(sdpmmch);
		table = &platdata->tuning_table_hs400;
	} else {
		sdp_mmch_set_hs200_default(sdpmmch);
		table = &platdata->tuning_table;
	}
	tune_values = (table->list_num/platdata->tuning_set_size);


	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
		struct sdp_mmch_reg_set rev1_drv_regset;
		rev1_drv_regset.addr = platdata->tuning_hs200_drv_default.list[0].addr;
		rev1_drv_regset.mask = platdata->tuning_hs200_drv_default.list[0].mask;
		rev1_drv_regset.value = 0x04800000;/* rev1 value */
		sdp_mmch_regset(sdpmmch, &rev1_drv_regset);
	}

	if(sdpmmch->is_hs400 && !platdata->is_hs400_cmd_tuning) {
		return 0;
	}

	if(!sdpmmch->is_hs400 && !platdata->is_hs200_tuning) {
		return 0;
	}

	if((platdata->fixups & SDP_MMCH_FIXUP_GOLFP_UD_TUNING_FIXED) && !sdpmmch->tuned_map) {
		dev_info(&sdpmmch->pdev->dev, "forced setting delay 0.\n");
		sdp_mmch_set_tune_value(sdpmmch, table, 0);
		sdpmmch->nr_bit = (table->list_num/platdata->tuning_set_size);
		sdpmmch->tuned_map = 0x1;
		sdpmmch->median = 0;
		return 0;
	}

	dev_info(&sdpmmch->pdev->dev, "start %s%s tuning! total %d tune values\n",
			(opcode == MMC_READ_SINGLE_BLOCK)?"non-":"",
			sdpmmch->is_hs400?"HS400":"HS200",
			table->list_num/platdata->tuning_set_size);

	sdpmmch->in_tuning = true;

	sam_map = sdp_mmch_do_sam_tuning(sdpmmch, opcode, NULL);
	median = sdp_mmch_select_median(sdpmmch, tune_values, sam_map, &max_len);

	if((platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) && max_len < 6) {
		struct sdp_mmch_reg_set rev1_drv_regset;

		sdp_mmch_make_bit_srting(sam_map, tune_values, median, bit_str);
		dev_info(&sdpmmch->pdev->dev, "sam tuning result map 0x%08lx(%s) median %d sam retuning..\n", sam_map, bit_str, median);

		rev1_drv_regset.addr = platdata->tuning_hs200_drv_default.list[0].addr;
		rev1_drv_regset.mask = platdata->tuning_hs200_drv_default.list[0].mask;
		rev1_drv_regset.value = 0x01800000;/* rev1 value */
		sdp_mmch_regset(sdpmmch, &rev1_drv_regset);

		sam_map = sdp_mmch_do_sam_tuning(sdpmmch, opcode, NULL);
		median = sdp_mmch_select_median(sdpmmch, tune_values, sam_map, &max_len);
	}

	if(median >= 0) {
		sdp_mmch_set_tune_value(sdpmmch, table, median);
		sdp_mmch_make_bit_srting(sam_map, tune_values, median, bit_str);
		dev_info(&sdpmmch->pdev->dev, "sam tuning result map 0x%08lx(%s) median %d\n", sam_map, bit_str, median);
	} else {
		sdp_mmch_set_hs200_default(sdpmmch);
		dev_err(&sdpmmch->pdev->dev, "sam tuning result map 0x%08lx tuning fail!\n", sam_map);
		return -EIO;
	}

	sdp_mmch_host_reset(sdpmmch);


	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
		unsigned long drv_map = 0;
		int drv_median = 0;
		struct sdp_mmch_reg_set drv_regset;
		int drv_max_len = 0;

		drv_regset.addr = platdata->tuning_hs200_drv_default.list[0].addr;
		drv_regset.mask = platdata->tuning_hs200_drv_default.list[0].mask;

		/* drv tuning */
		drv_map = sdp_mmch_do_golfs_drv_tuning(sdpmmch);
		drv_median = sdp_mmch_select_median(sdpmmch, tune_values, drv_map, &drv_max_len);

		sdp_mmch_make_bit_srting(drv_map, tune_values, drv_median, bit_str);
		dev_info(&sdpmmch->pdev->dev, "drv tuning result map 0x%08lx(%s) median %d\n", drv_map, bit_str, drv_median);

		/* drv retuning */
		if(drv_max_len < 6) {
			int new_median = (median+(tune_values/2))%tune_values;
			dev_info(&sdpmmch->pdev->dev, "change sam median %d -> %d\n", median, new_median);
			median = new_median;
			sdp_mmch_set_tune_value(sdpmmch, table, median);

			drv_map = sdp_mmch_do_golfs_drv_tuning(sdpmmch);
			drv_median = sdp_mmch_select_median(sdpmmch, tune_values, drv_map, &drv_max_len);

			sdp_mmch_make_bit_srting(drv_map, tune_values, drv_median, bit_str);
			dev_info(&sdpmmch->pdev->dev, "drv retuning result map 0x%08lx(%s) median %d\n", drv_map, bit_str, drv_median);
		}

		if(drv_median >= 0) {
			drv_regset.value = platdata->tuning_table.list[drv_median].value;
			sdp_mmch_regset(sdpmmch, &drv_regset);
		} else {
			dev_err(&sdpmmch->pdev->dev, "drv tuning result fail(%d)! retry tunning.... %d\n", drv_median, ++retry);
			error = -4;
			continue;
		}

	}
	} while((error < 0) && (retry < 10));

	sdpmmch->nr_bit = (table->list_num/platdata->tuning_set_size);
	sdpmmch->tuned_map = sam_map;
	sdpmmch->median = median;

	sdpmmch->in_tuning = false;

	return 0;
}

static inline int
sdp_mmch_rdqs_set(struct mmc_host *host, int rdqs)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	u32 temp, rdqs_raw;


	rdqs_raw = sdp_mmch_rdqs_shift(rdqs);

	if((rdqs_raw < 8) || (rdqs_raw > 0x3FF))
	{
		return -EINVAL;
	}

	//dev_info(&sdpmmch->pdev->dev, "sdp_mmch_rdqs_set: value: %lld, raw: 0x%x\n", rdqs, rdqs_raw);

	temp = mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
	temp &= ~0x3FFU;
	mmch_writel(temp|rdqs_raw, sdpmmch->base + MMCH_DDR200_DLINE_CTRL);

	return 0;
}

/* XXX: for booting time drv tuning.. */
static int __maybe_unused
sdp_mmch_drv_tuning(struct mmc_host *host, struct mmc_card *card, struct seq_file *s)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	static u8 __aligned(128) tb[512] = { 
		0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
		0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
		0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
		0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
		0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
		0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
		0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
		0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
		0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
		0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
		0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
		0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
		0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
		0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
		0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
		0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,

		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
		0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,

		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
		0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,

		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,
		0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,

	};

	int drv_median = 0, drv_maxlen;
	char bit_str[100] = {0};
	u8 part_config, save_part;

	struct sdp_mmch_reg_set drv_regset;
	u32 status = 0;
	int ret;
	int i;
	int tune_values = 0;
#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	u32 drv_values[8] = {
		0x00000000,0x00010000,0x00020000,0x00030000,
		0x00040000,0x00050000,0x00060000,0x00070000,
	};
#else
	const u32 drv_values[] = {
		0x00008000,0x00408000,0x00808000,0x00C08000,
		0x00010000,0x00410000,0x00810000,0x00C10000,
		0x00018000,0x00418000,0x00818000,0x00C18000,
		0x00020000,0x00420000,0x00820000,0x00C20000,
		0x00028000,0x00428000,0x00828000,0x00C28000,
		0x00030000,0x00430000,0x00830000,0x00C30000,
		0x00038000,0x00438000,0x00838000,0x00C38000,
		0x00040000,0x00440000,0x00840000,0x00C40000,
		0x00048000,0x00448000,0x00848000,0x00C48000,
		0x00050000,0x00450000,0x00850000,0x00C50000,
		0x00058000,0x00458000,0x00858000,0x00C58000,
		0x00060000,0x00460000,0x00860000,0x00C60000,
		0x00068000,0x00468000,0x00868000,0x00C68000,
		0x00070000,0x00470000,0x00870000,0x00C70000,
		0x00078000,0x00478000,0x00878000,0x00C78000,
		0x00080000,0x00480000,0x00880000,0x00C80000,
	};
#endif
	static DECLARE_BITMAP(bitmap_drv, ARRAY_SIZE(drv_values));

	bitmap_zero(bitmap_drv, ARRAY_SIZE(drv_values));

	drv_regset.addr = sdpmmch->mem_res->start + MMCH_CLKSEL;

#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	drv_regset.mask = 0x00070000;
#else
	drv_regset.mask = 0x00C78000;
#endif
	drv_regset.value= 0;

#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	tune_values = (int)ARRAY_SIZE(drv_values);
#else
	tune_values = (int)((((mmch_readl(sdpmmch->base + MMCH_CLKSEL)>>24)&0x7)+1) * 2) * 4;
#endif

	mmc_claim_host(host);
	
	//switch to boot1!

	part_config = card->ext_csd.part_config;
	save_part = part_config & EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= 2;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);
	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail!\n", __FUNCTION__);
		return -1;
	}
	card->ext_csd.part_config = part_config;

	sdpmmch->in_tuning = true;
	
	for(i = 0; i < tune_values; i++) {
		struct scatterlist sg;
		struct mmc_request mrq = {0};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data mmcdata = {0};

		/* set drv value */
		drv_regset.value = drv_values[i];
		sdp_mmch_regset(sdpmmch, &drv_regset);

		sdp_mmch_host_reset(sdpmmch);

		cmd.opcode = MMC_WRITE_BLOCK;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		sg_init_one(&sg, tb, ARRAY_SIZE(tb));
		mmcdata.blksz = ARRAY_SIZE(tb);//XXX
		mmcdata.blocks = 1;
		mmcdata.flags = MMC_DATA_WRITE;
		mmcdata.sg = &sg;
		mmcdata.sg_len = 1;

		mrq.cmd = &cmd;
		mrq.data = &mmcdata;
		mmc_wait_for_req(host, &mrq);


		mrq.cmd = &stop;
		mrq.data = NULL;
		mrq.stop = NULL;
		//mmc_wait_for_req(host, &mrq);

		if(!cmd.error) {
			do {
				ret = mmc_send_status(card, &status);
				if(ret) {
					dev_err(&sdpmmch->pdev->dev, "tuning %2d send status fail!(%d)", i, ret);
					break;
				}
				udelay(100);
			} while(R1_CURRENT_STATE(status) == R1_STATE_PRG);
		}

		if(!cmd.error && !mmcdata.error) {
			bitmap_set(bitmap_drv, i, 1);
			dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "drv tuning result %2d OK!!(status 0x%08x)\n", i, status);
		} else {
			dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "drv tuning result %2d FAIL(%3d 0x%08x %3d)!!\n", i, cmd.error, cmd.resp[0], mmcdata.error);
		}
	}
	


	//restore!
	part_config = card->ext_csd.part_config;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= save_part;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);
	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail!\n", __FUNCTION__);
		return -1;
	}
	card->ext_csd.part_config = part_config;

	sdpmmch->in_tuning = false;

	mmc_release_host(host);

	drv_median = sdp_mmch_find_median_in_bitmap(sdpmmch, bitmap_drv, tune_values, &drv_maxlen);
	bitmap_scnlistprintf(bit_str, ARRAY_SIZE(bit_str), bitmap_drv, tune_values);
	dev_info(&sdpmmch->pdev->dev, "drv tuning done! total %u, pass range %s median %d, window size %d\n", tune_values, bit_str, drv_median, drv_maxlen);
	
	if(s!=NULL){
		seq_printf(s, "%u / %s / %d\n", tune_values, bit_str, drv_median);
	}
	
	/* set drv value */
	drv_regset.value = drv_values[drv_median];
	sdp_mmch_regset(sdpmmch, &drv_regset);
	return drv_median;
}

static int sdp_mmch_dbg_write_pattern_set(void *data, u64 val)
{

	struct mmc_host *host = data;
	struct mmc_card *card = host->card;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	u8 part_config, save_part;
	int ret;
	int i;
	static u8 __aligned(128) buf[512] = {0};
	unsigned int error_flag = 0;
	u32 status = 0;

	dev_info(mmc_dev(host), "info write pattern 0x%llx\n", val);

	for(i = 0; i < (int)ARRAY_SIZE(buf); i+=2) {
		buf[i] = val&0xFF;
		buf[i+1] = (val>>8)&0xFF;
	}

	mmc_claim_host(host);
	
	//switch to partition2
	part_config = card->ext_csd.part_config;
	save_part = part_config & EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= 2;
	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);
	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail!\n", __FUNCTION__);
		return -1;
	}
	card->ext_csd.part_config = part_config;



	sdpmmch->in_tuning = true;
	
	for(i = 0; i < 1; i++) {
		struct scatterlist sg;
		struct mmc_request mrq = {0};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data mmcdata = {0};
		
		sdp_mmch_host_reset(sdpmmch);



		cmd.opcode = MMC_WRITE_BLOCK;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		sg_init_one(&sg, buf, ARRAY_SIZE(buf));

		mmcdata.blksz = ARRAY_SIZE(buf);//XXX
		mmcdata.blocks = 1;
		mmcdata.flags = MMC_DATA_WRITE;
		mmcdata.sg = &sg;
		mmcdata.sg_len = 1;
		
		//if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		//	mrq.sbc = &sbc;
		mrq.cmd = &cmd;
		mrq.data = &mmcdata;
		mmc_wait_for_req(host, &mrq);

		
		//mrq.stop= &stop;
		mrq.cmd = &stop;
		mrq.data = NULL;
		mrq.stop = NULL;
		
		//mmch_dbg_prepare_mrq(card, &mrq, 0x0, ARRAY_SIZE(buf), true);

		if(!cmd.error) {
			do {
				ret = mmc_send_status(card, &status);
				if(ret) {
					dev_err(&sdpmmch->pdev->dev, "tuning %2d send status fail!(%d)", i, ret);
					break;
				}
				udelay(10);
			} while(R1_CURRENT_STATE(status) == R1_STATE_PRG);
		}

		if (mmcdata.error) {
			dev_err(&sdpmmch->pdev->dev, "%s mmcdata.error : %d\n", __FUNCTION__, mmcdata.error);
			error_flag=mmcdata.error;
		}

	}
	
	//restore!
	part_config = card->ext_csd.part_config;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= save_part;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);

	sdpmmch->in_tuning = false;

	//mmc_wait_for_req(host, &mrq);


	if (ret || error_flag) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail!\n", __FUNCTION__);
		return -1;
	}
	mmc_release_host(host);
	
	card->ext_csd.part_config = part_config;

	return 0;
}


static int sdp_mmch_dbg_write_pattern_get(void *data, u64 *val)
{
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(sdp_mmch_dbg_write_pattern_fops, sdp_mmch_dbg_write_pattern_get, sdp_mmch_dbg_write_pattern_set,
	"0x%08llx\n");

static int sdp_mmch_dbg_write_boot(struct mmc_host *host, struct mmc_card *card, struct seq_file *s)
{

	//struct mmc_host *host = data;
	//struct mmc_card *card = host->card;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	u8 part_config, save_part;
	int ret;
	int i;
	u8 buf[512] = {0,};
	u64 val=0xaa55;
	unsigned int error_flag = 0;
	u32 status = 0;

	dev_info(mmc_dev(host), "info write boot partition 0x%llx\n", val);

	mmc_claim_host(host);

	//switch to boot1
	part_config = card->ext_csd.part_config;
	save_part = part_config & EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= 2;
	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);

	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail!\n", __FUNCTION__);
		return -1;
	}

	card->ext_csd.part_config = part_config;
	udelay(100);

	sdpmmch->in_tuning = true;

	for(i = 0; i < (int)ARRAY_SIZE(buf); i+=2) {
			buf[i] = val&0xFF;
			buf[i+1] = (val>>8)&0xFF;
	}
	
	for(i = 0; i < 1; i++) {
		struct scatterlist sg;
		struct mmc_request mrq = {0};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};
	
		sdp_mmch_host_reset(sdpmmch);



		cmd.opcode = MMC_WRITE_BLOCK;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		sg_init_one(&sg, buf, ARRAY_SIZE(buf));

		data.blksz = ARRAY_SIZE(buf);//XXX
		data.blocks = 1;
		data.flags = MMC_DATA_WRITE;
		data.sg = &sg;
		data.sg_len = 1;
		
		//if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		//	mrq.sbc = &sbc;
		mrq.cmd = &cmd;
		mrq.data = &data;
		mmc_wait_for_req(host, &mrq);

		if(!cmd.error) {
			do {
				ret = mmc_send_status(card, &status);
				if(ret) {
					dev_err(&sdpmmch->pdev->dev, "tuning %2d send status fail!(%d)", i, ret);
					break;
				}
				udelay(100);
			} while(R1_CURRENT_STATE(status) == R1_STATE_PRG);
		}

		if (data.error) {
			dev_err(&sdpmmch->pdev->dev, "%s data.error : %d\n", __FUNCTION__, data.error);
			error_flag=data.error;
		}

	}

	//restore!
	part_config = card->ext_csd.part_config;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= save_part;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);

	card->ext_csd.part_config = part_config;

	if (ret || error_flag) {
		dev_err(&sdpmmch->pdev->dev, "%s user part switch fail!\n", __FUNCTION__);
		seq_printf(s, "%d", ret|error_flag);
		return -1;
	}


	sdpmmch->in_tuning = false;

	mmc_release_host(host);
	
	seq_printf(s, "%d", ret|error_flag); //return to ASB test code

	dev_info(mmc_dev(host), "ret : %d, error_flag: %d\n", ret, error_flag);
	dev_info(mmc_dev(host), "info write boot partition done!!!\n");


	return 0;
}
	
static int mmc_write_boot_show(struct seq_file *s, void *data)
{
	struct mmc_host *host = s->private;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	sdpmmch->in_tuning = true;

	sdp_mmch_dbg_write_boot(host, host->card, s);

	sdpmmch->in_tuning = false;

	mmc_release_host(host);

	return 0;
}

static int mmc_write_boot_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_write_boot_show, inode->i_private);
}

static const struct file_operations sdp_mmch_dbg_write_boot_fops = {
	.open		= mmc_write_boot_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int sdp_mmch_dbg_read_pattern_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	static struct scatterlist sg;
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i, ret;
	static u8 __aligned(128) buf[512] = {0};


	u8 part_config; 
	u8 save_part;
	dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "printk read start 0x%llx\n", *val);

	mmc_claim_host(host);
	
	sdpmmch->in_tuning = true;

	part_config = card->ext_csd.part_config;
	save_part = part_config & EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= 2;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);
	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "(%s) boot part switch fail! to boot mode \n", __FUNCTION__);
		return -1;
	}
	dev_info(mmc_dev(host), "before read pattern, 0byte : %x, 1byte : %x\n", buf[0],buf[1]);


	sg_init_one(&sg, buf, ARRAY_SIZE(buf));

	sdpmmch->in_tuning = true;

	for(i=0; i<1; i++){

		sdp_mmch_host_reset(sdpmmch);

		cmd.opcode = MMC_READ_SINGLE_BLOCK;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;//MMC_CMD_AC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		sg_init_one(&sg, buf, ARRAY_SIZE(buf));

		mmcdata.blksz = ARRAY_SIZE(buf);//XXX
		mmcdata.blocks = 1;
		mmcdata.flags = MMC_DATA_READ;
		mmcdata.sg = &sg;
		mmcdata.sg_len = 1;

		mrq.cmd = &cmd;
		mrq.data = &mmcdata;

		mmc_wait_for_req(host, &mrq);

		mrq.cmd = &stop;
		mrq.data = NULL;
		mrq.stop = NULL;

	}

	sdpmmch->in_tuning = false;

	dev_info(mmc_dev(host), "after read pattern, 0byte : %x, 1byte : %x\n", buf[0],buf[1]);


	//restore!
	part_config = card->ext_csd.part_config;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= save_part;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, part_config,
					card->ext_csd.part_time);

	if (ret) {
		dev_err(&sdpmmch->pdev->dev, "%s boot part switch fail! to user \n", __FUNCTION__);
		dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "printk read pattern val_mem : 0x%llx\n", *val);
		dev_printk(KERN_DEBUG, &sdpmmch->pdev->dev, "printk read pattern save_part : 0x%x, part_config : 0x%x",save_part, part_config);
		return -1;
	}

	mmc_release_host(host);

	card->ext_csd.part_config = part_config;


	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mmch_dbg_read_pattern_fops, sdp_mmch_dbg_read_pattern_get, NULL,
	"0x%08llx\n");

int get_shift_bit(struct sdp_mmch_plat *platdata){

	u32 shift_bit = 0;
	u32 i = 0;
	while(!(platdata->strength_mask>>shift_bit & 0x1)){
		shift_bit++;
	}

	return shift_bit;

}
static int mmch_ew_dbg_strength_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	void *addr;
	u32 shift_bit = 0;

	mmc_claim_host(host);

#if defined(CONFIG_ARCH_SDP1404)
	addr = ioremap_nocache(0x11250C48, sizeof(u32));
	*val = (mmch_readl(addr)&0x30000000UL)>>28;
	iounmap(addr);
#elif defined(CONFIG_ARCH_SDP1501)
	addr = ioremap_nocache(0x005c10A0, sizeof(u32));
	*val = (mmch_readl(addr)&0x30000000UL)>>28;
	iounmap(addr);
#elif defined(CONFIG_ARCH_SDP1601)
	addr = ioremap_nocache(0x00F410A0, sizeof(u32));
	*val = (mmch_readl(addr)&0x70000000UL)>>28;
	iounmap(addr);
#else
	shift_bit=get_shift_bit(platdata);
	addr = ioremap_nocache(platdata->strength_reg, sizeof(u32));
	*val = (readl(addr) & platdata->strength_mask)>>shift_bit;
	dev_info(&sdpmmch->pdev->dev, "read strength %llu\n", *val);
	iounmap(addr);
#endif

	mmc_release_host(host);
	
	return 0;
}

static int mmch_ew_dbg_strength_set(void *data, u64 val)
{

	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);
	void *addr;	
	u32 temp, strength_value;
	u32 shift_bit = 0;
	
#if defined(CONFIG_ARCH_SDP1404) || defined(CONFIG_ARCH_SDP1501)
	if(val>=0 && val<=3){
		strength_value = (u32)val<<28;
	}
	else{
		dev_err(&sdpmmch->pdev->dev, "(%s) wrong input value\n", __FUNCTION__);
		return -EINVAL;
	}
#else
	if(val>=0 && val<=5){
#if	 defined(CONFIG_ARCH_SDP1601)
		strength_value = (u32)val<<28;
#else
		shift_bit=get_shift_bit(platdata);
		strength_value = (u32)val<<shift_bit;
#endif
	}
	else{
		dev_err(&sdpmmch->pdev->dev, "(%s) wrong input value\n", __FUNCTION__);
		return -EINVAL;
	}
#endif
	
#if defined(CONFIG_ARCH_SDP1404)
	addr = ioremap_nocache(0x11250C48, sizeof(u32));
	temp = mmch_readl(addr);
	temp &= ~0x30000000;
	mmch_writel(temp|strength_value, addr);
	dev_info(&sdpmmch->pdev->dev, "strength settting done: 0x%x\n", strength_value);
	iounmap(addr);
#elif defined(CONFIG_ARCH_SDP1501)
	addr = ioremap_nocache(0x5c10A0, sizeof(u32));
	temp = mmch_readl(addr);
	temp &= ~0x30000000;
	mmch_writel(temp|strength_value, addr);
	dev_info(&sdpmmch->pdev->dev, "strength settting done: 0x%x\n", strength_value);
	iounmap(addr);
#elif defined(CONFIG_ARCH_SDP1601)
	addr = ioremap_nocache(0xF410A0, sizeof(u32));
	temp = mmch_readl(addr);
	temp &= ~0x70000000;
	mmch_writel(temp|strength_value, addr);
	dev_info(&sdpmmch->pdev->dev, "strength settting done: 0x%x\n", strength_value);
	iounmap(addr);
#else
	addr = ioremap(platdata->strength_reg, sizeof(u32));
	mmch_writel(((readl(addr) & (~platdata->strength_mask)) | strength_value), addr);
	dev_info(&sdpmmch->pdev->dev, "write strength %d\n", strength_value);
	iounmap(addr);
#endif


	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmch_ew_dbg_strength_fops, mmch_ew_dbg_strength_get, mmch_ew_dbg_strength_set,
	"%llu\n");

static int mmch_ew_dbg_clk_phase_get(void *data, u64 *val)
{
	//for changing drv clk delay
	struct mmc_host *host = data;
//	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	void *addr;

	mmc_claim_host(host);

	addr = ioremap_nocache(0x001000A8, sizeof(u32));
	*val = (mmch_readl(addr)&0x00070000UL)>>16;
	iounmap(addr);

	mmc_release_host(host);
	
	return 0;
}

static int mmch_ew_dbg_clk_phase_set(void *data, u64 val)
{

	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	void *addr;	
	u32 temp, phase_value;
	
	if(val>=0 && val<=7){
		phase_value = (u32)val<<16;
	}	
	else{
		dev_err(&sdpmmch->pdev->dev, "(%s) wrong input value\n", __FUNCTION__);
		return -EINVAL;
	}

	addr = ioremap_nocache(0x001000A8, sizeof(u32));
	temp = mmch_readl(addr);
	temp &= ~0x00070000;
	mmch_writel(temp|phase_value, addr);
	dev_info(&sdpmmch->pdev->dev, "clk phase settting done\n");
	iounmap(addr);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmch_ew_dbg_clk_phase_fops, mmch_ew_dbg_clk_phase_get, mmch_ew_dbg_clk_phase_set,
	"%llu\n");


static int mmc_drv_show(struct seq_file *s, void *data)
{
	struct mmc_host *host = s->private;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	sdpmmch->in_tuning = true;

	sdp_mmch_drv_tuning(host, host->card, s);

	sdpmmch->in_tuning = false;

	mmc_release_host(host);

	return 0;
}

static int mmc_drv_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_drv_show, inode->i_private);
}

static const struct file_operations mmc_drv_fops = {
	.open		= mmc_drv_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define RDQS_MAX_VAL 255
#define RDQS_MAX_COUNT 256
static int
sdp_mmch_do_rdqs_tuning(struct mmc_host *host, struct mmc_card *card, int start, int end, int inc, struct seq_file *s)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	int rdqs_tune_count=0;
	int cur_rdqs;
	int ret;
	static DECLARE_BITMAP(bitmap_rdqs, RDQS_MAX_COUNT);
	static char str_rdqs[256];
	u8 *extcsd = NULL;
	int rdqs_save = 0;
	int median = 0, window_size = 0;

	if(!sdpmmch->is_hs400) {
		dev_err(&sdpmmch->pdev->dev, "RDQS tuning is not supported.\n");
		return -ENODEV;
	}

	if(end > RDQS_MAX_VAL) {
		end = RDQS_MAX_VAL;
	}

	rdqs_save = (int)((mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL) & 0x3FF)>>2);

	dev_info(&sdpmmch->pdev->dev, "RDQS Tuning start! range %d to %d, inc %d, current rdqs %d\n",
			 start, end, inc, rdqs_save);

	bitmap_zero(bitmap_rdqs, RDQS_MAX_COUNT);

	sdpmmch->in_tuning = true;

	for(cur_rdqs = start; cur_rdqs <= end; cur_rdqs += inc) {

		/* set cur rdqs value */
		ret = sdp_mmch_rdqs_set(host, cur_rdqs);

		/* check data error */
		ret = mmc_get_ext_csd(card, &extcsd);
		if(!ret) {
			bitmap_set(bitmap_rdqs, cur_rdqs, inc);
		}
		rdqs_tune_count++;
	}

	sdpmmch->in_tuning = false;
	kfree(extcsd);

	median = sdp_mmch_find_median_in_bitmap(sdpmmch, bitmap_rdqs, RDQS_MAX_COUNT, &window_size);
	if(median < 0) {
		sdp_mmch_rdqs_set(host, rdqs_save);

		dev_err(&sdpmmch->pdev->dev, "RDQS Tuning fail! error %d\n", median);
		ret = median;
	} else {
		sdp_mmch_rdqs_set(host, median);

		bitmap_scnlistprintf(str_rdqs, ARRAY_SIZE(str_rdqs), bitmap_rdqs, RDQS_MAX_COUNT);
		dev_info(&sdpmmch->pdev->dev, "RDQS Tuning done! pass range %s, new rdqs %d, window size %d\n",
			str_rdqs, median, window_size);
		if(s!=NULL){
			seq_printf(s, "%d / %s / %d\n", rdqs_tune_count,str_rdqs, median);
		}
		ret = median;
	}

	return ret;
}

static int mmc_rdqs_show(struct seq_file *s, void *data)
{
	struct mmc_host *host = s->private;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	sdpmmch->in_tuning = true;

	sdp_mmch_do_rdqs_tuning(host, host->card, 2, 255, 1, s);

	sdpmmch->in_tuning = false;

	mmc_release_host(host);
	return 0;
}

static int mmc_rdqs_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_rdqs_show, inode->i_private);
}

static const struct file_operations mmc_rdqs_fops = {
	.open		= mmc_rdqs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int
sdp_mmch_execute_hs400_rdqs_tuning(struct mmc_host *host, struct mmc_card *card)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	int ret = 0;

#ifdef CONFIG_SDP_MMC_DRV_AND_RDQS_TUNE
	dev_info(&sdpmmch->pdev->dev, "defined CONFIG_SDP_MMC_DRV_AND_RDQS_TUNE\n");
	//sdp_mmch_drv_tuning(host, card, NULL);

	sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_ENABLING;
#endif

	if(sdpmmch->rdqs_tune == MMCH_RDQS_TUNE_DISABLED) {
		dev_info(&sdpmmch->pdev->dev, "first boot. skip hs400 rdqs tuning.\n");
		sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_ENABLING;
		return 0;
	}

	ret = sdp_mmch_do_rdqs_tuning(host, card, 2, 102, 10, NULL);
	

	if(ret < 0) {
		sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_ERROR;
	} else {
		sdpmmch->successed_rdqs=sdp_mmch_rdqs_shift(ret);
		sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_DONE;
	}

	return ret;
}
EXPORT_SYMBOL(sdp_mmch_execute_hs400_rdqs_tuning);

static void __maybe_unused
sdp_mmch_pre_req(struct mmc_host *host, struct mmc_request *req, bool is_first_req)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	struct mmc_data *data = req->data;

	MMCH_FLOW("req: 0x%08x\n", (u32)req);

	BUG_ON(!data);

	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
		data->host_cookie++;
		sdpmmch->is_pre_post_req = true;
		mmch_dma_prepare_data(sdpmmch, data);
	}
}


static void __maybe_unused
sdp_mmch_post_req(struct mmc_host *host, struct mmc_request *req, int err)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	struct mmc_data *data = req->data;

	MMCH_FLOW("req: 0x%08x\n", (u32)req);

	BUG_ON(!data);

	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
		mmch_dma_cleanup_data(sdpmmch, data);
		data->host_cookie = 0;
	}
}

static int sdp_mmch_get_cd(struct mmc_host *host)
{
	struct platform_device *pdev = to_platform_device(host->parent);
	struct sdp_mmch_plat *platdata = dev_get_platdata(&pdev->dev);

#ifdef CONFIG_OF
	if (!platdata || (platdata->gpio_cd < 0))
		return -ENOSYS;

	return !gpio_direction_input((u32) platdata->gpio_cd);
#else
	if (!platdata || !platdata->get_cd)
		return -ENOSYS;

	return platdata->get_cd(0/*now support just one slot*/);
#endif
}

static int sdp_mmch_get_ro(struct mmc_host *host)
{
	struct platform_device *pdev = to_platform_device(host->parent);
	struct sdp_mmch_plat *platdata = dev_get_platdata(&pdev->dev);

#ifdef CONFIG_OF
	if (!platdata || (platdata->gpio_ro < 0))
		return -ENOSYS;

	return gpio_direction_input((u32) platdata->gpio_ro);
#else
	if (!platdata || !platdata->get_ro)
		return -ENOSYS;

	return platdata->get_ro(0/*now support just one slot*/);
#endif
}

static void
sdp_mmch_request(struct mmc_host *host, struct mmc_request *mrq)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	u32 regval = 0;
	unsigned long flags;

#ifdef MMCH_PRINT_REQUEST_TIME
	struct timeval tv_start, tv_end;
	unsigned long us = 0;
	do_gettimeofday(&tv_start);
#endif

	/* for debug */
	if(request_warn_msg){
		WARN(in_interrupt(), "%s: called in interrupt!!", __FUNCTION__);
	}

	/* use irqsave for call in interrupt. */
	spin_lock_irqsave(&sdpmmch->lock, flags);

	MMCH_FLOW("mrq: 0x%08x\n", (u32)mrq);

	/* if state is not idle, req done with error. */
	if(sdpmmch->state != MMCH_STATE_IDLE) {
		dev_info(mmc_dev(host), "--------- MMCH STATE IS NOT IDLE %s on %s ---------\n",
		       mmch_state_string(sdpmmch->state), __func__);
		mmch_register_dump_notitle(sdpmmch);
		dump_stack();
		mrq->cmd->error = EBUSY;
		mmc_request_done(sdpmmch->host, mrq);
		goto unlock;
	}

	/* check Card present.. */
	if(!sdp_mmch_get_cd(host)) {
		mrq->cmd->error = (u32) -ENOMEDIUM;
		mmc_request_done(sdpmmch->host, mrq);
		goto unlock;
	}

	/*Clear INTRs*/
	mmch_writel(MMCH_RINTSTS_ALL_ENABLED, sdpmmch->base+MMCH_RINTSTS);
	regval = MMCH_INTMSK_ALL_ENABLED;

	regval &=  ~(MMCH_INTMSK_RXDR | MMCH_INTMSK_TXDR);		 	// Using DMA, Not need RXDR, TXDR


#ifndef CONFIG_SDP_MMC_USE_AUTO_STOP_CMD/* disable auto stop cmd done 120619 drain.lee */
	regval &= ~MMCH_INTMSK_ACD;
#endif

	mmch_writel(regval, sdpmmch->base+MMCH_INTMSK);

	mmch_writel(0,sdpmmch->base+MMCH_BLKSIZ);
	mmch_writel(0,sdpmmch->base+MMCH_BYTCNT);

	sdpmmch->mrq = mrq;		// 111204 	move
	sdpmmch->request_count++;

	sdpmmch->intr_status = 0;
	sdpmmch->dma_status = 0;
	sdpmmch->event = 0;
	sdpmmch->intr_accumulated = 0;
	sdpmmch->idmac_accumulated = 0;
	sdpmmch->event_accumulated = 0;

	if(mrq->data) {		// Read or Write request

		/* HS400 DQS glich workaround */
		if(host->card && mmc_card_hs400(host->card)) {
			mmch_writel(0x1, sdpmmch->base+MMCH_DDR200_ASYNC_FIFO_CTRL);
			udelay(1);
			mmch_writel(0x0, sdpmmch->base+MMCH_DDR200_ASYNC_FIFO_CTRL);
		}

		if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
			if(!mrq->data->host_cookie) {
				mrq->data->host_cookie++;
				sdpmmch->is_pre_post_req = false;
			}
			mmch_dma_set_dma(sdpmmch);
			mmch_dma_start(sdpmmch);
		} else {
			mmch_pio_set_pio(sdpmmch);
		}
	}

	tasklet_enable(&sdpmmch->tasklet);

	//spin_lock(&sdpmmch->state_lock);

	if(mrq->sbc) {
		sdpmmch->state = MMCH_STATE_SENDING_SBC;
		mmch_start_mrq(sdpmmch, mrq->sbc);
	} else {
		sdpmmch->state = MMCH_STATE_SENDING_CMD;
		mmch_start_mrq(sdpmmch, mrq->cmd);	//111204
	}

	//spin_unlock(&sdpmmch->state_lock);

	do_gettimeofday(&sdpmmch->isr_time_now);


	if(sdpmmch->in_tuning) {
		/* support max 512byte, 50Kbyte/s */
		sdpmmch->polling_ms = 10;
		sdpmmch->timeout_ms = 10;
	} else {
		/* no data CMD */
		sdpmmch->timeout_ms = 40;
		sdpmmch->polling_ms = 20;

		if(mrq->data) {
			unsigned int bus_width = 1;
			unsigned int is_write = !!(mrq->data->flags & MMC_DATA_WRITE);
			//unsigned int speed = is_write ? 5:10;/* Mbyte/s 800ms/400ms*/

			if(sdpmmch->host->ios.bus_width == MMC_BUS_WIDTH_4) bus_width = 4;
			else if(sdpmmch->host->ios.bus_width == MMC_BUS_WIDTH_8) bus_width = 8;

			//sdpmmch->timeout_ms = ((MMCH_DESC_NUM * 0x1000 / 1024 * 1000) / (speed * 1024)) * (8/bus_width);
			sdpmmch->timeout_ms = 2000;

			if(mrq->data->timeout_ns) {
				if((mrq->data->timeout_ns/1000000) > sdpmmch->timeout_ms) {
					sdpmmch->timeout_ms = mrq->data->timeout_ns/1000000;
				}

			}

			sdpmmch->polling_ms = sdpmmch->timeout_ms / 4;

			/* polling interval is max default ms */
			if(sdpmmch->polling_ms > (sdpmmch->max_polling_ms*(is_write?2:1)) ) {
				sdpmmch->polling_ms = sdpmmch->max_polling_ms;
			}

#ifdef CONFIG_ARCH_SDP1412
			/* hawk-a emmc 4.41 is very lowspeed read 30MB/s, write 6MB/s */
			{
				unsigned int hawka_min_timeout_ms = is_write?2401:601;
				unsigned int hawka_min_polling_ms = is_write?801:201;

				if(sdpmmch->timeout_ms < hawka_min_timeout_ms) {
					sdpmmch->timeout_ms = hawka_min_timeout_ms;
				}
				if(sdpmmch->polling_ms < hawka_min_polling_ms) {
					sdpmmch->polling_ms = hawka_min_polling_ms;
				}
			}
#endif
		}
	}

	sdpmmch->polling_timer_start = jiffies;
#ifdef MMCH_USE_SW_TIMEOUT
	mod_timer(&sdpmmch->timeout_timer, sdpmmch->polling_timer_start + msecs_to_jiffies(sdpmmch->polling_ms));
#else
	if(sdpmmch->in_tuning) {
		mod_timer(&sdpmmch->timeout_timer, sdpmmch->polling_timer_start + msecs_to_jiffies(sdpmmch->polling_ms));
	}
#endif

#ifdef MMCH_DEBUG
	dev_err(&sdpmmch->pdev->dev, "sdp-mmc request#%06llu(CMD%-2d)!!!!!!!!!!!!!\n", sdpmmch->request_count, mrq->cmd->opcode);
	mmch_register_dump(sdpmmch);
#endif

unlock:

#ifdef MMCH_PRINT_REQUEST_TIME
	do_gettimeofday(&tv_end);

	us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
		     (tv_end.tv_usec - tv_start.tv_usec);

	dev_printk(MMCH_PRINT_LEVEL, &sdpmmch->pdev->dev,
		"REQ#%06llu process time %3luus (CMD%-2d, SG LEN %2d, Type %s Stop %s, timeout %3lums %3lums)\n",
		sdpmmch->request_count, us, mrq->cmd->opcode, mrq->data?mrq->data->sg_len:0,
		!mrq->data?"NoData":(mrq->data->flags&MMC_DATA_READ?"READ  ":"WRITE "), mrq->stop?"Y":"N", sdpmmch->timeout_ms, sdpmmch->polling_ms);
#endif

	spin_unlock_irqrestore(&sdpmmch->lock, flags);
}
static void sdp_mmch_set_ios(struct mmc_host *host, struct mmc_ios *ios)
{
	SDP_MMCH_T * sdpmmch = mmc_priv(host);
	u32 regval;
	u32 rddqs_en, clksel;
	int	retval = 0;
	struct sdp_mmch_plat *platdata = dev_get_platdata(&sdpmmch->pdev->dev);

	MMCH_FLOW("called\n");
	if(mmch_cmd_ciu_status(sdpmmch) < 0) {
		dev_err(mmc_dev(host), "[%s] Error wait to ready cmd register\n", __FUNCTION__);
		return;
	}

	regval = mmch_readl(sdpmmch->base+MMCH_CTRL);
	switch(ios->bus_mode){
	case MMC_BUSMODE_OPENDRAIN :
		MMCH_SET_BITS(regval, MMCH_CTRL_ENABLE_OD_PULLUP);
		break;

	case MMC_BUSMODE_PUSHPULL :
		MMCH_UNSET_BITS(regval, MMCH_CTRL_ENABLE_OD_PULLUP);
		break;

	default :
		break;
	}
	mmch_writel(regval, sdpmmch->base+MMCH_CTRL);



	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
		void *addr;
		u32 div = 0;
		addr = ioremap_nocache(0x10090908, sizeof(u32));
		if(!addr) {
			dev_err(mmc_dev(host), "sdp_mmch_set_ios: ioremap return NULL!\n");
			BUG();
			return;
		}

		if(ios->timing == MMC_TIMING_MMC_HS200) {
			div = 7;
			mmch_writel((readl(addr) &~0x00F00000UL) | ((div-1)<<20), addr);
			platdata->processor_clk = 1000000000 / div;
			//dev_err(mmc_dev(mmc), "sdp_mmch_set_ios: XXX: HS Mode clock %d!\n", platdata->processor_clk);
		} else {
			div = 5;
			mmch_writel((readl(addr) &~0x00F00000UL) | ((div-1)<<20), addr);
			platdata->processor_clk = 1000000000 / div;
			//dev_err(mmc_dev(mmc), "sdp_mmch_set_ios: XXX: non-HS Mode clock %d!\n", platdata->processor_clk);
		}
		wmb(); udelay(10);
		iounmap(addr);
	}



	switch(ios->bus_width){
		case MMC_BUS_WIDTH_1 :
			mmch_writel(MMCH_CTYPE_1BIT_MODE, sdpmmch->base + MMCH_CTYPE);
			break;

		case MMC_BUS_WIDTH_4 :
			mmch_writel(MMCH_CTYPE_4BIT_MODE, sdpmmch->base + MMCH_CTYPE);
			break;

		case MMC_BUS_WIDTH_8 :
			mmch_writel(MMCH_CTYPE_8BIT_MODE, sdpmmch->base + MMCH_CTYPE);
			break;

		default :
			break;
	}


	regval = mmch_readl(sdpmmch->base + MMCH_UHS_REG);
	rddqs_en= mmch_readl(sdpmmch->base + MMCH_DDR200_RDDQS_EN);
	// XXX
	clksel = mmch_readl(sdpmmch->base + MMCH_CLKSEL);
#ifndef CONFIG_ARCH_SDP1412
	clksel = (clksel&~0x07000000UL)|0x03000000UL;
#endif

	MMCH_UNSET_BITS(regval, 0x1U << 16);
	MMCH_UNSET_BITS(rddqs_en, 0x1U << 0);/* RDDQS_EN */
	MMCH_UNSET_BITS(rddqs_en, 0x1U << 5);/* RESP_RCLK_MODE */

	/* DDR mode set */
	switch(ios->timing){

	case MMC_TIMING_LEGACY:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
		/* all unset */
		break;

	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		MMCH_SET_BITS(regval, 0x1U << 16);
		/* 2.60a eMMC half startbit */
		if( (mmch_read_ip_version(sdpmmch)&0xFFFF) >= 0x240a) {
			mmch_writel(0x1, sdpmmch->base+MMCH_EMMC_DDR);
		}
		break;

	case MMC_TIMING_MMC_HS200:
		// XXX
		clksel = (clksel&~0x07000000U)|0x03000000U;
		break;

	case MMC_TIMING_MMC_HS400:/* DDR200 */
		MMCH_SET_BITS(regval, 0x1U << 16);
		MMCH_SET_BITS(rddqs_en, 0x1U << 0);/* RDDQS_EN */

		/* 2.60a eMMC half startbit */
		if( (mmch_read_ip_version(sdpmmch)&0xFFFF) >= 0x240a) {
			mmch_writel(0x1, sdpmmch->base+MMCH_EMMC_DDR);
		}
		if(ios->enhanced_strobe){
			MMCH_SET_BITS(rddqs_en, 0x1U << 5);/* RESP_RCLK_MODE */
		}
		// XXX
		sdpmmch->is_hs400 = true;
		clksel = (clksel&~0x07000000U)|0x01000000U;
		break;

	default:
		dev_err(mmc_dev(host), "not supported timing(%d)\n", ios->timing);
	}

	switch(ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		MMCH_UNSET_BITS(regval, 0x1U << 0);
		break;

	case MMC_SIGNAL_VOLTAGE_180:
		MMCH_SET_BITS(regval, 0x1U << 0);
		break;

	default:
		dev_err(mmc_dev(host), "not supported voltage(%d)\n", ios->signal_voltage);
	}
	mmch_writel(regval, sdpmmch->base + MMCH_UHS_REG);
	mmch_writel(rddqs_en, sdpmmch->base + MMCH_DDR200_RDDQS_EN);

#ifdef CONFIG_ARCH_SDP1404
	mmch_writel(clksel^0x00008000, sdpmmch->base + MMCH_CLKSEL);
#else
	mmch_writel(clksel, sdpmmch->base + MMCH_CLKSEL);
#endif
	wmb();

	if(ios->clock){
		MMCH_FLOW("ios->clock = %d\n", ios->clock);

		retval = mmch_set_op_clock(sdpmmch, ios->clock);  // ????
/* if clock setting is failed, retry to mmc reinit  becase it never appear the HLE error */
		if(retval < 0){
			dev_err(mmc_dev(host), "mmch retry to set the clock after mmc host controller initialization\n");

			mmch_platform_init(sdpmmch);
			mmch_initialization(sdpmmch);
			//	Descriptor set
			mmch_writel((u32) sdpmmch->dmadesc_pbase, sdpmmch->base+MMCH_DBADDR);
#ifdef MMCH_DBADDRU
			mmch_writel((u32) sdpmmch->dmadesc_pbase>>32, sdpmmch->base+MMCH_DBADDRU);
#endif
			 // mmc ios
			sdp_mmch_set_ios(sdpmmch->host, ios); //recurrent function
		}
	} else {
		/* clock disable */
		mmch_clock_disable(sdpmmch);
		sdpmmch->actual_clock = 0;
	}

	regval = mmch_readl(sdpmmch->base + MMCH_PWREN);
	switch( ios->power_mode){
	case MMC_POWER_OFF :
		MMCH_UNSET_BITS(regval, 0x1U << 0);
		break;

	case MMC_POWER_UP :
	case MMC_POWER_ON :
		MMCH_SET_BITS(regval, 0x1U << 0);
		break;
	default:
		break;
	}
	mmch_writel(regval, sdpmmch->base + MMCH_PWREN);
}

static void sdp_mmch_hw_reset(struct mmc_host *host)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	u32 rst = mmch_readl(sdpmmch->base + MMCH_RST_n);

	dev_info(mmc_dev(host), "MMC Host and Card reset!!\n");
	mmch_writel(rst&~0x1UL, sdpmmch->base + MMCH_RST_n);
	udelay(10);
	mmch_writel(rst, sdpmmch->base + MMCH_RST_n);

	udelay(500);
	mmch_platform_init(sdpmmch);
	sdp_mmch_host_reset(sdpmmch);

	sdpmmch->request_count = 0;
}
void sdp_mmch_debug_dump(struct mmc_host *host)
{
	SDP_MMCH_T * sdpmmch = mmc_priv(host);
	unsigned long flags;

	spin_lock_irqsave(&sdpmmch->lock, flags);

	dev_err(&sdpmmch->pdev->dev, "\n== %s ==\n", __FUNCTION__);
	mmch_register_dump(sdpmmch);
	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {
		mmch_dma_intr_dump(sdpmmch);
	}

	spin_unlock_irqrestore(&sdpmmch->lock, flags);
}
EXPORT_SYMBOL(sdp_mmch_debug_dump);


#ifdef CONFIG_SDP_MMC_RDQS_FIXUP
int sdp_mmch_rdqs_fixup(struct mmc_host *host, struct mmc_card *card)
{
	SDP_MMCH_T * sdpmmch = mmc_priv(host);
	BUG_ON(card == NULL);

	if(sdpmmch->rdqs_tune != MMCH_RDQS_TUNE_DONE) {
#ifdef CONFIG_ARCH_SDP1404
		if(sdp_get_revision_id() > 1) {
			u32 temp = 0;

			dev_info(mmc_dev(host), "sdp_mmch_rdqs_fixup: manfid 0x%06x, prod_name %s\n", card->cid.manfid, card->cid.prod_name);
			temp = mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
			temp &= ~0x3FFU;
			if(card->cid.manfid == 0x000013) {
				mmch_writel(temp|(26<<2), sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
			}
			else if(card->cid.manfid == 0x000015)
			{
				mmch_writel(temp|(36<<2), sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
			}
		}
#endif
	}

	return 0;
}
EXPORT_SYMBOL(sdp_mmch_rdqs_fixup);
#endif

static int sdp_mmch_prepare_hs400_tuning(struct mmc_host *host, struct mmc_ios *ios)
{
	SDP_MMCH_T * sdpmmch = mmc_priv(host);
	u32 clksel = 0;
	dev_info(mmc_dev(host), "sdp_mmch_prepare_hs400_tuning\n");

	sdpmmch->is_hs400 = true;

	clksel = mmch_readl(sdpmmch->base + MMCH_CLKSEL);
	clksel &= ~0x07000000U;
	clksel |= 0x01000000U;
#ifdef CONFIG_ARCH_SDP1404
	clksel ^= 0x00008000U;
#endif
	mmch_writel(clksel, sdpmmch->base + MMCH_CLKSEL);

	/* XXX test */
	if(ios->clock){
		mmch_set_op_clock(sdpmmch, ios->clock);
	} else {
		/* clock disable */
		mmch_clock_disable(sdpmmch);
		sdpmmch->actual_clock = 0;
	}
	return 0;
}

static int sdp_mmch_enable(struct mmc_host *host)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	if (sdpmmch->state != MMCH_STATE_IDLE) {
		dev_info(mmc_dev(host), "--------- MMCH STATE IS NOT IDLE %s on %s ---------\n",
		       mmch_state_string(sdpmmch->state), __func__);
		mmch_register_dump_notitle(sdpmmch);
		dump_stack();
		/* BUG(); */
	}

	return 0;
}

static int sdp_mmch_disable(struct mmc_host *host)
{
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	if (sdpmmch->state != MMCH_STATE_IDLE) {
		dev_info(mmc_dev(host), "--------- MMCH STATE IS NOT IDLE %s on %s ---------\n",
		       mmch_state_string(sdpmmch->state), __func__);
		mmch_register_dump_notitle(sdpmmch);
		dump_stack();
		/* BUG(); */
	}

	return 0;
}

static void sdp_mmch_hs400_enhanced_strobe(struct mmc_host *host, struct mmc_ios *ios)
{
	u32 rddqs_en;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	rddqs_en = mmch_readl(sdpmmch->base + MMCH_DDR200_RDDQS_EN);
	dev_info(mmc_dev(host), "sdpmmch->enhanced_strobe : %d\n", ios->enhanced_strobe);

	if (ios->enhanced_strobe){
		sdp_mmch_set_hs400_default(sdpmmch);
	}
}

static struct mmc_host_ops sdp_mmch_ops = {
	.request	= sdp_mmch_request,
	.set_ios	= sdp_mmch_set_ios,
	.get_ro		= sdp_mmch_get_ro,
	.get_cd		= sdp_mmch_get_cd,
#ifdef MMCH_USE_PRE_POST_REQ
	.pre_req	= sdp_mmch_pre_req,
	.post_req	= sdp_mmch_post_req,
#endif
	.execute_tuning	= sdp_mmch_execute_tuning,
	.prepare_hs400_tuning = sdp_mmch_prepare_hs400_tuning,
	.hs400_enhanced_strobe = sdp_mmch_hs400_enhanced_strobe,

	.hw_reset = sdp_mmch_hw_reset,

	.enable         = sdp_mmch_enable,
	.disable        = sdp_mmch_disable,
};

#ifdef CONFIG_OF

#ifdef CONFIG_64BIT
static u64 sdp_mmc_dmamask = DMA_BIT_MASK(64);
#else
static u64 sdp_mmc_dmamask = DMA_BIT_MASK(32);
#endif

static int sdp_mmc_of_parse_dt(struct device *dev, struct sdp_mmch_plat *plat)
{
	int val, buswidth = 0;
	const char *clkname;
	struct clk *clk = ERR_PTR(-EINVAL);

	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	if(of_property_read_u32(dev->of_node, "fifo-depth", &val))
	{
		dev_info(dev, "fifo-depth property not found, using default value\n");
		val = 128;
	}
	plat->fifo_depth = (u8) val;
	
	if(of_property_read_u32_array(dev->of_node, "min-max-clock", &plat->min_clk, 2))
	{
		dev_info(dev, "min-max-clock property not found, using default value\n");
		plat->min_clk = 400000;
		plat->max_clk = 50000000;
	}

#if defined(CONFIG_ARCH_SDP1803)
	if(of_property_read_u32_array(dev->of_node, "mmc_strength", &plat->strength_reg, 3))
	{
		dev_info(dev, "strength property not found, please check dtsi file\n");
	}
#endif

#ifdef CONFIG_ARCH_PHYS_ADDR_T_64BIT
	dev->coherent_dma_mask = DMA_BIT_MASK(64);
#else
	dev->coherent_dma_mask = DMA_BIT_MASK(val);
#endif
	dev->dma_mask = &sdp_mmc_dmamask;

	if(of_property_read_string(dev->of_node, "clock-names", &clkname))
	{
		dev_info(dev, "clock-names property not found, using default value\n");
		clkname = "emmc_clk";
	}

	clk = clk_get(dev, clkname);
	if (IS_ERR(clk))
	{
		dev_err(dev, "cannot get clk, check clock-names!!\n");
		return -ENODEV;
	}
	plat->processor_clk = clk_get_rate(clk);

	if(!of_property_read_u32(dev->of_node, "irq-affinity", &plat->irq_affinity)) {
	}

	if(!of_property_read_u32_array(dev->of_node, "irq-affinity-mask", (u32 *)cpumask_bits(&plat->irq_affinity_mask), BITS_TO_LONGS(NR_CPUS)) ) {
	}

	if(of_get_property(dev->of_node, "force-pio-mode", NULL)) {
		plat->force_pio_mode = true;
	}

	plat->gpio_cd = of_get_named_gpio(dev->of_node, "cd-gpios", 0);
	plat->gpio_ro = of_get_named_gpio(dev->of_node, "ro-gpios", 0);

	if(plat->gpio_cd >= 0)
		gpio_request_one((u32) plat->gpio_cd, GPIOF_DIR_IN, "sdif-cd");

	if(plat->gpio_ro >= 0)
		gpio_request_one((u32) plat->gpio_ro, GPIOF_DIR_IN, "sdif-ro");

	plat->caps = 0;
	plat->caps2 = 0;

	if(!of_property_read_u32(dev->of_node, "bus-width", &buswidth)) {
		switch(buswidth) {
		case 8:
			plat->caps |= MMC_CAP_8_BIT_DATA;
			/* Fall Through */
		case 4:
			plat->caps |= MMC_CAP_4_BIT_DATA;
		default:
			break;
		}
	}

	if(of_get_property(dev->of_node, "hw-reset", NULL)) {
		plat->caps |= (u32)MMC_CAP_HW_RESET;
	}

	if(of_get_property(dev->of_node, "non-removable", NULL)) {
		plat->caps |= MMC_CAP_NONREMOVABLE;
	}
		
	if(of_get_property(dev->of_node, "erase", NULL)){
		plat->caps |= MMC_CAP_ERASE;
	}
	
	if(of_get_property(dev->of_node, "wait-while-busy", NULL)){
		plat->caps |= MMC_CAP_WAIT_WHILE_BUSY;
	}

	if(of_get_property(dev->of_node, "bus-width-test", NULL)) {
		plat->caps |= MMC_CAP_BUS_WIDTH_TEST;
	}

	if(of_get_property(dev->of_node, "needs-poll", NULL)) {
		plat->caps |= MMC_CAP_NEEDS_POLL;
	}

	if(of_get_property(dev->of_node, "highspeed", NULL)) {
		plat->caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
	}

	if(of_get_property(dev->of_node, "sdr104", NULL)) {
		plat->caps |= MMC_CAP_UHS_SDR104;
	}

	if(of_get_property(dev->of_node, "ddr50", NULL)) {
		plat->caps |= MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR;
	}

	if(of_get_property(dev->of_node, "hs200", NULL)) {
		plat->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
	}

	if(of_get_property(dev->of_node, "hs200-tuning", NULL)) {
		plat->is_hs200_tuning = true;
	}

	if(of_get_property(dev->of_node, "hs400-tuning", NULL)) {
		plat->is_hs400_cmd_tuning = true;
	}

	if(of_get_property(dev->of_node, "hs400", NULL)) {
		plat->caps2 |= MMC_CAP2_HS400_1_8V | MMC_CAP2_HS200_1_8V_SDR;
	}


	if(of_get_property(dev->of_node, "full-pwr-cycle", NULL))
		plat->caps2 |= MMC_CAP2_FULL_PWR_CYCLE;

	if(of_get_property(dev->of_node, "packed-cmd", NULL))
		plat->caps2 |= MMC_CAP2_PACKED_CMD;

	if(of_get_property(dev->of_node, "fixup-golfs-input-clock", NULL)) {
		plat->fixups |= SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK;
	}

	if(of_get_property(dev->of_node, "fixup-golfp-ud-tuning-fixed", NULL)) {
		plat->fixups |= SDP_MMCH_FIXUP_GOLFP_UD_TUNING_FIXED;
	}

	/* tuning table parse */
	{
		const u32 *tuning_sam = NULL;
		u32 size = 0;
		u32 onesize = 3;
		int i, j, item_num;
		const char *prop_name[] = {"tuning_sam", "tuning_sam_hs400",
					"tuning_sam_hs200_default", "tuning_drv_hs200_default",
					"tuning_sam_hs400_default", "tuning_drv_hs400_default"};
		struct sdp_mmch_reg_list *table_addr[ARRAY_SIZE(prop_name)] = {NULL};

		table_addr[0] = &plat->tuning_table;
		table_addr[1] = &plat->tuning_table_hs400;
		table_addr[2] = &plat->tuning_hs200_sam_default;
		table_addr[3] = &plat->tuning_hs200_drv_default;
		table_addr[4] = &plat->tuning_hs400_sam_default;
		table_addr[5] = &plat->tuning_hs400_drv_default;

		item_num = ARRAY_SIZE(prop_name);

		if(of_property_read_u32(dev->of_node, "tuning_set_size", &plat->tuning_set_size)) {
			plat->tuning_set_size = 1;
		}

		/* Get propertys */
		for(j = 0; j < item_num; j++) {
			tuning_sam = of_get_property(dev->of_node, prop_name[j], &size);
			if(tuning_sam) {
				if(size % (sizeof(u32)*(u32)plat->tuning_set_size) ) {
					table_addr[j]->list_num = 0;
					dev_err(dev, "error! tuning table(%s) size %d!\n", prop_name[j], size);
				} else {
					size /= sizeof(u32);
				
					table_addr[j]->list_num = (int) size / 3;
					table_addr[j]->list = kmalloc(size * sizeof(struct sdp_mmch_reg_set), GFP_KERNEL);

					for(i = 0; size >= onesize; size -= onesize, tuning_sam += onesize, i++) {
						table_addr[j]->list[i].addr = be32_to_cpu(tuning_sam[0]);
						table_addr[j]->list[i].mask = be32_to_cpu(tuning_sam[1]);
						table_addr[j]->list[i].value = be32_to_cpu(tuning_sam[2]);
					}
				}
			}
		}

	#if 0/* debug, print table */
		for(j = 0; j < item_num; j++) {
			for(i = 0; i < table_addr[j]->list_num; i++) {
				dev_printk(KERN_INFO, dev,
					"%s%2d addr 0x%08llx, mask 0x%08llx, val 0x%08x\n",
					prop_name[j], i, (u64)table_addr[j]->list[i].addr, (u64)table_addr[j]->list[i].mask, table_addr[j]->list[i].value);
			}
		}
	#endif
	}
	return 0;

}

#endif


#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static int mmch_dbg_rdqs_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmc_claim_host(host);

	*val = (mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL)&0x3FF)>>2;

	mmc_release_host(host);
	return 0;
}

static int mmch_dbg_rdqs_set(void *data, u64 val)
{
	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	u32 temp, rdqs_raw, rdqs_min, rdqs_max;

	static u32 rdqs_orignal = 0;
	int bypass = 0;

	mmc_claim_host(host);

	/* for debug */
	if(val >= 1000) {
		bypass = 1;
		val -= 1000;
	}

	temp = mmch_readl(sdpmmch->base + MMCH_DDR200_DLINE_CTRL);
	if(rdqs_orignal == 0) {
		rdqs_orignal = (temp&0x3FFU)>>2;
		if(rdqs_orignal == 0) {
			dev_err(&sdpmmch->pdev->dev, "rdqs set: rdqs_orignal is 0!!! DDR200_DLINE_CTRL: 0x%08x", temp);
			mmc_release_host(host);
			return -EINVAL;
		}
	}

	rdqs_min = (rdqs_orignal <= 10) ? 0:(rdqs_orignal - 10);
	rdqs_max = (rdqs_orignal >= (0xFFFFFFFFU-10U)) ? 0xFFFFFFFF:(rdqs_orignal + 10);

	if(!bypass && (val < rdqs_min || val > rdqs_max) ) {
		dev_err(&sdpmmch->pdev->dev, "rdqs set: rdqs value(%llu) out of range!!(%u~%u) bypass:%d\n", val, rdqs_min, rdqs_max, bypass);
		mmc_release_host(host);
		return -EINVAL;
	}

	rdqs_raw = sdp_mmch_rdqs_shift(val);

	if((rdqs_raw < 8) || (rdqs_raw > 0x3FF))
	{
		mmc_release_host(host);
		return -EINVAL;
	}

	dev_info(&sdpmmch->pdev->dev, "rdqs set: value: %lld, raw: 0x%x\n", val, rdqs_raw);

	temp &= ~0x3FFU;
	mmch_writel(temp|rdqs_raw, sdpmmch->base + MMCH_DDR200_DLINE_CTRL);

	mmch_writel(0x1, sdpmmch->base+MMCH_DDR200_ASYNC_FIFO_CTRL);
	udelay(1);
	mmch_writel(0x0, sdpmmch->base+MMCH_DDR200_ASYNC_FIFO_CTRL);

	mmc_release_host(host);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_rdqs_fops, mmch_dbg_rdqs_get, mmch_dbg_rdqs_set,
	"%llu\n");

/* 
 * This is Speed Test Util funcs.
 */
static unsigned long mmch_dbg_calc_speed(unsigned long bytes, struct timeval * start, struct timeval * finish)
{
	unsigned long us, speed_kbs;

	us = ((u32)(finish->tv_sec - start->tv_sec) * 1000000u) +
	     ((u32)(finish->tv_usec - start->tv_usec) / 1u);

	if(us)
		speed_kbs = (bytes / us) << 10;
	else
		speed_kbs = 0;

	return speed_kbs;
}

/* return total secters */
static unsigned int mmch_dbg_get_capacity(struct mmc_card *card)
{
		if (!mmc_card_sd(card) && mmc_card_blockaddr(card))
				return card->ext_csd.sectors;
		else
				return card->csd.capacity << (card->csd.read_blkbits - 9);
}

static int
mmch_dbg_prepare_mrq(struct mmc_card *card, struct mmc_request *mrq, u32 blk_addr, u32 blk_count, int is_write)
{
	struct mmc_host *host = NULL;

	if(!card) {
		pr_err("mmch_dbg_prepare_mrq: card is NULL\n");
		return -EINVAL;
	}

	host = card->host;

	if(!(mrq && mrq->cmd && mrq->data && mrq->stop)) {
		pr_err("mmch_dbg_prepare_mrq: invalied arg!\n");
		if(mrq)
			pr_err("mmch_dbg_prepare_mrq: mrq %p, sbc %p, cmd %p, data %p, stop %p\n",
				mrq, mrq->sbc, mrq->cmd, mrq->data, mrq->stop);
		else
			pr_err("mmch_dbg_prepare_mrq: mrq is NULL\n");

		return -EINVAL;
	}

	if(mrq->sbc) {
		mrq->sbc->opcode = MMC_SET_BLOCK_COUNT;
		mrq->sbc->arg = blk_count;
		mrq->sbc->flags = MMC_RSP_R1 | MMC_CMD_AC;
		if (!mmc_card_blockaddr(card))
			mrq->sbc->arg <<= 9;
	}

	mrq->cmd->opcode = is_write ? MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	mrq->cmd->arg = blk_addr;
	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	if (!mmc_card_blockaddr(card))
		mrq->cmd->arg <<= 9;

	mrq->data->blksz = 512;
	mrq->data->blocks = blk_count;
	mrq->data->flags = is_write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = NULL;
	mrq->data->sg_len = 0;

	mrq->stop->opcode = MMC_STOP_TRANSMISSION;
	mrq->stop->arg = 0;
	mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;


	mrq->cmd->error = 0;
	mrq->cmd->mrq = mrq;
	if (mrq->data) {
		BUG_ON(mrq->data->blksz > host->max_blk_size);
		BUG_ON(mrq->data->blocks > host->max_blk_count);
		BUG_ON(mrq->data->blocks * mrq->data->blksz >
			host->max_req_size);


		mrq->cmd->data = mrq->data;
		mrq->data->error = 0;
		mrq->data->mrq = mrq;
		if (mrq->stop) {
			mrq->data->stop = mrq->stop;
			mrq->stop->error = 0;
			mrq->stop->mrq = mrq;
		}
	}

	return 0;
}

static int mmch_dbg_writespeed_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;

	static struct scatterlist sg[SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command sbc = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i;
	static u8 __aligned(128) buf[0x1000] = {0};

	static struct timeval start, finish;
	unsigned long us;

	u32 status[4];

	for(i = 0; i < (int)ARRAY_SIZE(buf); i+=2) {
		buf[i] = 0x55;
		buf[i+1] = 0xAA;
	}

	sg_init_table(sg, SG_NUM);
	for(i = 0; i < SG_NUM; i++) {
		sg_set_buf(sg+i, buf, ARRAY_SIZE(buf));
	}

	if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		mrq.sbc = &sbc;
	mrq.cmd = &cmd;
	mrq.data = &mmcdata;
	mrq.stop = &stop;

	mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-0x2000/*4MB*/, 0x2000, true);

	mmcdata.sg = sg;
	mmcdata.sg_len = SG_NUM;

	mmc_claim_host(host);
	do_gettimeofday(&start);

	mmc_wait_for_req(host, &mrq);

	do {
		udelay(10);
		mmc_send_status(host->card, status);
	} while( !(status[0] & R1_READY_FOR_DATA) || (R1_CURRENT_STATE(status[0]) == R1_STATE_PRG) );

	do_gettimeofday(&finish);
	mmc_release_host(host);

	*val = (u64)(cmd.error | (mmcdata.error <<16));

	if(!cmd.error && !mmcdata.error) {
		us = ((u32)(finish.tv_sec - start.tv_sec) * 1000000u) +
						     ((u32)(finish.tv_usec - start.tv_usec) / 1u);
		pr_info("mmcwrite: now speed %4luMB/s, len %10dbyte, %luus\n",
			mmch_dbg_calc_speed(mmcdata.blksz*mmcdata.blocks, &start, &finish)>>10, mmcdata.blksz*mmcdata.blocks, us);
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_writespeed_get_fops, mmch_dbg_writespeed_get, NULL,
	"0x%08llx\n");

enum {
	PATTERN_WRITE,
	RANDOM_WRITE,
	MAX_WRITE,
};


#define WRITE_SG_NUM 1
static int mmch_dbg_write_set(void *data, u64 val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	static struct scatterlist sg[WRITE_SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command sbc = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i;
	static u8 __aligned(128) buf[0x1000] = {0};

	u32 status[4];

	unsigned long totalbytes = 0;

	for(i = 0; i < (int)ARRAY_SIZE(buf); i+=2) {
		buf[i] = 0xFF;
		buf[i+1] = 0x00;
	}

	//sg_init_one(sg, buf, ARRAY_SIZE(buf));
	sg_init_table(sg, WRITE_SG_NUM);
	for(i = 0; i < WRITE_SG_NUM; i++) {
		sg_set_buf(sg+i, buf, ARRAY_SIZE(buf));
	}

	while(totalbytes < (0x100000*1024)) {

		if(val == RANDOM_WRITE) {
			get_random_bytes(&buf, sizeof(buf)/4);
			memcpy(buf+512, buf, 512);
			memcpy(buf+(512*2), buf, 512);
			memcpy(buf+(512*3), buf, 512);
			dma_map_sg(&sdpmmch->pdev->dev, &sg[0], 1, DMA_TO_DEVICE);
			dma_unmap_sg(&sdpmmch->pdev->dev, &sg[0], 1, DMA_TO_DEVICE);
		}

		if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
			mrq.sbc = &sbc;
		mrq.cmd = &cmd;
		mrq.data = &mmcdata;
		mrq.stop = &stop;

		mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-(0x100000*1024/512)+(totalbytes/512), ARRAY_SIZE(buf)*WRITE_SG_NUM/512, true);

		mmcdata.sg = sg;
		mmcdata.sg_len = WRITE_SG_NUM;


		mmc_claim_host(host);
		mmc_wait_for_req(host, &mrq);

		if(mmcdata.error) {
			mmc_release_host(host);
			return -(int)mmcdata.error;
		}

		do {
			udelay(10);
			mmc_send_status(host->card, status);
		} while( !(status[0] & R1_READY_FOR_DATA) || (R1_CURRENT_STATE(status[0]) == R1_STATE_PRG) );

		mmc_release_host(host);

		totalbytes += mmcdata.blksz*mmcdata.blocks;

	}
	return 0;
}

static int mmch_dbg_write_get(void *data, u64 *val)
{
	pr_info("PATTERN_WRITE : echo %d > write\n"
			"RANDOM_WRITE : echo %d > write\n",
			PATTERN_WRITE, RANDOM_WRITE);
	*val = 0;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_write_get_fops, mmch_dbg_write_get, mmch_dbg_write_set,
	"0x%08llx\n");


static int mmch_dbg_write_val_set(void *data, u64 val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	static struct scatterlist sg[WRITE_SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command sbc = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i;
	static u8 __aligned(128) buf[0x1000] = {0};

	u32 status[4];

	unsigned long totalbytes = 0;

	for(i = 0; i < (int)ARRAY_SIZE(buf); i+=2) {
		buf[i] = val&0xFF;
		buf[i+1] = (val>>8)&0xFF;
	}

	//sg_init_one(sg, buf, ARRAY_SIZE(buf));
	sg_init_table(sg, WRITE_SG_NUM);
	for(i = 0; i < WRITE_SG_NUM; i++) {
		sg_set_buf(sg+i, buf, ARRAY_SIZE(buf));
	}

	dma_map_sg(&sdpmmch->pdev->dev, &sg[0], 1, DMA_TO_DEVICE);
	dma_unmap_sg(&sdpmmch->pdev->dev, &sg[0], 1, DMA_TO_DEVICE);


	while(totalbytes < (0x100000*1024)) {

		if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
			mrq.sbc = &sbc;
		mrq.cmd = &cmd;
		mrq.data = &mmcdata;
		mrq.stop = &stop;

		mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-(0x100000*1024/512)+(totalbytes/512), ARRAY_SIZE(buf)*WRITE_SG_NUM/512, true);

		mmcdata.sg = sg;
		mmcdata.sg_len = WRITE_SG_NUM;


		mmc_claim_host(host);
		mmc_wait_for_req(host, &mrq);

		if(mmcdata.error) {
			mmc_release_host(host);
			return -(int)mmcdata.error;
		}

		do {
			udelay(10);
			mmc_send_status(host->card, status);
		} while( !(status[0] & R1_READY_FOR_DATA) || (R1_CURRENT_STATE(status[0]) == R1_STATE_PRG) );

		mmc_release_host(host);

		totalbytes += mmcdata.blksz*mmcdata.blocks;

	}
	return 0;
}

static int mmch_dbg_write_val_get(void *data, u64 *val)
{
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_write_val_get_fops, mmch_dbg_write_val_get, mmch_dbg_write_val_set,
	"0x%08llx\n");

static int mmch_dbg_readspeed_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;

	static struct scatterlist sg[SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command sbc = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i;
	static u8 __aligned(128) buf[0x1000] = {0};

	static struct timeval start, finish;
	unsigned long us;

	memset(buf, 0x0, ARRAY_SIZE(buf));

	sg_init_table(sg, SG_NUM);
	for(i = 0; i < SG_NUM; i++) {
		sg_set_buf(sg+i, buf, ARRAY_SIZE(buf));
	}

	if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		mrq.sbc = &sbc;
	mrq.cmd = &cmd;
	mrq.data = &mmcdata;
	mrq.stop = &stop;

	mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-0x2000/*4MB*/, 0x2000, false);

	mmcdata.sg = sg;
	mmcdata.sg_len = SG_NUM;

	mmc_claim_host(host);
	do_gettimeofday(&start);

	mmc_wait_for_req(host, &mrq);

	do_gettimeofday(&finish);
	mmc_release_host(host);


	*val = (u64)(cmd.error | (mmcdata.error <<16));

	if(!cmd.error && !mmcdata.error) {
		us = ((u32)(finish.tv_sec - start.tv_sec) * 1000000u) +
						     ((u32)(finish.tv_usec - start.tv_usec) / 1u);
		pr_info("mmcread: now speed %4luMB/s, len %10dbyte, %luus\n",
			mmch_dbg_calc_speed(mmcdata.blksz*mmcdata.blocks, &start, &finish)>>10, mmcdata.blksz*mmcdata.blocks, us);
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_readspeed_get_fops, mmch_dbg_readspeed_get, NULL,
	"0x%08llx\n");

static int mmch_dbg_read_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;

	static struct scatterlist sg[SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command sbc = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int i;
	static u8 __aligned(128) buf[0x1000] = {0};

	unsigned long totalbytes = 0;

	while(totalbytes < (0x100000*1024)) {

		memset(buf, 0x0, ARRAY_SIZE(buf));

		sg_init_table(sg, SG_NUM);
		for(i = 0; i < SG_NUM; i++) {
			sg_set_buf(sg+i, buf, ARRAY_SIZE(buf));
		}

		if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
			mrq.sbc = &sbc;
		mrq.cmd = &cmd;
		mrq.data = &mmcdata;
		mrq.stop = &stop;

		mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-(0x100000*1024/512)+(totalbytes/512),ARRAY_SIZE(buf)*SG_NUM/512, false);

		mmcdata.sg = sg;
		mmcdata.sg_len = SG_NUM;


		mmc_claim_host(host);
		mmc_wait_for_req(host, &mrq);
		mmc_release_host(host);

		totalbytes += mmcdata.blksz*mmcdata.blocks;
	}

	*val = (u64)(cmd.error | (mmcdata.error <<16));

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_read_get_fops, mmch_dbg_read_get, NULL,
	"0x%08llx\n");


//DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_drv_tuning_fops, mmch_dbg_drv_tuning, NULL,"0x%08llx\n");

/*
static int
mmch_dbg_rdqs_tuning(void *data, u64 *val)
{
	struct mmc_host *host = data;
	mmc_get_card(host->card);
	*val = (u64)sdp_mmch_do_rdqs_tuning(host, host->card, 2, 255, 1, NULL);
	mmc_put_card(host->card);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_rdqs_tuning_fops, mmch_dbg_rdqs_tuning, NULL,	"%lld\n");
*/
static int
mmch_dbg_hs400_set(void *data, u64 val)
{
	struct mmc_host *host = data;
	struct mmc_card *card = host->card;
	u8 card_type = host->card->ext_csd.raw_card_type;

	if(val == 0) {
		host->caps2 &= ~(u32)MMC_CAP2_HS400;
		card->mmc_avail_type &= ~(u32)EXT_CSD_CARD_TYPE_HS400_1_8V;
		card->mmc_avail_type &= ~(u32)EXT_CSD_CARD_TYPE_HS400_1_2V;
	}
	else if(val == 1
		&& (card_type&EXT_CSD_CARD_TYPE_HS400)) {
		host->caps2 |= MMC_CAP2_HS400;
		card->mmc_avail_type |= EXT_CSD_CARD_TYPE_HS400_1_8V;
		card->mmc_avail_type |= EXT_CSD_CARD_TYPE_HS400_1_2V;
	} else {
		return -EINVAL;
	}

	return mmc_hw_reset(host);
}
DEFINE_SIMPLE_ATTRIBUTE(mmch_dbg_hs400_fops, NULL, mmch_dbg_hs400_set,
	"0x%08llx\n");

int sdp_mmch_partition_switch(struct mmc_card *card, u32 partition){

	u8 save_part;
	int ret = 0;
	
	partition &= EXT_CSD_PART_CONFIG_ACC_MASK;
	save_part = card->ext_csd.part_config;

	card->ext_csd.part_config &= (~EXT_CSD_PART_CONFIG_ACC_MASK);
	card->ext_csd.part_config |= (u8) partition;

	ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_PART_CONFIG, card->ext_csd.part_config,
					card->ext_csd.part_time);
	if (ret) {
		pr_err("sdp-mmc boot part switch fail!\n");
		return -1;
	}

	return (u32)save_part;
}

int sdp_mmch_museo_lba_request(struct board_settings_museo *museo_bs, bool is_write) {
#ifdef CONFIG_EVAL
	#define MMC_DEV_PATH	"/dev/mmcblk0p4"
#else
	#define MMC_DEV_PATH	"/dev/mmcblk0p22"
#endif
	struct mmc_host *host = NULL;
	struct mmc_card *card = NULL;
	struct mmc_queue *mq = NULL;
	struct block_device *bdev = NULL;
	static struct scatterlist sg = {0};
	struct mmc_request mrq = {0};
	struct mmc_command sbc = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	int bs_sector = 128+8+1; /// end - secondary GPT 64KB - board settings 4KB - main ddr vref 512Byte
	int museo_lba = 0;
	static u8 __aligned(128) buf[512] = {0};
	int i=0;
	u32 status = 0;
	int ret = 0;
	u32 restored_partition = 0;
	bdev = blkdev_get_by_path(MMC_DEV_PATH, FMODE_READ|FMODE_WRITE, 0);
	if (IS_ERR(bdev)) {
		pr_err("can't get blkdev(%s) return %ld\n", MMC_DEV_PATH, PTR_ERR(bdev));
		return -1;
	} else {
		mq = (struct mmc_queue *) (bdev_get_queue(bdev)->queuedata);
		host = mq->card->host;
		card = host->card;
	}
	museo_lba = (card->ext_csd.sectors - bs_sector);
	pr_err("sdp-mmc total sectors : %d, board_settings_ddr size : %d, %s\n", card->ext_csd.sectors, sizeof(struct board_settings_museo), is_write ? "WRITE" : "READ");
	if(is_write){
		if(sizeof(struct board_settings_museo) > 512){
			pr_err("sdp-mmc size is over 512Byte\n");
			return -1;
		}
		memcpy(buf, museo_bs, sizeof(struct board_settings_museo));	
	}
	sg_init_table(&sg, 1);
	sg_set_buf(&sg, buf, ARRAY_SIZE(buf));
	if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23)){
		mrq.sbc = &sbc;
	}
	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;
	mmch_dbg_prepare_mrq(card, &mrq, museo_lba, WRITE_SG_NUM, is_write);
	data.sg = &sg;
	data.sg_len = WRITE_SG_NUM;
	mmc_claim_host(host);

	//switch to user partition
	pr_err("sdp-mmc part_config : 0x%x\n", card->ext_csd.part_config);
	if(card->ext_csd.part_config & EXT_CSD_PART_CONFIG_ACC_MASK){
		restored_partition = sdp_mmch_partition_switch(card, USER_PARTITION);
	}
	
	mmc_wait_for_req(host, &mrq);
	if(!cmd.error){
		do {
			ret = mmc_send_status(card, &status);
			if(ret) {
				pr_err("sdp-mmc send status fail for lba_request!(%d)", ret);
				break;
			}
			udelay(1);
		} while(R1_CURRENT_STATE(status) == R1_STATE_PRG);
	}
	else{
		pr_err("sdp-mmc cmd error for lba_request!");
		mmc_release_host(host);
		return -(int)cmd.error;
	}
	if(data.error) {
		pr_err("sdp-mmc data error for lba_request!");
		mmc_release_host(host);
		return -(int)data.error;
	}

	//switch to resotred partition
	if(restored_partition & EXT_CSD_PART_CONFIG_ACC_MASK){
		sdp_mmch_partition_switch(card, restored_partition);
	}

	mmc_release_host(host);
	if(!is_write){
		memcpy(museo_bs, buf, sizeof(struct board_settings_museo));	
#if 0
		for(i=0; i<512; i++){
			if(i%0x10 == 0)
				printk("\n");			
			printk("0x%x,", buf[i]);
		}
#endif
	}
	return 0;
}
EXPORT_SYMBOL(sdp_mmch_museo_lba_request);

static inline u32 checksum(u32 *data, size_t len)
{
	u32 a = 1, b = 0;
	len /= 4;
	while (len--) {
		a = (a + (((*data) >> 24) & 0xff)) % 65521;
		b = (b + a) % 65521;

		a = (a + (((*data) >> 16) & 0xff)) % 65521;
		b = (b + a) % 65521;

		a = (a + (((*data) >> 8) & 0xff)) % 65521;
		b = (b + a) % 65521;

		a = (a + (((*data) >> 0) & 0xff)) % 65521;
		b = (b + a) % 65521;
		
		data++;
	}
	return (b << 16) | a;
}


static int museo_mmc_get(void *data, u64 *val){

	int checksum = 0;
	int ret = 0;
	int i = 0;
	struct board_settings_museo museo_bs={0};

	sdp_mmch_museo_lba_request(&museo_bs, 0);

	printk("sdp-museo chipid : 0x%llx\n", museo_bs.head.chipid);
	printk("sdp-museo checksum : 0x%08x\n", museo_bs.head.checksum);
	printk("sdp-museo runtime_flag : 0x%08x\n", museo_bs.head.runtime_flags);
	printk("sdp-museo rdqs : ");
	for(i = 0; i < 4; i++){
		printk("sdp-museo 0x%08x ", museo_bs.museo_ddr.soc_vref[i]);
	}
	printk("\n");
	printk("sdp-museo wdqs : ");
	for(i = 0; i < 4; i++){
		printk("sdp-museo 0x%08x ", museo_bs.museo_ddr.mr14[i]);
	}
	printk("\n");
	return 0;
}

static int
museo_mmc_clear(void *data, u64 val)
{
	struct board_settings_museo museo_bs={0};

	
	return sdp_mmch_museo_lba_request(&museo_bs, val);
}
DEFINE_SIMPLE_ATTRIBUTE(museo_lba_fops, museo_mmc_get, museo_mmc_clear, "0x%08llx\n");

static int mmc_dump_get(void *data, u64 *val){
	struct mmc_host *host = data;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	mmch_register_dump(sdpmmch);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmc_dump_fops, mmc_dump_get, NULL, "0x%08llx\n");

int sdp_mmch_polling_request(struct mmc_host *host, u32 blk_addr, u32 blk_count, u8 *buf, bool is_write) {
	SDP_MMCH_T *sdpmmch = mmc_priv(host);
	struct mmc_card *card = host->card;
	static struct scatterlist sg[SG_NUM];
	struct mmc_request mrq = {0};
	struct mmc_command sbc = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	const unsigned int sgnum = (blk_count/(host->max_seg_size/512)) + (blk_count%(host->max_seg_size/512)?1:0);
	int i, timeout_ms = 1000;

	dev_info(&sdpmmch->pdev->dev, "PREQ: polling request enter!\n\n\n");

	//dev_info(&sdpmmch->pdev->dev, "PREQ: host info start\n");
	//mmch_register_dump_notitle(sdpmmch);
	//dev_info(&sdpmmch->pdev->dev, "PREQ: host info end\n\n");

	dev_info(&sdpmmch->pdev->dev, "PREQ: in_irq(%lx), in_softirq(%lx), in_interrupt(%lx), in_serving_softirq(%lx)\n",
		in_irq(), in_softirq(), in_interrupt(), in_serving_softirq());

	dev_info(&sdpmmch->pdev->dev, "PREQ: host %p, blk_addr 0x%x, blk_count 0x%x, buf %p, is_write %s\n", host, blk_addr, blk_count, buf, is_write?"true":"false");

	sg_init_table(sg, sgnum);
	for(i = 0; i < sgnum-1; i++) {
		sg_set_buf(sg+i, buf+(host->max_seg_size * i), host->max_seg_size);
	}
	sg_set_buf(sg+i, buf+(host->max_seg_size * (i)), (blk_count*512)-(host->max_seg_size*(i)));


	if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		mrq.sbc = &sbc;

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	mmch_dbg_prepare_mrq(card, &mrq, blk_addr, blk_count, is_write);

	data.sg = sg;
	data.sg_len = sgnum;
	data.timeout_ns = 1000*1000000;

	if(host->claimed) {
		dev_info(&sdpmmch->pdev->dev, "PREQ: forced release host->claimed\n");
		host->claimed = 0;
	}

	//mmc_claim_host(host);

	if(spin_is_locked(&sdpmmch->lock)) {
		dev_info(&sdpmmch->pdev->dev, "PREQ: forced unlock sdpmmch->lock\n");
		spin_unlock(&sdpmmch->lock);
	}

	if(spin_is_locked(&sdpmmch->state_lock)) {
		dev_info(&sdpmmch->pdev->dev, "PREQ: forced unlock sdpmmch->state_lock\n");
		spin_unlock(&sdpmmch->state_lock);
	}

	if(sdpmmch->state != MMCH_STATE_IDLE) {
		dev_info(&sdpmmch->pdev->dev, "PREQ: forced state to IDLE and request end\n");
		sdpmmch->state = MMCH_STATE_IDLE; 
		mmch_request_end(sdpmmch);
	}

	sdp_mmch_host_reset(sdpmmch);

	/* interrupt disable */
	mmch_writel(0, sdpmmch->base+MMCH_CTRL);

	dev_info(&sdpmmch->pdev->dev, "PREQ: start polling request\n");
	request_warn_msg=0;
	sdp_mmch_request(host, &mrq);
	request_warn_msg=1;


	dev_info(&sdpmmch->pdev->dev, "PREQ: polling isr & tasklet\n");
	while(sdpmmch->state != MMCH_STATE_IDLE ) {
		do {
			udelay(10);
			sdp_mmch_isr(SDP_MMCH_IRQ_IN_POLLING, sdpmmch);
		} while(sdpmmch->event == 0);

		dev_info(&sdpmmch->pdev->dev, "PREQ: run mmch_tasklet(event 0x%08lx, accum 0x%08lx)\n", sdpmmch->event, sdpmmch->event_accumulated);
		mmch_tasklet((unsigned long)sdpmmch);
	}

	dev_info(&sdpmmch->pdev->dev, "PREQ: start busy waitting..\n");

	for(i = 0; mmch_readl(sdpmmch->base+MMCH_STATUS)&MMCH_STATUS_DATA_BUSY; i++) {
		if(i >= timeout_ms) {
			dev_err(&sdpmmch->pdev->dev, "PREQ: busy waitting timeout!!!(%dms)\n", timeout_ms);
			break;
		}
		udelay(1000);
	}

	dev_info(&sdpmmch->pdev->dev, "PREQ: end polling request, wait %dms\n", i);

	/* interrupt enable */
	mmch_writel(MMCH_CTRL_INT_ENABLE, sdpmmch->base+MMCH_CTRL);

	//mmc_release_host(host);

	return 0;
}
EXPORT_SYMBOL(sdp_mmch_polling_request);



/* register debugfs */
static void mmch_add_host_debugfs(struct mmc_host *host)
{
	struct dentry *root = host->debugfs_root;
	SDP_MMCH_T *sdpmmch = mmc_priv(host);

	if (!root) {
		dev_err(&sdpmmch->pdev->dev, "host debugfs is not initialized.\n");
		return;
	}
	if (!debugfs_create_file("rdqs_tune", S_IRUSR, root, host, &mmc_rdqs_fops))
 		return;

	if (!debugfs_create_file("drv_tune", S_IRUSR, root, host, &mmc_drv_fops))
 		return;

	if (!debugfs_create_file("sam_tune", S_IRUSR, root, host, &mmc_sam_fops))
 		return;

	if (!debugfs_create_file("error", S_IRUSR, root, host, &mmc_error_fops))
 		return;
	//For ASB Board Test
	if (!debugfs_create_file("writeboot", S_IRUSR, root, host, &sdp_mmch_dbg_write_boot_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create writeboot file in debugfs.\n");
		return;
	}
	if (!debugfs_create_file("writepattern", S_IRUSR, root, host, &sdp_mmch_dbg_write_pattern_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create writepattern file in debugfs.\n");
		return;
	}
	if (!debugfs_create_file("readpattern", S_IRUSR, root, host, &sdp_mmch_dbg_read_pattern_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create readpattern file in debugfs.\n");
		return;
	}
	if (!debugfs_create_file("strength", S_IRUSR, root, host,&mmch_ew_dbg_strength_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create strength file in debugfs.\n");
		return;
	}
	if (!debugfs_create_file("clk_phase", S_IRUSR, root, host,&mmch_ew_dbg_clk_phase_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create strength file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("rdqs", S_IRUSR, root, host,
			&mmch_dbg_rdqs_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create rdqs file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("writespeed", S_IRUSR, root, host,
			&mmch_dbg_writespeed_get_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create writespeed file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("write", S_IRUSR, root, host,
			&mmch_dbg_write_get_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create write file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("write_val", S_IRUSR, root, host,
			&mmch_dbg_write_val_get_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create write_val file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("readspeed", S_IRUSR, root, host,
			&mmch_dbg_readspeed_get_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create read file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("read", S_IRUSR, root, host,
			&mmch_dbg_read_get_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create read file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("hs400", S_IRUSR, root, host,
			&mmch_dbg_hs400_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create hs400 file in debugfs.\n");
		return;
	}

	if(!debugfs_create_u32("max_polling_ms", S_IRUGO|S_IWUGO, root, (u32 *)&sdpmmch->max_polling_ms))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create max_polling_ms file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("museo_lba", S_IRUSR, root, host, &museo_lba_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create read file in debugfs.\n");
		return;
	}

	if (!debugfs_create_file("dump", S_IRUSR, root, host, &mmc_dump_fops))
	{
		dev_err(&sdpmmch->pdev->dev, "failed to create read file in debugfs.\n");
		return;
	}

	return;
}
#endif


static int sdp_mmch_probe(struct platform_device *pdev)
{
	SDP_MMCH_T *sdpmmch = NULL;
	struct mmc_host *host = NULL;
	struct resource *r, *mem = NULL;
	int ret = 0;
	unsigned int irq = 0;
	size_t mem_size;
	struct sdp_mmch_plat *platdata;
#if defined(CONFIG_ARCH_SDP1803)
	void *__addr;
#endif

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "request iomem error\n");
		ret = -ENODEV;
		goto out;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "request IRQ error\n");
		goto out;
	}
	irq = (unsigned int)ret;

	ret = -EBUSY;
	mem_size = (size_t) (r->end - r->start) +1;

	mem = request_mem_region(r->start, mem_size, pdev->name);
	if (!mem)
		goto out;

	ret = -ENOMEM;
	host = mmc_alloc_host(sizeof(SDP_MMCH_T), &pdev->dev);
	if (!host)
		goto out;

	sdpmmch = mmc_priv(host);
	sdpmmch->pdev = pdev;
	sdpmmch->host = host;
	sdpmmch->mem_res = mem;

	sdpmmch->base = ioremap((u32) mem->start, mem_size);

	if (!sdpmmch->base)
		goto out;

	sdpmmch->irq = irq;

	sdpmmch->pm_is_valid_clk_delay = false;
	sdpmmch->in_tuning = false;
	sdpmmch->is_hs400 = false;
	sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_DISABLED;
	sdpmmch->request_count = 0;
	request_warn_msg=1;

	platform_set_drvdata(pdev, sdpmmch);

#ifdef CONFIG_OF
	pdev->dev.platform_data = kzalloc(sizeof(struct sdp_mmch_plat), GFP_KERNEL);
	sdp_mmc_of_parse_dt(&pdev->dev, (struct sdp_mmch_plat *)pdev->dev.platform_data);
	mmc_of_parse(host);
#endif

	platdata = dev_get_platdata(&pdev->dev);
	if(!platdata)
		goto out;
	host->ops = &sdp_mmch_ops;
	host->f_min = platdata->min_clk;
	host->f_max = platdata->max_clk;
	host->caps  |= platdata->caps;
	host->caps2 |= platdata->caps2;
	if(platdata->get_ocr)
		host->ocr_avail = (u32) platdata->get_ocr(0);
	else
		host->ocr_avail = MMC_VDD_165_195 | MMC_VDD_33_34;

#ifdef CONFIG_SDP_MMC_USE_PRE_DEFINED_XFER
	host->caps |= MMC_CAP_CMD23;
#endif

	sdpmmch->successed_rdqs= -1;
	/* MMC/SD controller limits for multiblock requests */
	host->max_seg_size	= 0x1000;
	host->max_segs		= NR_SG;
	host->max_blk_size	= 0xFFFF;/* 16bit */
	host->max_blk_count	= host->max_seg_size * host->max_segs / 512;
	host->max_req_size	= host->max_seg_size * host->max_segs;

	sdpmmch->fifo_depth	= platdata->fifo_depth;

	mmch_platform_init(sdpmmch);
	mmch_initialization(sdpmmch);

	sdpmmch->data_shift = (u8)(((mmch_readl(sdpmmch->base+MMCH_HCON)>>7)&0x7)+1);/* bus width */

	dev_info(&pdev->dev, "clk %uMHz, irq %d, fifo depth %d, bus width %ubit, max_clk %uHz, min_clk %uHz\n",
		platdata->processor_clk/1000000U, sdpmmch->irq, sdpmmch->fifo_depth, 0x8U<<sdpmmch->data_shift, platdata->max_clk, platdata->min_clk);

#ifdef CONFIG_SDP_MMC_64BIT_ADDR
		if(!(mmch_readl(sdpmmch->base+MMCH_HCON)&(0x1<<27))) {
			dev_err(&pdev->dev, "Config error. MMCIF is Not Support 64bit address!\n");
			ret = -ENOTSUPP;
			goto out;
		}
#else
		if(mmch_readl(sdpmmch->base+MMCH_HCON)&(0x1<<27)) {
			dev_err(&pdev->dev, "Config error. MMCIF is Not Support 32bit address!\n");
			ret = -ENOTSUPP;
			goto out;
		}
#endif


	spin_lock_init(&sdpmmch->lock);
	spin_lock_init(&sdpmmch->state_lock);

	tasklet_init(&sdpmmch->tasklet, mmch_tasklet, (unsigned long)sdpmmch);
	tasklet_disable(&sdpmmch->tasklet);

	sdpmmch->max_polling_ms = 200;
	sdpmmch->timeout_ms = 0;
	sdpmmch->polling_ms = 0;
	init_timer(&sdpmmch->timeout_timer);
	setup_timer(&sdpmmch->timeout_timer, mmch_timeout_callback, (unsigned long)sdpmmch);

	dev_info(&pdev->dev, "lock: %p, state_lock: %p, tasklet: %p, timer: %p\n",
		&sdpmmch->lock, &sdpmmch->state_lock, &sdpmmch->tasklet, &sdpmmch->timeout_timer);


	/* select xfer mode */
#ifdef CONFIG_SDP_MMC_DMA
	sdpmmch->xfer_mode = MMCH_XFER_DMA_MODE;
#else
	sdpmmch->xfer_mode = MMCH_XFER_PIO_MODE;
#endif

	if(platdata->force_pio_mode) {
		sdpmmch->xfer_mode = MMCH_XFER_PIO_MODE;
		dev_info(&pdev->dev, "Forced PIO Mode!\n");
	}

	if(sdpmmch->xfer_mode == MMCH_XFER_DMA_MODE) {

		if(mmch_dma_desc_init(sdpmmch) < 0) //Internal dma controller desciptor initialization
			goto out;

		dev_info(&pdev->dev, "Using DMA, %d-bit mode\n",
			(host->caps & MMC_CAP_8_BIT_DATA) ? 8 : ((host->caps & MMC_CAP_4_BIT_DATA) ? 4 : 1));

	} else if(sdpmmch->xfer_mode == MMCH_XFER_PIO_MODE) {

		if(sdpmmch->data_shift == 3)
		{
			sdpmmch->pio_pull = mmch_pio_pull_data64;
			sdpmmch->pio_push = mmch_pio_push_data64;
		}
		else
		{
			sdpmmch->pio_pull = NULL;
			sdpmmch->pio_push = NULL;
			dev_err(&pdev->dev, "%dbit is No Supported Bus Width!!", 0x1<<sdpmmch->data_shift);
			ret = -ENOTSUPP;
			goto out;
		}

		dev_info(&pdev->dev, "Using PIO, %d-bit mode\n",
			(host->caps & MMC_CAP_8_BIT_DATA) ? 8 : ((host->caps & MMC_CAP_4_BIT_DATA) ? 4 : 1));
	} else {
		dev_err(&pdev->dev, "invalid xfer mode!!(%d)", sdpmmch->xfer_mode);
		ret = -EINVAL;
		goto out;
	}

#ifdef CONFIG_SDP_MMC_USE_AUTO_STOP_CMD
	dev_info(&pdev->dev, "Using H/W Auto Stop CMD.\n");
#endif

	if(host->caps & MMC_CAP_CMD23) {
		dev_info(&pdev->dev, "Using pre-defined data xfer.\n");
	}

	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFS_INPUT_CLOCK) {
		dev_info(&pdev->dev, "Fixup: Golf-S Input clock change.\n");
	}

	if(platdata->fixups & SDP_MMCH_FIXUP_GOLFP_UD_TUNING_FIXED) {
		dev_info(&pdev->dev, "Fixup: Golf-P UD tuning fixed.\n");
	}

	ret = request_irq(irq, sdp_mmch_isr, 0, dev_name(&pdev->dev), sdpmmch);
	if (ret)
		goto out;

	if(!cpumask_empty(&platdata->irq_affinity_mask)) {
		irq_set_affinity(irq, &platdata->irq_affinity_mask);
	} else if(cpu_online(platdata->irq_affinity)) {
		irq_set_affinity(irq, cpumask_of(platdata->irq_affinity));
	}

	if(sdpmmch->xfer_mode == MMCH_XFER_PIO_MODE) {
		if(num_online_cpus() > 2) {
			irq_set_affinity(irq, cpumask_of(2));
		}
	}
#ifdef CONFIG_ARCH_SDP1202
	if(num_online_cpus() > 2)
		irq_set_affinity(irq, cpumask_of(2));
#endif

	rename_region(mem, mmc_hostname(host));

	/* save sdp mmch struct address for debug */
	mmch_writel((uintptr_t)sdpmmch, sdpmmch->base+MMCH_USRID);
	sw_timeout_count = 0;

	/* add mmc host, after init! */
	ret = mmc_add_host(host);
	if (ret < 0)
		goto out;

#ifdef CONFIG_DEBUG_FS
	mmch_add_host_debugfs(host);
#endif

#if defined(CONFIG_ARCH_SDP1803)
	__addr = ioremap(platdata->strength_reg, sizeof(u32));
	mmch_writel(((readl(__addr) & (~platdata->strength_mask)) | platdata->strength_value), __addr);
	iounmap(__addr);

	v_pll_clk_cnt_base = ioremap(PLL_CLK_COUNTER_BASE, sizeof(int)*0x40);
	sdp_mmch_pll_counter(sdpmmch);
#endif

	return 0;

out:
	if(sdpmmch){
		if(sdpmmch->base)
			iounmap(sdpmmch->base);
	}

	if(host)
		mmc_free_host(host);

	if(mem)
		release_resource(mem);

	return ret;
}

static int sdp_mmch_remove(struct platform_device *pdev)
{
	SDP_MMCH_T *sdpmmch = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (sdpmmch){
#ifdef CONFIG_OF
		struct sdp_mmch_plat *plat = dev_get_platdata(&pdev->dev);
		kfree(plat->tuning_table.list);
		kfree(plat->tuning_hs200_sam_default.list);
		kfree(plat->tuning_hs200_drv_default.list);
		kfree(plat->tuning_hs400_sam_default.list);
		kfree(plat->tuning_hs400_drv_default.list);
#endif
		dma_free_coherent(sdpmmch->host->parent,
						MMCH_DESC_SIZE,
						sdpmmch->p_dmadesc_vbase,
						sdpmmch->dmadesc_pbase);

		mmc_remove_host(sdpmmch->host);
		free_irq(sdpmmch->irq, sdpmmch);
		del_timer_sync(&sdpmmch->timeout_timer);
		iounmap(sdpmmch->base);

		release_resource(sdpmmch->mem_res);

		mmc_free_host(sdpmmch->host);
	}

	return 0;
}

#ifdef CONFIG_PM
static int sdp_mmch_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	SDP_MMCH_T *sdpmmch = NULL;

	if(!dev || !pdev) {
		dev_err(dev, "device is null(dev %p, pdev %p)\n", dev, pdev);
		return -EINVAL;
	}

	sdpmmch = platform_get_drvdata(pdev);
	if(!sdpmmch) {
		dev_err(dev, "sdpmmch is null\n");
		return -EINVAL;
	}

	disable_irq(sdpmmch->irq);
	del_timer_sync(&sdpmmch->timeout_timer);

#ifdef CONFIG_OF
	/* update already stored value */
	sdpmmch->pm_is_valid_clk_delay = false;
	sdp_mmc_of_do_restoreregs(&sdpmmch->pdev->dev);
#endif

	sdpmmch->nr_bit = 0;
	sdpmmch->tuned_map = 0;
	sdpmmch->median = 0;
	sdpmmch->in_tuning = false;
	sdpmmch->is_hs400 = false;
	sdpmmch->rdqs_tune = MMCH_RDQS_TUNE_DISABLED;

	//card reset
	mmch_writel(mmch_readl(sdpmmch->base+MMCH_RST_n) & (~0x1), sdpmmch->base+MMCH_RST_n);

	return 0;
}

static int sdp_mmch_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	SDP_MMCH_T *sdpmmch = NULL;
#if defined(CONFIG_ARCH_SDP1803)
	struct sdp_mmch_plat *plat = dev_get_platdata(&pdev->dev);
	void *__addr;
#endif

	if(!dev || !pdev) {
		dev_err(dev, "device is null(dev %p, pdev %p)\n", dev, pdev);
		return -EINVAL;
	}

	sdpmmch = platform_get_drvdata(pdev);
	if(!sdpmmch) {
		dev_err(dev, "sdpmmch is null\n");
		return -EINVAL;
	}

	mmch_platform_init(sdpmmch);
	mmch_initialization(sdpmmch);

	//  Descriptor set
	mmch_writel((u32) sdpmmch->dmadesc_pbase, sdpmmch->base+MMCH_DBADDR);
#ifdef MMCH_DBADDRU
	mmch_writel((u32) sdpmmch->dmadesc_pbase>>32, sdpmmch->base+MMCH_DBADDRU);
#endif

	/* save sdp mmch struct address for debug */
	mmch_writel((uintptr_t)sdpmmch, sdpmmch->base+MMCH_USRID);

#if defined(CONFIG_ARCH_SDP1803)
	__addr = ioremap(plat->strength_reg, sizeof(u32));
	mmch_writel(((readl(__addr) & (~plat->strength_mask)) | plat->strength_value), __addr);
	iounmap(__addr);
	sdp_mmch_pll_counter(sdpmmch);
#endif

	enable_irq(sdpmmch->irq);

	return 0;
}

static const struct dev_pm_ops sdp_mmch_pm_ops = {
	.suspend_late  = sdp_mmch_suspend,
	.resume_early  = sdp_mmch_resume,
};
#endif

static const struct of_device_id sdp_mmch_dt_match[] = {
	{ .compatible = "samsung,sdp-mmc" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_mmch_dt_match);

static struct platform_driver sdp_mmch_driver = {

	.probe		= sdp_mmch_probe,
	.remove 	= sdp_mmch_remove,
	.driver 	= {
		.name = DRIVER_MMCH_NAME,
#ifdef CONFIG_OF
		.of_match_table = sdp_mmch_dt_match,
#endif
#ifdef CONFIG_PM
	.pm	= &sdp_mmch_pm_ops,
#endif
	},
};

static int __init sdp_mmc_host_init(void)
{
	pr_info("%s: registered SDP MMC Host driver. version %s\n",
		sdp_mmch_driver.driver.name, DRIVER_MMCH_VER);
	return platform_driver_register(&sdp_mmch_driver);
}

static void __exit sdp_mmc_host_exit(void)
{
	platform_driver_unregister(&sdp_mmch_driver);
}

subsys_initcall(sdp_mmc_host_init);
module_exit(sdp_mmc_host_exit);

MODULE_DESCRIPTION("Samsung DTV/BD MMC Host controller driver");
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("tukho.kim <tukho.kim@samsung.com>");

