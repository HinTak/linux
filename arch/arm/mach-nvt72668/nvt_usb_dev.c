#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/smsc911x.h>
#include <linux/spinlock.h>
#include <linux/usb/isp1760.h>
#include <linux/mtd/physmap.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/vexpress.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/usb/otg.h>

#include <asm/arch_timer.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/smp_twd.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/motherboard.h>

#include <asm/hardware/arm_timer.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/timer-sp.h>
#include <asm/delay.h>


void __iomem * USB0_EHCI_BASE = NULL;
void __iomem * USB1_EHCI_BASE = NULL;
void __iomem * USB2_EHCI_BASE = NULL;
void __iomem * USB3_EHCI_BASE = NULL;


void __iomem * USB0_APB = NULL;
void __iomem * USB1_APB = NULL;
void __iomem * USB2_APB = NULL;
void __iomem * USB3_APB = NULL;

void __iomem * NVTRST = NULL;

EXPORT_SYMBOL_GPL(USB0_APB);
EXPORT_SYMBOL_GPL(USB1_APB);
EXPORT_SYMBOL_GPL(USB2_APB);
EXPORT_SYMBOL_GPL(USB3_APB);

EXPORT_SYMBOL_GPL(USB0_EHCI_BASE);
EXPORT_SYMBOL_GPL(USB1_EHCI_BASE);
EXPORT_SYMBOL_GPL(USB2_EHCI_BASE);
EXPORT_SYMBOL_GPL(USB3_EHCI_BASE);

EXPORT_SYMBOL_GPL(NVTRST);

void dump_usb_reg(void);

void dump_usb_reg(void)
{
	unsigned int i = 0;
	void __iomem * pu0, *pu1, *pu2, *pu3, *pu4; 

	pu0 = pu1 = pu2 = pu3 = pu4 = NULL; 
	pu0 = ioremap_nocache((unsigned long)NVT_USB0_BASE, 0x10);
	pu1 = ioremap_nocache((unsigned long)NVT_USB1_BASE, 0x10);
	pu2 = ioremap_nocache((unsigned long)NVT_USB2_BASE, 0x10);
	pu3 = ioremap_nocache((unsigned long)NVT_USB3_BASE, 0x10);
	pu4 = ioremap_nocache((unsigned long)NVT_USB4_BASE, 0x10);
	if(pu0 == NULL || pu1 == NULL || pu2 == NULL || pu3 == NULL || pu4 == NULL) {
		printk("dump usb memory allocaton fail\n");
		return;
	}
		
	printk("\n************************** USB registers **********************************\n");
	printk("0x%03x: 0x%08x ", 4*i, 	  readl((const volatile void *)((unsigned int)pu0+4*i)));
	printk("0x%03x: 0x%08x ", 4*(i+1),  readl((const volatile void *)((unsigned int)pu0+4*(i+1))));
	printk("0x%03x: 0x%08x ", 4*(i+2),  readl((const volatile void *)((unsigned int)pu0+4*(i+2))));
	printk("0x%03x: 0x%08x\n", 4*(i+3), readl((const volatile void *)((unsigned int)pu0+4*(i+3))));
	printk("\n*************************************************************************************\n");
	printk("0x%03x: 0x%08x ", 4*i, 	  readl((const volatile void *)((unsigned int)pu1+4*i)));
	printk("0x%03x: 0x%08x ", 4*(i+1),  readl((const volatile void *)((unsigned int)pu1+4*(i+1))));
	printk("0x%03x: 0x%08x ", 4*(i+2),  readl((const volatile void *)((unsigned int)pu1+4*(i+2))));
	printk("0x%03x: 0x%08x\n", 4*(i+3), readl((const volatile void *)((unsigned int)pu1+4*(i+3))));
	printk("\n*************************************************************************************\n");
	printk("0x%03x: 0x%08x ", 4*i,	  readl((const volatile void *)((unsigned int)pu2+4*i)));
	printk("0x%03x: 0x%08x ", 4*(i+1),  readl((const volatile void *)((unsigned int)pu2+4*(i+1))));
	printk("0x%03x: 0x%08x ", 4*(i+2),  readl((const volatile void *)((unsigned int)pu2+4*(i+2))));
	printk("0x%03x: 0x%08x\n", 4*(i+3), readl((const volatile void *)((unsigned int)pu2+4*(i+3))));

	printk("\n*************************************************************************************\n");
	printk("0x%03x: 0x%08x ", 4*i,	  readl((const volatile void *)((unsigned int)pu3+4*i)));
	printk("0x%03x: 0x%08x ", 4*(i+1),  readl((const volatile void *)((unsigned int)pu3+4*(i+1))));
	printk("0x%03x: 0x%08x ", 4*(i+2),  readl((const volatile void *)((unsigned int)pu3+4*(i+2))));
	printk("0x%03x: 0x%08x\n", 4*(i+3), readl((const volatile void *)((unsigned int)pu3+4*(i+3))));
	printk("\n*************************************************************************************\n");
	printk("0x%03x: 0x%08x ", 4*i,	  readl((const volatile void *)((unsigned int)pu4 +4*i)));
	printk("0x%03x: 0x%08x ", 4*(i+1),	readl((const volatile void *)((unsigned int)pu4 +4*(i+1))));
	printk("0x%03x: 0x%08x ", 4*(i+2),	readl((const volatile void *)((unsigned int)pu4 +4*(i+2))));
	printk("0x%03x: 0x%08x\n", 4*(i+3), readl((const volatile void *)((unsigned int)pu4 +4*(i+3))));
	printk("\n*************************************************************************************\n");

	iounmap(pu0);
	iounmap(pu1);
	iounmap(pu2);
	iounmap(pu3);
	iounmap(pu4);

}

