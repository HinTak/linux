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

#define BASE_ADDR_SDS_CORE	0x00BE0000
#define BASE_ADDR_SDS_GPR	0x00BE4000
#define BASE_ADDR_SDS_PHY	0x00BF0000
#define CLK_RST_BASE_ADDR	0x00F50000

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

static int sdp_ocl_serdes_lock_check(void)
{
	unsigned int u32Val;
	int ret;

	ret = m_readl(BASE_ADDR_SDS_CORE + 0x370, &u32Val);
	if(ret < 0)
		return ret;

	if ((u32Val & 0x100E) == 0x100E)
		return 1;
	else
		return 0;
}

// 0. Before SERDES INIT Sequence
static void sdp_ocl_init_before(void)
{
	// Reset assert

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07D8, 0x00000000, 0x00001111); // 4*502
	//[12]i_rstn
	//[8]i_tx_rstn  
	//[4]i_rx_rstn 
	//[0]i_usb_rstn
	
	m_writel_mask(CLK_RST_BASE_ADDR + 0x07DC, 0x00000000, 0x11111111); // 4*503
	//[28] : usb_rstn_1
	//[24] : i2c_rstn
	//[20] : eth_rstn
	//[16] : eth_rstn_tv
	//[12] : lb_rstn_tv
	//[8 ] : rstn_ch
	//[4 ] : rstn_FMT_ocm
	//[0 ] : rstn_adc_ocm

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07E0, 0x00000000, 0x11110000); // 4*504
	//[28] : rstn_ts_ocm
	//[24] : rstn_vtg_ocm
	//[20] : SDS_i_init_rstn
	//[16] : SDS_i_apb_preset_n
	
	msleep(1);

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07D8, 0x00001000, 0x00001000); // 4*502
	//[12]i_rstn
	//[8]i_tx_rstn	
	//[4]i_rx_rstn 
	//[0]i_usb_rstn

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07E0, 0x00010000, 0x00010000); // 4*504
	//[28] : rstn_ts_ocm
	//[24] : rstn_vtg_ocm
	//[20] : SDS_i_init_rstn
	//[16] : SDS_i_apb_preset_n

	msleep(1);

	m_writel(BASE_ADDR_SDS_GPR + 0x00C, 0x00000001); // Slave I/F Select
	//[0] : 0 = I2C(MICOM) / 1 = APB(CPU)
}

// 1. SERDES PLL SETTING
// 2. SERDES PLL LOCKED
static void sdp_ocl_init_pll(void)
{
	unsigned int value, cnt=0;

#if 1 // umfa.todo : Is it need?
	// 1. SERDES PLL SETTING
	m_writel(BASE_ADDR_SDS_GPR + 0x040, 0x000C3003);// SERDES_PLL PMS 196Mhz
	//m_writel(BASE_ADDR_SDS_GPR+0x040, 0x000C1803);// SERDES_PLL PMS 98Mhz

	m_writel(BASE_ADDR_SDS_GPR + 0x044, 0x01001101);// SERDES_PLL CTRL

	// 2. SERDES PLL LOCKED
	m_readl(BASE_ADDR_SDS_GPR + 0x050, &value);

	while((value&0x00000001) != 0x00000001) {		// Check SERDES_PLL Locked
		m_readl(BASE_ADDR_SDS_GPR + 0x050, &value);
		msleep(1);
		if(cnt++ >= 200) {
			printk("[OCL] PLL lock fail val = 0x%x\n", value);
			break;
		}
	}
#endif	

}

