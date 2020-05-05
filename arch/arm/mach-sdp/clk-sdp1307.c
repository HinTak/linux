/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* SDP1307 PLL information
 * no		logic				fin	name	freq	devices
 * -----------------------------------------------------------------------------------------------------
 * PLL0		PULLABLE			24	DCXO	54		PLL/PHY input, 27MHz sub moudle, 6.75MHz SPI cloclk
 * PLL1		FMLPL36LH03			24	ARM		984		ARM
 * PLL2		FMLPL36SSLH03_512	24	DDR		792		DDR
 * PLL3		FMLPL36LH03			24	DSP		1968	Xroad I/F, SP, Graphic, TSD, AIO, AHB, SE, micom
 * PLL4		FMLPL36FH03			27	AUD1	1776	Audio sampling clock
 * PLL5		FMLPL36FH03			27	VID		1188	Main video clock
 * PLL6		FMLPL36MF01			4	GMAC	500		GMAC 125, HENC 250, SATA
 * PLL7		FMLPL36MF01			8	PCIE	400		PCIE	
 * PLL8		FMLPL36FH03			27	AUD2	1776	Audio sampling clock
 */

#define SDP1307REG_PLL0_PMS		0x00
#define SDP1307REG_PLL1_PMS		0x04
#define SDP1307REG_PLL2_PMS		0x08
#define SDP1307REG_PLL3_PMS		0x0c
#define SDP1307REG_PLL4_PMS		0x10
#define SDP1307REG_PLL5_PMS		0x14
#define SDP1307REG_PLL6_PMS		0x18
#define SDP1307REG_PLL7_PMS		0x1c
#define SDP1307REG_PLL8_PMS		0x20
#define SDP1307REG_PLL2_SS		0x24
#define SDP1307REG_PLL4_F		0x2C
#define SDP1307REG_PLL5_F		0x30
#define SDP1307REG_PLL8_F		0x34
#define SDP1307REG_CLK_MASK0	0xC0
#define SDP1307REG_CLK_MASK1	0xC4
#define SDP1307REG_CLK_MASK2	0xCC
#define SDP1307REG_CLK_MASK3	0xD0
#define SDP1307REG_CLK_MASK4	0xD4
#define SDP1307REG_CLK_MASK5	0xD8
#define SDP1307REG_CLK_MASK6	0xDC
#define SDP1307REG_SW_RESET0	0xF0
#define SDP1307REG_SW_RESET1	0xF4
#define SDP1307REG_SW_RESET2	0xF8

static unsigned long sdp1307_clk_regs[] __initdata = {
	SDP1307REG_PLL0_PMS,
	SDP1307REG_PLL1_PMS,
	SDP1307REG_PLL2_PMS,
	SDP1307REG_PLL3_PMS,
	SDP1307REG_PLL4_PMS,
	SDP1307REG_PLL5_PMS,
	SDP1307REG_PLL6_PMS,
	SDP1307REG_PLL7_PMS,
	SDP1307REG_PLL8_PMS,
	SDP1307REG_PLL2_SS,
	SDP1307REG_PLL4_F,
	SDP1307REG_PLL5_F,
	SDP1307REG_PLL8_F,
	SDP1307REG_CLK_MASK0,
	SDP1307REG_CLK_MASK1,
	SDP1307REG_CLK_MASK2,
	SDP1307REG_CLK_MASK3,
	SDP1307REG_CLK_MASK4,
	SDP1307REG_CLK_MASK5,
	SDP1307REG_CLK_MASK6,
	SDP1307REG_SW_RESET0,
	SDP1307REG_SW_RESET1,
	SDP1307REG_SW_RESET2,
};

struct sdp1307_pll_bitfield {
	u8 ratio;
	u8 ppm;
	u8 ip;
	u8 idivx;
	u8 idivfb;
	u8 idiv;
	u8 ssrate;
	u8 ssfreq;
	u8 ssmode;
	u8 sscf_en;
	u8 fden;
	u8 fnum;
};

struct sdp1307_pll {
	unsigned int	id;
	const char 		*name;
	const char		*parent;
	unsigned int	off_cont;
	unsigned int	off_opt;
	struct sdp1307_pll_bitfield	layout;
};

struct sdp1307_clk_pll {
	struct clk_hw		hw;
	const void __iomem	*reg_cont;
	const void __iomem	*reg_opt;
	struct sdp1307_pll_bitfield	layout;
	unsigned int		id;
};

/* Clock IDs for lookup. FIXME: remove these out! */

