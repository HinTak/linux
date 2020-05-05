////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2006-2012 MStar Semiconductor, Inc.
// All rights reserved.
//
// Unless otherwise stipulated in writing, any and all information contained
// herein regardless in any format shall remain the sole proprietary of
// MStar Semiconductor Inc. and be kept in strict confidence
// ("MStar Confidential Information") by the recipient.
// Any unauthorized act including without limitation unauthorized disclosure,
// copying, use, reproduction, sale, distribution, modification, disassembling,
// reverse engineering and compiling of the contents of MStar Confidential
// Information is unlawful and strictly prohibited. MStar hereby reserves the
// rights to any and all damages, losses, costs and expenses resulting therefrom.
//
////////////////////////////////////////////////////////////////////////////////

#include "eMMC.h"
#if defined(UNIFIED_eMMC_DRIVER) && UNIFIED_eMMC_DRIVER

static void dump_mem_line(char *buf, int cnt);

static void
dump_mem_line(char *buf, int cnt)
{
	int i;
	for(i = 0; i< cnt; i ++)
		printk("%02X ", (unsigned char)buf[i]);
	printk("|");
	for(i = 0; i< cnt; i ++)
		printk("%c", (buf[i] >= 32 && buf[i] < 128) ? buf[i] : '.');

	printk("\n");
}

void
dump_mem(char *buf, unsigned int count)
{
	unsigned int i;
	for(i = 0; i < count; i +=16) {
		printk("%08Xh %04d %04Xh: ",(U32)(buf+i),  i, i);
		dump_mem_line(buf + i, 16);
		if(i%512==496) printk("\n");
	}
}

U32
eMMC_hw_timer_delay(U32 u32us)
{
	U32 u32_i = u32us;

	while (u32_i > 1000) {
		mdelay(1);
		u32_i -= 1000;
	}
	udelay(u32_i);

	return u32us;
}

void eMMC_DebugDumpData(struct mmc_data * pData)
{
	U32 i, u32BusAddr, u32DmaLeng;
	struct scatterlist  *pScatterList = 0;

	for(i=0; i<pData->sg_len; i++) {

		pScatterList = &(pData->sg[i]);
		u32BusAddr = sg_dma_address(pScatterList);
		u32DmaLeng = sg_dma_len(pScatterList);
		dump_mem(phys_to_virt(u32BusAddr), u32DmaLeng);
		//dump_mem(phys_to_virt(u32BusAddr), 0x16);
	}
}

void eMMC_DumpCRCBank(void)
{
	U16 u16_i;
	volatile U16 u16_reg;

    U16 u16Job, u16JobRemain;

    eMMC_debug(0, 0, "[CRC]:\n");
    eMMC_debug(0, 0, "blkno: %08Xh\n", g_eMMCDrv.u32BlockAddrMightKeepCRC);

    u16Job = REG_FCIE_U16(FCIE_JOB_BL_CNT);
    REG_FCIE_SETBIT(FCIE_JOB_BL_CNT, BIT15);
    REG_FCIE_CLRBIT(FCIE_JOB_BL_CNT, BIT14);
    u16JobRemain = REG_FCIE_U16(FCIE_TR_BK_CNT); // check device side
    REG_FCIE_CLRBIT(FCIE_JOB_BL_CNT, BIT15); // restore for reg dump

    if(u16JobRemain) {
        eMMC_debug(0, 0, "Has remain job to do %u/%u\n", u16JobRemain, u16Job);
        eMMC_debug(0, 0, "last blkno should be: %08Xh\n", g_eMMCDrv.u32BlockAddrMightKeepCRC - u16JobRemain);
    }

	for (u16_i = 0; u16_i < 0x10; u16_i++) {
		if (0 == u16_i % 8)
			eMMC_debug(eMMC_DEBUG_LEVEL, 0, "\n%02Xh:| ", u16_i);

		REG_FCIE_R(GET_REG_ADDR(FCIE_CRC_BASE_ADDR, u16_i), u16_reg);
		eMMC_debug(eMMC_DEBUG_LEVEL, 0, "%04Xh ", u16_reg);
	}
	eMMC_debug(eMMC_DEBUG_LEVEL, 0, "\n\n");

}


#if (defined(CONFIG_MSTAR_PreX14)&&CONFIG_MSTAR_PreX14)

#if defined(HARDWARE_TIMER_CHECK)&&HARDWARE_TIMER_CHECK

void eMMC_hw_timer_start(void)
{
    // Reset PIU Timer1
    REG_FCIE_W(TIMER1_MAX_LOW, 0xFFFF);
    REG_FCIE_W(TIMER1_MAX_HIGH, 0xFFFF);
    REG_FCIE_W(TIMER1_ENABLE, 0);

    // Start PIU Timer1
    REG_FCIE_W(TIMER1_ENABLE, 0x1);
}

// 12MHz --> 83.3333 ns for 1 tick

// 1 sec       --> 0x00B71B00
// 1 mili sec  -->     0x2EE0
// 1 micro sec -->        0xC

// 12MHz, unit = 0.0833 us, 357.9139 sec overflow

U32 eMMC_hw_timer_tick(void)
{
    U32 u32HWTimer = 0;
    U32 u32TimerLow = 0;
    U32 u32TimerHigh = 0;

    // Get timer value
    u32TimerLow = REG_FCIE(TIMER1_CAP_LOW);
    u32TimerHigh = REG_FCIE(TIMER1_CAP_HIGH);

    u32HWTimer = (u32TimerHigh<<16) | u32TimerLow;

    return u32HWTimer;
}

U32 eMMC_Tick2MicroSec(U32 u32Tick)
{
    return (u32Tick/12);
}

U32 eMMC_TimerGetUs(void)
{
    return eMMC_Tick2MicroSec(eMMC_hw_timer_tick());
}

void eMMC_hw_timer_stop(void)
{
    REG_FCIE_W(TIMER1_ENABLE, 0);
}

#endif

void
eMMC_DumpPadClk(void)
{
	//----------------------------------------------
	eMMC_debug(0, 0, "\n[clk setting]: %uKHz \n", g_eMMCDrv.u32_ClkKHz);
	eMMC_debug(0, 0, "FCIE 1X (0x%X):0x%04X\n", reg_ckg_fcie_1X, REG_FCIE_U16(reg_ckg_fcie_1X));
	eMMC_debug(0, 0, "FCIE 4X (0x%X):0x%04X\n", reg_ckg_fcie_4X, REG_FCIE_U16(reg_ckg_fcie_4X));
	eMMC_debug(0, 0, "MIU (0x%X):0x%04X\n", reg_ckg_MIU, REG_FCIE_U16(reg_ckg_MIU));
	eMMC_debug(0, 0, "MCU (0x%X):0x%04X\n", reg_ckg_MCU, REG_FCIE_U16(reg_ckg_MCU));

	//----------------------------------------------
	eMMC_debug(0, 0, "\n[pad setting]: ");
	switch (g_eMMCDrv.u32_DrvFlag & FCIE_FLAG_PADTYPE_MASK) {
	case FCIE_FLAG_PADTYPE_DDR:
		eMMC_debug(0, 0, "DDR\n");
		break;
	case FCIE_FLAG_PADTYPE_SDR:
		eMMC_debug(0, 0, "SDR\n");
		break;
	case FCIE_FLAG_PADTYPE_BYPASS:
		eMMC_debug(0, 0, "BYPASS\n");
		break;
	default:
		eMMC_debug(0, 0, "eMMC Err: Pad unknown\n");
		eMMC_die("\n");
	}

	eMMC_debug(0, 0, "chiptop_0x12 (0x%X):0x%04X\n", reg_chiptop_0x12,
		   REG_FCIE_U16(reg_chiptop_0x12));
	eMMC_debug(0, 0, "chiptop_0x64 (0x%X):0x%04X\n", reg_chiptop_0x64,
		   REG_FCIE_U16(reg_chiptop_0x64));
	eMMC_debug(0, 0, "chiptop_0x40 (0x%X):0x%04X\n", reg_chiptop_0x40,
		   REG_FCIE_U16(reg_chiptop_0x40));
	eMMC_debug(0, 0, "chiptop_0x10 (0x%X):0x%04X\n", reg_chiptop_0x10,
		   REG_FCIE_U16(reg_chiptop_0x10));
	eMMC_debug(0, 0, "chiptop_0x1D (0x%X):0x%04X\n", reg_chip_dummy1,
		   REG_FCIE_U16(reg_chip_dummy1));
	eMMC_debug(0, 0, "chiptop_0x6F (0x%X):0x%04X\n", reg_chiptop_0x6F,
		   REG_FCIE_U16(reg_chiptop_0x6F));
	eMMC_debug(0, 0, "chiptop_0x5A (0x%X):0x%04X\n", reg_chiptop_0x5A,
		   REG_FCIE_U16(reg_chiptop_0x5A));
	eMMC_debug(0, 0, "chiptop_0x6E (0x%X):0x%04X\n", reg_chiptop_0x6E,
		   REG_FCIE_U16(reg_chiptop_0x6E));
	eMMC_debug(0, 0, "chiptop_0x08 (0x%X):0x%04X\n", reg_chiptop_0x08,
		   REG_FCIE_U16(reg_chiptop_0x08));
	eMMC_debug(0, 0, "chiptop_0x09 (0x%X):0x%04X\n", reg_chiptop_0x09,
		   REG_FCIE_U16(reg_chiptop_0x09));
	eMMC_debug(0, 0, "chiptop_0x0A (0x%X):0x%04X\n", reg_chiptop_0x0A,
		   REG_FCIE_U16(reg_chiptop_0x0A));
	eMMC_debug(0, 0, "chiptop_0x0B (0x%X):0x%04X\n", reg_chiptop_0x0B,
		   REG_FCIE_U16(reg_chiptop_0x0B));
	eMMC_debug(0, 0, "chiptop_0x0C (0x%X):0x%04X\n", reg_chiptop_0x0C,
		   REG_FCIE_U16(reg_chiptop_0x0C));
	eMMC_debug(0, 0, "chiptop_0x0D (0x%X):0x%04X\n", reg_chiptop_0x0D,
		   REG_FCIE_U16(reg_chiptop_0x0D));
	eMMC_debug(0, 0, "chiptop_0x50 (0x%X):0x%04X\n", reg_chiptop_0x50,
		   REG_FCIE_U16(reg_chiptop_0x50));
	eMMC_debug(0, 0, "chiptop_0x65 (0x%X):0x%04X\n", reg_chip_config,
		   REG_FCIE_U16(reg_chip_config));

	eMMC_debug(0, 0, "\n");
}

