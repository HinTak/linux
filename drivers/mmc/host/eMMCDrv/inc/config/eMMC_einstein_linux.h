#ifndef __eMMC_EINSTEIN_LINUX__
#define __eMMC_EINSTEIN_LINUX__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/scatterlist.h>
//#include <mstar/mstar_chip.h>
#include <../../../../../../arch/arm/arm-boards/x14/include/mach/mstar_chip.h>
#include <mach/io.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif
#include "chip_int.h"

#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/kthread.h>

//=====================================================
// HW registers
//=====================================================
#define REG_OFFSET_SHIFT_BITS               2U

#define REG_FCIE_U16(Reg_Addr)              ((*(volatile U16*)(Reg_Addr)))
#define GET_REG_ADDR(x, y)                  (((U32)x)+(((U32)y) << REG_OFFSET_SHIFT_BITS))

#define REG_FCIE(reg_addr)                  REG_FCIE_U16((U32)(reg_addr))
#define REG_FCIE_W(reg_addr, val)           REG_FCIE((U32)reg_addr) = (U16)(val)
#define REG_FCIE_R(reg_addr, val)           val = REG_FCIE((U32)reg_addr)
#define REG_FCIE_SETBIT(reg_addr, val)      REG_FCIE((U32)(reg_addr)) |= ((U16)(val))
//#define REG_FCIE_CLRBIT(reg_addr, val)      REG_FCIE((U32)(reg_addr)) &= ~(U16)((U16)(val))
#define REG_FCIE_CLRBIT(reg_addr, val)      REG_FCIE((U32)(reg_addr)) = (U16)(REG_FCIE(reg_addr) & (~(U16)(val)))

#define REG_FCIE_W1C(reg_addr, val)         REG_FCIE_W((U32)(reg_addr), REG_FCIE(reg_addr)&(val))

//------------------------------
#define RIU_PM_BASE                     (IO_ADDRESS(0x1F000000))
#define RIU_BASE                        (IO_ADDRESS(0x1F200000))

#define REG_BANK_FCIE0                  0x8980		// (0x1113 - 0x1000) x 80h
#define REG_BANK_FCIE1                  0x89E0		//              the same bank with FCIE0
#define REG_BANK_FCIE2                  0x8A00		// (0x1114 - 0x1000) x 80h
#define REG_BANK_FCIE_CRC               0x11D80U	// (0x123B - 0x1000) x 80h

#define FCIE0_BASE                      GET_REG_ADDR(RIU_BASE, REG_BANK_FCIE0)
#define FCIE1_BASE                      GET_REG_ADDR(RIU_BASE, REG_BANK_FCIE1)
#define FCIE2_BASE                      GET_REG_ADDR(RIU_BASE, REG_BANK_FCIE2)
#define FCIE_CRC_BASE			GET_REG_ADDR(RIU_BASE, REG_BANK_FCIE_CRC)

#define FCIE_REG_BASE_ADDR              FCIE0_BASE
#define FCIE_CIFC_BASE_ADDR             FCIE1_BASE
#define FCIE_CIFD_BASE_ADDR             FCIE2_BASE
#define FCIE_CRC_BASE_ADDR		FCIE_CRC_BASE

#include "eMMC_reg.h"

//--------------------------------miu2-----------------------------------------
#define REG_BANK_MIU2			0x0300	// (0x1006 - 0x1000) x 80h
#define MIU2_BASE			GET_REG_ADDR(RIU_BASE, REG_BANK_MIU2)

//--------------------------------clock gen------------------------------------
#define REG_BANK_CLKGEN0		0x0580	// (0x100B - 0x1000) x 80h
#define CLKGEN0_BASE			GET_REG_ADDR(RIU_BASE, REG_BANK_CLKGEN0)

//--------------------------------chiptop--------------------------------------
#define REG_BANK_CHIPTOP		0x0F00	// (0x101E - 0x1000) x 80h
#define PAD_CHIPTOP_BASE		GET_REG_ADDR(RIU_BASE, REG_BANK_CHIPTOP)

//--------------------------------gpio-----------------------------------------
#define REG_BANK_GPIO			0x1580	// (0x102B - 0x1000) x 80h
#define PAD_GPIO_BASE		GET_REG_ADDR(RIU_BASE, REG_BANK_GPIO)

