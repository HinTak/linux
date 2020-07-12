#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/poll.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>
#include "../../gpio/gpio-nvt.h"

#define NVT_IRB_DRV_NAME "nvt-irb"
#define IRB_OUT_PIN EN_KER_GPIO_GPA_15
#define WBUF_LEN 2048

#define IRB_DBG pr_debug

typedef void (*nvt_irb_callback_func)(int signal_status);

static int g_signal_in;
static int g_carr_mode;
static wait_queue_head_t in_wq;
struct nvt_irb_dev {
	struct device *dev;
	void __iomem *reg;
	u32 irq;
	wait_queue_head_t out_wq;
	unsigned int freq;		/* carrier frequency */
	unsigned int duty;	/* carrier duty cycle */
	int wbuf[WBUF_LEN];
	int wptr;
	int Modu;
	int Invt;
	int IdleSt;
};

/* timing define */
#define EN_MODU (1<<17)
#define EN_INVT (1<<18)
#define EN_IDLEST (1<<19)
#define POLAR_BIT (1<<23)

/* buf define */
#define IRBBUF_FULL (1<<2)
#define IRBBUF_AL_FULL (1<<3)
#define IRBBUF_HALF (1<<4)
#define IRBBUF_AL_EMTY (1<<5)
#define IRBBUF_EMTY (1<<6)
#define IRBBUF_THRESHOLD (1<<7)

/* nvt irb register */
#define IRB_INTEN          0x00
#define IRB_INT_FIFOST     0x04
#define IRB_SETFIFOFULL    0x08
#define IRB_SETFIFOETY     0x0c
#define IRB_SETFIFOHALF    0x10
#define IRB_RESERVED0X14   0x14
#define IRB_RESERVED0X18   0x18
#define IRB_RESERVED0X1C   0x1c
#define IRB_RESERVED0X20   0x20
#define IRB_RESERVED0X24   0x24
#define IRB_FIFO0          0x28
#define IRB_FIFO1          0x2c
#define IRB_TRIG_SET_OUT   0x30
#define IRB_SETFIFOPTR     0x34
#define IRB_OUTADDR        0x38
#define IRB_TRIGGER_IRB    0x3c
#define IRB_CARCLKDIV      0x40
#define IRB_CARCLKDUTY     0x44
#define IRB_ENVRESL_SYSCLK 0x48

#define IRB_SYSCLK_DIV 1 /* clk src is 100MHz, divide to 100MHz */
#define IRB_ENVE_RESO 100 /* envelope resolution set to 1MHz for unit=1us */
#define IRB_CARR_CLK 100000000 /* IRB clk is 100MHz */
#define IRB_CARR_CLK_MAX 1000000 /* IRB clk max is 1MHz */

irqreturn_t nvt_irb_isr(int irq, void *dev_id)
{
	struct nvt_irb_dev *irb = dev_id;
	irqreturn_t irqret = IRQ_NONE;
	u32 _st_reg, _irq_st, _fifo_num = 0;

	_st_reg = readl(irb->reg + IRB_INT_FIFOST);
	writel(_st_reg, irb->reg + IRB_INT_FIFOST);
	_irq_st = (_st_reg & 0x000001ff);
	_fifo_num = ((_st_reg & 0x0fff0000)>>16);

	if (_st_reg & IRBBUF_EMTY) {
		IRB_DBG("IRB FIFO empty\n");
		irb->wptr = -1;
		wake_up_interruptible(&irb->out_wq);
	}

	IRB_DBG("[irb_irg:%08x]FIFOnum=%08x\n", _irq_st, _fifo_num);
	irqret = IRQ_HANDLED;
	return irqret;
}

static int nvt_irb_timing_init(struct nvt_irb_dev *irb)
{
	u32 _freq_div, _duty_cal;

	IRB_DBG("[%s]%d\n", __func__, __LINE__);

	if (irb->freq == 0 || irb->freq > IRB_CARR_CLK_MAX)
		return -EINVAL;

	if (irb->duty < 20 || irb->duty > 60)
		return -EINVAL;

	/* init carrier freq and duty */
	_freq_div = IRB_CARR_CLK / irb->freq;
	_duty_cal = ((_freq_div * irb->duty) / 100);
	writel(_freq_div, irb->reg + IRB_CARCLKDIV);
	writel(_duty_cal, irb->reg + IRB_CARCLKDUTY);

	return 0;
}