U32
eMMC_pads_switch(U32 u32_FCIE_IF_Type)
{
	g_eMMCDrv.u32_DrvFlag &= ~FCIE_FLAG_PADTYPE_MASK;

	// pull up clock
	REG_FCIE_CLRBIT(REG_CHIPTOP_0x08, BIT1); REG_FCIE_SETBIT(reg_chiptop_0x6E, BIT12);

	switch (u32_FCIE_IF_Type) {
	case FCIE_eMMC_DDR:
		eMMC_debug(eMMC_DEBUG_LEVEL_PAD, 0, "eMMC pads: DDR\n");
		REG_FCIE_CLRBIT(FCIE_REG_2Dh, BIT0);
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8 | BIT9);
		g_eMMCDrv.u32_DrvFlag |= FCIE_FLAG_PADTYPE_DDR;
		break;

	case FCIE_eMMC_SDR:
		eMMC_debug(eMMC_DEBUG_LEVEL_PAD, 0, "eMMC pads: SDR\n");
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8);
		g_eMMCDrv.u32_DrvFlag |= FCIE_FLAG_PADTYPE_SDR;
		break;

	case FCIE_eMMC_BYPASS:
		eMMC_debug(eMMC_DEBUG_LEVEL_PAD, 0, "eMMC pads: BYPASS\n");
		REG_FCIE_SETBIT(reg_chiptop_0x10, BIT8);
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8 | BIT10 | BIT11);
		g_eMMCDrv.u32_DrvFlag |= FCIE_FLAG_PADTYPE_BYPASS;
		break;

	default:
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: unknown interface: %X\n",
			   u32_FCIE_IF_Type);
		return eMMC_ST_ERR_INVALID_PARAM;
	}

	// set chiptop

	return eMMC_ST_SUCCESS;
}

#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
static U8 sgau8_FCIEClk_1X_To_4X_[0x10] =	// index is 1X reg value
{
	0,
	BIT_FCIE_CLK4X_20M,
	BIT_FCIE_CLK4X_27M,
	0,
	BIT_FCIE_CLK4X_36M,
	BIT_FCIE_CLK4X_40M,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	BIT_FCIE_CLK4X_48M
};
#endif

U32
eMMC_clock_setting(U32 u32ClkParam)
{
	eMMC_PlatformResetPre();

	switch (u32ClkParam) {
	case BIT_FCIE_CLK_300K:
		g_eMMCDrv.u32_ClkKHz = 300;
		break;
	case BIT_FCIE_CLK_20M:
		g_eMMCDrv.u32_ClkKHz = 20000;
		break;

	case BIT_FCIE_CLK_27M:
		g_eMMCDrv.u32_ClkKHz = 27000;
		break;
#if !(defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING)
	case BIT_FCIE_CLK_32M:
		g_eMMCDrv.u32_ClkKHz = 32000;
		break;
#endif
	case BIT_FCIE_CLK_36M:
		g_eMMCDrv.u32_ClkKHz = 36000;
		break;
	case BIT_FCIE_CLK_40M:
		g_eMMCDrv.u32_ClkKHz = 40000;
		break;
#if !(defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING)
	case BIT_FCIE_CLK_43_2M:
		g_eMMCDrv.u32_ClkKHz = 43200;
		break;
#endif
	case BIT_FCIE_CLK_48M:
		g_eMMCDrv.u32_ClkKHz = 48000;
		break;
	default:
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: u32ClkParam:%u %Xh\n",
			   u32ClkParam, eMMC_ST_ERR_INVALID_PARAM);
		return eMMC_ST_ERR_INVALID_PARAM;
	}

	REG_FCIE_CLRBIT(reg_ckg_fcie_1X, (BIT_FCIE_CLK_Gate | BIT_FCIE_CLK_MASK));
	REG_FCIE_SETBIT(reg_ckg_fcie_1X, BIT_FCIE_CLK_SEL | (u32ClkParam << BIT_FCIE_CLK_SHIFT));

#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
	if( g_eMMCDrv.u32_DrvFlag & DRV_FLAG_DDR_MODE ) {
		REG_FCIE_CLRBIT(reg_ckg_fcie_4X, BIT_FCIE_CLK4X_Gate|BIT_FCIE_CLK4X_MASK);
		REG_FCIE_SETBIT(reg_ckg_fcie_4X, (sgau8_FCIEClk_1X_To_4X_[u32ClkParam]<<BIT_FCIE_CLK4X_SHIFT));

		// 1. enable DDR timing patch
		REG_FCIE_CLRBIT(FCIE_REG_2Dh, BIT_DDR_TIMING_PATCH_RSP);
		REG_FCIE_SETBIT(reg_chip_dummy1, BIT_DDR_TIMING_PATCH);

		// 2. disable reg_sd_en
		REG_FCIE_CLRBIT(FCIE_PATH_CTRL, BIT_SD_EN);
		REG_FCIE_SETBIT(reg_chip_dummy1, BIT_SW_RST_Z_EN); // enable lock reset

		// 3. do lock reset
		REG_FCIE_SETBIT(reg_chip_dummy1, BIT_SW_RST_Z);
		eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
		REG_FCIE_CLRBIT(reg_chip_dummy1, BIT_SW_RST_Z);

		// 4. enable DDR mode
		REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_SD_EN);
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT_SD_DDR_EN);
	}
#endif

	eMMC_debug(eMMC_DEBUG_LEVEL_CLOCK, 0,
		   "clk:%uKHz, Param:%d, fcie_1X(%Xh):%04Xh, fcie_4X(%Xh):%04Xh\n",
		   g_eMMCDrv.u32_ClkKHz, u32ClkParam, reg_ckg_fcie_1X,
		   REG_FCIE_U16(reg_ckg_fcie_1X), reg_ckg_fcie_4X, REG_FCIE_U16(reg_ckg_fcie_4X));

	g_eMMCDrv.u16_ClkRegVal = (U16) u32ClkParam;
	eMMC_PlatformResetPost();

	return eMMC_ST_SUCCESS;
}

U32
eMMC_clock_gating(void)
{
	eMMC_PlatformResetPre();
	g_eMMCDrv.u32_ClkKHz = 0;
	REG_FCIE_W(reg_ckg_fcie_1X, BIT_FCIE_CLK_Gate);
	REG_FCIE_W(reg_ckg_fcie_4X, BIT_FCIE_CLK4X_Gate);
	REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_CLK_EN);
	eMMC_PlatformResetPost();

	return eMMC_ST_SUCCESS;
}

U8 gau8_FCIEClkSel[eMMC_FCIE_VALID_CLK_CNT] = {
	BIT_FCIE_CLK_48M,
	BIT_FCIE_CLK_40M,
	BIT_FCIE_CLK_36M,
	BIT_FCIE_CLK_27M,
	BIT_FCIE_CLK_20M,
};

void
eMMC_set_WatchDog(U8 u8_IfEnable)
{
	// do nothing
}

void
eMMC_reset_WatchDog(void)
{
	// do nothing
}

U32
eMMC_translate_DMA_address_Ex(U32 u32_DMAAddr, U32 u32_ByteCnt)
{
	return (virt_to_phys((void *) u32_DMAAddr));
}

void
eMMC_Invalidate_data_cache_buffer(U32 u32_addr, S32 s32_size)
{

}

void
eMMC_flush_miu_pipe(void)
{

}

//---------------------------------------
//#if defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE
static DECLARE_WAIT_QUEUE_HEAD(fcie_wait);
static volatile U32 fcie_int = 0;
static volatile U32 abort_request_intr = 0;
//#endif

#if defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK
static DECLARE_WAIT_QUEUE_HEAD(emmc_busy_wait);
static volatile U32 emmc_busy_int = 0;
#endif

#if (defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE) || \
    (defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK)
irqreturn_t
eMMC_FCIE_IRQ(int irq, void *dummy)
{
	volatile u32 u32_Events;
	volatile u32 u32_mie_int;

	u32_Events = REG_FCIE(FCIE_MIE_EVENT);
	u32_mie_int = REG_FCIE(FCIE_MIE_INT_EN);

#if defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE
	if ((u32_Events & u32_mie_int) == BIT_SD_CMD_END) {
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_SD_CMD_END);
		fcie_int = 1;
		wake_up(&fcie_wait);
	}

	if ((u32_Events & u32_mie_int) == BIT_MIU_LAST_DONE) { // read
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_MIU_LAST_DONE);
#if defined(PERF_PROFILE)&&PERF_PROFILE
		mstar_mci_log_irq_time_stamp();
#endif
		fcie_int = 1;
		wake_up(&fcie_wait);
	}

	if ((u32_Events & u32_mie_int) == BIT_CARD_DMA_END) { // write
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_CARD_DMA_END);
		fcie_int = 1;
		wake_up(&fcie_wait);
	}
#endif

#if defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK
	if ((u32_Events & u32_mie_int) == BIT_SD_BUSY_END) {
		REG_FCIE_CLRBIT(FCIE_SD_CTRL, BIT_SD_BUSY_DET_ON);
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_SD_BUSY_END);

		emmc_busy_int = 1;
		wake_up(&emmc_busy_wait);
	}
#endif

	return IRQ_HANDLED;
}

