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

/*
// Previous modulus for 2015
static const Char* pubkeyN = "0xC0FF38E1C46DB70032C762C7168EFC5E40CDC41EA4281"
                             "E137ECC36E6DADD9692E69259475B125BB6FDFF94288ED4"
                             "BF4608E6BD5B4A8E39A7F902BA44CD957030961E6E14028"
                             "5364833360863410467ECCE128B00E27838B77B324F4DCD"
                             "BE0F3974B07D81B052FE13271F47FBD86AB969E10F481E3"
                             "CFFFCAC2FFFCD8AF39E7D37";
static const Char* pubkeyE = "0x101";

// Previous modulus for 2016
static const Char* pubkeyN = "0x00e45f0692701270007eb3babad7d8"
                                "fcf709ff6fea7578d8752aabdb1449"
                                "446b0c64097a4a2e07e58ed2f99408"
                                "fdfd011d2bf2dbf3d71cd2e3c495c0"
                                "ee86a7cd99df54bc9ab06affdeb771"
                                "4be1ea6263a396654288fdeebf2d42"
                                "0f9404a9807b821e08d65ba2711044"
                                "0942d33e0b45772f1253c624bacd03"
                                "a07853c335d6449753";
*/

static const Char* pubkey17N = "0x00c7f5ef8e47fa45c6dd87b0cbef09"
                                "f518c0f5797ac74e9ed0704a09fe7b"
                                "eba3cfa7b201910db599a6e95265da"
                                "f8a8cc48ad7527cd296200d6455bd3"
                                "df55e352bc5a81d406771b838efbbd"
                                "dcdfd4aebb9c4cea75b9de07a2b7e2"
                                "d4752163d03ed9025a446c4e2d4cdd"
                                "57dbab7d28ba98834715002278489c"
                                "3496aaa9753fade6a2ead689da792c"
                                "a3bf2634faa0c588cb8cf8072a6ee6"
                                "bd22ed7d268f36f67792241ccc6d3f"
                                "6b5342c840bbe592d7d95843dad92a"
                                "54ce8be768d52ef7496ab200304f03"
                                "5079e68bfdb5961410520597984f19"
                                "8293daf84b1ecbff05cd42fc13ad24"
                                "f2123cd5a86550c04bd6a2489c28f5"
                                "9623de7b91e89a722e5dffc043418a"
                                "1cf9";



static const Char* pubkey18N =  "0x00b05d2631818fd247019f5c546eba"
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
                                "58fb";



static const Char* pubkeyE = "0x10001";
//--------------------------------------------------------------------------------------------------

//static DECLARE_RWSEM(s_uepRwsem);
static Int   s_uepStatus = 1;
static Uint8 s_duidHash[ HASH_ALGO_LENGTH ] = { 0 };

