/*
 * cas_cnc.h - 
 *
 * Copyright (C) 2014-2015 Samsung Electronics Co., Ltd.
 *			http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CAS_CNC_H
#define __CAS_CNC_H


#define MAX_NUM_DONGLE				4
#define CNC_EVENT_BUFFER_MAX_SIZE	128
#define MAX_DEV_PATH_LEN			16


typedef enum
{
	CNC_NOTIFY_DEVICE_INTERRUPT = 1,
	CNC_NOTIFY_DEVICE_CONNECT,
	CNC_NOTIFY_DEVICE_DISCONNECT,
	CNC_NOTIFY_DEVICE_ERROR,

	// TODO: Add more event
}cas_proto_evt;


struct cas_port_info
{
	char dev_path[MAX_DEV_PATH_LEN];
	int dev_path_len;
};

// Context for CAS Protocol
struct cas_proto_ctx	
{
	cas_proto_evt cas_evt;
	int data_len;
	int dongle_id;
	char buf[CNC_EVENT_BUFFER_MAX_SIZE];
	struct cas_port_info port_info;
};


#endif	// __CAS_CNC_H

