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
#include <mach/motherboard.h>
#ifdef CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#endif

#define CORE_PERIPH_SIZE		(SZ_8K)
#define CORE_PERIPH_VIRT		(VMALLOC_END - CORE_PERIPH_SIZE)

#define NT72XXX_STBC_SIZE		(SZ_8K)
#define NT72XXX_STBC_VIRT		(CORE_PERIPH_VIRT - CORE_PERIPH_SIZE)
 
static const char * const v2m_dt_ca9_match[] __initconst = {
	"nvt,ca9",
	NULL,
};

static const char * const v2m_dt_ca53_match[] __initconst = {
	"nvt,ca53",
	NULL,
};
static struct map_desc ct_ca9x4_io_desc[] __initdata = {
	{
		.virtual	= CORE_PERIPH_VIRT,
		.length		= CORE_PERIPH_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= NT72XXX_STBC_VIRT,
		.pfn		= __phys_to_pfn(NT72XXX_STBC_BASE),
		.length 	= NT72XXX_STBC_SIZE,
		.type		= MT_DEVICE,
	},
};

struct v2m_bootprogress_t {
	/* mark boot progress to Micom SRAM */
	int (*mark_fn)(u32 value);
};

/* this function can be used after map_io(). */
static struct v2m_bootprogress_t bootprog = {NULL,};

int register_boot_progress_fn(int (*mark_fn)(u32))
{
	bootprog.mark_fn = mark_fn;
	return 0;
}
EXPORT_SYMBOL(register_boot_progress_fn);

int mark_boot_progress(u32 value)
{
	if (bootprog.mark_fn == NULL) {
		return -ENODEV;
	}

	bootprog.mark_fn(value);

	return 0;
}
EXPORT_SYMBOL(mark_boot_progress);

static int v2m_mark_boot_progress(u32 value)
{
	u32 temp;

	/* micom keypass */
	writel(NT72XXX_IPC_KEY_PASS_0, (void *)(NT72XXX_STBC_VIRT + NT72XXX_STBC_KEY));
	writel(NT72XXX_IPC_KEY_PASS_1, (void *)(NT72XXX_STBC_VIRT + NT72XXX_STBC_KEY));
	writel(NT72XXX_IPC_KEY_PASS_2, (void *)(NT72XXX_STBC_VIRT + NT72XXX_STBC_KEY_CHECK));

	/* mark boot progress to micom bank register byte4 */
	temp = readl((void *)(NT72XXX_STBC_VIRT + NT72XXX_STBC_BANK));
	temp &= ~M_NT72XXX_STBC_BANK_BYTE4;
	temp |= ((value << S_NT72XXX_STBC_BANK_BYTE4) & M_NT72XXX_STBC_BANK_BYTE4);
	writel(temp, (void *)(NT72XXX_STBC_VIRT + NT72XXX_STBC_BANK));

	return 0;
}

static void __init v2m_dt_map_io(void)
{
#if defined(CONFIG_ARCH_NVT72172) || defined(CONFIG_ARCH_NVT72673) || defined(CONFIG_ARCH_NVT72671D)
	ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(get_periph_base_ca53());
#else
	ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(get_periph_base());
#endif

#ifdef CONFIG_NVT_HW_CLOCK
	nvt_clk_vmap = 1;
#endif
	iotable_init(ct_ca9x4_io_desc, ARRAY_SIZE(ct_ca9x4_io_desc));
#if defined(CONFIG_ARCH_NVT72172) || defined(CONFIG_ARCH_NVT72673) || defined(CONFIG_ARCH_NVT72671D)
	debug_ll_io_init();
#endif
}

static void __init v2m_init_early(void)
{
	register_boot_progress_fn(v2m_mark_boot_progress);
	mark_boot_progress(0xB0);
}

DT_MACHINE_START(NVT72668_DT, "Novatek-Cortex A9")
	.dt_compat	= v2m_dt_ca9_match,
	.smp		= smp_ops(nvt_ca9_smp_ops),
	.map_io		= v2m_dt_map_io,
	.l2c_aux_val	= 0x00400000,
	.l2c_aux_mask	= 0xfe0fffff,
MACHINE_END

DT_MACHINE_START(NVT72172_DT, "Novatek-Cortex CA53")
	.dt_compat	= v2m_dt_ca53_match,
	.smp		= smp_ops(nvt_ca53_smp_ops),
	.map_io 	= v2m_dt_map_io,
	.init_early	= v2m_init_early,
MACHINE_END
