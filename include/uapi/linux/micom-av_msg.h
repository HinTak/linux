/*
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * Copyright (c) 2013 Samsung R&D Institute India-Delhi.
 * Author: Abhishek Jaiswal <abhishek1.j@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

/* internal Release1 */

#ifndef __MICOM_AV_MSG_H
#define __MICOM_AV_MSG_H

#include<asm-generic/ioctl.h>
#include <linux/micom-msg.h>

#define KEY_AV_PACKET_PARAM_SIZE	20
/* cmd data[3] and firmware data[128]*/
#define KEY_AV_ISP_PACKET_PARAM_SIZE	131

struct sdp_micom_usr_av_msg {
	char ack;
	int input_param_size;
	char input_param[KEY_AV_PACKET_PARAM_SIZE];
	char output_param[KEY_PACKET_PARAM_SIZE];
};

struct sdp_micom_usr_av_isp {
	enum block_cmd block_flag;
	char ack;
	int input_data_size;
	char input_data[KEY_AV_ISP_PACKET_PARAM_SIZE];
	char output_data[KEY_PACKET_PARAM_SIZE];
};

/* MSG IOCTL base */
#define MICOM_MSG_AV_IOCTL_IOCBASE		0xC3
/* IOCTL list for micom msg driver */
#define MICOM_MSG_AV_IOCTL_SEND_MSG_NO_ACK	_IOWR(MICOM_MSG_AV_IOCTL_IOCBASE, 0, \
						struct sdp_micom_usr_av_msg)
#define MICOM_MSG_AV_IOCTL_SEND_MSG	_IOWR(MICOM_MSG_AV_IOCTL_IOCBASE, 1, \
						struct sdp_micom_usr_av_msg)

/* ISP IOCTL base */
#define MICOM_ISP_AV_IOCTL_IOCBASE		0xC4
/* IOCTL list for micom isp driver */
#define MICOM_ISP_AV_IOCTL_SEND_ARRAY_DATA	_IOWR(MICOM_ISP_AV_IOCTL_IOCBASE, 0, \
						struct sdp_micom_usr_av_isp)
#define MICOM_ISP_AV_IOCTL_SET_BLOCK	_IOWR(MICOM_ISP_AV_IOCTL_IOCBASE, 1, \
						struct sdp_micom_usr_av_isp)
#endif

