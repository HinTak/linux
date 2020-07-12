#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <soc/sdp/soc.h>
#include "common.h"

#define ADDR_SERDES_PATH_MUX 0x005515B4
#if defined(CONFIG_ARCH_SDP1803)
#define MASK_SERDES_PATH_MUX 0x00010000
#define VALUE_SERDES_PATH_MUX 0x00010000
#elif defined(CONFIG_ARCH_SDP_NIKE)
#define MASK_SERDES_PATH_MUX 0x000F0000
#define VALUE_SERDES_PATH_MUX 0x000F0000
#endif

#define BASE_ADDR_SDS_CORE	0x00BE0000
#define BASE_ADDR_SDS_GPR	0x00BE4000
#define BASE_ADDR_SDS_PHY	0x00BF0000
#define BASE_ADDR_CLK_RST	0x00F50000

#define BASE_ADDR_HDMI_HMON 0x005C0000

#define BASE_ADDR_USB_EHCI_PORTA 0x00480000
#define BASE_ADDR_USB_EHCI_PORTB 0x00490000
#define BASE_ASDR_USB_LINK_CTRL 0x004D0000
#define BASE_ADDR_USB_PHY_CTRL 0x009C0900

#define BASE_ADDR_TSD_HW 0x00910000

static int m_readl(unsigned int base_address, unsigned int * value)
{
	volatile void __iomem * v_addr = NULL;

	v_addr = (void __iomem *)ioremap(base_address, 0x1000);
	if (v_addr == NULL)
	{
		printk("%s, ioremap() failed at m_readl Func !\n", __FUNCTION__);
		return -1;
	}

	*value = readl((void __iomem *)v_addr);

	iounmap(v_addr);

	return 0;
}

static int m_writel(unsigned int base_address, unsigned int value)
{
	volatile void __iomem * v_addr = NULL;

	v_addr = (void __iomem *)ioremap(base_address, 0x1000);
	if (v_addr == NULL){
		printk("%s, ioremap() failed at m_writel Func !\n", __FUNCTION__);
		return -1;
	}

	writel(value, (void __iomem *)v_addr);

	iounmap(v_addr);

	return 0;
}

static int m_writel_mask(unsigned int base_address, unsigned int uw_value, unsigned int uw_mask)
{
	unsigned int value;
	
	if(m_readl(base_address, &value))
		return -1;

	uw_value = (value & ~uw_mask) | (uw_value & uw_mask);

	if(m_writel(base_address, uw_value))
		return -1;
		
	return 0;
}

static int sdp_serdes_lock_check(void)
{
	unsigned int u32Val;
	int ret;

	ret = m_readl(BASE_ADDR_SDS_CORE + 0x0370, &u32Val);
	if(ret < 0)
		return ret;

	if ((u32Val & 0x100E) == 0x100E)
		return 1;
	else
		return 0;
}

// 0. Before SERDES INIT Sequence
static void sdp_serdes_init_before(void)
{
	// Reset assert

	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00000000, 0x11111111);
	//[28] : core reset
	//[24] : core_tx reset  
	//[20] : core rx reset
	//[16] : usb reset
	//[12] : usb1 reset
	//[08] : i2c reset
	//[04] : ethernet reset
	//[00] : ethernet_tv reset
	
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x00000000, 0x11111111);
	//[28] : lb_tv reset
	//[24] : ch reset
	//[20] : fmt reset
	//[16] : adc reset
	//[12] : tsd reset
	//[08] : vtg reset
	//[04] : apb reset
	//[00] : pht_init reset
	
	msleep(1);

	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x10000000, 0x10000000);
	//[28] : core reset

	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x00000010, 0x00000010);
	//[04] : apb reset

	msleep(1);

	m_writel(BASE_ADDR_SDS_GPR + 0x000C, 0x00000001); // Slave I/F Select
	//[0] : 0 = I2C(MICOM) / 1 = APB(CPU)
}

// 1. SERDES PLL SETTING
// 2. SERDES PLL LOCKED
static void sdp_serdes_init_pll(void)
{
	unsigned int value, cnt = 0;

	// 1. SERDES PLL SETTING
	m_writel(BASE_ADDR_SDS_GPR + 0x0040, 0x000C3003);	// SERDES_PLL PMS 196Mhz
//	m_writel(BASE_ADDR_SDS_GPR + 0x0040, 0x000C1803);	// SERDES_PLL PMS 98Mhz

	m_writel(BASE_ADDR_SDS_GPR + 0x0044, 0x01001101);	// SERDES_PLL CTRL

	// 2. SERDES PLL LOCKED
	m_readl(BASE_ADDR_SDS_GPR + 0x0050, &value);

	while((value & 0x00000001) != 0x00000001) {	// Check SERDES_PLL Locked
		m_readl(BASE_ADDR_SDS_GPR + 0x0050, &value);
		msleep(1);
		if(cnt++ >= 200) {
			printk("[SERDES] PLL lock fail val = 0x%x\n", value);
			break;
		}
	}
}

