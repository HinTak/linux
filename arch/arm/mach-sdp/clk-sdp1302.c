/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* SDP1302 PLL information
 * no		logic	fin	name	freq	devices
 * ----------------------------------------------------
 * PLL0		4508	24	ARM	600	ARM
 * PLL1		4508	24	BUS	960	GZIP3 DMA3 BUS3 PERI6 USBHSIC2
 * PLL2		4522	24	CORE	1000	AE2 GPU3 HDMI MMCIF10
 * PLL3		4600	24	AUD0	294.9
 * PLL4		4600	24	AUD1	294.9
 * PLL5		4600	24	VID	297	VIDEO, JPEG, MPEGA, DP_NRFC
 * PLL6		4651	297	LVDS	297+a	DP_VE, AVD_SCL, SVDL
 * PLL7		4601	24	PULL	27
 * PLL8		4651	24	DDR	667	DDR
 */

#define SDP1302REG_PLL0_PMS	0x000
#define SDP1302REG_PLL1_PMS	0x004
#define SDP1302REG_PLL2_PMS	0x008
#define SDP1302REG_PLL3_PMS	0x00c
#define SDP1302REG_PLL4_PMS	0x010
#define SDP1302REG_PLL5_PMS	0x014
#define SDP1302REG_PLL6_PMS	0x018
#define SDP1302REG_PLL7_PMS	0x01c
#define SDP1302REG_PLL8_PMS	0x020
#define SDP1302REG_PLL3_K	0x02c
#define SDP1302REG_PLL4_K	0x030
#define SDP1302REG_PLL5_K	0x034
#define SDP1302REG_PLL6_K	0x038
#define SDP1302REG_PLL7_K	0x03c
#define SDP1302REG_PLL8_K	0x040
#define SDP1302REG_MASK_CH	0x140
#define SDP1302REG_MASK_CLK0	0x144
#define SDP1302REG_MASK_CLK1	0x148
#define SDP1302REG_MASK_CLK2	0x14c
#define SDP1302REG_SW_RESET0	0x154
#define SDP1302REG_SW_RESET1	0x158

static unsigned long sdp1302_clk_regs[] __initdata = {
	SDP1302REG_PLL0_PMS,
	SDP1302REG_PLL1_PMS,
	SDP1302REG_PLL2_PMS,
	SDP1302REG_PLL3_PMS,
	SDP1302REG_PLL4_PMS,
	SDP1302REG_PLL5_PMS,
	SDP1302REG_PLL6_PMS,
	SDP1302REG_PLL7_PMS,
	SDP1302REG_PLL8_PMS,
	SDP1302REG_PLL3_K,
	SDP1302REG_PLL4_K,
	SDP1302REG_PLL5_K,
	SDP1302REG_PLL6_K,
	SDP1302REG_PLL7_K,
	SDP1302REG_PLL8_K,
	SDP1302REG_MASK_CH,
	SDP1302REG_MASK_CLK0,
	SDP1302REG_MASK_CLK1,
	SDP1302REG_MASK_CLK2,
	SDP1302REG_SW_RESET0,
	SDP1302REG_SW_RESET1,
};

struct sdp1302_pll {
	unsigned int		id;
	const char 		*name;
	const char		*parent;
	unsigned int		off_pms;
	unsigned int		off_k;
	struct sdp_pll_bitfield	layout;
};

/* Clock IDs for lookup. FIXME: remove these out! */

/* external clocks */
#define sdp1302_clk_fin_pll		1
#define sdp1302_clk_fin_lvds		2
/* plls */
#define sdp1302_clk_pll0_cpu		3
#define sdp1302_clk_pll1_bus		4
#define sdp1302_clk_pll2_core		5
#define sdp1302_clk_pll3_audio0		6
#define sdp1302_clk_pll4_audio1		7
#define sdp1302_clk_pll5_video		8
#define sdp1302_clk_pll6_lvds		9
#define sdp1302_clk_pll7_pull		10
#define sdp1302_clk_pll8_ddr		11
/* fixed factor clks */
#define sdp1302_clk_arm_clk		12
#define sdp1302_clk_apb_pclk		13
#define sdp1302_clk_gzip_clk		14
#define sdp1302_clk_dma_clk		15
#define sdp1302_clk_usb_hsic_clk	16
#define sdp1302_clk_emmc_clk		17
#define sdp1302_clk_ddr_clk		18
#define sdp1302_clk_spi_clk		19
#define sdp1302_clk_smp_twd		20
/* gate clks */
#define sdp1302_clk_rstn_i2c		21
#define sdp1302_nr_clks			22

static struct sdp_fixed_rate sdp1302_fixed_rate_ext_clks[] __initdata = {
	{ .id = sdp1302_clk_fin_pll, .name = "fin_pll", .flags = CLK_IS_ROOT, },
	{ .id = sdp1302_clk_fin_lvds, .name = "fin_lvds", .flags = CLK_IS_ROOT, },
};

