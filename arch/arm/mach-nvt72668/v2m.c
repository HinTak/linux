/*
 * Novatek CA9 Motherboard Support
 */
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <asm/arch_timer.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>

#include "mach/motherboard.h"
#include "core.h"

#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/serial_8250.h>

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/sections.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#endif
static struct resource NT72668_rtc[] = {
	[0] = {
		.start          = 0,
		.end            = 0,
		.flags          = IORESOURCE_MEM,
	}
};

static struct resource NT72668_wdt[] = {
	[0] = {
		.start          = 0,
		.end            = 0,
		.flags          = IORESOURCE_MEM,
	}
};

static struct platform_device NT72668_rtc_device = {
	.name           = "nt72668_rtc",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(NT72668_rtc),
	.resource       =  NT72668_rtc,
};

static struct platform_device NT72668_wdt_device = {
	.name           = "nt72668_wdt",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(NT72668_wdt),
	.resource       =  NT72668_wdt,
};

static struct platform_device *nvt_rtc_devices[] __initdata = {
	&NT72668_rtc_device,
};

static struct platform_device *nvt_wdt_devices[] __initdata = {
	&NT72668_wdt_device,
};

static struct map_desc ct_ca9x4_io_desc[] __initdata = {
	{
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	},
};

static void __init v2m_dt_map_io(void)
{
	if (!initial_boot_params)
		return;

	ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(get_periph_base());
	ct_ca9x4_io_desc[0].virtual = VMALLOC_END - ct_ca9x4_io_desc[0].length;

	iotable_init(ct_ca9x4_io_desc, ARRAY_SIZE(ct_ca9x4_io_desc));
#ifdef CONFIG_NVT_HW_CLOCK
	nvt_clk_vmap = 1;
#endif
}

static void __init v2m_dt_timer_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static void __init v2m_dt_init(void)
{
	platform_add_devices(nvt_rtc_devices, ARRAY_SIZE(nvt_rtc_devices));
	platform_add_devices(nvt_wdt_devices, ARRAY_SIZE(nvt_wdt_devices));
	l2x0_of_init(0x00400000, 0xfe0fffff);

	//of_platform_populate(NULL, v2m_dt_bus_match, NULL, NULL);
	//pm_power_off = nvt_ca9_power_off;
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const v2m_dt_match[] __initconst = {
	"novatek,ca9",
	NULL,
};

unsigned int Ker_chip = EN_SOC_NONE;
EXPORT_SYMBOL_GPL(Ker_chip);

static inline unsigned int get_chip_id(void)
{
	unsigned int* reg_base3;
	unsigned int  val;

	reg_base3 = ioremap_nocache(CHIP_ID_REG, 0x1000);
    val = *((volatile unsigned int *) reg_base3);
	iounmap(reg_base3);

    return (val & 0x0fff);
}


static void __init nt72668_late_init(void)
{
	//SOC determination
	Ker_chip = (get_chip_id() == EN_SOC_NT72656) ? EN_SOC_NT72656 : EN_SOC_NT72668 ;

}




DT_MACHINE_START(NVT72668_DT, "Novatek-Cortex A9")
	.dt_compat	= v2m_dt_match,
	.smp		= smp_ops(nvt_ca9_smp_ops),
	.map_io		= v2m_dt_map_io,
	.init_late	= nt72668_late_init,
	.init_time	= v2m_dt_timer_init,
	.init_machine	= v2m_dt_init,
	//.restart	= nvt_restart,
MACHINE_END

