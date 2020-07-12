/*
 * phy-nvt-u3.c - NVT USB 3.0 PHY driver
 *
 * che-chun Kuo <c_c_kuo@novatek.com.tw>
 * Howard Chang <Howard_PH_Chang@novatek.com.tw>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/usb/phy.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/usb/nvt_usb_phy.h>
#include "../host/xhci.h"
#include "../host/NT72668.h"
//#include <soc/nvt/nvt_soc.h>
#ifdef CONFIG_NVT_CPE_DRV
#include <linux/cpe_trim.h>
#endif

#if defined(CONFIG_ARCH_NVT72673)
static const struct nvt_xhci_platform_data xhci_pdata[1] = {
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U2_U3_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30,
	},
};
#elif defined(CONFIG_ARCH_NVT72671D)
static const struct nvt_xhci_platform_data xhci_pdata[2] = {
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P2,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P2_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30_P2,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U2_U3_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30,
	},
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P5,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P5_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30_P5,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U0_U1_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30_P5_671D,
	},
};
#elif defined(CONFIG_ARCH_NVT72685)
static const struct nvt_xhci_platform_data xhci_pdata[2] = {
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P2,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P2_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30_P2,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U2_U3_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30,
	},
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P5,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_P5_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30_P5,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U0_U1_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30_P5_685,
	},
};
#else
static const struct nvt_xhci_platform_data xhci_pdata[1] = {
	{
		.hclk_rst		= EN_SYS_CLK_RST_AHB_USB30,
		.pclk_rst		= EN_SYS_CLK_RST_AHB_USB30_PCLK,
		.aclk_rst		= EN_SYS_CLK_RST_AXI_USB30,
		.clk_src_u2		= EN_SYS_CLK_SRC_USB_U2_U3_30M,
		.clk_src_u3		= EN_SYS_CLK_SRC_USB30,
	},
};
#endif

static void dt_reg_setting(struct device_node *dn, char *dn_name,
	void __iomem *base_addr)
{
	int len;
	u32	offset, mask, value, count;
	void __iomem *temp_addr;

	if (!of_get_property(dn, dn_name, &len))
		return;

	len /= 4;
/*
	printk(KERN_INFO "[USBPHY] number of property %s is :%d\n",
		dn_name, len);
*/
	if (len % 3) {
		pr_warn("[USBPHY] number of property nvt,phy_setting is not correct :%d\n",
			len);
		return;
	}

	for (count = 0; count < len; count += 3) {
		of_property_read_u32_index(dn,
			dn_name, count, &offset);
		of_property_read_u32_index(dn,
			dn_name, count + 1, &mask);
		of_property_read_u32_index(dn,
			dn_name, count + 2, &value);
/*
		printk(KERN_INFO "[USBPHY] offset %x, mask %x, value %x\n",
			offset, mask, value);
*/
		/* no default base address, offset will be exact address */
		if (base_addr == 0) {
			temp_addr = ioremap_nocache(offset, 0x100);
			if (temp_addr == NULL) {
				pr_warn("[USBPHY] fail to ioremap 0x%x\n",
					offset);
				return;
			}
		} else
			temp_addr = base_addr + offset;

		writel((readl(temp_addr) & ~mask) |
				value, temp_addr);
/*
		printk(KERN_INFO "[USBPHY] regs %x, regs value %x\n",
			offset, readl(temp_addr));
*/
		if (base_addr == 0)
			iounmap(temp_addr);
	}
}

static int nvt_u3_set_vbus(struct usb_phy *x, int on)
{
	pr_info("[%s]on %d\n", __func__, on);
	if (on)
		dt_reg_setting(x->dev->of_node, "vbus-on", 0);
	else
		dt_reg_setting(x->dev->of_node, "vbus-off", 0);

	return 0;
}

static u32 nvt_get_phy_page_reg(struct nvt_u3_phy *pphy, int page, u32 offset)
{
	u32 new_offset = offset + page * 0x400;

	return readl(pphy->regs + new_offset);
}

