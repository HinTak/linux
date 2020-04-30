/*
 * drivers/net/ethernet/sdp/sdp_mac_phy.c
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
 * 20140801 add EPHY driver
 * 20140901 RTL8211E disable EEE.
 * 20140926 fix suspend/resume.
 * 20141128 add phy debugfs.
 * 20150107 support RTL8304E switch.
 * 20150108 add ephy reinit when link is down.
 * 20150112 fix W=123 compile warning.
 * 20150113 add RTL8304E switch config_init callback
 * 20150313 EPHY auto mdix workaround
 * 20150709 Add EPHY register dump in debugfs
 * 20150722 Add EPHY SW MDIX control debugfs
 * 20150729 Add EPHY Jazz-M LDO Ctrl debugfs
 * 20150729 Support EPHY WOL
 * 20150729(Support EPHY WOL)
 * 20151102(Support EPHY AnyPacket WOL)
 * 20150107(fix EPHY DSP Register init value for Jazz)
 * 20150111(Change sw mdix switch code, add debug log)
 * 20160303(linux4.1 compile error fix)
 * 20160311(bugfix, Jazz-L rev0 WOL is not work)
 *
 * modified by Mr son jung kwun <jk97.son@samsung.com>
 *
 * 20170804(add sdp_mac_phy.h / add selftest_enable option for test mode )
 * 20180913(to read/write via i2c / to use NVT phy on OCL board )
 * 20181114(Set dsp & bist on config init)
 * 20181115(Add nvt supend & resume function)
 * 20181120(Add S/W reset for MuseM during init)
 */

#define SDP_MAC_PHY_VER		"20181123(Add write functions for dsp,wol,cusom,bist register / Add iot and IPv6 WoL mode for debug)"


#include <linux/phy.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy_fixed.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inet.h>

#include <soc/sdp/soc.h>
#include "sdp_rewritable_const.h"
#include "sdp_mac_phy.h"
#include <trace/early.h>


MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("modified by Dongseok Lee");
MODULE_LICENSE("GPL");

static int ephy_dynamic_printk(struct phy_device *phydev, const char *fmt, ...)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	struct va_format vaf;
	va_list args;

	if(priv && (priv->dynamic_printk_level >= 0 && priv->dynamic_printk_level <= 7)) {
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		dev_printk_emit(priv->dynamic_printk_level, &phydev->dev, "%s %s: %pV",
			dev_driver_string(&phydev->dev), dev_name(&phydev->dev), &vaf);

		va_end(args);
	}

	return 0;
}

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

static void rtl82xx_EEE_Disable(struct phy_device *phydev)
{
	int phyVal;

	dev_printk(KERN_INFO, &phydev->dev, "RTL82xx EEE Disable!\n");

	mutex_lock(&phydev->lock);

	/* WOL Disable */
	phy_write(phydev, 31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal & ~(0x1<<10);
	phy_write(phydev,  19, (u16)phyVal);
	phy_write(phydev,  31, 0);

	/* LED Disable */
	phy_write(phydev,  31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal | (0x1<<3);
	phy_write(phydev,  19, (u16)phyVal);
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

	/* restart auto neg */
	genphy_restart_aneg(phydev);

	mutex_unlock(&phydev->lock);
}

static int rtl8201f_config_init(struct phy_device *phydev)
{
	rtl82xx_EEE_Disable(phydev);
	return 0;
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	rtl82xx_EEE_Disable(phydev);
	return 0;
}



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
	if(val < 0) {
		dev_err(&phydev->dev, "%s: phy_read return error %d\n", __func__, val);
		return val;
	}

	/* set mac3 link status up or down */
	if(port0->link || port1->link || (mac2_phy && mac2_phy->link)) {
		phy_write(mac3, 22, (u16)(val|(0x1<<15)));
	} else {
		phy_write(mac3, 22, (u16)(val&~(0x1<<15)));
	}
	genphy_read_status(mac3);

	if(change_link) {
		char buf[512];
		int pos = 0;

		if(port0->link)
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port0 Link is Up - %dMbps/%s, ",
				port0->speed, DUPLEX_FULL == port0->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port0 Link is Down, ");


		if(port1->link)
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port1 Link is Up - %dMbps/%s",
				port1->speed, DUPLEX_FULL == port1->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port1 Link is Down");

		if(mac2_phy) {
			if(mac2_phy->link)
				pos += snprintf(buf+pos, (size_t)(512-pos), ", MAC2 Phy Link is Up - %dMbps/%s",
					mac2_phy->speed, DUPLEX_FULL == mac2_phy->duplex ? "Full" : "Half");
			else
				pos += snprintf(buf+pos, (size_t)(512-pos), ", MAC2 Phy Link is Down");
		}

		dev_info(&phydev->dev, "%s\n", buf);
	}

	return 0;
}

static inline bool ephy_is_support_anypacket(struct phy_device *phydev) {

	if((phydev->phy_id&0xF) < 3) {
#ifdef CONFIG_ARCH_SDP1501//JAZZ
		if(soc_is_jazzl() || sdp_get_revision_id() >= 1) {
			return true;
		}
#endif
		return false;
	}

	return true;
}

static void ephy_test_init(struct phy_device *phydev) {
	/* DSP Reg enter init */
	phy_write(phydev, 20, 0x0000);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);
	phy_write(phydev, 20, 0x0000);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);
}

static int ephy_dspreg_read(struct phy_device *phydev, int addr) {
	phy_write(phydev, 20, 0x0);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);
	phy_write(phydev, 20, 0x0);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);

	phy_write(phydev, 20, EPHY_TSTCNTL_READ|EPHY_TSTCNTL_TEST_MODE|(addr<<EPHY_TSTCNTL_READ_OFF));
	return phy_read(phydev, 21);
}

static void ephy_dspreg_write(struct phy_device *phydev, int addr, int val) {
	phy_write(phydev, 20, 0x0);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);
	phy_write(phydev, 20, 0x0);
	phy_write(phydev, 20, EPHY_TSTCNTL_TEST_MODE);

	phy_write(phydev, 23, val);
	phy_write(phydev, 20, EPHY_TSTCNTL_WRITE|EPHY_TSTCNTL_TEST_MODE|(addr<<EPHY_TSTCNTL_WRITE_OFF));
}

static inline int ephy_wolreg_read(struct phy_device *phydev, int addr) {
	phy_write(phydev, 20, EPHY_TSTCNTL_READ|EPHY_TSTCNTL_WOL_REG_SEL|(addr<<EPHY_TSTCNTL_READ_OFF));
	return phy_read(phydev, 21);
}

static inline void ephy_wolreg_write(struct phy_device *phydev, int addr, int val) {
	phy_write(phydev, 23, val);
	phy_write(phydev, 20, EPHY_TSTCNTL_WRITE|EPHY_TSTCNTL_WOL_REG_SEL|(addr<<EPHY_TSTCNTL_WRITE_OFF));
}

static inline int ephy_customreg_read(struct phy_device *phydev, int addr) {
	phy_write(phydev, 20, EPHY_TSTCNTL_READ|EPHY_TSTCNTL_WOL_REG_SEL|EPHY_TSTCNTL_WOLCUS_REG_SEL|(addr<<EPHY_TSTCNTL_READ_OFF));
	return phy_read(phydev, 21);
}

static inline void ephy_customreg_write(struct phy_device *phydev, int addr, int val) {
	phy_write(phydev, 23, val);
	phy_write(phydev, 20, EPHY_TSTCNTL_WRITE|EPHY_TSTCNTL_WOL_REG_SEL|EPHY_TSTCNTL_WOLCUS_REG_SEL|(addr<<EPHY_TSTCNTL_WRITE_OFF));
}

#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
static inline int ephy_bistreg_read(struct phy_device *phydev, int addr) {
	phy_write(phydev, 20, EPHY_TSTCNTL_READ|EPHY_TSTCNTL_BIST_REG_SEL|(addr<<EPHY_TSTCNTL_READ_OFF));
	return phy_read(phydev, 21);
}

static inline void ephy_bistreg_write(struct phy_device *phydev, int addr, int val) {
	phy_write(phydev, 23, val);
	phy_write(phydev, 20, EPHY_TSTCNTL_WRITE|EPHY_TSTCNTL_BIST_REG_SEL|(addr<<EPHY_TSTCNTL_WRITE_OFF));
}
#endif



/* EPHY WOL Control func */
static void ephy_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol) {
	int wolval = 0;
	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	wolval = ephy_wolreg_read(phydev, 0x3);
	if(wolval < 0) {
		dev_err(&phydev->dev, "%s: ephy_wolreg_read return error(%d)!\n", __FUNCTION__, wolval);
		return;
	}

	if(wolval & 0x1) {
		wol->wolopts |= WAKE_MAGIC;
	}
}

static inline u16 ephy_reverse_nibble(u16 val) {
	int n = 0, b = 0;
	u16 res = 0;

	for(n = 0; n < 4; n++) {
		for(b = 0; b < 4; b++) {
			if(val & (0x1<<((n*4)+(3-b)))) {
				res |= 0x1<<((n*4)+b);
			}
		}
	}
	res =  ((res&0xF)<<4) | ((res&0xF0)>>4) | ((res&0xF00)<<4) | ((res&0xF000)>>4);
	return res;
}

