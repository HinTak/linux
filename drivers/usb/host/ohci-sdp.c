/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#ifdef CONFIG_64BIT
static u64 sdp_dma_mask = DMA_BIT_MASK(64);
#else
static u64 sdp_dma_mask = DMA_BIT_MASK(32);
#endif

#ifdef CONFIG_ARCH_SDP1601

#define SDP_DEV_RUN		0
#define SDP_DEV_STANDBY		1
#define SDP_MICOM_MAX_CTRL_TRY	3

enum sdp_ohci_dev_micom_ctrl
{
	SDP_USER	= 0x0,
	SDP_WIFI_CTRL	= 0x43, 
	SDP_BT_CTRL	= 0x44, 
	SDP_HUB_CTRL	= 0x45,
};

enum sdp_ohci_dev_micom_ctrl_ack
{
	SDP_USER_ACK		= 0x0,
	SDP_WIFI_CTRL_ACK	= 0x43, 
	SDP_BT_CTRL_ACK		= 0x44, 
	SDP_HUB_CTRL_ACK	= 0x45,
};

struct sdp_ohci_mcm_cmd
{
	u32 ctrl;
	u32 ctrl_ack;
};

typedef int (*micom_cmd_ack)(char cmd, char ack, char *data, int len);

#endif

struct sdp_ohci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;

#ifdef CONFIG_ARCH_SDP1601
	micom_cmd_ack		mcm_ctrl_fn;
	u32 			mcm_set_val;
	struct sdp_ohci_mcm_cmd *mcm_cmd;

	struct workqueue_struct *wq;
	struct work_struct	work_thr;
	u32 			w_param;

	u32	irq_affinity;
	bool	async_suspend;
#endif
};

struct ohci_priv
{
	struct sdp_ohci_hcd *sdp;
};

#define sdpohci_to_ohci(ehci)	(((struct ohci_priv *)(ohci)->priv)->sdp)


/*********** Attribute ****************************************************/
static ssize_t device_resume_timeout_show(struct device *dev,
		struct device_attribute *addr,
		char *buf)
{
	struct sdp_ohci_hcd *sdp_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	return snprintf(buf, PAGE_SIZE, "%d\n", ohci->device_resume_timeout_cnt);
}
DEVICE_ATTR_RO(device_resume_timeout);

static ssize_t port_status_regval_show(struct device *dev,
		struct device_attribute *addr,
		char *buf)
{
	struct sdp_ohci_hcd *sdp_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	return snprintf(buf, PAGE_SIZE, "%d\n", ohci->port_status_regval_cnt);
}
DEVICE_ATTR_RO(port_status_regval);
/****************************End Attribute************************************/

static void ohci_host_dump(struct ohci_hcd *ohci);

#ifdef CONFIG_ARCH_SDP1601
static unsigned long __find_symbol(struct device *dev,char *name)
{
	struct kernel_symbol *sym = NULL;
	const int MAX_CHK = 1000;
	const int SLEEP_MS = 30;
	int cnt = 0;
	
	for( cnt = 0 ; cnt < MAX_CHK ; cnt++ )
	{
		sym = (void *)find_symbol(name, NULL, NULL, 1, true);
		if(sym) {
			return sym->value;
		}
		dev_err(dev,"don't find[%d] wait[%d]ms\n",cnt,SLEEP_MS);
		msleep(SLEEP_MS);
	}
	
	dev_dbg(dev,"[%s] can not find a symbol[times:%d][msec:%d]\n"
		,name,MAX_CHK,SLEEP_MS);
	return (unsigned long)NULL;
}

static int __mcm_ctrl(struct device *dev,micom_cmd_ack fn
		,struct sdp_ohci_mcm_cmd *cmd ,u8 val)
{
	int ret = 0;
	u8 data[5] = {val,0,};
	int try_cnt = 0;

	msleep(80);
	do {
		dev_err(dev,"[send micom:0x%X,0x%X,0x%X]\n"
			,cmd->ctrl,cmd->ctrl_ack,val);
		ret = fn( (char)(cmd->ctrl & 0xFF)
			,(char)(cmd->ctrl_ack & 0xFF),data,5);
		if( ret != 0 ) msleep(100);
	}while( ret && (try_cnt++) < SDP_MICOM_MAX_CTRL_TRY );

	return ret;
}

