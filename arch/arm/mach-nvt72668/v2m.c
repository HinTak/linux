/*
 * Novatek CA9 Motherboard Support
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>

#include <asm/arch_timer.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/smp_twd.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/timer-sp.h>
#include <plat/sched_clock.h>
#include <plat/platsmp.h>

#include "mach/ct-ca9x4.h"
#include "mach/motherboard.h"
#include "core.h"
#include <mach/clk.h>

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/serial_8250.h>

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/sections.h>
#include <linux/dma-mapping.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>     
#include "mach/pm.h"

#define NVT72668_USB0_IRQ			(108)
#define NVT72668_MMC0_IRQ			(95)
#define NVT72668_USB1_IRQ			(86)
#define NVT72668_USB2_IRQ			(87)
#define NVT72668_USB3_IRQ			(88)
#define NVT72668_USB4_IRQ			(85)

extern struct platform_device *nvt_mmc_dev(int id);
extern struct platform_device *nvt_ahb_status_dev(void);

static unsigned int Gb_timer_freq_hz=0,Ext_timer_freq_hz=0;
static void __iomem *Timer0_base,__iomem *GIT_base; 

static u64 ehci_dmamask = DMA_BIT_MASK(32);
static u64 xhci_dmamask = DMA_BIT_MASK(32);

struct nt72668_soc_pm_info_struct *pm_info;
unsigned Gb_uart0_addr,Gb_uart1_addr;
          
void kernel_protect_range_get(void);

/*
 * IRQ handler for the timer
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
        struct clock_event_device *evt = dev_id;

        evt->event_handler(evt);

        return IRQ_HANDLED;
}

static void timer_set_mode(enum clock_event_mode mode,
        struct clock_event_device *evt)
{
        unsigned long interval;
        unsigned long ctl = 0, div = 0;

        switch (mode) {
                case CLOCK_EVT_MODE_PERIODIC:
                        interval = (Ext_timer_freq_hz / HZ)  ;
                        while (interval & ~0xFFFFFFUL)
                            interval >>= ++div;
                        WARN(div & ~0xFUL, "%s divisor overflow!\n", evt->name);
                        writel(interval, Timer0_base + 0x0);
                        ctl |= FLAG_ExtTIMER_ENABLE | div; /* bit 3:0  is divider */
                        writel(ctl, Timer0_base + 0x4);               /* control for Timer0 */
                        break;

                case CLOCK_EVT_MODE_ONESHOT:
                        ctl = readl(Timer0_base + 0x4);
                        ctl |= FLAG_ExtTIMER_ENABLE;
                        writel(ctl, Timer0_base + 0x4);
                        break;
                case CLOCK_EVT_MODE_UNUSED:
                case CLOCK_EVT_MODE_SHUTDOWN:
                        ctl = readl(Timer0_base + 0x4);
                        ctl &= ~(FLAG_ExtTIMER_ENABLE);
                        writel(ctl, Timer0_base + 0x4);
                default:
                        break;
        }
}

static int timer_set_next_event(unsigned long next, struct clock_event_device *evt)
{
        //set next event trigger counter
        unsigned long ctl = 0, div = 0;

        while (next & ~0xFFFFFFUL)
            next >>= ++div;
        WARN(div & ~0xFUL, "%s divisor overflow!\n", evt->name);

        ctl = readl(Timer0_base + 0x4);
        ctl &= ~FLAG_ExtTIMER_ENABLE;
        writel(ctl, Timer0_base + 0x4);

        writel(next, Timer0_base + 0x0);
        ctl |= FLAG_ExtTIMER_ENABLE | div; /* bit 3:0  is divider */
        writel(ctl, Timer0_base + 0x4);               /* control for Timer0 */
        return 0;
}

static struct clock_event_device clockevent_timer = {
        .name           = "TIMER0",
        .shift          = 32,
        .features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
        .set_mode       = timer_set_mode,
        .set_next_event = timer_set_next_event,
        .rating         = 300,
        .cpumask        = cpu_all_mask,
};

static struct irqaction nvt_timer_irq = {
        .name           = "CA9 Timer Tick",
        .flags          = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL | IRQF_TRIGGER_RISING ,
        .handler        = timer_interrupt,
        .percpu_dev_id  = &clockevent_timer,   
        .dev_id         = &clockevent_timer,
};




