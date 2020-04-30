#include <linux/module.h>
#include <linux/spinlock.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#include <linux/types.h>
#include <asm/atomic.h>
#include <mach/clk.h>
#include <linux/delay.h>

static unsigned long arm_clk, ahb_clk, mmc_clk, axi_clk;

void __iomem *clk_reg_base;
EXPORT_SYMBOL(clk_reg_base);
void __iomem *mpll_reg_base;
EXPORT_SYMBOL(mpll_reg_base);
void __iomem *apll_reg_base;
void __iomem *apll_page_en;

#define NVT_SYS_CLK_INFO(fmt, args...) pr_info(fmt, ## args)
#define NVT_SYS_CLK_ERR(fmt, args...) pr_err("clk " fmt, ## args)

/* RST_DIST = (Distance_of_Enable_Disable_Reset / sizeof(u32)) */
#if defined(CONFIG_ARCH_NVT72673)
#define RST_DIST	24	/* RST_DIST = 0x60 / sizeof(u32) */
#define APLL_EN_VALUE	0x9
#else
#define RST_DIST	1	/* RST_DIST = 0x4 / sizeof(u32) */
#define APLL_EN_VALUE	0x1
#endif

#define CONFIG_NVT_SYS_CLK_DEBUG 0
#undef NVT_SYS_CLK_DBG
#if CONFIG_NVT_SYS_CLK_DEBUG
#define NVT_SYS_CLK_DBG(fmt, args...) pr_debug("clk " fmt, ## args)
#else
#define NVT_SYS_CLK_DBG(fmt, args...)
#endif

#define _AHB_CLK_SEL ((readl((u32 *)((u32)clk_reg_base + 0x30)) >> 5) & 0x3)
#define _AXI_CLK_SEL ((readl((u32 *)((u32)clk_reg_base + 0x30)) >> 8) & 0x3)
#define _PERI_CLK_SEL ((readl((u32 *)((u32)clk_reg_base + 0xf4)) >> 5) & 0x7)

#define _MPLL_GetData(u8Offset) \
({	\
readl((u32 *)((u32)mpll_reg_base + ((u8Offset) * 4))); \
})

#define _MPLL_SetData(u8Offset, u8Val) \
({	\
writel(u8Val, (u32 *)((u32)mpll_reg_base + ((u8Offset) * 4))); \
})

#define _APLL_GetData(u8Offset) \
({	\
readl((u32 *)((u32)apll_reg_base + ((u8Offset) * 4))); \
})

#define _MPLL_EnablePageB() \
({	\
writel(((0x1 << EN_SYS_CLK_PAGE_SEL_B) | (0x1 << EN_SYS_CLK_PAGE_SEL_A)), \
		(u32 *)((u32)clk_reg_base + 0xbc)); \
})

#define _MPLL_EnablePage0() \
({	\
writel(((0x1 << EN_SYS_CLK_PAGE_SEL_0) | (0x1 << EN_SYS_CLK_PAGE_SEL_A)), \
		(u32 *)((u32)clk_reg_base + 0xbc)); \
})

#define _MPLL_EnablePageA() \
({	\
writel((0x1 << EN_SYS_CLK_PAGE_SEL_A), \
		(u32 *)((u32)clk_reg_base + 0xbc)); \
})

#define _APLL_EnablePage0() writel(APLL_EN_VALUE, apll_page_en);

#define _APLL_EnablePageB() \
	({	   \
	 (*((volatile unsigned long *)((unsigned long)apll_page_en))) = 0b10; \
	 })

#define IS_REAL_CHIP (_MPLL_GetData((EN_MPLL_OFF_ARM + 2)))

static void sys_set_bit(int nr, u32 *addr)
{
	u32 mask;

	addr += nr >> 5;
	mask = (1 << (nr & 0x1f));
	*addr |= mask;
};

static void sys_clear_bit(int nr, u32 *addr)
{
	u32 mask;

	addr += nr >> 5;
	mask = (1 << (nr & 0x1f));
	*addr &= ~mask;
};

DEFINE_RAW_SPINLOCK(clock_gen_lock);
EXPORT_SYMBOL(clock_gen_lock);