static void __mcm_cmd_chk_and_send(struct sdp_ohci_hcd *sdp_ohci
		,struct sdp_ohci_mcm_cmd *mcmd ,u32 val)
{
	if( mcmd == NULL || mcmd->ctrl == SDP_USER ) return;
	
	if( __mcm_ctrl(sdp_ohci->dev,sdp_ohci->mcm_ctrl_fn,mcmd,val) != 0 ) {
		dev_err(sdp_ohci->dev
			,"fail : micom send msg[0x%X][0x%X][0x%X]\n"
			,mcmd->ctrl,mcmd->ctrl_ack,val);
	}
}

static void __cmd_dev_exec(struct sdp_ohci_hcd *sdp_ohci,u32 val)
{
	struct sdp_ohci_mcm_cmd *mcmd = sdp_ohci->mcm_cmd;
	
	if(!mcmd) return;
	if( sdp_ohci->mcm_set_val == val ) return;
	if( sdp_ohci->mcm_ctrl_fn == NULL )
	{	/* set function */
		sdp_ohci->mcm_ctrl_fn = (micom_cmd_ack)__find_symbol(
					sdp_ohci->dev,"sdp_micom_send_cmd_ack");
		if( NULL == sdp_ohci->mcm_ctrl_fn ) {
			sdp_ohci->mcm_ctrl_fn = NULL;
			dev_err(sdp_ohci->dev,"don't find micom function.\n");
			return; /*-ENOENT;*/
		}
	}

	__mcm_cmd_chk_and_send(sdp_ohci,mcmd,val);
	sdp_ohci->mcm_set_val = val;
}

static void sdp_ohci_cmd_micom(struct work_struct *work)
{
	struct sdp_ohci_hcd *sdp_ohci 
		= container_of(work, struct sdp_ohci_hcd,work_thr);

	__cmd_dev_exec(sdp_ohci,sdp_ohci->w_param);
}

static void sdp_ohci_cmd_dev(struct sdp_ohci_hcd *sdp_ohci,u32 val,bool use_wq)
{
	if( !sdp_ohci->mcm_cmd ) return;
	
	if( use_wq ) {
		sdp_ohci->w_param = val;
		queue_work(sdp_ohci->wq, &sdp_ohci->work_thr);
	}
	else {
		__cmd_dev_exec(sdp_ohci,val);
	}
}

/* get platform data from device tree */
static int sdp_ohci_parse_dt(struct sdp_ohci_hcd *sdp_ohci,struct device_node* np)
{	
	u32 cmd;

	if (!np) {
		dev_err(sdp_ohci->dev,"device node is null.\n");
		return -EINVAL;
	}
	
	/* get info about connected device */
	if (!of_property_read_u32(np, "conn_dev", &cmd)) {
		struct sdp_ohci_mcm_cmd *mcmd = devm_kzalloc(sdp_ohci->dev
				,sizeof(struct sdp_ohci_mcm_cmd),GFP_KERNEL);
		if( !mcmd ) {
			dev_err(sdp_ohci->dev,"no-MEM.\n");
			return -ENOMEM;
		}

		mcmd->ctrl = cmd;
		dev_info(sdp_ohci->dev,"[OHCI-conn_dev:0x%X]\n",mcmd->ctrl);

		switch(mcmd->ctrl) {
			case SDP_WIFI_CTRL :
				mcmd->ctrl_ack = SDP_WIFI_CTRL_ACK;
				break;
			case SDP_BT_CTRL :
				mcmd->ctrl_ack = SDP_BT_CTRL_ACK;
				break;
			case SDP_HUB_CTRL :
				mcmd->ctrl_ack = SDP_HUB_CTRL_ACK;
				break;
			default :
				mcmd->ctrl = SDP_USER;
				mcmd->ctrl_ack = SDP_USER_ACK;
				break;
		}

		sdp_ohci->mcm_cmd = mcmd;
	}

	if(!of_property_read_u32(np,"irq-affinity",&cmd)) {
		sdp_ohci->irq_affinity = cmd;
		dev_info(sdp_ohci->dev,"[OHCI]irq-affinity[%d]\n",cmd);
	}

	sdp_ohci->async_suspend = of_property_read_bool(np,"async-suspend");
	dev_info(sdp_ohci->dev,"[OHCI]async_suspend[%d]\n"
					,sdp_ohci->async_suspend);

	return 0;
}