//--------------------------------emmc pll-------------------------------------
#define REG_BANK_EMMC_PLL		0x11F80	// (0x123F - 0x1000) x 80h
#define EMMC_PLL_BASE			GET_REG_ADDR(RIU_BASE, REG_BANK_EMMC_PLL)



#define REG_BANK_PM_TOP                     0x0F00 // 0x001E x 80h
#define PM_TOP_BASE                         GET_REG_ADDR(RIU_PM_BASE, REG_BANK_PM_TOP)

#define EINSTEIN_ECO			GET_REG_ADDR(PM_TOP_BASE, 0x01)


#define REG_BANK_TIMER1                     0x1800 // 0x0030 x 80h
#define TIMER1_BASE                         GET_REG_ADDR(RIU_PM_BASE, REG_BANK_TIMER1)

#define TIMER1_ENABLE                       GET_REG_ADDR(TIMER1_BASE, 0x20)
#define TIMER1_HIT                          GET_REG_ADDR(TIMER1_BASE, 0x21)
#define TIMER1_MAX_LOW                      GET_REG_ADDR(TIMER1_BASE, 0x22)
#define TIMER1_MAX_HIGH                     GET_REG_ADDR(TIMER1_BASE, 0x23)
#define TIMER1_CAP_LOW                      GET_REG_ADDR(TIMER1_BASE, 0x24)
#define TIMER1_CAP_HIGH                     GET_REG_ADDR(TIMER1_BASE, 0x25)


//--------------------------------miu2-----------------------------------------

typedef struct _RIU_MIU2 { // [100Bh] PAD_CLKGEN_BASE

	// 0x00 ~ 0x79 (122)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32;
// 0x7A
	U32 			:9;
	U32 reg_miu_sel2	:1; // 1: control by fcie miu_sel
	U32			:22;

}RIU_MIU2;

//--------------------------------clock gen------------------------------------

#define BIT_FCIE_CLK_20M	0x1
#define BIT_FCIE_CLK_27M	0x2
#define BIT_FCIE_CLK_32M	0x3
#define BIT_FCIE_CLK_36M        0x4
#define BIT_FCIE_CLK_40M        0x5
#define BIT_FCIE_CLK_43_2M	0x6
#define BIT_FCIE_CLK_54M	0x7
#define BIT_FCIE_CLK_62M	0x8
#define BIT_FCIE_CLK_72M	0x9
#define BIT_FCIE_CLK_EMMC_PLL	0xA
#define BIT_FCIE_CLK_86M	0xB
			//	0xC
#define BIT_FCIE_CLK_300K	0xD
#define BIT_FCIE_CLK_XTAL	0xE
#define BIT_FCIE_CLK_48M	0xF

#define eMMC_PLL_CLK__20M	0x8001
#define eMMC_PLL_CLK__27M	0x8002
#define eMMC_PLL_CLK__32M	0x8003
#define eMMC_PLL_CLK__36M	0x8004
#define eMMC_PLL_CLK__40M	0x8005
#define eMMC_PLL_CLK__48M	0x8006
#define eMMC_PLL_CLK__52M	0x8007
#define eMMC_PLL_CLK__62M	0x8008
#define eMMC_PLL_CLK__72M	0x8009
#define eMMC_PLL_CLK__80M	0x800A
#define eMMC_PLL_CLK__86M	0x800B
#define eMMC_PLL_CLK_100M	0x800C
#define eMMC_PLL_CLK_120M	0x800D
#define eMMC_PLL_CLK_140M	0x800E
#define eMMC_PLL_CLK_160M	0x800F
#define eMMC_PLL_CLK_200M	0x8010
#define eMMC_PLL_CLK_SLOW	eMMC_PLL_CLK__20M
#define eMMC_PLL_CLK_FAST	eMMC_PLL_CLK_200M


#define eMMC_FCIE_VALID_CLK_CNT 8 // FIXME
extern  U8 gau8_FCIEClkSel[];

typedef struct _RIU_CLKGEN { // [100Bh] PAD_CLKGEN_BASE

	// 0x00 ~ 0x63 (100)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
// 0x64
	U32 reg_fcie_clk_gating	:1; // 0: active
	U32 reg_fcie_clk_inverse:1; // 0: active
	U32 reg_clkgen_fcie	:4;
	U32 reg_fcie_clk_src_sel:1; // 1: active
	U32 			:25;

}RIU_CLKGEN;

//--------------------------------chiptop--------------------------------------

