/**
****************************************************************************************************
* @file SfdUepHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handlers for system calls that processed by
*   UEP submodule.
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @author ChangWoo Lee (jason77.lee@samsung.com)
* @date Created Apr 10, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfCore.h>
#include <uapi/linux/sf/protocol/SfEnvironmentFormat.h>
#include <uapi/linux/sf/protocol/SfOperationsFormat.h>

#include "uep/SfdUepHookHandlers.h"
#include "uep/UepConfig.h"
#include "uep/UepKey.h"
#include "uep/UepSignatureContext.h"
#include "dispatcher/SfdCache.h"

#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/elf.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/mount.h>

#include "SfdConfiguration.h"
#include "SfdUep.h"
#include "uep/base64.h"

#define	SF_PEMAG0		'M'
#define	SF_PEMAG1		'Z'

/**
* @note Guidlines for management of pubkeyN
*   The number of pubkey should be maintained.
*   Do not change index order in Array.
*   In case of adding new key, add at the end of array with comment.
*/
static const Char* pubkeyN[] ={ /*17*/"",
                                /*18*/
                                "0x00b05d2631818fd247019f5c546eba"
                                "514a52690d940842c34d600f7b3129"
                                "c0936c3438785f9128bdabbb46d381"
                                "c2178baf69441b43bc23f1b5d1f14a"
                                "a84f4d6d013d46d9b730a08f550c9c"
                                "40cf3ec736c5f58d105a8a6228ff90"
                                "b4ecb9cdbe521724bf834c2ed0d529"
                                "962f3d1c5b88f7a38541ccc1666588"
                                "6a34e6eb9c61fae68f833ecbc3082b"
                                "a3caa07d945b4916ddcffacac6d29a"
                                "1d372eb854405c1461cd5600880f23"
                                "76d9b9e9f625f591fad88cb51d565e"
                                "6ab5cac6149b153bf0f8919a6ea48f"
                                "c5cd2b4dc33881b28237cade360f11"
                                "ed9f297e6ca79a17c5925d93142c30"
                                "e3c2faefe89df8a2e9178afaaf01db"
                                "862882b7ef68eaba6e3b8987b93402"
                                "58fb",
                                /*19*/
                                "0x00eda57178fb1772a564f3395450b9"
                                "a9107129965e9d30bcd5f0c2a62b3c"
                                "6027773b857edf1336c446d80c0e54"
                                "edc8b87e0e7e997bb3850e54730184"
                                "2bd5a10fc183d29a37c4b1e3569874"
                                "2faccaa740118b564f81d0a881215e"
                                "4f5bba83fa3c8d4b312eac5896085b"
                                "2ea15e88dd94a0beacac012e12d37f"
                                "4a76f9f45f02ebddb8f8ec2a855384"
                                "f9b7c45fdfedacd26860a7aecddb38"
                                "ae945bd709e5423787a4a28f9bf216"
                                "e19f4b9fbefa635ade7e282072023e"
                                "9afd811be23286a754f84bf3ae0b22"
                                "abcd47d9dd265e5fb3da95383102af"
                                "7fa2b6ca935a7a55d04d38d656359a"
                                "b81f0c5b58af5fb5c5a8e3922bc8e8"
                                "7191df6eafc049f1d34d9e58351a4e"
                                "af0f"
                                };


static const Char* pubkeyE = "0x10001";
//--------------------------------------------------------------------------------------------------

//static DECLARE_RWSEM(s_uepRwsem);
static Int   s_uepStatus = 1;
static Uint8 s_duidHash[ HASH_ALGO_LENGTH ] = { 0 };

static const char c_uep_lead_meta [] = "\n#UEP30";
static const char c_func_read_error1[] = "SF_FUNC_CHECK_FILE_TYPE";
static const char c_func_read_error2[] = "SF_FUNC_READ_SIGN";
static const char c_func_read_error3[] = "SF_FUNC_CHECK_SIGN";
#define SF_ERR_ARRAY_SIZE 6

#define UEP_LEAD_META_LEN sizeof( c_uep_lead_meta ) - 1

#define CHECK_VALID_UEP_VERSION(KEY,VERSION)     (VERSION-'0') - ARRAY_SIZE(KEY) >= 1 && (VERSION-'0') - ARRAY_SIZE(KEY) <= 2 ? true : false
/**
****************************************************************************************************
* @brief file type
****************************************************************************************************
*/
typedef enum
{
    SF_FILE_TYPE_UNKNOWN = 0,
    SF_FILE_TYPE_ELF,
    SF_FILE_TYPE_MAX,
    SF_FILE_TYPE_SCRIPT, // support script someday
    SF_FILE_TYPE_PE // windows pe
} SF_FILE_TYPE;

#define IS_SF_UEP_TARGET_TYPE(x) ((x) > SF_FILE_TYPE_UNKNOWN && (x) < SF_FILE_TYPE_MAX)

//--------------------------------------------------------------------------------------------------

typedef struct
{
    Uint8   uepVersion;            ///< UEP signature Version
    Uint8   uepLevel;             ///< Level(Normal, SecureContainer)
    Char*   signature;            ///< Null-terminated signature in hex
    SF_FILE_TYPE fileType;        ///< File type (elf, script)
    Uint32  signatureLength;      ///< Signature length
    Uint32  signatureOffset;      ///< Signature offset in file
} UepSignatureInfo;

/**
****************************************************************************************************
* @brief                Get file size from inode structure
* @param [in] pFile     Pointer to the file
* @return               Value returned by the i_size_read(struct inode*) function
****************************************************************************************************
*/
static inline Uint64 SfdUepGetFileSize( const struct file* pFile )
{
    return i_size_read( pFile->f_inode );
}

