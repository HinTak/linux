/*
 * Copyright (C) 2018 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * SDP UART Direct I/O(non-tty) Driver. <drain.lee@samsung.com>
 */
//#define DEBUG//for debug

#define DRIVER_NAME	"sdp-udio"
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>//for kmalloc
#include <linux/uaccess.h>//for ioctl
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/circ_buf.h>
#include <asm/atomic.h>

#include <uapi/soc/sdp/sdp_uart_direct_io.h>
#include "sdp_uart_lld.h"

#define UDIODBG(args...) dev_dbg(args)

#define VERSION_STR	"20190513(first version)"

#define CHAR_NODE_NAME	"sdp_udio"
#define NR_DEV_MINORS 8
#define DEFAULT_BAUD_RATE	(115200)
#define RX_QUEUE_SIZE	0x1000//must be 2^n

#define UDIO_RD(off)			readl(udiodev->io_base + (off))
#define UDIO_WR(val, off)	writel((val), udiodev->io_base + (off))

static dev_t udio_dev_first;
static struct class *udio_class = NULL;

struct uart_direct_io_rxqueue {
	spinlock_t lock;
	unsigned long head;
	unsigned long tail;

	char data[RX_QUEUE_SIZE];
};

/* uart direct private data */
struct uart_direct_io_dev {
	struct device *dev;
	struct cdev cdev;
	void __iomem *io_base;
	int txirq, rxirq;
	unsigned long clk_rate;
	bool enhanced;
	u32 instid;
	int baudrate;
	struct sdp_udio_linectrl linectrl;

	bool used;
	pid_t used_pid;
	char used_comm[TASK_COMM_LEN];

	spinlock_t lock;
	struct semaphore sem;
	atomic_t read_available, write_available;
	wait_queue_head_t waitqueue;

	struct uart_direct_io_rxqueue rxqueue;

	/* suspend/resume */
	u32 saved_ucon, saved_ufcon;
};

static int lld_udio_set_linectrl(struct uart_direct_io_dev *udiodev, struct sdp_udio_linectrl *ctrl) {
	u32 ulcon = 0;

	switch(ctrl->parity_mode) {
	case PARITY_NONE: ulcon |= SDP_ULCON_PNONE;
		break;
	case PARITY_ODD: ulcon |= SDP_ULCON_PODD;
		break;
	case PARITY_EVEN: ulcon |= SDP_ULCON_PEVEN;
		break;
	case PARITY_FORCED_ONE: ulcon |= SDP_ULCON_PFORCEDONE;
		break;
	case PARITY_FORCED_ZERO: ulcon |= SDP_ULCON_PFORCEDZERO;
		break;
	default:
		return -EINVAL;
	}

	switch(ctrl->stop_bit) {
	case STOPBIT_1BIT: ulcon &= ~SDP_ULCON_STOPB;
		break;
	case STOPBIT_2BIT: ulcon |= SDP_ULCON_STOPB;
		break;
	default:
		return -EINVAL;
	}

	switch(ctrl->word_length) {
	case WORD_LENGTH_5BIT: ulcon |= SDP_ULCON_CS5;
		break;
	case WORD_LENGTH_6BIT: ulcon |= SDP_ULCON_CS6;
		break;
	case WORD_LENGTH_7BIT: ulcon |= SDP_ULCON_CS7;
		break;
	case WORD_LENGTH_8BIT: ulcon |= SDP_ULCON_CS8;
		break;
	default:
		return -EINVAL;
	}

	UDIO_WR(ulcon, SDP_ULCON);

	udiodev->linectrl = *ctrl;

	return 0;
}

static int lld_udio_set_baud_rate(struct uart_direct_io_dev *udiodev, int baud) {
	u32 ubrdiv = (udiodev->clk_rate / (baud / 10 * 16) - 5) / 10;

	if(ubrdiv < 1 || ubrdiv > 255 ) {
		return -EINVAL;
	}

	UDIO_WR(ubrdiv, SDP_UBRDIV);
	udiodev->baudrate = baud;

	return 0;
}

static bool lld_udio_is_txempty(struct uart_direct_io_dev *udiodev) {
	return ((UDIO_RD(SDP_UTRSTAT)&(SDP_UTRSTAT_TXE|SDP_UTRSTAT_TXFE))
		== (SDP_UTRSTAT_TXE|SDP_UTRSTAT_TXFE));
}

