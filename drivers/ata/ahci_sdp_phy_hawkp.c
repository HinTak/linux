#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/ahci_sdp.h>
#include <asm/io.h>

#undef PHY_VERBOSE

#if defined(PHY_VERBOSE)
# define phy_dbg(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#else
# define phy_dbg(...)
#endif

struct hawkp_phy {
	struct sdp_sata_phy phy;
	struct clk *clk;		/* GPR SW_RESET */
	struct clk *clk_link;		/* LINK MASK_CLK */
	struct clk *clk_rst_link;	/* LINK SW_RESET */
	u32 __iomem *gpr_regs;
	u32 __iomem *phy_regs;
	int		enabled;
	int		link_enabled;
};

static inline void phy_udelay(unsigned long t)
{
	phy_dbg("udelay(%lu)\n", t);
	udelay(t);
}
static inline void gpr_writel(const struct hawkp_phy *phy, const u32 val, int index)
{
	phy_dbg("gpr+0x%02x := 0x%08x\n", index * 4, val);
	writel(val, &phy->gpr_regs[index]);
	readl(&phy->gpr_regs[index]);	// flush
}
static inline u32 gpr_readl(const struct hawkp_phy *phy, int index)
{
	u32 ret = readl(&phy->gpr_regs[index]);
	phy_dbg("gpr+0x%02x := 0x%08x\n", index * 4, ret);
	return ret;
}
static inline int gpr_wait(const struct hawkp_phy *phy, u32 mask, u32 wait, int index, int timeout)
{
	while (timeout > 0) {
		if ((readl(&phy->gpr_regs[index]) & mask) == wait) {
			phy_dbg("gpr+0x%02x mask 0x%08x == val 0x%08x\n",
					index * 4, mask, wait);
			return 0;
		}
		udelay(10);
		timeout -= 10;
	}
	phy_dbg("gpr_wait(gpr+%02x) fail.\n", index * 4);
	return -ETIMEDOUT;
}
static inline void phy_writel(const struct hawkp_phy *phy, const u32 val, int index)
{
	phy_dbg("phy+0x%02x := 0x%08x\n", index * 4, val);
	writel(val, &phy->phy_regs[index]);
	readl(&phy->phy_regs[index]);	// flush
}
static inline u32 phy_readl(const struct hawkp_phy *phy, int index)
{
	u32 ret;
	ret = readl(&phy->phy_regs[index]);
	phy_dbg("phy+0x%02x := 0x%08x\n", index * 4, ret);
	return ret;
}

extern int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);

static void hawkp_phy_clk_enable(struct hawkp_phy *phy)
{
	dev_info (phy->phy.dev, "BIU reset off.\n");
	if (phy->clk_link) {
		clk_prepare_enable(phy->clk_link);
		udelay(5);
	}
	if (phy->clk_rst_link) {
		clk_prepare_enable(phy->clk_rst_link);
		udelay(5);
	}
}

static void hawkp_phy_clk_disable(struct hawkp_phy *phy)
{
	dev_info (phy->phy.dev, "BIU reset on.\n");

	if (phy->clk_rst_link) {
		clk_disable_unprepare(phy->clk_rst_link);
		udelay(5);
	}
	if (phy->clk_link) {
		clk_disable_unprepare(phy->clk_link);
		udelay(5);
	}
}

static void hawkp_phy_reset_off(void)
{
	sdp_set_clkrst_mux(0x112508b4, (1 << 31), (1 << 31));
}

static void hawkp_link_reset_ctrl(struct hawkp_phy *phy, int reset_on)
{
	if (reset_on && phy->link_enabled) {
		hawkp_phy_clk_disable(phy);
		phy->link_enabled = 0;
	} else if (!reset_on && !phy->link_enabled) {
		hawkp_phy_clk_enable(phy);
		phy->link_enabled = 1;
	}
}

#define GPR0_RX_RATE(x)	(x << 29)
#define GPR0_TX_RATE(x)	(x << 19)

#define GPR0_SLUMBER		(1 << 31)

#define GPR0_TRSV_REG_RESET_OFF	(1 << 16)
#define GPR0_CMN_REG_RESET_OFF	(1 << 15)

#define GPR0_MAC_RESET_OFF	(1 << 7)
#define GPR0_TRSV_RESET_OFF	(1 << 6)
#define GPR0_CMN_RESET_OFF	(1 << 5)
#define GPR0_GLOBAL_RESET_OFF	(1 << 4)