static struct resource NVT_USB0[] = {
        [0] = {
               .start          = NVT_USB0_BASE,
               .end            = NVT_USB0_BASE + 0x200,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb0",
        },
        [1] = {
               .start          = NVT_USB0_APB_BASE,
               .end            = NVT_USB0_APB_BASE + 0x500,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb0_apb",
        },
        [2] = {
               .start          = NVT72668_USB0_IRQ,
               .end            = NVT72668_USB0_IRQ,
               .flags          = IORESOURCE_IRQ,
										        },
};

static struct resource NVT_USB1[] = {
        [0] = {
               .start          = NVT_USB1_BASE,
               .end            = NVT_USB1_BASE + 0x200,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb1",
        },
        [1] = {
               .start          = NVT_USB1_APB_BASE,
               .end            = NVT_USB1_APB_BASE + 0x500,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb1_apb",
        },
        [2] = {
               .start          = NVT72668_USB1_IRQ,
               .end            = NVT72668_USB1_IRQ,
               .flags          = IORESOURCE_IRQ,
        },
};

static struct resource NVT_USB2[] = {
        [0] = {
               .start          = NVT_USB2_BASE,
               .end            = NVT_USB2_BASE + 0x200,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb2",
        },
        [1] = {
               .start          = NVT_USB2_APB_BASE,
               .end            = NVT_USB2_APB_BASE + 0x500,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb2_apb",
        },
        [2] = {
               .start          = NVT72668_USB2_IRQ,
               .end            = NVT72668_USB2_IRQ,
               .flags          = IORESOURCE_IRQ,
										        },
};

static struct resource NVT_USB3[] = {
        [0] = {
               .start          = NVT_USB3_BASE,
               .end            = NVT_USB3_BASE + 0x200,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb3",
        },
        [1] = {
               .start          = NVT_USB3_APB_BASE,
               .end            = NVT_USB3_APB_BASE + 0x500,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb3_apb",
        },        
        [2] = {
               .start          = NVT72668_USB3_IRQ,
               .end            = NVT72668_USB3_IRQ,
               .flags          = IORESOURCE_IRQ,
        },
};

static struct resource NVT_USB4[] = {
        [0] = {
               .start          = NVT_USB4_BASE,
               .end            = NVT_USB4_BASE + 0xd000,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_xhci",
        },
        [1] = {
               .start          = NVT_USB2_APB_BASE,
               .end            = NVT_USB2_APB_BASE + 0x500,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_usb2_apb",
        },
        [2] = {
               .start          = NVT_USB4_APB_BASE,
               .end            = NVT_USB4_APB_BASE + 0x1000,
               .flags          = IORESOURCE_MEM,
	       .name           = "NT72668_xhci_apb",
        }, 
        [3] = {
               .start          = NVT72668_USB4_IRQ,
               .end            = NVT72668_USB4_IRQ,
               .flags          = IORESOURCE_IRQ,
        },
};



static struct platform_device NVT_USB0_device = {
        .name           = "NT72668-ehci",
        .id             = 0,
        .dev = {
                .dma_mask               = &ehci_dmamask,
                .coherent_dma_mask      = 0xffffffff,
        },
        .num_resources  = ARRAY_SIZE(NVT_USB0),
        .resource       =  NVT_USB0,
};


