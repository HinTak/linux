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

#include "mstar_mci.h"
#include <linux/string.h>
#include "chip_setup.h"
#include <linux/mmc/mmc.h>

/******************************************************************************
 * Defines
 ******************************************************************************/
#define DRIVER_NAME					"mstar_mci"


/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
static void mstar_mci_setup_descriptor_addr(void);
static  U32 mstar_mci_prepare_descriptors(struct mmc_data * pData);
#else
static  U32 mstar_mci_setup_device_addr_n_job_count(struct mmc_data * pData);
#endif
static void mstar_mci_enable(struct mstar_mci_host *pMStarHost_st);
static  U32 mstar_mci_check_rsp_tran_sts(struct mmc_command * cmd);
static void mstar_mci_copy_rsp_back(struct mmc_command * cmd);
static  int mstar_mci_pre_dma_transfer(struct mstar_mci_host *host, struct mmc_data *data, struct mstar_mci_host_next *next);
static void mstar_mci_send_cmd(struct mmc_command * pCmd);
static void mstar_mci_send_cmd_data_tran(struct mmc_command * pCmdDT);
static void mstar_mci_cmd(void);
static void mstar_mci_dtc(void);

static void mstar_mci_dtc_bh(struct work_struct *work);
void mstar_mci_dump_debug_msg(void);
void mstar_mci_hardware_reset(void);
void mstar_mci_normal_resume(void);
void mstar_mci_fast_resume(void);
void mstar_mci_preinit_emmc(void);


//static void mstar_mci_send_command(struct mstar_mci_host *pMStarHost_st, struct mmc_command *cmd, U8 *pu8_str);
//static void mstar_mci_completed_command(struct mmc_command * cmd, u8 u8CpyRsp);
//static void mstar_mci_wait_r1b(struct mmc_command * cmd);
//static void mstar_mci_schedule_tasklet_test(void);

/*****************************************************************************
 * Define Static Global Variables
 ******************************************************************************/
#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
static u32 total_read_dma_len = 0;
static u32 total_read_dma_time = 0;
static u32 total_write_dma_len = 0;
static u32 total_write_dma_time = 0;
#endif
static struct mstar_mci_host *curhost;


/******************************************************************************
 * Functions
 ******************************************************************************/

/*static unsigned long mstar_mci_get_ms(void)
{
    struct timeval tv;
    unsigned long ms;

    do_gettimeofday(&tv);
    ms = tv.tv_usec/1000;
    ms += tv.tv_sec * 1000;

    return ms;
}*/

#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE

// if there is only 1 descriptor, only need to setup once

static void mstar_mci_setup_descriptor_addr(void)
{
	U32	u32DescptAddr;

	// give descriptor array addr to FCIE
	//u32DescptMiuAddr = eMMC_translate_DMA_address_Ex(g_ADMAInfo.DescriptorAddr, sizeof(ADMA_INFO));
	u32DescptAddr = virt_to_phys((void *)g_ADMAInfo.DescriptorAddr); // virtual to bus address

	//printk("Descpt Addr: %08Xh --> %08Xh", g_ADMAInfo.DescriptorAddr, u32DescptMiuAddr);

	// bus address to device address
	if( u32DescptAddr >= MSTAR_MIU1_BUS_BASE) {
		u32DescptAddr -= MSTAR_MIU1_BUS_BASE;
		REG_FCIE_SETBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
		//printk(" - %08Xh(MIU1) = %08Xh\n", MSTAR_MIU1_BUS_BASE , u32DescptMiuAddr);
	} else {
		u32DescptAddr -= MSTAR_MIU0_BUS_BASE;
		REG_FCIE_CLRBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
		//printk(" - %08Xh(MIU0) = %08Xh\n", MSTAR_MIU0_BUS_BASE , u32DescptMiuAddr);
	}

	REG_FCIE_W(FCIE_SDIO_ADDR0, u32DescptAddr & 0xFFFF);
	REG_FCIE_W(FCIE_SDIO_ADDR1, u32DescptAddr >> 16);

}

#define DEBUG_SG_ELEMENT 0

static U32 mstar_mci_prepare_descriptors(struct mmc_data * pData)
{
	U32 i, u32DescptAddr, u32BusAddr, u32DeviceAddr, u32DmaLeng, u32TotalLength = 0;
	struct scatterlist  *pScatterList = 0;

	u32DescptAddr = virt_to_phys((void *)g_ADMAInfo.DescriptorAddr); // virtual to bus address

	// setup descriptor
	memset(&g_ADMAInfo, 0, sizeof(ADMA_INFO)-4); // clear

	#if defined(DEBUG_SG_ELEMENT) && DEBUG_SG_ELEMENT
	printk("SG[%d] = ", pData->sg_len);
	#endif

	for(i=0; i<pData->sg_len; i++) {

		pScatterList = &(pData->sg[i]);
		u32BusAddr = sg_dma_address(pScatterList);
		u32DmaLeng = sg_dma_len(pScatterList);

		BUG_ON((u32DmaLeng>>9)&0xFFFFF000);
		u32TotalLength += u32DmaLeng;
		#if defined(DEBUG_SG_ELEMENT) && DEBUG_SG_ELEMENT
		printk("%d + ", u32DmaLeng>>9);
		#endif
		//printk("SG[%d] %d sector, BA = %08Xh", i, u32DmaLeng>>9, u32BusAddr);
		if( u32BusAddr >= MSTAR_MIU1_BUS_BASE) {
			u32DeviceAddr = u32BusAddr - MSTAR_MIU1_BUS_BASE;
			g_ADMAInfo.Descriptor[i].adma_miu_sel = 1;
			//printk(" - %08Xh(MIU1) = %08Xh\n", MSTAR_MIU1_BUS_BASE , u32DeviceAddr);
		} else {
			u32DeviceAddr = u32BusAddr - MSTAR_MIU0_BUS_BASE;
			//printk(" - %08Xh(MIU0) = %08Xh\n", MSTAR_MIU0_BUS_BASE , u32DeviceAddr);
		}

		g_ADMAInfo.Descriptor[i].adma_miu_addr = u32DeviceAddr;
		g_ADMAInfo.Descriptor[i].adma_job_cnt = (u32DmaLeng>>9)&0xFFF;

	}

	g_ADMAInfo.Descriptor[pData->sg_len-1].adma_end_flag = 1; // must give a end mark

	#if defined(DEBUG_SG_ELEMENT) && DEBUG_SG_ELEMENT
	printk("= %d\n", u32TotalLength>>9);
	#endif

	//MsOS_Dcache_Flush(g_ADMAInfo.Descriptor, sizeof(ADMA_INFO));
	Chip_Clean_Cache_Range_VA_PA(g_ADMAInfo.DescriptorAddr, u32DescptAddr, sizeof(ADMA_INFO));

	//dump_mem((U8 *)&g_ADMAInfo, 32); // debug use

	return u32TotalLength;
}

#else

static U32 mstar_mci_setup_device_addr_n_job_count(struct mmc_data * pData)
{
	U32 u32BusAddr, u32DeviceAddr, u32DmaLeng = 0;
	struct scatterlist  *pScatterList = 0;

	BUG_ON(pData->sg_len!=1);

	pScatterList = &(pData->sg[0]);
	u32BusAddr = sg_dma_address(pScatterList);
	u32DmaLeng = sg_dma_len(pScatterList);

	BUG_ON((u32DmaLeng>>9)&0xFFFFF000);

	//printk("SG[%d] %d sector, BA = %08Xh", u32DmaLeng>>9, u32BusAddr);
	if( u32BusAddr >= MSTAR_MIU1_BUS_BASE) {
		u32DeviceAddr = u32BusAddr - MSTAR_MIU1_BUS_BASE;
		REG_FCIE_SETBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
		//printk(" - %08Xh(MIU1) = %08Xh\n", MSTAR_MIU1_BUS_BASE , u32DeviceAddr);
	} else {
		u32DeviceAddr = u32BusAddr - MSTAR_MIU0_BUS_BASE;
		REG_FCIE_CLRBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
		//printk(" - %08Xh(MIU0) = %08Xh\n", MSTAR_MIU0_BUS_BASE , u32DeviceAddr);
	}

	REG_FCIE_W(FCIE_JOB_BL_CNT, u32DmaLeng>>9); // job count(s)

	REG_FCIE_W(FCIE_SDIO_ADDR0, u32DeviceAddr & 0xFFFF);
	REG_FCIE_W(FCIE_SDIO_ADDR1, u32DeviceAddr >> 16);

	return u32DmaLeng;
}

#endif