static int ephy_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol) {
	unsigned char *dev_addr = NULL;

	if(!phydev || !phydev->attached_dev) {
		dev_err(&phydev->dev, "%s: no attached dev!\n", __FUNCTION__);
		return -ENODEV;
	}

	dev_addr = phydev->attached_dev->dev_addr;

	if(!is_valid_ether_addr(dev_addr)) {
		dev_err(&phydev->dev, "%s: invalid dev_addr %pM\n", __FUNCTION__, dev_addr);
		return -EINVAL;
	}

	ephy_test_init(phydev);

	if (wol->wolopts & WAKE_MAGIC) {
		/* Store the device address for the magic packet */
		if(ephy_is_support_anypacket(phydev)) {
			ephy_wolreg_write(phydev, 0x0, dev_addr[4]<<8 | dev_addr[5]);
			ephy_wolreg_write(phydev, 0x1, dev_addr[2]<<8 | dev_addr[3]);
			ephy_wolreg_write(phydev, 0x2, dev_addr[0]<<8 | dev_addr[1]);
		} else {
			ephy_wolreg_write(phydev, 0x0, ephy_reverse_nibble(dev_addr[4]<<8 | dev_addr[5]));
			ephy_wolreg_write(phydev, 0x1, ephy_reverse_nibble(dev_addr[2]<<8 | dev_addr[3]));
			ephy_wolreg_write(phydev, 0x2, ephy_reverse_nibble(dev_addr[0]<<8 | dev_addr[1]));
		}

		/* Clear WOL status and enable magic packet matching */
		if(get_sdp_board_type() == SDP_BOARD_WALL){
			ephy_wolreg_write(phydev, 0x3, EPHY_WOL_EN | EPHY_WOL_MAGIC_DA_EN | EPHY_WOL_MAGIC_BA_EN 
			| EPHY_WOL_CUSTOM_EN | EPHY_WOL_CUS_SA_MATCH_EN ); 
		}
		else{
			ephy_wolreg_write(phydev, 0x3, EPHY_WOL_EN | EPHY_WOL_MAGIC_DA_EN | EPHY_WOL_MAGIC_BA_EN); 
		}		

		/* WOL INT UnMask */
		phy_write(phydev, 30, phy_read(phydev, 30) | 0x3F00);

		dev_info(&phydev->dev, "%s: WOL On(%pM) intr mask: 0x%x, WOL03: 0x%x\n", __FUNCTION__, dev_addr, phy_read(phydev, 30), ephy_wolreg_read(phydev, 0x3));
	} else {
		/* WOL INT Mask */
		phy_write(phydev, 30, phy_read(phydev, 30) & ~(0x7<<9));

		/* Store the device address fill zero */
		ephy_wolreg_write(phydev, 0x0, 0x0000);
		ephy_wolreg_write(phydev, 0x1, 0x0000);
		ephy_wolreg_write(phydev, 0x2, 0x0000);

		/* Clear WOL status and disable magic packet matching */
		ephy_wolreg_write(phydev, 0x3, 0x0);

		dev_info(&phydev->dev, "%s: WOL Off\n", __FUNCTION__);
	}

	return 0;
}


static int ephy_dsp_register_init(struct phy_device *phydev)
{
	u32 chip_number = 0;
#if defined(CONFIG_ARCH_SDP1803)
	if(soc_is_sdp1803()){
		ephy_dspreg_write(phydev, 0x16, 0x8404);
		if(get_sdp_board_type() == SDP_BOARD_WALL){
			ephy_dspreg_write(phydev, 0x18, 0x000F);
		}
	}
	else if(soc_is_sdp1804()){
		ephy_dspreg_write(phydev, 0x16, 0x8402);
		ephy_dspreg_write(phydev, 0x18, 0x000C);
	}
	ephy_dspreg_write(phydev, 0x17, 0x1A0C);
	ephy_dspreg_write(phydev, 0x1A, 0x6400);
	ephy_dspreg_write(phydev, 0x1D, 0x0000);

#elif defined(CONFIG_ARCH_SDP1601)
	//ephy_dspreg_write(phydev, 0x15, 0x0006);
	if(soc_is_sdp1601()){
		ephy_dspreg_write(phydev, 0x16, 0x8406);
	}
	else{
		ephy_dspreg_write(phydev, 0x16, 0x8404);
	}
	
	chip_number = (sdp_rev()>>13) & 0x7;		
	if(SDP1701_CHIP_Me == chip_number){
		dev_info(&phydev->dev, "chip 2727 number : %d\n", chip_number);
		ephy_dspreg_write(phydev, 0x18, 0x000A);
	}
	else
	{
		dev_info(&phydev->dev, "chip 3131 number : %d\n", chip_number);
		ephy_dspreg_write(phydev, 0x18, 0x000F);
	}
	
	ephy_dspreg_write(phydev, 0x17, 0x1204);
	ephy_dspreg_write(phydev, 0x1A, 0x6400);
	ephy_dspreg_write(phydev, 0x1D, 0x0000);
#else
	/* DSP Reg enter init */
	ephy_dspreg_write(phydev, 21, 0x4400);
	ephy_dspreg_write(phydev, 19, 0x3400);
	ephy_dspreg_write(phydev, 24, 0x0007);
#endif	

#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	/* RX DSP A3 rx fillter setting for jazz */
	ephy_dspreg_write(phydev, 20, 0xF900);
#endif

	return 0;
}