static size_t lld_udio_get_rxfifo_count(struct uart_direct_io_dev *udiodev) {
	u32 ufstat = UDIO_RD(SDP_UFSTAT);
	size_t count = 0;

	if(udiodev->enhanced) {
		count = (ufstat&SDP_UFSTAT2_RXFIFO_FULL)?
			0x40:(ufstat&SDP_UFSTAT2_RXFIFO_MASK);
	} else {
		count = (ufstat&SDP_UFSTAT_RXFIFO_FULL)?
			0x10:(ufstat&SDP_UFSTAT_RXFIFO_MASK);
	}

	return count;
}

static int lld_udio_get_rx_data(struct uart_direct_io_dev *udiodev, char *buf, size_t count) {
	int i = 0;
	u32 uerstat = 0;

	for(i = 0; i < count; i++) {
		if(lld_udio_get_rxfifo_count(udiodev) == 0) {
			return i;
		}
		uerstat = UDIO_RD(SDP_UERSTAT);
		buf[i] = (char)(UDIO_RD(SDP_URXH)&0xFF);

		if(uerstat) {
			dev_warn(udiodev->dev, "\tRx(0x%02x) error! (%s%s%s%s)\n", buf[i],
				uerstat&SDP_UERSTAT_OVERRUN?"Overrun ":"",
				uerstat&SDP_UERSTAT_PARITY?"Parity ":"",
				uerstat&SDP_UERSTAT_FRAME?"Frame ":"",
				uerstat&SDP_UERSTAT_BREAK?"Break ":"");
		}
	}

	return count;
}

static size_t lld_udio_get_txfifo_space(struct uart_direct_io_dev *udiodev) {
	u32 ufstat = UDIO_RD(SDP_UFSTAT);
	size_t space = 0;

	if(udiodev->enhanced) {
		space = (ufstat&SDP_UFSTAT2_TXFIFO_FULL)?
			0x40:((ufstat&SDP_UFSTAT2_TXFIFO_MASK) >> SDP_UFSTAT2_TXFIFO_SHIFT);
		space = 0x40 - space;
	} else {
		space = (ufstat&SDP_UFSTAT_TXFIFO_FULL)?
			0x10:((ufstat&SDP_UFSTAT_TXFIFO_MASK) >> SDP_UFSTAT_TXFIFO_SHIFT);
		space = 0x10 - space;
	}

	return space;
}

static int lld_udio_set_tx_data(struct uart_direct_io_dev *udiodev, const char *buf, size_t count) {
	int i = 0;

	for(i = 0; i < count; i++) {
		if(lld_udio_get_txfifo_space(udiodev) == 0) {
			return i;
		}
		UDIO_WR(buf[i], SDP_UTXH);
	}

	return count;
}

static void lld_udio_set_rx_interrupt(struct uart_direct_io_dev *udiodev,
	bool rx_irq, bool rx_timeout_irq, bool rx_error_irq) {
	u32 ucon = UDIO_RD(SDP_UCON);

	if(rx_irq) {
		ucon |= SDP_UCON_RXIRQMODE|SDP_UCON_RXIRQ2MODE;
	} else {
		ucon &= ~(SDP_UCON_RXIRQ2MODE);
	}

	if(rx_timeout_irq) {
		ucon |= SDP_UCON_RXFIFO_TOI;
	} else {
		ucon &= ~(SDP_UCON_RXFIFO_TOI);
	}

	if(rx_error_irq) {
		ucon |= SDP_UCON_ERRIRQEN;
	} else {
		ucon &= ~(SDP_UCON_ERRIRQEN);
	}

	UDIO_WR(ucon, SDP_UCON);
}

static void lld_udio_set_tx_interrupt(struct uart_direct_io_dev *udiodev,
	bool tx_irq) {
	u32 ucon = UDIO_RD(SDP_UCON);

	if(tx_irq) {
		ucon |= (SDP_UCON_TXIRQMODE|SDP_UCON_TXIRQ2MODE);
	} else {
		ucon &= ~(SDP_UCON_TXIRQ2MODE);
	}

	UDIO_WR(ucon, SDP_UCON);
}


/*
 * Ring buffer func for rx queue
 */
static void udio_rxq_init(struct uart_direct_io_rxqueue *rxq)
{
	spin_lock_init(&rxq->lock);
	rxq->head = 0;
	rxq->tail = 0;

	return;
}

