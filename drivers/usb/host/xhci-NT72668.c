/*
 * xhci-NT72668.c - xHCI host controller driver platform Bus Glue.
 *
 * che-chun Kuo <c_c_kuo@novatek.com.tw>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb/otg.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>

#include "xhci.h"
#include "NT72668.h"

static void nvt_phy_work(unsigned long arg)
{
	struct nvt_u3_phy *pphy;
	u32 temp2, temp;
	struct usb_phy *phy = usb_get_phy(USB_PHY_TYPE_USB3);
	pphy = container_of(phy, struct nvt_u3_phy, u_phy);

	temp2 = readl((volatile void *)((unsigned int)pphy->regs[2] + 0x18));
	temp = readl((volatile void *)((unsigned int)pphy->regs[0] + 0x430));
	if(((temp2 & (0xFF)) == 0x74) && ((temp &(0xFFF)) == 0x2e0)) {
		writel((temp & ~PORT_POWER) ,((volatile void *)((unsigned int)pphy->regs[0] + 0x430)));
		udelay(50);
		writel((PORT_POWER) ,((volatile void *)((unsigned int)pphy->regs[0] + 0x430)));
	}
	mod_timer(&pphy->phy_timer, jiffies + msecs_to_jiffies(500));

}


static int nt72668_u3_phy_init(struct usb_phy *phy)
{
	struct nvt_u3_phy *pphy;
	
	pphy = container_of(phy, struct nvt_u3_phy, u_phy);
	
	//usb2.0 PONRST	 
	set(pphy->regs[1], 1 << 8);
	clear(pphy->regs[1], 1 << 8);
	udelay(10);

	//usb3.0 PONRST
	clear(pphy->regs[2], 1 << 8);
	set(pphy->regs[2], 1 << 8);
	udelay(10);
	
	//ssc setting
	NVTRST = ioremap_nocache((unsigned long)0xfd020000, 0x1000);
	*(volatile unsigned long *)((unsigned int)NVTRST+0xbc) = 0x2;  
	udelay(0x20);
	iounmap(NVTRST);
	NVTRST = ioremap_nocache((unsigned long)0xfd670000, 0x1000);
	*(volatile unsigned long *)((unsigned int)NVTRST+ 0x36*4) = 0xBB;
	*(volatile unsigned long *)((unsigned int)NVTRST+ 0x35*4) = 0x3b;
	*(volatile unsigned long *)((unsigned int)NVTRST+ 0x38*4) = 0x4;
	udelay (0x20);
	*(volatile unsigned long *)((unsigned int)NVTRST+ 0x35*4) = 0x33;
	udelay(0x20);
	iounmap(NVTRST);

	//30M Clk switch
	SYS_CLK_SetClockSource(EN_SYS_CLK_SRC_USB_U2_U3_30M,1);
	udelay(10);

	//# set Aclk sw reset  
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB30, true);
	udelay(0x20);
	//# set Aclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_AXI_USB30, false);
	udelay(0x20);

	//set pbus clk reset
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB30_PCLK,true);
	udelay(0x20);
	SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB30_PCLK,false);
	udelay(0x20);

	//# set uclk sw reset  
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB30_HCLK_D2, true);
	udelay(0x20);
	//# set uclk sw clear
	SYS_SetClockReset(EN_SYS_CLK_RST_CORE_USB30_HCLK_D2, false);
	udelay(0x20);

	//lfps
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x10,(volatile void *)((unsigned int)pphy->regs[2] + 0x828));

	//line rc  
	writel(0x2,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x80,(volatile void *)((unsigned int)pphy->regs[2] + 0x9d0));

	//AEQ setting 
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0xcf,(volatile void *)((unsigned int)pphy->regs[2] + 0x9c4));
	
	//VBS change delay
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0xa0,(volatile void *)((unsigned int)pphy->regs[2] + 0x95c));

	//force lfps detect
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x1f,(volatile void *)((unsigned int)pphy->regs[2] + 0x8a8));
	writel(0x10,(volatile void *)((unsigned int)pphy->regs[2] + 0x828));

	//#force RX_EN_LPF
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x45,(volatile void *)((unsigned int)pphy->regs[2] + 0x858));
	writel(0x01,(volatile void *)((unsigned int)pphy->regs[2] + 0x8d8));

	//#enable LPF
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x6,(volatile void *)((unsigned int)pphy->regs[2] + 0x90c));

	//# abj
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0x900));

	//#comparator
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x10,(volatile void *)((unsigned int)pphy->regs[2] + 0x8f0));
	writel(0x0,(volatile void *)((unsigned int)pphy->regs[2] + 0x870));


	//#rx     
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x4,(volatile void *)((unsigned int)pphy->regs[2] + 0x814));

	//#tx
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x40,(volatile void *)((unsigned int)pphy->regs[2] + 0x844));

	//#trigger 
	writel(0x3,(volatile void *)((unsigned int)pphy->regs[2] + 0x80c));

	//# pre_emphasis
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0xff,(volatile void *)((unsigned int)pphy->regs[2] + 0x8d0));
	writel(0x8b,(volatile void *)((unsigned int)pphy->regs[2] + 0x850));

	//#change TX current
	writel(0x15,(volatile void *)((unsigned int)pphy->regs[2] + 0x810));

	//# change AEQ 
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x7,(volatile void *)((unsigned int)pphy->regs[2] + 0x90c));

	//#change wss_mode 
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x64,(volatile void *)((unsigned int)pphy->regs[2] + 0x908));
	writel(0xff,(volatile void *)((unsigned int)pphy->regs[2] + 0x910));
	
	//# disable spcur_en_in_lfps
	writel(2,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x4,(volatile void *)((unsigned int)pphy->regs[2] + 0x9cc));

	//# disable LFPS_K
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x69,(volatile void *)((unsigned int)pphy->regs[2] + 0x824));



       //#int LDO enable
       writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
       writel(0x2e,(volatile void *)((unsigned int)pphy->regs[2] + 0x804));
       writel(0xf0,(volatile void *)((unsigned int)pphy->regs[2] + 0x880));
       writel(0xd1,(volatile void *)((unsigned int)pphy->regs[2] + 0x800));
       
       //#modify EQ LDO level
       writel(0x64,(volatile void *)((unsigned int)pphy->regs[2] + 0x80c));

	//#change RX ICP
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x85,(volatile void *)((unsigned int)pphy->regs[2] + 0x858));
	
	//# force rrange
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x2c,(volatile void *)((unsigned int)pphy->regs[2] + 0x9c8));
	
	//# change AEQ
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0xc,(volatile void *)((unsigned int)pphy->regs[2] + 0x9c0));
	
	//#rx rcal offset     
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x3,(volatile void *)((unsigned int)pphy->regs[2] + 0x814));
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x40,(volatile void *)((unsigned int)pphy->regs[2] + 0x844));
	
	
	//#force RSW 
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x08,(volatile void *)((unsigned int)pphy->regs[2] + 0x898));
	writel(0x18,(volatile void *)((unsigned int)pphy->regs[2] + 0x874));
	
	//#change WSS 
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x24,(volatile void *)((unsigned int)pphy->regs[2] + 0x908));

	//ssc setting fine tune
	NVTRST = ioremap_nocache((unsigned long)0xfd020000, 0x1000);
	*(volatile unsigned long *)((unsigned int)NVTRST+0xbc) = 0x2;  
	udelay(0x20);
	iounmap(NVTRST);
	NVTRST = ioremap_nocache((unsigned long)0xfd670000, 0x1000);
	*(volatile unsigned long *)((unsigned int)NVTRST+ 0xd4) = 0x31;
	udelay(0x20);
	iounmap(NVTRST);

	//#change pre-emphsis 
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x03,(volatile void *)((unsigned int)pphy->regs[2] + 0x850));
	
	//#reset PHY
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x01,(volatile void *)((unsigned int)pphy->regs[2] + 0x808));
	writel(0x00,(volatile void *)((unsigned int)pphy->regs[2] + 0x808));
	
	//#add RX resist noise 
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x44,(volatile void *)((unsigned int)pphy->regs[2] + 0x80c));
	writel(0xa5,(volatile void *)((unsigned int)pphy->regs[2] + 0x858));
	 
	//125M Clk switch
	SYS_CLK_SetClockSource(EN_SYS_CLK_SRC_USB30,1);
	udelay(10);

	//#for sandisk disconnect
	writel(0,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x19,(volatile void *)((unsigned int)pphy->regs[2] + 0x82c));
	
	//using digital circuits to check CDR output signal
	writel(1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0x9,(volatile void *)((unsigned int)pphy->regs[2] + 0xa60));

	//modify rx det
	set(((unsigned int)pphy->regs[0] + 0xc110), 1 << 8);
	set(((unsigned int)pphy->regs[0] + 0xc200), 1 << 3);
	set(((unsigned int)pphy->regs[0] + 0xc12c), 1 << 14);
	return 0;
}


static int nt72668_u3_phy_disconnect(struct usb_phy *phy,
                enum usb_device_speed speed)
{
	struct nvt_u3_phy *pphy;
	pphy = container_of(phy, struct nvt_u3_phy, u_phy);
	writel(0x1,(volatile void *)((unsigned int)pphy->regs[2] + 0xbfc));
	writel(0xFF,(volatile void *)((unsigned int)pphy->regs[2] + 0x808));
	writel(0x0,(volatile void *)((unsigned int)pphy->regs[2] + 0x808));
	//for sandisk disconenct issue
	if(readl((const volatile void *)((unsigned int)pphy->regs[0] + 0xc110)) & 1 << 3){
		clear(((unsigned int)pphy->regs[0] + 0xc110), 1 << 3);
	}
    return 0;
}


static int nt72668_u3_phy_probe(struct platform_device *pdev, struct usb_hcd	*phcd)
{
	struct nvt_u3_phy *pphy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem	*phy_base;
	int	ret;
	unsigned int index;
	struct usb_hcd	*hcd = phcd;

	pphy = devm_kzalloc(dev, sizeof(struct nvt_u3_phy), GFP_KERNEL);
	if (!pphy)
		return -ENOMEM;

	for (index = 1; index < 3; index ++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, index);
		if (!res) {
			dev_err(dev, "missing mem resource\n");
			return -ENODEV;
		}
		phy_base = devm_ioremap_nocache(dev, res->start, resource_size(res));
		if (!phy_base) {
			dev_err(dev, "%s: register mapping failed\n", __func__);
			return -ENXIO;
		}
		pphy->regs[index]	= phy_base;
	}
	pphy->regs[0]	= hcd->regs;
	
	pphy->dev		= &pdev->dev;
	pphy->u_phy.dev	= pphy->dev;
	pphy->u_phy.label	= "nt72668-u3-phy";
	pphy->u_phy.init	= nt72668_u3_phy_init;
	pphy->u_phy.notify_disconnect= nt72668_u3_phy_disconnect;
	pphy->status = HC_STATE_HALT;
	ret = usb_add_phy(&pphy->u_phy, USB_PHY_TYPE_USB3);

	init_timer(&pphy->phy_timer);
	pphy->phy_timer.data = (unsigned long) phcd;
	pphy->phy_timer.function = nvt_phy_work;
	pphy->phy_timer.expires = jiffies + msecs_to_jiffies(300);
	
	if (ret)
		goto err;


err:
	return ret;
}


static int __exit nt72668_u3_phy_remove(struct platform_device *pdev)
{
	usb_remove_phy(usb_get_phy(USB_PHY_TYPE_USB3));

	return 0;
}

static void xhci_NT72668_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_BROKEN_MSI;
}


static int xhci_NT72668_setup(struct usb_hcd *hcd)
{
#ifdef CONFIG_ARCH_SDP1202
#define XHCI_GRXTHRCFG_OFFSET   (0xc10c)

	/*RX FIFO threadshold*/
	/*
		[29]receive packet count enable
		[27:24]Receive packet count
		[23:19]Max Receive Burst Szie 		
	*/
	int ret = 0;
	int temp;
	
	ret = xhci_gen_setup(hcd, xhci_plat_quirks);

	if (hcd->regs && usb_hcd_is_primary_hcd(hcd)) {
		temp = 1 << 29 | 3 << 24 | 3 << 19;
		writel(temp, (hcd->regs + XHCI_GRXTHRCFG_OFFSET));
		pr_info("xhci(Fox-AP) : Apply RxFifo Threshold control\n");
	}

	return ret;
