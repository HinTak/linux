/*
 * arch/arm/plat-sdp/sdp_phy.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * modified by dongseok.lee <drain.lee@samsung.com>
 * 
 * 20120420 drain.lee Create file.
 * 20130815 chagne EEE print level
 * 20130912 add PHY suspend/resume code(for restore BMCR)
 * 20150107 support RTL8304E switch(for LFD)
 * 20150112 add RTL8304E switch config_init callback
 */

#define SDP_MAC_PHY_VER		"20150112(add RTL8304E switch config_init callback)"
 
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy_fixed.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

MODULE_AUTHOR("modified by Dongseok Lee");


struct sdp_mac_phy_priv {
	u32 bmcr;/* saved phy bmcr value */
};

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl821x_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}


static void rtl8201f_EEE_Disable(struct phy_device *phydev)
{
	int phyVal;

	dev_printk(KERN_DEBUG, &phydev->dev, "RTL8201F EEE Disable!\n");

	/* WOL Disable */
	phy_write(phydev, 31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal & ~(0x1<<10);
	phy_write(phydev,  19, (u16) phyVal);
	phy_write(phydev,  31, 0);

	/* LED Disable */
	phy_write(phydev,  31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal | (0x1<<3);
	phy_write(phydev,  19, (u16) phyVal);
	phy_write(phydev,  17, 0);
	phy_write(phydev,  31, 0);

	/* EEE Disable */
	phy_write(phydev,  31, 0x4);
	phy_write(phydev,  16, 0x4077);
	phy_write(phydev,  31, 0x0);
	phy_write(phydev,  13, 0x7);
	phy_write(phydev,  14, 0x3c);
	phy_write(phydev,  13, 0x4007);
	phy_write(phydev,  14, 0x0);
	phy_write(phydev,  0, 0x1200);
}

static int rtl8021f_config_init(struct phy_device *phydev)
{
	rtl8201f_EEE_Disable(phydev);
	return 0;
}


#define RTL8304E_PORT0	0x0
#define RTL8304E_PORT1	0x1
#define RTL8304E_MAC2	0x5
#define RTL8304E_MAC3	0x6
#define RTL8304E_MAC2_PHY	0x7

static int rtl8304e_config_init(struct phy_device *phydev)
{
	struct mii_bus *bus = phydev->bus;
	struct phy_device *mac2_phy = bus->phy_map[RTL8304E_MAC2_PHY];

	if(mac2_phy) {
		if(mac2_phy->drv && mac2_phy->drv->config_init)
			mac2_phy->drv->config_init(mac2_phy);
	}

	return 0;
}

static int rtl8304e_read_status(struct phy_device *phydev)
{
	struct mii_bus *bus = phydev->bus;
	struct phy_device *port0 = bus->phy_map[RTL8304E_PORT0];
	struct phy_device *port1 = bus->phy_map[RTL8304E_PORT1];
	struct phy_device *mac2 = bus->phy_map[RTL8304E_MAC2];
	struct phy_device *mac3 = bus->phy_map[RTL8304E_MAC3];
	struct phy_device *mac2_phy = bus->phy_map[RTL8304E_MAC2_PHY];
	int val = 0;
	int old_link = 0;
	int change_link = 0;

	old_link = port0->link;
	genphy_read_status(port0);
	if(old_link != port0->link) change_link = 1;

	old_link = port1->link;
	genphy_read_status(port1);
	if(old_link != port1->link) change_link = 1;

	genphy_read_status(mac2);

	if(mac2_phy) {
		old_link = mac2_phy->link;
		genphy_read_status(mac2_phy);
		if(old_link != mac2_phy->link) change_link = 1;

		if(mac2_phy->link) {
			mac2->autoneg = AUTONEG_DISABLE;
			mac2->speed = mac2_phy->speed;
			mac2->duplex = mac2_phy->duplex;
			genphy_config_aneg(mac2);
		}
	}

	val = phy_read(mac3, 22);
	/* set mac3 link status up or down */
	if(port0->link || port1->link || (mac2_phy && mac2_phy->link)) {
		phy_write(mac3, 22, val|(0x1<<15));
	} else {
		phy_write(mac3, 22, val&~(0x1<<15));
	}
	genphy_read_status(mac3);

	if(change_link) {
		char buf[512];
		int pos = 0;

		if(port0->link)
			pos += snprintf(buf+pos, 512-pos, "Port0 Link is Up - %dMbps/%s, ",
				port0->speed, DUPLEX_FULL == port0->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, 512-pos, "Port0 Link is Down, ");


		if(port1->link)
			pos += snprintf(buf+pos, 512-pos, "Port1 Link is Up - %dMbps/%s",
				port1->speed, DUPLEX_FULL == port1->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, 512-pos, "Port1 Link is Down");

		if(mac2_phy) {
			if(mac2_phy->link)
				pos += snprintf(buf+pos, 512-pos, ", MAC2 Phy Link is Up - %dMbps/%s",
					mac2_phy->speed, DUPLEX_FULL == mac2_phy->duplex ? "Full" : "Half");
			else
				pos += snprintf(buf+pos, 512-pos, ", MAC2 Phy Link is Down");
		}

		dev_info(&phydev->dev, "%s\n", buf);
	}

	return 0;
}


#ifdef CONFIG_PM_DEBUG
#define PM_DEV_DBG(arg...)	dev_printk(KERN_DEBUG, arg)
#else
#define PM_DEV_DBG(arg...)	dev_dbg(arg)
#endif

static int realtek_suspend(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int value;

	PM_DEV_DBG(&phydev->dev, "realtek_suspend.\n");

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, MII_BMCR);

	priv->bmcr = (u32) value;
	PM_DEV_DBG(&phydev->dev, "realtek_suspend: save bmcr 0x%04x\n", priv->bmcr);

	mutex_unlock(&phydev->lock);

	return 0;
}

static int realtek_resume(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;

	PM_DEV_DBG(&phydev->dev, "realtek_resume.\n");

	mutex_lock(&phydev->lock);

	phy_scan_fixups(phydev);

	PM_DEV_DBG(&phydev->dev, "realtek_resume: restore bmcr 0x%04x\n", priv->bmcr);
	phy_write(phydev, MII_BMCR, (u16) priv->bmcr);
	priv->bmcr = 0x0;

	mutex_unlock(&phydev->lock);

	return 0;
}

static int realtek_probe(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv;

	if((phydev->drv->phy_id_mask & 0xF) == 0) {
		dev_info(&phydev->dev, "%s%c probed.\n", phydev->drv->name, (phydev->phy_id&0xF)+'A'-1);
	} else {
		dev_info(&phydev->dev, "%s probed.\n", phydev->drv->name);
	}

	priv = kzalloc(sizeof(struct sdp_mac_phy_priv), GFP_KERNEL);
	if(!priv) {
		return -ENOMEM;
	}

	phydev->priv = priv;
	return 0;
}

static void realtek_remove(struct phy_device *phydev)
{
	if((phydev->drv->phy_id_mask & 0xF) == 0) {
		dev_info(&phydev->dev, "%s%c removed.\n", phydev->drv->name, (phydev->phy_id&0xF)+'A'-1);
	} else {
		dev_info(&phydev->dev, "%s removed.\n", phydev->drv->name);
	}

	kfree(phydev->priv);
	phydev->priv = NULL;
}


/* supported phy list */
static struct phy_driver sdp_phy_drivers[] = {
	{/* RTL8201E */
		.phy_id		= 0x001CC815,
		.name		= "RTL8201E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8201F */
		.phy_id		= 0x001CC816,
		.name		= "RTL8201F",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= rtl8021f_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8201x this is RTL8201x common phy driver. */
		.phy_id		= 0x001CC810,
		.name		= "RTL8201",
		.phy_id_mask	= 0x001ffff0,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},


	/* for RGMII Phy */
	{/* RTL8211E */
		.phy_id		= 0x001cc915,
		.name		= "RTL8211E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8211x this is RTL8211x common phy driver. */
		.phy_id		= 0x001cc910,
		.name		= "RTL8211",
		.phy_id_mask	= 0x001ffff0,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},

	{/* RTL8304E this is RTL8304E swtich driver. phy addr 0x0~0x6 is used */
		.phy_id		= 0x001CC852,
		.name		= "RTL8304E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= rtl8304e_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= rtl8304e_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},
};

static int __init sdp_phy_init(void)
{
	int i;
	int ret = 0;

	for(i = 0; i < (int) ARRAY_SIZE(sdp_phy_drivers); i++)
	{
		ret = phy_driver_register(&sdp_phy_drivers[i]);
		if(ret < 0) {
			pr_err("phy driver register failed(index:%d name:%s).\n", i, sdp_phy_drivers[i].name);
			for(i = i-1; i >= 0; i--) {
				phy_driver_unregister(&sdp_phy_drivers[i]);
			}
			return ret;
		}
	}

	pr_info("Registered sdp-phy drivers.ver %s\n", SDP_MAC_PHY_VER);

	return ret;
}

static void __exit sdp_phy_exit(void)
{
	int i;
	for(i = 0; i < (int) ARRAY_SIZE(sdp_phy_drivers); i++) {
		phy_driver_unregister(&sdp_phy_drivers[i]);
	}
}

module_init(sdp_phy_init);
module_exit(sdp_phy_exit);

static struct mdio_device_id __maybe_unused sdp_phy_tbl[] = {
	{ 0x001CC815, 0x001fffff },
	{ 0x001CC816, 0x001fffff },
	{ 0x001CC810, 0x001ffff0 },
	{ 0x001cc915, 0x001fffff },
	{ 0x001cc910, 0x001ffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, sdp_phy_tbl);

#ifdef CONFIG_FIXED_PHY
/* for Fixed Phy Added */
static int __init sdp_fixed_phy_init(void)
{
	int ret = 0;
	struct device_node *fixed_link = NULL;
	struct fixed_phy_status status = {0};
	int phy_id;

	pr_info("sdp-fixed-phy: Added Fixed Phy devices\n");

	for_each_node_by_name(fixed_link, "fixed-link") {
		status.link = 1;
		of_property_read_u32(fixed_link, "phy-id", &phy_id);
		of_property_read_u32(fixed_link, "speed", &status.speed);
		of_property_read_u32(fixed_link, "duplex", &status.duplex);
		status.pause = of_property_read_bool(fixed_link, "pause");
		status.asym_pause = of_property_read_bool(fixed_link, "asym-pause");

		ret = fixed_phy_add(PHY_POLL, phy_id, &status);
		if (ret < 0) {
			pr_err( "sdp-fixed-phy: fail fixed_phy_add!(phyid:%d, ret:%d)\n", phy_id, ret);
		} else {
			pr_info("sdp-fixed-phy: \tphyid:%d, speed:%d, duplex:%s\n", phy_id, status.speed, status.duplex?"Full":"Half");
		}
	}

	return ret;
}

static void __exit sdp_fixed_phy_exit(void)
{

}

module_init(sdp_fixed_phy_init);
module_exit(sdp_fixed_phy_exit);
#endif
