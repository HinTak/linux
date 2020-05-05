/* this header contains those registers which will be used as
   control NVT sd controler's exotic behaviors */

#ifndef __NVT_SDC_H__
#define __NVT_SDC_H__

/*
  related physical address & irq nr
*/
#define NVT_MMC_IRQ_NR					(95)

/*
  SoC misc
*/
#define NVT_SDCLK_MAX                               	(SDCLK_SOURCE_HIGH/4)

#define NVT_REG_PHY_KEY_CTRL				(0xFC040204)
#define NVT_CTRL_KEY1					(0x72682)
#define NVT_CTRL_KEY2					(0x28627)

#define NVT_REG_PHY_SRC_CLK_CTRL                       	(0xFC040C18)
#define NVT_EMMC_FAST_CLK_SRC_ENABLE                	(1<<16)

#define NVT_REG_PHY_SECURE_LOCK_CTRL                   	(0xFD650000)
#define NVT_EMMC_SECURE_LOCK_ENABLE                 	(1<<4)

#define NVT_REG_PHY_MMC_ARBITOR_CTRL			(0xFC040200)
#define NVT_EMMC_STBC_ENABLE				(1<<4)
#define NVT_EMMC_MAINCHIP_ENABLE			(1<<5)

#define NVT_REG_PHY_HW_RESET_CTRL			(0xFC04021C)
#define NVT_EMMC_HW_RESET_START				(1<<25)

#define NVT_REG_PHY_ACP_CTRL0				(0xFD6B043C)
#define NVT_ACP_ARB_32BIT				(0x009FDFCC)

#define NVT_REG_PHY_ACP_CTRL1				(0XFD150084)
#define NVT_ACP_AHB2AXI_ENABLE				(1<<29)

#define NVT_REG_PHY_ACP_CTRL2				(0xFD150408)
#define NVT_ACP_64BIT_MODE				(1<<21)

#define NVT_REG_PHY_ACP_CTRL3				(0xFD060104)
#define NVT_ACP_ACCESS_WHOLE_DDR_RANGE			(3<<30)

#define NVT_REG_PHY_MPLL				(0xFC040904)
#define NVT_REG_PHY_EMMC_MPLL_ENABLE			(1<<26)

//#define REG_EMMC_ARBITOR_CTRL

/*
 FCR registers & related attributes
*/
#define REG_FCR_FUNC_CTRL                       (0x00)
#define REG_FCR_CPBLT                           (0x04)
#define REG_FCR_SD_2nd_FUNC_CTRL                (0x08)
#define REG_FCR_FUNC_CTRL_1                     (0x0c)
#define REG_FCR_HS200_CTRL                      (0x14)

#define FCR_FUNC_CTRL_SW_MS_CD                  (1 << 27)
#define FCR_FUNC_CTRL_SW_SD_CD                  (1 << 26)
#define FCR_FUNC_CTRL_SW_SD_WP                  (1 << 25)
#define FCR_FUNC_CTRL_SW_CDWP_ENABLE            (1 << 24)
#define FCR_FUNC_CTRL_LITTLE_ENDIAN             (1 << 23)
#define FCR_FUNC_CTRL_SD_FLEXIBLE_CLK           (1 << 22)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(n)     (((n) & 3) << 20)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE_MASK   FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(-1)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE_512    FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(3)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE_256    FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(2)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE_128    FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(1)
#define FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE_64     FCR_FUNC_CTRL_AHB_MAX_BURST_SIZE(0)
#define FCR_FUNC_CTRL_INT_OPT_STAT              (1 << 19)
#define FCR_FUNC_CTRL_INT_OPT_DMA               (1 << 18)
#define FCR_FUNC_CTRL_SD_PULLUP_RESISTOR        (1 << 17)
#define FCR_FUNC_CTRL_MMC_8BIT                  (1 << 16)
#define FCR_FUNC_CTRL_CD_DEBOUNCE_TIME(n)       (((n) & 0xf) << 12)
#define FCR_FUNC_CTRL_CD_DEBOUNCE_TIME_MASK     FCR_FUNC_CTRL_CD_DEBOUNCE_TIME(-1)
#define FCR_FUNC_CTRL_PW_UP_TIME(n)             (((n) & 0xf) << 8)
#define FCR_FUNC_CTRL_PW_UP_TIME_MASK           FCR_FUNC_CTRL_PW_UP_TIME(-1)
#define FCR_FUNC_CTRL_SD_SIG_PULLUP_TIME(n)     (((n) & 0xf) << 4)
#define FCR_FUNC_CTRL_SD_SIG_PULLUP_TIME_MASK   FCR_FUNC_CTRL_SD_SIG_PULLUP_TIME(-1)
#define FCR_FUNC_CTRL_MS_SIG_DELAY(n)           (((n) & 3) << 2)
#define FCR_FUNC_CTRL_MS_SIG_DELAY_MASK         FCR_FUNC_CTRL_MS_SIG_DELAY(-1)
#define FCR_FUNC_CTRL_SD_SIG_DELAY(n)           ((n) & 3)
#define FCR_FUNC_CTRL_SD_SIG_DELAY_MASK         FCR_FUNC_CTRL_SD_SIG_DELAY(-1)
#define FCR_FUNC_CTRL_READ_CLK_DELAY(n)         (((n) & 3) << 28)
#define FCR_FUNC_CTRL_READ_CLK_DELAY_MASK       FCR_FUNC_CTRL_READ_CLK_DELAY(-1)
#define FCR_FUNC_CTRL_DEFAULT			(0xf3020)

