/*
*
* btpcm_api.h
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

#ifndef BTPCM_API_H
#define BTPCM_API_H

/*******************************************************************************
 **
 ** Function         btpcm_open
 **
 ** Description      BTPCM Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_open(int pcm_stream);

/*******************************************************************************
 **
 ** Function         btpcm_close
 **
 ** Description      BTPCM Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_close(int pcm_stream);

/*******************************************************************************
 **
 ** Function         btpcm_config
 **
 ** Description      BTPCM Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_config(int pcm_stream, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
        void (*callback) (int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames));

/*******************************************************************************
 **
 ** Function         btpcm_start
 **
 ** Description      BTPCM Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_start(int pcm_stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization);

/*******************************************************************************
 **
 ** Function         btpcm_stop
 **
 ** Description      BTPCM Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_stop(int pcm_stream);

/*******************************************************************************
 **
 ** Function        btpcm_synchonization
 **
 ** Description     BTPCM Stream Synchonization function.
 **                 This function is called (for Broadcast AV channels) every time
 **                 a Broadcast Synchronization VSE is received (every 20ms).
 **                 The BTPCM can use this event to as timing reference to either
 **                 call the PCM callback or to perform PCM rate adaptation (to
 **                 compensate the clock drift between the Host and Controller).
 **                 Note that this function is subject to jitter (up to 20 ms)
 **
 ** Returns         void
 **
 *******************************************************************************/
void btpcm_synchonization(int pcm_stream);

/*******************************************************************************
 **
 ** Function         btpcm_set_shm_addr
 **
 ** Description      BTPCM function to receive PCM buffer addresses from
 **		     ALSA Lib.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_set_shm_addr(unsigned int base_addr, unsigned int access_point_addr, unsigned int buff_size, unsigned int frame_size_addr, unsigned enable_m_addr);

#endif

