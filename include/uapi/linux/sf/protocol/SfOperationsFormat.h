/**
****************************************************************************************************
* @file SfOperationsFormat.h
* @brief Security framework [SF] filter driver [D] system operations implementation.
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 18, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_OPERATIONS_FORMAT_H_
#define _SF_OPERATIONS_FORMAT_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#include "SfProtocolHeader.h"

#define SF_MNT_INFO_SIZE 4
#define SF_MNT_FLAG_SIZE 10
#define SF_RET_INFO_SIZE 20
#define SF_ADD_MNT_INFO  0
#define SF_ADD_RET_INFO  1

/**
****************************************************************************************************
* @def SF_VALIDATE_OPERATION
* @brief Performs validation of the operation structure
* @note Function-like macros. Return value (stack memory).
* @param[in] pOperationInterface An interface to the corresponding LSM function operation
* @return SF_STATUS_OK on validation success, SF_STATUS_BAD_ARG on validation fail
****************************************************************************************************
*/
#define SF_VALIDATE_OPERATION(pOperationInterface) \
({ \
	SF_STATUS validationResult = SF_STATUS_OK; \
	if (NULL == pOperationInterface || pOperationInterface->size < sizeof(SfProtocolHeader)) \
	{ \
		validationResult = SF_STATUS_BAD_ARG; \
	} \
	validationResult; \
})

#define SF_CREATE_OPERATION(operation, operationType) \
    ({ \
    SfProtocolHeader* pOperation = sf_malloc( sizeof( operation ) ); \
    if ( NULL != pOperation ) \
    { \
        sf_zero_memory( pOperation, sizeof( operation ) ); \
        ((operation*)pOperation)->header.size = sizeof ( operation ); \
        ((operation*)pOperation)->header.type = operationType; \
    } \
    else \
    { \
        SF_LOG_E( "%s can not allocate memory for the network environment structure", \
            __FUNCTION__ ); \
    } \
    pOperation; \
    })

/**
****************************************************************************************************
* @typedef SF_OPERATION_TYPE
* @brief Operations type definition. Operation mapped to corresponding system call
****************************************************************************************************
*/
typedef enum
{
    SF_OPERATION_TYPE_OPEN = 1,        ///< sys_open
    SF_OPERATION_TYPE_CONNECT = 2,     ///< sys_connect or sys_socketcall
    SF_OPERATION_TYPE_MMAP = 3,        ///< sys_mmap
    SF_OPERATION_TYPE_BIND = 4,        ///< sys_read
    SF_OPERATION_TYPE_LISTEN = 5,      ///< sys_write
    SF_OPERATION_TYPE_EXEC = 6,        ///< sys_execve
    SF_OPERATION_TYPE_ACCEPT = 7,      ///< sys_accept
    SF_OPERATION_TYPE_SOCKET = 8,      ///< sys_socket
    SF_OPERATION_TYPE_RULE = 9,        ///< block rule
    SF_OPERATION_TYPE_SETUP_DUID = 10, ///< DUID setup
    SF_OPERATION_TYPE_SENDMSG = 11,        ///< sendmsg()
    SF_OPERATION_TYPE_RECVMSG = 12,        ///< recvmsg()
    SF_OPERATION_TYPE_SND_RCV_RULE = 13,   ///< firewall rule for TCP/UDP
    SF_OPERATION_TYPE_REPORT_NET = 14,     ///< report about security problem on net
    SF_OPERATION_TYPE_REPORT_PROCESS = 15, ///< report about security problem on process
    SF_OPERATION_TYPE_REPORT_FILE = 16,    ///< report about security problem on file
    SF_OPERATION_TYPE_MAX = SF_OPERATION_TYPE_REPORT_FILE
} SF_OPERATION_TYPE;

#if defined(SF_LEVEL_KERNEL)

