/**
****************************************************************************************************
* @file SfdDispatcherHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handler for system calls implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Dmitriy Dorogovtsev (d.dorogvtse@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
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
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_SECURECONTAINER)
#include <linux/pid_namespace.h>
#endif

#include "SfdCache.h"
#include "uep/SfdUep.h"

#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER
#include "../../smack/smack.h"
#define SECURECONTAINER_LABEL '!'
#endif

#if defined(SF_CACHE_TEST)
extern unsigned int g_sf_cache_test_ino;
#endif // SF_CACHE_TEST

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

#ifndef SF_DEMO
    // do not filter open() event with SFD before 5 min from system boot
    if ( time_before( jiffies, openEventFilterDelay ) )
        return 0;
#endif // SF_DEMO

    if ( pFile && pCredentials )
    {
        result = SfdCheckFileIsInRW( pFile );
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
            uino = SfdGetUniqueIno(pFile->f_dentry->d_inode);

            // Check that the file is already processed.
            if( SF_SUCCESS( SfdCacheCheck( uino, SFD_MODULE_TYPE_DISPATCHER, &result, NULL ) ) )
            {
                if( SF_STATUS_PENDING != result )
                {
                    return SfReturn( result );
                }
            }

            // Not checked, proceed it.
            result = SfdProcessOperationThroughModules( &args.header );

            // Add the result of processing to cache
            if( SF_FAILED( SfdCacheAdd( uino, SFD_MODULE_TYPE_DISPATCHER, result, '0' ) ) )
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
    struct inode_smack *sip;
    unsigned long long uino = 0;
    int res = 0;


    if( NULL != inode )
    {
        uino = SfdGetUniqueIno(inode);
        

#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER

        sip = inode->i_security;
        if(sip->smk_inode[0] == SECURECONTAINER_LABEL)
        {
            if( SFD_UEP_LEVEL_SECURECONTAINER > current->uepLevel )
            {
                //SF_LOG_I( "[sf_security_inode_permission] !! Blocked accessing to SecureContainer. pid:%u, uepLevel:%c, ino:%d, dev:%d",current->pid, current->uepLevel, inode->i_ino, inode->i_rdev ); 
#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
                res = -EPERM;
#endif // CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
            }
            else
            {
                //SF_LOG_I( "[sf_security_inode_permission] Allow accessing to SecureContainer. pid:%u, uepLevel:%c, ino:%d, dev:%d",current->pid, current->uepLevel, inode->i_ino, inode->i_rdev ); 
            }
        }
#endif // CONFIG_SECURITY_SFD_SECURECONTAINER
        
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
    
    if( unlikely(name == NULL || path == NULL || func == NULL) )
    {
        SF_LOG_W("[%s] invalid arg, name:0x%X, path:0x%X, func:0x%X",
            __FUNCTION__, name, path, func );
        return 0;
    }
    sip = path->dentry->d_inode->i_security;

    if(sip->smk_inode[0] == SECURECONTAINER_LABEL)
    {
        if( SFD_UEP_LEVEL_SECURECONTAINER > current->uepLevel )
        {
            //SF_LOG_I("[%s:%s] Blocked!! pid:%u, comm:%s, kuep:%c, path:%s", __FUNCTION__, func,current->pid, current->comm, current->uepLevel, name );
#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE        
            res = -EPERM; 
#endif // CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
        }
        else
        {
            //SF_LOG_I("[%s:%s] Pass. pid:%u, comm:%s, kuep:%c, path:%s", __FUNCTION__, func,current->pid, current->comm, current->uepLevel, name );
        }
    }

    return res;
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_create_new_namespaces(unsigned long flags,
    struct task_struct *tsk, struct user_namespace *user_ns,
    struct fs_struct *new_fs)
{
    int res = 0;
    static int nDefaultSpaces = 1;

    if( nDefaultSpaces == 0 )
    {
        if( SFD_UEP_LEVEL_SECURECONTAINER != current->uepLevel )
        {

            SF_LOG_E("[%s] Blocked!! pid:%u, comm:%s, kuep:%c", 
                    __FUNCTION__, current->pid, current->comm, current->uepLevel);
    #ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE        
            res = -EPERM;
    #endif // CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
        }
        else
        {
            //SF_LOG_I("[%s] Pass. pid:%u, comm:%s, kuep:%c",__FUNCTION__, current->pid, current->comm, current->uepLevel );
        }    
    }
    else
    {
        nDefaultSpaces--;
    }   

    return res;
}

int sf_process_authorized(struct task_struct *cur, struct task_struct *tsk)
{
    struct pid_namespace *current_pid_ns = task_active_pid_ns(cur);
    struct pid_namespace *target_pid_ns = task_active_pid_ns(tsk);
#if 0
    static pid_t pre_current_pid = 0;
    static pid_t pre_parent_pid = 0;
#endif
    if ((current_pid_ns != target_pid_ns) && ( SFD_UEP_LEVEL_SECURECONTAINER != cur->uepLevel ))
    {
        #if 0
        if((cur->parent != NULL) && ((pre_current_pid != cur->pid) || (pre_parent_pid != cur->parent->pid)))
        {
            pre_current_pid = cur->pid;
            pre_parent_pid = cur->parent->pid;
            SF_LOG_I("[%s] securezone access process, cur pid:%u, comm:%s, kuep:%c, parent pid:%u, comm:%s, kuep:%c", __FUNCTION__, cur->pid, cur->comm, cur->uepLevel,cur->parent->pid,cur->parent->comm,cur->parent->uepLevel);
        }
        #endif
        #ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
        return FALSE;
        #endif
    }
    else if ((SFD_UEP_LEVEL_SECURECONTAINER != cur->uepLevel ) && (SFD_UEP_LEVEL_SECURECONTAINER == tsk->uepLevel ))
    {
        #if 0
        if((cur->parent != NULL) && ((pre_current_pid != cur->pid) || (pre_parent_pid != cur->parent->pid)))
        {
            pre_current_pid = cur->pid;
            pre_parent_pid = cur->parent->pid;
            SF_LOG_I("[%s] securezone access process, cur pid:%u, comm:%s, kuep:%c, parent pid:%u, comm:%s, kuep:%c", __FUNCTION__, cur->pid, cur->comm, cur->uepLevel,cur->parent->pid,cur->parent->comm,cur->parent->uepLevel);
        }
        #endif
        #ifdef CONFIG_SECURITY_SFD_SECURECONTAINER_MODE_ENFORCE
        return FALSE;
        #endif
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
                .bCheckAlways = 0
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
            .bCheckAlways = 1
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