static void udio_rxq_reset(struct uart_direct_io_rxqueue *rxq)
{
	unsigned long flags;

	spin_lock_irqsave(&rxq->lock, flags);
	rxq->head = 0;
	rxq->tail = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);

	return;
}

static u32 udio_rxq_count(struct uart_direct_io_rxqueue *rxq)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;
	unsigned long head;

	spin_lock_irqsave(&rxq->lock, flags);

	head = ACCESS_ONCE(rxq->head);
	tail = ACCESS_ONCE(rxq->tail);
	ret = CIRC_CNT(head, tail, RX_QUEUE_SIZE);
	spin_unlock_irqrestore(&rxq->lock, flags);

	return ret;
}

/* return num of copied bytes */
static int udio_rxq_enqueue(struct uart_direct_io_rxqueue *rxq, const char *data, int bytes)
{
	unsigned long flags;
	unsigned long tail;
	int i = 0;
	int space = 0;

	spin_lock_irqsave(&rxq->lock, flags);

	tail = ACCESS_ONCE(rxq->tail);
	space = CIRC_SPACE(rxq->head, tail, RX_QUEUE_SIZE);
	bytes = min(bytes, space);

	for(i = 0; i < bytes; i++) {
		rxq->data[rxq->head] = data[i];
		rxq->head = (rxq->head + 1) & (RX_QUEUE_SIZE-1);
	}

	spin_unlock_irqrestore(&rxq->lock, flags);

	return bytes;
}

/* return num of copied bytes */
static int udio_rxq_dequeue(struct uart_direct_io_rxqueue *rxq, char *data, int bytes)
{
	unsigned long flags;
	unsigned long head;
	int i = 0;
	int count = 0;

	spin_lock_irqsave(&rxq->lock, flags);

	head = ACCESS_ONCE(rxq->head);
	count = CIRC_CNT(head, rxq->tail, RX_QUEUE_SIZE);
	bytes = min(bytes, count);

	for(i = 0; i < bytes; i++) {
		data[i] = rxq->data[rxq->tail];
		rxq->tail = (rxq->tail + 1) & (RX_QUEUE_SIZE-1);
	}

	spin_unlock_irqrestore(&rxq->lock, flags);

	return bytes;
}

/*
 * Open and close
 */

static int udio_open(struct inode *inode, struct file *filp)
{
	struct uart_direct_io_dev *udiodev = container_of(inode->i_cdev, struct uart_direct_io_dev, cdev);
	u32 ufcon = 0;

	filp->private_data = udiodev; /* for other methods */

	if (down_interruptible(&udiodev->sem))
		return -ERESTARTSYS;

	if(udiodev->used) {
		dev_warn(udiodev->dev, "already using from process %.*s (pid: %d)\n",
			TASK_COMM_LEN, udiodev->used_comm, udiodev->used_pid);
		up(&udiodev->sem);
		return -EBUSY;
	}

	udiodev->used = true;
	udiodev->used_pid = current->pid;
	memcpy(udiodev->used_comm, current->comm, TASK_COMM_LEN);

	udio_rxq_reset(&udiodev->rxqueue);
	
	ufcon = UDIO_RD(SDP_UFCON);
	/* reset fifo */
	UDIO_WR(ufcon | (SDP_UFCON_RESETRX|SDP_UFCON_RESETTX), SDP_UFCON);

	/* enable RX/TX */
	UDIO_WR(SDP_UCON_TXIRQMODE|SDP_UCON_RXIRQMODE, SDP_UCON);

	lld_udio_set_rx_interrupt(udiodev, true, true, true);

	UDIODBG(udiodev->dev, "\"%s\" opened\n", current->comm);

	up(&udiodev->sem);

	return nonseekable_open(inode, filp);/* success */
}

