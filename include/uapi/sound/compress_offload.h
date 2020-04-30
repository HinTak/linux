/*
 *  compress_offload.h - compress offload header definations
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@linux.intel.com>
 *		Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */
#ifndef __COMPRESS_OFFLOAD_H
#define __COMPRESS_OFFLOAD_H

#include <linux/types.h>
#include <sound/asound.h>
#include <sound/compress_params.h>


#define SNDRV_COMPRESS_VERSION SNDRV_PROTOCOL_VERSION(0, 1, 2)
/**
 * struct snd_compressed_buffer - compressed buffer
 * @fragment_size: size of buffer fragment in bytes
 * @fragments: number of such fragments
 */
struct snd_compressed_buffer {
	__u32 fragment_size;
	__u32 fragments;
} __attribute__((packed, aligned(4)));

/**
 * struct snd_compr_params - compressed stream params
 * @buffer: buffer description
 * @codec: codec parameters
 * @no_wake_mode: dont wake on fragment elapsed
 */
struct snd_compr_params {
	struct snd_compressed_buffer buffer;
	struct snd_codec codec;
	__u8 no_wake_mode;
} __attribute__((packed, aligned(4)));

/**
 * struct snd_compr_tstamp - timestamp descriptor
 * @byte_offset: Byte offset in ring buffer to DSP
 * @copied_total: Total number of bytes copied from/to ring buffer to/by DSP
 * @pcm_frames: Frames decoded or encoded by DSP. This field will evolve by
 *	large steps and should only be used to monitor encoding/decoding
 *	progress. It shall not be used for timing estimates.
 * @pcm_io_frames: Frames rendered or received by DSP into a mixer or an audio
 * output/input. This field should be used for A/V sync or time estimates.
 * @sampling_rate: sampling rate of audio
 */
struct snd_compr_tstamp {
	__u32 byte_offset;
	__u32 copied_total;
	__u32 pcm_frames;
	__u32 pcm_io_frames;
	__u32 sampling_rate;
} __attribute__((packed, aligned(4)));

/**
 * struct snd_compr_avail - avail descriptor
 * @avail: Number of bytes available in ring buffer for writing/reading
 * @tstamp: timestamp information
 */
struct snd_compr_avail {
	__u64 avail;
	struct snd_compr_tstamp tstamp;
} __attribute__((packed, aligned(4)));

enum snd_compr_direction {
	SND_COMPRESS_PLAYBACK = 0,
	SND_COMPRESS_CAPTURE
};

/**
 * struct snd_compr_caps - caps descriptor
 * @codecs: pointer to array of codecs
 * @direction: direction supported. Of type snd_compr_direction
 * @min_fragment_size: minimum fragment supported by DSP
 * @max_fragment_size: maximum fragment supported by DSP
 * @min_fragments: min fragments supported by DSP
 * @max_fragments: max fragments supported by DSP
 * @num_codecs: number of codecs supported
 * @reserved: reserved field
 */
struct snd_compr_caps {
	__u32 num_codecs;
	__u32 direction;
	__u32 min_fragment_size;
	__u32 max_fragment_size;
	__u32 min_fragments;
	__u32 max_fragments;
	__u32 codecs[MAX_NUM_CODECS];
	__u32 reserved[11];
} __attribute__((packed, aligned(4)));

/**
 * struct snd_compr_codec_caps - query capability of codec
 * @codec: codec for which capability is queried
 * @num_descriptors: number of codec descriptors
 * @descriptor: array of codec capability descriptor
 */
struct snd_compr_codec_caps {
	__u32 codec;
	__u32 num_descriptors;
	struct snd_codec_desc descriptor[MAX_NUM_CODEC_DESCRIPTORS];
} __attribute__((packed, aligned(4)));

/**
 * Compress META COMMAND
 * @SNDRV_COMPRESS_ENCODER_PADDING: no of samples appended by the encoder at the
 * end of the track
 * @SNDRV_COMPRESS_ENCODER_DELAY: no of samples inserted by the encoder at the
 * beginning of the track
 */

