#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/cpu.h>

#include <linux/platform_data/clk-sdp1302.h>

#if !defined(CONFIG_COMMON_CLK)
#error This file is only for CONFIG_COMMON_CLK.
#endif

/* SDP1302 PLL information
 * no		logic	fin	name	freq	devices
 * ----------------------------------------------------
 * PLL0		4508	24	ARM	600	ARM
 * PLL1		4508	24	BUS	960	GZIP3 DMA3 BUS3 PERI6 USBHSIC2
 * PLL2		4522	24	CORE	1000	AE2 GPU3 HDMI MMCIF10
 * ...
 * PLL8		4651	24	DDR	667	DDR
 */

#define SDP1302_FIN	(24000000)

struct sdp1302_pmu_regs {
	u32	pll0_pms;
	u32	pll1_pms;
	u32	pll2_pms;
	u32	pll3_pms;

	u32	pll4_pms;
	u32	pll5_pms;
	u32	pll6_pms;
	u32	pll7_pms;

	u32	pll8_pms;
	u32	pll9_pms;
	u32	pll9_k;
	u32	pll3_k;

	u32	pll4_k;
	u32	pll5_k;
	u32	pll6_k;
	u32	pll7_k;

	u32	pll8_k;
	/* ... */
};

static unsigned long sdp1302_recalc_fclk(unsigned long parent_rate)
{
	struct sdp1302_pmu_regs __iomem *regs = (void*)VA_PMU_BASE;
	u32 arm_rate = pll4508_calc_freq(SDP1302_FIN, readl(&regs->pll0_pms));
	return (unsigned long)arm_rate;
}

static unsigned long sdp1302_recalc_armperi(unsigned long parent_rate)
{
	return parent_rate / 4;
}

#if !defined(MHZ)
#define MHZ	1000000
#endif
int __init sdp1302_init_clocks(void)
{
	struct sdp1302_pmu_regs __iomem *regs = (void*)VA_PMU_BASE;
	struct clk *fclk, *pclk, *busclk;
	struct clk *armperi_clk;
	u32 arm_rate, bus_rate, core_rate, ddr_rate;
	struct device *cpu_dev = get_cpu_device(0);

	arm_rate = pll4508_calc_freq(SDP1302_FIN, readl(&regs->pll0_pms));
	bus_rate = pll4508_calc_freq(SDP1302_FIN, readl(&regs->pll1_pms));
	core_rate = pll4522_calc_freq(SDP1302_FIN, readl(&regs->pll2_pms));
	ddr_rate = pll4651_calc_freq(SDP1302_FIN, readl(&regs->pll7_pms), readl(&regs->pll7_k));

	printk (KERN_INFO "SDP1302 PLLs: arm%d.%03d bus%d.%03d core%d.%03d ddr%d.%08d\n",
			arm_rate / MHZ, arm_rate % HZ / 1000,
			bus_rate / MHZ, bus_rate % HZ / 1000,
			core_rate / MHZ, core_rate % HZ / 1000,
			ddr_rate / MHZ, ddr_rate % HZ / 1000);

	/* 1st level: PLLs */
	fclk = clk_register_sdp_scalable_clk(cpu_dev, "fclk", NULL, sdp1302_recalc_fclk);
	clk_register_fixed_rate(NULL, "buspllclk", NULL, CLK_IS_ROOT, bus_rate);
	clk_register_fixed_rate(NULL, "coreclk", NULL, CLK_IS_ROOT, core_rate);

	/* 2nd level clocks */
	busclk = clk_register_fixed_rate(NULL, "busclk", "buspllclk", 0, bus_rate / 3);
	pclk = clk_register_fixed_rate(NULL, "pclk", "buspllclk", 0, bus_rate / 6);
	armperi_clk = clk_register_sdp_scalable_clk(NULL, "arm_peri", "fclk", sdp1302_recalc_armperi);
	
	/* TODO: use static lookup table? */
	clk_register_clkdev(fclk, "fclk", NULL);
	clk_register_clkdev(armperi_clk, NULL, "smp_twd");
	clk_register_clkdev(pclk, "sdp_uart", NULL);
	clk_register_clkdev(pclk, "sdp_timer", NULL);
	clk_register_clkdev(pclk, NULL, "sdp_i2c");
	clk_register_clkdev(pclk, "sdp_spi", NULL);
	clk_register_clkdev(busclk, NULL, "sdp-dma330.0");

	return 0;
}


