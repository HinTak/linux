#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/cpu.h>

#include <linux/platform_data/clk-sdp.h>

struct clk_sdp_hw {
	struct clk_hw		hw;
	sdp_clk_recalc_func	*recalc;
};

#define to_sdp_hw(hw) container_of(hw, struct clk_sdp_hw, hw)

static unsigned long clk_sdp_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct clk_sdp_hw *sdp_hw = to_sdp_hw(hw);
	return sdp_hw->recalc(parent_rate);
}

static long clk_sdp_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	return rate;
}

static int clk_sdp_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	return 0;
}

const struct clk_ops clk_sdp_ops = {
	.recalc_rate = clk_sdp_recalc_rate,
	.round_rate = clk_sdp_round_rate,
	.set_rate = clk_sdp_set_rate,
};

/* not gatable, scalable, only 1 parent is permitted */
struct clk *clk_register_sdp_scalable_clk(struct device *dev,
		const char *name, const char *parent_name, sdp_clk_recalc_func recalc)
{
	struct clk *clk;
	struct clk_init_data init;
	struct clk_sdp_hw *sdp_hw;
	
	sdp_hw = kzalloc(sizeof(*sdp_hw), GFP_KERNEL);
	if (!sdp_hw) {
		pr_err("%s: could not allocate sdp_hw\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_sdp_ops;
	init.flags = CLK_IS_BASIC | CLK_IGNORE_UNUSED | (parent_name ? 0 : CLK_IS_ROOT);
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	sdp_hw->recalc = recalc;
	sdp_hw->hw.init = &init;

	clk = clk_register(dev, &sdp_hw->hw);
	if (IS_ERR(clk))
		kfree(sdp_hw);

	return clk;
}

#define PLL4508_P(x)	(((x) >> 20) & 0x3f)
#define PLL4508_M(x)	(((x) >> 8) & 0x3ff)
#define PLL4508_S(x)	((x) & 0x7)
u32 pll4508_calc_freq(u32 input, u32 pms)
{
	u32 ret;

	BUG_ON(PLL4508_S(pms) < 1);	

	ret = (input >> (PLL4508_S(pms) - 1));
	ret /= PLL4508_P(pms);
	ret *= PLL4508_M(pms);

	return ret;
}

#define PLL4522_P(x)	(((x) >> 20) & 0x3f)
#define PLL4522_M(x)	(((x) >> 8) & 0x3f)
#define PLL4522_S(x)	((x) & 0x7)
u32 pll4522_calc_freq(u32 input, u32 pms)
{
	u32 ret;
	
	ret = (input >> PLL4508_S(pms));
	ret /= PLL4508_P(pms);
	ret *= PLL4508_M(pms);

	return ret;
}

#define PLL4651_P(x)	((x) >> 20)
#define PLL4651_M(x)	(((x) >> 8) & 0x1ff)
#define PLL4651_S(x)	((x) & 0x7)
u32 pll4651_calc_freq(u32 input, u32 pms, u32 k)
{
	u32 ret;
	signed long long tmp;
	int sign = 1;

	tmp = (input >> PLL4651_S(pms)) / PLL4651_P(pms);
	if (k & 0x8000) {
		k = 0x10000 - k;
		sign = -1;
	}
	ret =  (u32) ((tmp * PLL4651_P(pms)) + (((tmp * k) >> 16) * sign));
	
	return ret;
}

#define PLL2553_P(x)	(((x) >> 20) & 0x3f)
#define PLL2553_M(x)	(((x) >> 8) & 0x3ff)
#define PLL2553_S(x)	((x) & 0x7)
u32 pll2553_calc_freq(u32 input, u32 pms)
{
	u32 ret;

	ret = input >> PLL2553_S(pms);
	ret /= PLL2553_P(pms);
	ret *= PLL2553_M(pms);

	return ret;
}

#define PLL2650_P(x)	(((x) >> 20) & 0x3f)
#define PLL2650_M(x)	(((x) >> 8) & 0x3f)
#define PLL2650_S(x)	((x) & 0x7)
#define PLL2650_K(x)	((x) & 0xFFFF)
u32 pll2650_calc_freq(u32 input, u32 pms, u32 k)
{
	u32 ret;
	long long tmp;
	int sign = 1;

	ret = (input >> PLL2650_S(pms));
	ret /= PLL2650_P(pms);
	tmp = ret;

	k = PLL2650_K(k);

	if(k & 0x8000)
	{
		k = 0x10000-k;
		sign = -1;
	}	
	ret =  (u32) ((tmp * PLL2650_M(pms)) + (((tmp * k) >> 16) * sign));

	return ret;
}


