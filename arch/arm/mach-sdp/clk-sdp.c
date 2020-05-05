/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/mach/map.h>

static DEFINE_SPINLOCK(sdp_clk_lock);

static void __iomem *reg_base;
static phys_addr_t reg_phy_base;
static phys_addr_t reg_phy_end;
static struct clk **clk_table;
#ifdef CONFIG_OF
static struct clk_onecell_data clk_data;
#endif

struct sdp_pll_bitfield {
	u8	p_shift;
	u8	m_shift;
	u8	s_shift;
	u8	p_bits;
	u8	m_bits;
	u8	s_bits;
	u8	k_bits;
	u8	s_delta;
	u8	k_scale;
};

struct sdp_fixed_rate {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long fixed_rate;
};

struct sdp_fdiv_clock {
	unsigned int id;
	const char *dev_name;
	const char *name;
	const char *parent;
	unsigned long flags;
	unsigned int mult;
	unsigned int div;
	const char *alias;
};

struct sdp_gate_clock {
	unsigned int id;
	const char *dev_name;
	const char *name;
	const char *parent;
	unsigned long flags;
	unsigned long offset;
	u8 bit_idx;
	u8 gate_flags;
	const char *alias;
};

/* TODO: use string instead of number in the DTs */
/* refer to Fox-AP vol.1 chapter 2.3 */
enum sdp1202_clks {
	none,

	/* 1 */
	fin_pll,

	/* PLL */
	/* 2 - 7 */
	pll0_cpu, pll1_ams, pll2_gpu, pll3_dsp, pll4_lvds, pll5_ddr,

	/* fixed factor clocks */
	/* 8 - 15 */
	arm_clk, spi_clk, irr_clk, sci_clk, emmc_clk, gpu_clk, dma_clk, ahb_clk,
	/* 16 - 18 */
	video_clk, gzip_clk, g2d_clk /* graphic-2D */,
	/* 19 - 22 */
	plane_clk /* graphic-plane-core */, lvds_clk, lvds2_clk, ebus_clk,
	/* 23 - 28 */
	bus_clk, ddr2_clk, ddr4_clk, ddr16_clk, xmif4_clk, xmif16_clk,


	/* gate clocks */
	/* 29 - 34 */
	usb2_0_hclk, tz_mem, gzip, se_hclk, usb2_1_hclk, usb2_1_phy,
	/* 35 - 39 */
	usb2_1_ohci48, usb2_1_utmi_phy, usb2_2_hclk, usb2_2_phy, usb2_2_ohci48,
	/* 40 - 44 */
	usb2_2_utmi_phy, usb3_hclk, usb3_suspend, usb3_utmi, usb3_pipe3_rx_pclk,
	/* 45 - 49 */
	usb3_pipe3_tx_pclk, emac_125M, emac_phy_rx, emac_hclk, mmc_hclk,
	/* 50 - 54 */
	mmc_cclk_in, mmc_cclk_in_drv, mmc_cclk_in_sam, dma330_clk, dma330_pclk,
	/* 55 - 60 */
	xmif_ab_bclk, xmif_ab_pclk, xmif_ab_mclk, bus_gate, dsp2_bclk, dsp1_bclk,
	/* 61 - 67 */
	ga2d_bclk, cap_en, osdp_dclk, gp_dclk, sp_dclk, osdp_bclk, gp_bclk,
	/* 68 - 73 */
	sp_bclk, gpu_gate, jtag_clk, ddrphya_clk, usb2_2, usb2_1,

	/* reset clocks */
	/* 74 - 79 */
	wdt_reset, rstn_xmif, rstn_dsp2, rstn_dsp1, rstn_ga3d, rstn_gzip,
	/* 80 - 85 */
	rstn_cap, rstn_ga2d, rstn_disp_reg, rstn_disp, rstn_osdp, rstn_gp,
	/* 86 - 92 */
	rstn_sp, rstn_emac, rstn_uart, rstn_i2c, rstn_sci, rstn_spi, rstn_dma,
	/* 93 - 97 */
	rstn_smc, rstn_mmc, rstn_usb2_host2, rstn_usb3, rstn_usb2_otg_prst,
	/* 98 - 101 */
	rstn_usb2_otg, rstn_usb2_host1, rst_usb3_phy, rst_usb2_otg_por,