/**
****************************************************************************************************
* @struct SfOperationFileOpen
* @brief file_open LSM function argument formating structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Structure header

	/**
	* @brief Pointer to the file structure
	* @warning This structure pointer may point to the unexisting file.
	*/
    struct file* pFile;
    struct cred* pCred; ///< Pointer to the task security context
    int result;

} SfOperationFileOpen;

/**
****************************************************************************************************
* @struct SfOperationFileMmap
* @brief mmap_file LSM function argument formating structure
****************************************************************************************************
*/
typedef struct
{
	SfProtocolHeader header; ///< Structure header

	/**
	* @brief Pointer to the file structure to be maped to the memory
	* @warning May be NULL in case of mapping anonymous memory
	*/
	struct file* pFile;

	/**
	* @brief Protection requested by the application in which context operation is executing
	* @note Usually this handler raised on sys_mmap_pgoff and sys_mmap (sys_mmap2)
	*	system call handler
	*/
	unsigned long reqProt;

	/**
	* @brief Protection that stored in the kernel and will be applied for the operation with
	*	this resource
	*/
	unsigned long prot;
	unsigned long flags; ///< Contains the operational flags. Looking for PROT_EXEC flag.
	int result;
	// To fix bypass using mount vulnerability, add member indicator that kUEP should check 
	int bCheckAlways;
	int bIsSo;    
    int sysError; ///< the system error the kernel_read
    char szMntInfo[SF_MNT_INFO_SIZE + 1];                          ///< mount infomation
    char szMntFlag[SF_MNT_FLAG_SIZE + 1];                          ///< mount flag
    char szRetInfo[SF_RET_INFO_SIZE + 1];                          ///< return info
} SfOperationFileMmap;

/**
****************************************************************************************************
* @struct SfOperationSysExecve
* @brief sf_bprm_check_security LSM function argument formating structure. Raised at the moment when
*	started searching of the binary handler.
* @warning This handler may be raised several times during sys_exec or sys_execve system calls.
****************************************************************************************************
*/
typedef struct
{
	SfProtocolHeader header; ///< Structure header

	/**
	* @brief Pointer to the binary parameters of the application that going to be executed.
	* @warning May be NULL.
	*/
	struct linux_binprm* pBinParameters;

	int result; ///< Result of the operation verification
	int sysError; ///< the system error the kernel_read

    char szMntInfo[SF_MNT_INFO_SIZE + 1];                          ///< mount infomation
    char szMntFlag[SF_MNT_FLAG_SIZE + 1];                          ///< mount flag
    char szRetInfo[SF_RET_INFO_SIZE + 1];                          ///< return info
} SfOperationBprmCheckSecurity;

struct socket;
struct sockaddr;