static void nvt_set_phy_page_reg(struct nvt_u3_phy *pphy, int page,
					u32 offset, u32 value)
{
	u32 new_offset = offset + page * 0x400;

	writel(value, pphy->regs + new_offset);
}

static u32 nvt_get_apb_reg(struct nvt_u3_phy *pphy, u32 offset)
{
	return readl(pphy->apb_regs + offset);
}

static void nvt_set_apb_reg(struct nvt_u3_phy *pphy, u32 offset, u32 value)
{
	writel(value, pphy->apb_regs + offset);
}

static void nvt_phy_work(unsigned long arg)
{
	struct nvt_u3_phy *pphy = (struct nvt_u3_phy *)arg;
	u32 temp2, temp, temp3;

	if (pphy->phybnvttest == 1)
		goto done;
	/* writel(2,(volatile void *)((unsigned int)pphy->regs + 0xbfc)); */
	/* base_addr = 0xfd18c000 */
	temp3 = readl(pphy->regs + 0x834) & 0xFF;
	/* regs->apb_regs */
	temp2 = readl(pphy->apb_regs + 0x18);
	temp = readl(pphy->hcd_regs + 0x430);

	/* restore controller status as normal */
	if (((temp2 & 0xFF) == 0x74) &&
		(((temp & 0xFFF) == 0x2e0) || ((temp & 0xFFF) == 0x2F0))) {
		writel((temp & ~PORT_POWER), pphy->hcd_regs + 0x430);
		udelay(50);
		writel((PORT_POWER), pphy->hcd_regs + 0x430);
	}
	/* writel(0,(volatile void *)((unsigned int)pphy->regs + 0xbfc)); */
	/* base_addr = 0xfd18c000 */
	temp2 = readl(pphy->regs + 0x150) & 0xF;
	/* restore phy status as normal */
	if ((temp2 == 0xE) || ((temp2 == 0x7) && (temp3 & 0x80))) {
		/* writel(0x1, pphy->regs + 0xbfc); */
		/* base_addr = 0xfd18c000 */
		writel(0xFF, pphy->regs + 0x408);
		udelay(10);
		/* base_addr = 0xfd18c000 */
		writel(0x0, pphy->regs + 0x408);
		mdelay(1);
		writel((temp | PORT_WR), pphy->hcd_regs + 0x430);
	}

	/* restore controller status as normal */
	/* regs->apb_regs */
	temp2 = readl(pphy->apb_regs + 0x18);
	if ((temp2 & 0xF0) == 0x40) {
		writel((temp & ~PORT_POWER), pphy->hcd_regs + 0x430);
		udelay(50);
		writel((PORT_POWER), pphy->hcd_regs + 0x430);
		writel((temp | PORT_RESET), pphy->hcd_regs + 0x430);
	}
done:
	mod_timer(&pphy->phy_timer, jiffies + msecs_to_jiffies(500));

}