typedef struct _RIU_CHIPTOP { // [101Eh] PAD_CHIPTOP_BASE

	// 0x00 ~ 0x07 (8)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
// 0x08
	U32 reg_emmc_drv_rstz	:1;
	U32 reg_emmc_drv_clk	:1;
	U32 reg_emmc_drv_cmd	:1;
	U32			:29;
	// 0x09
	U32:32;
// 0x0A
	U32 reg_pcm_pe_23_16	:8;
	U32			:24;
	// 0x0B
	U32:32;
// 0x0C
	U32			:8;
	U32 reg_emmc_drv_d2	:1;
	U32 reg_emmc_drv_d3	:1;
	U32 reg_emmc_drv_d4	:1;
	U32 reg_emmc_drv_d0	:1;
	U32 reg_emmc_drv_d5	:1;
	U32 reg_emmc_drv_d1	:1;
	U32 reg_emmc_drv_d6	:1;
	U32 reg_emmc_drv_d7	:1;
	U32			:16;
// 0x0D
	U32 reg_emmc_pe_d0	:1;
	U32 reg_emmc_pe_d1	:1;
	U32 reg_emmc_pe_d2	:1;
	U32 reg_emmc_pe_d3	:1;
	U32 reg_emmc_pe_d4	:1;
	U32 reg_emmc_pe_d5	:1;
	U32 reg_emmc_pe_d6	:1;
	U32 reg_emmc_pe_d7	:1;
	U32 reg_emmc_pe_cmd	:1;
	U32 reg_emmc_pe_clk	:1;
	U32 reg_emmc_pe_rstn	:1;
	U32			:21;
	// 0x0E
	U32:32;
// 0x0F
	U32 reg_emmc_ps_d0	:1;
	U32 reg_emmc_ps_d1	:1;
	U32 reg_emmc_ps_d2	:1;
	U32 reg_emmc_ps_d3	:1;
	U32 reg_emmc_ps_d4	:1;
	U32 reg_emmc_ps_d5	:1;
	U32 reg_emmc_ps_d6	:1;
	U32 reg_emmc_ps_d7	:1;
	U32 reg_emmc_ps_cmd	:1;
	U32 reg_emmc_ps_clk	:1;
	U32 reg_emmc_ps_rstn	:1;
	U32			:21;
// 0x10
	U32				:8;
	U32 reg_fcie2macro_sd_bypass	:1;
	U32				:23;
	// 0x11
	U32:32;
// 0x12
	U32 reg_test_in_mode	:3;
	U32			:1;
	U32 reg_test_out_mode	:3;
	U32			:25;
// 0x13
	U32 reg_emmc_pad_driving:4;
	U32 			:28;
	// 0x14 ~ 0x3F (44)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32;
// 0x40
	U32 reg_sd_use_bypass	:1;
	U32			:31;
	// 0x41 ~ 0x42
	U32:32; U32:32;
// 0x43
	U32			:8;
	U32 reg_emmc_rstz_sw	:1; // emmc reset value
	U32			:23;
	// 0x44 ~ 0x4E (11)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32;
// 0x4F
	U32			:2;
	U32 reg_emmc_rstz_en	:1; // emmc reset enable
	U32			:29;
// 0x50
	U32			:15;
	U32 reg_all_pad_in	:1;
	U32			:16;
	// 0x51 ~ 0x59 (9)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; //(9)
// 0x5A
	U32			:8;
	U32 reg_sd_config	:2;
	U32			:22;

	// 0x5B ~ 0x63 (9)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; //(9)
// 0x64
	U32 reg_ciadconfig	:1;
	U32			:2;
	U32 reg_pcm2ctrlconfig	:1;
	U32 reg_pcmadconfig	:1;
	U32			:27;
	// 0x65 ~ 0x6D (9)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; //(9)
// 0x6E
	U32			:6;
	U32 reg_emmc_config	:2;
	U32			:24;
// 0x6F
	U32			:5;
	U32 reg_nand_cs1_en	:1;
	U32 reg_nand_mode	:2;
	U32			:24;
	// 0x70 ~ 0x7A (11)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32;
// 0x7B
	U32			:4;
	U32 reg_sdio_config	:2;
	U32			:26;

} RIU_CHIPTOP;

#define eMMC_RST_L()	{								\
	volatile RIU_CHIPTOP * const pRegChiptop = (RIU_CHIPTOP *)PAD_CHIPTOP_BASE;	\
	pRegChiptop->reg_emmc_rstz_en = 1;						\
	pRegChiptop->reg_emmc_rstz_sw = 0;						\
}