static int udio_release(struct inode *inode, struct file *filp)
{
	struct uart_direct_io_dev *udiodev = container_of(inode->i_cdev, struct uart_direct_io_dev, cdev);

	if (down_interruptible(&udiodev->sem))
		return -ERESTARTSYS;

	UDIODBG(udiodev->dev, "\"%s\" closing. rxcount %d, txspace %d, utrstat 0x%08x\n",
		current->comm, udio_rxq_count(&udiodev->rxqueue),
		lld_udio_get_txfifo_space(udiodev), UDIO_RD(SDP_UTRSTAT));

	// /* wait for flush tx buf and tx shifter */
	// while(!lld_udio_is_txempty(udiodev)) {
	// 	if(msleep_interruptible(1)) {
	// 		up(&udiodev->sem);

	// 		return -ERESTARTSYS;
	// 	}
	// }

	/* disable all irq */
	UDIO_WR(0x0, SDP_UCON);
	UDIO_WR(SDP_UTRSTAT_RXI|SDP_UTRSTAT_TXI|SDP_UTRSTAT_ERRI, SDP_UTRSTAT);//pend clear

	filp->private_data = NULL;

	UDIODBG(udiodev->dev, "\"%s\" closed\n", current->comm);

	udiodev->used = false;

	up(&udiodev->sem);

	return 0;
}

/*
 * Data management: read and write
 */

static ssize_t udio_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	size_t rx_count = udio_rxq_count(&udiodev->rxqueue);

	if (!atomic_dec_and_test(&udiodev->read_available)) {
		atomic_inc(&udiodev->read_available);
		return -EBUSY;
	}

	if(rx_count == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			atomic_inc(&udiodev->read_available);
			return -EAGAIN;
		}

		//wait rx data
		UDIODBG(udiodev->dev, "\"%s\" reading: going to sleep\n", current->comm);

		if(wait_event_interruptible(udiodev->waitqueue, (rx_count = udio_rxq_count(&udiodev->rxqueue)) > 0)) {
			atomic_inc(&udiodev->read_available);
		 	return -ERESTARTSYS;
		}
	}

	UDIODBG(udiodev->dev, "\"%s\" reading: req count %d, rx_count %d\n", current->comm, count, rx_count);

	count = min(count, rx_count);
	if (!access_ok(VERIFY_WRITE, buf, count)) {
		atomic_inc(&udiodev->read_available);
		return -EFAULT;
	}

	count = udio_rxq_dequeue(&udiodev->rxqueue, buf, count);

	atomic_inc(&udiodev->read_available);
	return count;
}

static ssize_t udio_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	size_t tx_space = lld_udio_get_txfifo_space(udiodev);

	if (!atomic_dec_and_test(&udiodev->write_available)) {
		atomic_inc(&udiodev->write_available);
		return -EBUSY;
	}

	if(tx_space == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			atomic_inc(&udiodev->write_available);
			return -EAGAIN;
		}

		//wait tx data
		UDIODBG(udiodev->dev, "\"%s\" writing: going to sleep\n", current->comm);

		lld_udio_set_tx_interrupt(udiodev, true);

		if(wait_event_interruptible(udiodev->waitqueue, (tx_space = lld_udio_get_txfifo_space(udiodev)) > 0)) {
			atomic_inc(&udiodev->write_available);
		 	return -ERESTARTSYS;
		}
	}

	UDIODBG(udiodev->dev, "\"%s\" writing: req count %d, tx_space %d\n", current->comm, count, tx_space);

	count = min(count, tx_space);
	if (!access_ok(VERIFY_READ, buf, count)) {
		atomic_inc(&udiodev->write_available);
		return -EFAULT;
	}

	count = lld_udio_set_tx_data(udiodev, buf, count);

	atomic_inc(&udiodev->write_available);
	return count;
}

static unsigned int udio_poll(struct file *filp, poll_table *wait)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	unsigned int mask = 0;

	if (!atomic_dec_and_test(&udiodev->read_available)) {
		atomic_inc(&udiodev->read_available);
		return -EBUSY;
	}

	if (!atomic_dec_and_test(&udiodev->write_available)) {
		atomic_inc(&udiodev->read_available);
		atomic_inc(&udiodev->write_available);
		return -EBUSY;
	}

	// lld_udio_set_tx_interrupt(udiodev, true);

	if(udio_rxq_count(&udiodev->rxqueue) == 0) {
		UDIODBG(udiodev->dev, "\"%s\" polling: going to wait\n", current->comm);

		poll_wait(filp, &udiodev->waitqueue, wait);
	}

	UDIODBG(udiodev->dev, "\"%s\" polling: event raised! rxcount %d, txspace %d\n",
		current->comm, udio_rxq_count(&udiodev->rxqueue),
		lld_udio_get_txfifo_space(udiodev));

	if(udio_rxq_count(&udiodev->rxqueue) > 0) {
		mask |= POLLIN | POLLRDNORM;	/* readable */
	}

	if(lld_udio_get_txfifo_space(udiodev) > 0) {
		mask |= POLLOUT | POLLWRNORM;	/* writable */
	}

	atomic_inc(&udiodev->read_available);
	atomic_inc(&udiodev->write_available);

	UDIODBG(udiodev->dev, "\"%s\" polling: done mask 0x%08x\n", current->comm, mask);

	return mask;
}


