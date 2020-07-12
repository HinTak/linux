/*
 * SDP Micom CEC interface driver(shared from micom)
 *
 * Copyright (C) 2017 dongseok lee <drain.lee@samsung.com>
 */

#define VERSION_STR		"0.1(20170725 create driver)"
//#define DEBUG
//#define DEBUG_VERBOSE

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/pm_runtime.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/circ_buf.h>
#include <linux/mfd/syscon.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <soc/sdp/sdp_micom_cecif.h>

#ifdef DEBUG_VERBOSE
#define dev_verbose(args...)	dev_dbg(args)
#else
#define dev_verbose(args...)	if(0) dev_dbg(args)
#endif

/* HDMI CEC Interface(after Kant) */
#define CEC_INT					0x00// CEC Interrupt Register R/W 0x0
#define CEC_CTRL				0x04// CEC Global Control Register R/W 0x0
#define CEC_RXSTAT				0x08// CEC Rx Status Register R 0x0
#define CEC_TXSTAT				0x0C// CEC Tx Status Register R 0x0
#define CEC_RXFIFO				0x10// CEC Rx FIFO Data Register R 0x0
#define CEC_TXFIFO				0x14// CEC Tx FIFO Data Register R/W 0x0
#define CEC_BIT_TIME			0x18// CEC Bit Timing setting Register R/W 0x4A30 1E0C
#define CEC_CONF				0x1C// CEC Configuration Register R/W 0x0000 0F5A
#define CEC_FREQ				0x20// CEC Frequency Control Register R/W 0x0
#define CEC_DETAIL1				0x24// CEC detail Frequency Setting Register R/W 0x0
#define CEC_DETAIL2				0x28// CEC detail Frequency Setting Register R/W 0x0
#define CEC_DETAIL3				0x2C// CEC detail Frequency Setting Register R/W 0x0
#define CEC_PULSE				0x30// CEC Pulse counter for 50us Generation R/W 0x0000 04CF
#define CEC_START_MARGIN		0x34// START bit width margin control R/W 0x04040404
#define CEC_ZERO_MARGIN			0x38// ZERO bit width margin control R/W 0x04040707
#define CEC_ONE_MARGIN			0x3C// ONE bit width margin control R/W 0x04040707
#define CEC_SIG_FREETIME		0x40// Signal free time setting Register R/W 0x012000C0
#define CEC_TRANS_COUNT			0x44// Transtion count setting Register R/W 0x00050180

#define CEC_INT_RX_NOACK			(0x1U<<17)
#define CEC_INT_RX_ERROR			(0x1U<<16)
#define CEC_INT_RX_ACK_BUS_FREE		(0x1U<<15)
#define CEC_INT_RX_DATA_BUS_FREE	(0x1U<<14)
#define CEC_INT_RX_HEADER_BUS_FREE	(0x1U<<13)
#define CEC_INT_RX_DONE				(0x1U<<12)
#define CEC_INT_RX_ALL				(0x3FU<<12)

#define CEC_INT_RX_INT_EN			(0x1U<<9)
#define CEC_INT_TX_INT_EN			(0x1U<<8)
#define CEC_INT_TX_DONE				(0x1U<<4)
#define CEC_INT_RX_CLR				(0x1U<<1)
#define CEC_INT_TX_CLR				(0x1U<<0)

#define CEC_CRTL_AUTO_WRITE			(0x1U<<28)
#define CEC_CRTL_FAKE_READ			(0x1U<<24)
#define CEC_CRTL_RX_FLUSH			(0x1U<<21)
#define CEC_CRTL_SECRET_READ		(0x1U<<20)
#define CEC_CRTL_LOGICAL_ADDR_MASK	((0xF)<<16)
#define CEC_CRTL_LOGICAL_ADDR(val)	(((val)&0xF)<<16)/* [19:16] 4bit */
#define CEC_CRTL_RX_ENABLE			(0x1U<<12)
#define CEC_CRTL_MANUAL_SEND		(0x1U<<11)
#define CEC_CRTL_SEND_MESSAGE		(0x1U<<8)
#define CEC_CRTL_PIN_SEL			(0x1U<<4)/*Test Mode*/
#define CEC_CRTL_TEST				(0x1U<<0)/*Pin value high/low*/

#define CEC_RXSTAT_BUSY			(0x1U<<16)
#define CEC_RXSTAT_FULL			(0x1U<<12)
#define CEC_RXSTAT_EMPTY		(0x1U<<8)
#define CEC_RXSTAT_POINTER(val)	((val)&0x1F)/*FIFO count*/

#define CEC_TXSTAT_BUSY			(0x1U<<24)
#define CEC_TXSTAT_ERROR		(0x1U<<20)
#define CEC_TXSTAT_NOACK		(0x1U<<16)
#define CEC_TXSTAT_FULL			(0x1U<<12)
#define CEC_TXSTAT_EMPTY		(0x1U<<8)
#define CEC_TXSTAT_POINTER(val)	((val)&0x1F)/*FIFO count*/

#define CEC_RXFIFO_ERROR		(0x1U<<9)
#define CEC_RXFIFO_HEAD			(0x1U<<8)
#define CEC_RXFIFO_DATA(val)	((val)&0xFF)