#define eMMC_RST_H()	{								\
	volatile RIU_CHIPTOP * const pRegChiptop = (RIU_CHIPTOP *)PAD_CHIPTOP_BASE;	\
	pRegChiptop->reg_emmc_rstz_en = 1;						\
	pRegChiptop->reg_emmc_rstz_sw = 1;						\
}

//--------------------------------gpio-----------------------------------------

typedef struct _RIU_GPIO { // [102Bh] PAD_GPIO_BASE

	// 0x00 ~ 0x31 (50)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
// 0x32
	U32 			:8;
	U32 reg_eeprom_wp_out	:1;
	U32 reg_eeprom_wp_oen	:1;
	U32 reg_eeprom_wp_in	:1;
	U32 			:21;
	// 0x33 ~ 0x48 (22)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32;
// 0x49
	U32 reg_gpio_156_out	:1; // CON1_ID
	U32 reg_gpio_156_oen	:1;
	U32 reg_gpio_156_in	:1;
	U32 			:5;
	U32 reg_gpio_155_out	:1; // AVI_ID
	U32 reg_gpio_155_oen	:1;
	U32 reg_gpio_155_in	:1;
	U32 			:21;

} RIU_GPIO;

//--------------------------------emmc pll-------------------------------------

typedef struct _RIU_EMMC_PLL { // [123Fh] EMMC_PLL_BASE

	// 0x00 ~ 0x02 (3)
	U32:32; U32:32; U32:32;
// 0x03
	U32 reg_emmc_pll_clkph_skew1	:4;
	U32 reg_emmc_pll_clkph_skew2	:4;
	U32 reg_emmc_pll_clkph_skew3	:4;
	U32 reg_emmc_pll_clkph_skew4	:4;
	U32				:16;
// 0x04
	U32 reg_emmc_pll_fbdiv_1st_etc	:8;
	U32 reg_emmc_pll_fbdiv_2nd	:8;
	U32				:16;
// 0x05
	U32 reg_emmc_pll_pdiv		:3; // [7:0] total 8 bits
	U32				:29;
// 0x06
	U32 reg_emmc_pll_reset		:1;
	U32				:31;
// 0x07
	U32 				:10;
	U32 reg_emmc_pll_test		:1; // [20:0] total 21 bits, andersen: reg_emmc_pll_test[10]
	U32				:21;
	// 0x08 ~ 0x17 (16)
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
	U32:32; U32:32; U32:32; U32:32; U32:32; U32:32;
// 0x18
	U32 reg_ddfset_15_00		:16;
	U32				:16;
// 0x19
	U32 reg_ddfset_23_16		:8;
	U32				:24;
// 0x1A
	U32 reg_emmc_test		:1;
	U32				:1;
	U32 reg_emmc_path		:1;
	U32				:29;
	// 0x1B
	U32:32;
// 0x1C
	U32 reg_hs200_patch		:1; // bit 0
	U32 reg_rsp_meta_patch_sw	:1; // bit 1
	U32 reg_rsp_meta_patch_hw	:1; // bit 2
	U32 reg_d0_meta_patch_sw	:1; // bit 3
	U32 reg_d0_meta_patch_hw	:1; // bit 4
	U32 reg_d0_in_patch		:1; // bit 5
	U32 reg_emmc_dqs_patch		:1; // bit 6
	U32 reg_rsp_mask_patch		:1; // bit 7
	U32 reg_ddr_rsp_patch		:1; // bit 8
	U32				:23;

} RIU_EMMCPLL;

//-----------------------------------------------------------------------------

#define eMMC_DBUS_WIDTH                 8


#define BIT_SD_DEFAULT_MODE_REG             (BIT_SD_CLK_AUTO_STOP|BIT_SD_DATA_SYNC|BIT_SD_CLK_EN)

//=====================================================
// API declarations
//=====================================================
extern void dump_mem(char *buf, unsigned int count);
extern U32 eMMC_hw_timer_delay(U32 u32us);
extern U32 eMMC_hw_timer_sleep(U32 u32ms);

#define eMMC_HW_TIMER_MHZ   1000000	//(384*100*1000)  // [FIXME]
#define FCIE_eMMC_DISABLE	0
#define FCIE_eMMC_DDR		1
#define FCIE_eMMC_SDR		2
#define FCIE_eMMC_BYPASS	3 // never use this
#define FCIE_eMMC_TMUX		4
#define FCIE_eMMC_HS200		5