#if defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE
U32 eMMC_WaitCompleteIntr(U32 u32_RegAddr, U16 u16_WaitEvent, U32 u32_MicroSec)
{
    U32 u32_i;
    U32 u32_ret = eMMC_ST_SUCCESS;
    U32 u32_wait_ret;
    unsigned long jiffies2wait;
    //U32 u32MicroSec1, u32MicroSec2, u32MicroSec3;

    jiffies2wait = usecs_to_jiffies(u32_MicroSec);

    //eMMC_hw_timer_start();
    //u32MicroSec1 = eMMC_hw_timer_tick();

    if(BIT_CARD_DMA_END&u16_WaitEvent) // wait 4 write
    {
        u32_wait_ret = wait_event_timeout(fcie_wait, (fcie_int||abort_request_intr), (long)jiffies2wait);
    }
    else // wait for command end or others...
    {
        u32_wait_ret = wait_event_timeout(fcie_wait, (fcie_int)                    , (long)jiffies2wait);
    }

    //u32MicroSec2 = eMMC_hw_timer_tick();

    if(u32_wait_ret==0) // switch to polling when intr fail
    {
        for(u32_i=0; u32_i<u32_MicroSec; u32_i++)
        {
            if((REG_FCIE(FCIE_MIE_EVENT) & u16_WaitEvent) == u16_WaitEvent )
                break;
            eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
        }

        if(u32_i == u32_MicroSec) // polling still time out
        {
            // If it is still timeout, check Start bit first
            REG_FCIE_CLRBIT(FCIE_TEST_MODE, BIT_FCIE_DEBUG_MODE_MASK);
            REG_FCIE_SETBIT(FCIE_TEST_MODE, 1<<BIT_FCIE_DEBUG_MODE_SHIFT);

            if( (REG_FCIE(FCIE_DEBUG_BUS0) & 0xFF) == 0x20 )
            {
                eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Warn: lost start bit, let mmc sub system to try again \n");
                eMMC_FCIE_Reset();
                u32_ret = eMMC_ST_ERR_LOST_START_BIT;
                goto ErrorHandle;
            }
            else
            {
                eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: events lose, WaitEvent: %Xh \n", u16_WaitEvent);
                eMMC_FCIE_ErrHandler_Stop();
                u32_ret = eMMC_ST_ERR_INT_TO;
                goto ErrorHandle;
            }
        }
        else // polling success
        {
            //u32MicroSec3 = eMMC_hw_timer_tick();
            //eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 0, "T1 = %d, T2 = %d, jiffies2wait = %ld\n", u32MicroSec2 - u32MicroSec1, u32MicroSec3 - u32MicroSec1, jiffies2wait);
            //eMMC_GPIO60_Debug(1);
            eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 0, "eMMC Warn: intr lose but polling ok, Event: %04Xh, Wait: %04Xh, IntEn: %04Xh\n", REG_FCIE(FCIE_MIE_EVENT), u16_WaitEvent, REG_FCIE(FCIE_MIE_INT_EN));
            eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "CIFC %02Xh\n", eMMC_FCIE_CmdRspBufGet(0));
            REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, u16_WaitEvent);
            //eMMC_GPIO60_Debug(0);
        }
    }

ErrorHandle:

    //eMMC_printf("wait complete intr: %u\n", u32_wait_ret); // 750

    if(fcie_int) // dma finish
    {
        //eMMC_printf("dma finsih\n");
    }
    else if(abort_request_intr)
    {
        //eMMC_printf("\33[1;35mabort success\33[m\n");
        u32_ret = eMMC_ST_ERR_ABORT_REQ;

        #if 1
        //if( REG_FCIE(FCIE_PATH_CTRL) & BIT_MMA_EN )
        {
            // 1. diable MMA_EN
            REG_FCIE_CLRBIT(FCIE_PATH_CTRL, BIT_MMA_EN);

            // 2. Wait mma_data_end
            for(u32_i=0; u32_i<100000; u32_i++) { // 100ms
                eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
                if(BIT_MMA_DATA_END == (REG_FCIE(u32_RegAddr) & BIT_MMA_DATA_END))
                    break;
            }

            // 3. Reset FCIE
            if(eMMC_FCIE_Init())
                eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: fcie init fail\n");
        }
        //this case might happen
        /*else
        {
            printk("mme_en not enable\n");
        }*/
        #endif
    }

    fcie_int = 0;

    return u32_ret;
}

void eMMC_AbortIntrWaitEvent(void)
{
    abort_request_intr = 1;
    wake_up_locked(&fcie_wait); // use in spinlocked
}

void eMMC_ContinueIntrWaitEvent(void)
{
    abort_request_intr = 0;
}
#endif // ENABLE_eMMC_INTERRUPT_MODE
#endif

U32
eMMC_FCIE_WaitD0High(void)
{
	volatile U32 u32_cnt;
	U16 u16_read0, u16_read1;

#if defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK

	// enable busy int
	REG_FCIE_SETBIT(FCIE_SD_CTRL, BIT_SD_BUSY_DET_ON);
	REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_SD_BUSY_END);

	// wait event
	if (wait_event_timeout
	    (emmc_busy_wait, (emmc_busy_int == 1),
	     (long int) usecs_to_jiffies(HW_TIMER_DELAY_1s * 3)) == 0) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait busy int timeout\n");

		for (u32_cnt = 0; u32_cnt < TIME_WAIT_DAT0_HIGH; u32_cnt++) {
			REG_FCIE_R(FCIE_SD_STATUS, u16_read0);
			eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
			REG_FCIE_R(FCIE_SD_STATUS, u16_read1);

			if ((u16_read0 & BIT_SD_CARD_D0_ST) && (u16_read1 & BIT_SD_CARD_D0_ST))
				break;
		}

		if (TIME_WAIT_DAT0_HIGH == u32_cnt) {
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait D0 H timeout %u us\n",
				   u32_cnt);
			return eMMC_ST_ERR_TIMEOUT_WAITD0HIGH;
		}
	}

	emmc_busy_int = 0;

	REG_FCIE_W1C(FCIE_MIE_EVENT, BIT_SD_BUSY_END);

#else

	for (u32_cnt = 0; u32_cnt < TIME_WAIT_DAT0_HIGH; u32_cnt++) {
		REG_FCIE_R(FCIE_SD_STATUS, u16_read0);
		eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
		REG_FCIE_R(FCIE_SD_STATUS, u16_read1);

		if ((u16_read0 & BIT_SD_CARD_D0_ST) && (u16_read1 & BIT_SD_CARD_D0_ST))
			break;
	}

	if (TIME_WAIT_DAT0_HIGH == u32_cnt) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait D0 H timeout %u us\n",
			   u32_cnt);
		return eMMC_ST_ERR_TIMEOUT_WAITD0HIGH;
	}
#endif

	return eMMC_ST_SUCCESS;

}

void
eMMC_GPIO60_init(void)
{
    #if defined(GPIO_DEBUG)&&GPIO_DEBUG
    //*(volatile U16*)(IO_ADDRESS(0x1f205618)) &= (~(BIT0|BIT1));
    REG_FCIE_CLRBIT(REG_CHIPTOP_0x02, BIT6+BIT7);
    REG_FCIE_CLRBIT(REG_GPIO_0x06, BIT0+BIT1);
    #endif
}

void
eMMC_GPIO60_Debug(U8 On)
{
    #if defined(GPIO_DEBUG)&&GPIO_DEBUG
    if(On)
    {
        //*(volatile U16*)(IO_ADDRESS(0x1f205618)) |= BIT0; // rise the GPIO 60 high
        REG_FCIE_SETBIT(REG_GPIO_0x06, BIT1); // rise the GPIO 60 high
    }
    else
    {
        //*(volatile U16*)(IO_ADDRESS(0x1f205618)) &= (~BIT0); // fall the GPIO 60 down
        REG_FCIE_CLRBIT(REG_GPIO_0x06, BIT1); // fall the GPIO 60 down
    }
    #endif
}

void eMMC_GPIO_Debug(U32 u32GPIO, U8 On)
{
}

U16 *
eMMC_GetCRCCode(void)
{
	U16 u16_i;
	U16 u16_CRCLen;

	if (g_eMMCDrv.u32_DrvFlag & DRV_FLAG_DDR_MODE) {
		if (g_eMMCDrv.u8_BUS_WIDTH == BIT_SD_DATA_WIDTH_8)
			u16_CRCLen = 16;
		else
			u16_CRCLen = 8;
	} else {
		if (g_eMMCDrv.u8_BUS_WIDTH == BIT_SD_DATA_WIDTH_8)
			u16_CRCLen = 8;
		else
			u16_CRCLen = 4;
	}

	for (u16_i = 0; u16_i < u16_CRCLen; u16_i++)
		g_eMMCDrv.u16_LastBlkCRC[u16_i] = REG_FCIE(GET_REG_ADDR(FCIE_CRC_BASE_ADDR, u16_i));

	return &g_eMMCDrv.u16_LastBlkCRC[0];
}

//EXPORT_SYMBOL(eMMC_GetCRCCode);

DEFINE_SEMAPHORE(fcie_mutex);

void eMMC_LockFCIE(void)
{
	down(&fcie_mutex);
}

void eMMC_UnlockFCIE(void)
{
	up(&fcie_mutex);
}

//---------------------------------------

U32
eMMC_PlatformResetPre(void)
{
	return eMMC_ST_SUCCESS;
}

U32
eMMC_PlatformResetPost(void)
{
	return eMMC_ST_SUCCESS;
}

U32
eMMC_PlatformInit(void)
{
	eMMC_debug(eMMC_DEBUG_LEVEL_FCIE, 1, "\n");

	eMMC_GPIO60_init(); // debug use, need remove

#if defined(HARDWARE_TIMER_CHECK)&&HARDWARE_TIMER_CHECK
	eMMC_hw_timer_start();
#endif
	eMMC_pads_switch(FCIE_DEFAULT_PAD);

	eMMC_clock_setting(FCIE_SLOWEST_CLK);

	return eMMC_ST_SUCCESS;
}

// --------------------------------------------
eMMC_ALIGN0 eMMC_DRIVER g_eMMCDrv eMMC_ALIGN1;

//=============================================================

