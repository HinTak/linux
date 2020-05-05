#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/smsc911x.h>
#include <linux/spinlock.h>
#include <linux/usb/isp1760.h>
#include <linux/mtd/physmap.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/platform_data/mmc-nvt.h>
#include <linux/vexpress.h>
#include <linux/delay.h>

#include <asm/arch_timer.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/smp_twd.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/timer-sp.h>
#include <asm/delay.h>

#include "nvt_sdc.h"
#include "mach/clk.h"
#include "include/mach/clk.h"

static void __iomem *g_sdc_vbase = NULL;

void dump_nvt_mmc_reg(void);
struct platform_device *nvt_mmc_dev(int id);

void dump_nvt_mmc_reg(void)
{
	if (!g_sdc_vbase) {
		return;
	}
#if 1
	printk("*** DEBUG Registers: 0x%x, 0x%x\n", readl(g_sdc_vbase+0x40), readl(g_sdc_vbase+0xfe));
#else
#define NVT_SDC_REG_MAX_WORD		64

	unsigned int i = 0;

	printk("\n************************** Novatek DEBUG registers **********************************\n");
	for (; i < NVT_SDC_REG_MAX_WORD ; i+=4) {
		printk("0x%x: 0x%08x ", 4*i, 	  readl(g_sdc_vbase+4*i));
		printk("0x%x: 0x%08x ", 4*(i+1),  readl(g_sdc_vbase+4*(i+1)));
		printk("0x%x: 0x%08x ", 4*(i+2),  readl(g_sdc_vbase+4*(i+2)));
		printk("0x%x: 0x%08x\n", 4*(i+3), readl(g_sdc_vbase+4*(i+3)));
	}

	/**
	   the 0xfe of SDC is the version register, it MUST be 0x1101;
	   the 0x40 of SDC is the capability registerm it MUST be 0x1e834b4;
	*/
	if ((readw(g_sdc_vbase+0xfe) != 0x1101) && (readl(g_sdc_vbase+0x40) != 0x1e834b4)) {
		printk("\nIt seems AHB bus timeout...\n");
	}
#ifndef CONFIG_MMC_SDHCI_NVT_PIO_MODE
	/**
	   the ADMA ptr(0x58) should bot be the same as the current discriptor line address
	 */
	if (readl(g_sdc_vbase+0x58) == readl(g_sdc_vbase+0x0)) {
		printk("\nIt seems data transfer hanged on AHB bus\n");
	}
#endif
	printk("\n*************************************************************************************\n");
#endif
}

