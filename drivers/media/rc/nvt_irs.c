#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/circ_buf.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>

#define NVT_IRS_DRV_NAME "nvt-irs"

#define IRS_DBG pr_debug

/* register offset definition */
#define IRS_INT_EN				0X00
#define IRS_INT_FLAG			0x04
#define IRS_CODELENGTH			0x08
#define IRS_CONTROLBYTE			0x0c
#define IRS_PULSE_HEADER		0x10
#define IRS_SPACE_HEADER		0x14
#define IRS_PULSESPACE_LOGIC0	0x18
#define IRS_PULSESPACE_LOGIC1	0x1c
#define IRS_BIPHASE				0x20
#define IRS_PULSE_GLITCH		0x24
#define IRS_PULSE_REPEAT		0x28
#define IRS_SPACE_REPEAT		0x2c
#define IRS_FIFO				0x30
#define IRS_FIFO_FLAG			0x34
#define IRS_SYS_ENVE_CLK_DIV	0x38
#define IRS_TM_INT_EN			0x3c
#define IRS_TM_INT_FLAG			0x40
#define IRS_TM_COUNTER			0x44
#define IRS_TM_TIMEOUT			0x48
#define IRS_TM_FIFORST			0x4c
#define IRS_TM_THRESHOLD		0x50
#define IRS_RESERVED_0X54		0x54
#define IRS_RESERVED_0X58		0x58
#define IRS_RESERVED_0X5C		0x5c
#define IRS_TM_SET_FIFO_FULL	0x60
#define IRS_TM_SET_FIFO_EMPTY	0x64
#define IRS_TM_SET_FIFO_HALF	0x68
#define IRS_IRQ_EN_FLAG			0x6c
#define IRS_SEL_PAD				0x70
#define IRS_FIFO01_D_CNT		0x74
#define IRS_RW_PTR				0x78
#define IRS_FIFOCNT_WPTR		0x7c
#define IRS_IRQ_R_PTR_CHG_EN	0x80

/* define setting bits */
#define IRSIRQ_EN (1<<22)
#define IRSIRQ_ENOUGH_PULSE (1<<0)
#define IRSIRQ_TIMEOUT (1<<1)
#define IRSIRQ_ALL_TM_IRQ (IRSIRQ_ENOUGH_PULSE | IRSIRQ_TIMEOUT)
#define IRSFIFO_RST (1<<16)
#define EN_TM_MODE (1<<4)

/* define clk settings */
/* The unit of fifo data = 1/ (irs_sys_clk/IRS_ENVE_RESO_DIV) = ?us */
#define IRS_SYS_CLK_DIV 1 /* clk src = 10MHz, this DIV gen 10MHz */
#define IRS_ENVE_RESO_DIV (0xa-1) /* 1/(10MHz/10) = 1us, settings need -1*/
#define IRS_SYS_ENVE_CLK ((IRS_SYS_CLK_DIV << 16) | IRS_ENVE_RESO_DIV)

/* implement ring buf */
#define NVT_IRS_RINGBUF_SIZE 512
u32 _to_usr[NVT_IRS_RINGBUF_SIZE];
struct nvt_irs_buf {
	spinlock_t rx_lock;
	unsigned long head;
	unsigned long tail;
	int qu_state;

	wait_queue_head_t wq;
	struct {
		u32 buf[1];
	} data[NVT_IRS_RINGBUF_SIZE];
};

static void nvt_irs_ringbuf_init(struct nvt_irs_buf *ringbuf)
{
	spin_lock_init(&ringbuf->rx_lock);
	ringbuf->qu_state = 0;
	ringbuf->head = 0;
	ringbuf->tail = 0;

	init_waitqueue_head(&ringbuf->wq);
}

static u32 nvt_irs_ringbuf_cnt(struct nvt_irs_buf *ringbuf)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->rx_lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	tail = ACCESS_ONCE(ringbuf->tail);
	ret = CIRC_CNT(head, tail, NVT_IRS_RINGBUF_SIZE);

	spin_unlock_irqrestore(&ringbuf->rx_lock, flags);

	return ret;
}

static int nvt_irs_ringbuf_enqu(struct nvt_irs_buf *ringbuf, u32 data)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;

	if (data == 0)
		pr_err("[%s]data=0\n", __func__);

	spin_lock_irqsave(&ringbuf->rx_lock, flags);

	tail = ACCESS_ONCE(ringbuf->tail);
	if (CIRC_SPACE(ringbuf->head, tail, NVT_IRS_RINGBUF_SIZE) >= 1) {
		ringbuf->data[ringbuf->head].buf[0] = data;
		ringbuf->head = (ringbuf->head + 1) & (NVT_IRS_RINGBUF_SIZE-1);
	} else {
		pr_err("[%s] no buf. drop data=%08d\n", __func__, data);
		ringbuf->qu_state = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->rx_lock, flags);
	return ret;
}

