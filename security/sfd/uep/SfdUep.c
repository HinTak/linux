/**
****************************************************************************************************
* @file SfdUep.c
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

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdUep.h"
#include "SfdUepHookHandlers.h"

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenUepContext(SfdUepContext* const pUep)
{
	SF_STATUS result = SF_STATUS_FAIL;

	/**
	* If you want to use this macros in your code, please, check it's implementation before.
	* Implementation located in libcore/SfValidator.h
	*/
	SF_CONTEXT_SAFE_INITIALIZATION(pUep, SfdUepContext, SF_CORE_VERSION, SfdCloseUepContext);

	pUep->module.moduleType = SFD_MODULE_TYPE_UEP;

	pUep->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE] = SfdUepPacketHandler;
	pUep->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION] = NULL;

	pUep->header.state = SF_CONTEXT_STATE_INITIALIZED;
	result = SfdRegisterModule(&pUep->module);

	SF_LOG_I("[%s] was done with result: %d", __FUNCTION__, result);
	return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseUepContext(SfdUepContext* const pUep)
{
	SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;

	if (!SfIsContextValid(&pUep->header, sizeof(SfdUepContext)))
	{
		SF_LOG_E("[%s] takes (pUep = %p) argument", __FUNCTION__, pUep);
		return SF_STATUS_BAD_ARG;
	}

	pUep->header.state = SF_CONTEXT_STATE_UNINITIALIZED;

	SF_LOG_I("[%s] was done with result: %d", __FUNCTION__, result);
	return result;
}