/* name --> connection ID */
static struct sdp1302_pll sdp1302_plls[] __initdata = {
	[0] = { .id = sdp1302_clk_pll0_cpu, .name = "pll0_cpu", .parent = "fin_pll",
		.off_pms = 0x000,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0, .s_delta = 1,
			.p_bits = 6, .m_bits = 10, .s_bits = 3,
		},
	},
	[1] = { .id = sdp1302_clk_pll1_bus, .name = "pll1_bus", .parent = "fin_pll",
		.off_pms = 0x004,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0, .s_delta = 1,
			.p_bits = 6, .m_bits = 10, .s_bits = 3,
		},
	},
	[2] = { .id = sdp1302_clk_pll2_core, .name = "pll2_core", .parent = "fin_pll",
		.off_pms = 0x008,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 10, .s_bits = 3,
		},
	},
	[3] = { .id = sdp1302_clk_pll3_audio0, .name = "pll3_audio0", .parent = "fin_pll",
		.off_pms = 0x00c, .off_k = 0x02c,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 9, .s_bits = 3, .k_bits = 16,
		},
	},
	[4] = { .id = sdp1302_clk_pll4_audio1, .name = "pll4_audio1", .parent = "fin_pll",
		.off_pms = 0x010, .off_k = 0x030,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 9, .s_bits = 3, .k_bits = 16,
		},
	},
	[5] = { .id = sdp1302_clk_pll5_video, .name = "pll5_video", .parent = "fin_pll",
		.off_pms = 0x014, .off_k = 0x034,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 9, .s_bits = 3, .k_bits = 16,
		},
	},
	[6] = { .id = sdp1302_clk_pll6_lvds, .name = "pll6_lvds", .parent = "fin_lvds",
		.off_pms = 0x018, .off_k = 0x038,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 9, .s_bits = 3, .k_bits = 16,
		},
	},
	[7] = { .id = sdp1302_clk_pll7_pull, .name = "pll7_pull", .parent = "fin_pll",
		.off_pms = 0x01c, .off_k = 0x03c,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 9, .s_bits = 3, .k_bits = 16,
		},
	},
	[8] = { .id = sdp1302_clk_pll8_ddr, .name = "pll8_ddr", .parent = "fin_pll",
		.off_pms = 0x020, .off_k = 0x040,
		.layout =  {
			.p_shift = 20, .m_shift = 8, .s_shift = 0,
			.p_bits = 6, .m_bits = 10, .s_bits = 3, .k_bits = 12, .k_scale = 10,
		},
	},
};

/* alias = con_id, dev_name = dev_id */
static struct sdp_fdiv_clock sdp1302_fdiv_clks[] __initdata = {
	{ .id = sdp1302_clk_arm_clk, .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	{ .id = sdp1302_clk_smp_twd, .name = "smp_twd", .parent = "arm_clk",
		.mult = 1, .div = 4, .dev_name = "smp_twd", },
	{ .id = sdp1302_clk_apb_pclk, .name = "apb_pclk", .parent = "pll1_bus",
		.mult = 1, .div = 6, .alias = "apb_pclk", },
	{ .id = sdp1302_clk_gzip_clk, .name = "gzip_clk", .parent = "pll1_bus",
		.mult = 1, .div = 3, .alias = "gzip_clk", },
	{ .id = sdp1302_clk_dma_clk, .name = "dma_clk", .parent = "pll1_bus",
		.mult = 1, .div = 3, .alias = "dma_clk", },
	{ .id = sdp1302_clk_usb_hsic_clk, .name = "usb_hsic_clk", .parent = "pll1_bus",
		.mult = 1, .div = 2, .alias = "usb_hsic_clk", },
	{ .id = sdp1302_clk_emmc_clk, .name = "emmc_clk", .parent = "pll1_bus",
		.dev_name = "sdp-mmc",
		.mult = 1, .div = 5, .alias = "emmc_clk", },
	{ .id = sdp1302_clk_spi_clk, .name = "spi_clk", .parent = "apb_pclk",
		.mult = 1, .div = 1, .alias = "spi_clk", },
	{ .id = sdp1302_clk_ddr_clk, .name = "ddr_clk", .parent = "pll8_ddr",
		.mult = 1, .div = 1, .alias = "ddr_clk", },
};

static struct sdp_gate_clock sdp1302_gate_clks[] __initdata = {
	{ .id = sdp1302_clk_rstn_i2c, .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1302REG_SW_RESET0, .bit_idx = 19, },
};

static struct of_device_id sdp1302_ext_clk_match[] __initdata = {
	{ .compatible = "samsung,sdp-clock-fin", .data = (void *)0, },
	{ .compatible = "samsung,sdp-clock-fin-lvds", .data = (void *)1, },
	{ /* sentinel */ },
};

static void __init sdp1302_clk_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk;
	struct sdp1302_pll *pll;
	int i;

	/* TODO: specify the register to save for suspend/resume. */
	base = sdp_clk_init(np, nr_clks, sdp1302_clk_regs,
			ARRAY_SIZE(sdp1302_clk_regs), NULL, 0);

	sdp_clk_of_register_fixed_ext(sdp1302_fixed_rate_ext_clks,
			ARRAY_SIZE(sdp1302_fixed_rate_ext_clks), sdp1302_ext_clk_match);

	/* 1st level plls */
	for (i = 0; i < (int) ARRAY_SIZE(sdp1302_plls); i++) {
		pll = &sdp1302_plls[i];
		clk = sdp_clk_register_pll(pll->name, pll->parent,
				base + pll->off_pms, pll->off_k ? base + pll->off_k : NULL,
				&pll->layout);
		sdp_clk_add_lookup(clk, pll->id);
	}

	/* fixed clocks. TODO: use mux register */
	sdp_clk_register_fixed_div(sdp1302_fdiv_clks, ARRAY_SIZE(sdp1302_fdiv_clks));

	/* gate clocks */	
	sdp_clk_register_gate(sdp1302_gate_clks, ARRAY_SIZE(sdp1302_gate_clks));

	/* introduce plls */
	pr_info ("sdp1302 %-12s:%10lu Hz\n", "fin_pll", _get_rate("fin_pll"));
	for (i = 0; i < (int) ARRAY_SIZE(sdp1302_plls); i++) {
		pll = &sdp1302_plls[i];
		pr_info ("sdp1302 %-12s:%10lu Hz\n", pll->name, _get_rate(pll->name));
	}
}