static void compute_mmc_clk(void);
static void compute_ahb_clk(void);
static void compute_axi_clk(void);
static void compute_cpu_clk(void);

struct ST_SYS_CLK_RST {
	u32 u32RegOff;
	u32 u32Mask;
};

struct ST_SYS_CLK_SRC_SEL {
	u32 u32RegOff;
	u32 u32FieldSize;
	u32 u32FieldPos;
};

const struct ST_SYS_CLK_RST stSYSClkRst[] = {
#if defined(CONFIG_ARCH_NVT72673)
	/* Parition A, AHB clock reset disable/enable (0x2000/0x2060) */
	/* EN_SYS_CLK_RST_AHB_USB20_U0,			bit[8]	*/
	{0x2000, (1UL << 8)},
	/* EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,	bit[9]	*/
	{0x2000, (1UL << 9)},
	/* EN_SYS_CLK_RST_AHB_USB20_U1,			bit[10]	*/
	{0x2000, (1UL << 10)},
	/* EN_SYS_CLK_RST_AHB_USB20_U1_PCLK,	bit[11]	*/
	{0x2000, (1UL << 11)},

	/* Partition C, core clock reset disable/enable (0x2008/0x2068) */
	/* EN_SYS_CLK_RST_AHB_USB30_PCLK,		bit[14] */
	{0x2008, (1UL << 14)},
	/* EN_SYS_CLK_RST_AHB_USB20_U3,			bit[19]	*/
	{0x2008, (1UL << 19)},
	/* EN_SYS_CLK_RST_AHB_USB20_U3_PCLK,	bit[20] */
	{0x2008, (1UL << 20)},
	/* EN_SYS_CLK_RST_AHB_USB20_U4,			bit[21] */
	{0x2008, (1UL << 21)},
	/* EN_SYS_CLK_RST_AHB_USB20_U4_PCLK,	bit[22] */
	{0x2008, (1UL << 22)},
	/* EN_SYS_CLK_RST_AXI_USB20_U3,			bit[24] */
	{0x2008, (1UL << 24)},
	/* EN_SYS_CLK_RST_AXI_USB20_U4,			bit[25] */
	{0x2008, (1UL << 25)},
	/* EN_SYS_CLK_RST_CORE_USB20_U3,		bit[27] */
	{0x2008,	(1UL << 27)},
	/* EN_SYS_CLK_RST_CORE_USB20_U4,		bit[28] */
	{0x2008,	(1UL << 28)},

	/* Partition A, AXI clock reset disable/enable (0x2100/0x2160) */
	/* EN_SYS_CLK_RST_AXI_USB20_U0,			bit[6] */
	{0x2100,	(1UL << 6)},
	/* EN_SYS_CLK_RST_AXI_USB20_U1,			bit[7] */
	{0x2100,	(1UL << 7)},

	/* Partition C, AXI clock reset disable/enable (0x2108/0x2168) */
	/* EN_SYS_CLK_RST_AXI_USB30,			bit[27] */
	{0x2108, (1UL << 27)},

	/* Parition A, core clock reset disable/enable (0x2200/0x2260) */
	/* EN_SYS_CLK_RST_CORE_USB20_U0,		bit[8] */
	{0x2200,	(1UL << 8)},
	/* EN_SYS_CLK_RST_CORE_USB20_U1,		bit[9] */
	{0x2200,	(1UL << 9)},

	/* Partition C, AHB clock reset disable/enable (0x2208/0x2268) */
	/* EN_SYS_CLK_RST_AHB_USB30,			bit[31] */
	{0x2208, (1UL << 31)},
#else
	/* Parition A, core clock reset disable/enable (0x50/0x54) */
	/* EN_SYS_CLK_RST_CORE_USB20_U0,			bit[8] */
	{0x50,	(1UL << 8)},
	/* EN_SYS_CLK_RST_CORE_USB20_U1,			bit[9] */
	{0x50,	(1UL << 9)},

	/* Partition C, core clock reset disable/enable (0x60/0x64) */
	/* EN_SYS_CLK_RST_CORE_USB20_U2,			bit[4] */
	{0x60,	(1UL << 4)},
	/* EN_SYS_CLK_RST_CORE_USB20_U3,			bit[5] */
	{0x60,	(1UL << 5)},

	/* Partition A, AXI clock reset disable/enable (0x70/0x74) */
	/* EN_SYS_CLK_RST_AXI_USB20_U0,			bit[6] */
	{0x70,	(1UL << 6)},
	/* EN_SYS_CLK_RST_AXI_USB20_U1,			bit[7] */
	{0x70,	(1UL << 7)},

	/* Partition C, core clock reset disable/enable (0x80/0x84) */
	/* EN_SYS_CLK_RST_AXI_USB20_U2,			bit[2] */
	{0x80, (1UL << 2)},
	/* EN_SYS_CLK_RST_AXI_USB20_U3,			bit[3] */
	{0x80, (1UL << 3)},

	/* Parition A, AHB clock reset disable/enable (0x90/0x94) */
	/* EN_SYS_CLK_RST_AHB_USB20_U0,			bit[8]	*/
	{0x90, (1UL << 8)},
	/* EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,	bit[9]	*/
	{0x90, (1UL << 9)},
	/* EN_SYS_CLK_RST_AHB_USB20_U1,			bit[10]	*/
	{0x90, (1UL << 10)},
	/* EN_SYS_CLK_RST_AHB_USB20_U1_PCLK,	bit[11]	*/
	{0x90, (1UL << 11)},

	/* Partition C, AHB clock reset disable/enable (0xA0/0xA4) */
	/* EN_SYS_CLK_RST_AHB_USB20_U2,			bit[10]	*/
	{0xA0, (1UL << 10)},
	/* EN_SYS_CLK_RST_AHB_USB20_U3,			bit[11] */
	{0xA0, (1UL << 11)},
	/* EN_SYS_CLK_RST_AHB_USB20_U2_PCLK,	bit[12] */
	{0xA0, (1UL << 12)},
	/* EN_SYS_CLK_RST_AHB_USB20_U3_PCLK,	bit[13] */
	{0xA0, (1UL << 13)},
#endif
};

