# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_COMMON_CLK_MEDIATEK) += clk-mtk.o clk-pll.o clk-gate.o clk-apmixed.o clk-cpumux.o
obj-$(CONFIG_COMMON_CLK_MT8167) += clk-mt8167.o clkchk_mt8167.o

ifeq ($(CONFIG_DEBUG_FS), y)
obj-$(CONFIG_COMMON_CLK_MEDIATEK) += clkdbg.o
obj-$(CONFIG_COMMON_CLK_MT8167) += clkdbg_mt8167.o
endif