static int udio_flush(struct file *filp, fl_owner_t id)
{

	struct uart_direct_io_dev *udiodev = filp->private_data;

	if (down_interruptible(&udiodev->sem))
		return -ERESTARTSYS;

	UDIODBG(udiodev->dev, "\"%s\" flushing. rxcount %d, txspace %d, utrstat 0x%08x\n",
		current->comm, udio_rxq_count(&udiodev->rxqueue),
		lld_udio_get_txfifo_space(udiodev), UDIO_RD(SDP_UTRSTAT));

	/* wait for flush tx buf and tx shifter */
	while(!lld_udio_is_txempty(udiodev)) {
		if(msleep_interruptible(1)) {
			up(&udiodev->sem);

			return -ERESTARTSYS;
		}
	}

	UDIODBG(udiodev->dev, "\"%s\" flushed\n", current->comm);

	up(&udiodev->sem);

	return 0;
}
/*
 * The ioctl() implementation
 */

static long udio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	int err = 0, tmp;
	int retval = 0;

	if (!atomic_dec_and_test(&udiodev->read_available)) {
		atomic_inc(&udiodev->read_available);
		return -EBUSY;
	}

	if (!atomic_dec_and_test(&udiodev->write_available)) {
		atomic_inc(&udiodev->read_available);
		atomic_inc(&udiodev->write_available);
		return -EBUSY;
	}

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SDP_UDIO_IOC_MAGIC) {
		retval = -ENOTTY;
		goto ioctl_out;
	}
	if (_IOC_NR(cmd) > SDP_UDIO_IOC_MAXNR) {
		retval = -ENOTTY;
		goto ioctl_out;
	}

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) {
		retval = -EFAULT;
		goto ioctl_out;
	}

	switch(cmd) {

	case SDP_UDIO_IOCRESET:
	{
		retval = -ENOSYS;
		break;
	}

	case SDP_UDIO_IOCT_APIVERSION:
	{
		retval = SDP_UDIO_API_VERSION;
		break;
	}

	case SDP_UDIO_IOCS_LINECTRL:
	{
		struct sdp_udio_linectrl *ctrl = (void *)arg;

		retval = lld_udio_set_linectrl(udiodev, ctrl);
		break;
	}

	case SDP_UDIO_IOCG_LINECTRL:
	{
		struct sdp_udio_linectrl *ctrl = (void *)arg;

		*ctrl = udiodev->linectrl;
		break;
	}


	case SDP_UDIO_IOCT_BAUDRATE:
	{
		retval = lld_udio_set_baud_rate(udiodev, arg);
		break;
	}

	case SDP_UDIO_IOCQ_BAUDRATE:
	{
		retval = udiodev->baudrate;
		break;
	}

	case SDP_UDIO_IOCT_LOOPBACK:
	{
		u32 ucon = UDIO_RD(SDP_UCON);

		if(arg) {
			ucon |= SDP_UCON_LOOPBACK;
		} else {
			ucon &= ~SDP_UCON_LOOPBACK;
		}
		UDIO_WR(ucon, SDP_UCON);
		break;
	}

	case SDP_UDIO_IOCQ_LOOPBACK:
	{
		retval = !!(UDIO_RD(SDP_UCON)&SDP_UCON_LOOPBACK);
		break;
	}

	case SDP_UDIO_IOCT_HWQSIZE:
	{
		retval = udiodev->enhanced ? 64 : 16;
		break;
	}

	case SDP_UDIO_IOCT_TXQSIZE:
	{
		retval = 0;//0 is queue is unavailable
		break;
	}

	case SDP_UDIO_IOCT_RXQSIZE:
	{
		retval = RX_QUEUE_SIZE;
		break;
	}


	default:  /* redundant, as cmd was checked against MAXNR */
		retval = -EINVAL;
		break;
	}

ioctl_out:
	atomic_inc(&udiodev->read_available);
	atomic_inc(&udiodev->write_available);

	return retval;
}