#define FCIE_DEFAULT_PAD                FCIE_eMMC_SDR

extern U32 eMMC_pads_switch(U32 u32_FCIE_IF_Type);
extern U32 eMMC_pll_setting(U16 u16_ClkParam);
extern U32 eMMC_clock_setting(U16 u16_ClkParam);
extern U32 eMMC_clock_gating(void);
extern void eMMC_set_WatchDog(U8 u8_IfEnable);
extern void eMMC_reset_WatchDog(void);
extern U32 eMMC_translate_DMA_address_Ex(U32 u32_DMAAddr, U32 u32_ByteCnt);
extern void eMMC_flush_data_cache_buffer(U32 u32_DMAAddr, U32 u32_ByteCnt);
extern void eMMC_Invalidate_data_cache_buffer(U32 u32_DMAAddr, U32 u32_ByteCnt);
extern void eMMC_flush_miu_pipe(void);
extern U32 eMMC_PlatformResetPre(void);
extern U32 eMMC_PlatformResetPost(void);
extern void eMMC_SetSkew4(U32 u32Skew4);
extern U32 eMMC_PlatformInit(void);
extern U32 eMMC_PlatformDeinit(void);
extern U32 eMMC_CheckIfMemCorrupt(void);
extern void eMMC_DumpPadClk(void);
extern void eMMC_DumpDebugInfo(void);

#define eMMC_BOOT_PART_W                BIT0
#define eMMC_BOOT_PART_R                BIT1
extern U32 eMMC_BootPartitionHandler_WR(U8 * pDataBuf, U16 u16_PartType, U32 u32_StartSector,
					U32 u32_SectorCnt, U8 u8_OP);
extern U32 eMMC_BootPartitionHandler_E(U16 u16_PartType);

extern  void eMMC_hw_timer_start(void);
extern void eMMC_hw_timer_stop(void);
extern  U32  eMMC_hw_timer_tick(void);
extern  U32 eMMC_TimerGetUs(void);

extern irqreturn_t eMMC_FCIE_IRQ(int irq, void *dummy);	// [FIXME]
extern U32 eMMC_WaitCompleteIntr(U32 u32_RegAddr, U16 u16_WaitEvent, U32 u32_MicroSec);
extern struct mutex FCIE3_mutex;
extern void eMMC_LockFCIE(void);
extern void eMMC_UnlockFCIE(void);
extern void eMMC_GPIO_init(void);
extern void eMMC_GPIO_Debug(U32 u32GPIO, U8 On);
extern void eMMC_DumpCRCBank(void);
extern void eMMC_DebugDumpData(struct mmc_data * pData);

//=====================================================
// partitions config
//=====================================================
// every blk is 512 bytes (reserve 2MB-64KB for internal use)
#define eMMC_DRV_RESERVED_BLK_CNT       ((0x200000-0x10000)/0x200)

#define eMMC_CIS_NNI_BLK_CNT            2
#define eMMC_CIS_PNI_BLK_CNT            2
#define eMMC_TEST_BLK_CNT               (0x100000/0x200)	// 1MB

#define eMMC_CIS_BLK_0                  (64*1024/512)	// from 64KB
#define eMMC_NNI_BLK_0                  (eMMC_CIS_BLK_0+0)
#define eMMC_NNI_BLK_1                  (eMMC_CIS_BLK_0+1)
#define eMMC_PNI_BLK_0                  (eMMC_CIS_BLK_0+2)
#define eMMC_PNI_BLK_1                  (eMMC_CIS_BLK_0+3)
#define eMMC_DDRTABLE_BLK_0             (eMMC_CIS_BLK_0+4)
#define eMMC_DDRTABLE_BLK_1             (eMMC_CIS_BLK_0+5)
#define eMMC_DrvContext_BLK_0           (eMMC_CIS_BLK_0+6)
#define eMMC_DrvContext_BLK_1           (eMMC_CIS_BLK_0+7)
#define eMMC_ALLRSP_BLK_0               (eMMC_CIS_BLK_0+8)
#define eMMC_ALLRSP_BLK_1               (eMMC_CIS_BLK_0+9)
#define eMMC_BURST_LEN_BLK_0            (eMMC_CIS_BLK_0+10)