static int nvt_u3_phy_init(struct usb_phy *phy)
{
	struct nvt_u3_phy *pphy;
	const struct nvt_xhci_platform_data *pdata = xhci_pdata;
	int len;
	u32 offset, mask, value, count;
	u32 bias_icdr_sel = 0xb;
#if (defined(CONFIG_ARCH_NVT72673) || defined(CONFIG_ARCH_NVT72671D) || defined(CONFIG_ARCH_NVT72685)) && defined(CONFIG_NVT_CPE_DRV)
	u32 chip_id, chip_type;
#endif

#if (defined(CONFIG_ARCH_NVT72673) || defined(CONFIG_ARCH_NVT72671D) || defined(CONFIG_ARCH_NVT72685)) && defined(CONFIG_NVT_CPE_DRV)
	chip_id = get_soc_id();
	chip_type = ntcpe_get_chip_type();	/* ES0 = 1, ES1 = 2 */

	if ((chip_id == NVT72671_CHIPID) || (chip_id == NVT72685_CHIPID) || ((chip_id == NVT72673_CHIPID) && (chip_type > 1))) {
		int tmp_bias_icdr_sel, rshift;

		/* get dts to get u3phy trim info */
		if (of_get_property(phy->dev->of_node, "nvt,u3phy_trim", &len)) {
			len /= 4;
			if (len % 2) {
				pr_warn("[USBPHY] number of property nvt,u3phy_trim is not correct :%d\n",
					len);
				return 0;
			}
			count = 0;
			of_property_read_u32_index(phy->dev->of_node, "nvt,u3phy_trim", count, &mask);
			of_property_read_u32_index(phy->dev->of_node, "nvt,u3phy_trim",	count+1, &rshift);
		}

		tmp_bias_icdr_sel = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_USB3_BIAS_ICDR_SEL);
		if(tmp_bias_icdr_sel == CPE_OTP_NO_VALID_TRIM_DATA)
			printk("[%s] illegal bias_icdr_sel(%d)\n", __func__, tmp_bias_icdr_sel);
		else {
			printk("[%s] tmp_bias_icdr_sel=0x%x, mask=0x%x, rshift=0x%x\n", __func__,
				tmp_bias_icdr_sel, mask, rshift);
			bias_icdr_sel = (tmp_bias_icdr_sel >> rshift) & mask;
		}

	}
	printk("chip_id 0x%x chip_type 0x%x bias_icdr_sel 0x%x\n", chip_id, chip_type, bias_icdr_sel);
#endif

	pphy = container_of(phy, struct nvt_u3_phy, u_phy);

	/* ssc setting */
	SYS_set_u3_ssc();

	/* 30M Clk switch */
	SYS_CLK_SetClockSource(pdata[pphy->id].clk_src_u2, 1);
	udelay(10);

	/* set aclk sw reset */
	SYS_SetClockReset(pdata[pphy->id].aclk_rst, true);
	udelay(0x20);
	/* set aclk sw clear */
	SYS_SetClockReset(pdata[pphy->id].aclk_rst, false);
	udelay(0x20);

	/* set pbus clk reset */
	SYS_SetClockReset(pdata[pphy->id].pclk_rst, true);
	udelay(0x20);
	/* set pbus clk clear */
	SYS_SetClockReset(pdata[pphy->id].pclk_rst, false);
	udelay(0x20);

	/* set hclk sw reset */
	SYS_SetClockReset(pdata[pphy->id].hclk_rst, true);
	udelay(0x20);
	/* set hclk sw clear */
	SYS_SetClockReset(pdata[pphy->id].hclk_rst, false);
	udelay(0x20);

	/* usb3.0 PONRST */
	/* regs->apb_regs */
	clear(1 << 8, pphy->apb_regs);
	set(1 << 8, pphy->apb_regs);
	udelay(10);

	/* 30M setting */
	/* regs->apb_regs */
	clear(0x3f << 20, pphy->apb_regs + 0xc);
	set(0x20 << 20, pphy->apb_regs + 0xc);

	/* phy setting */

	/* EOC RX_ICTRL's offset=0 */
	writel(0x40, pphy->regs + 0x6fc);

	/* TX_AMP_CTL=0x3, TX_DEC_EM_CTL=0xf */
	writel(0xfb, pphy->regs + 0x50);

	/* TX_LFPS_AMP_CTL=0x1 */
	writel(0xfc, pphy->regs + 0xd0);

	/* PHY power mode change ready response time = 3ms */
	writel(0xb, pphy->regs + 0x450);

	/* the low byte of vbs tune frequency count (560MHz, PLL=2.8GHz) */
	writel(0x2e, pphy->regs + 0x548);

	/* the high byte of vbs tune frequency count (560MHz, PLL=2.8GHz) */
	writel(0x01, pphy->regs + 0x54c);

	/* EOC use 2-data sampler output only */
	writel(0xc0, pphy->regs + 0x6c0);

	/* The low byte of EOC threshold */
	writel(0x91, pphy->regs + 0x6c4);

	/* The high byte of EOC threshold */
	writel(0x00, pphy->regs + 0x6c8);

	/* Enable VGA force mode */
	writel(0x88, pphy->regs + 0x4d4);

	/* VGA=5 */
	writel(0x50, pphy->regs + 0x4a8);

	/* Enable BIAS ICDR SEL force mode */
	writel(0x80, pphy->regs + 0x7c0);

	/* Set BIAS ICDR SEL = 0xB or OTP value*/
	bias_icdr_sel = (bias_icdr_sel << 4) | 0x1;
	writel(bias_icdr_sel, pphy->regs + 0x7d4);

	/* RX_ZRX's offset = 1*/
	writel(0x01, pphy->regs + 0x414);

	/* anaif_pc_rst=1 */
	writel(0x20, pphy->regs + 0x408);

	/* delay 1 us */
	udelay(1);

	/* anaif_pc_rst=0 */
	writel(0x00, pphy->regs + 0x408);

	/* get dts to setup phy setting */
	if (of_get_property(phy->dev->of_node, "nvt,phy_setting", &len)) {
		len /= 4;
/*
		printk (KERN_INFO
		"[USBPHY] number of property nvt,phy_setting is :%d\n",len);
*/
		if (len % 3) {
			pr_warn("[USBPHY] number of property nvt,phy_setting is not correct :%d\n",
				len);
			return 0;
		}
		for (count = 0; count < len; count += 3) {
			of_property_read_u32_index(
				phy->dev->of_node, "nvt,phy_setting",
				count, &offset);
			of_property_read_u32_index(
				phy->dev->of_node, "nvt,phy_setting",
				count + 1, &mask);
			of_property_read_u32_index(
				phy->dev->of_node, "nvt,phy_setting",
				count + 2, &value);
			writel((readl(pphy->regs + offset) & ~mask)
				| value, pphy->regs + offset);
		}
	}

	/*  125M Clk switch */
	SYS_CLK_SetClockSource(pdata[pphy->id].clk_src_u3, 1);
	udelay(10);

	/* debug attach, 673 doesn't this this. */
