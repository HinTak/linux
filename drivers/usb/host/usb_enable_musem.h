/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#ifndef __USB_ENABLE_MUSEM__
#define __USB_ENABLE_MUSEM__


#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1
#define MICOM_RETRY				3

#define OCL_USB_1		18
#define OCL_USB_2		14
#define OCL_RETRY		10

#define TVKEY_OCL_USB			3
#define TVKEY_BUILTIN_USB_1		2
#define TVKEY_BUILTIN_USB_2		3
#define TVKEY_RESET_TIME		500		// msec


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


static int gpio;
#if defined(__PRD_TYPE_WALL__)
static int gpio_valens;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// External symbol
extern int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len);
extern int sdp_ocm_gpio_write(unsigned int port, unsigned int level);
extern int sdp_ocm_gpio_read(unsigned int port);
extern int sdp_ocm_dongle_onoff(unsigned int onoff);
extern int tztv_sys_get_platform_info(void);


int _ext_sdp_ocm_gpio_write(unsigned int port, unsigned int level)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_gpio_write, port, level);

	return ret;
}

int _ext_sdp_ocm_gpio_read(unsigned int port)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_gpio_read, port);

	return ret;
}

int _ext_sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_micom_send_cmd_ack, cmd, ack, data, len);

	return ret;
}

int _ext_sdp_ocm_dongle_onoff(unsigned int onoff)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_dongle_onoff, onoff);

	return ret;
}

/*
typedef enum {
	PLAT_MUSEL = 0x0,
	PLAT_MUSEM_BUILTIN = 0x2,
	PLAT_MUSEM_SERIF = 0x3,
	PLAT_MUSEM_FRAME = 0x4,
	PLAT_MUSEM_WALL = 0x5,
	PLAT_MUSEM_PORTLAND = 0x06,
	PLAT_MUSEM_OC = 0x7,
} PLATFORM_TYPE;
*/
int _ext_tztv_sys_get_platform_info(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_get_platform_info);

	return ret;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int usb_is_ocl(void);
int send_micom_msg(int status);
void set_OCL_port(int port, int status);
void set_OCL_3G_port(int status);
void set_3G_port(int status);
void set_GPIO_port(int status);
// 2019.03.12 : The gpio for Valens reset is used for another purpose. So comment out.
/*
#if defined(__PRD_TYPE_WALL__)
int usb_is_wall(void);
int init_Valens_module(struct platform_device *pdev);
void set_Valens_VBus(int status);
#endif
*/
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

