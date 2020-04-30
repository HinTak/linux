/*
 * phy-nvt-u2.c - NVT USB 2.0 PHY driver
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
#include "../host/NT72668.h"
#include <linux/usb/nvt_usb_phy.h>
#include <linux/bitops.h>
#include <linux/cpe.h>

static u32 phy_reset_flag;

static u32 u2_get_apb_reg(struct nvt_u2_phy *pphy, u32 offset)
{
	return readl(pphy->apb_regs + offset);
}

static void u2_set_apb_reg(struct nvt_u2_phy *pphy, u32 offset, u32 value)
{
	writel(value, pphy->apb_regs + offset);
}

static u32 u2_get_phy_reg(struct nvt_u2_phy *pphy, u32 offset)
{
	return readl(pphy->phy_regs + offset);
}

static void u2_set_phy_reg(struct nvt_u2_phy *pphy, u32 offset, u32 value)
{
	writel(value, pphy->phy_regs + offset);
}

void dt_reg_setting(struct device_node *dn, char *dn_name,
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

static int nt72668_u2_set_vbus(struct usb_phy *x, int on)
{
	if (on)
		dt_reg_setting(x->dev->of_node, "vbus-on", 0);
	else
		dt_reg_setting(x->dev->of_node, "vbus-off", 0);
	return 0;
}

static int nt72668_u2_phy_init(struct usb_phy *phy)
{
	int len, phy_group, ret;
	u32 offset, mask, value, count;
	struct resource res, apb_res;
	void __iomem	*apb_ponrst_base;
	void __iomem	*chip_id_base;
	u32 chip_id, chip_ver;
	struct nvt_u2_phy *pphy;
	int trim_data;

	chip_id_base = devm_ioremap_nocache(phy->dev, 0xfd020100, 0x1000);
	chip_id = readl(chip_id_base) & 0xfff;
	chip_ver = readl(chip_id_base + 0x4) & 0xff;
	devm_iounmap(phy->dev, chip_id_base);

	pphy = container_of(phy, struct nvt_u2_phy, u_phy);

	if (!pphy->usb3_controller) {
		/* clear of APB read period */
		clear(0x7 << 5, pphy->apb_regs);
		udelay(0x20);

		/* set APB read period */
		set(0x5 << 5, pphy->apb_regs);
		udelay(0x20);
	}

	/* # check phy group reset flag  */
	ret = 1;
	if (!of_property_read_u32(phy->dev->of_node,
		"nvt,phy_group", &phy_group)) {
		if (phy_group < 32 && !(phy_reset_flag & BIT(phy_group))) {
			phy_reset_flag |= BIT(phy_group);

			/* get common register base of multi port phy */
			ret = of_address_to_resource(phy->dev->of_node,
				0, &apb_res);
			if (ret) {
				dev_err(phy->dev, "missing device tree node APB\n");
				return -ENODEV;
			}

			ret = of_address_to_resource(phy->dev->of_node,
				1, &res);
			if (ret) {
				dev_err(phy->dev, "missing device tree node PONRST APB\n");
				return -ENODEV;
			}

			apb_ponrst_base = devm_ioremap_nocache(phy->dev,
				res.start, resource_size(&res));
			if (!apb_ponrst_base) {
				dev_err(phy->dev,
				"%s: register mapping failed\n", __func__);
				return -ENXIO;
			}

			/* toggle ponst bit */
			set(0x1 << 8, apb_ponrst_base);
			clear(0x1 << 8, apb_ponrst_base);
			udelay(0x20);
			if (chip_id == 0x563) {
				/* trim */
				if (phy_group == 0)
					trim_data = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_USB01);
				else if (phy_group == 1)
					trim_data = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_USB23);
				else {
					trim_data = 0xb;
					dev_err(phy->dev,
						"phy group %d, out of range 0~1\n", phy_group);
				}

				if (trim_data < 0) {
					dev_err(phy->dev,
						"phy group %d APB 0x%x, no trim value\n", phy_group, apb_res.start);
					trim_data = 0xb;
				} else if (trim_data > 31) {
					dev_err(phy->dev,
						"phy group %d APB 0x%x, trim value out of range %d\n", phy_group, apb_res.start, trim_data);
					trim_data = 0xb;
				} else
					dev_info(phy->dev,
						"phy group %d APB 0x%x, trim value %d\n", phy_group, apb_res.start, trim_data);

				writel(0x30, pphy->phy_regs + 0x140);
				writel(0x60 | trim_data, pphy->phy_regs + 0x148);
				writel(0x20, pphy->phy_regs + 0x144);
				udelay(1);
				writel(0, pphy->phy_regs + 0x144);
			}

			devm_iounmap(phy->dev, apb_ponrst_base);
		}
		if ((phy_reset_flag & BIT(phy_group)) && phy_group < 32)
			ret = 0;
	}
	if (ret != 0)
		pr_err(
		"[USB2PHY] phy doesn't do proper ponrst\n");

	if (!pphy->usb3_controller) {
		/* # select SUSPN and RST mask as 0 */
		clear(0x3 << 14, pphy->apb_regs);
		udelay(0x20);

		/* for phy power */
		writel(0, pphy->phy_regs + 0x38);
	}

	/* low speed high temperature workaround */
	if (chip_id == 0x172 && chip_ver == 0x0)
		pphy->low_speed_high_temp_wk = 1;
	else
		pphy->low_speed_high_temp_wk = 0;

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
			writel((readl(pphy->phy_regs + offset) & ~mask)
				| value, pphy->phy_regs + offset);
		}
	}

	/* for other setting */
	dt_reg_setting(phy->dev->of_node, "extra-setting", 0);
	return 0;
}