#elif (defined(CONFIG_MSTAR_X14)&&CONFIG_MSTAR_X14)	// [FIXME] clone for your flatform

// check some fix value, print only when setting wrong
void eMMC_DumpPadClk(void)
{
	volatile RIU_MIU2	* const pRegMiu2    = (RIU_MIU2    *) MIU2_BASE;
	volatile RIU_CLKGEN	* const pRegClkgen  = (RIU_CLKGEN  *) CLKGEN0_BASE;
	volatile RIU_EMMCPLL	* const pRegEmmcPll = (RIU_EMMCPLL *) EMMC_PLL_BASE;
	volatile RIU_CHIPTOP	* const pRegChiptop = (RIU_CHIPTOP *) PAD_CHIPTOP_BASE;

	printk("\n[%s]\n", __FUNCTION__);

	if(pRegMiu2->reg_miu_sel2 != 1)			eMMC_debug(0, 0, "eMMC Err: reg_miu_sel2 = %d\n",		pRegMiu2->reg_miu_sel2);

	//---------------------------------------------------------------------
	if(pRegClkgen->reg_fcie_clk_gating != 0)	eMMC_debug(0, 0, "eMMC Err: reg_fcie_clk_gating = %d\n",	pRegClkgen->reg_fcie_clk_gating);
	if(pRegClkgen->reg_fcie_clk_inverse != 0)	eMMC_debug(0, 0, "eMMC Err: reg_fcie_clk_inverse = %d\n",	pRegClkgen->reg_fcie_clk_inverse);
	if(pRegClkgen->reg_fcie_clk_src_sel != 1)	eMMC_debug(0, 0, "eMMC Err: reg_fcie_clk_src_sel = %d\n",	pRegClkgen->reg_fcie_clk_src_sel);
	/* depends on clock gear selection */		eMMC_debug(0, 0, "reg_clkgen_fcie = %Xh\n",			pRegClkgen->reg_clkgen_fcie);

	//---------------------------------------------------------------------
	if(pRegEmmcPll->reg_emmc_pll_reset != 0)	eMMC_debug(0, 0, "eMMC Err: reg_emmc_pll_reset = %d\n",		pRegEmmcPll->reg_emmc_pll_reset);
	/*pRegEmmcPll->reg_ddfset_23_16;*/		eMMC_debug(0, 0, "reg_ddfset_23_16 = %04Xh\n",			pRegEmmcPll->reg_ddfset_23_16);
	/*pRegEmmcPll->reg_ddfset_15_00;*/		eMMC_debug(0, 0, "reg_ddfset_15_00 = %04Xh\n",			pRegEmmcPll->reg_ddfset_15_00);
	if(pRegEmmcPll->reg_emmc_pll_fbdiv_1st_etc!=0x6)eMMC_debug(0, 0, "eMMC Err: reg_emmc_pll_fbdiv_1st_etc = %d\n",	pRegEmmcPll->reg_emmc_pll_fbdiv_1st_etc);
	if(pRegEmmcPll->reg_emmc_pll_fbdiv_2nd != 0)	eMMC_debug(0, 0, "eMMC Err: reg_emmc_pll_fbdiv_2nd = %d\n",	pRegEmmcPll->reg_emmc_pll_fbdiv_2nd);
	/*pRegEmmcPll->reg_emmc_pll_pdiv;*/		eMMC_debug(0, 0, "reg_emmc_pll_pdiv = %d\n",			pRegEmmcPll->reg_emmc_pll_pdiv);
	if(pRegEmmcPll->reg_emmc_pll_test != 0)		eMMC_debug(0, 0, "eMMC Err: reg_emmc_pll_test = %d\n",		pRegEmmcPll->reg_emmc_pll_test);
	/*pRegEmmcPll->reg_emmc_pll_clkph_skew1;*/	eMMC_debug(0, 0, "reg_emmc_pll_clkph_skew1 = %d\n",		pRegEmmcPll->reg_emmc_pll_clkph_skew1);
	/*pRegEmmcPll->reg_emmc_pll_clkph_skew2;*/	eMMC_debug(0, 0, "reg_emmc_pll_clkph_skew2 = %d\n",		pRegEmmcPll->reg_emmc_pll_clkph_skew2);
	/*pRegEmmcPll->reg_emmc_pll_clkph_skew3;*/	eMMC_debug(0, 0, "reg_emmc_pll_clkph_skew3 = %d\n",		pRegEmmcPll->reg_emmc_pll_clkph_skew3);
	/*pRegEmmcPll->reg_emmc_pll_clkph_skew4;*/	eMMC_debug(0, 0, "reg_emmc_pll_clkph_skew4 = %d\n",		pRegEmmcPll->reg_emmc_pll_clkph_skew4);
							eMMC_debug(0, 0, "FCIE_2Ch = %04Xh\n", REG_FCIE(FCIE_SM_STS));
	if(pRegEmmcPll->reg_emmc_test != 1)		eMMC_debug(0, 0, "eMMC Err: not 1.8V IO setting\n");


	//---------------------------------------------------------------------
	eMMC_debug(0, 0, "[pad setting]: ");
	switch(g_eMMCDrv.u8_PadType)
	{
		case FCIE_eMMC_BYPASS:	eMMC_debug(0, 0, "BYPASS\n");
			if(pRegChiptop->reg_fcie2macro_sd_bypass==0) {
				eMMC_debug(0, 0, "eMMC Err: reg_fcie2macro_sd_bypass = %d\n", pRegChiptop->reg_fcie2macro_sd_bypass);
			}
			break;
		case FCIE_eMMC_SDR:	eMMC_debug(0, 0, "SDR\n");
			if(pRegChiptop->reg_fcie2macro_sd_bypass==0) {
				eMMC_debug(0, 0, "eMMC Err: reg_fcie2macro_sd_bypass = %d\n", pRegChiptop->reg_fcie2macro_sd_bypass);
			}
			break;
		case FCIE_eMMC_DDR:	eMMC_debug(0, 0, "DDR\n");
			if(pRegChiptop->reg_fcie2macro_sd_bypass==0) {
				eMMC_debug(0, 0, "eMMC Err: reg_fcie2macro_sd_bypass = %d\n", pRegChiptop->reg_fcie2macro_sd_bypass);
			}
			break;
		case FCIE_eMMC_HS200:	eMMC_debug(0, 0, "HS200\n");
			if(pRegChiptop->reg_fcie2macro_sd_bypass==0) {
				eMMC_debug(0, 0, "eMMC Err: reg_fcie2macro_sd_bypass = %d\n", pRegChiptop->reg_fcie2macro_sd_bypass);
			}
			break;
		default:
			eMMC_debug(0, 0, "eMMC Err: Pad unknown, %d\n", g_eMMCDrv.u8_PadType); eMMC_die("\n");
			break;
	}

	if(pRegChiptop->reg_test_out_mode != 0)		eMMC_debug(0, 0, "eMMC Err: reg_test_out_mode = %d\n",	pRegChiptop->reg_test_out_mode);
	if(pRegChiptop->reg_test_in_mode != 0)		eMMC_debug(0, 0, "eMMC Err: reg_test_in_mode = %d\n",	pRegChiptop->reg_test_in_mode);
	if(pRegChiptop->reg_ciadconfig != 0)		eMMC_debug(0, 0, "eMMC Err: reg_ciadconfig = %d\n",	pRegChiptop->reg_ciadconfig);
	if(pRegChiptop->reg_pcmadconfig != 0)		eMMC_debug(0, 0, "eMMC Err: reg_pcmadconfig = %d\n",	pRegChiptop->reg_pcmadconfig);
	if(pRegChiptop->reg_pcm2ctrlconfig != 0)	eMMC_debug(0, 0, "eMMC Err: reg_pcm2ctrlconfig = %d\n",	pRegChiptop->reg_pcm2ctrlconfig);
	if(pRegChiptop->reg_sd_use_bypass != 1)		eMMC_debug(0, 0, "eMMC Err: reg_sd_use_bypass = %d\n",	pRegChiptop->reg_sd_use_bypass);
	if(pRegChiptop->reg_nand_cs1_en != 0)		eMMC_debug(0, 0, "eMMC Err: reg_nand_cs1_en = %d\n",	pRegChiptop->reg_nand_cs1_en);
	if(pRegChiptop->reg_sd_config != 0)		eMMC_debug(0, 0, "eMMC Err: reg_sd_config = %d\n",	pRegChiptop->reg_sd_config);
	if(pRegChiptop->reg_sdio_config != 0)		eMMC_debug(0, 0, "eMMC Err: reg_sdio_config = %d\n",	pRegChiptop->reg_sdio_config);
	if(pRegChiptop->reg_nand_mode != 0)		eMMC_debug(0, 0, "eMMC Err: reg_nand_mode = %d\n",	pRegChiptop->reg_nand_mode);
	if(pRegChiptop->reg_emmc_config != 1)		eMMC_debug(0, 0, "eMMC Err: reg_emmc_config = %d\n",	pRegChiptop->reg_emmc_config);
	/*pRegChiptop->reg_emmc_rstz_en = X;*/		eMMC_debug(0, 0, "reg_emmc_rstz_en = %d\n",		pRegChiptop->reg_emmc_rstz_en);
	/*pRegChiptop->reg_emmc_rstz_sw = X;*/		eMMC_debug(0, 0, "reg_emmc_rstz_sw = %d\n",		pRegChiptop->reg_emmc_rstz_sw);
	/*pRegChiptop->reg_pcm_pe_23_16 = X;*/		eMMC_debug(0, 0, "reg_pcm_pe_23_16 = %d\n",		pRegChiptop->reg_pcm_pe_23_16);
	if(pRegChiptop->reg_all_pad_in != 0)		eMMC_debug(0, 0, "eMMC Err: reg_all_pad_in = %d\n",	pRegChiptop->reg_all_pad_in);

	eMMC_debug(0, 0, "FCIE_BOOT_CONFIG = %04Xh\n", REG_FCIE(FCIE_BOOT_CONFIG));
	eMMC_debug(0, 0, "FCIE_SD_MODE = %04Xh (check data sync)\n", REG_FCIE(FCIE_SD_MODE));
	eMMC_debug(0, 0, "FCIE_TOGGLE_CNT = %04Xh\n", REG_FCIE(FCIE_TOGGLE_CNT));
	eMMC_debug(0, 0, "FCIE_MACRO_REDNT = %04Xh\n", REG_FCIE(FCIE_MACRO_REDNT));

}