// 3. SERDES PHY INIT
static void sdp_ocl_init_phy_init(void)
{
	m_writel(BASE_ADDR_SDS_GPR + 0x400, 0x00000000);
	//[0] : power_off 
	//[4] : cmn_rstn 
	//[8] : ln0_rstn 
	
	m_writel(BASE_ADDR_SDS_GPR + 0x404, 0x00010000); 
	//[0 ] :  aux_en  
	//[4 ] :  bgr_en  
	//[8 ] :  bias_en  
	//[12] :  high_speed 
	//[16] :  pll_en  
	//[20] :  ssc_en  
	
//	m_writel(BASE_ADDR_SDS_GPR + 0x408, 0x00000301); REFCLK = XTAL
	m_writel(BASE_ADDR_SDS_GPR + 0x408, 0x00000101);// REFCLK = PLL
	//[1:0] : ln0_rate	  
	//[5:4] : phy_mode	  
	//[9:8] : pll_ref_clk_sel

	m_writel(BASE_ADDR_SDS_GPR + 0x40C, 0x00001111); 
	//[0]  : ln0_rx_bias_en  
	//[4]  : ln0_rx_cdr_en	
	//[8]  : ln0_rx_ctle_en  
	//[12] : ln0_rx_des_en	
	//[16] : ln0_rx_dfe_adap_en 
	//[20] : ln0_rx_dfe_adap_hold
	//[24] : ln0_rx_fom_en	
	//[28] : ln0_rx_lfps_det_en 
	
	m_writel(BASE_ADDR_SDS_GPR + 0x410, 0x00000011);
	//[0]  : ln0_rx_rterm_en  
	//[4]  : ln0_rx_sqhs_en  
	//[11:8]  : ln0_rx_ctle_rs1_ctrl
	//[15:12] : ln0_rx_ctle_rs2_ctrl

	m_writel(BASE_ADDR_SDS_GPR + 0x414, 0x00100000);
	//[4:0] : ln0_tx_drv_post_lvl_ctrl	
	//[8]	: ln0_tx_drv_beacon_lfps_out_en 
	//[12]	: ln0_tx_drv_cm_keeper_en	
	//[16]	: ln0_tx_drv_ei_en	  
	//[20]	: ln0_tx_drv_en 	
	//[24]	: ln0_tx_p2_async	  
	//[28]	: ln0_tx_rxd_en 	

	m_writel(BASE_ADDR_SDS_GPR + 0x418, 0x00000001);
	//[0] : ln0_tx_ser_en	
	//[7:4] : ln0_tx_drv_pre_lvl_ctrl 
	//[12:8] : ln0_tx_drv_lvl_ctrl	

//	m_writel(BASE_ADDR_SDS_GPR+0x054, 0x00000001);
	//[0] : 0: system_xtal. 1: serdes_xtal

	m_writel_mask(BASE_ADDR_SDS_GPR + 0x0410, 0x0000FF00, 0x0000FF00); //rx_ctle_rs0:0xd~0xf / rx_ctle_rs1:0x~0xf

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07E0, 0x00110000, 0x00110000); 
	//[28] : rstn_ts_ocm
	//[24] : rstn_vtg_ocm
	//[20] : SDS_i_init_rstn
	//[16] : SDS_i_apb_preset_n

	msleep(5);

	m_writel(BASE_ADDR_SDS_PHY + 0x414, 0x00000028);// [5]ovrd_tx_drv_post_lvl_ctrl [40]tx_drv_post_lvl_ctrl_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x418, 0x0000000A);// [40]tx_drv_post_lvl_ctrl_g3  // 180323 sweep
	m_writel(BASE_ADDR_SDS_PHY + 0x424, 0x00000013);// [4]ovrd_tx_drv_pre_lvl_ctrl [30]tx_drv_pre_lvl_ctrl_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x428, 0x00000022);// [74]tx_drv_pre_lvl_ctrl_g2 [30]tx_drv_pre_lvl_ctrl_g3
	m_writel(BASE_ADDR_SDS_PHY + 0x42C, 0x00000035);// [63]tx_drv_pre_lvl_ctrl_g4 [2]ana_tx_drv_beacon_lfps_sync_en [1]ovrd_tx_drv_idrv_en [0]tx_drv_idrv_en
	m_writel(BASE_ADDR_SDS_PHY + 0x4EC, 0x00000010);// [4]ln0_ovrd_rx_ctle_rs1_ctrl
	m_writel(BASE_ADDR_SDS_PHY + 0x4F8, 0x00000010);// [4]ln0_ovrd_rx_ctle_rs2_ctrl

	//Analog Set 24M
	m_writel(BASE_ADDR_SDS_PHY + 0x018, 0x00000028); //ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x0B4, 0x00000050); //pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0C4, 0x00000050); //pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0D0, 0x00000011); //ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x0D4, 0x00000001); //ana_pll_pms_refdiv 

	m_writel(BASE_ADDR_SDS_PHY + 0x0D8, 0x00000011); //pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x18C, 0x00000003); // ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x480, 0x00000033); //rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x488, 0x00000011); //rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x490, 0x00000033); //rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x498, 0x00000000); //rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x620, 0x00000000); //rx_cdr_pms_m_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x624, 0x000000A0); //rx_cdr_pms_m_g1
	m_writel(BASE_ADDR_SDS_PHY + 0x628, 0x00000000); //rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x62C, 0x000000A0); //rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x404, 0x00000020); // ADD same to ATE

	m_writel(BASE_ADDR_SDS_PHY+0x408, 0x0000001F); //(Tx_Lvl setting) okh 

//	m_writel(BASE_ADDR_SDS_PHY + 0x408, 0x0000000); (Tx_Lvl setting) okh 
	