const struct ST_SYS_CLK_SRC_SEL stKerClkSrcSel[] = {
	/* u32RegOff, u32FieldSize, u32FieldPos */
	/* Engine clock select (0x30) */
	/* EN_SYS_CLK_SRC_AHB,				bit[6:5] */
	{0x30,	2,	5},
	/* EN_SYS_CLK_SRC_AXI,				bit[9:8] */
	{0x30,	2,	8},

	/* Engine clock select (0x38) */
	/* EN_SYS_CLK_SRC_USB_20PHY_12M,	bit[6:5] */
	{0x38,	2,	5},

	/* Engine clock select (0x48) */
	/* EN_SYS_CLK_SRC_USB_U2_U3_30M,	bit[22]	*/
	{0x48,	1,	22},
	/* EN_SYS_CLK_SRC_USB_U0_U1_30M,	bit[27]	*/
	{0x48,	1,	27},
	/* EN_SYS_CLK_SRC_USB30,			bit[30] */
	{0x48,	1,	30},
};

/**
 * @brief Get System PLL by Offset
 *
 * @param [in] off	Offset
 * @return Frequency of PLL (MHz)
 */
unsigned long sys_clk_getmpll(unsigned long off)
{
	unsigned long retVal = 0;
	unsigned long flags;

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("func %s\n", __func__);
#endif

	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	/* Select page and set MPLL in crirical section */

	_MPLL_EnablePageB();

	retVal = (_MPLL_GetData(off));
	retVal |= (_MPLL_GetData(off + 1) << 8);
	retVal |= (_MPLL_GetData(off + 2) << 16);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

	retVal *= 12;
	retVal += ((1UL << 17) - 1);
	retVal >>= 17;

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("\toffset 0x%02x\n", (int)off);
	NVT_SYS_CLK_DBG("\tVal[0]: 0x%08x\n",  _MPLL_GetData((off + 0)));
	NVT_SYS_CLK_DBG("\tVal[1]: 0x%08x\n",  _MPLL_GetData((off + 1)));
	NVT_SYS_CLK_DBG("\tVal[2]: 0x%08x\n",  _MPLL_GetData((off + 2)));
#endif

	return retVal;
}
EXPORT_SYMBOL(sys_clk_getmpll);

