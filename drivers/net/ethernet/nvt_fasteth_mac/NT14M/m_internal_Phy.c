/**\file
 *  To distinguish different manufacture
 * \internal
 * ----------------------------REVISION HISTORY-----------------------------
 * NVT			15/Apr/2007			Created
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/clk.h>

#include "synopGMAC_Host.h"
#include "synopGMAC_plat.h"
#include "synopGMAC_network_interface.h"
#include "synopGMAC_Dev.h"
#include "m_internal_Phy.h"

extern raw_spinlock_t clock_gen_lock;

struct timer_list NVT_Phy_InitTimer;
u32 NVT_Phy_Init_Done = 1;

u32 *internal_PHY_reset;
u32 *internal_PHY_ADC_POW;
u32 *internal_PHY_TX_POW;

u32 *Phy_EQ_Reset;




void synopGMAC_eq_reset_function(u32 notused)
{
	u32 value;

	value = readl(Phy_EQ_Reset);
	value &= (u32)(~(0x00000002));
	writel(value, Phy_EQ_Reset);
}

void synopGMAC_eq_set_function(void)
{
	u32 value;
	value = readl(Phy_EQ_Reset);
	value |= 0x00000002;
	writel(value, Phy_EQ_Reset);
}

void  synopGMAC_set_nvt_phy_mii(synopGMACdevice * gmacdev)
{
	u16 data;

	data = 0;
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase, 0x1a, data);
	return;
}

s32  synopGMAC_nvt_phy_disable_isolate(synopGMACdevice * gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase, PHY_CONTROL_REG, &data);

	data = data & (~(u16)0x0040);
	status = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

s32  synopGMAC_nvt_phy_power_down(synopGMACdevice * gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase, PHY_CONTROL_REG, &data);

	data |= 0x0800;
	status = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

s32  synopGMAC_nvt_phy_power_on(synopGMACdevice * gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase, PHY_CONTROL_REG, &data);

	data = data & (~(u16)0x0800);
	status = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

void  synopGMAC_nvt_phy_reset_RX_statemach(synopGMACdevice * gmacdev)
{
	u16 data;
	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase, 0x10, &data);
	data |= 0x0008;
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x10, data);
	data = data & (~(u16)0x0008);
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x10, data);
}

u16  synopGMAC_get_nvt_phy_link_trigger_level(synopGMACdevice * gmacdev)
{	u16 data;
	data = 0;

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_EXT_ADDR_CBL_DIAG, &data);
	return data;
}

void  synopGMAC_set_nvt_phy_link_trigger_level(synopGMACdevice * gmacdev)
{
	u16 data;

	data = 0;
	//+++ HCK, Fine-tune (trigger/zero crossing) level for link state detection.
	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_EXT_ADDR_CBL_DIAG, &data);
	data |= 0x0180;         // trigger level
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_EXT_ADDR_CBL_DIAG, data);
}


void  nvt_internal_PHY_inverse_clock(synopGMACdevice * gmacdev){

	unsigned long flags;

#ifdef USE_DEVICE_TREE
	u32 value;
	// Set MII clock enable
	ioremap_AND_value(0xFD1300B0, (u32)(~(0x00000040)));

	// MII TX(22) RX(23).
	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	value = readl(gmacdev->ClockCtrlReg);
	value |= 0x00C00000;
	writel(value, gmacdev->ClockCtrlReg);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

#else
	u32 *clock_ctrl;
	// Set MII clock enable.
	ioremap_AND_value(0xFD1300B0, (u32)(~(0x00000040)));

	// MII TX(22) RX(23).
	clock_ctrl = ioremap(MAC_CLOCK_INVERSE_ADDR, 0x4);

	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	*clock_ctrl |= 0x00C00000;
	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

	iounmap(clock_ctrl);
#endif

}


void nvt_internal_PHY_iounmap_all(void){

	//original code
	iounmap(internal_PHY_reset);
	iounmap(internal_PHY_ADC_POW);
	iounmap(internal_PHY_TX_POW);

	iounmap(Phy_EQ_Reset);
}

int nvt_internal_PHY_ioremap_all(void){

	Phy_EQ_Reset = ioremap(NVT_PHY_REG_EQ_RESET, 0x4);
	if(Phy_EQ_Reset == NULL){
		return -1;
	}

	internal_PHY_reset = ioremap(0xFD130000, 0x4);
	if(internal_PHY_reset == NULL){
		return -ESYNOPGMACNOMEM;
	}

	internal_PHY_ADC_POW = ioremap(0xFD13009C, 0x4);
	if(internal_PHY_ADC_POW == NULL){
		return -ESYNOPGMACNOMEM;
	}

	internal_PHY_TX_POW = ioremap(0xFD1300A8, 0x4);
	if(internal_PHY_TX_POW == NULL){
		return -ESYNOPGMACNOMEM;
	}

	return ESYNOPGMACNOERR;
}


int nvt_nternal_PHY_display_status(synopGMACdevice * gmacdev){
	s32 status = ESYNOPGMACNOERR;
	u16 data;

	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG, &data);
	if(status)	
		return status;
	printk(KERN_CRIT "[NTKETHMAC] Auto-nego result = 0x%04x\n",data);
	if ( data & Mii_Speed_100 ){
		gmacdev->Speed = SPEED100;
		printk(KERN_CRIT "[NTKETHMAC] PHY 100M\n");
	}else{
		gmacdev->Speed = SPEED10;
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_ERROR] PHY 10M\033[0m\n");
	}

	if ( data & Mii_Duplex ){
		gmacdev->DuplexMode = FULLDUPLEX;
		printk(KERN_CRIT "[NTKETHMAC] PHY FULLDUPLEX\n");
	}else{
		gmacdev->DuplexMode = HALFDUPLEX;
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_ERROR] PHY HALFDUPLEX\033[0m\n");
	}

	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x4, &data);
	printk(KERN_CRIT "[NTKETHMAC] PHY reg 0x4 data = 0x%04x\n",data);
	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x5, &data);
	printk(KERN_CRIT "[NTKETHMAC] PHY reg 0x5 data = 0x%04x\n",data);
	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x6, &data);
	printk(KERN_CRIT "[NTKETHMAC] PHY reg 0x6 data = 0x%04x\n",data);

	printk(KERN_CRIT "[NTKETHMAC] : Set PHY into rising edge output\n");		

	return status;
}

/*
 *	internal PHY reset
 */