	/* TODO: add clocks */
	apb_pclk,

	nr_clks,
};

enum {
	PLL0_CPU = 0,
	PLL1_AMS,
	PLL2_GPU,
	PLL3_DSP,
	PLL4_LVDS,
	PLL5_DDR,
	PLL_MAX, /* sentinel */
};

#define PLL0_PMS       0x000
#define PLL1_PMS       0x004
#define PLL2_PMS       0x008
#define PLL3_PMS       0x00C
#define PLL4_PMS       0x010
#define PLL5_PMS       0x014
#define PLL3_K         0x02C
#define PLL4_K         0x030
#define PLL5_K         0x034

#define MASK_CLK0      0x144
#define MASK_CLK1      0x148

#define SW_RESET0      0x154
#define SW_RESET1      0x158

static __initdata unsigned long sdp1202_clk_regs[] = {
	PLL0_PMS,
	PLL1_PMS,
	PLL2_PMS,
	PLL3_PMS,
	PLL4_PMS,
	PLL5_PMS,
	PLL4_K,
	PLL5_K,
	MASK_CLK0,
	MASK_CLK1,
	SW_RESET0,
	SW_RESET1,

	/* TODO: add regs.. */
};

static unsigned long _get_rate(const char *clk_name)
{
	struct clk *clk;
	unsigned long rate;

	clk = clk_get(NULL, clk_name);
	if (IS_ERR(clk)) {
		pr_err("%s: could not find clock %s\n", __func__, clk_name);
		return 0;
	}
	rate = clk_get_rate(clk);
	clk_put(clk);
	return rate;
}

static void __iomem* __init sdp_clk_init(struct device_node *np,
		unsigned long nr_clk, unsigned long *rdump,
		unsigned long nr_rdump, unsigned long *soc_rdump,
		unsigned long nr_soc_rdump)
{
	struct resource res;
	
	if (!np)
		goto sdp_clk_init_err;
	
	/* to get physical address of pmu registers */	
	if (of_address_to_resource(np, 0, &res))
		goto sdp_clk_init_err;
	reg_phy_base = res.start;
	reg_phy_end = res.end;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		goto sdp_clk_init_err;

#ifdef CONFIG_OF
	clk_table = kzalloc(sizeof(struct clk *) * nr_clk, GFP_KERNEL);
	if (!clk_table)
		panic("could not allocate clock lookup table\n");

	clk_data.clks = clk_table;
	clk_data.clk_num = nr_clk;

	/* TODO: use string instead of number in the DTs */
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
#endif

#ifdef CONFIG_PM_SLEEP
	/* TODO: reg dump */
#endif
	return reg_base;

sdp_clk_init_err:
	panic("failed to map clock controller registers,"
			" aborting clock initialization\n");
	return NULL;
}

static struct sdp_fixed_rate sdp1202_fixed_rate_ext_clks[] __initdata = {
	{ .id = fin_pll, .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

static void sdp_clk_add_lookup(struct clk *clk, unsigned int id)
{
	if (clk_table && id)
		clk_table[id] = clk;
}

static void __init sdp_clk_register_fixed_rate(struct sdp_fixed_rate *clks,
		unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int i;
	int r;

	for (i = 0; i < nr_clk; i++, clks++) {
		clk = clk_register_fixed_rate(NULL, clks->name,
				clks->parent_name, clks->flags,
				clks->fixed_rate);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		sdp_clk_add_lookup(clk, clks->id);

		r = clk_register_clkdev(clk, clks->name, NULL);
		if (r)
			pr_err("%s: failed to register clock lookup for %s\n",
					__func__, clks->name);
	}
}

static void __init sdp_clk_of_register_fixed_ext(
		struct sdp_fixed_rate *clk, unsigned int nr_clk,
		struct of_device_id *clk_matches)
{
	const struct of_device_id *match;
	struct device_node *np;
	u32 freq;

	for_each_matching_node_and_match(np, clk_matches, &match) {
		if (of_property_read_u32(np, "clock-frequency", &freq))
			continue;
		clk[(u32)match->data].fixed_rate = freq;
	}

