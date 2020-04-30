/***************************************************************************
 *
 *	drivers/net/ethernet/sdp/sdp_mac_phy.h
 *	Samsung Elecotronics.Co
 *	Created by jk97.son
 *
 * ************************************************************************/
/*
 * 2016.10.18,jk97.son: Created by jk97.son
 */



//debug mode
#ifdef CONFIG_PM_DEBUG
#define PM_DEV_DBG(arg...)	dev_printk(KERN_DEBUG, arg)
#else
#define PM_DEV_DBG(arg...)	dev_dbg(arg)
#endif

struct sdp_mac_phy_priv {
	u16 bmcr;/* saved phy bmcr value */
	struct dentry *debugfs_root;
	int dynamic_printk_level;

	/* EPHY Property */
	u32 ephy_sw_mdix_en;
	int ephy_sw_mdix_switch_cnt;
	unsigned long ephy_next_mdix_jiffies;
	bool selftest_enable;
};

#define EPHY_PHYID			0xabcd0005

//external PHY register
#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

#define RTL8304E_PORT0	0x0
#define RTL8304E_PORT1	0x1
#define RTL8304E_MAC2	0x5
#define RTL8304E_MAC3	0x6
#define RTL8304E_MAC2_PHY	0x7

//internal PHY register.
#define EPHY_MDIO17_MDI_MODE		(0x1U<<6)
#define EPHY_MDIO17_AUTO_MDIX_EN	(0x1U<<7)


//internal DSP register[20]
#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803)
#define EPHY_TSTCNTL_READ	(1<<15)
#define EPHY_TSTCNTL_WRITE	(1<<14)
#define EPHY_TSTCNTL_WOLCUS_REG_SEL	(1<<13)
#define EPHY_TSTCNTL_BIST_REG_SEL	(3<<11)
#define EPHY_TSTCNTL_WOL_REG_SEL	(1<<11)
#define EPHY_TSTCNTL_TEST_MODE	(1<<10)
#define EPHY_TSTCNTL_READ_OFF	(5)
#define EPHY_TSTCNTL_WRITE_OFF	(0)
#else
#define EPHY_TSTCNTL_READ	(1<<15)
#define EPHY_TSTCNTL_WRITE	(1<<14)
#define EPHY_TSTCNTL_WOL_REG_SEL	(1<<11)
#define EPHY_TSTCNTL_WOLCUS_REG_SEL	(1<<12)
#define EPHY_TSTCNTL_TEST_MODE	(1<<10)
#define EPHY_TSTCNTL_READ_OFF	(5)
#define EPHY_TSTCNTL_WRITE_OFF	(0)
#endif

#define EPHY_WOL_EN	(0x1<<0)
#define EPHY_WOL_MAGIC_DA_EN	(0x1<<1)
#define EPHY_WOL_MAGIC_BA_EN	(0x1<<2)
#define EPHY_WOL_MAGIC_ALL_EN	(0x1<<3)
#define EPHY_WOL_MAGIC_SA_UC_EN	(0x1<<4)
#define EPHY_WOL_CUSTOM_EN	(0x1<<5)
#define EPHY_WOL_CUS_SA_MATCH_EN	(0x1<<6)
#define EPHY_WOL_CUS_DA_MATCH_EN	(0x1<<7)
#define EPHY_WOL_CUS_BYPASS_TYPE_CHK	(0x1<<8)
#define EPHY_WOL_CUS_BYPASS_CRC_CHK	(0x1<<9)

#define SDP1406_EPHY_LDO_ADDR 0x00580500
#define SDP1501_EPHY_LDO_ADDR 0x007C1068
#define SDP1601_EPHY_LDO_ADDR 0x009C12A8

#if defined(CONFIG_ARCH_SDP1601)
#define SDP1701_CHIP_M 0
#define SDP1701_CHIP_Ms 1
#define SDP1701_CHIP_Me 2
#endif

#if defined(CONFIG_ARCH_SDP1803)
#define EMAC_CTRL_0 				0x140000
#define EPHY_SERDES_SET_BIT 		12
#define EPHY_SERDES_PATH 			(1<<EPHY_SERDES_SET_BIT)
#endif

#define EPHY_DSP0_GAIN_PRE_BIT					0		
#define EPHY_DSP0_ALLOW_TRANSITION_BIT			5
#define EPHY_DSP0_STATE_BIT						8
#define EPHY_DSP0_FEQ_COEFF_IS_SMALL_BIT		10
#define EPHY_DSP0_TIMEOUT_VIOLATION_BIT			11
#define EPHY_DSP0_BLW_VALID_BIT					12
#define EPHY_DSP0_OFF_ENERGY_DETECT_BIT			13
#define EPHY_DSP0_MLT3_ZERO_VIOLATION_BIT		14
#define EPHY_DSP0_ENERGY_DETECT_BIT				15

static void *ephy_serdes_path;

int ephy_hw_reset(void);


