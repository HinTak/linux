/*
 * ca_tztv.h
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

#ifndef _DVBCA_TZTV_H_
#define _DVBCA_TZTV_H_
#if 1 /* Todo : enable later */
#include <linux/dvb/ca.h>

typedef struct ca_reset
{
	unsigned int  slot;
} ca_reset_t;

typedef struct ca_get_attr_data {
	__u32  slot;
	__u8   cmd_id;        /*input  CISTPL_ID*/
	__u8   buf[257];	  /*output CISTPL data buffer*/
	__u32  buf_len;		  /*output CISTPL data lenth*/
}ca_get_attr_data_t;


typedef enum ca_set_frontend
{
	CA_SOURCE_FRONTEND0,		// for original CI/CI+ 1.3. main tuner source
	CA_SOURCE_FRONTEND1,		// for original CI/CI+ 1.3. sub tuner source
	CA_SOURCE_MSPU_FRONTEND0,	// for new CI+1.4 mspu, main tuner source
	CA_SOURCE_MSPU_FRONTEND1,	// for new CI+1.4 mspu, sub tuner source
	CA_SOURCE_MSPU_DVR0,		// for new CI+1.4 mspu, dvr(ddr ts) source 0
	CA_SOURCE_MSPU_DVR1,		// for new CI+1.4 mspu, dvr(ddr ts) source 1
}ca_set_frontend_t;

typedef struct ca_set_source {
	__u32  slot;
	ca_set_frontend_t source;
}ca_set_source_t;


typedef enum ca_set_route_mode{
	CA_TS_ROUTE_MODE_DISCONNECT = 0,
	CA_TS_ROUTE_MODE_BYPASS,
	CA_TS_ROUTE_MODE_THROUGH,
}ca_set_route_mode_t;

typedef struct ca_set_ts_route_mode {
	__u32  slot;
	ca_set_route_mode_t mode;
}ca_set_ts_route_mode_t;


typedef struct ca_hcas_info {
	unsigned int flags;
	//#define CA_CI_MODULE_PRESENT 1 /* module (or card) inserted */
	//#define CA_CI_MODULE_READY   2
} ca_hcas_info_t;

typedef struct ca_set_hcas {
	__u16  pid;
	__u16 streamid;
	ca_set_frontend_t source;
}ca_set_hcas_t;

/* new structure for MSPU setting */
typedef struct ca_set_mspu {
	__u16 pid;
	__u16 lts_id;
	ca_set_frontend_t source;
}ca_set_mspu_t;

#define CISTPL_DEVICE			0x01
#define CISTPL_DEVICE_A			0x17
#define CISTPL_DEVICE_0A		0x1d
#define CISTPL_DEVICE_0C		0x1c
#define CISTPL_VERS_1			0x15
#define CISTPL_MANFID			0x20
#define CISTPL_CONFIG			0x1a
#define CISTPL_CFTABLE_ENTRY		0x1b
#define CISTPL_NOLINK			0x14
#define CISTPL_END			0xff

#define CA_RESET_V2		_IOW('o', 139, ca_reset_t)
#define CA_GET_ATTR_DATA	_IOR('o', 140, ca_get_attr_data_t)
#define CA_SET_SOURCE		_IOR('o', 141, ca_set_source_t)
#define CA_SET_TS_ROUTE_MODE	_IOR('o', 142, ca_set_ts_route_mode_t)

#define CA_RESET_SLOT		_IOW('o', 143, ca_reset_t)
#define CA_GET_TS_ROUTE_MODE	_IOR('o', 144, ca_set_ts_route_mode_t)

#define HCAS_SET_STREAMID	_IOW('o', 145, ca_set_hcas_t)
#define HCAS_CLEAR_STREAMID	_IOW('o', 146, ca_set_hcas_t)
#define HCAS_CLEAR_ALL		_IOW('o', 147, ca_set_frontend_t)
#define HCAS_START		_IOW('o', 148, ca_set_frontend_t)
#define HCAS_STOP		_IOW('o', 149, ca_set_frontend_t)
#define HCAS_GET_SLOT_INFO	_IOR('o', 150, ca_hcas_info_t)
#define HCAS_RESET		_IO('o', 150)

/* set lts_id, source, pid. API.  
 User should call set_Its_is ioctl for All PIDs which uses MSPU individually.*/
#define MSPU_SET_LTS_ID  _IOW('o', 151, ca_set_mspu_t)

/* clear lts_id, source, pid. */
#define MSPU_CLEAR_LTS_ID _IOW('o', 152, ca_set_mspu_t)

/* clear all pids MSPU setting from tuner source */
#define MSPU_CLEAR_ALL  _IOW('o', 153, ca_set_frontend_t)

/* start mspu from tuner source */
#define MSPU_START _IOW('o', 154, ca_set_frontend_t)

/* stop mspu from tuner source */
#define MSPU_STOP _IOW('o', 155, ca_set_frontend_t)
 
/* start mspu from lts id */
#define MSPU_START_LTSID _IOW('o', 156, __u16)

/* stop mspu from lts id */
#define MSPU_STOP_LTSID _IOW('o', 157, __u16)

/* clear all pids MSPU setting from lts id */
#define MSPU_CLEAR_ALL_LTSID  _IOW('o', 158, __u16)


#endif
#endif
