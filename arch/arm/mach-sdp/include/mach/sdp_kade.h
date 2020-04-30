/*********************************************************************************************
 *
 *	sdp_kade.h (Samsung KADE driver)
 *
 *	author : seungjun.heo@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
// May,07,2015 	seungjun.heo	created
 ********************************************************************************************/

#ifndef __SDP_KADE_H__
#define __SDP_KADE_H__

#define CMD_KADE_GET_SYSERROR		(0x01)
#define CMD_KADE_GET_CHIPID			(0x02)
#define CMD_KADE_SET_ERROR			(0x10)
#define CMD_KADE_GET_CHIPID2			(0x11)



enum sdp_kade_sys_errorno
{
	SDP_KADE_ERR_DUMPSTACK	=	1,
	SDP_KADE_ERR_SPINLOCK	=	2,
	SDP_KADE_ERR_SOFTLOCKUP	=	4,
	SDP_KADE_ERR_MAX		=	7,
};

extern void sdp_kade_set_system_error(unsigned int error);

extern unsigned int sdp_kade_get_system_error(void);

#endif /* __SDP_KADE_H__ */