static int mstar_mci_get_dma_dir(struct mmc_data *data)
{
	if (data->flags & MMC_DATA_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_FROM_DEVICE;
}

static void mstar_mci_pre_dma_read(struct mstar_mci_host *pMStarHost_st)
{
	struct mmc_command  *pCmd = 0;
	struct mmc_data     *pData = 0;

	pCmd = pMStarHost_st->req_dtc->cmd;
	pData = pCmd->data;

	mstar_mci_pre_dma_transfer(pMStarHost_st, pData, NULL);

	#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
	eMMC_hw_timer_start();
	#endif


	/*if( pCmd->opcode != 8 ) { // recoard arg(sector addr) except CMD8
		g_eMMCDrv.u32LastBlockAddrInArg = pCmd->arg;
		g_eMMCDrv.u16BlockLen[0] = u32_dmalen;
		for(u8_i=1; u8_i<16; u8_i++) g_eMMCDrv.u16BlockLen[u8_i] = 0; // clear
		g_eMMCDrv.u16SGNum = pData->sg_len;
		g_eMMCDrv.u32BlockAddrMightKeepCRC = pCmd->arg + u32_dmalen - 1;
	}*/

	#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
	if( g_eMMCDrv.u32_DrvFlag & DRV_FLAG_DDR_MODE )
	{
		REG_FCIE_W(FCIE_TOGGLE_CNT, BITS_8_R_TOGGLE_CNT);
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_MACRO_DIR);
		REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_TOGGLE_CNT_RST);
		eMMC_hw_timer_delay(TIME_WAIT_FCIE_RST_TOGGLE_CNT); // Brian needs 2T
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_TOGGLE_CNT_RST);
	}
	#endif

	if(g_eMMCDrv.u32_DrvFlag & (DRV_FLAG_DDR_MODE|DRV_FLAG_SPEED_HS200) )
	{
		REG_FCIE_W(FCIE_TOGGLE_CNT, (g_eMMCDrv.u32_DrvFlag&DRV_FLAG_SPEED_HS200) ? TOGGLE_CNT_512_CLK_R : TOGGLE_CNT_256_CLK_R);
		REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_TOGGLE_CNT_RST);
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_MACRO_DIR);
		eMMC_hw_timer_delay(TIME_WAIT_FCIE_RST_TOGGLE_CNT); // Brian needs 2T
		REG_FCIE_CLRBIT(FCIE_MACRO_REDNT, BIT_TOGGLE_CNT_RST);
	}

	#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE

	pData->bytes_xfered = mstar_mci_prepare_descriptors(pData);
	mstar_mci_setup_descriptor_addr();

	#else
	pData->bytes_xfered = mstar_mci_setup_device_addr_n_job_count(pData);
	#endif

	REG_FCIE_CLRBIT(FCIE_MMA_PRI_REG, BIT_DMA_DIR_W);
	eMMC_FCIE_FifoClkRdy(0);

	#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
	REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_ADMA_EN);
	#else
	REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_MMA_EN);
	#endif
}

static void mstar_mci_post_dma_read(struct mstar_mci_host *pMStarHost_st)
{
	//struct mmc_command	*pCmd = 0;
	//struct mmc_data		*pData = 0;

	#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
	u32                 u32Ticks = 0;
	#endif

	//eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "\n");

	//pCmd = pMStarHost_st->req_dtc->cmd;
	//pData = pCmd->data;

#if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
	#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
		REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_ADMA_END);
	#else
		REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_MIU_LAST_DONE);
	#endif
#endif

	#if defined (ASYNCIO_SUPPORT) && ASYNCIO_SUPPORT

		schedule_work(&pMStarHost_st->workqueue_dtc); // mstar_mci_dtc_bh()

	#else
	{
		struct work_struct workq;
		mstar_mci_dtc_bh(&workq);
	}
	#endif
}

#ifdef CONFIG_WRITE_PROTECT
unsigned int check_protection_area(unsigned int address);
#endif

static void mstar_mci_dma_write(struct mstar_mci_host *pMStarHost_st)
{
	struct mmc_command	*pCmd = 0;
	struct mmc_data		*pData = 0;

	#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
	u32                 u32Ticks = 0;
	#endif

#ifdef CONFIG_WRITE_PROTECT
	unsigned int part_type = 0;
#endif
	//eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "\n");

	if( pMStarHost_st->req_dtc->cmd->opcode==25 ) pMStarHost_st->req_progress = REQ_PGS_DMA;

	#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
	if( g_eMMCDrv.u32_DrvFlag & DRV_FLAG_DDR_MODE )
	{
		REG_FCIE_W(FCIE_TOGGLE_CNT, BITS_8_W_TOGGLE_CNT);
		REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_MACRO_DIR);
	}
	#endif

	if(g_eMMCDrv.u32_DrvFlag & (DRV_FLAG_DDR_MODE|DRV_FLAG_SPEED_HS200) ) {

		REG_FCIE_W(FCIE_TOGGLE_CNT, (g_eMMCDrv.u32_DrvFlag&DRV_FLAG_SPEED_HS200) ? TOGGLE_CNT_512_CLK_W : TOGGLE_CNT_256_CLK_W);
		REG_FCIE_SETBIT(FCIE_MACRO_REDNT, BIT_MACRO_DIR);
	}

	pCmd = pMStarHost_st->req_dtc->cmd;
	pData = pCmd->data;

#ifdef CONFIG_WRITE_PROTECT
	part_type = check_protection_area(pCmd->arg);
#endif
	mstar_mci_pre_dma_transfer(pMStarHost_st, pData, NULL);

	#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
		eMMC_hw_timer_start();
	#endif

	#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
		pData->bytes_xfered = mstar_mci_prepare_descriptors(pData);
		mstar_mci_setup_descriptor_addr();
	#else
		pData->bytes_xfered = mstar_mci_setup_device_addr_n_job_count(pData);
	#endif

        REG_FCIE_SETBIT(FCIE_MMA_PRI_REG, BIT_DMA_DIR_W);
        eMMC_FCIE_FifoClkRdy(BIT_DMA_DIR_W);

        //eMMC_FCIE_ClearMieEvent();

        #if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
		#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
	        	REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_ADMA_END);
		#else
		        REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_CARD_DMA_END);
		#endif
        #endif

        //if(i>0) eMMC_FCIE_WaitD0High(); // wait before burst mode

        //eMMC_GPIO60_Debug(1);

	#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE

		REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_ADMA_EN); // start ADMA
	#else
        	REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_MMA_EN);
	        REG_FCIE_W(FCIE_SD_CTRL, BIT_SD_DAT_EN|BIT_SD_DAT_DIR_W); // start write DMA
	#endif

        #if defined (ASYNCIO_SUPPORT) && ASYNCIO_SUPPORT

		schedule_work(&pMStarHost_st->workqueue_dtc); // mstar_mci_dtc_bh()

	#else
        {
		struct work_struct workq;
		mstar_mci_dtc_bh(&workq);
        }
        #endif

}

static U32 mstar_mci_check_rsp_tran_sts(struct mmc_command * cmd)
{
	unsigned int u32Status;

	u32Status = REG_FCIE(FCIE_SD_STATUS) & BIT_SD_FCIE_ERR_FLAGS; // 0x1F
	if(u32Status)
	{
		if((u32Status & BIT_SD_RSP_CRC_ERR) && !(mmc_resp_type(cmd) & MMC_RSP_CRC))
		{
			cmd->error = 0;
		}
		else
		{
			eMMC_debug(1, 1, "eMMC: STS: %04Xh, CMD:%u, retry:%u\n", u32Status, cmd->opcode, cmd->retries);
			//if(u32Status&BIT_SD_RSP_TIMEOUT) printk("no response\n");
			if(u32Status&BIT_SD_R_CRC_ERR  ) eMMC_debug(1, 1, "read data CRC error\n");
			if(u32Status&BIT_SD_W_FAIL     ) eMMC_debug(1, 1, "no CRC status latched\n");
			if(u32Status&BIT_SD_W_CRC_ERR  ) eMMC_debug(1, 1, "negtive CRC status latched\n");
			if(u32Status&BIT_SD_RSP_CRC_ERR) eMMC_debug(1, 1, "rsp CRC error\n");

			printk("CMD%02d_%08Xh\n", cmd->opcode, cmd->arg);
			printk("Backup CMD%02d_%08Xh\n", curhost->cmd_backup, curhost->arg_backup);

			//printk("gu32BusAddr = %Xh\n", gu32BusAddr);
			//dump_mem((U8 *)phys_to_virt(gu32BusAddr), 0x400);

			if(u32Status & BIT_SD_RSP_TIMEOUT)
			{
				eMMC_debug(1, 0, "no response %02Xh %02Xh %02Xh %02Xh %02Xh\n",
				eMMC_FCIE_CmdRspBufGet(0), eMMC_FCIE_CmdRspBufGet(1),
				eMMC_FCIE_CmdRspBufGet(2), eMMC_FCIE_CmdRspBufGet(3),
				eMMC_FCIE_CmdRspBufGet(4));
				cmd->error = -ETIMEDOUT; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
				eMMC_FCIE_ErrHandler_Stop();
			}
			else if(u32Status & (BIT_SD_RSP_CRC_ERR | BIT_SD_R_CRC_ERR | BIT_SD_W_CRC_ERR | BIT_SD_W_FAIL))
			{
				cmd->error = -EILSEQ; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
				eMMC_DumpDebugInfo();
				if(cmd->data) {
					eMMC_printf("CMD%02d_%08Xh_%d\n", cmd->opcode, cmd->arg, cmd->data->blocks); // red color
					eMMC_DebugDumpData(cmd->data);
				}
				eMMC_die("\n");
			}
			else
			{
				cmd->error = -EIO; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
			}
		}
	}
	else
	{
		cmd->error = 0;
	}

	eMMC_FCIE_ClearTranErr();

	return u32Status;
}

