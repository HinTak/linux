#ifndef NVTUSB_REGS
#define NVTUSB_REGS

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/writeback.h>
#include <mach/clk.h>
#include "mach/motherboard.h"

extern void __iomem * USB0_EHCI_BASE;
extern void __iomem * USB1_EHCI_BASE;
extern void __iomem * USB2_EHCI_BASE;
extern void __iomem * USB3_EHCI_BASE;


extern void __iomem * USB0_APB;
extern void __iomem * USB1_APB;
extern void __iomem * USB2_APB;
extern void __iomem * USB3_APB;

extern void __iomem * NVTRST;
extern void __iomem * SYSTEM_RAM_CLK;
extern unsigned int Ker_chip;


#define OTGC_INT_BSRPDN 						  1	
#define OTGC_INT_ASRPDET						  1<<4
#define OTGC_INT_AVBUSERR						  1<<5
#define OTGC_INT_RLCHG							  1<<8
#define OTGC_INT_IDCHG							  1<<9
#define OTGC_INT_OVC							  1<<10
#define OTGC_INT_BPLGRMV						  1<<11
#define OTGC_INT_APLGRMV						  1<<12

#define OTGC_INT_A_TYPE 						  (OTGC_INT_ASRPDET|OTGC_INT_AVBUSERR|OTGC_INT_OVC|OTGC_INT_RLCHG|OTGC_INT_IDCHG|OTGC_INT_BPLGRMV|OTGC_INT_APLGRMV)
#define OTGC_INT_B_TYPE 						  (OTGC_INT_BSRPDN|OTGC_INT_AVBUSERR|OTGC_INT_OVC|OTGC_INT_RLCHG|OTGC_INT_IDCHG)

#define clear(add,wValue)    writel(~((unsigned int)wValue)&readl((const volatile void *)add),(volatile void *)add)
#define set(add,wValue)      writel(wValue|readl((const volatile void *)add),(volatile void *)add)

int xhci_NT72668_probe(struct platform_device *pdev);

#endif