/**
****************************************************************************************************
* @brief                Read data from file
* @param [in] pFile     Pointer to the file
* @param [in] offset    Offset to read file
* @param [out] pBuffer  Pointer to the output buffer
* @param [in] size      Size of the file to read
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_STATUS SfdUepReadFile( struct file* pFile, unsigned long offset, unsigned char* pBuffer,
                                 unsigned long size, int* retErr )
{
    SF_STATUS result = SF_STATUS_FAIL;
    Int ret_val = 0;

    if (retErr == NULL)
    {
        return result;
    }
    
    ret_val = kernel_read( pFile, offset, pBuffer, size );
    
    *retErr = 0;
    if (ret_val < 0) // system error
    {
        SF_LOG_E("[%s] System error occurs(%d)", __FUNCTION__, ret_val);
        *retErr = SF_SYSERR_TO_STATUS(ret_val);
        return SF_STATUS_SYSTEM_ERROR_IN_READ;
    }
    else if (ret_val < size) // doesn't read enough
    {
        SF_LOG_E("[%s] Insufficient buffer(input:%ul,output:%d)", __FUNCTION__, size,  ret_val);
        *retErr = SF_STATUS_NOT_ENOUGH_BUFFER;
        return SF_STATUS_NOT_ENOUGH_BUFFER; 
    }
    else if (ret_val == size) // read success
    {
        return SF_STATUS_OK;
    }
    return result;
}

/**
****************************************************************************************************
* @brief                Convert big endian number to little endian
* @param [in] pData     Pointer to the input data to convert
* @return               Number in little endian format
****************************************************************************************************
*/
static inline Uint32 SfdUepBigToLittle( const Uint8* pData )
{
    return ((Uint32)pData[0] << 24) |
           ((Uint32)pData[1] << 16) |
           ((Uint32)pData[2] << 8 ) |
           ((Uint32)pData[3]);
}

/**
****************************************************************************************************
* @brief                Check UEP signature MAGIC number
* @param [in] pData     Pointer to data to check magic number
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static inline SF_STATUS SfdUepCheckMagicNumber( const Uint8* pData )
{
    return ( (pData[0] == SFD_UEP_SIGN_MAG0) &&
             (pData[1] == SFD_UEP_SIGN_MAG1) &&
             (pData[2] == SFD_UEP_SIGN_MAG2) &&
             (pData[3] == SFD_UEP_SIGN_MAG3) ) ? SF_STATUS_OK : SF_STATUS_UEP_FILE_NO_MAGICNUM;
}

/**
****************************************************************************************************
* @brief                Read signature from file
* @param [in] pFile     Pointer to the file
* @param [in] fileSize  Size of of file
* @param [out] pInfo    Pointer to the signature context
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
//#define SFD_UEP_MEMORY_TEST

static SF_STATUS SfdUepReadSignatureFromFile( struct file* pFile, Uint64 fileSize,
                                              UepSignatureInfo* pInfo, int* retErr )
{
    SF_STATUS result = SF_STATUS_UEP_FILE_NOT_SIGNED;
#define UEP_MAGIC_LEN   4
    Uint8 signatureMagic[ UEP_MAGIC_LEN ] = { 0 };

#define UEP_ENC_SIG_LEN 8
#define UEP_DEC_SIG_LEN 4
    Uint8 encodedSigLen[ UEP_ENC_SIG_LEN ] = { 0 };
    Uint8 decodedSigLen[ UEP_DEC_SIG_LEN ] = { 0 };
    size_t decodedLen = UEP_DEC_SIG_LEN;

    do
    {

#ifdef SFD_UEP_MEMORY_TEST
        /**
        * @note Test part for UEP Memory Test.
        */
        {
            Char* pBuffer = NULL;
            Char* pName = NULL;
            const Char* TCFile = "/opt/SFDUEP/MemTestFile";
            Uint32 pathLen = 0;
            Uint32 TCLen   = 0;

            pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
            pathLen = strlen(pName);
            TCLen   = strlen(TCFile);

            if ( pathLen == TCLen )
            {
                if( strncmp(pName, TCFile, pathLen) == 0)
                {
                    SF_LOG_I("[UEP] Resource Not Enough;");
                    result = SF_STATUS_NOT_ENOUGH_RESOURCE;
                    sf_free( pBuffer);
                    pBuffer = NULL;
                    break;
                }
            }
            sf_free( pBuffer);
            pBuffer = NULL;
        }