static int ephy_config_init(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int mdi_mode = phy_read(phydev, 17);

	dev_err(&phydev->dev, "%s: start setting\n", __FUNCTION__);

	mutex_lock(&phydev->lock);

	ephy_dsp_register_init(phydev);

	priv->ephy_sw_mdix_switch_cnt = 0;

	/* for try auto mdix mode */
	if(!(mdi_mode&EPHY_MDIO17_AUTO_MDIX_EN)) {
		phy_write(phydev, 17, mdi_mode|EPHY_MDIO17_AUTO_MDIX_EN);
		mdi_mode = phy_read(phydev, 17);
	}

	ephy_dynamic_printk(phydev, "init now %s %s mode",
		phy_read(phydev, 17)&EPHY_MDIO17_AUTO_MDIX_EN?"Auto":"Manual",
		mdi_mode&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI");

#if defined(CONFIG_ARCH_SDP1803)
	ephy_bistreg_write(phydev, 0x1B, 0x0005);
#endif

	mutex_unlock(&phydev->lock);

	return 0;
}

static u32 ephy_get_random_ms(struct phy_device *phydev)
{
		u32 random_ms;
		const u32 min_ms = 2000, max_ms = 2500, setp_ms = 100;

		/* set random next mdix time */
		get_random_bytes(&random_ms, sizeof(random_ms));
		random_ms %= max_ms - min_ms;
		random_ms += min_ms;
		return (random_ms / setp_ms) * setp_ms;
}

static int ephy_auto_mdix_fixup(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	unsigned long now = jiffies;
	u16 phyVal;

	if(time_after_eq(now, priv->ephy_next_mdix_jiffies))
	{
		u32 random_ms;

		random_ms = ephy_get_random_ms(phydev);
		priv->ephy_next_mdix_jiffies = now + msecs_to_jiffies(random_ms);
		priv->ephy_sw_mdix_switch_cnt++;

		/* mdix switch! */
		phyVal = (u16)phy_read(phydev, 17);
		phyVal &= (u16)(~EPHY_MDIO17_AUTO_MDIX_EN);

		/* toggle mdi/mdix mode */
		phyVal ^= (u16)EPHY_MDIO17_MDI_MODE;

		phy_write(phydev, 17, phyVal);

		if(AUTONEG_ENABLE == phydev->autoneg) {
			genphy_restart_aneg(phydev);
		}

		ephy_dynamic_printk(phydev, "now switch to %s %s mode, "
			"next switching after %dms\n",
			phyVal&EPHY_MDIO17_AUTO_MDIX_EN?"Auto":"Manual",
			phyVal&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI", random_ms);

		return phyVal;
	}
	return 0;
}

static int nvt_ephy_read_status(struct phy_device *phydev)
{
    struct sdp_mac_phy_priv *priv = phydev->priv;
    int old_link = phydev->link;
    int ret = 0;

    ret = genphy_read_status(phydev);

    /* phy link is down ephy reinit! */
    if(old_link && !phydev->link) {
        phydev->link = 0;
        ephy_dynamic_printk(phydev, "Link is down!!!!!!!!!!!!!!");
        trace_early_message("ConnMan eth0 Link is down!!!!!!!!!!!!!!");
    }
    else if(!old_link && phydev->link) {
#if	defined(CONFIG_ARCH_SDP1803)
		if(!(readl(ephy_serdes_path) & EPHY_SERDES_PATH)){
			phydev->link = 0;
		}
		else
#endif
		{
	        phydev->link = 1;
	        ephy_dynamic_printk(phydev, "Link is up!!!!!!!!!!!!!!");
	        trace_early_message("ConnMan eth0 Link is up!!!!!!!!!!!!!!");
    	}
	}
    
    return 0;
}


static int ephy_read_status(struct phy_device *phydev)
{
	int old_link = phydev->link;
	int ret = genphy_read_status(phydev);
	struct sdp_mac_phy_priv *priv = phydev->priv;
	u16 mdi_mode = phy_read(phydev, 17);
#if	defined(CONFIG_ARCH_SDP1803)
	u16 dsp_reg_0 = ephy_dspreg_read(phydev, 0);
#endif

	/* phy link is down ephy reinit! */
	if(old_link && !phydev->link) {
		//dev_info(&phydev->dev, "ephy_read_status applay init!\n");

		ephy_dsp_register_init(phydev);

		priv->ephy_sw_mdix_switch_cnt = 0;

		/* for try auto mdix mode */
		if(!(mdi_mode&EPHY_MDIO17_AUTO_MDIX_EN)) {
			phy_write(phydev, 17, mdi_mode|EPHY_MDIO17_AUTO_MDIX_EN);
			mdi_mode = phy_read(phydev, 17);
		}
		

		if(priv->ephy_sw_mdix_en) {
			u32 random_ms = ephy_get_random_ms(phydev);

			priv->ephy_next_mdix_jiffies = jiffies + msecs_to_jiffies(random_ms);
			ephy_dynamic_printk(phydev, "Link is down! now %s %s mode, "
				"next switching after %dms",
				mdi_mode&EPHY_MDIO17_AUTO_MDIX_EN?"Auto":"Manual",
				mdi_mode&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI", random_ms);
		} else {
			ephy_dynamic_printk(phydev, "Link is down! now Auto %s mode",
				mdi_mode&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI ");
		}

	} else if(!old_link && !phydev->link) {
		/* link is already down */
		if(priv->ephy_sw_mdix_en) {
			ephy_auto_mdix_fixup(phydev);
		}
	} else if(!old_link && phydev->link) {
#if	defined(CONFIG_ARCH_SDP1803)
		if((phydev->speed == SPEED_100) && (!(phydev->duplex)) && (dsp_reg_0 & (0x1<<EPHY_DSP0_MLT3_ZERO_VIOLATION_BIT)))
		{
			phydev->link = 0;
		}
		else
#endif
		{
			ephy_dynamic_printk(phydev, "Link is up! now %s %s mode, switch count %d",
			mdi_mode&EPHY_MDIO17_AUTO_MDIX_EN?"Auto":"Manual",
			mdi_mode&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI", priv->ephy_sw_mdix_switch_cnt);
		}
	}

	return ret;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static int ephy_dbg_reg_dump_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int addr = 0;

	seq_printf(s, "PHY Register dump\n");

	for(addr = 0; addr <= 0x1F; addr++) {
		seq_printf(s, "%02d:0x%04x", addr, phy_read(phydev, addr));
		if(addr%4 == 3) {
			seq_printf(s, "\n");
		} else {
			seq_printf(s, "\t");
		}
	}

	return 0;
}

static int ephy_dbg_reg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_reg_dump_show, inode->i_private);
}

static ssize_t ephy_dbg_reg_dump_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;

	int ret;
	u32 addr, val;

	ret = sscanf(buf + *ppos, "%u:%x", &addr, &val);
	if(ret != 2) {
		return -EINVAL;
	}

	if(addr > 0x1F) {
		dev_err(&phydev->dev, "invalid address(%u)\n", addr);
		return -EINVAL;
	}
	if(val > 0xFFFF) {
		dev_err(&phydev->dev, "invalid value(0x%x)\n", val);
		return -EINVAL;
	}

	phy_write(phydev, addr, val);

	return count;
}

static const struct file_operations ephy_dbg_reg_dump_fops = {
	.open		= ephy_dbg_reg_dump_open,
	.read		= seq_read,
	.write		= ephy_dbg_reg_dump_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ephy_dbg_wolreg_dump_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int addr = 0;

	seq_printf(s, "EPHY WOL Register dump\n");

	for(addr = 0; addr <= 0x1F; addr++) {
		seq_printf(s, "WOL%02d:0x%04x", addr, ephy_wolreg_read(phydev, addr));
		if(addr%4 == 3) {
			seq_printf(s, "\n");
		} else {
			seq_printf(s, "\t");
		}
	}

	if(ephy_wolreg_read(phydev, 0x3) & 0x1) {
		u16 match_addr[3];

		if(ephy_is_support_anypacket(phydev)) {
			match_addr[0] = ephy_wolreg_read(phydev, 0x2);
			match_addr[1] = ephy_wolreg_read(phydev, 0x1);
			match_addr[2] = ephy_wolreg_read(phydev, 0x0);
		} else {
			match_addr[0] = ephy_reverse_nibble(ephy_wolreg_read(phydev, 0x2));
			match_addr[1] = ephy_reverse_nibble(ephy_wolreg_read(phydev, 0x1));
			match_addr[2] = ephy_reverse_nibble(ephy_wolreg_read(phydev, 0x0));
		}

		seq_printf(s, "WOL On. Match Addr %02x:%02x:%02x:%02x:%02x:%02x\n",
			match_addr[0]>>8, match_addr[0]&0xFF, match_addr[1]>>8, match_addr[1]&0xFF, match_addr[2]>>8, match_addr[2]&0xFF);
	}

	return 0;
}

static int ephy_dbg_wolreg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_wolreg_dump_show, inode->i_private);
}

static ssize_t ephy_dbg_wolreg_dump_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;

	int ret;
	u32 addr, val;

	ret = sscanf(buf + *ppos, "%u:%x", &addr, &val);
	if(ret != 2) {
		return -EINVAL;
	}

	if(addr > 0x1F) {
		dev_err(&phydev->dev, "invalid address(%u)\n", addr);
		return -EINVAL;
	}
	if(val > 0xFFFF) {
		dev_err(&phydev->dev, "invalid value(0x%x)\n", val);
		return -EINVAL;
	}

	ephy_wolreg_write(phydev, addr, val);

	return count;
}

static const struct file_operations ephy_dbg_wolreg_dump_fops = {
	.open		= ephy_dbg_wolreg_dump_open,
	.read		= seq_read,
	.write		= ephy_dbg_wolreg_dump_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
static int ephy_dbg_bistreg_dump_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int addr = 0;

	seq_printf(s, "EPHY BIST Register dump\n");

	for(addr = 0; addr <= 0x1F; addr++) {
		seq_printf(s, "BIST%02d:0x%04x", addr, ephy_bistreg_read(phydev, addr));
		if(addr%4 == 3) {
			seq_printf(s, "\n");
		} else {
			seq_printf(s, "\t");
		}
	}

	return 0;
}

static int ephy_dbg_bistreg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_bistreg_dump_show, inode->i_private);
}

static ssize_t ephy_dbg_bistreg_dump_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;

	int ret;
	u32 addr, val;

	ret = sscanf(buf + *ppos, "%u:%x", &addr, &val);
	if(ret != 2) {
		return -EINVAL;
	}

	if(addr > 0x1F) {
		dev_err(&phydev->dev, "invalid address(%u)\n", addr);
		return -EINVAL;
	}
	if(val > 0xFFFF) {
		dev_err(&phydev->dev, "invalid value(0x%x)\n", val);
		return -EINVAL;
	}

	ephy_bistreg_write(phydev, addr, val);

	return count;
}

