/**
****************************************************************************************************
* @file SfdCache.c
* @brief Security framework [SF] filter driver [D] Caching routine implementation
* @author ChangWoo Lee (jason77.lee@samsung.com)
* @date Created Mar 1, 2015
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2015. All rights reserved.
****************************************************************************************************
*/

#include "SfdCache.h"
#include <linux/fs.h>
#include <linux/hashtable.h>

//#define _SFD_CACHE_TEST_

#ifdef _SFD_CACHE_TEST_
#define SFD_CACHE_HASH_BITS	1
#else // _SFD_CACHE_TEST_
#define SFD_CACHE_HASH_BITS	9
#endif // _SFD_CACHE_TEST_

static DEFINE_HASHTABLE(g_SfdCacheHashTable, SFD_CACHE_HASH_BITS);


struct SfdCacheNode {
	struct hlist_node hlist;
	unsigned long long i_ino;
	unsigned long node_jiffies;
	Uint8 uepLevel;
	bool mntInfo;
	SF_STATUS results[SFD_MODULE_TYPE_MAX];
};

static DEFINE_RWLOCK(g_SfdCacheLock);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static struct SfdCacheNode * SfdCacheAllocNode( void )
{
	int i = 0;
	struct SfdCacheNode * pNode = sf_malloc(sizeof(struct SfdCacheNode));
	
	if( NULL != pNode )
	{
		pNode->i_ino = 0;
		pNode->node_jiffies = 0;
		pNode->uepLevel = 0;
		pNode->mntInfo = TRUE;
		for( i = 0 ; i < SFD_MODULE_TYPE_MAX; i++ )
			pNode->results[i] = SF_STATUS_PENDING;
	}

	return pNode;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
#define SfdCacheFreeNode(x)	sf_free(x)

#ifdef _SFD_CACHE_TEST_
/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdCacheTestListup()
{
	struct SfdCacheNode * pCache;
	int bucket = 0;
	bool bIsRW = TRUE;

	read_lock( &g_SfdCacheLock );
	printk( "SfdCacheTestListup entry\n" );
	hash_for_each( g_SfdCacheHashTable, bucket, pCache, hlist )
		printk( "SfdCacheTestListup each_entry bucket[%d], i_no:%u, result[0]:%u\n", bucket, pCache->i_ino, pCache->results[0] );
	read_unlock( &g_SfdCacheLock );	
}

void SfdCacheTest()
{
	int i = 0;
	SF_STATUS status = SF_STATUS_FAIL;

	for( i = 0 ; i < 5 ; i++ )
	{
		SfdCacheAdd( i+1, SFD_MODULE_TYPE_DISPATCHER, SF_STATUS_FAIL, false );
	}

	printk( "SfdCacheTest test nodes added\n" );
	SfdCacheTestListup();

	if( SF_SUCCESS( SfdCacheCheck( 1, SFD_MODULE_TYPE_DISPATCHER, &status, &bIsRW ) ) && SF_STATUS_PENDING != status )
		printk( "SfdCacheTest Test Case #1 PASSED, status:%u, mount:%d \n", status, bIsRW );
	else
		printk( "SfdCacheTest Test Case #1 FAILED\n");

	if( SF_SUCCESS( SfdCacheCheck( 1, SFD_MODULE_TYPE_NOTIFIER, &status, &bIsRW ) ) && SF_STATUS_PENDING == status )
		printk( "SfdCacheTest Test Case #2 PASSED\n" );
	else
		printk( "SfdCacheTest Test Case #2 FAILED\n");
	
	SfdCacheRemove(1);
	printk( "SfdCacheTest first node removed\n" );
	SfdCacheTestListup();


	if( SF_FAILED( SfdCacheCheck( 1, SFD_MODULE_TYPE_DISPATCHER, &status, &bIsRW ) ) )
		printk( "SfdCacheTest Test Case #3 PASSED, status:%u, mount : %d\n", status, bIsRW );
	else
		printk( "SfdCacheTest Test Case #3 FAILED\n");


	SfdCacheDeinit();

	printk( "SfdCacheTest deinitalized\n" );
	SfdCacheTestListup();
}


#endif // _SFD_CACHE_TEST_


#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_USE_CACHE)



/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheInit( void )
{
#ifdef _SFD_CACHE_TEST_
	SfdCacheTest();
#endif // _SFD_CACHE_TEST

	return 0;
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SFAPI SfdCacheDeinit( void )
{
	struct SfdCacheNode * pCache;
	struct hlist_node *tmp;
	int bucket = 0;


	SF_LOG_I( "SfdCacheDeinit called\n" );

	write_lock( &g_SfdCacheLock );
	hash_for_each_safe( g_SfdCacheHashTable, bucket, tmp, pCache, hlist )
	{
		hash_del( &pCache->hlist );
		SfdCacheFreeNode( pCache );
	}
	write_unlock( &g_SfdCacheLock );

	return;
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheAdd(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS status, Uint8 uepLevel, bool bIsRW)
{
	struct SfdCacheNode * pCache	= NULL;
	struct SfdCacheNode * pNewCache	= NULL;
	struct hlist_node *tmp 			= NULL;
	SF_STATUS result = SF_STATUS_FAIL;
	int shouldFree = 1;

	if( 0 == i_ino || !SFD_IS_VALID_MODULE_TYPE(moduleType) )
	{
		return 	-EINVAL;
	}

	pNewCache = SfdCacheAllocNode();

	// find node
	write_lock( &g_SfdCacheLock );
	hash_for_each_possible_safe( g_SfdCacheHashTable, pCache, tmp, hlist, i_ino )
	{
		if( i_ino == pCache->i_ino )
		{
			pCache->results[moduleType] = status;
			pCache->mntInfo = bIsRW;
			if( pCache->uepLevel < uepLevel )
				pCache->uepLevel = uepLevel;
			pCache->node_jiffies = jiffies;
#ifdef SFD_CACHE_DEBUG
			SF_LOG_I( "SfdCacheAdd Update type:%u, i_ino:%u, status:%u, mount:%d\n",
						moduleType, pCache->i_ino, pCache->results[moduleType], pCache->mntInfo );
#endif // #ifdef SFD_CACHE_DEBUG
			result = SF_STATUS_OK;
			break;
		}
	}

	if( SF_FAILED( result ) )
	{
		if( NULL != pNewCache )
		{
			pNewCache->i_ino = i_ino;
			pNewCache->uepLevel = uepLevel;
			pNewCache->results[moduleType] = status;
			pNewCache->mntInfo = bIsRW;
			pNewCache->node_jiffies = jiffies;
            
			hash_add_rcu( g_SfdCacheHashTable, &pNewCache->hlist, pNewCache->i_ino );

			shouldFree = 0;

			result = SF_STATUS_OK;
#ifdef SFD_CACHE_DEBUG
			SF_LOG_I( "SfdCacheAdd Added type:%u, i_ino:%u, status:%u, mount:%d\n",
						moduleType, pNewCache->i_ino, status, bIsRW );
#endif // SFD_CACHE_DEBUG
		}
		else
		{
			SF_LOG_E( "SfdCacheAdd not enough memory\n" );
			result = -ENOMEM;
		}	
	}

	write_unlock( &g_SfdCacheLock );

	if( NULL != pNewCache && 1 == shouldFree )
	{
		SfdCacheFreeNode( pNewCache );
		pNewCache = NULL;
	}
	

	return result;
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheCheck(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS * pStatus, Uint8 *pUepLevel, bool bIsRW)
{
	struct SfdCacheNode * pCache = NULL;
	struct hlist_node *tmp;
	const Ulong CacheOldTimeInterval  = 15 * 60 * HZ;
	SF_STATUS result = SF_STATUS_FAIL;

	if( 0 == i_ino || NULL == pStatus )
	{
		return 	-EINVAL;
	}

	*pStatus = SF_STATUS_PENDING;

	write_lock( &g_SfdCacheLock );
	hash_for_each_possible_safe( g_SfdCacheHashTable, pCache, tmp, hlist, i_ino )
	{
		if( i_ino == pCache->i_ino )
		{
			*pStatus = pCache->results[moduleType];
			if( NULL != pUepLevel )
				*pUepLevel = pCache->uepLevel;

			if( bIsRW != pCache->mntInfo )
				*pStatus = SF_STATUS_FAIL;
			/*
			SF_LOG_I( "SfdCacheCheck type:%u, Found i_ino:%u, pStatus:%u, bIsRw : %d\n",
					moduleType, pCache->i_ino, *pStatus, bIsRw );
			//*/
			pCache->node_jiffies = jiffies;
			result = SF_STATUS_OK;
			//break;
		}
		else
		{
			if (pCache)
			{
				if (time_after(jiffies, pCache->node_jiffies + CacheOldTimeInterval))
				{
					hash_del( &pCache->hlist );
					SfdCacheFreeNode( pCache );
				}
			}
		}
	}
	write_unlock( &g_SfdCacheLock );

	if( SF_FAILED( result ) )
	{
#ifdef SFD_CACHE_DEBUG
		printk( "SfdCacheCheck Not found i_ino:%u\n", i_ino );
#endif // #ifdef SFD_CACHE_DEBUG
	}

	return result;
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheRemove(unsigned long long i_ino)
{
	struct SfdCacheNode * pCache = NULL;
	struct hlist_node *tmp;
	SF_STATUS result = SF_STATUS_FAIL;

	if( 0 == i_ino )
	{
		return 	-EINVAL;
	}

	write_lock( &g_SfdCacheLock );
	hash_for_each_possible_safe( g_SfdCacheHashTable, pCache, tmp, hlist, i_ino )
	{
		if( i_ino == pCache->i_ino )
		{
#ifdef SFD_CACHE_DEBUG
			SF_LOG_I( "SfdCacheRemove i_no:%u\n", pCache->i_ino );
#endif // #ifdef SFD_CACHE_DEBUG
			hash_del( &pCache->hlist );
			SfdCacheFreeNode( pCache );
			result = SF_STATUS_OK;
			break;
		}
	}
	write_unlock( &g_SfdCacheLock );

	if( SF_FAILED( result ) )
	{
#ifdef SFD_CACHE_DEBUG
		SF_LOG_I( "SfdCacheRemove Not found i_no:%u\n", i_ino );
#endif // #ifdef SFD_CACHE_DEBUG
	}

	return result;
}

#endif // SFD && USE_CACHE