	sdp_clk_register_fixed_rate(clk, nr_clk);
}

static __initdata struct of_device_id ext_clk_match[] = {
	{ .compatible = "samsung,sdp-clock-fin", .data = (void *)0, },
	{ /* sentinel */ },
};

#define PMS_MASK(bits)	((1UL << bits) - 1)

struct sdp_clk_pll {
	struct clk_hw		hw;
	const void __iomem	*reg_pms;
	const void __iomem	*reg_k;
	struct sdp_pll_bitfield	layout;
};

#define to_sdp_clk_pll(_hw) container_of(_hw, struct sdp_clk_pll, hw)

static inline unsigned long sdp_pll_calc_f_out(u64 f_in,
		int p, int m, int s, int k, int k_scale)
{
	f_in = (u64)((f_in * m) + ((f_in * k) >> k_scale));
	do_div(f_in, (p << s));

	return (unsigned long)f_in;
}

static unsigned long sdp_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct sdp_clk_pll *pll = to_sdp_clk_pll(hw);
	struct sdp_pll_bitfield *layout= &pll->layout;
	u32 pll_pms;
	int pll_k, k_scale;
	u32 mdiv;
	u32 pdiv;
	u32 sdiv;

	pll_pms = __raw_readl(pll->reg_pms);
	mdiv = (pll_pms >> layout->m_shift) & PMS_MASK(layout->m_bits);
	pdiv = (pll_pms >> layout->p_shift) & PMS_MASK(layout->p_bits);
	sdiv = (pll_pms >> layout->s_shift) & PMS_MASK(layout->s_bits);
	sdiv -= layout->s_delta;
	k_scale = layout->k_scale ? layout->k_scale : 16;

	if (pll->reg_k) {
		u32 k = __raw_readl(pll->reg_k) & PMS_MASK(layout->k_bits);
		pll_k = (k >> (layout->k_bits - 1)) << 31;
		pll_k = pll_k >> (31 - layout->k_bits);
		pll_k = pll_k | k;
	} else
		pll_k = 0;