// set pad first, then config clock
U32 eMMC_pads_switch(U32 u32_FCIE_IF_Type)
{
	volatile RIU_CHIPTOP * const pRegChiptop = (RIU_CHIPTOP *)PAD_CHIPTOP_BASE;
	volatile RIU_EMMCPLL * const pRegEmmcPll = (RIU_EMMCPLL *)EMMC_PLL_BASE;

	pRegChiptop->reg_test_out_mode = 0;
	pRegChiptop->reg_test_in_mode = 0;
	pRegChiptop->reg_ciadconfig = 0;
	pRegChiptop->reg_pcmadconfig = 0;
	pRegChiptop->reg_pcm2ctrlconfig = 0;
	pRegChiptop->reg_sd_use_bypass = 1;
	//pRegChiptop->reg_fcie2macro_sd_bypass --> depends on pad type
	pRegChiptop->reg_nand_cs1_en = 0;
	pRegChiptop->reg_sd_config = 0;
	pRegChiptop->reg_sdio_config = 0;
	pRegChiptop->reg_nand_mode = 0;
	pRegChiptop->reg_emmc_config = 1;
	//pRegChiptop->reg_emmc_rstz_en = X;
	//pRegChiptop->reg_emmc_rstz_sw = X;
	//pRegChiptop->reg_pcm_pe_23_16 = X;
	pRegChiptop->reg_all_pad_in = 0;

	REG_FCIE_CLRBIT(FCIE_BOOT_CONFIG, BIT8 | BIT9 | BIT10 | BIT11 | BIT12 | BIT_MACRO_TO_IP_2ND_4X | BIT15);

	if((REG_FCIE(EINSTEIN_ECO)&0x00FF)==0x0001) {

		REG_FCIE_CLRBIT(FCIE_HS200_PATCH, BIT_HS200_RD_DAT_PATCH|BIT_HS200_NORSP_PATCH|BIT_HS200_W_CRC_PATCH);
		pRegEmmcPll->reg_hs200_patch		= 0;
		pRegEmmcPll->reg_rsp_meta_patch_sw	= 0;
		pRegEmmcPll->reg_rsp_meta_patch_hw	= 0;
		pRegEmmcPll->reg_d0_meta_patch_sw	= 0;
		pRegEmmcPll->reg_d0_meta_patch_hw	= 0;
		pRegEmmcPll->reg_d0_in_patch		= 0;
		pRegEmmcPll->reg_emmc_dqs_patch		= 0;
		pRegEmmcPll->reg_rsp_mask_patch		= 0;
		pRegEmmcPll->reg_ddr_rsp_patch		= 0;

		pRegEmmcPll->reg_emmc_path		= 0;
	}

	switch (u32_FCIE_IF_Type) {

	case FCIE_eMMC_BYPASS:
		//eMMC_debug(1, 0, "eMMC pads: BYPASS\n");
		eMMC_debug(1, 0, "eMMC Warn: Why are you using bypass mode, Daniel not alow this!!!\n");
		pRegChiptop->reg_fcie2macro_sd_bypass = 1;
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8 | BIT10 | BIT11);
		/*if(low speed) {
			REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_DATA_SYNC);
		} else {
			REG_FCIE_SETBIT(FCIE_SD_MODE, BIT_SD_DATA_SYNC);
		}*/
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200|BIT_LATE_DATA0_W_IP_CLK);
		g_eMMCDrv.u8_PadType = FCIE_eMMC_BYPASS;
		break;

	case FCIE_eMMC_SDR:
		//eMMC_debug(1, 0, "eMMC pads: SDR\n");
		pRegChiptop->reg_fcie2macro_sd_bypass = 1;
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8);
		//REG_FCIE_SETBIT(FCIE_SD_MODE, BIT_SD_DATA_SYNC);
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200|BIT_LATE_DATA0_W_IP_CLK);
		g_eMMCDrv.u8_PadType = FCIE_eMMC_SDR;
		break;

	case FCIE_eMMC_DDR:
		//eMMC_debug(1, 0, "eMMC pads: DDR\n");
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8 | BIT9 | BIT_MACRO_TO_IP_2ND_4X);
		//REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_DATA_SYNC); // config in eMMC_ATOP_EnableDDR52()
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200);
		REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_LATE_DATA0_W_IP_CLK);

		if((REG_FCIE(EINSTEIN_ECO)&0x00FF)==0x0001) {

			REG_FCIE_SETBIT(FCIE_HS200_PATCH, BIT_HS200_NORSP_PATCH|BIT_HS200_W_CRC_PATCH);
			pRegEmmcPll->reg_rsp_meta_patch_hw	= 1;
			pRegEmmcPll->reg_d0_meta_patch_hw	= 1;
			pRegEmmcPll->reg_d0_in_patch		= 1;
			pRegEmmcPll->reg_rsp_mask_patch		= 1;
			pRegEmmcPll->reg_ddr_rsp_patch		= 1;

			pRegEmmcPll->reg_emmc_path		= 1;
		}

		pRegEmmcPll->reg_emmc_pll_clkph_skew1 = 5;
		pRegEmmcPll->reg_emmc_pll_clkph_skew2 = 0;
		pRegEmmcPll->reg_emmc_pll_clkph_skew3 = 0;
		g_eMMCDrv.u8_PadType = FCIE_eMMC_DDR;
		pRegChiptop->reg_fcie2macro_sd_bypass = 0; // move to last to prevent glitch
		break;

	case FCIE_eMMC_HS200:
		//eMMC_debug(1, 0, "eMMC pads: SDR200\n");
		REG_FCIE_SETBIT(FCIE_BOOT_CONFIG, BIT8 | BIT_MACRO_TO_IP_2ND_4X | BIT15);
		//REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_DATA_SYNC); // config in eMMC_SetBusSpeed()

		if((REG_FCIE(EINSTEIN_ECO)&0x00FF)==0x0001) {

			REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200|BIT_LATE_DATA0_W_IP_CLK);

			// 0x3F
			REG_FCIE_SETBIT(FCIE_HS200_PATCH, BIT_HS200_RD_DAT_PATCH|BIT_HS200_NORSP_PATCH|BIT_HS200_W_CRC_PATCH|BIT_WRITE_CLK_PATCH);

			pRegEmmcPll->reg_hs200_patch		= 1;
			pRegEmmcPll->reg_rsp_meta_patch_hw	= 1;
			pRegEmmcPll->reg_d0_meta_patch_hw	= 1;
			pRegEmmcPll->reg_d0_in_patch		= 1;
			pRegEmmcPll->reg_emmc_dqs_patch		= 1;
			pRegEmmcPll->reg_rsp_mask_patch		= 1;

			pRegEmmcPll->reg_emmc_path		= 1;


		} else {
			#if 1
				REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200);
				REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_LATE_DATA0_W_IP_CLK);
			#else
				REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_CRC_STATUS_4_HS200|BIT_LATE_DATA0_W_IP_CLK);
			#endif
		}

		if((REG_FCIE(EINSTEIN_ECO)&0x00FF)==0x0001) {
			pRegEmmcPll->reg_emmc_pll_clkph_skew1 = 0;
		} else {
			pRegEmmcPll->reg_emmc_pll_clkph_skew1 = 8;
		}

		pRegEmmcPll->reg_emmc_pll_clkph_skew2 = 0;
		pRegEmmcPll->reg_emmc_pll_clkph_skew3 = 0;
		g_eMMCDrv.u8_PadType = FCIE_eMMC_HS200;
		pRegChiptop->reg_fcie2macro_sd_bypass = 0; // move to last to prevent glitch
		break;

	default:
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: unknown interface: %X\n", u32_FCIE_IF_Type);
		eMMC_die("\n");
		return eMMC_ST_ERR_INVALID_PARAM;
		break;
	}

	return eMMC_ST_SUCCESS;
}

