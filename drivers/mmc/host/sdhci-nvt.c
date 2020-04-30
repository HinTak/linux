#include <linux/err.h>
#include <linux/io.h>
#include <linux/mmc/sdhci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/delay.h>

#include "sdhci-pltfm.h"
#include "linux/dma-mapping.h"
#include "nvt_sdc.h"
#define EN_MPLL_OFF_EMMC	 0xc4	///< EMMC
extern unsigned long SYS_CLK_SetMpll(unsigned long off, unsigned long freq);
static void __iomem *g_sdc_vbase = NULL;
static void sdhci_nvt_writeb(struct sdhci_host *host, u8 val, int reg)
{
	//debug_log("[MMC] reg = 0x%x, val = 0x%x", reg, val);

	switch (reg) {
		case SDHCI_POWER_CONTROL:
			/* clear pwr setting */
			val &= (u8)(~(7<<1));
			/* even 1.8V bus voltage on the board, we need to set SDHCI_POWER_330 still
			   since we switch 1.8V/3.3V via other way
			*/
			val |= SDHCI_POWER_330;
			writeb(val, host->ioaddr + reg);
			return;
		case SDHCI_TIMEOUT_CONTROL:
			/* always use the longest data timeout to prevent poor devices failed */
			writeb(0xf, host->ioaddr + reg);
			return;
		case SDHCI_HOST_CONTROL:
			writeb(val&((u8)(~SDHCI_CTRL_LED)), host->ioaddr + reg);
			return;
		default:
			break;
	}

	writeb(val, host->ioaddr + reg);
}

/* sdhci_nvt_writew(), sdhci_nvt_writel() and sdhci_nvt_readw() callback functions do nothing than setting/reading register
   we attach these MMIO callback functions for debug use when we need to trace the control flow of mmc layer
 */

static void sdhci_nvt_writew(struct sdhci_host *host, u16 val, int reg)
{
	//debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
	writew(val, host->ioaddr + reg);
}

static void sdhci_nvt_writel(struct sdhci_host *host, u32 val, int reg)
{
	//debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
	writel(val, host->ioaddr + reg);
}

static u16 sdhci_nvt_readw(struct sdhci_host *host, int reg)
{
	//debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
	return readw(host->ioaddr + reg);
}

static unsigned int sdhci_nvt_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	return hwplat->max_bus_clk;
}

static unsigned int sdhci_nvt_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	return hwplat->min_bus_clk;
}

static unsigned int sdhci_nvt_get_timeout_clock(struct sdhci_host *host)
{
	return 1;
}

static u32 nvt_mmc_clk2div(struct nvt_mmc_hwplat *hwplat, u32 clk)
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

static int nvt_mmc_init(struct nvt_mmc_hwplat *hwplat)
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
	hwplat->gating_status = 1;
	udelay(1);

	return 0;
}

static void nvt_mmc_deinit(struct nvt_mmc_hwplat *hwplat)
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

static int nvt_mmc_set_bus_width(struct nvt_mmc_hwplat *hwplat, int bus_width)
{
	void __iomem *sdc = hwplat->sdc_vbase;
	void __iomem *fcr = hwplat->fcr_vbase;

	debug_log("[MMC] bus width = %d", bus_width);

        switch (bus_width) {
		case MMC_BUS_WIDTH_1:
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_4BIT)), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) & (u32)(~FCR_FUNC_CTRL_MMC_8BIT)), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_8BIT)), sdc+REG_SDC_HOST_CTRL);
                        break;
                case MMC_BUS_WIDTH_4:
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_4BIT), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) & (u32)(~FCR_FUNC_CTRL_MMC_8BIT)), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) & (u8)(~SDC_HOST_CTRL_8BIT)), sdc+REG_SDC_HOST_CTRL);
                        break;
                case MMC_BUS_WIDTH_8:
			/* note: following setting sequence is very important, __NEVER__ try to change the sequence!! */
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_4BIT), sdc+REG_SDC_HOST_CTRL);
                        writel((readl(fcr+REG_FCR_FUNC_CTRL) | (u32)FCR_FUNC_CTRL_MMC_8BIT), fcr+REG_FCR_FUNC_CTRL);
                        writeb((readb(sdc+REG_SDC_HOST_CTRL) | (u8)SDC_HOST_CTRL_8BIT), sdc+REG_SDC_HOST_CTRL);
                        break;
                default:
                        info_log("invalid bus width(%d)", bus_width);
                        //return 1;
                        return -1;
        }

	udelay(1);

	return 0;
}