void nvt_internal_PHY_reset(synopGMACdevice * gmacdev){
	u16 data;

	*internal_PHY_reset  |= 0x2;	// Global Reset Digital Phy.

	//+++ Setting of Analog Phy.
	ioremap_OR_value(0xFD13009C, 0x00000001);	// Power down - ADC.
	mdelay(10);

	ioremap_OR_value(0xFD1302E0, 0x00000010);	// Power down - DAC.
	ioremap_OR_value(0xFD1302C0, 0x00000003);	// Power down - LDRV (Line driver) 1/0.
	mdelay(10);

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,  gmacdev->PhyBase, PHY_CONTROL_REG, &data);
	data |= Mii_reset;
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, data);

	/* Power on RX ATT Power */
	ioremap_OR_value(0xFD1300CC, 0x00000001);
	ioremap_AND_value(0xFD13008C, (u32)(~(0x00000001)));

	/* Power on 100M Base RX Power (PGA) */
	ioremap_OR_value(0xFD1300D8, 0x00000001);
	ioremap_AND_value(0xFD130098, (u32)(~(0x00000001)));

	/* Power on 10M Base RX Power */
	ioremap_OR_value(0xFD1300D0, 0x00000001);
	ioremap_AND_value(0xFD130090, (u32)(~(0x00000001)));

	/* Power on ADC Power */
	ioremap_AND_value(0xFD13009C, (u32)(~(0x00000001)));
	mdelay(10);

	/* Power on DAC and LDRV (Line driver) 1/0. */
	ioremap_AND_value(0xFD1302E0, (u32)(~(0x00000010)));
	ioremap_AND_value(0xFD1302C0, (u32)(~(0x00000003)));
	//--- Setting of Analog Phy.

	*internal_PHY_reset  = 0;		// Global Reset Digital Phy (EQ reset included).

	mdelay(1200);

	synopGMAC_set_nvt_phy_mii(gmacdev);
	synopGMAC_set_nvt_phy_link_trigger_level(gmacdev);
}
static void nvt_Phy_InitTimer_function(u32 notused)
{
	NVT_Phy_Init_Done = 1;
	printk(KERN_CRIT "[NTKETHMAC]: Reset Digital Phy is ok.\n");
}


