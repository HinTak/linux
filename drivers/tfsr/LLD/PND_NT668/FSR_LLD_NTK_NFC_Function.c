#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include    "FSR_LLD_NTK_NFC_Function.h"
#include    "FSR_LLD_PureNAND.h"
#include    "FSR_LLD_NTK_668_Reg.h"
#include    "NandDef.h"

#define     FPGA_CVT_ONLY            0
#define     NAND_NFC_HARDRESET_USED  0

#define WR_REG(addr,value)              *((unsigned long volatile *)(addr))=(value)
#define RD_REG(addr)                    *((unsigned long volatile *)(addr))
#define OR_REG(addr,value)              WR_REG(addr, RD_REG(addr) | (value))
#define CLR_REG(addr,value)             WR_REG(addr, RD_REG(addr) & ~(value))
#define SET_REG(addr,mask,value)        WR_REG(addr, (RD_REG(addr) & ~(mask)) | (value))

#define dump_ECC_Reg NFC_DumpReg
#define DEBUG_PRINTF //FSR_OAM_DbgMsg

#define PrintString(x) 
#define PrintDWord(x)
#define printf FSR_OAM_DbgMsg

#ifndef __KERNEL__
#define EXPORT_SYMBOL(name)
#define external_sync()
#endif

#if NAND_ACP_ENABLE
#define NT72668_ENABLE_ACP_ARB32BIT() \
            do{ \
                *(volatile unsigned long *)(0xfd6b043c ) = 0x009FDFCC; \
            }while(0)

// ACP MAU LV2 32/64-bit mode, select 32bit mode
#define NT72668_ENABLE_ACP_MAU_LV2_32BIT() \
    do { \
        *((volatile unsigned int *) 0xfd150408) = *((volatile unsigned int *) 0xfd150408) & (~(1 << 21)); \
    } while (0)

// enable AHB to AXI ACP
#define NT72668_ENABLE_AHB2AXI_ACP() \
            do{ \
                *(volatile unsigned long *)(0xFD150084) |= 1 << 29; \
            }while(0)
#endif


//#define TRACE
#ifdef TRACE
#define N_HALT_DEBUG(_fmt, args...)  \
	do{\
		FSR_OAM_DbgMsg("\n{%s%d}"_fmt, __FUNCTION__,__LINE__,args);\
	}\
while(0)
#else //#ifdef TRACE
#define N_HALT_DEBUG(_fmt, args...)  
#endif//#ifdef TRACE

#ifdef NFC_USE_INTERRUPT
UINT32 NFC_IRQInit(UINT32 INT_FLAG);
struct semaphore NFC_IRQ;
#endif


extern void delay_us(unsigned long count);
void NFC_ControllerReset(void);
UINT NFC_GetECCBitsAndPos_RS(PECC_INFO pucErrorBitArray);
UINT NFC_GetECCBitsAndPos_BCH54(PECC_INFO pucErrorBitArray);
UINT NFC_GetECCBitsAndPos(PECC_INFO pucErrorBitArray);
void NFC_DumpReg(void);
void hex_dump(UINT8 *pu8Data, UINT32 u32Length);

unsigned int S_REG_NFC_CFG0 ;
unsigned int S_REG_NFC_CFG0_Read ;
unsigned int S_REG_NFC_CFG0_Program ;
unsigned int S_REG_NFC_CFG1 ;
unsigned int S_REG_NFC_SYSCTRL ;
unsigned int S_REG_NFC_SYSCTRL1 ;
unsigned int S_REG_NFC_SYSCTRL2 ;
unsigned int S_REG_NFC_Fine_Tune ;

NAND_CFG NandInfo;
static void __iomem *  nfc_reg_base;

void nfc_init(void)
{
	static init = 1;
	printk("[%4d]%s\n",__LINE__,__FILE__);
	if(init)
	{
#if defined(__KERNEL__) && !defined(__U_BOOT__)
		struct device_node *node = NULL;

		node = of_find_compatible_node(NULL, NULL, "nvt,nfc");

		if (node) 
		{
			nfc_reg_base = of_iomap(node, 0);
			printk("USE NFC Node 0x%08x\n",nfc_reg_base);
		}
		else
		{
			of_node_put(node);
			printk("can't find node\n");
			return ;
		}
#endif
		init = 0;
	}
	printk("[%4d]%s\n",__LINE__,__FILE__);
}
ER NFC_Init(unsigned char ucCE)
{
    ULONG ulTemp = 0;
    ULONG ulNandID = 0;

	nfc_init();
	//TODO check this
    //WR_REG(0xfc040204, 0x72682);
    //WR_REG(0xfc040204, 0x28627);
    //WR_REG(0xfc040208, 0x01);

    //WR_REG(0xfc040200, (RD_REG(0xfc040200) & (~(1<<4)))| (1<<5)) ;//  |(1<<4));

    ////enable AHB access right
    //WR_REG(0xfd650000, (RD_REG(0xfd650000) & (~(1<<4)))) ;

//#if NAND_ACP_ENABLE
//    //For ACP setting
//    NT72668_ENABLE_ACP_ARB32BIT();      // ACP arbiter 32/64-bit mode, select 32bit mode
//    NT72668_ENABLE_ACP_MAU_LV2_32BIT(); // ACP MAU LV2 32/64-bit mode, select 32bit mode
//    NT72668_ENABLE_AHB2AXI_ACP();       // enable AHB2AXI ACP path
//    WR_REG(0xfd060104, (RD_REG(0xfd060104) | (1 << 30) | (1 << 31)));
//#endif

    // NFC part setting
    // software reset host
    REG_NFC_SW_RESET = 0x03;
    while(REG_NFC_SW_RESET != 0);

    // clear interrupt status
    REG_NFC_INT_ENABLE = 0x0;
    REG_NFC_INT_STAT = 0xFFFFFFFF;

	REG_NFC_CFG0 = NAND_READ_TIMING;
	REG_NFC_CFG1 = NFC_CFG1_READY_TO_BUSY_TIMEOUT(0xF) | NFC_CFG1_LITTLE_ENDIAN_XTRA | 
		           NFC_CFG1_LITTLE_ENDIAN | NFC_CFG1_BUSY_TO_READY_TIMEOUT(0xFFFF);

    REG_NFC_SYSCTRL = 0;
    REG_NFC_SYSCTRL1 = 0x01;
    REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 |  16;

    S_REG_NFC_CFG0_Program = ((REG_NFC_CFG0) & ~(0xFFFFFFF0)) | NAND_PROGRAM_TIMING;
    S_REG_NFC_CFG0_Read = (REG_NFC_CFG0 & ~(0xFFFFFFF0)) | NAND_READ_TIMING;
	S_REG_NFC_Fine_Tune = NAND_FINETUNE_TIMING;

	S_REG_NFC_CFG1 = REG_NFC_CFG1;
	S_REG_NFC_SYSCTRL = REG_NFC_SYSCTRL;
	S_REG_NFC_SYSCTRL1 = REG_NFC_SYSCTRL1 ;
	S_REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 ;
	
#ifdef NFC_USE_INTERRUPT
		sema_init(&NFC_IRQ,0);
		if(0 != NFC_IRQInit(0))
		{
			FSR_OAM_DbgMsg("\nInit NFC IRQ Fail");
		}
#endif
	return FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_Init);


#if 0
UINT32 NFC_ReadID(NANDIDData    *pstNANDID)
{
	UINT32  device_id=0x00, device_id_ext=0x00;

	FSR_OAM_DbgMsg("[%s]\n", __FUNCTION__);


	NFC_NAND_REIN();
	w32(REG_NFC_CFG0, S_REG_NFC_CFG0_RR);  // adjust timing for Freerun Read Page

	w32(REG_NFC_TRAN_MODE,0x00);
	w32(REG_NFC_TRAN_MODE,NFC_TRAN_MODE_CE_IDX(dwCE) | NFC_TRAN_MODE_BLK_SIZE(6));

	// clear interrupt status
	w32(REG_NFC_INT_STAT,0xFFFFFFFF);

	w32(REG_NFC_CMD,NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT | NFC_CMD_ADDR_CYCLE_DUMMY |
			NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x90));

#ifndef NFC_USE_INTERRUPT
	NFC_WaitSignal(NFC_INT_CMD_COMPLETE);			// wait for command complete
#endif
	NFC_WaitSignal(NFC_INT_DATA_TRAN_COMPLETE);		// wait for transfer complete

	if (!(r32(REG_NFC_INT_STAT) & NFC_INT_ERR))
	{
		device_id     = r32(REG_NFC_DATA_PORT);
		device_id_ext = r32(REG_NFC_DATA_PORT);

		pstNANDID->nMID         = device_id & 0xff;        
		pstNANDID->nDID         = (device_id >> 8) & 0xff; 
		pstNANDID->n3rdIDData   = (device_id >> 16) & 0xff;
		pstNANDID->n4thIDData   = (device_id >> 24) & 0xff;
		pstNANDID->n5thIDData   = device_id_ext & 0xff;    
		pstNANDID->nPad0        = (device_id_ext >> 8) & 0xff;

		//TODO modify return value
		return 0;
	}
	else
	{
		FSR_OAM_DbgMsg("[%s] Warning: Read ID FAILS\n", __FUNCTION__);

		//TODO modify return value
		return -1;
	}
}
#endif

ER NFC_ReadID(unsigned char ucCE, NANDIDData* pstNANDID)
{ 
    ULONG   device_id = 0xaaaaaaaa , device_id_ext = 0xbbbbbbbb;

    REG_NFC_SW_RESET = 0x03;
    while(REG_NFC_SW_RESET != 0);

    // NFC_CFG1_RS_ECC_ENABLE can not be set, when this function is not use
    REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;  // NFC_CFG1_RS_ECC_ENABLE can not be set
    // transfer 6 bytes (ID)
    REG_NFC_TRAN_MODE = NFC_TRAN_MODE_CE_IDX(ucCE) | NFC_TRAN_MODE_BLK_SIZE(6);

    // clear interrupt status
    REG_NFC_INT_STAT = -1;

    REG_NFC_CFG0 =  S_REG_NFC_CFG0_Read;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;

    REG_NFC_CMD = NFC_CMD_CE_IDX(ucCE) | NFC_CMD_WP_NEG | NFC_CMD_WP_KEEP | NFC_CMD_DATA_PRESENT | NFC_CMD_ADDR_CYCLE_DUMMY |
        NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x90);
    //PrintDWord(REG_NFC_CMD);
    //PrintString(string10);

    // wait for command complete
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_CMD_COMPLETE)));
    // wait for transfer complete
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_DATA_TRAN_COMPLETE)));

    if (!(REG_NFC_INT_STAT & NFC_INT_ERR))
    {
        device_id = REG_NFC_DATA_PORT;
        device_id_ext = REG_NFC_DATA_PORT;

		pstNANDID->nMID         = device_id & 0xff;        
		pstNANDID->nDID         = (device_id >> 8) & 0xff; 
		pstNANDID->n3rdIDData   = (device_id >> 16) & 0xff;
		pstNANDID->n4thIDData   = (device_id >> 24) & 0xff;
		pstNANDID->n5thIDData   = device_id_ext & 0xff;    
		pstNANDID->nPad0        = (device_id_ext >> 8) & 0xff;

        printf("CE:%d NFC_ReadID:0x%lx  0x%lx\r\n", ucCE, device_id, device_id_ext);

        return FSR_LLD_SUCCESS;
    }else
    {
        printf("NFC_ReadID Fails \n");
        return NAND_ERR_SYSTEM;
    }
}