#define CEC_TXFIFO_MANUAL_WRITE	(0x1U<<12)
#define CEC_TXFIFO_DATA(val)	((val)&0xFF)

#define CEC_PULSE_DOUT_SEL		(0x1U<<31)
#define CEC_PULSE_COUNTER(val)	((val)&0xFFFFFF)


#define SDP_CEC_MAX_MSG_SIZE	16
#define SDP_CEC_RX_MSG_RING_SIZE 0x4
#define SDP_CEC_TX_POLLING_INERVAL_MS	30/* 1 frame time */
#define SDP_CEC_TX_START_WAIT_TIME_MS	3000

#define SDP_CEC_CONGIF_REG_START	0x18
#define SDP_CEC_CONGIF_REG_END		0x44
#define SDP_CEC_CONGIF_REG_SIZE		(SDP_CEC_CONGIF_REG_END-SDP_CEC_CONGIF_REG_START+4)

#define SDP_CECIF_RAW_RXFIFO_SIZE (128U)

#define CEC_NAME	"sdp-mc-cecif"

static struct platform_driver sdp_mc_cecif_pdrv;

struct sdp_cec_txstat {
	uint32_t busy             : 1;
	uint32_t done             : 1;
	uint32_t noack            : 1;
	uint32_t arbitration_lost : 1;
	uint32_t low_drive        : 1;
};

struct cec_rx_ringbuf_t {
	spinlock_t lock;
	unsigned long head;
	unsigned long tail;
	u32 raw_rxfifos[SDP_CECIF_RAW_RXFIFO_SIZE];
};

struct sdp_mc_cecif_dev {
	struct device *dev;
	int irq;
	void* __iomem regbase;

	bool is_enabled;

#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
	struct cec_adapter *adap;
	//struct cec_notifier *notifier;
#else
	struct device		*attached_dev;
	sdp_mc_cecif_cb		eventcb_fn;
	void				*eventcb_args;
#endif

	/* debugfs */
	struct dentry *dbgfs_root;

	struct cec_rx_ringbuf_t rxring;

	struct sdp_cec_txstat	txstat;
	struct cec_msg txmsg;
	struct cec_msg rxmsg;

	/* for tx timeout timer */
	struct timer_list	tx_timeout_timer;
	u8					tx_cnt;/* -1=pre start, 0=tx started, 1~16=tx done */
	u32					tx_ideal_time_ms;/* ideal message send time */
	u32					tx_timeout_ms;
	u32					tx_start_ms;

	struct regmap		*regmap_mccmu;

	bool				cec_config_is_valid;
	u32					cec_config_regs[SDP_CEC_CONGIF_REG_SIZE/4];
};


/**************************** Ring buffer func ********************************/
static void sdp_cecif_rx_ringbuf_init(struct cec_rx_ringbuf_t *ringbuf)
{
	spin_lock_init(&ringbuf->lock);
	ringbuf->head = 0;
	ringbuf->tail = 0;
}