#if 0
	//Analog Set 98M
	m_writel(BASE_ADDR_SDS_PHY + 0x018, 0x00000028); //ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x0B4, 0x00000014); //pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0C4, 0x00000014); //pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0D0, 0x00000011); //ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x0D4, 0x00000004); //ana_pll_pms_refdiv
	m_writel(BASE_ADDR_SDS_PHY + 0x0D8, 0x00000011); //pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x18C, 0x00000003); // ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x480, 0x00000033); //rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x488, 0x00000011); //rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x490, 0x00000033); //rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x498, 0x00000000); //rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x628, 0x00000000); //rx_cdr_pms_m_g2 
	m_writel(BASE_ADDR_SDS_PHY + 0x62C, 0x000000A0); //rx_cdr_pms_m_g2 

	// Analog Set 196M
	m_writel(BASE_ADDR_SDS_PHY + 0x018, 0x00000028); //ana_pll_afc
	m_writel(BASE_ADDR_SDS_PHY + 0x0B4, 0x00000014); //pll_pms_mdiv_afc_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0C4, 0x00000014); //pll_pms_mdiv_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x0D0, 0x00000012); //ana_pll_pms_pm_div
	m_writel(BASE_ADDR_SDS_PHY + 0x0D4, 0x00000008); //ana_pll_pms_refdiv
	m_writel(BASE_ADDR_SDS_PHY + 0x0D8, 0x00000011); //pll_pms_sdiv_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x18C, 0x00000003); // ana_pll_reserved
	m_writel(BASE_ADDR_SDS_PHY + 0x480, 0x00000033); //rx_cdr_refdiv_sel_pll
	m_writel(BASE_ADDR_SDS_PHY + 0x488, 0x00000011); //rx_cdr_refdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x490, 0x00000033); //rx_cdr_mdiv_sel_pll_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x498, 0x00000000); //rx_cdr_mdiv_sel_data_g1_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x628, 0x00000000); //rx_cdr_pms_m_g2
	m_writel(BASE_ADDR_SDS_PHY + 0x62C, 0x000000A0); //rx_cdr_pms_m_g2
