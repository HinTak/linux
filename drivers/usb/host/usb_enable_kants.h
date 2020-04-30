/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#ifndef __USB_ENABLE_KANTS__
#define __USB_ENABLE_KANTS__


#if defined(CONFIG_ARCH_NVT72172)		// Kant.S
#define TVKEY_BUILTIN_USB_1		3
#define TVKEY_BUILTIN_USB_2		4
#elif defined(CONFIG_ARCH_NVT72673)		// Kant.SU
#define TVKEY_BUILTIN_USB_1		4
#define TVKEY_BUILTIN_USB_2		3
#endif
#define TVKEY_RESET_TIME		500		// msec


static int gpio;
#ifndef CONFIG_USB_DONGLE_BY_STBC
static int dongle_gpio;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void set_3G_port(int status);
void set_GPIO_port(int status);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cause dongle usb is usually used for Wifi
// So it should be controlled by STBC
// But in some case, this USB port can also be controlled in arm side

// More, in some case, like seret, don't have tztv-micom module
// Which means GPIO inside STBC should not be controlled here
// So add a new kernel config "CONFIG_USB_DONGLE_BY_STBC" to turn on/off STBC cmd
#ifdef CONFIG_USB_DONGLE_BY_STBC
#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1
#define MICOM_RETRY				3


struct sdp_micom_msg {
	int msg_type;
	int length;
	unsigned char msg[10];
};

struct sdp_micom_msg micom_msg[] = {
	[USB_OFF] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length 	= KEY_PACKET_DATA_SIZE,
		.msg[0] 	= 0x38,
		.msg[1] 	= 2,
		.msg[2] 	= 0,
	},

	[USB_ON] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length		= KEY_PACKET_DATA_SIZE,
		.msg[0]		= 0x38,
		.msg[1]		= 2,
		.msg[2] 	= 1,
	},
};


// External symbol
extern int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len);

int _ext_sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_micom_send_cmd_ack, cmd, ack, data, len);

	return ret;
}


int send_micom_msg(int status);
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

