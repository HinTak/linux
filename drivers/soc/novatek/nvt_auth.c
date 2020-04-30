/*
 * Cryptographic API.
 *
 * SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <mach/nvt_ungzip.h>
#include <linux/buffer_head.h>
#include <mach/nvt_hwclock.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <mach/nvt_auth.h>
#include "timelog.h"

//#define NT16M
//#define NVT_SHA_DEBUG
//#define NVT_SHA_TEST
//#define DBG_DESCRIPTOR
/*#define NVT_AUTH_INPUT_ISGL_NON_ALIGN*/
//#define NVT_SHA_DESCRIPTOR_SKIP

#define TRACE_FLOW 0
#define TRACE(...) \
	if(TRACE_FLOW) printk(__VA_ARGS__);

#define TRACE_ENTRY() \
	if(TRACE_FLOW) printk("nvt_auth %s %d entry\n", __FUNCTION__, __LINE__);

#define TRACE_EXIT() \
	if(TRACE_FLOW) printk("nvt_auth %s %d exit\n", __FUNCTION__, __LINE__);

/*ADMA*/
#define ADMA_HW_SETTING                 0x00
#define ADMA_SRC_DESCRIPTOR_ADDR        0x04
#define ADMA_SRC_CUR_DESCRIPTOR_ADDR    0x08
#define ADMA_SRC_STATUS                 0x0C
#define ADMA_DST_DESCRIPTOR_ADDR        0x10
#define ADMA_DST_CUR_DESCRIPTOR_ADDR    0x14
#define ADMA_DST_STATUS                 0x18
#define ADMA_HW_CONFIGURE               0x1C
#define ADMA_HW_DEBUG                   0x20
#define ADMA_SRC_BYTE_COUNT             0x24
#define ADMA_DST_BYTE_COUNT             0x28
#define ADMA_TIMEOUT                    0x2C
#define ADMA_INT_ENABLE                 0x30
#define ADMA_INT_STATUS_AND_CLR         0x34
#define ADMA_SRC_FINAL_READ_ADDR        0x38
#define ADMA_DST_FINAL_WRITE_ADDR       0x3C
#define ADMA_DST_DEBUG_SRC_SELECT       0x40
#define ADMA_DST_DEBUG_VALUE            0x44
#define ADMA_SRC_DEBUG_SRC_SELECT       0x48
#define ADMA_SRC_DEBUG_VALUE            0x4C
#define ADMA_FIFO_MSG_LEN               0x68

#define ADMA_BIT_WIDTH                  0x110
#define ADMA_SHA_MODE                   0x400
#define ADMA_SHA_INIT                   0x404
#define ADMA_SHA_STATUS                 0x408
#define ADMA_SHA_DATA                   0x40C
#define ADMA_SHA_DMA                    0x410
#define ADMA_SHA_PADDING_CFG            0x420
#define ADMA_SHA_PADDING_LEN            0x424
#define ADMA_SHA_PADDING_CTL            0x428

#define BE_8(p) ((p[0]<<24) | (p[1]<<16) | ( p[2]<<8) | (p[3]))	

typedef enum ADMA_ATTRIBUTE
{
    EN_ADMA_ATTRIBUTE_NOP   = 0x0,
    EN_ADMA_ATTRIBUTE_VALID = 0x1,
    EN_ADMA_ATTRIBUTE_END   = 0x2,
    EN_ADMA_ATTRIBUTE_INT   = 0x4,
    EN_ADMA_ATTRIBUTE_RSV   = 0x10,
    EN_ADMA_ATTRIBUTE_TRAN  = 0x20,
    EN_ADMA_ATTRIBUTE_LINK  = 0x30
}EN_ADMA_ATTRIBUTE;

typedef enum ADMA_INT_STAT
{
    EN_ADMA_SRC_EOF         =   (1<<0),
    EN_ADMA_SRC_STOP        =   (1<<1),
    EN_ADMA_SRC_DATA_ERR    =   (1<<2),
    EN_ADMA_DST_EOF         =   (1<<3),
    EN_ADMA_DST_STOP        =   (1<<4),
    EN_ADMA_DST_DATA_ERR    =   (1<<5),
    EN_ADMA_TIMEOUT         =   (1<<6),
    EN_ADMA_SRC_INVALID_DES =   (1<<18),
    EN_ADMA_DST_INVALID_DES =   (1<<19),
    EN_ADMA_FIFO_DST_EOF    =   (1<<30),   
    EN_ADMA_SHA_DONE        =   (1<<31)
}EN_ADMA_INT_STAT;

/* SHA interrupt setting */
#ifdef NT16M
#define ADMA_INT_SETTING        (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP|EN_ADMA_SRC_DATA_ERR| \
                                EN_ADMA_DST_EOF|EN_ADMA_DST_STOP|EN_ADMA_DST_DATA_ERR|  \
                                EN_ADMA_TIMEOUT|EN_ADMA_SRC_INVALID_DES|EN_ADMA_DST_INVALID_DES)
#define ADMA_INT_ERR            (EN_ADMA_SRC_DATA_ERR|EN_ADMA_DST_DATA_ERR|  \
                                EN_ADMA_SRC_INVALID_DES|EN_ADMA_DST_INVALID_DES)
#define ADMA_INT_DATA_DONE      (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP |\
                                EN_ADMA_DST_EOF|EN_ADMA_DST_STOP)
#else
#define ADMA_INT_SETTING        (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP  |\
                                EN_ADMA_TIMEOUT|EN_ADMA_SRC_INVALID_DES|\
                                EN_ADMA_DST_INVALID_DES|EN_ADMA_SHA_DONE)
#define ADMA_INT_ERR            (EN_ADMA_SRC_INVALID_DES|EN_ADMA_DST_INVALID_DES)
#define ADMA_INT_DATA_DONE      (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP |EN_ADMA_SHA_DONE)
#endif

#define ADMA_INT_TIMEOUT        (EN_ADMA_TIMEOUT)

#define ADMA_MAX_DECODE_LEN     (1<<15)

