/*********************************************************************************************
 *
 *	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
 *
 *	author : seungjun.heo@samsung.com
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

#include <linux/delay.h>
#include <plat/sdp_unzip.h>
#include <linux/of.h>
#include <mach/soc.h>

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

#define V_GZIP_CTL_ADVMODE	(0x1 << 24)
#define V_GZIP_CTL_ISIZE	(0x0 << 21)
#define V_GZIP_CTL_CRC32	(0x0 << 20)
#define V_GZIP_CTL_OUT_PAR	(0x1 << 12)
#define V_GZIP_CTL_IN_PAR	(0x1 << 8)
#define V_GZIP_CTL_OUT_ENDIAN_LITTLE	(0x1 << 4)
#define V_GZIP_CTL_IN_ENDIAN_LITTLE		(0x1 << 0)

#define GZIP_WINDOWSIZE	4		//(0:256, 1:512, 2:1024, 3:2048, 4:4096...
#define GZIP_ALIGNSIZE	64

#define GZIP_PAGESIZE	4096
#define GZIP_OUTPUTSIZE	128*1024

struct sdp_unzip_t
{
	struct device *dev;
	void __iomem *base;
	phys_addr_t pLzBuf;
	void (*isrfp)(void *);
	void *israrg;
	void *sdp1202buf;
	dma_addr_t sdp1202phybuf;
	struct page *opages[32];
	u32 clk_rst_addr;
	u32 clk_rst_mask;
	u32 clk_rst_value;
	u32 clk_mask_addr;
	u32 clk_mask_mask;
	u32 clk_mask_value;
	void *pvibuff;
	u32 isize;
	u32  opages_cnt;
};

static struct sdp_unzip_t *sdp_unzip = NULL;

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

void sdp_unzip_update_endpointer(void)
{
#ifndef CONFIG_ARCH_SDP1202
	writel(0x40000, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
#endif
}
EXPORT_SYMBOL(sdp_unzip_update_endpointer);

void sdp_unzip_clockgating(int bOn)
{
	if(!sdp_unzip->clk_rst_addr || !sdp_unzip->clk_mask_addr)
		return;
	
	if(bOn) {		
		sdp_set_clockgating(sdp_unzip->clk_mask_addr, sdp_unzip->clk_mask_mask, sdp_unzip->clk_mask_value);
		udelay(1);
		sdp_set_clockgating(sdp_unzip->clk_rst_addr, sdp_unzip->clk_rst_mask, sdp_unzip->clk_rst_value);
		udelay(1);
	} else {
		udelay(1);
		sdp_set_clockgating(sdp_unzip->clk_rst_addr, sdp_unzip->clk_rst_mask, 0);
		udelay(1);
		sdp_set_clockgating(sdp_unzip->clk_mask_addr, sdp_unzip->clk_mask_mask, 0);
	}
}

void sdp_unzip_dump(void)
{
	int i, j;
	u32 ibuff;
	volatile unsigned int *buf;
	
	printk("-------------DUMP GZIP registers------------\n");
	for(i = 0; i < 0xDF ; i += 0x10)	{
		for(j = 0 ; j < 0x10 ; j += 4)
			printk("0x%08X ", readl(sdp_unzip->base + (u32) (i + j)));
		printk("\n");
	}
	printk("--------------------------------------------\n");

	ibuff = readl(sdp_unzip->base + R_GZIP_IN_BUF_ADDR);
	printk("Input buffer pointer phy=0x%08X vir=%p\n", ibuff, sdp_unzip->pvibuff);
	dma_unmap_single(sdp_unzip->dev, ibuff, sdp_unzip->isize, DMA_FROM_DEVICE);
	buf = (volatile unsigned int *) sdp_unzip->pvibuff;
	
	printk("-------------DUMP GZIP Input Buffers--------\n");
	for(i = 0 ; i < 10; i++)	{
		for(j = 0 ; j < 4 ; j++)
			printk("0x%08X ", buf[j+i*4]);
		printk("\n");
	}
	printk("--------------------------------------------\n");
}

static irqreturn_t sdp_unzip_isr(int irq, void* devId)
{
	int i;
	u32 value;
	struct device *dev = sdp_unzip->dev;

	value = readl(sdp_unzip->base + R_GZIP_IRQ);
	writel(value, sdp_unzip->base + R_GZIP_IRQ);

	if((value == 0) || (value & 0x8))
	{
		pr_err(KERN_ERR "unzip: unzip interrupt flags=%d errorcode=0x%08X\n"
			, value, readl(sdp_unzip->base + R_GZIP_ERR_CODE));
		sdp_unzip_dump();
	}

	if (sdp_unzip->sdp1202buf) {
		dma_unmap_single(dev, sdp_unzip->sdp1202phybuf,
				 GZIP_OUTPUTSIZE, DMA_FROM_DEVICE);
		for (i = 0; i < sdp_unzip->opages_cnt; ++i) {
			void *kaddr = kmap_atomic(sdp_unzip->opages[i]);
			memcpy(kaddr, sdp_unzip->sdp1202buf + i * PAGE_SIZE,
			       PAGE_SIZE);
			kunmap_atomic(kaddr);
		}
	}

	if(sdp_unzip->isrfp)
		sdp_unzip->isrfp(sdp_unzip->israrg);

#ifndef CONFIG_ARCH_SDP1202
	writel(0, sdp_unzip->base + R_GZIP_CMD);			//Gzip Reset
#endif

	sdp_unzip_clockgating(0);
	
	return IRQ_HANDLED;
	
}

int sdp_unzip_decompress_start(char *ibuff, int ilength,
			       struct page **opages, int npages)
{
	int i;
	u32 value;
	void *pibuff;
	struct device *dev = sdp_unzip->dev;

	if (sdp_unzip == NULL) {
		pr_err("SDP Unzip Engine is not Initialized!!!\n");
		return -1;
	}

	sdp_unzip_clockgating(1);

	/* Gzip reset */
	writel(0, sdp_unzip->base + R_GZIP_CMD);

	pibuff = (void *) (u32) __pa(ibuff);
	sdp_unzip->pvibuff = ibuff;

	/* Set Source */
	writel(pibuff, sdp_unzip->base + R_GZIP_IN_BUF_ADDR);
	/* Set Src Address */
	ilength = ((ilength + GZIP_ALIGNSIZE) / GZIP_ALIGNSIZE)
		* GZIP_ALIGNSIZE;
	/* Set Src Size */
	writel(ilength, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	/* Set LZ Buf Address */
	writel(sdp_unzip->pLzBuf, sdp_unzip->base + R_GZIP_LZ_ADDR);
	sdp_unzip->isize = ilength;

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
			sdp_unzip->opages[i] = opages[i];
		}
		writel(4096, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
		dma_map_single(dev, sdp_unzip->sdp1202buf, 128*1024,
			       DMA_FROM_DEVICE);
	} else {
		/* Set page phys addr */
		for (i = 0; i < npages; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
					  R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);
			unsigned long phys = page_to_phys(opages[i]);

			writel(phys, sdp_unzip->base + off + ind * 4);

		}
		writel(4096, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
	}

	value = GZIP_WINDOWSIZE << 16;
	value |= V_GZIP_CTL_OUT_PAR | V_GZIP_CTL_ADVMODE;
	value |= 0x11;

	/* Set Decoding Control Register */
	writel(value, sdp_unzip->base + R_GZIP_DEC_CTRL);
	/* Set Timeout Value */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_TIMEOUT);
	/* Set IRQ Mask Register */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_IRQ_MASK);
	/* Set ECO value */
	writel(0x400, sdp_unzip->base + R_GZIP_PROC_DELAY);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
	if (soc_is_sdp1304())
		writel((1 << 8) | (3 << 4) | 1,
		       sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);
	/* Start Decoding */
	writel(1, sdp_unzip->base + R_GZIP_CMD);

	return 0;
}
EXPORT_SYMBOL(sdp_unzip_decompress_start);


