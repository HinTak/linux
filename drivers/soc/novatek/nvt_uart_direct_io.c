/* Novatek UART Direct I/O Driver */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/circ_buf.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "nvt_uart_direct_io.h"

#define DRIVER_NAME		"nvt-udio"
#define CHAR_NODE_NAME		"nvt_udio"
#define UDIO_DEV_MINORS		1
#define DEFAULT_BAUD_RATE	115200
#define RX_QUEUE_SIZE		0x1000 /* must be 2^n */
#define TX_DATA_SIZE		16
#define RX_DATA_SIZE		128
#define TX_TIMEOUT_MSEC		10

#define UDIODBG(args...) dev_dbg(args)

static dev_t udio_dev;
static struct class *udio_class;

struct uart_direct_io_rxqueue {
	spinlock_t lock;
	unsigned long head;
	unsigned long tail;

	char data[RX_QUEUE_SIZE];
};

struct uart_line_ctrl {
	u32 length;
	u32 stop_bit;
	u32 parity_enable;
	u32 parity_type;
	u32 stick_parity;
};

struct uart_direct_io_dev {
	struct device *dev;
	struct cdev cdev;
	void __iomem *io_base;
	unsigned int irq;
	int clk_rate;
	int baud_rate;
	struct uart_line_ctrl line_ctrl;
	u32 fcr_setting;

	bool used;
	pid_t used_pid;
	char used_comm[TASK_COMM_LEN];

	spinlock_t lock;
	struct semaphore sem;
	atomic_t read_available, write_available;
	wait_queue_head_t waitqueue;

	struct uart_direct_io_rxqueue rxqueue;
};

static u32 ioremap_read(u32 address)
{
	u32 *remap_address;
	u32 value;

	remap_address = ioremap(address, 0x4);

	if (!remap_address)
		return -1;

	value = readl(remap_address);
	iounmap(remap_address);

	return value;
}

static int ioremap_write(u32 address, u32 value)
{
	u32 *remap_address;

	remap_address = ioremap(address, 0x4);

	if (!remap_address)
		return -1;

	writel(value, remap_address);
	iounmap(remap_address);
	return 0;
}

/*
 * Ring buffer func for rx queue
 */
static void udio_rxq_init(struct uart_direct_io_rxqueue *rxq)
{
	spin_lock_init(&rxq->lock);
	rxq->head = 0;
	rxq->tail = 0;
}