typedef struct adma_desc_s {
        unsigned short attribute;
        unsigned short len;                     /* decode length */
        unsigned int   addr;            /* buffer address(physical) */
} adma_desc_t;

struct sha_buf {
	void       *vaddr;
	dma_addr_t  paddr;
	size_t      size;
};
#define GZIP_ALIGNSIZE	64
#define GZIP_ALIGNADDR	8

#define GZIP_NR_PAGE	33
#define GZIP_PAGESIZE	4096

#define MAX_ADMA_NUM (256)
struct st_adma_tab
{
	adma_desc_t des[MAX_ADMA_NUM];;
	dma_addr_t des_phys;
};
struct nvt_adma_t
{
    struct completion wait;
    void __iomem *base;
    struct st_adma_tab *data_input;
    struct st_adma_tab *data_output;
	u32 data_input_cnt;	
    u32 data_output_cnt;		
    bool data_input_started;
    volatile u32 ISRStat;
    volatile u32 ISRErr;
};
struct nvt_auth_t
{
	struct device *dev;
	struct semaphore sema;
    struct nvt_adma_t shadma;
	struct sha_buf pad_buf;
    struct sha_buf dst_buf;
    u32 data_tail_len; 
    u32 req_idx;
    struct nvt_unzip_auth_t* auth;
    bool hwpadding;
    bool bypass;
    unsigned long long clock_ns;
};

struct nvt_auth_debug_t {
	unsigned int last_intr_status;
	void *input_descriptor_list;
	void *output_descriptor_list;
	unsigned int input_size;
	int data_input_cnt;
    int data_output_cnt;
};

static struct nvt_auth_debug_t nvt_auth_sha_dbg = {
	.last_intr_status = 0,
	.input_descriptor_list = NULL,
	.output_descriptor_list = NULL,
	.input_size = 0,
	.data_input_cnt = 0,
    .data_output_cnt = 0,
};

static u64 sha_dmamask = DMA_BIT_MASK(32);
static struct nvt_auth_t *nvt_auth = NULL;
static unsigned long long nvt_auth_calls=0;
static unsigned long long nvt_auth_errors=0;
static unsigned long long nvt_auth_nsecs=0;
static unsigned int       nvt_auth_quiet=0;

static unsigned long long nvt_auth_get_nsecs(void)
{
#ifdef CONFIG_NVT_HW_CLOCK 
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	struct timespec ts;
	getrawmonotonic(&ts);
	return ((unsigned long long)((unsigned long long)ts.tv_sec*1000000000 +ts.tv_nsec));
#endif
}

