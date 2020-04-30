/**
****************************************************************************************************
* @file SfRulesList.c
* @brief Security framework [SF] filter driver [D] blocking rules list
* @date Created Sep 24, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include "SfRulesList.h"

#include <linux/types.h>
#include <asm/unistd.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include "dispatcher/SfdCache.h"

typedef struct
{
    struct list_head node;
    Uint64           fileInode;
} FileRule;

static DEFINE_RWLOCK(s_lockRules);
static LIST_HEAD(s_fileRulesList);

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS AddFileRule( Uint64 inode )
{
    SF_STATUS r = SF_STATUS_FAIL;
    FileRule* pRule = sf_malloc( sizeof(FileRule) );
    if ( pRule )
    {
        pRule->fileInode = inode;

        // append new rule to list under write lock
        write_lock( &s_lockRules );
        list_add_tail( &pRule->node, &s_fileRulesList );
        write_unlock( &s_lockRules );

        SF_LOG_I( "%s(): added file block rule for %lu", __FUNCTION__, inode );
        r = SF_STATUS_OK;
    }
    else
    {
        SF_LOG_E( "%s(): failed to allocate file rule", __FUNCTION__ );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
Bool FileAccessRestricted( Uint64 inode )
{
    Bool r = FALSE;
    FileRule* pRule = NULL;

    // check if file is blocked under read lock
    read_lock( &s_lockRules );
    list_for_each_entry( pRule, &s_fileRulesList, node )
    {
        if ( inode == pRule->fileInode )
        {
            SF_LOG_I( "%s(): rule is machted for %lu", __FUNCTION__, inode );
            r = TRUE;
            break;
        }
    }
    read_unlock( &s_lockRules );
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void ClearFileRulesList( void )
{
    // no locking here
    FileRule *p = NULL, *n = NULL;
    write_lock( &s_lockRules );
    list_for_each_entry_safe( p, n, &s_fileRulesList, node )
    {
        SfdCacheRemove( p->fileInode );
        list_del( &p->node );
        sf_free( p );
    }
    write_unlock( &s_lockRules );

    SF_LOG_I( "%s(): All of rules are deleted", __FUNCTION__ );
}