/**
****************************************************************************************************
* @file SfNetlinkKernel.c
* @brief Security framework [SF] filter driver [D] Implementation of the Netlink transport
*   mechanisms for kernel space modules
* @date Created May 23, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfMemory.h>
#include <uapi/linux/sf/transport/SfTransport.h>
#include <uapi/linux/sf/transport/SfSerialization.h>
#include <uapi/linux/sf/protocol/SfPacket.h>
#include <uapi/linux/sf/protocol/SfEnvironmentFormat.h>
#include <uapi/linux/netlink.h>

#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <net/sock.h>

#define SF_NETLINK_CACHE_SIZE 10
#define SF_NETLINK_CACHE_ENV_TYPE_MAX (SF_ENVIRONMENT_TYPE_MAX + 1)

#define SF_CACHE_HASH_SIZE 16
#define SF_SFPMD_BIND_END_POINT 0xEC27

#define MD5_memcpy sf_memcpy
#define MD5_memset sf_memset
#define CalcMD5(pText, pOutput) \
({ \
    MD5_CTX ctx_md5; \
    MD5Init(&ctx_md5); \
    MD5Update(&ctx_md5, (unsigned char*)pText, strlen(pText)); \
    MD5Final(&ctx_md5, pOutput); \
})

#define SF_EVENT_CACHE_COMPARE01(EnvType, pEnvironment, member1, member2) \
    ({ \
        int idx = 0; \
        EnvType *pEnv = NULL; \
        pEnv = (EnvType *)pEnvironment; \
        for (idx = 0; idx < gCacheContext.CacheCnt[pEnvironment->type]; idx++) \
        { \
            if ((pEnv->member1 == gCacheContext.pCache[pEnvironment->type][idx].inode) && \
                (pEnv->processContext.processId == gCacheContext.pCache[pEnvironment->type][idx].EnvprocessId)) \
            { \
                if (pEnv->member2) \
                { \
                    Uchar out[SF_CACHE_HASH_SIZE] = {0}; \
                    CalcMD5(pEnv->member2, out); \
                    if (memcmp(gCacheContext.pCache[pEnvironment->type][idx].szNameHash, out, SF_CACHE_HASH_SIZE) == 0) \
                    { \
                        if (pEnv->processContext.pProcessName) \
                        { \
                            CalcMD5(pEnv->processContext.pProcessName, out); \
                            if (memcmp(gCacheContext.pCache[pEnvironment->type][idx].szEnvProcessNameHash, out, SF_CACHE_HASH_SIZE) == 0) \
                            { \
                                result = SF_STATUS_OK; \
                                return result; \
                            } \
                        } \
                    } \
                } \
            } \
        } \
    })

#define SF_EVENT_CACHE_COMPARE02(EnvType, pEnvironment, member1, member2) \
    ({ \
        int idx = 0; \
        EnvType *pEnv = NULL; \
        pEnv = (EnvType *)pEnvironment; \
        for (idx = 0; idx < gCacheContext.CacheCnt[pEnvironment->type]; idx++) \
        { \
            if ((pEnv->member1 == gCacheContext.pCache[pEnvironment->type][idx].member1) && \
                (pEnv->member2 == gCacheContext.pCache[pEnvironment->type][idx].member2) && \
                (pEnv->processContext.processId == gCacheContext.pCache[pEnvironment->type][idx].EnvprocessId)) \
            { \
                if (pEnv->processContext.pProcessName) \
                { \
                    Uchar out[SF_CACHE_HASH_SIZE] = {0}; \
                    CalcMD5(pEnv->processContext.pProcessName, out); \
                    if (memcmp(gCacheContext.pCache[pEnvironment->type][idx].szEnvProcessNameHash, out, SF_CACHE_HASH_SIZE) == 0) \
                    { \
                        result = SF_STATUS_OK; \
                        return result; \
                    } \
                } \
            } \
        } \
    })

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))
#define FF(a, b, c, d, x, s, ac) { \
 (a) += F ((b), (c), (d)) + (x) + (unsigned long int)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }

#define GG(a, b, c, d, x, s, ac) { \
 (a) += G ((b), (c), (d)) + (x) + (unsigned long int)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }

#define HH(a, b, c, d, x, s, ac) { \
 (a) += H ((b), (c), (d)) + (x) + (unsigned long int)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }

#define II(a, b, c, d, x, s, ac) { \
 (a) += I ((b), (c), (d)) + (x) + (unsigned long int)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }

/* MD5 context. */
typedef struct 
{
	unsigned long int state[4];                                   /* state (ABCD) */
	unsigned long int count[2];        /* number of bits, modulo 2^64 (lsb first) */
	unsigned char buffer[64];                         /* input buffer */
} MD5_CTX;

