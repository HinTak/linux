/*
 * Novatek CA9 Motherboard Support
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/sizes.h>
#include "core.h"
#ifdef CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#endif
 
static const char * const v2m_dt_match[] __initconst = {
	"nvt,ca9",
	"nvt,ca53",
	NULL,
};
static struct map_desc ct_ca9x4_io_desc[] __initdata = {
	{
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	},
};

static void __init v2m_dt_map_io(void)
{
#ifndef CONFIG_ARCH_NVT72172
	ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(get_periph_base());
#else
	ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(get_periph_base_ca53());
#endif
	ct_ca9x4_io_desc[0].virtual = VMALLOC_END - ct_ca9x4_io_desc[0].length;

#ifdef CONFIG_NVT_HW_CLOCK
	nvt_clk_vmap = 1;
#endif
	iotable_init(ct_ca9x4_io_desc, ARRAY_SIZE(ct_ca9x4_io_desc));
#if defined(CONFIG_ARCH_NVT72172) 
	debug_ll_io_init();
#endif
}

DT_MACHINE_START(NVT72668_DT, "Novatek-Cortex A9")
	.dt_compat	= v2m_dt_match,
	.smp		= smp_ops(nvt_ca9_smp_ops),
	.map_io		= v2m_dt_map_io,
	.l2c_aux_val	= 0x00400000,
	.l2c_aux_mask	= 0xfe0fffff,
MACHINE_END

DT_MACHINE_START(NVT72172_DT, "Novatek-Cortex CA53")
	.dt_compat	= v2m_dt_match,
	.smp		= smp_ops(nvt_ca53_smp_ops),
	.map_io 	= v2m_dt_map_io,
MACHINE_END