#endif

	m_writel(BASE_ADDR_SDS_PHY + 0x0E4, 0x00000002); //ana_pll_ref_clk_sel 
	//[3]	  : 1'h0  / ovrd_pll_ref_clk_sel			 // set 1
	//[2:1]   : 2'h1  / pll_ref_clk_sel 				 // set 1
	//[0]	  : 1'h0  / ana_pll_ref_dig_clk_sel

	m_writel(BASE_ADDR_SDS_PHY + 0x190, 0x0000004C);	 // 'h4C
	//[7]	  : 1'h0  / ana_aux_rx_tx_sel
	//[6]	  : 1'h0  / ovrd_aux_en 					 // set 1
	//[5]	  : 1'h0  / aux_en							 // set 0
	//[4]	  : 1'h0  / ana_aux_rx_cap_bypass
	//[3]	  : 1'h1  / ana_aux_rx_term_gnd_en
	//[2:0]   : 3'h4  / ana_aux_tx_term

	m_writel(BASE_ADDR_SDS_PHY + 0x448, 0x00000051);					  // trsv_reg012 'h51
	//[6]	  : 1'h0  / ln0_ovrd_pi_rate				 // set 1
	//[5:4]   : 2'h0  / ln0_pi_rate 					 // set 2
	//[0]	  : 1'h1  / ln0_ana_tx_jeq_en

	m_writel(BASE_ADDR_SDS_PHY + 0x21C, 0x0000002C);	// cmn_reg083 'h2C
	//[6]	  : 1'h0  / pcs_bias_sel
	//[5]	  : 1'h0  / ovrd_pi_ssc_en
	//[4]	  : 1'h0  / pi_ssc_en
	//[3]	  : 1'h0  / ovrd_pcs_pll_en 				 // set 1
	//[2]	  : 1'h0  / pcs_pll_en						 // set 1
	//[1]	  : 1'h0  / ovrd_pcs_bias_en
	//[0]	  : 1'h0  / pcs_bias_en

	m_writel(BASE_ADDR_SDS_PHY + 0x4AC, 0x000000F0);					  // trsv_reg02B 'hF0
	//[7]	  : 1'h0  / ln0_ovrd_pcs_rx_bias_en 		 // set 1
	//[6]	  : 1'h0  / ln0_pcs_rx_bias_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_cdr_en			 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_cdr_en				 // set 1
	//[3:0]   : 4'h0  / ln0_ana_rx_cdr_vco_bbcap_dn_ctrl

	m_writel(BASE_ADDR_SDS_PHY + 0x4B0, 0x000000F1);	// trsv_reg02C 'hF1
	//[7]	  : 1'h0  / ln0_ovrd_pcs_rx_des_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_rx_des_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_sqhs_en 		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_rx_sqhs_en			 // set 1
	//[3]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g1
	//[2]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g2
	//[1]	  : 1'h0  / ln0_rx_cdr_vco_freq_boost_g3
	//[0]	  : 1'h1  / ln0_rx_cdr_vco_freq_boost_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x0BC, 0x00000050);					  //pll_pms_mdiv_afc_g4 // 
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_drv_en				 // set 0
	//[5]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_ei_en		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_tx_drv_ei_en			 // set 0
	//[3]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g1
	//[2]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g2
	//[1]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g3
	//[0]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x468, 0x0000003F);		  // trsv_reg01A 'h3F
	//[5]	  : 1'h1  / ln0_tx_ser_40bit_en_g1
	//[4]	  : 1'h1  / ln0_tx_ser_40bit_en_g2
	//[3]	  : 1'h1  / ln0_tx_ser_40bit_en_g3
	//[2]	  : 1'h1  / ln0_tx_ser_40bit_en_g4
	//[1]	  : 1'h0  / ln0_ovrd_tx_ser_data_rstn		 // set 1
	//[0]	  : 1'h0  / ln0_tx_ser_data_rstn			 // set 1

	m_writel(BASE_ADDR_SDS_PHY + 0x4D4, 0x000000B0);		  // trsv_reg035 'hB0 //DC Choi
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_p2_async		 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_p2_async
	//[5]	  : 1'h0  / ln0_ovrd_pcs_rx_ctle_en 		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_rx_ctle_en				 // set 1
	//[1]	  : 1'h0  / ln0_ovrd_rx_ctle_oc_en
	//[0]	  : 1'h0  / ln0_rx_ctle_oc_en

	m_writel(BASE_ADDR_SDS_PHY + 0x59C, 0x00000003);				// trsv_reg067 'h03
	//[5]	  : 1'h0  / ln0_ovrd_rx_rcal_bias_en
	//[4]	  : 1'h0  / ln0_rx_rcal_bias_en
	//[3:2]   : 2'h0  / ln0_ana_rx_rcal_irmres_ctrl
	//[1]	  : 1'h0  / ln0_ovrd_rx_rterm_en			 // set 1
	//[0]	  : 1'h0  / ln0_rx_rterm_en 				 // set 1

	m_writel(BASE_ADDR_SDS_PHY + 0x4BC, 0x000000EF);	// trsv_reg02F 'hEF
	//[7]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_en			 // set 1
	//[6]	  : 1'h0  / ln0_pcs_tx_drv_en				 // set 1
	//[5]	  : 1'h0  / ln0_ovrd_pcs_tx_drv_ei_en		 // set 1
	//[4]	  : 1'h0  / ln0_pcs_tx_drv_ei_en			 // set 0
	//[3]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g1
	//[2]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g2
	//[1]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g3
	//[0]	  : 1'h1  / ln0_rx_ctle_high_bw_en_g4

	m_writel(BASE_ADDR_SDS_PHY + 0x400, 0x00000028); // trsv_reg000 'h28
	//[7]	  : 1'h0  / ln0_ovrd_tx_drv_en
	//[6]	  : 1'h0  / ln0_tx_drv_en
	//[5]	  : 1'h0  / ln0_ovrd_tx_drv_beacon_lfps_out_en	// set 1
	//[4]	  : 1'h0  / ln0_tx_drv_beacon_lfps_out_en
	//[3]	  : 1'h0  / ln0_ovrd_tx_drv_cm_keeper_en		// set 1
	//[2]	  : 1'h0  / ln0_tx_drv_cm_keeper_en
	//[1]	  : 1'h0  / ln0_ovrd_tx_drv_ei_en
	//[0]	  : 1'h0  / ln0_tx_drv_ei_en

	m_writel(BASE_ADDR_SDS_PHY + 0x460, 0x00000032); // trsv_reg018 'h32
	//[7]	  : 1'h0  / ln0_tx_rterm_42p5_en_g1
	//[6]	  : 1'h0  / ln0_tx_rterm_42p5_en_g2
	//[5]	  : 1'h1  / ln0_tx_rterm_42p5_en_g3
	//[4]	  : 1'h1  / ln0_tx_rterm_42p5_en_g4
	//[3]	  : 1'h0  / ln0_ovrd_tx_rxd_comp_en
	//[2]	  : 1'h0  / ln0_tx_rxd_comp_en
	//[1]	  : 1'h0  / ln0_ovrd_tx_rxd_en					// set 1
	//[0]	  : 1'h0  / ln0_tx_rxd_en

	m_writel(BASE_ADDR_SDS_PHY + 0x5D4, 0x00000004); // trsv_reg075 'h04 // DC Choi
	//[6:3]   : 4'h0  / ln0_ana_rx_pwm_oc_code
	//[2]	  : 1'h0  / ln0_ovrd_rx_lfps_det_en 			// set 1
	//[1]	  : 1'h0  / ln0_rx_lfps_det_en
	//[0]	  : 1'h0  / ln0_ana_rx_lfps_loss_det_en

	m_writel(BASE_ADDR_SDS_PHY + 0x54C, 0x000000D5); // trsv_reg053 'hD4
	//[7]	  : 1'h1  / ln0_rx_dfe_adap_short_done
	//[6]	  : 1'h0  / ln0_ovrd_rx_dfe_adap_en 			// set 1
	//[5]	  : 1'h1  / ln0_rx_dfe_adap_en
	//[4]	  : 1'h0  / ln0_ovrd_rx_dfe_eom_en
	//[3]	  : 1'h0  / ln0_rx_dfe_eom_en
	//[2:0]   : 3'h4  / ln0_ana_rx_dfe_eom_pi_div_sel

	m_writel(BASE_ADDR_SDS_PHY + 0x4B8, 0x0000006C);
	m_writel(BASE_ADDR_SDS_PHY + 0x4BC, 0x0000000F);
	m_writel(BASE_ADDR_SDS_PHY + 0x4C0, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x4C4, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x4C8, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x4CC, 0x00000012);
	m_writel(BASE_ADDR_SDS_PHY + 0x4D8, 0x00000003);
	m_writel(BASE_ADDR_SDS_PHY + 0x4DC, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x4E0, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x4E4, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x4E8, 0x00000009);
	m_writel(BASE_ADDR_SDS_PHY + 0x4EC, 0x0000001F);
	m_writel(BASE_ADDR_SDS_PHY + 0x4F0, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x4F8, 0x00000018);
	m_writel(BASE_ADDR_SDS_PHY + 0x500, 0x00000008);
	m_writel(BASE_ADDR_SDS_PHY + 0x504, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x508, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x50C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x510, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x518, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x51C, 0x000000FF);
	m_writel(BASE_ADDR_SDS_PHY + 0x520, 0x0000000A);
	m_writel(BASE_ADDR_SDS_PHY + 0x524, 0x00000077);
	m_writel(BASE_ADDR_SDS_PHY + 0x52C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x530, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x534, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x538, 0x00000000);

	m_writel(BASE_ADDR_SDS_PHY + 0x4EC, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs1_ctrl
	m_writel(BASE_ADDR_SDS_PHY + 0x4F8, 0x00000010);	// [4]ln0_ovrd_rx_ctle_rs2_ctrl

	m_writel(BASE_ADDR_SDS_PHY + 0x4F0, 0x00);	// rs1[7:4] EQ1
	m_writel(BASE_ADDR_SDS_PHY + 0x4FC, 0x00);	// rs2[7:4] EQ2

	m_writel(BASE_ADDR_SDS_PHY + 0x418, 0x8 << 1);	// [40]tx_drv_post_lvl_ctrl_g2  // 180323 sweep		

	m_writel(BASE_ADDR_SDS_PHY + 0x428, 0x0 << 4);	// [74]tx_drv_pre_lvl_ctrl_g2 [30]tx_drv_pre_lvl_ctrl_g3

	m_writel(BASE_ADDR_SDS_PHY + 0x4C4, 0x7F);		// 181030 CTLE Itail increase
	m_writel(BASE_ADDR_SDS_PHY + 0x4E0, 0x0F);		// 181030 R load decrease caused by CTLE Current increase
	m_writel(BASE_ADDR_SDS_PHY + 0x510, 0x57);		// 181030 CTLE 2nd Stage Off
	m_writel(BASE_ADDR_SDS_PHY + 0x5A4, 0x0C);		// 181030 Invcm modify
	m_writel(BASE_ADDR_SDS_PHY + 0x508, 0x01);		// 181030  CS Ctrl 0=>1

	//180917 added
	m_writel(BASE_ADDR_SDS_PHY + 0x740, 0x00000060);	// sigvql overide

	// CTLE Tune 180918 added
	m_writel(BASE_ADDR_SDS_PHY + 0x518, 0x000000EF);	// Negc_En	EF, EE, EC, E8, E0
	m_writel(BASE_ADDR_SDS_PHY + 0x51C, 0x000000FF); 
	m_writel(BASE_ADDR_SDS_PHY + 0x520, 0x0000000A);	// Negc_itail_Ctrl (Eval : 0x0A, Product: 0x0F)
//	m_writel(BASE_ADDR_SDS_PHY + 0x524, 0x00000000); 
	m_writel(BASE_ADDR_SDS_PHY + 0x528, 0x00000000);	// vcm_sel

	msleep(1);	// wait 1ms

#if 0 //for Trim Test
	m_writel(BASE_ADDR_SDS_PHY+0x6FC, 0x00000001);
	m_writel(BASE_ADDR_SDS_PHY+0x700, 0x00000040);

	pr_err("[OCL] Set Register value for TRIM test\n");
#endif

	m_writel(BASE_ADDR_SDS_PHY + 0x5EC, 0x00000020);
	m_writel(BASE_ADDR_SDS_PHY + 0x5A8, 0x00000000);

	m_writel(BASE_ADDR_SDS_PHY + 0x59C, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x5AC, 0x00000038);
	m_writel(BASE_ADDR_SDS_PHY + 0x670, 0x000000FC);
	m_writel(BASE_ADDR_SDS_PHY + 0x678, 0x000000F4);
	m_writel(BASE_ADDR_SDS_PHY + 0x7F8, 0x00000033);
	m_writel(BASE_ADDR_SDS_PHY + 0x4A0, 0x00000004);
	m_writel(BASE_ADDR_SDS_PHY + 0x468, 0x0000003C);
	m_writel(BASE_ADDR_SDS_PHY + 0x540, 0x00000000);

	m_writel(BASE_ADDR_SDS_GPR + 0x400, 0x00000110);	// GPR__phy_ctrl_0

	m_writel(BASE_ADDR_SDS_PHY + 0x000, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x704, 0x00000000);
	m_writel(BASE_ADDR_SDS_PHY + 0x708, 0x000000ED);	// RX manual BIST//[7]bist_en[6]data_en[5]rx_en[4]rx_hold [3]rx_start[2]tx_en[1]tx_errinj[0]tx_start
	m_writel(BASE_ADDR_SDS_PHY + 0x704, 0x00000000);	// [1:0] prbs7 (00: prbs7,   01: prbs11,   11: prbs23,   (10:X)

	msleep(1);//wait 1ms

#if 0 // Unmark, In case of DFE ON
	m_writel(BASE_ADDR_SDS_PHY+0x54C, 0x24);// DFE Adap
	m_writel(BASE_ADDR_SDS_PHY+0x61C, 0x3e);// DFE Adap
#endif
}