static int nvt_mmc_set_bus_timing_mode(struct nvt_mmc_hwplat *hwplat,
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
			writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_OUPUT_SELECT_PHASE(0x1), fcr+REG_FCR_HS200_CTRL);
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

static int nvt_mmc_set_bus_clk(struct nvt_mmc_hwplat *hwplat,unsigned int clk)
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

		//for mt case, onboot set emmc run 200 Mhz(emmc pll 800MHz)  
		//if we don't enable fast clk.. it will be just 100Mhz we use 100M to decide this case.
		#define HS200_FASTEST_CLK_THRESHOLD	(100000000) 
		debug_log("[mmc]clk %d clk source %d\n", clk, clk_src);
		if ( (clk >= (clk_src/4)) || (clk > HS200_FASTEST_CLK_THRESHOLD) ) {
			debug_log("[mmc]mmc fast clk enable \n");
			writel(readl(fcr+REG_FCR_HS200_CTRL) | FCR_HS200_CRTL_FASTEST_CLK, fcr+REG_FCR_HS200_CTRL);
		} else {
			writel(readl(fcr+REG_FCR_HS200_CTRL) & (u32)(~FCR_HS200_CRTL_FASTEST_CLK), fcr+REG_FCR_HS200_CTRL);

			divisor = nvt_mmc_clk2div(hwplat, clk);

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

		/* wait 10us to do anything further, in order to make sure bus clk is stable */
		udelay(10);
	}
#if defined (CONFIG_MMC_CLKGATE) || defined(CONFIG_NVT_MMC_CLKGATE)
	else{
		//this is clock gated status.
		writel(0, sdc+REG_SDC_CLK_CTRL);
	}
#endif

	return 0;
}

static int sdhci_nvt_platform_bus_width(struct sdhci_host *host, int width)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;
	unsigned char timing = host->mmc->ios.timing;
	int timeout_count = 1000;
	int err = 0;

	debug_log("[MMC] mmc ask bus width = %d", width);
	if( (err = nvt_mmc_set_bus_width(hwplat ,width ) ) ){
		return err; 
	}

	/* since sdhci layer only set DDR/SDR/HS200 when host is capable of SD 3.0,
	   we set them here
	 */
	switch (timing) {
		case MMC_TIMING_LEGACY:
		case MMC_TIMING_MMC_HS:
		case MMC_TIMING_SD_HS:
		case MMC_TIMING_UHS_SDR12:
		case MMC_TIMING_UHS_SDR25:
		case MMC_TIMING_UHS_SDR50:
			nvt_mmc_set_bus_timing_mode(hwplat, NVT_MMC_HIGH_SPEED, NVT_MMC_SINGLE_LATCH);
			break;
		case MMC_TIMING_UHS_DDR50:
			nvt_mmc_set_bus_timing_mode(hwplat, NVT_MMC_HIGH_SPEED, NVT_MMC_DUAL_LATCH);
			break;
		case MMC_TIMING_MMC_HS200:
			nvt_mmc_set_bus_timing_mode(hwplat,NVT_MMC_HS200, NVT_MMC_SINGLE_LATCH);
			/* SW workaround for NVT controller when switch to HS200 */
			nvt_mmc_set_bus_clk(hwplat,52000000);
			writeb(SDHCI_RESET_CMD|SDHCI_RESET_DATA, host->ioaddr + SDHCI_SOFTWARE_RESET);
			while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & (SDHCI_RESET_CMD|SDHCI_RESET_DATA)) {
				udelay(1);
				timeout_count--;
				if (timeout_count == 0) {
					break;
				}
			}
			nvt_mmc_set_bus_clk(hwplat, hwplat->cur_clk);
			break;
		default:
			return 1;
	}

	return 0;
}