#define eMMC_CIS_BLK_END                eMMC_BURST_LEN_BLK_0
// last 1MB in reserved area, use for eMMC test
#define eMMC_TEST_BLK_0                 (eMMC_CIS_BLK_END+1)

#define eMMC_LOGI_PART                  0x8000	// bit-or if the partition needs Wear-Leveling
#define eMMC_HIDDEN_PART                0x4000	// bit-or if this partition is hidden, normally it is set for the LOGI PARTs.

#define eMMC_PART_HWCONFIG              (1|eMMC_LOGI_PART)
#define eMMC_PART_BOOTLOGO              (2|eMMC_LOGI_PART)
#define eMMC_PART_BL                    (3|eMMC_LOGI_PART|eMMC_HIDDEN_PART)
#define eMMC_PART_OS                    (4|eMMC_LOGI_PART)
#define eMMC_PART_CUS                   (5|eMMC_LOGI_PART)
#define eMMC_PART_UBOOT                 (6|eMMC_LOGI_PART|eMMC_HIDDEN_PART)
#define eMMC_PART_SECINFO               (7|eMMC_LOGI_PART|eMMC_HIDDEN_PART)
#define eMMC_PART_OTP                   (8|eMMC_LOGI_PART|eMMC_HIDDEN_PART)
#define eMMC_PART_RECOVERY              (9|eMMC_LOGI_PART)
#define eMMC_PART_E2PBAK                (10|eMMC_LOGI_PART)
#define eMMC_PART_NVRAMBAK              (11|eMMC_LOGI_PART)
#define eMMC_PART_APANIC                (12|eMMC_LOGI_PART)
#define eMMC_PART_ENV                   (13|eMMC_LOGI_PART|eMMC_HIDDEN_PART)	// uboot env
#define eMMC_PART_MISC                  (14|eMMC_LOGI_PART)
#define eMMC_PART_DEV_NODE              (15|eMMC_LOGI_PART|eMMC_HIDDEN_PART)

#define eMMC_PART_FDD                   (17|eMMC_LOGI_PART)
#define eMMC_PART_TDD                   (18|eMMC_LOGI_PART)

#define eMMC_PART_E2P0                  (19|eMMC_LOGI_PART)
#define eMMC_PART_E2P1                  (20|eMMC_LOGI_PART)
#define eMMC_PART_NVRAM0                (21|eMMC_LOGI_PART)
#define eMMC_PART_NVRAM1                (22|eMMC_LOGI_PART)
#define eMMC_PART_SYSTEM                (23|eMMC_LOGI_PART)
#define eMMC_PART_CACHE                 (24|eMMC_LOGI_PART)
#define eMMC_PART_DATA                  (25|eMMC_LOGI_PART)
#define eMMC_PART_FAT                   (26|eMMC_LOGI_PART)
extern char *gpas8_eMMCPartName[];

//=====================================================
// Driver configs
//=====================================================
#define DRIVER_NAME                     "mstar_mci"
#define eMMC_UPDATE_FIRMWARE            0

#define eMMC_ST_PLAT                    0x80000000
// [CAUTION]: to verify IP and HAL code, defaut 0
#define IF_IP_VERIFY                    0	// [FIXME] -->
// [CAUTION]: to detect DDR timiing parameters, only for DL
#define IF_DETECT_eMMC_DDR_TIMING       0
#define eMMC_IF_DDRT_TUNING()           (g_eMMCDrv.u32_DrvFlag&DRV_FLAG_DDR_TUNING)

#define IF_FCIE_SHARE_PINS		0	// 1: need to eMMC_pads_switch
#define IF_FCIE_SHARE_CLK               0	// 1: need to eMMC_clock_setting
#define IF_FCIE_SHARE_IP                0

// need to eMMC_pads_switch
// need to eMMC_clock_setting

//------------------------------
#define FICE_BYTE_MODE_ENABLE			1 // always 1
#define ENABLE_eMMC_INTERRUPT_MODE		1
#define ENABLE_eMMC_RIU_MODE			0 // for debug cache issue
#define ENABLE_eMMC_ADMA_MODE			1

#define ASYNCIO_SUPPORT                     1

#define HPI_SUPPORT                         0

#define PERF_PROFILE                        0 // debug use

#define PRINT_REQUEST_INFO                  0 // debug use

#define GPIO_DEBUG                          0 // debug use

#define CHECK_ABORT_TIMING                  0 // debug use

