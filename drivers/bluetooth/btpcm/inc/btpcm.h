/*
 *
 * btpcm.h
 *
 *
 *
 * Copyright (C) 2013 Broadcom Corporation.
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation (the "GPL"), and may
 * be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL for more details.
 *
 *
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php
 * or by writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
 *
 *
 */

#ifndef BTPCM_H
#define BTPCM_H

#ifndef TRUE
#define TRUE (1==1)
#endif

/*
 * Definitions
 */
/* Number of PCM streams */
#ifndef BTPCM_NB_STREAM_MAX
#define BTPCM_NB_STREAM_MAX 2
#endif


#define BTPCM_DBGFLAGS_DEBUG    0x01
#define BTPCM_DBGFLAGS_INFO     0x02
#define BTPCM_DBGFLAGS_WRN      0x04
//#define BTPCM_DBGFLAGS (BTPCM_DBGFLAGS_DEBUG | BTPCM_DBGFLAGS_INFO | BTPCM_DBGFLAGS_WRN)
#define BTPCM_DBGFLAGS (BTPCM_DBGFLAGS_INFO | BTPCM_DBGFLAGS_WRN)

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_DEBUG)
#define BTPCM_DBG(fmt, ...) \
    printk(KERN_DEBUG "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_DBG(fmt, ...)
#endif

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_INFO)
#define BTPCM_INFO(fmt, ...) \
    printk(KERN_INFO "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_INFO(fmt, ...)
#endif

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_WRN)
#define BTPCM_WRN(fmt, ...) \
    printk(KERN_WARNING "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_WRN(fmt, ...)
#endif

#define BTPCM_ERR(fmt, ...) \
    printk(KERN_ERR "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)


#define BTPCM_SAMPLE_8BIT_SIZE      (sizeof(char))
#define BTPCM_SAMPLE_16BIT_SIZE     (sizeof(short))

#define BTPCM_SAMPLE_MONO_SIZE      1
#define BTPCM_SAMPLE_STEREO_SIZE    2


/* BTPCM Operation */
struct btpcm_op
{
    int (*init) (void);
    void (*exit) (void);
    int (*open) (int pcm_stream);
    int (*close) (int pcm_stream);
    int (*config) (int pcm_stream, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
            void (*callback) (int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames));
    int (*start) (int pcm_stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization);
    int (*stop) (int pcm_stream);
    int (*set_shm_addr) (unsigned int base_addr, unsigned int access_point_addr, unsigned int buff_size,
            unsigned int frame_size_addr, unsigned int enable_m_add );
    void (*synchronization) (int pcm_stream);
};
#endif /* BTPCM_H */
