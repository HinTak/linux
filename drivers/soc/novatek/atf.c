
/****************************************************************************
*
* atf.c (Novateck AHB TO AXI fifo driver)
*
*	author : jianhao.su@novatek.com.tw
*
* 2016/07/14, jianhao.su: novatek implementation for Samsung platform.
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
#include "atf.h"

#if defined(CONFIG_ARCH_NVT72673)
/*#define nvt_atf->base	0xfd060300*/
#define HAL_ATF_OFFSET		(0x0)
#define HAL_ATF_LENGTH		(0x4)
#define HAL_ATF_TIMEOUT		(0x8)

#define HAL_ATF_CONFIG		(0xc)
#define ATF_SRC_SPI		(BIT(0))
#define ATF_SRC_NFC		(BIT(1))
#define ATF_SRC_NFC_ARM		(BIT(2))
#define ATF_START		(BIT(4))
#define ATF_TO_ZIP		(BIT(5))
#define ATF_TO_AES		(BIT(6))
#define ATF_TO_SHA		(BIT(7))
#define ATF_SRC_SPI_BIT			(0)
#define ATF_SRC_SPI_BIT_LEN		(1)
#define ATF_SRC_NFC_BIT			(1)
#define ATF_SRC_NFC_BIT_LEN		(1)
#define ATF_SRC_NFC_ARM_BIT		(2)
#define ATF_SRC_NFC_ARM_BIT_LEN		(1)
#define ATF_START_BIT			(4)
#define ATF_START_BIT_LEN		(1)
#define ATF_TO_ZIP_BIT			(5)
#define ATF_TO_ZIP_BIT_LEN		(1)
#define ATF_TO_AES_BIT			(6)
#define ATF_TO_AES_BIT_LEN		(1)
#define ATF_TO_SHA_BIT			(7)
#define ATF_TO_SHA_BIT_LEN		(1)

#define HAL_ATF_INT_EN_W	(0x10)
#define ATF_TIMEOUT		(BIT(0))
#define ATF_INTR_DONE		(BIT(1))
#define ATF_TIMEOUT_BIT			(0)
#define ATF_TIMEOUT_BIT_LEN		(1)
#define ATF_INTR_DONE_BIT		(1)
#define ATF_INTR_DONE_BIT_LEN		(1)
/*#define ATF_INT*/

/*notice : to clear this register , write 1 and write 0 is needed*/
#define HAL_ATF_INT_W		(0x14)
/*#define ATF_TIMEOUT		(BIT(0))*/
/*#define ATF_INTR_DONE		(BIT(1))*/

#define HAL_ATF_START_OFFSET	(0x1c)
#define HAL_ATF_START_LEN	(0x20)

#define	HAL_ATF_DEBUG		(0x24)
#define ATF_FIFO_WADDR		(0)
#define ATF_FIFO_WADDR_LEN	(2-0+1)
#define ATF_FIFO_FULL		(3)
#define ATF_FIFO_FULL_LEN	(3-3+1)
#define ATF_FIFO_RADD		(4)
#define ATF_FIFO_RADD_LEN	(6-4+1)
#define ATF_FIFO_EMPTY		(7)
#define ATF_FIFO_EMPTY_LEN	(7-7+1)
#define ATF_FIFO_STATE		(8)
#define ATF_FIFO_STATE_LEN	(9-8+1)
#else /* !CONFIG_ARCH_NVT72673 */
/*#define nvt_atf->base	0xfd060100*/
#define HAL_ATF_OFFSET		(0x9c)
#define HAL_ATF_LENGTH		(0xa0)
#define HAL_ATF_TIMEOUT		(0xa4)

#define HAL_ATF_CONFIG		(0xa8)
#define ATF_SRC_SPI		(BIT(0))
#define ATF_SRC_NFC		(BIT(1))
#define ATF_SRC_NFC_ARM		(BIT(2))
#define ATF_START		(BIT(4))
#define ATF_TO_ZIP		(BIT(5))
#define ATF_TO_AES		(BIT(6))
#define ATF_TO_SHA		(BIT(7))
#define ATF_SRC_SPI_BIT			(0)
#define ATF_SRC_SPI_BIT_LEN		(1)
#define ATF_SRC_NFC_BIT			(1)
#define ATF_SRC_NFC_BIT_LEN		(1)
#define ATF_SRC_NFC_ARM_BIT		(2)
#define ATF_SRC_NFC_ARM_BIT_LEN		(1)
#define ATF_START_BIT			(4)
#define ATF_START_BIT_LEN		(1)
#define ATF_TO_ZIP_BIT			(5)
#define ATF_TO_ZIP_BIT_LEN		(1)
#define ATF_TO_AES_BIT			(6)
#define ATF_TO_AES_BIT_LEN		(1)
#define ATF_TO_SHA_BIT			(7)
#define ATF_TO_SHA_BIT_LEN		(1)

#define HAL_ATF_INT_EN_W	(0xac)
#define ATF_TIMEOUT		(BIT(0))
#define ATF_INTR_DONE		(BIT(1))
#define ATF_TIMEOUT_BIT			(0)
#define ATF_TIMEOUT_BIT_LEN		(1)
#define ATF_INTR_DONE_BIT		(1)
#define ATF_INTR_DONE_BIT_LEN		(1)
/*#define ATF_INT*/