EXPORT_SYMBOL(NFC_ReadID);

UCHAR NFC_ReadStatus(unsigned char ucCE)
{  
    ULONG   ret;

    REG_NFC_CFG0 =  S_REG_NFC_CFG0_Read;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;

    REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;

    REG_NFC_TRAN_MODE = NFC_TRAN_MODE_CE_IDX(ucCE) | NFC_TRAN_MODE_BLK_SIZE(1); // transfer 1 byte (status)
    REG_NFC_INT_STAT = NFC_INT_CMD_COMPLETE | NFC_INT_DATA_TRAN_COMPLETE;   // clear command complete & transfer complete status

    REG_NFC_CMD = NFC_CMD_CE_IDX(ucCE) | NFC_CMD_DATA_PRESENT | NFC_CMD_WP_NEG | NFC_CMD_WP_KEEP | NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x70);
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_CMD_COMPLETE)));  // wait for command complete
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_DATA_TRAN_COMPLETE)));    // wait for transfer complete


    if (REG_NFC_CFG1 & NFC_CFG1_LITTLE_ENDIAN)
    { // little endian
        ret = !(REG_NFC_INT_STAT & NFC_INT_ERR) ? (REG_NFC_DATA_PORT & 0xff) : 0;
    }
    else
    { // big endian
        ret = !(REG_NFC_INT_STAT & NFC_INT_ERR) ? REG_NFC_DATA_PORT >> 24 : 0;
    }

    return ret;
}
EXPORT_SYMBOL(NFC_ReadStatus);


#if 0
UINT32 NFC_ReadPage_FreeRun( PureNANDSpec *pstPNDSpec , UINT32 dwPageAddr,
		UINT8 *dwBuffer, UINT8 *dwExtraBuffer)
{

	UINT32  bResult = FSR_LLD_SUCCESS;
	UINT32  sNAND_Page_Size ;
	//UINT32  blank_ecc_error_cnt = 0;
	UINT32  subpage_cnt ;
	UINT32  ECN_Total = 0;
	UINT32  blank_page;



	sNAND_Page_Size = pstPNDSpec->nSctsPerPg * FSR_SECTOR_SIZE ;

	subpage_cnt = pstPNDSpec->nSctsPerPg ;

	N_HALT_DEBUG("NFC_ReadPage Page:[0x%08x]", dwPageAddr);
	NFC_NAND_REIN();

	w32(REG_NFC_CFG0, S_REG_NFC_CFG0_RR);  // adjust timing for Freerun Read Page
	w32(REG_NFC_Fine_Tune,(r32(REG_NFC_Fine_Tune)|0xc0)) ;                      // Add for adjust read duty cycle
	w32(REG_NFC_SYSCTRL1, (r32(REG_NFC_SYSCTRL1)|0x800 ));                 // used for adjust latch time

	//reset ecc decode
	BIT_SET(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));
	BIT_CLEAR(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));

	// software reset host
	w32(REG_NFC_SW_RESET, 0x03);
	while(r32(REG_NFC_SW_RESET) & 0x03);

	BIT_SET(REG_NFC_SYSCTRL2, NFC_SYS_CTRL2_BLANK_CHECK_EN); // Set bit 6 : Check Blank function

	w32(REG_NFC_INT_STAT, -1);  // Clean Interrupt Status

	//enable Free run mechanism
	BIT_SET(REG_NFC_SYSCTRL1, NFC_SYS_CTRL1_FREE_RUN_EN); // Set Bit 2

	//set 1: for new free-run read: RS and BCH one page 1 time 
	BIT_SET(REG_NFC_SYSCTRL1, NFC_SYS_CTRL1_ECC_DEC_MODE); // Set Bit 10

	//set 1: disable "Remove extra 2 dummy bytes  of extra"
	BIT_SET(REG_NFC_SYSCTRL1, NFC_SYS_CTRL1_REMOVE_EXT_DATA); // Bit 6

	// Set Row Address
	w32(REG_NFC_ROW_ADDR, dwPageAddr );

	// Set Col Address to subpage0 (0)
	w32(REG_NFC_COL_ADDR, 0);


	// NFC_TRAN_MODE_XTRA_DATA_COUNT_16 : This config will not use when BCH is ready
	w32(REG_NFC_TRAN_MODE, NFC_TRAN_MODE_CE_IDX(dwCE) | NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_TWO | 
			NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET | 
			NFC_TRAN_MODE_XTRA_DATA_COUNT_16 | NFC_TRAN_MODE_BLK_SIZE(sNAND_Page_Size) | 
			NFC_TRAN_MODE_START_WAIT_RDY | NFC_TRAN_MODE_DATA_SEL_DMA);

	// Set random access command
	w32(REG_NFC_RAND_ACC_CMD, NFC_RAND_ACC_CMD_CODE1(0xe0) | NFC_RAND_ACC_CMD_CODE0(0x05) |
			NFC_RAND_ACC_CMD_COL_ADDR(sNAND_Page_Size));


	// Set DMA Destination
	w32(REG_NFC_DMA_ADDR, ((UINT32)dwBuffer) & 0x1fffffff );

	// Set XTRA Buffer
	w32(REG_NFC_XTRA_ADDR, ((UINT32)dwExtraBuffer) & 0x1fffffff );

	// Set DMA Control
	w32(REG_NFC_DMA_CTRL, NFC_DMA_CTRL_READ |( sNAND_Page_Size));  // Free Run mode does not need to -1

	if(gEnableECC==1)
	{
		//REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_ECC_TYPE_MSK);
        BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_MSK);

		//REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_ECC_TYPE_RS;
        BIT_SET(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_RS);

		//REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 & (~(1<<7)) ;	// Macoto enable correct
		BIT_CLEAR(REG_NFC_SYSCTRL2, NFC_SYS_CTRL2_AUTO_CORRECT_DISABLE);

		//REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_RS | NFC_SYS_CTRL_SUBPAGE_512;
        BIT_SET(REG_NFC_SYSCTRL, NFC_SYS_CTRL_ECC_RS | NFC_SYS_CTRL_SUBPAGE_512);

		//REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_REMOVE_EXTRA_2DUMMY_BYTE_DIS;
        BIT_SET(REG_NFC_SYSCTRL1, NFC_SYS_CTRL1_REMOVE_EXTRA_2DUMMY_BYTE_DIS);
	}
	else
	{
		BIT_SET(REG_NFC_SYSCTRL2, (NFC_SYS_CTRL2_AUTO_CORRECT_DISABLE));    // Bit 7  // Disable Auto Correct
	}
	// Set command
	w32(REG_NFC_CMD, NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT | NFC_CMD_WP_NEG |//NFC_CMD_FREERUN | 
			NFC_CMD_ADDR_CYCLE_COL_ROW | NFC_CMD_END_WAIT_BUSY_TO_RDY | 
			NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0x30) | NFC_CMD_CODE0(0x00));

	// wait for system memory transfer complete 
#if 1 
	NFC_WaitSignal(NFC_INT_ERR_RW | NFC_INT_MEM_TRAN_COMPLETE);	// wait for system memory transfer complete   
#else 
	UINT32 dwTemp = 0;   
	while(!(r32(REG_NFC_INT_STAT) & (NFC_INT_ERR_RW | NFC_INT_MEM_TRAN_COMPLETE)))
	{
		printf("\nwait for transfer complete");
		dwTemp++;               
		if (dwTemp > 500)
		{
			FSR_OAM_DbgMsg("Warning : NFC_ReadPage_FR wait < NFC_INT_DATA_TRAN_COMPLETE > timeout! page=%d, i=%d\n", dwPageAddr, i);
			FSR_OAM_DbgMsg("REG_NFC_INT_STAT=%#lx\n", ntk_readl(REG_NFC_INT_STAT));

			w32(REG_NFC_CFG1, r32(REG_NFC_CFG1) & (~NFC_CFG1_RS_ECC_ENABLE));

			w32(REG_NFC_INT_STAT, 0xFFFFFFFF);

			w32(REG_NFC_SW_RESET, 0x03);
			while(r32(REG_NFC_SW_RESET) & 0x03);

			FSR_OAM_DbgMsg("NAND_REDO -- Error : CFG0 = 0x%x, !\n", ntk_readl(REG_NFC_CFG0));

			dwTemp = 0;
			return FSR_LLD_PREV_READ_ERROR;
		}
	}
#endif    

#ifdef RDUMP
	if(NFC_debug != 0)
	{
		dump_ECC_Reg();
	}
#endif
	if(gEnableECC==1)
	{
		ECN_Total = NFC_GetECCBits(pstPNDSpec->nSctsPerPg);
		//print ECN_Page
		if ( ECN_Total != 0x00 ){
			FSR_OAM_DbgMsg("+ECN_Total :%d\r\n",ECN_Total);
		}


		if ( r32(REG_NFC_INT_STAT) & NFC_INT_FR_UNCORECTABLE )
		{
			FSR_OAM_DbgMsg("+NFC_INT_FR_UNCORECTABLE \r\n");
			return FSR_LLD_PREV_READ_ERROR;
		}
	}

	// Check blank page
	blank_page = ((r32(REG_NFC_INT_STAT))>>22) &0x01;
	if (blank_page == 0x01){
		//FSR_OAM_DbgMsg("+Blank Page \r\n");
	}

	bResult = (r32(REG_NFC_INT_STAT)) & NFC_INT_ERR;  

	if (bResult)
	{
		FSR_OAM_DbgMsg("+REG_NFC_INT_STAT Error !!\r\n");
		return FSR_LLD_PREV_READ_ERROR;
	}

#ifdef TEST_INTERRUPT_MODE
	//NFC_Interrupt_Disable();
#endif
	return FSR_LLD_SUCCESS;
}
#endif 