U32 eMMC_pll_setting(U16 u16_ClkParam)
{
	volatile RIU_EMMCPLL * const pRegEmmcPll = (RIU_EMMCPLL *)EMMC_PLL_BASE;
	U32 u32_value_reg_emmc_pll_pdiv;

	// HS200 --> 200M, 160M, 140M, 120M
	// DDR52 -->  52M,  48M, 40M

	//printf("EMMC_PLL_BASE = %Xh\n", EMMC_PLL_BASE);
	//printf("FCIE0_BASE = %Xh\n", FCIE0_BASE);

	// 1. reset emmc pll
	pRegEmmcPll->reg_emmc_pll_reset = 1;
	pRegEmmcPll->reg_emmc_pll_reset = 0;

	// 2. synth clock
	switch(u16_ClkParam)	{
		case eMMC_PLL_CLK_200M: // 200M
#if 0
			pRegEmmcPll->reg_ddfset_23_16 = 0x22;
			pRegEmmcPll->reg_ddfset_15_00 = 0x8F5C;
#else
			// 195MHz
			pRegEmmcPll->reg_ddfset_23_16 = 0x24;
			pRegEmmcPll->reg_ddfset_15_00 = 0x03D8;
#endif
			//printf("pRegEmmcPll->reg_ddfset_15_00 = %Xh\n", pRegEmmcPll->reg_ddfset_15_00);
			//printf("R: %Xh\n", (U32)&(pRegEmmcPll->reg_ddfset_15_00));
			//printf("R: %Xh\n", *(volatile U32 *)(0x1F247E48)); // wrong addr
			//printf("R: %Xh\n", *(volatile U32 *)(0x1F247E60));
			u32_value_reg_emmc_pll_pdiv = 1; // PostDIV: 2
			break;
		case eMMC_PLL_CLK_160M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x2B;
			pRegEmmcPll->reg_ddfset_15_00 = 0x3333;
			u32_value_reg_emmc_pll_pdiv = 1; // PostDIV: 2
			break;
		case eMMC_PLL_CLK_140M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x31;
			pRegEmmcPll->reg_ddfset_15_00 = 0x5F15;
			u32_value_reg_emmc_pll_pdiv = 1; // PostDIV: 2
			break;
		case eMMC_PLL_CLK_120M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x39;
			pRegEmmcPll->reg_ddfset_15_00 = 0x9999;
			u32_value_reg_emmc_pll_pdiv = 1; // PostDIV: 2
			break;
		case eMMC_PLL_CLK_100M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x45;
			pRegEmmcPll->reg_ddfset_15_00 = 0x1EB8;
			u32_value_reg_emmc_pll_pdiv = 1; // PostDIV: 2
			break;
		case eMMC_PLL_CLK__86M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x28;
			pRegEmmcPll->reg_ddfset_15_00 = 0x2FA0;
			u32_value_reg_emmc_pll_pdiv = 2; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__80M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x2B;
			pRegEmmcPll->reg_ddfset_15_00 = 0x3333;
			u32_value_reg_emmc_pll_pdiv = 2; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__72M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x30;
			pRegEmmcPll->reg_ddfset_15_00 = 0x0000;
			u32_value_reg_emmc_pll_pdiv = 2; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__62M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x37;
			pRegEmmcPll->reg_ddfset_15_00 = 0xBDEF;
			u32_value_reg_emmc_pll_pdiv = 4; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__52M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x42;
			pRegEmmcPll->reg_ddfset_15_00 = 0x7627;
			u32_value_reg_emmc_pll_pdiv = 2; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__48M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x48;
			pRegEmmcPll->reg_ddfset_15_00 = 0x0000;
			u32_value_reg_emmc_pll_pdiv = 2; // PostDIV: 4
			break;
		case eMMC_PLL_CLK__40M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x2B;
			pRegEmmcPll->reg_ddfset_15_00 = 0x3333;
			u32_value_reg_emmc_pll_pdiv = 4; // PostDIV: 8
			break;
		case eMMC_PLL_CLK__36M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x30;
			pRegEmmcPll->reg_ddfset_15_00 = 0x0000;
			u32_value_reg_emmc_pll_pdiv = 4; // PostDIV: 8
			break;
		case eMMC_PLL_CLK__32M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x36;
			pRegEmmcPll->reg_ddfset_15_00 = 0x0000;
			u32_value_reg_emmc_pll_pdiv = 4; // PostDIV: 8
			break;
		case eMMC_PLL_CLK__27M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x40;
			pRegEmmcPll->reg_ddfset_15_00 = 0x0000;
			u32_value_reg_emmc_pll_pdiv = 4; // PostDIV: 8
			break;
		case eMMC_PLL_CLK__20M:
			pRegEmmcPll->reg_ddfset_23_16 = 0x2B;
			pRegEmmcPll->reg_ddfset_15_00 = 0x3333;
			u32_value_reg_emmc_pll_pdiv = 7; // PostDIV: 16
			break;
		default:
			eMMC_die("eMMC Err: emmc PLL not configed\n");
			return eMMC_ST_ERR_FCIE_NO_CLK;
			break;
	}

	// 3. VCO clock ( loop N = 4 )
	pRegEmmcPll->reg_emmc_pll_fbdiv_1st_etc = 0x06;	// RX04[ 7:0] = 8¡¦h0E
	pRegEmmcPll->reg_emmc_pll_fbdiv_2nd = 0;	// RX04[15:8] = 8¡¦h00

	// 4. 1X clock
	pRegEmmcPll->reg_emmc_pll_pdiv = u32_value_reg_emmc_pll_pdiv & 7U;
	if(u16_ClkParam==eMMC_PLL_CLK__20M) {
		pRegEmmcPll->reg_emmc_pll_test = 1;
	}
	else {
		pRegEmmcPll->reg_emmc_pll_test = 0;
	}

	return eMMC_ST_SUCCESS;
}

// Notice!!! you need to set pad before config clock
U32 eMMC_clock_setting(U16 u16_ClkParam)
{
	volatile RIU_CLKGEN * const pRegClkgen = (RIU_CLKGEN *)CLKGEN0_BASE;

	//printk("%s(%Xh)\n", __FUNCTION__, u16_ClkParam);
	eMMC_PlatformResetPre();

	REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_CLK_EN);
	pRegClkgen->reg_fcie_clk_gating = 1;
	pRegClkgen->reg_fcie_clk_inverse = 0;
	pRegClkgen->reg_fcie_clk_src_sel = 1;

	//if(g_eMMCDrv.u32_DrvFlag & (DRV_FLAG_DDR_MODE|DRV_FLAG_SPEED_HS200) ) { // use atop + emmc pll

	switch(u16_ClkParam)	{
			case eMMC_PLL_CLK__20M	: g_eMMCDrv.u32_ClkKHz =  20000; break;
			case eMMC_PLL_CLK__27M	: g_eMMCDrv.u32_ClkKHz =  27000; break;
			case eMMC_PLL_CLK__32M	: g_eMMCDrv.u32_ClkKHz =  32000; break;
			case eMMC_PLL_CLK__36M	: g_eMMCDrv.u32_ClkKHz =  36000; break;
			case eMMC_PLL_CLK__40M	: g_eMMCDrv.u32_ClkKHz =  40000; break;
			case eMMC_PLL_CLK__48M	: g_eMMCDrv.u32_ClkKHz =  48000; break;
			case eMMC_PLL_CLK__52M	: g_eMMCDrv.u32_ClkKHz =  52000; break;
			case eMMC_PLL_CLK__62M	: g_eMMCDrv.u32_ClkKHz =  62000; break;
			case eMMC_PLL_CLK__72M	: g_eMMCDrv.u32_ClkKHz =  72000; break;
			case eMMC_PLL_CLK__80M	: g_eMMCDrv.u32_ClkKHz =  80000; break;
			case eMMC_PLL_CLK__86M	: g_eMMCDrv.u32_ClkKHz =  86000; break;
			case eMMC_PLL_CLK_100M	: g_eMMCDrv.u32_ClkKHz = 100000; break;
			case eMMC_PLL_CLK_120M	: g_eMMCDrv.u32_ClkKHz = 120000; break;
			case eMMC_PLL_CLK_140M	: g_eMMCDrv.u32_ClkKHz = 140000; break;
			case eMMC_PLL_CLK_160M	: g_eMMCDrv.u32_ClkKHz = 160000; break;
			case eMMC_PLL_CLK_200M	: g_eMMCDrv.u32_ClkKHz = 200000; break;
			/*default:
				eMMC_debug(1, 1, "eMMC Err: emmc_pll %Xh\n", eMMC_ST_ERR_INVALID_PARAM);
				eMMC_die();
				return eMMC_ST_ERR_INVALID_PARAM; break;*/
		//}

	//}
	//else {
		//switch(u16_ClkParam) {

		case BIT_FCIE_CLK_20M	: g_eMMCDrv.u32_ClkKHz =  20000; break;
		case BIT_FCIE_CLK_27M	: g_eMMCDrv.u32_ClkKHz =  27000; break;
		case BIT_FCIE_CLK_32M	: g_eMMCDrv.u32_ClkKHz =  32000; break;
		case BIT_FCIE_CLK_36M	: g_eMMCDrv.u32_ClkKHz =  36000; break;
		case BIT_FCIE_CLK_40M	: g_eMMCDrv.u32_ClkKHz =  40000; break;
		case BIT_FCIE_CLK_43_2M	: g_eMMCDrv.u32_ClkKHz =  43200; break;
		case BIT_FCIE_CLK_300K	: g_eMMCDrv.u32_ClkKHz =    300; break;
		case BIT_FCIE_CLK_48M	: g_eMMCDrv.u32_ClkKHz =  48000; break;

		default:
			eMMC_debug(1, 1, "eMMC Err: clkgen %Xh\n", eMMC_ST_ERR_INVALID_PARAM);
			eMMC_die("\n");
			return eMMC_ST_ERR_INVALID_PARAM; break;
	}
	//}

	if( (g_eMMCDrv.u32_DrvFlag & (DRV_FLAG_DDR_MODE|DRV_FLAG_SPEED_HS200)) && (u16_ClkParam&0x8000) ) {
		//eMMC_debug(1, 1, "\33[1;36m%s(%Xh) EMMC PLL CLOCK\33[m\n", __FUNCTION__, u16_ClkParam);
		eMMC_pll_setting(u16_ClkParam);
		pRegClkgen->reg_clkgen_fcie = BIT_FCIE_CLK_EMMC_PLL;
	}
	else {
		//eMMC_debug(1, 1, "\33[1;36m%s(%Xh) CLKGEN CLOCK\33[m\n", __FUNCTION__, u16_ClkParam);
		pRegClkgen->reg_clkgen_fcie = u16_ClkParam & 15U;
	}

	pRegClkgen->reg_fcie_clk_gating = 0;

	g_eMMCDrv.u16_ClkRegVal = (U16)u16_ClkParam;

	eMMC_PlatformResetPost();

	return eMMC_ST_SUCCESS;
}

U32 eMMC_clock_gating(void)
{
	volatile RIU_CLKGEN * const pRegClkgen = (RIU_CLKGEN *)CLKGEN0_BASE;

	eMMC_PlatformResetPre();
	g_eMMCDrv.u32_ClkKHz = 0;
	pRegClkgen->reg_fcie_clk_gating = 1; // gate clock
	REG_FCIE_CLRBIT(FCIE_SD_MODE, BIT_SD_CLK_EN);
	eMMC_PlatformResetPost();
	return eMMC_ST_SUCCESS;
}

U8 gau8_FCIEClkSel[eMMC_FCIE_VALID_CLK_CNT] = {

	BIT_FCIE_CLK_48M,
	BIT_FCIE_CLK_43_2M,
	BIT_FCIE_CLK_40M,
	BIT_FCIE_CLK_36M,
	BIT_FCIE_CLK_32M,
	BIT_FCIE_CLK_27M,
	BIT_FCIE_CLK_20M,
	BIT_FCIE_CLK_300K
};



