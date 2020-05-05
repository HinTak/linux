#ifndef __NANDDEF_H__
#define __NANDDEF_H__

//#include "Type.h"

#define   NAND_ACP_ENABLE    0
#define   NAND_BCH_SUPPORTED 0 

#if 1
#define NAND_PROGRAM_TIMING  0x44440110
#define NAND_READ_TIMING     0x44440120
#define NAND_FINETUNE_TIMING 0x0
#else
#define NAND_PROGRAM_TIMING  0x10100110
#define NAND_READ_TIMING     0x72100120
#define NAND_FINETUNE_TIMING 0xC0C0
#endif

typedef enum
{
    NAND_CMD_TYPE_1_CMD = 0,
    NAND_CMD_TYPE_2_CMD
}CMD_TYPE;

typedef struct ECC_INFO
{
    unsigned short usErrorPos[24];
    unsigned short usSymbolCount;
    BOOL           bUncorrectable;
}ECC_INFO, *PECC_INFO; //Sub page unit

typedef struct CHIP_ID
{
    unsigned char  ManufactureID;
    unsigned char  DeviceID;
    unsigned char  Byte2;
    unsigned char  Byte3;
}CHIP_ID, *PCHIP_ID;


typedef struct tagNFCHIP_CFG
{
	CHIP_ID        NANDFlashID;
    unsigned long  BlocksPerChip;
    unsigned long  PagesPerBlock;
    unsigned long  BytesPerPage;
    unsigned long  BytesPerSubPage;
    unsigned long  SpareSizePerPage;
    unsigned long  SpareSizePerSubPage;
    unsigned long  ProgramableSizeInSpare;
    unsigned long  ulPageType;
    unsigned long  ulBlockSizeType;
    unsigned char  CMDType;
    unsigned char  ECCType;
    unsigned char  NumberOfChips;
    unsigned char  DataBusWidth;
    unsigned char  RowAddrCycleCount;
    unsigned char  ColAddrCycleCount;
    unsigned char  bRandomCMD;

    ER (*pNFC_ReadPage)(unsigned char ucCE, UINT32 dwPageAddr, UINT8 *dwBuffer, UINT8 *dwExtraBuffer, UINT8 ucECCType);
    ER (*pNFC_WritePage)(unsigned char ucCE, ULONG dwPageAddr, UINT8 *dwBuffer, UINT8 *dwExtraBuffer, UINT8 ucECCType);
}NAND_CFG, *PNAND_CFG;

//****************NAND status register
#define NAND_STATUS_READY     0x40
#define NAND_STATUS_FAILED    0x1


#define NAND_ERR_OK           0x000000000
#define NAND_ERR_UNCORECTABLE 0x000000001
#define NAND_ERR_PROGRAM      0x000000002

#define NAND_ERR_SYSTEM       0x000000010



#endif
