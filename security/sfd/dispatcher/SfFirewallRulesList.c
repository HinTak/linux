/**
****************************************************************************************************
* @file SfFirewallRulesList.c
* @brief Security framework [SF] filter driver [D] blocking rules list
* @author Dorogovtsev Dmitriy(d.dorogovtse@samsung.com)
* @date Created May 20, 2015
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2015. All rights reserved.
****************************************************************************************************
*/
#include "SfFirewallRulesList.h"

#include <linux/list.h>
#include <linux/rwsem.h>
#include <net/inet_sock.h>

typedef struct
{
    struct list_head node;
    FwRule rule;
} FRule;

typedef struct
{
    FwRuleDirection dir;
    FwRuleProtocol proto;
    Uint32 localAddr;
    Uint16 localPort;
    Uint32 remoteAddr;
    Uint32 remotePort;
} CallParams;


static DEFINE_RWLOCK(s_lockRules);
static LIST_HEAD(s_fwRuleList);

/**
****************************************************************************************************
*
****************************************************************************************************
*/
const char* GetProtocolName( FwRuleProtocol proto )
{
    switch ( proto )
    {
        case FW_TCP: return "tcp";
        case FW_UDP: return "udp";
        default:     return "";
    }
}

const char* GetDirectionName( FwRuleDirection dir )
{
    switch ( dir )
    {
        case FW_DIR_INBOUND:  return "inbound";
        case FW_DIR_OUTBOUND: return "outbound";
        case FW_DIR_ANY:      return "any";
        default:              return "";
    }
}

const char* GetCallName( FwRuleDirection dir )
{
    switch ( dir )
    {
        case FW_DIR_INBOUND:  return "recvmsg()";
        case FW_DIR_OUTBOUND: return "sendmsg()";
        default:              return "unknown";
    }
}

const char* GetFieldTypeName( FwRuleFieldType type )
{
    switch ( type )
    {
        case FW_FIELD_SINGLE: return "single";
        case FW_FIELD_MASK:   return "mask";
        case FW_FIELD_RANGE:  return "range";
        default:              return "";
    }
}