/**
****************************************************************************************************
* @brief This structure implements each cache item
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfNetlinkPacket* pItem;                         ///< point for storing SfNetlinkPacket 
    Uint32  env_type;                               ///< SfPacket's environment type
    Uint32  op_type;                                ///< SfPacket's operation type
    Uchar szNameHash[SF_CACHE_HASH_SIZE];           ///< text's hash value (pFileName, pLibraryName, pProcessName)
    Uchar szEnvProcessNameHash[SF_CACHE_HASH_SIZE]; ///< text's hash value (execution environment process name)
    Uint64 inode;                                   ///< inode, processImageId
    Uint32 addr;                                    ///< socket addr
    Uint32 EnvprocessId;                            ///< execution environment process id
    Uint16 port;                                    ///< socket port
} SfNetlinkCacheFormat;

/**
****************************************************************************************************
* @brief This structure implements cache context.
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfNetlinkCacheFormat pCache[SF_NETLINK_CACHE_ENV_TYPE_MAX][SF_NETLINK_CACHE_SIZE]; ///< cache store
    Int CachePos[SF_NETLINK_CACHE_ENV_TYPE_MAX];                                       ///< cache position in each message type
    Int CacheCnt[SF_NETLINK_CACHE_ENV_TYPE_MAX];                                       ///< cache count in each message type
} SfNetlinkCacheContext;

static DEFINE_RWLOCK(g_SfdEventCacheLock); ///< Defining spin lock for reading and writting
static volatile int CacheInitialized = 0;  ///< Indicate whether cache context is initialized.
SfNetlinkCacheContext gCacheContext;       ///< Declaring Cache context

SfNode* gpNetlinkNode = NULL;
SfBindingCallback cbSendQueueInBind = NULL;///< Callback that sending netlink message on queue.

//Constants for MD5Transform routine.
static unsigned char S[4][4] = {
	{7, 12, 17, 22},
	{5,  9, 14, 20},
	{4, 11, 16, 23},
	{6, 10, 15, 21}
};

static unsigned char PADDING[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
****************************************************************************************************
* MD5 initialization. Begins an MD5 operation, writing a new context.
****************************************************************************************************
*/
void MD5Init (MD5_CTX *context)
{
	context->count[0] = 0;
    context->count[1] = 0;
	/* Load magic initialization constants.*/
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
}

/*
****************************************************************************************************
* Encodes input (unsigned long int) into output (unsigned char). Assumes len is a multiple of 4.
****************************************************************************************************
*/
void Encode (unsigned char *output, unsigned long int *input, unsigned int len)
{
	unsigned int i = 0;
    unsigned int j = 0;

	for (i = 0, j = 0; j < len; i++, j += 4) 
	{
		output[j] = (unsigned char)(input[i] & 0xff);
		output[j+1] = (unsigned char)((input[i] >> 8) & 0xff);
		output[j+2] = (unsigned char)((input[i] >> 16) & 0xff);
		output[j+3] = (unsigned char)((input[i] >> 24) & 0xff);
	}
}

/*
****************************************************************************************************
* Decodes input (unsigned char) into output (unsigned long int). Assumes len is a multiple of 4.
****************************************************************************************************
*/
void Decode (unsigned long int *output, unsigned char *input, unsigned int len)
{
	unsigned int i = 0;
    unsigned int j = 0;

	for (i = 0, j = 0; j < len; i++, j += 4)
	{
		output[i] = ((unsigned long int)input[j]) | (((unsigned long int)input[j+1]) << 8) |
		            (((unsigned long int)input[j+2]) << 16) | (((unsigned long int)input[j+3]) << 24);
	}
}

