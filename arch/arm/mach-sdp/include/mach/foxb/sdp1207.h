/*
 * sdp1207.h : defined SoC SFRs
 *
 * Copyright (C) 2009-2013 Samsung Electronics
 * Seihee Chon <sh.chon@samsung.com>
 * Sola lee<sssol.lee@samsung.com>
 */

#ifndef _SDP1207_H_
#define _SDP1207_H_

#define VA_IO_BASE0           (0xFE000000)
#define PA_IO_BASE0           (0x10000000)
#define DIFF_IO_BASE0	      (VA_IO_BASE0 - PA_IO_BASE0)

#define VA_SFR0_BASE		(VA_IO_BASE0)
#define VA_SFR_BASE(pa)		(VA_SFR0_BASE)
#define SFR0_BASE		(PA_IO_BASE0)
#define SFR0_SIZE		(0x01000000)

#define PA_MMC_BASE           (0x10000000)
#define VA_MMC_BASE           (PA_MMC_BASE + DIFF_IO_BASE0)

/* Static Memory Controller Register */
#define PA_SMC_BASE           (0x10078000)
#define VA_SMC_BASE           (PA_SMC_BASE + DIFF_IO_BASE0)

#define PA_SMCDMA_BASE		  (0x10078200)
#define VA_SMCDMA_BASE		  (PA_SMCDMA_BASE + DIFF_IO_BASE0)

/* Power Management unit & PLL Register */
#define PA_PMU_BASE           (0x10090800)
#define VA_PMU_BASE           (PA_PMU_BASE + DIFF_IO_BASE0)

/* Pad Control Register */
#define PA_PADCTRL_BASE       (0x10090C00)
#define VA_PADCTRL_BASE       (PA_PADCTRL_BASE + DIFF_IO_BASE0)

/* Timer Register */
#define PA_TIMER_BASE         (0x10090400)
#define VA_TIMER_BASE         (PA_TIMER_BASE + DIFF_IO_BASE0)

/* UART */
#define SDP_UART_NR		(4)
#define PA_UART_BASE		(0x10090A00)
#define VA_UART_BASE		(PA_UART_BASE + DIFF_IO_BASE0)

/* UART DMA TX Register */
#define PA_UDMA_TX_BASE		  (0x100B0000)
#define VA_UDMA_TX_BASE		  (PA_UDMA_TX_BASE + DIFF_IO_BASE0)

/* UART DMA RX Register */
#define PA_UDMA_RX_BASE		  (0x100C0000)
#define VA_UDMA_RX_BASE		  (PA_UDMA_RX_BASE + DIFF_IO_BASE0)

/* Interrupt controller register */
#define PA_INTC_BASE          (0x10090F00)
#define VA_INTC_BASE          (PA_INTC_BASE + DIFF_IO_BASE0)

/* Ethernet Controller Register */
#define PA_GMAC_BASE           (0x100d0000)
#define VA_GMAC_BASE           (PA_GMAC_BASE + DIFF_IO_BASE0)
#define SDP_GMAC_BUS		   (32)/* Bus bits */

/* Watchdog Register */
#define PA_WDT_BASE           (0x10090600)
#define VA_WDT_BASE           (PA_WDT_BASE + DIFF_IO_BASE0)

/* PL310 L2 cache controller */
#define VA_IO_CONV(a)		((a & 0x00FFFFFF) | VA_IO_BASE0)

/* GZIP Decomprocessor register */
#define PA_UNZIP_BASE	(0x30140000)
#define VA_UNZIP_BASE	VA_IO_CONV(PA_UNZIP_BASE)

#define PA_L2C_BASE		(0x30b50000)
#define VA_L2C_BASE		VA_IO_CONV(PA_L2C_BASE)

/* Core PMU Register */
#define PA_COREPMU_BASE	(0x30b70000)
#define VA_COREPMU_BASE		VA_IO_CONV(PA_COREPMU_BASE)

/* Core Base Register */
#define PA_CORE_BASE	(0x30b40000)
#define PA_CORE(x)		(PA_CORE_BASE + (x))

