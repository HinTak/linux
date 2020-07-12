/*
 * xhci-nvt.c - xHCI host controller driver platform Bus Glue.
 *
 * Howard PH Chang <Howard_PH_Chang@novatek.com.tw>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/usb/phy.h>
#include "xhci.h"
#include <linux/usb/nvt_usb_phy.h>
#include "NT72668.h"

static struct hc_driver __read_mostly xhci_nvt_hc_driver;

static ssize_t store_compliance(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct xhci_hcd	*xhci;
	__le32 __iomem **port_array;
	SYS_en_u3_ssc();

	xhci = hcd_to_xhci(dev_get_drvdata(dev));
	port_array = xhci->usb3_ports;
	writel(0x00410340, port_array[0]);
	printk("[%s]write 0x00410340 to regs\n", __func__);
	return count;
}
static DEVICE_ATTR(compliance, S_IWUSR|S_IWGRP, NULL, store_compliance);

static ssize_t store_compliance_pattern(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_phy *phy = hcd->usb_phy;
	struct nvt_u3_phy *pphy;
	int val;

	if (sscanf(buf, "%d", &val) != 1 || val < 0 || val > 1)
		return -EINVAL;

	pphy = container_of(phy, struct nvt_u3_phy, u_phy);
	writel(0x81, pphy->regs + 0x24);
	writel(0xf1, pphy->regs + 0xa4);
	writel(0xf0, pphy->regs + 0x944);
	writel(0x0, pphy->regs + 0x984);
	writel(0x0, pphy->regs + 0x884);
	writel(0x93, pphy->regs + 0x9c0);
	writel(0x13, pphy->regs + 0x9c0);
	writel(0x1, pphy->regs + 0x9ac);
	writel(0x2, pphy->regs + 0x980);
	writel(0x0, pphy->regs + 0x980);

	if (val) /* CP1 */
		writel(0x11, pphy->regs + 0x9ac);
	else /* CP0 */
		writel(0x1, pphy->regs + 0x9ac);

	return count;
}
static DEVICE_ATTR(compliance_pattern, S_IWUSR|S_IWGRP, NULL, store_compliance_pattern);

static ssize_t store_loopback(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	int val;

	if (sscanf(buf, "%d", &val) != 1 || val < 0 || val > 1)
		return -EINVAL;

	if (val) /* Tell driver we'll enter loopback test */
		hcd->shared_hcd->loopback = 1;
	else /* Tell driver we'll leave loopback test */
		hcd->shared_hcd->loopback = 0;

	return count;
}
static DEVICE_ATTR(loopback, S_IWUSR|S_IWGRP, NULL, store_loopback);

/*
 * SW workaround for buggy device that cannot keep speed stable,
 * such as Toshiba Y-2
 */
static void xhci_nvt_patch2(struct usb_hcd *hcd)
{
	struct nvt_u3_phy *pphy;
	struct usb_phy *phy = hcd->usb_phy;

	if (!hcd->loopback) {
		pphy = container_of(phy, struct nvt_u3_phy, u_phy);
		/* Turn off Rx termination */
		writel(0x8, pphy->regs + 0x24);
		writel(0xf1, pphy->regs + 0xa4);
	}
}

/*
 * SW workaround for buggy device that cannot keep speed stable,
 * such as Netac HD
 */
static void xhci_nvt_patch(struct usb_hcd *hcd)
{
	struct nvt_u3_phy *pphy;
	struct usb_phy *phy = hcd->usb_phy;

	if (!hcd->loopback) {
		pphy = container_of(phy, struct nvt_u3_phy, u_phy);
		/* Turn off Rx termination */
		writel(0x8, pphy->regs + 0x24);
		writel(0xf1, pphy->regs + 0xa4);
		/* Reset phy */
		writel(0xFF, pphy->regs + 0x408);
		udelay(10);
		writel(0x0, pphy->regs + 0x408);
	}
}

/* SW workaround for device DCLS-P300
 * This device supports USB3.0 but violate below USB3.0 spec's rule:
 * "Simultaneous operation of SuperSpeed and non-SuperSpeed modes is not allowed for peripheral devices."
 * Firstly, this device enters non-SuperSpeed mode but also assert rx termination and does not send LFPS signal.
 * Cause downstream port LTSSM enter inactive state.
 * Then, SW issues warm reset to recover this situation but this device won't work well to change to SuperSpeed mode.
 *
 * The solution is to skip warm reset if this device is still in non-SuperSpeed mode.
 */
