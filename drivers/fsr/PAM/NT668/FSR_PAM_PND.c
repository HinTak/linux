#include "FSR.h"
#include "nvt_PND.h"

#define     DBG_PRINT(x)            FSR_DBG_PRINT(x)
#define     RTL_PRINT(x)            FSR_RTL_PRINT(x)


/*****************************************************************************/
/* Function Implementation                                                   */
/*****************************************************************************/

extern void NFC_Reset(void);
unsigned long u32ahb;
int PNFC_Init( void )
{

    unsigned int uiTemp;

    if (u32ahb == 0)
    {
		//TODO implement this
        //AHB_CLOCK_MHZ = get_ahb_clk_by_NFC();
    }

    NFC_Reset();
    RTL_PRINT((TEXT("[PAM:   ]   NAND Init complete!\r\n")));

    return FSR_LLD_SUCCESS;
}
void HAL_ParFlash_Init(void)
{
    /* Init hardware */
    if(PNFC_Init() != FSR_LLD_SUCCESS)
    {
        //printf("\n\nWarning : PNFC_Init NG!!!\n\n");            
        RTL_PRINT((TEXT("[PAM:ERR]   Warning : PNFC_Init NG!!!\r\n")));
    }
    else
    {
        //printf("\n\nPNFC_Init OK!!!\n\n");
        RTL_PRINT((TEXT("[PAM:   ]   PNFC_Init OK!!!\r\n")));
    }

}


