/*
 * AHCI SATA SDP driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 * Copyright 2013  Samsung electronics
 *   Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_sdp.h>
#include <linux/of.h>
#include "ahci.h"

#define SDP_REFCLK_125MHZ	1

static void ahci_host_stop(struct ata_host *host);

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

enum ahci_type {
	AHCI,		/* standard platform ahci */
};

static struct platform_device_id ahci_devtype[] = {
	{
		.name = "ahci",
		.driver_data = AHCI,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, ahci_devtype);

static struct ata_port_operations ahci_sdp_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_host_stop,
};

static const struct ata_port_info ahci_port_info[] = {
	/* by features */
	[AHCI] = {
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_sdp_ops,
	},
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("ahci_sdp"),
};

static int ahci_prep_init(struct device *dev, void __iomem *addr)
{
	u32 val;
	
	/* OOBR */
	val = readl(addr + 0xbc);
	val = val | 0x80000000;
	writel(val, addr + 0xbc);

	val = readl(addr + 0xbc);
	val = val | 0x02060b14;
	writel(val, addr + 0xbc);

	val = readl(addr + 0xbc);
	val = val & (~0x80000000);
	writel(val, addr + 0xbc);

	/* CAP */
	val = readl(addr + 0x00);
	writel(val, addr + 0x00);

	/* PI */
	val = readl(addr + 0x0C);
	val = val | 0x1;
	writel(val, addr + 0x0C);

	/* P0CMD */
	val = readl(addr + 0x118);
	val = val & 0xffc3ffff;
	writel(val, addr + 0x118);

	val = readl(addr + 0x118);
	val = val | 0x10;
	writel(val, addr + 0x118);
	
	val = readl(addr + 0x118);
	val = val | 0x4;
	writel(val, addr + 0x118);

	val = readl(addr + 0x118);
	val = val | 0x2;
	writel(val, addr + 0x118);

	val = readl(addr + 0x118);
	val = val | 0x1;
	writel(val, addr + 0x118);

	return 0;
}

#ifdef CONFIG_OF
static int init_platform_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ahci_sdp_data *pdata;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "platform data alloc fail.\n");
		return -1;
	}

	pdata->init = ahci_prep_init;

	of_property_read_u32(np, "phy_base", &pdata->phy_base);
	of_property_read_u32(np, "gpr_base", &pdata->gpr_base);
	of_property_read_u32(np, "phy_bit", &pdata->phy_bit);
	of_property_read_u32(np, "link_bit", &pdata->link_bit);
	
	dev_info(dev, "phy_base = %08x\n", pdata->phy_base);
	dev_info(dev, "gpr_base = %08x\n", pdata->gpr_base);
	dev_info(dev, "phy_bit = %u, link_bit = %u\n", pdata->phy_bit, pdata->link_bit);

	platform_device_add_data(pdev, pdata, sizeof(*pdata));

	return 0;
}
#endif

