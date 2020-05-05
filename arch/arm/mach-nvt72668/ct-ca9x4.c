/*
 * Novatek Cortex A9x4 Support
 */
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/clkdev.h>
#include <asm/hardware/gic.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>
#include <asm/hardware/timer-sp.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/irqs.h>
#include <linux/of_fdt.h>
#include "mach/ct-ca9x4.h"
#include "core.h"
#include "mach/motherboard.h"

void __iomem * Cortex_a9_base ;

static struct map_desc ct_ca9x4_io_desc[] __initdata = {
	{
		.virtual        = V2T_PERIPH,
		//.pfn            = __phys_to_pfn(CT_CA9X4_MPIC),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
    .virtual        = 0,
    //.pfn            = __phys_to_pfn(0xfd090000),
    .length         = SZ_128K,
    .type           = MT_DEVICE,
  }
};

static const char *nvt_dt_uart_match[] __initconst = {
	"nvt,serial8250",
	NULL
};

static int __init nvt_dt_find_uart(unsigned long node,
		const char *uname, int depth, void *data)
{
  if (of_flat_dt_match(node, nvt_dt_uart_match)) {
		phys_addr_t addr;
		
		__be32 *reg = of_get_flat_dt_prop(node, "reg", NULL);
		if (WARN_ON(!reg))
			 return -EINVAL;
		addr = be32_to_cpup(reg);
		if (ct_ca9x4_io_desc[1].virtual == 0) {
		      /* only do one time */
		      ct_ca9x4_io_desc[1].virtual = addr;
          ct_ca9x4_io_desc[1].pfn = __phys_to_pfn(addr);
		      return 0;
		}
		if (WARN_ON(!addr))
			return -EFAULT;
  }   
  return 0;
}

static void __init ct_ca9x4_map_io(void)
{
	phys_addr_t cpu_phys_addr = get_periph_base();

  if (initial_boot_params)
	{
    WARN_ON(of_scan_flat_dt(nvt_dt_find_uart, NULL));
		ct_ca9x4_io_desc[0].pfn = __phys_to_pfn(cpu_phys_addr);
	  iotable_init(ct_ca9x4_io_desc, ARRAY_SIZE(ct_ca9x4_io_desc));
	  Cortex_a9_base = ioremap(cpu_phys_addr, SZ_8K);
	}
}

static void __init ct_ca9x4_init_irq(void)
{
  return;
}
static void __init ct_ca9x4_init(void)
{
  return;
}

#ifdef CONFIG_SMP
static void __init ct_ca9x4_init_cpu_map(void)
{
	return;
}

static void __init ct_ca9x4_smp_enable(unsigned int max_cpus)
{
	return;
}
#endif

struct ct_desc ct_ca9x4_desc __initdata = {
	.id		= V2M_CT_ID_CA9,
	.name		= "CA9x4",
	.map_io		= ct_ca9x4_map_io,
	.init_irq	= ct_ca9x4_init_irq,
	.init_tile	= ct_ca9x4_init,
#ifdef CONFIG_SMP
	.init_cpu_map	= ct_ca9x4_init_cpu_map,
	.smp_enable	= ct_ca9x4_smp_enable,
#endif
};