/*notice : to clear this register , write 1 and write 0 is needed*/
#define HAL_ATF_INT_W		(0xac+2)
/*#define ATF_TIMEOUT		(BIT(0))*/
/*#define ATF_INTR_DONE		(BIT(1))*/

#define HAL_ATF_START_OFFSET	(0xb0)
#define HAL_ATF_START_LEN	(0xb4)

#define	HAL_ATF_DEBUG		(0xb8)
#define ATF_FIFO_WADDR		(0)
#define ATF_FIFO_WADDR_LEN	(2-0+1)
#define ATF_FIFO_FULL		(3)
#define ATF_FIFO_FULL_LEN	(3-3+1)
#define ATF_FIFO_RADD		(4)
#define ATF_FIFO_RADD_LEN	(6-4+1)
#define ATF_FIFO_EMPTY		(7)
#define ATF_FIFO_EMPTY_LEN	(7-7+1)
#define ATF_FIFO_STATE		(8)
#define ATF_FIFO_STATE_LEN	(9-8+1)
#endif /* CONFIG_ARCH_NVT72673 */

#define TRACE_FLOW 0
#define TRACE(...) \
	do {						\
		if (TRACE_FLOW)				\
			printk(__VA_ARGS__);		\
	} while (0)
#define TRACE_ENTRY() \
	do {								\
		if (TRACE_FLOW)						\
			printk("ENTRY %s %d\n",				\
				__func__, __LINE__);			\
	} while (0)

#define TRACE_EXIT() \
	do {								\
		if (TRACE_FLOW)						\
			pr_err("EXIT %s %d\n",				\
				__func__, __LINE__);			\
	} while (0)

struct nvt_atf_t {
	struct device *dev;
	void __iomem *base;
};
static struct nvt_atf_t *nvt_atf;/*= NULL;*/

void fifo_atf_config(int timeout, int done_intr)
{
	u32 cfg = 0;

	TRACE_ENTRY();
	if (timeout) {
		writel(timeout, nvt_atf->base + HAL_ATF_TIMEOUT);
		cfg |= ATF_TIMEOUT;
	}

	if (done_intr) {
		/*enable done interrupt*/
		cfg |= ATF_INTR_DONE;
	}

	writel(cfg, nvt_atf->base + HAL_ATF_INT_EN_W);

	/* clear status */
	writew(ATF_INTR_DONE | ATF_TIMEOUT, nvt_atf->base+HAL_ATF_INT_W);
	writew(0, nvt_atf->base+HAL_ATF_INT_W);
	TRACE_EXIT();

}

void fifo_atf_issue(struct atf_setting config)
{
	u32 cfg = 0;

	TRACE_ENTRY();
	cfg |= config.src;
	cfg |= config.dest;
	cfg |= (ATF_START);

	writel(config.length, nvt_atf->base + HAL_ATF_LENGTH);
	writel(config.offset, nvt_atf->base + HAL_ATF_OFFSET);
	writel(cfg, nvt_atf->base + HAL_ATF_CONFIG);
	TRACE_EXIT();

}

void fifo_atf_start(struct atf_setting config)
{
	TRACE_ENTRY();
	fifo_atf_config(config.timeout, 0);
	fifo_atf_issue(config);
	TRACE_EXIT();
}

void nvt_atf_execute(int length, int target, int offset)
{
	struct atf_setting config;

	TRACE_ENTRY();
	memset(&config, 0x0, sizeof(struct atf_setting));

	config.offset = offset;
	config.timeout = (1<<30);
	config.src = iff_NFC;
	config.dest = target;
	config.length = length;
	fifo_atf_start(config);
	TRACE_EXIT();
}
EXPORT_SYMBOL(nvt_atf_execute);

static irqreturn_t nvt_atf_isr(int irq, void *devId)
{
	u16 status = readw(nvt_atf->base+HAL_ATF_INT_W);
	u16 done_event = status & (ATF_INTR_DONE);
	u16 timeout_event = status & (ATF_TIMEOUT);

	TRACE_ENTRY();
	/*printk("<<atf intr :  status %x done_event %x timeout %x>>\n",
		status, done_event, timeout_event);*/
	writew(status, nvt_atf->base+HAL_ATF_INT_W);

	if (timeout_event) {
		status -= ATF_TIMEOUT;
		pr_err("__FIFO_TIMEOUT__\n");
		fifo_regdump();
#ifndef CONFIG_VD_RELEASE
		BUG_ON(timeout_event);
#endif
	}
	if (done_event)
		status -= ATF_INTR_DONE;

	writew(status, nvt_atf->base+HAL_ATF_INT_W);
	/*writel(0, nvt_atf->base+ HAL_ATF_CONFIG );// reset atf*/
	TRACE_EXIT();
	return IRQ_HANDLED;
}

static void debug_range(char *regname, u32 REG, char *name, int offset, int len)
{
	u32 reg = readl(nvt_atf->base+REG);

	pr_err("   %s->%s(0x%x): 0x%x\n", regname, name,
		(((1<<len)-1)<<offset),
		(reg & (((1<<len)-1)<<offset)) >> offset);
}

