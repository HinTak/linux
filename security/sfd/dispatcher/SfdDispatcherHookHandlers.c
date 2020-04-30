/**
****************************************************************************************************
* @file SfdDispatcherHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handler for system calls implementation
* @date Created Apr 10, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include "SfdDispatcher.h"
#include "SfdConfiguration.h"

#include <linux/in.h>
#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/jiffies.h>
#include <uapi/linux/sf/core/SfMemory.h>
#include <linux/fs_struct.h>
#include <linux/sched.h>   // wake_up_process()
#include <linux/kthread.h> // kthread_create(), kthread_run()
#include <linux/err.h>     // IS_ERR(), PTR_ERR()
#include <linux/rtc.h>     // rtc_time_to_tm
#include "SfdCache.h"
#include "uep/SfdUep.h"

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_SECURECONTAINER)
#include <linux/pid_namespace.h>
#include <linux/sf_security.h>
#include "../../smack/smack.h"
#define SECURECONTAINER_LABEL '!'

#define ZONE_REPORT_LEN_PATH 256
#define ZONE_REPORT_LEN_DESC 256 
#endif

#if defined(SF_CACHE_TEST)
extern unsigned int g_sf_cache_test_ino;
#endif // SF_CACHE_TEST

#define SFD_SEND_QUEUE_THREAD_NAME  "sfd-send-worker"
extern SfdDispatcherContext* g_pDispatcher;
int gSfpmdBinded = 0;                     ///< Indicate whether user space's daemon (sfpmd) was started.
static DEFINE_RWLOCK(g_SfdSendQueueLock); ///< Defining spin lock for reading and writting send queue in kernel thread

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_OPEN)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_file_open( struct file* pFile, const struct cred* pCredentials )
{
    SF_STATUS   result                  = SF_STATUS_OK;
    const Ulong openEventFilterDelay    = 2 * 60 * HZ;
    unsigned long long uino             = 0;
    bool bIsRW = FALSE;
#ifndef SF_DEMO
    // do not filter open() event with SFD before 5 min from system boot
    if ( time_before( jiffies, openEventFilterDelay ) )
        return 0;
#endif // SF_DEMO

    if ( pFile && pCredentials )
    {
        result = SfdCheckFileIsInRW( pFile );
        bIsRW = result == SF_STATUS_OK? TRUE:FALSE;
        if ( SF_SUCCESS( result ) )
        {
            SfOperationFileOpen args =
            {
                .header =
                {
                    .size = sizeof(SfOperationFileOpen),
                    .type = SF_OPERATION_TYPE_OPEN
                },
                .pFile = pFile,
                .pCred = (struct cred*)pCredentials
            };

            // get unique inode number
            uino = SfdGetUniqueIno(pFile->f_path.dentry->d_inode);

            // Check that the file is already processed.
            if( SF_SUCCESS( SfdCacheCheck( uino, SFD_MODULE_TYPE_DISPATCHER, &result, NULL, bIsRW ) ) )
            {
                if( SF_STATUS_PENDING != result )
                {
                    return SfReturn( result );
                }
            }

            // Not checked, proceed it.
            result = SfdProcessOperationThroughModules( &args.header );

            // Add the result of processing to cache
            if( SF_FAILED( SfdCacheAdd( uino, SFD_MODULE_TYPE_DISPATCHER, result, '0', bIsRW ) ) )
            {
                SF_LOG_W("[%s] Failed to add cache", __FUNCTION__ );
            }           
        }
    }
    return SfReturn( result );
}

#endif // CONFIG_SECURITY_SFD & CONFIG_SECURITY_SFD_DISPATCHER_OPEN

/**
 * sf_security_inode_permission
 *
 * 1. Check the permission before file is opened.
 * 2. If the file is opened to be wrote, remove the cache node to check modification
 * 
 * @param  inode inode of file
 * @param  mask  file open mask
 * @return       to pass, return 0
 */