static void sdp_ohci_set_irq_affinity(struct sdp_ohci_hcd *sdp_ohci,u32 irqnum)
{	
	u32 affinity = sdp_ohci->irq_affinity;
	if( affinity && affinity < num_online_cpus() ) {
		int ret = irq_set_affinity_hint(irqnum,cpumask_of(affinity));
		if( ret ) {
			dev_err(sdp_ohci->dev
				,"irq %d can't irq_set_affinity %u\n"
				,irqnum, affinity);
		} else {
			dev_err(sdp_ohci->dev
				,"[OHCI]set irq-affinity[%d]\n",affinity);
		}
	}
}
#endif

static int sdp_ohci_reset(struct usb_hcd *hcd)
{
	return ohci_init(hcd_to_ohci(hcd));
}

static int sdp_ohci_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret = ohci_run(ohci);

	if( ret == 0 )
	{
#ifdef CONFIG_ARCH_SDP1601
		struct sdp_ohci_hcd *sdp_ohci = sdpohci_to_ohci(ohci);
		sdp_ohci_cmd_dev(sdp_ohci,SDP_DEV_RUN,false);
#endif
		if( ohci->hcca ) {
			ohci_err(ohci,"[HCCA Address][0x%p][0x%llx]\n"
				,ohci->hcca,(u64)ohci->hcca_dma);
		}
	}
	else {
		dev_err(hcd->self.controller,"can't start %s\n",
			hcd->self.bus_name);
		ohci_stop(hcd);
	}

	return ret;	
}

static const struct hc_driver sdp_ohci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "SDP OHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ohci_hcd) +
				sizeof(struct ohci_priv) /* private data */,

	.irq			= ohci_irq,
	.flags			= HCD_MEMORY | HCD_USB11,

	.reset			= sdp_ohci_reset,
	.start			= sdp_ohci_start,
	
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,

	.get_frame_number	= ohci_get_frame,

	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend		= ohci_bus_suspend,
	.bus_resume		= ohci_bus_resume,
#endif
	.start_port_reset	= ohci_start_port_reset,
};

static void sdp_ohci_gpio_init(struct platform_device *pdev)
{
	int err;
	int gpio;

	if (!pdev->dev.of_node)
		return;

	gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
	if (!gpio_is_valid(gpio))
		return;

	err = gpio_request((u32)gpio, "usb_enable");
	if (err) {
		if (err != -EBUSY)
			dev_err(&pdev->dev, "Can't request gpio: %d\n", err);
		return;
	}

	gpio_direction_output((u32)gpio, 0);
	gpio_set_value((u32)gpio, 1);
}

static int sdp_ohci_probe(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci;
	struct usb_hcd *hcd;
	struct ohci_hcd *ohci;
	struct ohci_priv *priv;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	pdev->dev.dma_mask = &sdp_dma_mask;
#if defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT)
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else	
	if (!pdev->dev.coherent_dma_mask)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	

	sdp_ohci = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_ohci_hcd), GFP_KERNEL);
	if (!sdp_ohci)
		return -ENOMEM;

	sdp_ohci->dev = &pdev->dev;

	hcd = usb_create_hcd(&sdp_ohci_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
#ifdef USB_AMBIENTMODE
	pdev->dev.is_usb = 1;
#endif
	sdp_ohci->hcd = hcd;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev
			,res->start,(unsigned long)hcd->rsrc_len);

	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