static int nvt_irs_ringbuf_dequ(struct nvt_irs_buf *ringbuf, u32 *out_data)
{
	int ret = 0;
	u32 cnt;
	unsigned long flags;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->rx_lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	if (CIRC_CNT(head, ringbuf->tail, NVT_IRS_RINGBUF_SIZE) >= 1) {
		memcpy(out_data, ringbuf->data[ringbuf->tail].buf, 4*1);
		ringbuf->tail = (ringbuf->tail + 1) & (NVT_IRS_RINGBUF_SIZE-1);
	} else {
		ret = -ENODATA;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->rx_lock, flags);
	return ret;
}
/* end of implement ring buf */

struct nvt_irs_dev {
	struct device *dev;
	void __iomem *reg;
	u32 irq;
	struct nvt_irs_buf ringbuf;
};

irqreturn_t nvt_irs_irq(int irq, void *dev_id)
{
	struct nvt_irs_dev *irs = dev_id;
	irqreturn_t irqret = IRQ_NONE;
	u32 irq_flag, irq_en, fifo_cnt;
	u32 i = 0;

	irq_flag = readl(irs->reg + IRS_TM_INT_FLAG);
	writel(irq_flag, irs->reg + IRS_TM_INT_FLAG);

	/* Read data in HW buffer */
	fifo_cnt = readl(irs->reg + IRS_TM_FIFORST);
	fifo_cnt &= 0x7ff;
	while (fifo_cnt--)
		nvt_irs_ringbuf_enqu(&irs->ringbuf,
				readl(irs->reg + IRS_TM_COUNTER));


	/*check int statue */
	irq_en = readl(irs->reg + IRS_TM_INT_EN);
	if ((irq_en & IRSIRQ_TIMEOUT) && (irq_flag & IRSIRQ_TIMEOUT))
		writel(IRSFIFO_RST, irs->reg + IRS_TM_FIFORST);
	else if (irq_flag & IRSIRQ_ENOUGH_PULSE)
		writel(irq_en | IRSIRQ_TIMEOUT, irs->reg + IRS_TM_INT_FLAG);

	irqret = IRQ_HANDLED;
	wake_up_interruptible(&irs->ringbuf.wq);
	return irqret;
}

static int nvt_irs_open(struct inode *inode, struct file *file)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(file);
	u32 reg_val;

	IRS_DBG("[%s]%d\n", __func__, __LINE__);

	writel(0, irs->reg + IRS_INT_EN);
	writel(0x3000, irs->reg + IRS_TM_TIMEOUT);
	writel(0x1, irs->reg + IRS_TM_THRESHOLD);
	writel(IRSFIFO_RST, irs->reg + IRS_TM_FIFORST);
	writel(IRSIRQ_ALL_TM_IRQ, irs->reg + IRS_TM_INT_FLAG);
	writel(IRSIRQ_ALL_TM_IRQ | EN_TM_MODE, irs->reg + IRS_TM_INT_EN);
	writel(IRSIRQ_EN, irs->reg + IRS_IRQ_EN_FLAG);
	return 0;
}

static ssize_t nvt_irs_write(struct file *file, const char *buf,
			  size_t n, loff_t *ppos)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(file);

	dev_err(irs->dev, "[%s]%d, Not Support\n", __func__, __LINE__);
	return -EINVAL;
}

static long nvt_irs_ioctl(struct file *filep,
			unsigned int cmd, unsigned long arg)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(filep);

	dev_err(irs->dev, "[%s]%d, Not Support\n", __func__, __LINE__);
	return -EINVAL;
}

ssize_t nvt_irs_read(struct file *file,
			  char __user *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(file);
	int ret = 0;
	u32 _irraw = 0, i = 0;

	if (wait_event_interruptible(irs->ringbuf.wq,
				nvt_irs_ringbuf_cnt(&irs->ringbuf)))
		return -ERESTARTSYS;

	while ((nvt_irs_ringbuf_dequ(&irs->ringbuf, &_irraw) >= 0))
		_to_usr[i++] = _irraw;

	if (copy_to_user((void __user *)buffer,
				_to_usr, (sizeof(_to_usr[0])*i)))
		return -EFAULT;

	/* check fifo overflow */
	if (irs->ringbuf.qu_state) {
		ret = irs->ringbuf.qu_state;
		irs->ringbuf.qu_state = 0;
	}

	return ret ? ret : i;
}

