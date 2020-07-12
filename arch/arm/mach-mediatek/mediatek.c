/*
 * Device Tree support for Mediatek SoCs
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <asm/mach/arch.h>
#if defined(CONFIG_MTK_HW_CLOCK)
#include <asm/mach/map.h>
#include <mach/mtk_hwclock.h>
#endif

static const char * const mediatek_board_dt_compat[] = {
	"mediatek,mt6589",
	"mediatek,mt6592",
	"mediatek,mt8127",
	"mediatek,mt8135",
	"mediatek,mt8167",
	"mediatek,mt8516",
	NULL,
};

#if defined(CONFIG_MTK_HW_CLOCK)
static struct map_desc systimer_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(SYSTIMER_IO_BASE),
		.pfn		= __phys_to_pfn(SYSTIMER_IO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void __init mediatek_map_io(void)
{
	iotable_init(systimer_io_desc, ARRAY_SIZE(systimer_io_desc));
	mtk_clk_vmap = 1;
}
#endif

DT_MACHINE_START(MEDIATEK_DT, "Mediatek Cortex-A7 (Device Tree)")
	.dt_compat	= mediatek_board_dt_compat,
#if defined(CONFIG_MTK_HW_CLOCK)
	.map_io		= mediatek_map_io,
#endif
MACHINE_END
