/*!
 * @file clk.h
 *
 */
#ifndef __NVT_CLK_H__
#define __NVT_CLK_H__

typedef enum _MPLL_OFF_e
{
	EN_MPLL_OFF_ARM		= 0x62,	///< ARM
	EN_MPLL_OFF_AHB		= 0x9d,	///< AHB
	EN_MPLL_OFF_AXI		= 0x6a,	///< AXI
	EN_MPLL_OFF_ETH		= 0xaa,	///< Ethernet
	EN_MPLL_OFF_USB_20	= 0x8a,	///< USB 2.0
	EN_MPLL_OFF_USB_30	= 0x15,	///< USB 3.0
	EN_MPLL_OFF_EMMC	= 0xc4,	///< EMMC
	EN_MPLL_OFF_DDR		= 0x82,	///< DDR
} MPLL_OFF;

/**
 * @ingroup type
 *
 * @defgroup Inv Emueration of clock inverse
 * @{
 *
 * @var EN_SYS_CLK_INV
 *
 * The enumeration of clock inverse 
 *
 * @hideinitializer
 */
typedef enum _EN_SYS_CLK_INV
{
	///< Engine clock select (0x44)
	EN_SYS_CLK_INV_MAC0 			= ((0x44 << 3) + 6),	///< bit[6]
	EN_SYS_CLK_INV_MAC1 			= ((0x44 << 3) + 13),	///< bit[13]
	EN_SYS_CLK_INV_MAC_OUT,									///< bit[14]
} EN_SYS_CLK_INV;
//@}

/**
 * @ingroup type
 *
 * @defgroup Src Emueration of clock source
 * @{
 *
 * @var EN_SYS_CLK_SRC
 *
 * The enumeration of clock inverse 
 *
 * @hideinitializer
 */
typedef enum _EN_SYS_CLK_SRC
{
	///< System clock select (0x30)
	EN_SYS_CLK_SRC_AHB = 0,						///< bit[6:5]
	EN_SYS_CLK_SRC_AXI,							///< bit[9:8]

	///< Engine clock select (0x38)                  
	EN_SYS_CLK_SRC_USB_20PHY_12M,				///< bit[5]

	///< Engine clock select (0x44)                  
	EN_SYS_CLK_SRC_MAC_PADIN,					///< bit[0]
	EN_SYS_CLK_SRC_MAC0_DELAY,					///< bit[5:1]
	EN_SYS_CLK_SRC_MAC1_DIV2_EN,				///< bit[7], for MAC clock from pad-in
	EN_SYS_CLK_SRC_MAC1_DELAY,					///< bit[12:8]
	EN_SYS_CLK_SRC_MAC_OUTPUT_CLK_EN,			///< bit[15]

	///< Engine clock select (0x48)                  
	EN_SYS_CLK_SRC_USB_U2_U3_30M,				///< bit[22]
	EN_SYS_CLK_SRC_USB_U0_U1_30M,				///< bit[27]
	EN_SYS_CLK_SRC_USB30,						///< bit[30]
} EN_SYS_CLK_SRC;
//@}

/**
 * @internal
 * @ingroup type
 *
 * @defgroup AHBPLL Enumeration of AHB clock clock source
 * @{
 *
 * @typedef EN_SYS_AHB_CLK_SRC
 *
 * A enumeration for AHB clock source select 
 */
typedef enum _EN_SYS_AHB_CLK_SRC
{
	///< AHB clock source select
    EN_SYS_AHB_CLK_SRC_REF_96M	= 0b00,	///< 2'b00: refserence clock 96M 
    EN_SYS_AHB_CLK_SRC_ARM_D8	= 0b01,	///< 2'b01: ARM PLL div 8
    EN_SYS_AHB_CLK_SRC_AHB		= 0b10,	///< 2'b1x: AHB PLL
} EN_SYS_AHB_CLK_SRC;
//@}

/**
 * @internal
 * @ingroup type
 *
 * @defgroup AXIPLL Enumeration of AXI clock clock source
 * @{
 *
 * @typedef EN_SYS_AXI_CLK_SRC
 *
 * A enumeration for AXI clock source select 
 */
typedef enum _EN_SYS_AXI_CLK_SRC
{
	///< AHB clock source select
    EN_SYS_AXI_CLK_SRC_ARM_D8	= 0b00,	///< 2'b00: ARM PLL div 8 
    EN_SYS_AXI_CLK_SRC_DDR_D2	= 0b01,	///< 2'b01: DDR PLL div 2
    EN_SYS_AXI_CLK_SRC_AXI_D2	= 0b10,	///< 2'b1x: AXI PLL div 2
    EN_SYS_AXI_CLK_SRC_AXI		= 0b11,	///< 2'b1x: AXI PLL
} EN_SYS_AXI_CLK_SRC;
//@}

