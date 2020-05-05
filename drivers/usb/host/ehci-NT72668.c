/*
 *
 * che-chun Kuo <c_c_kuo@novatek.com.tw>
 *
 * This file is licenced under the GPL.
 */
#include "NT72668.h"
#include <linux/spinlock.h>


#ifdef CONFIG_DMA_COHERENT
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
                         | USB_MCFG_PFEN | USB_MCFG_RDCOMB \
                         | USB_MCFG_SSDEN | USB_MCFG_UCAM \
                         | USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#else
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
                         | USB_MCFG_PFEN | USB_MCFG_RDCOMB \
                         | USB_MCFG_SSDEN \
                         | USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#endif


void NT72668_Plat_Resume(struct usb_hcd *hcd){
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	int retval,retry=0;

	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	retry=0;
	do{
		ehci_writel(ehci, INTR_MASK,
		    &ehci->regs->intr_enable); 
		retval = handshake (ehci, &ehci->regs->intr_enable,
			    INTR_MASK, INTR_MASK,250 );
		retry ++;
	}while(retval !=0 );
	if (unlikely(retval != 0))
		ehci_err(ehci, "write fail!\n");
}


static void NT72668_init(void)
{

	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,true);
	udelay(0x20);
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,false);

	// clear of APB read period
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x7 << 5));
	udelay(0x20);

	//set APB read period
	*(volatile unsigned long *)(USB0_APB) |= (0x5 << 5);  
	udelay(0x20);

	//# set PONRST to 1    
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x1<<8)); 
	udelay(0x20);

	//# select SUSPN and RST mask as 0
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x3<<14));
	udelay(0x20);

	//# set uclk sw reset  
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB20_U0, true);
	udelay(0x20);

	//# set hclk sw reset
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0, true);
	udelay(0x20);

	//# set aclk sw reset
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB20_U0, true);
	udelay(0x20);


	//# set aclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB20_U0, false);
	udelay(0x20);
	
	//# set hclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0, false);
	udelay(0x20);

	//# set uclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB20_U0, false);
	udelay(0x20);


	//#set UCLK to PHY 30M
	SYS_CLK_SetClockSource(EN_SYS_CLK_SRC_USB_U0_U1_30M,1);
	udelay(0x20);

	writel(0x20,(volatile void *)((unsigned int)USB0_EHCI_BASE+0x100));
	writel(0x0200006e,(volatile void *)((unsigned int)USB0_EHCI_BASE+0xe0));
	writel(readl((const volatile void *)((unsigned int)USB0_EHCI_BASE+0x84)),(volatile void *)((unsigned int)USB0_EHCI_BASE+0x84));

	set(((unsigned int)USB0_EHCI_BASE+0xc4),0xb);				 
	clear(((unsigned int)USB0_EHCI_BASE+0x88),OTGC_INT_B_TYPE); 
	set(((unsigned int)USB0_EHCI_BASE+0x88),OTGC_INT_A_TYPE);				 

	clear(((unsigned int)USB0_EHCI_BASE+0x80),0x20);
	set(((unsigned int)USB0_EHCI_BASE+0x80),0x10);
	//for phy power
	writel(0, (volatile void *)((unsigned int)USB0_APB+0x438));
	//for full speed device
	set(((unsigned int)USB0_EHCI_BASE+0x80),1<<28);				 
	if(Ker_chip == EN_SOC_NT72668) {
		//TX parameter
		clear((volatile void *)((unsigned int)USB0_APB+0x418), 7<<1);
		set((volatile void *)((unsigned int)USB0_APB+0x418), 6<<1);
		//timing parameter
		clear((volatile void *)((unsigned int)USB0_APB+0x4dc), 1<<5);
		
		//dis parameter
		set((volatile void *)((unsigned int)USB0_APB+0x414), 3<<0);

	}
	if(Ker_chip == EN_SOC_NT72656) {
		//TX parameter
		clear((volatile void *)((unsigned int)USB0_APB+0x418), 7<<1);
		set((volatile void *)((unsigned int)USB0_APB+0x418), 7<<1);
		//dis parameter
		set((volatile void *)((unsigned int)USB0_APB+0x414), 3<<0);
	}
	// for solving full speed BT disconnect issue
	writel(0x6000,(volatile void *)((unsigned int)USB0_EHCI_BASE+0x44));

}