/*	set(1 << 8, pphy->hcd_regs + 0xc110); */

	/* UTMI 16 bit mode */
	set(1 << 3, pphy->hcd_regs + 0xc200);

	/* Host IN Auto Retry - enable */
	set(1 << 14, pphy->hcd_regs + 0xc12c);

	/*
	* fix sandisk disconnect issue with new fifo setting
	* Enable Elasticity Buffer Mode for PHY.
	*/
	set(1, pphy->hcd_regs + 0xc2c0);

	/*
	* fix super speed device disconnection when enter u3.
	* bit 28 raise from P3 to P2 when XHC want to request
	* rx termination detection. This problem may be caused
	* by low sleep clk of XHC
	*/
	set(1 << 28, pphy->hcd_regs + 0xc2c0);

	return 0;
}

static int nvt_u3_phy_disconnect(struct usb_phy *phy,
	enum usb_device_speed speed)
{
	struct nvt_u3_phy *pphy;

	pphy = container_of(phy, struct nvt_u3_phy, u_phy);
	writel(0xFF, pphy->regs + 0x408);
	udelay(10);
	writel(0x0, pphy->regs + 0x408);

	/* Turn on Rx termination */
	writel(0, pphy->regs + 0xa4);

#if defined(CONFIG_NVT_SEAGATE_BAD_RX_TERM_WK)
	/* 72673 set terminator controlled by controller */
	printk("[%s] restore rx term\n", __func__);
	writel(0, pphy->regs + 0x94c);
#endif

	return 0;
}

static int __exit nvt_u3_phy_remove(struct platform_device *pdev)
{
	struct resource res;
	struct nvt_u3_phy *pphy = platform_get_drvdata(pdev);
	struct device_node *dn = pdev->dev.of_node;
	int	ret;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "%s : missing mem resource\n", __func__);
		return -ENODEV;
	}
	release_mem_region(res.start, resource_size(&res));
	usb_remove_phy(&pphy->u_phy);

	ret = of_address_to_resource(dn, 1, &res);
	if (ret) {
		dev_err(&pdev->dev, "%s : missing mem resource\n", __func__);
		return -ENODEV;
	}
	release_mem_region(res.start, resource_size(&res));

	devm_kfree(&pdev->dev, pphy);
	return 0;
}