/**
 * @brief Set Value to System PLL by Offset
 *
 * @param [in] off	Offset
 * @param [in] freq	Frequency (MHz)
 * @return Value of PLL
 */
unsigned long SYS_CLK_SetMpll(unsigned long off, unsigned long freq)
{
	unsigned long flags;

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
#endif

	freq <<= 17;
	freq += (12 - 1);
	freq /= 12;

	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	/* Select page and set MPLL in crirical section */
	_MPLL_EnablePageB();

	_MPLL_SetData(off + 0, ((freq >>  0) & 0xff));
	_MPLL_SetData(off + 1, ((freq >>  8) & 0xff));
	_MPLL_SetData(off + 2, ((freq >> 16) & 0xff));

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("\toffset 0x%02x, value 0x%08x\n", (int)off, (int)freq);
	NVT_SYS_CLK_DBG("\tVal[0]: 0x%08x\n", _MPLL_GetData((off + 0)));
	NVT_SYS_CLK_DBG("\tVal[1]: 0x%08x\n", _MPLL_GetData((off + 1)));
	NVT_SYS_CLK_DBG("\tVal[2]: 0x%08x\n", _MPLL_GetData((off + 2)));
#endif

	return freq;
}
EXPORT_SYMBOL(SYS_CLK_SetMpll);

#ifdef CONFIG_DEBUG_FS
/*
 * Read MPLL value for debugging
 */
static int mpll_debug_show(struct seq_file *m, void *v)
{
	unsigned long mpll_clk = 0;
	/**
	 * Get ratio of ARM PLL
	 */
	mpll_clk = sys_clk_getmpll(EN_MPLL_OFF_ARM);

	seq_printf(m, "%lu\n", mpll_clk);

	return 0;
}

static int mpll_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mpll_debug_show, NULL);
}

static const struct file_operations mpll_debug_fops = {
	.open		= mpll_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Read ref value for judgement
 */
static int ref_debug_show(struct seq_file *m, void *v)
{

	/* Give a reference value for judgement*/
	seq_printf(m, "137\n");

	return 0;
}

static int ref_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, ref_debug_show, NULL);
}

static const struct file_operations ref_debug_fops = {
	.open		= ref_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Read maxdiff value for judgement
 */
static int maxdiff_debug_show(struct seq_file *m, void *v)
{

	/* Give a tolerance value for judgement */
	seq_printf(m, "5\n");

	return 0;
}

static int maxdiff_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, maxdiff_debug_show, NULL);
}