static void mstar_mci_copy_rsp_back(struct mmc_command * cmd)
{
	U8 i, u8RspByteNum;
	U8 * pTemp;

	pTemp = (U8*)&(cmd->resp[0]);

	#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
	printk("\tRSP: %02X ", eMMC_FCIE_CmdRspBufGet(0));
	#endif
	if(cmd->flags&MMC_RSP_136) {
		u8RspByteNum = 16;
	} else {
		u8RspByteNum = 4;
	}
	for(i=0; i<u8RspByteNum; i++) {
		pTemp[ (3-(i%4)) + ((i/4)<<2) ] = eMMC_FCIE_CmdRspBufGet((U8)(i+1));

		if(cmd->opcode == 2) {
			g_eMMCDrv.au8_CID[i] = eMMC_FCIE_CmdRspBufGet((U8)(i+1));
			printk("%02X ", eMMC_FCIE_CmdRspBufGet((U8)(i+1)));
		}

		#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
		printk("%02X ", eMMC_FCIE_CmdRspBufGet((U8)(i+1)));
		#endif
	}
	#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
	if( (mmc_resp_type(cmd)==MMC_RSP_R1) || (mmc_resp_type(cmd)==MMC_RSP_R1B) ) { // with card state
		printk("(%d)\n", (cmd->resp[0]&0x1E00)>>9);
	} else {
		printk("\n");
	}
	#endif

	#if 0
	for(i = 0; i < 15; i++)
	{
		pTemp[(3 - (i % 4)) + (4 * (i / 4))] =
			(U8)(REG_FCIE(FCIE1_BASE+(((i+1)/2)*4)) >> (8*((i+1)%2)));
	}
	#endif

	//eMMC_printf("RSP 4 CMD%02d: %08Xh(%d)\n", cmd->opcode, cmd->resp[0], (cmd->resp[0]&0x1E00)>>9);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,20)
static int mstar_mci_pre_dma_transfer(struct mstar_mci_host *pMStarHost_st, struct mmc_data *data, struct mstar_mci_host_next *next)
{
	U32 dma_len;

	/*if (!next && data->host_cookie &&
	data->host_cookie != pMStarHost_st->next_data.mstar_cookie) {
		pr_warning("[%s] invalid cookie: data->host_cookie %d"
			" pMStarHost_st->next_data.mstar_cookie %d\n",
			__func__, data->host_cookie, pMStarHost_st->next_data.mstar_cookie);
		data->host_cookie = 0;
	}*/

	/*if(!next && data->host_cookie != host->next_data.mstar_cookie)
	{
		printk("no cookie map, cmd->opcode = %d\n", host->request->cmd->opcode);
		printk("host_cookie = %d, mstar_cookie = %d\n", data->host_cookie, host->next_data.mstar_cookie);
	}*/

	if (next || (!next && data->host_cookie != pMStarHost_st->next_data.mstar_cookie)) {

		dma_len = (U32)dma_map_sg(mmc_dev(pMStarHost_st->mmc), data->sg, (int)data->sg_len, mstar_mci_get_dma_dir(data));

	} else {

		dma_len = pMStarHost_st->next_data.dma_len;
		pMStarHost_st->next_data.dma_len = 0;
	}

	if (dma_len == 0)
		return -EINVAL;

	if (next) {
		next->dma_len = dma_len;
		data->host_cookie = ++next->mstar_cookie < 0 ? 1 : next->mstar_cookie;
	}
	// else
	//	pMStarHost_st->dma_len = dma_len;

	return 0;
}

#if defined (ASYNCIO_SUPPORT) && ASYNCIO_SUPPORT

static void mstar_mci_pre_req(struct mmc_host *pMMCHost_st, struct mmc_request *mrq, bool is_first_req)
{
	struct mstar_mci_host *pMStarHost_st = mmc_priv(pMMCHost_st);

	//eMMC_debug(eMMC_DEBUG_LEVEL_LINUX, 1, "\n");

	//mstar_mci_schedule_tasklet_test();

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(mrq->cmd->opcode==18) {
		if (pMStarHost_st->profile_fcie.idx_pre_req == 0) {
			eMMC_hw_timer_stop(); eMMC_hw_timer_start();
		}
		if(pMStarHost_st->profile_fcie.idx_pre_req < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_pre_req].pre_request.t_begin = eMMC_TimerGetUs();
		}
	}
	#endif

	if( mrq->data->host_cookie ) {
		mrq->data->host_cookie = 0;
		return;
	}

	//eMMC_GPIO_Debug(114, 1);

	if (mstar_mci_pre_dma_transfer(pMStarHost_st, mrq->data, &pMStarHost_st->next_data))
		mrq->data->host_cookie = 0;

	//eMMC_GPIO_Debug(114, 0);

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(mrq->cmd->opcode==18) {
		if(pMStarHost_st->profile_fcie.idx_pre_req < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_pre_req++].pre_request.t_end = eMMC_TimerGetUs();
		}
	}
	#endif

}

static void mstar_mci_post_req(struct mmc_host *pMMCHost_st, struct mmc_request *mrq, int err)
{
	struct mstar_mci_host *pMStarHost_st = mmc_priv(pMMCHost_st);
	struct mmc_data *pData = mrq->data;

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(mrq->cmd->opcode==18) {
		if(pMStarHost_st->profile_fcie.idx_post_req < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_post_req].post_request.t_begin = eMMC_TimerGetUs();
		}
	}
	#endif

	//eMMC_debug(eMMC_DEBUG_LEVEL_LINUX, 1, "\n");

	//mstar_mci_schedule_tasklet_test();
	//eMMC_GPIO_Debug(155, 1);

	if (pData->host_cookie) {
		dma_unmap_sg(mmc_dev(pMStarHost_st->mmc), pData->sg, (int)pData->sg_len, mstar_mci_get_dma_dir(pData));
	}

	//eMMC_GPIO_Debug(155, 0);

	pData->host_cookie = 0;

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(mrq->cmd->opcode==18) {
		if(pMStarHost_st->profile_fcie.idx_post_req < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_post_req++].post_request.t_end = eMMC_TimerGetUs();
		}
	}
	#endif
}

#endif // ASYNCIO_SUPPORT

#if defined (HPI_SUPPORT) && HPI_SUPPORT

#if defined (CHECK_ABORT_TIMING) && CHECK_ABORT_TIMING

static void log_abort_time_minus_requet_time(void)
{
	curhost->TimeUsAbort = eMMC_TimerGetUs();
	curhost->TimeUsLag = curhost->TimeUsAbort - curhost->TimeUsRequest;
}

void show_lag_time(void)
{
	printk("recieve abort after request %d us\n", curhost->TimeUsLag);
}

#endif // CHECK_ABORT_TIMING

static int mstar_mci_abort_req(struct mmc_host *pMMCHost_st, struct mmc_request *mrq)
{
	unsigned long flags;
	enum mstar_request_progress eProgress;
	int abort_result = 0;
	struct mstar_mci_host *pMStarHost_st = mmc_priv(pMMCHost_st);

	//eMMC_GPIO60_Debug(1);

	#if defined (CHECK_ABORT_TIMING) && CHECK_ABORT_TIMING
		log_abort_time_minus_requet_time();
	#endif

	pMStarHost_st->abort_success = 0;

	if( (!mrq) || (mrq != pMStarHost_st->req_dtc) || (mrq->cmd->opcode!=25) || (mrq->cmd->opcode!=pMStarHost_st->req_dtc->cmd->opcode) || (mrq->cmd->arg!=pMStarHost_st->req_dtc->cmd->arg) ) {
		eMMC_debug(eMMC_DEBUG_LEVEL_WARNING, 1, "abort request not match current request\n");
		abort_result = -EBADR;
		goto abort_finish;
	}

	spin_lock_irqsave(&pMStarHost_st->irq_lock, flags);
	eProgress = pMStarHost_st->req_progress;
	if ( eProgress <= REQ_PGS_DMA ) { // before or doing DMA
		eMMC_AbortIntrWaitEvent();
		pMStarHost_st->abort_in_process = 1;
	}
	spin_unlock_irqrestore(&pMStarHost_st->irq_lock, flags);

	if ( eProgress <= REQ_PGS_DMA ) {
		down(&pMStarHost_st->sem_aborted_req_go_through); // wait for aborting req finish
		pMStarHost_st->abort_in_process = 0;
		eMMC_ContinueIntrWaitEvent();
	} /*else {
		eMMC_debug(eMMC_DEBUG_LEVEL_WARNING, 1, "abort really late\n");
	}*/

	if(!pMStarHost_st->abort_success) {
		eMMC_debug(eMMC_DEBUG_LEVEL_WARNING, 1, "abort too late\n"); // debug use, to be remove
		abort_result = -ETIME; // abort fail, abort too late
	}

abort_finish:

	mmc_request_done(pMStarHost_st->mmc, pMStarHost_st->req_dtc); // call after normal requrest job finish

	return abort_result;

}

#endif // HPI_SUPPORT

#endif // KERNEL_VERSION(3,0,20)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
static void mstar_mci_reset(struct mmc_host *pMMCHost_st)
{
    //struct mstar_mci_host *pMStarHost_st = mmc_priv(mmc);

    eMMC_debug(eMMC_DEBUG_LEVEL_LINUX, 1, "\n");

    // reset eMMC with hardware pin
}
#endif

#if defined(PERF_PROFILE)&&PERF_PROFILE