// 4. CHECK SERDES PHY INIT DONE
static void checkSerdesPhyLock(void)
{
	unsigned int r_tmp_data;

	do{
		//	[1]	  : 1'h0  / ana_pll_lock_done
		//	[0]	  : 1'h0  / ana_pll_afc_done
		m_readl(BASE_ADDR_SDS_PHY + 0x1D4, &r_tmp_data);	// cmn_reg075 'h00		
	}while((r_tmp_data & 0x3) != 0x3);						// Check PHY PLL Locked

	do{
		//	[3]	  : 1'h0  / ln0_mon_rx_cdr_lock_done
		m_readl(BASE_ADDR_SDS_PHY + 0x7E8, &r_tmp_data);	// cmn_reg075 'h00
	}while((r_tmp_data & 0x8) != 0x8);						// Check PHY CDR Locked
}

// 5. SERDES CORE INIT 
static void sdp_ocl_init_core(void)
{
#if 0
	m_writel(BASE_ADDR_SDS_CORE + 0x03C, 0x00001111); // USB Simulation speed up. Simulation only
	m_writel(BASE_ADDR_SDS_CORE + 0x19C, 0x00000120); // RX Side Uniform Off
	m_writel(BASE_ADDR_SDS_CORE + 0x070, 0x00001313); // TV usb mode set to Peri attatched mode
	m_writel(BASE_ADDR_SDS_CORE + 0x210, 0x00000003); // SPI path to TX FLASH update
	m_writel(BASE_ADDR_SDS_CORE + 0x1FC, 0x00000401); // gpio_pad direction setting. [10], [0] set to output
	m_writel(BASE_ADDR_SDS_CORE + 0x0B4, 0x000003BC); // Backward TS1 setting
	m_writel(BASE_ADDR_SDS_CORE + 0x1DC, 0x0000015E); // Sat ADC FIFO Level Adjust
	m_writel(BASE_ADDR_SDS_CORE + 0x00C, 0x00000F63); // sbd_thr=0xF
#else
	m_writel(BASE_ADDR_SDS_CORE + 0x19C, 0x00000120);//  RX Side Uniform Off
	m_writel(BASE_ADDR_SDS_CORE + 0x070, 0x00005353);//  TV usb mode set to Peri attatched mode
	m_writel(BASE_ADDR_SDS_CORE + 0x210, 0x00000003);//  SPI path to TX FLASH update
	m_writel(BASE_ADDR_SDS_CORE + 0x1FC, 0x00000001);//  gpio_pad direction setting. [10], [0] set to output
	m_writel(BASE_ADDR_SDS_CORE + 0x0B4, 0x000003BC);//  Backward TS1 setting
	m_writel(BASE_ADDR_SDS_CORE + 0x1DC, 0x0000015E);//  Sat ADC FIFO Level Adjust
	m_writel(BASE_ADDR_SDS_CORE + 0x040, 0x0000FFFF);  
	m_writel(BASE_ADDR_SDS_CORE + 0x010, 0x00000208);
#endif

	m_writel(BASE_ADDR_SDS_PHY + 0x704, 0x60); // BIST_AUTO_RUN Clear, comdet_num3. PRBS15;
	//[7]	   : 1'h0  / ln0_bist_auto_run							   
	//[6:5]   : 2'h0  / ln0_bist_comdet_num
	//[4:2]   : 3'h0  / ln0_bist_seed_sel
	//[1:0]   : 2'h0  / ln0_bist_prbs_mode

}

