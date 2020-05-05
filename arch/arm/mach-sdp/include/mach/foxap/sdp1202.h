/*
 * sdp1202fpga.h : defined SoC SFRs
 *
 * Copyright (C) 2012 Samsung Electronics co.
 * Seungjun Heo <seungjun.heo@samsung.com>
 *
 */

#ifndef _SDP1202_H_
#define _SDP1202_H_

/* common macros */
#define VA_SFR(blk)		(VA_SFR_BASE(PA_##blk##_BASE) + SFR_OFFSET(PA_##blk##_BASE))

#define VA_SFR0_BASE		(SFR_VA)
#define VA_SFR_BASE(pa)		(VA_SFR0_BASE)

#if 0
/* for 2 banks structures */
#define VA_SFR1_BASE		(VA_SFR0_BASE + SFR0_SIZE)
#define VA_SFR_BASE(pa)		(SFR_BANK(pa) ? VA_SFR1_BASE : VA_SFR0_BASE)
#endif

/* for compatibility */
#define PA_IO_BASE0		SFR0_BASE
#define DIFF_IO_BASE0		(VA_SFR0_BASE - SFR0_BASE)

/* machine-dependent macros */
#define SFR_NR_BANKS		(1)
#define SFR_VA			(0xfe000000)
#define SFR0_BASE		(0x10000000)
#define SFR0_SIZE		(0x01000000)
#define SFR_OFFSET(pa)		(pa & 0x00ffffff)
#define SFR_BANK(x)		(0)

/* list of SFRs */
//#define PA_CORE_BASE		(0x10b70000)

//#define PA_SCU_BASE		PA_CORE_BASE
//#define VA_SCU_BASE		VA_SFR(SCU)

#define PA_GIC_BASE			(0x10b80000)

#define PA_GIC_CPU_BASE		(PA_GIC_BASE + 0x2000)
#define VA_GIC_CPU_BASE		VA_SFR(GIC_CPU)

#define PA_GIC_DIST_BASE	(PA_GIC_BASE + 0x1000)
#define VA_GIC_DIST_BASE	VA_SFR(GIC_DIST)

//#define PA_TWD_BASE		(PA_CORE_BASE + 0x600)
//#define VA_TWD_BASE		VA_SFR(TWD)

//#define PA_L2C_BASE		(0x10b50000)
//#define VA_L2C_BASE		VA_SFR(L2C)

#define PA_CORE_POWER_BASE	(0x10b70000)
#define VA_CORE_POWER_BASE	VA_SFR(CORE_POWER)
#define V_CORE_POWER_VALUE	(0xFFFFF)

#define PA_SMC_BASE			(0x10078000)
//#define VA_SMC_BASE		VA_SFR(SMC)
#define PA_SMCDMA_BASE		(0x10078200)
//#define VA_SMCDMA_BASE		VA_SFR(SMCDMA)

#define PA_MMC_BASE			(0x10000000)
//#define VA_MMC_BASE		VA_SFR(MMC)

#define PA_GMAC_BASE           (0x100D0000)
//#define VA_GMAC_BASE		VA_SFR(GMAC)
#define SDP_GMAC_BUS		   (64)/* Bus bits */

#define PA_EHCI0_BASE		(0x100E0000)
#define VA_EHCI0_BASE		VA_SFR(EHCI0)

#define PA_EHCI1_BASE		(0x100F0000)
#define VA_EHCI1_BASE		VA_SFR(EHCI1)

#define PA_OHCI0_BASE		(0x100E8000)
#define VA_OHCI0_BASE		VA_SFR(OHCI0)

#define PA_OHCI1_BASE 		(0x100F8000)
#define VA_OHCI1_BASE		VA_SFR(OHCI1)

#define PA_XHCI0_BASE 		(0x100C0000)
#define VA_XHCI0_BASE		VA_SFR(XHCI1)

#define PA_OTG_BASE			(0x10020000)
#define VA_OTG_BASE			VA_SFR(OTG)

#define PA_PMU_BASE			(0x10090800)
#define VA_PMU_BASE			VA_SFR(PMU)

#define PA_PADCTRL_BASE		(0x10090C00)
#define VA_PADCTRL_BASE		VA_SFR(PADCTRL)

#define PA_UART_BASE		(0x10090a00)
#define VA_UART_BASE		VA_SFR(UART)
#define SDP_UART_NR		(4)

#define PA_TIMER_BASE		(0x10090400)
#define VA_TIMER_BASE		VA_SFR(TIMER)

#define PA_SPI_BASE			(0x10090200)
#define VA_SPI_BASE			VA_SFR(SPI)