void mstar_mci_log_irq_time_stamp(void)
{

    if(curhost->req_dtc->cmd->opcode==18) {
        if(curhost->profile_fcie.idx_request < REC_REQUEST_NUM) {
            curhost->profile_fcie.req[curhost->profile_fcie.idx_request].transfer.t_irq_happen = eMMC_TimerGetUs();
        }
    }

}

EXPORT_SYMBOL(mstar_mci_log_irq_time_stamp);

#endif

static void mstar_mci_send_cmd(struct mmc_command * pCmd)
{
	u32 u32_sd_ctl = 0;
	U32 u32CIFC_Check;

	eMMC_FCIE_ClearMieEvent();

	u32_sd_ctl |= BIT_SD_CMD_EN;

	if((pCmd->opcode==12)&&(pCmd->arg&1))
	{
		//eMMC_GPIO60_Debug(0);
		//eMMC_printf("\33[1;33mHPI\33[m\r\n");
	}

	u32CIFC_Check = (((pCmd->arg >> 24)<<8) | (0x40|pCmd->opcode));
	REG_FCIE_W(FCIE_CIFC_ADDR(0), (((pCmd->arg >> 24)<<8) | (0x40|pCmd->opcode)));
	if(REG_FCIE(FCIE_CIFC_ADDR(0))!=u32CIFC_Check) {
		printk("CIFC check fail 1\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	u32CIFC_Check = ((pCmd->arg & 0xFF00) | ((pCmd->arg>>16)&0xFF));
	REG_FCIE_W(FCIE_CIFC_ADDR(1), ((pCmd->arg & 0xFF00) | ((pCmd->arg>>16)&0xFF)));
	if(REG_FCIE(FCIE_CIFC_ADDR(1))!=u32CIFC_Check) {
		printk("CIFC check fail 2\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	u32CIFC_Check = (pCmd->arg & 0xFF);
	REG_FCIE_W(FCIE_CIFC_ADDR(2), (pCmd->arg & 0xFF));
	if(REG_FCIE(FCIE_CIFC_ADDR(2))!=u32CIFC_Check) {
		printk("CIFC check fail 3\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	if(mmc_resp_type(pCmd) == MMC_RSP_NONE)
	{
		u32_sd_ctl &= ~BIT_SD_RSP_EN;
		REG_FCIE_W(FCIE_RSP_SIZE, 0);
	}
	else
	{
		u32_sd_ctl |= BIT_SD_RSP_EN;
		if(mmc_resp_type(pCmd) == MMC_RSP_R2)
		{
			u32_sd_ctl |= BIT_SD_RSPR2_EN;
			REG_FCIE_W(FCIE_RSP_SIZE, 16); /* (136-8)/8 */
		}
		else
		{
			REG_FCIE_W(FCIE_RSP_SIZE, 5); /*(48-8)/8 */
		}
	}

#if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
	REG_FCIE_W(FCIE_MIE_INT_EN, BIT_SD_CMD_END);
#endif

	REG_FCIE_W(FCIE_SD_MODE, curhost->sd_mod);
	REG_FCIE_W(FCIE_SD_CTRL, u32_sd_ctl); // start cmd action

	//  48 M --> 21   ns x (48+64+136) = 5208 ns, use 10 ms as time out value
	// 300 K -->  3.3 us x (48+64+136) =  826 us
	/*if (g_eMMCDrv.u32_ClkKHz==300) {
		u32TimeOut = HW_TIMER_DELAY_1s; //printk("HW_TIMER_DELAY_1s\n");
	} else {
		u32TimeOut = HW_TIMER_DELAY_50ms; //printk("HW_TIMER_DELAY_20ms\n");
	}*/

	//if( eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_SD_CMD_END, u32TimeOut) ) {
	if( eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_SD_CMD_END, HW_TIMER_DELAY_1s) ) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: cmd timeout\n");
		pCmd->error = -ETIMEDOUT; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
	}

	eMMC_FCIE_CLK_DIS();

	if(mstar_mci_check_rsp_tran_sts(pCmd)!=BIT_SD_RSP_TIMEOUT) {
		mstar_mci_copy_rsp_back(pCmd);
	}

	//eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "CMD%2d[%08Xh] RSP: %08Xh\n", pCmd->opcode, pCmd->arg, pCmd->resp[0]);

	//mstar_mci_wait_r1b(cmd); // Park: MMCSS check for CMD6/CMD38/CMD12

	/*eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 0, "RSP: %02X %02X %02X %02X %02X (%d)\n", \
                                                eMMC_FCIE_CmdRspBufGet(0),
                                                eMMC_FCIE_CmdRspBufGet(1),
                                                eMMC_FCIE_CmdRspBufGet(2),
                                                eMMC_FCIE_CmdRspBufGet(3),
                                                eMMC_FCIE_CmdRspBufGet(4),
                                                (eMMC_FCIE_CmdRspBufGet(3)&0x1E)>>1);*/

	if((pCmd->opcode==12)&&(pCmd->arg&1))
	{
		//eMMC_GPIO60_Debug(0);
	}
}

static void mstar_mci_send_cmd_data_tran(struct mmc_command * pCmdDT)
{
	struct mstar_mci_host *pMStarHost_st = curhost;
	U32 u32CIFC_Check;
	u32 u32_sd_ctl=0, u32_sd_mode;
	//U32 u32SetCount = 0;

	u32_sd_mode = pMStarHost_st->sd_mod;

	eMMC_FCIE_ClearMieEvent();

	if( pCmdDT && pCmdDT->data ) {

		pCmdDT->data->bytes_xfered = 0;

		if (pCmdDT->data->flags & MMC_DATA_READ)
		{
			#if (!defined ENABLE_eMMC_ADMA_MODE) || (!ENABLE_eMMC_ADMA_MODE)
			u32_sd_ctl |= BIT_SD_DAT_EN;
			#endif

			mstar_mci_pre_dma_read(pMStarHost_st);
		}
		else {
			u32_sd_ctl |= BIT_SD_DAT_DIR_W;
		}
	}

	u32_sd_ctl |= BIT_SD_CMD_EN;

	//eMMC_printf("CMD%02d [%08Xh]\n", pCmdDT->opcode, pCmdDT->arg);

	u32CIFC_Check = (((pCmdDT->arg >> 24)<<8) | (0x40|pCmdDT->opcode));
	REG_FCIE_W(FCIE_CIFC_ADDR(0), (((pCmdDT->arg >> 24)<<8) | (0x40|pCmdDT->opcode)));
	if(REG_FCIE(FCIE_CIFC_ADDR(0))!=u32CIFC_Check) {
		printk("DTC CIFC check fail 1\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	u32CIFC_Check = ((pCmdDT->arg & 0xFF00) | ((pCmdDT->arg>>16)&0xFF));
	REG_FCIE_W(FCIE_CIFC_ADDR(1), ((pCmdDT->arg & 0xFF00) | ((pCmdDT->arg>>16)&0xFF)));
	if(REG_FCIE(FCIE_CIFC_ADDR(1))!=u32CIFC_Check) {
		printk("DTC CIFC check fail 2\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	u32CIFC_Check = (pCmdDT->arg & 0xFF);
	REG_FCIE_W(FCIE_CIFC_ADDR(2), (pCmdDT->arg & 0xFF));
	if(REG_FCIE(FCIE_CIFC_ADDR(2))!=u32CIFC_Check) {
		printk("DTC CIFC check fail 3\n");
		eMMC_FCIE_ErrHandler_Stop();
	}

	if(mmc_resp_type(pCmdDT) == MMC_RSP_NONE)
	{
		u32_sd_ctl &= ~BIT_SD_RSP_EN;
		REG_FCIE_W(FCIE_RSP_SIZE, 0);
	}
	else
	{
		u32_sd_ctl |= BIT_SD_RSP_EN;
		if(mmc_resp_type(pCmdDT) == MMC_RSP_R2)
		{
			u32_sd_ctl |= BIT_SD_RSPR2_EN;
			REG_FCIE_W(FCIE_RSP_SIZE, 16); /* (136-8)/8 */
		}
		else
		{
			REG_FCIE_W(FCIE_RSP_SIZE, 5); /*(48-8)/8 */
		}
	}

	#if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
		#if (!defined ENABLE_eMMC_ADMA_MODE) || (!ENABLE_eMMC_ADMA_MODE)
        		REG_FCIE_W(FCIE_MIE_INT_EN, BIT_SD_CMD_END);
		#endif
	#endif

	REG_FCIE_W(FCIE_SD_MODE, u32_sd_mode);
	//printk("u32_sd_ctl = %04Xh\n", u32_sd_ctl); // R: 000Eh, W: 001Eh
	REG_FCIE_W(FCIE_SD_CTRL, u32_sd_ctl); // start cmd & dma

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(pCmdDT->opcode==18) {
		if(pMStarHost_st->profile_fcie.idx_request < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request].transfer.t_start_dma = eMMC_TimerGetUs();
		}
	}
	#endif

#if (!defined ENABLE_eMMC_ADMA_MODE) || (!ENABLE_eMMC_ADMA_MODE)
	if( eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_SD_CMD_END, eMMC_GENERIC_WAIT_TIME) ) {
        	eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: cmd timeout\n");
		pCmdDT->error = -ETIMEDOUT; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
	} /*else {
		printk("send cmd end\n");
	}*/
	if (mstar_mci_check_rsp_tran_sts(pCmdDT)!=BIT_SD_RSP_TIMEOUT) {
		mstar_mci_copy_rsp_back(pCmdDT);
	}
#endif

	//printk("CMD%2d[%08Xh] RSP: %08Xh\n", pCmdDT->opcode, pCmdDT->arg, pCmdDT->resp[0]);

	//mstar_mci_wait_r1b(cmd); // Park: MMCSS check for CMD6/CMD38/CMD12

	/*eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 0, "RSP: %02X %02X %02X %02X %02X (%d)\n", \
                                                eMMC_FCIE_CmdRspBufGet(0),
                                                eMMC_FCIE_CmdRspBufGet(1),
                                                eMMC_FCIE_CmdRspBufGet(2),
                                                eMMC_FCIE_CmdRspBufGet(3),
                                                eMMC_FCIE_CmdRspBufGet(4),
                                                (eMMC_FCIE_CmdRspBufGet(3)&0x1E)>>1);*/

	if( pCmdDT && pCmdDT->data )
	{
		if(pMStarHost_st->req_dtc->data->flags & MMC_DATA_WRITE)
		{
			mstar_mci_dma_write(pMStarHost_st);
		}
		else if(pMStarHost_st->req_dtc->data->flags & MMC_DATA_READ)
		{
			mstar_mci_post_dma_read(pMStarHost_st);
		}
	}

}

static void mstar_mci_cmd(void)
{
	struct mstar_mci_host *pMStarHost_st = curhost;

	mstar_mci_send_cmd(pMStarHost_st->req_cmd->cmd);

	mmc_request_done(pMStarHost_st->mmc, pMStarHost_st->req_cmd);
}

static void mstar_mci_dtc(void)
{
	struct mstar_mci_host *pMStarHost_st = curhost;

	#if defined(CONFIG_MMC_MSTAR_PREDEFINED)&&CONFIG_MMC_MSTAR_PREDEFINED
	if(pMStarHost_st->req_dtc->sbc) {
		mstar_mci_send_cmd(pMStarHost_st->req_dtc->sbc);
	}
	#endif

	#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
		//printk("CMD%d %d\n", mrq->cmd->opcode, mrq->data->blocks);
		eMMC_printf("\33[1;31mCMD%02d_%08Xh_%d\33[m", pMStarHost_st->req_dtc->cmd->opcode, pMStarHost_st->req_dtc->cmd->arg, pMStarHost_st->req_dtc->data->blocks); // red color
	#endif

	if( pMStarHost_st->req_dtc->cmd->opcode==25 ) pMStarHost_st->req_progress = REQ_PGS_SEND_CMD;

	mstar_mci_send_cmd_data_tran(pMStarHost_st->req_dtc->cmd);
}

static void mstar_mci_dtc_bh(struct work_struct *work)
{
	struct mstar_mci_host *pMStarHost_st = curhost;
	struct mmc_command	*pCmd = 0;
	struct mmc_data		*pData = 0;
	U32 u32WaitIntrRet = eMMC_ST_SUCCESS;

#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(pMStarHost_st->req_dtc->cmd->opcode==18) {
		if(pMStarHost_st->profile_fcie.idx_request < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request].transfer.t_bh_run = eMMC_TimerGetUs();
		}
	}
#endif

	pCmd = pMStarHost_st->req_dtc->cmd;
	pData = pCmd->data;

	if(pMStarHost_st->req_dtc->data->flags & MMC_DATA_WRITE) {

#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
		u32WaitIntrRet = eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_SD_CMD_END|BIT_ADMA_END, eMMC_ADMA_WAIT_TIME);
#else
		u32WaitIntrRet = eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_ALL_DMA_EVENT, eMMC_GENERIC_WAIT_TIME);
#endif
		if(u32WaitIntrRet == eMMC_ST_ERR_ABORT_REQ)
		{
			pMStarHost_st->abort_success = 1;
			//break;
		}
		else if(u32WaitIntrRet)
		{
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: w timeout %08Xh\n", u32WaitIntrRet);
			pData->bytes_xfered = 0;
			pData->error = -ETIMEDOUT; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
		}

#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
		if (mstar_mci_check_rsp_tran_sts(pCmd)!=BIT_SD_RSP_TIMEOUT) {
			mstar_mci_copy_rsp_back(pCmd);
		}
#endif
		mstar_mci_check_rsp_tran_sts(pCmd); // check data CRC status

#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
		u32Ticks = eMMC_hw_timer_tick();
		total_write_dma_len += pData->bytes_xfered;
		total_write_dma_time += u32Ticks;
#endif

		if(!pData->host_cookie) {
			dma_unmap_sg(mmc_dev(pMStarHost_st->mmc), pData->sg, (int)pData->sg_len, mstar_mci_get_dma_dir(pData));
		}

	}
	else // read direction
	{
		#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
			u32WaitIntrRet = eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_ADMA_END, eMMC_ADMA_WAIT_TIME);
		#else
			u32WaitIntrRet = eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_ALL_DMA_EVENT, eMMC_GENERIC_WAIT_TIME);
		#endif

		if(u32WaitIntrRet) {
			eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: r timeout %Xh\n", u32WaitIntrRet);
			pData->bytes_xfered = 0;
			pData->error = -ETIMEDOUT; // THE_SAME_WARNING_IN_OTHER_HOST_DRV
			//goto POST_DMA_READ_END;
		}

		/*for(i=1; i<pData->sg_len; i++)
		{
			sg = &(pData->sg[i]);
			dmaaddr = sg_dma_address(sg);
			u32_dmalen = sg_dma_len(sg);

			g_eMMCDrv.u32BlockAddrMightKeepCRC += (u32_dmalen/512);
			if(i<16) g_eMMCDrv.u16BlockLen[i] = u32_dmalen>>9;

			if( dmaaddr >= MSTAR_MIU1_BUS_BASE)
			{
				dmaaddr -= MSTAR_MIU1_BUS_BASE;
				REG_FCIE_SETBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
			}
			else
			{
				dmaaddr -= MSTAR_MIU0_BUS_BASE;
				REG_FCIE_CLRBIT(FCIE_MIU_DMA_26_16, BIT_MIU1_SELECT);
			}

			REG_FCIE_W(FCIE_JOB_BL_CNT,(u32_dmalen/512));
			REG_FCIE_W(FCIE_SDIO_ADDR0,(((u32)dmaaddr) & 0xFFFF));
			REG_FCIE_W(FCIE_SDIO_ADDR1,(((u32)dmaaddr) >> 16));
			eMMC_FCIE_FifoClkRdy(0);

			eMMC_FCIE_ClearMieEvent();

			#if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
			REG_FCIE_SETBIT(FCIE_MIE_INT_EN, BIT_MIU_LAST_DONE);
			#endif

			REG_FCIE_SETBIT(FCIE_PATH_CTRL, BIT_MMA_EN);
			REG_FCIE_W(FCIE_SD_CTRL, BIT_SD_DAT_EN);
			if(eMMC_FCIE_WaitEvents(FCIE_MIE_EVENT, BIT_ALL_DMA_EVENT, eMMC_GENERIC_WAIT_TIME))
			{
				eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1,"eMMC Err: r timeout \n");
				pData->error = -ETIMEDOUT;
				break;
			}

			pData->bytes_xfered += sg->length;
		}*/

		//POST_DMA_READ_END:

		#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
		if (mstar_mci_check_rsp_tran_sts(pCmd)!=BIT_SD_RSP_TIMEOUT) {
			mstar_mci_copy_rsp_back(pCmd);
		}
		#endif

		mstar_mci_check_rsp_tran_sts(pCmd); // check data CRC status

		#if defined(DMA_TIME_TEST) && DMA_TIME_TEST
		u32Ticks = eMMC_hw_timer_tick();
		total_read_dma_len += pData->bytes_xfered;
		total_read_dma_time += u32Ticks;
	        #endif

		if(!pData->host_cookie) {
			dma_unmap_sg(mmc_dev(pMStarHost_st->mmc), pData->sg, (int)pData->sg_len, mstar_mci_get_dma_dir(pData));
		}

	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	if( pMStarHost_st->req_dtc->cmd->opcode==25 ) pMStarHost_st->req_progress = REQ_PGS_SEND_STOP;

#if !defined(CONFIG_MMC_MSTAR_PREDEFINED)||(CONFIG_MMC_MSTAR_PREDEFINED==0)

		if(pMStarHost_st->req_dtc->stop)
		{
			#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
			eMMC_printf("CMD%02d_%08Xh  ", pMStarHost_st->req_dtc->stop->opcode, pMStarHost_st->req_dtc->stop->arg);
			#endif
			mstar_mci_send_cmd(pMStarHost_st->req_dtc->stop); // stop
		}
#endif
	eMMC_FCIE_CLK_DIS();

	if( pMStarHost_st->req_dtc->cmd->opcode==25 ) pMStarHost_st->req_progress = REQ_PGS_CHECK_ABORT;

	if (pMStarHost_st->abort_in_process) {
		up(&pMStarHost_st->sem_aborted_req_go_through); // aborting job finish, let abort_req complete
	}

	if( pMStarHost_st->req_dtc->cmd->opcode==25 ) pMStarHost_st->req_progress = REQ_PGS_FINISH;

	mmc_request_done(pMStarHost_st->mmc, pMStarHost_st->req_dtc);

	//eMMC_GPIO_Debug(156, 0);

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(pMStarHost_st->req_dtc->cmd->opcode==18) {
		//printk("%d, %d, %d\n", mrq->cmd->arg, mrq->data->blocks, eMMC_TimerGetUs());
		if(pMStarHost_st->profile_fcie.idx_request < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request++].request.t_end = eMMC_TimerGetUs();
		}
	}
	#endif

	eMMC_UnlockFCIE();

}