static unsigned int nvt_irs_poll(struct file *file, poll_table *wait)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(file);

	dev_err(irs->dev, "[%s]%d, Not Support\n", __func__, __LINE__);
	return -EINVAL;
}

static int nvt_irs_release(struct inode *inode, struct file *file)
{
	struct nvt_irs_dev *irs = lirc_get_pdata(file);

	dev_notice(irs->dev, "[%s]\n", __func__);
	return 0;
}

/* this is for driver init apis */
static const struct of_device_id nvt_irs_match[] = {
	{
		.compatible = "nvt,nvt-irs",
	},
	{},
};
MODULE_DEVICE_TABLE(of, nvt_irs_match);

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.write		= nvt_irs_write,
	.unlocked_ioctl	= nvt_irs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nvt_irs_ioctl,
#endif
	.read		= nvt_irs_read,
	.poll		= nvt_irs_poll,
	.open		= nvt_irs_open,
	.release	= nvt_irs_release,
};

static void _nvt_irs_hw_init(struct nvt_irs_dev *irs)
{
	writel(IRS_SYS_ENVE_CLK, irs->reg + IRS_SYS_ENVE_CLK_DIV);

	pr_info("[%s]IRS use STBC_IR pin\n", __func__);
	writel(0x1, irs->reg + IRS_SEL_PAD);
}

static int nvt_irs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lirc_driver *drv;
	struct nvt_irs_dev *irs;
	struct resource *r;
	struct device_node *np = NULL;
	int ret;

	drv = devm_kzalloc(dev, sizeof(struct lirc_driver), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	irs = devm_kzalloc(dev, sizeof(struct nvt_irs_dev), GFP_KERNEL);
	if (!irs)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "%s get io resrc fail!\n", __func__);
		return -ENXIO;
	}

	/* init private data */
	irs->dev = dev;
	irs->reg = ioremap_nocache(r->start, resource_size(r));
	if (!irs->reg) {
		dev_err(dev, "%s remap nvt irs fail!\n", __func__);
		return -ENOMEM;
	}

	/* register IRQ */
	np = of_find_node_by_name(NULL, "nvt_irs");
	if (np == NULL) {
		dev_err(dev, "find node fail\n");
		return -ENXIO;
	}

	irs->irq = irq_of_parse_and_map(np, 0);
	ret = request_irq(irs->irq, nvt_irs_irq,
			IRQF_TRIGGER_HIGH, "nvt_irs", irs);
	if (ret)
		dev_err(dev, "request_irq err %d'!!\n", ret);

	/* init irs para and register as lirc */
	snprintf(drv->name, sizeof(drv->name), NVT_IRS_DRV_NAME);
	drv->minor = 1;
	drv->code_length = 1;
	drv->data = irs;
	drv->fops = &lirc_fops;
	drv->dev = &pdev->dev;
	drv->owner = THIS_MODULE;
	drv->minor = lirc_register_driver(drv);
	if (drv->minor < 0) {
		dev_err(dev, ": lirc_register_driver failed: %d\n",
		       drv->minor);
		return drv->minor;
	}

	platform_set_drvdata(pdev, drv);

	nvt_irs_ringbuf_init(&irs->ringbuf);
	_nvt_irs_hw_init(irs);
	return 0;
}

static int nvt_irs_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irs_dev *irs = drv->data;

	disable_irq(irs->irq);
	free_irq(irs->irq, irs);

	IRS_DBG("[%s] minor = %d\n", __func__, drv->minor);
	ret = lirc_unregister_driver(drv->minor);
	if (ret)
		return ret;

	/* TODO : free memory ?? */

	return 0;
}

static int nvt_irs_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irs_dev *irs = drv->data;

	disable_irq(irs->irq);
	return 0;
}

static int nvt_irs_resume(struct platform_device *pdev)
{
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irs_dev *irs = drv->data;

	enable_irq(irs->irq);
	_nvt_irs_hw_init(irs);
	return 0;
}

struct platform_driver nvt_irs_platform_driver = {
	.probe		= nvt_irs_probe,
	.remove		= nvt_irs_remove,
	.suspend	= nvt_irs_suspend,
	.resume		= nvt_irs_resume,
	.driver		= {
		.name	= NVT_IRS_DRV_NAME,
		.of_match_table = of_match_ptr(nvt_irs_match),
	},
};
module_platform_driver(nvt_irs_platform_driver);

MODULE_DESCRIPTION("LIRC RX driver for NVT IRS HW");
MODULE_AUTHOR("Novatek");
MODULE_LICENSE("GPL");
