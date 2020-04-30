/**\file
 *  To distinguish different manufacture
 * \internal
 * ----------------------------REVISION HISTORY-----------------------------
 * NVT			15/Apr/2007			Created
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpe.h>
#include <mach/clk.h>

#include "synopGMAC_Host.h"
#include "synopGMAC_plat.h"
#include "synopGMAC_network_interface.h"
#include "synopGMAC_Dev.h"
#include "m_internal_Phy.h"
#include "nvt_phy.h"
#include <linux/cpe.h>

DECLARE_WORK(priv_work_trim, nvt_wq_read_trim);
struct workqueue_struct *Read_trim_Wq;

struct timer_list NVT_Phy_InitTimer;
u32 NVT_Phy_Init_Done = 1;

u32 ioremap_read(u32 address)
{
	u32 *remap_address;
	u32 value;

	remap_address = ioremap(address, 0x4);

	if (!remap_address)
		return -1;

	value = readl(remap_address);
	iounmap(remap_address);

	return value;
}

void synopGMAC_eq_reset_function(u32 notused)
{
	u32 value;

	value = ioremap_read(0xFD130018);
	value &= (u32)(~(0x00000002));
	ioremap_write(0xFD130018, value);
}

void synopGMAC_eq_set_function(void)
{
	u32 value;

	value = ioremap_read(0xFD130018);
	value |= 0x00000002;
	ioremap_write(0xFD130018, value);
}

void  synopGMAC_set_nvt_phy_mii(struct synop_device *gmacdev)
{
	u16 data;

	data = 0;
	synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase,
		gmacdev->PhyBase,
		0x1a,
		data);
}

s32  synopGMAC_nvt_phy_disable_isolate(struct synop_device *gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, &data);

	data = data & (~(u16)0x0040);
	status = synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

s32  synopGMAC_nvt_phy_power_down(struct synop_device *gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, &data);

	data |= 0x0800;
	status = synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

s32  synopGMAC_nvt_phy_power_on(struct synop_device *gmacdev)
{
	u16 data;
	s32 status = -ESYNOPGMACNOERR;

	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, &data);

	data = data & (~(u16)0x0800);
	status = synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_CONTROL_REG, data);

	if (status)
		return status;
	else
		return -ESYNOPGMACNOERR;
}

void  synopGMAC_nvt_phy_reset_RX_statemach(struct synop_device *gmacdev)
{
	u16 data;

	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase, 0x10, &data);
	data |= 0x0008;
	synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase, 0x10, data);
	data = data & (~(u16)0x0008);
	synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase, 0x10, data);
}

u16  synopGMAC_get_nvt_phy_link_trigger_level(struct synop_device *gmacdev)
{
	u16 data;

	data = 0;
	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_EXT_ADDR_CBL_DIAG, &data);
	return data;
}

void  synopGMAC_set_nvt_phy_link_trigger_level(struct synop_device *gmacdev)
{
	u16 data = 0;

	synopGMAC_read_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_EXT_ADDR_CBL_DIAG, &data);

	data |= 0x0080;
	synopGMAC_write_phy_reg(
		(u32 *)gmacdev->MacBase, gmacdev->PhyBase,
		PHY_EXT_ADDR_CBL_DIAG, data);
}

void  nvt_internal_PHY_inverse_clock(struct synop_device *gmacdev)
{
	unsigned long flags;

#ifdef USE_DEVICE_TREE
	u32 value;
	/* Set MII clock enable */
	ioremap_AND_value(0xFD1300B0, (u32)(~(0x00000040)));

	/* MII TX(22) RX(23). */
	raw_spin_lock_irqsave(&clock_gen_lock, flags);

	value = readl(gmacdev->clock_ctrl_reg);
	value |= 0x00C00000;
	writel(value, gmacdev->clock_ctrl_reg);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

#else
	u32 *clock_ctrl;
	/* Set MII clock enable. */
	ioremap_AND_value(0xFD1300B0, (u32)(~(0x00000040)));

	/* MII TX(22) RX(23). */
	clock_ctrl = ioremap(MAC_CLOCK_INVERSE_ADDR, 0x4);

	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	*clock_ctrl |= 0x00C00000;
	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);

	iounmap(clock_ctrl);
