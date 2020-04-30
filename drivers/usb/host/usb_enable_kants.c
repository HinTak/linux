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
#include "usb_enable_kants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize
int init_usb_module(struct platform_device *pdev)
{
	int ret = -1;

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

#ifndef CONFIG_USB_DONGLE_BY_STBC
	dongle_gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-don-enable", 0);
	if(!gpio_is_valid(dongle_gpio)) {
		printk(KERN_ERR "[%s] invalid dongle GPIO\n", __func__);
		return -EINVAL;
	}

	printk(KERN_INFO "of_node Name = %s\n", (pdev->dev.of_node)->name);
	printk(KERN_INFO "of_node Type = %s\n", (pdev->dev.of_node)->type);
	
	ret = gpio_request((u32)dongle_gpio, "usb_don_enable");
	if(ret) {
		if(ret != -EBUSY) {
			dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
			return ret;
		}
	}

	gpio_direction_output((u32)dongle_gpio, 0);
#endif

	return ret;
}


//--------------------------------------------------------------------------------------------------------------------//
/*	=====================================================
	| Year	| SoC		| Board Type	| Power Control	|
	=====================================================
	| 2018	| Kant.S	| Two USB		| CPU & Micom	|
	-		-			---------------------------------
	|		|			| One USB		| Micom			|
	-		---------------------------------------------
	|		| Kant.SU	| Built-in		| CPU & Micom	|
	=====================================================	*/
// Set usb power
void set_usb_power(int status)
{
	// IMPORTANT : It should always be called in GPIO, 3G order
	set_GPIO_port(status);
	set_3G_port(status);
}


//--------------------------------------------------------------------------------------------------------------------//
/*	=====================================================
	| TVKey support port info.							|
	=====================================================
	| ** Kant.S											|
	|	-. Two USB	: 3-1(by micom), 4-1(by gpio)		|
	|	-. One USB	: 4-1(by micom)						|
	|													|
	| ** Kant.SU										|
	|	-. Built-in	: 3-1(by gpio), 4-1(by micom)		|
	=====================================================	*/
// Reset usb power for TVKey dongle
void reset_tvkey_dongle(int bus_num)
{
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


//--------------------------------------------------------------------------------------------------------------------//
// Return usb power status
int get_usb_status(void)
{
	// Not implemented. Because there is no place to use.
	return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void set_3G_port(int status)
{
#ifdef CONFIG_USB_DONGLE_BY_STBC
	if(!send_micom_msg(status)) {
		printk(KERN_ERR "[%s] set 3g %s success\n", __func__, status == 1 ? "on" : "off");
	}
#else
	gpio_set_value((u32)dongle_gpio, status);
	printk(KERN_ERR "[%s] set 3g gpio %s success\n", __func__, status == 1 ? "on" : "off");
#endif
}


void set_GPIO_port(int status)
{
	gpio_set_value((u32)gpio, status);
	printk(KERN_ERR "[%s] set gpio %s success\n", __func__, status == 1 ? "on" : "off");
}


#ifdef CONFIG_USB_DONGLE_BY_STBC
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
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