/**
****************************************************************************************************
* @struct SfOperationSocketBind
* @brief socket_bind LSM function argument formating structure.
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Structure header
	struct socket* pSocket; ///< Pointer to the socket structure
	struct sockaddr* pAddress; ///< Pointer to the address that need to be bind
	int addressLength; ///< Size in bytes of the pAddress
} SfOperationSocketBind;

/**
****************************************************************************************************
* @struct SfOperationSysConnect
* @brief socket_connect LSM function argument formating structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header;   ///< Structure header
	struct socket* pSocket;    ///< Pointer to the socket structure
	struct sockaddr* pAddress; ///< Pointer to the address that need to be bind
	int addressLength;         ///< Size in bytes of the pAddress
    int result;                ///< Processing result
} SfOperationSocketConnect;

/**
****************************************************************************************************
* @struct SfOperationSocketListen
* @brief socket_listen LSM function argument formating structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Structure header
	struct socket* pSocket; ///< Pointer to the socket structure
	int backLog; ///< Maximum length of the pending connection queue
} SfOperationSocketListen;

/**
****************************************************************************************************
* @struct SfOperationSocketAccept
* @brief socket_accept LSM function argument formating structure
* @note This handler raise after socket was created but before accept operation was done
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Structure header
	struct socket* pSocket; ///< Pointer to the listening socket structure
	struct socket* pNewSocket; ///< Pointer to the newly created socket structure
} SfOperationSocketAccept;

/**
****************************************************************************************************
* @struct SfOperationSocketCreate
* @brief socket_create function argument formating structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Structure header
	int family; ///< Socket family
	int type; ///< connection type
	int protocol; ///< connection protorol
	int kernel; ///< socket level
} SfOperationSocketCreate;

/**
****************************************************************************************************
* @struct   SfOperationSocketSendmsg
* @brief    security_socket_sendmsg() function argument formatting structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;    ///< Structure header
    struct socket*   pSocket;   ///< Socket
    struct msghdr*   pMsg;      ///< Outcoming message
    int              size;      ///< Size
} SfOperationSocketSendmsg;

/**
****************************************************************************************************
* @struct   SfOperationSocketRecvmsg
* @brief    security_socket_recvmsg() function argument formatting structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;    ///< Structure header
    struct socket*   pSocket;   ///< Socket
    struct msghdr*   pMsg;      ///< Incoming message
    int              size;      ///< Size
    int              flags;     ///< Flags
} SfOperationSocketRecvmsg;

#endif  // SF_LEVEL_KERNEL

typedef enum
{
    SF_RULE_SOCKET_CONNECT,
    SF_RULE_FILE_OPEN
} SfBlockRuleType;

typedef enum
{
    SF_RULE_ADD,
    SF_RULE_DEL
} SfRuleActionType;

typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;

    SfBlockRuleType  ruleType;
    SfRuleActionType action;
    Uint32           ipAddr;
    Uint64           fileInode;
} SfOperationBlockRule;

typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;

    Char*            pDUID;
} SfOperationSetupDUID;

/**
****************************************************************************************************
* @struct   SfOperationSecurityReport
* @brief    sf_send_report_net_(...) function from security problem argument formatting structure
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;    ///< Structure header
    char* caller;               ///< module name from external
    char* filetype;             ///< module type from external
    char* filepath;             ///< file's path occurred security problem
    char* description;          ///< defail information about security problem
    int   sendfileflag;         ///< whether request is sending only log file
    int   autodatafile;         ///< whether description is parted.
    int   descriptionsize;      ///< partial description size
    int   desc_partial_cnt;     ///< total partial description count
    int   desc_partial_seq;     ///< partial description order in total description.
    char* desc_time;            ///< received time
} SfOperationSecurityReport;


typedef enum
{
    FW_DIR_INBOUND,
    FW_DIR_OUTBOUND,
    FW_DIR_ANY
} FwRuleDirection;

typedef enum
{
    FW_FIELD_SINGLE,
    FW_FIELD_MASK,
    FW_FIELD_RANGE
} FwRuleFieldType;

typedef enum
{
    FW_ADDR_LOCAL,
    FW_ADDR_REMOTE,
    FW_ADDR_ANY
} FwRuleAddressType;

typedef enum
{
    FW_TCP,
    FW_UDP
} FwRuleProtocol;

typedef struct
{
    FwRuleProtocol    protocol;
    FwRuleDirection   direction;

    FwRuleFieldType   ipFType;
    FwRuleAddressType ipAType;
    Uint32            ip[ 2 ];

    FwRuleFieldType   portFType;
    FwRuleAddressType portAType;
    Uint32            port[ 2 ];
} FwRule;

typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;

    FwRule rule;
} SfOperationFwRule;


void SfDestroyOperationRule( SfOperationBlockRule* pOperation );

void SfDestroyOperationSetupDUID( SfOperationSetupDUID* pOperation );

void SfDestroyOperationFwRule( SfOperationFwRule* pOperation );

void SfDestroyOperationReportInfo( SfOperationSecurityReport* pHeader );

void SfDestroyOperation( SfProtocolHeader* pHeader );

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif	/* !_SF_OPERATIONS_FORMAT_H_ */