#ifdef CONFIG_NVT_MMC_CLKGATE

void nvt_mmc_clk_gating(struct sdhci_host *host, struct mmc_command *cmd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;
	if(hwplat->gating_status == 0){
		nvt_mmc_set_bus_clk(hwplat, 0);
	}else{
		if(cmd)
			info_log("%s check host command : %x \n",__FUNCTION__, cmd->opcode);
		else
			info_log("%s check host command : NULL \n",__FUNCTION__);
	}
	hwplat->gating_status = 1;
}
EXPORT_SYMBOL(nvt_mmc_clk_gating);

void nvt_mmc_clk_ungating(struct sdhci_host *host, struct mmc_command *cmd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;
	
	if(hwplat->gating_status == 1){
		nvt_mmc_set_bus_clk(hwplat, hwplat->cur_clk);
	}else{
		if(cmd)
			info_log("%s check host command : %x \n",__FUNCTION__, cmd->opcode);
		else
			info_log("%s check host command : NULL \n",__FUNCTION__);
	}
	hwplat->gating_status = 0;
}
EXPORT_SYMBOL(nvt_mmc_clk_ungating);
#endif

static void sdhci_nvt_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	hwplat->cur_clk = clock;
	nvt_mmc_set_bus_clk(hwplat, clock);
}

static void sdhci_nvt_platform_reset_enter(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = NULL;
	struct nvt_mmc_hwplat *hwplat = NULL;

	if (mask & SDHCI_RESET_ALL) {
		printk("*** %s:%d\n", __func__, __LINE__);
		pltfm_host = sdhci_priv(host);
		hwplat = pltfm_host->priv;
		/* sdhci layer have ioremap SD base, we need to keep it in our own for later use */
		hwplat->sdc_vbase = host->ioaddr;
		if (nvt_mmc_init(hwplat) != 0) {
			printk("*** mmc hw reset failed\n");
		}
	}
}

static u64 mmc_dmamask = DMA_BIT_MASK(32);
static int nvt_mmc_parse_dt(struct nvt_mmc_hwplat * hwplat, struct platform_device * plt_dev)
{
	struct device *dev = &plt_dev->dev;
	struct device_node *node = dev->of_node;
	if(!hwplat || !plt_dev)
		return -1;
	
	//node = of_find_compatible_node(NULL, NULL, "nvt,hsmmc");
	if (node) {
		struct sdhci_host *host = dev_get_drvdata(dev);
		unsigned long get_mmc_clk(void);
		/* get regs */
		//of_address_to_resource(node, 0, &mmc_resources0[0]);
		//TOCHECK: get sdc_base back?
		hwplat->nfc_vbase = of_iomap(node, 1);
		hwplat->fcr_vbase = of_iomap(node, 2);

		/* get irq */
		//irq_of_parse_and_map(node, 0);

		/*set irq*/	

		/* get specific info of our controller */
		hwplat->src_clk = get_mmc_clk();
		of_property_read_u32(node, "max-bus-frequency", &hwplat->max_bus_clk);
		of_node_put(node);
		mmc_of_parse(host->mmc);
		debug_log("*** [MMC] src_clk = %d, max bus clk = %d, irq= %d\n",
			hwplat->src_clk, hwplat->max_bus_clk, host->irq);
	} else {
		debug_log("*** [MMC] can not find mmc device tree node.\n");
	}

	return 0;
	
}

static struct sdhci_ops sdhci_nvt_ops = {
	.write_b = sdhci_nvt_writeb,
	.write_w = sdhci_nvt_writew,
	.write_l = sdhci_nvt_writel,
	.read_w = sdhci_nvt_readw,
	.get_max_clock = sdhci_nvt_get_max_clock,
	.get_min_clock = sdhci_nvt_get_min_clock,
	.get_timeout_clock = sdhci_nvt_get_timeout_clock,
	.set_clock = sdhci_nvt_set_clock,
	.platform_bus_width = sdhci_nvt_platform_bus_width,
	.platform_reset_enter = sdhci_nvt_platform_reset_enter,
};

