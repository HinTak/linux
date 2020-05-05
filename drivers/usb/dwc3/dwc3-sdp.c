/**
 * dwc3-sdp.c - Samsung SDP DWC3 Specific Glue layer, cloned from dwc3-exynos.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Ikjoon Jang <ij.jang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>
#include <linux/of.h>
#include <mach/soc.h>
#include "core.h"

struct dwc3_sdp {
	struct platform_device	*dwc3;
	struct platform_device	*usb2_phy;
	struct platform_device	*usb3_phy;
	struct device		*dev;
	struct clk		*clk;
};

static struct platform_device* dwc3_sdp_register_nop_phy(
		struct nop_usb_xceiv_platform_data *pdata, int id)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc("nop_usb_xceiv", id);
	if (!pdev)
		return NULL;
	
	ret = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (ret)
		goto err;
	
	ret = platform_device_add(pdev);
	if (ret)
		goto err;

	return pdev;
err:
	platform_device_put(pdev);
	return NULL;
}

static int dwc3_sdp_register_phys(struct dwc3_sdp *sdp)
{
	struct platform_device	*pdev_usb2, *pdev_usb3;
	
	struct nop_usb_xceiv_platform_data pdata_usb2 = {
		.type = USB_PHY_TYPE_USB2,
	};
	struct nop_usb_xceiv_platform_data pdata_usb3 = {
		.type = USB_PHY_TYPE_USB3,
	};

	pdev_usb2 = dwc3_sdp_register_nop_phy(&pdata_usb2, 0);
	pdev_usb3 = dwc3_sdp_register_nop_phy(&pdata_usb3, 1);
	
	if (!pdev_usb2 || !pdev_usb3)
		goto err;

	sdp->usb2_phy = pdev_usb2;
	sdp->usb3_phy = pdev_usb3;

	return 0;
err:
	if (pdev_usb2)  {
		platform_device_del(pdev_usb2);
		platform_device_put(pdev_usb2);
	}
	if (pdev_usb3)  {
		platform_device_del(pdev_usb3);
		platform_device_put(pdev_usb3);
	}
	return -EBUSY;
}

static struct dwc3_platform_data dwc3_pdata =  {
	.mode	= DWC3_MODE_UNKNOWN,
	.flags	= DWC3_PDATA_PM_NEED_REINIT,
};

/* parse of properties of dwc3 and add platform data to pdev */
static void dwc3_parse_of_properties(struct device_node *np, struct platform_device *dwc3_pdev)
{
	const char *val;

	if (!np)
		return;
	if (of_property_read_string(np, "dwc3_mode", &val) < 0) {
		pr_warn("of property 'dwc3_mode' is missing.\n");
		return;
	}

	if (!strcmp(val, "host")) {
		dwc3_pdata.mode = DWC3_MODE_HOST;
	} else if (!strcmp(val, "device")) {
		dwc3_pdata.mode = DWC3_MODE_DEVICE;
	}

	platform_device_add_data(dwc3_pdev, &dwc3_pdata, sizeof(dwc3_pdata));
}

static void sdp_parse_boardtype(struct platform_device *dwc3_pdev)
{
	if (soc_is_sdp1304()) {
		/* Golf-P special board support: board type param overrides operaional mode.
		 * 17 Dec 2013 requested by nicolao.han */
		switch (get_sdp_board_type()) {
		case SDP_BOARD_DEFAULT:
			return;
		case SDP_BOARD_SBB:
			dwc3_pdata.mode = DWC3_MODE_DEVICE;
			break;
		default:
			dwc3_pdata.mode = DWC3_MODE_HOST;
			break;
		}
		platform_device_add_data(dwc3_pdev, &dwc3_pdata, sizeof(dwc3_pdata));
	}
}

static u64 dwc3_sdp_dma_mask = DMA_BIT_MASK(32);

static int dwc3_sdp_probe(struct platform_device *pdev)
{
	struct platform_device	*dwc3;
	struct dwc3_sdp		*sdp;
#if 0
	struct clk		*clk;
#endif
	int			ret = -ENOMEM;

	sdp = kzalloc(sizeof(*sdp), GFP_KERNEL);
	if (!sdp) {
		dev_err(&pdev->dev, "not enough memory\n");
		goto err0;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &dwc3_sdp_dma_mask;
#if defined(CONFIG_ARM_LPAE)
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif

	platform_set_drvdata(pdev, sdp);

	ret = dwc3_sdp_register_phys(sdp);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register PHYs\n");
		goto err1;
	}

	dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc3) {
		dev_err(&pdev->dev, "couldn't allocate dwc3 device\n");
		goto err1;
	}
	pr_info("dwc3 dev created dev=%p\n", &dwc3->dev);

#if 0
	clk = clk_get(&pdev->dev, "usbdrd30");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "couldn't get clock\n");
		ret = -EINVAL;
		goto err3;
	}
#endif

	dma_set_coherent_mask(&dwc3->dev, pdev->dev.coherent_dma_mask);

	dwc3->dev.parent = &pdev->dev;
	dwc3->dev.dma_mask = pdev->dev.dma_mask;
	dwc3->dev.coherent_dma_mask = pdev->dev.coherent_dma_mask;
	dwc3->dev.dma_parms = pdev->dev.dma_parms;
	sdp->dwc3	= dwc3;
	sdp->dev	= &pdev->dev;
#if 0
	sdp->clk	= clk;

	clk_enable(sdp->clk);
#endif

	ret = platform_device_add_resources(dwc3, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "couldn't add resources to dwc3 device\n");
		goto err4;
	}

	dwc3_parse_of_properties(pdev->dev.of_node, dwc3);
	/* board-type param will override OF property */
	sdp_parse_boardtype(dwc3);

	ret = platform_device_add(dwc3);
	if (ret) {
		dev_err(&pdev->dev, "failed to register dwc3 device\n");
		goto err4;
	}

	return 0;

err4:
#if 0
	clk_disable(clk);
	clk_put(clk);
err3:
#endif
	platform_device_put(dwc3);
err1:
	kfree(sdp);
err0:
	return ret;
}

static int dwc3_sdp_remove(struct platform_device *pdev)
{
	struct dwc3_sdp	*sdp = platform_get_drvdata(pdev);

	platform_device_unregister(sdp->dwc3);
	platform_device_unregister(sdp->usb2_phy);
	platform_device_unregister(sdp->usb3_phy);

#if 0
	clk_disable(sdp->clk);
	clk_put(sdp->clk);
#endif

	kfree(sdp);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sdp_dwc3_match[] = {
	{ .compatible = "samsung,sdp-dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_dwc3_match);
#endif

static struct platform_driver dwc3_sdp_driver = {
	.probe		= dwc3_sdp_probe,
	.remove		= dwc3_sdp_remove,
	.driver		= {
		.name	= "sdp-dwc3",
		.of_match_table = of_match_ptr(sdp_dwc3_match),
	},
};

module_platform_driver(dwc3_sdp_driver);

MODULE_ALIAS("platform:sdp-dwc3");
MODULE_AUTHOR("Ikjoon Jang<ij.jang@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 SDP Glue Layer");