#ifdef CONFIG_ARCH_SDP1601
	sdp_ohci->mcm_ctrl_fn	= NULL;
	sdp_ohci->mcm_set_val	= SDP_DEV_STANDBY;
	sdp_ohci->mcm_cmd 	= NULL;
	sdp_ohci->irq_affinity	= 0;
	sdp_ohci->async_suspend	= false;
		
	if( sdp_ohci_parse_dt(sdp_ohci,pdev->dev.of_node) ) {
		dev_err(&pdev->dev, "Failed to get info,\n");
		err = -ENXIO;
		goto fail_io;
	}

	sdp_ohci->wq = create_singlethread_workqueue("sdp_ohci_wq");
	if (!sdp_ohci->wq) {
		dev_err(&pdev->dev,"Failed to get mem for wq\n");
		err = -ENOMEM;
		goto fail_io;
	}

	INIT_WORK(&sdp_ohci->work_thr,sdp_ohci_cmd_micom);
#endif

	ohci = hcd_to_ohci(hcd);
	priv = (struct ohci_priv *)ohci->priv;
	priv->sdp = sdp_ohci;
	
	ohci_hcd_init(ohci);

	err = usb_add_hcd(hcd,(u32)irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev,"Failed to add USB HCD\n");
		goto fail_io;
	}

#ifdef CONFIG_ARCH_SDP1601
	sdp_ohci_set_irq_affinity(sdp_ohci,irq);

	if( sdp_ohci->async_suspend ) {
		device_enable_async_suspend(sdp_ohci->dev);
		dev_info(&pdev->dev,"[OHCI]set async suspend/resume\n");
	}
#endif	

	platform_set_drvdata(pdev, sdp_ohci);

#ifdef CONFIG_OF
	sdp_ohci_gpio_init(pdev);
#endif

	device_create_file(&pdev->dev, &dev_attr_device_resume_timeout);
	device_create_file(&pdev->dev, &dev_attr_port_status_regval);

	return 0;

fail_io:

#ifdef CONFIG_ARCH_SDP1601
	if( sdp_ohci->wq )
		destroy_workqueue(sdp_ohci->wq);
#endif

	usb_put_hcd(hcd);
	devm_kfree(&pdev->dev,sdp_ohci);
	return err;
}

static int sdp_ohci_remove(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ohci->hcd;

	device_remove_file(&pdev->dev, &dev_attr_device_resume_timeout);
        device_remove_file(&pdev->dev, &dev_attr_port_status_regval);
	usb_remove_hcd(hcd);

	usb_put_hcd(hcd);

	return 0;
}

static void sdp_ohci_shutdown(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ohci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_SDP1601)

/* use this function instead of ohci_resume() : resume performance */
static inline void __sdp_resume(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);

	ohci->device_resume_timeout_cnt = 0;
	ohci->port_status_regval_cnt = 0;

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrenable);
	(void)ohci_readl(ohci, &ohci->regs->intrenable);
	wmb();
	
	usb_hcd_resume_root_hub(hcd);
}

static int ohci_sdp_suspend(struct device *dev)
{
	struct sdp_ohci_hcd *sdp_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ohci->hcd;

	int ret = ohci_suspend(hcd, device_may_wakeup(dev));

	sdp_ohci->mcm_set_val = SDP_DEV_STANDBY;
	
	return ret;
}

static int ohci_sdp_resume(struct device *dev)
{
	struct sdp_ohci_hcd *sdp_ohci = dev_get_drvdata(dev);
	__sdp_resume(sdp_ohci->hcd);
	sdp_ohci_cmd_dev(sdp_ohci,SDP_DEV_RUN,true);
	return 0;
}

static const struct dev_pm_ops ohci_sdp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ohci_sdp_suspend, ohci_sdp_resume)
};

#define DEV_OHCI_PM_OPS		(&ohci_sdp_pm_ops)
#else
#define DEV_OHCI_PM_OPS		NULL
#endif

static const struct of_device_id sdp_ohci_match[] = {
	{ .compatible = "samsung,sdp-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_ohci_match);

static struct platform_driver sdp_ohci_driver = {
	.probe		= sdp_ohci_probe,
	.remove		= sdp_ohci_remove,
	.shutdown	= sdp_ohci_shutdown,
	.driver = {
		.name	= "sdp-ohci",
		.owner	= THIS_MODULE,
		.pm	= DEV_OHCI_PM_OPS,
		.of_match_table = of_match_ptr(sdp_ohci_match),
	}
};
MODULE_ALIAS("platform:sdp-ohci");

