#ifndef __SFD_CACHE_H__
#define __SFD_CACHE_H__


#include <uapi/linux/sf/core/SfCore.h>
#include "SfdModuleInterface.h"
#include <linux/fs.h>


#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_USE_CACHE)

/**
****************************************************************************************************
* @brief Initalize cache for inode number.
* @note Call this function before use the other functions.
* @return SF_STATUS_OK if success, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheInit( void )
{
	return SF_STATUS_OK;
}


/**
****************************************************************************************************
* @brief Deinitalize cache for inode number
* @note The function should be called before exit sfd.
****************************************************************************************************
*/
static inline void SFAPI SfdCacheDeinit( void )
{
	return;
}


/**
****************************************************************************************************
* @brief Add result of module to cache table.
* @note If there is already result for the module, result value will be updated.
* @param [in] inode inode 
* @param [in] status result value from module. the value is stored in cache table
* @param [in] level kUEP sign Level
* @return SF_STATUS_OK if result is stored successfully, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheAdd(struct inode * pInode, SF_STATUS status, Uint8 uepLevel)
{
	if( likely(pInode) )
	{
		pInode->sfd_status = (u16) status;
		pInode->sfd_level = (u8) uepLevel;
		return 0;
	}
	else
	{
		return 	-EINVAL;
	}
}


/**
****************************************************************************************************
* @brief Check wether inode is already checked by the module
* @param [in] pInode inode
* @param [inout] pStatus status
* @param [inout] pUepLevel uel level
* @return SF_STATUS_OK if inode already checked, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheCheck(struct inode * pInode, SF_STATUS *pStatus, Uint8 *pUepLevel)
{
	if( likely( pInode && pStatus && pUepLevel ) )
	{
		if( 0 != pInode->sfd_level )
		{
			*pStatus = (SF_STATUS) pInode->sfd_status;
			*pUepLevel = (Uint8) pInode->sfd_level;
			return SF_STATUS_OK;
		}
		return SF_STATUS_FAIL;
	}
	else
	{
		return 	-EINVAL;
	}
}

/**
****************************************************************************************************
* @brief Remove cache node from cache table
* @note If inode is changed, the function should be called
* @param [in] pInode inode
* @return SF_STATUS_OK if file was found in cached data, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheRemove(struct inode * pInode)
{
	if( likely(pInode) )
	{
		pInode->sfd_status = SF_STATUS_OK;
		pInode->sfd_level = 0;
		return SF_STATUS_OK;
	}
	else
	{
		return -EINVAL;
	}
}

#else // SFD && USE_CACHE


/**
****************************************************************************************************
* @brief Initalize cache for inode number.
* @note Call this function before use the other functions.
* @return SF_STATUS_OK if success, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheInit( void )
{
	// Not using cache, do nothing
	return SF_STATUS_OK;
}


/**
****************************************************************************************************
* @brief Deinitalize cache for inode number
* @note The function should be called before exit sfd.
****************************************************************************************************
*/
static inline void SFAPI SfdCacheDeinit( void )
{
	// Not using cache, do nothing
}


/**
****************************************************************************************************
* @brief Add result of module to cache table.
* @note If there is already result for the module, result value will be updated.
* @param [in] i_ino inode number which module checks
* @param [in] moduleType type of module which checks the inode
* @param [in] status result value from module. the value is stored in cache table
* @return SF_STATUS_OK if result is stored successfully, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheAdd(struct inode * pInode, u16 status, u8 uepLevel)
{
	// Not using cache, do nothing
	return SF_STATUS_OK;
}


/**
****************************************************************************************************
* @brief Check wether inode is already checked by the module
* @param [in] i_ino inode number
* @param [in] moduleType type of module
* @param [in] pStatus the value stored in cache table if return value is SF_STATUS_OK
* @param [in] pLevel kUEP sign Level
* @param [in] bIsRW RW is True RO is false
* @return SF_STATUS_OK if inode already checked, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheCheck(struct inode * pInode, SF_STATUS *pStatus, Uint8 *pUepLevel)
{
	// CAUTION: If not use cache, this function always has to return FAIL;
	return SF_STATUS_FAIL;
}

/**
****************************************************************************************************
* @brief Remove cache node from cache table
* @note If inode is changed, the function should be called
* @param [in] i_ino inode number
* @return SF_STATUS_OK if file was found in cached data, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
static inline SF_STATUS SFAPI SfdCacheRemove(struct inode * pInode)
{
	// Not using cache, do nothing
	return SF_STATUS_OK;
}


#endif // SFD && USE CACHE

#endif // FILE