#endif /*SFD_UEP_MEMORY_TEST*/

        /**
        * @note Read signature magic number
        */
        result = SfdUepReadFile(pFile, fileSize - UEP_MAGIC_LEN, signatureMagic, UEP_MAGIC_LEN, retErr);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] Can not read data from file", __FUNCTION__);
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        result = SfdUepCheckMagicNumber(signatureMagic);
        if (SF_STATUS_OK != result)
        {
            break;
        }

        /**
        * @note Now signatureMagic used to read signature size
        */
        result = SfdUepReadFile(pFile, fileSize - ( UEP_MAGIC_LEN + UEP_ENC_SIG_LEN ),                          
                                encodedSigLen, UEP_ENC_SIG_LEN, retErr);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] failed to get encodedSigLen ", __FUNCTION__);
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        if( base64_decode( (const char *)encodedSigLen, UEP_ENC_SIG_LEN, (char *)decodedSigLen, &decodedLen ) &&
            UEP_DEC_SIG_LEN == decodedLen )
        {
            pInfo->signatureLength = SfdUepBigToLittle(decodedSigLen);
            pInfo->signatureOffset = fileSize - pInfo->signatureLength - ( UEP_MAGIC_LEN + UEP_ENC_SIG_LEN );
        }
        else
        {
            SF_LOG_W("[%s] failed to decode signature len ", __FUNCTION__);
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        // get Level
        result = SfdUepReadFile(pFile, pInfo->signatureOffset-1, &(pInfo->uepLevel), 1, retErr);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] failed to get level ", __FUNCTION__);
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }
        else
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            // SF_LOG_I("[%s] uep level:%c", __FUNCTION__, pInfo->uepLevel);
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        }
		
        // get Version
        result = SfdUepReadFile(pFile, pInfo->signatureOffset-2, &(pInfo->uepVersion), 1, retErr);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] failed to get uepVersion ", __FUNCTION__);
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        /**
        * @note NULL symbol included into size of signature. Since, signature
        *   stored in string format.
        */
        pInfo->signature = (Char*) sf_malloc(pInfo->signatureLength+1);
        if (NULL == pInfo->signature)
        {
            result = SF_STATUS_NOT_ENOUGH_RESOURCE;
            break;
        }

        result = SfdUepReadFile(pFile, pInfo->signatureOffset, pInfo->signature,
            pInfo->signatureLength, retErr);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] failed to get signature ", __FUNCTION__);
            sf_free(pInfo->signature);
            pInfo->signature = NULL;
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        pInfo->signature[pInfo->signatureLength] = 0;
        pInfo->signatureLength++;


        result = SF_STATUS_OK;

    } while(FALSE);

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS HashFile( struct file* pFile, UepSignatureContext* pCtx, Uint32 offset, int* retErr )
{
    SF_STATUS r = SF_STATUS_OK;
    const Uint32 dataSize = 4 * 1024;
    Uint32 rOffset = 0;
    unsigned char* pData = NULL;

    pData = (unsigned char*)sf_malloc( dataSize );
    if ( NULL == pData )
    {
        return SF_STATUS_NOT_ENOUGH_RESOURCE;
    }

    SignatureInit( pCtx );
    while ( offset )
    {
        Uint32 toRead = ( dataSize < offset ) ? dataSize : offset;
        r = SfdUepReadFile( pFile, rOffset, pData, toRead, retErr );
        if ( SF_STATUS_OK != r )
        {
            SF_LOG_E("[UEP] Failed to read file for Hashfile"); 
            break;
        }
        SignatureUpdate( pCtx, pData, toRead );
        offset  -= toRead;
        rOffset += toRead;
    }
    sf_free( pData );
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepPacketHandler(const SfProtocolHeader* const pPacketInterface)
{
    SF_STATUS result = SF_STATUS_OK;

//    down_read( &s_uepRwsem );
    do
    {
        SfPacket* pCurrentPacket = NULL;
        /* 
        * @note remove to get kuep signature level.
        * check SfdUepVerifyFileSignature.
        */
        /*if ( 0 == s_uepStatus )
        {
            // UEP is disabled
            result = SF_STATUS_OK;
            break;
        }*/

        result = SF_VALIDATE_PACKET(pPacketInterface);
        if ( SF_FAILED(result) )
            break;

        pCurrentPacket = (SfPacket*)pPacketInterface;
        result = SF_VALIDATE_OPERATION( pCurrentPacket->op );
        if ( SF_FAILED(result) )
            break;

        switch ( pCurrentPacket->op->type )
        {
            case SF_OPERATION_TYPE_MMAP:
                result = SfdUepVerifyDynamicLibrary( pCurrentPacket->op );
                break;

            case SF_OPERATION_TYPE_EXEC:
                result = SfdUepVerifyExecutableBinary( pCurrentPacket->op );
                break;

            default:
                result = SF_STATUS_NOT_IMPLEMENTED;
                break;
        }
    } while ( FALSE );
    //up_read( &s_uepRwsem );

    if ( result == SF_STATUS_UEP_SIGNATURE_DUID )
    {
        // disable UEP because of DUID hash match
        //down_write( &s_uepRwsem );
        s_uepStatus = 0;
        //up_write( &s_uepRwsem );
        SF_LOG_I( "[%s] UEP has been disabled", __FUNCTION__ );
    }
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyDynamicLibrary(const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationFileMmap* pCurrentOperation = NULL;

    SF_ASSERT( SF_DEBUG_CLASS_UEP, pOperationInterface->size == sizeof(SfOperationFileMmap),
               "%s got invalid packet", __FUNCTION__ );

    pCurrentOperation = (SfOperationFileMmap*)pOperationInterface;
    if ( pCurrentOperation->prot & PROT_EXEC )
    {
        result = SfdUepVerifyFileSignature( pCurrentOperation->pFile, pCurrentOperation->bCheckAlways, pCurrentOperation->bIsSo, pOperationInterface );
        pCurrentOperation->result = result;
    }
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyExecutableBinary(const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationBprmCheckSecurity* pCurrentOperation = NULL;

    SF_ASSERT( SF_DEBUG_CLASS_UEP, pOperationInterface->size == sizeof(SfOperationBprmCheckSecurity),
               "%s got invalid operation", __FUNCTION__ );

    pCurrentOperation = (SfOperationBprmCheckSecurity*)pOperationInterface;
    result = SfdUepVerifyFileSignature( pCurrentOperation->pBinParameters->file, 0, 0 , pOperationInterface );
    pCurrentOperation->result = result;
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdUepCheckFileSignature(struct file* const pFile, UepSignatureInfo* const pInfo, int* retErr)
{
    SF_STATUS result = SF_STATUS_FAIL;
    UepSignatureContext ctx = { .hashCtx = { 0 } };
    Uint32 signatureOffset = pInfo->signatureOffset;
    Char* pName = NULL;
    Char* pBuffer = NULL;
    
    do
    {
        UepKey* pKey = NULL;
        result = SetupSignatureContext( &ctx );
        if ( SF_FAILED( result ) )
        {
            SF_LOG_E( "[%s] failed to setup signature context", __FUNCTION__ );
            break;
        }

        // skip c_uep_lead_meta
        //signatureOffset -= UEP_LEAD_META_LEN;
        result = HashFile( pFile, &ctx, signatureOffset, retErr );

        if ( result != SF_STATUS_OK )
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            SF_LOG_I("[%s] failed to hash file", __FUNCTION__);
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            if (result == SF_STATUS_NOT_ENOUGH_RESOURCE)
            {
                SF_LOG_E("[%s][%d] Fail to allocate Hash memory ", __FUNCTION__, __LINE__);
            }
            break;
        }

        if ( CHECK_VALID_UEP_VERSION(pubkeyN, pInfo->uepVersion) )
        {
            pKey = CreateKey( pubkeyN[pInfo->uepVersion - SFD_UEP_INIT_VERSION], pubkeyE);

            if ( NULL == pKey )
            {
                SF_LOG_E( "[%s] failed to create RSA key", __FUNCTION__ );
                result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
                break;
            }
        }
        else
        {
            SF_LOG_E( "[%s] Not supported UEP version(%c)", __FUNCTION__, pInfo->uepVersion );
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        result = SignatureVerify( &ctx, pKey, pInfo->signature, pInfo->signatureLength, s_duidHash );
        if (result != SF_STATUS_UEP_SIGNATURE_CORRECT)
        {
            pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
            if ( (NULL != pBuffer) && (NULL != pName) )
            {
                SF_LOG_E("%s():[verifying signature failed :%s]", __FUNCTION__, pName); 
                sf_free( pBuffer );
            }
        }
        DestroyKey( pKey );
    } while( FALSE );

    FreeSignatureContext( &ctx );
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void PushMountInfo(int nAddType, const SfProtocolHeader* const pOperationInterface, const Char *pMntType, int nMntFlag, 
    int* pArrResult, int ArrSize)
{
    SF_STATUS result = SF_STATUS_OK;
    SF_OPERATION_TYPE opType = SF_OPERATION_TYPE_MAX;
    SfOperationBprmCheckSecurity* pProcessOp = NULL;
    SfOperationFileMmap* pMmapOp = NULL;
    char* pMntInfo = NULL;
    char* pMntFlag = 0;
    char* pRetInfo = NULL;
    int idx = 0;
    int text_size = 0;
    int copy_size = 0;

    if (pOperationInterface == NULL)
    {
        return;
    }
    if ( (nAddType < SF_ADD_MNT_INFO) || (nAddType > SF_ADD_RET_INFO) )
    {
        return;
    }
    if ( (nAddType == SF_ADD_MNT_INFO) && (pMntType == NULL) )
    {
        return;
    }
    if ( (nAddType == SF_ADD_RET_INFO) && (pArrResult == NULL) )
    {
        return;
    }
 
    opType = pOperationInterface->type;
    switch(opType)
    {
        case SF_OPERATION_TYPE_EXEC:
            pProcessOp = (SfOperationBprmCheckSecurity*)pOperationInterface;
            if (pProcessOp == NULL)
            {
                return;
            }
            pMntInfo = pProcessOp->szMntInfo;
            pMntFlag = pProcessOp->szMntFlag;
            pRetInfo = pProcessOp->szRetInfo;
            break;
        case SF_OPERATION_TYPE_MMAP:
            pMmapOp = (SfOperationFileMmap*)pOperationInterface;
            if (pMmapOp == NULL)
            {
                return;
            }
            pMntInfo = pMmapOp->szMntInfo;
            pMntFlag = pMmapOp->szMntFlag;
            pRetInfo = pMmapOp->szRetInfo;
            break;
        default:
            return;
    }

    switch (nAddType)
    {
        case SF_ADD_MNT_INFO:
            text_size = strlen(pMntType);
            copy_size = (text_size > SF_MNT_INFO_SIZE) ? SF_MNT_INFO_SIZE : text_size;
            for (idx = 0; idx < copy_size; ++idx)
            {
                pMntInfo[idx] = pMntType[idx];
            }
            snprintf(pMntFlag, SF_MNT_FLAG_SIZE + 1, "0x%x", nMntFlag);
            break;
        case SF_ADD_RET_INFO:
            snprintf(pRetInfo, SF_RET_INFO_SIZE + 1, "%d:%d:%d:%d:%d:%d", 
                pArrResult[0], pArrResult[1], pArrResult[2], pArrResult[3], pArrResult[4], pArrResult[5]);
            break;
        default:
            break;
    } // end switch
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdShowSystemErr(int* pArrResult, int ArrSize, Char* pName)
{
    SF_STATUS ret = SF_STATUS_FAIL;
    int idx = 0;
    int sys_error = 0;

    if ( (pArrResult == NULL) || (ArrSize < 1) || (pName == NULL) )
    {
        return ret;
    }
    
    for (idx = 3; idx < SF_ERR_ARRAY_SIZE; ++idx)
    {
        if (pArrResult[idx] == SF_STATUS_NOT_ENOUGH_BUFFER)
        {
            SF_LOG_E( "[%s] file %s (Not Enough Buffer on read file:%s)", __FUNCTION__, pName,
                (idx == 3) ? c_func_read_error1 : (idx == 4) ? c_func_read_error2 : (idx == 5) ? c_func_read_error3 : "");
            if (ret == SF_STATUS_FAIL)
            {
                ret = SF_STATUS_OK;
            }
        }
        else if (SF_IS_SYSERR(pArrResult[idx]))
        {
            sys_error = pArrResult[idx] - SF_STATUS_SYSERR_BORDER;
            SF_LOG_E( "[%s] file %s (System error occurs[-%d]:%s)", __FUNCTION__, pName, sys_error,
                (idx == 3) ? c_func_read_error1 : (idx == 4) ? c_func_read_error2 : (idx == 5) ? c_func_read_error3 : "");
            pArrResult[idx] = -sys_error;
            if (ret == SF_STATUS_FAIL)
            {
                ret = SF_STATUS_OK;
            }
        }
    }

    return ret;
}

/**
****************************************************************************************************
* @brief                Print message about UEP verification routine
* @param   [in] pFile   Pointer to the file was processed
* @param   [in] result  Result was returned from UEP verification routine
* @warning              Print messages only in debug and release mode
* @return
****************************************************************************************************
*/
static void SfdUepHandleVerificationResult(struct file* const pFile, SF_STATUS result, Uint8 uepLevel, 
    const SfProtocolHeader* const pOperationInterface, int* pArrResult, int ArrSize )
{
    SF_STATUS ret_err = SF_STATUS_OK;
    Char* pName = NULL;
    char fsname[5] = "null";
    int mnt_flags = 0;
    Char* pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
    
    if ( NULL == pBuffer )
    {
        SF_LOG_E( "[%s] failed to construct file name", __FUNCTION__ );
        return;
    }

    // If the file is not signed for kUEP or a error happnes, get file system name and mount flags;
    if( result != SF_STATUS_OK )
    {
        struct path* pPath = &pFile->f_path;
        if (pPath)
        {
            path_get(pPath);
            if ( pPath->mnt != NULL && pPath->mnt->mnt_sb != NULL 
                && pPath->mnt->mnt_sb->s_type != NULL && pPath->mnt->mnt_sb->s_type->name != NULL )
            
            {
                strncpy(fsname, pPath->mnt->mnt_sb->s_type->name, ARRAY_SIZE(fsname)-1);
                mnt_flags = pPath->mnt->mnt_flags;
            }
            else
            {
                // DO NOTHING
            }
            path_put(pPath);
        }
    }

    switch ( result )
    {
        case SF_STATUS_UEP_FILE_NOT_SIGNED:
            // 2016.12.14;The below log was changed ERROR level to WARNING level.
            // For removing this log on the performace firmware.
            // In the debug firmware, log is printed from  unimportant level to WARNING level.
            // In the release & perf firmware, log is printed from ERROR level to more ciritical level.
            if (pArrResult && pName)
            {
                ret_err = SfdShowSystemErr(pArrResult, ArrSize, pName);
            }
            if (ret_err != SF_STATUS_OK)
            {
                SF_LOG_E( "[%s] file %s is not signed. Sign it.(fsname:%s, mnt_flags:%x)", __FUNCTION__, pName, fsname, mnt_flags );
            }
            if (pOperationInterface)
            {
                PushMountInfo(SF_ADD_RET_INFO, pOperationInterface, NULL, 0, pArrResult, ArrSize);
            }
            break;

        case SF_STATUS_UEP_SIGNATURE_CORRECT:
            SF_LOG_I( "[Verification correct][%s].Level:%c", pName, uepLevel );
            // SF_LOG_I( "[Verification correct][%s].Level:%c", pFile->f_path.dentry->d_iname, uepLevel );
            break;

        case SF_STATUS_UEP_SIGNATURE_INCORRECT:
            if (pArrResult && pName)
            {
                ret_err = SfdShowSystemErr(pArrResult, ArrSize, pName);
            }
            if (ret_err != SF_STATUS_OK)
            {
                SF_LOG_E( "[%s] file %s has incorrect signature.(fsname:%s, mnt_flags:%x)", __FUNCTION__, pName, fsname, mnt_flags );
            }
            if (pOperationInterface)
            {
                PushMountInfo(SF_ADD_RET_INFO, pOperationInterface, NULL, 0, pArrResult, ArrSize);
            }
            break;
        case SF_STATUS_NOT_ENOUGH_BUFFER: /* fall through */
        case SF_STATUS_SYSTEM_ERROR_IN_READ:
            if (pArrResult && pName)
            {
                ret_err = SfdShowSystemErr(pArrResult, ArrSize, pName);
            }
            if (pOperationInterface)
            {
                PushMountInfo(SF_ADD_RET_INFO, pOperationInterface, NULL, 0, pArrResult, ArrSize);
            }
            break;
        case SF_STATUS_FAIL:
            SF_LOG_E( "[%s] signature general check error on file %s.(fsname:%s, mnt_flags:%x)", __FUNCTION__, pName, fsname, mnt_flags );
            break;
        case SF_STATUS_OK:
            // To show log for RO, uncomment the below line
            // SF_LOG_I( "[%s] signature verification skipped. It's in RO. %s ", __FUNCTION__, pName );
            break;
        case SF_STATUS_UEP_FILE_NO_MAGICNUM:
            SF_LOG_E( "[%s] file %s doesn't has magic number.(fsname:%s, mnt_flags:%x)", __FUNCTION__, pName, fsname, mnt_flags );
            break;
        case SF_STATUS_PENDING:
            // SF_LOG_I( "[%s] file %s is pended", __FUNCTION__, pName );
            break;
        default:
            SF_LOG_W( "[%s] unexpected result: %d", __FUNCTION__, result );
            break;
    }
    sf_free( pBuffer );
}

/**
****************************************************************************************************
* @brief Check the ELF identification
* @warning This function doesn't check for NULL pointer. The function expect that buffer will be
*   not NULL.
* @return SF_STATUS_OK on ELF file identification magic numbers, SF_STATUS_FAIL otherwise
****************************************************************************************************
*
static SF_STATUS SfdUepElfIdentificationCheck(const char* const pBuffer)
{
    return (pBuffer[EI_MAG0] == ELFMAG0 &&
            pBuffer[EI_MAG1] == ELFMAG1 &&
            pBuffer[EI_MAG2] == ELFMAG2 &&
            pBuffer[EI_MAG3] == ELFMAG3) ? SF_STATUS_OK : SF_STATUS_FAIL;
}
*/



/**
****************************************************************************************************
* @brief Performs checking of the file type. ELF file has the following magic number identification
*  0x7f, 'E', 'L', 'F'.
* @return SF_STATUS_OK in case if file is ELF format, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_FILE_TYPE SfdUepCheckFileType(struct file* const pFile, int* retErr )
{
    SF_FILE_TYPE type = SF_FILE_TYPE_UNKNOWN;
    char eIdent[4] = {0};
    SF_STATUS ret = SF_STATUS_FAIL;

    do
    {
        ret = SfdUepReadFile( pFile, 0, eIdent, sizeof(eIdent), retErr );
        if (ret != SF_STATUS_OK)
        {
            SF_LOG_E("[%s] Can't read ELF identification number", __FUNCTION__);
            break;
        }

        if( eIdent[EI_MAG0] == ELFMAG0 && eIdent[EI_MAG1] == ELFMAG1 &&
            eIdent[EI_MAG2] == ELFMAG2 && eIdent[EI_MAG3] == ELFMAG3 )
        {
            //SF_LOG_I("[%s] elf", __FUNCTION__);
            type = SF_FILE_TYPE_ELF;
        }
        else if( eIdent[0] == '#' && eIdent[1] == '!' )
        {
            //SF_LOG_I("[%s] script", __FUNCTION__);
            type = SF_FILE_TYPE_SCRIPT;
        }
        else if ( eIdent[EI_MAG0] == SF_PEMAG0 && eIdent[EI_MAG1] == SF_PEMAG1)
        {
            type = SF_FILE_TYPE_PE;   
        }
        else
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            SF_LOG_I("[%s] unknown file type(%c%c%c%c)", __FUNCTION__,
                eIdent[EI_MAG0], eIdent[EI_MAG1], eIdent[EI_MAG2], eIdent[EI_MAG3] );
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        }

    } while(FALSE);

    return type;
}

/*
****************************************************************************************************
* Check the file is in vdfs
****************************************************************************************************
*/
bool SfdUepCheckRWVDFS(struct file* const pFile, const SfProtocolHeader* const pOperationInterface)
{
    struct path* pPath;
    Char * pBuffer = NULL;
    Char * pName = NULL;
    const Char * pMntName = NULL;
    char fsname[5] = "null";
    int mnt_flags = 0;
    bool bVDFS    = FALSE;


    if( unlikely( NULL == pFile ) )
    {
        return FALSE;
    }

    pPath = &pFile->f_path;

    if( unlikely( NULL == pPath ) )
    {
        return FALSE;
    }

    path_get(pPath);

    if ( pPath->mnt != NULL && pPath->mnt->mnt_sb != NULL                 
        && pPath->mnt->mnt_sb->s_type != NULL && pPath->mnt->mnt_sb->s_type->name != NULL )
    {
        strncpy(fsname, pPath->mnt->mnt_sb->s_type->name, ARRAY_SIZE(fsname)-1);
        mnt_flags = pPath->mnt->mnt_flags;
        PushMountInfo(SF_ADD_MNT_INFO, pOperationInterface, fsname, mnt_flags, NULL, 0);
    }
    
    if( MNT_READONLY & pPath->mnt->mnt_flags )
    {
        pMntName = pPath->mnt->mnt_sb->s_type->name;
        if( pMntName[0] == 'v' && pMntName[1] == 'd' && pMntName[2] == 'f' && pMntName[3] == 's' )
        {
            bVDFS = TRUE;
        }
    }

#if 0 // For debugging
    if( TRUE == bNeedToCheck )
    {

        pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
        if ( NULL != pBuffer )
        {
            SF_LOG_I("[SfdUepNeedToCheck] %s mount type:%s, flag:0x%X", pName, pPath->mnt->mnt_sb->s_type->name, pPath->mnt->mnt_flags );

            sf_free( pBuffer);
            pBuffer = NULL;


        }
        else
        {
            SF_LOG_E( "[%s] Failed to get path!!! (0x%X)", __FUNCTION__, pBuffer );
        }
    }
#endif 

    path_put(pPath);


    return bVDFS;
}

void SetSystemError(const SfProtocolHeader* const pOperationInterface, int* sysError)
{
    SF_OPERATION_TYPE opType = SF_OPERATION_TYPE_MAX;
    SfOperationBprmCheckSecurity* pProcessOp = NULL;
    SfOperationFileMmap* pMmapOp = NULL;
    
    if (pOperationInterface == NULL)
    {
        return;
    }
    opType = pOperationInterface->type;
    
    switch(opType)
    {
        case SF_OPERATION_TYPE_EXEC:
            pProcessOp = (SfOperationBprmCheckSecurity*)pOperationInterface;
            if (pProcessOp == NULL)
            {
                return;
            }
            if (*sysError < 0)
            {
                pProcessOp->sysError = -(*sysError - SF_STATUS_SYSERR_BORDER);
            }
            else
            {
                pProcessOp->sysError = *sysError;
            }
            break;
        case SF_OPERATION_TYPE_MMAP:
            pMmapOp = (SfOperationFileMmap*)pOperationInterface;
            if (pMmapOp == NULL)
            {
                return;
            }
            if (*sysError < 0)
            {
                pMmapOp->sysError = -(*sysError - SF_STATUS_SYSERR_BORDER);
            }
            else
            {
                pMmapOp->sysError = *sysError;
            }
            break;
        default:
            return;
    }
    
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyFileSignature(struct file* const pFile, int bCheckAlways, int bIsSo, 
    const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_OK;
    UepSignatureInfo info;
    bool bIsRW = FALSE;
    bool bNeedPrintLog = TRUE;
    int ArrResult[SF_ERR_ARRAY_SIZE] = {0};
    int ret_val = 0;
    // unsigned long long uino = 0;

    info.uepLevel = SFD_UEP_LEVEL_NULL;

    // Check cache tale    
    if( unlikely(NULL == pFile) || (NULL == pFile->f_inode) )
    {
        SF_LOG_W("[%s] inode is null", __FUNCTION__);
        return SF_STATUS_BAD_ARG;     
    }

    // uino = SfdGetUniqueIno(pFile->f_inode);
    
#ifdef CONFIG_SECURITY_SFD_CHECK_STATFS
    ArrResult[0] = 1;
    bIsRW = !SfdUepCheckRWVDFS( pFile, pOperationInterface );
#else 
    bIsRW = SF_STATUS_OK == SfdCheckFileIsInRW( pFile, pOperationInterface )? TRUE:FALSE;
    ArrResult[0] = 0;
#endif
    ArrResult[1] = (bIsRW == TRUE) ? 1 : 0;

    // if( SF_SUCCESS( SfdCacheCheck( uino, SFD_MODULE_TYPE_UEP, &result, &(info.uepLevel), bIsRW ) ) )
    if( SF_SUCCESS( SfdCacheCheck( pFile->f_inode, &result, &(info.uepLevel) ) ) )
    {
        if( SF_STATUS_PENDING != result )
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            //SF_LOG_I("[%s] Cache Hit!!!, i_ino:%u, result:%u, uepLevel:%c",
            //    __FUNCTION__, pFile->f_inode->i_ino, result, info.uepLevel );
#endif // CONFIG_SECURITY_SFD_LEVEL_DEBUG
            bNeedPrintLog = FALSE;
            goto SF_UEP_CACHED;     
        }
        else
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            // SF_LOG_I("[%s] Cache Not hit, i_ino:%u, result:%u", __FUNCTION__, pFile->f_inode->i_ino, result );
#endif // 
        }
    }
    else
    {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        // TODO: uncomment
        // SF_LOG_I("[%s] Cache Node Not found, i_ino:%u", __FUNCTION__, pFile->f_inode->i_ino); 
#endif //   
    }

    // Not cached, start to check for kUEP

    // check file type 
    ret_val = 0;
    info.fileType = SfdUepCheckFileType(pFile, &ret_val);
    ArrResult[2] = info.fileType;
    ArrResult[3] = ret_val;
    if (ret_val == SF_STATUS_NOT_ENOUGH_BUFFER)
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_NOT_ENOUGH_BUFFER;
        goto SF_UEP_CACHE_PASS;
    }
    else if (SF_IS_SYSERR(ret_val))
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_SYSTEM_ERROR_IN_READ;
        goto SF_UEP_CACHE_PASS;
    }
    
    if( !IS_SF_UEP_TARGET_TYPE( info.fileType ) )
    {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        //SF_LOG_W("[%s] Not uep target type", __FUNCTION__);
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        goto SF_UEP_ADD_CACHE;
    }

    // Read sign from file
    info.signature = 0;
    info.uepLevel = SFD_UEP_LEVEL_NOTSIGNED;
    info.uepVersion = 0;
    info.signatureLength = 0;
    info.signatureOffset = 0;
    info.fileType = 0;

    result = SfdUepReadSignatureFromFile(pFile, SfdUepGetFileSize(pFile), &info, &ret_val);
    ArrResult[4] = ret_val;
    if (ret_val == SF_STATUS_NOT_ENOUGH_BUFFER)
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_NOT_ENOUGH_BUFFER;
        goto SF_UEP_CACHE_PASS;
    }
    else if (SF_IS_SYSERR(ret_val))
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_SYSTEM_ERROR_IN_READ;
        goto SF_UEP_CACHE_PASS;
    }
    
    if ( SF_STATUS_UEP_FILE_NO_MAGICNUM == result )
    {
        if( FALSE == bIsRW )
        {
            if( FALSE == bCheckAlways )
            {
                // The file is in RO and not sigend. It should be passed.
                result = SF_STATUS_OK;
                info.uepLevel = SFD_UEP_LEVEL_RO;                
            }
            else
            {
                result = SF_STATUS_UEP_FILE_NOT_SIGNED;   
            }
            goto SF_UEP_ADD_CACHE;
        }
        else
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            // The file is in RW and not sigend. It should be passed.
            // SF_LOG_I("[%s] The file is mounted from RW, Start to check for kUEP. i_ino:%u", __FUNCTION__, pFile->f_inode->i_ino );
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            result = SF_STATUS_UEP_FILE_NOT_SIGNED;
            goto SF_UEP_CACHE_PASS;
        }
    }
    else if( SF_STATUS_OK == result )
    {
        if( FALSE == bCheckAlways && FALSE == bIsRW && (SFD_UEP_LEVEL_CONTAINER > info.uepLevel) )
        {
            // The file is in RO. but it's signed and it's not about SecureZone
            // Pass checking signature
            info.uepLevel = SFD_UEP_LEVEL_RO;
            if (info.signature != NULL)
            {
                sf_free(info.signature);
                info.signature = NULL;
            }
            goto SF_UEP_ADD_CACHE;
        }
        else
        {
            // This case is SecureZone
            // It need to check signature
            // DO NOTHING
        }
    }
    else
    {
        if (result == SF_STATUS_NOT_ENOUGH_RESOURCE)
        {
            goto SF_UEP_CACHE_PASS;
        }            
        info.uepLevel = SFD_UEP_LEVEL_NOTSIGNED;
        result = SF_STATUS_UEP_FILE_NOT_SIGNED;
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        SF_LOG_I("[%s] No signature founded", __FUNCTION__);
#endif // 
        goto SF_UEP_ADD_CACHE;
    }

    // Check sign 
    result = SfdUepCheckFileSignature(pFile, &info, &ret_val);
    ArrResult[5] = ret_val;
    if (info.signature != NULL)
    {
        sf_free(info.signature);
        info.signature = NULL;
    }

    if (ret_val == SF_STATUS_NOT_ENOUGH_BUFFER)
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_NOT_ENOUGH_BUFFER;
        goto SF_UEP_CACHE_PASS;
    }
    else if (SF_IS_SYSERR(ret_val))
    {
        SetSystemError(pOperationInterface, &ret_val);
        result = SF_STATUS_SYSTEM_ERROR_IN_READ;
        goto SF_UEP_CACHE_PASS;
    }

    if (result != SF_STATUS_UEP_SIGNATURE_CORRECT)
    {   
        // if failed, goto pass
        goto SF_UEP_CACHE_PASS;
    } 
    