// 3. SERDES PHY INIT
static void sdp_serdes_init_phy(void)
{
	m_writel(BASE_ADDR_SDS_GPR + 0x0400, 0x00000000);
	//[0] : power_off 
	//[4] : cmn_rstn 
	//[8] : ln0_rstn 

	m_writel(BASE_ADDR_SDS_GPR + 0x0404, 0x00010000); 
	//[0 ] :  aux_en  
	//[4 ] :  bgr_en  
	//[8 ] :  bias_en  
	//[12] :  high_speed 
	//[16] :  pll_en  
	//[20] :  ssc_en  

//	m_writel(BASE_ADDR_SDS_GPR + 0x0408, 0x00000301); REFCLK = XTAL
	m_writel(BASE_ADDR_SDS_GPR + 0x0408, 0x00000101);// REFCLK = PLL
	//[1:0] : ln0_rate	  
	//[5:4] : phy_mode	  
	//[9:8] : pll_ref_clk_sel

	m_writel(BASE_ADDR_SDS_GPR + 0x040C, 0x00001111); 
	//[0]  : ln0_rx_bias_en  
	//[4]  : ln0_rx_cdr_en	
	//[8]  : ln0_rx_ctle_en  
	//[12] : ln0_rx_des_en	
	//[16] : ln0_rx_dfe_adap_en 
	//[20] : ln0_rx_dfe_adap_hold
	//[24] : ln0_rx_fom_en	
	//[28] : ln0_rx_lfps_det_en 

	m_writel(BASE_ADDR_SDS_GPR + 0x0410, 0x00000011);
	//[0]  : ln0_rx_rterm_en  
	//[4]  : ln0_rx_sqhs_en  
	//[11:8]  : ln0_rx_ctle_rs1_ctrl
	//[15:12] : ln0_rx_ctle_rs2_ctrl

	m_writel(BASE_ADDR_SDS_GPR + 0x0414, 0x00100000);
	//[4:0] : ln0_tx_drv_post_lvl_ctrl	
	//[8]	: ln0_tx_drv_beacon_lfps_out_en 
	//[12]	: ln0_tx_drv_cm_keeper_en	
	//[16]	: ln0_tx_drv_ei_en	  
	//[20]	: ln0_tx_drv_en 	
	//[24]	: ln0_tx_p2_async	  
	//[28]	: ln0_tx_rxd_en 	

	m_writel(BASE_ADDR_SDS_GPR + 0x0418, 0x00000001);
	//[0] : ln0_tx_ser_en	
	//[7:4] : ln0_tx_drv_pre_lvl_ctrl 
	//[12:8] : ln0_tx_drv_lvl_ctrl	

//	m_writel(BASE_ADDR_SDS_GPR + 0x0054, 0x00000001);
	//[0] : 0: system_xtal. 1: serdes_xtal

	m_writel_mask(BASE_ADDR_SDS_GPR + 0x0410, 0x0000FF00, 0x0000FF00); //rx_ctle_rs0:0xd~0xf / rx_ctle_rs1:0x~0xf

	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x00000011, 0x00000011); 
	//[04] : apb reset
	//[00] : pht_init reset

	msleep(5);

	m_writel(BASE_ADDR_SDS_PHY + 0x0414, 0x00000028);	// [5]ovrd_tx_drv_post_lvl_ctrl [4:0]tx_drv_post_lvl_ctrl_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x0418, 0x0000000A);	// [4:0]tx_drv_post_lvl_ctrl_g3  // 180323 sweep
	m_writel(BASE_ADDR_SDS_PHY + 0x0424, 0x00000013);	// [4]ovrd_tx_drv_pre_lvl_ctrl [3:0]tx_drv_pre_lvl_ctrl_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x0428, 0x00000022);	// [7:4]tx_drv_pre_lvl_ctrl_g2 [3:0]tx_drv_pre_lvl_ctrl_g3
	m_writel(BASE_ADDR_SDS_PHY + 0x042C, 0x00000035);	// [6:3]tx_drv_pre_lvl_ctrl_g4 [2]ana_tx_drv_beacon_lfps_sync_en [1]ovrd_tx_drv_idrv_en [0]tx_drv_idrv_en
	m_writel(BASE_ADDR_SDS_PHY + 0x04EC, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs1_ctrl
	m_writel(BASE_ADDR_SDS_PHY + 0x04F8, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs2_ctrl

	//Analog Set 24M
	m_writel(BASE_ADDR_SDS_PHY + 0x0018, 0x00000028);	// ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x00B4, 0x00000050);	// pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00C4, 0x00000050);	// pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00D0, 0x00000011);	// ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x00D4, 0x00000001);	// ana_pll_pms_refdiv 

	m_writel(BASE_ADDR_SDS_PHY + 0x00D8, 0x00000011);	// pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x018C, 0x00000003);	// ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x0480, 0x00000033);	// rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x0488, 0x00000011);	// rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0490, 0x00000033);	// rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0498, 0x00000000);	// rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0620, 0x00000000);	// rx_cdr_pms_m_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x0624, 0x000000A0);	// rx_cdr_pms_m_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x0628, 0x00000000);	// rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x062C, 0x000000A0);	// rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x0404, 0x00000020);	// ADD same to ATE

	m_writel(BASE_ADDR_SDS_PHY + 0x0408, 0x0000001F);	//(Tx_Lvl setting) okh 

#if 0
	//Analog Set 98M
	m_writel(BASE_ADDR_SDS_PHY + 0x0018, 0x00000028);	// ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x00B4, 0x00000014);	// pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00C4, 0x00000014);	// pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00D0, 0x00000011);	// ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x00D4, 0x00000004);	// ana_pll_pms_refdiv
	m_writel(BASE_ADDR_SDS_PHY + 0x00D8, 0x00000011);	// pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x018C, 0x00000003);	// ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x0480, 0x00000033);	// rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x0488, 0x00000011);	// rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0490, 0x00000033);	// rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0498, 0x00000000);	// rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0628, 0x00000000);	// rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x062C, 0x000000A0);	// rx_cdr_pms_m_g2 

	// Analog Set 196M
	m_writel(BASE_ADDR_SDS_PHY + 0x0018, 0x00000028);	// ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x00B4, 0x00000014);	// pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00C4, 0x00000014);	// pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x00D0, 0x00000012);	// ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x00D4, 0x00000008);	// ana_pll_pms_refdiv
	m_writel(BASE_ADDR_SDS_PHY + 0x00D8, 0x00000011);	// pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x018C, 0x00000003);	// ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x0480, 0x00000033);	// rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x0488, 0x00000011);	// rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0490, 0x00000033);	// rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0498, 0x00000000);	// rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0628, 0x00000000);	// rx_cdr_pms_m_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x062C, 0x000000A0);	// rx_cdr_pms_m_g2
#endif

	m_writel(BASE_ADDR_SDS_PHY + 0x00E4, 0x00000002);	//ana_pll_ref_clk_sel 
	//[3]	  : 1'h0  / ovrd_pll_ref_clk_sel			 // set 1
	//[2:1]   : 2'h1  / pll_ref_clk_sel 				 // set 1
	//[0]	  : 1'h0  / ana_pll_ref_dig_clk_sel

	m_writel(BASE_ADDR_SDS_PHY + 0x0190, 0x0000004C);	// 'h4C
	//[7]	  : 1'h0  / ana_aux_rx_tx_sel
	//[6]	  : 1'h0  / ovrd_aux_en 					 // set 1
	//[5]	  : 1'h0  / aux_en							 // set 0
	//[4]	  : 1'h0  / ana_aux_rx_cap_bypass
	//[3]	  : 1'h1  / ana_aux_rx_term_gnd_en
	//[2:0]   : 3'h4  / ana_aux_tx_term

	m_writel(BASE_ADDR_SDS_PHY + 0x0448, 0x00000051);	// trsv_reg012 'h51
	//[6]	  : 1'h0  / ln0_ovrd_pi_rate				 // set 1
	//[5:4]   : 2'h0  / ln0_pi_rate 					 // set 2
	//[0]	  : 1'h1  / ln0_ana_tx_jeq_en

	m_writel(BASE_ADDR_SDS_PHY + 0x021C, 0x0000002C);	// cmn_reg083 'h2C
	//[6]	  : 1'h0  / pcs_bias_sel
	//[5]	  : 1'h0  / ovrd_pi_ssc_en
	//[4]	  : 1'h0  / pi_ssc_en
	//[3]	  : 1'h0  / ovrd_pcs_pll_en 				 // set 1
	//[2]	  : 1'h0  / pcs_pll_en						 // set 1
	//[1]	  : 1'h0  / ovrd_pcs_bias_en
	//[0]	  : 1'h0  / pcs_bias_en

	m_writel(BASE_ADDR_SDS_PHY + 0x04AC, 0x000000F0);	// trsv_reg02B 'hF0
	//[7]	  : 1'h0  / ln0_ovrd_pcs_rx_bias_en 		 // set 1
	//[6]	  : 1'h0  / ln0_pcs_rx_bias_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_cdr_en			 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_cdr_en				 // set 1
	//[3:0]   : 4'h0  / ln0_ana_rx_cdr_vco_bbcap_dn_ctrl

	m_writel(BASE_ADDR_SDS_PHY + 0x04B0, 0x000000F1);	// trsv_reg02C 'hF1
	//[7]	  : 1'h0  / ln0_ovrd_pcs_rx_des_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_rx_des_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_sqhs_en 		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_rx_sqhs_en			 // set 1
	//[3]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g1
	//[2]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g2
	//[1]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g3
	//[0]	  : 1'h1  / ln0_rx_cdr_vco_freq_boost_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x00BC, 0x00000050);	//pll_pms_mdiv_afc_g4 // 
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_drv_en				 // set 0
	//[5]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_ei_en		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_tx_drv_ei_en			 // set 0
	//[3]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g1
	//[2]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g2
	//[1]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g3
	//[0]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x0468, 0x0000003F);	// trsv_reg01A 'h3F
	//[5]	  : 1'h1  / ln0_tx_ser_40bit_en_g1
	//[4]	  : 1'h1  / ln0_tx_ser_40bit_en_g2
	//[3]	  : 1'h1  / ln0_tx_ser_40bit_en_g3
	//[2]	  : 1'h1  / ln0_tx_ser_40bit_en_g4
	//[1]	  : 1'h0  / ln0_ovrd_tx_ser_data_rstn		 // set 1
	//[0]	  : 1'h0  / ln0_tx_ser_data_rstn			 // set 1

	m_writel(BASE_ADDR_SDS_PHY + 0x04D4, 0x000000B0);	// trsv_reg035 'hB0 //DC Choi
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_p2_async		 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_p2_async
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_ctle_en 		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_ctle_en				 // set 1
	//[1]	  : 1'h0  / ln0_ovrd_rx_ctle_oc_en
	//[0]	  : 1'h0  / ln0_rx_ctle_oc_en

	m_writel(BASE_ADDR_SDS_PHY + 0x059C, 0x00000003);	// trsv_reg067 'h03
	//[5]	  : 1'h0  / ln0_ovrd_rx_rcal_bias_en
	//[4]	  : 1'h0  / ln0_rx_rcal_bias_en
	//[3:2]   : 2'h0  / ln0_ana_rx_rcal_irmres_ctrl
	//[1]	  : 1'h0  / ln0_ovrd_rx_rterm_en			 // set 1
	//[0]	  : 1'h0  / ln0_rx_rterm_en 				 // set 1

	m_writel(BASE_ADDR_SDS_PHY + 0x04BC, 0x000000EF);	// trsv_reg02F 'hEF
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_drv_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_ei_en		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_tx_drv_ei_en			 // set 0
	//[3]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g1
	//[2]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g2
	//[1]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g3
	//[0]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x0400, 0x00000028);	// trsv_reg000 'h28
	//[7]	  : 1'h0  / ln0_ovrd_tx_drv_en
	//[6]	  : 1'h0  / ln0_tx_drv_en
	//[5]	  : 1'h0  / ln0_ovrd_tx_drv_beacon_lfps_out_en	// set 1
	//[4]	  : 1'h0  / ln0_tx_drv_beacon_lfps_out_en
	//[3]	  : 1'h0  / ln0_ovrd_tx_drv_cm_keeper_en		// set 1
	//[2]	  : 1'h0  / ln0_tx_drv_cm_keeper_en
	//[1]	  : 1'h0  / ln0_ovrd_tx_drv_ei_en
	//[0]	  : 1'h0  / ln0_tx_drv_ei_en

	m_writel(BASE_ADDR_SDS_PHY + 0x0460, 0x00000032);	// trsv_reg018 'h32
	//[7]	  : 1'h0  / ln0_tx_rterm_42p5_en_g1
	//[6]	  : 1'h0  / ln0_tx_rterm_42p5_en_g2
	//[5]	  : 1'h1  / ln0_tx_rterm_42p5_en_g3
	//[4]	  : 1'h1  / ln0_tx_rterm_42p5_en_g4
	//[3]	  : 1'h0  / ln0_ovrd_tx_rxd_comp_en
	//[2]	  : 1'h0  / ln0_tx_rxd_comp_en
	//[1]	  : 1'h0  / ln0_ovrd_tx_rxd_en					// set 1
	//[0]	  : 1'h0  / ln0_tx_rxd_en

	m_writel(BASE_ADDR_SDS_PHY + 0x05D4, 0x00000004);	// trsv_reg075 'h04 // DC Choi
	//[6:3]   : 4'h0  / ln0_ana_rx_pwm_oc_code
	//[2]	  : 1'h0  / ln0_ovrd_rx_lfps_det_en 			// set 1
	//[1]	  : 1'h0  / ln0_rx_lfps_det_en
	//[0]	  : 1'h0  / ln0_ana_rx_lfps_loss_det_en

	m_writel(BASE_ADDR_SDS_PHY + 0x054C, 0x000000D5);	// trsv_reg053 'hD4
	//[7]	  : 1'h1  / ln0_rx_dfe_adap_short_done
	//[6]	  : 1'h0  / ln0_ovrd_rx_dfe_adap_en 			// set 1
	//[5]	  : 1'h1  / ln0_rx_dfe_adap_en
	//[4]	  : 1'h0  / ln0_ovrd_rx_dfe_eom_en
	//[3]	  : 1'h0  / ln0_rx_dfe_eom_en
	//[2:0]   : 3'h4  / ln0_ana_rx_dfe_eom_pi_div_sel

	m_writel(BASE_ADDR_SDS_PHY + 0x04B8, 0x0000006C);
	m_writel(BASE_ADDR_SDS_PHY + 0x04BC, 0x0000000F);
	m_writel(BASE_ADDR_SDS_PHY + 0x04C0, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x04C4, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x04C8, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x04CC, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x04D8, 0x00000003);
	m_writel(BASE_ADDR_SDS_PHY + 0x04DC, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x04E0, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x04E4, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x04E8, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x04EC, 0x0000001F);
	m_writel(BASE_ADDR_SDS_PHY + 0x04F0, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x04F8, 0x00000018);
	m_writel(BASE_ADDR_SDS_PHY + 0x0500, 0x00000008);
	m_writel(BASE_ADDR_SDS_PHY + 0x0504, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0508, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x050C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0510, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x0518, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x051C, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x0520, 0x0000000A);
	m_writel(BASE_ADDR_SDS_PHY + 0x0524, 0x00000077);
	m_writel(BASE_ADDR_SDS_PHY + 0x052C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0530, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0534, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0538, 0x00000000);

	m_writel(BASE_ADDR_SDS_PHY + 0x04EC, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs1_ctrl
	m_writel(BASE_ADDR_SDS_PHY + 0x04F8, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs2_ctrl

	m_writel(BASE_ADDR_SDS_PHY + 0x04F0, 0x00);	// rs1[7:4] EQ1
	m_writel(BASE_ADDR_SDS_PHY + 0x04FC, 0x00);	// rs2[7:4] EQ2

	m_writel(BASE_ADDR_SDS_PHY + 0x0418, 0x0A);	// [4:0]tx_drv_post_lvl_ctrl_g2  // 180323 sweep		

	m_writel(BASE_ADDR_SDS_PHY + 0x0428, 0x0 << 4);	// [7:4]tx_drv_pre_lvl_ctrl_g2 [3:0]tx_drv_pre_lvl_ctrl_g3

	m_writel(BASE_ADDR_SDS_PHY + 0x04C4, 0x7F);		// 181030 CTLE Itail increase
	m_writel(BASE_ADDR_SDS_PHY + 0x04E0, 0x0F);		// 181030 R load decrease caused by CTLE Current increase
	m_writel(BASE_ADDR_SDS_PHY + 0x0510, 0x57);		// 181030 CTLE 2nd Stage Off
	m_writel(BASE_ADDR_SDS_PHY + 0x05A4, 0x0C);		// 181030 Invcm modify
	m_writel(BASE_ADDR_SDS_PHY + 0x0508, 0x01);		// 181030  CS Ctrl 0=>1

	// 180917 added
	m_writel(BASE_ADDR_SDS_PHY + 0x0740, 0x00000060);	// sigvql overide

	// CTLE Tune 180918 added
	m_writel(BASE_ADDR_SDS_PHY + 0x0518, 0x000000EF);	// Negc_En	EF, EE, EC, E8, E0
	m_writel(BASE_ADDR_SDS_PHY + 0x051C, 0x000000FF); 
	m_writel(BASE_ADDR_SDS_PHY + 0x0520, 0x0000000A);	// Negc_itail_Ctrl (Eval : 0x0A, Product: 0x0F)
	m_writel(BASE_ADDR_SDS_PHY + 0x0528, 0x00000000);	// vcm_sel

	msleep(1);	// wait 1ms

	m_writel(BASE_ADDR_SDS_PHY + 0x05EC, 0x00000020);
	m_writel(BASE_ADDR_SDS_PHY + 0x05A8, 0x00000000);

	m_writel(BASE_ADDR_SDS_PHY + 0x059C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x05AC, 0x00000038);
	m_writel(BASE_ADDR_SDS_PHY + 0x0670, 0x000000FC);
	m_writel(BASE_ADDR_SDS_PHY + 0x0678, 0x000000F4);
	m_writel(BASE_ADDR_SDS_PHY + 0x07F8, 0x00000033);
	m_writel(BASE_ADDR_SDS_PHY + 0x04A0, 0x00000004);
	m_writel(BASE_ADDR_SDS_PHY + 0x0468, 0x0000003C);
	m_writel(BASE_ADDR_SDS_PHY + 0x0540, 0x00000000);

	m_writel(BASE_ADDR_SDS_GPR + 0x0400, 0x00000110);	// GPR__phy_ctrl_0

	m_writel(BASE_ADDR_SDS_PHY + 0x0000, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0704, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x0708, 0x000000ED);	// RX manual BIST//[7]bist_en[6]data_en[5]rx_en[4]rx_hold [3]rx_start[2]tx_en[1]tx_errinj[0]tx_start
	m_writel(BASE_ADDR_SDS_PHY + 0x0704, 0x00000000);	// [1:0] prbs7 (00: prbs7,   01: prbs11,   11: prbs23,   (10:X)

	msleep(1);	//wait 1ms
}

// 4. CHECK SERDES PHY INIT DONE
static void check_serdes_phy_lock(void)
{
	unsigned int r_tmp_data;

	//	[1]	  : 1'h0  / ana_pll_lock_done
	//	[0]	  : 1'h0  / ana_pll_afc_done
	m_readl(BASE_ADDR_SDS_PHY + 0x01D4, &r_tmp_data);	// cmn_reg075 'h00
	while((r_tmp_data & 0x03) != 0x03)
		m_readl(BASE_ADDR_SDS_PHY + 0x01D4, &r_tmp_data);	// Check PHY PLL Locked

	//	[3]	  : 1'h0  / ln0_mon_rx_cdr_lock_done
	m_readl(BASE_ADDR_SDS_PHY + 0x07E8, &r_tmp_data);	// cmn_reg075 'h00
	while((r_tmp_data & 0x08) != 0x08)
		m_readl(BASE_ADDR_SDS_PHY + 0x07E8, &r_tmp_data);	// Check PHY CDR Locked

	msleep(30);	// 30ms delay after SERDES CDR Lock checking
}

// 5. SERDES CORE INIT 
static void sdp_serdes_init_core(void)
{
	m_writel(BASE_ADDR_SDS_CORE + 0x019C, 0x00000120);	// RX Side Uniform Off
	m_writel(BASE_ADDR_SDS_CORE + 0x0070, 0x00005353);	// TV usb mode set to Peri attatched mode
	m_writel(BASE_ADDR_SDS_CORE + 0x0210, 0x00000003);	// SPI path to TX FLASH update
	m_writel(BASE_ADDR_SDS_CORE + 0x01FC, 0x00000001);	// gpio_pad direction setting. [10], [0] set to output
	m_writel(BASE_ADDR_SDS_CORE + 0x00B4, 0x000003BC);	// Backward TS1 setting
	m_writel(BASE_ADDR_SDS_CORE + 0x01DC, 0x0000015E);	// Sat ADC FIFO Level Adjust
	m_writel(BASE_ADDR_SDS_CORE + 0x0040, 0x0000FFFF);
	m_writel(BASE_ADDR_SDS_CORE + 0x0010, 0x00000208);

	m_writel_mask(BASE_ADDR_SDS_CORE + 0x0060, 0x0200, 0x0200); // [9] RW  i2c_stretch_en_1 // set 1

	m_writel(BASE_ADDR_SDS_PHY + 0x0704, 0x60); // BIST_AUTO_RUN Clear, comdet_num3. PRBS15;
	//[7]	   : 1'h0  / ln0_bist_auto_run							   
	//[6:5]   : 2'h0  / ln0_bist_comdet_num
	//[4:2]   : 3'h0  / ln0_bist_seed_sel
	//[1:0]   : 2'h0  / ln0_bist_prbs_mode
}

// 6. SERDES TX_RSTN RX_RSTN Release
static void sdp_serdes_init_rstn(void)
{
	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x01100000, 0x01100000);
	//[24] : core_tx reset	
	//[20] : core rx reset

	m_writel(BASE_ADDR_SDS_PHY + 0x0708, 0x00000000);
}

static void sdp_serdes_reset_UserLogic(void)
{
	/* RX reset assert */
	m_writel_mask(BASE_ADDR_SDS_PHY + 0x047C, 0x00000010, 0x00000018);	// cdr_en off : 0xBF047C[4:3] : 0x2 / [4]:1 [3]:0
	m_writel_mask(BASE_ADDR_SDS_PHY + 0x0548, 0x00000080, 0x000000C0);	// des_en off : 0xBF0548[7:6] : 0x2 / [7]:1 [6]:0

	msleep(10);

	/* RX reset deassert */
	m_writel_mask(BASE_ADDR_SDS_PHY + 0x047C, 0x00000000, 0x00000018);	// cdr_en on : 0xBF047C[4:3] : 0x0 / [4]:0 [3]:0
	m_writel_mask(BASE_ADDR_SDS_PHY + 0x0548, 0x00000000, 0x000000C0);	// des_en on : 0xBF0548[7:6] : 0x0 / [7]:0 [6]:0
	
	msleep(40);

	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00000000, 0x00001111);	// SERDES eth_tv/eth/i2c/usb1 reset assert
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x00000000, 0x11111100);	// SERDES adc/fmt/ch/lb/vtg/tsd reset assert
	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00000000, 0x00100000);	// SERDES core_rx reset assert

	msleep(10);	// delay for reset

	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00100000, 0x00100000);	// SERDES core_rx reset deassert
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x11111100, 0x11111100);	// SERDES adc/fmt/ch/lb/vtg/tsd reset deassert
	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00001111, 0x00001111);	// SERDES eth_tv/eth/i2c/usb1 reset deassert

	printk("[SERDES] %s\n", __FUNCTION__);

	msleep(40);
}

