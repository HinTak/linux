/**
****************************************************************************************************
* @file SfdDispatcher.c
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

#include <linux/in.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include "SfdDispatcher.h"
#include "SfdPlatformInterface.h"
#include "SfRulesList.h"
#include "SfFirewallRulesList.h"
#include "SfdCache.h"
#include "SfdConfiguration.h"
#include "uep/SfdUepHookHandlers.h"

#include <uapi/linux/sf/transport/SfSerialization.h>
#include <uapi/linux/sf/core/SfDebug.h>

/**
* @brief The below is used for sending Security Report message after staring sfpmd(user space daemon).
*/
extern int gSfpmdBinded;                      ///< Indicate whether user space's daemon (sfpmd) was started.
int SfdSendReportInQueue(void* pData);
void SfdClearReportQueueList(void);


/**
* @brief Global dispatcher pointer definition. This pointer may be used as global symbol by other
*   kernel modules.
*/
SfdDispatcherContext* g_pDispatcher = NULL;

/**
****************************************************************************************************
* @brief                    Handle rule update request
* @param [in] pRule         Rule
* @return                   void
****************************************************************************************************
 */
static void HandleRuleUpdate( const SfOperationBlockRule* pRule )
{
    if ( ( pRule->ruleType == SF_RULE_FILE_OPEN ) && ( pRule->action == SF_RULE_ADD ) )
    {
        AddFileRule( pRule->fileInode );
    }
    else if( pRule->action == SF_RULE_DEL )
    {
        SF_LOG_I( "[%s] Delete all of rules", __FUNCTION__ );
        ClearFileRulesList();
        ClearFirewallRulesList();
    }
}


/**
****************************************************************************************************
* @brief                    Callback for receiving messages
* @param [in] skb           Input socket buffer
* @return                   void
****************************************************************************************************
 */