int nvt_u3_phy_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct nvt_u3_phy *pphy;
	struct device *dev = &pdev->dev;
	struct resource res;
	void __iomem	*phy_base;
	void __iomem	*apb_base;
	int	ret;

	pphy = devm_kzalloc(dev, sizeof(struct nvt_u3_phy), GFP_KERNEL);
	if (!pphy)
		return -ENOMEM;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		dev_err(dev, "missing mem resource\n");
		return -ENODEV;
	}

	pr_info("[USB3PHY] pdev : %s\n", pdev->name);

	if (!request_mem_region(res.start, resource_size(&res), pdev->name)) {
		dev_err(&pdev->dev, "memory base of usb3 phy is in use\n");
		return -EBUSY;
	}

	phy_base = devm_ioremap_nocache(dev, res.start, resource_size(&res));
	if (!phy_base) {
		dev_err(dev, "%s: register mapping failed\n", __func__);
		return -ENXIO;
	}

	ret = of_address_to_resource(dn, 1, &res);
	if (ret) {
		dev_err(dev, "missing apb mem resource\n");
		return -ENODEV;
	}

	if (!request_mem_region(res.start, resource_size(&res), pdev->name)) {
		dev_err(&pdev->dev, "memory base of usb3 phy apb is in use\n");
		return -EBUSY;
	}

	apb_base = devm_ioremap_nocache(dev, res.start, resource_size(&res));
	if (!apb_base) {
		dev_err(dev, "%s: register apb mapping failed\n", __func__);
		return -ENXIO;
	}
	pphy->apb_regs	= apb_base;

	pphy->regs	= phy_base;
	pphy->dev		= &pdev->dev;
	pphy->u_phy.dev	= pphy->dev;
	pphy->u_phy.label	= "nt72668-u3-phy";

	pphy->u_phy.init	= nvt_u3_phy_init;
	pphy->u_phy.notify_disconnect = nvt_u3_phy_disconnect;
	pphy->get_phy_page_reg = nvt_get_phy_page_reg;
	pphy->get_apb_reg = nvt_get_apb_reg;
	pphy->set_phy_page_reg = nvt_set_phy_page_reg;
	pphy->set_apb_reg = nvt_set_apb_reg;
	pphy->u_phy.set_vbus = nvt_u3_set_vbus;
	/* TODO: HC_STATE_HALT should be used by hcd.status? */
	/* pphy->status = HC_STATE_HALT; */
	ret = usb_add_phy_dev(&pphy->u_phy);
	if (ret)
		goto err;

	init_timer(&pphy->phy_timer);
	pphy->phy_timer.data = (unsigned long) pphy;
	pphy->phy_timer.function = nvt_phy_work;
	pphy->phy_timer.expires = jiffies + msecs_to_jiffies(300);

	platform_set_drvdata(pdev, pphy);

err:
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id nvt_usb3phy_match[] = {
	{ .compatible = "nvt,NT72668-usb3phy" },
	{},
};
#endif

static struct platform_driver nvt_usb3phy_driver = {
	.probe = nvt_u3_phy_probe,
	.remove = nvt_u3_phy_remove,
	.driver = {
		.name = "nvt-usb3phy",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(nvt_usb3phy_match),
#endif
	}
};

static int __init phy_nvt_u3_init(void)
{
	return platform_driver_register(&nvt_usb3phy_driver);
}

static void __exit phy_nvt_u3_cleanup(void)
{
	platform_driver_unregister(&nvt_usb3phy_driver);
}

module_init(phy_nvt_u3_init);
module_exit(phy_nvt_u3_cleanup);
MODULE_LICENSE("GPL");