static struct sdhci_pltfm_data sdhci_nvt_pdata = {
        .ops    = &sdhci_nvt_ops, /* some register setting must be defined by our own to make NVT controller happy */
        .quirks = 
#ifndef CONFIG_NVT_MMC_REDUCE_DELAY
		SDHCI_QUIRK_DELAY_AFTER_POWER | /* this will be several millisecond delay after bus pwr on */
#endif
		  SDHCI_QUIRK_MISSING_CAPS | /* we want to fine tune controller's behaviors, set this */
		  SDHCI_QUIRK_FORCE_DMA | /* always use DMA to transfer DATA */
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN | /* we have our own clk scheme, not te same as standard SD host spec */
		  SDHCI_QUIRK_NONSTANDARD_CLOCK | /* we do not use clk setting in spec */
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC | /* we do not support zero length descriptor in ADMA table */
		  SDHCI_QUIRK_FORCE_BLK_SZ_2048 | /* use max block size as 2K as default value, can fine fune */
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC /* we ask the very last descriptor must be valid in ADMA table */
#ifdef CONFIG_MMC_SDHCI_NVT_OPEN_ENDED_RW
		  | SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12 /* enable AUTO CMD12, might save a little time for issuing seperate CMD12 */
#endif
};

static const struct of_device_id sdhci_nvt_dt_match[] = {
	{ .compatible = "novatek,nvt7266x-sdhci", .data = &sdhci_nvt_pdata },
	{ .compatible = "nvt,hsmmc", .data = &sdhci_nvt_pdata },
	{},
};

MODULE_DEVICE_TABLE(of, sdhci_nvt_dt_match);
static int sdhci_nvt_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct sdhci_host *host = NULL;
	struct sdhci_pltfm_host *pltfm_host = NULL;
	struct nvt_mmc_hwplat *hwplat = NULL;
	int ret = 0;

	match = of_match_device(sdhci_nvt_dt_match, &pdev->dev);
	if (!match){
		printk("[MMC] OF not found \n");
		return -EINVAL;
	}

	host = sdhci_pltfm_init(pdev, &sdhci_nvt_pdata, 0);
        if (IS_ERR(host)) {
		printk("[MMC] sdhci plat init fail\n");
                ret = PTR_ERR(host);
                goto err_sdhci_pltfm_init;
        }

	if( pdev->dev.platform_data){
		printk("[MMC] nvt mmc platform data should be NULL when initial\n");
		goto err_sdhci_pltfm_init;
	}
#ifdef CONFIG_MMC_SDHCI_NVT_OPEN_ENDED_RW
	/* disable CMD23, always use CMD12 to stop multiple block read/write */
	host->quirks2 |= SDHCI_QUIRK2_HOST_NO_CMD23;
#endif
	host->caps |= SDHCI_CAN_DO_ADMA2 |
		      SDHCI_CAN_DO_HISPD |
		      SDHCI_CAN_DO_8BIT  |
		      SDHCI_CAN_DO_SDMA  |
		      SDHCI_CAN_VDD_330  |
		      SDHCI_CAN_VDD_180
		     ;

#ifdef CONFIG_MMC_SDHCI_NVT_PIO_MODE
	host->caps &= (u32)(~(SDHCI_CAN_DO_ADMA2 | SDHCI_CAN_DO_SDMA));
	host->quirks &= (u32)(~(SDHCI_QUIRK_FORCE_DMA));
#endif

	host->mmc->caps |= MMC_CAP_NONREMOVABLE  |
			   MMC_CAP_8_BIT_DATA    |
			   MMC_CAP_MMC_HIGHSPEED |
			   MMC_CAP_UHS_DDR50	 |
			   MMC_CAP_1_8V_DDR
			;
#ifdef CONFIG_MMC_SDHCI_NVT_ENABLE_DEV_CACHE
	host->mmc->caps2 |= MMC_CAP2_CACHE_CTRL;
#endif
#ifdef CONFIG_MMC_SDHCI_NVT_HS200
	host->mmc->caps2 |= MMC_CAP2_HS200_1_8V_SDR |
			    MMC_CAP2_HS200
			;