static int mmc_init(struct nvt_mmc_hwplat *hwplat)
{
	void __iomem *fcr = hwplat->fcr_vbase;
	void __iomem *nfc = hwplat->nfc_vbase;
	static int ioremap_done = 0;

	g_sdc_vbase = hwplat->sdc_vbase;

	if (!ioremap_done) {
		hwplat->key_ctrl = ioremap_nocache(NVT_REG_PHY_KEY_CTRL, 4);
		hwplat->clk_src_ctrl = ioremap_nocache(NVT_REG_PHY_SRC_CLK_CTRL, 4);
		hwplat->arbitor_ctrl = ioremap_nocache(NVT_REG_PHY_MMC_ARBITOR_CTRL, 4);
		hwplat->hw_reset_ctrl = ioremap_nocache(NVT_REG_PHY_HW_RESET_CTRL, 4);
		hwplat->mpll_ctrl = ioremap_nocache(NVT_REG_PHY_MPLL, 4);

		if ((!hwplat->key_ctrl) || (!hwplat->clk_src_ctrl) ||
				(!hwplat->arbitor_ctrl) || (!hwplat->hw_reset_ctrl)) {
			info_log("key_ctrl(%p), clk_src_ctrl(%p), arbitor_ctrl(%p) or hw_reset_ctrl(%p) remap failed",
					hwplat->key_ctrl, hwplat->clk_src_ctrl, hwplat->arbitor_ctrl, hwplat->hw_reset_ctrl);
			return -1;
		}

		hwplat->acp_ctrl0 = ioremap_nocache(NVT_REG_PHY_ACP_CTRL0, 4);
		hwplat->acp_ctrl1 = ioremap_nocache(NVT_REG_PHY_ACP_CTRL1, 4);
		hwplat->acp_ctrl2 = ioremap_nocache(NVT_REG_PHY_ACP_CTRL2, 4);
		hwplat->acp_ctrl3 = ioremap_nocache(NVT_REG_PHY_ACP_CTRL3, 4);

		if ((!hwplat->acp_ctrl0) || (!hwplat->acp_ctrl1) || (!hwplat->acp_ctrl2) || (!hwplat->acp_ctrl3)) {
			info_log("acp_ctrl0(%p), acp_ctrl1(%p), acp_ctrl2(%p) or acp_ctrl3(%p) remap failed",
					hwplat->acp_ctrl0, hwplat->acp_ctrl1, hwplat->acp_ctrl2, hwplat->acp_ctrl3);
			return -1;
		}

		ioremap_done = 1;
	}

	/* set mmc controller arbitor to allow only mainchip to control mmc registers */
	writel(NVT_CTRL_KEY1, hwplat->key_ctrl);
	writel(NVT_CTRL_KEY2, hwplat->key_ctrl);
	writel((readl(hwplat->arbitor_ctrl) & (u32)(~NVT_EMMC_STBC_ENABLE)), hwplat->arbitor_ctrl);
	
	writel(NVT_CTRL_KEY1, hwplat->key_ctrl);
	writel(NVT_CTRL_KEY2, hwplat->key_ctrl);
	writel((readl(hwplat->arbitor_ctrl) | NVT_EMMC_MAINCHIP_ENABLE), hwplat->arbitor_ctrl);

	/**
	 Be Careful,
	 1. low MPLL
	 2. HW reset
	 3. use user MPLL (much higher clk)
	 */

	/* use low MPLL first before HW reset */
	SYS_CLK_SetMpll(EN_MPLL_OFF_EMMC, 208>>2);
	udelay(1);

	/* host HW reset */
	writel(NVT_CTRL_KEY1, hwplat->key_ctrl);
	writel(NVT_CTRL_KEY2, hwplat->key_ctrl);
	writel((readl(hwplat->hw_reset_ctrl) | NVT_EMMC_HW_RESET_START), hwplat->hw_reset_ctrl);
	
	udelay(1);
	
	writel(NVT_CTRL_KEY1, hwplat->key_ctrl);
	writel(NVT_CTRL_KEY2, hwplat->key_ctrl);
	writel((readl(hwplat->hw_reset_ctrl) & (u32)(~NVT_EMMC_HW_RESET_START)), hwplat->hw_reset_ctrl);

	udelay(1);

	/* OK, we can use user-defined MPLL now */
	SYS_CLK_SetMpll(EN_MPLL_OFF_EMMC, (hwplat->src_clk/1000000)>>2);
	udelay(1);

	/* TODO: make sure the src clk value is correct */
	/* ... */

	/* change to high emmc clk src */
	writel(NVT_CTRL_KEY1, hwplat->key_ctrl);
	writel(NVT_CTRL_KEY2, hwplat->key_ctrl);
	writel(readl(hwplat->clk_src_ctrl)|NVT_EMMC_FAST_CLK_SRC_ENABLE, hwplat->clk_src_ctrl);

	/* this register might be modified by STBC, need to set its default value again, or cmd might error */
	writel(FCR_CPBLT_DEFAULT, fcr+REG_FCR_CPBLT);

	/* set DMA beat mode, 0x3:16_8_4, 0x2: 8_4, 0x1:4, 0x0:1*/
	writel(((0x3<<20) | FCR_FUNC_CTRL_DEFAULT), fcr+REG_FCR_FUNC_CTRL);

	/* enable SW card detect function */
	writel(readl(fcr+REG_FCR_FUNC_CTRL)|FCR_FUNC_CTRL_SW_CDWP_ENABLE, fcr+REG_FCR_FUNC_CTRL);

	/* force card always exists */
	writel((readl(fcr+REG_FCR_FUNC_CTRL) & (u32)(~FCR_FUNC_CTRL_SW_SD_CD)), fcr+REG_FCR_FUNC_CTRL);

	/* data bus is little endian */
	writel((readl(fcr+REG_FCR_FUNC_CTRL) | FCR_FUNC_CTRL_LITTLE_ENDIAN), fcr+REG_FCR_FUNC_CTRL);

	/* do not bypass sd clk */
	writel((readl(fcr+REG_FCR_CPBLT) & (u32)(~FCR_CPBLT_SD_CLK_BYPASS)), fcr+REG_FCR_CPBLT);

	/* since NFC has nand and eMMC, select eMMC */
	writel((readl(nfc+REG_NFC_SYS_CTRL) | NFC_EMMC_SEL), nfc+REG_NFC_SYS_CTRL);

	/* disable HS200 for now */
	writel(0, fcr+REG_FCR_HS200_CTRL);

	/* disable AHB to AXI ACP */
	writel((readl(hwplat->acp_ctrl1) & (u32)(~NVT_ACP_AHB2AXI_ENABLE)), hwplat->acp_ctrl1);

	udelay(1);

	return 0;
}