static void NT72668_stop(struct platform_device *dev)
{

	writel(0x2,(volatile void *)((unsigned int)USB0_EHCI_BASE+0x10));
	udelay(1000);

	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,true);
	udelay(0x20);
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,false);


	// clear of APB read period
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x7 << 5));
	udelay(0x20);

	//set APB read period
	*(volatile unsigned long *)(USB0_APB) |= (0x1 << 5);  
	udelay(0x20);

	//# set PONRST to 1    
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x1<<8)); 
	udelay(0x20);

	//# select SUSPN and RST mask as 0
	*(volatile unsigned long *)(USB0_APB) &= ~((unsigned int)(0x3<<14));   
	udelay(0x20);

	//# set uclk sw reset  
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB20_U0, true);
	udelay(0x20);

	//# set hclk sw reset
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0, true);
	udelay(0x20);

	//# set aclk sw reset
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB20_U0, true);


	//# set aclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB20_U0, false);
	udelay(0x20);
	
	//# set hclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB20_U0, false);
	udelay(0x20);

	//# set uclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB20_U0, false);
	udelay(0x20);


	//#set UCLK to PHY 30M
	SYS_CLK_SetClockSource(EN_SYS_CLK_SRC_USB_U0_U1_30M,1);


}


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_ehci_NT72668_probe - initialize NT72668-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
static int usb_ehci_NT72668_probe(const struct hc_driver *driver,
			  struct usb_hcd **hcd_out, struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res_mem = NULL;


	if (dev->resource[2].flags != IORESOURCE_IRQ) {
		pr_debug("resource[2] is not IORESOURCE_IRQ");
		retval = -ENOMEM;
	}

	res_mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&dev->dev, "no memory resource provided for index 0");		
		return -ENXIO;	
	}

	hcd = usb_create_hcd(driver, &dev->dev, "NT72668");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res_mem->start;	
	hcd->rsrc_len = resource_size(res_mem);


	hcd->regs = USB0_EHCI_BASE = devm_request_and_ioremap(&dev->dev, res_mem);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err2;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = (void __iomem *)ehci->caps +
		HC_LENGTH(ehci, readl(&ehci->caps->hc_capbase));

	ehci->sbrn = HCD_USB2;

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);


	/* ehci_hcd_init(hcd_to_ehci(hcd)); */
	res_mem = NULL;
	res_mem = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (!res_mem) {
		dev_err(&dev->dev, "no memory resource provided for index 1");		
		return -ENXIO;	
	}
	
	ehci->rsrc_start = res_mem->start;
	ehci->rsrc_len = resource_size(res_mem);
	ehci->apbs = USB0_APB = devm_request_and_ioremap(&dev->dev, res_mem);
	
	dev_err(&dev->dev, "APB %p", ehci->apbs);	
 	if (!ehci->apbs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err3;
	}
	NT72668_init();
	retval =
	    usb_add_hcd(hcd, dev->resource[2].start, IRQF_DISABLED | IRQF_SHARED);
	if (retval == 0){
		platform_set_drvdata(dev, hcd);
		return retval;
	}
	NT72668_stop(dev);
err3:	
	release_mem_region(ehci->rsrc_start, ehci->rsrc_len);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */


static void usb_ehci_NT72668_remove(struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	pr_debug("calling usb_put_hcd\n");
	usb_put_hcd(hcd);
	NT72668_stop(dev);

}