static struct platform_device NVT_USB1_device = {
        .name           = "NT726681-ehci",
        .id             = 0,
        .dev = {
		.dma_mask               = &ehci_dmamask,
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(NVT_USB1),
	.resource       =  NVT_USB1,
};

static struct platform_device NVT_USB2_device = {
        .name           = "NT726682-ehci",
        .id             = 0,
        .dev = {
                .dma_mask               = &ehci_dmamask,
                .coherent_dma_mask      = 0xffffffff,
        },
        .num_resources  = ARRAY_SIZE(NVT_USB2),
        .resource       =  NVT_USB2,
};


static struct platform_device NVT_USB3_device = {
        .name           = "NT726683-ehci",
        .id             = 0,
        .dev = {
		.dma_mask               = &ehci_dmamask,
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(NVT_USB3),
	.resource       =  NVT_USB3,
};


static struct platform_device NVT_USB4_device = {
        .name           = "NT72668-xhci",
        .id             = 0,
        .dev = {
		.dma_mask               = &xhci_dmamask,
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(NVT_USB4),
	.resource       =  NVT_USB4,
};


static struct platform_device *nvt_usb_devices[] __initdata = {
	&NVT_USB0_device,
	&NVT_USB1_device,
	&NVT_USB2_device,
	&NVT_USB3_device,
	&NVT_USB4_device,
};

static struct platform_device synopGMAC_plat_dev = {
        .name = "NT72668-synopGMAC",
        .id = -1,
        .num_resources = 0,
        .resource = NULL,
};
static struct platform_device *nvt_eth_devices[] __initdata = {
	&synopGMAC_plat_dev,
};

static void __init nvt_clockevents_init(unsigned int irq, unsigned int freq_hz)
{
        struct clock_event_device *evt = &clockevent_timer;

        evt->irq = (int)irq;
        
        writel(0x0,Timer0_base+ 0x4);                 // disable Timer0 
        setup_irq(irq, &nvt_timer_irq);               // for SPI interrupt or dual Core 
        clockevents_config_and_register(evt, freq_hz, 0x1, 0xffffff);
}

static struct plat_serial8250_port uart8250_data[] = {
        {
                .iotype         = UPIO_MEM32,
                .flags          = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
                .regshift       = 2,
        },
        {
                .iotype         = UPIO_MEM32,
                .flags          = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
                .regshift       = 2,
        },
        {
                .iotype         = UPIO_MEM32,
                .flags          = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
                .regshift       = 2,
        },
		{ /* list termination, do NOT remove */ },
};

static struct platform_device nvt_uart8250_device = {
        .name                   = "serial8250",
        .id                     = PLAT8250_DEV_PLATFORM,
        .dev                    = {
        .platform_data          = uart8250_data,
        },
};

struct ct_desc *ct_desc;

static struct ct_desc *ct_descs[] __initdata = {
	&ct_ca9x4_desc,
};

static void __init v2m_populate_ct_desc(void)
{
	ct_desc = ct_descs[0];
}

static int __init v2m_dt_scan_memory_map(unsigned long node, const char *uname,
		int depth, void *data)
{
 
	if (strcmp(uname, "motherboard") != 0)
		return 0;

	return 1;
}

static void __init v2m_dt_map_io(void)
{
	const char *map = NULL;

	of_scan_flat_dt(v2m_dt_scan_memory_map, &map);

	v2m_populate_ct_desc();
	if (ct_desc->map_io)
	  ct_desc->map_io();
}

static void __init v2m_dt_init_early(void)
{
    return;
}

static void __init v2m_dt_init_irq(void)
{
	void __iomem *dist = Cortex_a9_base + A9_MPCORE_GIC_DIST_OFFSET;
	void __iomem *cpu = Cortex_a9_base + A9_MPCORE_GIC_CPU_OFFSET;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	gic_init_bases(0, 29, dist, cpu, 0, node);
	of_node_put(node);
}


static cycle_t dt_timer_read(struct clocksource *cs)
{
	u32 upper, lower;

	do {
		upper = readl(GIT_base + 0x04);
		lower = readl(GIT_base);
	} while (upper != readl(GIT_base + 0x04));

	return (((cycle_t) upper) << 32) | lower;
}

static struct clocksource dt_clocksource_timer = {
        .name           = "global_timer",
        .rating         = 200,
        .read           = dt_timer_read,
        .mask           = CLOCKSOURCE_MASK(64),
        .shift          = 20,
        .flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};
                     
static void __init dt_nvt_clocksource_init(unsigned int freq_hz)
{
        struct clocksource *cs = &dt_clocksource_timer;
        writel(0x1, GIT_base + 0x08); //enable
        cs->mult = clocksource_khz2mult(freq_hz/1000, cs->shift); /* unit is Khz */
        clocksource_register(cs);
}

static const __initconst struct of_device_id ca9_72668_fixed_clk_match[] = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
//	{ .compatible = "nvt,ca9-72668-osc", .data = nvt_osc_of_setup, },
	{}
};

static const __initconst struct of_device_id ca9_72668_clkgen_match[] = {
	{ .compatible = "nvt,clkgen", .data = nvt_clk_init},
//	{ .compatible = "nvt,ca9-72668-osc", .data = nvt_osc_of_setup, },
	{}
};

static void __init v2m_dt_timer_init(void)
{
	struct device_node *node = NULL;
	unsigned int  irq_num;
	struct clk *armperi_clk;
#ifdef CONFIG_SMP
	struct twd_local_timer local_timer  = {
		.res	= {
			DEFINE_RES_MEM(get_periph_base() + A9_MPCORE_TWD_OFFSET, 0x10),
			DEFINE_RES_IRQ(29),
		},
	};
#endif

	of_clk_init(ca9_72668_clkgen_match);
	Gb_timer_freq_hz = get_periph_clk();

	GIT_base = Cortex_a9_base + A9_MPCORE_GIT_OFFSET;
	nvt_sched_clock_init(GIT_base,Gb_timer_freq_hz);
	dt_nvt_clocksource_init(Gb_timer_freq_hz);

	Ext_timer_freq_hz = get_ahb_clk();

	node = of_find_compatible_node(NULL, NULL, "nvt,ext-timer");
	if (node) {
		Timer0_base = of_iomap(node, 0);
		irq_num = irq_of_parse_and_map(node, 0);
		if (irq_num) {
			nvt_clockevents_init(irq_num,Ext_timer_freq_hz);  
		}
		of_node_put(node);
	} 

  	armperi_clk = clk_register_fixed_rate(NULL, "arm_peri",NULL, CLK_IS_ROOT, Gb_timer_freq_hz);
  	clk_register_clkdev(armperi_clk, NULL, "smp_twd");
#ifdef CONFIG_SMP
	twd_local_timer_register(&local_timer);
#endif
}

static struct sys_timer v2m_dt_timer = {
	.init = v2m_dt_timer_init,
};

static const struct of_device_id v2m_dt_bus_match[] __initconst = {
	{ .compatible = "nvt-amba-bus", },
	{}
};

static void __init v2m_uart_init(void)
{
	struct device_node *node = NULL;
	u32 uart_freq = get_ahb_clk();
	struct resource res;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(uart8250_data) - 1; i++) {
		node = of_find_compatible_node(node, NULL, "nvt,serial8250");
		if (!node) {
			WARN(!node, "missing dt for ttyS%d\n", i);
			break;
		}

