
#include "SfdConfiguration.h"
#include "uep/SfdUepHookHandlers.h"

#include <linux/sched.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/binfmts.h>
#include <linux/mount.h>
#include <linux/statfs.h>

#include "../../../fs/mount.h"



#ifdef  SF_CACHE_TEST
unsigned int g_sf_cache_test_ino;
#endif // SF_CACHE_TEST

/**
****************************************************************************************************
* @brief Number of prefix in the RW device name
****************************************************************************************************
*/
#define SFD_MAX_RWDEV   16
static char *g_rwdevPrefixes[SFD_MAX_RWDEV];
static int g_rwdevCount = 0;


/*
****************************************************************************************************
* SfInitConfig
****************************************************************************************************
*/
SF_STATUS SfdInitConfig( void )
{
    char  * prefixes = CONFIG_SECURITY_SFD_RWDEV_PREFIXES;

    // Init filter path prefix
    if( g_rwdevCount == 0 )
    {
        g_rwdevPrefixes[g_rwdevCount++] = prefixes;
        for(;*prefixes;prefixes++)
        {
            if(*prefixes==':')
            {
                prefixes++;
                g_rwdevPrefixes[g_rwdevCount++] = prefixes;
            }
        }
    }

    SF_LOG_I( "[%s] prefix:%s, count:%d", __FUNCTION__, prefixes, g_rwdevCount);

    return SF_STATUS_OK;
}

/*
****************************************************************************************************
* SfCheckPrefix
****************************************************************************************************
*/
char * SfdCheckPathPrefix(char * path)
{
    int i=0,j=0;
    char * prefix = NULL;
    if( g_rwdevCount > 0 )
    {
        for(i=0; i<g_rwdevCount; i++)
        {
            prefix = g_rwdevPrefixes[i];
            for(j=0; *(path+j) != 0 && *(prefix+j) == *(path+j); j++);

            if( *(prefix+j) == ':' || *(prefix+j) == 0 )
                return path;
        }   
    }

    return NULL;
}



extern int vfs_statfs(struct path *, struct kstatfs *);


/*
****************************************************************************************************
* Check
****************************************************************************************************
*/
SF_STATUS SfdCheckFileIsInRW(struct file* pFile, const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_FAIL;
#if defined(CONFIG_SECURITY_SFD_CHECK_STATFS)
    struct kstatfs stat;

    if( unlikely( NULL == pFile ) )
    {
        return SF_STATUS_BAD_ARG;
    }


    // Check real mount point.
    // These routine should be included in release image or for performance team
    result = vfs_statfs(&pFile->f_path, &stat);
    if (result != 0)
    {
        SF_LOG_E( "%s Failed to vfs_statfs: result:%d", __FUNCTION__, result);
        return result;
    }

    return stat.f_flags & ST_RDONLY ? SF_STATUS_FAIL: SF_STATUS_OK;

#else // CONFIG_SECURITY_SFD_CHECK_STATFS
    Char* pBuffer = NULL;
    Char* pName = NULL;
    char fsname[5] = "null";

    if( unlikely( NULL == pFile ) )
    {
        return SF_STATUS_BAD_ARG;
    }

    // Check just file path prefix
    // These routine can be used in debug or perf image
    pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
    if ( NULL != pBuffer )
    {
        if (pOperationInterface)
        {
            if (strlen(pName) > ARRAY_SIZE(fsname) - 1)
            {
                memcpy(fsname, pName, ARRAY_SIZE(fsname) - 1);
                PushMountInfo(SF_ADD_MNT_INFO, pOperationInterface, fsname, 0, NULL, 0);
            }
        }
        //SF_LOG_I( "%s path:%s", __FUNCTION__, pName);
        if ( SfdCheckPathPrefix( pName ) )
        {
            // the file is in RW
            result = SF_STATUS_OK;
        } 
        sf_free( pBuffer);
        pBuffer = NULL;
    }
    else
    {
        SF_LOG_E( "[%s] Failed to get path!!! (0x%X)", __FUNCTION__, pBuffer );
    }
    return result;
#endif // CONFIG_SECURITY_SFD_CHECK_STATFS
}