struct file_operations udio_fops = {
	.owner =    THIS_MODULE,
	.read =     udio_read,
	.write =    udio_write,
	.poll =     udio_poll,
	.flush =   udio_flush,
	.unlocked_ioctl = udio_ioctl,
	.llseek =   no_llseek,
	.open =     udio_open,
	.release =  udio_release,
};

static irqreturn_t udio_isr(int irq, void* data)
{
	struct uart_direct_io_dev *udiodev = data;
	u32 utrstat = UDIO_RD(SDP_UTRSTAT);

	UDIODBG(udiodev->dev, "udio_isr %d(%s) TX/RX Status 0x%08x\n", irq,
		irq == udiodev->txirq?"Tx":(irq == udiodev->rxirq?"Rx":"??"), utrstat);

	if((utrstat&(SDP_UTRSTAT_TXI|SDP_UTRSTAT_RXI|SDP_UTRSTAT_ERRI)) == 0) {
		return IRQ_NONE;
	}

	if(irq == udiodev->txirq) {

		UDIODBG(udiodev->dev, "udio_isr TX handled(txspace %d)\n",
				lld_udio_get_txfifo_space(udiodev));

		//lld_udio_set_tx_interrupt(udiodev, false);
		UDIO_WR(SDP_UTRSTAT_TXI, SDP_UTRSTAT);//clear

		//wake_up_poll(&udiodev->waitqueue, POLLOUT | POLLWRNORM);
		wake_up_interruptible(&udiodev->waitqueue);

	} else if(irq == udiodev->rxirq) {
		if(utrstat & SDP_UTRSTAT_RXI) {
			char rxdata[64];
			int rxcount = 0, enqcount = 0;

			rxcount = lld_udio_get_rx_data(udiodev, rxdata, ARRAY_SIZE(rxdata));
			enqcount = udio_rxq_enqueue(&udiodev->rxqueue, rxdata, rxcount);
			if(rxcount > enqcount) {
				dev_err(udiodev->dev, "rx queue overflow! droped %dbytes\n",
					rxcount - enqcount);
			}
			UDIODBG(udiodev->dev, "udio_isr RX handled(rxcount %d)\n", rxcount);
		}

		if(utrstat & SDP_UTRSTAT_ERRI) {
			UDIODBG(udiodev->dev, "udio_isr RXERR handled\n");
		}

		UDIO_WR(SDP_UTRSTAT_RXI|SDP_UTRSTAT_ERRI, SDP_UTRSTAT);//clear

		//wake_up_poll(&udiodev->waitqueue, POLLIN | POLLRDNORM);
		wake_up_interruptible(&udiodev->waitqueue);
	} else {
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int udio_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	struct uart_direct_io_dev *udiodev = NULL;
	struct resource *res = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct device *devnode = ERR_PTR(-EINVAL);
	struct sdp_udio_linectrl linectrl;
	int ret = 0;

	udiodev = devm_kzalloc(dev, sizeof(*udiodev), GFP_KERNEL);
	if (!udiodev)
		return -ENOMEM;

	spin_lock_init(&udiodev->lock);
	sema_init(&udiodev->sem, 1);
	atomic_set(&udiodev->read_available, 1);
	atomic_set(&udiodev->write_available, 1);
	init_waitqueue_head(&udiodev->waitqueue);

	udio_rxq_init(&udiodev->rxqueue);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	udiodev->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(udiodev->io_base)) {
		return PTR_ERR(udiodev->io_base);
	}
	UDIODBG(dev, "udiodev->io_base %p\n", udiodev->io_base);

	clk = devm_clk_get(dev, "apb_pclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	udiodev->clk_rate = clk_get_rate(clk);
	if(udiodev->clk_rate == 0) {
		return -EINVAL;
	}
	UDIODBG(dev, "udiodev->clk_rate %lu\n", udiodev->clk_rate);

	if(of_get_property(dev->of_node, "enhanced", NULL))	{
		udiodev->enhanced = true;
	}

	//TODO: uart init

	/* default setting */
	linectrl.parity_mode = PARITY_NONE;
	linectrl.stop_bit = STOPBIT_1BIT;
	linectrl.word_length = WORD_LENGTH_8BIT;
	lld_udio_set_linectrl(udiodev, &linectrl);


	/* set baud rate */
	if(of_property_read_u32(dev->of_node, "samsung,udio-baudrate", &udiodev->baudrate) < 0) {
		lld_udio_set_baud_rate(udiodev, DEFAULT_BAUD_RATE);
	} else {
		if(lld_udio_set_baud_rate(udiodev, udiodev->baudrate) < 0) {
			dev_warn(dev, "baudrate %dHz is not supported! "
				"using default boudrate.\n", udiodev->baudrate);

			lld_udio_set_baud_rate(udiodev, DEFAULT_BAUD_RATE);
		}
	}
	UDIODBG(dev, "udiodev->baudrate %d\n", udiodev->baudrate);

	/* disable all irq */
	UDIO_WR(0x0, SDP_UCON);

	/* fifo reset & fifo enable */
	UDIO_WR(SDP_UFCON_RESETTX|SDP_UFCON_RESETRX|SDP_UFCON_FIFOMODE, SDP_UFCON);
	UDIO_WR(SDP_UTRSTAT_RXI|SDP_UTRSTAT_TXI|SDP_UTRSTAT_ERRI, SDP_UTRSTAT);//pend clear

	if(udiodev->enhanced) {
		u32 ufcon = UDIO_RD(SDP_UFCON);

		UDIO_WR(ufcon | SDP_UFCON_ENHANCED, SDP_UFCON);
		ufcon = UDIO_RD(SDP_UFCON);
		if(!(ufcon & SDP_UFCON_ENHANCED)) {
			udiodev->enhanced = false;
			dev_warn(dev, "enhanced mode is not supported! using legacy mode.\n");
		}
	}

	if(udiodev->enhanced) {
		u32 ufcon = UDIO_RD(SDP_UFCON);

		UDIO_WR(ufcon | (4<<8), SDP_UFCON);//TX
		UDIO_WR(60<<8, SDP_UMCON);//RX
	} else {
		u32 ufcon = UDIO_RD(SDP_UFCON);

		UDIO_WR(ufcon | (SDP_UFCON_RXTRIG12|SDP_UFCON_TXTRIG4), SDP_UFCON);
	}

	udiodev->txirq = irq_of_parse_and_map(dev->of_node, 0);
	if (udiodev->txirq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;	
	}

	ret = devm_request_threaded_irq(dev, udiodev->txirq, udio_isr,
			NULL, 0, pdev->name, udiodev);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq for tx return %d\n", ret);
		return ret;
	}
	UDIODBG(dev, "udiodev->txirq %d\n", udiodev->txirq);

	udiodev->rxirq = irq_of_parse_and_map(dev->of_node, 1);
	if (udiodev->rxirq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;	
	}

	ret = devm_request_threaded_irq(dev, udiodev->rxirq, udio_isr,
			NULL, 0, pdev->name, udiodev);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq for rx return %d\n", ret);
		return ret;
	}
	UDIODBG(dev, "udiodev->rxirq %d\n", udiodev->rxirq);

	udiodev->dev = dev;
	platform_set_drvdata(pdev, udiodev);


	
	if(of_property_read_u32(dev->of_node, "samsung,udio-instid", &udiodev->instid) < 0) {
		dev_err(dev, "can not found property \"samsung,udio-instid\"!\n");
		return -EINVAL;
	}
	UDIODBG(dev, "udiodev->instid %d\n", udiodev->instid);

	cdev_init(&udiodev->cdev, &udio_fops);
	udiodev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&udiodev->cdev, MKDEV(MAJOR(udio_dev_first), MINOR(udio_dev_first) + udiodev->instid), 1);

	if(ret < 0) {
		dev_err(dev, "cdev add failed! instid %d ret %d\n", udiodev->instid, ret);
		return ret;
	}

	/* create dev node to /dev/ */
	devnode = device_create(udio_class, dev, udiodev->cdev.dev, NULL, CHAR_NODE_NAME"%u", udiodev->instid);
	if(IS_ERR(devnode)) {
		dev_err(dev, "can not create device node \""CHAR_NODE_NAME"%u\"! ret %ld\n", udiodev->instid, PTR_ERR(devnode));
		return PTR_ERR(devnode);
	}

	dev_info(dev, "Probing in %s mode completed.\n", udiodev->enhanced?"enhanced":"legacy");
	
	return 0;
}

