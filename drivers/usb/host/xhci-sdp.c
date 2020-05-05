/*
 * xhci-sdp.c - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * A lot of code borrowed from the Linux xHCI driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include "xhci.h"
static void __iomem *xhci_phy_base;
static void __iomem *xhci_lnk_base;
static void __iomem *xhci_rst_base;


static void sdp_xhci_init(struct device *dev, struct xhci_hcd *xhci)
{
    pr_info("xHCI Initilize.....\n");
#if 1
    xhci_phy_base = ioremap(0x100fc000, 0x1000);
    xhci_lnk_base = ioremap(0x1020c000, 0x1000);
    xhci_rst_base = ioremap(0x10b00000, 0x1000);
     
    xhci_writel(xhci, 0x20000|xhci_readl(xhci, (__le32*)(xhci_phy_base + 0x48)), (__le32*)(xhci_phy_base + 0x48));//Phy
    
    xhci_writel(xhci, 0x40|xhci_readl(xhci, (__le32*)(xhci_rst_base+0x954)), (__le32*)(xhci_rst_base+0x954));//reset85 release
    xhci_writel(xhci, 0x4|xhci_readl(xhci, (__le32*)(xhci_rst_base+0x958)), (__le32*)(xhci_rst_base+0x958));//reset86 release

    
    xhci_writel(xhci, 0x2540, (__le32*)(xhci_lnk_base+0x200));//Link
    xhci_writel(xhci, 0x0, (__le32*)(xhci_lnk_base+0x2c0));//Link

//    xhci_writel(xhci, 0x10000|xhci_readl(xhci, (__le32*)(xhci_phy_base+0x4c)), (__le32*)(xhci_phy_base+0x4c)); //VBus Enable
#endif

  
}

static void sdp_xhci_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_BROKEN_MSI;
}

/* called during probe() after chip reset completes */
static int sdp_xhci_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, sdp_xhci_quirks);	
}

static const struct hc_driver sdp_xhci_driver = {
	.description =		"sdp-xhci",
	.product_desc =		"xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		sdp_xhci_setup,
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

static u64 sdp_dma_mask = DMA_BIT_MASK(32);

static int sdp_xhci_probe(struct platform_device *pdev)
{
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd;
	int			ret;
	int			irq;


	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Snce share usb code relies on it, set it here for now
	 * Once we move to full device tree support this will 
	 */
	if (!pdev->dev.dma_mask)
	    pdev->dev.dma_mask = &sdp_dma_mask;

#if defined(CONFIG_ARM_LPAE)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else	
	if (!pdev->dev.coherent_dma_mask)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	

	sdp_xhci_init(&pdev->dev, xhci);

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	hcd = usb_create_hcd(&sdp_xhci_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd){
		dev_err(&pdev->dev, "Unable to create xhci HCD\n");
		return -ENOMEM;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				sdp_xhci_driver.description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto put_hcd;
	}

	hcd->regs = devm_ioremap_nocache(&pdev->dev, hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto release_mem_region;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto unmap_registers;

	/* USB 2.0 roothub is stored in the platform_device now. */
	hcd = dev_get_drvdata(&pdev->dev);
	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(&sdp_xhci_driver, &pdev->dev,
			dev_name(&pdev->dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	/*
	 * Set the xHCI pointer before sdp_xhci_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (ret){
		goto put_usb3_hcd;
	}

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

static int sdp_xhci_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	usb_put_hcd(hcd);
	kfree(xhci);

	return 0;
}

static const struct of_device_id sdp_xhci_match[]= {
    {.compatible = "samsung,sdp-xhci" },
    {},
};

static struct platform_driver usb_xhci_driver = {
	.probe	= sdp_xhci_probe,
	.remove	= sdp_xhci_remove,
	.driver	= {
		.name = "sdp-xhci",
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,
		.of_match_table = of_match_ptr(sdp_xhci_match),
	},
};
MODULE_ALIAS("platform:sdp-xhci");

int xhci_register_plat(void)
{
	return platform_driver_register(&usb_xhci_driver);
}

void xhci_unregister_plat(void)
{
	platform_driver_unregister(&usb_xhci_driver);
}
