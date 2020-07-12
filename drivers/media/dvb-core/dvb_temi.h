/*
 * dvb_temi.h: TEMI read driver for media synchronization
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

#ifndef _DVB_TEMI_H_
#define _DVB_TEMI_H_


#include <linux/list.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/dmx_tztv.h>

#include "dvbdev.h"
#include "demux.h"
#include "dvb_ringbuffer.h"


struct dvb_temi {

	/* the module owning this structure */
	struct module* owner;
	int (*start)(struct dvb_temi *p);	
	int (*stop)(struct dvb_temi *p);
	int (*set_source)(struct dvb_temi *p, const dmx_source_t *src);

	/* private data, used by caller */
	void* data;

	/* Opaque data used by the dvb_temi core. Do not modify! */
	void* private;

	/* for temi packet data */
	struct dvb_ringbuffer temi_buffer;
	int (*temi_cb) (struct dvb_temi *p, const u8 *buffer1, size_t buffer1_len, const u8 *buffer2, size_t buffer2_len);
};

/* ******************************************************************************** */
/* Initialisation/shutdown functions */

/**
 * Initialise a new DVB TEMI device.
 *
 * @param dvb_adapter DVB adapter to attach the new TEMI device to.
 * @param dmx The dvb_temi instance.
 * @param flags Flags describing the TEMI device (dvb_temi_FLAG_*).
 * @return 0 on success, nonzero on failure
 */
extern int dvb_temi_init(struct dvb_adapter *dvb_adapter, struct dvb_temi* ca, int flags);


/**
 * Release a DVB TEMI device.
 *
 * @param dmx The associated dvb_temi instance.
 */
extern void dvb_temi_release(struct dvb_temi* ca);


#endif	//_DVB_HYBRID_CAS_H_