#endif
}

void nvt_internal_PHY_iounmap_all(void)
{
	/* original code */

}

int nvt_internal_PHY_ioremap_all(void)
{

	return -ESYNOPGMACNOERR;
}

int nvt_nternal_PHY_display_status(struct synop_device *gmacdev)
{
	s32 status = ESYNOPGMACNOERR;
	u16 data;

	status = synopGMAC_read_phy_reg(
				(u32 *)gmacdev->MacBase,
				gmacdev->PhyBase,
				PHY_CONTROL_REG,
				&data);
	if (status)
		return status;

	pr_warn("[NTKETHMAC] AN Result = 0x%04x\n", data);

	if (data & Mii_Speed_100) {
		gmacdev->Speed = SPEED100;
		pr_warn("[NTKETHMAC] PHY 100M\n");
	} else {
		gmacdev->Speed = SPEED10;
		pr_crit("\033[1;31m[NTKETHMAC_ERROR] PHY 10M\033[0m\n");
	}

	if (data & Mii_Duplex) {
		gmacdev->DuplexMode = FULLDUPLEX;
		pr_warn("[NTKETHMAC] PHY FULLDUPLEX\n");
	} else {
		gmacdev->DuplexMode = HALFDUPLEX;
		pr_crit(
			"\033[1;31m[NTKETHMAC_ERROR] PHY HALFDUPLEX\033[0m\n");
	}

	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,
					gmacdev->PhyBase,
				0x4,
				&data);

	pr_warn("[NTKETHMAC] PHY reg 0x4 = 0x%04x\n", data);
	status = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,
					gmacdev->PhyBase,
				0x5,
				&data);

	pr_warn("[NTKETHMAC] PHY reg 0x5 = 0x%04x\n", data);
	status = synopGMAC_read_phy_reg(
				(u32 *)gmacdev->MacBase,
				gmacdev->PhyBase,
				0x6,
				&data);

	pr_warn("[NTKETHMAC] PHY reg 0x6 = 0x%04x\n", data);

	pr_warn("[NTKETHMAC] Set PHY into rising edge output\n");

	return status;
}

#ifdef CONFIG_NVT_PHY_NVT72673
enum trim_type {
	TRIMDATA_ETH_RTER_RX,
	TRIMDATA_ETH_RTER_TX,
	TRIMDATA_ETH_TX_ROUT,
	TRIMDATA_ETH_TX_SWING
};

static int read_trimdata(enum trim_type type)
{
	int trim;
	int trim_valid_eth_rter = 0;
	int trim_valid_eth_tx = 0;

	/*The following API will retrun 'valid' trim data*/
	trim_valid_eth_rter = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_ETH_CURRENT);
	trim_valid_eth_tx = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_ETH_RESISTANCE);

	switch (type) {
		case TRIMDATA_ETH_RTER_RX:
			if (trim_valid_eth_rter < 0)
				trim = -1;
			else
				/* bit[2:0], 3bits */
				trim = (trim_valid_eth_rter & 0x7);
			break;
		case TRIMDATA_ETH_RTER_TX:
			if (trim_valid_eth_rter < 0)
				trim = -1;
			else
				/* bit[5:3], 3bits */
				trim = ((trim_valid_eth_rter >> 3) & 0x7);
			break;
		case TRIMDATA_ETH_TX_ROUT:
			if (trim_valid_eth_tx < 0)
				trim = -1;
			else
				/* bit[2:0], 3bits */
				trim = (trim_valid_eth_tx & 0x7);
			break;
		case TRIMDATA_ETH_TX_SWING:
			if (trim_valid_eth_tx < 0)
				trim = -1;
			else
				/* bit[7:3], 5bits */
				trim = ((trim_valid_eth_tx >> 3) & 0x1f);
			break;
		default:
			return -1;
	}

	return trim;
}

