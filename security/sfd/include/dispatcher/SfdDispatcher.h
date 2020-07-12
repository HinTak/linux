/**
****************************************************************************************************
* @file SfdDispatcher.h
* @brief Security framework [SF] filter driver [D] modules dispather implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_DISPATCHER_H_
#define _SFD_DISPATCHER_H_

#include "SfdModuleInterface.h"

#include <linux/rwsem.h>

/**
****************************************************************************************************
* @brief This structure store Security Report message.
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    int   sendfileflag;
    unsigned int optype;
    void* pCaller;
    void* pFileType;
    void* pFilePath;
    void* pDescription;
    int   autodatafile;
    int   descriptionsize;
    int   desc_partial_cnt;
    int   desc_partial_seq;
    void* desc_time;
    struct list_head list;
} QueueFormat;

/**
****************************************************************************************************
* @brief This structure implements the logic sending netlink message received before starting user space daemon
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    struct task_struct* kthread_noti_send_t; ///< task struct for kernel thread (notification) after starting user space daemon(sfpmd)
    struct list_head    noti_que;            ///< legacy linked list for noti_que provided by kernel API side.
} SfQueueContext;

/**
****************************************************************************************************
* @brief This structure implements dispatcher context
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfContextHeader 				header;		///< Dispatcher context header
	SfdModuleInterface 				module;		///< Module interface
	struct rw_semaphore				modSema;	///< Read/write semaphore for module list
	SfQueueContext  				queue;  	///< Securiy Report queue context.
} SfdDispatcherContext;

/**
***************************************************************************************************
* @brief Open dispather context
* @param [in,out] pDispatcher Pointer to the dispathcer context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
***************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenDispatcherContext(SfdDispatcherContext* const pDispatcher);

/**
***************************************************************************************************
* @brief Close dispatcher context
* @param [in,out] pDispatcher Pointer to the dispathcer context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
***************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseDispatcherContext(SfdDispatcherContext* const pDispatcher);

/**
****************************************************************************************************
* @brief Call the hookchain of the module
* @param [in] pOperation Pointer to the SfOperationHeader structure
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdProcessOperationThroughModules(SfProtocolHeader* const pOperation);

#endif	/* !_SFD_DISPATCHER_H_ */