static void nt72668_u2_phy_shutdown(struct usb_phy *phy)
{
	int len;
	struct nvt_u2_phy *pphy;

	pphy = container_of(phy, struct nvt_u2_phy, u_phy);

	/* clear of APB read period */
	clear(0x7 << 5, pphy->apb_regs);
	udelay(0x20);

	/* set APB read period */
	set(0x1 << 5, pphy->apb_regs);
	udelay(0x20);

	/* # set PONRST to 1 */
	if (of_get_property(phy->dev->of_node, "nvt,ponrst", &len)) {
		clear(0x1 << 8, pphy->apb_regs);
		udelay(0x20);
	}
	clear(0x1 << 8, pphy->apb_regs);
	udelay(0x20);

	/* # select SUSPN and RST mask as 0 */
	clear(0x3 << 14, pphy->apb_regs);
	udelay(0x20);
	phy_reset_flag = 0;
}


static int nt72668_u2_phy_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct nvt_u2_phy *u2phy;
	void __iomem *phy_base;
	void __iomem *apb_base;

	struct resource res;
	u32 of_apb, of_phy;
	int ret, len;

	u2phy = devm_kzalloc(&pdev->dev, sizeof(struct nvt_u2_phy), GFP_KERNEL);
	if (!u2phy)
		return -ENOMEM;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "missing mem resource\n");
		ret = -ENODEV;
		goto err1;
	}

	if (of_get_property(dn, "nvt,usb3_controller", &len))
		u2phy->usb3_controller = 1;
	else
		u2phy->usb3_controller = 0;

	if (!u2phy->usb3_controller &&
		!request_mem_region(
			res.start, resource_size(&res),
			pdev->name)) {
		dev_err(&pdev->dev, "memory base of usb2 phy is in use\n");
		ret = -EBUSY;
		goto err1;
	}

	of_apb = res.start;

	apb_base = devm_ioremap_nocache(
		&pdev->dev, res.start, resource_size(&res));
	if (!apb_base) {
		dev_err(&pdev->dev,
			"%s: fail to register APB mapping\n", __func__);
		ret = -ENXIO;
		goto err2;
	}

	ret = of_address_to_resource(dn, 2, &res);
	if (ret) {
		dev_err(&pdev->dev, "missing  phy mem resource\n");
		ret = -ENODEV;
		goto err3;
	}
	phy_base = devm_ioremap_nocache(
		&pdev->dev, res.start, resource_size(&res));
	if (!phy_base) {
		dev_err(&pdev->dev,
			"%s: fail to register PHY mapping\n", __func__);
		ret = -ENXIO;
		goto err3;
	}

	of_phy = res.start;
	pr_info("[USB2PHY] pdev : %s apb 0x%x phy 0x%x\n",
		pdev->name, of_apb, of_phy);

	u2phy->phy_regs			= phy_base;
	u2phy->apb_regs			= apb_base;
	u2phy->dev				= &pdev->dev;
	u2phy->u_phy.dev		= u2phy->dev;
	u2phy->u_phy.label		= "nt72668-u2-phy";
	u2phy->u_phy.init		= nt72668_u2_phy_init;
	u2phy->u_phy.set_vbus	= nt72668_u2_set_vbus;
	u2phy->get_apb_reg		= u2_get_apb_reg;
	u2phy->set_apb_reg		= u2_set_apb_reg;
	u2phy->get_phy_reg		= u2_get_phy_reg;
	u2phy->set_phy_reg		= u2_set_phy_reg;
	if (!u2phy->usb3_controller)
		u2phy->u_phy.shutdown	= nt72668_u2_phy_shutdown;

	platform_set_drvdata(pdev, u2phy);

	ret = usb_add_phy_dev(&u2phy->u_phy);
	if (ret) {
		dev_err(&pdev->dev, "fail to register USB PHY\n");
		goto err4;
	}
	return 0;
err4:
	devm_iounmap(&pdev->dev, phy_base);
err3:
	devm_iounmap(&pdev->dev, apb_base);
err2:
	release_mem_region(res.start, resource_size(&res));
err1:
	devm_kfree(&pdev->dev, u2phy);
	return ret;

}


static int __exit nt72668_u2_phy_remove(struct platform_device *pdev)
{
	struct resource res;
	struct nvt_u2_phy *u2phy = platform_get_drvdata(pdev);
	struct device_node *dn = pdev->dev.of_node;

	if (of_address_to_resource(dn, 0, &res)) {
		dev_err(&pdev->dev, "%s : missing mem resource\n", __func__);
		return -ENODEV;
	}

	usb_remove_phy(&u2phy->u_phy);
	iounmap(u2phy->apb_regs);
	iounmap(u2phy->phy_regs);
	if (!u2phy->usb3_controller)
		release_mem_region(res.start, resource_size(&res));
	devm_kfree(&pdev->dev, u2phy);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id nvt_usb2phy_match[] = {
	{ .compatible = "nvt,NT72668-usb2phy" },
	{},
};
#endif

static struct platform_driver nvt_usb2phy_driver = {
	.probe = nt72668_u2_phy_probe,
	.remove = nt72668_u2_phy_remove,
	.driver = {
		.name = "nvt-usb2phy",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(nvt_usb2phy_match),
#endif
	}
};

static int __init phy_nvt_u2_init(void)
{
	phy_reset_flag = 0;
	return platform_driver_register(&nvt_usb2phy_driver);
}


static void __exit phy_nvt_u2_cleanup(void)
{
	platform_driver_unregister(&nvt_usb2phy_driver);
}


module_init(phy_nvt_u2_init);
module_exit(phy_nvt_u2_cleanup);
MODULE_LICENSE("GPL");