static int xhci_nvt_nc3(struct usb_hcd *hcd)
{
	#define USB_VENDOR_ID_DCLS_P300		0x04b4
	#define USB_PRODUCT_ID_DCLS_P300	0x4721

	if ((le16_to_cpu(hcd->primary_hcd->usb2_device_vid) == USB_VENDOR_ID_DCLS_P300) &&
			(le16_to_cpu(hcd->primary_hcd->usb2_device_pid) == USB_PRODUCT_ID_DCLS_P300)) {
		return 0;
	} else {
		return -ENODEV;
	}
}

static void xhci_nvt_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	* As of now platform drivers don't provide MSI support so we ensure
	* here that the generic code does not try to make a pci_dev from our
	* dev struct in order to setup MSI
	*/
	xhci->quirks |= XHCI_BROKEN_MSI;
}

static int xhci_nvt_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, xhci_nvt_quirks);
}

static int xhci_nvt_probe(struct platform_device *pdev)
{
	struct device_node	*node = pdev->dev.of_node;
	const struct hc_driver	*driver;
	struct xhci_hcd	*xhci;
	struct resource	res;
	struct usb_hcd	*hcd;
	int	ret;
	int	irq;
	struct nvt_u3_phy *pphy;
	struct usb_phy *phy2;

	if (usb_disabled())
		return -ENODEV;

	driver = &xhci_nvt_hc_driver;

	/* Try to set 64-bit DMA first */
	if (!pdev->dev.dma_mask)
		/* Platform did not initialize dma_mask */
		ret = dma_coerce_mask_and_coherent(&pdev->dev,
						   DMA_BIT_MASK(64));
	else
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	/* If seting 64-bit DMA mask fails, fall back to 32-bit DMA mask */
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			return ret;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, "xhci-nvt_20");
	if (!hcd) {
		dev_err(&pdev->dev, "fail to create xhci hcd\n");
		return -ENOMEM;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "xhci no dts regs\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->regs = devm_ioremap_resource(&pdev->dev, &res);
	if (IS_ERR(hcd->regs)) {
		dev_err(&pdev->dev, "fail to map regs\n");
		ret = PTR_ERR(hcd->regs);
		goto put_hcd;
	}

	hcd->rsrc_start = res.start;
	hcd->rsrc_len = resource_size(&res);

	irq = irq_of_parse_and_map(node, 0);
	if (irq == 0) {
		dev_err(&pdev->dev, "could not get usb irq\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	/* get usb2phy by device tree node */
	phy2 = devm_usb_get_phy_by_phandle(&pdev->dev, "usb2phy", 0);
	if (IS_ERR(phy2)) {
		dev_err(&pdev->dev, "could not get usb2phy structure, err %ld\n",
			PTR_ERR(up));
		ret = -ENODEV;
		goto dispose_irq_mapping;
	}

	/* for current requirement, we only need to init usb2phy  */
	usb_phy_init(phy2);

	/* get usb3phy by device tree node */
	hcd->usb_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "usb3phy", 0);
	if (IS_ERR(hcd->usb_phy)) {
		dev_err(&pdev->dev, "could not get usb3phy structure, err %ld\n",
			PTR_ERR(up));
		ret = -ENODEV;
		goto dispose_irq_mapping;
	}

	pphy = container_of(hcd->usb_phy, struct nvt_u3_phy, u_phy);

	if (of_property_read_u32(node, "id", &pphy->id) != 0) {
		dev_err(&pdev->dev, "could not get usb id\n");
		ret = -ENODEV;
		goto dispose_irq_mapping;
	}

	/*
	* Pass hcd reg address to phy driver. The certain workaround
	* operation needs hcd reg address in USB PHY driver.
	*/
	pphy->hcd_regs = hcd->regs;
	usb_phy_init(hcd->usb_phy);

#if defined(CONFIG_NVT_USB3_HW_INIT_WARMRESET)
	/*
	* portsc offset can compare to xhci->usb3_ports[0] when doing
	* porting task
	*/
	writel(0x800002e0, hcd->regs + 0x430);
#endif

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret) {
		dev_err(&pdev->dev, "fail to add xhci hcd\n");
		ret = -ENODEV;
		goto dispose_irq_mapping;
	}

	device_wakeup_enable(hcd->self.controller);

	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(driver, &pdev->dev,
			"xhci-nvt_30", hcd);
	if (!xhci->shared_hcd) {
		dev_err(&pdev->dev, "fail to create xhci shared hcd\n");
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	xhci->shared_hcd->regs = hcd->regs;

	/*
	 * Set the xHCI pointer before xhci_nvt_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	if (HCC_MAX_PSA(xhci->hcc_params) >= 4)
		xhci->shared_hcd->can_do_streams = 1;

	xhci->shared_hcd->usb_phy = hcd->usb_phy;
	ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);

	if (ret)
		goto put_usb3_hcd;
	usb_phy_vbus_on(hcd->usb_phy);

	device_create_file(&pdev->dev, &dev_attr_compliance);
	device_create_file(&pdev->dev, &dev_attr_compliance_pattern);
	device_create_file(&pdev->dev, &dev_attr_loopback);

	/* for USB3.0 port link state error handle */
	xhci->shared_hcd->inactive_cnt = 0x0;

#if defined(CONFIG_ARCH_NVT72671D)
	if (pphy->id == 1)
		/* usb3.0 PONRST */
		clear(1 << 8, pphy->apb_regs);
#endif

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

dispose_irq_mapping:
	irq_dispose_mapping(irq);

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int xhci_nvt_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_phy_shutdown(hcd->usb_phy);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	irq_dispose_mapping(hcd->irq);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int xhci_nvt_suspend(struct device *dev)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	/*
	 * xhci_suspend() needs `do_wakeup` to know whether host is allowed
	 * to do wakeup during suspend. Since xhci_plat_suspend is currently
	 * only designed for system suspend, device_may_wakeup() is enough
	 * to dertermine whether host is allowed to do wakeup. Need to
	 * reconsider this when xhci_plat_suspend enlarges its scope, e.g.,
	 * also applies to runtime suspend.
	 */
	return xhci_suspend(xhci, device_may_wakeup(dev));
}

static int xhci_nvt_resume(struct device *dev)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	usb_phy_init(hcd->usb_phy);

#if defined(CONFIG_NVT_USB3_HW_INIT_WARMRESET)
	writel(0x800002e0, hcd->regs + 0x430);
#endif

	return xhci_resume(xhci, 0);
}