static int udio_remove(struct platform_device *pdev) {
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);

	/* disable all irq */
	UDIO_WR(0x0, SDP_UCON);
	UDIO_WR(SDP_UTRSTAT_RXI|SDP_UTRSTAT_TXI|SDP_UTRSTAT_ERRI, SDP_UTRSTAT);//pend clear

	device_destroy(udio_class, udiodev->cdev.dev);
	cdev_del(&udiodev->cdev);
	/* ... */

	kfree(udiodev);

	return 0;
}

static int udio_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);

	UDIODBG(udiodev->dev, "susepnding. rxcount %d, txspace %d, utrstat 0x%08x\n",
		udio_rxq_count(&udiodev->rxqueue),
		lld_udio_get_txfifo_space(udiodev), UDIO_RD(SDP_UTRSTAT));

	udiodev->saved_ucon = UDIO_RD(SDP_UCON);
	udiodev->saved_ufcon = UDIO_RD(SDP_UFCON);

	return 0;
}

static int udio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);

	UDIODBG(udiodev->dev, "resuming. rxcount %d, txspace %d, utrstat 0x%08x\n",
		udio_rxq_count(&udiodev->rxqueue),
		lld_udio_get_txfifo_space(udiodev), UDIO_RD(SDP_UTRSTAT));

	/* disable all irq */
	UDIO_WR(0x0, SDP_UCON);
	UDIO_WR(SDP_UTRSTAT_RXI|SDP_UTRSTAT_TXI|SDP_UTRSTAT_ERRI, SDP_UTRSTAT);//pend clear

	/* restore setting */
	lld_udio_set_linectrl(udiodev, &udiodev->linectrl);
	lld_udio_set_baud_rate(udiodev, udiodev->baudrate);

	/* restore fifo setting & reset fifo */
	UDIO_WR(udiodev->saved_ufcon | (SDP_UFCON_RESETRX|SDP_UFCON_RESETTX), SDP_UFCON);

	/* restore, RX/TX and IRQ */
	UDIO_WR(udiodev->saved_ucon, SDP_UCON);

	return 0;
}