static void mstar_mci_request(struct mmc_host *pMMCHost_st, struct mmc_request *mrq)
{
	struct mstar_mci_host *pMStarHost_st;

	if (!pMMCHost_st)
	{
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: pMMCHost_st is NULL\n");
		return;
	}
	if (!mrq)
	{
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: mrq is NULL\n");
		return;
	}

	pMStarHost_st = mmc_priv(pMMCHost_st);

	#if defined(PERF_PROFILE)&&PERF_PROFILE
	if(mrq->cmd->opcode==18) {

		//eMMC_GPIO_Debug(156, 1);

		if(pMStarHost_st->profile_fcie.idx_request < REC_REQUEST_NUM) {
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request].request.t_begin = eMMC_TimerGetUs();
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request].sector.addr = mrq->cmd->arg;
			pMStarHost_st->profile_fcie.req[pMStarHost_st->profile_fcie.idx_request].sector.length = mrq->data->blocks;
		}
	}
	#endif

	#if 0
	switch(mmc_cmd_type(cmd)) {
		case MMC_CMD_AC:   printk("MMC_CMD_AC, ");   break;
		case MMC_CMD_ADTC: printk("MMC_CMD_ADTC, "); break;
		case MMC_CMD_BC:   printk("MMC_CMD_BC, ");   break;
		case MMC_CMD_BCR:  printk("MMC_CMD_BCR, ");  break;
		default: printk("cmd->flags = %08Xh, ", cmd->flags); break;
	}

	if(data) {
		switch(data->flags) {
			case MMC_DATA_WRITE:  printk("MMC_DATA_WRITE, ");  break;
			case MMC_DATA_READ:   printk("MMC_DATA_READ, ");   break;
			case MMC_DATA_STREAM: printk("MMC_DATA_STREAM, "); break;
			default: printk("data->flags = %08Xh, ", data->flags); break;
		}
	}

	switch(mmc_resp_type(cmd)) {
		case MMC_RSP_NONE: printk("MMC_RSP_NONE\n"); break;
		case MMC_RSP_R1:   printk("MMC_RSP_R1\n");   break;
		case MMC_RSP_R1B:  printk("MMC_RSP_R1B\n");  break;
		case MMC_RSP_R2:   printk("MMC_RSP_R2\n");   break;
		case MMC_RSP_R3:   printk("MMC_RSP_R3\n");   break;
		default: printk("cmd->flags = %08Xh, ", cmd->flags); break;
		//case MMC_RSP_R4:   printk("\n");  break;
		//case MMC_RSP_R5:   printk("MMC_RSP_R5\n");  break;
		//case MMC_RSP_R6:   printk("MMC_RSP_R6\n");  break;
		//case MMC_RSP_R7:   printk("MMC_RSP_R7\n");  break;
	}
	#endif

	curhost->cmd_backup = mrq->cmd->opcode;
	curhost->arg_backup = mrq->cmd->arg;

	if(!mrq->cmd->data) // cmd only request
	{
		#if defined(SPEED_UP_EMMC_RESUME)&&SPEED_UP_EMMC_RESUME
			if(pMStarHost_st->SpeedUpeMMCResume) {
				if(mrq->cmd->opcode==MMC_GO_IDLE_STATE) {
					//printk("Skip mmc sub system CMD0 MMC_GO_IDLE_STATE to speed up...\n");
					pMStarHost_st->SpeedUpeMMCResume = 0;
					mmc_request_done(pMStarHost_st->mmc, mrq);
				}
				return;
			}
		#endif

		eMMC_LockFCIE();

		#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
			//printk("CMD%d\n", mrq->cmd->opcode);
			//eMMC_printf("CMD%02d_%08Xh_%08Xh\n", cmd->opcode, cmd->arg, cmd->flags);
			//eMMC_printf("CMD%02d_%08Xh\n", cmd->opcode, cmd->arg);
			eMMC_printf("\33[1;31mCMD%02d_%08Xh\33[m  ", mrq->cmd->opcode, mrq->cmd->arg); // red color
		#endif

		pMStarHost_st->req_cmd = mrq;

		mstar_mci_cmd();

		eMMC_UnlockFCIE();
	}
	else // command with data transfer request
	{
		eMMC_LockFCIE();

		if( mrq->cmd->opcode==25 ) {
			pMStarHost_st->req_progress = REQ_PGS_RECIEVE_REQ;
			#if defined (CHECK_ABORT_TIMING) && CHECK_ABORT_TIMING
				curhost->TimeUsRequest = eMMC_TimerGetUs();
			#endif
		}

		#if defined(PRINT_REQUEST_INFO)&&PRINT_REQUEST_INFO
		if(mrq->sbc) {
			eMMC_printf("\33[1;36mCMD%02d_%08Xh\33[m\n", mrq->sbc->opcode, mrq->sbc->arg); // cyan color
		}
		#endif

		pMStarHost_st->req_dtc = mrq;

		mstar_mci_dtc();
	}

}

