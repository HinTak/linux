#ifndef __DEMUX_NS__
#define __DEMUX_NS__

#define true 1
#define false 0

typedef enum {
	GP_TYPE_IPV4,
	GP_TYPE_IPV6,
	GP_TYPE_COMPRESSED,
	GP_TYPE_MPEG2TS,
	GP_TYPE_L2SIGNAL,
	GP_TYPE_MAX,
} gp_type_e;

/**
* @brief Called to get gathered packets in the payload of Generic Packet.
*
* @param[in] demux_id Which demux gathers this
* @param[in] plp_nr The PLP indication number
* @param[in] type The type of payload
* @param[in] buff The buffer address of payload which has been gathered
* from PUSI start to end
* @param[in] len The buffer size
*
* @return @c true to continue with the next iteration of the loop, \n
*   otherwise @c false to break out of the loop
*
* @pre demux_set_net_streamer_cb() installed this callback function.
*/
typedef bool (*net_streamer_cb)(int demux_id, int plp_nr, gp_type_e type,
				const unsigned char *buff, unsigned int len);

/**
* @brief Retrieves the number of gathered packets.
* @details This function invokes the callback function whenever the number of packets are gathered. \n
*    If net_streamer_cb() returns @c false, then the iteration will be finished.
*
* @param[in] demux_id Which demux gathers packets
* @param[in] callback The iteration callback function
*
* @return @c 0 on success,
*   otherwise a negative error value
*
* @retval #DEMUX_ERROR_NONE Successful
* @retval #DEMUX_ERROR_INVALID_DEMUXID Invalid demux_id
* @retval #DEMUX_ERROR_INVALID_CALLBACK Invalid callback
* @retval #DEMUX_ERROR_INVALID_BUFF Invalid buffer address
* @retval #DEMUX_ERROR_INVALID_LEN Invalid buffer size
* @retval #DEMUX_ERROR_SMALL_BUF_LEN Too small to gather payloads properly
* @retval #DEMUX_ERROR_BIG_NR_PACKETS Too big amount of packets for gathering packets properly
*
* @post This function invokes net_streamer_cb() repeatedly whenever the number of packets are gathered.
* @see net_streamer_cb()
*/
int demux_set_net_streamer_cb(int demux_id, net_streamer_cb callback);

/**
* @brief Stop retrieving.
* @details This function stops invoking the callback function. \n
*
* @param[in] demux_id Which demux gathers packets
* @param[in] callback The iteration callback function
* @param[in] sync_b If this is true, the callee must wait for completion of
* the final PUSI gathering before returning.
*
* @return @c 0 on success,
*   otherwise a negative error value
*
* @see net_streamer_cb()
*/
int demux_unset_net_streamer_cb(int demux_id,
				net_streamer_cb callback, int sync_b);

#endif
