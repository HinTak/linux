/**
****************************************************************************************************
* @file SfFirewallRulesList.h
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
#include <uapi/linux/sf/protocol/SfPacket.h>

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS AddFirewallRule( const FwRule* pRule );

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS CheckSendmsgCall( SfPacket* pPacket );

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS CheckRecvmsgCall( SfPacket* pPacket );

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void ClearFirewallRulesList( void );