static int put_data_in_FIFO_and_trig(struct nvt_irb_dev *irb, size_t len)
{
	unsigned int i = 0;
	u32 _reg;

	if (len > WBUF_LEN)
		return -EINVAL;

	IRB_DBG("[%s]set out len = %d\n", __func__, len);

	/* clear fifo irq and buf, set irq threshold */
	writel(0x800001ff, irb->reg + IRB_INT_FIFOST);
	writel((((len*7/10)<<16)|len), irb->reg + IRB_SETFIFOFULL);
	writel((((len*3/10)<<16)|0), irb->reg + IRB_SETFIFOETY);
	writel((((10)<<16)|(len/2)), irb->reg + IRB_SETFIFOHALF);

	/* set irb output addr and put data into buf */
	writel(0x0, irb->reg + IRB_OUTADDR);
	writel(((0<<16) & 0x07ff0000), irb->reg + IRB_SETFIFOPTR);
	for (i = 0; i < len; i++) {
		/* polar to invt, idleH, modu. Data0 always high in modu mode*/
		irb->wbuf[i] |= (((i%2))?POLAR_BIT:0);

		if ((i) < (WBUF_LEN/2))
			writel(irb->wbuf[i], irb->reg + IRB_FIFO0);
		else
			writel(irb->wbuf[i], irb->reg + IRB_FIFO1);

		IRB_DBG("wbuf[%d]%x\n", i, irb->wbuf[i]);
	}

	_reg = readl(irb->reg + IRB_TRIG_SET_OUT);
	_reg &= (~0xfff);
	_reg |= len;
	writel(_reg, irb->reg + IRB_TRIG_SET_OUT);
	writel(0x1, irb->reg + IRB_TRIGGER_IRB); /* trigger output */
	writel(0x140, irb->reg + IRB_INTEN);     /* only enable empty irq*/
	return 0;
}

static ssize_t nvt_irb_write(struct file *file, const char *buf,
			  size_t n, loff_t *ppos)
{
	int count, i;
	struct nvt_irb_dev *irb = lirc_get_pdata(file);

	IRB_DBG("[%s]%d\n", __func__, __LINE__);

	if (n % sizeof(int))
		return -EINVAL;

	count = n / sizeof(int);
	if ((count > WBUF_LEN) || (count % 2 == 0))
		return -EINVAL;

	/* Wait any pending transfers to finish */
	wait_event_interruptible(irb->out_wq, irb->wptr < 0);
	irb->wptr = 0;

	if (copy_from_user(irb->wbuf, buf, n))
		return -EFAULT;

	/* Sanity check the input pulses */
	for (i = 0; i < count; i++)
		if (irb->wbuf[i] < 0)
			return -EINVAL;

	nvt_irb_timing_init(irb);
	put_data_in_FIFO_and_trig(irb, count);

	/* Don't return userspace until transfer done */
	wait_event_interruptible(irb->out_wq, irb->wptr < 0);

	return n;
}

static int nvt_irb_set_duty(struct nvt_irb_dev *irb, u32 duty)
{
	if (duty < 20 || duty > 60) {
		dev_err(irb->dev, ": invalid duty cycle %d\n", duty);
		return -EINVAL;
	}

	irb->duty = duty;
	IRB_DBG("[%s] Set Duty = %d\n", __func__, irb->duty);
	return 0;
}

static int nvt_irb_set_carrier(struct nvt_irb_dev *irb, u32 carrier)
{
	if (carrier == 0 || carrier > IRB_CARR_CLK_MAX) {
		dev_err(irb->dev, ": invalid carrier freq %d\n", carrier);
		return -EINVAL;
	}

	irb->freq = carrier;
	IRB_DBG("[%s] Set Freq = %d\n", __func__, irb->freq);
	return 0;
}