/**
 * @ingroup type
 *
 * @defgroup RST Emueration of clock reset
 * @{
 *
 * @var EN_SYS_CLK_RST
 *
 * The enumeration of clock reset
 *
 * @hideinitializer
 */
typedef enum _EN_SYS_CLK_RST
{
	///< Parition A, core clock reset disable/enable (0x50/0x54)
	EN_SYS_CLK_RST_CORE_USB20_U0,						///< bit[8]
	EN_SYS_CLK_RST_CORE_USB20_U1,						///< bit[9]

	///< Partition E/F/G/H, core clock reset disable/enable (0xD4/0xD8)
	EN_SYS_CLK_RST_CORE_USB20_U2,						///< bit[25]
	EN_SYS_CLK_RST_CORE_USB20_U3,						///< bit[26]
#if 0
	EN_SYS_CLK_RST_CORE_USB30,							///< bit[13]
#endif
	EN_SYS_CLK_RST_CORE_USB30_HCLK_D2,					///< bit[27]

	///< Partition A, AXI clock reset disable/enable (0x70/0x74)
	EN_SYS_CLK_RST_AXI_USB20_U0,						///< bit[6]
	EN_SYS_CLK_RST_AXI_USB20_U1,						///< bit[7]

	///< Partition E/F, core clock reset disable/enable (0xDC/0xE0)
	EN_SYS_CLK_RST_AXI_USB20_U2,						///< bit[16]
	EN_SYS_CLK_RST_AXI_USB20_U3,						///< bit[17]
	EN_SYS_CLK_RST_AXI_USB30,							///< bit[18]

	///< Partition A, AHB clock reset disable/enable (0x90/0x94)
	EN_SYS_CLK_RST_AHB_USB20_U0,						///< bit[8]
	EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,					///< bit[9]
	EN_SYS_CLK_RST_AHB_USB20_U1,						///< bit[10]
	EN_SYS_CLK_RST_AHB_USB20_U1_PCLK,					///< bit[11]

	///< Partition E/F/G, AHB clock reset disable/enable (0xE4/0xE8)
	EN_SYS_CLK_RST_AHB_USB20_U2,						///< bit[14]
	EN_SYS_CLK_RST_AHB_USB20_U3,						///< bit[15]
	EN_SYS_CLK_RST_AHB_USB20_U2_PCLK,					///< bit[16]
	EN_SYS_CLK_RST_AHB_USB20_U3_PCLK,					///< bit[17]
	EN_SYS_CLK_RST_AHB_USB20,							///< bit[18]
	EN_SYS_CLK_RST_AHB_USB30_PCLK,						///< bit[19]
} EN_SYS_CLK_RST;
//@}

/**
 * @ingroup type
 *
 * @defgroup MASK Emueration of clock mask
 * @{
 *
 * @var EN_SYS_CLK_MASK
 *
 * The enumeration of clock mask
 *
 * @hideinitializer
 */
