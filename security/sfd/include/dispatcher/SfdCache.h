#ifndef __SFD_CACHE_H__
#define __SFD_CACHE_H__


#include <uapi/linux/sf/core/SfCore.h>
#include "SfdModuleInterface.h"


#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_USE_CACHE)

/**
****************************************************************************************************
* @brief Initalize cache for inode number.
* @note Call this function before use the other functions.
* @return SF_STATUS_OK if success, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheInit( void );


/**
****************************************************************************************************
* @brief Deinitalize cache for inode number
* @note The function should be called before exit sfd.
****************************************************************************************************
*/
void SFAPI SfdCacheDeinit( void );


/**
****************************************************************************************************
* @brief Add result of module to cache table.
* @note If there is already result for the module, result value will be updated.
* @param [in] i_ino inode number which module checks
* @param [in] moduleType type of module which checks the inode
* @param [in] status result value from module. the value is stored in cache table
* @param [in] pLevel kUEP sign Level
* @param [in] bIsRW RW is True RO is false
* @return SF_STATUS_OK if result is stored successfully, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheAdd(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS status, Uint8 uepLevel, bool bIsRW);


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
SF_STATUS SFAPI SfdCacheCheck(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS *pStatus, Uint8 *pUepLevel, bool bIsRW);

/**
****************************************************************************************************
* @brief Remove cache node from cache table
* @note If inode is changed, the function should be called
* @param [in] i_ino inode number
* @return SF_STATUS_OK if file was found in cached data, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCacheRemove(unsigned long long i_ino);

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
static inline SF_STATUS SFAPI SfdCacheAdd(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS status)
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
static inline SF_STATUS SFAPI SfdCacheCheck(unsigned long long i_ino, SFD_MODULE_TYPE moduleType, SF_STATUS *pStatus, Uint32 *pLevel, bool bIsRW)
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
static inline SF_STATUS SFAPI SfdCacheRemove(unsigned long long i_ino)
{
	// Not using cache, do nothing
	return SF_STATUS_OK;
}


#endif // SFD && USE CACHE

#endif // FILE