#if 0
	if( mode == 0 ) //Write
	{
		Chip_Clean_Cache_Range_VA_PA(u32_DMAAddr,__pa(u32_DMAAddr), u32_ByteCnt);
	}
	else //Read
	{
		Chip_Flush_Cache_Range_VA_PA(u32_DMAAddr,__pa(u32_DMAAddr), u32_ByteCnt);
	}
#endif

U32
eMMC_translate_DMA_address_Ex(U32 u32_DMAAddr, U32 u32_ByteCnt)
{
	return (virt_to_phys((void *) u32_DMAAddr));
}

//---------------------------------------
#if defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE

static DECLARE_WAIT_QUEUE_HEAD(fcie_wait);
static volatile U32 fcie_int = 0;

#define eMMC_IRQ_DEBUG    0

irqreturn_t
eMMC_FCIE_IRQ(int irq, void *dummy)
{
	volatile u16 u16_Events;

	/*if ((REG_FCIE(FCIE_REG16h) & BIT_EMMC_ACTIVE) != BIT_EMMC_ACTIVE) {
		printk("...");
		return IRQ_NONE;
	}*/
	// one time enable one bit
	u16_Events = REG_FCIE(FCIE_MIE_EVENT) & REG_FCIE(FCIE_MIE_INT_EN);

	if (u16_Events & BIT_MIU_LAST_DONE) {
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_MIU_LAST_DONE);
		fcie_int = 1;
		wake_up(&fcie_wait);
		return IRQ_HANDLED;
	} else if (u16_Events & BIT_CARD_DMA_END) {
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_CARD_DMA_END);
		fcie_int = 1;
		wake_up(&fcie_wait);
		return IRQ_HANDLED;
	} else if (u16_Events & BIT_SD_CMD_END) {
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_SD_CMD_END);
		fcie_int = 1;
		wake_up(&fcie_wait);
		return IRQ_HANDLED;
	} else if(u16_Events & BIT_ADMA_END) {
		REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, BIT_ADMA_END);
		fcie_int = 1;
		wake_up(&fcie_wait);
		return IRQ_HANDLED;
	}
#if eMMC_IRQ_DEBUG
	if (0 == fcie_int)
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Warn: Int St:%Xh, En:%Xh, Evt:%Xh \n",
			   REG_FCIE(FCIE_MIE_EVENT), REG_FCIE(FCIE_MIE_INT_EN), u16_Events);
#endif

	return IRQ_NONE;

}

U32
eMMC_WaitCompleteIntr(U32 u32_RegAddr, U16 u16_WaitEvent, U32 u32_MicroSec)
{
	U32 u32_i = 0;

#if eMMC_IRQ_DEBUG
	U32 u32_isr_tmp[2];
	unsigned long long u64_jiffies_tmp, u64_jiffies_now;
	struct timeval time_st;
	time_t sec_tmp;
	suseconds_t us_tmp;

	u32_isr_tmp[0] = fcie_int;
	do_gettimeofday(&time_st);
	sec_tmp = time_st.tv_sec;
	us_tmp = time_st.tv_usec;
	u64_jiffies_tmp = jiffies_64;
#endif

	//----------------------------------------
	if (wait_event_timeout(fcie_wait, (fcie_int == 1), (signed long)usecs_to_jiffies(u32_MicroSec)) == 0) {
#if eMMC_IRQ_DEBUG
		u32_isr_tmp[1] = fcie_int;
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1,
			   "eMMC Warn: int timeout, WaitEvt:%Xh, NowEvt:%Xh, IntEn:%Xh, ISR:%u->%u->%u \n",
			   u16_WaitEvent, REG_FCIE(FCIE_MIE_EVENT), REG_FCIE(FCIE_MIE_INT_EN),
			   u32_isr_tmp[0], u32_isr_tmp[1], fcie_int);

		do_gettimeofday(&time_st);
		u64_jiffies_now = jiffies_64;
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1,
			   " PassTime: %lu s, %lu us, %llu jiffies.  WaitTime: %u us, %lu jiffies, HZ:%u.\n",
			   time_st.tv_sec - sec_tmp, time_st.tv_usec - us_tmp,
			   u64_jiffies_now - u64_jiffies_tmp, u32_MicroSec,
			   usecs_to_jiffies(u32_MicroSec), HZ);
#else
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1,
			   "eMMC Warn: int timeout, WaitEvt: %04Xh, NowEvt: %04Xh, IntEn: %04Xh\n",
			   u16_WaitEvent, REG_FCIE(FCIE_MIE_EVENT), REG_FCIE(FCIE_MIE_INT_EN));
#endif

		// switch to polling
		for (u32_i = 0; u32_i < u32_MicroSec; u32_i++) {
			if ((REG_FCIE(u32_RegAddr) & u16_WaitEvent) == u16_WaitEvent)
				break;

			eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
		}

		if (u32_i == u32_MicroSec) {
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1,
				   "eMMC Err: events lose, WaitEvent: %04Xh\n", u16_WaitEvent);
			eMMC_DumpDebugInfo();
			return eMMC_ST_ERR_INT_TO;
		} else {
			REG_FCIE_CLRBIT(FCIE_MIE_INT_EN, u16_WaitEvent);
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Warn: but polling ok: %04Xh\n",
				   REG_FCIE(u32_RegAddr));
		}

	}
	//----------------------------------------
	if (u16_WaitEvent & BIT_MIU_LAST_DONE) {
		for (u32_i = 0; u32_i < TIME_WAIT_1_BLK_END; u32_i++) {
			if (REG_FCIE(u32_RegAddr) & BIT_CARD_DMA_END)
				break;	// should be very fase
			eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
		}

		if (TIME_WAIT_1_BLK_END == u32_i) {
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: no CARD_DMA_END\n");
			eMMC_FCIE_ErrHandler_Stop();
		}
	}
	//----------------------------------------
	fcie_int = 0;
	return eMMC_ST_SUCCESS;

}

#endif

U32
eMMC_FCIE_WaitD0High(void)
{
	volatile U32 u32_cnt;
	U16 u16_read0, u16_read1;

#if defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK

	// enable busy int
	REG_FCIE_SETBIT(FCIE_SD_CTRL, BIT_SD_BUSY_DET_ON);
	REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_SD_BUSY_END);

	// wait event
	if (wait_event_timeout
	    (emmc_busy_wait, (emmc_busy_int == 1),
	     (long int) usecs_to_jiffies(HW_TIMER_DELAY_1s * 3)) == 0) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait busy int timeout\n");

		for (u32_cnt = 0; u32_cnt < TIME_WAIT_DAT0_HIGH; u32_cnt++) {
			REG_FCIE_R(FCIE_SD_STATUS, u16_read0);
			eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
			REG_FCIE_R(FCIE_SD_STATUS, u16_read1);

			if ((u16_read0 & BIT_SD_CARD_D0_ST) && (u16_read1 & BIT_SD_CARD_D0_ST))
				break;
		}

		if (TIME_WAIT_DAT0_HIGH == u32_cnt) {
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait D0 H timeout %u us\n",
				   u32_cnt);
			return eMMC_ST_ERR_TIMEOUT_WAITD0HIGH;
		}
	}

	emmc_busy_int = 0;

	REG_FCIE_W1C(FCIE_MIE_EVENT, BIT_SD_BUSY_END);

#else

	for (u32_cnt = 0; u32_cnt < TIME_WAIT_DAT0_HIGH; u32_cnt++) {
		REG_FCIE_R(FCIE_SD_STATUS, u16_read0);
		eMMC_hw_timer_delay(HW_TIMER_DELAY_1us);
		REG_FCIE_R(FCIE_SD_STATUS, u16_read1);

		if ((u16_read0 & BIT_SD_CARD_D0_ST) && (u16_read1 & BIT_SD_CARD_D0_ST))
			break;
	}

	if (TIME_WAIT_DAT0_HIGH == u32_cnt) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: wait D0 H timeout %u us\n",
			   u32_cnt);
		return eMMC_ST_ERR_TIMEOUT_WAITD0HIGH;
	}
#endif

	return eMMC_ST_SUCCESS;

}

void eMMC_GPIO_init(void)
{
#if defined(GPIO_DEBUG)&&GPIO_DEBUG

	volatile RIU_GPIO * const pRegGpio = (RIU_GPIO *)PAD_GPIO_BASE;

	pRegGpio->reg_eeprom_wp_oen = 0;
	pRegGpio->reg_eeprom_wp_out = 0;

	pRegGpio->reg_gpio_155_oen = 0;
	pRegGpio->reg_gpio_155_out = 0;

	pRegGpio->reg_gpio_156_oen = 0;
	pRegGpio->reg_gpio_156_out = 0;

#endif
}

void eMMC_GPIO_Debug(U32 u32GPIO, U8 On)
{
#if defined(GPIO_DEBUG)&&GPIO_DEBUG

	volatile RIU_GPIO * const pRegGpio = (RIU_GPIO *)PAD_GPIO_BASE;

	switch(u32GPIO) {

		case 114:
			pRegGpio->reg_eeprom_wp_out = (On)?1:0;
			break;
		case 155:
			pRegGpio->reg_gpio_155_out = (On)?1:0;
			break;
		case 156:
			pRegGpio->reg_gpio_156_out = (On)?1:0;
			break;
		default:
			printk("wrong gpio index\n");
			break;
	}
#endif
}

#if defined(HARDWARE_TIMER_CHECK)&&HARDWARE_TIMER_CHECK

void eMMC_hw_timer_start(void)
{
    // Reset PIU Timer1
    REG_FCIE_W(TIMER1_MAX_LOW, 0xFFFF);
    REG_FCIE_W(TIMER1_MAX_HIGH, 0xFFFF);
    REG_FCIE_W(TIMER1_ENABLE, 0);

    // Start PIU Timer1
    REG_FCIE_W(TIMER1_ENABLE, 0x1);
}