static const char c_uep_lead_meta [] = "\n#UEP30";
#define UEP_LEAD_META_LEN sizeof( c_uep_lead_meta ) - 1

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
                                 unsigned long size )
{
    Int readSize = kernel_read( pFile, offset, pBuffer, size );
    return ( readSize == size ) ? SF_STATUS_OK : SF_STATUS_FAIL;
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
static SF_STATUS SfdUepReadSignatureFromFile( struct file* pFile, Uint64 fileSize,
                                              UepSignatureInfo* pInfo )
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
        /**
        * @note Read signature magic number
        */
        result = SfdUepReadFile(pFile, fileSize - UEP_MAGIC_LEN, signatureMagic, UEP_MAGIC_LEN);
        if (SF_STATUS_OK != result)
        {
            SF_LOG_E("[%s] Can not read data from file", __FUNCTION__);
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
                          
                                encodedSigLen, UEP_ENC_SIG_LEN);
        if (SF_STATUS_OK != result)
        {
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
        result = SfdUepReadFile(pFile, pInfo->signatureOffset-1, &(pInfo->uepLevel), 1);
        if (SF_STATUS_OK != result)
        {
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
        result = SfdUepReadFile(pFile, pInfo->signatureOffset-2, &(pInfo->uepVersion), 1);
        if (SF_STATUS_OK != result)
        {
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
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        result = SfdUepReadFile(pFile, pInfo->signatureOffset, pInfo->signature,
            pInfo->signatureLength);
        if (SF_STATUS_OK != result)
        {
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
static SF_STATUS HashFile( struct file* pFile, UepSignatureContext* pCtx, Uint32 offset )
{
    SF_STATUS r = SF_STATUS_OK;
    const Uint32 dataSize = 16 * 1024;
    Uint32 rOffset = 0;
    unsigned char* pData = NULL;

    pData = (unsigned char*)sf_malloc( dataSize );
    if ( NULL == pData )
    {
        return SF_STATUS_FAIL;
    }

    SignatureInit( pCtx );
    while ( offset )
    {
        Uint32 toRead = ( dataSize < offset ) ? dataSize : offset;
        if ( SF_FAILED(SfdUepReadFile( pFile, rOffset, pData, toRead )) )
        {
            r = SF_STATUS_FAIL;
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
        result = SfdUepVerifyFileSignature( pCurrentOperation->pFile, pCurrentOperation->bCheckAlways, pCurrentOperation->bIsSo );
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
    result = SfdUepVerifyFileSignature( pCurrentOperation->pBinParameters->file, 0, 0 );
    pCurrentOperation->result = result;
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdUepCheckFileSignature(struct file* const pFile, UepSignatureInfo* const pInfo)
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

        result = HashFile( pFile, &ctx, signatureOffset );

        if ( SF_FAILED( result ) )
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            SF_LOG_I("[%s] failed to hash file", __FUNCTION__);
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
            break;
        }

        switch (pInfo->uepVersion)
        {
            case SFD_UEP_18_VERSION:
                pKey = CreateKey( pubkey18N, pubkeyE );
                break;
            case SFD_UEP_17_VERSION:
                pKey = CreateKey( pubkey17N, pubkeyE );
                break;
            default:
                break;
        }

        if ( NULL == pKey )
        {
            SF_LOG_E( "[%s] failed to create RSA key", __FUNCTION__ );
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

/**
****************************************************************************************************
* @brief                Print message about UEP verification routine
* @param   [in] pFile   Pointer to the file was processed
* @param   [in] result  Result was returned from UEP verification routine
* @warning              Print messages only in debug and release mode
* @return
****************************************************************************************************
*/
static void SfdUepHandleVerificationResult(struct file* const pFile, SF_STATUS result, Uint8 uepLevel)
{
    Char* pName = NULL;
    Char* pBuffer = SfdConstructAbsoluteFileNameByFile( pFile, &pName );
    if ( NULL == pBuffer )
    {
        SF_LOG_E( "[%s] failed to construct file name", __FUNCTION__ );
        return;
    }

    switch ( result )
    {
        case SF_STATUS_UEP_FILE_NOT_SIGNED:
            // 2016.12.14;The below log was changed ERROR level to WARNING level.
            // For removing this log on the performace firmware.
            // In the debug firmware, log is printed from  unimportant level to WARNING level.
            // In the release & perf firmware, log is printed from ERROR level to more ciritical level.
            SF_LOG_W( "[%s] file %s is not signed. Sign it", __FUNCTION__, pName );
            break;

        case SF_STATUS_UEP_SIGNATURE_CORRECT:
            SF_LOG_I( "[Verification correct][%s].Level:%c", pFile->f_path.dentry->d_iname, uepLevel );
            break;

        case SF_STATUS_UEP_SIGNATURE_INCORRECT:
            SF_LOG_E( "[%s] file %s has incorrect signature", __FUNCTION__, pName );
            break;

        case SF_STATUS_FAIL:
            SF_LOG_E( "[%s] signature general check error on file %s", __FUNCTION__, pName );
            break;
        case SF_STATUS_OK:
            // To show log for RO, uncomment the below line
            // SF_LOG_I( "[%s] signature verification skipped. It's in RO. %s ", __FUNCTION__, pName );
            break;
        case SF_STATUS_UEP_FILE_NO_MAGICNUM:
            SF_LOG_E( "[%s] file %s doesn't has magic number", __FUNCTION__, pName );
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
static SF_FILE_TYPE SfdUepCheckFileType(struct file* const pFile)
{
    SF_FILE_TYPE type = SF_FILE_TYPE_UNKNOWN;
    char eIdent[4] = {0};

    do
    {
        if ( SF_FAILED( SfdUepReadFile( pFile, 0, eIdent, sizeof(eIdent) ) ) )
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
bool SfdUepCheckRWVDFS(struct file* const pFile)
{
    struct path* pPath;
    Char * pBuffer = NULL;
    Char * pName = NULL;
    const Char * pMntName = NULL;
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


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyFileSignature(struct file* const pFile, int bCheckAlways, int bIsSo)
{
    SF_STATUS       result = SF_STATUS_OK;
    UepSignatureInfo info;
    bool bIsRW = FALSE;
    unsigned long long uino = 0;

    info.uepLevel = SFD_UEP_LEVEL_NULL;

    // Check cache tale    
    if( unlikely(NULL == pFile) || (NULL == pFile->f_inode) )
    {
        SF_LOG_W("[%s] inode is null", __FUNCTION__);
        return SF_STATUS_BAD_ARG;     
    }

    uino = SfdGetUniqueIno(pFile->f_inode);
    
#ifdef CONFIG_SECURITY_SFD_CHECK_STATFS
    bIsRW = !SfdUepCheckRWVDFS( pFile );
#else 
    bIsRW = SF_STATUS_OK == SfdCheckFileIsInRW( pFile )? TRUE:FALSE;
#endif

    if( SF_SUCCESS( SfdCacheCheck( uino, SFD_MODULE_TYPE_UEP, &result, &(info.uepLevel), bIsRW ) ) )
    {
        if( SF_STATUS_PENDING != result )
        {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
            //SF_LOG_I("[%s] Cache Hit!!!, i_ino:%u, result:%u, uepLevel:%c",
            //    __FUNCTION__, pFile->f_inode->i_ino, result, info.uepLevel );
#endif // CONFIG_SECURITY_SFD_LEVEL_DEBUG
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
    info.fileType = SfdUepCheckFileType(pFile);
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

    result = SfdUepReadSignatureFromFile(pFile, SfdUepGetFileSize(pFile), &info);
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
            goto SF_UEP_ADD_CACHE;
        }
    }
    else if( SF_STATUS_OK == result )
    {
        if( FALSE == bCheckAlways && FALSE == bIsRW && (SFD_UEP_LEVEL_CONTAINER_ALLOW > info.uepLevel) )
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
        info.uepLevel = SFD_UEP_LEVEL_NOTSIGNED;
        result = SF_STATUS_UEP_FILE_NOT_SIGNED;
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        SF_LOG_I("[%s] No signature founded", __FUNCTION__);
#endif // 
        goto SF_UEP_ADD_CACHE;
    }

    // Check sign 
    result = SfdUepCheckFileSignature(pFile, &info);
    if (info.signature != NULL)
    {
        sf_free(info.signature);
        info.signature = NULL;
    }
SF_UEP_ADD_CACHE:
    //SF_LOG_I("\e[1;33m[file : %s][SFDInode : %lld];\e[0m",pFile->f_path.dentry->d_iname,uino);
    // Add the result into cache table
    if( SF_FAILED( SfdCacheAdd( uino, SFD_MODULE_TYPE_UEP, result, info.uepLevel, bIsRW ) ) )
    {
#ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
        SF_LOG_W("[%s] Failed to cache UEP Result: i_ino:%u, result:%u",
                __FUNCTION__, pFile->f_inode->i_ino, result );
#endif // #ifdef CONFIG_SECURITY_SFD_LEVEL_DEBUG
    }


    /**
    * @note This function print logs. It will be changed changed by the static inline empty stub,
    *   when kernel will be compiled in the release mode.
    */
    SfdUepHandleVerificationResult(pFile, result, info.uepLevel);

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
        result = SF_STATUS_UEP_SIGNATURE_INCORRECT;
    
	/**
    * @note Securezone needs signature'level in condition of kUEP disabled.
    */
    if( s_uepStatus == 0 )
    {
        result = SF_STATUS_UEP_SIGNATURE_CORRECT;
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
