/**
****************************************************************************************************
* @file SfStatus.h
* @brief Security framework [SF] return codes and helpers
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 4, 2014 13:26
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_STATUS_H_
#define _SF_STATUS_H_

#include "SfTypes.h"

/**
****************************************************************************************************
* @brief Security framework status enumeration
****************************************************************************************************
*/
typedef enum
{
    SF_STATUS_OK = 0,                       ///< Operation has been finished successfully
    SF_STATUS_PENDING = 1,                  ///< Everything is ok but resource busy (try later)
    SF_STATUS_UEP_SIGNATURE_CORRECT = 2,    ///< Signature is correct
    SF_STATUS_UEP_SIGNATURE_INCORRECT = 3,  ///< Signature is incorrect
    SF_STATUS_UEP_FILE_NOT_SIGNED = 4,      ///< File is not signed
    SF_STATUS_UEP_FILE_ALREADY_SIGNED = 5,  ///< File is already signed
    SF_STATUS_UEP_FILE_NO_MAGICNUM = 6,      ///< There is no magic number
    SF_STATUS_RESOURCE_BLOCK = 7,           ///< Block resource at real-time by the rules list
    SF_STATUS_UEP_SIGNATURE_DUID = 8,       ///< File signature is DUID hash
    SF_STATUS_SYSTEM_ERROR_IN_READ = 9,   ///< System error from kernel read()
    SF_STATUS_NOT_ENOUGH_BUFFER = 10,  ///< Out buffer is not enough to keep handling
    SF_STATUS_FAIL = -(1),                  ///< Operation has been failed
    SF_STATUS_BAD_ARG = -(2),               ///< Bad arguments was passed
    SF_STATUS_NOT_IMPLEMENTED = -(3),       ///< Function currently not implemented
    SF_STATUS_ALREADY_INITIALIZED = -(4),   ///< The object already initialized
    SF_STATUS_ACCESS_DENIED = -(5),         ///< Access denied
    SF_STATUS_NOT_INITIALIZED = (-9),       ///< When instance is not initialized
    SF_STATUS_DESTINATION_UNREACHABLE = (-10),
    SF_STATUS_NOT_ENOUGH_RESOURCE = (-11),      ///< Even though resource is not enough, sfd must not block it    
    ///< Notice : You must be carefull in defining new error code. Refer to SF_SYSERR_TO_STATUS.
    SF_STATUS_SYSERR_TOPMOST = -(1024),        ///< The variable's most value which includes system error.
    SF_STATUS_SYSERR_BORDER = (-2048),         ///< border line to indicate system error number 
    SF_STATUS_MAX = (Int) -1 ///< Max value
} SF_STATUS;

/**
****************************************************************************************************
* @brief Security framework status SUCCESS checker
****************************************************************************************************
*/
#define SF_SUCCESS(x) (x >= 0)

/**
****************************************************************************************************
* @brief Security framework status FAILED checker
****************************************************************************************************
*/
#define SF_FAILED(x) (x < 0)

/**
****************************************************************************************************
* @brief Security framework status pend(cache) checker
****************************************************************************************************
*/
#define SF_PENDED(x) (x == SF_STATUS_PENDING)

/**
****************************************************************************************************
* @brief Make status code with system error code
****************************************************************************************************
*/
#define SF_SYSERR_TO_STATUS(x) (-x + SF_STATUS_SYSERR_BORDER )
/**
****************************************************************************************************
* @brief Check wether status code has error code
****************************************************************************************************
*/
#define SF_IS_SYSERR(x) ((x) > SF_STATUS_SYSERR_BORDER && (x) < SF_STATUS_SYSERR_TOPMOST)

#endif /* !_SF_STATUS_H_ */
