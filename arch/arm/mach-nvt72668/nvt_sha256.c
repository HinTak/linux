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
#ifdef CONFIG_SW_SHA256_USE
#include <linux/crypto.h>
#include <crypto/hash.h>
#endif

//#define WORKAROUND_NO_HASH_PARAS
//#define NVT_SHA_TEST

#define TRACE_FLOW 0
#define TRACE(...) \
	if(TRACE_FLOW) printk(__VA_ARGS__);

#define TRACE_ENTRY() \
	if(TRACE_FLOW) printk("###nvtunzip hal: %s %d entry\n", __FUNCTION__, __LINE__);

#define TRACE_EXIT() \
	if(TRACE_FLOW) printk("###nvtunzip hal: %s %d exit\n", __FUNCTION__, __LINE__);

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
#define ADMA_BIT_WIDTH              0x110
#define ADMA_SHA_MODE               0x400
#define ADMA_SHA_INIT               0x404
#define ADMA_SHA_STATUS             0x408
#define ADMA_SHA_DATA               0x40C
#define ADMA_SHA_DMA                0x410
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
    EN_ADMA_DST_INVALID_DES =   (1<<19)
}EN_ADMA_INT_STAT;
#define ADMA_INT_SETTING        (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP|EN_ADMA_SRC_DATA_ERR| \
                                EN_ADMA_DST_EOF|EN_ADMA_DST_STOP|EN_ADMA_DST_DATA_ERR|  \
                                EN_ADMA_TIMEOUT|EN_ADMA_SRC_INVALID_DES|EN_ADMA_DST_INVALID_DES)
#define ADMA_INT_ERR            (EN_ADMA_SRC_DATA_ERR|EN_ADMA_DST_DATA_ERR|  \
                                EN_ADMA_SRC_INVALID_DES|EN_ADMA_DST_INVALID_DES)
