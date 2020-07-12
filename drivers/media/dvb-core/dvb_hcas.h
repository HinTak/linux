/*
 * dvb_hcas.h: HybridCAS interfaces
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

#ifndef _DVB_HYBRID_CAS_H_
#define _DVB_HYBRID_CAS_H_

#include <linux/list.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/ca_tztv.h>

#include "dvbdev.h"


struct dvb_hcas {

	/* the module owning this structure */
	struct module* owner;
	void (*hcas_set_streamid)(struct dvb_hcas *sdp_ca, ca_set_hcas_t *tag);
	void (*hcas_clear_streamid)(struct dvb_hcas *sdp_ca, ca_set_hcas_t *tag);
	void (*hcas_clear_all)(struct dvb_hcas *sdp_ca, ca_set_frontend_t *source);
	void (*hcas_start)(struct dvb_hcas *sdp_ca, ca_set_frontend_t *source);	
	void (*hcas_stop)(struct dvb_hcas *sdp_ca, ca_set_frontend_t *source);

	/* private data, used by caller */
	void* data;

	/* Opaque data used by the dvb_ca core. Do not modify! */
	void* private;
};




/* ******************************************************************************** */
/* Initialisation/shutdown functions */

/**
 * Initialise a new DVB CA device.
 *
 * @param dvb_adapter DVB adapter to attach the new CA device to.
 * @param ca The dvb_ca instance.
 * @param flags Flags describing the CA device (DVB_HCAS_FLAG_*).
 * @param slot_count Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */
extern int dvb_hcas_init(struct dvb_adapter *dvb_adapter, struct dvb_hcas* ca, int flags);

/**
 * Release a DVB CA device.
 *
 * @param ca The associated dvb_ca instance.
 */
extern void dvb_hcas_release(struct dvb_hcas* ca);



#endif	//_DVB_HYBRID_CAS_H_