/* Tizen 2.4 */
enum {
	SNDRV_COMPRESS_ENCODER_PADDING = 1,
	SNDRV_COMPRESS_ENCODER_DELAY = 2,
	SNDRV_COMPRESS_ENCODER_PTS = 3,		
	SNDRV_COMPRESS_ENCODER_TZ_HANDLE = 4,
	SNDRV_COMPRESS_ENCODER_PHY_ADDR = 5, 
	SNDRV_COMPRESS_RENDERER_OUT_RATE = 6,
	SNDRV_COMPRESS_ENCODER_REMAIN_BUFFER_SIZE = 7,
	SNDRV_COMPRESS_ENCODER_DUAL_MONO = 8,
	SNDRV_COMPRESS_DECODER_OUT_SAMPLE_RATE = 9,
};

/* Decoder Get,Set For Tizen 3.0 */
enum {
	SNDRV_COMPRESS_GET_OUT_SAMPLE_RATE = 0x10000,
	SNDRV_COMPRESS_GET_OUT_INFO,
	SNDRV_COMPRESS_GET_OUT_REMAIN_SIZE,
	SNDRV_COMPRESS_GET_OUT_PTS,
	SNDRV_COMPRESS_GET_TZ_HANDLE,
	SNDRV_COMPRESS_GET_STEREO_MODE,
	SNDRV_COMPRESS_GET_PHY_ADDR, 
	SNDRV_COMPRESS_GET_OUT_PCM_LEVEL,
	SNDRV_COMPRESS_GET_PADDING,
	SNDRV_COMPRESS_GET_DELAY,
	SNDRV_COMPRESS_GET_MID_PCM_PHY_ADDR,
	SNDRV_COMPRESS_GET_MID_PCM_BUF_SIZE,
	SNDRV_COMPRESS_GET_UI_META_DATA,
};

enum {
	SNDRV_COMPRESS_SET_START_PTS = 0x20000,
	SNDRV_COMPRESS_SET_REPEAT_CNT,
	SNDRV_COMPRESS_SET_SKIP_CNT,
	SNDRV_COMPRESS_SET_IN_PTS,
	SNDRV_COMPRESS_SET_TZ_HANDLE,
	SNDRV_COMPRESS_SET_STEREO_MODE,
	SNDRV_COMPRESS_SET_START_RENDERING,
	SNDRV_COMPRESS_SET_AV_SYNC,
	SNDRV_COMPRESS_SET_COMPRESS_MODE,
	SNDRV_COMPRESS_SET_IN_GAIN,
	SNDRV_COMPRESS_SET_PAUSE_PTS,
	SNDRV_COMPRESS_SET_STC,
	SNDRV_COMPRESS_SET_AUDIO_FLUSH,
	SNDRV_COMPRESS_SET_OUT_SPEAKER_INFO,
	SNDRV_COMPRESS_SET_SEAMLESS_INFO,
	SNDRV_COMPRESS_SET_SEAMLESS_INFO_CLEAR,
	SNDRV_COMPRESS_SET_LOCK_STATE,	//0x10
	SNDRV_COMPRESS_SET_PADDING,
	SNDRV_COMPRESS_SET_DELAY,
	SNDRV_COMPRESS_SET_UI_META_DATA,
	SNDRV_COMPRESS_SET_DISCARD_PCM,
	SNDRV_COMPRESS_SET_CHECK_ZERO_ENC,
};

/* For AV Product */
enum {
	SNDRV_COMPRESS_STATE_AV_SYNC = 1000,
	SNDRV_COMPRESS_STATE_COMPRESS_MODE,
	SNDRV_COMPRESS_STATE_STEREO_MODE,
	SNDRV_COMPRESS_STATE_LOCK_STATE,
	SNDRV_COMPRESS_STATE_PAUSE_AT_PTS,
	SNDRV_COMPRESS_STATE_STC,
	SNDRV_COMPRESS_MM_SUBMIT,
	SNDRV_COMPRESS_IN_GAIN,
	SNDRV_COMPRESS_DECODING_OPTION,
	SNDRV_COMPRESS_SEAMLESS,	
	SNDRV_COMPRESS_PES_USING_DEALY,	
	SNDRV_COMPRESS_SEAMLESS_INFO_CLEAR,	
	SNDRV_COMPRESS_INFO_MM_MID,
	SNDRV_COMPRESS_INFO_MM_SIZE,
};


/**
 * struct snd_compr_metadata - compressed stream metadata
 * @key: key id
 * @value: key value
 */
struct snd_compr_metadata {
	 __u32 key;
	 __u32 value[8];
} __attribute__((packed, aligned(4)));

