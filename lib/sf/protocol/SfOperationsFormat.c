/**
****************************************************************************************************
* @file SfOperationsFormat.c
* @brief Security framework [SF] filter driver [D] SF packet operations support utilities
* @date Created Sep 24, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/protocol/SfOperationsFormat.h>

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyOperationRule( SfOperationBlockRule* pOperation )
{
    if ( pOperation )
    {
        sf_free( pOperation );
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyOperationSetupDUID( SfOperationSetupDUID* pOperation )
{
    if ( pOperation )
    {
        if ( pOperation->pDUID )
            sf_free( pOperation->pDUID );
        sf_free( pOperation );
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyOperationFwRule( SfOperationFwRule* pOperation )
{
    if ( pOperation )
    {
        sf_free( pOperation );
    }
}

void SfDestroyOperationReportInfo( SfOperationSecurityReport* pOperation )
{
    if (pOperation)
    {
        if (pOperation->caller)
        {
            sf_free(pOperation->caller);
        }
        if (pOperation->filetype)
        {
            sf_free(pOperation->filetype);
        }
        if (pOperation->filepath)
        {
            sf_free(pOperation->filepath);
        }
        if (pOperation->description)
        {
            sf_free(pOperation->description);
        }
        if (pOperation->desc_time)
        {
            sf_free(pOperation->desc_time);
        }
        sf_free( pOperation );
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyOperation( SfProtocolHeader* pHeader )
{
    if ( pHeader )
    {
        switch ( pHeader->type )
        {
            case SF_OPERATION_TYPE_RULE:
                SfDestroyOperationRule( (SfOperationBlockRule*)pHeader );
                break;

            case SF_OPERATION_TYPE_SETUP_DUID:
                SfDestroyOperationSetupDUID( (SfOperationSetupDUID*)pHeader );
                break;
                
            case SF_OPERATION_TYPE_SND_RCV_RULE:
                SfDestroyOperationFwRule( (SfOperationFwRule*)pHeader );
                break;
            case SF_OPERATION_TYPE_REPORT_NET:     /*fall through*/
            case SF_OPERATION_TYPE_REPORT_PROCESS: /*fall through*/
            case SF_OPERATION_TYPE_REPORT_FILE:
                SfDestroyOperationReportInfo( (SfOperationSecurityReport*)pHeader );
                break;
        }
    }
}