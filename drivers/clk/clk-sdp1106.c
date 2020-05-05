#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/platform_data/clk-sdp1106.h>
#include <linux/cpu.h>

#if !defined(CONFIG_COMMON_CLK)
#error This file is only for CONFIG_COMMON_CLK.
#endif

#define INPUT_FREQ		(24000000)

#define PMU_PLL_P_VALUE(x)      ((x >> 20) & 0x3F)
#define PMU_PLL_M_VALUE(x)      ((x >> 8) & 0x3FF)
#define PMU_PLL_S_VALUE(x)      (x & 0x7)
#define PMU_PLL_K_VALUE(x)      (x & 0xFFFF)

#define GET_PLL_M(x)            PMU_PLL_M_VALUE(x)
#define GET_PLL_P(x)            PMU_PLL_P_VALUE(x)
#define GET_PLL_S(x)            PMU_PLL_S_VALUE(x)

static unsigned long sdp1106_calc_cpu_pms(unsigned int pms)
{
	unsigned long ret;

	ret = (INPUT_FREQ >> (GET_PLL_S(pms))) / GET_PLL_P(pms);
	ret *= GET_PLL_M(pms); 
	return ret;
}

static unsigned long sdp1106_recalc_fclk(unsigned long parent_rate)
{
	u32 pms_cpu = readl(VA_PMU_BASE + 0x00);
	return sdp1106_calc_cpu_pms(pms_cpu);
}

static unsigned long sdp1106_recalc_armperi(unsigned long parent_rate)
{
	return parent_rate / 4;	/* FIXME */
}

static unsigned long __init sdp1106_calc_bus_pms(unsigned int pms)
{
	unsigned long ret;
	ret = (INPUT_FREQ >> (GET_PLL_S(pms))) / GET_PLL_P(pms);
	ret *= GET_PLL_M(pms); 
	return ret;
}

static unsigned long __init sdp1106_calc_ddr_pmsk(unsigned int pms, unsigned int k)
{
	unsigned long ret;
	signed long long tmp;
	int sign = 1;
	tmp = (INPUT_FREQ >> GET_PLL_S(pms)) / GET_PLL_P(pms);
	if(k & 0x8000)
	{
		k = 0x10000-k;
		sign = -1;
	}
	ret =  (tmp * GET_PLL_M(pms)) + (((tmp * k) >> 16) * sign);
	return ret;
}

#if !defined(MHZ)
#define MHZ	1000000
#endif
int __init sdp1106_init_clocks(void)
{
	struct clk *fclk, *pclk, *armperi_clk;
	struct device *cpu_dev = get_cpu_device(0);

	unsigned long arm_rate, bus_rate, ddr_rate;
	
	u32 pms_core	= readl(VA_PMU_BASE + 0x00);
	u32 pms_ddr	= readl(VA_PMU_BASE + 0x20);
	u32 pms_bus	= readl(VA_PMU_BASE + 0x28);
	u32 k_ddr	= readl(VA_PMU_BASE + 0x40);

	arm_rate = sdp1106_calc_cpu_pms(pms_core);
	bus_rate = sdp1106_calc_bus_pms(pms_bus);
	ddr_rate = sdp1106_calc_ddr_pmsk(pms_ddr, k_ddr);

	printk (KERN_INFO "SDP1106 PLLs: arm%ld.%03ld bus%ld.%03ld ddr%ld.%03ld\n",
			arm_rate / MHZ, arm_rate % HZ / 1000,
			bus_rate / MHZ, bus_rate % HZ / 1000,
			ddr_rate / MHZ, ddr_rate % HZ / 1000);

	/* 1st level: PLLs */
	fclk = clk_register_fixed_rate(cpu_dev, "fclk", NULL, CLK_IS_ROOT, arm_rate);
	clk_register_fixed_rate(NULL, "busclk", NULL, CLK_IS_ROOT, bus_rate);

	/* 2nd level clocks */
	pclk = clk_register_fixed_rate(NULL, "pclk", "busclk", 0, bus_rate / 2);
	armperi_clk = clk_register_fixed_rate(NULL, "arm_peri", "fclk", 0, arm_rate / 4);
	
	/* TODO: use static lookup table? */
	clk_register_clkdev(fclk, "fclk", NULL);
	clk_register_clkdev(armperi_clk, NULL, "smp_twd");
	clk_register_clkdev(pclk, "sdp_uart", NULL);
	clk_register_clkdev(pclk, "sdp_timer", NULL);
	clk_register_clkdev(pclk, NULL, "sdp_i2c");
	clk_register_clkdev(pclk, "sdp_spi", NULL);
	/* FIXME: add more ... dma330... */

	return 0;
}