#endif
	pltfm_host = sdhci_priv(host);

	hwplat  = pdev->dev.platform_data =  devm_kzalloc(&pdev->dev, sizeof(struct nvt_mmc_hwplat), GFP_KERNEL);
	if(!hwplat){
		ret = -ENOMEM;
		goto err_sdhci_add;
	}
	nvt_mmc_parse_dt(hwplat, pdev );
	pltfm_host->priv = hwplat;

	/* set platform  device information*/
	pdev->dev.dma_mask = &mmc_dmamask;
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		
	ret = sdhci_add_host(host);
	if (ret) {
		goto err_sdhci_add;
	}

	debug_log("[MMC] add NVT host\n");

	return 0;

err_sdhci_add:
	sdhci_pltfm_free(pdev);
err_sdhci_pltfm_init:
	return ret;
}

static int sdhci_nvt_remove(struct platform_device *pdev)
{
        struct sdhci_host *host = platform_get_drvdata(pdev);
        struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
        struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	nvt_mmc_deinit(hwplat);
	devm_kfree(&pdev->dev, hwplat);	
	pdev->dev.platform_data = NULL;
        sdhci_pltfm_unregister(pdev);

        return 0;
}

static struct platform_driver sdhci_nvt_driver = {
        .driver         = {
                .name   = "sdhci_nvt",
                .owner  = THIS_MODULE,
                .pm     = SDHCI_PLTFM_PMOPS,
		.of_match_table = sdhci_nvt_dt_match,
        },
        .probe          = sdhci_nvt_probe,
        .remove         = sdhci_nvt_remove,
};

#ifdef CONFIG_EMRG_SAVE_KLOG
#include <linux/vmalloc.h> 
#include <linux/slab.h> 
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/mmc.h>