const char* GetAddressTypeName( FwRuleAddressType type )
{
    switch ( type )
    {
        case FW_ADDR_LOCAL:  return "local";
        case FW_ADDR_REMOTE: return "remote";
        case FW_ADDR_ANY:    return "any";
        default:             return "";
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static void PrintMatchedRule( const CallParams* pParams, const FwRule* pRule )
{
    SF_LOG_I( "%s(): %s call, protocol = %s, local %pI4:%u, remote %pI4:%u has matched rule: "
              "[protocol = %s, direction = %s, IP = (%s, %s, [0] = %pI4, [1] = %pI4), "
              "port = (%s, %s, [0] = %u, [1] = %u) ]",
              __FUNCTION__,
              GetCallName( pParams->dir ),
              GetProtocolName( pParams->proto ),
              &pParams->localAddr,  ntohs( pParams->localPort ),
              &pParams->remoteAddr, ntohs( pParams->remotePort ),
              GetProtocolName( pRule->protocol ),
              GetDirectionName( pRule->direction ),
              GetFieldTypeName( pRule->ipFType ),
              GetAddressTypeName( pRule->ipAType ),
              &( pRule->ip[ 0 ] ), &( pRule->ip[ 1 ] ),
              GetFieldTypeName( pRule->portFType ),
              GetAddressTypeName( pRule->portAType ),
              ntohs( pRule->port[ 0 ] ), ntohs( pRule->port[ 1 ] ) );
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchIpAddressSingle( const FwRule* pRule, Uint32 addr )
{
    Bool r = FALSE;

    switch ( pRule->ipFType )
    {
        case FW_FIELD_SINGLE:
            r = ( addr == pRule->ip[ 0 ] );
            break;

        case FW_FIELD_MASK:
            r = ( ( addr & pRule->ip[ 1 ] ) == ( pRule->ip[ 0 ] & pRule->ip[ 1 ] ) );
            break;

        case FW_FIELD_RANGE:
            r = ( pRule->ip[ 0 ] <= addr ) && ( addr <= pRule->ip[ 1 ] );
            break;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchIpAddressRule( const FwRule* pRule, Uint32 localAddr, Uint32 remoteAddr )
{
    Bool r = FALSE;

    switch ( pRule->ipAType )
    {
        case FW_ADDR_ANY:
            r = MatchIpAddressSingle( pRule, localAddr ) ||
                MatchIpAddressSingle( pRule, remoteAddr );
            break;

        case FW_ADDR_LOCAL:
            r = MatchIpAddressSingle( pRule, localAddr );
            break;

        case FW_ADDR_REMOTE:
            r = MatchIpAddressSingle( pRule, remoteAddr );
            break;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchPortSingle( const FwRule* pRule, Uint16 port )
{
    Bool r = FALSE;

    switch ( pRule->portFType )
    {
        case FW_FIELD_SINGLE:
            r = ( port == pRule->port[ 0 ] );
            break;

        case FW_FIELD_MASK:
            // no mask for port
            break;

        case FW_FIELD_RANGE:
            r = ( pRule->port[ 0 ] <= port ) && ( port <= pRule->port[ 1 ] );
            break;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchPortRule( const FwRule* pRule, Uint16 localPort, Uint16 remotePort )
{
    Bool r = FALSE;

    switch ( pRule->portAType )
    {
        case FW_ADDR_ANY:
            r = MatchPortSingle( pRule, localPort ) || MatchPortSingle( pRule, remotePort );
            break;

        case FW_ADDR_LOCAL:
            r = MatchPortSingle( pRule, localPort );
            break;

        case FW_ADDR_REMOTE:
            r = MatchPortSingle( pRule, remotePort );
            break;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchSingleRule( const FwRule* pRule, const CallParams* pParams )
{
    Bool r = FALSE;
    if ( pParams->proto != pRule->protocol )
        goto out;

    if ( ( pRule->direction == FW_DIR_ANY ) || ( pRule->direction == pParams->dir ) )
        r = MatchIpAddressRule( pRule, pParams->localAddr, pParams->remoteAddr ) &&
            MatchPortRule( pRule, pParams->localPort, pParams->remotePort );
out:

    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool MatchRules( const CallParams* pParams )
{
    Bool r = FALSE;
    FRule* pRule = NULL;

    read_lock( &s_lockRules );
    list_for_each_entry( pRule, &s_fwRuleList, node )
    {
        if ( MatchSingleRule( &pRule->rule, pParams ) )
        {
            PrintMatchedRule( pParams, &pRule->rule );
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
SF_STATUS AddFirewallRule( const FwRule* pRule )
{
    FRule* pNewRule = NULL;
    if ( !pRule )
        return SF_STATUS_BAD_ARG;

    pNewRule = sf_malloc( sizeof(FRule) );
    if ( !pNewRule )
    {
        SF_LOG_E( "%s(): failed to allocate firewall rule", __FUNCTION__ );
        return SF_STATUS_FAIL;
    }

    // FwRule is POD struct
    pNewRule->rule = *pRule;
    write_lock( &s_lockRules );
    list_add_tail( &pNewRule->node, &s_fwRuleList );
    write_unlock( &s_lockRules );
    SF_LOG_I( "%s(): added new firewall rule", __FUNCTION__ );
    return SF_STATUS_OK;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static void GetParametersFromTcpSocket( const struct socket* pSocket, const struct msghdr* pMsg,
                                        CallParams* pParams )
{
    const struct inet_sock* pInetSock = inet_sk( pSocket->sk );
    pParams->localAddr  = pInetSock->inet_rcv_saddr;
    pParams->remoteAddr = pInetSock->inet_daddr;
    pParams->localPort  = pInetSock->inet_sport;
    pParams->remotePort = pInetSock->inet_dport;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static void GetParametersFromUdpSocket( const struct socket* pSocket, const struct msghdr* pMsg,
                                        CallParams* pParams )
{
    const struct inet_sock* pInetSock = inet_sk( pSocket->sk );
    pParams->localAddr = pInetSock->inet_rcv_saddr;
    pParams->localPort = pInetSock->inet_sport;

    if ( pMsg->msg_name )
    {
        struct sockaddr_in* pInAddr = (struct sockaddr_in*)( pMsg->msg_name );
        pParams->remoteAddr = pInAddr->sin_addr.s_addr;
        pParams->remotePort = pInAddr->sin_port;
    }
    else
    {
        pParams->remoteAddr = pInetSock->inet_daddr;
        pParams->remotePort = pInetSock->inet_dport;
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool GetParametersToCheck( const struct socket* pSocket, const struct msghdr* pMsg,
                                  CallParams* pParams )
{
    Bool r = TRUE;
    if ( pSocket->sk->sk_family != PF_INET )
        return FALSE;

    if ( pSocket->sk->sk_protocol == IPPROTO_TCP )
    {
        pParams->proto = FW_TCP;
        GetParametersFromTcpSocket( pSocket, pMsg, pParams );
    }
    else if ( pSocket->sk->sk_protocol == IPPROTO_UDP )
    {
        pParams->proto = FW_UDP;
        GetParametersFromUdpSocket( pSocket, pMsg, pParams );
    }
    else
        r = FALSE;
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Bool CreateSendRcvEnvironment( SfPacket* pPacket, const CallParams* pParams )
{
    SfSndRcvEnvironment* pEnv = NULL;
    SfProtocolHeader* pHeader = SF_CREATE_ENVIRONMENT( SfSndRcvEnvironment,
                                                       SF_ENVIRONMENT_TYPE_SND_RCV );
    if ( !pHeader )
    {
        SF_LOG_E( "%s(): failed to allocate sendmsg()/recvmsg() environment", __FUNCTION__ );
        return FALSE;
    }

    pEnv = (SfSndRcvEnvironment*)pHeader;
    if ( SF_FAILED( SfdFillExecutionEnvironment( &pEnv->processContext, current, 0, 1 ) ) )
    {
        SfDestroySndRcvEnvironment( pEnv );
        return FALSE;
    }
    pEnv->protocol = pParams->proto;
    if ( pParams->dir == FW_DIR_OUTBOUND )
    {
        pEnv->type     = CALL_SENDMSG;
        pEnv->srcAddr  = pParams->localAddr;
        pEnv->srcPort  = pParams->localPort;
        pEnv->destAddr = pParams->remoteAddr;
        pEnv->destPort = pParams->remotePort;
    }
    else
    {
        pEnv->type     = CALL_RECVMSG;
        pEnv->srcAddr  = pParams->remoteAddr;
        pEnv->srcPort  = pParams->remotePort;
        pEnv->destAddr = pParams->localAddr;
        pEnv->destPort = pParams->localPort;
    }
    pPacket->env = pHeader;
    return TRUE;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS CheckSendmsgCall( SfPacket* pPacket )
{
    SfOperationSocketSendmsg* pOp = NULL;
    CallParams params = { .dir = FW_DIR_OUTBOUND };
    SF_STATUS r = SF_STATUS_OK;

    if ( !pPacket || !pPacket->op || pPacket->op->type != SF_OPERATION_TYPE_SENDMSG )
        return SF_STATUS_OK;

    pOp = (SfOperationSocketSendmsg*)( pPacket->op );
    if ( !GetParametersToCheck( pOp->pSocket, pOp->pMsg, &params ) )
        return SF_STATUS_OK;

    if ( MatchRules( &params ) )
    {
        CreateSendRcvEnvironment( pPacket, &params );
        r = SF_STATUS_RESOURCE_BLOCK;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS CheckRecvmsgCall( SfPacket* pPacket )
{
    SfOperationSocketRecvmsg* pOp = NULL;
    CallParams params = { .dir = FW_DIR_INBOUND };
    SF_STATUS r = SF_STATUS_OK;

    if ( !pPacket || !pPacket->op || pPacket->op->type != SF_OPERATION_TYPE_RECVMSG )
        return SF_STATUS_OK;

    pOp = (SfOperationSocketRecvmsg*)( pPacket->op );
    if ( !GetParametersToCheck( pOp->pSocket, pOp->pMsg, &params ) )
        return SF_STATUS_OK;

    if ( MatchRules( &params ) )
    {
        CreateSendRcvEnvironment( pPacket, &params );
        r = SF_STATUS_RESOURCE_BLOCK;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void ClearFirewallRulesList( void )
{
    FRule *p = NULL, *n = NULL;

    write_lock( &s_lockRules );
    list_for_each_entry_safe( p, n, &s_fwRuleList, node )
    {
        list_del( &p->node );
        sf_free( p );
    }
    write_unlock( &s_lockRules );

    SF_LOG_I( "%s(): All of rules are deleted", __FUNCTION__ );
}