/*
 * sdp_micom_ir.h - sdp infrared remote control header file
 * last modified 2018/11/29 by drain.lee
 */

#ifndef __SDP_MICOM_IR_H__
#define __SDP_MICOM_IR_H__

/* below define is must sync from tztv-micom/sdp_micom.h */
enum sdp_ir_event_e {
	SDP_IR_EVT_KEYPRESS = 0x10,
	SDP_IR_EVT_KEYRELEASE = 0x1E,
	SDP_IR_EVT_KEY_UNDEFINED = 0xAA,
};

typedef void (*sdp_micom_irr_cb)(enum sdp_ir_event_e event,
		unsigned int code, unsigned long long timestemp_ns, void *priv);

int sdp_messagebox_register_irr_cb(sdp_micom_irr_cb irrcb_fn, void *priv);

#endif