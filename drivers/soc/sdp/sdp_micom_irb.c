/*
 * modify Loopback driver for micom irb
 */
//#define VERSION_STR "v0.1(first version)"
#define VERSION_STR "v0.2(add isr, add irr carrier report)"

#define DEBUG

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include <media/rc-core.h>
#include <soc/sdp/sdp_micom_ir.h>

#define IRBDBG(args...) dev_dbg(args)

#define DRIVER_NAME	"sdp-mcirb"
#define TXMASK_REGULAR	0x1

#define APBCLK 24576 //KHz
#define CLK_DIV 0x40	

#define IRBCR 	 0x00
#define IRBDR 	 0x04
#define IRBCNT 	 0x08
#define IRBCARR  0x0C
#define IRBSR 	 0x10
#define IRBCNTRD 0x14


struct mcirb_dev {
	struct device *dev;
	struct rc_dev *rcdev;

	void __iomem *io_base;
	int irq;

	struct completion done;
	u32 txmask;
	u32 txcarrier;
	u32 txduty;
	bool idle;
	bool carrierreport;
};

static irqreturn_t mcirb_isr(int irq, void* dev)
{
	struct mcirb_dev *irbdev = dev;
	u32 pend = readl(irbdev->io_base + IRBSR);

	IRBDBG(irbdev->dev, "mcirb_isr pend 0x%08x\n", pend);

	if(pend == 0) {
		return IRQ_NONE;
	}

	if(pend & 0x4) {
		writel((readl(irbdev->io_base+IRBCR)&(~0x10)), irbdev->io_base + IRBCR);//int empty disable
		writel((readl(irbdev->io_base+IRBCR)&(~0x4)), irbdev->io_base + IRBCR);	//tx disable
		complete(&irbdev->done);
	}

	return IRQ_HANDLED;
}

static void mc_irr_cb(enum sdp_ir_event_e event,
		unsigned int code, unsigned long long timestemp_ns, void *priv) {
	struct rc_dev *rcdev = priv;
	struct mcirb_dev *irbdev = rcdev->priv;
	DEFINE_IR_RAW_EVENT(rawir);

	IRBDBG(irbdev->dev, "mc_irr_cb event 0x%02x, code 0x%02x, time %lluns\n", event, code, timestemp_ns);

	if(irbdev->carrierreport && event == SDP_IR_EVT_KEYPRESS) {
		/* send a carrier event for to notify key received by micom irr.  */
		IRBDBG(irbdev->dev, "mc_irr_cb carrier report 0x%02x, code 0x%02x\n", event, code);
		rawir.carrier_report = true;
		rawir.carrier = 38000;
		ir_raw_event_store(rcdev, &rawir);
		ir_raw_event_handle(rcdev);
	}
}

static void mcirb_init(struct mcirb_dev *irbdev)
{		
	/* Set IRBCNT_IRBCNTRD */
	writel(0x1, irbdev->io_base + IRBCNTRD);	
	writel(CLK_DIV, irbdev->io_base + IRBCNT);	
	writel(0x288, irbdev->io_base + IRBCARR);	
	//writel(0xc2b7, irbdev->io_base + IRBCR);
	writel(0xC2C3, irbdev->io_base + IRBCR);

	writel((readl(irbdev->io_base+IRBCR)|0x8), irbdev->io_base + IRBCR);//clear fifo set
	writel((readl(irbdev->io_base+IRBCR)&(~0x8)), irbdev->io_base + IRBCR);//clear fifo clr
}

static int mcirb_set_tx_mask(struct rc_dev *rcdev, u32 mask)
{
	struct mcirb_dev *irbdev = rcdev->priv;

	if ((mask & (TXMASK_REGULAR)) != mask) {
		IRBDBG(irbdev->dev, "invalid tx mask: %u\n", mask);
		return -EINVAL;
	}

	IRBDBG(irbdev->dev, "setting tx mask: %u\n", mask);
	irbdev->txmask = mask;
	return 0;
}