// 6. SERDES OCL TX_RSTNRX_RSTN Release
static void sdp_ocl_init_rstn(void)
{
	//[8]i_tx_rstn	
	//[4]i_rx_rstn 
	//[0]i_usb_rstn
	m_writel_mask(CLK_RST_BASE_ADDR+0x07D8, 0x00000110, 0x00000110);

	m_writel(BASE_ADDR_SDS_PHY+0x0708, 0x00000000);
}

// 7. Wait SERDES CORE CONNECT
static int checkSerdesOconnect(unsigned int *value)
{
	unsigned int read_val;
	int ret;

	int i;
	for(i = 0; i < 10; i++) {
		ret = m_readl(BASE_ADDR_SDS_CORE + 0x370, &read_val);
//		pr_err("[OCL] Lock = 0x%x, Lane Error = 0x%x\n", read_val, read_val2);

		if (0x100e == (read_val & 0x100e) && ret >= 0) {
			continue;
		} else {
			*value = read_val;
			return 0;
		}
	}

	return 1;
}

// 8. SERDES DATA PATH setting
static void sdp_ocl_usb_path_setting(void)
{
	unsigned int value;

	m_writel(0x00480050, 0x0); 
	m_writel(0x00490050, 0x0); 

	//PHY/LINK Reset ON
	m_writel_mask(0x00F507F0, 0x00000000, 0x00001100);		//0xF507F0 [12]:A = 0, [8]:B = 0
	
	//PHY_LINK_CLK_SEL
	m_writel_mask(0x004D0000, 0x00000000, 0x00100000);//[20]=0  LINK_WORDINTERFACE
	m_writel_mask(0x004D0010, 0x00000000, 0x00100000);//[20]=0
	m_writel_mask(0x009C0934, 0x00000000, 0x00040000);//[18]=0
	m_writel_mask(0x009C0944, 0x00000000, 0x00040000);//[18]=0

	//UTMI PATH SEL -> SERDES
	m_writel(0x004D0050, 0x1111); 
	
	//PHY/LINK Reset OFF
	m_writel_mask(0x00F507F0, 0x00001100, 0x00001100);		//0xF507F0 [12]:A = 1, [8]:B = 1

	//SERDES USB Setup
	m_writel_mask(0x00BE003C, 0x00000000, 0x00007100);
	m_writel(0x00BE0070, 0x00001313); 
	m_writel_mask(0x00BE5000, 0x00003130, 0x00003131);
	m_writel_mask(0x00BE6000, 0x00003130, 0x00003131);

	//SERDES USB Reset off   
	//=>>>   move to  sdp_ocl_data_path_reset_release()
	//m_writel_mask(CLK_RST_BASE_ADDR+0x07D8, 0x00000001, 0x00000001);
	//m_writel_mask(CLK_RST_BASE_ADDR+0x07DC, 0x10000000, 0x10000000);
}