SF_UEP_ADD_CACHE:
    //SF_LOG_I("\e[1;33m[file : %s][SFDInode : %lld];\e[0m",pFile->f_path.dentry->d_iname,uino);
    // Add the result into cache table
    // if( SF_FAILED( SfdCacheAdd( uino, SFD_MODULE_TYPE_UEP, result, info.uepLevel, bIsRW ) ) )
    if( SF_FAILED( SfdCacheAdd( pFile->f_inode, result, info.uepLevel ) ) )
    {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        SF_LOG_W("[%s] Failed to cache UEP Result: i_ino:%u, result:%u",
                __FUNCTION__, pFile->f_inode->i_ino, result );
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
    }

SF_UEP_CACHED:

#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER
    if(current->uepLevel != SFD_UEP_LEVEL_CONTAINER_DEBUG)
    {
        if(( SF_STATUS_UEP_SIGNATURE_CORRECT == result ) || (SF_STATUS_OK  == result))
        {
            if( FALSE == bIsSo && FALSE == bCheckAlways)
                current->uepLevel = info.uepLevel;
        }
        else
        {
            current->uepLevel = SFD_UEP_LEVEL_NOTSIGNED;			
        }
    }
#endif
	
    if( result == SF_STATUS_FAIL )
    {
        result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
    }