static void mmc_deinit(struct nvt_mmc_hwplat *hwplat)
{
	iounmap(hwplat->key_ctrl);
	iounmap(hwplat->clk_src_ctrl);
	iounmap(hwplat->arbitor_ctrl);
	iounmap(hwplat->hw_reset_ctrl);
	iounmap(hwplat->acp_ctrl0);
	iounmap(hwplat->acp_ctrl1);
	iounmap(hwplat->acp_ctrl2);
	iounmap(hwplat->acp_ctrl3);
	iounmap(hwplat->mpll_ctrl);
}

static u32 mmc_clk2div(struct nvt_mmc_hwplat *hwplat, u32 clk)
{
	u32 q = 0;
	u32 r = 0;
	/* TODO: make sure we have select faster clk source */
	u32 clk_src = hwplat->src_clk;

	/* the max clk using this approach is clk_src divied by 8 */
	if (clk_src/8 <= clk) {
		return 0;
	}

	q = (clk_src/(clk*4));
	r = (clk_src%(clk*4));
	q -= 2;
	if (r) {
		q += 1;
	}

	return q;
}

static int mmc_set_bus_clk(struct nvt_mmc_hwplat *hwplat, unsigned int clk)
{
	u32 divisor = 0;
	void __iomem *sdc = NULL;
	void __iomem *fcr = NULL;
	u32 clk_src = 0;

	if (unlikely(!hwplat)) {
		info_log("hwplat is null");
		return 0;
	}

	sdc = hwplat->sdc_vbase;
	fcr = hwplat->fcr_vbase;
	clk_src = hwplat->src_clk;

	debug_log("bus clk = %u", clk);

	if (clk != 0) {
		/* stop bus clk first */
		writel(0, sdc+REG_SDC_CLK_CTRL);

		/* if clk is higher than 1/4 of eMMC clk source, use 1/4 clk source, which is the max rate */
		if (clk >= (clk_src/4)) {
			writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_CRTL_FASTEST_CLK, fcr+REG_FCR_HS200_CTRL);
		} else {
			writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~FCR_HS200_CRTL_FASTEST_CLK), fcr+REG_FCR_HS200_CTRL);

			divisor = mmc_clk2div(hwplat, clk);

			/* sanity check divisor */
			WARN_ON((divisor > SDC_MAX_CLK_DIV));

			/* use NVT's clk scheme */
			writel((readl(fcr+REG_FCR_FUNC_CTRL)|FCR_FUNC_CTRL_SD_FLEXIBLE_CLK), fcr+REG_FCR_FUNC_CTRL);
		}

		/* turn on bus internal clk */
		writel((SDC_CLK_CTRL_INCLK_ENABLE|SDC_CLK_CTRL_SDCLK_FREQ_SEL_EX(divisor)), sdc+REG_SDC_CLK_CTRL);

		/* wait internal clk stable */
		while (!(readl(sdc+REG_SDC_CLK_CTRL) & SDC_CLK_CTRL_INCLK_STABLE)) {
			debug_log("wait clk stable");
		}

		/* OK, turn on the bus clk */
		writel((readl(sdc+REG_SDC_CLK_CTRL)|SDC_CLK_CTRL_SDCLK_ENABLE), sdc+REG_SDC_CLK_CTRL);

		/* wait 1ms to do anything further, in order to make sure bus clk is stable */
		mdelay(1);
	}

	return 0;
}