static const struct file_operations ephy_dbg_bistreg_dump_fops = {
	.open		= ephy_dbg_bistreg_dump_open,
	.read		= seq_read,
	.write		= ephy_dbg_bistreg_dump_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int ephy_dbg_customreg_dump_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int addr = 0;
	int wol_control = 0;

	seq_printf(s, "EPHY WOL Custom Packet Register dump\n");

	for(addr = 0; addr <= 0x1F; addr++) {
		seq_printf(s, "WOLC%02d:0x%04x", addr, ephy_customreg_read(phydev, addr));
		if(addr%4 == 3) {
			seq_printf(s, "\n");
		} else {
			seq_printf(s, "\t");
		}
	}

	/* Wol_custom_pkt_en */
	wol_control = ephy_wolreg_read(phydev, 0x3);
	if((wol_control&EPHY_WOL_EN) && (wol_control&EPHY_WOL_CUSTOM_EN)
			&& (wol_control&(EPHY_WOL_CUS_SA_MATCH_EN|EPHY_WOL_CUS_DA_MATCH_EN))) {

		u16 match_addr[8];
		u16 custom_offset;
		u16 custom_len;
		int i = 0;

		/* dest MAC Address */
		match_addr[0] = htons(ephy_wolreg_read(phydev, 0x2));
		match_addr[1] = htons(ephy_wolreg_read(phydev, 0x1));
		match_addr[2] = htons(ephy_wolreg_read(phydev, 0x0));

		seq_printf(s, "\nWOL Custom On. %s %s\n",
			wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK?"Bypass Type check":"",
			wol_control&EPHY_WOL_CUS_BYPASS_CRC_CHK?"Bypass CRC check":"");

		seq_printf(s, "Ethernet Dest MAC Addr Match %pM\n", match_addr);

		/* wol_custom_pkt_sa_ip_addrs_match_en */
		if(wol_control & EPHY_WOL_CUS_SA_MATCH_EN) {
			match_addr[0] = htons(ephy_customreg_read(phydev, 0x7));
			match_addr[1] = htons(ephy_customreg_read(phydev, 0x6));
			match_addr[2] = htons(ephy_customreg_read(phydev, 0x5));
			match_addr[3] = htons(ephy_customreg_read(phydev, 0x4));
			match_addr[4] = htons(ephy_customreg_read(phydev, 0x3));
			match_addr[5] = htons(ephy_customreg_read(phydev, 0x2));
			match_addr[6] = htons(ephy_customreg_read(phydev, 0x1));
			match_addr[7] = htons(ephy_customreg_read(phydev, 0x0));

			custom_offset = ephy_customreg_read(phydev, 0x8)&0xFF;
			custom_len = ephy_customreg_read(phydev, 0x9)&0x1F;

			seq_printf(s, "Src Match On.(custom off %d, len %d)\n",
				custom_offset, custom_len);
			if(!(wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK)) {
				seq_printf(s, "\tMatch IPv4 Addr %pI4\n", &match_addr[6]);
				seq_printf(s, "\tMatch IPv6 Addr %pI6\n", match_addr);
			}

			if(custom_len) {
				seq_printf(s, "\tMatch Custom   ");
				for(i = sizeof(match_addr) - custom_len; i < sizeof(match_addr); i++) {
					seq_printf(s, " %02x", ((u8 *)match_addr)[i]);
				}
				seq_printf(s, "\n");
			}
		}

		/* wol_custom_pkt_da_ip_addrs_match_en */
		if(wol_control & EPHY_WOL_CUS_DA_MATCH_EN) {
			match_addr[0] = htons(ephy_customreg_read(phydev, 0x11));
			match_addr[1] = htons(ephy_customreg_read(phydev, 0x10));
			match_addr[2] = htons(ephy_customreg_read(phydev, 0x0f));
			match_addr[3] = htons(ephy_customreg_read(phydev, 0x0e));
			match_addr[4] = htons(ephy_customreg_read(phydev, 0x0d));
			match_addr[5] = htons(ephy_customreg_read(phydev, 0x0c));
			match_addr[6] = htons(ephy_customreg_read(phydev, 0x0b));
			match_addr[7] = htons(ephy_customreg_read(phydev, 0x0a));

			custom_offset = ephy_customreg_read(phydev, 0x12)&0xFF;
			custom_len = ephy_customreg_read(phydev, 0x13)&0x1F;

			seq_printf(s, "Dest Match On.(custom off %d, len %d)\n",
				custom_offset, custom_len);

			if(!(wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK)) {
				seq_printf(s, "\tMatch IPv4 Addr %pI4\n", &match_addr[6]);
				seq_printf(s, "\tMatch IPv6 Addr %pI6\n", match_addr);
			}

			if(custom_len) {
				seq_printf(s, "\tMatch Custom   ");
				for(i = sizeof(match_addr) - custom_len; i < sizeof(match_addr); i++) {
					seq_printf(s, " %02x", ((u8 *)match_addr)[i]);
				}
				seq_printf(s, "\n");
			}
		}
	}

	/* XXX Test WOL Custom */

	if(0) {
		u8 match_sip[4] = {10, 88, 92, 224};
		u8 match_dip[4] = {10, 88, 91, 216};

		unsigned char *dev_addr = phydev->attached_dev->dev_addr;

		ephy_test_init(phydev);

		ephy_wolreg_write(phydev, 0x0, dev_addr[4]<<8 | dev_addr[5]);
		ephy_wolreg_write(phydev, 0x1, dev_addr[2]<<8 | dev_addr[3]);
		ephy_wolreg_write(phydev, 0x2, dev_addr[0]<<8 | dev_addr[1]);

		ephy_customreg_write(phydev, 0x1, match_sip[0] <<8 | match_sip[1]);
		ephy_customreg_write(phydev, 0x0, match_sip[2] <<8 | match_sip[3]);
		ephy_customreg_write(phydev, 0x8, 12);//src ip offset
		ephy_customreg_write(phydev, 0x9, 4);//src ip len

		ephy_customreg_write(phydev, 0xb, match_dip[0] <<8 | match_dip[1]);
		ephy_customreg_write(phydev, 0xa, match_dip[2] <<8 | match_dip[3]);
		ephy_customreg_write(phydev, 0x12, 16);//dst ip offset
		ephy_customreg_write(phydev, 0x13, 4);//dst ip len

		if(false) {/* Test custom packet */
			//WOL MAGIC 28: ff ff ff ff ff ff 02 99 99 99 42 42 02 99 99 99
			ephy_customreg_write(phydev, 0x11, 0xFFFF);
			ephy_customreg_write(phydev, 0x10, 0xFFFF);
			ephy_customreg_write(phydev, 0x0f, 0xFFFF);
			ephy_customreg_write(phydev, 0x0e, 0x0299);
			ephy_customreg_write(phydev, 0x0d, 0x9999);
			ephy_customreg_write(phydev, 0x0c, 0x4242);
			ephy_customreg_write(phydev, 0x0b, 0x0299);
			ephy_customreg_write(phydev, 0x0a, 0x9999);

			ephy_customreg_write(phydev, 0x12, 28);//dst ip offset
			ephy_customreg_write(phydev, 0x13, 16);//dst ip len
		}

		if(true) {
			/* Test IPv4, IPv6, Custom
			Ethernet Dest MAC Addr Match 02:99:99:99:42:42
			Src Match On.(custom off 0, len 12)
					Match IPv4 Addr 0.7.0.8
					Match IPv6 Addr 0001:0002:0003:0004:0005:0006:0007:0008
					Match Custom    00 03 00 04 00 05 00 06 00 07 00 08
			Dest Match On.(custom off 16, len 8)
					Match IPv4 Addr 10.88.91.216
					Match IPv6 Addr 0000:0000:0000:0000:0000:ffff:0a58:5bd8
					Match Custom    00 00 ff ff 0a 58 5b d8
			*/
			ephy_customreg_write(phydev, 0x7, 0x0001);
			ephy_customreg_write(phydev, 0x6, 0x0002);
			ephy_customreg_write(phydev, 0x5, 0x0003);
			ephy_customreg_write(phydev, 0x4, 0x0004);
			ephy_customreg_write(phydev, 0x3, 0x0005);
			ephy_customreg_write(phydev, 0x2, 0x0006);
			ephy_customreg_write(phydev, 0x1, 0x0007);
			ephy_customreg_write(phydev, 0x0, 0x0008);

			ephy_customreg_write(phydev, 0x8, 0);//src ip offset
			ephy_customreg_write(phydev, 0x9, 12);//src ip len

			ephy_customreg_write(phydev, 0x11, 0x0000);
			ephy_customreg_write(phydev, 0x10, 0x0000);
			ephy_customreg_write(phydev, 0x0f, 0x0000);
			ephy_customreg_write(phydev, 0x0e, 0x0000);
			ephy_customreg_write(phydev, 0x0d, 0x0000);
			ephy_customreg_write(phydev, 0x0c, 0xffff);
			ephy_customreg_write(phydev, 0x0b, 0x0a58);
			ephy_customreg_write(phydev, 0x0a, 0x5bd8);

			ephy_customreg_write(phydev, 0x12, 16);//dst ip offset
			ephy_customreg_write(phydev, 0x13, 8);//dst ip len
		}

		ephy_wolreg_write(phydev, 0x3, ephy_wolreg_read(phydev, 0x3) | EPHY_WOL_EN|EPHY_WOL_CUSTOM_EN|EPHY_WOL_CUS_SA_MATCH_EN|EPHY_WOL_CUS_DA_MATCH_EN);

		/* WOL CUSTOM INT UnMask */
		phy_write(phydev, 30, phy_read(phydev, 30) | 0x1<<13);
	}
	
	return 0;
}

static int ephy_dbg_customreg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_customreg_dump_show, inode->i_private);
}

static ssize_t ephy_dbg_cumtomreg_dump_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;

	int ret;
	u32 addr, val;

	ret = sscanf(buf + *ppos, "%u:%x", &addr, &val);
	if(ret != 2) {
		return -EINVAL;
	}

	if(addr > 0x1F) {
		dev_err(&phydev->dev, "invalid address(%u)\n", addr);
		return -EINVAL;
	}
	if(val > 0xFFFF) {
		dev_err(&phydev->dev, "invalid value(0x%x)\n", val);
		return -EINVAL;
	}

	ephy_customreg_write(phydev, addr, val);

	return count;
}

static const struct file_operations ephy_dbg_customreg_dump_fops = {
	.open		= ephy_dbg_customreg_dump_open,
	.read		= seq_read,
	.write		= ephy_dbg_cumtomreg_dump_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ephy_dbg_dspreg_dump_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int addr = 0;

	seq_printf(s, "EPHY DSP Register dump\n");

	for(addr = 0; addr <= 0x1F; addr++) {
		seq_printf(s, "DSP%02d:0x%04x", addr, ephy_dspreg_read(phydev, addr));
		if(addr%4 == 3) {
			seq_printf(s, "\n");
		} else {
			seq_printf(s, "\t");
		}
	}

	return 0;
}