ER NFC_ReadPage_WholePage(unsigned char ucCE, UINT32 dwPageAddr, UINT8 *pucBuffer, UINT8 *pucExtraBuffer, UINT8 ucECCType)
{
	ECC_INFO ECCInfo[8];
	unsigned char  BlankPage;
	unsigned char* pbBlankPage = &BlankPage;
	unsigned char  ucMaxECCError = 0;

    ucECCType = NAND_ECC_TYPE_REEDSOLOMON;


	NFC_ControllerReset();

#if TEST_INTERRUPT_MODE
		REG_NFC_INT_ENABLE = 0xFFFFFFFF;
		REG_NFC_INT_STAT = 0xFFFFFFFF;
		NFC_Interrupt_Enable();
		SystemFlag = 1;
#endif

    //reset ecc decode
    REG_NFC_CFG1 |= (NFC_CFG1_ECC_DECODE_RESET);
    REG_NFC_CFG1 &= ~(NFC_CFG1_ECC_DECODE_RESET);

//    REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 | (1<<6) ; // Set Check Blank function
    REG_NFC_SYSCTRL2 = NFC_SYS_CTRL2_BLANK_ENABLE | NandInfo.SpareSizePerSubPage | NFC_SYS_CTRL2_XTRA_REG_SET1 | NFC_SYS_CTRL2_DISABLE_FREE_WRITE_ECC_ENCODE;
    REG_NFC_INT_STAT = 0xFFFFFFFF;  // Clean Interrupt Status

    //enable Free run mechanism
    REG_NFC_SYSCTRL1 = NFC_SYS_CTRL1_FREE_RUN | NFC_SYS_CTRL1_NEW_VERSION;
    //set 1: for new free-run read: RS and BCH one page 1 time
    REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_BCH16_ENABLE_NEW_ARCH;
    //set 2: Delay 1T to latch data
    REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_DELAY_LATCH_1T;

    REG_NFC_CFG0 = S_REG_NFC_CFG0_Read;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;

    //set data bus to 8bit
    REG_NFC_CFG0 &= ~NFC_CFG0_DATAWIDTH_16;
    REG_NFC_SYSCTRL = NFC_SYS_CTRL_EXTRA_SIZE(NandInfo.SpareSizePerSubPage) | NandInfo.ulPageType | NandInfo.ulBlockSizeType | NFC_SYS_CTRL_8_BIT_NAND;
		
    // Set Row Address
    REG_NFC_ROW_ADDR = dwPageAddr;
    // Set Col Address to subpage0 (0)
    REG_NFC_COL_ADDR = 0;

    REG_NFC_TRAN_MODE = NFC_TRAN_MODE_CE_IDX(ucCE) | NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_TWO |
        NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET |
        NFC_TRAN_MODE_XTRA_DATA_COUNT_16 | NFC_TRAN_MODE_TRANSFER_BYTE_COUNT(NandInfo.BytesPerPage) |
        NFC_TRAN_MODE_START_WAIT_RDY | NFC_TRAN_MODE_DATA_SEL_DMA;

    // Set random access command
    REG_NFC_RAND_ACC_CMD = NFC_RAND_ACC_CMD_CODE1(0xe0) | NFC_RAND_ACC_CMD_CODE0(0x05) |
        NFC_RAND_ACC_CMD_COL_ADDR(NandInfo.BytesPerPage);

    // Set DMA Destination
    REG_NFC_DMA_ADDR = (unsigned long)pucBuffer;

    // Set XTRA Buffer
    REG_NFC_XTRA_ADDR = (unsigned long)pucExtraBuffer;

    REG_NFC_AHB_BURST_SIZE &= NFC_AHB_MASTER_MSK;
    REG_NFC_AHB_BURST_SIZE |= NFC_AHB_MASTER_16_8_4BEAT;

    if(ucECCType == NAND_ECC_TYPE_REEDSOLOMON)
    {
        DEBUG_PRINTF("R RS\r\n");
        REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_RS_ECC_ENABLE;
        REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 & (~(1<<7)); 
        REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_RS | NFC_SYS_CTRL_SUBPAGE_512;
		//disable "Remove extra 2 dummy bytes of extra"
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_REMOVE_EXTRA_2DUMMY_BYTE_DIS;
    }
#if NAND_BCH_SUPPORTED	
    else if(ucECCType == NAND_ECC_TYPE_BCH54)
    {
        DEBUG_PRINTF("R BCH54\r\n");
        REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_RS_ECC_ENABLE);
        REG_NFC_CFG1 |= NFC_CFG1_BCH_ECC_ENABLE | NFC_CFG1_BCH_ENABLE_PL;
        REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 & (~(1<<7));
        REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_BCH | NFC_SYS_CTRL_SUBPAGE_1024;
    }
    else if(ucECCType == NAND_ECC_TYPE_NONE_54)
    {
        DEBUG_PRINTF("R NONE-BCH54\r\n");
        REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_RS_ECC_ENABLE);
        REG_NFC_CFG1 |= NFC_CFG1_BCH_ECC_ENABLE | NFC_CFG1_BCH_ENABLE_PL;

        REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_BCH | NFC_SYS_CTRL_SUBPAGE_1024;
        REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 | (1<<7);
        REG_NFC_SYSCTRL2 |= 0x80; //RS off
        REG_NFC_SYSCTRL2 &= ~0x800; //It should be setted to 0 when BCH is selected.

    }
#endif	
    else if(ucECCType == NAND_ECC_TYPE_NONE_16)
    {
        DEBUG_PRINTF("R NONE-RS16\r\n");
        REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 | (1<<7);
    }

    // Set DMA Control
    REG_NFC_DMA_CTRL = NFC_DMA_CTRL_READ | (NandInfo.BytesPerPage);  // Free Run mode does not need to -1

    // Set command
    REG_NFC_CMD = NFC_CMD_CE_IDX(ucCE) | NFC_CMD_DATA_PRESENT /*| NFC_CMD_WP_NEG */ | NFC_CMD_WP_KEEP | NFC_CMD_FREERUN |
    NFC_CMD_ADDR_CYCLE_COL_ROW | NFC_CMD_END_WAIT_BUSY_TO_RDY |
    NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0x30) | NFC_CMD_CODE0(0x00);

    // wait for command complete
#if TEST_INTERRUPT_MODE
    while(SystemFlag) ;
#endif

    // wait for system memory transfer complete
    while( !(REG_NFC_INT_STAT & (NFC_INT_ERR_RW | NFC_INT_MEM_TRAN_COMPLETE | NFC_INT_FREE_RUN_COMPLETE) ) );

//    ulStatusReg = REG_NFC_INT_STAT;

    REG_NFC_CFG1 &= ~NFC_CFG1_RS_ECC_ENABLE;

#if NAND_BCH_SUPPORTED	
    REG_NFC_CFG1 &= ~NFC_CFG1_BCH_ECC_ENABLE;
#endif

    // Check blank page
    if ((REG_NFC_INT_STAT & NFC_INT_BLANK_PAGE) == NFC_INT_BLANK_PAGE)
        *pbBlankPage = TRUE;
    else
        *pbBlankPage = FALSE;

    if((ucECCType != NAND_ECC_TYPE_NONE_16) && (ucECCType != NAND_ECC_TYPE_NONE_54))
	{   //ECC is enabled
		unsigned char ucSubPageIndex = 0;
		unsigned char ucSubPage = NandInfo.BytesPerPage / NandInfo.BytesPerSubPage;

	    if(*pbBlankPage == FALSE)
		{
        	ucMaxECCError = NFC_GetECCBitsAndPos(&ECCInfo[0]);
#ifndef CONFIG_VD_RELEASE  // only debug case
			if(ucMaxECCError > 0 )
			{
	            FSR_OAM_DbgMsg("Block:%4d Page:%3d   ", dwPageAddr / NandInfo.PagesPerBlock, dwPageAddr % NandInfo.PagesPerBlock);
				for (ucSubPageIndex = 0; ucSubPageIndex < ucSubPage ; ucSubPageIndex++)
					FSR_OAM_DbgMsg("  SubPage[%d]:%d", ucSubPageIndex, ECCInfo[ucSubPageIndex].usSymbolCount);
				FSR_OAM_DbgMsg("\r\n");
	            FSR_OAM_DbgMsg("Register: ECC0:0x%8x    ECC1:0x%8x\r\n", NFC_ERR_CNT0, NFC_ERR_CNT1);
			}
#endif
	    }

	    if (REG_NFC_INT_STAT & NFC_INT_FR_UNCORECTABLE )
	    {
			FSR_OAM_DbgMsg("+NFC_INT_FR_UNCORECTABLE\r\nBlock:%4d Page:%3d	 ", dwPageAddr / NandInfo.PagesPerBlock, dwPageAddr % NandInfo.PagesPerBlock);
			for (ucSubPageIndex = 0; ucSubPageIndex < ucSubPage ; ucSubPageIndex++)
				FSR_OAM_DbgMsg("  SubPage[%d]:%d", ucSubPageIndex, ECCInfo[ucSubPageIndex].usSymbolCount);
			FSR_OAM_DbgMsg("\r\n");
			FSR_OAM_DbgMsg("Register: ECC0:0x%8x	ECC1:0x%8x\r\n", NFC_ERR_CNT0, NFC_ERR_CNT1);

			NFC_DumpReg();

#ifndef CONFIG_VD_RELEASE  // only debug case
			FSR_OAM_DbgMsg("\r\ninfinite loop... \r\n");
			while(1);
#endif
	        return NAND_ERR_UNCORECTABLE;
	    }

		if (ucMaxECCError >= 0x3 )
		{
#if NAND_BCH_SUPPORTED	
			if(ucECCType == NAND_ECC_TYPE_BCH54)
			{	
				if (ucMaxECCError >= 0x20 )
					return FSR_LLD_PREV_2LV_READ_DISTURBANCE;		
			}	
#endif
			return FSR_LLD_PREV_2LV_READ_DISTURBANCE;		
		}	
	}
	
    if ((REG_NFC_INT_STAT & NFC_INT_ERR) != 0)
    {
		FSR_OAM_DbgMsg("+REG_NFC_INT_STAT Error !!\r\n");
		NFC_DumpReg();
		return FSR_LLD_PREV_READ_ERROR;
    }

    return FSR_LLD_SUCCESS;
}



EXPORT_SYMBOL(NFC_ReadPage_WholePage);

#if 0

UINT32 NFC_ReadPage( PureNANDSpec *pstPNDSpec , UINT32 dwPageAddr, 
		UINT8 *dwBuffer, UINT8 *dwExtraBuffer)
{


	INT32   dwSubPagesPerPage = NAND_Page_Size/NAND_SubPage_Size;
	UINT32   i;    

	N_HALT_DEBUG("NFC_ReadPage Page:[0x%08x]", dwPageAddr);
	NFC_NAND_REIN();
	w32(REG_NFC_CFG0, S_REG_NFC_CFG0_RR);  // adjust timing for Freerun Read Page

	BIT_CLEAR(REG_NFC_SYSCTRL2, (1<<10));

	for(i = 0; i < dwSubPagesPerPage; ++i)    
	{
		BIT_SET(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));
		BIT_CLEAR(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));

		w32(REG_NFC_SW_RESET, 0x03);
		while(r32(REG_NFC_SW_RESET) & 0x03);

		w32(REG_NFC_INT_STAT, -1);


		// Set Column Address
		w32(REG_NFC_COL_ADDR, NAND_SubPage_Size * i);
		w32(REG_NFC_TRAN_MODE, NFC_TRAN_MODE_CE_IDX(dwCE) | NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_TWO | 
				NFC_TRAN_MODE_XTRA_DATA_COUNT_16 | NFC_TRAN_MODE_BLK_SIZE(NAND_SubPage_Size) | 
				NFC_TRAN_MODE_DATA_SEL_DMA);

		if(gEnableECC == 1)
		{
			//REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_ECC_TYPE_MSK);
			BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_MSK);
			
			//REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_ECC_TYPE_RS;
			BIT_SET(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_RS);
			BIT_SET(REG_NFC_TRAN_MODE,NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET);
		}

		// Set random access command
		w32(REG_NFC_RAND_ACC_CMD, NFC_RAND_ACC_CMD_CODE1(0xe0) | NFC_RAND_ACC_CMD_CODE0(0x05) |
				NFC_RAND_ACC_CMD_COL_ADDR( 16 * i + NAND_Page_Size));

		// Set data and oob DMA address
		w32(REG_NFC_DMA_ADDR, ((NAND_SubPage_Size * i + (UINT32)dwBuffer) & 0x1fffffff));

		// Set DMA Control
		w32(REG_NFC_DMA_CTRL, NFC_DMA_CTRL_READ |( NAND_SubPage_Size - 1));

		if(i == 0)        
		{   // SubPage 0:           0x80, col, row, data(512), 0x85, col, data(16)            
			// Set Row Address
			w32(REG_NFC_ROW_ADDR, dwPageAddr );
			// Set command
			w32(REG_NFC_CMD, NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT | NFC_CMD_ADDR_CYCLE_COL_ROW | 
					NFC_CMD_END_WAIT_BUSY_TO_RDY | NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0x30) | 
					NFC_CMD_CODE0(0x00));
		}
		else
		{
			// SubPage 1, 2, ... n: 0x85, col,      data(512), 0x85, col, data(16)           
			w32(REG_NFC_CMD, NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT |
					NFC_CMD_ADDR_CYCLE_COL | NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0xe0) | NFC_CMD_CODE0(0x05));
		}