#define HARDWARE_TIMER_CHECK                0 // debug use

#define SPEED_UP_EMMC_RESUME			1

#if ENABLE_eMMC_RIU_MODE
#undef IF_DETECT_eMMC_DDR_TIMING
#define IF_DETECT_eMMC_DDR_TIMING       0	// RIU mode can NOT use DDR
#endif
// <-- [FIXME]

//------------------------------
#define eMMC_FEATURE_RELIABLE_WRITE     1
#if eMMC_UPDATE_FIRMWARE
#undef  eMMC_FEATURE_RELIABLE_WRITE
#define eMMC_FEATURE_RELIABLE_WRITE     0
#endif

//------------------------------
#define eMMC_RSP_FROM_RAM               1
#define eMMC_BURST_LEN_AUTOCFG              1
#define eMMC_PROFILE_WR                 0

//------------------------------
#define eMMC_SECTOR_BUF_BYTECTN         eMMC_SECTOR_BUF_16KB
extern U8 gau8_eMMC_SectorBuf[];
extern U8 gau8_eMMC_PartInfoBuf[];

//------------------------------
// Boot Partition:
//   [FIXME]: if platform has ROM code like G2P
//------------------------------
//      No Need in A3
#define BL_BLK_OFFSET                   0
#define BL_BLK_CNT                      (0xF200/0x200)
#define OTP_BLK_OFFSET                  BL_BLK_CNT
#define OTP_BLK_CNT                     (0x8000/0x200)
#define SecInfo_BLK_OFFSET              (BL_BLK_CNT+OTP_BLK_CNT)
#define SecInfo_BLK_CNT                 (0x1000/0x200)
#define BOOT_PART_TOTAL_CNT             (BL_BLK_CNT+OTP_BLK_CNT+SecInfo_BLK_CNT)
// <-- [FIXME]

#define eMMC_CACHE_LINE                 0x20	// [FIXME]

//=====================================================
// tool-chain attributes
//===================================================== [FIXME] -->
#define eMMC_PACK0
#define eMMC_PACK1                      __attribute__((__packed__))
#define eMMC_ALIGN0
#define eMMC_ALIGN1                     __attribute__((aligned(eMMC_CACHE_LINE)))
// <-- [FIXME]

//=====================================================
// debug option
//=====================================================
#define eMMC_TEST_IN_DESIGN             0	// [FIXME]: set 1 to verify HW timer

#ifndef eMMC_DEBUG_MSG
#define eMMC_DEBUG_MSG                  1
#endif

/* Define trace levels. */
#define eMMC_DEBUG_LEVEL_ERROR          (1)    /* Error condition debug messages. */
#define eMMC_DEBUG_LEVEL_WARNING        (2)    /* Warning condition debug messages. */
#define eMMC_DEBUG_LEVEL_HIGH           (3)    /* Debug messages (high debugging). */
#define eMMC_DEBUG_LEVEL_MEDIUM         (4)    /* Debug messages. */
#define eMMC_DEBUG_LEVEL_LOW            (5)    /* Debug messages (low debugging). */

/* Higer debug level means more verbose */
#ifndef eMMC_DEBUG_LEVEL
#define eMMC_DEBUG_LEVEL                eMMC_DEBUG_LEVEL_HIGH
#endif

#if defined(eMMC_DEBUG_MSG) && eMMC_DEBUG_MSG

#define eMMC_printf(fmt, arg...)	printk(KERN_ERR fmt, ##arg)

#define eMMC_DEBUG_LEVEL_LINUX		eMMC_DEBUG_LEVEL_LOW
#define eMMC_DEBUG_LEVEL_FCIE		eMMC_DEBUG_LEVEL_LOW
#define eMMC_DEBUG_LEVEL_CLOCK		eMMC_DEBUG_LEVEL_MEDIUM
#define eMMC_DEBUG_LEVEL_PAD		eMMC_DEBUG_LEVEL_MEDIUM

#define eMMC_debug(dbg_lv, tag, str, ...)						\
	do {										\
		if (dbg_lv > eMMC_DEBUG_LEVEL)						\
			break;								\
		else if(eMMC_IF_DDRT_TUNING())						\
			break;								\
		else {									\
			if (tag)							\
				eMMC_printf("[ %s() Ln.%u ] ", __FUNCTION__, __LINE__);	\
			eMMC_printf(str, ##__VA_ARGS__);				\
		}									\
	} while(0)
#else /* eMMC_DEBUG_MSG */
#define eMMC_printf(...)
#define eMMC_debug(enable, tag, str, ...)	do{}while(0)
#endif /* eMMC_DEBUG_MSG */

#define eMMC_die(str) do {								\
	eMMC_printf("eMMC Die: %s() Ln.%u, %s \n", __FUNCTION__, __LINE__, str);	\
	panic("\n");									\
} while(0)