static int ephy_dbg_dspreg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_dspreg_dump_show, inode->i_private);
}

static ssize_t ephy_dbg_dspreg_dump_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;

	int ret;
	u32 addr, val;

	ret = sscanf(buf + *ppos, "%u:%x", &addr, &val);
	if(ret != 2) {
		return -EINVAL;
	}

	if(addr > 0x1F) {
		dev_err(&phydev->dev, "invalid address(%u)\n", addr);
		return -EINVAL;
	}
	if(val > 0xFFFF) {
		dev_err(&phydev->dev, "invalid value(0x%x)\n", val);
		return -EINVAL;
	}

	ephy_dspreg_write(phydev, addr, val);

	return count;
}

static const struct file_operations ephy_dbg_dspreg_dump_fops = {
	.open		= ephy_dbg_dspreg_dump_open,
	.read		= seq_read,
	.write		= ephy_dbg_dspreg_dump_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ephy_dbg_anypacket_wol_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	int wol_control = 0;

	seq_printf(s, "EPHY Anypacket WOL Status\n");
	seq_printf(s, "mode=[disable|auto|custom]\n");
	seq_printf(s, "src/dst-match,offset,length,ipaddr\n");

	ephy_test_init(phydev);

	/* Wol_custom_pkt_en */
	wol_control = ephy_wolreg_read(phydev, 0x3);

	if(!(wol_control&(EPHY_WOL_EN|EPHY_WOL_CUSTOM_EN))) {
		seq_printf(s, "\nAnypacket WOL is diabled\n");
		return 0;
	}

	if((wol_control&(EPHY_WOL_CUS_SA_MATCH_EN|EPHY_WOL_CUS_DA_MATCH_EN))) {

		u16 match_addr[8];
		u16 custom_offset;
		u16 custom_len;
		int i = 0;

		/* dest MAC Address */
		match_addr[0] = htons(ephy_wolreg_read(phydev, 0x2));
		match_addr[1] = htons(ephy_wolreg_read(phydev, 0x1));
		match_addr[2] = htons(ephy_wolreg_read(phydev, 0x0));

		seq_printf(s, "\nAnypacket WOL is enabled. %s %s\n",
			wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK?"Custom mode":"Auto mode",
			wol_control&EPHY_WOL_CUS_BYPASS_CRC_CHK?"Bypass CRC check":"");

		seq_printf(s, "Dst MAC Address Match %pM\n", match_addr);

		/* wol_custom_pkt_sa_ip_addrs_match_en */
		if(wol_control & EPHY_WOL_CUS_SA_MATCH_EN) {
			match_addr[0] = htons(ephy_customreg_read(phydev, 0x7));
			match_addr[1] = htons(ephy_customreg_read(phydev, 0x6));
			match_addr[2] = htons(ephy_customreg_read(phydev, 0x5));
			match_addr[3] = htons(ephy_customreg_read(phydev, 0x4));
			match_addr[4] = htons(ephy_customreg_read(phydev, 0x3));
			match_addr[5] = htons(ephy_customreg_read(phydev, 0x2));
			match_addr[6] = htons(ephy_customreg_read(phydev, 0x1));
			match_addr[7] = htons(ephy_customreg_read(phydev, 0x0));

			custom_offset = ephy_customreg_read(phydev, 0x8)&0xFF;
			custom_len = ephy_customreg_read(phydev, 0x9)&0x1F;

			seq_printf(s, "Src Match On.(custom off %d, len %d)\n",
				custom_offset, custom_len);
			if(!(wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK)) {
				seq_printf(s, "\tMatch IPv4 Addr %pI4\n", &match_addr[6]);
				seq_printf(s, "\tMatch IPv6 Addr %pI6\n", match_addr);
			}

			if(custom_len) {
				seq_printf(s, "\tMatch Custom   ");
				for(i = sizeof(match_addr) - custom_len; i < sizeof(match_addr); i++) {
					seq_printf(s, " %02x", ((u8 *)match_addr)[i]);
				}
				seq_printf(s, "\n");
			}
		}

		/* wol_custom_pkt_da_ip_addrs_match_en */
		if(wol_control & EPHY_WOL_CUS_DA_MATCH_EN) {
			match_addr[0] = htons(ephy_customreg_read(phydev, 0x11));
			match_addr[1] = htons(ephy_customreg_read(phydev, 0x10));
			match_addr[2] = htons(ephy_customreg_read(phydev, 0x0f));
			match_addr[3] = htons(ephy_customreg_read(phydev, 0x0e));
			match_addr[4] = htons(ephy_customreg_read(phydev, 0x0d));
			match_addr[5] = htons(ephy_customreg_read(phydev, 0x0c));
			match_addr[6] = htons(ephy_customreg_read(phydev, 0x0b));
			match_addr[7] = htons(ephy_customreg_read(phydev, 0x0a));

			custom_offset = ephy_customreg_read(phydev, 0x12)&0xFF;
			custom_len = ephy_customreg_read(phydev, 0x13)&0x1F;

			seq_printf(s, "Dest Match On.(custom off %d, len %d)\n",
				custom_offset, custom_len);

			if(!(wol_control&EPHY_WOL_CUS_BYPASS_TYPE_CHK)) {
				seq_printf(s, "\tMatch IPv4 Addr %pI4\n", &match_addr[6]);
				seq_printf(s, "\tMatch IPv6 Addr %pI6\n", match_addr);
			}

			if(custom_len) {
				seq_printf(s, "\tMatch Custom   ");
				for(i = sizeof(match_addr) - custom_len; i < sizeof(match_addr); i++) {
					seq_printf(s, " %02x", ((u8 *)match_addr)[i]);
				}
				seq_printf(s, "\n");
			}
		}
	} else {
		seq_printf(s, "\nAnypacket WOL is enabled. but src match, dst match is disabled\n");
	}

	return 0;
}

static int ephy_dbg_anypacket_wol_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_anypacket_wol_show, inode->i_private);
}