#define GPR0_HIGH_SPEED		(1 << 12)
#define GPR0_REFCLK_SEL		(1 << 10)	/* 1: use internal clock source */
#define GPR0_REFCLK_RATE	(1 << 9)	/* 1: 100Mhz, 0: 25Mhz */

#define GPR0_TX_INV_OFF		(1 << 2)
#define GPR0_MODE_SATA		(1 << 1)

#define to_hawkp_phy(phy)	((struct hawkp_phy*)(phy))

static int limit_gen = 2;
module_param(limit_gen, int, S_IRUGO);
MODULE_PARM_DESC(limit_gen, "[obsolute] limit phy operating speed. gen 1/2/3, default=2,3.0Gpbs");

static int use_extclk = 0;
module_param(use_extclk, int, S_IRUGO);
MODULE_PARM_DESC(use_extclk, "set 1 for using external reference clock");

static int _hawkp_phy_init(struct hawkp_phy *phy)
{
	u32 val;
	int r;

	dev_info (phy->phy.dev, "phy init.\n");

	hawkp_link_reset_ctrl(phy, 1);

	/* force reset on */
	gpr_writel(phy, 0x00000000, 0);
	gpr_writel(phy, 0x00000000, 4);	/* clock domain reset */
	phy_udelay(5);

	/* default value */
	gpr_writel(phy, 0x00000000, 1);
	gpr_writel(phy, 0x00000000, 2);
	gpr_writel(phy, 0x00000000, 3);
	gpr_writel(phy, 0x00000000, 4);
	gpr_writel(phy, 0x00000000, 5);
	gpr_writel(phy, 0x00000000, 6);
	phy_udelay(5);

	gpr_writel(phy, 0x00000002, 6);	/* MAC_RESET_N write enable */
	phy_udelay(5);

	val =
		GPR0_TRSV_REG_RESET_OFF |
		GPR0_CMN_REG_RESET_OFF |

		GPR0_TRSV_RESET_OFF |
		GPR0_CMN_RESET_OFF |
		
		GPR0_REFCLK_RATE |
		GPR0_REFCLK_SEL |
		GPR0_HIGH_SPEED |
		GPR0_MODE_SATA;

	if (use_extclk)
		val &= ~(GPR0_REFCLK_SEL);
	
	gpr_writel(phy, val, 0);

	phy_udelay(5);	/* min := REFCLK * 10 */

	/* release G_RST */
	val |= GPR0_GLOBAL_RESET_OFF;
	gpr_writel(phy, val, 0);
	phy_udelay(5);	/* min := REFCLK * 10 */

	/* PHY common */
	phy_writel(phy, 0x01, 0x01);	// Reg01 Data
	phy_writel(phy, 0x44, 0x02);	// Reg02 Data	// 100MHz
	phy_writel(phy, 0xE1, 0x03);	// Reg03 Data
	phy_writel(phy, 0x35, 0x04);	// Reg04 Data
	phy_writel(phy, 0x29, 0x05);	// Reg05 Data
	phy_writel(phy, 0x83, 0x06);	// Reg06 Data
	phy_writel(phy, 0x3C, 0x07);	// Reg07 Data
	phy_writel(phy, 0x5E, 0x08);	// Reg08 Data
	phy_writel(phy, 0x13, 0x09);	// Reg09 Data
	phy_writel(phy, 0x00, 0x0a);	// Reg0A Data
	phy_writel(phy, 0x3F, 0x0b);	// Reg0B Data
	phy_writel(phy, 0x48, 0x0c);	// Reg0C Data
	phy_writel(phy, 0x6A, 0x0d);	// Reg0D Data
	phy_writel(phy, 0x2C, 0x0e);	// Reg0E Data
	phy_writel(phy, 0x01, 0x0f);	// Reg0F Data
	phy_writel(phy, 0x03, 0x10);	// Reg10 Data
	phy_writel(phy, 0xFC, 0x11);	// Reg11 Data
	phy_writel(phy, 0x0D, 0x12);	// Reg12 Data
	phy_writel(phy, 0x00, 0x13);	// Reg13 Data
	phy_writel(phy, 0x00, 0x14);	// Reg14 Data
	phy_writel(phy, 0x04, 0x15);	// Reg15 Data
	phy_writel(phy, 0x01, 0x16);	// Reg16 Data
	phy_writel(phy, 0x00, 0x17);	// Reg17 Data
	phy_writel(phy, 0x00, 0x18);	// Reg18 Data
	phy_writel(phy, 0x43, 0x19);	// Reg19 Data
	phy_writel(phy, 0x00, 0x1a);	// Reg1A Data
	phy_udelay(5);

	/* PHY TRSV */
	phy_writel(phy, 0xE0, 0x21);	// Reg21 Data
	phy_writel(phy, 0x7C, 0x22);	// Reg22 Data
	phy_writel(phy, 0xFC, 0x23);	// Reg23 Data
	phy_writel(phy, 0x00, 0x24);	// Reg24 Data
	phy_writel(phy, 0x42, 0x25);	// Reg25 Data
	phy_writel(phy, 0x3C, 0x26);	// Reg26 Data
	phy_writel(phy, 0x80, 0x27);	// Reg27 Data
	phy_writel(phy, 0xC4, 0x28);	// Reg28 Data
	phy_writel(phy, 0x04, 0x29);	// Reg29 Data
	phy_writel(phy, 0x00, 0x2a);	// Reg2A Data
	phy_writel(phy, 0x82, 0x2b);	// Reg2B Data
	phy_writel(phy, 0x83, 0x2c);	// Reg2C Data
	phy_writel(phy, 0x00, 0x2d);	// Reg2D Data
	phy_writel(phy, 0x00, 0x2e);	// Reg2E Data
	phy_writel(phy, 0x0C, 0x2f);	// Reg2F Data
	phy_writel(phy, 0x80, 0x30);	// Reg30 Data
	phy_writel(phy, 0x35, 0x31);	// Reg31 Data
	phy_writel(phy, 0x2C, 0x32);	// Reg32 Data
	phy_writel(phy, 0x21, 0x33);	// Reg33 Data
	phy_writel(phy, 0x87, 0x34);	// Reg34 Data
	phy_writel(phy, 0x3B, 0x35);	// Reg35 Data
	phy_writel(phy, 0x88, 0x36);	// Reg36 Data
	phy_udelay(5);

	/* CMN_RESET */
	val &= ~(GPR0_CMN_RESET_OFF);
	gpr_writel(phy, val, 0);
	phy_udelay(5);
	
	val |= GPR0_CMN_RESET_OFF;
	gpr_writel(phy, val, 0);
	phy_udelay(5);

	/* Release MAC_RESET */
	val |= GPR0_MAC_RESET_OFF;
	gpr_writel(phy, val, 0);
	
	phy_udelay(5);
	r = gpr_wait(phy, 0x5000, 0x5000, 8, 2000);
	if (r < 0)
		return r;
	phy_udelay(10);

	/* 33bit addressing support */
	gpr_writel(phy, 0x300, 3);

	/* XXX: i_clk_enSATA & i_reset_n_d3 should be off after phy initialization */
	hawkp_link_reset_ctrl(phy, 0);

	/* XXX: and then this */
	gpr_writel(phy, 0x0000000f, 4);	/* clock domain reset off */
	phy_udelay(5);
	
	return 0;
}