int sf_security_inode_permission(struct inode *inode, int mask)
{
#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER
    struct inode_smack *sip;
#endif // CONFIG_SECURITY_SFD_SECURECONTAINER
    unsigned long long uino = 0;
    int res = 0;


    if( NULL != inode )
    {
        uino = SfdGetUniqueIno(inode);
        
        #ifdef CONFIG_SECURITY_SFD_SECURECONTAINER
        sip = inode->i_security;
        if((sip != NULL) && (sip->smk_inode != NULL) && (sip->smk_inode->smk_known != NULL))
        {
            if(sip->smk_inode->smk_known[0] == SECURECONTAINER_LABEL)
            {
                if( SFD_UEP_LEVEL_CONTAINER_PATH > current->uepLevel )
                {
                    #ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
                    res = -EPERM;
                    #endif
                }
            }	
        }
        #endif 
        
        if( mask & MAY_WRITE )
        {
#ifdef SF_CACHE_TEST
            if( g_sf_cache_test_ino == inode->i_ino )
                SF_LOG_I( "SfdCacheTest: inode_permission sf_open_test: %u", inode->i_ino );
#endif // SF_CACHE_TEST
            // spin_lock(&inode->i_lock);
            SfdCacheRemove( uino );
            // spin_unlock(&inode->i_lock);
        }
    }
    
    return res;
}


/**
 * sf_security_inode_unlink
 *
 * 1. If the file is opened to be wrote, remove the cache node to check modification
 * 
 * @param  dir inode of file
 * @param  dentry dentry
 * @return       to pass, return 0
 */
int sf_security_inode_unlink(struct inode *dir, struct dentry *dentry)
{
    if (unlikely(IS_PRIVATE(dentry->d_inode)))
        return 0;

#ifdef SF_CACHE_TEST
    if( g_sf_cache_test_ino == dentry->d_inode->i_ino )
        SF_LOG_I( "SfdCacheTest: inode_unlink sf_open_test: %u", dentry->d_inode->i_ino );
#endif // SF_CACHE_TEST
    SfdCacheRemove( SfdGetUniqueIno(dentry->d_inode) );

    return 0;
}

/**
 * sf_security_inode_free
 *
 * 1. If the file is opened to be wrote, remove the cache node to check modification
 * 
 * @param  inode  inode of file
 */
void sf_security_inode_free(struct inode *inode)
{
    if(unlikely(inode))
        return;

#ifdef SF_CACHE_TEST
        if( g_sf_cache_test_ino == inode->i_ino )
            SF_LOG_I( "SfdCacheTest: inode_free sf_open_test: %u", inode->i_ino );
#endif // SF_CACHE_TEST

    SfdCacheRemove( SfdGetUniqueIno( inode ) );

    return;
}


#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_SECURECONTAINER)

static int sf_process_authorized(struct task_struct *cur, struct task_struct *tsk)
{
	struct nsproxy * host_ns = NULL;
	struct nsproxy * current_ns  = NULL;
	struct nsproxy * target_ns  = NULL;
	struct task_struct *init_t = &init_task;

	if((init_t == NULL) || (cur == NULL) || (tsk == NULL))
	{
		return TRUE;
	}

	host_ns = init_t->nsproxy;
	current_ns = cur->nsproxy;
	target_ns = tsk->nsproxy;

	if((host_ns == NULL) || (current_ns == NULL) || (target_ns == NULL))
	{
		return TRUE;
	}

	if(current_ns->mnt_ns== host_ns->mnt_ns)
	{
		if(current_ns->mnt_ns == target_ns->mnt_ns)
		{
			if((tsk->uepLevel >= SFD_UEP_LEVEL_CONTAINER_SECURE) && (cur->uepLevel < SFD_UEP_LEVEL_CONTAINER_PROC))
			{
				return FALSE;
			}
		}
		else
		{
			if((target_ns->type >= NS_TYPE_SECURE) && (cur->uepLevel < SFD_UEP_LEVEL_CONTAINER_PROC))
			{
				return FALSE;
			}
		}
	}
	else
	{
		if(current_ns->mnt_ns != target_ns->mnt_ns)
		{
			return FALSE;
		}
	}

	return TRUE;

}

static int sf_set_report_log(char **cmdline,char **description,struct task_struct *cur)
{

	if((*cmdline != NULL) || (*description != NULL))
	{
		return 0;
	}

	*cmdline = (char*)sf_malloc(ZONE_REPORT_LEN_PATH);

	if(*cmdline == NULL)
	{
		return 0;
	}
	
	*description = (char*)sf_malloc(ZONE_REPORT_LEN_DESC);

	if(*description == NULL)
	{
		return 0;
	}

	if(!get_cmdline(cur,*cmdline,ZONE_REPORT_LEN_PATH-1))
	{
		return 0;
	}

	return 1;
}