	return sdp_pll_calc_f_out(parent_rate,
			pdiv, mdiv, sdiv, pll_k, k_scale);
}

static long sdp_pll_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	printk("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, *prate);

	/* TODO: use pms table */

	return sdp_pll_recalc_rate(hw, *prate);
}

static int sdp_pll_set_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long prate)
{
	printk("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, prate);

	/* TODO: remove 'TODO'  */

	return 0;
}

static const struct clk_ops sdp_pll_clk_ops = {
	.recalc_rate = sdp_pll_recalc_rate,
	.round_rate = sdp_pll_round_rate,
	.set_rate = sdp_pll_set_rate,
};

static struct clk * __init sdp_clk_register_pll(const char *name,
		const char *parent, const void __iomem *reg_pms,
		const void __iomem *reg_k,
		const struct sdp_pll_bitfield *layout)
{
	struct sdp_clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init = {
		.name = name,
		.ops = &sdp_pll_clk_ops,
		.flags = CLK_GET_RATE_NOCACHE,
		.parent_names = &parent,
		.num_parents = 1,
	};
	int r;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	pll->hw.init = &init;
	pll->reg_pms = reg_pms;
	pll->reg_k = reg_k;
	pll->layout = *layout;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	r = clk_register_clkdev(clk, name, NULL);
	if (r)
		pr_err("%s: failed to register lookup for %s\n", __func__,
				name);

	return clk;
}

#define PLL2XXXX_MDIV_BITS  (10)
#define PLL2XXXX_PDIV_BITS  (6)
#define PLL2XXXX_SDIV_BITS  (3)
#define PLL2XXXX_KDIV_BITS  (16)
#define PLL2XXXX_MDIV_SHIFT (8)
#define PLL2XXXX_PDIV_SHIFT (20)
#define PLL2XXXX_SDIV_SHIFT (0)

static struct clk * __init sdp_clk_register_pll2xxxx(const char *name,
		const char *parent, const void __iomem *reg_pms,
		const void __iomem *reg_k)
{
	struct sdp_pll_bitfield pll2xxxx_layout = {
		.p_shift = PLL2XXXX_PDIV_SHIFT,
		.m_shift = PLL2XXXX_MDIV_SHIFT,
		.s_shift = PLL2XXXX_SDIV_SHIFT,
		.p_bits = PLL2XXXX_PDIV_BITS,
		.m_bits = PLL2XXXX_MDIV_BITS,
		.s_bits = PLL2XXXX_SDIV_BITS,
		.k_bits = PLL2XXXX_KDIV_BITS,
	};

	return sdp_clk_register_pll(name, parent, reg_pms, reg_k, &pll2xxxx_layout);
}

struct sdp_plls {
	unsigned int id;
	const char *name;
	const char *parent;
	unsigned int off_ams;
	unsigned int off_k;
};

static __initdata struct sdp_plls sdp1202_plls[] = {
	{ .id = pll0_cpu, .name = "pll0_cpu", .parent = "fin_pll",
		.off_ams = PLL0_PMS, },
	{ .id = pll1_ams, .name = "pll1_ams", .parent = "fin_pll",
		.off_ams = PLL1_PMS, },
	{ .id = pll2_gpu, .name = "pll2_gpu", .parent = "fin_pll",
		.off_ams = PLL2_PMS, },
	{ .id = pll3_dsp, .name = "pll3_dsp", .parent = "fin_pll",
		.off_ams = PLL3_PMS, },
	{ .id = pll4_lvds, .name = "pll4_lvds", .parent = "fin_pll",
		.off_ams = PLL4_PMS, .off_k = PLL4_K, },
	{ .id = pll5_ddr, .name = "pll5_ddr", .parent = "fin_pll",
		.off_ams = PLL5_PMS, .off_k = PLL5_K, },
};

static __initdata struct sdp_plls sdp1304_plls[] = {
	{ .id = pll0_cpu, .name = "pll0_cpu", .parent = "fin_pll",
		.off_ams = PLL0_PMS, },
	{ .id = pll1_ams, .name = "pll1_ams", .parent = "fin_pll",
		.off_ams = PLL1_PMS, },
	{ .id = pll2_gpu, .name = "pll2_gpu", .parent = "fin_pll",
		.off_ams = PLL2_PMS, },
	{ .id = pll3_dsp, .name = "pll3_dsp", .parent = "fin_pll",
		.off_ams = PLL3_PMS, .off_k = PLL3_K, },
	{ .id = pll4_lvds, .name = "pll4_lvds", .parent = "fin_pll",
		.off_ams = PLL4_PMS, .off_k = PLL4_K, },
	{ .id = pll5_ddr, .name = "pll5_ddr", .parent = "fin_pll",
		.off_ams = PLL5_PMS, .off_k = PLL5_K, },
};

/* TODO: fill the parent clock */
static struct sdp_gate_clock sdp1202_gate_clks[] __initdata = {
/*
	{ .id = usb2_0_hclk, .name = "usb2_0_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 31, },
	{ .id = tz_mem, .name = "tz_mem", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 30, },
	{ .id = gzip, .name = "gzip", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 29, },
	{ .id = se_hclk, .name = "se_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 28, },
	{ .id = usb2_1_hclk, .name = "usb2_1_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 27, },
	{ .id = usb2_1_phy, .name = "usb2_1_phy", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 26, },
	{ .id = usb2_1_ohci48, .name = "usb2_1_ohci48", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 25, },
	{ .id = usb2_1_utmi_phy, .name = "usb2_1_utmi_phy", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 24, },
	{ .id = usb2_2_hclk, .name = "usb2_2_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 23, },
	{ .id = usb2_2_phy, .name = "usb2_2_phy", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 22, },
	{ .id = usb2_2_ohci48, .name = "usb2_2_ohci48", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 21, },
	{ .id = usb2_2_utmi_phy, .name = "usb2_2_utmi_phy", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 20, },
	{ .id = usb3_hclk, .name = "usb3_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 19, },
	{ .id = usb3_suspend, .name = "usb3_suspend", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 18, },
	{ .id = usb3_utmi, .name = "usb3_utmi", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 17, },
	{ .id = usb3_pipe3_rx_pclk, .name = "usb3_pipe3_rx_pclk",
		.parent = NULL, .offset = MASK_CLK0, .bit_idx = 16, },
	{ .id = usb3_pipe3_tx_pclk, .name = "usb3_pipe3_tx_pclk",
		.parent = NULL, .offset = MASK_CLK0, .bit_idx = 15, },
	{ .id = emac_125M, .name = "emac_125M", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 14, },
	{ .id = emac_phy_rx, .name = "emac_phy_rx", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 13, },
	{ .id = emac_hclk, .name = "emac_hclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 12, },
	{ .id = mmc_hclk, .name = "mmc_hclk", .parent = "ams_clk",
		.alias = "mmc", .dev_name = "sdp-mmc",
		.flags = CLK_SET_RATE_PARENT,
		.offset = MASK_CLK0, .bit_idx = 11, },
	{ .id = mmc_cclk_in, .name = "mmc_cclk_in", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 10, },
	{ .id = mmc_cclk_in_drv, .name = "mmc_cclk_in_drv", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 9, },
	{ .id = mmc_cclk_in_sam, .name = "mmc_cclk_in_sam", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 8, },
	{ .id = dma330_clk, .name = "dma330_clk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 7, },
	{ .id = dma330_pclk, .name = "dma330_pclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 6, },
	{ .id = xmif_ab_bclk, .name = "xmif_ab_bclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 5, },
	{ .id = xmif_ab_pclk, .name = "xmif_ab_pclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 4, },
	{ .id = xmif_ab_mclk, .name = "xmif_ab_mclk", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 3, },
	{ .id = bus_gate, .name = "bus_gate", .parent = NULL,
		.offset = MASK_CLK0, .bit_idx = 0, },
	{ .id = dsp2_bclk, .name = "dsp2_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 21, },
	{ .id = dsp1_bclk, .name = "dsp1_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 20, },
	{ .id = ga2d_bclk, .name = "ga2d_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 15, },
	{ .id = cap_en, .name = "cap_en", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 14, },
	{ .id = osdp_dclk, .name = "osdp_dclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 13, },
	{ .id = gp_dclk, .name = "gp_dclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 12, },
	{ .id = sp_dclk, .name = "sp_dclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 11, },
	{ .id = osdp_bclk, .name = "osdp_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 10, },
	{ .id = gp_bclk, .name = "gp_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 9, },
	{ .id = sp_bclk, .name = "sp_bclk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 8, },
	{ .id = gpu_gate, .name = "gpu_gate", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 7, },
	{ .id = jtag_clk, .name = "jtag_clk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 3, },
	{ .id = ddrphya_clk, .name = "ddrphya_clk", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 2, },
	{ .id = usb2_2, .name = "usb2_2", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 1, },
	{ .id = usb2_1, .name = "usb2_1", .parent = NULL,
		.offset = MASK_CLK1, .bit_idx = 0, },
	{ .id = wdt_reset, .name = "wdt_reset", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 31, },
	{ .id = rstn_xmif, .name = "rstn_xmif", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 28, },
	{ .id = rstn_dsp2, .name = "rstn_dsp2", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 26, },
	{ .id = rstn_dsp1, .name = "rstn_dsp1", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 25, },
	{ .id = rstn_ga3d, .name = "rstn_ga3d", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 24, },
	{ .id = rstn_gzip, .name = "rstn_gzip", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 23, },
	{ .id = rstn_cap, .name = "rstn_cap", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 22, },
	{ .id = rstn_ga2d, .name = "rstn_ga2d", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 21, },
	{ .id = rstn_disp_reg, .name = "rstn_disp_reg", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 20, },
	{ .id = rstn_disp, .name = "rstn_disp", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 19, },
	{ .id = rstn_osdp, .name = "rstn_osdp", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 18, },
	{ .id = rstn_gp, .name = "rstn_gp", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 17, },
	{ .id = rstn_sp, .name = "rstn_sp", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 16, },
	{ .id = rstn_emac, .name = "rstn_emac", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 15, },
	{ .id = rstn_uart, .name = "rstn_uart", .parent = "ahb_clk",
		.alias = "uart", .dev_name = "sdp-uart",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SW_RESET0, .bit_idx = 14, },
*/
	{ .id = rstn_i2c, .name = "rstn_i2c", .parent = "ahb_clk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SW_RESET0, .bit_idx = 13, },
/*
	{ .id = rstn_sci, .name = "rstn_sci", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 12, },
	{ .id = rstn_spi, .name = "rstn_spi", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 11, },
	{ .id = rstn_dma, .name = "rstn_dma", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 10, },
	{ .id = rstn_smc, .name = "rstn_smc", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 9, },
	{ .id = rstn_mmc, .name = "rstn_mmc", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 8, },
	{ .id = rstn_usb2_host2, .name = "rstn_usb2_host2", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 6, },
	{ .id = rstn_usb3, .name = "rstn_usb3", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 4, },
	{ .id = rstn_usb2_otg_prst, .name = "rstn_usb2_otg_prst",
		.parent = NULL, .offset = SW_RESET0, .bit_idx = 2, },
	{ .id = rstn_usb2_otg, .name = "rstn_usb2_otg", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 1, },
	{ .id = rstn_usb2_host1, .name = "rstn_usb2_host1", .parent = NULL,
		.offset = SW_RESET0, .bit_idx = 0, },
	{ .id = rst_usb3_phy, .name = "rst_usb3_phy", .parent = NULL,
		.offset = SW_RESET1, .bit_idx = 5, },
	{ .id = rst_usb2_otg_por, .name = "rst_sub2_otg_por", .parent = NULL,
		.offset = SW_RESET1, .bit_idx = 3, },
 */
};

static void __init sdp_clk_register_gate(struct sdp_gate_clock *clks,
		unsigned int nr_clk)
{
	struct clk *clk;
	int i;
	int r;

	for (i = 0; i < (int) nr_clk; i++, clks++) {
		clk = clk_register_gate(NULL, clks->name, clks->parent,
				clks->flags, reg_base + clks->offset,
				clks->bit_idx, clks->gate_flags, &sdp_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		if (clks->alias) {
			r = clk_register_clkdev(clk, clks->alias,
					clks->dev_name);
			if (r)
				pr_err("%s: failed to register lookup %s\n",
						__func__, clks->alias);
		}

		sdp_clk_add_lookup(clk, clks->id);
	}
}

static struct sdp_fdiv_clock sdp1202_fdiv_clks[] __initdata = {
	{ .id = arm_clk, .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	{ .id = spi_clk, .name = "spi_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 2), .alias = "spi_clk", },
	{ .id = irr_clk, .name = "irr_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 10), .alias = "irr_clk", },
	{ .id = sci_clk, .name = "sci_clk", .parent = "pll1_ams",
		.mult = 1, .div = 37, .alias = "sci_clk", },
	{ .id = emmc_clk, .name = "emmc_clk", .parent = "pll1_ams",
		.dev_name = "sdp-mmc",
		.mult = 1, .div = 10, .alias = "emmc_clk", },
	{ .id = gpu_clk, .name = "gpu_clk", .parent = "pll2_gpu",
		.mult = 1, .div = 2, .alias = "gpu_clk", },
	{ .id = dma_clk, .name = "dma_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "dma_clk", },
	{ .id = ahb_clk, .name = "ahb_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 6, .alias = "ahb_clk", },
	{ .id = video_clk, .name = "video_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 2, .alias = "video_clk", },
	{ .id = gzip_clk, .name = "gzip_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "gzip_clk", },
	{ .id = g2d_clk, .name = "g2d_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "g2d_clk", },
	{ .id = plane_clk, .name = "plane_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 5, .alias = "plane_clk", },
	{ .id = lvds_clk, .name = "lvds_clk", .parent = "pll4_lvds",
		.mult = 1, .div = 1, .alias = "lvds_clk", },
	{ .id = lvds2_clk, .name = "lvds2_clk", .parent = "pll4_lvds",
		.mult = 2, .div = 1, .alias = "lvds2_clk", },
	{ .id = ebus_clk, .name = "ebus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 8, .alias = "ebus_clk", },
	{ .id = bus_clk, .name = "bus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "bus_clk", },
	{ .id = ddr2_clk, .name = "ddr2_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 2, .alias = "ddr2_clk", },
	{ .id = ddr4_clk, .name = "ddr4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "ddr4_clk", },
	{ .id = ddr16_clk, .name = "ddr16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "ddr16_clk", },
	{ .id = xmif4_clk, .name = "xmif4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "xmif4_clk", },
	{ .id = xmif16_clk, .name = "xmif16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "xmif16_clk", },
	{ .id = apb_pclk, .name = "apb_pclk", .parent = "ahb_clk",
		.mult = 1, .div = 1, .alias = "apb_pclk", },
};

static struct sdp_fdiv_clock sdp1304_fdiv_clks[] __initdata = {
	{ .id = arm_clk, .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	{ .id = spi_clk, .name = "spi_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 2), .alias = "spi_clk", },
	{ .id = irr_clk, .name = "irr_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 10), .alias = "irr_clk", },
	{ .id = sci_clk, .name = "sci_clk", .parent = "pll1_ams",
		.mult = 1, .div = 37, .alias = "sci_clk", },
	{ .id = emmc_clk, .name = "emmc_clk", .parent = "pll1_ams",
		.dev_name = "sdp-mmc",
		.mult = 1, .div = 4, .alias = "emmc_clk", },
	{ .id = gpu_clk, .name = "gpu_clk", .parent = "pll2_gpu",
		.mult = 1, .div = 2, .alias = "gpu_clk", },
	{ .id = dma_clk, .name = "dma_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "dma_clk", },
	{ .id = ahb_clk, .name = "ahb_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 8, .alias = "ahb_clk", },
	{ .id = video_clk, .name = "video_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 2, .alias = "video_clk", },
	{ .id = gzip_clk, .name = "gzip_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "gzip_clk", },
	{ .id = g2d_clk, .name = "g2d_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "g2d_clk", },
	{ .id = plane_clk, .name = "plane_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 5, .alias = "plane_clk", },
	{ .id = lvds_clk, .name = "lvds_clk", .parent = "pll4_lvds",
		.mult = 1, .div = 1, .alias = "lvds_clk", },
	{ .id = lvds2_clk, .name = "lvds2_clk", .parent = "pll4_lvds",
		.mult = 2, .div = 1, .alias = "lvds2_clk", },
	{ .id = ebus_clk, .name = "ebus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 8, .alias = "ebus_clk", },
	{ .id = bus_clk, .name = "bus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "bus_clk", },
	{ .id = ddr2_clk, .name = "ddr2_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 2, .alias = "ddr2_clk", },
	{ .id = ddr4_clk, .name = "ddr4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "ddr4_clk", },
	{ .id = ddr16_clk, .name = "ddr16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "ddr16_clk", },
	{ .id = xmif4_clk, .name = "xmif4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "xmif4_clk", },
	{ .id = xmif16_clk, .name = "xmif16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "xmif16_clk", },
	{ .id = apb_pclk, .name = "apb_pclk", .parent = "ahb_clk",
		.mult = 1, .div = 1, .alias = "apb_pclk", },
};

static void __init sdp_clk_register_fixed_div(struct sdp_fdiv_clock *clks,
		unsigned int nr_clk)
{
	struct clk *clk;
	int i;
	int r;

	for (i = 0; i < (int) nr_clk; i++, clks++) {
		clk = clk_register_fixed_factor(NULL, clks->name, clks->parent,
				clks->flags, clks->mult, clks->div);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		if (clks->alias || clks->dev_name ) {
			r = clk_register_clkdev(clk, clks->alias,
					clks->dev_name);
			if (r)
				pr_err("%s: failed to register lookup %s\n",
						__func__, clks->alias);
		}

		sdp_clk_add_lookup(clk, clks->id);
	}
}

static void __init sdp1202_clk_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk;
	struct sdp_plls *pll;
	int i;

	/* TODO: specify the register to save for suspend/resume. */
	base = sdp_clk_init(np, nr_clks, sdp1202_clk_regs,
			ARRAY_SIZE(sdp1202_clk_regs), NULL, 0);
	sdp_clk_of_register_fixed_ext(sdp1202_fixed_rate_ext_clks,
			ARRAY_SIZE(sdp1202_fixed_rate_ext_clks), ext_clk_match);

	for (i = 0; i < ARRAY_SIZE(sdp1202_plls); i++) {
		pll = &sdp1202_plls[i];
		clk = sdp_clk_register_pll2xxxx(pll->name, pll->parent,
				base + pll->off_ams,
				pll->off_k ? base + pll->off_k : NULL);
		sdp_clk_add_lookup(clk, pll->id);
	}

	/* TODO: register mux */
	sdp_clk_register_fixed_div(sdp1202_fdiv_clks,
			ARRAY_SIZE(sdp1202_fdiv_clks));
	sdp_clk_register_gate(sdp1202_gate_clks, ARRAY_SIZE(sdp1202_gate_clks));

	for (i = 0; i < (int) ARRAY_SIZE(sdp1202_plls); i++) {
		pll = &sdp1202_plls[i];
		pr_debug("SDP1202: %s = %ldHz\n",
				pll->name, _get_rate(pll->name));
	}

	pr_info("SDP1202 clocks: ARM %ldHz DDR %ldHz AHB %ldHz\n",
			_get_rate("arm_clk"), _get_rate("pll5_ddr"),
			_get_rate("ahb_clk"));
}

static void __init sdp1304_clk_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk;
	struct sdp_plls *pll;
	int i;

	/* TODO: specify the register to save for suspend/resume. */
	base = sdp_clk_init(np, nr_clks, sdp1202_clk_regs,
			ARRAY_SIZE(sdp1202_clk_regs), NULL, 0);
	sdp_clk_of_register_fixed_ext(sdp1202_fixed_rate_ext_clks,
			ARRAY_SIZE(sdp1202_fixed_rate_ext_clks), ext_clk_match);

	for (i = 0; i < (int) ARRAY_SIZE(sdp1304_plls); i++) {
		pll = &sdp1304_plls[i];
		clk = sdp_clk_register_pll2xxxx(pll->name, pll->parent,
				base + pll->off_ams,
				pll->off_k ? base + pll->off_k : NULL);
		sdp_clk_add_lookup(clk, pll->id);
	}

	/* TODO: register mux */
	sdp_clk_register_fixed_div(sdp1304_fdiv_clks,
			ARRAY_SIZE(sdp1304_fdiv_clks));
	sdp_clk_register_gate(sdp1202_gate_clks, ARRAY_SIZE(sdp1202_gate_clks));

	for (i = 0; i < (int) ARRAY_SIZE(sdp1304_plls); i++) {
		pll = &sdp1304_plls[i];
		pr_debug("SDP1304: %s = %ldHz\n",
				pll->name, _get_rate(pll->name));
	}

	pr_info("SDP1304 clocks: ARM %ldHz DDR %ldHz APB %ldHz\n",
			_get_rate("arm_clk"), _get_rate("pll5_ddr"),
			_get_rate("apb_pclk"));
}

#include "clk-sdp1302.c"
#include "clk-sdp1307.c"

static const __initconst struct of_device_id clk_match[] = {
	{ .compatible = "samsung,sdp1202-clock", .data = sdp1202_clk_init, },
	{ .compatible = "samsung,sdp1302-clock", .data = sdp1302_clk_init, },
	{ .compatible = "samsung,sdp1304-clock", .data = sdp1304_clk_init, },
	{ .compatible = "samsung,sdp1307-clock", .data = sdp1307_clk_init, },
	{ /* sentinel */ }
};

void __init sdp_init_clocks(void)
{
	printk("[%d] %s\n", __LINE__, __func__);

	of_clk_init(clk_match);
}

extern int sdp_pmu_regset(void* reg_addr, u32 mask, u32 value);
int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value)
{
	void *addr;
	
	if((phy_addr < reg_phy_base) || (phy_addr > reg_phy_end)) {
		panic("%s: invalid address for clockgating! addr=0x%08X\n", __func__, phy_addr);
		return -EINVAL;
	}

	addr = reg_base + (phy_addr - reg_phy_base);
	return sdp_pmu_regset(addr, mask, value);
}
EXPORT_SYMBOL(sdp_set_clockgating);