static void mstar_mci_set_ios(struct mmc_host *pMMCHost_st, struct mmc_ios *ios)
{
	/* Define Local Variables */
	struct mstar_mci_host *pMStarHost_st = mmc_priv(pMMCHost_st);

	//eMMC_debug(eMMC_DEBUG_LEVEL_LINUX, 1, "\n");

	if (!pMMCHost_st)
	{
		eMMC_debug(1, 1, "eMMC Err: mmc is NULL \n");
		return;
	}

	if (!ios)
	{
		eMMC_debug(1, 1, "eMMC Err: ios is NULL \n");
		return;
	}

	eMMC_debug(eMMC_DEBUG_LEVEL_MEDIUM, 0, "eMMC: clock: %u, bus_width %Xh \n", ios->clock, ios->bus_width);

	// ----------------------------------
	if (ios->clock == 0)
	{
		pMStarHost_st->sd_mod = 0;
		eMMC_debug(eMMC_DEBUG_LEVEL_MEDIUM, 0, "eMMC Warn: disable clk \n");
		eMMC_clock_gating();
	}
	else
	{
		//printk("\33[1;36mios->clock = %d\33[m\n", ios->clock);

		pMStarHost_st->sd_mod = BIT_SD_DEFAULT_MODE_REG;

		if(ios->clock <= CLK_400KHz)
		{
			eMMC_clock_setting(FCIE_SLOWEST_CLK);
		}
		else if(ios->clock <= CLK__52MHz)
		{
			eMMC_clock_setting(FCIE_DEFAULT_CLK);
		}
		else {
		}
	}

	// ----------------------------------
	if (ios->bus_width == MMC_BUS_WIDTH_8)
	{
		pMStarHost_st->sd_mod = (pMStarHost_st->sd_mod & ~BIT_SD_DATA_WIDTH_MASK) | BIT_SD_DATA_WIDTH_8;
	}
	else if (ios->bus_width == MMC_BUS_WIDTH_4)
	{
		pMStarHost_st->sd_mod = (pMStarHost_st->sd_mod & ~BIT_SD_DATA_WIDTH_MASK) | BIT_SD_DATA_WIDTH_4;
	}
	else
	{
		pMStarHost_st->sd_mod = (pMStarHost_st->sd_mod & ~BIT_SD_DATA_WIDTH_MASK);
	}

	//printk("\33[1;36mios.timing = %d\33[m\n", pMStarHost_st->mmc->ios.timing);

	#if defined(CONFIG_MSTAR_X14) && CONFIG_MSTAR_X14

	if( pMStarHost_st->mmc->ios.timing == MMC_TIMING_MMC_HS ) {

	}
	else if( pMStarHost_st->mmc->ios.timing == MMC_TIMING_UHS_DDR50 ) {

		if( (g_eMMCDrv.au8_CID[0]==0x15) &&	(g_eMMCDrv.au8_CID[3]=='4') &&
							(g_eMMCDrv.au8_CID[4]=='F') &&
							(g_eMMCDrv.au8_CID[5]=='E') &&
							(g_eMMCDrv.au8_CID[6]=='A') &&
							(g_eMMCDrv.au8_CID[7]=='C') &&
							(g_eMMCDrv.au8_CID[8]=='C') )
		{
			printk("KLM4G1FEAC\n");
			eMMC_SetSkew4(6); // predict value -> Samsung eMMC
		}
		else if( (g_eMMCDrv.au8_CID[0]==0xFE) &&(g_eMMCDrv.au8_CID[3]=='P') &&
							(g_eMMCDrv.au8_CID[4]=='1') &&
							(g_eMMCDrv.au8_CID[5]=='X') &&
							(g_eMMCDrv.au8_CID[6]=='X') &&
							(g_eMMCDrv.au8_CID[7]=='X') &&
							(g_eMMCDrv.au8_CID[8]=='X') )
		{
			printk("MTFC4GMCAM\n");
			eMMC_SetSkew4(6); // Micron eMMC
		}
		else
		{
			printk("new kind of eMMC\n");
			while(1);
			// to do .....
		}

		pMStarHost_st->sd_mod &= ~BIT_SD_DATA_SYNC;
		g_eMMCDrv.u32_DrvFlag |= DRV_FLAG_DDR_MODE;
		eMMC_clock_setting(FCIE_DDR52_CLK);
		eMMC_pads_switch(FCIE_eMMC_DDR);
	}
	else if( pMStarHost_st->mmc->ios.timing == MMC_TIMING_MMC_HS200 ) {

		pMStarHost_st->sd_mod &= ~BIT_SD_DATA_SYNC;
		g_eMMCDrv.u32_DrvFlag |= DRV_FLAG_SPEED_HS200;	// step: 1
		eMMC_clock_setting(FCIE_HS200_CLK);		// step: 2
		eMMC_pads_switch(FCIE_eMMC_HS200);		// step: 3
	}
	#endif

	#if defined(CONFIG_MSTAR_PreX14) && CONFIG_MSTAR_PreX14
	#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
	if( pMStarHost_st->mmc->ios.timing == MMC_TIMING_UHS_DDR50 )
	{
		eMMC_pads_switch(FCIE_eMMC_DDR);
		#if defined(MSTAR_DEMO_BOARD) && MSTAR_DEMO_BOARD
		REG_FCIE_W(FCIE_SM_STS, BIT_DQS_MODE_3_5T);
		#elif defined(SEC_X12_BOARD) && SEC_X12_BOARD
		REG_FCIE_W(FCIE_SM_STS, BIT_DQS_MODE_2_5T);
		#endif
		g_eMMCDrv.u32_DrvFlag |= DRV_FLAG_DDR_MODE;
		pMStarHost_st->sd_mod &= ~BIT_SD_DATA_SYNC;
	}
	#endif
	#endif

}