static void udio_rxq_reset(struct uart_direct_io_rxqueue *rxq)
{
	unsigned long flags;

	spin_lock_irqsave(&rxq->lock, flags);
	rxq->head = 0;
	rxq->tail = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
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
static int udio_rxq_enqueue(struct uart_direct_io_rxqueue *rxq,
			    const char *data, int bytes)
{
	unsigned long flags;
	unsigned long tail;
	int i = 0;
	int space = 0;

	spin_lock_irqsave(&rxq->lock, flags);

	tail = ACCESS_ONCE(rxq->tail);
	space = CIRC_SPACE(rxq->head, tail, RX_QUEUE_SIZE);
	bytes = min(bytes, space);

	for (i = 0; i < bytes; i++) {
		rxq->data[rxq->head] = data[i];
		rxq->head = (rxq->head + 1) & (RX_QUEUE_SIZE-1);
	}

	spin_unlock_irqrestore(&rxq->lock, flags);

	return bytes;
}

/* return num of copied bytes */
static int udio_rxq_dequeue(struct uart_direct_io_rxqueue *rxq, char *data,
			    int bytes)
{
	unsigned long flags;
	unsigned long head;
	int i = 0;
	int count = 0;

	spin_lock_irqsave(&rxq->lock, flags);

	head = ACCESS_ONCE(rxq->head);
	count = CIRC_CNT(head, rxq->tail, RX_QUEUE_SIZE);
	bytes = min(bytes, count);

	for (i = 0; i < bytes; i++) {
		data[i] = rxq->data[rxq->tail];
		rxq->tail = (rxq->tail + 1) & (RX_QUEUE_SIZE-1);
	}

	spin_unlock_irqrestore(&rxq->lock, flags);

	return bytes;
}

static int udio_rx_chars(u32 lsr, struct uart_direct_io_dev *udiodev, char *buf,
			 size_t count) {
	int i = 0;

	for (i = 0; i < count; i++) {
		if (lsr & UART_LSR_DR) {
			buf[i] = (char)(UDIO_RD(UART_RBR) & UART_RBR_RX);
			if (lsr & UART_LSR_BRK_ERROR_BITS) {
				if (lsr & UART_LSR_OE)
					dev_err(udiodev->dev,
						"RX - overrun Error\n");
				if (lsr & UART_LSR_PE)
					dev_err(udiodev->dev,
						"RX - parity Error\n");
				if (lsr & UART_LSR_FE)
					dev_err(udiodev->dev,
						"RX - frame Error\n");
				if (lsr & UART_LSR_BI)
					dev_err(udiodev->dev,
						"RX - break Error\n");
			}
		} else {
			return i;
		}

		lsr = UDIO_RD(UART_LSR);
	}

	return count;
}

static int udio_tx_chars(struct uart_direct_io_dev *udiodev, const char *buf,
			 size_t count, size_t tx_completed)
{
	int i;
	size_t this_count;
	u32 lsr;

	this_count = min((count - tx_completed), (size_t)TX_DATA_SIZE);
	for (i = 0; i < this_count; i++)
		UDIO_WR(buf[tx_completed + i], UART_THR);

	return i;
}

static void udio_set_baud_rate(struct uart_direct_io_dev *udiodev,
			       int baud_rate)
{
	u32 div;

	div = (udiodev->clk_rate) / (baud_rate * 16);
	UDIO_WR_OR(UART_LCR_DLAB, UART_LCR);
	UDIO_WR(div, UART_DLAB0);
	UDIO_WR((div >> 8), UART_DLAB1);
	UDIO_WR_AND(UART_LCR_DLAB, UART_LCR);
}

/* clear all settings and set line ctrl */
static void udio_set_line_ctrl(struct uart_direct_io_dev *udiodev)
{
	UDIO_WR_AND(UART_LCR_ALL_SETTINGS, UART_LCR);

	UDIO_WR_OR(udiodev->line_ctrl.length, UART_LCR);
	UDIO_WR_OR(udiodev->line_ctrl.stop_bit, UART_LCR);
	if (udiodev->line_ctrl.parity_enable) {
		UDIO_WR_OR(udiodev->line_ctrl.parity_enable, UART_LCR);
		UDIO_WR_OR(udiodev->line_ctrl.parity_type, UART_LCR);
		UDIO_WR_OR(udiodev->line_ctrl.stick_parity, UART_LCR);
	}
}

static ssize_t udio_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *f_pos)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	size_t rx_count = udio_rxq_count(&udiodev->rxqueue);
	char *rx_buf;

	if (!atomic_dec_and_test(&udiodev->read_available)) {
		atomic_inc(&udiodev->read_available);
		return -EBUSY;
	}

	rx_buf = (char *)kmalloc(count * sizeof(char), GFP_KERNEL);
	if (!rx_buf) {
		dev_err(udiodev->dev, "%s - kmalloc() is failed.\n", __func__);
		atomic_inc(&udiodev->read_available);
		return -ENOMEM;
	}

	if (rx_count == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			atomic_inc(&udiodev->read_available);
			kfree(rx_buf);
			return -EAGAIN;
		}

		/* wait rx data */
		UDIODBG(udiodev->dev, "\"%s\" reading - going to sleep\n",
			current->comm);

		if (wait_event_interruptible(udiodev->waitqueue,
		    udio_rxq_count(&udiodev->rxqueue) > 0)) {
			atomic_inc(&udiodev->read_available);
			kfree(rx_buf);
			return -ERESTARTSYS;
		}
	}

	rx_count = udio_rxq_count(&udiodev->rxqueue);

	UDIODBG(udiodev->dev, "\"%s\" reading - req count %d, rx_count %d\n",
		current->comm, count, rx_count);

	count = min(count, rx_count);
	if (!access_ok(VERIFY_WRITE, buf, count)) {
		dev_err(udiodev->dev, "%s - access_ok() is failed.\n",
			__func__);
		atomic_inc(&udiodev->read_available);
		kfree(rx_buf);
		return -EFAULT;
	}

	count = udio_rxq_dequeue(&udiodev->rxqueue, rx_buf, count);
	if (copy_to_user(buf, rx_buf, count)) {
		dev_err(udiodev->dev, "%s - copy_to_user() is failed.\n",
			__func__);
		atomic_inc(&udiodev->read_available);
		kfree(rx_buf);
		return -EFAULT;
	}

	atomic_inc(&udiodev->read_available);

	kfree(rx_buf);

	return count;
}