#define ADMA_INT_DATA_DONE      (EN_ADMA_SRC_EOF|EN_ADMA_SRC_STOP |\
                                EN_ADMA_DST_EOF|EN_ADMA_DST_STOP)
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
#define MAX_ADMA_NUM (256)
struct st_src_adma
{
	adma_desc_t *des;
	dma_addr_t des_phys;
};
struct st_dst_adma
{
	adma_desc_t *des;
	dma_addr_t des_phys;
};
struct nvt_sha_t
{
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
    struct st_src_adma data_input;
    struct st_dst_adma data_output;
	u32 data_input_cnt;		
    u32 data_output_cnt;		
    struct sha_buf pad_buf;
    struct sha_buf dst_buf;
    u64 data_head_len; 
    u32 data_tail_len; 

};
static u64 sha_dmamask = DMA_BIT_MASK(32);
static struct nvt_sha_t *nvt_sha = NULL;
static volatile u32 ISRStat=0;
static volatile u32 ISRErr=0;
static unsigned long long nvt_sha_calls=0;
static unsigned long long nvt_sha_errors=0;
static unsigned long long nvt_sha_nsecs=0;
static unsigned int       nvt_sha_quiet=0;
/*generate source descriptor */
static void sha_prepare_src_adma( unsigned int addr, unsigned int len, unsigned int padaddr, unsigned int padlen)
{
	struct st_src_adma * data_input = &nvt_sha->data_input;
	unsigned int calc_len = 0;
	int unit_num = 0; 
    bool last_unit = false;
	adma_desc_t * des = NULL;
	nvt_sha->data_input_cnt=0;
	while( (calc_len < len) ){
		last_unit = false;
		des = &data_input->des[nvt_sha->data_input_cnt];
		BUG_ON(!des);
		if( (len <= ADMA_MAX_DECODE_LEN)  || calc_len+ADMA_MAX_DECODE_LEN>=len)
			last_unit = true;
		des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN ;
		des->len = (last_unit) ? len - calc_len : ADMA_MAX_DECODE_LEN;
		des->addr = addr + calc_len;
		TRACE(">>>NVT_SHA : input des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				nvt_sha->data_input_cnt , des->len, des->addr, des->attribute);
		unit_num++;
		calc_len += ADMA_MAX_DECODE_LEN;	
		nvt_sha->data_input_cnt += 1;	
	}
    //last padding data
    des = &data_input->des[nvt_sha->data_input_cnt];
    BUG_ON(!des);
    des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN |EN_ADMA_ATTRIBUTE_END|EN_ADMA_ATTRIBUTE_INT;
    des->len = padlen;
    des->addr = padaddr;
    TRACE(">>>NVT_SHA : input des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				nvt_sha->data_input_cnt , des->len, des->addr, des->attribute);
	writel(data_input->des_phys, nvt_sha->base + ADMA_SRC_DESCRIPTOR_ADDR);
}
static void sha_prepare_dst_adma( unsigned int addr, unsigned int len, unsigned int padlen)
{
    struct st_dst_adma * data_output = &nvt_sha->data_output;
	unsigned int calc_len = 0;
	int unit_num = 0;   
    bool last_unit = false;
	adma_desc_t * des = NULL;
	nvt_sha->data_output_cnt=0;
	while( (calc_len < len) ){
		last_unit = false;
		des = &data_output->des[nvt_sha->data_output_cnt];
		BUG_ON(!des);
		if( (len <= ADMA_MAX_DECODE_LEN)  || calc_len+ADMA_MAX_DECODE_LEN>=len)
			last_unit = true;
		des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN ;
		des->len = (last_unit) ? len - calc_len : ADMA_MAX_DECODE_LEN;
		des->addr = addr ;
		TRACE(">>>NVT_SHA : output des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				nvt_sha->data_output_cnt , des->len, des->addr, des->attribute);
		unit_num++;
		calc_len += ADMA_MAX_DECODE_LEN;	
		nvt_sha->data_output_cnt += 1;	
	}
    //last padding data
    des = &data_output->des[nvt_sha->data_output_cnt];
    BUG_ON(!des);
    des->attribute = EN_ADMA_ATTRIBUTE_VALID | EN_ADMA_ATTRIBUTE_TRAN |EN_ADMA_ATTRIBUTE_END|EN_ADMA_ATTRIBUTE_INT;
    des->len = padlen;
    des->addr = addr;
    TRACE(">>>NVT_SHA : output des: [%2d]len: 0x%x addr %x attr: 0x%x\n", 
				nvt_sha->data_output_cnt , des->len, des->addr, des->attribute);
    writel(data_output->des_phys, nvt_sha->base + ADMA_DST_DESCRIPTOR_ADDR);
}
static irqreturn_t nvt_sha_isr(int irq, void* devId)
{
    u32 val;
    u32 datadone;
    val = readl(nvt_sha->base+ADMA_INT_STATUS_AND_CLR);
    nvt_sha_calls++;
    if(val&ADMA_INT_ERR)
    {
        nvt_sha_errors++;
        ISRErr = val&ADMA_INT_ERR;
        writel(ISRErr, nvt_sha->base+ADMA_INT_STATUS_AND_CLR);    
        complete(&nvt_sha->wait);
    }
    else if (val&ADMA_INT_DATA_DONE)
    {
        datadone = val&ADMA_INT_DATA_DONE;
        ISRStat |= datadone;
        writel(datadone, nvt_sha->base+ADMA_INT_STATUS_AND_CLR);
        if(ISRStat == ADMA_INT_DATA_DONE)
        {
            ISRStat=0;
            complete(&nvt_sha->wait);
        }
    }
    else
    {
        ISRErr = 0xFFFFFFFF;    
        complete(&nvt_sha->wait);
    }
    return IRQ_HANDLED;
}
static void sha256_init(void)
{   
    nvt_sha->data_input_cnt = 0;
    nvt_sha->data_output_cnt = 0;
    nvt_sha->data_head_len = 0;
    nvt_sha->data_tail_len = 0;
    ISRStat=0;
    ISRErr=0;
    writel(0x10,            nvt_sha->base+ADMA_HW_SETTING);
    writel(ADMA_INT_SETTING, nvt_sha->base+ADMA_INT_STATUS_AND_CLR);    
    writel(ADMA_INT_SETTING, nvt_sha->base+ADMA_INT_ENABLE);
    writel(0x1,             nvt_sha->base+ADMA_SHA_MODE);
    writel(0x40,            nvt_sha->base+ADMA_BIT_WIDTH);
    writel(0x1,             nvt_sha->base+ADMA_SHA_DMA);
    writel(0x1,             nvt_sha->base+ADMA_SHA_INIT);
}
static void sha256_Transdata(u8 *data, dma_addr_t pbuff, unsigned int len)
{
    u32 index, padlen;
    u32 padoffset=0;
    __be64 bits;
    static const u8 padding[64] = { 0x80, };
    TRACE("sha256_Transdata\n");
	bits = cpu_to_be64(len << 3);
	/* Pad out to 56 mod 64 */
	index = len & 0x3f;
	padlen = (index < 56) ? (56 - index) : ((64+56) - index);
    TRACE("sha256 adr:%x, len:%d\n", (u32)data, len);
    TRACE("residual %d\n", index);
    TRACE("padding len %d\n", padlen);
    if(index>0)
    {
        memcpy(((u8*)nvt_sha->pad_buf.vaddr), &data[len-index], index);
        len-=index;
    }
    padoffset += index;
    memcpy(((u8*)nvt_sha->pad_buf.vaddr)+padoffset, padding, padlen);
    padoffset += padlen;
    memcpy(((u8*)nvt_sha->pad_buf.vaddr)+padoffset, (void*)&bits, sizeof(bits));
    padoffset +=sizeof(bits);
    nvt_sha->data_head_len = len;
    nvt_sha->data_tail_len = padoffset;
    TRACE("data_head_len %lld, data_tail_len %d\n", nvt_sha->data_head_len, nvt_sha->data_tail_len);
    writel(0x60,    nvt_sha->base+ADMA_HW_SETTING);
    //writel(0x20000,    nvt_sha->base+ADMA_TIMEOUT);
    sha_prepare_src_adma((u32)pbuff, len, nvt_sha->pad_buf.paddr, padoffset);
    sha_prepare_dst_adma((u32)nvt_sha->dst_buf.paddr, len, padoffset);
    writel(0x0,     nvt_sha->base+ADMA_HW_CONFIGURE);
    writel(0x5,     nvt_sha->base+ADMA_HW_SETTING);
}
void regdump(void)
{ 
    printk("ADMA_HW_SETTING 0x%08x\n",          readl(nvt_sha->base+ADMA_HW_SETTING));
    printk("ADMA_SRC_DESCRIPTOR_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_SRC_DESCRIPTOR_ADDR));
    printk("ADMA_SRC_CUR_DESCRIPTOR_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_SRC_CUR_DESCRIPTOR_ADDR));
    printk("ADMA_SRC_STATUS 0x%08x\n", readl(nvt_sha->base+ADMA_SRC_STATUS));
    printk("ADMA_DST_DESCRIPTOR_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_DST_DESCRIPTOR_ADDR));
    printk("ADMA_DST_CUR_DESCRIPTOR_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_DST_CUR_DESCRIPTOR_ADDR));
    printk("ADMA_DST_STATUS 0x%08x\n", readl(nvt_sha->base+ADMA_DST_STATUS));
    printk("ADMA_HW_CONFIGURE 0x%08x\n", readl(nvt_sha->base+ADMA_HW_CONFIGURE));
    printk("ADMA_HW_DEBUG 0x%08x\n", readl(nvt_sha->base+ADMA_HW_DEBUG));
    printk("ADMA_SRC_BYTE_COUNT 0x%08x\n", readl(nvt_sha->base+ADMA_SRC_BYTE_COUNT));
    printk("ADMA_DST_BYTE_COUNT 0x%08x\n", readl(nvt_sha->base+ADMA_DST_BYTE_COUNT));
    printk("ADMA_TIMEOUT 0x%08x\n", readl(nvt_sha->base+ADMA_TIMEOUT));
    printk("ADMA_INT_ENABLE 0x%08x\n", readl(nvt_sha->base+ADMA_INT_ENABLE));
    printk("ADMA_INT_STATUS_AND_CLR 0x%08x\n", readl(nvt_sha->base+ADMA_INT_STATUS_AND_CLR));
    printk("ADMA_SRC_FINAL_READ_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_SRC_FINAL_READ_ADDR));
    printk("ADMA_DST_FINAL_WRITE_ADDR 0x%08x\n", readl(nvt_sha->base+ADMA_DST_FINAL_WRITE_ADDR));
    printk("ADMA_BIT_WIDTH 0x%08x\n", readl(nvt_sha->base+ADMA_BIT_WIDTH));
    printk("ADMA_SHA_MODE 0x%08x\n", readl(nvt_sha->base+ADMA_SHA_MODE));
    printk("ADMA_SHA_INIT 0x%08x\n", readl(nvt_sha->base+ADMA_SHA_INIT));
    printk("ADMA_SHA_STATUS 0x%08x\n", readl(nvt_sha->base+ADMA_SHA_STATUS));
    printk("ADMA_SHA_DATA 0x%08x\n", readl(nvt_sha->base+ADMA_SHA_DATA));
    printk("ADMA_SHA_DMA 0x%08x\n", readl(nvt_sha->base+ADMA_SHA_DMA));
}
void calculate_hw_hash_sha256(unsigned char *buf,
		dma_addr_t pbuff, unsigned int buf_len)
{
    BUG_ON(!nvt_sha);
    BUG_ON(!nvt_sha->data_input.des);
    BUG_ON(!nvt_sha->data_output.des);
    BUG_ON(!nvt_sha->pad_buf.vaddr);
    BUG_ON(!nvt_sha->dst_buf.vaddr);
    BUG_ON(!buf);
    BUG_ON(!pbuff);
    BUG_ON(!buf_len);
    TRACE("calculate_hw_hash_sha256\n");
    down(&nvt_sha->sema);
    sha256_init();
    sha256_Transdata(buf, pbuff,buf_len);
}
int hw_sha256_wait(unsigned char* hash)
{
    int i;
    u32 dst[8];
    BUG_ON(!nvt_sha);
#ifndef WORKAROUND_NO_HASH_PARAS 
    BUG_ON(!hash);
#endif
    TRACE("hw_sha256_wait\n");
    wait_for_completion(&nvt_sha->wait);
    if(ISRErr)
    {
        printk("ISRStat %x, ISRErr %x\n", ISRStat, ISRErr);
        regdump();
        up(&nvt_sha->sema);
        return (-EINVAL);
    }
    writel(0x2, nvt_sha->base+ADMA_SHA_INIT); 
	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(readl(nvt_sha->base+ADMA_SHA_DATA));
#ifndef WORKAROUND_NO_HASH_PARAS 
    memcpy(hash, dst, 32);
#endif
    up(&nvt_sha->sema);
    return 0;
}
EXPORT_SYMBOL(calculate_hw_hash_sha256);
EXPORT_SYMBOL(hw_sha256_wait);
static int nvt_sha_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;
#ifdef CONFIG_OF	
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif
	nvt_sha = devm_kzalloc(dev, sizeof(struct nvt_sha_t), GFP_KERNEL);
	if(nvt_sha == NULL)
	{
		dev_err(dev, "cannot allocate memory!!!\n");
		return -ENOMEM;
	}
	sema_init(&nvt_sha->sema, 1);
	init_completion(&nvt_sha->wait);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, nvt_sha);
		return -ENODEV;
	}
	nvt_sha->base = devm_request_and_ioremap(&pdev->dev, res);
	if (nvt_sha->base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, nvt_sha);
        return -ENODEV;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, nvt_sha);
		return -ENODEV;
	}
	ret = request_irq(irq , nvt_sha_isr, 0, dev_name(dev), nvt_sha);
	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d\n", irq);
		devm_kfree(dev, nvt_sha);
		return -ENODEV;
	}
	dev->dma_mask = &sha_dmamask;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	platform_set_drvdata(pdev, (void *) nvt_sha);
    nvt_sha->data_input.des = dma_alloc_coherent(dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, &nvt_sha->data_input.des_phys, GFP_NOWAIT );
    if(nvt_sha->data_input.des == NULL){
		dev_err(dev, "cannot allocate source descriptor buffer\n" );
        goto MEMFAIL; 
	}
    nvt_sha->data_output.des = dma_alloc_coherent(dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, &nvt_sha->data_output.des_phys, GFP_NOWAIT );
    if(nvt_sha->data_output.des == NULL){
		dev_err(dev, "cannot allocate destination descriptor buffer\n" );
        goto MEMFAIL; 
	}
    nvt_sha->pad_buf.vaddr = dma_alloc_coherent(dev, 128, &nvt_sha->pad_buf.paddr, GFP_NOWAIT );
    if(nvt_sha->pad_buf.vaddr == NULL){
		dev_err(dev, "cannot allocate padding buffer\n" );
        goto MEMFAIL;
	}
    nvt_sha->pad_buf.size = 128;
    nvt_sha->dst_buf.vaddr = dma_alloc_coherent(dev, ADMA_MAX_DECODE_LEN, &nvt_sha->dst_buf.paddr, GFP_NOWAIT );
    if(nvt_sha->dst_buf.vaddr == NULL){
		dev_err(dev, "cannot allocate output buffer\n" );
        goto MEMFAIL; 
	}
    nvt_sha->dst_buf.size = ADMA_MAX_DECODE_LEN;
	/* set platform  device information*/
	nvt_sha->dev = dev;
	dev_info(dev, "Registered Novatek sha driver orig: %p \n", nvt_sha);
	return 0;
MEMFAIL:
    if(!nvt_sha->pad_buf.vaddr)
        dma_free_coherent(&pdev->dev, nvt_sha->pad_buf.size, nvt_sha->pad_buf.vaddr, nvt_sha->pad_buf.paddr);
    if(!nvt_sha->dst_buf.vaddr)
        dma_free_coherent(&pdev->dev, nvt_sha->dst_buf.size, nvt_sha->dst_buf.vaddr, nvt_sha->dst_buf.paddr);
    if(!nvt_sha->data_input.des)
        dma_free_coherent(&pdev->dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, nvt_sha->data_input.des, nvt_sha->data_input.des_phys);
    if(!nvt_sha->data_output.des)
        dma_free_coherent(&pdev->dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, nvt_sha->data_output.des, nvt_sha->data_output.des_phys );
    if(!nvt_sha)
		devm_kfree(dev, nvt_sha);
    return -ENOMEM;
}

static int nvt_sha_remove(struct platform_device *pdev)
{
    dma_free_coherent(&pdev->dev, nvt_sha->dst_buf.size, nvt_sha->dst_buf.vaddr, nvt_sha->dst_buf.paddr);
    dma_free_coherent(&pdev->dev, nvt_sha->pad_buf.size, nvt_sha->pad_buf.vaddr, nvt_sha->pad_buf.paddr);
	dma_free_coherent(&pdev->dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, nvt_sha->data_input.des, nvt_sha->data_input.des_phys);
    dma_free_coherent(&pdev->dev, sizeof(adma_desc_t)*MAX_ADMA_NUM, nvt_sha->data_output.des, nvt_sha->data_output.des_phys);
	devm_kfree(&pdev->dev, nvt_sha);
	return 0;
}

static const struct of_device_id nvt_sha_dt_match[] = {
	{ .compatible = "novatek,nvt-sha", },
	{},
};

MODULE_DEVICE_TABLE(of, nvt_sha_dt_match);
static struct platform_driver nvt_sha_driver = {
	.probe		= nvt_sha_probe,
	.remove		= nvt_sha_remove,
	.driver = {
		.name	= "nvt-sha",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(nvt_sha_dt_match),
	},
};

int nvt_sha_init(void)
{
	return platform_driver_register(&nvt_sha_driver);
}
subsys_initcall(nvt_sha_init);

static void __exit nvt_sha_exit(void)
{
	platform_driver_unregister(&nvt_sha_driver);
}
module_exit(nvt_sha_exit);

#ifdef NVT_SHA_TEST
int sw_sha256(unsigned char *buf, unsigned int buf_len,
		unsigned char *hash)
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
static unsigned long long nvt_sha_get_nsecs(void)
{
#ifdef CONFIG_NVT_HW_CLOCK 
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	struct timespec ts;
	getrawmonotonic(&ts);
	return ts.tv_sec*1000000000 +ts.tv_nsec;
#endif
}

static struct dentry *nvt_sha_debugfs;
static void nvt_sha_debugfs_create(void)
{
	nvt_sha_debugfs = debugfs_create_dir("nvt_sha", NULL);
	if (!IS_ERR_OR_NULL(nvt_sha_debugfs)) {
		debugfs_create_u64("calls",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_sha_debugfs, &nvt_sha_calls);
		debugfs_create_u64("errors",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_sha_debugfs, &nvt_sha_errors);
		debugfs_create_u64("nsecs",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   nvt_sha_debugfs, &nvt_sha_nsecs);
		debugfs_create_bool("quiet",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    nvt_sha_debugfs, &nvt_sha_quiet);
	}
}
static void nvt_sha_debugfs_destroy(void)
{
	debugfs_remove_recursive(nvt_sha_debugfs);
}
void data(unsigned char* pbuf, unsigned int size)
{   
    int i=0;
    memset(pbuf, 0x0, size);
    for(i=0;i<size;i++)
        pbuf[i] = i&0xff;
}

static int nvt_sha_open(struct inode *inode, struct file *file)
{    
    return 0;
}

static int nvt_sha_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t nvt_sha_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
    return 0;
}

static ssize_t nvt_sha_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
    return 0;
}

static const struct file_operations nvt_sha_fops = {
	.owner	= THIS_MODULE,
	.open    = nvt_sha_open,
	.release = nvt_sha_close,
	.llseek	 = no_llseek,
	.read	 = nvt_sha_read,
	.write	 = nvt_sha_write
};

static struct miscdevice nvt_sha_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "nvt_sha",
	.fops	= &nvt_sha_fops,
};


static int __init nvt_sha_file_init(void)
{
	if (!nvt_sha)
		return -EINVAL;
	nvt_sha_debugfs_create();
	return misc_register(&nvt_sha_misc);
}

static void __exit nvt_sha_file_cleanup(void)
{
	nvt_sha_debugfs_destroy();
	misc_deregister(&nvt_sha_misc);
}

module_init(nvt_sha_file_init);
module_exit(nvt_sha_file_cleanup);
#endif
MODULE_DESCRIPTION("Novatek HW SHA");
MODULE_LICENSE("GPL v2");