static s32 mstar_mci_get_ro(struct mmc_host *pMMCHost_st)
{
	s32 read_only = 0;

	if(!pMMCHost_st)
	{
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: mmc is NULL \n");
		read_only = -EINVAL;
	}

	return read_only;
}

#if 0
int mstar_mci_check_D0_status(void)
{
    u16 u16Reg = 0;

    u16Reg = REG_FCIE(FCIE_SD_STATUS);

    if(u16Reg & BIT_SD_D0 )
        return 1;
    else
        return 0;
}

//EXPORT_SYMBOL(mstar_mci_check_D0_status);
#endif

static void mstar_mci_enable(struct mstar_mci_host *pMStarHost_st)
{
	u32 u32_err;

	u32_err = eMMC_FCIE_Init();

	if(eMMC_ST_SUCCESS != u32_err)
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1, "eMMC Err: eMMC_FCIE_Init fail: %Xh \n", u32_err);
}

static void mstar_mci_disable(void)
{
	u32 u32_err;

	eMMC_debug(eMMC_DEBUG_LEVEL, 1, "\n");

	eMMC_clock_setting(FCIE_DEFAULT_CLK); // enable clk

	u32_err = eMMC_FCIE_Reset();
	if(eMMC_ST_SUCCESS != u32_err)
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1, "eMMC Err: eMMC_FCIE_Reset fail: %Xh\n", u32_err);

	eMMC_clock_gating();
}

#ifdef CONFIG_DEBUG_FS

//#if defined(MMC_SPEED_TEST) && MMC_SPEED_TEST
static int mstar_mci_perf_show(struct seq_file *seq, void *v)
{
    #if defined(PERF_PROFILE)&&PERF_PROFILE

    U32 i;

    for(i=0; i < REC_REQUEST_NUM; i++) {
        if(!curhost->profile_fcie.req[i].sector.length) {
            printk("break at %d\n", i);
            break;
        }
        printk(", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
                                            curhost->profile_fcie.req[i].sector.addr,
                                            curhost->profile_fcie.req[i].sector.length,

                                            curhost->profile_fcie.req[i].pre_request.t_begin,
                                            curhost->profile_fcie.req[i].pre_request.t_end,

                                            curhost->profile_fcie.req[i].request.t_begin,
                                            curhost->profile_fcie.req[i].transfer.t_start_dma,
                                            curhost->profile_fcie.req[i].transfer.t_bh_run,
                                            curhost->profile_fcie.req[i].transfer.t_irq_happen,
                                            curhost->profile_fcie.req[i].request.t_end,

                                            curhost->profile_fcie.req[i].post_request.t_begin,
                                            curhost->profile_fcie.req[i].post_request.t_end  );
    }

    __memzero(&curhost->profile_fcie, sizeof(struct profile_hcd));

    #endif

    return 0;
}

static int mstar_mci_perf_open(struct inode *inode, struct file *file)
{
	return single_open(file, mstar_mci_perf_show, inode->i_private);
}

static const struct file_operations mstar_mci_fops_perf = {
	.owner		= THIS_MODULE,
	.open		= mstar_mci_perf_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
//#endif

static void mstar_mci_debugfs_attach(struct mstar_mci_host *pMStarHost_st)
{
	struct device *dev = mmc_dev(pMStarHost_st->mmc);

	pMStarHost_st->debug_root = debugfs_create_dir(dev_name(dev), NULL);

	if (IS_ERR(pMStarHost_st->debug_root)) {
		dev_err(dev, "failed to create debugfs root\n");
		return;
	}

	// #if defined(MMC_SPEED_TEST) && MMC_SPEED_TEST
	pMStarHost_st->debug_perf = debugfs_create_file("fcie_performance", 0444,
					       pMStarHost_st->debug_root, pMStarHost_st,
					       &mstar_mci_fops_perf);

	if (IS_ERR(pMStarHost_st->debug_perf))
		dev_err(dev, "failed to create debug regs file\n");
	//#endif
}

static void mstar_mci_debugfs_remove(struct mstar_mci_host *pMStarHost_st)
{
	#if defined(MMC_SPEED_TEST) && MMC_SPEED_TEST
		debugfs_remove(pMStarHost_st->debug_perf);
	#endif
	debugfs_remove(pMStarHost_st->debug_root);
}

#else

static inline void mstar_mci_debugfs_attach(struct mstar_mci_host *pMStarHost_st) { }
static inline void mstar_mci_debugfs_remove(struct mstar_mci_host *pMStarHost_st) { }

#endif

/* MSTAR Multimedia Card Interface Operations */
static const struct mmc_host_ops mstar_mci_ops =
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,20)
#if defined (ASYNCIO_SUPPORT) && ASYNCIO_SUPPORT
	.pre_req =  mstar_mci_pre_req,
	.post_req = mstar_mci_post_req,
#endif
#endif

#if defined (HPI_SUPPORT) && HPI_SUPPORT
	.abort_req = mstar_mci_abort_req,
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	.hw_reset = mstar_mci_reset,
#endif
	.request =	mstar_mci_request,

	.set_ios =	mstar_mci_set_ios,

	.get_ro =	mstar_mci_get_ro,
};

static s32 mstar_mci_probe(struct platform_device *dev)
{
	struct mmc_host *pMMCHost_st = 0;
	struct mstar_mci_host *pMStarHost_st = 0;
	s32 s32_ret = 0;

	/*eMMC_debug(eMMC_DEBUG_LEVEL_HIGH, 0,
			 "\33[1;31meMMC test built at %s on %s\33[m\n", __TIME__, __DATE__);*/

	eMMC_LockFCIE();

	eMMC_PlatformInit();

	mstar_mci_enable(pMStarHost_st);

	eMMC_UnlockFCIE();

	if (!dev) {
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: dev is NULL \n");
		s32_ret = -EINVAL;
		goto LABEL_END;
	}

	pMMCHost_st = mmc_alloc_host(sizeof(struct mstar_mci_host), &dev->dev);
	if (!pMMCHost_st)
	{
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: mmc_alloc_host fail \n");
		s32_ret = -ENOMEM;
		goto LABEL_END;
	}

	pMMCHost_st->ops = &mstar_mci_ops;
	pMMCHost_st->f_min = CLK_400KHz;
	pMMCHost_st->f_max = CLK_200MHz;
	pMMCHost_st->ocr_avail =
		MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 |
		MMC_VDD_31_32 | MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195;

	pMMCHost_st->max_blk_count = BIT_SD_JOB_BLK_CNT_MASK; // 4095
	pMMCHost_st->max_blk_size = 512; /* sector */
	pMMCHost_st->max_req_size = pMMCHost_st->max_blk_count * pMMCHost_st->max_blk_size;
	pMMCHost_st->max_seg_size = pMMCHost_st->max_req_size;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,20)
	pMMCHost_st->max_phys_segs      = 128;
	pMMCHost_st->max_hw_segs        = 128;
#else

#if defined(ENABLE_eMMC_ADMA_MODE) && ENABLE_eMMC_ADMA_MODE
	pMMCHost_st->max_segs           = 512; // X14H
	//printk("\33[1;35mEnable ADMA\33[m\n");
#else
	pMMCHost_st->max_segs           = 1; // X12
#endif
#endif

	pMMCHost_st->caps =
		MMC_CAP_8_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_NONREMOVABLE | MMC_CAP_ERASE;

#ifdef CONFIG_MMC_MSTAR_PREDEFINED
	pMMCHost_st->caps |= MMC_CAP_CMD23;
