/*
 * cas_proto.h - 
 *
 * Copyright (C) 2014-2015 Samsung Electronics Co., Ltd.
 *			http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CAS_PROTO_H
#define __CAS_PROTO_H


#include <linux/completion.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/debugfs.h>
#include <linux/time.h>

#include "cas_cnc.h"


/*
 * If urb submit length and actual length doesn't match, step usb driver report error(bad length).
 * So data length reduced to 16
 */
//#define MAX_INTR_DATA_LEN		64
#define MAX_INTR_DATA_LEN		16
// TODO: Check this value really use for tvkey in funtrue.

struct cas_proto_dongle_ctx
{
	__le16 idVendor;
	__le16 idProduct;
	__u8 iSerialNumber;
	int valid;
	int dongle_id;
	char dev_path[MAX_DEV_PATH_LEN];
};

struct cas_proto_dev_ctx
{
	int valid;
	int dongle_id;
	struct usb_device *udev;
};

struct cas_proto_evt_ctx
{
	struct cas_proto_ctx ctx;
	struct list_head list;
	struct list_head list_status_event;
};


#endif	// __CAS_PROTO_H