static void nvt_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	unsigned long timeout;

	sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET);

	/* Wait max 100 ms */
	timeout = 100;

	/* hw clears the bit when it's done */
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & mask) {
		if (timeout == 0) {
			pr_err("%s: Reset 0x%x never completed.\n",
				mmc_hostname(host->mmc), (int)mask);
			//sdhci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void nvt_sdhci_tasklet_finish(unsigned long param)
{
	struct sdhci_host *host;
	unsigned long flags;
	struct mmc_request *mrq;
	host = (struct sdhci_host*)param;
	spin_lock_irqsave(&host->lock, flags);
	if (!host->mrq) {
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	del_timer(&host->timer);

	mrq = host->mrq;

	if (!(host->flags & SDHCI_DEVICE_DEAD) &&
	    ((mrq->cmd && mrq->cmd->error) ||
		 (mrq->data && (mrq->data->error ||
		  (mrq->data->stop && mrq->data->stop->error))) ||
		   (host->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST))) {
		/* Spec says we should do both at the same time, but Ricoh
		   controllers do not like that. */
		nvt_sdhci_reset(host, SDHCI_RESET_CMD);
		nvt_sdhci_reset(host, SDHCI_RESET_DATA);
	}

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	mmc_request_done(host->mmc, mrq);
}

static void nvt_mmc_prepare_mrq(struct mmc_card	*card,
	struct mmc_request *mrq, struct scatterlist *sg, unsigned sg_len,
	unsigned dev_addr, unsigned blocks, unsigned blksz, int write)
{
	BUG_ON(!mrq || !mrq->cmd || !mrq->data || !mrq->stop);

	if (blocks > 1) {
		mrq->cmd->opcode = write ?
			MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	} else {
		mrq->cmd->opcode = write ?
			MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
	}
	mrq->cmd->arg = dev_addr;
	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	if (!mmc_card_blockaddr(card))
		mrq->cmd->arg <<= 9;
	
	if (blocks == 1)
		mrq->stop = NULL;
	else {
		mrq->stop->opcode = MMC_STOP_TRANSMISSION;
		mrq->stop->arg = 0;
		if(write)
		    mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;
		else
			mrq->stop->flags =  MMC_RSP_R1 | MMC_CMD_AC;;
	}

	mrq->data->blksz = blksz;
	mrq->data->blocks = blocks;
	mrq->data->flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = sg;
	mrq->data->sg_len = sg_len;

	mmc_set_data_timeout(mrq->data, card);
}

void nvt_mmc_start_request(struct mmc_host *host, struct mmc_request *mrq)
{
	void sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd);
	struct sdhci_host * nvt_host = mmc_priv(host);

	WARN_ON(!host->claimed);
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
	//sdhci_request
	WARN_ON(nvt_host->mrq != NULL);

	if (!mrq->sbc && (nvt_host->flags & SDHCI_AUTO_CMD12)) {
		if (mrq->stop) {
			mrq->data->stop = NULL;
			mrq->stop = NULL;
		}
    }
	nvt_host->mrq = mrq;
	sdhci_send_command(nvt_host, mrq->cmd);
}

#define CMD_ERRORS							\
	(R1_OUT_OF_RANGE |	/* Command argument out of range */	\
	 R1_ADDRESS_ERROR |	/* Misaligned address */		\
	 R1_BLOCK_LEN_ERROR |	/* Transferred block length incorrect */\
	 R1_WP_VIOLATION |	/* Tried to write to protected block */	\
	 R1_CC_ERROR |		/* Card controller error */		\
	 R1_ERROR)		/* General/unknown error */
static void nvt_sdhci_cmd_irq(struct sdhci_host *host)
{
	debug_log(" %s Enter \n", __FUNCTION__);
	if (!host->cmd) {
		printk("no command operation was in progress.\n");
		return;
	}

	while(!( SDHCI_INT_RESPONSE & (sdhci_readl(host , SDHCI_INT_STATUS))) ){
		debug_log(" mmc intr status 1: %x \n", sdhci_readl(host , SDHCI_INT_STATUS));
		if(SDHCI_INT_ERROR & (sdhci_readl(host , SDHCI_INT_STATUS)) ){
			host->cmd->error = -EILSEQ;
			return;
		}
	}

	sdhci_writel(host, SDHCI_INT_RESPONSE,SDHCI_INT_STATUS);

	//sdhci_finish_command(host);
	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			int i = 0;
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0;i < 4;i++) {
				host->cmd->resp[i] = sdhci_readl(host,
					SDHCI_RESPONSE + (3-i)*4) << 8;
				if (i != 3)
					host->cmd->resp[i] |=
						sdhci_readb(host,
						SDHCI_RESPONSE + (3-i)*4-1);
			}
		} else {
			host->cmd->resp[0] = sdhci_readl(host, SDHCI_RESPONSE);
		}
	}

	if(host->cmd->resp[0] & CMD_ERRORS){ //CMD_ERRORS 0XFDF90008
		host->cmd->error = -EILSEQ;
		return;
	}

	host->cmd->error = 0;
	host->cmd = NULL;
	debug_log(" %s Exit \n", __FUNCTION__);
}

static void nvt_sdhci_adma_table_post(struct sdhci_host *host,
	struct mmc_data *data)
{
#ifdef CONFIG_NVT_MMC_IF
#define ADMA_MAX_SEGS		(32)
#else
#define ADMA_MAX_SEGS		(128)
#endif
	int direction;

	struct scatterlist *sg;
	int i, size;
	u8 *align;
	char *buffer;
	unsigned long flags;

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	dma_unmap_single(mmc_dev(host->mmc), host->adma_addr,
		(128 * 2 + 1) * 4, DMA_TO_DEVICE);

	dma_unmap_single(mmc_dev(host->mmc), host->align_addr,
		ADMA_MAX_SEGS * 4, direction);

	if (data->flags & MMC_DATA_READ) {
		dma_sync_sg_for_cpu(mmc_dev(host->mmc), data->sg,
			data->sg_len, direction);

		align = host->align_buffer;

		for_each_sg(data->sg, sg, host->sg_count, i) {
			if (sg_dma_address(sg) & 0x3) {
				size = 4 - (sg_dma_address(sg) & 0x3);

				//buffer = sdhci_kmap_atomic(sg, &flags);
				local_irq_save(flags);
	            buffer = (char *)(kmap_atomic(sg_page(sg)) + sg->offset);
				
				//WARN_ON(((long)buffer & PAGE_MASK) > (PAGE_SIZE - 3));
				memcpy(buffer, align, size);
				
				//sdhci_kunmap_atomic(buffer, &flags);
				kunmap_atomic(buffer);
				local_irq_restore(flags);

				align += 4;
			}
		}
	}