#define PA_I2C_BASE			(0x10090100)
#define VA_I2C_BASE			VA_SFR(I2C)

#define PA_AMS_DMA330_0_BASE	(0x100A0000)
#define VA_AMS_DMA330_0_BASE	VA_SFR(AMS_DMA330_0)

#define PA_AMS_DMA330_1_BASE	(0x100A4000)
#define VA_AMS_DMA330_1_BASE	VA_SFR(AMS_DMA330_1)


#define PA_UNZIP_BASE		(0x10350000)
#define VA_UNZIP_BASE		VA_SFR(UNZIP)

#define PA_TSC_BASE			(0x10090CDC)
#define VA_TSC_BASE			VA_SFR(TSC)

#define	PA_EBUS_BASE		(0x10408000)
#define VA_EBUS_BASE		VA_SFR(EBUS)

/* XXX extra peripherals not defined in here:
 * IRR 0x10090300
 * IRB 0x10090700
 * RTC 0x10090500
 */

/* SMC Register */
#define PA_SMC_BASE	(0x10078000)
#define VA_SMC_BASE	VA_SFR(SMC)

#define VA_SMC(offset)  (*(volatile unsigned *)(VA_SMC_BASE+(offset)))

#define VA_SMC_BANK(bank, offset)  (*(volatile unsigned *)(VA_SMC_BASE+(bank)+(offset)))

#define O_SMC_BANK0		0x48
#define O_SMC_BANK1		0x24
#define O_SMC_BANK2		0x00
#define O_SMC_BANK3		0x6c

#define VA_SMC_BANK0(offset)  	VA_SMC_BANK(O_SMC_BANK0, offset)
#define VA_SMC_BANK1(offset)  	VA_SMC_BANK(O_SMC_BANK1, offset)
#define VA_SMC_BANK2(offset)  	VA_SMC_BANK(O_SMC_BANK2, offset)
#define VA_SMC_BANK3(offset)  	VA_SMC_BANK(O_SMC_BANK3, offset)

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

#define R_SMC_WST3_BK0		VA_SMC(O_SMC_WST3_BK0)
#define R_SMC_WST3_BK1		VA_SMC(O_SMC_WST3_BK1)
#define R_SMC_WST3_BK2		VA_SMC(O_SMC_WST3_BK2)
#define R_SMC_WST3_BK3		VA_SMC(O_SMC_WST3_BK3)

/* bank 0 */
#define R_SMC_IDCY_BK0		VA_SMC_BANK0(O_SMC_SMBIDCYR)
#define R_SMC_WST1_BK0		VA_SMC_BANK0(O_SMC_SMBWST1R)
#define R_SMC_WST2_BK0		VA_SMC_BANK0(O_SMC_SMBWST2R)
#define R_SMC_WSTOEN_BK0	VA_SMC_BANK0(O_SMC_SMBWSTOENR)
#define R_SMC_WSTWEN_BK0	VA_SMC_BANK0(O_SMC_SMBWSTWENR)
#define R_SMC_CR_BK0		VA_SMC_BANK0(O_SMC_SMBCR)
#define R_SMC_SR_BK0		VA_SMC_BANK0(O_SMC_SMBSR)
#define R_SMC_CIWRCON_BK0	VA_SMC_BANK0(O_SMC_CIWRCON)
#define R_SMC_CIRDCON_BK0	VA_SMC_BANK0(O_SMC_CIRDCON)

/* bank 1 */
#define R_SMC_IDCY_BK1		VA_SMC_BANK1(O_SMC_SMBIDCYR)
#define R_SMC_WST1_BK1		VA_SMC_BANK1(O_SMC_SMBWST1R)
#define R_SMC_WST2_BK1		VA_SMC_BANK1(O_SMC_SMBWST2R)
#define R_SMC_WSTOEN_BK1	VA_SMC_BANK1(O_SMC_SMBWSTOENR)
#define R_SMC_WSTWEN_BK1	VA_SMC_BANK1(O_SMC_SMBWSTWENR)
#define R_SMC_CR_BK1		VA_SMC_BANK1(O_SMC_SMBCR)
#define R_SMC_SR_BK1		VA_SMC_BANK1(O_SMC_SMBSR)
#define R_SMC_CIWRCON_BK1	VA_SMC_BANK1(O_SMC_CIWRCON)
#define R_SMC_CIRDCON_BK1	VA_SMC_BANK1(O_SMC_CIRDCON)