static struct dev_pm_ops udio_pm = {
	.suspend_noirq	= udio_suspend,
	.resume_noirq	= udio_resume,
};

static const struct of_device_id udio_match[] = {
	{.compatible = "samsung,sdp-uart-direct-io"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, udio_match);

static struct platform_driver udio_driver = {
	.probe	= udio_probe,
	.remove	= udio_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.pm	= &udio_pm,
		.of_match_table = udio_match,
	},
};

#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
static int udio_get_smack64_label(struct device *dev, char* buf, int size){
	snprintf(buf, size, "%s", "*");
	return 0;
}
static char * udio_devnode(struct device *dev, umode_t *mode)
{
	if ( !mode ) {
		pr_err("mode is NULL\n" );
		return NULL;
	}

	*mode = 0666;// mode = rw-rw-rw-
	return NULL;    
}
#endif/*CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL*/


static int __init mod_udio_init(void)
{
	int ret;

	pr_info("module init. version %s\n", VERSION_STR);

	ret = alloc_chrdev_region(&udio_dev_first, 0, NR_DEV_MINORS, DRIVER_NAME);
	if(ret < 0) {
		pr_err("alloc_chrdev_region() failed %d\n", ret);
		return ret;
	}

	udio_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(udio_class)) {
		unregister_chrdev_region(udio_dev_first, NR_DEV_MINORS);
		pr_err("mod_udio_init() cless create failed %ld", PTR_ERR(udio_class));
		return PTR_ERR(udio_class);
	}

#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	udio_class->devnode = udio_devnode;
	udio_class->get_smack64_label = udio_get_smack64_label;
#endif/*CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL*/
	
	ret = platform_driver_register(&udio_driver);
	if (ret < 0) {
		class_destroy(udio_class);
		unregister_chrdev_region(udio_dev_first, NR_DEV_MINORS);
		pr_err("mod_udio_init() platform driver register failed %d", ret);
	}

	return ret;
}
module_init(mod_udio_init);

static void __exit mod_udio_exit(void)
{
	platform_driver_unregister(&udio_driver);
	class_destroy(udio_class);
	unregister_chrdev_region(udio_dev_first, NR_DEV_MINORS);
}
module_exit(mod_udio_exit);

MODULE_AUTHOR("Dongseok Lee, <drain.lee@samsung.com>");
MODULE_DESCRIPTION("SDP UART Direct I/O(non-tty) Driver");
MODULE_LICENSE("GPL");
