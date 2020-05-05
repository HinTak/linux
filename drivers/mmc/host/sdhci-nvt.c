#include <linux/err.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/mmc-nvt.h>
#include <linux/delay.h>

#include "sdhci-pltfm.h"

static void sdhci_nvt_writeb(struct sdhci_host *host, u8 val, int reg)
{
	debug_log("[MMC] reg = 0x%x, val = 0x%x", reg, val);

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
	debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
	writew(val, host->ioaddr + reg);
}

static void sdhci_nvt_writel(struct sdhci_host *host, u32 val, int reg)
{
	debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
	writel(val, host->ioaddr + reg);
}

static u16 sdhci_nvt_readw(struct sdhci_host *host, int reg)
{
	debug_log("[MMC] reg = 0x%x, val = %x", reg, val);
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

static int sdhci_nvt_platform_8bit_width(struct sdhci_host *host, int width)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;
	unsigned char timing = host->mmc->ios.timing;
	int timeout_count = 1000;
	int err = 0;

	debug_log("[MMC] mmc ask bus width = %d", width);
	
        switch (width) {
                case MMC_BUS_WIDTH_1:
			err = hwplat->set_bus_width(hwplat, 1);
			break;
                case MMC_BUS_WIDTH_4:
			err = hwplat->set_bus_width(hwplat, 4);
			break;
                case MMC_BUS_WIDTH_8:
			err = hwplat->set_bus_width(hwplat, 8);
			break;
                default:
                        err = -1;
			break;
        }

        if (unlikely(err)) {
                printk("[MMC] bus width setting failed, %d\n", width);
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
                        err = hwplat->set_bus_timing_mode(hwplat, NVT_MMC_HIGH_SPEED, NVT_MMC_SINGLE_LATCH);
                        break;
                case MMC_TIMING_UHS_DDR50:
                        err = hwplat->set_bus_timing_mode(hwplat, NVT_MMC_HIGH_SPEED, NVT_MMC_DUAL_LATCH);
                        break;
                case MMC_TIMING_MMC_HS200:
                        err = hwplat->set_bus_timing_mode(hwplat, NVT_MMC_HS200, NVT_MMC_SINGLE_LATCH);
			/* SW workaround for NVT controller when switch to HS200 */
			hwplat->set_bus_clk(hwplat, 52000000);
			writeb(SDHCI_RESET_CMD|SDHCI_RESET_DATA, host->ioaddr + SDHCI_SOFTWARE_RESET);
			while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & (SDHCI_RESET_CMD|SDHCI_RESET_DATA)) {
				udelay(1);
				timeout_count--;
				if (timeout_count == 0) {
					break;
				}
			}
			hwplat->set_bus_clk(hwplat, hwplat->cur_clk);
                        break;
                default:
                        err = 1;
                        break;

        }

        if (unlikely(err)) {
                printk("[MMC] timing setting is invalid, %u", timing);
                return err;
        }

	return 0;
}

static void sdhci_nvt_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	hwplat->cur_clk = clock;
	hwplat->set_bus_clk(hwplat, clock);
}

static void sdhci_nvt_platform_suspend(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	hwplat->host_clk_gating(hwplat);
}

static void sdhci_nvt_platform_resume(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct nvt_mmc_hwplat *hwplat = pltfm_host->priv;

	hwplat->host_clk_ungating(hwplat);
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
		if (hwplat->init(hwplat) != 0) {
			printk("*** mmc hw reset failed\n");
		}
	}
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
	.platform_8bit_width = sdhci_nvt_platform_8bit_width,
	.platform_suspend = sdhci_nvt_platform_suspend,
	.platform_resume = sdhci_nvt_platform_resume,
	.platform_reset_enter = sdhci_nvt_platform_reset_enter,
#if 0
	void	(*platform_suspend)(struct sdhci_host *host);
	void	(*platform_resume)(struct sdhci_host *host);
#endif
};

static struct sdhci_pltfm_data sdhci_nvt_pdata = {
        .ops    = &sdhci_nvt_ops, /* some register setting must be defined by our own to make NVT controller happy */
        .quirks = SDHCI_QUIRK_DELAY_AFTER_POWER | /* this will be several millisecond delay after bus pwr on */
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

static int sdhci_nvt_probe(struct platform_device *pdev)
{
	struct sdhci_host *host = NULL;
	struct sdhci_pltfm_host *pltfm_host = NULL;
	struct nvt_mmc_hwplat *hwplat = NULL;
	int ret = 0;

	hwplat = pdev->dev.platform_data;

	host = sdhci_pltfm_init(pdev, &sdhci_nvt_pdata);
        if (IS_ERR(host)) {
                ret = PTR_ERR(host);
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
	pltfm_host->priv = hwplat;
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

	hwplat->deinit(hwplat);
        sdhci_pltfm_unregister(pdev);

        return 0;
}

static struct platform_driver sdhci_nvt_driver = {
        .driver         = {
                .name   = "sdhci_nvt",
                .owner  = THIS_MODULE,
                .pm     = SDHCI_PLTFM_PMOPS,
        },
        .probe          = sdhci_nvt_probe,
        .remove         = sdhci_nvt_remove,
};

module_platform_driver(sdhci_nvt_driver);

MODULE_DESCRIPTION("SDHCI driver for NVT");
MODULE_AUTHOR("Novatek Corp.");
MODULE_LICENSE("GPL v2");