// 12MHz --> 83.3333 ns for 1 tick

// 1 sec       --> 0x00B71B00
// 1 mili sec  -->     0x2EE0
// 1 micro sec -->        0xC

// 12MHz, unit = 0.0833 us, 357.9139 sec overflow

U32 eMMC_hw_timer_tick(void)
{
    U32 u32HWTimer = 0;
    U32 u32TimerLow = 0;
    U32 u32TimerHigh = 0;

    // Get timer value
    u32TimerLow = REG_FCIE(TIMER1_CAP_LOW);
    u32TimerHigh = REG_FCIE(TIMER1_CAP_HIGH);

    u32HWTimer = (u32TimerHigh<<16) | u32TimerLow;

    return u32HWTimer;
}

U32 eMMC_Tick2MicroSec(U32 u32Tick)
{
    return (u32Tick/12);
}

U32 eMMC_TimerGetUs(void)
{
    return eMMC_Tick2MicroSec(eMMC_hw_timer_tick());
}

void eMMC_hw_timer_stop(void)
{
    REG_FCIE_W(TIMER1_ENABLE, 0);
}

#endif

DEFINE_SEMAPHORE(fcie_mutex);

void
eMMC_LockFCIE(void)
{
	down(&fcie_mutex);
}

void
eMMC_UnlockFCIE(void)
{
	up(&fcie_mutex);
}

//---------------------------------------

U32
eMMC_PlatformResetPre(void)
{
	/**((volatile unsigned short *)(0x25007DCC))|=0x02;	// emi mask
	*((volatile unsigned short *)(0x25007C18))|=0x02;	// imi0 mask
	*((volatile unsigned short *)(0x25007C58))|=0x02;	// imi1 mask
	*/
	return eMMC_ST_SUCCESS;
}

U32
eMMC_PlatformResetPost(void)
{
	/**((volatile unsigned short *)(0x25007DCC))&=(~0x02);	// emi unmask
	*((volatile unsigned short *)(0x25007C18))&=(~0x02);	// imi0 unmask
	*((volatile unsigned short *)(0x25007C58))&=(~0x02);	// imi1 unmask
	*/
	return eMMC_ST_SUCCESS;
}

void eMMC_SetSkew4(U32 u32Skew4)
{
	volatile RIU_EMMCPLL * const pRegEmmcPll = (RIU_EMMCPLL *)EMMC_PLL_BASE;

	if(u32Skew4<9) {
		REG_FCIE_CLRBIT(FCIE_SM_STS, BIT11);
		pRegEmmcPll->reg_emmc_pll_clkph_skew4 = u32Skew4 & 15U;
	}
	else {
		REG_FCIE_SETBIT(FCIE_SM_STS, BIT11);
		pRegEmmcPll->reg_emmc_pll_clkph_skew4 = (u32Skew4-9) & 15U;
	}
}

U32 eMMC_PlatformInit(void)
{
	volatile RIU_EMMCPLL	* const pRegEmmcPll = (RIU_EMMCPLL *) EMMC_PLL_BASE;
	volatile RIU_MIU2	* const pRegMiu2    = (RIU_MIU2    *) MIU2_BASE;
	volatile RIU_CHIPTOP * const pRegChiptop = (RIU_CHIPTOP *)PAD_CHIPTOP_BASE;

	memset(&g_ADMAInfo, 0, sizeof(ADMA_INFO));
	g_ADMAInfo.DescriptorAddr = (U32)g_ADMAInfo.Descriptor;
	//dump_mem((U8 *)&g_ADMAInfo, 40); // debug use
	//printk("ADMA descriptor addr: %08Xh\n", g_ADMAInfo.DescriptorAddr); // debug use

	if(pRegMiu2->reg_miu_sel2 != 1) {
		eMMC_debug(0, 0, "eMMC Err: reg_miu_sel2 = %d\n", pRegMiu2->reg_miu_sel2);
		eMMC_die("\n");
	}

#if 1	// for eMMC 4.5 HS200 need 1.8V, unify all eMMC IO power to 1.8V
	// works both for eMMC 4.4 & 4.5
	// printf("1.8V IO power for eMMC\n");
	// Irwin Tyan: set this bit to boost IO performance at low power supply.

	if(pRegEmmcPll->reg_emmc_test != 1) {
		eMMC_debug(1, 0, "eMMC Err: not 1.8V IO setting\n");
		eMMC_die("\n");
		//pRegEmmcPll->reg_emmc_test = 1; // 1.8V must set this bit
	}
#else
	printf("3.3V IO power for eMMC\n");
	pRegEmmcPll->reg_emmc_test = 0; // 3.3V must clear this bit
#endif
	eMMC_pads_switch(FCIE_eMMC_SDR);
	eMMC_clock_setting(FCIE_SLOWEST_CLK);
	REG_FCIE_SETBIT(FCIE_REG_2Dh, BIT3);

	pRegChiptop->reg_emmc_pad_driving = 0xF; // emmc pad driving

	//eMMC_SkewSelectTester();
	//printf("gau8_eMMC_SectorBuf --> %08Xh\n", (U32)gau8_eMMC_SectorBuf);
	//printf("gau8_eMMC_PartInfoBuf --> %08Xh\n", (U32)gau8_eMMC_PartInfoBuf);

	eMMC_GPIO_init();

#if defined(HARDWARE_TIMER_CHECK)&&HARDWARE_TIMER_CHECK
	eMMC_hw_timer_start();
#endif

	return eMMC_ST_SUCCESS;
}

U32 eMMC_PlatformDeinit(void)
{
	return eMMC_ST_SUCCESS;
}

#if 0
U32
eMMC_BootPartitionHandler_WR(U8 * pDataBuf, U16 u16_PartType, U32 u32_StartSector,
			     U32 u32_SectorCnt, U8 u8_OP)
{
	switch (u16_PartType) {
	case eMMC_PART_BL:
		u32_StartSector += BL_BLK_OFFSET;
		break;

	case eMMC_PART_OTP:
		u32_StartSector += OTP_BLK_OFFSET;
		break;

	case eMMC_PART_SECINFO:
		u32_StartSector += SecInfo_BLK_OFFSET;
		break;

	default:
		return eMMC_ST_SUCCESS;
	}

	eMMC_debug(eMMC_DEBUG_LEVEL, 1, "SecAddr: %Xh, SecCnt: %Xh\n", u32_StartSector,
		   u32_SectorCnt);

	if (eMMC_BOOT_PART_W == u8_OP)
		return eMMC_WriteBootPart(pDataBuf,
					  u32_SectorCnt << eMMC_SECTOR_512BYTE_BITS,
					  u32_StartSector, 1);
	else
		return eMMC_ReadBootPart(pDataBuf,
					 u32_SectorCnt << eMMC_SECTOR_512BYTE_BITS,
					 u32_StartSector, 1);

}

U32
eMMC_BootPartitionHandler_E(U16 u16_PartType)
{
	U32 u32_eMMCBlkAddr_start, u32_eMMCBlkAddr_end;

	switch (u16_PartType) {
	case eMMC_PART_BL:
		u32_eMMCBlkAddr_start = 0;
		u32_eMMCBlkAddr_end = u32_eMMCBlkAddr_start + BL_BLK_CNT - 1;
		break;

	case eMMC_PART_OTP:
		u32_eMMCBlkAddr_start = OTP_BLK_OFFSET;
		u32_eMMCBlkAddr_end = u32_eMMCBlkAddr_start + OTP_BLK_CNT - 1;
		break;

	case eMMC_PART_SECINFO:
		u32_eMMCBlkAddr_start = SecInfo_BLK_OFFSET;
		u32_eMMCBlkAddr_end = u32_eMMCBlkAddr_start + SecInfo_BLK_CNT - 1;
		break;

	default:
		return eMMC_ST_SUCCESS;
	}

	eMMC_debug(eMMC_DEBUG_LEVEL, 1, "BlkAddr_start: %Xh, BlkAddr_end: %Xh\n",
		   u32_eMMCBlkAddr_start, u32_eMMCBlkAddr_end);

	return eMMC_EraseBootPart(u32_eMMCBlkAddr_start, u32_eMMCBlkAddr_end, 1);

}

char *gpas8_eMMCPartName[] = {
	"e2pbak", "nvrambak", "hwcfgs", "recovery", "os",
	"fdd", "tdd", "blogo", "apanic", "misc", "cus",
	"e2p0", "e2p1", "nvram0", "nvram1", "system", "cache", "data", "internal sd"
};
#endif

// --------------------------------------------
static U32 sgu32_MemGuard0 = 0xA55A;
eMMC_ALIGN0 eMMC_DRIVER g_eMMCDrv eMMC_ALIGN1;
eMMC_ALIGN0 ADMA_INFO g_ADMAInfo eMMC_ALIGN1; // = {.DescriptorAddr = .Descriptor};
static U32 sgu32_MemGuard1 = 0x1289;

eMMC_ALIGN0 U8 gau8_eMMC_SectorBuf[eMMC_SECTOR_BUF_16KB] eMMC_ALIGN1;	// 512 bytes
eMMC_ALIGN0 U8 gau8_eMMC_PartInfoBuf[eMMC_SECTOR_512BYTE] eMMC_ALIGN1;	// 512 bytes

U32
eMMC_CheckIfMemCorrupt(void)
{
	if (0xA55A != sgu32_MemGuard0 || 0x1289 != sgu32_MemGuard1)
		return eMMC_ST_ERR_MEM_CORRUPT;

	return eMMC_ST_SUCCESS;
}

void eMMC_DumpDebugInfo(void)
{
	printk("\n[%s]\n", __FUNCTION__);

	smp_send_stop();

	eMMC_DumpPadClk();
	eMMC_FCIE_DumpRegisters();
	eMMC_FCIE_DumpDebugBus();
	eMMC_FCIE_ClockTest();

	dump_mem((U8 *)&g_ADMAInfo, 8*32); // debug use, total 8 * 512
}

#else

#error "Error! no platform functions."

#endif

#endif
