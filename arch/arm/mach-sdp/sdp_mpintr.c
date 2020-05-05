/*************************************8********************************************************
 *
 *	sdp1202_mpintr.c (FoxMP Interrupt Controller Driver for DualMP)
 *
 *	author : seungjun.heo@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
	Sep,10,2012 	seungjun.heo	created
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <asm/uaccess.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#define MPISR_REG_STAT_CLEAR	0x0
#define MPISR_REG_MASK		0x4
#define MPISR_REG_MASKED_STAT	0x8
#define MPISR_REG_POLARITY	0xC
#define MPISR_REG_TYPE		0x10
#define	MPISR_REG_STMMODE	0x14

#define MPISR_IRQ_TSD	0
#define MPISR_IRQ_AIO	1
#define MPISR_IRQ_AE	2
#define MPISR_IRQ_JPEG	3
#define MPISR_IRQ_DP	4
#define MPISR_IRQ_GA	5
#define MPISR_IRQ_MFD	6

#define DRIVER_MPISR_NAME		"sdp_mpisr"

int sdp_mpint_enable(int n_mpirq);
int sdp_mpint_disable(int n_mpirq);
int sdp_mpint_request_irq(int n_mpirq, void (*fp)(void*), void* args);

//#define	IRQ_MPISR	31
#define	IRQ_MPISR	IRQ_EXT6

#define NR_MP_INTR	32

struct sdp_mpisr_isr_t {
	void * 	args;
	void (*fp)(void*);
};

static struct sdp_mpisr_t {
	void *base;
	u32 nr_irqs;
	u32 irq_mpisr;
	spinlock_t lock;
	struct sdp_mpisr_isr_t *handler;
} sdp_mpisr;

static void __init sdp_mpisr_preinit(void)	//mask 설정 안하는 이유 : 기본 셋팅이 masking
{
	writel(0xFFFFFFFF, sdp_mpisr.base + MPISR_REG_STAT_CLEAR);	// all clear
}

int sdp_mpint_enable(int n_mpirq)
{
	unsigned long flags, val;

	if(n_mpirq >= NR_MP_INTR){
		return -1;
	}

	spin_lock_irqsave(&sdp_mpisr.lock, flags);
	
	val = readl(sdp_mpisr.base + MPISR_REG_MASK);
	val &= ~(1UL << n_mpirq);
	writel(val, sdp_mpisr.base + MPISR_REG_MASK);	//enable
	writel(1 << n_mpirq, sdp_mpisr.base + MPISR_REG_STAT_CLEAR);	//clear

	spin_unlock_irqrestore(&sdp_mpisr.lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_mpint_enable);

int sdp_mpint_disable(int n_mpirq)
{
	unsigned long flags, val;

	if(n_mpirq >= NR_MP_INTR){
		return -1;
	}

	spin_lock_irqsave(&sdp_mpisr.lock, flags);
	
	val = readl(sdp_mpisr.base + MPISR_REG_MASK);
	val |= (1UL << n_mpirq);
	writel(val, sdp_mpisr.base + MPISR_REG_MASK);	//disable
	writel(1 << n_mpirq, sdp_mpisr.base + MPISR_REG_STAT_CLEAR);	//clear

	spin_unlock_irqrestore(&sdp_mpisr.lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_mpint_disable);

int sdp_mpint_request_irq(int n_mpirq, void (*fp)(void*), void* args)
{
	int retval = 0;

	if(sdp_mpisr.base == 0)
	{
		printk(KERN_ERR"Cannot Register Interrupt!!! MP1 Interrupt is not initialized!!\n");
		return -1;
	}
	
	if(n_mpirq >= sdp_mpisr.nr_irqs) {
		return -1;
	}


	if(sdp_mpisr.handler[n_mpirq].fp) {
		printk(KERN_ERR"[%s] %d sub ISR slot not empty\n", DRIVER_MPISR_NAME, n_mpirq);
		retval = -1;
		goto __out_register;
	}
		
	sdp_mpisr.handler[n_mpirq].fp = fp;
	sdp_mpisr.handler[n_mpirq].args = args;

	printk(KERN_INFO"[%s] %d sub ISR is registered successfully\n", DRIVER_MPISR_NAME, n_mpirq);

	sdp_mpint_enable(n_mpirq);

__out_register:

	return retval;
}
EXPORT_SYMBOL(sdp_mpint_request_irq);

static void call_mpint_fp (int n_mpirq)
{
	struct sdp_mpisr_isr_t* p_mpint;

	p_mpint = &sdp_mpisr.handler[n_mpirq];

	if(p_mpint->fp) {
		p_mpint->fp(p_mpint->args);
	}
	else {
		printk(KERN_ERR"[%s] %d sub not exist ISR\n", DRIVER_MPISR_NAME, n_mpirq);
		printk(KERN_ERR"[%s] %d sub is disabled\n", DRIVER_MPISR_NAME, n_mpirq);
		sdp_mpint_disable(n_mpirq);
	}
}

static irqreturn_t sdp_mpisr_isr(int irq, void* devid)
{
	int idx;
	int n_mpirq = 0;	
	u32 status;

#if 0	
	switch(irq){
		case IRQ_MPISR:
			status = readl(sdp_mpisr.base + MPISR_REG_MASKED_STAT);
			break;
		default:
			printk(KERN_ERR"[%s] %d Not registered interrupt source \n", DRIVER_MPISR_NAME,irq);
			return IRQ_NONE;
			break;
	}
#else
	status = readl(sdp_mpisr.base + MPISR_REG_MASKED_STAT);
#endif

	if(!status) return IRQ_NONE;
	for(idx = 0; idx < NR_MP_INTR; idx++){
		if(status & (1UL << idx))
		{
			n_mpirq = idx;
			call_mpint_fp(n_mpirq);
		}
	}
	writel(status, sdp_mpisr.base + MPISR_REG_STAT_CLEAR);
	return IRQ_HANDLED;
}

#if 0
static u32 dp_regbase;

#define DP_INT_MASK_STAT	0x284
#define DISPOUT_VSYNC_MASK	0x1
#define DISPOUT_VSYNC_STAT	0x10000

static void dpisr(void * args)
{
	unsigned int stat;
	static int cnt = 0;

	stat = readl(dp_regbase + DP_INT_MASK_STAT);
//	mask = stat & 0xFFFF;
	
	if(stat & DISPOUT_VSYNC_STAT)
		cnt++;
	if(cnt % 30 == 0)
		printk("DP Vsync %d occured!!!!\n", cnt);

	writel(stat, dp_regbase + DP_INT_MASK_STAT);
}

void testdpirq(void)
{
	unsigned int stat, mask;

	dp_regbase = (u32) ioremap(0x1A910000, 0x10000);
	
	mask = DISPOUT_VSYNC_MASK;
	stat = DISPOUT_VSYNC_STAT;
	writel(mask | stat, dp_regbase + DP_INT_MASK_STAT);		//DP VSync clear and only enable
	
	sdp_mpint_request_irq(MPISR_IRQ_DP, dpisr, NULL);
}
#endif

static int sdp_mpisr_of_do_initregs(struct device *dev)
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
				writel( (readl(iomem)&~mask) | (val&mask), iomem );
				dev_printk(KERN_DEBUG, dev,
					"of initreg addr 0x%08x, mask 0x%08x, val 0x%08x\n",
					addr, mask, val);
				iounmap(iomem);
			} else {
				return -ENOMEM;
			}
		}
	}
	return 0;
}

static int sdp_mpisr_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret = 0;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		return -ENOENT;
	}

	sdp_mpisr.base = devm_request_and_ioremap(&pdev->dev, res);

	if (!sdp_mpisr.base) {
		dev_err(dev, "ioremap failed\n");
		return -ENODEV;
	}

	if (of_property_read_u32(dev->of_node, "nr-irqs", &sdp_mpisr.nr_irqs))
		sdp_mpisr.nr_irqs = 32;
	
	sdp_mpisr.handler = devm_kzalloc(dev, sizeof(struct sdp_mpisr_isr_t) * sdp_mpisr.nr_irqs, GFP_KERNEL);
	if (!sdp_mpisr.handler) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	sdp_mpisr_of_do_initregs(dev);
	
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;
	}

	sdp_mpisr.irq_mpisr = ret;

	sdp_mpisr_preinit();

	spin_lock_init(&sdp_mpisr.lock);
	ret = request_irq(sdp_mpisr.irq_mpisr, sdp_mpisr_isr, 
				IRQF_DISABLED, DRIVER_MPISR_NAME, (void*) &sdp_mpisr.handler[0]);
	if (ret)
	{
		dev_err(dev, "request_irq failed\n");
		return -ENODEV;
	}

	dev_info(dev, "sdp-mpisr initialized.. IRQ=%d, nirqs=%d\n", sdp_mpisr.irq_mpisr, sdp_mpisr.nr_irqs);

	return 0;
}

static int sdp_mpisr_remove(struct platform_device *pdev)
{
	return 0;
}

static int sdp_mpisr_suspend(struct device *dev)
{
	return 0;
}

static int sdp_mpisr_resume(struct device *dev)
{
	return 0;
}

static const struct of_device_id sdp_mpisr_dt_match[] = {
	{ .compatible = "samsung,sdp-mpisr" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_mpisr_dt_match);

static struct platform_driver sdp_mpisr_driver = {

	.probe		= sdp_mpisr_probe,
	.remove 	= sdp_mpisr_remove,
	.driver 	= {
		.name = DRIVER_MPISR_NAME,
#ifdef CONFIG_OF
		.of_match_table = sdp_mpisr_dt_match,
#endif
	},
};

static int __init sdp_mpisr_init(void)
{
	return platform_driver_register(&sdp_mpisr_driver);
}

static void __exit sdp_mpisr_exit(void)
{
	platform_driver_unregister(&sdp_mpisr_driver);
}

module_init(sdp_mpisr_init);
module_exit(sdp_mpisr_exit);

MODULE_DESCRIPTION("Samsung DTV MP Interrupt controller driver");
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("seungjun.heo <seungjun.heo@samsung.com>");