static ssize_t udio_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos)
{
	struct uart_direct_io_dev *udiodev = filp->private_data;
	size_t tx_completed = 0;
	char *tx_buf;
	u32 lsr;

	if (!atomic_dec_and_test(&udiodev->write_available)) {
		atomic_inc(&udiodev->write_available);
		return -EBUSY;
	}

	if (!access_ok(VERIFY_READ, buf, count)) {
		dev_err(udiodev->dev, "%s - access_ok() is failed.\n", __func__);
		atomic_inc(&udiodev->write_available);
		return -EFAULT;
	}

	tx_buf = (char *)kmalloc(count * sizeof(char), GFP_KERNEL);
	if (!tx_buf) {
		dev_err(udiodev->dev, "%s - kmalloc() is failed.\n", __func__);
		atomic_inc(&udiodev->write_available);
		return -ENOMEM;
	}

	if (copy_from_user(tx_buf, buf, count)) {
		dev_err(udiodev->dev, "%s - copy_from_user() is failed.\n",
			__func__);
		atomic_inc(&udiodev->write_available);
		kfree(tx_buf);
		return -EFAULT;
	}

	while (tx_completed < count) {
		lsr = UDIO_RD(UART_LSR);
		if ((lsr & UART_LSR_THRE) == 0) {
			if (filp->f_flags & O_NONBLOCK) {
				atomic_inc(&udiodev->write_available);
				kfree(tx_buf);
				return -EAGAIN;
			}

			/* wait tx data */
			UDIODBG(udiodev->dev,
				"\"%s\" writing - going to sleep\n",
				current->comm);

			if (!wait_event_interruptible_timeout(
			    udiodev->waitqueue,
			    (UDIO_RD(UART_LSR) & UART_LSR_THRE) > 0,
			    msecs_to_jiffies(TX_TIMEOUT_MSEC)))
				continue;
		}

		UDIODBG(udiodev->dev,
			"\"%s\" writing - req count %d, tx status %s\n",
			current->comm, count, ((lsr & UART_LSR_THRE) > 0 ?
			"completed" : "not completed"));

		tx_completed += udio_tx_chars(udiodev, tx_buf, count,
					      tx_completed);
		UDIODBG(udiodev->dev,
			"\"%s\" writing - count %d, tx_completed %d\n",
			current->comm, count, tx_completed);
	}

	atomic_inc(&udiodev->write_available);
	kfree(tx_buf);

	return count;
}

static int udio_open(struct inode *inode, struct file *filp)
{
	struct uart_direct_io_dev *udiodev =
	container_of(inode->i_cdev, struct uart_direct_io_dev, cdev);

	u32 ret, reg_val, ufcon = 0;

	/* set UART_C GPIO */
	reg_val = ioremap_read(UART_GPIO_REG);
	reg_val &= ~UART_GPIO_UART_C_CLEAR;
	reg_val |= UART_GPIO_UART_C;
	ret = ioremap_write(UART_GPIO_REG, reg_val);
	if (ret < 0) {
		dev_err(udiodev->dev, "ioremap() is failed\n");
		return -ENOMEM;
	}

	/* set INTERNAL_UART_CONNECT to PAD (disabled) */
	reg_val = ioremap_read(UART_CONNECT_REG);
	reg_val &= ~UART_CONNECT_ENABLE;
	ret = ioremap_write(UART_CONNECT_REG, reg_val);
	if (ret < 0) {
		dev_err(udiodev->dev, "ioremap() is failed\n");
		return -ENOMEM;
	}

	filp->private_data = udiodev;

	if (down_interruptible(&udiodev->sem))
		return -ERESTARTSYS;

	if (udiodev->used) {
		dev_warn(udiodev->dev,
			 "already using from process %.*s (pid: %d)\n",
			 TASK_COMM_LEN, udiodev->used_comm, udiodev->used_pid);
		up(&udiodev->sem);
		return -EBUSY;
	}

	udiodev->used = true;
	udiodev->used_pid = current->pid;
	memcpy(udiodev->used_comm, current->comm, TASK_COMM_LEN);

	udio_rxq_reset(&udiodev->rxqueue);

	/* fifo reset & fifo enable & set trigger level */
	UDIO_WR(udiodev->fcr_setting, UART_FCR);

	/* enable all irq */
	UDIO_WR_OR(UART_IER_ALL_IRQ, UART_IER);

	UDIODBG(udiodev->dev, "\"%s\" opened\n", current->comm);

	up(&udiodev->sem);

	return nonseekable_open(inode, filp); /* success */
}