static void debug_range_w(char *regname, u32 REG, char *name,
		int offset, int len)
{
	u32 reg = readw(nvt_atf->base+REG);

	pr_err("   %s->%s(0x%x): 0x%x\n", regname, name,
		(((1<<len)-1)<<offset),
		(reg & (((1<<len)-1)<<offset)) >> offset);
}
#define ATF_DR(REG, NAME) debug_range(#REG, REG, #NAME, NAME, NAME##_LEN)
#define ATF_DR_W(REG, NAME) debug_range_w(#REG, REG, #NAME, NAME, NAME##_LEN)

void fifo_atf_done(void)
{
	writel(0, nvt_atf->base+HAL_ATF_CONFIG);
}

void atf_reg_info_w(u32 reg, char name[64])
{
	pr_err("	%s(%x) : 0x%x\n", name, reg,
		readw(nvt_atf->base  + reg));
}

void atf_reg_info(u32 reg, char name[64])
{
	pr_err("	%s(%x) : 0x%x\n", name, reg,
		readl(nvt_atf->base  + reg));
}

void fifo_regdump(void)
{
	pr_err("========atf reg dump=====\n");
#define ATF_REG_INFO(REG) atf_reg_info(REG, #REG)
#define ATF_REG_INFO_W(REG) atf_reg_info_w(REG, #REG)

	ATF_REG_INFO(HAL_ATF_OFFSET);
	ATF_REG_INFO(HAL_ATF_LENGTH);

	ATF_REG_INFO(HAL_ATF_CONFIG);
	ATF_DR(HAL_ATF_CONFIG, ATF_SRC_SPI_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_SRC_NFC_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_SRC_NFC_ARM_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_START_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_TO_ZIP_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_TO_AES_BIT);
	ATF_DR(HAL_ATF_CONFIG, ATF_TO_SHA_BIT);

	ATF_REG_INFO(HAL_ATF_TIMEOUT);
	ATF_REG_INFO_W(HAL_ATF_INT_EN_W);
	ATF_DR_W(HAL_ATF_INT_EN_W, ATF_TIMEOUT_BIT);
	ATF_DR_W(HAL_ATF_INT_EN_W, ATF_INTR_DONE_BIT);
	ATF_REG_INFO_W(HAL_ATF_INT_W);
	ATF_DR_W(HAL_ATF_INT_W, ATF_TIMEOUT_BIT);
	ATF_DR_W(HAL_ATF_INT_W, ATF_INTR_DONE_BIT);

	pr_err("========atf debug reg dump=====\n");
	ATF_REG_INFO(HAL_ATF_START_OFFSET);
	ATF_REG_INFO(HAL_ATF_START_LEN);
	ATF_REG_INFO(HAL_ATF_DEBUG);
	ATF_DR(HAL_ATF_DEBUG, ATF_FIFO_WADDR);
	ATF_DR(HAL_ATF_DEBUG, ATF_FIFO_RADD);
	ATF_DR(HAL_ATF_DEBUG, ATF_FIFO_EMPTY);

	pr_err("========atf reg dump=====\n");
}


static int nvt_atf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;

	TRACE_EXIT();
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	nvt_atf = devm_kzalloc(dev, sizeof(struct nvt_atf_t), GFP_KERNEL);
	if (nvt_atf == NULL) {
		/*dev_err(dev, "cannot allocate memory!!!\n");*/
		return -ENOMEM;
	}

	nvt_atf->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, nvt_atf);
		return -ENODEV;
	}

	nvt_atf->base = devm_ioremap_resource(&pdev->dev, res);
	dev_dbg(nvt_atf->dev, "[DEBUG] interregister base %x\n", res->start);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, nvt_atf);
		return -ENODEV;
	}
	ret = request_irq(irq, nvt_atf_isr, 0, dev_name(dev), nvt_atf);

	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d err :%d\n", irq, ret);
		devm_kfree(dev, nvt_atf);
		return -ENODEV;
	}
	dev_info(dev, "Registered Novatek atf driver(%p)\n",
		nvt_atf->base);
	TRACE_EXIT();
	return 0;
}

static int nvt_atf_remove(struct platform_device *pdev)
{
	devm_kfree(&pdev->dev, nvt_atf);
	return 0;
}

static const struct of_device_id nvt_atf_dt_match[] = {
	{ .compatible = "nvt,atf", },
	{},
};
MODULE_DEVICE_TABLE(of, nvt_atf_dt_match);

static struct platform_driver nvt_atf_driver = {
	.probe		= nvt_atf_probe,
	.remove		= nvt_atf_remove,
	.driver = {
		.name	= "nvt,atf",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(nvt_atf_dt_match),
	},
};

int nvt_atf_init(void)
{
	return platform_driver_register(&nvt_atf_driver);
}
subsys_initcall(nvt_atf_init);

static void __exit nvt_atf_exit(void)
{
	platform_driver_unregister(&nvt_atf_driver);
}
module_exit(nvt_atf_exit);