void nvt_trim(void)
{
	int trim;

	trim = read_trimdata(TRIMDATA_ETH_RTER_RX);
	if (trim >= 0) {
		/* [7:5]: RX_TRIM_SEL_RX<2:0> */
		ioremap_AND_value(0xFD130368, ~((u32)0x7  << 5));
		ioremap_OR_value( 0xFD130368,  ((u32)trim << 5));
		pr_crit("[NTKETHMAC]: Trim RTER_RX = 0x%X, 0368[%x]\n", trim, ioremap_read(0xFD130368));
	}
	else {
		pr_crit("[NTKETHMAC]: No Trim RTER_RX\n");
	}

	trim = read_trimdata(TRIMDATA_ETH_RTER_TX);
	if (trim >= 0) {
		/* [4:2]: RX_TRIM_SEL_TX<2:0> */
		ioremap_AND_value(0xFD130368, ~((u32)0x7  << 2));
		ioremap_OR_value( 0xFD130368,  ((u32)trim << 2));
		pr_crit("[NTKETHMAC]: Trim RTER_TX = 0x%X, 0368[%x]\n", trim, ioremap_read(0xFD130368));
	}
	else {
		pr_crit("[NTKETHMAC]: No Trim RTER_TX\n");
	}

	trim = read_trimdata(TRIMDATA_ETH_TX_ROUT);
	if (trim >= 0) {
		/* [5:3]: TX_TRIM_ROUT<2:0> */
		ioremap_AND_value(0xFD130374, ~((u32)0x7  << 3));
		ioremap_OR_value( 0xFD130374,  ((u32)trim << 3));
		pr_crit("[NTKETHMAC]: Trim TX_ROUT = 0x%X, 0374[%x]\n", trim, ioremap_read(0xFD130374));
	}
	else {
		pr_crit("[NTKETHMAC]: No Trim TX_ROUT\n");
	}

	trim = read_trimdata(TRIMDATA_ETH_TX_SWING);
	if (trim >= 0) {
		/* [4:0]: TX_TRIM_DAC<4:0> */
		ioremap_AND_value(0xFD130378, ~((u32)0x1f << 0));
		ioremap_OR_value( 0xFD130378,  ((u32)trim << 0));
		pr_crit("[NTKETHMAC]: Trim TX_SWING = 0x%X, 0378[%x]\n", trim, ioremap_read(0xFD130378));
	}
	else {
		pr_crit("[NTKETHMAC]: No Trim TX_SWING\n");
	}
}

#endif

void nvt_wq_read_trim(struct work_struct *ignored)
{
#ifdef CONFIG_NVT_PHY_NVT72673
	nvt_trim();
#else
	s32 trim;
	u32 recal;

	trim = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_ETH_RESISTANCE);

	if (trim > 0) {
		ioremap_write(0xFD1302C0, 0x4);

		ioremap_write(0xFD1302CC, (u32)trim);
		ioremap_write(0xFD1302D0, (u32)(trim >> 1));

		pr_warn("[NTKETHMAC]: Trim resistance - 02CC[%x] - 02D0[%x]\n",
			ioremap_read(0xFD1302CC),
			ioremap_read(0xFD1302D0));
	} else {
		ioremap_write(0xFD1302C0, 0x0);

		/* Re-Cal process. */
		ioremap_write(0xFD1302E4, 0x1);

		for (recal = 0; recal < 10; recal++) {
			udelay(3);

			if (ioremap_read(0xFD1302E4) != 0x0) {
				/* Re-Calibrate fail. */
				ioremap_write(0xFD1302E4, 0x0);
				ioremap_write(0xFD1302E4, 0x1);
				pr_warn("[NTKETHMAC]: Re-Cal = %d\n", recal);
			} else {
				break;
			}
		}

		usleep_range(1 * 1000, 2 * 1000);
		pr_warn("[NTKETHMAC]: Re-Cal 02CC[%x] - 02D0[%x]\n",
			ioremap_read(0xFD1302CC),
			ioremap_read(0xFD1302D0));
	}

	ioremap_write(0xFD130080, 0x68);	/*increase current (global)*/
	ioremap_write(0xFD1302D4, 0x27);	/*TX swing*/
	ioremap_write(0xFD1302C4, 0x88);	/*TX line driver current*/
	ioremap_write(0xFD1302C8, 0x88);	/*RX line driver current*/

	/* Idle-pulse: [7]level enable and [5:0]DAC_LEVEL. */
	ioremap_write(0xFD1302FC, 0xB8);

	/* Trimming Tx swing. */
	trim = ntcpe_read_trimdata(EN_CPE_TRIMDATA_TYPE_ETH_CURRENT);
	if (trim > 0) {
		ioremap_write(0xFD1302D4, (u32)trim);
		pr_warn("[NTKETHMAC]: Trim current = 0x%X\n",
			ioremap_read(0xFD1302D4));
	}