static void sf_log_free(char **cmdline,char **description)
{
	if(*cmdline)
	{
		sf_free(*cmdline);
		*cmdline = NULL;
	}

	if(*description)
	{
		sf_free(*description);
		*description = NULL;
	}
}

/**
 * [SfdPathFilter description]
 * @param  name [description]
 * @param  path [description]
 * @param  func [description]
 * @return      [description]
 */
int SfdPathFilter(const char *name, const struct path * const path, const char *func)
{
    struct inode_smack *sip;
    int res = 0;
	char *cmdline = NULL;
	char *description = NULL;
    
    if( unlikely(name == NULL || path == NULL || func == NULL) )
    {
        SF_LOG_W("[%s] invalid arg, name:0x%X, path:0x%X, func:0x%X",
            __FUNCTION__, name, path, func );
        return 0;
    }
    
    if(path->dentry == NULL || path->dentry->d_inode == NULL )
    {
        return 0;
    }

    sip = path->dentry->d_inode->i_security;

    if(sip == NULL || sip->smk_inode == NULL || sip->smk_inode->smk_known == NULL)
    {
        return 0;
    }

    if(sip->smk_inode->smk_known[0] == SECURECONTAINER_LABEL)
    {
        if( SFD_UEP_LEVEL_CONTAINER_PATH > current->uepLevel )
        {
			if(sf_set_report_log(&cmdline, &description, current))
			{
				snprintf(description,ZONE_REPORT_LEN_DESC-1,"call:lv%c,path:%s",current->uepLevel,name);
				SF_LOG_I("[%s] send zone report %s,%s", __FUNCTION__, cmdline, description);

				#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
				if(sf_send_report_file("SECUREZONE","DATA_SZ_PATH",cmdline,description,0))
				{
					SF_LOG_I("[%s] failed to send report %s,%s", __FUNCTION__, cmdline, description);
				}
				#endif
			}

			sf_log_free(&cmdline,&description);
			
			#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE        
            res = -EPERM; 
			#endif
        }

    }

    return res;
}

int sf_proc_authorized(struct task_struct *cur, struct task_struct *tsk)
{
	if(!sf_process_authorized(cur,tsk))
	{
		#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
		return FALSE;
		#endif
	}
	return TRUE;
}

int sf_signal_authorized(struct task_struct *cur, struct task_struct *tsk,int sig)
{
	if(!sf_process_authorized(cur,tsk))
	{
		SF_LOG_I("[%s] Blocked! cur pid:%u, comm:%s, kuep:%c, tsk pid:%u, comm:%s, kuep:%c, sig:%d", __FUNCTION__, cur->pid, cur->comm, cur->uepLevel,tsk->pid, tsk->comm, tsk->uepLevel, sig);
		#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
		return FALSE;
		#endif
	}
	return TRUE;
}

int sf_syscall_task_authorized(struct task_struct *cur, const char *func)
{
	char *cmdline = NULL;
	char *description = NULL;
	
	if(cur->uepLevel < SFD_UEP_LEVEL_CONTAINER_SYSCALL)
	{
		if(sf_set_report_log(&cmdline, &description, cur))
		{
			snprintf(description,ZONE_REPORT_LEN_DESC-1,"syscall:%s,caller:lv%c",func,cur->uepLevel);
			SF_LOG_I("[%s] send zone report %s,%s", __FUNCTION__, cmdline, description);

			#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
			if(sf_send_report_process("SECUREZONE","DATA_SZ_SYSCALL",cmdline,description,0))
			{
				SF_LOG_I("[%s] failed to send report %s,%s", __FUNCTION__, cmdline, description);
			}
			#endif
		}
		sf_log_free(&cmdline,&description);
		
		#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
		return FALSE;
		#endif
	}
	return TRUE;
}

int sf_syscall_ns_authorized(struct task_struct *tsk, char* func, int nstype)
{
	struct nsproxy * ns = NULL;

	if ((tsk == NULL) || (tsk->nsproxy == NULL))
	{
		return TRUE;
	}

	ns = tsk->nsproxy;

	if (ns->type >= nstype)
	{
		return sf_syscall_task_authorized(tsk,func);
	}

	return TRUE;
}