static void SfdReceiveMessageCallback( struct sk_buff* skb )
{
    SfNetlinkPacket netlinkPacket = { .pBuffer = skb };
    SfPacket* pPacket = SfDeserializePacket( &netlinkPacket );

    if ( pPacket )
    {
        SF_LOG_I( "[%s] received packet, type = %d", __FUNCTION__, pPacket->header.type );
        if ( pPacket->op )
        {
            switch( pPacket->op->type )
            {
            case SF_OPERATION_TYPE_RULE:
                {
                    SfOperationBlockRule* pRule = (SfOperationBlockRule*)( pPacket->op );
                    SF_LOG_I( "[%s] received rule, type %u, action %u, addr %u, inode %llu",
                              __FUNCTION__, pRule->ruleType, pRule->action, pRule->ipAddr,
                              pRule->fileInode );
                    HandleRuleUpdate( pRule );
                }
                break;
            case SF_OPERATION_TYPE_SETUP_DUID:
                {
                    SfOperationSetupDUID* pDuidOp = (SfOperationSetupDUID*)( pPacket->op );
                    SF_LOG_I( "[%s] received DUID = [%s]", __FUNCTION__, pDuidOp->pDUID );
                    SetupDuidHash( pDuidOp->pDUID );
                }
                break;
            case SF_OPERATION_TYPE_SND_RCV_RULE:
                {
                    SfOperationFwRule* pFwRule = (SfOperationFwRule*)( pPacket->op );
                    SF_LOG_I( "[%s] received FW rule: proto = %u, direction = %u, ip field type = %u,"
                                "ip addr type = %u, ip[0] = %u, ip[1] = %u, port[0] = %u, port[1] = %u",
                              __FUNCTION__,
                              pFwRule->rule.protocol, pFwRule->rule.direction, pFwRule->rule.ipFType,
                              pFwRule->rule.ipAType, pFwRule->rule.ip[ 0 ], pFwRule->rule.ip[ 1 ],
                              pFwRule->rule.port[ 0 ], pFwRule->rule.port[ 1 ] );
                    AddFirewallRule( &pFwRule->rule );
                }
                break;
            default:                
                SF_LOG_W( "[%s] Not supported type: %d", __FUNCTION__, pPacket->op->type );
                break;
            }
        }
        SfDestroyPacket( pPacket );
    }
    else
    {
        SF_LOG_E( "[%s] failed to deserialize packet", __FUNCTION__ );
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenDispatcherContext(SfdDispatcherContext* const pDispatcher)
{
    SF_STATUS result = SF_STATUS_FAIL;

    SfdInitConfig();

    SfdCacheInit();

   /**
    * @note If you want to use this macros in your code, please, check it's implementation before.
    *   Implementation located in libcore/SfValidator.h
    */
    result = SF_CONTEXT_SAFE_INITIALIZATION(pDispatcher, SfdDispatcherContext,
        SF_CORE_VERSION, SfdCloseDispatcherContext);

    if (SF_SUCCESS(result))
    {
        g_pDispatcher = pDispatcher;

        pDispatcher->module.moduleType = SFD_MODULE_TYPE_DISPATCHER;
        INIT_LIST_HEAD(&pDispatcher->module.list);
        init_rwsem( &pDispatcher->modSema );
        INIT_LIST_HEAD(&pDispatcher->queue.noti_que);
        pDispatcher->queue.kthread_noti_send_t = NULL;
        gSfpmdBinded = 0;        
        SfSetCbInBind(&SfdSendReportInQueue);

        result = SfdCreatePlatformInterface();
        if (SF_SUCCESS(result))
        {
            result = SfCreateNode(&pDispatcher->module.pNode, "Disp", 0xbedabeda, SfdReceiveMessageCallback);
            if (SF_SUCCESS(result))
            {
                pDispatcher->header.state = SF_CONTEXT_STATE_INITIALIZED;
                SfSetNetlinkNode(pDispatcher->module.pNode);
            }
        }
    }

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseDispatcherContext(SfdDispatcherContext* const pDispatcher)
{
    SF_STATUS result = SF_STATUS_FAIL;

    if (!SfIsContextValid(&pDispatcher->header, sizeof(SfdDispatcherContext)))
    {
        SF_LOG_E("[%s] Invalid 'pDispatcher'", __FUNCTION__);
        return SF_STATUS_BAD_ARG;
    }

    result = SfdDestroyPlatformInterface();

    if (SF_SUCCESS(result))
    {
        pDispatcher->header.size = 0;
        pDispatcher->header.state = SF_CONTEXT_STATE_UNINITIALIZED;
    }

    result = SfDestroyNode(pDispatcher->module.pNode);
    SfdClearReportQueueList();
    g_pDispatcher = NULL;
    ClearFileRulesList();
    ClearFirewallRulesList();

    SfdCacheDeinit();

    SF_LOG_I("[%s] was done with result: %d", __FUNCTION__, result);
    return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdPerformBlocking( SfPacket* pPacket )
{
    SF_STATUS result = SF_STATUS_OK;
    SfProtocolHeader* pOperation = pPacket->op;

    switch ( pOperation->type )
    {
        case SF_OPERATION_TYPE_OPEN:
        {
            SfOperationFileOpen* pFileOpenOperation = (SfOperationFileOpen*)pOperation;

            struct inode * inode = pFileOpenOperation->pFile->f_path.dentry->d_inode;
            unsigned long long uino = (unsigned long long)(int)(inode->i_sb->s_dev);
            unsigned long long inodeNum = uino<<32 | inode->i_ino;
            
            if ( FileAccessRestricted( (Uint64)inodeNum ) )
            {
                SF_LOG_I( "[%s] access to file with inode %llu restricted", __FUNCTION__,
                          inodeNum );
                result = SF_STATUS_RESOURCE_BLOCK;
                pFileOpenOperation->result = result;
            }
            break;
        }

        case SF_OPERATION_TYPE_SENDMSG:
            result = CheckSendmsgCall( pPacket );
            break;

        case SF_OPERATION_TYPE_RECVMSG:
            result = CheckRecvmsgCall( pPacket );
            break;

        default:
            break;
    }

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdProcessOperationThroughModules(SfProtocolHeader* const pOperation)
{
    SF_STATUS result = SF_STATUS_OK;
    SfdModuleInterface* pModule = NULL;
    SfPacket packet =
    {
        .header =
        {
            .size = sizeof(SfPacket),
            .type = SF_PACKET_TYPE_OPERATION
        },
        .env = NULL,
        .op = pOperation
    };

    do
    {
        if (NULL == g_pDispatcher)
        {
            break;
        }

        down_read( &g_pDispatcher->modSema );
        {
            list_for_each_entry( pModule, &g_pDispatcher->module.list, list )
            {
                if (NULL != pModule &&
                    NULL != pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE])
                {
                    /**
                    * @note Passing header in this case will pass the C compiler rules. Header,
                    *   is every time first bytes in the protol structure. Header is necessary
                    *   to verify passed data. The same as for the following cycle.
                    */
                    result =
                        pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE](&packet.header);
                    if (SF_FAILED(result))
                    {
                        break;
                    }
                }
            }

            if ((result != SF_STATUS_SYSTEM_ERROR_IN_READ) && 
                 (result != SF_STATUS_NOT_ENOUGH_BUFFER))
            {
                if ( pOperation->type == SF_OPERATION_TYPE_OPEN    ||
                     pOperation->type == SF_OPERATION_TYPE_SENDMSG ||
                     pOperation->type == SF_OPERATION_TYPE_RECVMSG )
                {
                    result = SfdPerformBlocking( &packet );
                }
            }

            list_for_each_entry( pModule, &g_pDispatcher->module.list, list )
            {
                if (NULL != pModule &&
                    NULL != pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION])
                {
                    pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION](&packet.header);
                }
            }
        }
        up_read( &g_pDispatcher->modSema );

        if (NULL != packet.env)
        {
           SfDestroyEnvironment(packet.env);
        }
    } while(FALSE);

    return result;
}