void SysPlat_Init_Ethernet_Phy(synopGMACdevice * gmacdev, int Init_boot)
{
	u16 data;
#ifdef USE_DEVICE_TREE
	u32 value;
#else
	u32 *clock_ctrl;
#endif

	// Set MPLL speed into 50MHz.
	SYS_CLK_SetMpll(EN_MPLL_OFF_ETH, 50);

	// Set STBC Key.
	ioremap_write(MAC_STBC_KEY_ADDR,	 0x72682);
	ioremap_write(MAC_STBC_KEY_ADDR,	 0x28627);
	ioremap_write(MAC_STBC_KEY_SET_ADDR, 0x1);

	// Switch MAC controlling to ARM.
	ioremap_OR_value(MAC_STBC_CTRL_SWITCH_ADDR, 0x00000100);

	// Switch MAC clock from AHB.
	ioremap_OR_value(MAC_CLK_SRC_SWITCH_ADDR, 0x0000000C);


#ifdef USE_DEVICE_TREE

	#ifndef CONFIG_NVT_INTERNAL_PHY
	// Select RMII mode.
	value = readl(gmacdev->RmiiModeCtrlReg);
	value |= 0x80000000;
	writel(value, gmacdev->RmiiModeCtrlReg);
	#endif

	// Set GPIO Pin Mux.
	#ifdef MAC_RMII_CLK_PHY_TO_MAC
	value = readl(gmacdev->RxPadinReg);
	value &= (u32)(~(0x02000000));
	writel(value, gmacdev->RxPadinReg);

	value |= 0x74000000;
	writel(value, gmacdev->RxPadinReg);

	#else
	value = readl(gmacdev->RxPadinReg);
	value |= 0x72000000;
	writel(value, gmacdev->RxPadinReg);
	#endif


	/* Clock inverse */
	#ifdef CONFIG_NVT_INTERNAL_PHY
	nvt_internal_PHY_inverse_clock(gmacdev);
	#else
	// RMII - MAC to Phy.
	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	value = readl(gmacdev->ClockCtrlReg);
	value |= 0x00002000;

	writel(value, gmacdev->ClockCtrlReg);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	#endif

#else	// Not define USE_DEVICE_TREE.

	#ifdef CONFIG_MACH_NT14M
	ioremap_OR_value(MAC_STBC_RMII_GPIO_SEL_ADDR, 0x00000003);
	ioremap_AND_value(MAC_STBC_RMII_GPIO_CTRL_ADDR_1, (u32)(~(0x0480D000)));
	ioremap_AND_value(MAC_STBC_RMII_GPIO_CTRL_ADDR_2, (u32)(~(0x00003C00)));
	#endif

	// Set GPIO Pin Mux.
	#ifdef MAC_RMII_CLK_PHY_TO_MAC
	ioremap_AND_value(MAC_CLOCK_GPIO_ADDR, (u32)(~(0x02000000)));
	ioremap_OR_value(MAC_CLOCK_GPIO_ADDR, 0x74000000);
	#else
	ioremap_OR_value(MAC_CLOCK_GPIO_ADDR, 0x72000000);
	#endif

	/* Clock inverse */
	#ifdef CONFIG_NVT_INTERNAL_PHY
	nvt_internal_PHY_inverse_clock(gmacdev);
	#else
	// RMII - MAC to Phy.
	clock_ctrl = ioremap(MAC_CLOCK_INVERSE_ADDR, 0x4);
	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	*clock_ctrl |= 0x00002000;
	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	iounmap(clock_ctrl);

	#endif

#endif		// USE_DEVICE_TREE.


	/* Release AXI and AHB RESET of MAC */
	ioremap_AND_value(MAC_AXI_CLK_RESET, (u32)(~(0x20000000)));
	mdelay(10);

	/* Set spread spectrum */
	ioremap_write(0xFD0200BC, 0x00000001);
	ioremap_write(0xFD6702A4, 0x0000004A);
	ioremap_write(0xFD6702B4, 0x00000096);
	ioremap_write(0xFD6702B8, 0x00000009);

	*internal_PHY_reset  |= 0x2;	// Global Reset Digital Phy.

	ioremap_OR_value(0xFD130008, 0x00000004);

	//+++ Setting of Analog Phy.
	ioremap_OR_value(0xFD13009C, 0x00000001);	// Power down - ADC.
	msleep(10);

	ioremap_OR_value(0xFD1302E0, 0x00000010);	// Power down - DAC.
	ioremap_OR_value(0xFD1302C0, 0x00000003);	// Power down - LDRV (Line driver) 1/0.
	msleep(10);

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,  gmacdev->PhyBase, PHY_CONTROL_REG, &data);
	data |= Mii_reset;
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, data);

	/* Power on RX ATT Power */
	ioremap_OR_value(0xFD1300CC, 0x00000001);
	ioremap_AND_value(0xFD13008C, (u32)(~(0x00000001)));

	/* Power on 100M Base RX Power (PGA) */
	ioremap_OR_value(0xFD1300D8, 0x00000001);
	ioremap_AND_value(0xFD130098, (u32)(~(0x00000001)));

	/* Power on 10M Base RX Power */
	ioremap_OR_value(0xFD1300D0, 0x00000001);
	ioremap_AND_value(0xFD130090, (u32)(~(0x00000001)));

	/* Power on ADC Power */
	ioremap_AND_value(0xFD13009C, (u32)(~(0x00000001)));
	msleep(10);

	/* Power on DAC and LDRV (Line driver) 1/0. */
	ioremap_AND_value(0xFD1302E0, (u32)(~(0x00000010)));
	ioremap_AND_value(0xFD1302C0, (u32)(~(0x00000003)));
	//--- Setting of Analog Phy.

	*internal_PHY_reset  = 0;		// Global Reset Digital Phy (EQ reset included).

	if (Init_boot)
	{
		NVT_Phy_Init_Done = 0;

		// call by Init.
		init_timer(&NVT_Phy_InitTimer);
		NVT_Phy_InitTimer.function = (void *)nvt_Phy_InitTimer_function;
		NVT_Phy_InitTimer.expires = jiffies + (HZ + HZ/5);
		add_timer(&NVT_Phy_InitTimer);
	}
	else
	{
		// call by resume (multi-thread environment).
		msleep(1200);
	}

	synopGMAC_set_nvt_phy_mii(gmacdev);
	synopGMAC_set_nvt_phy_link_trigger_level(gmacdev);

	printk(KERN_CRIT "[NTKETHMAC]: NVT Internal Phy is Init.\n");
}