#endif

	NVT_Phy_Init_Done = 1;
	pr_warn("[NTKETHMAC]: Reset Digital Phy is ok.\n");
}

static void nvt_Phy_InitTimer_function(u32 notused)
{
	queue_work(Read_trim_Wq, &priv_work_trim);
}

void SysPlat_Init_Ethernet_Phy(struct synop_device *gmacdev, int Init_boot)
{
#ifdef USE_DEVICE_TREE
	u32 value;
#else
	u32 *clock_ctrl;
#endif

	/*Set STBC Key.*/
	ioremap_write(MAC_STBC_KEY_ADDR,	 0x72682);
	ioremap_write(MAC_STBC_KEY_ADDR,	 0x28627);
	ioremap_write(MAC_STBC_KEY_SET_ADDR, 0x1);

	/*Switch MAC controlling to ARM.*/
	ioremap_OR_value(MAC_STBC_CTRL_SWITCH_ADDR, 0x00000100);

	/*Switch MAC clock from AHB.*/
	ioremap_OR_value(MAC_CLK_SRC_SWITCH_ADDR, 0x0000000C);

#ifdef USE_DEVICE_TREE

	#ifndef CONFIG_NVT_INTERNAL_PHY
	/*Select RMII mode.*/
	value = readl(gmacdev->RmiiModeCtrlReg);
	value |= 0x80000000;
	writel(value, gmacdev->RmiiModeCtrlReg);
	#endif

	/*Set GPIO Pin Mux.*/
	#ifdef MAC_RMII_CLK_PHY_TO_MAC
	value = readl(gmacdev->rx_padin_reg);
	value &= (u32)(~(0x02000000));
	writel(value, gmacdev->rx_padin_reg);

	value |= 0x74000000;
	writel(value, gmacdev->rx_padin_reg);

	#else
	value = readl(gmacdev->rx_padin_reg);
	value |= 0x62000000;
	writel(value, gmacdev->rx_padin_reg);
	#endif

	/* Clock inverse */
	#ifdef CONFIG_NVT_INTERNAL_PHY
	nvt_internal_PHY_inverse_clock(gmacdev);
	#else
	/*RMII - MAC to Phy.*/
	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	value = readl(gmacdev->clock_ctrl_reg);
	value |= 0x00002000;

	writel(value, gmacdev->clock_ctrl_reg);

	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	#endif

#else	/*Not define USE_DEVICE_TREE.*/

	/*Set GPIO Pin Mux.*/
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
	/*RMII - MAC to Phy.*/
	clock_ctrl = ioremap(MAC_CLOCK_INVERSE_ADDR, 0x4);
	raw_spin_lock_irqsave(&clock_gen_lock, flags);
	*clock_ctrl |= 0x00002000;
	raw_spin_unlock_irqrestore(&clock_gen_lock, flags);
	iounmap(clock_ctrl);

	#endif

#endif		/*USE_DEVICE_TREE.*/

	/* Release AXI and AHB RESET of MAC */
	ioremap_AND_value(MAC_AXI_CLK_RESET, (u32)(~(0x20000000)));
	usleep_range(1 * 1000, 2 * 1000);

	/* Set spread spectrum */
	ioremap_write(0xFD0200BC, 0x00000001);
	ioremap_write(0xFD6702A4, 0x0000004A);
	ioremap_write(0xFD6702B4, 0x00000096);
	ioremap_write(0xFD6702B8, 0x00000009);

	nvt_phy_reset(gmacdev);

	if (Init_boot) {
		NVT_Phy_Init_Done = 0;

		/*call by Init.*/
		init_timer(&NVT_Phy_InitTimer);
		NVT_Phy_InitTimer.function = (void *)nvt_Phy_InitTimer_function;
		NVT_Phy_InitTimer.expires = jiffies + (HZ * 2);
		add_timer(&NVT_Phy_InitTimer);
	} else {
		/*call by resume (multi-thread environment).*/
		msleep(1200);
	}

	pr_warn("[NTKETHMAC]: NVT Internal Phy is Init.\n");
}