static int phy_init(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	void __iomem * gpr_base;
	u32 val;

	if (!pdata) {
		dev_err(dev, "pdata is NULL\n");
		return -1;
	}

	if (!pdata->phy_base) {
		dev_err(dev, "can't get phy base address\n");
		return -1;
	}

	dev_info(dev, "phy init\n");

	gpr_base = ioremap(pdata->gpr_base, 0x10);

	sdp_set_clockgating(pdata->phy_base, (1 << pdata->phy_bit) | (1 << pdata->link_bit), 0);

	udelay(1);

#if SDP_REFCLK_125MHZ
	/* select 125MHz pll mux, [14] : use 125MHz path */
	val = readl((void*)(VA_SFR0_BASE + 0x000908a4)) | (1 << 14);
	writel(val, (void*)(VA_SFR0_BASE + 0x000908a4));
#endif
	
	/* SATA_clk_set to 1 for 25MHz & 125MHz */
	val = readl(gpr_base) | (1 << 31);
	writel(val, gpr_base);

	/* Step 3. Set MPLL_CK_OFF to 1'b1, MPLL prescale for 25MHz 2'b01 */
#if SDP_REFCLK_125MHZ
	/* MPLL_NCY5 [5:4] 00 */
	val = readl(gpr_base) & (~(0x3 << 4));
	writel(val, gpr_base);
#else /* 25MHz */
	val = readl(gpr_base + 0x4) & (~(0x3 << 8));
	val |= 1 << 8;
	writel(val, gpr_base + 0x4);	
#endif

	/* Step 4. Set MPLL_PRESCAACLE, MPLL_NCY, MPLL_NCY5 to appropriate value */
	udelay(1);

	/* Step 5. Set MPLL_CK_OFF to 1'b0 */
#if SDP_REFCLK_125MHZ
	// MPLL_NCY 00101
	val = readl(gpr_base + 0x4) & (~(1 << 16));
	val = (val & (~0x1F)) | 0x5;
#else /* 25MHz */
	val = readl(gpr_base + 0x4) & (~(1 << 16));
#endif
	writel(val, gpr_base + 0x4);

	/* Step 6. Perform a PHY reset by either toggling RESET_N. (more than one ACLK_I cycle) */
	sdp_set_clockgating(pdata->phy_base, 1 << pdata->phy_bit, 1 << pdata->phy_bit);

	/* Step 7. Wait 100us */
	udelay(200);

	/* Step 8. Deassert ARESETX_I */
	sdp_set_clockgating(pdata->phy_base, 1 << pdata->link_bit, 1 << pdata->link_bit);

	/* Step 9. Wait 200ns */
	udelay(1);

	iounmap(gpr_base);

	return 0;
}

static int phy_exit(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	void __iomem * gpr_base;
	unsigned int val;
	
	if (!pdata->phy_base) {
		dev_err(dev, "can't get phy base address\n");
		return -1;
	}

	gpr_base = ioremap(pdata->gpr_base, 0x10);

	/* wait more than 200ns */
	udelay(1);
	
	/* Set MPLL_CK_OFF to 1'b1 */
	val = readl(gpr_base + 0x4) | (1 << 16);
	writel(val, gpr_base + 0x4);

	iounmap(gpr_base);
	
	return 0;
}

static int ahci_sdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_sdp_data *pdata;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct ata_port_info pi = ahci_port_info[id ? id->driver_data : 0];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	struct resource *mem;
	int irq;
	int n_ports;
	int i;
	int rc;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "no mmio space\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -EINVAL;
	}

#ifdef CONFIG_OF
	rc = init_platform_data(pdev);
	if (rc) {
		dev_err(dev, "can't get platform data\n");
		return -EINVAL;
	}
#endif

	pdata = dev_get_platdata(dev);

	/* init phy */
	rc = phy_init(dev);
	if (rc < 0)
		return -EINVAL;

	if (pdata && pdata->ata_port_info)
		pi = *pdata->ata_port_info;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		return -ENOMEM;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	hpriv->mmio = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!hpriv->mmio) {
		dev_err(dev, "can't map %pR\n", mem);
		return -ENOMEM;
	}

	//hpriv->clk = clk_get(dev, NULL);
	hpriv->clk = clk_get(NULL, "sata_clk");
	if (IS_ERR(hpriv->clk)) {
		dev_err(dev, "can't get clock\n");
	} else {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			goto free_clk;
		}
	}

	/*
	 * Some platforms might need to prepare for mmio region access,
	 * which could be done in the following init call. So, the mmio
	 * region shouldn't be accessed before init (if provided) has
	 * returned successfully.
	 */
	if (pdata && pdata->init) {
		rc = pdata->init(dev, hpriv->mmio);
		if (rc)
			goto disable_unprepare_clk;
	}

	ahci_save_initial_config(dev, hpriv,
		pdata ? pdata->force_port_map : 0,
		pdata ? pdata->mask_port_map  : 0);

	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	ahci_set_em_messages(hpriv, &pi);

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));

	host = ata_host_alloc_pinfo(dev, ppi, n_ports);
	if (!host) {
		rc = -ENOMEM;
		goto pdata_exit;
	}

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		printk(KERN_INFO "ahci: SSS flag set, parallel bus scan disabled\n");

	if (pi.flags & ATA_FLAG_EM)
		ahci_reset_em(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_port_desc(ap, "mmio %pR", mem);
		ata_port_desc(ap, "port 0x%x", 0x100 + ap->port_no * 0x80);

		/* set enclosure management message type */
		if (ap->flags & ATA_FLAG_EM)
			ap->em_message_type = hpriv->em_msg_type;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}

	rc = ahci_reset_controller(host);
	if (rc)
		goto pdata_exit;

	ahci_init_controller(host);
	ahci_print_info(host, "sdp");

	rc = ata_host_activate(host, irq, ahci_interrupt, IRQF_SHARED,
			       &ahci_platform_sht);
	if (rc)
		goto pdata_exit;

	return 0;