static int sha_prepare_dst_adma(unsigned int len, struct st_adma_tab *table,
                            u32* tabcnt, bool padding)
{
    int err=0;
    dma_addr_t des_phys = 0; 
	unsigned int calc_len = 0;
	int unit_num = 0;   
    bool last_unit = false;
	adma_desc_t * des = NULL;
    TRACE_ENTRY();

    des_phys = dma_map_single(nvt_auth->dev, nvt_auth->dst_buf.vaddr,
		nvt_auth->dst_buf.size, DMA_FROM_DEVICE);
	if (dma_mapping_error(nvt_auth->dev, des_phys)) {
		dev_err(nvt_auth->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}
    nvt_auth->dst_buf.paddr = des_phys;

	while( (calc_len < len) ){
		last_unit = false;
		des = &table->des[*tabcnt];
		BUG_ON(!des);
		if( (len <= ADMA_MAX_DECODE_LEN)  || calc_len+ADMA_MAX_DECODE_LEN>=len)
			last_unit = true;
		des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN ;
		des->len = (last_unit) ? len - calc_len : ADMA_MAX_DECODE_LEN;
		des->addr = nvt_auth->dst_buf.paddr ;

        if(last_unit && !padding)
                des->attribute |= EN_ADMA_ATTRIBUTE_END | EN_ADMA_ATTRIBUTE_INT;
#ifdef DBG_DESCRIPTOR
        dev_info(nvt_auth->dev, ">>>NVT_SHA : output des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				*tabcnt , des->len, des->addr, des->attribute);
#endif
		unit_num++;
		calc_len += ADMA_MAX_DECODE_LEN;	
		*tabcnt = *tabcnt+1;	
	}

    if(padding) {
        //last padding data
        des = &table->des[*tabcnt];
        BUG_ON(!des);
        des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN |EN_ADMA_ATTRIBUTE_END|EN_ADMA_ATTRIBUTE_INT;
        des->len = nvt_auth->data_tail_len;
        des->addr = nvt_auth->dst_buf.paddr;
        *tabcnt = *tabcnt+1;
    #ifdef DBG_DESCRIPTOR
        dev_info(nvt_auth->dev, ">>>NVT_SHA : output des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				*tabcnt , des->len, des->addr, des->attribute);
    #endif
    }
    des_phys = dma_map_single(nvt_auth->dev, table->des,
		sizeof(adma_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
	if (dma_mapping_error(nvt_auth->dev, des_phys)) {
		dev_err(nvt_auth->dev, "unable to map output buffer\n");
		err = (-EINVAL);
	}
    table->des_phys = des_phys;
    #ifdef DBG_DESCRIPTOR
    dev_info(nvt_auth->dev,"output descriptor vaddr %p, paddr %x\n", 
            table->des, table->des_phys);
    #endif
    return err;
}

static void finish_src_adma(struct st_adma_tab *table, bool padding)
{
	if (table->des_phys) {
		dma_unmap_single(nvt_auth->dev, table->des_phys,
		sizeof(adma_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
        table->des_phys=0;
	}
    if (nvt_auth->pad_buf.paddr && padding) {
		dma_unmap_single(nvt_auth->dev,
		nvt_auth->pad_buf.paddr,
		nvt_auth->pad_buf.size, DMA_TO_DEVICE);
        nvt_auth->pad_buf.paddr=0;
	}
}

static void finish_dst_adma(struct st_adma_tab *table)
{
    if (table->des_phys) {
		dma_unmap_single(nvt_auth->dev, table->des_phys,
		sizeof(adma_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
        table->des_phys=0;
    }
}

static void sha_upmap_dst_buf(void)
{
    if(nvt_auth->dst_buf.paddr) {
        dma_unmap_single(nvt_auth->dev,
			nvt_auth->dst_buf.paddr,
		nvt_auth->dst_buf.size, DMA_FROM_DEVICE);
        nvt_auth->dst_buf.paddr=0;
    }
}

static int prepare_adma_table(dma_addr_t* pages,
                            int nr_pages, 
                            unsigned int sz,
                            struct st_adma_tab *table,
                            u32* tbcnt,
                            bool padding,
                            u32 offset)
{
	dma_addr_t des_phys = 0;
	int err = 0;
	adma_desc_t *des = NULL;
	int i = 0;
    bool last_unit = false;
    unsigned int calc_len = 0;  
    unsigned int len = sz;
	unsigned int addr = pages[0];
    TRACE_ENTRY();

	if (nr_pages == 1) {
		/*ss special case............................*/		
		while ((calc_len < len)) {
		    last_unit = false;
			des = &table->des[*tbcnt];
			BUG_ON(!des);

			if ((len <= ADMA_MAX_DECODE_LEN) || (calc_len+ADMA_MAX_DECODE_LEN >= len))
				last_unit = true;

			des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN;
			des->len = (last_unit) ? len - calc_len : ADMA_MAX_DECODE_LEN;
			des->addr = addr + calc_len;

            if(last_unit && !padding)
                des->attribute |= EN_ADMA_ATTRIBUTE_END | EN_ADMA_ATTRIBUTE_INT;
#ifdef DBG_DESCRIPTOR
			dev_info(nvt_auth->dev,
                    ">>>NVT_SHA: input des: [%2d]len: 0x%x, addr 0x%08x, attr: 0x%08x",
					*tbcnt,
					des->len, des->addr, des->attribute);
#endif
			calc_len += ADMA_MAX_DECODE_LEN;
			*tbcnt = *tbcnt+1;
		}
	} else {
        calc_len=0;
        last_unit = false;
		for (i = 0; i < nr_pages; i++) {
			des = &table->des[*tbcnt];
			BUG_ON(!des);

			des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN;
			if (i == (nr_pages-1))
			    last_unit = true;

            des->len = (last_unit) ? sz-calc_len : PAGE_SIZE;        
			des->addr = pages[i];

            if(last_unit && !padding)
                des->attribute |= EN_ADMA_ATTRIBUTE_END | EN_ADMA_ATTRIBUTE_INT;

#ifdef DBG_DESCRIPTOR
			dev_info(nvt_auth->dev,
				">>>NVT_SHA : input des: [%2d]len: 0x%x, addr 0x%08x, attr: 0x%08x",
				*tbcnt, des->len, des->addr, des->attribute);
			
#endif
            calc_len += PAGE_SIZE;
            *tbcnt = *tbcnt+1;
		}
	}
    
    if(padding) {

        nvt_auth->pad_buf.paddr = dma_map_single(nvt_auth->dev, nvt_auth->pad_buf.vaddr, nvt_auth->pad_buf.size, DMA_TO_DEVICE);
        if (dma_mapping_error(nvt_auth->dev, nvt_auth->pad_buf.paddr)) {
            dev_err(nvt_auth->dev, "unable to map padding buffer\n");
            err = (-EINVAL);
        }
        

        des = &table->des[*tbcnt];
        des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN|EN_ADMA_ATTRIBUTE_END|EN_ADMA_ATTRIBUTE_INT;
        des->len = nvt_auth->data_tail_len; 
        des->addr = nvt_auth->pad_buf.paddr;
        *tbcnt = *tbcnt+1;
#ifdef DBG_DESCRIPTOR
        dev_info(nvt_auth->dev,
                    ">>>NVT_SHA : input des: [%2d]len: 0x%x, addr 0x%08x",
                    *tbcnt, des->len, des->addr);        
#endif
        
    }

	des_phys = dma_map_single(nvt_auth->dev, table->des,
		sizeof(adma_desc_t) * MAX_ADMA_NUM, DMA_TO_DEVICE);
	if (dma_mapping_error(nvt_auth->dev, des_phys)) {
		dev_err(nvt_auth->dev, "unable to map input buffer\n");
		err = (-EINVAL);
	}
    
	table->des_phys = des_phys;
#ifdef DBG_DESCRIPTOR
    dev_info(nvt_auth->dev,"input descriptor vaddr %p, paddr %x\n", 
            table->des, table->des_phys);
#endif

	return err;
}

static void nvt_auth_dump_reg(void)
{ 
    #define SHA_REG_INFO(REG)					\
	dev_err(nvt_auth->dev, "	%-30s : 0x%08x\n",	\
		#REG, readl(nvt_auth->shadma.base + REG))

	dev_err(nvt_auth->dev,
		"-------------DUMP SHA registers------------\n");
    SHA_REG_INFO(ADMA_HW_SETTING);
    SHA_REG_INFO(ADMA_SRC_DESCRIPTOR_ADDR);
    SHA_REG_INFO(ADMA_SRC_CUR_DESCRIPTOR_ADDR);
    SHA_REG_INFO(ADMA_SRC_STATUS);
    SHA_REG_INFO(ADMA_DST_DESCRIPTOR_ADDR);
    SHA_REG_INFO(ADMA_DST_CUR_DESCRIPTOR_ADDR);
    SHA_REG_INFO(ADMA_DST_STATUS);
    SHA_REG_INFO(ADMA_HW_CONFIGURE);    
    SHA_REG_INFO(ADMA_SRC_BYTE_COUNT);
    SHA_REG_INFO(ADMA_DST_BYTE_COUNT);
    SHA_REG_INFO(ADMA_TIMEOUT);
    SHA_REG_INFO(ADMA_INT_ENABLE);
    SHA_REG_INFO(ADMA_INT_STATUS_AND_CLR);
    SHA_REG_INFO(ADMA_SRC_FINAL_READ_ADDR);
    SHA_REG_INFO(ADMA_DST_FINAL_WRITE_ADDR);
#ifndef NT16M
    SHA_REG_INFO(ADMA_FIFO_MSG_LEN);
#endif
    SHA_REG_INFO(ADMA_BIT_WIDTH);
    SHA_REG_INFO(ADMA_SHA_MODE);
    SHA_REG_INFO(ADMA_SHA_INIT);
    SHA_REG_INFO(ADMA_SHA_STATUS);
    SHA_REG_INFO(ADMA_SHA_DATA);
    SHA_REG_INFO(ADMA_SHA_DMA);
#ifndef NT16M
    SHA_REG_INFO(ADMA_SHA_PADDING_CFG);
    SHA_REG_INFO(ADMA_SHA_PADDING_LEN);
    SHA_REG_INFO(ADMA_SHA_PADDING_CTL);
#endif
    dev_err(nvt_auth->dev,
		"-------------------------------------------\n");
}

static void nvt_auth_dump_descriptor(void)
{
	int i = 0;
    struct st_adma_tab *data_input;
    struct st_adma_tab *data_output;
    
    dev_err(nvt_auth->dev,
		"---------NVT AUTH DUMP SHA ADMA Descriptor-----------\n");
    data_input = nvt_auth->shadma.data_input;
    data_output = nvt_auth->shadma.data_output;
                        
    finish_src_adma(nvt_auth->shadma.data_input, !nvt_auth->hwpadding);
	dev_err(nvt_auth->dev,
		" SRC descriptor[%d]:\n", nvt_auth->shadma.data_input_cnt);

	for (i = 0; i < nvt_auth->shadma.data_input_cnt; i++) {
		dev_err(nvt_auth->dev,
		"[%03d] attr: 0x%x length: 0x%8x addr :0x%08x\n"
			, i,
			data_input->des[i].attribute, data_input->des[i].len,
			data_input->des[i].addr);
	}

	finish_dst_adma(nvt_auth->shadma.data_output);    	
	dev_err(nvt_auth->dev,
            " DST descriptor[%d]:\n", nvt_auth->shadma.data_output_cnt);
	for (i = 0; i < nvt_auth->shadma.data_output_cnt; i++)
		dev_err(nvt_auth->dev,
				"[%03d] attr: %8x length: 0x%8x addr :0x%08x\n",
				i, data_output->des[i].attribute,
				data_output->des[i].len,
				data_output->des[i].addr);
	dev_err(nvt_auth->dev,
		"--------------------------------------------\n");
}

static void debug_nvt_auth_status(void)
{
	if (nvt_auth == NULL)
		return;

	dev_err(nvt_auth->dev,
		"====dump nvt_auth data struct====\n");
    dev_err(nvt_auth->dev, "sha base: 0x%x\n",
            (unsigned int)nvt_auth->shadma.base);
    dev_err(nvt_auth->dev, "sha start:0x%x\n",
            nvt_auth->shadma.data_input_started);
    dev_err(nvt_auth->dev, "sha isr done: 0x%x\n",
            nvt_auth->shadma.ISRStat);
    dev_err(nvt_auth->dev, "sha isr err: 0x%x\n",
            nvt_auth->shadma.ISRErr);
    dev_err(nvt_auth->dev, "sha padding buf: 0x%x\n",
            (unsigned int)nvt_auth->pad_buf.vaddr);
    dev_err(nvt_auth->dev, "sha dst buf: 0x%x\n",
            (unsigned int)nvt_auth->dst_buf.vaddr);
    dev_err(nvt_auth->dev, "req idx: 0x%x\n",
            nvt_auth->req_idx);
    dev_err(nvt_auth->dev, "hwpadding 0x%x\n",
            nvt_auth->hwpadding);
	dev_err(nvt_auth->dev, "===================================\n");
}

static void debug_nvt_auth_dbg_status(void)
{
	dev_err(nvt_auth->dev, "====dump nvt_auth_sha_dbg data struct====\n");
	dev_err(nvt_auth->dev, "last_intr_status : 0x%x\n",
		nvt_auth_sha_dbg.last_intr_status);
	dev_err(nvt_auth->dev, "input_descriptor_list : 0x%x\n",
		(unsigned int)nvt_auth_sha_dbg.input_descriptor_list);
	dev_err(nvt_auth->dev, "output_descriptor_list : 0x%x\n",
		(unsigned int)nvt_auth_sha_dbg.output_descriptor_list);
	dev_err(nvt_auth->dev, "input_size : 0x%x\n",
		(unsigned int)nvt_auth_sha_dbg.input_size);
	dev_err(nvt_auth->dev, "data_input_cnt : 0x%x\n",
		(unsigned int)nvt_auth_sha_dbg.data_input_cnt);
	dev_err(nvt_auth->dev, "data_output_cnt : 0x%x\n",
		(unsigned int)nvt_auth_sha_dbg.data_output_cnt);
	dev_err(nvt_auth->dev, "===================================\n");

}

static void nvt_auth_dump(void)
{
    nvt_auth_dump_reg();
	nvt_auth_dump_descriptor();
	debug_nvt_auth_status();
	debug_nvt_auth_dbg_status();
}

static irqreturn_t nvt_auth_shaisr(int irq, void* devId)
{
    u32 val=0;
    u32 datadone=0;
    u32 errmask=0;
    u32 donemask=0;
    u32 tomask=0;
    val = readl(nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);
    nvt_auth_calls++;
    TRACE_ENTRY();
#ifdef NVT_SHA_DEBUG    
    dev_info(nvt_auth->dev,"sha interrupt: %x\n", val);
#endif
    
    errmask=ADMA_INT_ERR;
    donemask=ADMA_INT_DATA_DONE;
    tomask=ADMA_INT_TIMEOUT;

    if(val&errmask)
    {
        nvt_auth_errors++;
        nvt_auth->shadma.ISRErr = val&errmask;
        writel(nvt_auth->shadma.ISRErr, nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);    
        complete(&nvt_auth->shadma.wait);

        if (1 || !nvt_auth_quiet) {
			pr_err("nvt_auth: sha error interrupt happened\n");
			nvt_auth_dump();
			BUG_ON(val);
        }
    }
    if(val&donemask)
    {
        datadone = val&donemask;
        nvt_auth->shadma.ISRStat |= datadone;
        writel(datadone, nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);
        if(nvt_auth->shadma.ISRStat == donemask)
        {
            //nvt_auth->shadma.ISRStat=0;
            nvt_auth_nsecs += nvt_auth_get_nsecs() - nvt_auth->clock_ns;
            complete(&nvt_auth->shadma.wait);
        }
        val&=~datadone;
    }
    if(val&tomask)
    {
        //timeout
        writel(tomask, nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);    
        nvt_auth->shadma.ISRErr = 0xFFFFFFFF;    
        complete(&nvt_auth->shadma.wait);
    }
    nvt_auth_sha_dbg.last_intr_status = val;
    nvt_auth_calls += 1;
    
    return IRQ_HANDLED;
}

static void sha256_reset(void)
{
    TRACE_ENTRY();   
    writel(0x70,            nvt_auth->shadma.base+ADMA_HW_SETTING);
    writel(-1, nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);    
    writel(0, nvt_auth->shadma.base+ADMA_INT_ENABLE);
}
static void sha256_init(void)
{  
    TRACE_ENTRY();   
    reinit_completion(&nvt_auth->shadma.wait); 
    nvt_auth->shadma.data_input_cnt = 0;
    nvt_auth->shadma.data_output_cnt = 0;
    nvt_auth->data_tail_len = 0;
    nvt_auth->shadma.data_input_started=0;
    nvt_auth->shadma.ISRStat=0;
    nvt_auth->shadma.ISRErr=0;
#ifdef NT16M
    nvt_auth->hwpadding=0;
#else
    nvt_auth->hwpadding=1;
#endif
    writel(0x10,            nvt_auth->shadma.base+ADMA_HW_SETTING);
    writel(-1, nvt_auth->shadma.base+ADMA_FIFO_MSG_LEN);    
    writel(-1, nvt_auth->shadma.base+ADMA_INT_STATUS_AND_CLR);    
    writel(ADMA_INT_SETTING, nvt_auth->shadma.base+ADMA_INT_ENABLE);
    writel(0x1,             nvt_auth->shadma.base+ADMA_SHA_MODE);
    writel(0x40,            nvt_auth->shadma.base+ADMA_BIT_WIDTH);
    writel(0x1,             nvt_auth->shadma.base+ADMA_SHA_DMA);
    writel(0x1,             nvt_auth->shadma.base+ADMA_SHA_INIT);
}
static int sha256_transdata(dma_addr_t *ipages, int nr_ipages, unsigned int input_bytes, u32 offset)
{
    int err;
    __be64 bits;
    static const u8 padding[64] = { 0x80, };
    TRACE_ENTRY();   

    if(!nvt_auth->hwpadding)
    {
	    bits = cpu_to_be64(input_bytes << 3);
        memcpy(((u8*)nvt_auth->pad_buf.vaddr), padding, 56);
        memcpy(((u8*)nvt_auth->pad_buf.vaddr)+56, (void*)&bits, sizeof(bits));
        nvt_auth->data_tail_len = 64;
    }
    
    writel(0x60,    nvt_auth->shadma.base+ADMA_HW_SETTING);
    writel(-1,    nvt_auth->shadma.base+ADMA_TIMEOUT);
    //sha_prepare_src_adma(ipages, nr_ipages, input_bytes);
   
    err = prepare_adma_table(ipages, nr_ipages, input_bytes, nvt_auth->shadma.data_input, 
                        &nvt_auth->shadma.data_input_cnt, !nvt_auth->hwpadding, offset);
    if(err<0)
        return err;

    writel(nvt_auth->shadma.data_input->des_phys, nvt_auth->shadma.base + ADMA_SRC_DESCRIPTOR_ADDR);

    err = sha_prepare_dst_adma(input_bytes, nvt_auth->shadma.data_output, 
                        &nvt_auth->shadma.data_output_cnt, !nvt_auth->hwpadding);
    if(err<0)
        return err;


    writel(nvt_auth->shadma.data_output->des_phys, nvt_auth->shadma.base + ADMA_DST_DESCRIPTOR_ADDR);

    writel(0x0,     nvt_auth->shadma.base+ADMA_HW_CONFIGURE);

    if(nvt_auth->hwpadding)
    {
        writel((64<<8)|3, nvt_auth->shadma.base+ADMA_SHA_PADDING_CFG);
        writel(input_bytes, nvt_auth->shadma.base+ADMA_SHA_PADDING_LEN);
        writel(1, nvt_auth->shadma.base+ADMA_SHA_PADDING_CTL);
    }
    else
    {
        writel(0x0, nvt_auth->shadma.base+ADMA_SHA_PADDING_CFG);
        writel(0x0, nvt_auth->shadma.base+ADMA_SHA_PADDING_LEN);
    }

    nvt_auth_sha_dbg.input_descriptor_list = nvt_auth->shadma.data_input->des;
    nvt_auth_sha_dbg.output_descriptor_list = nvt_auth->shadma.data_output->des;
    nvt_auth_sha_dbg.data_input_cnt = nvt_auth->shadma.data_input_cnt;
    nvt_auth_sha_dbg.data_output_cnt = nvt_auth->shadma.data_output_cnt;
    nvt_auth_sha_dbg.input_size = input_bytes;
    
    return 0;
}
static void sha256_trigger(void)
{
    nvt_auth->shadma.data_input_started = 1;
#ifdef NT16M
    writel(0x5,     nvt_auth->shadma.base+ADMA_HW_SETTING);
#else
    writel(0x1,     nvt_auth->shadma.base+ADMA_HW_SETTING);
#endif
    nvt_auth->clock_ns = nvt_auth_get_nsecs();
}

static int sha256_wait(void)
{    
    wait_for_completion(&nvt_auth->shadma.wait);
    if(nvt_auth->shadma.ISRErr)
    {
        dev_err(nvt_auth->dev, "SHA ISRStat %x, ISRErr %x\n", 
                nvt_auth->shadma.ISRStat, nvt_auth->shadma.ISRErr);
        //up(&nvt_auth->sema);
        return (-EINVAL);
    }
    finish_src_adma(nvt_auth->shadma.data_input, !nvt_auth->hwpadding);
    finish_dst_adma(nvt_auth->shadma.data_output);    
    sha_upmap_dst_buf();
    
    return 0;    
}

static void sha256_digest(u32 digest[8])
{
    int i=0;
    writel(0x2, nvt_auth->shadma.base+ADMA_SHA_INIT); 
    for (i = 0; i < 8; i++)
    {
        digest[i] = cpu_to_be32(readl(nvt_auth->shadma.base+ADMA_SHA_DATA));
        //dev_err(nvt_auth->dev,"hash 0x%08x\n", digest[i]);       
    }
}

int hw_auth_start(dma_addr_t *ipages, int nr_ipages, unsigned int input_bytes, struct nvt_unzip_auth_t *auth, u32 offset)
{
    int i=0;
    int ret=0;
    BUG_ON(!nvt_auth);
    BUG_ON(!nvt_auth->shadma.data_input);
    BUG_ON(!nvt_auth->shadma.data_output);
    BUG_ON(!nvt_auth->pad_buf.vaddr);
    BUG_ON(!nvt_auth->dst_buf.vaddr);
    if(!auth) {
        dev_err(nvt_auth->dev, "req#%u Invalid auth data", nvt_auth->req_idx);
        return -EINVAL;
    }

    if (!(nr_ipages > 0 && nr_ipages <= GZIP_NR_PAGE)) {
		dev_err(nvt_auth->dev, "req#%u Invalid Input page number %d",
			nvt_auth->req_idx, nr_ipages);
		return -EINVAL;
	}

	/* check input align*/
	if (input_bytes&(GZIP_ALIGNSIZE-1)) {
		dev_err(nvt_auth->dev, "req#%u Invalid Input align bytes 0x%x",
			nvt_auth->req_idx, input_bytes);
		return -EINVAL;
	}

#ifndef NVT_AUTH_INPUT_ISGL_NON_ALIGN
	for (i = 0; i < nr_ipages; i++) {
		if (ipages[i]&
			((nr_ipages == 1 ? GZIP_ALIGNADDR-1:GZIP_PAGESIZE-1))) {
			dev_err(nvt_auth->dev,
				"req#%u Invalid Input align address[%d] 0x%llx",
				nvt_auth->req_idx, i, (u64)ipages[i]);
			return -EINVAL;
		}
	}
#endif
#ifdef NVT_SHA_DEBUG
    dev_err(nvt_auth->dev, "isgl offset %x\n", offset);
    dev_err(nvt_auth->dev,
	    "req#%u, number of pages %d, 0x%x bytes",
	    nvt_auth->req_idx, nr_ipages, input_bytes);
    for (i = 0; i < nr_ipages; i++) {
	    dev_err(nvt_auth->dev, "page%d input [0x%08llx]", i, (u64)ipages[i]);
    }
#endif
    nvt_auth->auth = auth;
    auth->sha256_result = GZIP_ERR_HASH_INPROGRESS;

    //down(&nvt_auth->sema); 
    sha256_init();
    ret = sha256_transdata(ipages, nr_ipages, input_bytes, offset);
      
    nvt_auth->req_idx++;
    return ret;
}
int hw_auth_wait(void)
{
    u32 dst[8];
    struct nvt_unzip_auth_t *auth;
    BUG_ON(!nvt_auth);
    TRACE_ENTRY();   

    auth = nvt_auth->auth;
    sha256_wait();
    
    if(nvt_auth->shadma.ISRErr)
    {
        auth->sha256_result = GZIP_ERR_HASH_TIMEOUT;
        //up(&nvt_auth->sema);
        return (-EINVAL);
    }
    
    if(auth->sha256_digest_out) {/* only use SHA256 hw cal value, not use match result */
        sha256_digest(auth->sha256_digest_out) ;
	}
    if(auth->sha256_digest) {
        sha256_digest(dst);
        if(memcmp(auth->sha256_digest, dst, 32))
            auth->sha256_result = GZIP_ERR_HASH_MISSMATCH;
        else
            auth->sha256_result = GZIP_HASH_OK;
	
    }
    else if(auth->sha256_digest_out) 
    {
        auth->sha256_result = GZIP_HASH_OK;
    }
    nvt_auth->auth=NULL;
   
    sha256_reset(); 
    //up(&nvt_auth->sema);
    return 0;
}
void hw_auth_update_endpointer(void)
{
    TRACE_ENTRY();   
    sha256_trigger();
}

EXPORT_SYMBOL(hw_auth_start);
EXPORT_SYMBOL(hw_auth_wait);
EXPORT_SYMBOL(hw_auth_update_endpointer);

static int nvt_auth_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int shairq;
#ifdef CONFIG_OF	
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif
	nvt_auth = devm_kzalloc(dev, sizeof(struct nvt_auth_t), GFP_KERNEL);
	if(nvt_auth == NULL)
	{
		dev_err(dev, "cannot allocate memory!!!\n");
		return -ENOMEM;
	}
	sema_init(&nvt_auth->sema, 1);
	init_completion(&nvt_auth->shadma.wait);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, nvt_auth);
		return -ENODEV;
	}
    //pr_err("SHA base %x\n", res->start);
	nvt_auth->shadma.base = devm_ioremap_resource(&pdev->dev, res);
	if (nvt_auth->shadma.base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, nvt_auth);
        return -ENODEV;
	}

    res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, nvt_auth);
		return -ENODEV;
	}

	shairq = platform_get_irq(pdev, 0);
	if (shairq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, nvt_auth);
		return -ENODEV;
	}

	ret = request_irq(shairq , nvt_auth_shaisr, 0, dev_name(dev), nvt_auth);
	if (ret != 0) {
		dev_err(dev, "cannot request SHA IRQ %d\n", shairq);
		devm_kfree(dev, nvt_auth);
		return -ENODEV;
	}

	dev->dma_mask = &sha_dmamask;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	platform_set_drvdata(pdev, (void *) nvt_auth);

    nvt_auth->shadma.data_input = devm_kzalloc(dev,
		sizeof(struct st_adma_tab), GFP_KERNEL);
	if (nvt_auth->shadma.data_input == NULL) {
		dev_err(dev, "cannot allocate SHA input description buffer\n");
		goto MEMFAIL; 
	}
	nvt_auth->shadma.data_output = devm_kzalloc(dev,
            sizeof(struct st_adma_tab), GFP_KERNEL);
	if (nvt_auth->shadma.data_output == NULL) {
		dev_err(dev, "can't allocate SHA output description buffer\n");
		goto MEMFAIL; 
	}

    nvt_auth->pad_buf.size = 64;
    nvt_auth->pad_buf.vaddr = devm_kzalloc(dev, nvt_auth->pad_buf.size, GFP_KERNEL);
	if (nvt_auth->pad_buf.vaddr == NULL) {
		dev_err(dev, "can't allocate sha padding buffer\n");
	    goto MEMFAIL; 
	}

    nvt_auth->dst_buf.size = ADMA_MAX_DECODE_LEN;
    nvt_auth->dst_buf.vaddr = devm_kzalloc(dev, nvt_auth->dst_buf.size , GFP_KERNEL);
	if (nvt_auth->dst_buf.vaddr == NULL) {
		dev_err(dev, "can't allocate sha output buffer\n");
        goto MEMFAIL; 
	}

    init_log();
	init_timedifflog();

	/* set platform  device information*/
	nvt_auth->dev = dev;
	dev_info(dev, 
            "Registered Novatek hw auth driver(%p), \
            sha DBG: %p,\
            sha: %p, ", 
            nvt_auth, &nvt_auth_sha_dbg, 
            nvt_auth->shadma.base);
   
	return 0;
MEMFAIL:
    if(nvt_auth->shadma.data_input == NULL)
        devm_kfree(dev, nvt_auth->shadma.data_input);
    if(nvt_auth->shadma.data_output == NULL)
        devm_kfree(dev, nvt_auth->shadma.data_output);
    if (nvt_auth->pad_buf.vaddr == NULL) 
        devm_kfree(dev, nvt_auth->pad_buf.vaddr);
    if (nvt_auth->dst_buf.vaddr == NULL) 
        devm_kfree(dev, nvt_auth->dst_buf.vaddr);
    if(!nvt_auth)
		devm_kfree(dev, nvt_auth);
    return -ENOMEM;
}

static int nvt_auth_remove(struct platform_device *pdev)
{ 
    devm_kfree(&pdev->dev, nvt_auth->shadma.data_input);
    devm_kfree(&pdev->dev, nvt_auth->shadma.data_output);
    devm_kfree(&pdev->dev, nvt_auth->pad_buf.vaddr);
    devm_kfree(&pdev->dev, nvt_auth->dst_buf.vaddr);
	devm_kfree(&pdev->dev, nvt_auth);
	return 0;
}

static const struct of_device_id nvt_auth_dt_match[] = {
	{ .compatible = "nvt,auth", },
	{},
};

MODULE_DEVICE_TABLE(of, nvt_auth_dt_match);
static struct platform_driver nvt_auth_driver = {
	.probe		= nvt_auth_probe,
	.remove		= nvt_auth_remove,
	.driver = {
		.name	= "nvt,auth",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(nvt_auth_dt_match),
	},
};

static int nvt_auth_init(void)
{
	return platform_driver_register(&nvt_auth_driver);
}
subsys_initcall(nvt_auth_init);

static void __exit nvt_auth_exit(void)
{
	platform_driver_unregister(&nvt_auth_driver);
}
module_exit(nvt_auth_exit);

#ifndef CONFIG_VD_RELEASE
static int sw_sha256(unsigned char *buf, unsigned int buf_len, unsigned char *hash)
{
	int ret = 0;
	struct crypto_shash *sha256;
    struct shash_desc *shash;
	sha256 = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(sha256))
		return PTR_ERR(sha256);
    shash = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(sha256),
		       GFP_NOFS);
	if (!shash)
    {
		crypto_free_shash(sha256);
		return -ENOMEM;
    }
	shash->tfm = sha256;
	shash->flags = 0x0;
	ret = crypto_shash_init(shash);
	if (ret)
		goto exit;
	ret = crypto_shash_update(shash, buf, buf_len);
	if (ret)
		goto exit;
	ret = crypto_shash_final(shash, hash);
exit:
	crypto_free_shash(sha256);
    kfree(shash);
	return ret;

}

static int nvt_auth_sha_sgl(struct scatterlist *isg, unsigned int input_nents, int input_bytes, struct nvt_unzip_auth_t *auth)
{
    int i=0;
    struct scatterlist *sg = NULL;
    dma_addr_t input_page_phys[16];

    for_each_sg(isg, sg, input_nents, i) {

        if ((sg_phys(sg)&~PAGE_MASK) || ((i < input_nents-1)
				&& (sg->length != PAGE_SIZE))) {
				dev_err(nvt_auth->dev,
				"Not Support sglist(not page aligned)\n");
				return  -EINVAL;;
			}
        input_page_phys[i] =sg_phys(sg);
        dev_err(nvt_auth->dev, "page %d, paddr %x, vaddr %p, size %d"
            ,i, input_page_phys[i], sg_virt(sg), sg->length);

    }

    hw_auth_start(input_page_phys, input_nents, input_bytes, auth, 0);
    hw_auth_update_endpointer();
    hw_auth_wait();

    for(i=0 ;i<8; i++) {
        dev_err(nvt_auth->dev, "digest 0x%08x, ", auth->sha256_digest_out[i]);
    }

    if(auth->sha256_result==GZIP_ERR_HASH_MISSMATCH)
        dev_err(nvt_auth->dev, "hash digest compare fail");
    else if(auth->sha256_result==GZIP_HASH_OK)
        dev_err(nvt_auth->dev, "hash digest compare pass");
    else if(auth->sha256_result==GZIP_ERR_HASH_TIMEOUT)
        dev_err(nvt_auth->dev, "hash digest compare timeout");



    return 0;
}

static void bufset(u8* buf, u32 sz)
{
    int i=0;
    for(i=0; i<sz; i++)
    {
        buf[i] = i&0xff;
    }   
}

static int nvt_auth_shatest(void)
{
    #define MAX_IBUFF_SZ    (16 << PAGE_SHIFT)
    int ret=0;
    int i=0;
    int bufsize = 0;
    u32 hash_buf[8];
    u32 hash_buf_expec[8];
    struct scatterlist isg[16];
    unsigned int input_nents = 0;
    struct scatterlist *sg = NULL;
    void* in_buff=0;
    struct nvt_unzip_auth_t auth;
    TRACE_ENTRY();   

    in_buff = (void *)__get_free_pages(GFP_KERNEL,
						get_order(MAX_IBUFF_SZ));
	if (!in_buff) {
        dev_err(nvt_auth->dev, "alocate input bufferr fail\n");
		goto err;
    }
   
	for(bufsize=PAGE_SIZE; bufsize<MAX_IBUFF_SZ; bufsize+=PAGE_SIZE)
    {
        auth.sha256_digest = hash_buf_expec;
        auth.sha256_digest_out = hash_buf;

        get_random_bytes(in_buff, bufsize);

        sw_sha256(in_buff, bufsize, (u8*)auth.sha256_digest);

        for(i=0 ;i<8; i++) {
            dev_err(nvt_auth->dev, "target digest 0x%08x, ", auth.sha256_digest[i]);
        }
    //#1
    
        sg_init_one(isg, in_buff, bufsize);

        input_nents = sg_nents(isg);

        ret = dma_map_sg(nvt_auth->dev, isg, input_nents, DMA_TO_DEVICE);
        if (ret == 0) {
            dev_err(nvt_auth->dev, "unable to map input bufferr\n");
            goto err;
        }

        dev_err(nvt_auth->dev, "in_buff %p, size %d ,nunber of pages %d"
                , in_buff, bufsize, input_nents);

        nvt_auth_sha_sgl(isg, input_nents, bufsize, &auth);
        
        dma_unmap_sg(nvt_auth->dev, isg, input_nents, DMA_TO_DEVICE);

    //#2    
        sg_init_table(isg, bufsize/PAGE_SIZE);
        input_nents = sg_nents(isg);
        for_each_sg(isg, sg, input_nents, i) {
            sg_set_buf(&isg[i], ((u8*)in_buff)+i*PAGE_SIZE, PAGE_SIZE);
            dev_err(nvt_auth->dev, "[cheek in sg ] vaddr %p phys : %x\n",
                sg_virt(&isg[i]), sg_phys(&isg[i]));
        }

        

        ret = dma_map_sg(nvt_auth->dev, isg, input_nents, DMA_TO_DEVICE);
        if (ret == 0) {
            dev_err(nvt_auth->dev, "unable to map input bufferr\n");
            goto err;
        }

        dev_err(nvt_auth->dev, "in_buff %p, size %d ,nunber of pages %d"
                , in_buff, bufsize, input_nents);

        nvt_auth_sha_sgl(isg, input_nents, bufsize, &auth);
        

        dma_unmap_sg(nvt_auth->dev, isg, input_nents, DMA_TO_DEVICE);

    }

    free_pages((unsigned long)(in_buff), get_order(MAX_IBUFF_SZ));
    return 0;

err:
    if(in_buff)
        free_pages((unsigned long)(in_buff), get_order(MAX_IBUFF_SZ));
    return  -EINVAL;
}
static struct dentry *nvt_auth_debugfs;
static void nvt_auth_debugfs_create(void)
{
	nvt_auth_debugfs = debugfs_create_dir("nvt_auth", NULL);
	if (!IS_ERR_OR_NULL(nvt_auth_debugfs)) {
		debugfs_create_u64("calls",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_auth_debugfs, &nvt_auth_calls);
		debugfs_create_u64("errors",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_auth_debugfs, &nvt_auth_errors);
		debugfs_create_u64("nsecs",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_auth_debugfs, &nvt_auth_nsecs);
		debugfs_create_bool("quiet",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    nvt_auth_debugfs, &nvt_auth_quiet);
	}
}
static void nvt_auth_debugfs_destroy(void)
{
	debugfs_remove_recursive(nvt_auth_debugfs);
}

static int nvt_auth_open(struct inode *inode, struct file *file)
{   
    TRACE_ENTRY();   
    nvt_auth_shatest();
    return 0;
}

static int nvt_auth_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t nvt_auth_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
    return 0;
}

static ssize_t nvt_auth_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
    return 0;
}

static const struct file_operations nvt_auth_fops = {
	.owner	= THIS_MODULE,
	.open    = nvt_auth_open,
	.release = nvt_auth_close,
	.llseek	 = no_llseek,
	.read	 = nvt_auth_read,
	.write	 = nvt_auth_write
};

static struct miscdevice nvt_auth_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "nvt_auth",
	.fops	= &nvt_auth_fops,
};

static int __init nvt_auth_file_init(void)
{
	if (!nvt_auth)
		return -EINVAL;
	nvt_auth_debugfs_create();
	return misc_register(&nvt_auth_misc);
}

static void __exit nvt_auth_file_cleanup(void)
{
	nvt_auth_debugfs_destroy();
	misc_deregister(&nvt_auth_misc);
}

module_init(nvt_auth_file_init);
module_exit(nvt_auth_file_cleanup);
#endif
MODULE_DESCRIPTION("Novatek HW SHA");
MODULE_LICENSE("GPL v2");