#endif // CONFIG_SECURITY_SFD_SECURECONTAINER



#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_EXEC)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_bprm_check( struct linux_binprm* pBinaryParameters )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pBinaryParameters && pBinaryParameters->file )
    {
        // TODO: uncomment
        // result = SfdCheckFileIsInRW( pBinaryParameters->file );
        // if ( SF_SUCCESS( result ) || (pBinaryParameters->buf[0] == "#" && pBinaryParameters->buf[1] =="!") )
        {
            SfOperationBprmCheckSecurity args =
            {
                .header =
                {
                    .size = sizeof(SfOperationBprmCheckSecurity),
                    .type = SF_OPERATION_TYPE_EXEC
                },
                .pBinParameters = pBinaryParameters
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_MMAP)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_mmap_file( struct file* pFile, unsigned long prot, unsigned long flags )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pFile )
    {
        // TODO: uncomment
        // result = SfdCheckFileIsInRW( pFile );
        // if ( SF_SUCCESS( result ) )
        {
            SfOperationFileMmap args =
            {
                .header =
                {
                    .size = sizeof(SfOperationFileMmap),
                    .type = SF_OPERATION_TYPE_MMAP
                },
                .pFile = pFile,
                .prot  = prot,
                .flags = flags,
                .bCheckAlways = 0,
                .bIsSo = 1
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_LOAD_KERNEL_MODULE)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_kernel_module_from_file( struct file* pFile )
{
    SF_STATUS result = SF_STATUS_OK;

    
    if ( pFile )
    {
        // mmap() operation with PROT_EXEC is chosen here so
        // UEP will be able to verify kernel modules
        SfOperationFileMmap args =
        {
            .header =
            {
                .size = sizeof(SfOperationFileMmap),
                .type = SF_OPERATION_TYPE_MMAP
            },
            .pFile = pFile,
            .prot  = PROT_EXEC,
            .bCheckAlways = 1,
            .bIsSo = 0
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_SOCKET)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_create( int family, int type, int protocol, int kernel )
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationSocketCreate args =
    {
        .header =
        {
            .size = sizeof(SfOperationSocketCreate),
            .type = SF_OPERATION_TYPE_SOCKET
        },
        .family   = family,
        .type     = type,
        .protocol = protocol,
        .kernel   = kernel
    };
    result = SfdProcessOperationThroughModules( &args.header );
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_BIND)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_bind( struct socket* pSocket, struct sockaddr* pAddress, int addrlen )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pAddress )
    {
        SfOperationSocketBind args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketBind),
                .type = SF_OPERATION_TYPE_BIND
            },
            .pSocket       = pSocket,
            .pAddress      = pAddress,
            .addressLength = addrlen
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
};
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_CONNECT)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_connect( struct socket* pSocket, struct sockaddr* pAddress, int addrlen )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pAddress && ( AF_INET == pAddress->sa_family ) )
    {
        SfOperationSocketConnect args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketConnect),
                .type = SF_OPERATION_TYPE_CONNECT
            },
            .pSocket       = pSocket,
            .pAddress      = pAddress,
            .addressLength = addrlen
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_LISTEN)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_listen( struct socket* pSocket, int backlog )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket )
    {
        SfOperationSocketListen args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketListen),
                .type = SF_OPERATION_TYPE_LISTEN
            },
            .pSocket = pSocket,
            .backLog = backlog
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_ACCEPT)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_accept( struct socket* pSocket, struct socket* pNewSocket )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pNewSocket )
    {
        SfOperationSocketAccept args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketAccept),
                .type = SF_OPERATION_TYPE_ACCEPT
            },
            .pSocket    = pSocket,
            .pNewSocket = pNewSocket
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_SENDMSG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_sendmsg( struct socket* pSocket, struct msghdr* pMsg, int size )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pMsg )
    {
        SfOperationSocketSendmsg args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketSendmsg),
                .type = SF_OPERATION_TYPE_SENDMSG
            },
            .pSocket = pSocket,
            .pMsg    = pMsg,
            .size    = size
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_RECVMSG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_recvmsg( struct socket* pSocket, struct msghdr* pMsg, int size, int flags )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pMsg )
    {
        SfOperationSocketRecvmsg args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketRecvmsg),
                .type = SF_OPERATION_TYPE_RECVMSG
            },
            .pSocket = pSocket,
            .pMsg    = pMsg,
            .size    = size,
            .flags   = flags
        };
         result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif


#if defined(CONFIG_SECURITY_SFD)
#define SF_REPORT_CALLER_LIMIT   16
#define SF_REPORT_FILETYPE_LIMIT 16
#define SF_REPORT_FILEPATH_LIMIT 256
#define SF_REPORT_DESC_LIMIT     256
#define SF_REPORT_DATE_STR_SIZE  9
/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdMakeSysTime(char *pszDate, int size)
{
    SF_STATUS result = SF_STATUS_OK;
    struct timeval now_time;
    struct rtc_time tm_time;
    unsigned int milliseconds = 0;
    unsigned long local_time;
   
    // GetTime
    do_gettimeofday(&now_time);
    milliseconds = now_time.tv_usec / 1000;
    local_time = (u32)(now_time.tv_sec - (sys_tz.tz_minuteswest * 60));
    rtc_time_to_tm(local_time, &tm_time);
    snprintf(pszDate, size, "%02d%02d%02d%03d", tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, milliseconds);
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
int SfdSendReport(const char *caller, const char* filetype, const char *filepath, const char *description, const int sendfileflag, SF_OPERATION_TYPE optype)
{
    SfOperationSecurityReport args =
    {
        .header =
        {
            .size = sizeof(SfOperationSecurityReport),
            .type = optype
        },
        .caller       = NULL,
        .filetype     = NULL,
        .filepath     = NULL,
        .description  = NULL,
        .sendfileflag = sendfileflag,
        .autodatafile = 0,
        .descriptionsize  = 0,
        .desc_partial_cnt = 0,
        .desc_partial_seq = 0,
        .desc_time = NULL
    };
    SF_STATUS result = SF_STATUS_FAIL;
    Char* pCaller = NULL, *pFiletype = NULL, *pFilepath = NULL, *pDescription = NULL, *pSysTime = NULL;
    QueueFormat* pItem = NULL;
    int nCopyLen = 0;
    int nLen = 0;
    int nSendCnt = 0;
    int nRestSize = 0;
    int nNeedSendCnt = 0;
    int idx = 0;
    int nLeftSize = 0;
    int nPos = 0;

    if ( caller && filetype && filepath && description )
    {
        nLen = strlen(caller);
        nCopyLen = (nLen > SF_REPORT_CALLER_LIMIT) ? SF_REPORT_CALLER_LIMIT:nLen;
        pCaller = (Char*)sf_malloc(nCopyLen + 1);
        if (pCaller)
        {
            sf_memcpy(pCaller, caller, nCopyLen);
            pCaller[nCopyLen] = 0x00;
            args.caller = pCaller;
        }
        else
        {
            goto END_PROC;
        }

        nLen = strlen(filetype);
        nCopyLen = (nLen > SF_REPORT_FILETYPE_LIMIT) ? SF_REPORT_FILETYPE_LIMIT:nLen;
        pFiletype = (Char*)sf_malloc(nCopyLen + 1);
        if (pFiletype)
        {
            sf_memcpy(pFiletype, filetype, nCopyLen);
            pFiletype[nCopyLen] = 0x00;
            args.filetype = pFiletype;
        }
        else
        {
            goto END_PROC;
        }

        nLen = strlen(filepath);
        nCopyLen = (nLen > SF_REPORT_FILEPATH_LIMIT) ? SF_REPORT_FILEPATH_LIMIT:nLen;
        pFilepath = (Char*)sf_malloc(nCopyLen + 1);
        if (pFilepath)
        {
            if (nLen > SF_REPORT_FILEPATH_LIMIT)
                sf_memcpy(pFilepath, filepath + (nLen - SF_REPORT_FILEPATH_LIMIT), nCopyLen);
            else
                sf_memcpy(pFilepath, filepath, nCopyLen);
            pFilepath[nCopyLen] = 0x00;
            args.filepath = pFilepath;
        }
        else
        {
            goto END_PROC;
        }

        nLen = strlen(description);
        if (nLen > SF_REPORT_DESC_LIMIT)
        {
            nSendCnt = nLen / SF_REPORT_DESC_LIMIT;
            nRestSize = nLen % SF_REPORT_DESC_LIMIT;
            nNeedSendCnt = (nRestSize == 0) ? nSendCnt : (nSendCnt + 1);
            //args.descriptionsize = nLen;
            args.desc_partial_cnt = nNeedSendCnt;
            args.autodatafile = 1;
            nLeftSize = nLen;
            pSysTime = (Char*)sf_malloc(SF_REPORT_DATE_STR_SIZE + 1);
            if (pSysTime == NULL)
            {
                goto END_PROC;
            }
            SfdMakeSysTime(pSysTime, SF_REPORT_DATE_STR_SIZE + 1);
        }
        else
        {
            nNeedSendCnt = 1;
            nLeftSize = nLen;
            pSysTime = (Char*)sf_malloc(1);
            if (pSysTime == NULL)
            {
                goto END_PROC;
            }
            pSysTime[0] = 0x00;
        }
        args.desc_time = pSysTime;

        for (idx = 0; idx < nNeedSendCnt; idx++)
        {
            nCopyLen = (nLeftSize >= SF_REPORT_DESC_LIMIT) ? SF_REPORT_DESC_LIMIT : nLeftSize;
            
            pDescription = (Char*)sf_malloc(nCopyLen + 1);
            if (pDescription == NULL)
            {
                goto END_PROC;
            }
            
            sf_memcpy(pDescription, description + (idx * SF_REPORT_DESC_LIMIT), nCopyLen);
            pDescription[nCopyLen] = 0x00;
            args.descriptionsize = nCopyLen;
            args.description = pDescription;
            args.desc_partial_seq = idx;

            write_lock(&g_SfdSendQueueLock); 
            if (gSfpmdBinded)
            {
                result = SfSendReport(&args.header);        
            }
            else
            {
                // store to queue.
                pItem = (QueueFormat*)sf_malloc(sizeof(QueueFormat)); // malloc (item)
                if (!pItem)
                {
                    SF_LOG_E("[allocating memory error]");
                    write_unlock(&g_SfdSendQueueLock);
                    goto END_PROC;
                }
                pItem->sendfileflag = sendfileflag;
                pItem->pCaller = pCaller;
                pItem->pFileType = pFiletype;
                pItem->pFilePath = pFilepath;
                pItem->pDescription = pDescription;
                pItem->optype = optype;
                pItem->autodatafile = args.autodatafile;
                pItem->descriptionsize = args.descriptionsize;
                pItem->desc_partial_cnt = args.desc_partial_cnt;
                pItem->desc_partial_seq = args.desc_partial_seq;
                pItem->desc_time = args.desc_time;
                if (g_pDispatcher)
                {
                    list_add(&(pItem->list), &(g_pDispatcher->queue.noti_que));
                    if (nNeedSendCnt == 1) // If it needs to send data only 1 times.
                    {
                        write_unlock(&g_SfdSendQueueLock);
                        return 0;
                    }
                }
            } /* end if (gSfpmdBinded) */
            nLeftSize -= nCopyLen;
            write_unlock(&g_SfdSendQueueLock);
        } /* end for (idx = 0; idx < nNeedSendCnt; idx++) */
        
    } /* end if ( caller && filetype && filepath && description ) */
    
END_PROC:
    if (pCaller)
    {
        sf_free(pCaller);
    }
    if (pFiletype)
    {
        sf_free(pFiletype);
    }
    if (pFilepath)
    {
        sf_free(pFilepath);
    }
    if (pDescription)
    {
        sf_free(pDescription);
    }
    if (pSysTime)
    {
        sf_free(pSysTime);
    }
    return ((result == SF_STATUS_OK)? 0 : 1);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static int SfdSendQueueWorker(void* pData)
{
    SfdDispatcherContext* pDispatcher = NULL;
    QueueFormat* pItem = NULL;
    SfOperationSecurityReport args =
    {
        .header =
        {
            .size = sizeof(SfOperationSecurityReport),
            .type = 0
        },
        .caller       = NULL,
        .filetype     = NULL,
        .filepath     = NULL,
        .description  = NULL,
        .sendfileflag = 0
    };
    
    if (pData == NULL)
    {
        SF_LOG_E("[invalid param];");
        return 1;
    }
    pDispatcher = (SfdDispatcherContext*)pData;

    write_lock(&g_SfdSendQueueLock);
    if (g_pDispatcher)
    {
        if (!list_empty((const struct list_head *)&g_pDispatcher->queue.noti_que))
        {
            SF_LOG_I("queue is NOT empty");
            list_for_each_entry(pItem, &g_pDispatcher->queue.noti_que, list)
            {
                if (pItem && pItem->pCaller && pItem->pFileType && pItem->pFilePath && pItem->pDescription)
                {
                    args.header.type = pItem->optype;
                    args.caller = pItem->pCaller;
                    args.filetype = pItem->pFileType;
                    args.filepath = pItem->pFilePath;
                    args.description = pItem->pDescription;
                    args.sendfileflag = pItem->sendfileflag;
                    args.autodatafile = pItem->autodatafile;
                    args.descriptionsize = pItem->descriptionsize;
                    args.desc_partial_cnt = pItem->desc_partial_cnt;
                    args.desc_partial_seq = pItem->desc_partial_seq;
                    args.desc_time = pItem->desc_time;       
                    if (SfSendReport(&args.header) != SF_STATUS_OK)
                    {
                        SF_LOG_E("send queue message error.");
                    }
                }
                else
                {
                    SF_LOG_E("pItem and some data are NULL.");
                }
            } /* list_for_each_entry */
        }
        else
        {
            SF_LOG_I("queue is empty");
        }
    }
    write_unlock(&g_SfdSendQueueLock);

    return 0;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
int SfdSendReportInQueue(void* pData)
{
    SF_STATUS result = SF_STATUS_FAIL;
    static int sAlreadyBind = -1;
    write_lock(&g_SfdSendQueueLock);
    if (sAlreadyBind == -1)
    {
        gSfpmdBinded = 1; // sfpmd was binded.
        sAlreadyBind = gSfpmdBinded;
    }
    else if (sAlreadyBind == 1)
    {
        write_unlock(&g_SfdSendQueueLock);
        return 0;
    }
    write_unlock(&g_SfdSendQueueLock);

    if (g_pDispatcher)
    {
        g_pDispatcher->queue.kthread_noti_send_t = kthread_create(SfdSendQueueWorker,  (void*)g_pDispatcher, SFD_SEND_QUEUE_THREAD_NAME);
        if(IS_ERR(g_pDispatcher->queue.kthread_noti_send_t))
        {
            SF_LOG_E("[creating worker is failed.];");
            g_pDispatcher->queue.kthread_noti_send_t = NULL;
        }
        else
        {
            wake_up_process(g_pDispatcher->queue.kthread_noti_send_t);
            result = SF_STATUS_OK;
        }
    }
    return (result == SF_STATUS_OK)? 0 : 1;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdClearReportQueueList(void)
{
    QueueFormat* pItem = NULL;
    write_lock( &g_SfdSendQueueLock );
    if (g_pDispatcher)
    {
        list_for_each_entry(pItem, &g_pDispatcher->queue.noti_que, list)
        {
            if (pItem)
            {
                list_del(&pItem->list);
                if (pItem->pCaller)
                {
                    sf_free(pItem->pCaller);
                }
                if (pItem->pFileType)
                {
                    sf_free(pItem->pFileType);
                }
                if (pItem->pFilePath)
                {
                    sf_free(pItem->pFilePath);
                }
                if (pItem->pDescription)
                {
                    sf_free(pItem->pDescription);
                }
                if (pItem->desc_time)
                {
                    sf_free(pItem->desc_time);
                }
                sf_free(pItem);
            }
        }
    }
    write_unlock( &g_SfdSendQueueLock );
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_send_report_net(const char *caller, const char* filetype, const char *filepath, const char *description, const int sendfileflag)
{
    return SfdSendReport(caller, filetype, filepath, description, sendfileflag, SF_OPERATION_TYPE_REPORT_NET);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_send_report_process(const char *caller, const char* filetype, const char *filepath, const char *description, const int sendfileflag)
{
    return SfdSendReport(caller, filetype, filepath, description, sendfileflag, SF_OPERATION_TYPE_REPORT_PROCESS);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_send_report_file(const char *caller, const char* filetype, const char *filepath, const char *description, const int sendfileflag)
{
    return SfdSendReport(caller, filetype, filepath, description, sendfileflag, SF_OPERATION_TYPE_REPORT_FILE);
}

#endif // if defined(CONFIG_SECURITY_SFD)