/* Core Power Register */
#define PA_CORE_POWER_BASE	0x30b70000
#define VA_CORE_POWER_BASE	VA_IO_CONV(PA_CORE_POWER_BASE)
#define V_CORE_POWER_VALUE	0x3FFF

/* SCU Register */
#define PA_SCU_BASE	PA_CORE(0x0)
#define VA_SCU_BASE		VA_IO_CONV(PA_SCU_BASE)

/* GIC Register */
#define PA_GIC_CPU_BASE	PA_CORE(0x100)
#define VA_GIC_CPU_BASE		VA_IO_CONV(PA_GIC_CPU_BASE)
#define PA_GIC_DIST_BASE	PA_CORE(0x1000)
#define VA_GIC_DIST_BASE	VA_IO_CONV(PA_GIC_DIST_BASE)

/* ARM TWD Register */
#define PA_TWD_BASE	PA_CORE(0x600)
#define VA_TWD_BASE		VA_IO_CONV(PA_TWD_BASE)


/* SPI Register */
#define PA_SPI_BASE          (0x30070000)
#define VA_SPI_BASE          (PA_SPI_BASE + DIFF_IO_BASE0)

/* PCI PHY */
/* TODO */

/* USB EHCI0 host controller register */
#define PA_EHCI0_BASE         (0x100e0000)
#define VA_EHCI0_BASE         (PA_EHCI0_BASE + DIFF_IO_BASE0)

/* USB EHCI1 host controller register */
#define PA_EHCI1_BASE         (0x100f0000)
#define VA_EHCI1_BASE         (PA_EHCI1_BASE + DIFF_IO_BASE0)

/* USB OHCI0 host controller register */
#define PA_OHCI0_BASE         (0x100e8000)
#define VA_OHCI0_BASE         (PA_OHCI0_BASE + DIFF_IO_BASE0)

/* USB OHCI1 host controller register */
#define PA_OHCI1_BASE         (0x100f8000)
#define VA_OHCI1_BASE         (PA_OHCI1_BASE + DIFF_IO_BASE0)

/* SATA0 controller */
#define PA_SATA0_BASE        (0x30040000)
#define VA_SATA0_BASE         (PA_SATA0_BASE + DIFF_IO_BASE0)

/* SATA1 controller */
#define PA_SATA1_BASE         (0x30050000)
#define VA_SATA1_BASE         (PA_SATA1_BASE + DIFF_IO_BASE0)

/* end of SFR_BASE */

/* Micom PAD BASE */
#define PA_MPADCTRL_BASE	(0x30200600)


/* SMC Register */
#define O_SMC_BANK0		0x48
#define O_SMC_BANK1		0x24
#define O_SMC_BANK2		0x00
#define O_SMC_BANK3		0x6c

#define O_SMC_IDCYR		(0x00)
#define O_SMC_WST1		(0x04)
#define O_SMC_WST2		(0x08)
#define O_SMC_WSTOEN		(0x0C)
#define O_SMC_WSTWEN		(0x10)
#define O_SMC_CR		(0x14)
#define O_SMC_SR		(0x18)
#define O_SMC_CIWRCON		(0x1C)
#define O_SMC_CIRDCON		(0x20)
#define O_SMC_WST1_BK0		0x148
#define O_SMC_WST3_BK1		0x144
#define O_SMC_WST3_BK2		0x140
#define O_SMC_WST3_BK3		0x14c

/* clock & power management */
#define VA_PMU(offset)    (*(volatile unsigned *)(VA_PMU_BASE+(offset)))

#define O_PMU_CPU_CON		(0x04)
#define O_PMU_DDR_CON		(0x08)
#define O_PMU_DSP_CON		(0x0C)

#define R_PMU_CPU_CON	VA_PMU(O_PMU_CPU_CON)
#define R_PMU_DDR_CON	VA_PMU(O_PMU_DDR_CON)
#define R_PMU_DSP_CON	VA_PMU(O_PMU_DSP_CON)

#endif  /* _SDP1207_H_ */

