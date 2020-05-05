/*
 * sdp1106.h : defined SoC SFRs
 *
 * Copyright (C) 2009-2013 Samsung Electronics
 *
 * seungjun.heo<seungjun.heo@samsung.com>
 * Ikjoon Jang <ij.jang@samsung.com>
 *
 */

#ifndef _SDP1106_H_
#define _SDP1106_H_

/* common macros */
#define VA_SFR(blk)		((void __iomem *)(VA_SFR_BASE(PA_##blk##_BASE) + SFR_OFFSET(PA_##blk##_BASE)))
#define VA_SFR0_BASE		((void __iomem *)SFR_VA)
#define VA_SFR_BASE(pa)		(VA_SFR0_BASE)

/* for compatibility, obsolete */
#define PA_IO_BASE0		SFR0_BASE
#define DIFF_IO_BASE0		(SFR_VA - SFR0_BASE)

/* machine-dependent macros */
#define SFR_NR_BANKS		(1)
#define SFR_VA			(0xfe000000)
#define SFR0_BASE		(0x30000000)
#define SFR0_SIZE		(0x01000000)
#define SFR_OFFSET(pa)		(pa & 0x00ffffff)
#define SFR_BANK(x)		(0)

/* list of SFRs */
#define PA_CORE_BASE		(0x30b40000)

#define PA_SCU_BASE		PA_CORE_BASE
#define VA_SCU_BASE		VA_SFR(SCU)

#define PA_GIC_CPU_BASE		(PA_CORE_BASE + 0x100)
#define VA_GIC_CPU_BASE		VA_SFR(GIC_CPU)

#define PA_GIC_DIST_BASE	(PA_CORE_BASE + 0X1000)
#define VA_GIC_DIST_BASE	VA_SFR(GIC_DIST)

#define PA_TWD_BASE		(PA_CORE_BASE + 0x600)
#define VA_TWD_BASE		VA_SFR(TWD)

#define PA_L2C_BASE		(0x10b50000)
#define VA_L2C_BASE		VA_SFR(L2C)

#define PA_PMU_BASE		(0x30090800)
#define VA_PMU_BASE		VA_SFR(PMU)

#define PA_CORE_POWER_BASE	(0x30b70000)
#define VA_CORE_POWER_BASE	VA_SFR(CORE_POWER)
#define V_CORE_POWER_VALUE	(0x3FFF)

#define PA_GMAC_BASE		(0x30050000)
#define SDP_GMAC_BUS		(128)/* Bus bits */

#define PA_TIMER_BASE		(0x30090400)
#define VA_TIMER_BASE		VA_SFR(TIMER)

#define PA_WDT_BASE		(0x30090600)
#define VA_WDT_BASE		VA_SFR(WDT)

#define PA_PADCTRL_BASE		(0x30090C00)
#define VA_PADCTRL_BASE		VA_SFR(PADCTRL)

#define PA_UART_BASE		(0x30090A00)
#define VA_UART_BASE		VA_SFR(UART)
#define SDP_UART_NR		(4)

#define PA_MMC_BASE           	(0x30000000)
#define PA_CPU_DMA330_BASE	(0x30B60000)
#define PA_AMS_DMA330_BASE      (0x30040000)
#define PA_SMC_BASE		(0x30028000)
#define PA_SMCDMA_BASE		(0x30020200)
#define PA_SPI_BASE 		(0x30090200)
#define PA_EHCI0_BASE		(0x30070000)
#define PA_EHCI1_BASE		(0x30080000)
#define PA_OHCI0_BASE		(0x30078000)
#define PA_OHCI1_BASE		(0x30088000)

#endif