static int mmc_set_bus_width(struct nvt_mmc_hwplat *hwplat, int width)
{
	void __iomem *sdc = hwplat->sdc_vbase;
	void __iomem *fcr = hwplat->fcr_vbase;
	
	debug_log("[MMC] bus width = %d", width);

        switch (width) {
                case 1:
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_4BIT)), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) & (u32)(~FCR_FUNC_CTRL_MMC_8BIT)), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_8BIT)), sdc+REG_SDC_HOST_CTRL);
                        break;
                case 4:
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_4BIT), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) & (u32)(~FCR_FUNC_CTRL_MMC_8BIT)), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_8BIT)), sdc+REG_SDC_HOST_CTRL);
                        break;
                case 8:
			/* note: following setting sequence is very important, __NEVER__ try to change the sequence!! */
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_4BIT), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) | (u32)FCR_FUNC_CTRL_MMC_8BIT), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_8BIT), sdc+REG_SDC_HOST_CTRL);
                        break;
                default:
                        info_log("invalid bus width(%d)", width);
                        return 1;
        }

	udelay(1);

	return 0;
}

static int mmc_set_bus_timing_mode(struct nvt_mmc_hwplat *hwplat,
                                   enum NVT_MMC_SPEED_MODE speed_mode,
                                   enum NVT_MMC_DATA_LATCH latch)
{
	void __iomem *sdc = hwplat->sdc_vbase;
	void __iomem *fcr = hwplat->fcr_vbase;

	debug_log("[MMC] speed_mode = %d", speed_mode);
	debug_log("[MMC] latch = %d", latch);

        if ((speed_mode == NVT_MMC_HIGH_SPEED) || (speed_mode == NVT_MMC_HS200)) {
                writeb(readb(sdc+REG_SDC_HOST_CTRL) | SDC_HOST_CTRL_HIGH_SPEED, sdc+REG_SDC_HOST_CTRL);
		mdelay(1);
                if (speed_mode == NVT_MMC_HS200) {
			writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_CTRL_DISABLE_CMD_CONFLICT, fcr+REG_FCR_HS200_CTRL);
			writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~(FCR_HS200_OUTPUT_SELECT_MASK)), fcr+REG_FCR_HS200_CTRL);
			writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_OUPUT_SELECT_PHASE(0x2), fcr+REG_FCR_HS200_CTRL);
                        writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_CTRL_ENABLE, fcr+REG_FCR_HS200_CTRL);
                } else {
                        writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~FCR_HS200_CTRL_ENABLE), fcr+REG_FCR_HS200_CTRL);
			writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~FCR_HS200_CTRL_DISABLE_CMD_CONFLICT), fcr+REG_FCR_HS200_CTRL);
                }
        } else if (speed_mode == NVT_MMC_LEGACY_SPEED) {
                writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~FCR_HS200_CTRL_ENABLE), fcr+REG_FCR_HS200_CTRL);
                writeb(readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_HIGH_SPEED), sdc+REG_SDC_HOST_CTRL);
        } else {
                info_log("[MMC] not support host speed mode(%d)", speed_mode);
                return -1;
        }

        if (latch == NVT_MMC_SINGLE_LATCH) {
                writel(readl(fcr+REG_FCR_CPBLT) & (u32)(~FCR_CPBLT_DUAL_DATA_RATE_ENABLE), fcr+REG_FCR_CPBLT);
        } else if (latch == NVT_MMC_DUAL_LATCH) {
                writel(readl(fcr+REG_FCR_CPBLT) | FCR_CPBLT_DUAL_DATA_RATE_ENABLE, fcr+REG_FCR_CPBLT);
        } else {
                info_log("[MMC] latch(%d) is invalid.", latch);
                return -1;
        }

	udelay(1);

        return 0;
}