static int udio_release(struct inode *inode, struct file *filp)
{
	struct uart_direct_io_dev *udiodev =
	container_of(inode->i_cdev, struct uart_direct_io_dev, cdev);

	if (down_interruptible(&udiodev->sem))
		return -ERESTARTSYS;

	UDIODBG(udiodev->dev, "\"%s\" closing. rxcount %d\n",
		current->comm, udio_rxq_count(&udiodev->rxqueue));

	/* disable all irq */
	UDIO_WR_AND(UART_IER_ALL_IRQ, UART_IER);

	filp->private_data = NULL;

	UDIODBG(udiodev->dev, "\"%s\" closed\n", current->comm);

	udiodev->used = false;

	up(&udiodev->sem);

	return 0;
}

static const struct file_operations udio_fops = {
	.owner =    THIS_MODULE,
	.read =     udio_read,
	.write =    udio_write,
	.llseek =   no_llseek,
	.open =     udio_open,
	.release =  udio_release,
};

static irqreturn_t udio_isr(int irq, void *data)
{
	struct uart_direct_io_dev *udiodev = data;
	unsigned long flags;
	u32 iir, lsr;

	iir = UDIO_RD(UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_NONE;

	spin_lock_irqsave(&udiodev->lock, flags);

	lsr = UDIO_RD(UART_LSR);
	if (lsr & UART_LSR_DR) {
		char rxdata[RX_DATA_SIZE + 1];
		int rxcount = 0, enqcount = 0;

		rxcount = udio_rx_chars(lsr, udiodev, rxdata, RX_DATA_SIZE);
		rxdata[rxcount] = '\0';

		enqcount = udio_rxq_enqueue(&udiodev->rxqueue, rxdata, rxcount);
		if (rxcount > enqcount)
			dev_err(udiodev->dev,
				"rx queue overflow! droped %d bytes\n",
				rxcount - enqcount);

		UDIODBG(udiodev->dev, "udio_isr RX handled(rxcount %d)\n",
			rxcount);

		wake_up_interruptible(&udiodev->waitqueue);
	} else if (lsr & UART_LSR_THRE) {
		UDIODBG(udiodev->dev, "udio_isr TX handled\n");

		wake_up_interruptible(&udiodev->waitqueue);
	}

	spin_unlock_irqrestore(&udiodev->lock, flags);

	return IRQ_HANDLED;
}

static int udio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uart_direct_io_dev *udiodev = NULL;
	struct resource *res = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct device *devnode = ERR_PTR(-EINVAL);
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
	if (IS_ERR(udiodev->io_base))
		return PTR_ERR(udiodev->io_base);
	dev_crit(dev, "io_base %p\n", udiodev->io_base);

	/* parse clk_rate */
	if (of_property_read_u32(dev->of_node, "clock-frequency",
	    &udiodev->clk_rate) < 0) {
		dev_err(dev, "can not get clk_rate\n");
	}
	dev_crit(dev, "clock rate %d\n", udiodev->clk_rate);

	/* linectrl default setting
	 * charaacter bit: 8, stop bit: 1, parity: disabled
	 */
	udiodev->line_ctrl.length = UART_LCR_LENGTH_8BIT;
	udiodev->line_ctrl.stop_bit = UART_LCR_STOP_BIT1;
	udiodev->line_ctrl.parity_enable = UART_LCR_PARITY_DISABLE;
	udio_set_line_ctrl(udiodev);

	/* set baud rate */
	if (of_property_read_u32(dev->of_node, "baud",
				 &udiodev->baud_rate) < 0) {
		/* set default baud rate */
		dev_crit(dev, "can not get baud rate from dts\n");
		udio_set_baud_rate(udiodev, DEFAULT_BAUD_RATE);
		udiodev->baud_rate = DEFAULT_BAUD_RATE;
	} else {
		/* set baud rate from dts */
		udio_set_baud_rate(udiodev, udiodev->baud_rate);
	}
	dev_crit(dev, "baud rate %d\n", udiodev->baud_rate);

	/* FCR default setting
	 * trigger level: 14, TX & RX FIFO reset, enable FIFO
	 */
	udiodev->fcr_setting = (UART_FCR_TRIGGER_LV_14 | UART_FCR_RX_TX_RESET |
				UART_FCR_FIFO_ENABLE);
	UDIO_WR(udiodev->fcr_setting, UART_FCR);

	/* disable all irq */
	UDIO_WR_AND(UART_IER_ALL_IRQ, UART_IER);

	/* parse IRQ */
	udiodev->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (udiodev->irq < 0) {
		dev_err(dev, "can not find IRQ\n");
		return -ENODEV;
	}
	dev_crit(dev, "irq %u\n", udiodev->irq);

	ret = devm_request_threaded_irq(dev, udiodev->irq, udio_isr,
					NULL, 0, pdev->name, udiodev);

	udiodev->dev = dev;
	platform_set_drvdata(pdev, udiodev);

	cdev_init(&udiodev->cdev, &udio_fops);
	udiodev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&udiodev->cdev, MKDEV(MAJOR(udio_dev), MINOR(udio_dev)),
		       1);
	if (ret < 0) {
		dev_err(dev, "cdev_add() is failed! ret %d\n", ret);
		return ret;
	}

	/* create dev node to /dev/ */
	devnode = device_create(udio_class, dev, udiodev->cdev.dev, NULL,
				CHAR_NODE_NAME);
	if (IS_ERR(devnode)) {
		dev_err(dev, "can not create device node \""CHAR_NODE_NAME
			"\"! ret %ld\n", PTR_ERR(devnode));
		return PTR_ERR(devnode);
	}

	dev_crit(dev, "probing completed\n");

	return 0;
}