/**
 * compress path ioctl definitions
 * SNDRV_COMPRESS_GET_CAPS: Query capability of DSP
 * SNDRV_COMPRESS_GET_CODEC_CAPS: Query capability of a codec
 * SNDRV_COMPRESS_SET_PARAMS: Set codec and stream parameters
 * Note: only codec params can be changed runtime and stream params cant be
 * SNDRV_COMPRESS_GET_PARAMS: Query codec params
 * SNDRV_COMPRESS_TSTAMP: get the current timestamp value
 * SNDRV_COMPRESS_AVAIL: get the current buffer avail value.
 * This also queries the tstamp properties
 * SNDRV_COMPRESS_PAUSE: Pause the running stream
 * SNDRV_COMPRESS_RESUME: resume a paused stream
 * SNDRV_COMPRESS_START: Start a stream
 * SNDRV_COMPRESS_STOP: stop a running stream, discarding ring buffer content
 * and the buffers currently with DSP
 * SNDRV_COMPRESS_DRAIN: Play till end of buffers and stop after that
 * SNDRV_COMPRESS_IOCTL_VERSION: Query the API version
 */
#define SNDRV_COMPRESS_IOCTL_VERSION	_IOR('C', 0x00, int)
#define SNDRV_COMPRESS_GET_CAPS		_IOWR('C', 0x10, struct snd_compr_caps)
#define SNDRV_COMPRESS_GET_CODEC_CAPS	_IOWR('C', 0x11,\
						struct snd_compr_codec_caps)
#define SNDRV_COMPRESS_SET_PARAMS	_IOW('C', 0x12, struct snd_compr_params)
#define SNDRV_COMPRESS_GET_PARAMS	_IOR('C', 0x13, struct snd_codec)
#define SNDRV_COMPRESS_SET_METADATA	_IOW('C', 0x14,\
						 struct snd_compr_metadata)
#define SNDRV_COMPRESS_GET_METADATA	_IOWR('C', 0x15,\
						 struct snd_compr_metadata)
#define SNDRV_COMPRESS_TSTAMP		_IOR('C', 0x20, struct snd_compr_tstamp)
#define SNDRV_COMPRESS_AVAIL		_IOR('C', 0x21, struct snd_compr_avail)
#define SNDRV_COMPRESS_PAUSE		_IO('C', 0x30)
#define SNDRV_COMPRESS_RESUME		_IO('C', 0x31)
#define SNDRV_COMPRESS_START		_IO('C', 0x32)
#define SNDRV_COMPRESS_STOP		_IO('C', 0x33)
#define SNDRV_COMPRESS_DRAIN		_IO('C', 0x34)
#define SNDRV_COMPRESS_NEXT_TRACK	_IO('C', 0x35)
#define SNDRV_COMPRESS_PARTIAL_DRAIN	_IO('C', 0x36)
/*
 * TODO
 * 1. add mmap support
 *
 */
#define SND_COMPR_TRIGGER_DRAIN 7 /*FIXME move this to pcm.h */
#define SND_COMPR_TRIGGER_NEXT_TRACK 8
#define SND_COMPR_TRIGGER_PARTIAL_DRAIN 9

#define EncoderMpegHeader
typedef struct
{
	unsigned int iSyncWord;	
	unsigned int FrameSize;	
	unsigned int PTS;	
	unsigned int PhyAddress;	
	unsigned int VirAddress;	
	unsigned int Reserved[3];	
}EncoderMpegHeader_t, *pEncoderMpegHeader_t;

enum sdp_compress_av_source_type{
	SDP_AV_FILE_PLAY = 0,
	SDP_AV_BD_PLAY,
	SDP_AV_BDRE_PLAY,
	SDP_AV_DVD_PLAY,
	SDP_AV_ACM_PLAY,
	SDP_AV_AUX_PLAY,
	SDP_AV_DIN_PLAY,	
	SDP_AV_RAW_FILE_PLAY,
	SDP_AV_CDDA_PLAY,
#ifdef CONFIG_BD_CACHE_ENABLED
	SDP_AV_CDRIPPING_PLAY, 
#endif	
	SDP_AV_UNKNOWN_PLAY 
};

#define RESERVE_MAX (100)

typedef struct
{
	unsigned int pcmSize;	//pcm length
	long long pts;			//current pts of pcm
	unsigned int chNum;
	unsigned int chMap;		//ch Map
	unsigned int pcmPtr;	//pcm offset 
	unsigned int reserve[RESERVE_MAX];
}CompressData_t, *pCompressData_t;

#endif
