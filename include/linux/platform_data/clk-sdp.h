#ifndef _CLK_SDP_H_
#define _CLK_SDP_H_

#include <linux/clk.h>
#include <linux/clkdev.h>

typedef unsigned long (sdp_clk_recalc_func)(unsigned long parent_rate);

struct clk *clk_register_sdp_scalable_clk(struct device *dev,
		const char *name, const char *parent_name, sdp_clk_recalc_func recalc);

/* pll calculations */
u32 pll4508_calc_freq(u32 input, u32 pms);
u32 pll4522_calc_freq(u32 input, u32 pms);
u32 pll4651_calc_freq(u32 input, u32 pms, u32 k);

u32 pll2553_calc_freq(u32 input, u32 pms);
u32 pll2650_calc_freq(u32 input, u32 pms, u32 k);

#endif