SF_UEP_CACHE_PASS:
    /**
    * @note Securezone needs signature'level in condition of kUEP disabled.
    */
    if( s_uepStatus == 0 )
    {
        result = SF_STATUS_UEP_SIGNATURE_CORRECT;
    }
    
    /**
    * @note This function print logs. It will be changed by the static inline empty stub,
    *   when kernel will be compiled in the release mode.
    */
    if ( bNeedPrintLog )
    {
        SfdUepHandleVerificationResult(pFile, result, info.uepLevel, pOperationInterface, ArrResult, sizeof(ArrResult) / sizeof(int));
    }

    if (result == SF_STATUS_NOT_ENOUGH_RESOURCE)
    {
        SF_LOG_E("[%s] [memory allocating was failed;resource error]", __FUNCTION__);
        result = SF_STATUS_OK;
    }

    return result;
}
EXPORT_SYMBOL(SfdUepVerifyFileSignature);

//--------------------------------------------------------------------------------------------------

static SF_STATUS HashDuid( const Char* duid, Uint8* out )
{
    struct hash_desc desc;
    struct scatterlist sg;
    Uint duidLength = 0;

    desc.flags = 0;
    desc.tfm   = crypto_alloc_hash( HASH_ALGO_NAME, 0, CRYPTO_ALG_ASYNC );
    if ( IS_ERR( desc.tfm ) )
    {
        SF_LOG_E( "[%s] failed to allocate %s hashing algorithm", __FUNCTION__, HASH_ALGO_NAME );
        return SF_STATUS_FAIL;
    }

    duidLength = strlen( duid );
    sg_init_one( &sg, (Uint8*)duid, duidLength );
    crypto_hash_digest( &desc, &sg, duidLength, out );
    crypto_free_hash( desc.tfm );
    return SF_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

SF_STATUS SetupDuidHash( const Char* duid )
{
    SF_STATUS r = SF_STATUS_FAIL;
    if ( NULL == duid )
    {
        SF_LOG_E( "[%s] input is NULL", __FUNCTION__ );
        return SF_STATUS_BAD_ARG;
    }

    //down_write( &s_uepRwsem );
    r = HashDuid( duid, s_duidHash );
    //up_write( &s_uepRwsem );
    if ( SF_SUCCESS( r ) )
        SF_LOG_I( "[%s] hash of DUID [%s] has been set", __FUNCTION__, duid );
    return r;
}

//--------------------------------------------------------------------------------------------------