static const struct file_operations maxdiff_debug_fops = {
	.open		= maxdiff_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void nvt_pll_init_debugfs(void)
{
	struct dentry *pll_d, *monitor_d, *mpll_d;
	struct dentry *measure_f, *ref_f, *maxdiff_f;

	/* Create /sys/kernel/debug/pll/monitor/mpll/measure for reading MPLL */
	pll_d = debugfs_create_dir("pll", NULL);
	monitor_d = debugfs_create_dir("monitor", pll_d);
	mpll_d = debugfs_create_dir("mpll", monitor_d);
	if (!mpll_d) {
		pr_warning("Can't create pll/monitor/mpll\n");
		return;
	}

	measure_f = debugfs_create_file("measure", S_IRUGO,
					mpll_d, NULL, &mpll_debug_fops);
	if (!measure_f) {
		pr_warning("Can't create meansure\n");
		return;
	}
	
	ref_f = debugfs_create_file("ref", S_IRUGO,
					mpll_d, NULL, &ref_debug_fops);
	if (!ref_f) {
		pr_warning("Can't create ref\n");
		return;
	}
	
	maxdiff_f = debugfs_create_file("maxdiff", S_IRUGO,
					mpll_d, NULL, &maxdiff_debug_fops);
	if (!maxdiff_f) {
		pr_warning("Can't create maxdiff\n");
		return;
	}
}

static int __init nvt_pll_debug_init(void)
{
	int error = 0;

	nvt_pll_init_debugfs();
	return error;
}

__initcall(nvt_pll_debug_init);

#endif /* CONFIG_DEBUG_FS */

/**
 * Initialize clock
 *
 * Get base address of clkgen, MPLL and APLL
 * Get frequency of ARM, AHB and EMMC
 */
static void __init nvt_clk_init(struct device_node *node)
{
	arm_clk = 0;
	ahb_clk = 0;
	mmc_clk = 0;
	axi_clk = 0;

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
	NVT_SYS_CLK_DBG("nvt clkgen initialization\n");
#endif

	if (node) {
		clk_reg_base = of_iomap(node, 0);
		mpll_reg_base = of_iomap(node, 1);
		apll_reg_base = of_iomap(node, 2);
		apll_page_en = of_iomap(node, 3);

		of_node_put(node);
	} else {
#if CONFIG_NVT_SYS_CLK_DEBUG
		NVT_SYS_CLK_DBG("can't find node\n");
#endif

		return;
	}

	_MPLL_EnablePageB();

	compute_cpu_clk();
	compute_ahb_clk();
	compute_axi_clk();
	compute_mmc_clk();

	pr_info("cpu clk = %4dMHz\n", (int)(arm_clk / 1000000));
	pr_info("ahb clk = %4dMHz\n", (int)(ahb_clk / 1000000));
	pr_info("axi clk = %4dMHz\n", (int)(axi_clk / 1000000));
	pr_info("mmc clk = %4dMHz\n", (int)(mmc_clk / 1000000));
}

CLK_OF_DECLARE(nvt72668_clkgen, "nvt,clkgen", nvt_clk_init);

static void __init nvt_periph_clk_init(struct device_node *node)
{
	struct clk *periph_clk;

	periph_clk = clk_register_fixed_rate(NULL,
			"periph_clk",
			NULL,
			CLK_IS_ROOT,
			get_periph_clk());
	clk_register_clkdev(periph_clk, NULL, "periph_clk_dev");
	of_clk_add_provider(node, of_clk_src_simple_get, periph_clk);
}

CLK_OF_DECLARE(nvt_periph_clk, "nvt,periph_clk", nvt_periph_clk_init);

static void __init nvt_ahb_clk_init(struct device_node *node)
{
	struct clk *ahb_clk;

	ahb_clk = clk_register_fixed_rate(NULL, "ahb_clk", NULL, CLK_IS_ROOT,
			get_ahb_clk());
	clk_register_clkdev(ahb_clk, NULL, "ahb_clk_dev");
	of_clk_add_provider(node, of_clk_src_simple_get, ahb_clk);
}

CLK_OF_DECLARE(nvt_ahb_clk, "nvt,ahb_clk", nvt_ahb_clk_init);

/**
 * Get frequency of peripheral clock
 */
unsigned long get_periph_clk(void)
{
	return (arm_clk/(_PERI_CLK_SEL + 1));
}

/**
 * Get frequency of AHB clock
 */
unsigned long get_ahb_clk(void)
{
	return ahb_clk;
}
EXPORT_SYMBOL(get_ahb_clk);

/**
 * Get frequency of AXI clock
 */
unsigned long get_axi_clk(void)
{
	return axi_clk;
}
EXPORT_SYMBOL(get_axi_clk);

/**
 * Get frequency of CPU clock
 */
unsigned long get_cpu_clk(void)
{
	return arm_clk;
}
EXPORT_SYMBOL(get_cpu_clk);

/**
 * Get frequency of MMC clock
 */
unsigned long get_mmc_clk(void)
{
	return mmc_clk;
}
EXPORT_SYMBOL(get_mmc_clk);

/**
 * @fn void SYS_CLK_SetClockSource(EN_SYS_CLK_SRC enSrc, u32 u32Src)
 *
 * @brief  Set clock source for specific top or engine
 *
 * @param[in]  enSrc	Indicate the clock source which will be changed.
 * @param[in]  u32Src	New clock source value
 *
 */
void SYS_CLK_SetClockSource(enum EN_SYS_CLK_SRC enSrc, u32 u32Src)
{
	unsigned long flags;
	u32 u32RegVal;
	u32 u32RegOff = stKerClkSrcSel[(int)enSrc].u32RegOff;
	u32 u32FieldSize = stKerClkSrcSel[(int)enSrc].u32FieldSize;
	u32 u32FieldPos = stKerClkSrcSel[(int)enSrc].u32FieldPos;
	u32 *off = (u32 *)((u32)clk_reg_base + u32RegOff);

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s, SrcReg(%p) = 0x%08x, pos = %d, size = %d, src = %d\n",
						 __func__, off, readl(off), u32FieldPos, u32FieldSize, u32Src);
#endif

	if (u32Src >= (1UL << u32FieldSize))
		return;

	/**
	 * Critical section start
	 */
	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	u32RegVal = readl(off);
	u32RegVal &= ~(((1UL << u32FieldSize) - 1) << u32FieldPos);
	u32RegVal |= (u32Src << u32FieldPos);
	writel(u32RegVal, off);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	/**
	 * Critical section end
	 */

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("New: SrcReg(%p) = 0x%08x\n", off, readl(off));
#endif
}
EXPORT_SYMBOL(SYS_CLK_SetClockSource);