int sdp_unzip_register_isr(void (*fp)(void *), void *args)
{
	if(sdp_unzip == NULL)
	{
		pr_err("SDP Unzip Engine is not Initialized!!!\n");
		return -1;
	}
	sdp_unzip->isrfp = fp;
	sdp_unzip->israrg = args;

	return 0;
}

EXPORT_SYMBOL(sdp_unzip_register_isr);

static int sdp_unzip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;
	int affinity = 0;
	void *buf;
	u32 tmp[3];

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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}
	
	sdp_unzip->base= devm_request_and_ioremap(&pdev->dev, res);

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

#ifndef CONFIG_ARCH_SDP1202
	if(soc_is_sdp1202())
	{
#endif
		sdp_unzip->sdp1202buf = kmalloc(128*1024, GFP_KERNEL);
		if(sdp_unzip->sdp1202buf == NULL)
		{
			dev_err(dev, "output buffer allocation failed!!!\n");
			devm_kfree(dev, sdp_unzip);
			return -ENOMEM;
		}
#ifndef CONFIG_ARCH_SDP1202
	}
#else
	sdp_unzip->clk_rst_addr = 0x10090954;
	sdp_unzip->clk_rst_mask = 0x00800000;
	sdp_unzip->clk_rst_value = 0x00800000;
	sdp_unzip->clk_mask_addr = 0x10090944;
	sdp_unzip->clk_mask_mask = 0x20000000;
	sdp_unzip->clk_mask_value = 0x20000000;	
#endif

#ifdef CONFIG_OF
	if(!of_property_read_u32_array(dev->of_node, "clock_reset", &tmp[0], 3))
	{
		sdp_unzip->clk_rst_addr = tmp[0];
		sdp_unzip->clk_rst_mask = tmp[1];
		sdp_unzip->clk_rst_value = tmp[2];
	}
	if(!of_property_read_u32_array(dev->of_node, "clock_mask", &tmp[0], 3))
	{
		sdp_unzip->clk_mask_addr = tmp[0];
		sdp_unzip->clk_mask_mask = tmp[1];
		sdp_unzip->clk_mask_value = tmp[2];
	}
#endif

	buf = kmalloc(4096, GFP_KERNEL);
	if(buf == NULL)	{
		dev_err(dev, "cannot allocate lzbuf memory!!!\n");
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}
	sdp_unzip->pLzBuf = __pa(buf);

	platform_set_drvdata(pdev, (void *) sdp_unzip);
	sdp_unzip->dev = dev;

	dev_info(dev, "Registered unzip driver!!\n");

	return 0;
}

static int sdp_unzip_remove(struct platform_device *pdev)
{
	kfree(__va(sdp_unzip->pLzBuf));
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
	return platform_driver_register(&sdp_unzip_driver);
}
subsys_initcall(sdp_unzip_init);

static void __exit sdp_unzip_exit(void)
{
	platform_driver_unregister(&sdp_unzip_driver);
}
module_exit(sdp_unzip_exit);



MODULE_DESCRIPTION("Samsung SDP SoCs HW Decompress driver");
MODULE_LICENSE("GPL v2");