/* bank 2 */
#define R_SMC_IDCY_BK2		VA_SMC_BANK2(O_SMC_SMBIDCYR)
#define R_SMC_WST1_BK2		VA_SMC_BANK2(O_SMC_SMBWST1R)
#define R_SMC_WST2_BK2		VA_SMC_BANK2(O_SMC_SMBWST2R)
#define R_SMC_WSTOEN_BK2	VA_SMC_BANK2(O_SMC_SMBWSTOENR)
#define R_SMC_WSTWEN_BK2	VA_SMC_BANK2(O_SMC_SMBWSTWENR)
#define R_SMC_CR_BK2		VA_SMC_BANK2(O_SMC_SMBCR)
#define R_SMC_SR_BK2		VA_SMC_BANK2(O_SMC_SMBSR)
#define R_SMC_CIWRCON_BK2	VA_SMC_BANK2(O_SMC_CIWRCON)
#define R_SMC_CIRDCON_BK2	VA_SMC_BANK2(O_SMC_CIRDCON)

/* bank 3 */
#define R_SMC_IDCY_BK3		VA_SMC_BANK3(O_SMC_SMBIDCYR)
#define R_SMC_WST1_BK3		VA_SMC_BANK3(O_SMC_SMBWST1R)
#define R_SMC_WST2_BK3		VA_SMC_BANK3(O_SMC_SMBWST2R)
#define R_SMC_WSTOEN_BK3	VA_SMC_BANK3(O_SMC_SMBWSTOENR)
#define R_SMC_WSTWEN_BK3	VA_SMC_BANK3(O_SMC_SMBWSTWENR)
#define R_SMC_CR_BK3		VA_SMC_BANK3(O_SMC_SMBCR)
#define R_SMC_SR_BK3		VA_SMC_BANK3(O_SMC_SMBSR)
#define R_SMC_CIWRCON_BK3	VA_SMC_BANK3(O_SMC_CIWRCON)
#define R_SMC_CIRDCON_BK3	VA_SMC_BANK3(O_SMC_CIRDCON)

#define R_SMC_SMBEWS		VA_SMC(0x120)
#define R_SMC_CI_RESET		VA_SMC(0x128)
#define R_SMC_CI_ADDRSEL	VA_SMC(0x12c)
#define R_SMC_CI_CNTL		VA_SMC(0x130)
#define R_SMC_CI_REGADDR	VA_SMC(0x134)
#define R_SMC_PERIPHID0		VA_SMC(0xFE0)
#define R_SMC_PERIPHID1		VA_SMC(0xFE4)
#define R_SMC_PERIPHID2		VA_SMC(0xFE8)
#define R_SMC_PERIPHID3		VA_SMC(0xFEC)
#define R_SMC_PCELLID0		VA_SMC(0xFF0)
#define R_SMC_PCELLID1		VA_SMC(0xFF4)
#define R_SMC_PCELLID2		VA_SMC(0xFF8)
#define R_SMC_PCELLID3		VA_SMC(0xFFC)
#define R_SMC_CLKSTOP		VA_SMC(0x1e8)
#define R_SMC_SYNCEN		VA_SMC(0x1ec)

/* clock & power management */
#define VA_PMU(offset)    (*(volatile unsigned *)(VA_PMU_BASE+(offset)))

#define O_PMU_CPU_PMS_CON		(0x0)
#define O_PMU_BUS_PMS_CON		(0x0C)
#define O_PMU_DDR_PMS_CON		(0x14)
#define O_PMU_DDR_K_CON			(0x34)

/* define for 'C' */
#define R_PMU_CPU_PMS_CON	VA_PMU(O_PMU_CPU_PMS_CON)
#define R_PMU_BUS_PMS_CON	VA_PMU(O_PMU_BUS_PMS_CON)
#define R_PMU_DDR_PMS_CON	VA_PMU(O_PMU_DDR_PMS_CON)
#define R_PMU_DDR_K_CON		VA_PMU(O_PMU_DDR_K_CON)

#define PMU_PLL_P_VALUE(x)	((x >> 20) & 0x3F)
#define PMU_PLL_M_VALUE(x)	((x >> 8) & 0x3FF)
#define PMU_PLL_S_VALUE(x)	(x & 0x7)
#define PMU_PLL_K_VALUE(x)	(x & 0xFFFF)

#define GET_PLL_M(x)		PMU_PLL_M_VALUE(x)
#define GET_PLL_P(x)		PMU_PLL_P_VALUE(x)
#define GET_PLL_S(x)		PMU_PLL_S_VALUE(x)

#define REQ_FCLK	1		/* CPU Clock */
#define REQ_DCLK	2		/* DDR Clock */
#define REQ_BUSCLK	3		/* BUS Clock */
#define REQ_HCLK	4		/* AHB Clock */
#define REQ_PCLK	5		/* APB Clock */

#endif	/* _SDP1202_H_ */