#define FCR_CPBLT_SD_CLK_BYPASS                 (1 << 20)
#define FCR_CPBLT_DUAL_DATA_RATE_ENABLE         (1 << 19)
#define FCR_CPBLT_VOL_18V                       (1 << 18)
#define FCR_CPBLT_VOL_30V                       (1 << 17)
#define FCR_CPBLT_VOL_33V                       (1 << 16)
#define FCR_CPBLT_SD_BASE_CLK_FREQ(n)           (((n) & 0x3f) << 8)
#define FCR_CPBLT_SD_BASE_CLK_FREQ_MASK         FCR_CPBLT_SD_BASE_CLK_FREQ(-1)
#define FCR_CPBLT_SD_MAX_CURR_CPBLT(n)          ((n) & 0xff)
#define FCR_CPBLT_SD_MAX_CURR_CPBLT_MASK        FCR_CPBLT_SD_MAX_CURR_CPBLT(-1)
#define FCR_CPBLT_DEFAULT			(0xf8134ff)

#define FCR_HS200_CTRL_ENABLE                           (1<<0)
#define FCR_HS200_CTRL_SW_OVERSAMPLE_ENABLE             (1<<1)
#define FCR_HS200_CTRL_SW_OVERSAMPLE_CMD_MASK           (3<<2)
#define FCR_HS200_CTRL_HW_TRACK_EACH_BLK                (1<<6)
#define FCR_HS200_CRTL_FASTEST_CLK                      (1<<7)
#define FCR_HS200_CTRL_DISABLE_CMD_CONFLICT             (1<<8)
#define FCR_HS200_OUTPUT_SELECT_MASK                    (3<<26)
#define FCR_HS200_OUPUT_SELECT_PHASE(phase)             ((phase&3)<<26)

/*
 NFC registers & related attributes
*/
#define REG_NFC_SYS_CTRL                        (0x5c)
#define NFC_EMMC_SEL                            (1<<7)

/*
  SDC registers & related attributes
*/
#define REG_SDC_HOST_CTRL                       (0x28)
#define SDC_HOST_CTRL_8BIT                      (1 << 5)
#define SDC_HOST_CTRL_HIGH_SPEED                (1 << 2)
#define SDC_HOST_CTRL_4BIT                      (1 << 1)
#define SDC_HOST_CTRL_LED_ON                    (1 << 0)

#define REG_SDC_CLK_CTRL                        (0x2c)
#define SDC_CLK_CTRL_SDCLK_FREQ_SEL(n)          ((((WORD)(n)) & 0xff) << 8)
#define SDC_CLK_CTRL_SDCLK_FREQ_SEL_EX(n)       ((((n) & 0xff) << 8) | ((n&0x300)>>2))
#define SDC_CLK_CTRL_SDCLK_ENABLE               (1 << 2)
#define SDC_CLK_CTRL_INCLK_STABLE               (1 << 1)
#define SDC_CLK_CTRL_INCLK_ENABLE               (1 << 0)
#define SDC_MAX_CLK_DIV                         (1023)

#ifdef DEBUG 
#define debug_log(msg, args...) printk("%s:%s:%d: " msg "\n", __FILE__, __FUNCTION__, __LINE__, ## args)
#else
#define debug_log(msg, args...)
#endif

#define info_log(msg, args...)   printk("[ERR_BSP_MMC]%s:%s:%d: " #msg "\n", __FILE__, __FUNCTION__, __LINE__, ##args)

enum NVT_MMC_DATA_LATCH {
        NVT_MMC_SINGLE_LATCH,
        NVT_MMC_DUAL_LATCH
};

enum NVT_MMC_SPEED_MODE {
        NVT_MMC_LEGACY_SPEED,
        NVT_MMC_HIGH_SPEED,
        NVT_MMC_HS200
};

struct nvt_mmc_hwplat {
#if 0
	int (*init)(struct nvt_mmc_hwplat *hwplat);
	void (*deinit)(struct nvt_mmc_hwplat *hwplat);
	int (*set_bus_clk)(struct nvt_mmc_hwplat *hwplat, unsigned int clk);
	int (*set_bus_width)(struct nvt_mmc_hwplat *hwplat, int width);
	int (*set_bus_timing_mode)(struct nvt_mmc_hwplat *hwplat, enum NVT_MMC_SPEED_MODE speed_mode, enum NVT_MMC_DATA_LATCH latch);
	void (*host_clk_gating)(struct nvt_mmc_hwplat *hwplat);
	void (*host_clk_ungating)(struct nvt_mmc_hwplat *hwplat);
#endif
	unsigned int src_clk;
	unsigned int max_bus_clk;
	unsigned int min_bus_clk;
	unsigned int cur_clk;
	int id;
	void __iomem *nfc_vbase;
	void __iomem *fcr_vbase;
	void __iomem *sdc_vbase;
	void __iomem *key_ctrl;
	void __iomem *clk_src_ctrl;
	void __iomem *secure_lock_ctrl;
	void __iomem *arbitor_ctrl;
	void __iomem *hw_reset_ctrl;
	void __iomem *acp_ctrl0;
	void __iomem *acp_ctrl1;
	void __iomem *acp_ctrl2;
	void __iomem *acp_ctrl3;
	void __iomem *mpll_ctrl;
	u8 gating_status ;
};
#endif //end of __NVT_SDC_H__