pdata_exit:
	if (pdata && pdata->exit)
		pdata->exit(dev);
disable_unprepare_clk:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);
free_clk:
	if (!IS_ERR(hpriv->clk))
		clk_put(hpriv->clk);
	return rc;
}

static int ahci_sdp_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv = host->private_data;
	unsigned int val;
	
	ata_host_detach(host);

	/* phy power down, write 1;b1 to P0PHYCR PD bit */
	dev_info(dev, "phy power down\n");
	val = readl(hpriv->mmio + 0x178);
	val = val | (1 << 23);
	writel(val, hpriv->mmio + 0x178);

	/* exit phy */
	phy_exit(dev);

	return 0;
}

static void ahci_host_stop(struct ata_host *host)
{
	struct device *dev = host->dev;
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;

	if (pdata && pdata->exit)
		pdata->exit(dev);

	if (!IS_ERR(hpriv->clk)) {
		clk_disable_unprepare(hpriv->clk);
		clk_put(hpriv->clk);
	}
}

#ifdef CONFIG_PM_SLEEP
static int ahci_sdp_suspend(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 ctl;
	int rc;

	if (hpriv->flags & AHCI_HFLAG_NO_SUSPEND) {
		dev_err(dev, "firmware update required for suspend/resume\n");
		return -EIO;
	}

	/*
	 * AHCI spec rev1.1 section 8.3.3:
	 * Software must disable interrupts prior to requesting a
	 * transition of the HBA to D3 state.
	 */
	ctl = readl(mmio + HOST_CTL);
	ctl &= ~HOST_IRQ_EN;
	writel(ctl, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */

	rc = ata_host_suspend(host, PMSG_SUSPEND);
	if (rc)
		return rc;

	if (pdata && pdata->suspend)
		return pdata->suspend(dev);

	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return 0;
}

static int ahci_sdp_resume(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	if (!IS_ERR(hpriv->clk)) {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			return rc;
		}
	}

	if (pdata && pdata->resume) {
		rc = pdata->resume(dev);
		if (rc)
			goto disable_unprepare_clk;
	}

	if (dev->power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			goto disable_unprepare_clk;

		ahci_init_controller(host);
	}

	ata_host_resume(host);

	return 0;

disable_unprepare_clk:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return rc;
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_pm_ops, ahci_sdp_suspend, ahci_sdp_resume);

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = "samsung,sdp-ahci", },
	{},
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver ahci_driver = {
	.probe = ahci_sdp_probe,
	.remove = ahci_sdp_remove,
	.driver = {
		.name = "ahci",
		.owner = THIS_MODULE,
		.of_match_table = ahci_of_match,
		.pm = &ahci_pm_ops,
	},
	.id_table	= ahci_devtype,
};
module_platform_driver(ahci_driver);

MODULE_DESCRIPTION("AHCI SATA SDP driver");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("sdp:ahci");
