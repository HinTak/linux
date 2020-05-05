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
#ifndef _MACH_SOC_H_
#define _MACH_SOC_H_

enum sdp_board	{
	SDP_BOARD_DEFAULT,
	SDP_BOARD_MAINTV,
	SDP_BOARD_JACKPACKTV,
	SDP_BOARD_LFD,
	SDP_BOARD_SBB,
	SDP_BOARD_HCN,
	SDP_BOARD_VGW,
	SDP_BOARD_MAX
};

extern enum sdp_board get_sdp_board_type(void);

#if defined(CONFIG_OF)
unsigned int sdp_rev(void);
int sdp_get_revision_id(void);

#define SDP1202_CHIPID	0x9
#define SDP1302_CHIPID	0x2
#define SDP1304_CHIPID	0x4
#define SDP1307_CHIPID	0x7

#define GET_SDP_REV()	((sdp_rev() >> 10) & 0x3)
#define GET_SDP_SOC()	((sdp_rev() >> 12) & 0xF)

/* software-defined */
#define GET_SDP_SUB()		(sdp_rev() & 0xf)
#define SDP1302MPW_SUBID	(1)

#define IS_SDP_SOC(class, id)			\
static inline int is_##class (void)		\
{					\
	return ((GET_SDP_SOC() == id) ? 1 : 0);	\
}

IS_SDP_SOC(sdp1202, SDP1202_CHIPID)		//Fox-AP
IS_SDP_SOC(sdp1304, SDP1304_CHIPID)		//Golf-AP
IS_SDP_SOC(sdp1302, SDP1302_CHIPID)		//Golf-S
IS_SDP_SOC(sdp1307, SDP1307_CHIPID)		//Golf-V

#define soc_is_sdp1202()	is_sdp1202()
#define soc_is_sdp1302()	is_sdp1302()
#define soc_is_sdp1304()	is_sdp1304()
#define soc_is_sdp1307()	is_sdp1307()

#define soc_is_sdp1302mpw()	(is_sdp1302() && (GET_SDP_SUB() == SDP1302MPW_SUBID))

#else

#if defined(CONFIG_ARCH_SDP1202)
#define soc_is_sdp1202()	(1)
#else
#define soc_is_sdp1202()	(0)
#endif

#if defined(CONFIG_ARCH_SDP1207)
#define soc_is_sdp1207()	(1)
#else
#define soc_is_sdp1207()	(0)
#endif

#define soc_is_sdp1302()	(0)
#define soc_is_sdp1304()	(0)
#define soc_is_sdp1307()	(0)

#endif
#endif