#endif

#if defined(IF_DETECT_eMMC_DDR_TIMING) && IF_DETECT_eMMC_DDR_TIMING
	pMMCHost_st->caps |= MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50;
#endif

#if defined(CONFIG_MSTAR_X14) && CONFIG_MSTAR_X14

	pMMCHost_st->caps |= MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50;
	if((REG_FCIE(EINSTEIN_ECO)&0x00FF)==0x0001) {
	//pMMCHost_st->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
	}
	//pMMCHost_st->caps2 |= MMC_CAP2_CACHE_CTRL;

#endif

	//---------------------------------------------------------

	pMStarHost_st                   = mmc_priv(pMMCHost_st);
	pMStarHost_st->mmc              = pMMCHost_st;
	curhost                         = pMStarHost_st;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,20)
	pMStarHost_st->next_data.mstar_cookie = 1;
#endif

#if defined (ASYNCIO_SUPPORT) && ASYNCIO_SUPPORT
	INIT_WORK(&pMStarHost_st->workqueue_dtc, mstar_mci_dtc_bh);
#endif

	pMStarHost_st->irq_lock = __SPIN_LOCK_UNLOCKED(pMStarHost_st->irq_lock);

	sema_init(&pMStarHost_st->sem_aborted_req_go_through, 0);

	pMStarHost_st->abort_success = 0;

	pMStarHost_st->SpeedUpeMMCResume = 0;

	//---------------------------------------------------------

	mmc_add_host(pMMCHost_st);

#ifdef CONFIG_DEBUG_FS
	mstar_mci_debugfs_attach(pMStarHost_st);
#endif

	platform_set_drvdata(dev, pMMCHost_st);

#if (defined(ENABLE_eMMC_INTERRUPT_MODE)&&ENABLE_eMMC_INTERRUPT_MODE) || \
	(defined(ENABLE_FCIE_HW_BUSY_CHECK)&&ENABLE_FCIE_HW_BUSY_CHECK)
	//s32_ret = request_irq(E_IRQ_NFIE, eMMC_FCIE_IRQ, 0, DRIVER_NAME, pMStarHost_st);
	s32_ret = request_irq(E_IRQ_NFIE, eMMC_FCIE_IRQ, IRQF_DISABLED, DRIVER_NAME, pMStarHost_st);
	if (s32_ret)
	{
		eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: request_irq fail \n");
		mmc_free_host(pMMCHost_st);
		goto LABEL_END;
	}
#endif

LABEL_END:

	if(s32_ret) eMMC_debug(eMMC_DEBUG_LEVEL_ERROR, 1, "eMMC Err: %d\n", s32_ret);

	return 0;
}

static s32 __exit mstar_mci_remove(struct platform_device *dev)
{
    /* Define Local Variables */
    struct mmc_host *pMMCHost_st = platform_get_drvdata(dev);
    struct mstar_mci_host *pMStarHost_st = mmc_priv(pMMCHost_st);
    s32 s32_ret = 0;

    eMMC_debug(eMMC_DEBUG_LEVEL_LINUX, 1, "\n");

    eMMC_LockFCIE();

    if (!dev)
    {
        eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1,"eMMC Err: dev is NULL\n");
        s32_ret = -EINVAL;
        goto LABEL_END;
    }

    if (!pMMCHost_st)
    {
        eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1,"eMMC Err: mmc is NULL\n");
        s32_ret= -1;
        goto LABEL_END;
    }

    eMMC_debug(eMMC_DEBUG_LEVEL,1,"eMMC, remove +\n");

    #ifdef CONFIG_DEBUG_FS
    mstar_mci_debugfs_remove(pMStarHost_st);
    #endif

    mmc_remove_host(pMMCHost_st);

    mstar_mci_disable();

    #if defined(ENABLE_eMMC_INTERRUPT_MODE) && ENABLE_eMMC_INTERRUPT_MODE
    free_irq(E_IRQ_NFIE, pMStarHost_st);
    #endif

    mmc_free_host(pMMCHost_st);
    platform_set_drvdata(dev, NULL);

    eMMC_debug(eMMC_DEBUG_LEVEL,1,"eMMC, remove -\n");

    LABEL_END:
    eMMC_UnlockFCIE();

    return s32_ret;
}


#ifdef CONFIG_PM

void mstar_mci_hardware_reset(void)
{
	eMMC_RST_L();
	eMMC_hw_timer_delay(HW_TIMER_DELAY_1ms);
	eMMC_RST_H();
	eMMC_hw_timer_delay(HW_TIMER_DELAY_1ms);
}

void mstar_mci_normal_resume(void)
{
	eMMC_LockFCIE();

	eMMC_PlatformInit();

	mstar_mci_enable(curhost);

	mstar_mci_hardware_reset();

	eMMC_UnlockFCIE();
}

#if defined(SPEED_UP_EMMC_RESUME)&&SPEED_UP_EMMC_RESUME

void mstar_mci_fast_resume(void)
{
	struct mmc_command cmd;

	curhost->SpeedUpeMMCResume = 1;

	eMMC_LockFCIE();

	eMMC_PlatformInit();

	mstar_mci_enable(curhost);

	mstar_mci_hardware_reset();

	curhost->sd_mod = BIT_SD_DEFAULT_MODE_REG;
	eMMC_clock_setting(FCIE_SLOWEST_CLK);

	cmd.opcode = MMC_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_NONE;
	mstar_mci_send_cmd(&cmd);

	cmd.opcode = MMC_SEND_OP_COND;
	cmd.arg = 0x40200000;
	cmd.flags = MMC_RSP_R3;
	mstar_mci_send_cmd(&cmd);

	eMMC_UnlockFCIE();
}

void mstar_mci_preinit_emmc(void)
{
	mstar_mci_fast_resume();
}

EXPORT_SYMBOL(mstar_mci_preinit_emmc);

#endif

static s32 mstar_mci_suspend(struct platform_device *dev, pm_message_t state)
{
    /* Define Local Variables */
    struct mmc_host *pMMCHost_st = platform_get_drvdata(dev);
    s32 ret = 0;

    if (pMMCHost_st)
    {
        eMMC_debug(eMMC_DEBUG_LEVEL,1,"eMMC, suspend +\n");
        ret = mmc_suspend_host(pMMCHost_st);
    }

    eMMC_debug(eMMC_DEBUG_LEVEL,1,"eMMC, suspend -, %Xh\n", ret);

    return ret;
}

static s32 mstar_mci_resume(struct platform_device *dev)
{
	struct mmc_host *pMMCHost_st = platform_get_drvdata(dev);
	s32 ret = 0;

	if(!curhost->SpeedUpeMMCResume) {

		mstar_mci_normal_resume();
	}

	if (pMMCHost_st)
	{
		ret = mmc_resume_host(pMMCHost_st);
	}

	return ret;
}

#endif  /* End ifdef CONFIG_PM */

/******************************************************************************
 * Define Static Global Variables
 ******************************************************************************/
static struct platform_driver mstar_mci_driver =
{
    .probe = mstar_mci_probe,
    .remove = __exit_p(mstar_mci_remove),

    #ifdef CONFIG_PM
    .suspend = mstar_mci_suspend,
    .resume = mstar_mci_resume,
    #endif

    .driver  =
    {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
};

static u64 mstar_dma_mask = BLK_BOUNCE_ANY; // to access high memory directly without bounce 

static struct platform_device mstar_mci_device =
{
    .name =	DRIVER_NAME,
    .id = 0,
    .resource =	NULL,
    .num_resources = 0,
	.dev = {
		.dma_mask = &mstar_dma_mask,
	},
};


/******************************************************************************
 * Init & Exit Modules
 ******************************************************************************/
static s32 __init mstar_mci_init(void)
{
    int err = 0;
    eMMC_debug(eMMC_DEBUG_LEVEL_LINUX,1,"\n");

    if((err = platform_device_register(&mstar_mci_device)) < 0)
        eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1,"eMMC Err: platform_driver_register fail, %Xh\n", err);

    if((err = platform_driver_register(&mstar_mci_driver)) < 0)
        eMMC_debug(eMMC_DEBUG_LEVEL_ERROR,1,"eMMC Err: platform_driver_register fail, %Xh\n", err);

    return err;
}

static void __exit mstar_mci_exit(void)
{
    platform_driver_unregister(&mstar_mci_driver);
}

void mstar_mci_dump_debug_msg(void)
{
	printk("\n[%s]\n", __FUNCTION__);

	eMMC_DumpDebugInfo();

	if(curhost->req_cmd) {
		eMMC_printf("\33[1;31m Last CMD: CMD%02d_%08Xh\33[m\n", curhost->req_cmd->cmd->opcode, curhost->req_cmd->cmd->arg);
	}
	if(curhost->req_dtc) {
		eMMC_printf("\33[1;31m Last DTC: CMD%02d_%08Xh\33[m\n", curhost->req_dtc->cmd->opcode, curhost->req_dtc->cmd->arg);
		//eMMC_DebugDumpData(curhost->req_dtc->data);
	}
}

rootfs_initcall(mstar_mci_init);
module_exit(mstar_mci_exit);

MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("Mstar Multimedia Card Interface driver");
MODULE_AUTHOR("Hill.Sung/Hungda.Wang");
