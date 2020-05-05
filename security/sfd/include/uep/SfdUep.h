/**
****************************************************************************************************
* @file SfdUep.h
* @brief Security framework [SF] filter driver [D] Unauthorized Execution Prevention (UEP) module
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef	_SFD_UEP_H_
#define _SFD_UEP_H_

#include "dispatcher/SfdModuleInterface.h"

#define SFD_UEP_LEVEL_NULL              '0'
#define SFD_UEP_LEVEL_NOTSIGNED         '1'
#define SFD_UEP_LEVEL_RO                '2'
#define SFD_UEP_LEVEL_SIGNED            '3'
#define SFD_UEP_LEVEL_NOTUSED           '4'
#define SFD_UEP_LEVEL_CONTAINER_PROC    '5'
#define SFD_UEP_LEVEL_CONTAINER_SYSCALL '6'
#define SFD_UEP_LEVEL_CONTAINER_PATH    '7'
#define SFD_UEP_LEVEL_CONTAINER_DEBUG   '8'
#define SFD_UEP_LEVEL_CONTAINER_SECURE  '9'

#define  SFD_UEP_LEVEL_CONTAINER_ALLOW  '5'

#define SFD_UEP_17_VERSION              '3'
#define SFD_UEP_18_VERSION              '4'
/**
****************************************************************************************************
* @brief Context of the Unathorized Execution Prevention part of the Secutiry Filter Driver
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfContextHeader 	header; ///< UEP context header
	SfdModuleInterface 	module; ///< UEP module interface to interact with 'SfdDispatcher'
} SfdUepContext;

/**
****************************************************************************************************
* @brief Open UEP context
* @param [in,out] pUep Pointer to the UEP context
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenUepContext(SfdUepContext* const pUep);

/**
****************************************************************************************************
* @brief Close UEP context
* @param [in,out] pUep Pointer to the UEP context
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseUepContext(SfdUepContext* const pUep);

#endif	/* !_SFD_UEP_H_ */