#else
	return xhci_gen_setup(hcd, xhci_NT72668_quirks);
#endif

	
}

static const struct hc_driver xhci_NT72668_xhci_driver = {
	.description =		"xhci-hcd",
	.product_desc =		"Novatek NT72668 u3",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		xhci_NT72668_setup,
	.start =		xhci_run,
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
#ifdef SAMSUNG_PATCH_WITH_NEW_xHCI_API_FOR_BUGGY_DEVICE
	.enable_control_endpoint= xhci_enable_control_endpoint, /*Aman, added for BSR=1 address command*/
#endif

	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/* Root hub support */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,
};

int xhci_NT72668_probe(struct platform_device *pdev)
{
	const struct hc_driver	*driver;
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd;
	int			ret;

	if (usb_disabled())
		return -ENODEV;


	driver = &xhci_NT72668_xhci_driver;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	hcd = usb_create_hcd(driver, &pdev->dev, "NT72668-xhci");

	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto put_hcd;
	}

	hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto release_mem_region;
	}
	
	nt72668_u3_phy_probe(pdev,hcd);	
	usb_phy_init(usb_get_phy(USB_PHY_TYPE_USB3));


	hcd->phy = usb_get_phy(USB_PHY_TYPE_USB3);
	ret = usb_add_hcd(hcd, pdev->resource[3].start, IRQF_SHARED);
	if (ret)
		goto unmap_registers;


	hcd = dev_get_drvdata(&pdev->dev);
	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(driver, &pdev->dev,
			dev_name(&pdev->dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	/*
	 * Set the xHCI pointer before xhci_NT72668_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	ret = usb_add_hcd(xhci->shared_hcd, pdev->resource[3].start, IRQF_SHARED);
	xhci->shared_hcd->phy = usb_get_phy(USB_PHY_TYPE_USB3);

	if (ret)
		goto put_usb3_hcd;

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

unmap_registers:
	iounmap(hcd->regs);

release_mem_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int xhci_NT72668_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	kfree(xhci);
	nt72668_u3_phy_remove(dev);

	return 0;
}

#if defined(CONFIG_PM)
static int NT72668_xhci_suspend(struct device *dev)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct nvt_u3_phy *pphy;
	
	pphy = container_of(hcd->phy, struct nvt_u3_phy, u_phy);
	pphy->status = HC_STATE_SUSPENDED;
	return xhci_suspend(xhci);
}

static int NT72668_xhci_resume(struct device *dev)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct nvt_u3_phy *pphy;
	
	pphy = container_of(hcd->phy, struct nvt_u3_phy, u_phy);
	pphy->status = HC_STATE_RESUMING;
	
	usb_phy_init(usb_get_phy(USB_PHY_TYPE_USB3));
	return xhci_resume(xhci, false);
}
#else
#define NT72668_xhci_suspend	NULL
#define NT72668_xhci_resume	NULL

#endif

static const struct dev_pm_ops NT72668_xhci_pm_ops = {
	.suspend	= NT72668_xhci_suspend,
	.resume		= NT72668_xhci_resume,
};




static struct platform_driver usb_xhci_driver = {
	.probe	= xhci_NT72668_probe,
	.remove	= xhci_NT72668_remove,
	.driver	= {
		.name = "NT72668-xhci",
		.bus  = &platform_bus_type,		
		.pm   = &NT72668_xhci_pm_ops,
	},
};
MODULE_ALIAS("NT72668-xhci");

int xhci_register_plat(void)
{
	return platform_driver_register(&usb_xhci_driver);
}

void xhci_unregister_plat(void)
{
	platform_driver_unregister(&usb_xhci_driver);
}