typedef enum _EN_SYS_CLK_MASK
{
	///< Partition A, AHB clock mask (0x00)
	EN_SYS_CLK_MASK_AHB_USB20_U0		= ((0x00 << 3) + 7),	///< bit[7]
	EN_SYS_CLK_MASK_AHB_USB20_U1,								///< bit[8]

	///< Partition E/F/G/H, AHB clock mask (0xc8)
	EN_SYS_CLK_MASK_AHB_USB30			= ((0xc8 << 3) + 21), 	///< bit[21]
	EN_SYS_CLK_MASK_AHB_USB20_U2, 								///< bit[22]
	EN_SYS_CLK_MASK_AHB_USB20_U3, 								///< bit[23]
	EN_SYS_CLK_MASK_AHB_MAC				= ((0xc8 << 3) + 18), 	///< bit[18]

	///< Partition A, AXI clock mask (0x10)
	EN_SYS_CLK_MASK_AXI_USB20_U0		= ((0x10 << 3) + 5),	///< bit[5]
	EN_SYS_CLK_MASK_AXI_USB20_U1,								///< bit[6]

	///< Partition E/F/G/H, AXI clock mask (0xcc)
	EN_SYS_CLK_MASK_AXI_USB20_U2		= ((0xcc << 3) + 16),	///< bit[16]
	EN_SYS_CLK_MASK_AXI_USB20_U3,								///< bit[17]
	EN_SYS_CLK_MASK_AXI_USB30,									///< bit[18]
	EN_SYS_CLK_MASK_AXI_MAC				= ((0xcc << 3) + 13),	///< bit[13]

	///< Partition A, engine clock mask (0x20)
	EN_SYS_CLK_MASK_CORE_USB20_A_12M	= ((0x20 << 3) + 20),	///< bit[12]
	EN_SYS_CLK_MASK_CORE_USB20_U0_30M, 							///< bit[13]
	EN_SYS_CLK_MASK_CORE_USB20_U1_30M, 							///< bit[14]

	///< Parition E/F/G/H, engine clock mask (0xd0)
	EN_SYS_CLK_MASK_CORE_USB20_F_12M	= ((0xd0 << 3) + 26),	///< bit[26]
	EN_SYS_CLK_MASK_CORE_USB20_U2_30M, 							///< bit[27]
	EN_SYS_CLK_MASK_CORE_USB20_U3_30M, 							///< bit[28]
	EN_SYS_CLK_MASK_CORE_USB30_20PHY,							///< bit[29]
	EN_SYS_CLK_MASK_CORE_USB30_30PHY,							///< bit[30]
	EN_SYS_CLK_MASK_CORE_MAC0			= ((0xd0 << 3) + 22),	///< bit[22]
	EN_SYS_CLK_MASK_CORE_MAC_TX, 								///< bit[23]
	EN_SYS_CLK_MASK_CORE_MAC_RX, 								///< bit[24]
} EN_SYS_CLK_MASK;
//@}

/**
 * @fn void SYS_CLK_SetClockSource(EN_SYS_CLK_SRC enSrc, u32 u32Src)
 *
 * @brief  Set clock source for specific top or engine
 * 
 * @param[in]  enSrc	Indicate the clock source which will be changed.
 * @param[in]  u32Src	New clock source value
 *
 */
void SYS_CLK_SetClockSource(EN_SYS_CLK_SRC enSrc, u32 u32Src);

/**
 * @fn void SYS_SetClockReset(EN_SYS_CLK_RST enRst, bool b8EnRst)
 *
 * @brief Clock reset enable/disable
 *
 * @param[in] enRst		Clock reset which will be enable or disable
 * @param[in] b8EnRst	TRUE: enable clock reset, FALSE: disable clock reset
 *
 */
void SYS_SetClockReset(EN_SYS_CLK_RST enRst, bool b8EnRst);

/**
 * @fn void SYS_CLK_SetClockInv(EN_SYS_CLK_INV enInv, bool b8EnInv)
 *
 * @brief Clock inverse enable/disable
 *
 * @param[in] enInv		Clock inverse which will be enable or disable
 * @param[in] b8EnInv	TRUE: clock inverse enable, FALSE clock inversse disable
 *
 */
void SYS_SetClockInv(EN_SYS_CLK_INV enInv, bool b8EnInv);

/**
 * @fn void SYS_SetClockMask(EN_SYS_CLK_MASK enMask, bool b8EnMask)
 *
 * @brief Clock mask enable/disable
 *
 * @param[in] enMask	Clock mask which will be enable or disable
 * @param[in] b8EnMask	TRUE: clock mask enable, FALSE clock mask disable
 *
 */
void SYS_SetClockMask(EN_SYS_CLK_MASK enMask, bool b8EnMask);

/**
 * @brief Get System PLL by Offset
 *
 * @param [in] off	Offset
 * @return Frequency of PLL (MHz)
 */
unsigned long SYS_CLK_GetMpll(unsigned long off);

/**
 * @brief Set Value to System PLL by Offset
 *
 * @param [in] off	Offset
 * @param [in] freq	Frequency (MHz)
 */
unsigned long SYS_CLK_SetMpll(unsigned long off, unsigned long freq);

/**
 * Get frequency of AHB clock
 */
unsigned long get_ahb_clk(void);

/**
 * Get frequency of CPU clock
 */
unsigned long get_cpu_clk(void);

/**
 * Get frequency of EMMC clock
 */
unsigned long get_mmc_clk(void);

/**
 * Get frequency of AXI clock
 */
unsigned long get_axi_clk(void);

/**
 * Get frequency of Peripheral clock
 */
unsigned long get_periph_clk(void);

/**
 * Initialize clock
 *
 * 	Get base address of clkgen, MPLL and APLL
 * 	Get frequency of ARM, AHB and EMMC
 */
void __init nvt_clk_init(void);

#endif /* __NVT_CLK_H__ */