// 8. SERDES DATA PATH setting
static void sdp_ocl_path_setting(void)
{
#ifndef USES_EXT_I2C
	m_writel_mask(0x5515B4, 0x00010000, 0x00010000); // path setting of Uart, I2C for OC path
#else
	m_writel_mask(0x5515B4, 0x00000000, 0x00010000); // path setting of Uart, I2C for Built-in path
#endif
	m_writel_mask(0x910070, 0x1294A529, 0x1294A529); // path setting of CAM for OC path
	m_writel_mask(0x5C0880, 0x00000001, 0x00000001); // path setting of DDC for OC path
	m_writel_mask(0x4D0050, 0x00001111, 0x00001111); // path setting of USB for OC path
}

// 9. SERDES DATA PATH reset release
static void sdp_ocl_data_path_reset_release(void)
{
	m_writel_mask(CLK_RST_BASE_ADDR + 0x07DC, 0x11111111, 0x11111111);
	//[28] : usb_rstn_1
	//[24] : i2c_rstn
	//[20] : eth_rstn
	//[16] : eth_rstn_tv
	//[12] : lb_rstn_tv
	//[8 ] : rstn_ch
	//[4 ] : rstn_FMT_ocm
	//[0 ] : rstn_adc_ocm

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07D8, 0x00000001, 0x00000001);
	//[8]i_tx_rstn	
	//[4]i_rx_rstn 
	//[0]i_usb_rstn

	m_writel_mask(CLK_RST_BASE_ADDR + 0x07E0, 0x11000000, 0x11000000);
	//[28] : rstn_ts_ocm
	//[24] : rstn_vtg_ocm
	//[20] : SDS_i_init_rstn		
}