static ssize_t ephy_dbg_anypacket_wol_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct phy_device *phydev = s->private;
	unsigned char *dev_addr = phydev->attached_dev->dev_addr;

	char kbuf[128];
	char *running, *token, *opt, *val;
	size_t buf_size;

	int wol_config = 0;
	int src_offset = -1, dst_offset = -1, src_length = -1, dst_length = -1;
	bool src_match = false, dst_match = false, bypass_crc = false;
	u8 src_ipaddr[16], dst_ipaddr[16];

	if(count > (sizeof(kbuf)-1)) {
		return -ENOMEM;
	}

	buf_size = count;
	if (copy_from_user(kbuf, buf, buf_size))
		return -EFAULT;

	if(kbuf[count-1] == '\n')
		kbuf[count-1] = '\0';
	else
		kbuf[count] = '\0';

	wol_config = ephy_wolreg_read(phydev, 0x3);
	running = kbuf;
	token = strsep(&running, " ");

	ephy_test_init(phydev);

	ephy_wolreg_write(phydev, 0x0, dev_addr[4]<<8 | dev_addr[5]);
	ephy_wolreg_write(phydev, 0x1, dev_addr[2]<<8 | dev_addr[3]);
	ephy_wolreg_write(phydev, 0x2, dev_addr[0]<<8 | dev_addr[1]);

	while(token) {
		opt = token;
		val = strpbrk(token, "=");

		if(val != NULL) val[0] = '\0';
		if(!(val != NULL && val[1] != '\0')) {
			goto _invalid;
		}
		val++;

		//dev_info(&phydev->dev, "%s: %s:%s\n", __FUNCTION__, opt, val);
		if(strncasecmp("mode", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: mode: %s\n", __FUNCTION__, val);
			if(strncasecmp("disable", val, strlen(val)) == 0) {
				/* TODO: cheak EPHY_WOL_EN setting */
				wol_config &= ~EPHY_WOL_CUSTOM_EN;
			} else if(strncasecmp("iot", val, strlen(val)) == 0) {
				wol_config |= EPHY_WOL_EN | EPHY_WOL_CUSTOM_EN | EPHY_WOL_CUS_SA_MATCH_EN;
			
				/* WOL CUSTOM INT UnMask */
				phy_write(phydev, 30, phy_read(phydev, 30) | 0x2000);
			} else if(strncasecmp("ipv6", val, strlen(val)) == 0) {
				wol_config |= EPHY_WOL_EN | EPHY_WOL_MAGIC_DA_EN | EPHY_WOL_MAGIC_BA_EN | EPHY_WOL_MAGIC_ALL_EN;
			
				/* WOL CUSTOM INT UnMask */
				phy_write(phydev, 30, phy_read(phydev, 30) | 0x7<<9);
			} else if(strncasecmp("magic", val, strlen(val)) == 0) {
				wol_config |= EPHY_WOL_EN | EPHY_WOL_MAGIC_DA_EN | EPHY_WOL_MAGIC_BA_EN;
			
				/* WOL CUSTOM INT UnMask */
				phy_write(phydev, 30, phy_read(phydev, 30) | 0x3<<9);
			} else if(strncasecmp("auto", val, strlen(val)) == 0) {
				wol_config |= EPHY_WOL_EN | EPHY_WOL_CUSTOM_EN;
				wol_config &= ~EPHY_WOL_CUS_BYPASS_TYPE_CHK;

				/* WOL CUSTOM INT UnMask */
				phy_write(phydev, 30, phy_read(phydev, 30) | 0x1<<13);
			} else if(strncasecmp("custom", val, strlen(val)) == 0) {
				wol_config |= EPHY_WOL_EN | EPHY_WOL_CUSTOM_EN;
				wol_config |= EPHY_WOL_CUS_BYPASS_TYPE_CHK;

				/* WOL CUSTOM INT UnMask */
				phy_write(phydev, 30, phy_read(phydev, 30) | 0x1<<13);
			} else {
				goto _invalid;
			}

		} else if(strncasecmp("src-match", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: src-match: %s\n", __FUNCTION__, val);
			if(strtobool(val, &src_match) != 0) {
				goto _invalid;
			}

			if(src_match)
				wol_config |= EPHY_WOL_CUS_SA_MATCH_EN;
			else
				wol_config &= ~EPHY_WOL_CUS_SA_MATCH_EN;

		} else if(strncasecmp("dst-match", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: dst-match: %s\n", __FUNCTION__, val);
			if(strtobool(val, &dst_match) != 0) {
				goto _invalid;
			}

			if(dst_match)
				wol_config |= EPHY_WOL_CUS_DA_MATCH_EN;
			else
				wol_config &= ~EPHY_WOL_CUS_DA_MATCH_EN;

		} else if(strncasecmp("bypass-crc", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: bypass-crc: %s\n", __FUNCTION__, val);
			if(strtobool(val, &bypass_crc) != 0) {
				goto _invalid;
			}

			if(bypass_crc)
				wol_config |= EPHY_WOL_CUS_BYPASS_CRC_CHK;
			else
				wol_config &= ~EPHY_WOL_CUS_BYPASS_CRC_CHK;

		} else if(strncasecmp("src-offset", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: src-offset: %s\n", __FUNCTION__, val);
			if(kstrtoint(val, 10, &src_offset) == 0 && src_offset >= 0 && src_offset <= 0xFF) {
				ephy_customreg_write(phydev, 0x8, src_offset);
			} else {
				goto _invalid;
			}

		} else if(strncasecmp("dst-offset", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: dst-offset: %s\n", __FUNCTION__, val);
			if(kstrtoint(val, 10, &dst_offset) == 0 && dst_offset >= 0 && dst_offset <= 0x1F) {
				ephy_customreg_write(phydev, 0x12, dst_offset);
			} else {
				goto _invalid;
			}

		} else if(strncasecmp("src-length", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: src-length: %s\n", __FUNCTION__, val);
			if(kstrtoint(val, 10, &src_length) == 0 && src_length >= 0 && src_length <= 0xFF) {
				ephy_customreg_write(phydev, 0x9, src_length);
			} else {
				goto _invalid;
			}

		} else if(strncasecmp("dst-length", opt, strlen(opt)) == 0) {
			//dev_info(&phydev->dev, "%s: dst-length: %s\n", __FUNCTION__, val);
			if(kstrtoint(val, 10, &dst_length) == 0 && dst_length >= 0 && dst_length <= 0x1F) {
				ephy_customreg_write(phydev, 0x13, dst_length);
			} else {
				goto _invalid;
			}

		} else if(strncasecmp("src-ipaddr", opt, strlen(opt)) == 0) {
			const char *end;

			memset(src_ipaddr, 0x0, sizeof(src_ipaddr));
			if (!strchr(val, ':') && in4_pton(val, -1, &src_ipaddr[12], -1, &end) > 0) {
			} else if(in6_pton(val, -1, src_ipaddr, -1, &end) > 0) {
			} else {
				goto _invalid;
			}

			ephy_customreg_write(phydev, 0x7, htons(((u16*)src_ipaddr)[0]));
			ephy_customreg_write(phydev, 0x6, htons(((u16*)src_ipaddr)[1]));
			ephy_customreg_write(phydev, 0x5, htons(((u16*)src_ipaddr)[2]));
			ephy_customreg_write(phydev, 0x4, htons(((u16*)src_ipaddr)[3]));
			ephy_customreg_write(phydev, 0x3, htons(((u16*)src_ipaddr)[4]));
			ephy_customreg_write(phydev, 0x2, htons(((u16*)src_ipaddr)[5]));
			ephy_customreg_write(phydev, 0x1, htons(((u16*)src_ipaddr)[6]));
			ephy_customreg_write(phydev, 0x0, htons(((u16*)src_ipaddr)[7]));

		} else if(strncasecmp("dst-ipaddr", opt, strlen(opt)) == 0) {
			const char *end;

			memset(dst_ipaddr, 0x0, sizeof(dst_ipaddr));
			if (!strchr(val, ':') && in4_pton(val, -1, &dst_ipaddr[12], -1, &end) > 0) {
			} else if(in6_pton(val, -1, dst_ipaddr, -1, &end) > 0) {
			} else {
				goto _invalid;
			}

			ephy_customreg_write(phydev, 0x11, htons(((u16*)dst_ipaddr)[0]));
			ephy_customreg_write(phydev, 0x10, htons(((u16*)dst_ipaddr)[1]));
			ephy_customreg_write(phydev, 0x0f, htons(((u16*)dst_ipaddr)[2]));
			ephy_customreg_write(phydev, 0x0e, htons(((u16*)dst_ipaddr)[3]));
			ephy_customreg_write(phydev, 0x0d, htons(((u16*)dst_ipaddr)[4]));
			ephy_customreg_write(phydev, 0x0c, htons(((u16*)dst_ipaddr)[5]));
			ephy_customreg_write(phydev, 0x0b, htons(((u16*)dst_ipaddr)[6]));
			ephy_customreg_write(phydev, 0x0a, htons(((u16*)dst_ipaddr)[7]));

		} else {
			goto _invalid;
		}

		token = strsep(&running, " ");
	}

	ephy_wolreg_write(phydev, 0x3, wol_config);

	return count;

_invalid:
	dev_err(&phydev->dev, "%s: invalid token \"%s:%s\"\n", __FUNCTION__, opt, val==NULL?"":val);
	return -EINVAL;
}

static const struct file_operations ephy_dbg_anypacket_wol_fops = {
	.open		= ephy_dbg_anypacket_wol_open,
	.read		= seq_read,
	.write		= ephy_dbg_anypacket_wol_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ephy_dbg_bist_show(struct seq_file *s, void *data)
{
	struct phy_device *phydev = s->private;
	unsigned long timeout_ms = 1000;

	seq_printf(s, "EPHY Built-in self-test start..");

	/* BIST Enable */
	ephy_wolreg_write(phydev, 9, 0x0000);
	ephy_wolreg_write(phydev, 9, 0xFFFF);

	timeout_ms = jiffies_to_msecs(jiffies) + timeout_ms;
	/* BIST checker */
	while(!(ephy_wolreg_read(phydev, 13) & 0x401)) {
		if(timeout_ms <= jiffies_to_msecs(jiffies)) {
			seq_printf(s, " timeout!!!!\n");
			ephy_wolreg_write(phydev, 9, 0x0000);
			return 0;
		}
	}
	seq_printf(s, " done 0x%04x\n", ephy_wolreg_read(phydev, 13));

	/* BIST Disable */
	ephy_wolreg_write(phydev, 9, 0x0000);

	return 0;
}

static int ephy_dbg_bist_open(struct inode *inode, struct file *file)
{
	return single_open(file, ephy_dbg_bist_show, inode->i_private);
}

static const struct file_operations ephy_dbg_bist_fops = {
	.open		= ephy_dbg_bist_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int
sdp1406_ephy_dbg_sw_mdix_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;
	struct sdp_mac_phy_priv *priv = phydev->priv;

	*val = (u64)priv->ephy_sw_mdix_en;
	return 0;
}

static int
sdp1406_ephy_dbg_sw_mdix_set(void *data, u64 val)
{
	struct phy_device *phydev = data;
	struct sdp_mac_phy_priv *priv = phydev->priv;

	dev_info(&phydev->dev, "EPHY SW MDIX is %s(Previous %s)\n",
		val?"Enabled":"Disabled", priv->ephy_sw_mdix_en?"Enabled":"Disabled");
	priv->ephy_sw_mdix_en = val;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp1406_ephy_dbg_sw_mdix_fops, sdp1406_ephy_dbg_sw_mdix_get, sdp1406_ephy_dbg_sw_mdix_set,
	"%lld\n");

static int
sdp1406_ephy_dbg_ldo_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;
	void *addr;

	*val = 0;

	addr = ioremap_nocache(SDP1406_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	*val = readl(addr);
	iounmap(addr);

	return 0;
}

static int
sdp1406_ephy_dbg_ldo_set(void *data, u64 val)
{
	struct phy_device *phydev = data;
	void *addr;

	if(val > 0x1F) {
		dev_err(&phydev->dev, "%s: ldo value(0x%llx) is invalied!\n", __FUNCTION__, val);
		return -EINVAL;
	}

	addr = ioremap_nocache(SDP1406_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	writel((readl(addr)&~(0x1FUL<<20)) | ((u32)val<<20) | (0x1<<19), addr);
	wmb();
	dev_info(&phydev->dev, "set ldo 0x%02llx(raw 0x%08x)\n", val, readl(addr));
	iounmap(addr);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp1406_ephy_dbg_ldo_fops, sdp1406_ephy_dbg_ldo_get, sdp1406_ephy_dbg_ldo_set,
	"0x%08llx\n");


static int
sdp1501_ephy_dbg_ldo_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;
	void *addr;

	*val = 0;

	addr = ioremap_nocache(SDP1501_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	*val = (readl(addr)>>8)&0x1F;
	iounmap(addr);

	return 0;
}

static int
sdp1501_ephy_dbg_ldo_set(void *data, u64 val)
{
	struct phy_device *phydev = data;
	void *addr;

	if(val > 0x1F) {
		dev_err(&phydev->dev, "%s: ldo value(0x%llx) is invalied!\n", __FUNCTION__, val);
		return -EINVAL;
	}

	addr = ioremap_nocache(SDP1501_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	writel((readl(addr)&~(0x1FUL<<8)) | ((u32)val<<8) | (0x1<<16), addr);
	wmb();
	dev_info(&phydev->dev, "set ldo 0x%02llx(raw 0x%08x)\n", val, readl(addr));
	iounmap(addr);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp1501_ephy_dbg_ldo_fops, sdp1501_ephy_dbg_ldo_get, sdp1501_ephy_dbg_ldo_set,
	"0x%08llx\n");

static int
sdp1601_ephy_dbg_ldo_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;
	void *addr;
	u32 ldo_arr[27]={0x17,0x14,0x15,0x1A,0x1B,0x18,0x19,0x1E,0x1F,0x1C,0x1D,0x2,0x3,0,0x1,0x6,0x7,0x4,0x5,0xA,0xB,0x8,0x9,0xE,0xF,0xC,0xD};
	u64 i=0;
	int temp=0;
	*val = 0;


	addr = ioremap_nocache(SDP1601_EPHY_LDO_ADDR, sizeof(u64));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	temp = (readl(addr)>>8)&0x1F;
	iounmap(addr);

	for(i = 0; i < 27; i++){
		if(temp==ldo_arr[i]){
			*val = i;
		}
	}

	return 0;
}

static int
sdp1601_ephy_dbg_ldo_set(void *data, u64 val)
{
	struct phy_device *phydev = data;
	void *addr;
	u32 ldo_arr[27]={0x17,0x14,0x15,0x1A,0x1B,0x18,0x19,0x1E,0x1F,0x1C,0x1D,0x2,0x3,0,0x1,0x6,0x7,0x4,0x5,0xA,0xB,0x8,0x9,0xE,0xF,0xC,0xD};
	
	if(val > 26) {
		dev_err(&phydev->dev, "%s: ldo value(0x%llx) is invalied!\n", __FUNCTION__, val);
		return -EINVAL;
	}

	addr = ioremap_nocache(SDP1601_EPHY_LDO_ADDR, sizeof(u64));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	writel((readl(addr)&~(0x1FUL<<8)) | ((u32)ldo_arr[val]<<8), addr);
	wmb();
	dev_info(&phydev->dev, "set ldo 0x%02llx(raw 0x%08x)\n", val, readl(addr));
	iounmap(addr);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp1601_ephy_dbg_ldo_fops, sdp1601_ephy_dbg_ldo_get, sdp1601_ephy_dbg_ldo_set,
	"%08llu\n");



/* create debugfs node! */
static void
sdp_phy_add_debugfs(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	struct dentry *root;

	//root = debugfs_create_dir(dev_name(&phydev->dev), NULL);
	root = debugfs_create_dir("mdio:00", NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	priv->debugfs_root = root;

	if (!debugfs_create_file("reg_dump", S_IRUSR|S_IWUSR, root, phydev, &ephy_dbg_reg_dump_fops))
		goto err_node;

	if(!debugfs_create_u32("dynamic_printk_level", S_IRUGO|S_IWUGO, root, &priv->dynamic_printk_level))
		goto err_node;

	if((phydev->drv->phy_id&0xFFFFFFF0) == 0xABCD0000) {

		if (!debugfs_create_file("wolreg_dump", S_IRUSR, root, phydev, &ephy_dbg_wolreg_dump_fops))
			goto err_node;
#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
		if (!debugfs_create_file("bistreg_dump", S_IRUSR, root, phydev, &ephy_dbg_bistreg_dump_fops))
			goto err_node;
#endif

		if(ephy_is_support_anypacket(phydev)) {
			if (!debugfs_create_file("customreg_dump", S_IRUSR, root, phydev, &ephy_dbg_customreg_dump_fops))
				goto err_node;

			if (!debugfs_create_file("anypacket_wol", S_IRUSR, root, phydev, &ephy_dbg_anypacket_wol_fops))
				goto err_node;
		}

		if (!debugfs_create_file("dspreg_dump", S_IRUSR, root, phydev, &ephy_dbg_dspreg_dump_fops))
			goto err_node;

		if (!debugfs_create_file("bist", S_IRUSR, root, phydev, &ephy_dbg_bist_fops))
			goto err_node;

		if (!debugfs_create_file("sw_mdix_en", S_IRUSR, root, phydev, &sdp1406_ephy_dbg_sw_mdix_fops))
			goto err_node;

		if(of_machine_is_compatible("samsung,sdp1406") || of_machine_is_compatible("samsung,sdp1406fhd")) {
			if (!debugfs_create_file("ldo", S_IRUSR, root, phydev, &sdp1406_ephy_dbg_ldo_fops))
				goto err_node;
		} 
		else if(of_machine_is_compatible("samsung,sdp1501") || of_machine_is_compatible("samsung,sdp1511")
			 || of_machine_is_compatible("samsung,sdp1521") || of_machine_is_compatible("samsung,sdp1531")) {
			if (!debugfs_create_file("ldo", S_IRUSR, root, phydev, &sdp1501_ephy_dbg_ldo_fops))
				goto err_node;
		}
		else if(of_machine_is_compatible("samsung,sdp1601")){
			if (!debugfs_create_file("ldo", S_IRUSR, root, phydev, &sdp1601_ephy_dbg_ldo_fops))
				goto err_node;
		}
	}

	return;

err_node:
	debugfs_remove_recursive(root);
	priv->debugfs_root = NULL;
err_root:
	dev_err(&phydev->dev, "failed to initialize debugfs\n");
}

static void
sdp_phy_remove_debugfs(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;

	debugfs_remove_recursive(priv->debugfs_root);
}
#endif

#if defined(CONFIG_ARCH_SDP1803)
static int set_serdes_path(bool flag){
	int restore = 0;

	restore = (readl(ephy_serdes_path)>>EPHY_SERDES_SET_BIT) & 0x1;
	if(flag == true){
		writel(readl(ephy_serdes_path)|EPHY_SERDES_PATH, ephy_serdes_path);
	}
	else{
		writel(readl(ephy_serdes_path)&(~EPHY_SERDES_PATH), ephy_serdes_path);
	}

	return restore;
}

static int get_serdes_path(bool flag){
	int restore = 0;

	restore = (readl(ephy_serdes_path)>>EPHY_SERDES_SET_BIT) & 0x1;

	return restore;
}

static int nvt_config_init(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int mdi_mode = 0;
	int serdes_enable = 0;

	dev_info(&phydev->dev, "nvt_ephy_config_init!\n");

	mutex_lock(&phydev->lock);

	serdes_enable = set_serdes_path(false);

	ephy_dsp_register_init(phydev);
	ephy_bistreg_write(phydev, 0x1B, 0x0005);

	mdi_mode = phy_read(phydev, 17);
	mdi_mode &= (~EPHY_MDIO17_AUTO_MDIX_EN);
	phy_write(phydev, 17, mdi_mode);

	dev_err(&phydev->dev, "Omniphy(REG17) now %s %s mode",
	phy_read(phydev, 17)&EPHY_MDIO17_AUTO_MDIX_EN?"Auto":"Manual",
	mdi_mode&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI");	

	set_serdes_path(serdes_enable ? true : false);	

	mutex_unlock(&phydev->lock);

	return 0;
}

static int nvt_phy_suspend(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int value;
	int serdes_enable = 0;

	PM_DEV_DBG(&phydev->dev, "%s: enter\n", __FUNCTION__);

	if(priv->selftest_enable){
		PM_DEV_DBG(&phydev->dev, "%s: exit selftest suspend \n", __FUNCTION__);	
		return 0;
	}

	serdes_enable = set_serdes_path(false);

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, MII_BMCR);

	priv->bmcr = (u16)value;
	PM_DEV_DBG(&phydev->dev, "%s: save bmcr 0x%04x\n", __FUNCTION__, priv->bmcr);
	
	mutex_unlock(&phydev->lock);

	set_serdes_path(serdes_enable ? true : false);

	PM_DEV_DBG(&phydev->dev, "%s: exit\n", __FUNCTION__);

	return 0;
}

static int nvt_phy_resume(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int serdes_enable = 0;
	
	PM_DEV_DBG(&phydev->dev, "%s: enter\n", __FUNCTION__);

	if(priv->selftest_enable){
		PM_DEV_DBG(&phydev->dev, "%s: exit selftest resume \n", __FUNCTION__);	
		return 0;
	}
	
	serdes_enable = set_serdes_path(false);

	PM_DEV_DBG(&phydev->dev, "%s: call phy_init_hw\n", __FUNCTION__);

	/* include reset, scan fixup, config init */
	phy_init_hw(phydev);

	mutex_lock(&phydev->lock);

	PM_DEV_DBG(&phydev->dev, "%s: restore bmcr 0x%04x\n", __FUNCTION__, priv->bmcr);
	phy_write(phydev, MII_BMCR, priv->bmcr);
	priv->bmcr = 0x0;

	mutex_unlock(&phydev->lock);

	set_serdes_path(serdes_enable ? true : false);

	dev_err(&phydev->dev, "ephy bmcr : 0x%x, bmsr: 0x%x, phyid: 0x%x", 
		phy_read(phydev, 0), phy_read(phydev, 1), (phy_read(phydev, 2)<<16) | phy_read(phydev, 3));


	PM_DEV_DBG(&phydev->dev, "%s: exit\n", __FUNCTION__);

	return 0;
}
#endif

static int common_phy_suspend(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int value;

	PM_DEV_DBG(&phydev->dev, "%s: enter\n", __FUNCTION__);

	if(priv->selftest_enable){
		PM_DEV_DBG(&phydev->dev, "%s: exit selftest suspend \n", __FUNCTION__);	
		return 0;
	}

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, MII_BMCR);

	priv->bmcr = (u16)value;
	PM_DEV_DBG(&phydev->dev, "%s: save bmcr 0x%04x\n", __FUNCTION__, priv->bmcr);
	
#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	//To check power_down of ephy after supend/resume
	if(phy_read(phydev, MII_BMSR) & (0x1<<2)){
		phy_write(phydev, 17, (phy_read(phydev, 17) | 0x1));
	}
#endif

	mutex_unlock(&phydev->lock);

	PM_DEV_DBG(&phydev->dev, "%s: exit\n", __FUNCTION__);

	return 0;
}

static int common_phy_resume(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;

	PM_DEV_DBG(&phydev->dev, "%s: enter\n", __FUNCTION__);
#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
	if(priv->selftest_enable){
		PM_DEV_DBG(&phydev->dev, "%s: exit selftest resume \n", __FUNCTION__);	
		return 0;
	}

	if(!(phy_read(phydev, 17) & 0x1)){
		PM_DEV_DBG(&phydev->dev, "%s: call phy_init_hw\n", __FUNCTION__);

		/* include reset, scan fixup, config init */
		phy_init_hw(phydev);

		mutex_lock(&phydev->lock);

		PM_DEV_DBG(&phydev->dev, "%s: restore bmcr 0x%04x\n", __FUNCTION__, priv->bmcr);
		phy_write(phydev, MII_BMCR, priv->bmcr);
		priv->bmcr = 0x0;

		dev_err(&phydev->dev, "ephy bmcr : 0x%x, bmsr: 0x%x, phyid: 0x%x", 
			phy_read(phydev, 0), phy_read(phydev, 1), (phy_read(phydev, 2)<<16) | phy_read(phydev, 3));

		mutex_unlock(&phydev->lock);
	}
#else
	PM_DEV_DBG(&phydev->dev, "%s: call phy_init_hw\n", __FUNCTION__);

	/* include reset, scan fixup, config init */
	phy_init_hw(phydev);

	mutex_lock(&phydev->lock);

	PM_DEV_DBG(&phydev->dev, "%s: restore bmcr 0x%04x\n", __FUNCTION__, priv->bmcr);
	phy_write(phydev, MII_BMCR, priv->bmcr);
	priv->bmcr = 0x0;

	mutex_unlock(&phydev->lock);
#endif

	PM_DEV_DBG(&phydev->dev, "%s: exit\n", __FUNCTION__);

	return 0;
}

static int common_phy_probe(struct phy_device *phydev)
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

	priv->dynamic_printk_level = 8;

	phydev->priv = priv;

#ifdef CONFIG_DEBUG_FS
	sdp_phy_add_debugfs(phydev);
#endif
	return 0;
}

static void common_phy_remove(struct phy_device *phydev)
{
	if((phydev->drv->phy_id_mask & 0xF) == 0) {
		dev_info(&phydev->dev, "%s%c removed.\n", phydev->drv->name, (phydev->phy_id&0xF)+'A'-1);
	} else {
		dev_info(&phydev->dev, "%s removed.\n", phydev->drv->name);
	}

#ifdef CONFIG_DEBUG_FS
	sdp_phy_remove_debugfs(phydev);
#endif

	kfree(phydev->priv);
	phydev->priv = NULL;
}

static int ephy_sw_reset(struct phy_device *phydev)
{
	return 0;
}

int ephy_hw_reset(void)
{
	u8 * __iomem iomem;
	u32 value = 0;

	iomem = ioremap_nocache(0x9C0800, sizeof(phys_addr_t));
	value = readl(iomem);
	writel(value & ~0x30, iomem);
	mdelay(10);
	writel(value | 0x30, iomem);
	mdelay(10);
	iounmap(iomem);

	return 0;
}
EXPORT_SYMBOL(ephy_hw_reset);


#if defined(CONFIG_ARCH_SDP1601)
static void sdp1601_init(struct phy_device *phydev){

	ephy_hw_reset();

	if(get_sdp_board_type() == SDP_BOARD_JACKPACK){
		ephy_test_init(phydev);
		ephy_bistreg_write(phydev, 0x1B, 0x0005);
		ephy_bistreg_write(phydev, 0x1D, 0x028B);
		ephy_bistreg_write(phydev, 0x1C, 0x1700);
	}
}
#endif

static int ephy_probe(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = NULL;

#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1406) || defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1412) || defined(CONFIG_ARCH_SDP1803)
	static DEF_RWCONST_U32(EPHY_SW_MDIX_VAL, 1, "EPHY S/W MDIX Enable value(disable:0)");
#endif

	common_phy_probe(phydev);
	priv = phydev->priv;
	priv->selftest_enable = 0;

	/* set ephy spec value */
#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1406) || defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1412) || defined(CONFIG_ARCH_SDP1803)
	priv->ephy_sw_mdix_en = REF_RWCONST_U32(EPHY_SW_MDIX_VAL);
#endif
	dev_info(&phydev->dev, "EPHY SW MDIX is %s\n",	priv->ephy_sw_mdix_en?"Enabled":"Disabled");

#if defined(CONFIG_ARCH_SDP1601)
	sdp1601_init(phydev);
#endif
#if defined(CONFIG_ARCH_SDP1803)
	ephy_serdes_path = ioremap(EMAC_CTRL_0, sizeof(u32));
#endif

	return 0;
}


/* supported phy list */
static struct phy_driver sdp_phy_drivers[] = {
	{/* RTL8201E */
		.phy_id		= 0x001CC815,
		.name		= "RTL8201E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
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
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
		.config_init	= rtl8201f_config_init,
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
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
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
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
		.config_init	= rtl8211e_config_init,
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
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
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
		.probe			= common_phy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
		.config_init	= rtl8304e_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= rtl8304e_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},

	{/* SDP EPHY(SoC internal PHY) */
		.phy_id		= 0xABCD0000,
		.name		= "SDP-EPHY",
		.soft_reset = ephy_sw_reset,
		.phy_id_mask	= 0xfffffff0,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= ephy_probe,
		.remove			= common_phy_remove,
		.suspend		= common_phy_suspend,
		.resume			= common_phy_resume,
		.config_init	= ephy_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= ephy_read_status,
		.get_wol = ephy_get_wol,
		.set_wol = ephy_set_wol,
		.driver		= { .owner = THIS_MODULE,},
	},

#if defined(CONFIG_ARCH_SDP1803)
	{/* NVT EPHY */
		.phy_id		= 0x00000001,
		.phy_id_mask	= 0xffffffff,
		.name		= "NVT-EPHY",
		.soft_reset = ephy_sw_reset,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= ephy_probe,
		.remove 		= common_phy_remove,
		.suspend		= nvt_phy_suspend,
		.resume 		= nvt_phy_resume,
		.config_init	= nvt_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= nvt_ephy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
#endif
	
};

static int __init sdp_phy_init(void)
{
	int i;
	int ret = 0;

	for(i = 0; i < (int)ARRAY_SIZE(sdp_phy_drivers); i++)
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
	for(i = 0; i < (int)ARRAY_SIZE(sdp_phy_drivers); i++) {
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
	{ 0 }
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

