/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#ifndef __USB_ENABLE_COMMON__
#define __USB_ENABLE_COMMON__


#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/cdev.h>

#include <trace/early.h>
#include <soc/sdp/soc.h>

#include <linux/usb.h>

#define USB_ON			1
#define USB_OFF			0

#define	BUS_NUM_MASK	0xFFFF
#define	PORT_NUM_MASK	0xFFFF
#define	PORT_NUM_SHIFT	16


static struct cdev usben_gpio_cdev;
static dev_t usben_id;
static struct class *device_class;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common functions
// It is implemented in usb_enable_common.c
long control_usb_power(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param);
int create_device_node(void);
void destroy_device_node(void);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions that must be defined for each SoC
// It is implemented in usb_enable_xxx(SoC).c
int init_usb_module(struct platform_device *pdev);
void set_usb_power(int status);
void reset_tvkey_dongle(int bus_num);
int get_usb_status(void);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