/*
****************************************************************************************************
* Check whether the path is included in RwPrefix info, and print mount info. 
****************************************************************************************************
*/
SF_STATUS SfdCheckFileIsInRWUsingPath(struct file* pFile, Char* pName)
{
    const char * pMntName = NULL;
    struct path* pPath = NULL;
    SF_STATUS result = SF_STATUS_OK;

    if( unlikely( NULL == pFile ) )
    {
        return SF_STATUS_BAD_ARG;
    }

    // Check just file path prefix
    // These routine can be used for printing abnormal situation in Release image.
    if ( NULL != pName )
    {
        // the file is in RO area.
        pPath = &pFile->f_path;
        if (pPath)
        {
            path_get(pPath);
            if ( pPath->mnt != NULL && pPath->mnt->mnt_sb != NULL 
                && pPath->mnt->mnt_sb->s_type != NULL && pPath->mnt->mnt_sb->s_type->name != NULL )
            
            {
                pMntName = pPath->mnt->mnt_sb->s_type->name;
                SF_LOG_E("[%s] Unexpected mount file:%s, mnt_floags:%x, filesystem:%c%c%c%c",                         
                    __FUNCTION__, pName, pPath->mnt->mnt_flags, pMntName[0], pMntName[1], pMntName[2], pMntName[3]);
            }
            else
            {
                SF_LOG_E( "[%s] Unexpected Mount !!! But parameters are invalid.", __FUNCTION__);
            }
            path_put(pPath);
        }
    }
    else
    {
        SF_LOG_E( "[%s] Failed to get path!!! (0x%X)", __FUNCTION__, pName );
    }
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SFAPI SfdConstructAbsoluteFileNameByPath(struct path* const pPath, Char** const ppName)
{
    char* pBuffer = NULL;

    do
    {
        if (NULL == pPath || NULL == ppName)
        {
            SF_LOG_E( "%s takes invalid argument (pFile = %p, ppName = %p)",
                __FUNCTION__, pPath, ppName );
            break;
        }

        // Allocate buffer to be returned
        pBuffer = sf_malloc(PATH_MAX);

        if (NULL == pBuffer)
        {
            // Comment out considering that there is no free memory
            // SF_LOG_E("%s can not allocate memory with PATH_MAX = %d", __FUNCTION__, PATH_MAX);
            break;
        }

        path_get(pPath);

        /**
        * @brief Construct absolute file name *ppName is a pointer in the pBuffer array in case
        *   of successfull operation.
        */
        *ppName = d_path(pPath, pBuffer, PATH_MAX);

        if (IS_ERR(*ppName))
        {
            path_put(pPath);
            sf_free(pBuffer);
            pBuffer = NULL;
            SF_LOG_E("%s Failed to d_path", __FUNCTION__);
            break;
        }

        path_put(pPath);

    } while(FALSE);

    return pBuffer;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SfdConstructAbsoluteFileNameByFile(struct file* const pFile, Char** const ppName)
{
    Char* pBuffer = NULL;

    do
    {
        if ( NULL == pFile || NULL == ppName )
        {
            SF_LOG_E( "[%s] takes invalid argument (pFile = %p, ppName = %p)",
                __FUNCTION__, pFile, ppName );
            break;
        }

        pBuffer = SfdConstructAbsoluteFileNameByPath( &pFile->f_path, ppName);

    } while( FALSE );

    return pBuffer;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SFAPI SfdConstructAbsoluteFileNameByTask(const struct task_struct* const pProcessContext,
	Char** const ppName)
{
    Char* pBuffer = NULL;
    struct mm_struct* pProcessMemoryMap;

    do
    {
        if ( NULL == pProcessContext || NULL == ppName )
        {
            SF_LOG_E("[%s] takes invalid argument (pProcessContext = %p, ppName = %p)",
                __FUNCTION__, pProcessContext, ppName);
            break;
        }

        pProcessMemoryMap = pProcessContext->mm;

        if ( NULL == pProcessMemoryMap )
        {
            // SF_LOG_E( "[%s] can not get process memory map", __FUNCTION__ );
            break;
        }

        down_read( &pProcessMemoryMap->mmap_sem );
        if (NULL != pProcessMemoryMap->exe_file)
        {
            /**
            * @ brief Each process created from and executable file (process image) exe_file used
            *   to find absolute path to process image.
            */
            pBuffer = SfdConstructAbsoluteFileNameByPath( &pProcessMemoryMap->exe_file->f_path,
                ppName );
        }
        else
        {
            SF_LOG_E( "[%s] can not get process image (exe_file)", __FUNCTION__ );
        }
        up_read( &pProcessMemoryMap->mmap_sem );
    } while( FALSE );

    return pBuffer;
}