/**
 * @fn void SYS_SetClockReset(EN_SYS_CLK_RST enRst, bool b8EnRst)
 *
 * @brief Clock reset enable/disable
 *
 * @param[in] enRst		Clock reset which will be enable or disable
 * @param[in] b8EnRst	TRUE: enable clock reset, FALSE: disable clock reset
 *
 */
void SYS_SetClockReset(enum EN_SYS_CLK_RST enRst, bool b8EnRst)
{
	unsigned long flags;
	u32 u32RegOff = stSYSClkRst[(int)enRst].u32RegOff;
	u32 u32Mask = stSYSClkRst[(int)enRst].u32Mask;
	u32 *off = (u32 *)((u32)clk_reg_base + u32RegOff);

#if CONFIG_NVT_SYS_CLK_DEBUG
	if (b8EnRst) /* Reset enable */
		NVT_SYS_CLK_DBG("Func %s(enable), RstReg(%p) = 0x%08x, Mask = 0x%08x\n",
						 __func__, off+RST_DIST, readl(off+RST_DIST), u32Mask);
	else /* Reset disable */
		NVT_SYS_CLK_DBG("Func %s(disable), RstReg(%p) = 0x%08x, Mask = 0x%08x\n",
						 __func__, off, readl(off), u32Mask);
#endif

	/**
	 * Critical section start
	 */
	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	if (b8EnRst) /* Reset enable */
		writel(u32Mask, off + RST_DIST);
	else /* Reset disable */
		writel(u32Mask, off);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	/**
	 * Critical section end
	 */

#if CONFIG_NVT_SYS_CLK_DEBUG
	if (b8EnRst) /* Reset enable */
		NVT_SYS_CLK_DBG("New: RstReg(%p) = 0x%08x\n", off+RST_DIST, readl(off+RST_DIST));
	else /* Reset disable */
		NVT_SYS_CLK_DBG("New: RstReg(%p) = 0x%08x\n", off, readl(off));
#endif
}
EXPORT_SYMBOL(SYS_SetClockReset);

/**
 * @fn void SYS_CLK_SetClockInv(EN_SYS_CLK_INV enInv, bool b8EnInv)
 *
 * @brief Clock inverse enable/disable
 *
 * @param[in] enInv		Clock inverse which will be enable or disable
 * @param[in] b8EnInv	TRUE: clock inverse enable, FALSE clock inversse disable
 *
 */
void SYS_SetClockInv(enum EN_SYS_CLK_INV enInv, bool b8EnInv)
{
	unsigned long flags;
#if CONFIG_NVT_SYS_CLK_DEBUG
	u32 *off = (u32 *)(((enInv >> 5) << 2) + clk_reg_base);
	NVT_SYS_CLK_DBG("Func %s, InvReg(%p) = 0x%08x, Bit[%d], b8EnInv = %d\n",
					 __func__, off, readl(off), (enInv&0x1f), b8EnInv);
#endif

	if (b8EnInv){
		/* Critical section start */
		raw_spin_lock_irqsave(&clock_gen_lock, flags);

		sys_set_bit(enInv, (void *)(clk_reg_base));

		raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
		/* Critical section end */

	} else {
		/* Critical section start */
		raw_spin_lock_irqsave(&clock_gen_lock, flags);

		sys_clear_bit(enInv, (void *)(clk_reg_base));

		raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
		/* Critical section end */
	}

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("New: InvReg(%p) = 0x%08x\n", off, readl(off));
#endif
}
EXPORT_SYMBOL(SYS_SetClockInv);

