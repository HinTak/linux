/* cas_cnc.h - 
 * Copyright (C) 2014-2015 Samsung Electronics Co., Ltd.
 *          http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CAS_CNC_H
#define __CAS_CNC_H

#define MAX_NUM_DONGLE	4
#define CNC_EVENT_BUFFER_MAX_SIZE	128

typedef enum
{
	CNC_NOTIFY_DEVICE_INTERRUPT = 1,
	CNC_NOTIFY_DEVICE_CONNECT,
	CNC_NOTIFY_DEVICE_DISCONNECT,
	CNC_NOTIFY_DEVICE_ERROR,
	//TODO add more event
}cas_proto_evt;

struct cas_proto_ctx { /*!<  context for CAS Protocol */
	cas_proto_evt cas_evt;
	int data_len;
	int dongle_id;
	char buf[CNC_EVENT_BUFFER_MAX_SIZE];
};

#endif /* __CAS_CNC_H */