static int _hawkp_phy_exit(struct hawkp_phy *phy)
{
	dev_info (phy->phy.dev, "phy reset.\n");
	
	gpr_writel(phy, 0x00000000, 0);	/* phy reset */
	phy_udelay(5);
	
	gpr_writel(phy, 0x00000000, 4);	/* clock domain reset */
	phy_udelay(5);

	hawkp_link_reset_ctrl(phy, 1);

	return 0;
}

static int hawkp_phy_init(struct sdp_sata_phy *phy)
{
	struct hawkp_phy *hphy = to_hawkp_phy(phy);
	
	_hawkp_phy_init(hphy);
	hphy->enabled = 1;

	return 0;
}

static int hawkp_phy_exit(struct sdp_sata_phy *phy)
{
	struct hawkp_phy *hphy = to_hawkp_phy(phy);
	
	_hawkp_phy_exit(hphy);
	hphy->enabled = 0;

	return 0;
}

static int hawkp_phy_release(struct sdp_sata_phy *phy)
{
	return 0;
}

static int hawkp_ahci_phy_probe(struct platform_device *pdev)
{
	struct hawkp_phy *phy;
	struct resource *gpr_res, *phy_res;
	u32 __iomem *gpr_regs, *phy_regs;
	struct clk *clk, *clk_link, *clk_rst_link;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	
	gpr_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!gpr_res) {
		dev_err(&pdev->dev, "failed to get gpr resource.\n");
		return -ENODEV;
	}
	gpr_regs = devm_ioremap_resource(&pdev->dev, gpr_res);
	if (IS_ERR(gpr_regs)) {
		dev_err(&pdev->dev, "failed to map gpr resource.\n");
		return PTR_RET(gpr_regs);
	}

	phy_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!phy_res) {
		dev_err(&pdev->dev, "failed to get phy resource.\n");
		return -EINVAL;
	}
	phy_regs = devm_ioremap_resource(&pdev->dev, phy_res);
	if (IS_ERR(phy_regs)) {
		dev_err(&pdev->dev, "failed to map phy resource.\n");
		return PTR_RET(phy_regs);
	}

	dev_info(&pdev->dev, "probed.\n");

	/* XXX: phy_clk is GPR reset control, this should not be touched. */
	clk = devm_clk_get(&pdev->dev, "phy_clk");
	if (!IS_ERR(clk))
		phy->clk = clk;

	clk_link = devm_clk_get(&pdev->dev, "ahci_clk");
	if (!IS_ERR(clk_link))
		phy->clk_link = clk_link;
	else
		dev_warn(&pdev->dev, "clk is not specified!\n");

	clk_rst_link = devm_clk_get(&pdev->dev, "ahci_rst_clk");
	if (!IS_ERR(clk_rst_link))
		phy->clk_rst_link = clk_rst_link;
	else
		dev_warn(&pdev->dev, "reset clk is not specified!\n");

	phy->phy_regs = phy_regs;
	phy->gpr_regs = gpr_regs;
	phy->phy.dev = &pdev->dev;
	phy->phy.ops.init = hawkp_phy_init;
	phy->phy.ops.exit = hawkp_phy_exit;
	phy->phy.ops.release = hawkp_phy_release;
	
	platform_set_drvdata(pdev, phy);

	hawkp_phy_reset_off();

	dev_info (&pdev->dev, "initialized. limit_gen=%d refclk=%s clock=%s link_clock=%s\n",
			limit_gen,
			use_extclk ? "external" : "internal",
			phy->clk ? "specified" : "none",
			phy->clk_link ? "specified" : "none");

	return sdp_sata_phy_register(&phy->phy);
}

