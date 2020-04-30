/*
 * SDP cpu type detection
 *
 * Copyright (C) 2013 Samsung Electronics
 *
 * Written by SeungJun Heo <seungjun.heo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef _SOC_SDP_H_
#define _SOC_SDP_H_

#include <linux/compiler.h>

enum sdp_board	{
	SDP_BOARD_DEFAULT,
	SDP_BOARD_MAIN,
	SDP_BOARD_JACKPACK,
	SDP_BOARD_LFD,
	SDP_BOARD_SBB,
	SDP_BOARD_HCN,
	SDP_BOARD_VGW,
	SDP_BOARD_FPGA,
	SDP_BOARD_AV,
	SDP_BOARD_MTV,
	SDP_BOARD_OCM,
	SDP_BOARD_ATSC30,
	SDP_BOARD_HTV,
	SDP_BOARD_EBD,
	SDP_BOARD_OC,
	SDP_BOARD_WALL,
	SDP_BOARD_MAX
};

enum sdp_board get_sdp_board_type(void);
int sdp_get_revision_id(void);

unsigned int __deprecated sdp_get_mem_cfg(int nType);
unsigned int __deprecated sdp_rev(void);
unsigned int __deprecated sdp_soc(void);

#if defined(CONFIG_OF)
enum sdp_chipid {
	NON_CHIPID = 0,
	SDP1404_CHIPID,
	SDP1406FHD_CHIPID,
	SDP1406UHD_CHIPID,	
	SDP1106_CHIPID,
	SDP1412_CHIPID,
	SDP1501_CHIPID,/*Jazz-Mu*/
	SDP1511_CHIPID,/*Jazz-Mt*/
	SDP1521_CHIPID,/*Jazz-ML*/
	SDP1531_CHIPID,/*Jazz-L*/
	SDP1601_CHIPID,/*Kant-M*/
	SDP1701_CHIPID,/*Kant-M2*/
	SDP1803_CHIPID,/*Muse-M*/
	SDP1804_CHIPID,/*Muse-L*/
	MAX_SDP_CHIPID
};

/* software-defined */
#define IS_SDP_SOC(class, id)			\
static inline int is_##class (void)		\
{					\
	return ((sdp_soc() == (id)) ? 1 : 0);	\
}

IS_SDP_SOC(sdp1404, SDP1404_CHIPID)		//Hawk-P
//IS_SDP_SOC(sdp1406, SDP1406_CHIPID)		//Hawk-M
IS_SDP_SOC(sdp1406fhd, SDP1406FHD_CHIPID)		//Hawk-M FHD
IS_SDP_SOC(sdp1406uhd, SDP1406UHD_CHIPID)		//Hawk-M UHD
IS_SDP_SOC(sdp1412, SDP1412_CHIPID) 	//Hawk-A

IS_SDP_SOC(sdp1501, SDP1501_CHIPID)/*Jazz-Mu*/
IS_SDP_SOC(sdp1511, SDP1511_CHIPID)/*Jazz-Mt*/
IS_SDP_SOC(sdp1521, SDP1521_CHIPID)/*Jazz-ML*/
IS_SDP_SOC(sdp1531, SDP1531_CHIPID)/*Jazz-L*/

IS_SDP_SOC(sdp1601, SDP1601_CHIPID)/*Kant-M*/
IS_SDP_SOC(sdp1701, SDP1701_CHIPID)/*Kant-M*/
IS_SDP_SOC(sdp1803, SDP1803_CHIPID)/*Muse-M*/
IS_SDP_SOC(sdp1804, SDP1804_CHIPID)/*Muse-L*/

#define soc_is_sdp1404()	is_sdp1404()
#define soc_is_sdp1406fhd()	is_sdp1406fhd()
#define soc_is_sdp1406uhd()	is_sdp1406uhd()
#define soc_is_sdp1412()	is_sdp1412()
//#define soc_is_sdp1406()	is_sdp1406()
static inline int soc_is_sdp1406(void)
{
	return (((sdp_soc() == (SDP1406FHD_CHIPID)) || (sdp_soc() == (SDP1406UHD_CHIPID))) ? 1 : 0);
}
static inline int soc_is_jazz(void)
{
	return is_sdp1501() || is_sdp1511() || is_sdp1521() || is_sdp1531();
}
static inline int soc_is_sdp1501(void)
{
	return is_sdp1501() || is_sdp1511();
}
static inline int soc_is_jazzl(void)
{
	return is_sdp1521() || is_sdp1531();
}
static inline int soc_is_jazzm(void)
{
	return is_sdp1501() || is_sdp1511();
}
static inline int soc_is_sdp1601(void)
{
	return is_sdp1601() || is_sdp1701();
}
static inline int soc_is_muse(void)
{
	return is_sdp1803() || is_sdp1804();
}

#define soc_is_sdp1511()	is_sdp1511()
#define soc_is_sdp1521()	is_sdp1521()
#define soc_is_sdp1531()	is_sdp1531()
#define soc_is_sdp1701()	is_sdp1701()
#define soc_is_sdp1803()	is_sdp1803()
#define soc_is_sdp1804()	is_sdp1804()

#define soc_is_sdp1202()	0
#define soc_is_sdp1304()	0
#define soc_is_sdp1302()	0

#endif	/* CONFIG_OF */
#endif

