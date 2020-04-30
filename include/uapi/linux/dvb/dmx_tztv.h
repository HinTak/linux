/*
 * dmx_tztv.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _UAPI_DVBDMX_TZTV_H_
#define _UAPI_DVBDMX_TZTV_H_
#if 1 /* Todo : enable later */
#include <linux/dvb/dmx.h>

#define DMX_BIG_SECTION_BUFFER_SIZE 	(128*1024)
#define DMX_SMALL_SECTION_BUFFER_SIZE	(8*1024)

typedef enum {
	DMX_REC_MODE_DIGITAL,
	DMX_REC_MODE_ANALOG,
}record_mode_t;


typedef enum dmx_ca_type
{
	DMX_CA_BYPASS = 0,
	DMX_CA_DES_ECB,
	DMX_CA_3DES_CBC,
	DMX_CA_3DES_ECB,
	DMX_CA_AES_ECB,
	DMX_CA_AES_CBC,
	DMX_CA_MULTI2,
	DMX_CA_DVB_CSA,
	DMX_CA_AES_CTR,
	DMX_CA_AES_CBC_XOR,
	DMX_CA_AES_CTR_XOR,
} dmx_ca_type_t;

typedef enum {
	DMX_OUTPUT_NONE,
	DMX_OUTPUT_EXT1,
	DMX_OUTPUT_EXT2,
	DMX_OUTPUT_EXT3,
	DMX_OUTPUT_EXT4,
	DMX_OUTPUT_EXT5,
	DMX_OUTPUT_PVR1,
	DMX_OUTPUT_PVR2,
} dmx_redirect_t;

enum dmx_status_cmd {
	DEMUX_STATUS_TSDLOCK
};

struct dmx_status {
	__u32 cmd;
	__u32 data;
};

#define DRY_CRYPT_KEY_LEN 16
typedef struct dvr_key_info
{
	__u8  key_table[DRY_CRYPT_KEY_LEN];    // < [in] key value (in little endian and in the order of first key,second key, etc.)
	__u32 key_len;    // < [in] Length of the key (in number of bytes)
	__u8* iv;     // < [in] IV (Initial Vector) value used only in CBC mode
	__u32 iv_len;     // < [in] IV length used only in CBC mode
	__u8* modulo;///< [in] Pointer of 256bytes Modulo values. used only in RSA mode
	__u32 modulo_len; ///< [in] Length of 256bytes Modulo values. used only in RSA mode
}dvr_key_info_t;

typedef struct dvr_key
{
	__s32 odd_key; /* 1: odd, 0: even */
	dvr_key_info_t key;
}dvr_scr_t;


typedef enum {
	DVR_PIC_TYPE_I, ///< I Picture
	DVR_PIC_TYPE_B, ///< B Picture
	DVR_PIC_TYPE_P, ///< P Picture
	DVR_SEQ_HDR, ///< Sequence Header
	DVR_PIC_TYPE_NULL,
} dvr_pic_type_t;


typedef struct
{
	dvr_pic_type_t pic_type;  
	__u64 pkg_count;          
	__u32 time;            
	__u64 pts;   //added for PVR_PTS           
} dvr_index_info_t;

typedef enum {
	DVR_VIDEO_FAMAT_MPEG,
	DVR_VIDEO_FAMAT_H264,
	DVR_VIDEO_FAMAT_AVS,
	DVR_VIDEO_FAMAT_HEVC,
	DVR_VIDEO_FAMAT_NONE, /* audio only channel */
} dvr_video_format_t;

typedef struct {
	dvr_video_format_t video_format;
	__u32 interval; /* time in ms between index events for audio only channel. */
} dvr_index_config_t;

/* Parameter for DVR_SET_DATA_TYPE */
#define DVR_DATA_TYPE_NORMAL        0
#define DVR_DATA_TYPE_SCRAMBLED     1


typedef struct dvr_hdr {
	__u64 pkg_count;
	__u64 pts;
	__u32 picture_type;
#define DVR_PIC_TYPE_I		0
#define DVR_PIC_TYPE_P		1
#define DVR_PIC_TYPE_B		2
#define DVR_SEQ_HDR		3
#define DVR_PIC_TYPE_NULL	4
	__u32 time;
} dvr_hdr_t;

typedef struct dvr_play_buff {
	int level;
	int total;
} dvr_play_buff_t;

typedef struct ca_config {
	int mode;	// 0: disable, 1: bank matching enable, 2: pid matching enable.
	dmx_ca_type_t ca_type;
	__u16 pid;	// in case of pid matching;
	__u8 keyidx;	// in case of pid matching. (0~31)
	__u8 use_hcas;	// 0: disable, 1: enable.
} ca_config_t;

typedef enum dvr_play_state
{
	DVR_PLAY_STATE_STOP,
	DVR_PLAY_STATE_PLAY,
	DVR_PLAY_STATE_PAUSE,
	DVR_PLAY_STATE_RESUME,
	DVR_PLAY_STATE_MAX
} dvr_play_state_t;

#define MAX_DEVICE_NODE_LEN 30

typedef struct dmx_pestype_info
{
	__s8 device_node[MAX_DEVICE_NODE_LEN];
	dmx_pes_type_t pes_type;
}dmx_pestype_info_t;

typedef enum dvr_filetype {
	DVR_FILE_TS,
	DVR_FILE_M2TS,
	DVR_FILE_MAX
} dvr_filetype_t;

typedef struct dvr_play_params {
	dvr_filetype_t filetype;
} dvr_play_params_t;

typedef struct dvr_rec_params {
	__u32 reserved;
} dvr_rec_params_t;


#define DVR_SET_DESCR            _IOW('o', 53, dvr_scr_t) /* set dvr decrypt key for playback scrambled data */
#define DVR_SET_SCR              _IOW('o', 54, dvr_scr_t) /* set dvr encrypt key */
#define DVR_IDX_SET_BUFFER_SIZE  _IO('o', 55) /* set dvr index data buffer size */
#define DMX_GET_STATUS           _IOR('o', 57, struct dmx_status)
#define DMX_SET_RECORD_MODE      _IO('o', 58)
#define DMX_GET_PESTYPE          _IOWR('o', 59, dmx_pestype_info_t) /* get PES TYPE info */
#define DMX_SET_STC              _IOW('o', 60, struct dmx_stc)

/* IOCTL numbers From 80 are private */
#define DMX_IOCTL_BASE		96
#define SDP_SET_TSD_SOURCE		_IOW('o', 97, dmx_source_t)
#define SDP_SET_TSD_REDIRECT	_IOW('o', 98, dmx_redirect_t)
#define DVR_IDX_SET_CONFIG		_IOWR('o', 99, dvr_index_config_t) /* set dvr recording config. */
#define DVR_GET_PLAY_BUFFER     _IOWR('o', 100, dvr_play_buff_t) /* get pvr play buffer level */

#define SDP_SET_CA_CTRL         _IOW('o', 102, ca_config_t)		/* set descrambler on/off, ca_type */
#define DVR_SET_PLAY_STATE      _IOW('o', 103, dvr_play_state_t)  /* set PVR play state */
#define	DVR_SET_PLAY_PARAMS	_IOW('o', 104, dvr_play_params_t) /* set PVR play parameters */
#define	DVR_SET_REC_PARAMS      _IOW('o', 105, dvr_rec_params_t) /* set PVR rec parameters */

#define DMX_IOCTL_END		127
#endif
#endif