	dma_unmap_sg(mmc_dev(host->mmc), data->sg,
		data->sg_len, direction);
}
static void nvt_sdhci_data_irq(struct sdhci_host *host)
{
	struct mmc_data *data;
	//void sdhci_adma_table_post(struct sdhci_host *host, struct mmc_data *data);
	debug_log(" %s Enter \n", __FUNCTION__);
	if (!host->data) {
		printk("no data operation was in progress.\n");
		return;
	}

	while(!(SDHCI_INT_DATA_END & (sdhci_readl(host , SDHCI_INT_STATUS))) ){
		debug_log("mmc  intr status 2: %x \n", sdhci_readl(host , SDHCI_INT_STATUS));
		if(SDHCI_INT_ERROR & (sdhci_readl(host , SDHCI_INT_STATUS)) ){
			host->data->error = -EILSEQ;
		}
	}
	sdhci_writel(host, SDHCI_INT_DATA_END,SDHCI_INT_STATUS);

    //sdhci_finish_data(host);
	data = host->data;
	host->data = NULL;
	
	if (host->flags & SDHCI_REQ_USE_DMA) {
		if (host->flags & SDHCI_USE_ADMA)
			nvt_sdhci_adma_table_post(host, data);
		else {
			dma_unmap_sg(mmc_dev(host->mmc), data->sg,
				data->sg_len, (data->flags & MMC_DATA_READ) ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE);
		}
	}
	if (data->error)
		data->bytes_xfered = 0;
	else
		data->bytes_xfered = data->blksz * data->blocks;

	/*
	 * Need to send CMD12 if -
	 * a) open-ended multiblock transfer (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (data->stop &&
	    (data->error ||
	     !host->mrq->sbc)) {

		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */ 
		if (data->error) {
			nvt_sdhci_reset(host, SDHCI_RESET_CMD);
			nvt_sdhci_reset(host, SDHCI_RESET_DATA);
		}
		sdhci_send_command(host, data->stop);
		nvt_sdhci_cmd_irq(host);
	}
	debug_log(" %s Exit \n", __FUNCTION__);

}

void nvt_mmc_wait_for_req_rw(struct mmc_host *_host, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(_host);

	nvt_mmc_start_request(_host, mrq);

	nvt_sdhci_cmd_irq(host);
	nvt_sdhci_data_irq(host);

	nvt_sdhci_tasklet_finish((unsigned long)host);	
}

static int nvt_mmc_wait_busy(struct mmc_card *card)
{
	int ret;
	struct sdhci_host *nvt_host = mmc_priv(card->host);
	struct mmc_request mrq = {NULL};
	struct mmc_command cmd = {0};
	debug_log(" %s Enter \n", __FUNCTION__);

	do {
		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;	
		
		memset(cmd.resp, 0, sizeof(cmd.resp));
		cmd.retries = 0;//retries;
		mrq.cmd = &cmd;
		cmd.data = NULL;

		nvt_mmc_start_request(card->host, &mrq);
	    nvt_sdhci_cmd_irq(nvt_host);
		ret = cmd.error;
		nvt_sdhci_tasklet_finish((unsigned long)nvt_host);	
		if (ret)
			break;
	} while (!(cmd.resp[0] & R1_READY_FOR_DATA));
	debug_log(" %s Exit \n", __FUNCTION__);
	return ret;
}