static const struct dev_pm_ops xhci_nvt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xhci_nvt_suspend, xhci_nvt_resume)
};
#define DEV_PM_OPS	(&xhci_nvt_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static const struct of_device_id usb_xhci_nvt_of_match[] = {
	{ .compatible = "nvt,NT72668-xhci" },
	{ },
};
MODULE_DEVICE_TABLE(of, usb_xhci_nvt_of_match);

static struct platform_driver usb_xhci_nvt_driver = {
	.probe	= xhci_nvt_probe,
	.remove	= xhci_nvt_remove,
	.driver	= {
		.name = "nvt-xhci",
		.pm = DEV_PM_OPS,
		.of_match_table = of_match_ptr(usb_xhci_nvt_of_match),
	},
};

MODULE_ALIAS("NT72668-xhci");

static int __init xhci_nvt_init(void)
{
	xhci_init_driver(&xhci_nvt_hc_driver, xhci_nvt_setup);
	xhci_nvt_hc_driver.product_desc = "Novatek XHCI";
	xhci_nvt_hc_driver.port_nc = xhci_nvt_patch;
	xhci_nvt_hc_driver.port_nc2 = xhci_nvt_patch2;
	xhci_nvt_hc_driver.port_nc3 = xhci_nvt_nc3;

	return platform_driver_register(&usb_xhci_nvt_driver);
}

static void __exit xhci_nvt_exit(void)
{
	platform_driver_unregister(&usb_xhci_nvt_driver);
}

module_init(xhci_nvt_init);
module_exit(xhci_nvt_exit);

MODULE_DESCRIPTION("xHCI Platform Host Controller Driver");
MODULE_LICENSE("GPL");