static int sdp_ocl_serdes_init(void){
	unsigned int value;
	int lockFailCount = 0;
	
	if(sdp_ocl_serdes_lock_check() == 1) {
		printk("[OCL] Serdes was already locked !!\n");
		return 0;
	}

	printk("[OCL] SerDes Init musem_serdes_init_180920_connect_8G_release.cmm\n");

	printk("[trace] SerDes INIT Start\n");

	// 0. Before SERDES INIT Sequence
	sdp_ocl_init_before();

	// 1. SERDES PLL SETTING
	// 2. SERDES PLL LOCKED
	sdp_ocl_init_pll();

	// 3. SERDES PHY INIT
	sdp_ocl_init_phy_init();

	// 4. CHECK SERDES PHY INIT DONE
	checkSerdesPhyLock();

	// 5. SERDES CORE INIT 
	sdp_ocl_init_core();

#if 0
	//	;debug_clock:b_GPIO_RES13
		write_l(0x00F52084) %l 0xE000 
		write_l(&SDS_GPR_BASE_ADDR+0x030) %l 0x00000001  ; 0:lsb, 1:msb / 0:dbg_clk, 1:o_dtb
		write_l(0x00BE4034) %l 0x05 ; 0x5: rx_clk, 0x7: tx_clk 

		write_l(&SDS_CORE_BASE_ADDR+0x208) %l 0x00000200	  ;[7:4]0:test_mux [15:8]test_mux_sel
		write_l(&SDS_PHY_BASE_ADDR+0x4EC) %l 0x00000010  ; [4]ln0_ovrd_rx_ctle_rs1_ctrl
		write_l(&SDS_PHY_BASE_ADDR+0x4F8) %l 0x00000010  ; [4]ln0_ovrd_rx_ctle_rs2_ctrl

		write_l(&SDS_PHY_BASE_ADDR+0x704) %l 0x60									// BIST_AUTO_RUN Clear, comdet_num3. PRBS7;
#endif
	
	// 6. SERDES OCL TX_RSTNRX_RSTN Release
	sdp_ocl_init_rstn();

	printk("[trace] SerDes INIT End\n");

	// 7. Wait SERDES CORE CONNECT
	while(1) {
		if(checkSerdesOconnect(&value)) {
			break;
		} else {
			if(lockFailCount++ == 100) {
				pr_err("[OCL] O Connect Time Out lock val = 0x%x\n", value);
				goto serdes_lock_fail;
			}
			msleep(100);
		}
	}

#if 1
	// 8. SERDES DATA PATH setting
	sdp_ocl_usb_path_setting();
	sdp_ocl_path_setting();
	
	// 9. SERDES DATA PATH reset release
	sdp_ocl_data_path_reset_release();
#endif

	printk("[trace] SerDes Lock\n");

	return 0;

serdes_lock_fail:
	return -1;

}

static int __init sdp1803_serdes_init(void)
{
	if(soc_is_sdp1803() && get_sdp_board_type() == SDP_BOARD_OC){
		if (sdp_ocl_serdes_init() == 0) {
			printk("[OCL] SERDES lock\n");
		} else {
			printk("[OCL] SERDES unlock\n");
		}
	}

	return 0;
}

module_init(sdp1803_serdes_init);

MODULE_LICENSE("GPL");