/**
 * @fn void SYS_SetClockMask(EN_SYS_CLK_MASK enMask, bool b8EnMask)
 *
 * @brief Clock mask enable/disable
 *
 * @param[in] enMask	Clock mask which will be enable or disable
 * @param[in] b8EnMask	TRUE: clock mask enable, FALSE clock mask disable
 *
 */
void SYS_SetClockMask(enum EN_SYS_CLK_MASK enMask, bool b8EnMask)
{
	unsigned long flags;
#if CONFIG_NVT_SYS_CLK_DEBUG
	u32 *off = (u32 *)(((enMask >> 5) << 2) + clk_reg_base);
	NVT_SYS_CLK_DBG("Func %s, MaskReg(%p) = 0x%08x, Bit[%d], b8EnMask = %d\n",
					 __func__, off, readl(off), (enMask&0x1f), b8EnMask);
#endif

	if (b8EnMask){
		/* Critical section start */
		raw_spin_lock_irqsave(&clock_gen_lock, flags);

		sys_set_bit(enMask, (void *)(clk_reg_base));

		raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
		/* Critical section end */

	} else {
		/* Critical section start */
		raw_spin_lock_irqsave(&clock_gen_lock, flags);

		sys_clear_bit(enMask, (void *)(clk_reg_base));

		raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
		/* Critical section end */
	}

#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("New: MaskReg(%p) = 0x%08x\n", off, readl(off));
#endif
}
EXPORT_SYMBOL(SYS_SetClockMask);


/**
*	Setup USB 3.0 Spread Spectrum Clock circuit.
*	Because compatibility concern, SSC function is disabled in default.
*/

void SYS_set_u3_ssc(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	_MPLL_EnablePage0();
	udelay(0x20);

	/* ssc setting */
	_MPLL_SetData(0x36, 0xbb);
	_MPLL_SetData(0x35, 0x3b);
	_MPLL_SetData(0x38, 0x4);
	udelay(0x20);

	_MPLL_SetData(0x35, 0x33);
	udelay(0x20);

	/*  ssc setting fine tune, ssc_en : 0 */
	_MPLL_SetData(0x35, 0x31);
	udelay(0x20);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

}
EXPORT_SYMBOL(SYS_set_u3_ssc);


static void compute_ahb_clk(void)
{
#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
#endif
	if (_MPLL_GetData(0x64)) {
		/**
		 * Real Chip
		 */
		switch (_AHB_CLK_SEL) {
		case EN_SYS_AHB_CLK_SRC_REF_96M:	/* OSC16X/2 */
			ahb_clk = 96000000;

			break;

		case EN_SYS_AHB_CLK_SRC_ARM_D8:		/* ARM_D8CK */
			ahb_clk = (arm_clk / 8);

			break;

		case EN_SYS_AHB_CLK_SRC_AHB:		/* AHB_CK */
		default:
			ahb_clk = sys_clk_getmpll(EN_MPLL_OFF_AHB);
			ahb_clk *= 1000000;

			break;
		}
	} else {
		ahb_clk = 12000000;
	}
}

static void compute_axi_clk(void)
{
#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
#endif
	if (_MPLL_GetData(0x64)) {
		/**
		 * Real Chip
		 */
		switch (_AXI_CLK_SEL) {
		case EN_SYS_AXI_CLK_SRC_ARM_D8:	/* ARM_D8CK */
			axi_clk = (arm_clk / 8);
		break;

		case EN_SYS_AXI_CLK_SRC_DDR_D2:	/* DDR_D2CK */
			axi_clk = (sys_clk_getmpll(EN_MPLL_OFF_DDR) / 2);
			axi_clk *= 1000000;
			break;

		case EN_SYS_AXI_CLK_SRC_AXI_D2:	/* AXI_CLK/2 */
		case EN_SYS_AXI_CLK_SRC_AXI:	/* AXI_CLK */
			axi_clk =
				(sys_clk_getmpll(EN_MPLL_OFF_AXI) /
				 (4 - _AXI_CLK_SEL));
			axi_clk *= 1000000;
			break;

		default:
			NVT_SYS_CLK_ERR("Invalid AXI clock selection\n");
			break;
		}
	} else {
		axi_clk = 27000000;
	}
}