static int mcirb_set_tx_carrier(struct rc_dev *rcdev, u32 carrier)
{
	struct mcirb_dev *irbdev = rcdev->priv;

	IRBDBG(irbdev->dev, "setting tx carrier: %u\n", carrier);

	if(carrier) {
		//dev_info(irbdev->dev, "set carrier \n");
		irbdev->txcarrier = APBCLK*1000 / carrier;
		
		/* Set Carrier frequency */
		writel(irbdev->txcarrier, irbdev->io_base + IRBCARR);

		//IR data modulation enable
		writel((readl(irbdev->io_base+IRBCR)|0x2), irbdev->io_base + IRBCR);	
	} else {
		//IR data modulation disable
		writel((readl(irbdev->io_base+IRBCR)&(~0x2)), irbdev->io_base + IRBCR);	
	}

	return 0;
}

static int mcirb_set_tx_duty_cycle(struct rc_dev *rcdev, u32 duty_cycle)
{
	struct mcirb_dev *irbdev = rcdev->priv;

	if (duty_cycle < 1 || duty_cycle > 99) {
		IRBDBG(irbdev->dev, "invalid duty cycle: %u\n", duty_cycle);
		return -EINVAL;
	}
	IRBDBG(irbdev->dev, "setting duty cycle: %u\n", duty_cycle);
	//dev_info(irbdev->dev, "set duty cycle = %d \n", irbdev->txcarrier);
	irbdev->txduty = (irbdev->txcarrier*duty_cycle)/100;
	
	/* Set Duty Cycle */
	writel((readl(irbdev->io_base + IRBCR)&(~0xFFFFFF00))
		| (irbdev->txduty<<8), irbdev->io_base + IRBCR); 
	return 0;
}

/* refer: https://linuxtv.org/downloads/v4l-dvb-apis/uapi/rc/lirc-set-measure-carrier-mode.html#c.LIRC_SET_MEASURE_CARRIER_MODE */
static int mcirb_set_carrier_report(struct rc_dev *rcdev, int enable)
{
	struct mcirb_dev *irbdev = rcdev->priv;

	if (irbdev->carrierreport != enable) {
		IRBDBG(irbdev->dev, "%sabling carrier reports\n", enable ? "en" : "dis");
		irbdev->carrierreport = !!enable;
	}

	return 0;
}

static int mcirb_tx_ir(struct rc_dev *rcdev, unsigned *txbuf, unsigned count)
{
	struct mcirb_dev *irbdev = rcdev->priv;
	unsigned i;
	uint32_t duration;
	bool pulse;

	IRBDBG(irbdev->dev, "mcirb_tx_ir count %u\n", count);

	reinit_completion(&irbdev->done);

	writel((readl(irbdev->io_base+IRBCR)&(~0x4)), irbdev->io_base + IRBCR);	
	writel((readl(irbdev->io_base+IRBCR)|0x8), irbdev->io_base + IRBCR);//clear fifo set
	writel((readl(irbdev->io_base+IRBCR)&(~0x8)), irbdev->io_base + IRBCR);//clear fifo clr

	for (i = 0; i < count; i++) {
		pulse = i % 2 ? false : true;
		duration =(txbuf[i] * APBCLK)/(CLK_DIV*1000);

		if (duration) {
			writel((pulse<<20 | duration), irbdev->io_base + IRBDR);
			//IRBDBG(irbdev->dev, "wave data[%08x]\n", pulse<<20 | duration);
		}
	}

	writel(readl(irbdev->io_base + IRBCR) | 0x10, irbdev->io_base + IRBCR);//empty int enable
	writel(readl(irbdev->io_base + IRBCR) | 0x4, irbdev->io_base + IRBCR);	//set irb start	

	wait_for_completion(&irbdev->done);

	return count;
}