#ifndef NFC_USE_INTERRUPT
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_CMD_COMPLETE);			// wait for command complete
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_DATA_TRAN_COMPLETE);	// wait for transfer complete
#endif
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_MEM_TRAN_COMPLETE);	// wait for system memory transfer complete   

		if(r32(REG_NFC_INT_STAT) & NFC_INT_ERR_RW)
		{
//			w32(REG_NFC_CFG1, r32(REG_NFC_CFG1) & (~NFC_CFG1_RS_ECC_ENABLE));
			BIT_CLEAR(REG_NFC_SYSCTRL2, (1<<10));
			return FSR_LLD_PREV_READ_ERROR ;
		}
		else
		{
			FSR_OAM_MEMCPY(((UINT32*)dwExtraBuffer + i * 16), (UINT32*)REG_NFC_XTRA_DATA0, 16);
		}
//		BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_RS_ECC_ENABLE);
	}

	return (r32(REG_NFC_INT_STAT) & NFC_INT_ERR) ? FSR_LLD_PREV_READ_ERROR : FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_ReadPage);
#endif

#if 0
UINT32 NFC_WritePage_FreeRun( PureNANDSpec *pstPNDSpec , UINT32 dwPageAddr,
		UINT8 *dwBuffer, UINT8 *dwExtraBuffer)
{
	UINT32  bResult = FSR_LLD_SUCCESS;
	UINT8 bStatus;
	UINT32  sNAND_Page_Size ;

	if(gEnableECC != 1)			//NFC_WritePage_FreeRun doesnt support ECC OFF
	{
		FSR_OAM_DbgMsg("NFC_WritePage_FreeRun doesnt support ECC OFF\n");
		return FSR_LLD_PREV_READ_ERROR;
	}

	sNAND_Page_Size = pstPNDSpec->nSctsPerPg * FSR_SECTOR_SIZE ;

	N_HALT_DEBUG("NFC_WritePage_FreeRun Page:[0x%08x]", dwPageAddr);
	NFC_NAND_REIN();

	//reset ecc decode
	BIT_SET(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));
	BIT_CLEAR(REG_NFC_CFG1, (NFC_CFG1_ECC_DECODE_RESET));

	// software reset host
	w32(REG_NFC_SW_RESET, 0x03);
	while(r32(REG_NFC_SW_RESET) & 0x03);

	BIT_SET(REG_NFC_SYSCTRL1 ,(1<<5) | (1<<2));

	w32(REG_NFC_INT_STAT, -1);  // Clean Interrupt Status

	// Set Row Address
	w32(REG_NFC_ROW_ADDR, dwPageAddr );

	// Set Col Address to subpage0 (0)
	w32(REG_NFC_COL_ADDR, 0);

	// Set DMA Destination
	w32(REG_NFC_DMA_ADDR, ((UINT32)dwBuffer) & 0x1fffffff );

	// Set XTRA Buffer
	w32(REG_NFC_XTRA_ADDR, ((UINT32)dwExtraBuffer) & 0x1fffffff );

	w32(REG_NFC_TRAN_MODE,NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(dwCE) | 
		NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE | NFC_TRAN_MODE_XTRA_DATA_COUNT_16 |
		NFC_TRAN_MODE_BLK_SIZE(sNAND_Page_Size) | NFC_TRAN_MODE_DATA_SEL_DMA | 
		NFC_TRAN_MODE_WRITE);

	BIT_SET(REG_NFC_TRAN_MODE , (NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET));

	//REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_ECC_TYPE_MSK);
	BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_MSK);
	//REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_ECC_TYPE_RS;
	BIT_SET(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_RS);

	// Set random access command
	w32(REG_NFC_RAND_ACC_CMD, NFC_RAND_ACC_CMD_CODE0(0x85) | NFC_RAND_ACC_CMD_COL_ADDR(sNAND_Page_Size));

	// Set command, Queer.....
	w32(REG_NFC_CMD , NFC_CMD_CE_IDX(dwCE) | NFC_CMD_WP_NEG | //NFC_CMD_FREERUN |
		NFC_CMD_DATA_PRESENT | NFC_CMD_ADDR_CYCLE_COL_OTHER | 
		NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x80));

	// Set DMA Control
	w32(REG_NFC_DMA_CTRL,  sNAND_Page_Size);  // Free Run mode does not need to -1

	// wait for transfer complete
#ifndef NFC_USE_INTERRUPT
	NFC_WaitSignal(NFC_INT_ERR | NFC_INT_DATA_TRAN_COMPLETE);	// wait for transfer complete
#endif
	NFC_WaitSignal(NFC_INT_ERR | NFC_INT_FREE_RUN_COMPLETE);	// wait for free run transfer complete   

	bResult = r32(REG_NFC_INT_STAT) & NFC_INT_ERR;
	if(bResult)
	{
		FSR_OAM_DbgMsg("NFC_WritePage_FreeRun Fail !!\n");
		return FSR_LLD_PREV_READ_ERROR;
	}

	// check status
	bStatus = NFC_ReadStatus();
	if(bStatus & 0x40)
	{// ready
		if(bStatus & 1)
		{// fail
			FSR_OAM_DbgMsg("Status Fail !!\n");
			return FSR_LLD_PREV_READ_ERROR;
		} // end of if(bStatus & 1)
	} // end of if(bStatus & 0x40)

	return FSR_LLD_SUCCESS;
}
#endif

ER NFC_WritePage_WholePage(unsigned char ucCE, ULONG dwPageAddr, UINT8 *pucBuffer, UINT8 *pucExtraBuffer, UINT8 ucECCType)
{
    unsigned char ucStatus = 0;

    ucECCType = NAND_ECC_TYPE_REEDSOLOMON;

	NFC_ControllerReset();
#if TEST_INTERRUPT_MODE
		REG_NFC_INT_ENABLE = 0xFFFFFFFF;
		REG_NFC_INT_STAT = 0xFFFFFFFF;
		NFC_Interrupt_Enable();
		SystemFlag = 1;
#endif

    // Set DMA Destination
    REG_NFC_DMA_ADDR = (unsigned long)pucBuffer;

    // Set XTRA Buffer
    REG_NFC_XTRA_ADDR = (unsigned long)pucExtraBuffer;

    REG_NFC_CFG1 = S_REG_NFC_CFG1 | NFC_CFG1_ECC_ENCODE_RESET;
    REG_NFC_CFG1 &= ~NFC_CFG1_ECC_ENCODE_RESET;

    REG_NFC_CFG0 = S_REG_NFC_CFG0_Program;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;
    REG_NFC_INT_STAT = 0xFFFFFFFF;

    // Set Row and Column Address
    REG_NFC_ROW_ADDR = dwPageAddr;
    REG_NFC_COL_ADDR = 0;

    REG_NFC_AHB_BURST_SIZE &= NFC_AHB_MASTER_MSK;
    REG_NFC_AHB_BURST_SIZE |= NFC_AHB_MASTER_16_8_4BEAT;

    REG_NFC_CFG0 &= ~NFC_CFG0_DATAWIDTH_16;
    REG_NFC_SYSCTRL = NFC_SYS_CTRL_EXTRA_SIZE(NandInfo.SpareSizePerSubPage) | NandInfo.ulPageType | NandInfo.ulBlockSizeType | NFC_SYS_CTRL_8_BIT_NAND;

    if(ucECCType == NAND_ECC_TYPE_REEDSOLOMON)
    {
        DEBUG_PRINTF("W RS\r\n");
        REG_NFC_SYSCTRL1 = NFC_SYS_CTRL1_FREE_RUN | NFC_SYS_CTRL1_NEW_VERSION | NFC_SYS_CTRL1_FREE_RUN_WRITE;
        //set 1: for new free-run read: RS and BCH one page 1 time
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_BCH16_ENABLE_NEW_ARCH;
        //set 1: disable "Remove extra 2 dummy bytes  of extra"
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_REMOVE_EXTRA_2DUMMY_BYTE_DIS;
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_DELAY_LATCH_1T;

        REG_NFC_TRAN_MODE = NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET |
                            NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(ucCE) |
                            NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE | NFC_TRAN_MODE_XTRA_DATA_COUNT_16 |
                            NFC_TRAN_MODE_BLK_SIZE(NandInfo.BytesPerPage) | NFC_TRAN_MODE_DATA_SEL_DMA |
                            NFC_TRAN_MODE_WRITE;

        REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_RS_ECC_ENABLE;
        REG_NFC_SYSCTRL2 = 0;
    }
#if NAND_BCH_SUPPORTED
    else if(ucECCType ==NAND_ECC_TYPE_BCH54)
    {
		DEBUG_PRINTF("W BCH54\r\n");
        REG_NFC_SYSCTRL1 = NFC_SYS_CTRL1_FREE_RUN | NFC_SYS_CTRL1_FREE_RUN_WRITE;
        //set 1: for new free-run read: RS and BCH one page 1 time
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_BCH16_ENABLE_NEW_ARCH;
        //set 1: disable "Remove extra 2 dummy bytes  of extra"
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_DELAY_LATCH_1T;

        REG_NFC_TRAN_MODE = NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET |
                            NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(ucCE) |
                            NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE | NFC_TRAN_MODE_XTRA_DATA_COUNT_16 |
                            NFC_TRAN_MODE_BLK_SIZE(NandInfo.BytesPerPage) | NFC_TRAN_MODE_DATA_SEL_DMA |
                            NFC_TRAN_MODE_WRITE | NFC_TRAN_MODE_ECC_ENABLE | NFC_TRAN_MODE_ECC_RESET;

        REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE; 
        REG_NFC_CFG1 |= NFC_CFG1_BCH_ECC_ENABLE | NFC_CFG1_BCH_ENABLE_PL;
        REG_NFC_CFG1 |= (NFC_CFG1_LITTLE_ENDIAN_XTRA | NFC_CFG1_LITTLE_ENDIAN);
        REG_NFC_SYSCTRL2 = 0;
        REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_BCH | NFC_SYS_CTRL_SUBPAGE_1024;
    }
    else if(ucECCType == NAND_ECC_TYPE_NONE_54)
    {
		DEBUG_PRINTF("W NONE BCH54\r\n");    
        REG_NFC_SYSCTRL1 = NFC_SYS_CTRL1_FREE_RUN | NFC_SYS_CTRL1_FREE_RUN_WRITE;
        //set 1: for new free-run read: RS and BCH one page 1 time
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_BCH16_ENABLE_NEW_ARCH;
        //set 1: disable "Remove extra 2 dummy bytes  of extra"
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_DELAY_LATCH_1T;

        REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;

        REG_NFC_TRAN_MODE = NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(ucCE) |
                            NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE | NFC_TRAN_MODE_XTRA_DATA_COUNT_16 |
                            NFC_TRAN_MODE_BLK_SIZE(NandInfo.BytesPerPage) | NFC_TRAN_MODE_DATA_SEL_DMA |
                            NFC_TRAN_MODE_WRITE | NFC_TRAN_MODE_ECC_ENABLE  | NFC_TRAN_MODE_ECC_RESET;

        REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;  
        REG_NFC_CFG1 |= (NFC_CFG1_LITTLE_ENDIAN_XTRA | NFC_CFG1_LITTLE_ENDIAN); 
        REG_NFC_CFG1 |= NFC_CFG1_BCH_ECC_ENABLE | NFC_CFG1_BCH_ENABLE_PL;

        REG_NFC_SYSCTRL2 |= 0x80; //RS off
        REG_NFC_SYSCTRL2 &= ~0x800; //It should be setted to 0 when BCH is selected.
        REG_NFC_SYSCTRL |= NFC_SYS_CTRL_ECC_BCH | NFC_SYS_CTRL_SUBPAGE_1024;
    }
#endif	
    else//(ucECCType == NAND_ECC_TYPE_NONE_16)
    {
		DEBUG_PRINTF("W NONE 16\r\n");
        REG_NFC_SYSCTRL1 = NFC_SYS_CTRL1_FREE_RUN | NFC_SYS_CTRL1_NEW_VERSION | NFC_SYS_CTRL1_FREE_RUN_WRITE;
        //set 1: for new free-run read: RS and BCH one page 1 time
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_BCH16_ENABLE_NEW_ARCH;
        //set 1: disable "Remove extra 2 dummy bytes  of extra"
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_REMOVE_EXTRA_2DUMMY_BYTE_DIS;
        REG_NFC_SYSCTRL1 |= NFC_SYS_CTRL1_DELAY_LATCH_1T;

	
        REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;    

        REG_NFC_TRAN_MODE = NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(ucCE) |
                            NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE | NFC_TRAN_MODE_XTRA_DATA_COUNT_16 |
                            NFC_TRAN_MODE_BLK_SIZE(NandInfo.BytesPerPage) | NFC_TRAN_MODE_DATA_SEL_DMA |
                            NFC_TRAN_MODE_WRITE;

        REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;  
        REG_NFC_CFG1 |= (NFC_CFG1_LITTLE_ENDIAN_XTRA | NFC_CFG1_LITTLE_ENDIAN); 

        REG_NFC_SYSCTRL2 |= 0x800; //RS off
    }

    // Set random access command
    REG_NFC_RAND_ACC_CMD = NFC_RAND_ACC_CMD_CODE0(0x85) | NFC_RAND_ACC_CMD_COL_ADDR(NandInfo.BytesPerPage);

    // Set command, Queer.....
    REG_NFC_CMD = NFC_CMD_CE_IDX(ucCE) | NFC_CMD_WP_NEG | NFC_CMD_WP_KEEP |
        NFC_CMD_DATA_PRESENT | NFC_CMD_ADDR_CYCLE_COL_OTHER |
        NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x80);

    REG_NFC_DMA_CTRL = NandInfo.BytesPerPage;   // Modify for Extra data lose when DMA busy  0906

    // wait for command complete
