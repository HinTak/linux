/**
****************************************************************************************************
* @file SfdModuleInterface.c
* @brief Security framework [SF] filter driver [D] security module interface implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 9, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdModuleInterface.h"
#include "SfdDispatcher.h"

/**
* @brief Global pointer to SfdDispatcherContext object. It is initialized in SfdDispatcher.c. See
* 	'SfdOpenDispatcherContext' function for details.
*/
extern SfdDispatcherContext* g_pDispatcher;

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdRegisterModule(SfdModuleInterface* const pInterface)
{
	SF_STATUS result = SF_STATUS_OK;

	if (NULL == g_pDispatcher || NULL == pInterface)
	{
		SF_LOG_E("[%s] Dispatcher module was not initialized", __FUNCTION__);
		return SF_STATUS_BAD_ARG;
	}

	down_write( &g_pDispatcher->modSema );
	list_add(&pInterface->list, &g_pDispatcher->module.list);
	up_write( &g_pDispatcher->modSema );

	SF_LOG_I("[%s] was done with result: %d for module type %d", __FUNCTION__, result,
		pInterface->moduleType);
	return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUnregisterModule(SfdModuleInterface* const pInterface)
{
	SF_STATUS result = SF_STATUS_OK;

	if (NULL == g_pDispatcher || NULL == pInterface)
	{
		SF_LOG_E("[%s] Dispatcher module was not initialized", __FUNCTION__);
		return SF_STATUS_BAD_ARG;
	}

	down_write( &g_pDispatcher->modSema );
	list_del(&pInterface->list);
	up_write( &g_pDispatcher->modSema );

	SF_LOG_I("[%s] was done with result: %d for module type %d", __FUNCTION__, result, pInterface->moduleType);
	return result;
}