static int udio_remove(struct platform_device *pdev)
{
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);

	/* disable all irq */
	UDIO_WR_AND(UART_IER_ALL_IRQ, UART_IER);

	device_destroy(udio_class, udiodev->cdev.dev);
	cdev_del(&udiodev->cdev);

	dev_crit(udiodev->dev, "removing completed\n");

	return 0;
}

static int udio_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);
	u32 lsr;

	lsr = UDIO_RD(UART_LSR);
	dev_crit(udiodev->dev, "suspending - rxcount: %d, tx status: %s\n",
		 udio_rxq_count(&udiodev->rxqueue),
		 ((lsr & UART_LSR_THRE) > 0 ? "completed" : "not completed"));

	dev_crit(udiodev->dev, "suspended\n");

	return 0;
}

static int udio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uart_direct_io_dev *udiodev = platform_get_drvdata(pdev);
	u32 lsr;

	lsr = UDIO_RD(UART_LSR);
	dev_crit(udiodev->dev, "resuming - rxcount: %d, tx status: %s\n",
		 udio_rxq_count(&udiodev->rxqueue),
		 ((lsr & UART_LSR_THRE) > 0 ? "completed" : "not completed"));

	/* disable all irq */
	UDIO_WR_AND(UART_IER_ALL_IRQ, UART_IER);

	/* restore line ctrl setting */
	udio_set_line_ctrl(udiodev);

	/* restore baud rate setting */
	udio_set_baud_rate(udiodev, udiodev->baud_rate);

	/* restore fcr setting */
	UDIO_WR(udiodev->fcr_setting, UART_FCR);

	dev_crit(udiodev->dev, "resumed\n");

	return 0;
}

static const struct dev_pm_ops udio_pm = {
	.suspend_noirq  = udio_suspend,
	.resume_noirq   = udio_resume,
};

static const struct of_device_id udio_match[] = {
	{.compatible = "nvt,udio-c"}
};
MODULE_DEVICE_TABLE(of, udio_match);

static struct platform_driver udio_driver = {
	.probe	= udio_probe,
	.remove	= udio_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.pm = &udio_pm,
		.of_match_table = udio_match
	}
};

static int __init nvt_udio_init(void)
{
	int ret;

	pr_crit("Novatek UART Direct I/O Driver Init.\n");

	ret = alloc_chrdev_region(&udio_dev, 0, UDIO_DEV_MINORS, DRIVER_NAME);
	if (ret < 0) {
		pr_err("%s - alloc_chrdev_region() is failed %d\n", __func__,
		       ret);
		return ret;
	}

	udio_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(udio_class)) {
		unregister_chrdev_region(udio_dev, UDIO_DEV_MINORS);
		pr_err("%s - cless_create() is failed %ld", __func__,
		       PTR_ERR(udio_class));
		return PTR_ERR(udio_class);
	}

	ret = platform_driver_register(&udio_driver);
	if (ret < 0) {
		class_destroy(udio_class);
		unregister_chrdev_region(udio_dev, UDIO_DEV_MINORS);
		pr_err("%s - platform driver register failed %d", __func__,
		       ret);
	}

	return ret;
}

static void __exit nvt_udio_exit(void)
{
	platform_driver_unregister(&udio_driver);
	class_destroy(udio_class);
	unregister_chrdev_region(udio_dev, UDIO_DEV_MINORS);
}

module_init(nvt_udio_init);
module_exit(nvt_udio_exit);

MODULE_AUTHOR("Novatek");
MODULE_DESCRIPTION("Novatek Direct UART I/O Driver");
MODULE_LICENSE("GPL");