#if TEST_INTERRUPT_MODE
    while(SystemFlag) ;
#endif

    // wait for transfer complete
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR |NFC_INT_FREE_RUN_COMPLETE)));

    REG_NFC_INT_STAT = NFC_INT_ERR | NFC_INT_FREE_RUN_COMPLETE;

    REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_RS_ECC_ENABLE;

#if NAND_BCH_SUPPORTED
    REG_NFC_CFG1 = REG_NFC_CFG1 & ~NFC_CFG1_BCH_ECC_ENABLE;
#endif

    if((REG_NFC_INT_STAT & NFC_INT_ERR) != 0)
    {
        FSR_OAM_DbgMsg("%s:Program failed Status:0x%x\r\n", __FUNCTION__, REG_NFC_INT_STAT);
        return FSR_LLD_PREV_WRITE_ERROR;
    }

	while(1)
	{
	    // check status
	    ucStatus = NFC_ReadStatus(ucCE);
	    if(ucStatus & NAND_STATUS_READY)
	    {   // ready
	        if(ucStatus & NAND_STATUS_FAILED)
	        {   
				FSR_OAM_DbgMsg("%s:Status failed 0x%x !!\n", __FUNCTION__, ucStatus);
				return FSR_LLD_PREV_WRITE_ERROR;
	        } // end of if(bStatus & 1)
	        break;
	    } // end of if(bStatus & 0x40)
	}
    return FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_WritePage_WholePage);

#if 0
UINT32 NFC_WritePage( PureNANDSpec *pstPNDSpec , UINT32 dwPageAddr, UINT8 *dwBuffer, UINT8 *dwExtraBuffer)
{

	INT32   dwSubPagesPerPage = NAND_Page_Size/NAND_SubPage_Size;
	UINT32   i;    

	N_HALT_DEBUG("NFC_WritePage Page:[0x%08x]", dwPageAddr);
	NFC_NAND_REIN();

	w32(REG_NFC_SW_RESET, 0x03);
	while(r32(REG_NFC_SW_RESET) != 0);

	for(i = 0; i < dwSubPagesPerPage; ++i)    
	{
		FSR_OAM_MEMCPY((UINT32*)REG_NFC_XTRA_DATA0, dwExtraBuffer + i * 16,16);

		w32(REG_NFC_INT_STAT, -1);

		BIT_SET(REG_NFC_CFG1, (NFC_CFG1_ECC_ENCODE_RESET));
		BIT_CLEAR(REG_NFC_CFG1, (NFC_CFG1_ECC_ENCODE_RESET));

		w32(REG_NFC_DMA_ADDR, ((NAND_SubPage_Size * i + (UINT32)dwBuffer) & 0x1fffffff));

		w32(REG_NFC_DMA_CTRL, NFC_DMA_CTRL_TRAN_BYTE_COUNT(NAND_SubPage_Size));

		w32(REG_NFC_COL_ADDR, NAND_SubPage_Size * i);

		w32(REG_NFC_RAND_ACC_CMD, NFC_RAND_ACC_CMD_CODE0(0x85) | NFC_RAND_ACC_CMD_COL_ADDR(16* i + NAND_Page_Size));

		if(gEnableECC == 1)
		{
			//REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_ECC_TYPE_MSK);
			BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_MSK);
			
			//REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_ECC_TYPE_RS;
			BIT_SET(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_RS);

			w32(REG_NFC_TRAN_MODE, NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(dwCE) | NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE |
					NFC_TRAN_MODE_XTRA_DATA_COUNT_16 | NFC_TRAN_MODE_BLK_SIZE(NAND_SubPage_Size) | NFC_TRAN_MODE_ECC_ENABLE |
					NFC_TRAN_MODE_ECC_RESET | NFC_TRAN_MODE_DATA_SEL_DMA | NFC_TRAN_MODE_WRITE);

			w32(REG_NFC_SYSCTRL2, 0);
		}
		else
		{
			//REG_NFC_CFG1 = REG_NFC_CFG1 & ~(NFC_CFG1_ECC_TYPE_MSK);
			BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_MSK);
			
			//REG_NFC_CFG1 = REG_NFC_CFG1 | NFC_CFG1_ECC_TYPE_RS;
			BIT_SET(REG_NFC_CFG1, NFC_CFG1_ECC_TYPE_RS);
			w32(REG_NFC_TRAN_MODE, NFC_TRAN_MODE_KEEP_CE | NFC_TRAN_MODE_CE_IDX(dwCE) | NFC_TRAN_MODE_RAND_ACC_CMD_CYCLE_ONE |
					NFC_TRAN_MODE_XTRA_DATA_COUNT_16 | NFC_TRAN_MODE_BLK_SIZE(NAND_SubPage_Size) | NFC_TRAN_MODE_DATA_SEL_DMA |
					NFC_TRAN_MODE_WRITE);
		}

		if(i == 0)        
		{   
			// SubPage 0:           0x80, col, row, data(512), 0x85, col, data(16)            
			w32(REG_NFC_ROW_ADDR, dwPageAddr);
			w32(REG_NFC_CMD, NFC_TRAN_MODE_KEEP_CE | NFC_CMD_WP_NEG | NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT |
					NFC_CMD_ADDR_CYCLE_COL_ROW | NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x80));

		}        
		else        
		{   
			// SubPage 1, 2, ... n: 0x85, col,      data(512), 0x85, col, data(16)           
			w32(REG_NFC_CMD, NFC_TRAN_MODE_KEEP_CE | NFC_CMD_WP_NEG | NFC_CMD_CE_IDX(dwCE) | NFC_CMD_DATA_PRESENT |
					NFC_CMD_ADDR_CYCLE_COL | NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x85));

		}        

#ifndef NFC_USE_INTERRUPT
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_CMD_COMPLETE);			// wait for command complete
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_DATA_TRAN_COMPLETE);	// wait for transfer complete
#endif
		NFC_WaitSignal(NFC_INT_ERR | NFC_INT_MEM_TRAN_COMPLETE);	// wait for system memory transfer complete   

		if(r32(REG_NFC_INT_STAT) & NFC_INT_ERR)        
		{         
			//TODO add msg
			return FSR_LLD_PREV_WRITE_ERROR;        
		}
	}

	w32(REG_NFC_INT_STAT, -1);

	w32(REG_NFC_CMD, NFC_CMD_CE_IDX(dwCE) | NFC_CMD_END_WAIT_BUSY_TO_RDY | NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0x10));

	NFC_WaitSignal(NFC_INT_ERR | NFC_INT_CMD_COMPLETE);			// wait for command complete

//	BIT_CLEAR(REG_NFC_CFG1, NFC_CFG1_RS_ECC_ENABLE);

	if(!(r32(REG_NFC_INT_STAT) & NFC_INT_ERR)){
		// check status
		UINT8 bStatus = NFC_ReadStatus();
		if(bStatus & 0x40)
		{   // ready
			if(bStatus & 1)
			{   // fail
				//TODO add msg
				return FSR_LLD_PREV_WRITE_ERROR;
			} // end of if(bStatus & 1)
		} // end of if(bStatus & 0x40)
#ifdef RDUMP
		//TODO this
		if(0)
		{
			INT32          nLLDRe      = FSR_LLD_SUCCESS;             
			NFC_ReadPage(pstPNDSpec,dwPageAddr,bufMtmp,bufStmp);
		}
#endif
		return FSR_LLD_SUCCESS;
	}else{
		return FSR_LLD_PREV_WRITE_ERROR; 
	}

	return FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_WritePage);
#endif	