static u32 sdp_cecif_rx_ringbuf_count(struct cec_rx_ringbuf_t *ringbuf)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	tail = ACCESS_ONCE(ringbuf->tail);
	ret = CIRC_CNT(head, tail, SDP_CECIF_RAW_RXFIFO_SIZE);
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static int sdp_cecif_rx_ringbuf_enqueue(struct cec_rx_ringbuf_t *ringbuf, u32 in_raw_rxfifo)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;

	spin_lock_irqsave(&ringbuf->lock, flags);

	tail = ACCESS_ONCE(ringbuf->tail);
	if(CIRC_SPACE(ringbuf->head, tail, SDP_CECIF_RAW_RXFIFO_SIZE) >= 1) {
		ringbuf->raw_rxfifos[ringbuf->head] = in_raw_rxfifo;
		ringbuf->head = (ringbuf->head + 1) & (SDP_CECIF_RAW_RXFIFO_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static int sdp_cecif_rx_ringbuf_dequeue(struct cec_rx_ringbuf_t *ringbuf, u32 *out_raw_rxfifo)
{
	int ret = 0;
	unsigned long flags;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	if(CIRC_CNT(head, ringbuf->tail, SDP_CECIF_RAW_RXFIFO_SIZE) >= 1) {
		*out_raw_rxfifo = ringbuf->raw_rxfifos[ringbuf->tail];
		ringbuf->tail = (ringbuf->tail + 1) & (SDP_CECIF_RAW_RXFIFO_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static inline u32 cec_readl(struct sdp_mc_cecif_dev *smcec, u32 offset) {
	return readl(smcec->regbase + offset);
}

static inline void cec_writel(struct sdp_mc_cecif_dev *smcec, u32 value, u32 offset) {
	writel(value, smcec->regbase + offset);
}

static inline void cec_writel_bitset(struct sdp_mc_cecif_dev *smcec, u32 value, u32 offset) {
	writel(readl(smcec->regbase + offset) | value, smcec->regbase + offset);
}

static inline void cec_writel_bitclr(struct sdp_mc_cecif_dev *smcec, u32 value, u32 offset) {
	writel(readl(smcec->regbase + offset) & (~value), smcec->regbase + offset);
}


static unsigned long long __get_nsecs(void)
{
	return sched_clock();
}

static void __cecif_save_config_regs(struct sdp_mc_cecif_dev *smcec)
{
	int i;

	if(smcec->cec_config_is_valid) {
		return;
	}

	for(i = 0; i < SDP_CEC_CONGIF_REG_SIZE; i += 4) {
		smcec->cec_config_regs[i/4] = cec_readl(smcec, SDP_CEC_CONGIF_REG_START + i);
	}

	smcec->cec_config_is_valid = true;
}

static void __cecif_restore_config_regs(struct sdp_mc_cecif_dev *smcec)
{
	int i;

	if(!smcec->cec_config_is_valid) {
		dev_err(smcec->dev, "restore: cec config is not saved\n");
		return;
	}

	for(i = 0; i < SDP_CEC_CONGIF_REG_SIZE; i += 4) {
		dev_dbg(smcec->dev, "restore: cec_config_regs[%d]=0x%08x\n", i/4, smcec->cec_config_regs[i/4]);
		cec_writel(smcec, smcec->cec_config_regs[i/4], SDP_CEC_CONGIF_REG_START + i);
	}
}

static int __cecif_hwreset(struct sdp_mc_cecif_dev *smcec) {
	u32 ctrl, intr;

	dev_err(smcec->dev, "do cecif hwreset!\n");

	ctrl = cec_readl(smcec, CEC_CTRL);
	intr = cec_readl(smcec, CEC_INT);

	/* CEC HW Reset assert */
	regmap_update_bits(smcec->regmap_mccmu, 0x20, 0x2, 0x0);

	/* CEC HW Reset deassert */
	regmap_update_bits(smcec->regmap_mccmu, 0x20, 0x2, 0x2);

	__cecif_restore_config_regs(smcec);
	cec_writel(smcec, ctrl, CEC_CTRL);
	cec_writel(smcec, intr, CEC_INT);

	return 0;
}

static int __process_tx_done(struct sdp_mc_cecif_dev *smcec, struct sdp_cec_txstat *txstat) {

#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
	if(txstat->arbitration_lost) {
		dev_dbg(smcec->dev, "cec_transmit_done CEC_TX_STATUS_ARB_LOST\n");
		cec_transmit_done(smcec->adap, CEC_TX_STATUS_ARB_LOST, 1, 0, 0, 0);
	} else if(txstat->noack) {
		dev_dbg(smcec->dev, "cec_transmit_done CEC_TX_STATUS_NACK\n");
		cec_transmit_done(smcec->adap, CEC_TX_STATUS_NACK, 0, 1, 0, 0);
	} else if(txstat->low_drive) {
		dev_dbg(smcec->dev, "cec_transmit_done CEC_TX_STATUS_LOW_DRIVE\n");
		cec_transmit_done(smcec->adap, CEC_TX_STATUS_LOW_DRIVE, 0, 0, 1, 0);
	} else {
		dev_dbg(smcec->dev, "cec_transmit_done CEC_TX_STATUS_OK\n");
		cec_transmit_done(smcec->adap, CEC_TX_STATUS_OK, 0, 0, 0, 0);
	}
#else
	struct cec_msg *txmsg = &smcec->txmsg;
	u32 event = 0;

	if(txstat->arbitration_lost) {
		dev_dbg(smcec->dev, "event set SDP_CEC_EVENT_TX_ARB_LOST\n");
		event |= SDP_CEC_EVENT_TX_ARB_LOST;
	} else if(txstat->noack) {
		dev_dbg(smcec->dev, "event set SDP_CEC_EVENT_TX_NACK\n");
		event |= SDP_CEC_EVENT_TX_NACK;
	} else if(txstat->low_drive) {
		dev_dbg(smcec->dev, "event set SDP_CEC_EVENT_TX_LOW_DRIVE\n");
		event |= SDP_CEC_EVENT_TX_LOW_DRIVE;
	} else {
		dev_dbg(smcec->dev, "event set SDP_CEC_EVENT_TX_OK\n");
		event |= SDP_CEC_EVENT_TX_OK;
	}

	dev_dbg(smcec->dev, "__process_tx_done call data=[%*ph]\n", txmsg->len, txmsg->msg);
	smcec->eventcb_fn(event, NULL, smcec->eventcb_args);
#endif

	return 0;
}

static int __process_rx_msg(struct sdp_mc_cecif_dev *smcec, struct cec_msg *rxmsg, bool is_errormsg) {

	if(is_errormsg) {
		return -EINVAL;
	}

#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
	cec_received_msg(smcec->adap, rxmsg);
#else
	dev_dbg(smcec->dev, "__process_rx_msg call data=[%*ph]\n", rxmsg->len, rxmsg->msg);
	smcec->eventcb_fn(SDP_CEC_EVENT_RX_DONE, rxmsg, smcec->eventcb_args);
#endif
	return 0;
}

/******************************** cec isr *************************************/
static irqreturn_t sdp_mc_cecif_irq_handler(int irq, void *priv)
{
	struct sdp_mc_cecif_dev *smcec = priv;
	u32 pend = cec_readl(smcec, CEC_INT);
	irqreturn_t irqret = IRQ_NONE;

	dev_dbg(smcec->dev, "interrupt occer. pend=0x%08x(%s%s%s%s%s%s%s)\n", pend,
		pend&CEC_INT_TX_DONE?"TX_DONE ":"",
		pend&CEC_INT_RX_DONE?"RX_DONE ":"",
		pend&CEC_INT_RX_HEADER_BUS_FREE?"HBUS_FREE ":"",
		pend&CEC_INT_RX_DATA_BUS_FREE?"DBUS_FREE ":"",
		pend&CEC_INT_RX_ACK_BUS_FREE?"ABUS_FREE ":"",
		pend&CEC_INT_RX_ERROR?"ERROR ":"",
		pend&CEC_INT_RX_NOACK?"NOACK ":""
	);


	/* RX Interrupt */
	if(pend & CEC_INT_RX_ALL) {
		if(pend & (CEC_INT_RX_DONE|CEC_INT_RX_NOACK)) {
			int fifocnt = 0;
			u32 rxfifo = 0;

			for(fifocnt = 0; fifocnt < CEC_MAX_MSG_SIZE*2; fifocnt++) {
				if(cec_readl(smcec, CEC_RXSTAT) & CEC_RXSTAT_EMPTY) {
					break;
				}
				rxfifo = cec_readl(smcec, CEC_RXFIFO);
				if(sdp_cecif_rx_ringbuf_enqueue(&smcec->rxring, rxfifo) < 0) {
					dev_err(smcec->dev, "rx ring is full! dropped byte %x\n", rxfifo);
				}
			}

			if(pend&CEC_INT_RX_NOACK) {
				dev_err(smcec->dev, "rx no-ack occurred! maybe rxfifo full.\n");
			}

			irqret = IRQ_WAKE_THREAD;
		} else {
			int fifocnt = 0;
			u8 errmsg[CEC_MAX_MSG_SIZE];
			u32 rxfifo;

			dev_err(smcec->dev, "RX Error! pend=0x%08x(%s%s%s%s%s)\n", pend,
				pend&CEC_INT_RX_HEADER_BUS_FREE?"HBUS_FREE ":"",
				pend&CEC_INT_RX_DATA_BUS_FREE?"DBUS_FREE ":"",
				pend&CEC_INT_RX_ACK_BUS_FREE?"ABUS_FREE ":"",
				pend&CEC_INT_RX_ERROR?"ERROR ":"",
				pend&CEC_INT_RX_NOACK?"NOACK ":""
			);

			for(fifocnt = 0; fifocnt < CEC_MAX_MSG_SIZE; fifocnt++) {
				if(cec_readl(smcec, CEC_RXSTAT) & CEC_RXSTAT_EMPTY) {
					break;
				}
				rxfifo = cec_readl(smcec, CEC_RXFIFO);
				if(rxfifo>>8) {
					dev_err(smcec->dev, "errmsg[%d]=0x%x(%s%s)\n", fifocnt, rxfifo,
						rxfifo&CEC_RXFIFO_HEAD?"HEAD ":"",
						rxfifo&CEC_RXFIFO_ERROR?"ERROR ":""
					);
				}
				errmsg[fifocnt] = CEC_RXFIFO_DATA(rxfifo);
			}
			dev_err(smcec->dev, "errmsg dump! len=%d, data=%*ph\n",
				fifocnt, fifocnt, errmsg);
		}

		cec_writel_bitset(smcec, CEC_INT_RX_CLR, CEC_INT);
	}


	/* TX Interrupt */
	if(pend & CEC_INT_TX_DONE) {
		const u32 tx_stat = cec_readl(smcec, CEC_TXSTAT);

		dev_dbg(smcec->dev, "tx done interrupt. tx_stat=0x%08x\n", tx_stat);

		if(!smcec->txstat.busy) {
			dev_err(smcec->dev, "invalid tx interrupt! tx is not busy! pend=0x%08x\n", pend);
		} else {
			if(tx_stat & CEC_TXSTAT_ERROR) {
				smcec->txstat.arbitration_lost = 1;
			}
			if(tx_stat & CEC_TXSTAT_NOACK) {
				smcec->txstat.noack = 1;
			}
			smcec->txstat.done = 1;
			irqret = IRQ_WAKE_THREAD;
		}

		cec_writel_bitset(smcec, CEC_INT_TX_CLR, CEC_INT);
	}

	return irqret;
}

static irqreturn_t sdp_mc_cecif_irq_handler_thread(int irq, void *priv)
{
	struct sdp_mc_cecif_dev *smcec = priv;
	struct cec_msg *rxmsg = &smcec->rxmsg;
	bool is_errormsg = false;
	u32 rxfifo = 0;

	if(smcec->txstat.busy && smcec->txstat.done) {
		del_timer(&smcec->tx_timeout_timer);
		if(smcec->txstat.arbitration_lost) {
			/* dump for debug */
			//SDP_CEC_LineLevelDump(cec_res);
		}
		__process_tx_done(smcec, &smcec->txstat);
		memset(&smcec->txstat, 0x0, sizeof(smcec->txstat));
	}



	/* parse fifo data to msg */
	rxmsg->len = 0;
	while(sdp_cecif_rx_ringbuf_dequeue(&smcec->rxring, &rxfifo) == 0) {
		if(rxmsg->len == 0) {
			if(!(rxfifo & CEC_RXFIFO_HEAD)) {
				is_errormsg = true;
				dev_err(smcec->dev, "first data is not header! raw_rxfifo=0x%08x\n",
					rxfifo);
			}
		} else {
			if(rxfifo & CEC_RXFIFO_HEAD) {
				/* new msg starting, process current msg */
				dev_dbg(smcec->dev, "current msg parse done! msg len=%d, data=%*ph\n",
					rxmsg->len, rxmsg->len, rxmsg->msg);
				__process_rx_msg(smcec, rxmsg, is_errormsg);
				rxmsg->len = 0;
				is_errormsg = false;
			}
		}

		if(rxfifo & CEC_RXFIFO_ERROR) {
			is_errormsg = true;
		}
		rxmsg->msg[rxmsg->len++] = CEC_RXFIFO_DATA(rxfifo);
	}

	if(rxmsg->len > 0) {
		dev_dbg(smcec->dev, "all msg parse done! msg len=%d, data=%*ph\n",
			rxmsg->len, rxmsg->len, rxmsg->msg);
		__process_rx_msg(smcec, rxmsg, is_errormsg);
	}

	return IRQ_HANDLED;
}

static void sdp_mc_cecif_txtimeout_timercb(unsigned long data)
{
	struct sdp_mc_cecif_dev *smcec = (struct sdp_mc_cecif_dev *)data;
	struct cec_msg *txmsg = &smcec->txmsg;
	uint32_t txstat = 0;
	uint32_t now_ms = (uint32_t)div64_u64(__get_nsecs(), NSEC_PER_MSEC);

	if(!smcec->txstat.busy) {
		dev_err(smcec->dev, "not busy!! INT=0x%08x, CTRL=0x%08x, TX_STAT=0x%08x, busy=%d\n",
			cec_readl(smcec, CEC_INT), cec_readl(smcec, CEC_CTRL), cec_readl(smcec, CEC_TXSTAT), smcec->txstat.busy);
	}

	//disable_irq_nosync(smcec->irq);

	dev_verbose(smcec->dev, "timer enter INT=0x%08x, CTRL=0x%08x, TX_STAT=0x%08x, busy=%d\n",
		cec_readl(smcec, CEC_INT), cec_readl(smcec, CEC_CTRL), cec_readl(smcec, CEC_TXSTAT), smcec->txstat.busy);

	txstat = cec_readl(smcec, CEC_TXSTAT);

	if(CEC_TXSTAT_POINTER(txstat) == txmsg->len) {
		if((smcec->tx_start_ms + SDP_CEC_TX_START_WAIT_TIME_MS) < now_ms) {
			dev_err(smcec->dev, "Tx Line idle detect SW timeout %ums! tx_msg->len=%u, TX_STAT=0x%08x(0x%08x), start %ums, now %ums\n",
				SDP_CEC_TX_START_WAIT_TIME_MS,
				txmsg->len, txstat, cec_readl(smcec, CEC_TXSTAT), smcec->tx_start_ms, now_ms);
			dev_err(smcec->dev, "CEC Tx Idle Timeout Dump len=%u data=%*ph",
				txmsg->len, txmsg->len, txmsg->msg);

			goto l_hwreset_and_txabort;
		} else {
			dev_verbose(smcec->dev, "timer wait line idle start %ums, wait %ums\n", smcec->tx_start_ms, now_ms - smcec->tx_start_ms);
		}
	} else if(CEC_TXSTAT_POINTER(txstat) < txmsg->len) {
		int8_t cur_tx_cnt = txmsg->len - CEC_TXSTAT_POINTER(txstat) - (txstat&CEC_TXSTAT_BUSY?1:0);

		/* line get, in xmit */
		if(smcec->tx_cnt == -1) {
			dev_verbose(smcec->dev, "timer first timeout, update tx_cnt=%d\n", cur_tx_cnt);
			smcec->tx_cnt = cur_tx_cnt;
		} else {
			/* check tx stuck */
			if(smcec->tx_cnt == cur_tx_cnt) {
				dev_verbose(smcec->dev, "timer tx stuck tx_cnt=%d\n", cur_tx_cnt);

				dev_err(smcec->dev, "Tx SW timeout %ums(%ums, %ums)! tx_msg->len=%u, tx_cnt=%u, TX_STAT=0x%08x(0x%08x), start %ums, now %ums\n",
					max(smcec->tx_ideal_time_ms, smcec->tx_timeout_ms),
					smcec->tx_ideal_time_ms, smcec->tx_timeout_ms,
					txmsg->len, smcec->tx_cnt, txstat, cec_readl(smcec, CEC_TXSTAT), smcec->tx_start_ms, now_ms);
				dev_err(smcec->dev, "CEC Tx Timeout Dump len=%u data=%*ph",
					txmsg->len, txmsg->len, txmsg->msg);

				goto l_hwreset_and_txabort;
			} else {
				dev_verbose(smcec->dev, "timer tx in progress tx_cnt=%d\n", cur_tx_cnt);
				smcec->tx_cnt = cur_tx_cnt;
			}
		}

		dev_verbose(smcec->dev, "timer restart(%ums) INT=0x%08x, CTRL=0x%08x, TX_STAT=0x%08x(0x%08x), RX_STAT=0x%08x\n",
				SDP_CEC_TX_POLLING_INERVAL_MS,
				cec_readl(smcec, CEC_INT), cec_readl(smcec, CEC_CTRL), txstat, cec_readl(smcec, CEC_TXSTAT), cec_readl(smcec, CEC_RXSTAT));
	} else {
		/* ERROR!! */
	}

	/* restart timeout timer */
	mod_timer(&smcec->tx_timeout_timer, jiffies + msecs_to_jiffies(SDP_CEC_TX_POLLING_INERVAL_MS));

	dev_verbose(smcec->dev, "timer end INT=0x%08x, CTRL=0x%08x, TX_STAT=0x%08x, busy=%d\n",
			cec_readl(smcec, CEC_INT), cec_readl(smcec, CEC_CTRL), cec_readl(smcec, CEC_TXSTAT), smcec->txstat.busy);

	//enable_irq(smcec->irq);

	return;

l_hwreset_and_txabort:
	__cecif_hwreset(smcec);

	smcec->txstat.arbitration_lost = true;
	smcec->txstat.done = true;
	irq_wake_thread(smcec->irq, smcec);
}



/*************************** sdp cec lowlevel API *****************************/
int sdp_mc_cecif_enable(struct sdp_mc_cecif_dev *smcec, bool enable)
{
	dev_dbg(smcec->dev, "adapter %s->%s\n",
		smcec->is_enabled?"enable":"disable", enable?"enable":"disable");

	if(smcec->is_enabled == enable) {
		dev_info(smcec->dev, "already %s\n", enable?"enabled":"disabled");
		return 0;
	}

	if (enable) {
			if(!(cec_readl(smcec, CEC_CTRL)&CEC_CRTL_RX_ENABLE) ||
				(cec_readl(smcec, CEC_INT)&(CEC_INT_RX_INT_EN|CEC_INT_TX_INT_EN))) {
				dev_err(smcec->dev, "can't get ownership! currently "
					"using by micom!(ctrl=0x%08x, int=0x%08x)\n",
					cec_readl(smcec, CEC_CTRL), cec_readl(smcec, CEC_INT));
				return -EBUSY;
			}

			smcec->is_enabled = true;

			__cecif_save_config_regs(smcec);

			cec_writel_bitset(smcec, CEC_INT_RX_CLR | CEC_INT_TX_CLR, CEC_INT);
			cec_writel_bitset(smcec, CEC_INT_RX_INT_EN | CEC_INT_TX_INT_EN, CEC_INT);
			enable_irq(smcec->irq);
	} else {
			smcec->is_enabled = false;

			disable_irq(smcec->irq);
			cec_writel_bitset(smcec, CEC_INT_RX_CLR | CEC_INT_TX_CLR, CEC_INT);
			cec_writel_bitclr(smcec, CEC_INT_RX_INT_EN | CEC_INT_TX_INT_EN, CEC_INT);

			memset(&smcec->txstat, 0x0, sizeof(smcec->txstat));
	}

	return 0;
}

int sdp_mc_cecif_log_addr(struct sdp_mc_cecif_dev *smcec, u8 addr)
{
	u32 ctrl_save = 0;

	if(cec_readl(smcec, CEC_TXSTAT) & CEC_TXSTAT_BUSY) {
		dev_err(smcec->dev, "HW is Busy\n");
		return -EBUSY;
	}

	if(cec_readl(smcec, CEC_RXSTAT) & CEC_RXSTAT_BUSY) {
		dev_err(smcec->dev, "RX STAT is Busy\n");
		return -EBUSY;
	}

	dev_dbg(smcec->dev, "set log addr %x\n", addr);

	ctrl_save = cec_readl(smcec, CEC_CTRL);
	cec_writel_bitclr(smcec, CEC_CRTL_RX_ENABLE, CEC_CTRL);
	cec_writel_bitclr(smcec, CEC_CRTL_LOGICAL_ADDR_MASK, CEC_CTRL);
	cec_writel_bitset(smcec, CEC_CRTL_LOGICAL_ADDR(addr), CEC_CTRL);
	if(ctrl_save & CEC_CRTL_RX_ENABLE) {
		cec_writel_bitset(smcec, CEC_CRTL_RX_ENABLE, CEC_CTRL);
	}

	return 0;
}

int sdp_mc_cecif_transmit(struct sdp_mc_cecif_dev *smcec, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	int i;

	if(smcec->txstat.busy) {
		dev_err(smcec->dev, "Tx is Busy\n");
		return -EBUSY;
	}

	if(cec_readl(smcec, CEC_TXSTAT) & CEC_TXSTAT_BUSY) {
		dev_err(smcec->dev, "HW is Busy\n");
		return -EBUSY;
	}

	if(msg->len <= 0 || msg->len > SDP_CEC_MAX_MSG_SIZE) {
		dev_err(smcec->dev, "msg size inval %d\n", msg->len);
		return -EINVAL;
	}

	smcec->txstat.busy = 1;

	dev_dbg(smcec->dev, "tx msg len=%d, data=%*ph\n", msg->len, msg->len, msg->msg);

	for(i = 0; i < msg->len; i++) {
		cec_writel(smcec, CEC_TXFIFO_DATA(msg->msg[i]), CEC_TXFIFO);
	}

	cec_writel_bitset(smcec, CEC_CRTL_SEND_MESSAGE, CEC_CTRL);

	smcec->txmsg = *msg;/* deep copy cec msg */

	smcec->tx_start_ms = (uint32_t)div64_u64(__get_nsecs(), NSEC_PER_MSEC);
	smcec->tx_cnt = -1;
	smcec->tx_ideal_time_ms = 4 + (msg->len * 30);// start 4ms + (freme 27.5ms + margin 2.5ms) * n
	smcec->tx_timeout_ms = msg->timeout;

	mod_timer(&smcec->tx_timeout_timer,
		jiffies + msecs_to_jiffies(max(smcec->tx_ideal_time_ms, smcec->tx_timeout_ms)));

	return 0;
}

#ifndef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
static int __mc_cecif_match_device_node(struct device *dev, void *data)
{
	return dev->of_node == (struct device_node *)data;
}

static struct sdp_mc_cecif_dev *__mc_cecif_dev_lookup_by_device_node(struct device_node *node)
{
	struct device *dev;
	struct syscon *syscon;

	dev = driver_find_device(&sdp_mc_cecif_pdrv.driver, NULL, (void *)node,
				 __mc_cecif_match_device_node);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	return dev_get_drvdata(dev);
}

int sdp_mc_cecif_of_device_attach(struct device *dev, sdp_mc_cecif_cb eventcb_fn,
						void *eventcb_args, struct sdp_mc_cecif_dev **out_smcec)
{
	struct device_node *node = NULL;
	struct sdp_mc_cecif_dev *smcec = NULL;

	if(!dev || !eventcb_fn || !out_smcec) {
		pr_err("sdp_mc_cecif_of_device_init: invalid args!"
			"(dev=%p, eventcb_fn=%p, out_smcec=%p)\n", dev, eventcb_fn, out_smcec);
		return -EINVAL;
	}

	node = of_parse_phandle(dev->of_node, "samsung,mc-cecif", 0);
	if(!node) {
		dev_err(smcec->dev, "can not get phandle(samsung,mc-cecif)\n");
		return -ENODEV;
	}

	smcec = __mc_cecif_dev_lookup_by_device_node(node);
	if(IS_ERR(smcec)) {
		dev_err(smcec->dev, "can not get cecif device\n");
		return PTR_ERR(smcec);
	}

	if(smcec->attached_dev) {
		dev_err(smcec->dev, "device is already attached!\n");
		return -EBUSY;
	}

	smcec->attached_dev = dev;
	smcec->eventcb_fn = eventcb_fn;
	smcec->eventcb_args = eventcb_args;
	*out_smcec = smcec;

	dev_info(smcec->dev, "%s is attached\n", dev_name(dev));

	return 0;
}

EXPORT_SYMBOL(sdp_mc_cecif_enable);
EXPORT_SYMBOL(sdp_mc_cecif_log_addr);
EXPORT_SYMBOL(sdp_mc_cecif_transmit);
EXPORT_SYMBOL(sdp_mc_cecif_of_device_attach);
#endif


#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
/***************************** cec adap API ***********************************/
static int sdp_mc_cecif_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct sdp_mc_cecif_dev *smcec = cec_get_drvdata(adap);

	return sdp_mc_cecif_enable(smcec, enable);
}

static int sdp_mc_cecif_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct sdp_mc_cecif_dev *smcec = cec_get_drvdata(adap);

	return sdp_mc_cecif_log_addr(smcec, addr);
}

static int sdp_mc_cecif_adap_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	struct sdp_mc_cecif_dev *smcec = cec_get_drvdata(adap);

	return sdp_mc_cecif_transmit(smcec, attempts,
				 signal_free_time, msg);
}

static const struct cec_adap_ops sdp_mc_cecif_adap_ops = {
	.adap_enable = sdp_mc_cecif_adap_enable,
	.adap_log_addr = sdp_mc_cecif_adap_log_addr,
	.adap_transmit = sdp_mc_cecif_adap_transmit,
};

static int sdp_mc_cecif_register_adapter(struct sdp_mc_cecif_dev *smcec) {
	int ret = 0;

	smcec->adap = cec_allocate_adapter(&sdp_mc_cecif_adap_ops, smcec,
		dev_name(smcec->dev),
		CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT |
		CEC_CAP_PASSTHROUGH | CEC_CAP_RC, 1);
	ret = PTR_ERR_OR_ZERO(smcec->adap);
	if (ret)
		return ret;

	ret = cec_register_adapter(smcec->adap, smcec->dev);
	if (ret)
		goto err_delete_adapter;

	dev_info(smcec->dev, "CEC adapter has successfully registered.\n");
	return 0;

err_delete_adapter:
	cec_delete_adapter(smcec->adap);
	return ret;
}
#endif

/****************************** debugfs ***************************************/
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static void sdp_mc_cecif_add_debugfs(struct device *dev)
{
	struct sdp_mc_cecif_dev *smcec = NULL;
	struct dentry *root;

	smcec = dev_get_drvdata(dev);

	root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	smcec->dbgfs_root = root;

	return;

err_root:
	smcec->dbgfs_root = NULL;
	return;
}
#endif


/***************************** micom syscall API ******************************/
extern int sdp_mc_ctrl_syscall_get_version(u8 target, u16 *api, u16 *drv);

#define VERSION_MAJOR_MINOR(ma, mi)	( (((ma)&0xFF)<<8) | ((mi)&0xFF) )

static int sdp_mc_cecif_check_compatible(struct platform_device *pdev) {
	int ret;
	u16 api = 0, drv = 0;

	ret = sdp_mc_ctrl_syscall_get_version(3, &api, &drv);

	if(ret < 0) {
		dev_err(&pdev->dev, "cat not get micom driver version!!\n");
		return ret;
	}

	if(api < VERSION_MAJOR_MINOR(0, 2) || drv < VERSION_MAJOR_MINOR(0, 13)) {
		dev_err(&pdev->dev, "micom cecif drv version is lower!!"
			"(api %.4x, drv %.4x)\n", api, drv);
		return -ENOTSUPP;
	}

	return 0;
}


/***************************** platform device ********************************/
static int sdp_mc_cecif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct resource *res;
	struct sdp_mc_cecif_dev *smcec;
	int ret;

	ret = sdp_mc_cecif_check_compatible(pdev);
	if(ret < 0) {
		return -ENOTSUPP;
	}

	smcec = devm_kzalloc(&pdev->dev, sizeof(*smcec), GFP_KERNEL);
	if (!smcec)
		return -ENOMEM;

	platform_set_drvdata(pdev, smcec);
	smcec->dev = dev;
	sdp_cecif_rx_ringbuf_init(&smcec->rxring);
	setup_timer(&smcec->tx_timeout_timer,
		sdp_mc_cecif_txtimeout_timercb, (unsigned long)smcec);

	smcec->regmap_mccmu = 
		syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "samsung,mccmu");
	if(IS_ERR_OR_NULL(smcec->regmap_mccmu)) {
		dev_err(dev, "can not get syscon phandle \'samsung,mccmu\'\n");
		return PTR_ERR(smcec->regmap_mccmu);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	smcec->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(smcec->regbase))
		return PTR_ERR(smcec->regbase);

	/* pend clear */
	cec_writel_bitset(smcec, CEC_INT_RX_CLR | CEC_INT_TX_CLR, CEC_INT);

	smcec->irq = platform_get_irq(pdev, 0);
	if (smcec->irq < 0)
		return smcec->irq;

	ret = devm_request_threaded_irq(dev, smcec->irq, sdp_mc_cecif_irq_handler,
		sdp_mc_cecif_irq_handler_thread, 0, pdev->name, smcec);
	if (ret)
		return ret;

	disable_irq(smcec->irq);

#ifdef CONFIG_SDP_MICOM_CECIF_SUPPORT_SUBSYSTEM
	ret = sdp_mc_cecif_register_adapter(smcec);
	if(ret < 0) {
		dev_err(dev, "regiter cec adapter fail(%d)\n", ret);
		return ret;
	}
#endif

#ifdef CONFIG_DEBUG_FS
	sdp_mc_cecif_add_debugfs(dev);
#endif

	dev_info(dev, "successfully probed(instance=%p, iobase=%p, irq=%d)\n",
		smcec, smcec->regbase, smcec->irq);
	return 0;
}

static int sdp_mc_cecif_remove(struct platform_device *pdev)
{
	return -ENOTSUPP;
}

static const struct of_device_id sdp_mc_cecif_match[] = {
	{
		.compatible	= "samsung,sdp-mc-cecif",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sdp_mc_cecif_match);

static struct platform_driver sdp_mc_cecif_pdrv = {
	.probe	= sdp_mc_cecif_probe,
	.remove	= sdp_mc_cecif_remove,
	.driver	= {
		.name		= CEC_NAME,
		.of_match_table	= sdp_mc_cecif_match,
	},
};

static int __init sdp_mc_cecif_init (void)
{
	pr_info("%s: module init. version %s\n", sdp_mc_cecif_pdrv.driver.name, VERSION_STR);
	return platform_driver_register(&sdp_mc_cecif_pdrv);	
}

static void __exit sdp_mc_cecif_exit(void)
{
	platform_driver_unregister(&sdp_mc_cecif_pdrv);
}
module_init(sdp_mc_cecif_init);
module_exit(sdp_mc_cecif_exit);


MODULE_AUTHOR("Dongseok Lee <drain.lee@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung SDP Micom CECIF driver");