static int nvt_irb_set_carrier_report(struct nvt_irb_dev *irb, int enable)
{
	if (enable)
		g_carr_mode = 1;
	else
		g_carr_mode = 0;

	return 0;
}

static long nvt_irb_ioctl(struct file *filep,
			unsigned int cmd, unsigned long arg)
{
	int ret;
	u32 val;
	struct nvt_irb_dev *irb = lirc_get_pdata(filep);

	if (!irb) {
		IRB_DBG("[%s]%d err\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = get_user(val, (u32 *) arg);
		if (ret)
			return ret;
	}

	switch (cmd) {
	case LIRC_SET_SEND_DUTY_CYCLE:
		return nvt_irb_set_duty(irb, val);

	case LIRC_SET_SEND_CARRIER:
		return nvt_irb_set_carrier(irb, val);

	case LIRC_SET_MEASURE_CARRIER_MODE:
		return nvt_irb_set_carrier_report(irb, !!val);

	default:
		return -ENOIOCTLCMD;
	}

	IRB_DBG("[%s]%d\n", __func__, __LINE__);
	return 0;
}

static ssize_t nvt_irb_read(struct file *file,
			  char __user *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct nvt_irb_dev *irb = lirc_get_pdata(file);

	dev_dbg(irb->dev, "[%s]SEC request just return -1\n", __func__);

	return -1; /* SEC request if dummy read, just return -1 */
}

void nvt_irb_signal_iscoming(int signal_status)
{
	IRB_DBG("[%s]%d\n", __func__, __LINE__);

	if (!g_carr_mode) {
		IRB_DBG("not continuous mode, Ignore irb callback\n");
		return;
	}

	if (signal_status) {
		g_signal_in = 1;
		wake_up_interruptible(&in_wq);
	} else
		g_signal_in = 0;

}
EXPORT_SYMBOL(nvt_irb_signal_iscoming);

nvt_irb_callback_func nvt_irb_regsiter_callback(void)
{
	pr_info("[%s]%d\n", __func__, __LINE__);
	return (nvt_irb_callback_func)nvt_irb_signal_iscoming;
}
EXPORT_SYMBOL(nvt_irb_regsiter_callback);

static unsigned int nvt_irb_poll(struct file *file, poll_table *wait)
{
	struct nvt_irb_dev *irb = lirc_get_pdata(file);

	if (!irb) {
		pr_err("called with invalid irctl\n");
		return POLLERR;
	}

	nvt_irb_signal_iscoming(0);
	if (wait_event_interruptible(in_wq, g_signal_in))
		return -ERESTARTSYS;

	dev_dbg(irb->dev, "poll ret\n");
	return POLLPRI|POLLIN;
}

static int nvt_irb_open(struct inode *inode, struct file *file)
{
	struct nvt_irb_dev *irb = lirc_get_pdata(file);

	IRB_DBG("[%s]%d\n", __func__, __LINE__);
	BUG_ON(!irb);

	return 0;
}

static int nvt_irb_release(struct inode *inode, struct file *file)
{
	struct nvt_irb_dev *irb = lirc_get_pdata(file);

	IRB_DBG("[%s]%d\n", __func__, __LINE__);
	return 0;
}

/* this is for driver init apis */
static const struct of_device_id nvt_irb_match[] = {
	{
		.compatible = "nvt,nvt-irb",
	},
	{},
};
MODULE_DEVICE_TABLE(of, nvt_irb_match);

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.write		= nvt_irb_write,
	.unlocked_ioctl	= nvt_irb_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nvt_irb_ioctl,
#endif
	.read		= nvt_irb_read,
	.poll		= nvt_irb_poll,
	.open		= nvt_irb_open,
	.release	= nvt_irb_release,
};