		of_address_to_resource(node, 0, &res);
		uart8250_data[i].membase = of_iomap(node, 0);
		uart8250_data[i].mapbase = res.start;
		uart8250_data[i].irq = irq_of_parse_and_map(node, 0);
		uart8250_data[i].uartclk = uart_freq;
		of_node_put(node);
	}

	platform_device_register(&nvt_uart8250_device);
	Gb_uart0_addr = (unsigned)uart8250_data[0].membase;
	Gb_uart1_addr = (unsigned)uart8250_data[1].membase;
}

#ifdef CONFIG_NVT_MMC_IF
static void __init v2m_mmc_init(void)
{
	platform_device_register(nvt_mmc_dev(0));
}
#endif

static void __init v2m_ahb_status_init(void)
{
	platform_device_register(nvt_ahb_status_dev());
}

u64 Ker_MEM[3][2] = {{~0ULL, ~0ULL}, {~0ULL, ~0ULL}, {~0ULL, ~0ULL}};
static int mcount = 0;
static int __init mem_setup(char *str)
{
        char *endp;

        Ker_MEM[mcount][1]  = memparse(str, &endp);
        if (*endp == '@')
                Ker_MEM[mcount][0] = memparse(endp + 1, NULL);
	else
                Ker_MEM[mcount][0] = 0;
	mcount++;
	return 1;
}
__setup("mem=", mem_setup);

unsigned long Ker_ROSectionStart = ~0UL, Ker_ROSectionEnd = ~0UL;
void kernel_protect_range_get(void)
{
	int i;
	Ker_ROSectionStart = (unsigned int)virt_to_phys(_text);
	Ker_ROSectionEnd = (unsigned int)virt_to_phys(__end_rodata - 4);
	printk("============================================================\n");
	printk("Kernel range1 [RO sections]: 0x%08x ~ 0x%08x\n", (unsigned int)Ker_ROSectionStart, (unsigned int)Ker_ROSectionEnd);
	for(i = 0; i < mcount; i++)
		printk("Range%d [kernel memory]: 0x%08x ~ 0x%08x\n", i, (unsigned int)Ker_MEM[i][0], (unsigned int)(Ker_MEM[i][0] + Ker_MEM[i][1]));
	printk("============================================================\n");
		
}
EXPORT_SYMBOL(Ker_ROSectionStart);
EXPORT_SYMBOL(Ker_ROSectionEnd);
EXPORT_SYMBOL(Ker_MEM);

static void __init v2m_dt_init(void)
{
	v2m_uart_init();
	platform_add_devices(nvt_usb_devices, ARRAY_SIZE(nvt_usb_devices));
	platform_add_devices(nvt_eth_devices, ARRAY_SIZE(nvt_eth_devices));
	l2x0_of_init(0x00400000, 0xfe0fffff);
	
	//of_platform_populate(NULL, v2m_dt_bus_match, NULL, NULL);
	//pm_power_off = nvt_ca9_power_off;
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	v2m_ahb_status_init();

#ifdef CONFIG_NVT_MMC_IF
	v2m_mmc_init();
#endif
}

static const char * const v2m_dt_match[] __initconst = {
	"novatek,ca9",
	NULL,
};

static void nt72668_set_irq_affinity(void)
{
#ifdef CONFIG_NVT_FASTETH_MAC
	struct device_node *node;
	const __be32 *irq;
	int len;

	node = of_find_compatible_node(NULL, NULL, "nvt,synopsys-mac");
	if (node) {
		irq = of_get_property(node, "interrupts", &len);

		if (irq && cpu_online(2))
			irq_set_affinity(*irq, get_cpu_mask(2));

		of_node_put(node);
	}
#endif

#ifdef CONFIG_MMC_SDHCI_NVT_PIO_MODE
	if (cpu_online(2)) {
		irq_set_affinity(NVT72668_MMC0_IRQ, get_cpu_mask(2));
	}
#endif

#ifdef CONFIG_USB_NT72668_HCD
	if (cpu_online(2)) {
		irq_set_affinity(NVT72668_USB0_IRQ, get_cpu_mask(2));
		irq_set_affinity(NVT72668_USB3_IRQ, get_cpu_mask(2));
	}

	if (cpu_online(3)) {
		irq_set_affinity(NVT72668_USB1_IRQ, get_cpu_mask(3));
		irq_set_affinity(NVT72668_USB2_IRQ, get_cpu_mask(3));
	}
#endif

	if (cpu_online(3))
		irq_set_affinity(uart8250_data[0].irq, get_cpu_mask(3));
}

#ifdef CONFIG_SUSPEND
static int __nt72668_suspend_enter(unsigned long unused)
{
#ifdef CONFIG_NVT_SUSPEND_MICOM_UART
	nt72668_micom_uart_init();
#endif	
	soft_restart(virt_to_phys(nt72668_wait_for_die));
	return 0;
}

static int nt72668_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;
	