static void mmc_host_clk_gating(struct nvt_mmc_hwplat *hwplat)
{
#if 0
	void __iomem *mpll = hwplat->mpll_ctrl;
	printk("[%s starts]\n", __func__);
	writel((readl(mpll) & (~NVT_REG_PHY_EMMC_MPLL_ENABLE)), mpll);
	printk("[%s end]\n", __func__);
#endif	
}

static void mmc_host_clk_ungating(struct nvt_mmc_hwplat *hwplat)
{
#if 0
	void __iomem *mpll = hwplat->mpll_ctrl;
	printk("[%s starts]\n", __func__);
	writel((readl(mpll) | NVT_REG_PHY_EMMC_MPLL_ENABLE), mpll);
	printk("[%s end]\n", __func__);
#endif	
}

static struct nvt_mmc_hwplat mmc_hwplat = {
	.init = mmc_init,
	.deinit = mmc_deinit,
	.set_bus_clk = mmc_set_bus_clk,
	.set_bus_width = mmc_set_bus_width,
	.set_bus_timing_mode = mmc_set_bus_timing_mode,
	.max_bus_clk = 1000000,
	.min_bus_clk = 0,
	.host_clk_gating = mmc_host_clk_gating,
	.host_clk_ungating = mmc_host_clk_ungating,
	.id = 0,
	.key_ctrl = NULL,
	.clk_src_ctrl = NULL,
};

/* we get controller's resource from device tree, so set each fields zero here */
static struct resource mmc_resources0[] = {
	[0] = {
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_IRQ,
	},
};

static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct platform_device mmc_dev0 = {
	.name			= "sdhci_nvt",
	.id			= 0,
	.dev			= { 
					.dma_mask = &mmc_dmamask,
					.coherent_dma_mask = DMA_BIT_MASK(32),
					.platform_data = &mmc_hwplat
	},
	.resource		= mmc_resources0,
	.num_resources		= ARRAY_SIZE(mmc_resources0),
};

struct platform_device *nvt_mmc_dev(int id)
{
	struct device_node *node = NULL;

	/* currently, we only have one mmc controller */
	if (id != 0) {
		return NULL;
	}

	node = of_find_compatible_node(NULL, NULL, "nvt,hsmmc");
	if (node) {
		/* get regs */
		of_address_to_resource(node, 0, &mmc_resources0[0]);
		mmc_hwplat.nfc_vbase = of_iomap(node, 1);
		mmc_hwplat.fcr_vbase = of_iomap(node, 2);

		/* get irq */
		irq_of_parse_and_map(node, 0);
		mmc_resources0[1].start = irq_of_parse_and_map(node, 0);
		mmc_resources0[1].end = mmc_resources0[1].start;

		/* get specific info of our controller */
		mmc_hwplat.src_clk = get_mmc_clk();
		of_property_read_u32(node, "max-bus-frequency", &mmc_hwplat.max_bus_clk);
		of_node_put(node);
	} else {
		debug_log("*** [MMC] can not find mmc device tree node.\n");
	}

	debug_log("*** [MMC] mmc_resources0[0].start = 0x%x, end = 0x%x, flags = %ld\n",
		mmc_resources0[0].start,
		mmc_resources0[0].end,
		mmc_resources0[0].flags);

	printk("*** [MMC] src_clk = %d, max bus clk = %d, irq=%d\n",
			mmc_hwplat.src_clk, mmc_hwplat.max_bus_clk, mmc_resources0[1].start);

	return &mmc_dev0;
}