/* external clocks */
#define sdp1307_clk_fin_pll			1

/* plls */
#define sdp1307_clk_pll0_dcxo		2
#define sdp1307_clk_pll1_cpu		3
#define sdp1307_clk_pll2_ddr		4
#define sdp1307_clk_pll3_dsp		5
#define sdp1307_clk_pll4_audio1		6
#define sdp1307_clk_pll5_video		7
#define sdp1307_clk_pll6_gmac		8
#define sdp1307_clk_pll7_pcie		9
#define sdp1307_clk_pll8_audio2		10
/* fixed factor clks */
#define sdp1307_clk_arm_clk			11
#define sdp1307_clk_twd_clk			12
#define sdp1307_clk_apb_pclk		13
#define sdp1307_clk_gzip_clk		14
#define sdp1307_clk_usb_clk			15
#define sdp1307_clk_emmc_clk		16

#define sdp1307_clk_ddr_clk			17
#define sdp1307_clk_spi_clk			18
#define sdp1307_clk_sata_clk		19
#define sdp1307_clk_sdif_clk		21
/* gate clks */
#define sdp1307_clk_rstn_i2c		20
#define sdp1307_nr_clks				22

static struct sdp_fixed_rate sdp1307_fixed_rate_ext_clks[] __initdata = {
	{ .id = sdp1307_clk_fin_pll, .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

/* name --> connection ID */
static struct sdp1307_pll sdp1307_plls[] __initdata = {
	[0] = { .id = sdp1307_clk_pll0_dcxo, .name = "pll0_dcxo", .parent = "fin_pll",
		.off_cont = 0x0,
		.layout =  {
			.ratio = 0, .ppm = 4,
		},
	},
	[1] = { .id = sdp1307_clk_pll1_cpu, .name = "pll1_cpu", .parent = "fin_pll",
		.off_cont = 0x4,
		.layout =  {
			.ip = 0, .idivx = 4, .idivfb = 8,
		},
	},
	[2] = { .id = sdp1307_clk_pll2_ddr, .name = "pll2_ddr", .parent = "fin_pll",
		.off_cont = 0x8, .off_opt = 0x24,
		.layout =  {
			.ip = 0, .idivfb = 4, .idiv = 8,
			.ssrate = 0, .ssfreq = 12, .ssmode = 16, .sscf_en = 20,
		},
	},
	[3] = { .id = sdp1307_clk_pll3_dsp, .name = "pll3_dsp", .parent = "fin_pll",
		.off_cont = 0xC,
		.layout =  {
			.ip = 0, .idivx = 4, .idiv = 8, .idivfb = 16,
		},
	},
	[4] = { .id = sdp1307_clk_pll4_audio1, .name = "pll4_audio1", .parent = "pll0_dcxo",
		.off_cont = 0x10, .off_opt = 0x2C,
		.layout =  {
			.ip = 0, .idivfb = 4, .idiv = 8,
			.fden = 0, .fnum = 16,
		},
	},
	[5] = { .id = sdp1307_clk_pll5_video, .name = "pll5_video", .parent = "pll0_dcxo",
		.off_cont = 0x14, .off_opt = 0x30,
		.layout =  {
			.ip = 0, .idivfb = 4, .idiv = 8,
			.fden = 0, .fnum = 16,
		},
	},
	[6] = { .id = sdp1307_clk_pll6_gmac, .name = "pll6_gmac", .parent = "fin_pll",
		.off_cont = 0x18,
		.layout =  {
			.idivfb = 0,
		},
	},
	[7] = { .id = sdp1307_clk_pll7_pcie, .name = "pll7_pcie", .parent = "fin_pll",
		.off_cont = 0x1C,
		.layout =  {
			.idivfb = 0,
		},
	},
	[8] = { .id = sdp1307_clk_pll8_audio2, .name = "pll8_audio2", .parent = "pll0_dcxo",
		.off_cont = 0x20, .off_opt = 0x34,
		.layout =  {
			.ip = 0, .idivfb = 4, .idiv = 8,
			.fden = 0, .fnum = 16,
		},
	},
};

/* alias = con_id, dev_name = dev_id */
static struct sdp_fdiv_clock sdp1307_fdiv_clks[] __initdata = {
	{ .id = sdp1307_clk_arm_clk, .name = "arm_clk", .parent = "pll1_cpu",
		.mult = 1, .div = 2, .alias = "arm_clk", },
	{ .id = sdp1307_clk_twd_clk, .name = "twd_clk", .parent = "arm_clk",
		.mult = 1, .div = 4, .alias = "twd_clk", },
	{ .id = sdp1307_clk_apb_pclk, .name = "apb_pclk", .parent = "pll3_dsp",
		.mult = 1, .div = 12, .alias = "apb_pclk", },
	{ .id = sdp1307_clk_gzip_clk, .name = "gzip_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 6, .alias = "gzip_clk", },
	{ .id = sdp1307_clk_usb_clk, .name = "usb_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 12, .alias = "usb_clk", },
	{ .id = sdp1307_clk_emmc_clk, .name = "emmc_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 20, .alias = "emmc_clk", },
	{ .id = sdp1307_clk_sdif_clk, .name = "sdif_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 20, .alias = "sdif_clk", },
	{ .id = sdp1307_clk_ddr_clk, .name = "ddr_clk", .parent = "pll2_ddr",
		.mult = 1, .div = 2, .alias = "ddr_clk", },
	{ .id = sdp1307_clk_spi_clk, .name = "spi_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 12, .alias = "spi_clk", },
	{ .id = sdp1307_clk_sata_clk, .name = "sata_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 12, .alias = "sata_clk", },
};

static struct sdp_gate_clock sdp1307_gate_clks[] __initdata = {
	{ .id = sdp1307_clk_rstn_i2c, .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1307REG_SW_RESET1, .bit_idx = 5, },
};

static struct of_device_id sdp1307_ext_clk_match[] __initdata = {
	{ .compatible = "samsung,sdp-clock-fin", .data = (void *)0, },
	{ /* sentinel */ },
};

static unsigned long sdp1307_pll_pullable_calc(struct sdp1307_clk_pll * pll,
		unsigned long parent_rate)
{
	u32 val;
	//u8 ratio;
	u8 ppm;

	val = __raw_readl(pll->reg_cont);
	//ratio = (val >> pll->layout.ratio) & 0x7;
	ppm = (val >> pll->layout.ppm) & 0xFF;

	return (54000000 + (24000000 * (128 - ppm)));	
}

static unsigned long sdp1307_pll_fmlpl36lh03_calc(struct sdp1307_clk_pll * pll,
		unsigned long parent_rate)
{
	u32 val;
	u8 ip;
	//u8 idivx;
	u8 idivfb;

	val = __raw_readl(pll->reg_cont);
	ip = (val >> pll->layout.ip) & 0xF;
	//idivx = (val >> pll->layout.idivx) & 1;
	idivfb = (val >> pll->layout.idivfb) & 0xFF;

	return parent_rate * ((idivfb + 1)*2);
}

static unsigned long sdp1307_pll_fmlpl36sslh03_calc(struct sdp1307_clk_pll * pll,
		unsigned long parent_rate)
{
	u32 val;
	u8 ip;
	//u8 idivfb;
	u8 idiv;

	val = __raw_readl(pll->reg_cont);
	ip = (val >> pll->layout.ip) & 0xF;
	//idivfb = (val >> pll->layout.idivfb) & 0x1;
	idiv = (val >> pll->layout.idiv) & 0xFF;
	
	return parent_rate * idiv;
}

static unsigned long sdp1307_pll_fmlpl36fh03_calc(struct sdp1307_clk_pll * pll,
		unsigned long parent_rate)
{
	u32 val;
	u32 idiv;
	u32 fden;
	u32 fnum;
	u64 freq;

	val = __raw_readl(pll->reg_cont);
	idiv = (val >> pll->layout.idiv) & 0xFF;

	val = __raw_readl(pll->reg_opt);
	fden = (val >> pll->layout.fden) & 0xFFFF;
	fden = (fden >> 8) * 256 + (fden & 0xFF);
	fnum = (val >> pll->layout.fnum) & 0xFFFF;
	fnum = (fnum >> 8) * 256 + (fnum & 0xFF);

	parent_rate >>= 1;

	//printk("parent=%d, idiv=%d, fnum=%d, fden=%d\n", parent_rate, idiv, fnum, fden);
	freq = (u64)(fnum / fden) * 1000000;
	freq = ((u64)idiv * 1000000) + freq;
	freq = parent_rate / 1000000 * freq;
	
	return (unsigned long)freq;
}

static unsigned long sdp1307_pll_fmlpl36mf01_calc(struct sdp1307_clk_pll * pll,
		unsigned long parent_rate)
{
	u32 val;
	u8 idivfb;

	val = __raw_readl(pll->reg_cont);
	idivfb = (val >> pll->layout.idivfb) & 0x7F;
	
	return parent_rate * (idivfb + 1);
}

#define to_sdp1307_clk_pll(_hw) container_of(_hw, struct sdp1307_clk_pll, hw)

static unsigned long sdp1307_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct sdp1307_clk_pll *pll = to_sdp1307_clk_pll(hw);
	unsigned long freq = 0;

	switch (pll->id) {
	case sdp1307_clk_pll0_dcxo:
		freq = sdp1307_pll_pullable_calc(pll, parent_rate);
		break;
		
	case sdp1307_clk_pll1_cpu:
		freq = sdp1307_pll_fmlpl36lh03_calc(pll, parent_rate) / 2;
		break;
		
	case sdp1307_clk_pll3_dsp:
		freq = sdp1307_pll_fmlpl36lh03_calc(pll, parent_rate);
		break;
		
	case sdp1307_clk_pll2_ddr:
		freq = sdp1307_pll_fmlpl36sslh03_calc(pll, parent_rate) / 2;
		break;
		
	case sdp1307_clk_pll4_audio1:
	case sdp1307_clk_pll8_audio2:
	case sdp1307_clk_pll5_video:
		freq = sdp1307_pll_fmlpl36fh03_calc(pll, parent_rate);
		break;
		
	case sdp1307_clk_pll6_gmac:
		freq = sdp1307_pll_fmlpl36mf01_calc(pll, parent_rate / 6);
		break;
		
	case sdp1307_clk_pll7_pcie:
		freq = sdp1307_pll_fmlpl36mf01_calc(pll, parent_rate / 3);
		break;
		
	default:
		printk(KERN_WARNING "clock id is not matched(%d)", pll->id);
		break;
	}

	return freq;
}

static long sdp1307_pll_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	printk("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, *prate);

	/* TODO: use pms table */

	return sdp_pll_recalc_rate(hw, *prate);
}

static int sdp1307_pll_set_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long prate)
{
	printk("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, prate);

	/* TODO: remove 'TODO'  */

	return 0;
}

static const struct clk_ops sdp1307_pll_clk_ops = {
	.recalc_rate = sdp1307_pll_recalc_rate,
	.round_rate = sdp1307_pll_round_rate,
	.set_rate = sdp1307_pll_set_rate,
};

static struct clk * __init sdp1307_clk_register_pll(const char *name,
		const char *parent, const void __iomem *reg_cont,
		const void __iomem *reg_opt,
		const struct sdp1307_pll_bitfield *layout,
		const unsigned int id)
{
	struct sdp1307_clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init = {
		.name = name,
		.ops = &sdp1307_pll_clk_ops,
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
	pll->reg_cont = reg_cont;
	pll->reg_opt = reg_opt;
	pll->layout = *layout;
	pll->id = id;

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


static void __init sdp1307_clk_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk;
	struct sdp1307_pll *pll;
	int i;

	/* TODO: specify the register to save for suspend/resume. */
	base = sdp_clk_init(np, nr_clks, sdp1307_clk_regs,
			ARRAY_SIZE(sdp1307_clk_regs), NULL, 0);

	sdp_clk_of_register_fixed_ext(sdp1307_fixed_rate_ext_clks,
			ARRAY_SIZE(sdp1307_fixed_rate_ext_clks), sdp1307_ext_clk_match);

	/* 1st level plls */
	for (i = 0; i < (int) ARRAY_SIZE(sdp1307_plls); i++) {
		pll = &sdp1307_plls[i];
		clk = sdp1307_clk_register_pll(pll->name, pll->parent,
				base + pll->off_cont, pll->off_opt ? base + pll->off_opt : NULL,
				&pll->layout, pll->id);
		sdp_clk_add_lookup(clk, pll->id);
	}

	/* fixed clocks. TODO: use mux register */
	sdp_clk_register_fixed_div(sdp1307_fdiv_clks, ARRAY_SIZE(sdp1307_fdiv_clks));

	/* gate clocks */	
	sdp_clk_register_gate(sdp1307_gate_clks, ARRAY_SIZE(sdp1307_gate_clks));

	/* introduce plls */
	pr_info ("sdp1307 %-12s:%10lu Hz\n", "fin_pll", _get_rate("fin_pll"));
	for (i = 0; i < (int) ARRAY_SIZE(sdp1307_plls); i++) {
		pll = &sdp1307_plls[i];
		pr_info ("sdp1307 %-12s:%10lu Hz\n", pll->name, _get_rate(pll->name));
	}
}