// 7. Wait SERDES CORE CONNECT
static bool check_serdes_Oconnect(unsigned int *value)
{
	unsigned int read_val;
	int ret;

	int i;
	for(i = 0; i < 10; i++) {
		ret = m_readl(BASE_ADDR_SDS_CORE + 0x0370, &read_val);

		if (0x100E == (read_val & 0x100E) && ret >= 0) {
			continue;
		} else {
			*value = read_val;
			return false;
		}
	}

	return true;
}

// 8. SERDES DATA PATH setting
static void sdp_serdes_usb_path_setting(void)
{
	unsigned int value;

	m_writel(BASE_ADDR_USB_EHCI_PORTA + 0x0050, 0x00000000); 
	m_writel(BASE_ADDR_USB_EHCI_PORTB + 0x0050, 0x00000000); 

	// PHY/LINK Reset ON
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0830, 0x00000000, 0x00001100);		//0xF507F0 [12]:A = 0, [8]:B = 0

	msleep(1);
	
	// PHY_LINK_CLK_SEL
	m_writel_mask(BASE_ASDR_USB_LINK_CTRL + 0x0000, 0x00000000, 0x00100000);	//[20]=0 / LINK_WORDINTERFACE of LINK_CTRL_A
	m_writel_mask(BASE_ASDR_USB_LINK_CTRL + 0x0010, 0x00000000, 0x00100000);	//[20]=0 / LINK_WORDINTERFACE of LINK_CTRL_B
	m_writel_mask(BASE_ADDR_USB_PHY_CTRL + 0x0034, 0x00000000, 0x00040000);		//[18]=0 / PortA PHY Control 1
	m_writel_mask(BASE_ADDR_USB_PHY_CTRL + 0x0044, 0x00000000, 0x00040000);		//[18]=0 / PortB PHY Control 1

	// UTMI PATH SEL -> SERDES
	m_writel_mask(BASE_ASDR_USB_LINK_CTRL + 0x0050, 0x00001111, 0x00001111);

	msleep(1);
	
	// PHY/LINK Reset OFF
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0830, 0x00001100, 0x00001100);		//0xF507F0 [12]:A = 1, [8]:B = 1

	msleep(1);

	// SERDES USB Setup
	m_writel_mask(BASE_ADDR_SDS_CORE + 0x003C, 0x00000000, 0x00007100);
	m_writel(BASE_ADDR_SDS_CORE + 0x0070, 0x00001313); 
	m_writel_mask(BASE_ADDR_SDS_GPR + 0x1000, 0x00003130, 0x00003131);
	m_writel_mask(BASE_ADDR_SDS_GPR + 0x2000, 0x00003130, 0x00003131);
}