#if 0 
UINT32 NFC_EraseBlock( PureNANDSpec *pstPNDSpec , UINT32 dwPageAddr)
{
	UINT8 bStatus;

	UINT32 ret;

	//FSR_OAM_DbgMsg("NFC_EraseBlock: Block=%d\n", dwPageAddr/128);

	N_HALT_DEBUG("NFC_EraseBlock Page:[0x%08x]", dwPageAddr);
	NFC_NAND_REIN();

	w32(REG_NFC_INT_STAT,0xFFFFFFFF);

	w32(REG_NFC_ROW_ADDR,dwPageAddr);
	w32(REG_NFC_COL_ADDR,0x00);

	w32(REG_NFC_CMD,NFC_CMD_CE_IDX(dwCE) | NFC_CMD_WP_NEG | NFC_CMD_ADDR_CYCLE_ROW | 
			NFC_CMD_END_WAIT_BUSY_TO_RDY | NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0xd0) | 
			NFC_CMD_CODE0(0x60));

	// wait for command complete        
	while(!(ntk_readl(REG_NFC_INT_STAT) & (NFC_INT_ERR| NFC_INT_CMD_COMPLETE)));

	ret = (ntk_readl(REG_NFC_INT_STAT) & NFC_INT_ERR_RW);
	if (ret)
	{

		FSR_OAM_DbgMsg("[%s] Erase Block with dwPageAddr =%d FAILS\n" , __FUNCTION__, dwPageAddr);

		return FSR_LLD_PREV_ERASE_ERROR;
	}

	while (1)
	{
		// check status
		bStatus = NFC_ReadStatus();

		if(bStatus & 0x40){   // ready
			if(bStatus & 0x01){   // fail
				NFC_Reset();
				FSR_OAM_DbgMsg("[%s] Erase block %d FAILs, INT_STAT=%#lx, Erase Status %02x\n" , __FUNCTION__, 
						dwPageAddr/128, ntk_readl(REG_NFC_INT_STAT), bStatus);
				return FSR_LLD_PREV_ERASE_ERROR;
			} // end of if(bStatus & 1)
			else
			{
				//Erase Block OK.
				break;
			}
		} // end of if(bStatus & 0x40)
	} // while( 1 )

	return FSR_LLD_SUCCESS;
}
#endif

ER NFC_EraseBlock(unsigned char ucCE, ULONG dwPageAddr)
{
    ULONG   ret;
    unsigned char ucStatus = 0;
    REG_NFC_ROW_ADDR = dwPageAddr;
    REG_NFC_COL_ADDR = 0;
    REG_NFC_INT_STAT = -1;

#if TEST_INTERRUPT_MODE
	REG_NFC_INT_ENABLE = 0xFFFFFFFF;
    REG_NFC_INT_STAT = 0xFFFFFFFF;
    NFC_Interrupt_Enable();
    SystemFlag = 1;
#endif

    REG_NFC_CFG0 = S_REG_NFC_CFG0_Program;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;

    REG_NFC_CMD = NFC_CMD_WP_NEG | NFC_CMD_WP_KEEP | NFC_CMD_CE_IDX(ucCE) | NFC_CMD_ADDR_CYCLE_ROW |
        NFC_CMD_END_WAIT_BUSY_TO_RDY | NFC_CMD_CYCLE_TWO | NFC_CMD_CODE1(0xd0) |
        NFC_CMD_CODE0(0x60);

    // wait for command complete
#if TEST_INTERRUPT_MODE
    while(SystemFlag) ;
#endif
    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_CMD_COMPLETE)));

    if (REG_NFC_INT_STAT & NFC_INT_ERR_RW)
	{
		FSR_OAM_DbgMsg("[%s] Erase Block with dwPageAddr =%d FAILS\n" , __FUNCTION__, dwPageAddr);
		return FSR_LLD_PREV_ERASE_ERROR;
    }

	// check status
    while(1)
    {
		// check status
		ucStatus = NFC_ReadStatus(ucCE);
		if(ucStatus & NAND_STATUS_READY)
		{	// ready
			if(ucStatus & NAND_STATUS_FAILED)
			{	
				NFC_Reset(ucCE);
				printf("[%s] Erase block %d FAILs, INT_STAT=%#lx, Erase Status %02x\n" , __FUNCTION__, 
						dwPageAddr/NandInfo.PagesPerBlock, REG_NFC_INT_STAT, ucStatus);
                return FSR_LLD_PREV_ERASE_ERROR;
			} // end of if(bStatus & 1)
			break;
		} // end of if(bStatus & 0x40)
    }

    return FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_EraseBlock);

#if 0
UINT32 NFC_Reset(void)
{
	UINT32 ret;
	UINT32 temp;

	w32(REG_NFC_INT_STAT,0xFFFFFFFF);

	w32(REG_NFC_CMD,NFC_CMD_CE_IDX(dwCE) | NFC_CMD_END_WAIT_BUSY_TO_RDY | 
			NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0xff));

	temp = 0;

	// wait for command complete
	while( !(ntk_readl(REG_NFC_INT_STAT)&(NFC_INT_ERR | NFC_INT_CMD_COMPLETE)) )
	{
		delay_p1_us(1);
		temp++;

		if (temp > 500000)
		{
			break;
		}
	}

	ret = !(ntk_readl(REG_NFC_INT_STAT) & NFC_INT_ERR);

	if (ret){
		return 0;
	}else{
		return -1;
	}
}
#endif

ER NFC_Reset(unsigned char ucCE)
{   // 0xff, busy
	UINT32 temp;

	nfc_init();

	printk("REG_NFC_INT_STAT 0x%08x\n",REG_NFC_INT_STAT);
    REG_NFC_INT_STAT = 0xFFFFFFFF;
    REG_NFC_CFG0 = S_REG_NFC_CFG0_Program;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune;

    REG_NFC_CMD = NFC_CMD_CE_IDX(ucCE) | NFC_CMD_END_WAIT_BUSY_TO_RDY | NFC_CMD_WP_NEG | NFC_CMD_WP_KEEP | NFC_CMD_CYCLE_ONE | NFC_CMD_CODE0(0xff);

    while(!(REG_NFC_INT_STAT & (NFC_INT_ERR | NFC_INT_CMD_COMPLETE)))  // wait for command complete
	{
		delay_us(1);
		temp++;

		if (temp > 1000000)
		{
			break;
		}
	}

    if (!(REG_NFC_INT_STAT & NFC_INT_ERR))
    {
        return FSR_LLD_SUCCESS;
    }
    else
    {
        return -1;
    }
}
EXPORT_SYMBOL(NFC_Reset);

void NFC_ControllerReset(void)
{
#if 0
	unsigned int uiLoop;
    REG_NFC_RESET = REG_NFC_RESET |(( 0x01 )<<25 ) ;
    for(uiLoop=0 ; uiLoop < 1000 ; uiLoop++);
    	REG_NFC_RESET =   REG_NFC_RESET & (~(( 0x01 )<<25 )) ;

    REG_NFC_CFG0 =  ( S_REG_NFC_CFG0_Read) ; //& 0x00FEFF0F) | 0x32000020; // 0x11100133 ;
    REG_NFC_CFG1 = S_REG_NFC_CFG1 ;
    REG_NFC_SYSCTRL = S_REG_NFC_SYSCTRL;
    REG_NFC_SYSCTRL1 = S_REG_NFC_SYSCTRL1 ; //| 0x800 ;
    REG_NFC_SYSCTRL2 = S_REG_NFC_SYSCTRL2 ;
    REG_NFC_Fine_Tune = S_REG_NFC_Fine_Tune ; //| 0xc0 ; //S_REG_NFC_Fine_Tune;
#else	
	REG_NFC_SW_RESET = 0x7;
	while(REG_NFC_SW_RESET & 0x07);
#endif
}