#define eMMC_stop() \
	while(1)  eMMC_reset_WatchDog();

#define REG_BANK_TIMER1                 0x1800
#define TIMER1_BASE                     GET_REG_ADDR(RIU_PM_BASE, REG_BANK_TIMER1)

#define TIMER1_ENABLE                   GET_REG_ADDR(TIMER1_BASE, 0x20)
#define TIMER1_HIT                      GET_REG_ADDR(TIMER1_BASE, 0x21)
#define TIMER1_MAX_LOW                  GET_REG_ADDR(TIMER1_BASE, 0x22)
#define TIMER1_MAX_HIGH                 GET_REG_ADDR(TIMER1_BASE, 0x23)
#define TIMER1_CAP_LOW                  GET_REG_ADDR(TIMER1_BASE, 0x24)
#define TIMER1_CAP_HIGH                 GET_REG_ADDR(TIMER1_BASE, 0x25)

//=====================================================
// unit for HW Timer delay (unit of us)
//=====================================================
#define HW_TIMER_DELAY_1us              1
#define HW_TIMER_DELAY_5us              5
#define HW_TIMER_DELAY_10us             10
#define HW_TIMER_DELAY_100us            100
#define HW_TIMER_DELAY_500us            500
#define HW_TIMER_DELAY_1ms              (1000 * HW_TIMER_DELAY_1us)
#define HW_TIMER_DELAY_5ms              (5    * HW_TIMER_DELAY_1ms)
#define HW_TIMER_DELAY_10ms             (10   * HW_TIMER_DELAY_1ms)
#define HW_TIMER_DELAY_100ms            (100  * HW_TIMER_DELAY_1ms)
#define HW_TIMER_DELAY_500ms            (500  * HW_TIMER_DELAY_1ms)
#define HW_TIMER_DELAY_1s               (1000 * HW_TIMER_DELAY_1ms)

//=====================================================
// set FCIE clock
//=====================================================
#define FCIE_SLOWEST_CLK                BIT_FCIE_CLK_300K
#define FCIE_SLOW_CLK                   BIT_FCIE_CLK_20M
#define FCIE_DEFAULT_CLK                BIT_FCIE_CLK_48M
#define FCIE_DDR52_CLK			eMMC_PLL_CLK__52M
#define FCIE_HS200_CLK			eMMC_PLL_CLK_200M
//=====================================================
// transfer DMA Address
//=====================================================
#define MIU_BUS_WIDTH_BITS              3	// Need to confirm
/*
 * Important:
 * The following buffers should be large enough for a whole eMMC block
 */
// FIXME, this is only for verifing IP
#define DMA_W_ADDR                      0x40C00000
#define DMA_R_ADDR                      0x40D00000
#define DMA_W_SPARE_ADDR                0x40E00000
#define DMA_R_SPARE_ADDR                0x40E80000
#define DMA_BAD_BLK_BUF                 0x40F00000

#define MIU_CHECK_LAST_DONE             1

//=====================================================
// misc
//=====================================================
//#define BIG_ENDIAN
#define LITTLE_ENDIAN

#if (defined(BIT_DQS_MODE_MASK) && (BIT_DQS_MODE_MASK != (BIT12|BIT13|BIT14)))

#undef BIT_DQS_MODE_MASK
#undef BIT_DQS_MODE_2T
#undef BIT_DQS_MODE_1_5T
#undef BIT_DQS_MODE_2_5T
#undef BIT_DQS_MODE_1T

#define BIT_DQS_MODE_MASK               (BIT12|BIT13|BIT14)
#define BIT_DQS_MODE_0T                 (0 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_0_5T               (1 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_1T                 (2 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_1_5T               (3 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_2T                 (4 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_2_5T               (5 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_3T                 (6 << BIT_DQS_MDOE_SHIFT)
#define BIT_DQS_MODE_3_5T               (7 << BIT_DQS_MDOE_SHIFT)

#endif

#endif	/* __eMMC_G2P_UBOOT__ */