static void sdp_serdes_path_setting(void)
{
#ifndef USES_EXT_I2C
	m_writel_mask(ADDR_SERDES_PATH_MUX, VALUE_SERDES_PATH_MUX, MASK_SERDES_PATH_MUX); // path setting of Uart, SPI, I2C for OC path
	// [19]    : UART
	// [18]    : SPI
	// [17:16] : I2C
#else
	m_writel_mask(ADDR_SERDES_PATH_MUX, ~VALUE_SERDES_PATH_MUX, MASK_SERDES_PATH_MUX); // path setting of Uart, SPI, I2C for Built-in path
#endif
	m_writel_mask(BASE_ADDR_TSD_HW + 0x0070, 0x1294A529, 0x1294A529); // path setting of CAM for OC path
	m_writel_mask(BASE_ADDR_HDMI_HMON + 0x0880, 0x00000001, 0x00000001); // path setting of DDC for OC path
	m_writel_mask(BASE_ASDR_USB_LINK_CTRL + 0x0050, 0x00001111, 0x00001111); // path setting of USB for OC path
}

// 9. SERDES DATA PATH reset release
static void sdp_serdes_data_path_reset_release(void)
{
	m_writel_mask(BASE_ADDR_CLK_RST + 0x080C, 0x00011111, 0x00011111);
	//[28] : core reset
	//[24] : core_tx reset  
	//[20] : core rx reset
	//[16] : usb reset
	//[12] : usb1 reset
	//[08] : i2c reset
	//[04] : ethernet reset
	//[00] : ethernet_tv reset
	
	m_writel_mask(BASE_ADDR_CLK_RST + 0x0810, 0x11111100, 0x11111100);
	//[28] : lb_tv reset
	//[24] : ch reset
	//[20] : fmt reset
	//[16] : adc reset
	//[12] : tsd reset
	//[08] : vtg reset
	//[04] : apb reset
	//[00] : pht_init reset
}