static int NT72668_ehci_init(struct usb_hcd *hcd)
{
	struct ehci_hcd 	*ehci = hcd_to_ehci(hcd);
	u32 		temp;
	int 		retval;
	u32 		hcc_params;
	struct ehci_qh_hw	*hw;

	spin_lock_init(&ehci->lock);

	ehci->need_io_watchdog = 1;

	hrtimer_init(&ehci->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ehci->hrtimer.function = ehci_hrtimer_func;
	ehci->next_hrtimer_event = EHCI_HRTIMER_NO_EVENT;

	hcc_params = ehci_readl(ehci, &ehci->caps->hcc_params);

	ehci->uframe_periodic_max = 100;

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	ehci->periodic_size = DEFAULT_I_TDPS;
	INIT_LIST_HEAD(&ehci->intr_qh_list);
	INIT_LIST_HEAD(&ehci->cached_itd_list);
	INIT_LIST_HEAD(&ehci->cached_sitd_list);
	if ((retval = ehci_mem_init(ehci, GFP_KERNEL)) < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	hcc_params = ehci_readl(ehci, &ehci->caps->hcc_params);
	if (HCC_ISOC_CACHE(hcc_params)) 	// full frame cache
		ehci->i_thresh = 8;
	else					// N microframes cached
		ehci->i_thresh = 2 + HCC_ISOC_THRES(hcc_params);

	/*
	 * dedicate a qh for the async ring head, since we couldn't unlink
	 * a 'real' qh without stopping the async schedule [4.8].  use it
	 * as the 'reclamation list head' too.
	 * its dummy is used in hw_alt_next of many tds, to prevent the qh
	 * from automatically advancing to the next td after short reads.
	 */
	ehci->async->qh_next.qh = NULL;
	hw = ehci->async->hw;
	hw->hw_next = QH_NEXT(ehci, ehci->async->qh_dma);
	hw->hw_info1 = cpu_to_hc32(ehci, QH_HEAD);
	hw->hw_token = cpu_to_hc32(ehci, QTD_STS_HALT);
	hw->hw_qtd_next = EHCI_LIST_END(ehci);
	ehci->async->qh_state = QH_STATE_LINKED;
	hw->hw_alt_next = QTD_NEXT(ehci, ehci->async->dummy->qtd_dma);

	/* clear interrupt enables, set irq latency */
	if (log2_irq_thresh < 0 || log2_irq_thresh > 6)
		log2_irq_thresh = 0;
	temp = 1 << (16 + log2_irq_thresh);
	ehci->has_ppcd = 0;
	if (HCC_CANPARK(hcc_params)) {
		/* HW default park == 3, on hardware that supports it (like
		 * NVidia and ALI silicon), maximizes throughput on the async
		 * schedule by avoiding QH fetches between transfers.
		 *
		 * With fast usb storage devices and NForce2, "park" seems to
		 * make problems:  throughput reduction (!), data errors...
		 */
		if (park) {
			park = min(park, (unsigned) 3);
			temp |= CMD_PARK;
			temp |= park << 8;
		}
	}
	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		temp &= ~(3 << 2);
		temp |= (EHCI_TUNE_FLS << 2);
		switch (EHCI_TUNE_FLS) {
		case 0: ehci->periodic_size = 1024; break;
		case 1: ehci->periodic_size = 512; break;
		case 2: ehci->periodic_size = 256; break;
		default:	BUG();
		}
	}
	ehci->command = temp;
	hcd->has_tt = 1;
	hcd->self.sg_tablesize = 0;
	return 0;
}


static void NT72668_patch(struct usb_hcd *hcd){
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	unsigned int	command = ehci_readl(ehci, &ehci->regs->command);
	int retval,retry=0;

	command |= CMD_RESET;
	ehci_writel(ehci, command, &ehci->regs->command);
	do{
		retval = handshake (ehci, &ehci->regs->command,
			    CMD_RESET, 0, 250 * 1000);
		retry ++;
	}while(retval &&retry <3);

	if (unlikely(retval !=0 && retry >= 3))
		ehci_err(ehci, "reset fail!\n");

	command = ehci->command;

	ehci_writel(ehci, (command &~((unsigned int)(CMD_RUN|CMD_PSE|CMD_ASE))), &ehci->regs->command);
	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	retry=0;
	do{
		ehci_writel(ehci, INTR_MASK,
		    &ehci->regs->intr_enable); 
		retval = handshake (ehci, &ehci->regs->intr_enable,
			    INTR_MASK, INTR_MASK,250 );
		retry ++;
	}while(retval !=0 );
	if (unlikely(retval != 0))
		ehci_err(ehci, "write fail!\n");
	ehci->command &= ~((unsigned int)(CMD_PSE|CMD_ASE));
	set_bit(1, &hcd->porcd);
}


static const struct hc_driver ehci_NT72668_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"Novatek NT72668",
	.hcd_priv_size =	sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ehci_irq,
	.flags =		HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset =		NT72668_ehci_init,
	.start =		ehci_run,
	.stop =			ehci_stop,
	.shutdown =		ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ehci_urb_enqueue,
	.urb_dequeue =		ehci_urb_dequeue,
	.endpoint_disable =	ehci_endpoint_disable,
	.endpoint_reset =	ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ehci_hub_status_data,
	.hub_control =		ehci_hub_control,
	.bus_suspend =		ehci_bus_suspend,
	.bus_resume =		ehci_bus_resume,
	.relinquish_port =	ehci_relinquish_port,
	.port_handed_over =	ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
	.port_nc = NT72668_patch,
};




/*-------------------------------------------------------------------------*/

static int ehci_hcd_NT72668_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	int ret;

	pr_debug("In ehci_hcd_NT72668_drv_probe\n");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_ehci_NT72668_probe(&ehci_NT72668_hc_driver, &hcd, pdev);
	return ret;
}

static int ehci_hcd_NT72668_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	pr_debug("In ehci_hcd_NT72668_drv_remove\n");
	usb_ehci_NT72668_remove(hcd, pdev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int NT72668_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);
	struct platform_device *pdev = to_platform_device(dev);
	int rc;
	rc = ehci_suspend(hcd, do_wakeup);
	disable_irq(pdev->resource[2].start);	
	NT72668_stop(pdev);


	return rc;
}

static int NT72668_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	NT72668_init();
	NT72668_Plat_Resume(hcd);
	ehci_resume(hcd, false);
	enable_irq(pdev->resource[2].start);
	return 0;
}
#else
#define NT72668_ehci_suspend	NULL
#define NT72668_ehci_resume		NULL
#endif

static const struct dev_pm_ops NT72668_ehci_pm_ops = {
	.suspend	= NT72668_ehci_suspend,
	.resume		= NT72668_ehci_resume,
};


#ifdef CONFIG_OF
static const struct of_device_id NT72668_ehci_match[] = {
	{ .compatible = "nvt,NT72668-ehci0" },
	{},
};
MODULE_DEVICE_TABLE(of, NT72668_ehci_match);
#endif


MODULE_ALIAS("NT72668-ehci");
static struct platform_driver ehci_hcd_NT72668_driver = {
	.probe = ehci_hcd_NT72668_drv_probe,
	.remove = ehci_hcd_NT72668_drv_remove,
	.driver = {
		.name = "NT72668-ehci",
		.bus = &platform_bus_type,		
		.pm	= &NT72668_ehci_pm_ops,
	}	
};
