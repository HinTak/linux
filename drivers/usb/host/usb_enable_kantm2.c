/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#include "usb_enable_common.h"
#include "usb_enable_kantm2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize
int init_usb_module(struct platform_device *pdev)
{
	int ret = -1;

	if(usb_is_ocl()) {		// Kant.M2 OCL
		printk(KERN_INFO "[%s] ocl do not use usben gpio\n", __func__);
		ret = 0;
	}
	else {					// Kant.M2 Built-in
		gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
		if(!gpio_is_valid(gpio)) {
			printk(KERN_ERR "[%s] invalid GPIO\n", __func__);
			return -EINVAL;
		}

		printk(KERN_INFO "of_node Name = %s\n", (pdev->dev.of_node)->name);
		printk(KERN_INFO "of_node Type = %s\n", (pdev->dev.of_node)->type);

		ret = gpio_request((u32)gpio, "usb_enable");
		if(ret) {
			if(ret != -EBUSY) {
				dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
				return ret;
			}
		}
		
		gpio_direction_output((u32)gpio, 0);
	}

	return ret;
}



//--------------------------------------------------------------------------------------------------------------------//
/*	=====================================================
	| Year	| SoC		| Board Type	| Power Control	|
	=====================================================
	| 2018	| Kant.M2	| Built-in		| CPU & Micom	|
	-		-			---------------------------------
	|		|			| OCL			| I2C			|
	-		---------------------------------------------
	|		| Kant.M2E	| Built-in		| CPU & Micom	|
	=====================================================	*/
// Set usb power
void set_usb_power(int status)
{
	if(usb_is_ocl()) {		// Kant.M2 OCL
		set_OCL_port(OCL_USB_1, status);
		set_OCL_port(OCL_USB_2, status);
		set_OCL_3G_port(status);
	}
	else {					// Kant.M2 Built-in
		set_3G_port(status);
		set_GPIO_port(status);
	}
}


//--------------------------------------------------------------------------------------------------------------------//
/*	=====================================================
	| TVKey support port info.							|
	=====================================================
	| ** Kant.M2(E)										|
	|	-. OCL		: 2-1.2								|
	|	-. Built-in : 2-1(by micom), 3-1(by gpio)		|
	=====================================================	*/
// Reset usb power for TVKey dongle
void reset_tvkey_dongle(int bus_num)
{
	if(usb_is_ocl()) {		// Kant.M2 OCL
		if(bus_num == TVKEY_OCL_USB) {
			set_OCL_port(OCL_USB_1, USB_OFF);
			msleep(TVKEY_RESET_TIME);
			set_OCL_port(OCL_USB_1, USB_ON);
		}
		else {
			printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
		}
	}
	else {					// Kant.M2 Built-in
		if(bus_num == TVKEY_BUILTIN_USB_1) {
			set_3G_port(USB_OFF);
			msleep(TVKEY_RESET_TIME);
			set_3G_port(USB_ON);
		}
		else if(bus_num == TVKEY_BUILTIN_USB_2) {
			set_GPIO_port(USB_OFF);
			msleep(TVKEY_RESET_TIME);
			set_GPIO_port(USB_ON);
		}
		else {
			printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
		}
	}
}


//--------------------------------------------------------------------------------------------------------------------//
// Return usb power status
int get_usb_status(void)
{
	// Not implemented. Because there is no place to use.
	return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int usb_is_ocl(void)
{
	// 3 == M2s(OCL), 4 == OCL Frame, 7 == OCL 8K
	if((soc_is_sdp1701()) && ((_ext_tztv_sys_get_platform_info() == 3) || (_ext_tztv_sys_get_platform_info() == 4) || (_ext_tztv_sys_get_platform_info() == 7))) {
		return 1;
	}
	else {
		return 0;
	}
}


int send_micom_msg(int status)
{
	unsigned char cmd = 0;
	char ack = 0, data[4] = {0, };
	int len = 0, ret = 0, cnt = 0;

	cmd		= micom_msg[status].msg[0];
	ack 	= micom_msg[status].msg[0];
	data[0]	= micom_msg[status].msg[1];
	data[1]	= micom_msg[status].msg[2];
	len		= 2;

RETRY:
	ret = _ext_sdp_micom_send_cmd_ack(cmd, ack, data, len);
	if(ret && ((++cnt) < MICOM_RETRY)) {
		msleep( 100 );
		goto RETRY;
	}

	printk(KERN_ERR "[%s] send msg %s for %s\n",
		__func__, cnt >= MICOM_RETRY ? "fail" : "success", status == 1 ? "on" : "off");

	return ret;
}


void set_OCL_port(int port, int status)
{
	int cnt = 0;

RETRY:
	_ext_sdp_ocm_gpio_write(port, status);
	// ocm_gpio_write always retrun '0'
	// It has to check whether it was correctly set up using ocm_gpio_read
	if((status != _ext_sdp_ocm_gpio_read(port)) && ((++cnt) < OCL_RETRY)) {
		msleep(500);
		goto RETRY;
	}

	printk(KERN_ERR "[%s] set ocl %s %s for [%d]\n",
		__func__, status == 1 ? "on" : "off", cnt >= OCL_RETRY ? "fail" : "success", port);
}


void set_OCL_3G_port(int status)
{
	int cnt = 0, ret = 0;

RETRY:
	ret = _ext_sdp_ocm_dongle_onoff(status);
	if(ret && ((++cnt) < OCL_RETRY)) {
		msleep(500);
		goto RETRY;
	}

	printk(KERN_ERR "[%s] set ocl 3g %s %s\n",
		__func__, status == 1 ? "on" : "off", cnt >= OCL_RETRY ? "fail" : "success");
}


void set_3G_port(int status)
{
	if(!send_micom_msg(status)) {
		printk(KERN_ERR "[%s] set 3g %s success\n", __func__, status == 1 ? "on" : "off");
	}
}


void set_GPIO_port(int status)
{
	gpio_set_value((u32)gpio, status);
	printk(KERN_ERR "[%s] set gpio %s success\n", __func__, status == 1 ? "on" : "off");
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