static int sdp_serdes_init(void){
	unsigned int value;
	int lockFailCount = 0;
	
	if(sdp_serdes_lock_check() == 1) {
		printk("[SERDES] already locked !!\n");
		return 0;
	}

	printk("[SERDES] INIT Start\n");

	// 0. Before SERDES INIT Sequence
	sdp_serdes_init_before();

	// 1. SERDES PLL SETTING
	// 2. SERDES PLL LOCKED
	sdp_serdes_init_pll();

	// 3. SERDES PHY INIT
	sdp_serdes_init_phy();

	// 4. CHECK SERDES PHY INIT DONE
	check_serdes_phy_lock();

	// 5. SERDES CORE INIT 
	sdp_serdes_init_core();
	
	// 6. SERDES TX_RSTN RX_RSTN Release
	sdp_serdes_init_rstn();

	printk("[SERDES] INIT Finish !\n");

	// 7. Wait SERDES CORE CONNECT
	while(1) {
		if(check_serdes_Oconnect(&value)) {
			break;
		} else {
			if(lockFailCount++ >= 100) {
				printk("[SERDES] O Connect Time Out lock val = 0x%x\n", value);
				goto serdes_lock_fail;
			}

			if(lockFailCount >= 40) // 40*10ms without reset + 60*100ms with reset
				sdp_serdes_reset_UserLogic();
			else
				msleep(10);
		}
	}

	// 8. SERDES DATA PATH setting
	sdp_serdes_usb_path_setting();
	sdp_serdes_path_setting();
	
	// 9. SERDES DATA PATH reset release
	sdp_serdes_data_path_reset_release();

	printk("[SERDES] Lock !\n");

	return 0;

serdes_lock_fail:
	return -1;
}

static int __init nikem_serdes_init(void)
{
	if(soc_is_nikem() && get_sdp_board_type() == SDP_BOARD_OC){
		if (sdp_serdes_init() == 0) {
			printk("[SERDES] lock\n");
		} else {
			printk("[SERDES] unlock\n");
		}
	}

	return 0;
}

module_init(nikem_serdes_init);

MODULE_LICENSE("GPL");