static void mcirb_set_idle(struct rc_dev *rcdev, bool enable)
{
	struct mcirb_dev *irbdev = rcdev->priv;

	if (irbdev->idle != enable) {
		IRBDBG(irbdev->dev, "%sing idle mode\n", enable ? "enter" : "exit");
		irbdev->idle = enable;
	}
}


static int sdp_mcirb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rc_dev *rc = NULL;
	struct mcirb_dev *irbdev = NULL;
	struct resource *res = NULL;
	int ret = -EINVAL;

	irbdev = devm_kzalloc(&pdev->dev, sizeof(*irbdev), GFP_KERNEL);
	if (!irbdev)
		return -ENOMEM;

	init_completion(&irbdev->done);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "irb");
	irbdev->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(irbdev->io_base)) {
		return PTR_ERR(irbdev->io_base);
	}
	IRBDBG(irbdev->dev, "irbdev->io_base %p\n", irbdev->io_base);

	mcirb_init(irbdev);

	irbdev->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irbdev->irq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;	
	}

	ret = devm_request_threaded_irq(dev, irbdev->irq, mcirb_isr,
			NULL, 0, pdev->name, irbdev);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq return %d\n", ret);
		return ret;
	}

	IRBDBG(irbdev->dev, "irbdev->irq %d\n", irbdev->irq);

	irbdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, irbdev);


	/* register rc */
	rc = rc_allocate_device();
	if (!rc) {
		dev_err(irbdev->dev, "rc_dev allocation failed\n");
		return -ENOMEM;
	}

	rc->input_name		= DRIVER_NAME;
	rc->input_phys		= DRIVER_NAME"/input0";
	rc->input_id.bustype	= BUS_HOST;
	rc->input_id.version	= 1;
	rc->driver_name		= DRIVER_NAME;
	rc->map_name		= RC_MAP_EMPTY;
	rc->priv		= irbdev;
	rc->driver_type		= RC_DRIVER_IR_RAW;
	rc->allowed_protocols	= RC_BIT_LIRC;
	rc->timeout		= 100 * 1000 * 1000; /* 100 ms */
	rc->tx_resolution	= 1000;
	//rc->s_tx_mask		= mcirb_set_tx_mask;
	rc->s_tx_carrier	= mcirb_set_tx_carrier;
	rc->s_tx_duty_cycle	= mcirb_set_tx_duty_cycle;
	rc->tx_ir		= mcirb_tx_ir;
	rc->s_idle		= mcirb_set_idle;
	rc->s_carrier_report	= mcirb_set_carrier_report;

	/* default setting */
	irbdev->txmask		= TXMASK_REGULAR;
	irbdev->txcarrier	= 36000;
	irbdev->txduty		= 50;
	irbdev->idle		= true;
	irbdev->carrierreport	= false;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(irbdev->dev, "rc_dev registration failed\n");
		rc_free_device(rc);
		return ret;
	}
	IRBDBG(irbdev->dev, "rc_register_device done\n");

	irbdev->rcdev = rc;

	sdp_messagebox_register_irr_cb(mc_irr_cb, rc);

	dev_info(&pdev->dev, "probe done.%s\n", VERSION_STR);
	
	return 0;
}

static int sdp_mcirb_remove(struct platform_device *pdev)
{
	struct mcirb_dev *irbdev = platform_get_drvdata(pdev);

	rc_unregister_device(irbdev->rcdev);

	return 0;
}

static const struct of_device_id sdp_mcirb_match[] = {
	{.compatible = "samsung,sdp-mc-irb"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdp_mcirb_match);

static struct platform_driver sdp_mcirb_driver = {
	.probe	= sdp_mcirb_probe,
	.remove	= sdp_mcirb_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.of_match_table = sdp_mcirb_match,
	},
};
module_platform_driver(sdp_mcirb_driver);

MODULE_DESCRIPTION("SDP Micom IRB Driver");
MODULE_LICENSE("GPL");