ER NFC_SetType(unsigned char ucCE, NANDIDData *pNandID)
{
#define UNKNOWN_ID       0
    unsigned char ucFlashListIndex = 0;
	
    const NAND_CFG NandInfoTable[] = {  
		  //       FlashID                           BlocksPerChip  PagesPerBlock  BytesPerPage  BytesPerSubPage  SpareSizePerPage  SpareSizePerSubPage  ProgramableSizeInSpare       ulPageType               ulBlockSizeType         CMDType                  ECCType                NumberOfChips  DataBusWidth  RowAddrCycleCount  ColAddrCycleCount  bRandomCMD   (*pNFC_ReadPage)             (*pNFC_WritePage)
		/* KF92G16Q2X */ /* KF92G16Q2W */
		{{0xEC, 0xBA, UNKNOWN_ID, UNKNOWN_ID},              2048,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* KF94G16Q2W */                                                                                                                                               
		{{0xEC, 0xBC, UNKNOWN_ID, UNKNOWN_ID},              4096,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* MT29F2G08ABAEAWP  */                                                                                                                                        
		{{0x2C, 0xDA, UNKNOWN_ID, UNKNOWN_ID},              2048,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* MT29F4G08ABAEAWP  */                                                                                                                                        
		{{0x2C, 0xDC, UNKNOWN_ID, UNKNOWN_ID},              4096,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* K9F2G08UXA */                                                                                                                                               
		{{0xEC, 0xDA, 0x10, 0x95},                          2048,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* K9F1G08U0D */                                                                                                                                               
		{{0xEC, 0xF1, 0x80, 0x95},                          1024,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* MT29F2GxxAxxEAxx */                                                                                                                                     
		{{0x2C, 0xDA, UNKNOWN_ID, UNKNOWN_ID},              2048,           64,           2048,          512,            64,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_2_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},

#ifdef FPGA_CVT_ONLY
		/* K9F1208U0D*/
		{{0xEC, 0x76, 0x5A, 0x3F},                          1024,           32,            512,          512,            16,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_1_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              3,                  1,          FALSE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* K9F5608U0D*/ 
		{{0xEC, 0x75, 0xA5, 0xBD},                          2048,           32,            512,          512,            16,                 16,                   6,             NFC_SYS_CTRL_PAGE_2048,   NFC_SYS_CTRL_BLK_128K,  NAND_CMD_TYPE_1_CMD,   NAND_ECC_TYPE_REEDSOLOMON,          1,             8,              2,                  1,          FALSE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
		/* H27UBG8T2A*/ 
		{{0xAD, 0x75, 0xAD, 0x75},                          1024,          256,           8192,         1024,        (8*54),                 54,                  12,             NFC_SYS_CTRL_PAGE_8192,     NFC_SYS_CTRL_BLK_1M,  NAND_CMD_TYPE_2_CMD,         NAND_ECC_TYPE_BCH54,          1,             8,              3,                  2,           TRUE,      NFC_ReadPage_WholePage,   NFC_WritePage_WholePage},
#endif
		/* Should be the last item in the flash list*/
		{{UNKNOWN_ID, UNKNOWN_ID, UNKNOWN_ID, UNKNOWN_ID},     0,            0,              0,            0,             0,                  0,                   0,                        0,                        0,                   0,                        0,                       0,             0,              0,                  0,              0,                  0,                                  0},
		};                          
	

	NandInfo.BlocksPerChip = 0;
	for (ucFlashListIndex = 0 ; NandInfoTable[ucFlashListIndex].NANDFlashID.ManufactureID != 0 ; ucFlashListIndex++)
	{
		if ((pNandID->nMID == NandInfoTable[ucFlashListIndex].NANDFlashID.ManufactureID) &&
				(pNandID->nDID == NandInfoTable[ucFlashListIndex].NANDFlashID.DeviceID))
		{
		    NandInfo.NANDFlashID.ManufactureID = NandInfoTable[ucFlashListIndex].NANDFlashID.ManufactureID;
			NandInfo.NANDFlashID.DeviceID = NandInfoTable[ucFlashListIndex].NANDFlashID.DeviceID;
			NandInfo.NANDFlashID.Byte2 = NandInfoTable[ucFlashListIndex].NANDFlashID.Byte2;
			NandInfo.NANDFlashID.Byte3 = NandInfoTable[ucFlashListIndex].NANDFlashID.Byte3;

	        NandInfo.BlocksPerChip = NandInfoTable[ucFlashListIndex].BlocksPerChip;           
	        NandInfo.PagesPerBlock = NandInfoTable[ucFlashListIndex].PagesPerBlock;           
	        NandInfo.BytesPerPage =  NandInfoTable[ucFlashListIndex].BytesPerPage;            
	        NandInfo.BytesPerSubPage = NandInfoTable[ucFlashListIndex].BytesPerSubPage;       
	        NandInfo.SpareSizePerPage = NandInfoTable[ucFlashListIndex].SpareSizePerPage;     
	        NandInfo.SpareSizePerSubPage = NandInfoTable[ucFlashListIndex].SpareSizePerSubPage;
			NandInfo.ProgramableSizeInSpare = NandInfoTable[ucFlashListIndex].ProgramableSizeInSpare;
			NandInfo.ulPageType =NandInfoTable[ucFlashListIndex].ulPageType;				  
			NandInfo.ulBlockSizeType = NandInfoTable[ucFlashListIndex].ulBlockSizeType; 	  
	        NandInfo.CMDType = NandInfoTable[ucFlashListIndex].CMDType;                       
	        NandInfo.ECCType = NandInfoTable[ucFlashListIndex].ECCType;                       
	        NandInfo.NumberOfChips = NandInfoTable[ucFlashListIndex].NumberOfChips;   
	        NandInfo.DataBusWidth = NandInfoTable[ucFlashListIndex].DataBusWidth;             
	        NandInfo.ColAddrCycleCount = NandInfoTable[ucFlashListIndex].ColAddrCycleCount;   
	        NandInfo.RowAddrCycleCount = NandInfoTable[ucFlashListIndex].RowAddrCycleCount;   
	        NandInfo.bRandomCMD = NandInfoTable[ucFlashListIndex].bRandomCMD;                 
	        NandInfo.pNFC_WritePage = NandInfoTable[ucFlashListIndex].pNFC_WritePage;         
	        NandInfo.pNFC_ReadPage = NandInfoTable[ucFlashListIndex].pNFC_ReadPage;           
			break;
		}
		/* else continue */
	}


	if(NandInfo.BlocksPerChip == 0)
	{	
		printf("%s:Flash Init failed\r\n", __FUNCTION__);
		return FSR_LLD_OPEN_FAILURE;
	}
	
	printf("Block size:%d", NandInfo.BytesPerPage * NandInfo.PagesPerBlock);
	printf("\r\n");

	printf("Page size:%d", NandInfo.BytesPerPage);
	printf("\r\n");

	printf("Spare size:%d", NandInfo.SpareSizePerPage);
	printf("\r\n");

	printf("Pages per block:%d", NandInfo.PagesPerBlock);
	printf("\r\n");

	printf("Sub page size:%d", NandInfo.BytesPerSubPage);
	printf("\r\n");

	printf("Sub size per sub page:%d", NandInfo.SpareSizePerSubPage);
	printf("\r\n");

	REG_NFC_CFG0 = REG_NFC_CFG0 & ~(1<<2);

	REG_NFC_CFG0 &= ~0x3;
	if(NandInfo.RowAddrCycleCount == 3) 	   // Row address cycle count
		REG_NFC_CFG0 |= NFC_CFG0_ROW_ADDR_3CYCLES;		  // Row address cycle count
	else
		REG_NFC_CFG0 |= NFC_CFG0_ROW_ADDR_2CYCLES;		  // Row address cycle count

	if(NandInfo.ColAddrCycleCount == 2) 	   // Column address cycle count
		REG_NFC_CFG0 |= NFC_CFG0_COL_ADDR_2CYCLES;		  // Row address cycle count
	else
		REG_NFC_CFG0 |= NFC_CFG0_COL_ADDR_1CYCLES;		  // Row address cycle count

	REG_NFC_CFG1 = NFC_CFG1_READY_TO_BUSY_TIMEOUT(-1) | NFC_CFG1_LITTLE_ENDIAN_XTRA |
		NFC_CFG1_LITTLE_ENDIAN | NFC_CFG1_BUSY_TO_READY_TIMEOUT(-1);

	REG_NFC_CFG1 = REG_NFC_CFG1 & ~(1<<17);
	REG_NFC_CFG1 = REG_NFC_CFG1 & ~(1<<21);

	REG_NFC_Fine_Tune = NAND_FINETUNE_TIMING;

	S_REG_NFC_CFG0_Program = ((REG_NFC_CFG0) & ~(0xFFFFFFF0)) | NAND_PROGRAM_TIMING;
	S_REG_NFC_CFG0_Read = (REG_NFC_CFG0 & ~(0xFFFFFFF0)) | NAND_READ_TIMING;

#if NAND_NFC_HARDRESET_USED

	S_REG_NFC_CFG0 = REG_NFC_CFG0;
	S_REG_NFC_CFG1 = REG_NFC_CFG1;
	S_REG_NFC_SYSCTRL = REG_NFC_SYSCTRL;
	S_REG_NFC_SYSCTRL1 = REG_NFC_SYSCTRL1 ;
	S_REG_NFC_SYSCTRL2 = REG_NFC_SYSCTRL2 ;
	S_REG_NFC_Fine_Tune = REG_NFC_Fine_Tune ;
#endif

	printf("Program Timing:%x\r\n", S_REG_NFC_CFG0_Program);
	printf("Read Timing:%x\r\n", S_REG_NFC_CFG0_Read);
	printf("Tine TUNE Timing:%x\r\n", NAND_FINETUNE_TIMING);

	return FSR_LLD_SUCCESS;
}
EXPORT_SYMBOL(NFC_SetType);

#if 0
UINT32 NFC_WaitSignal(UINT32 NFC_signalType)
{
#ifdef NFC_USE_INTERRUPT
	BIT_SET(REG_NFC_INT_ENABLE,NFC_signalType);
#endif
	while(!(r32(REG_NFC_INT_STAT) & (NFC_signalType)))
	{
#ifdef NFC_USE_INTERRUPT
		down(&NFC_IRQ);
#endif
	}
	return 0;
}
#endif

#ifdef NFC_USE_INTERRUPT
irqreturn_t NFC_IRQhandler(int irq, void *dev_id)
{
	//FSR_OAM_DbgMsg("\nS:0x%08x E:0x%08x",r32(REG_NFC_INT_STAT),r32(REG_NFC_INT_ENABLE));
	w32(REG_NFC_INT_ENABLE,(~(r32(REG_NFC_INT_STAT))));
	up(&NFC_IRQ);
	return IRQ_HANDLED;
}

UINT32 NFC_IRQInit(UINT32 INT_FLAG)
{
	if(gNFC_IRQInit == 0)
	{
		if(request_irq (NFC_568_INT ,NFC_IRQhandler, IRQF_DISABLED, "NFC", NULL))
		{
			FSR_OAM_DbgMsg("Warning : Error in request nvt NAND irq!\n");
			return -1;
		}
		else
		{
			FSR_DBZ_DBGMOUT(FSR_DBZ_LLD_LOG,
					(TEXT("NAND IRQ done\r\n")));
			gNFC_IRQInit = 1;
		}
	}

	//hardware setting
	BIT_SET(REG_NFC_INT_ENABLE, NFC_INT_ENABLE);

	BIT_CLEAR(REG_IRQ_MASK, NFC_568_INT_BIT);		//clear mask
	BIT_CLEAR(REG_IRQ_TYPE, NFC_568_INT_BIT);		//set to level trigger

	return 0;
}
#endif // #ifdef NFC_USE_INTERRUPT

//============================================================
//		Debug Function
//============================================================
void NFC_DumpReg(void)
{
    unsigned long ulRegIndex = REG_NFC_BASE;

    printf("\r\nDump NFC register\r\n");

    printf("REG_NFC_CFG0            (0x00)0x%x\r\n", REG_NFC_CFG0);    
    printf("REG_NFC_CFG1            (0x04)0x%x\r\n", REG_NFC_CFG1);    
    printf("REG_NFC_CMD             (0x08)0x%x\r\n", REG_NFC_CMD);     
    printf("REG_NFC_TRAN_MODE       (0x0C)0x%x\r\n", REG_NFC_TRAN_MODE);
    printf("REG_NFC_COL_ADDR        (0x10)0x%x\r\n", REG_NFC_COL_ADDR);
    printf("REG_NFC_ROW_ADDR        (0x14)0x%x\r\n", REG_NFC_ROW_ADDR);
                                                                     
    printf("REG_NFC_RAND_ACC_CMD    (0x1C)0x%x\r\n", REG_NFC_RAND_ACC_CMD);
    printf("REG_NFC_INT_ENABLE      (0x20)0x%x\r\n", REG_NFC_INT_ENABLE);
    printf("REG_NFC_INT_STAT        (0x24)0x%x\r\n", REG_NFC_INT_STAT);
                                                                 
    printf("REG_NFC_DIRCTRL         (0x40)0x%x\r\n", REG_NFC_DIRCTRL); 
    printf("REG_NFC_STAT            (0x44)0x%x\r\n", REG_NFC_STAT);    
    printf("REG_NFC_DMA_ADDR        (0x50)0x%x\r\n", REG_NFC_DMA_ADDR);
    printf("REG_NFC_DMA_CTRL        (0x54)0x%x\r\n", REG_NFC_DMA_CTRL);
    printf("REG_NFC_SYSCTRL         (0x5c)0x%x\r\n", REG_NFC_SYSCTRL); 
                                                                     
    printf("REG_NFC_AHB_BURST_SIZE  (0x78)0x%x\r\n", REG_NFC_AHB_BURST_SIZE);
                                                                     
    printf("REG_NFC_XTRA_ADDR       (0x104)0x%x\r\n", REG_NFC_XTRA_ADDR);
    printf("REG_NFC_SYSCTRL1        (0x10C)0x%x\r\n", REG_NFC_SYSCTRL1);
    printf("REG_NFC_Fine_Tune       (0x110)0x%x\r\n", REG_NFC_Fine_Tune);
    printf("NFC_ERR_CNT0            (0x114)0x%x\r\n", NFC_ERR_CNT0);    
    printf("NFC_ERR_CNT1            (0x118)0x%x\r\n", NFC_ERR_CNT1);    
    printf("REG_NFC_SYSCTRL2        (0x11C)0x%x\r\n", REG_NFC_SYSCTRL2);    
}
EXPORT_SYMBOL(NFC_DumpReg);

UINT NFC_GetECCBitsAndPos_RS(PECC_INFO pucErrorBitArray)
{
    UCHAR ucErrorCnt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    BOOL  bUncorrectable[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char i;
    unsigned char ucMaxECN = 0;
    unsigned char subpage_cnt = NandInfo.BytesPerPage / NandInfo.BytesPerSubPage;

    for ( i = 0; i< subpage_cnt ; i++)
    {
        if((i % 2 ) == 0)
        {

            ucErrorCnt[i] = (NFC_ERR_CNT0 >> (5 * (i / 2))) & 0x07;
            if(((NFC_ERR_CNT0 >> ((5 * (i / 2)) + 3)) & 0x03) == 0x2)
            {
                bUncorrectable[i] = TRUE;
            }
            else
                bUncorrectable[i] = FALSE;
        }
        else
        {
            ucErrorCnt[i] = (NFC_ERR_CNT1 >> (5 * (i / 2))) & 0x07;
            if(((NFC_ERR_CNT1 >> ((5 * (i / 2)) + 3)) & 0x03) == 0x2)
            {
                bUncorrectable[i] = TRUE;
            }
            else
                bUncorrectable[i] = FALSE;
        }
    }

    for ( i = 0 ; i < subpage_cnt ; i++ )
    {
        if(ucMaxECN < ucErrorCnt[i])
            ucMaxECN = ucErrorCnt[i];
    }

    if(pucErrorBitArray != NULL)
    {
        for ( i = 0; i< subpage_cnt ; i++)
        {
            pucErrorBitArray[i].usSymbolCount = ucErrorCnt[i];
            pucErrorBitArray[i].bUncorrectable = bUncorrectable[i];
        }
    }
    return ucMaxECN;
}

//return the maximum error bit of all decode unit
UINT NFC_GetECCBitsAndPos_BCH54(PECC_INFO pucErrorBitArray)
{
    UCHAR ucErrorCnt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    BOOL  bUncorrectable[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char i;
    unsigned char ucMaxECN = 0;
    unsigned char subpage_cnt = NandInfo.BytesPerPage / NandInfo.BytesPerSubPage;

    for ( i = 0; i< subpage_cnt ; i++)
    {
        if((i & 1 ) == 0)
        {

            ucErrorCnt[i] = (NFC_ERR_CNT0 >> (5 * (i / 2))) & 0x1F;
            if(((NFC_ERR_CNT0 >> ((2 * (i / 2)) + 24)) & 0x03) == 0x2)
            {
                bUncorrectable[i] = TRUE;
            }
            else
                bUncorrectable[i] = FALSE;
        }
        else
        {
            ucErrorCnt[i] = (NFC_ERR_CNT1 >> (5 * (i / 2))) & 0x1F;

            if(((NFC_ERR_CNT1 >> ((2 * (i / 2)) + 24)) & 0x03) == 0x2)
            {
                bUncorrectable[i] = TRUE;
            }
            else
                bUncorrectable[i] = FALSE;
        }
    }

    for ( i = 0 ; i < subpage_cnt ; i++ )
    {
        if(ucMaxECN < ucErrorCnt[i])
            ucMaxECN = ucErrorCnt[i];
    }

    if(pucErrorBitArray != NULL)
    {
        for ( i = 0; i< subpage_cnt ; i++)
        {
            pucErrorBitArray[i].usSymbolCount = ucErrorCnt[i];
            pucErrorBitArray[i].bUncorrectable = bUncorrectable[i];
        }
    }
    return ucMaxECN;
}

//return the maximum error bit of all decode unit
UINT NFC_GetECCBitsAndPos(PECC_INFO pucErrorBitArray)
{
    if(NandInfo.ECCType == NAND_ECC_TYPE_REEDSOLOMON)
    {
        return NFC_GetECCBitsAndPos_RS(pucErrorBitArray);
    }
    else if(NandInfo.ECCType == NAND_ECC_TYPE_BCH54)
    {
        return NFC_GetECCBitsAndPos_BCH54(pucErrorBitArray);
    }

    return 0xFFFFFFFF;
}

EXPORT_SYMBOL(NFC_GetECCBitsAndPos);

void NFC_SelfRWTest(unsigned char ucCE, unsigned int uiTimes, unsigned int uiStartBlock, unsigned int uiEndBlock)
{
	unsigned long BufForWrite[512];
	unsigned long BufForRead[512];
	unsigned long SpareForWrite[16];
	unsigned long SpareForRead[16];
	unsigned long ulBlockIndex;
	unsigned long ulPageIndex;
	unsigned int  uiLoopIndex = 0;

	FSR_OAM_DbgMsg("BufForWrite:0x%x\r\n", &BufForWrite[0]);
	FSR_OAM_DbgMsg("BufForRead:0x%x\r\n", &BufForRead[0]);
	FSR_OAM_DbgMsg("SpareForWrite:0x%x\r\n", &SpareForWrite[0]);
	FSR_OAM_DbgMsg("SpareForRead:0x%x\r\n", &SpareForRead[0]);

	for(uiLoopIndex = 0; uiLoopIndex < uiTimes; uiLoopIndex++)
	{
		FSR_OAM_DbgMsg("Testing %d\r\n", uiLoopIndex);

		FSR_OAM_DbgMsg("Start to first stage testing\r\n");
		for(ulBlockIndex = uiStartBlock ; ulBlockIndex < uiEndBlock ; ulBlockIndex++)
		{
			FSR_OAM_DbgMsg("Block:%d\r\n", ulBlockIndex);
			NFC_EraseBlock(ucCE, ulBlockIndex * NandInfo.PagesPerBlock);
			for(ulPageIndex = 0; ulPageIndex < NandInfo.PagesPerBlock ; ulPageIndex++)
			{
				FSR_OAM_DbgMsg("     Page:%d\r\n", ulPageIndex);
				memset((unsigned char*)&BufForWrite[0], 0x55, 2048);
				memset((unsigned char*)&SpareForWrite[0], 0xCC, 64);
				memset((unsigned char*)&SpareForRead[0], 0x77, 64);

				SpareForWrite[0] = 0xCCCCCCFF;
				BufForWrite[0] = ulBlockIndex;
				BufForWrite[1] = ulPageIndex;

				NFC_WritePage_WholePage(ucCE, ulBlockIndex * NandInfo.PagesPerBlock + ulPageIndex, (unsigned char*) &BufForWrite[0], (unsigned char*) &SpareForWrite[0], NAND_ECC_TYPE_REEDSOLOMON); 
				NFC_ReadPage_WholePage(ucCE, ulBlockIndex * NandInfo.PagesPerBlock + ulPageIndex, (unsigned char*) &BufForRead[0], (unsigned char*) &SpareForRead[0], NAND_ECC_TYPE_REEDSOLOMON);

				if(memcmp((unsigned char*) &BufForWrite[0], (unsigned char*) &BufForRead[0], 2048) != 0)
				{
					FSR_OAM_DbgMsg("Data mismatch\r\n");
					FSR_OAM_DbgMsg("Write\r\n");
				    hex_dump((unsigned char*) &BufForWrite[0], 2048);

					FSR_OAM_DbgMsg("\r\nRead\r\n");
					hex_dump((unsigned char*) &BufForRead[0], 2048);
					
				}

				if(memcmp((unsigned char*) &SpareForWrite[0], (unsigned char*) &SpareForRead[0], 6) != 0)
				{
					FSR_OAM_DbgMsg("Spare mismatch\r\n");
					FSR_OAM_DbgMsg("Write\r\n");
					hex_dump((unsigned char*) &SpareForWrite[0], 6);
				
					FSR_OAM_DbgMsg("\r\nRead\r\n");
					hex_dump((unsigned char*) &SpareForRead[0], 6);

					while(1);
				}
			}
		}

		FSR_OAM_DbgMsg("Start to second stage testing\r\n");
		for(ulBlockIndex = uiStartBlock ; ulBlockIndex < uiEndBlock ; ulBlockIndex++)
		{
			FSR_OAM_DbgMsg("Block:%d\r\n", ulBlockIndex);
			for(ulPageIndex = 0; ulPageIndex < NandInfo.PagesPerBlock ; ulPageIndex++)
			{
				FSR_OAM_DbgMsg("     Page:%d\r\n", ulPageIndex);
				memset((unsigned char*)&BufForWrite[0], 0x55, 2048);
				memset((unsigned char*)&SpareForWrite[0], 0xCC, 64);
				memset((unsigned char*)&SpareForRead[0], 0x77, 64);

				SpareForWrite[0] = 0xCCCCCCFF;
				BufForWrite[0] = ulBlockIndex;
				BufForWrite[1] = ulPageIndex;

				NFC_ReadPage_WholePage(ucCE, ulBlockIndex * NandInfo.PagesPerBlock + ulPageIndex, (unsigned char*) &BufForRead[0], (unsigned char*) &SpareForRead[0], NAND_ECC_TYPE_REEDSOLOMON);

				if(memcmp((unsigned char*) &BufForWrite[0], (unsigned char*) &BufForRead[0], 2048) != 0)
				{
					FSR_OAM_DbgMsg("Data mismatch\r\n");
					FSR_OAM_DbgMsg("Write\r\n");
				    hex_dump((unsigned char*) &BufForWrite[0], 2048);

					FSR_OAM_DbgMsg("\r\nRead\r\n");
					hex_dump((unsigned char*) &BufForRead[0], 2048);
					
				}

				if(memcmp((unsigned char*) &SpareForWrite[0], (unsigned char*) &SpareForRead[0], 6) != 0)
				{
					FSR_OAM_DbgMsg("Spare mismatch\r\n");
					FSR_OAM_DbgMsg("Write\r\n");
					hex_dump((unsigned char*) &SpareForWrite[0], 6);
				
					FSR_OAM_DbgMsg("\r\nRead\r\n");
					hex_dump((unsigned char*) &SpareForRead[0], 6);

					while(1);
				}
			}
		}
	}
	FSR_OAM_DbgMsg("Testing finished\r\n");
}




#define ASCII_CODE(x)	((x >= 32) && (x <= 127) ? x : '.')
void hex_dump(UINT8 *pu8Data, UINT32 u32Length)
{
	int 	nIndex;
	UINT8	u8Index;
	UINT32	u32Count = 0;

	FSR_OAM_DbgMsg("\n----------------------- Buffer Addr -> %lX---------------------------",  (UINT32)pu8Data);
	FSR_OAM_DbgMsg("\n  offset  ");
	for(u8Index = 0; u8Index < 16; u8Index++)
		FSR_OAM_DbgMsg("%02X ", u8Index);
	for(nIndex = 0; (UINT32)nIndex < u32Length; nIndex += 16)
	{
		FSR_OAM_DbgMsg("\n%09X ",  nIndex);
		for(u8Index = 0; u8Index < 16; u8Index++, u32Count++)
		{
			if((UINT32)(nIndex + u8Index) < u32Length)
				FSR_OAM_DbgMsg("%02X ", pu8Data[nIndex + u8Index]);
			else if(u32Count < u32Length)
				FSR_OAM_DbgMsg("00 ");
			else FSR_OAM_DbgMsg("   ");
		}
		for(u8Index = 0; u8Index < 16; u8Index++)
		{
			if((UINT32)(nIndex + u8Index) < u32Length)
				FSR_OAM_DbgMsg("%c", ASCII_CODE(pu8Data[nIndex + u8Index]));
			else if(u32Count < u32Length)
				FSR_OAM_DbgMsg(".");
			else FSR_OAM_DbgMsg(" ");
		}
	}
	FSR_OAM_DbgMsg("\n");
}

