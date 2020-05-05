#ifndef _FSR_LLD_NTK_NFC_FUNCTION_
#define _FSR_LLD_NTK_NFC_FUNCTION_
#include    <FSR.h>
#include    "Type.h"

#define ER unsigned int

typedef enum
{
    NAND_ECC_TYPE_HAMMING = 0,
    NAND_ECC_TYPE_REEDSOLOMON,
    NAND_ECC_TYPE_BCH8,
    NAND_ECC_TYPE_BCH27,
    NAND_ECC_TYPE_BCH54,
    NAND_ECC_TYPE_NONE_16,
    NAND_ECC_TYPE_NONE_54
}ECC_TYPE;

typedef struct
{
	UINT8           nMID;
	UINT8           nDID;
	UINT8           n3rdIDData;
	UINT8           n4thIDData;
	UINT8           n5thIDData;
	UINT8           nPad0;
	UINT16          nPad1;
} NANDIDData;

ER NFC_Init(unsigned char ucCE);
ER NFC_ReadID(unsigned char ucCE, NANDIDData* pstNANDID);
ER NFC_SetType(unsigned char ucCE, NANDIDData* pNandID);
ER NFC_ReadPage_WholePage(unsigned char ucCE, UINT32 dwPageAddr, UINT8 *dwBuffer, UINT8 *dwExtraBuffer, UINT8 ucECCType);
ER NFC_WritePage_WholePage(unsigned char ucCE, ULONG dwPageAddr, UINT8 *dwBuffer, UINT8 *dwExtraBuffer, UINT8 ucECCType);
ER NFC_EraseBlock(unsigned char ucCE, ULONG dwPageAddr);
ER NFC_Reset(unsigned char ucCE);
void NFC_SelfRWTest(unsigned char ucCE, unsigned int uiTimes, unsigned int uiStartBlock, unsigned int uiEndBlock);

void hex_dump(UINT8 *pu8Data, UINT32 u32Length);

#endif
