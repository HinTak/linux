/*
 * dvb_atsc30dmx.h: demux for atsc3.0 interfaces
 *
 * Copyright (C) 2004 Andrew de Quincey
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _DVB_ATSC30_DMX_H_
#define _DVB_ATSC30_DMX_H_


#include <linux/list.h>
#include <linux/dvb/dmx.h>

#include "dvbdev.h"
#include "demux.h"
#include "dvb_ringbuffer.h"




struct dvb_atsc30dmx {

	/* the module owning this structure */
	struct module* owner;
	int (*start)(struct dvb_atsc30dmx *sdp_atsc30dmx);	
	int (*stop)(struct dvb_atsc30dmx *sdp_atsc30dmx);

	/* private data, used by caller */
	void* data;

	/* Opaque data used by the dvb_atsc30dmx core. Do not modify! */
	void* private;

	/* for atsc3.0 ip packet data */
	struct dvb_ringbuffer ip_buffer;
	int (*atsc30dmx_cb) (struct dvb_atsc30dmx *dmxpub, const u8 *buffer1, size_t buffer1_len, const u8 *buffer2, size_t buffer2_len);
};




/* ******************************************************************************** */
/* Initialisation/shutdown functions */

/**
 * Initialise a new DVB atsc3.0 dmx device.
 *
 * @param dvb_adapter DVB adapter to attach the new atsc30dmx device to.
 * @param dmx The dvb_atsc30dmx instance.
 * @param flags Flags describing the atsc30dmx device (dvb_atsc30dmx_FLAG_*).
 * @return 0 on success, nonzero on failure
 */
extern int dvb_atsc30dmx_init(struct dvb_adapter *dvb_adapter, struct dvb_atsc30dmx* ca, int flags);

/**
 * Release a DVB ATSC30DMX device.
 *
 * @param dmx The associated dvb_atsc30dmx instance.
 */
extern void dvb_atsc30dmx_release(struct dvb_atsc30dmx* ca);



#endif	//_DVB_HYBRID_CAS_H_
