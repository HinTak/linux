/* this file defines structs about NVT mmc controller,
   different NVT SoC should use these structs to describe specific hardware behaviors */

#ifndef __NVT_MMC_H__
#define __NVT_MMC_H__

#include <linux/types.h>
#include <linux/compiler.h>

#if 0
#define debug_log(msg, args...) printk("%s:%s:%d: " #msg "\n", __FILE__, __FUNCTION__, __LINE__, ##args)
#else
#define debug_log(msg, args...)
#endif

#define info_log(msg, args...)   printk("%s:%s:%d: " #msg "\n", __FILE__, __FUNCTION__, __LINE__, ##args)

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
	int (*init)(struct nvt_mmc_hwplat *hwplat);
	void (*deinit)(struct nvt_mmc_hwplat *hwplat);
	int (*set_bus_clk)(struct nvt_mmc_hwplat *hwplat, unsigned int clk);
	int (*set_bus_width)(struct nvt_mmc_hwplat *hwplat, int width);
	int (*set_bus_timing_mode)(struct nvt_mmc_hwplat *hwplat, enum NVT_MMC_SPEED_MODE speed_mode, enum NVT_MMC_DATA_LATCH latch);
	void (*host_clk_gating)(struct nvt_mmc_hwplat *hwplat);
	void (*host_clk_ungating)(struct nvt_mmc_hwplat *hwplat);
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
};

#endif // end of __NVT_MMC_H__