#define NR_SG 0x400            
#define ALIGH_MASK	(32/8-1)
int nvt_mmc_rw_request(struct mmc_host *host, u32 blk_addr, u32 blk_count, u8 *buf, bool is_write) 
{
	static struct scatterlist sg[NR_SG];
	const unsigned int sgnum = (blk_count/(host->max_seg_size/512)) + (blk_count%(host->max_seg_size/512)?1:0);

	int i;
	struct mmc_card *card = host->card;
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	struct sdhci_host * nvt_mmch = mmc_priv(host);

	unsigned int seg_size = host->max_seg_size& ~(ALIGH_MASK);

	printk("mmc_read_write_block Enter (buf: %p start=%x blcokcount=%d.is_write=%d) \n",buf,blk_addr,blk_count,is_write);
	printk("nvt_mmc_: in_irq(%lx), in_softirq(%lx), in_interrupt(%lx), in_serving_softirq(%lx)\n",
			in_irq(), in_softirq(), in_interrupt(), in_serving_softirq());

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;
	
	sg_init_table(sg, sgnum);
	for(i = 0; i < sgnum-1; i++) {
		sg_set_buf(sg+i, buf+(seg_size * i), seg_size);
	}
	sg_set_buf(sg+i, buf+(seg_size * (i)), (blk_count*512)-(seg_size*(i)));
	

	nvt_mmc_prepare_mrq(card, &mrq, sg, sgnum, blk_addr, blk_count, 512, is_write);

	if(host->claimed){
		printk( "%s: forced release host->claimed\n", mmc_hostname(nvt_mmch->mmc));
		host->claimed = 0;
	}
	if(spin_is_locked(&nvt_mmch->lock)) {
		printk( "%s: forced unlock mmc->lock\n", mmc_hostname(nvt_mmch->mmc));
		spin_unlock(&nvt_mmch->lock);
	}
	if(nvt_mmch->mrq != NULL ) 
	{
		printk("%s : host mrq is not finish force clear\n",__FUNCTION__);
		nvt_sdhci_tasklet_finish((unsigned long)nvt_mmch);
	}

    //nvt_sdhci_reset(nvt_mmch, SDHCI_RESET_ALL);
	
	//we don't dependo on interrupt 
	disable_irq(nvt_mmch->irq);
	tasklet_disable(&nvt_mmch->finish_tasklet);
	del_timer(&nvt_mmch->timer);

	host->claimed = 1;

	nvt_mmc_wait_for_req_rw(host,&mrq);

	if (cmd.error){
		printk("%s cmd.error %x \n", __FUNCTION__, cmd.error);
		return cmd.error;
	}
	if (data.error){
		printk("%s data.error %x \n", __FUNCTION__, data.error);
		return data.error;
	}
	if (stop.error){
		printk("%s stop.error %x \n", __FUNCTION__, stop.error);
		return stop.error;
	}
	nvt_mmc_wait_busy(card);
	
	enable_irq(nvt_mmch->irq);
	tasklet_enable(&nvt_mmch->finish_tasklet);
	host->claimed = 0;

	return 0;
}
EXPORT_SYMBOL(nvt_mmc_rw_request);

int nvt_mmc_read_write_log(struct mmc_host *host, u32 blk_addr, u32 blk_count, u8 *buf, bool is_write) 
{
	int ret;

	char * content_buf = kmalloc(blk_count*512, GFP_KERNEL);
	int i = 0;
	memset(content_buf, 0x0, blk_count*512);
#if 0
	for(i = 0 ; i <blk_count*512;i++){
	printk("[%x]",(unsigned int)buf[i]);
		if((i%32) == 0 && (i!=0))
			printk("\n");
	}
#endif
	ret = nvt_mmc_rw_request(host, blk_addr, blk_count, buf, true);
	ret = nvt_mmc_rw_request(host, blk_addr, blk_count, content_buf, false);

	for( i = 0 ;  i< blk_count*512;i++){
		if(buf[i] != content_buf[i]){
			printk("the %d data is not correct %x %x \n", i, buf[i], content_buf[i]);
			break;
		}
		//else{
		//	printk("[%x]",(unsigned int)content_buf[i]);
		//}
	}
	
	if(i == blk_count*512)
		printk("====>w/r emerg_log test correct\n");
	else{
		printk("====>w/r emerg_log test wrong at %d \n",i);
		}
	kfree(content_buf);
	return ret;
}

EXPORT_SYMBOL(nvt_mmc_read_write_log);
#endif

module_platform_driver(sdhci_nvt_driver);

MODULE_DESCRIPTION("SDHCI driver for NVT");
MODULE_AUTHOR("Novatek Corp.");
MODULE_LICENSE("GPL v2");