static int hawkp_ahci_phy_remove(struct platform_device *pdev)
{
	struct hawkp_phy *phy = platform_get_drvdata(pdev);
	
	if (phy->enabled) {
		_hawkp_phy_exit(phy);
		phy->enabled = 0;
	}
	hawkp_link_reset_ctrl(phy, 1);

	sdp_sata_phy_unregister(&phy->phy);

	return 0;
}

static int hawkp_phy_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hawkp_phy *phy = platform_get_drvdata(pdev);

	if (phy->enabled)
		_hawkp_phy_exit(phy);	/* keep enabled set */

	hawkp_link_reset_ctrl(phy, 1);

	return 0;
}

static int hawkp_phy_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hawkp_phy *phy = platform_get_drvdata(pdev);

	hawkp_phy_reset_off();

	if (phy->enabled)	
		_hawkp_phy_init(phy);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id hawkp_ahci_phy_ids[] = {
	{ .compatible = "samsung,hawkp-sata-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, hawkp_ahci_phy_ids);
#endif

static SIMPLE_DEV_PM_OPS(hawkp_phy_pm_ops, hawkp_phy_suspend, hawkp_phy_resume);

static struct platform_driver hawkp_ahci_phy_driver = {
	.probe		= hawkp_ahci_phy_probe,
	.remove		= hawkp_ahci_phy_remove,
	.driver		= {
		.name	= "sdp_hawkp_sata_phy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(hawkp_ahci_phy_ids),
		.pm = &hawkp_phy_pm_ops,
	},
};
module_platform_driver(hawkp_ahci_phy_driver);

MODULE_ALIAS("platform: sdp_hawkp_ahci_phy");
MODULE_AUTHOR("ij.jang@samsung.com");
MODULE_DESCRIPTION("SDP HAKW-P SATA phy driver");
MODULE_LICENSE("GPL");

