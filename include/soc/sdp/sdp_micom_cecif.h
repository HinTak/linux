/*
 * SDP Micom CEC interface driver header(shared from micom)
 *
 * Copyright (C) 2017 dongseok lee <drain.lee@samsung.com>
 */
#ifndef _SDP_MICOM_CECIF_H
#define _SDP_MICOM_CECIF_H
#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
#include <media/cec.h>

#else/* !CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM *****************************/
#include <linux/types.h>

/* cec event field */
#define SDP_CEC_EVENT_TX_OK			(1UL << 0)
#define SDP_CEC_EVENT_TX_ARB_LOST		(1UL << 1)
#define SDP_CEC_EVENT_TX_NACK			(1UL << 2)
#define SDP_CEC_EVENT_TX_LOW_DRIVE		(1UL << 3)
//#define SDP_CEC_EVENT_TX_ERROR			(1UL << 4)

#define SDP_CEC_EVENT_RX_DONE			(1UL << 16)
//#define SDP_CEC_EVENT_RX_OVERFLOW		(1UL << 17)
//#define SDP_CEC_EVENT_RX_NOACK			(1UL << 18)
//#define SDP_CEC_EVENT_RX_ERROR			(1UL << 19)

/* copy from kernel 4.12 <linux/cec.h> */
#define CEC_MAX_MSG_SIZE	16
struct cec_msg {
	__u64 tx_ts;
	__u64 rx_ts;
	__u32 len;
	__u32 timeout;
	__u32 sequence;
	__u32 flags;
	__u8 msg[CEC_MAX_MSG_SIZE];
	__u8 reply;
	__u8 rx_status;
	__u8 tx_status;
	__u8 tx_arb_lost_cnt;
	__u8 tx_nack_cnt;
	__u8 tx_low_drive_cnt;
	__u8 tx_error_cnt;
};

struct sdp_mc_cecif_dev;
typedef int (*sdp_mc_cecif_cb)(u32 event, struct cec_msg *rxmsg, void *args);

int sdp_mc_cecif_enable(struct sdp_mc_cecif_dev *smcec, bool enable);
int sdp_mc_cecif_log_addr(struct sdp_mc_cecif_dev *smcec, u8 addr);
int sdp_mc_cecif_transmit(struct sdp_mc_cecif_dev *smcec, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg);
int sdp_mc_cecif_of_device_attach(struct device *dev, sdp_mc_cecif_cb eventcb_fn,
				void *eventcb_args, struct sdp_mc_cecif_dev **out_smcec);

#endif/*CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM*/
#endif/*_SDP_MICOM_CECIF_H*/