static void _nvt_irb_hw_init(struct nvt_irb_dev *irb)
{
	int ret;
	u32 _reg;

	/* re-init irb register */
	_reg = readl(irb->reg + IRB_TRIG_SET_OUT);
	_reg = (irb->Modu)?(_reg | EN_MODU):(_reg & (~EN_MODU));
	_reg = (irb->Invt)?(_reg | EN_INVT):(_reg & (~EN_INVT));
	_reg = (irb->IdleSt)?(_reg | EN_IDLEST):(_reg & (~EN_IDLEST));
	writel(_reg, irb->reg + IRB_TRIG_SET_OUT);

	/* re-init irb_sys clk*/
	writel(((IRB_SYSCLK_DIV<<16) | IRB_ENVE_RESO),
			irb->reg + IRB_ENVRESL_SYSCLK);

	ret = gpio_request(IRB_OUT_PIN, NULL);
	if (ret)
		pr_err("[%s]Request GPIO Fail, pin=%d\n", __func__, IRB_OUT_PIN);
	else {
		nt726xx_pinctrl_set(NULL, IRB_OUT_PIN, 6);
		gpio_free(IRB_OUT_PIN);
	}
}

static int nvt_irb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lirc_driver *drv;
	struct nvt_irb_dev *irb;
	struct resource *r;
	struct device_node *np = NULL;
	int ret;

	drv = devm_kzalloc(dev, sizeof(struct lirc_driver), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	irb = devm_kzalloc(dev, sizeof(struct nvt_irb_dev), GFP_KERNEL);
	if (!irb)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "%s get io resrc fail!\n", __func__);
		return -ENXIO;
	}

	/* init private and static data */
	g_signal_in = 0;
	g_carr_mode = 0;
	irb->wptr = -1;
	irb->dev = dev;
	init_waitqueue_head(&irb->out_wq);
	init_waitqueue_head(&in_wq);
	irb->reg = ioremap_nocache(r->start, resource_size(r));
	if (!irb->reg) {
		dev_err(dev, "%s remap nvt irb fail!\n", __func__);
		return -ENOMEM;
	}

	/* register IRQ */
	np = of_find_node_by_name(NULL, "nvt_irb");
	if (np == NULL) {
		dev_err(dev, "find node fail\n");
		return -ENXIO;
	}

	irb->irq = irq_of_parse_and_map(np, 0);
	ret = request_irq(irb->irq, nvt_irb_isr,
			IRQF_TRIGGER_HIGH, "nvt_irb", irb);
	if (ret)
		dev_err(dev, "request_irq err %d'!!\n", ret);

	/* init irb para and register as lirc */
	snprintf(drv->name, sizeof(drv->name), NVT_IRB_DRV_NAME);
	irb->Modu = 1;
	irb->Invt = 1;
	irb->IdleSt = 0;
	irb->freq = 0;
	irb->duty = 0;
	_nvt_irb_hw_init(irb);

	drv->minor = 0;
	drv->code_length = 1;
	drv->data = irb;
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

	return 0;
}

static int nvt_irb_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irb_dev *irb = drv->data;

	disable_irq(irb->irq);
	free_irq(irb->irq, irb);

	IRB_DBG("[%s] minor = %d\n", __func__, drv->minor);
	ret = lirc_unregister_driver(drv->minor);
	if (ret)
		return ret;

	/* TODO : free memory ?? */

	return 0;
}

static int nvt_irb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irb_dev *irb = drv->data;

	disable_irq(irb->irq);
	return 0;
}

static int nvt_irb_resume(struct platform_device *pdev)
{
	struct lirc_driver *drv = platform_get_drvdata(pdev);
	struct nvt_irb_dev *irb = drv->data;

	enable_irq(irb->irq);
	_nvt_irb_hw_init(irb); /* re-init reg after resume */

	return 0;
}

struct platform_driver nvt_irb_platform_driver = {
	.probe		= nvt_irb_probe,
	.remove		= nvt_irb_remove,
	.suspend	= nvt_irb_suspend,
	.resume		= nvt_irb_resume,
	.driver		= {
		.name	= NVT_IRB_DRV_NAME,
		.of_match_table = of_match_ptr(nvt_irb_match),
	},
};
module_platform_driver(nvt_irb_platform_driver);

MODULE_DESCRIPTION("LIRC TX driver for NVT IRB HW");
MODULE_AUTHOR("Novatek");
MODULE_LICENSE("GPL");