	printk("[%s]\n", __FUNCTION__);
	
	cpu_suspend(0, __nt72668_suspend_enter);
	
	printk("resume successful and back to [%s]\n", __FUNCTION__);

	return ret;
}

static int nt72668_pm_begin(suspend_state_t state)
{
	printk("[%s]\n", __FUNCTION__);
	return 0;
}

static void nt72668_pm_end(void)
{
	printk("[%s]\n", __FUNCTION__);
}

static void nt72668_pm_finish(void)
{
	printk("[%s]\n", __FUNCTION__);

	nt72668_set_irq_affinity();
}

static const struct platform_suspend_ops nt72668_pm_ops = {
	.begin		= nt72668_pm_begin,
	.end		= nt72668_pm_end,
	.enter		= nt72668_pm_enter,
	.finish		= nt72668_pm_finish,
	.valid		= suspend_valid_only_mem,
};

#endif /* CONFIG_SUSPEND */

#ifdef CONFIG_PM
static void __init nt72668_pm_late_init(void)
{
#ifdef CONFIG_SUSPEND
	pm_info = kmalloc(sizeof(struct nt72668_soc_pm_info_struct), GFP_KERNEL);
	if (pm_info == NULL)
	{
    		printk("[%s] kmalloc failed.\n", __FUNCTION__);
    		return ;
    	}
	pm_info->cpu_a9_addr = (unsigned) Cortex_a9_base;
	pm_info->ext_timer_addr = (unsigned)Timer0_base;
	pm_info->ext_timer_counter_low = ((Ext_timer_freq_hz / HZ) % 0x1000000);
	pm_info->uart0_addr =  (unsigned)uart8250_data[0].membase;
	pm_info->uart1_addr =  (unsigned)uart8250_data[1].membase;
	pm_info->uart2_addr =  (unsigned)uart8250_data[2].membase;
	suspend_set_ops(&nt72668_pm_ops);
	nt72668_pm_syscore_init();
#endif
}
#endif


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
#ifdef CONFIG_PM
	nt72668_pm_late_init();
#endif
	nt72668_set_irq_affinity();
	//SOC determination 
	Ker_chip = (get_chip_id() == EN_SOC_NT72656) ? EN_SOC_NT72656 : EN_SOC_NT72668 ;

}




DT_MACHINE_START(NVT72668_DT, "Novatek-Cortex A9")
	.dt_compat	= v2m_dt_match,
	.smp		= smp_ops(nvt_ca9_smp_ops),
	.map_io		= v2m_dt_map_io,
	.init_early	= v2m_dt_init_early,
	.init_late	= nt72668_late_init,
	.init_irq	= v2m_dt_init_irq,
	.timer		= &v2m_dt_timer,
	.init_machine	= v2m_dt_init,
	.handle_irq	= gic_handle_irq,
	//.restart	= nvt_restart,
MACHINE_END