static void compute_mmc_clk(void)
{
#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
#endif

	/* Real Chip */
	if (_MPLL_GetData(0x64)) {
		mmc_clk = sys_clk_getmpll(EN_MPLL_OFF_EMMC);
		mmc_clk *= 1000000;
		mmc_clk *= 4;
	} else {
		mmc_clk = 12000000;
	}
}

static void compute_cpu_clk(void)
{
#if CONFIG_NVT_SYS_CLK_DEBUG
	NVT_SYS_CLK_DBG("Func %s\n", __func__);
#endif

	/* Real Chip */
	if (_MPLL_GetData(0x64)) {
		/**
		 * Get ratio of ARM PLL
		 */
		arm_clk = sys_clk_getmpll(EN_MPLL_OFF_ARM);

		/**
		 * Check MUX
		 */
		if ((_APLL_GetData(0x00) & 0x1)) {	/* Select local PLL */
			NVT_SYS_CLK_INFO("\tSelect local PLL\n");
			arm_clk *= 8;
		} else {
			NVT_SYS_CLK_INFO("\tSelect MPLL\n");
		}

		arm_clk *= 1000000;
	} else {
		arm_clk = 55000000;
	}
}

void change_arm_clock(u32 rate)
{
	u32 val, loop;
	unsigned long flags;
	u32 set_freq = rate;

	loop = 0;
	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	_APLL_EnablePage0();

	/* switch ARMPLL's CKMUX from 8x to 1x */
	writel_relaxed(0x2, apll_reg_base);

	_MPLL_EnablePageB();

/*	SYS_CLK_SetMpll(EN_MPLL_OFF_ARM, rate); */
#if defined(CONFIG_ARCH_NVT72172) || defined(CONFIG_ARCH_NVT72673)
	if (set_freq <= 800) {
		rate <<= 17; // NVT72172, we needn't divide 8 if rate is lower 800MHz. 
	} else {
		rate <<= (17-3); // -3 means we divide 8
	}
#else
	rate <<= (17-3); // -3 means we divide 8
#endif
	rate /= 12;

/*
	_MPLL_SetData(off + 0, ((freq >>  0) & 0xff));
	_MPLL_SetData(off + 1, ((freq >>  8) & 0xff));
	_MPLL_SetData(off + 2, ((freq >> 16) & 0xff));
*/

	writel_relaxed((rate >>  0) & 0xff, mpll_reg_base +
					(EN_MPLL_OFF_ARM * 4));
	writel_relaxed((rate >>  8) & 0xff, mpll_reg_base +
					(EN_MPLL_OFF_ARM * 4) + 4);
	writel_relaxed((rate >>  16) & 0xff, mpll_reg_base +
					(EN_MPLL_OFF_ARM * 4) + 8);


	udelay(2);

	while (1) {
		val = readl_relaxed(mpll_reg_base + 0xd0);
		if ((val & 0x60) == 0x60)
			break;
		udelay(20);
		loop++;
	}
	pr_info("[CLOCK] pass %d\n", loop);

	_APLL_EnablePage0();

#if defined(CONFIG_ARCH_NVT72172) || defined(CONFIG_ARCH_NVT72673)
	writel_relaxed(0x2, apll_reg_base);
	if (set_freq > 800) {
		writel_relaxed(0x3, apll_reg_base);
	}
#else
	writel_relaxed(0x2, apll_reg_base);
	writel_relaxed(0x3, apll_reg_base);
#endif
	/* put memory barrier*/
	wmb();
	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

	compute_cpu_clk(); //For updating the newest cpu clk to arm_clk; 
}
EXPORT_SYMBOL_GPL(change_arm_clock);
