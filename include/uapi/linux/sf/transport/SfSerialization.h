
#ifndef _SF_SERIALIZATION_H_
#define _SF_SERIALIZATION_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#include <uapi/linux/sf/transport/netlink/SfNetlinkSerialization.h>
#include <uapi/linux/sf/protocol/SfPacket.h>

/**
****************************************************************************************************
* @brief                    Serialize Security Framework Packet into netlink message
* @param [in] pPacket       Pointer to Security Framework packet
* @param [in] nDataSize     Data size that will be loaded to netlink packet
* @warning                  This function allocate memory, and it should be freed
* @return                   SfNetlinkPacket on success, NULL otherwise
****************************************************************************************************
*/
SfNetlinkPacket* SfSerializePacket( const SfPacket* pPacket, int nDataSize );

/**
****************************************************************************************************
* @brief                    Deserialize SF packet from Netlink message
* @param [in] pNPacket      Netlink message
* @warning                  This function allocate memory, and it should be freed
* @return                   SfPacket on success, NULL otherwise
****************************************************************************************************
*/
SfPacket* SfDeserializePacket( const SfNetlinkPacket* pNPacket );

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif  /* !_SF_SERIALIZATION_H_ */