/*
****************************************************************************************************
* MD5 basic transformation. Transforms state based on block.
****************************************************************************************************
*/
void MD5Transform (unsigned long int state[4], unsigned char block[64])
{
    unsigned int a = state[0];
	unsigned int b = state[1];
	unsigned int c = state[2];
	unsigned int d = state[3];
	unsigned int x[16] = {0};

	Decode ((void*)x, block, 64);

	/* Round 1 */
	FF (a, b, c, d, x[ 0], S[0][0], 0xd76aa478); /* 1 */
	FF (d, a, b, c, x[ 1], S[0][1], 0xe8c7b756); /* 2 */
	FF (c, d, a, b, x[ 2], S[0][2], 0x242070db); /* 3 */
	FF (b, c, d, a, x[ 3], S[0][3], 0xc1bdceee); /* 4 */
	FF (a, b, c, d, x[ 4], S[0][0], 0xf57c0faf); /* 5 */
	FF (d, a, b, c, x[ 5], S[0][1], 0x4787c62a); /* 6 */
	FF (c, d, a, b, x[ 6], S[0][2], 0xa8304613); /* 7 */
	FF (b, c, d, a, x[ 7], S[0][3], 0xfd469501); /* 8 */
	FF (a, b, c, d, x[ 8], S[0][0], 0x698098d8); /* 9 */
	FF (d, a, b, c, x[ 9], S[0][1], 0x8b44f7af); /* 10 */
	FF (c, d, a, b, x[10], S[0][2], 0xffff5bb1); /* 11 */
	FF (b, c, d, a, x[11], S[0][3], 0x895cd7be); /* 12 */
	FF (a, b, c, d, x[12], S[0][0], 0x6b901122); /* 13 */
	FF (d, a, b, c, x[13], S[0][1], 0xfd987193); /* 14 */
	FF (c, d, a, b, x[14], S[0][2], 0xa679438e); /* 15 */
	FF (b, c, d, a, x[15], S[0][3], 0x49b40821); /* 16 */

	/* Round 2 */
	GG (a, b, c, d, x[ 1], S[1][0], 0xf61e2562); /* 17 */
	GG (d, a, b, c, x[ 6], S[1][1], 0xc040b340); /* 18 */
	GG (c, d, a, b, x[11], S[1][2], 0x265e5a51); /* 19 */
	GG (b, c, d, a, x[ 0], S[1][3], 0xe9b6c7aa); /* 20 */
	GG (a, b, c, d, x[ 5], S[1][0], 0xd62f105d); /* 21 */
	GG (d, a, b, c, x[10], S[1][1], 0x02441453); /* 22 */
	GG (c, d, a, b, x[15], S[1][2], 0xd8a1e681); /* 23 */
	GG (b, c, d, a, x[ 4], S[1][3], 0xe7d3fbc8); /* 24 */
	GG (a, b, c, d, x[ 9], S[1][0], 0x21e1cde6); /* 25 */
	GG (d, a, b, c, x[14], S[1][1], 0xc33707d6); /* 26 */
	GG (c, d, a, b, x[ 3], S[1][2], 0xf4d50d87); /* 27 */
	GG (b, c, d, a, x[ 8], S[1][3], 0x455a14ed); /* 28 */
	GG (a, b, c, d, x[13], S[1][0], 0xa9e3e905); /* 29 */
	GG (d, a, b, c, x[ 2], S[1][1], 0xfcefa3f8); /* 30 */
	GG (c, d, a, b, x[ 7], S[1][2], 0x676f02d9); /* 31 */
	GG (b, c, d, a, x[12], S[1][3], 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH (a, b, c, d, x[ 5], S[2][0], 0xfffa3942); /* 33 */
	HH (d, a, b, c, x[ 8], S[2][1], 0x8771f681); /* 34 */
	HH (c, d, a, b, x[11], S[2][2], 0x6d9d6122); /* 35 */
	HH (b, c, d, a, x[14], S[2][3], 0xfde5380c); /* 36 */
	HH (a, b, c, d, x[ 1], S[2][0], 0xa4beea44); /* 37 */
	HH (d, a, b, c, x[ 4], S[2][1], 0x4bdecfa9); /* 38 */
	HH (c, d, a, b, x[ 7], S[2][2], 0xf6bb4b60); /* 39 */
	HH (b, c, d, a, x[10], S[2][3], 0xbebfbc70); /* 40 */
	HH (a, b, c, d, x[13], S[2][0], 0x289b7ec6); /* 41 */
	HH (d, a, b, c, x[ 0], S[2][1], 0xeaa127fa); /* 42 */
	HH (c, d, a, b, x[ 3], S[2][2], 0xd4ef3085); /* 43 */
	HH (b, c, d, a, x[ 6], S[2][3], 0x04881d05); /* 44 */
	HH (a, b, c, d, x[ 9], S[2][0], 0xd9d4d039); /* 45 */
	HH (d, a, b, c, x[12], S[2][1], 0xe6db99e5); /* 46 */
	HH (c, d, a, b, x[15], S[2][2], 0x1fa27cf8); /* 47 */
	HH (b, c, d, a, x[ 2], S[2][3], 0xc4ac5665); /* 48 */

	/* Round 4 */
	II (a, b, c, d, x[ 0], S[3][0], 0xf4292244); /* 49 */
	II (d, a, b, c, x[ 7], S[3][1], 0x432aff97); /* 50 */
	II (c, d, a, b, x[14], S[3][2], 0xab9423a7); /* 51 */
	II (b, c, d, a, x[ 5], S[3][3], 0xfc93a039); /* 52 */
	II (a, b, c, d, x[12], S[3][0], 0x655b59c3); /* 53 */
	II (d, a, b, c, x[ 3], S[3][1], 0x8f0ccc92); /* 54 */
	II (c, d, a, b, x[10], S[3][2], 0xffeff47d); /* 55 */
	II (b, c, d, a, x[ 1], S[3][3], 0x85845dd1); /* 56 */
	II (a, b, c, d, x[ 8], S[3][0], 0x6fa87e4f); /* 57 */
	II (d, a, b, c, x[15], S[3][1], 0xfe2ce6e0); /* 58 */
	II (c, d, a, b, x[ 6], S[3][2], 0xa3014314); /* 59 */
	II (b, c, d, a, x[13], S[3][3], 0x4e0811a1); /* 60 */
	II (a, b, c, d, x[ 4], S[3][0], 0xf7537e82); /* 61 */
	II (d, a, b, c, x[11], S[3][1], 0xbd3af235); /* 62 */
	II (c, d, a, b, x[ 2], S[3][2], 0x2ad7d2bb); /* 63 */
	II (b, c, d, a, x[ 9], S[3][3], 0xeb86d391); /* 64 */

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	/* Zeroize sensitive information.
	*/
	MD5_memset ((unsigned char*)x, 0, sizeof (x));
}



/*
****************************************************************************************************
* MD5 block update operation. Continues an MD5 message-digest 
* operation, processing another message block, and updating the context.
****************************************************************************************************
*/
void MD5Update (MD5_CTX *context, unsigned char *input, unsigned int inputLen)
                        /* input block */ /* length of input block */
{
	unsigned int i = 0;
    unsigned int index = 0;
    unsigned int partLen = 0;

	/* Compute number of bytes mod 64 */
	index = (unsigned int)((context->count[0] >> 3) & 0x3F);

	/* Update number of bits */
	if ((context->count[0] += ((unsigned long int)inputLen << 3)) < ((unsigned long int)inputLen << 3))
	{
		context->count[1]++;
	}
	context->count[1] += ((unsigned long int)inputLen >> 29);

	partLen = 64 - index;

	/* Transform as many times as possible.*/
	if (inputLen >= partLen) 
	{
		MD5_memcpy((unsigned char*)&context->buffer[index], (unsigned char*)input, partLen);
		MD5Transform (context->state, context->buffer);

		for (i = partLen; i + 63 < inputLen; i += 64)
		{
			MD5Transform (context->state, &input[i]);
		}
		index = 0;
	}
	else
	{
		i = 0;
	}

	/* Buffer remaining input */
	MD5_memcpy((unsigned char*)&context->buffer[index], (unsigned char*)&input[i], inputLen-i);
}

/*
****************************************************************************************************
* MD5 finalization. Ends an MD5 message-digest operation, 
* writing the the message digest and zeroizing the context.
****************************************************************************************************
*/
void MD5Final (MD5_CTX *context, unsigned char digest[16])
                    /* context */      /* message digest */
{
	unsigned char bits[8] = {0};
	unsigned int index = 0;
    unsigned int padLen = 0;

	/* Save number of bits */
	Encode (bits, context->count, 8);

	/* Pad out to 56 mod 64.*/
	index = (unsigned int)((context->count[0] >> 3) & 0x3f);
	padLen = (index < 56) ? (56 - index) : (120 - index);
	MD5Update (context, PADDING, padLen);

	/* Append length (before padding) */
	MD5Update (context, bits, 8);
	/* Store state in digest */
	Encode (digest, context->state, 16);

	/* Zeroize sensitive information.*/
	MD5_memset ((unsigned char*)context, 0, sizeof(*context));
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdStoreEventToCache(SfProtocolHeader* const pEnvironment, Uint OpType, SfNetlinkPacket* pNPacket)
{
    SF_STATUS result = SF_STATUS_FAIL;
    Uint EnvType = 0;

    if ((!pEnvironment) || (!pNPacket))
    {
        SF_LOG_E("invalid parameter");
        return result;
    }    
    EnvType = pEnvironment->type;
    
    sf_memset(gCacheContext.pCache [EnvType] + gCacheContext.CachePos[EnvType], 0x00, sizeof(SfNetlinkCacheFormat));
    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].pItem = pNPacket;
    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].op_type = OpType;
    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].env_type = EnvType;
    switch (EnvType)
    {
        case SF_ENVIRONMENT_TYPE_FILE:
            {
                SfFileEnvironment *pFileEnv = NULL;
                pFileEnv = (SfFileEnvironment *)pEnvironment;
                if (pFileEnv->pFileName)
                {
                    CalcMD5(pFileEnv->pFileName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szNameHash);
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].inode = pFileEnv->inode;
            
                    if (pFileEnv->processContext.pProcessName)
                    {
                        CalcMD5(pFileEnv->processContext.pProcessName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szEnvProcessNameHash);
                    }
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].EnvprocessId = pFileEnv->processContext.processId;
                }
                result = SF_STATUS_OK;
            }
            break;
        case SF_ENVIRONMENT_TYPE_NETWORK:
            {
                SfNetworkEnvironment *pNetworkEnv = NULL;
                pNetworkEnv = (SfNetworkEnvironment *)pEnvironment;
                gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].addr = pNetworkEnv->addr;
                gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].port = pNetworkEnv->port;
               
                if (pNetworkEnv->processContext.pProcessName)
                {
                    CalcMD5(pNetworkEnv->processContext.pProcessName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szEnvProcessNameHash);
                }
                gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].EnvprocessId = pNetworkEnv->processContext.processId;
                result = SF_STATUS_OK;
            }
            break;
        case SF_ENVIRONMENT_TYPE_PROCESS:
            {
                SfProcessEnvironment *pProcessEnv = NULL;
                pProcessEnv = (SfProcessEnvironment *)pEnvironment;
                if (pProcessEnv->pProcessName)
                {
                    CalcMD5(pProcessEnv->pProcessName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szNameHash);
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].inode = pProcessEnv->processImageId;
                
                    if (pProcessEnv->processContext.pProcessName)
                    {
                        CalcMD5(pProcessEnv->processContext.pProcessName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szEnvProcessNameHash);
                    }
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].EnvprocessId = pProcessEnv->processContext.processId;
                }
                result = SF_STATUS_OK;
            }
            break;
        case SF_ENVIRONMENT_TYPE_MMAP:
            {
                SfMmapEnvironment *pMmapEnv = NULL;
                pMmapEnv = (SfMmapEnvironment *)pEnvironment;
                if (pMmapEnv->pLibraryName)
                {
                    CalcMD5(pMmapEnv->pLibraryName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szNameHash);
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].inode = pMmapEnv->inode;
          
                    if (pMmapEnv->processContext.pProcessName)
                    {
                        CalcMD5(pMmapEnv->processContext.pProcessName, gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].szEnvProcessNameHash);
                    }
                    gCacheContext.pCache[EnvType][gCacheContext.CachePos[EnvType]].EnvprocessId = pMmapEnv->processContext.processId;
                }
                result = SF_STATUS_OK;
            }
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
SF_STATUS SfdIsDuplicateEvent(SfProtocolHeader* const pEnvironment)
{
    SF_STATUS result = SF_STATUS_FAIL;

    if (!pEnvironment)
    {
        SF_LOG_E("invalid parameter");
        return result;
    }

    switch (pEnvironment->type)
    {
        case SF_ENVIRONMENT_TYPE_FILE:
            {
                SF_EVENT_CACHE_COMPARE01(SfFileEnvironment, pEnvironment, inode, pFileName);
            }
            break;
        case SF_ENVIRONMENT_TYPE_NETWORK:
            {
                SF_EVENT_CACHE_COMPARE02(SfNetworkEnvironment, pEnvironment, addr, port);
            }
            break;
        case SF_ENVIRONMENT_TYPE_PROCESS:
            {
                SF_EVENT_CACHE_COMPARE01(SfProcessEnvironment, pEnvironment, processImageId, pProcessName);
            }
            break;
        case SF_ENVIRONMENT_TYPE_MMAP:
            {
                SF_EVENT_CACHE_COMPARE01(SfMmapEnvironment, pEnvironment, inode, pLibraryName);
            }
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
SF_STATUS SFAPI SfSendPacket(SfNode* const pNode, const SfPacket* const pPacket)
{
    SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;
    SfNetlinkPacket* pNetlinkPacket = NULL;
    SfNode* pCurrentNode = pNode;
    Bool bFileOpenBlocked = FALSE;
    Bool bUepBlocked = FALSE;
    int idx = 0;
    int i = 0;

    if (!pPacket || !pPacket->env)
    {
        SF_LOG_E( "%s(): packet or its environment is NULL", __FUNCTION__ );
        return SF_STATUS_BAD_ARG;
    }

    if (pPacket->op->type == SF_OPERATION_TYPE_EXEC)
    {
        SfOperationBprmCheckSecurity* pBprmCheckOperation = NULL;
        pBprmCheckOperation = (SfOperationBprmCheckSecurity*)pPacket->op;
        if ((pBprmCheckOperation->result == SF_STATUS_UEP_SIGNATURE_INCORRECT) ||
            (pBprmCheckOperation->result == SF_STATUS_UEP_FILE_NOT_SIGNED))
        {
            SfProcessEnvironment* pEnv = (SfProcessEnvironment*)pPacket->env;
            bUepBlocked = TRUE;
        }
    }
    else if (pPacket->op->type == SF_OPERATION_TYPE_OPEN)
    {
        SfOperationFileOpen* pFileOpenOperation = NULL;
        pFileOpenOperation = (SfOperationFileOpen*)pPacket->op;
        if (pFileOpenOperation->result == SF_STATUS_RESOURCE_BLOCK)
        {
            bFileOpenBlocked = TRUE;
        }
    }
    else if (pPacket->op->type == SF_OPERATION_TYPE_MMAP)
    {
        SfOperationFileMmap* pFileMmapOperation = NULL;
        pFileMmapOperation = (SfOperationFileMmap*)pPacket->op;
        if ((pFileMmapOperation->result == SF_STATUS_UEP_SIGNATURE_INCORRECT) ||
            (pFileMmapOperation->result == SF_STATUS_UEP_FILE_NOT_SIGNED))
        {
            SfMmapEnvironment* pMmapEnv = (SfMmapEnvironment*)pPacket->env;
            bUepBlocked = TRUE;
        }
    }

        // In below 3 case, message contains information about problem.
    if (
        // Blocked message from KUEP
        (bUepBlocked == TRUE) ||
        // Blocked message from firewall - file open
        (bFileOpenBlocked == TRUE) || 
        // Blocked message from firewall - send message
        ((pPacket->op->type == SF_OPERATION_TYPE_SENDMSG) && (pPacket->env != NULL)) ||
        // Blocked message from firewall - receive message
        ((pPacket->op->type == SF_OPERATION_TYPE_RECVMSG) && (pPacket->env != NULL)) 
        )
    {
        do
        {
            // check if nobody wants to receive this packet
            // for some reason it does not work on Tizen kernel
            if (!netlink_has_listeners( pCurrentNode->pHandle, pPacket->op->type))
            {
                result = SF_STATUS_OK;
                break;
            }
            
            pNetlinkPacket = SfSerializePacket(pPacket);
            if ( !pNetlinkPacket )
            {
                SF_LOG_E( "%s(): failed to serialize packet", __FUNCTION__);
                result = SF_STATUS_FAIL;
                break;
            }
    
            // broadcast message
            nlmsg_multicast(pCurrentNode->pHandle, pNetlinkPacket->pBuffer, 0, pPacket->op->type,
                             GFP_KERNEL);
            
            // fix 4byte memory leak, free just SfNetlinkPacket not sk_buff. refer DF141103-00255.
            sf_free( pNetlinkPacket );
    	    pNetlinkPacket = NULL;
            result = SF_STATUS_OK;
        } while(FALSE);
    }
    else // Message will be sent to user-level from kernel-level.  The message is no problem.
    {
        // Cache context is not initialized... Init context        
        write_lock(&g_SfdEventCacheLock);
        if (CacheInitialized == 0)
        {
            CacheInitialized = 1;
            sf_memset(&gCacheContext, 0x00, sizeof(SfNetlinkCacheContext));
        } /* end if (CacheInitialized == 0) */
        write_unlock(&g_SfdEventCacheLock);
        
        do
        {
            Uint OperationType = pPacket->op->type;
            Uint EnvironmentType = pPacket->env->type;
            if (EnvironmentType > SF_NETLINK_CACHE_ENV_TYPE_MAX)
            {
                SF_LOG_E( "Invalud environment type" );
                result = SF_STATUS_FAIL;
                break;
            }

            read_lock(&g_SfdEventCacheLock);
            if (SfdIsDuplicateEvent(pPacket->env) == SF_STATUS_OK)
            {
                result = SF_STATUS_OK;
                read_unlock(&g_SfdEventCacheLock);
                break;
            }
            read_unlock(&g_SfdEventCacheLock);
            
            pNetlinkPacket = SfSerializePacket(pPacket);
            if (!pNetlinkPacket)
            {
                SF_LOG_E( "Failed to serialize packet" );
                result = SF_STATUS_FAIL;
                break;
            }

            if (!write_trylock(&g_SfdEventCacheLock))
            {
                SfDestroyNetlinkPacket(pNetlinkPacket);
                result = SF_STATUS_OK;
                break;
            }

            if (gCacheContext.CacheCnt[EnvironmentType] > 0)
            {                
                if (gCacheContext.CacheCnt[EnvironmentType]  == SF_NETLINK_CACHE_SIZE)
                {
                    int idx = 0;
                    SfNetlinkPacket* pNPacketIter = NULL;

                    for (idx = 0; idx < SF_NETLINK_CACHE_SIZE; idx++)
                    {
                        pNPacketIter = gCacheContext.pCache[EnvironmentType][idx].pItem;
                        if (pNPacketIter)
                        {
                            // check if nobody wants to receive this packet
                            // for some reason it does not work on Tizen kernel
                            if (!netlink_has_listeners(pCurrentNode->pHandle, 
                                gCacheContext.pCache[EnvironmentType][idx].op_type))
                            {
                                SfDestroyNetlinkPacket(pNPacketIter);
                            }
                            else
                            {
                                // broadcast message
                                nlmsg_multicast(pCurrentNode->pHandle, pNPacketIter->pBuffer, 0, gCacheContext.pCache[EnvironmentType][idx].op_type,
                                        GFP_ATOMIC);
                                // fix 4byte memory leak, free just SfNetlinkPacket not sk_buff. refer DF141103-00255.
                        	    sf_free(pNPacketIter);
                                pNPacketIter = NULL;
                    	    }
                    	    gCacheContext.pCache[EnvironmentType][idx].pItem = NULL;
                        }
                    } /* end for (idx = 0; idx < SF_NETLINK_CACHE_SIZE; idx++) */
                    // init
                    gCacheContext.CacheCnt[EnvironmentType] = 0;
                    gCacheContext.CachePos[EnvironmentType] = 0;
                }
                else
                {
                    gCacheContext.CachePos[EnvironmentType]++;
                }                
            }
            
            if (SfdStoreEventToCache(pPacket->env, OperationType, pNetlinkPacket) == SF_STATUS_OK)
            {
                gCacheContext.CacheCnt[EnvironmentType]++;
            }
            else
            {
                SF_LOG_E("writing cahce failed");
                result = SF_STATUS_FAIL;
                SfDestroyNetlinkPacket(pNetlinkPacket);
                write_unlock(&g_SfdEventCacheLock);
                break;
            }
            result = SF_STATUS_OK;
            write_unlock(&g_SfdEventCacheLock);
        } while(FALSE);
    }
    return result;
}
EXPORT_SYMBOL(SfSendPacket);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfReceivePacket( SfNode* const pNode, SfPacket** const ppPacket )
{
    SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;

    return result;
}

//void SfBind(int group)
int SfBind(struct net *net, int group)
{
    static int BindDigit = 0;
    SF_LOG_I("Bind was called for process %s and pid %d", current->comm, current->pid);
    if (cbSendQueueInBind && memcmp(current->comm, "sfpmd", 5) == 0)
    {
        if ((group > 0) && (group <= (int)SF_OPERATION_TYPE_MAX))
        {
            BindDigit |= (1 << (group - 1));
            if (BindDigit == SF_SFPMD_BIND_END_POINT)
            {
                SF_LOG_I("Called; do callback for %s;", current->comm);
                cbSendQueueInBind(NULL);
                cbSendQueueInBind = NULL;
            }
        }
    }
    return  0;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfCreateNode(SfNode** ppNode, const Char* const name, Ulong id,
    SfReceiveMessagePtr pHandler)
{
    SF_STATUS result = SF_STATUS_OK;
    SfNode* pNode = NULL;
    struct netlink_kernel_cfg netlinkConfig =
    {
        .bind = SfBind,
        .input = NULL,
        .flags = NL_CFG_F_NONROOT_RECV
    };

    do
    {
        if (NULL == ppNode || NULL == name)
        {
            SF_LOG_E("%s ppNode parameter equals to %p", __FUNCTION__, ppNode);
            result = SF_STATUS_BAD_ARG;
            break;
        }

        *ppNode = sf_malloc(sizeof(SfNode));
        if (NULL == *ppNode)
        {
            SF_LOG_E("%s *ppNode equals to %p", __FUNCTION__, *ppNode);
            result = SF_STATUS_FAIL;
            break;
        }

        pNode = *ppNode;
        netlinkConfig.input = pHandler;

        sf_strncpy(pNode->id.name, name, sizeof(pNode->id.name));
        pNode->id.magic = id;

        pNode->pHandle = netlink_kernel_create(&init_net, SF_PROTOCOL_NUMBER, &netlinkConfig);

        if (NULL == pNode->pHandle)
        {
            SF_LOG_E("%s Can not create netlink socket", __FUNCTION__);
            result = SF_STATUS_FAIL;
            sf_free(*ppNode);
            *ppNode = NULL;
        }

    } while(FALSE);

    return result;
}
EXPORT_SYMBOL(SfCreateNode);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfDestroyNode(SfNode* const pNode)
{
    SF_STATUS result = SF_STATUS_FAIL;

    if (NULL != pNode)
    {
        if (NULL != pNode->pHandle)
        {
            netlink_kernel_release(pNode->pHandle);
        }
        sf_free(pNode);
        result = SF_STATUS_OK;
    }

    return result;
}
EXPORT_SYMBOL(SfDestroyNode);

SF_STATUS SFAPI SfSetNetlinkNode(SfNode* const pNode)
{
    if (pNode == NULL)
    {
        return SF_STATUS_BAD_ARG;
    }
    
    gpNetlinkNode = pNode;
    return SF_STATUS_OK;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfSendReport(SfProtocolHeader* const pOperation)
{
    SF_STATUS result = SF_STATUS_FAIL;
    SfPacket packet =
    {
        .header =
        {
            .size = sizeof(SfPacket),
            .type = SF_PACKET_TYPE_REPORT
        },
        .env = NULL,
        .op = pOperation
    };
    
    
    result = SfSendPacketInternal(&packet);
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfSendPacketInternal(const SfPacket* const pPacket)
{
    SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;
    SfNode* pCurrentNode = NULL;

    if ( !pPacket || !pPacket->op )
    {
        SF_LOG_E( "%s(): packet or its operation is NULL", __FUNCTION__ );
        return SF_STATUS_BAD_ARG;
    }

    if (gpNetlinkNode == NULL)
    {
        SF_LOG_E( "%s(): Netlink node for report is NULL", __FUNCTION__ );
        return SF_STATUS_FAIL;
    }
    pCurrentNode = gpNetlinkNode;

    do
    {
        SfNetlinkPacket* pNetlinkPacket = NULL;

        // check if nobody wants to receive this packet
        // for some reason it does not work on Tizen kernel
        if ( !netlink_has_listeners( pCurrentNode->pHandle, pPacket->op->type ) )
        {
            result = SF_STATUS_OK;
            break;
        }

        pNetlinkPacket = SfSerializePacket( pPacket );
        if ( !pNetlinkPacket )
        {
            SF_LOG_E( "%s(): failed to serialize packet", __FUNCTION__ );
            result = SF_STATUS_FAIL;
            break;
        }

        // broadcast message
        nlmsg_multicast(pCurrentNode->pHandle, pNetlinkPacket->pBuffer, 0, pPacket->op->type,
                         GFP_KERNEL );
        // fix 4byte memory leak, free just SfNetlinkPacket not sk_buff. refer DF141103-00255.
        sf_free( pNetlinkPacket );
	    pNetlinkPacket = NULL;

        result = SF_STATUS_OK;
    }
    while ( FALSE );
    return result;
}

SF_STATUS SFAPI SfSetCbInBind(SfBindingCallback callback)
{
    if (callback == NULL)
    {
        return SF_STATUS_BAD_ARG;
    }
    
    cbSendQueueInBind = callback;
    return SF_STATUS_OK;
}

