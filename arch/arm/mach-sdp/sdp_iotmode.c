
/*********************************************************************************************
 *
 *	sdp_iotmode.c 
 *
 *	author : yongjin79.kim@samsung.com
 *	
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

#ifdef CONFIG_ARCH_SDP1501
#include <linux/platform_data/sdp-cpufreq.h>

int sdp_cpufreq_get_level(unsigned int freq, unsigned int *level);
int sdp_cpufreq_limit(unsigned int nId, enum cpufreq_level_index cpufreq_level);
void sdp_cpufreq_limit_free(unsigned int nId);

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);

#if 0
#define sdp_iotmode_trace \
{ \
	printk(KERN_INFO "%s():%d\n", __FUNCTION__, __LINE__); \
	msleep(100); \
}
static void dump_clkrst_regs(void)
{
	u32 addr = 0x5c0000;

	for (addr=0x5c0000; addr < 0x5c1000; addr += 16) {
		printk(KERN_INFO "%08x : %08x %08x %08x %08x\n", 
			addr,
			sdp_read_clkrst_mux(addr),
			sdp_read_clkrst_mux(addr+4),
			sdp_read_clkrst_mux(addr+8),
			sdp_read_clkrst_mux(addr+12));
	}
}
#else
#define sdp_iotmode_trace
static void dump_clkrst_regs(void) 
{
}
#endif

u32 sdp_read_clkrst_mux(u32 phy_addr);

void jazz_m_clk_rst_iot_on(void)
{
	printk(KERN_INFO "jazz_m_clk_rst_iot_on\n");
	dump_clkrst_regs();

	sdp_set_clkrst_mux(0x005C0734, 0xffffffff, 0x00000000);
	sdp_set_clkrst_mux(0x005C0738, 0xffffffff, 0x00000000);
	sdp_set_clkrst_mux(0x005C073c, 0xffffffff, 0x00000001);
	sdp_set_clkrst_mux(0x005C0740, 0xffffffff, 0x01110100);
	sdp_set_clkrst_mux(0x005C0744, 0xffffffff, 0x00000000);
	sdp_set_clkrst_mux(0x005C0748, 0xffffffff, 0x00000100);
	sdp_set_clkrst_mux(0x005C074c, 0xffffffff, 0x00000000);
	sdp_set_clkrst_mux(0x005C0760, 0xffffffff, 0x00000000);
	//sdp_set_clkrst_mux(0x005C0764, 0xffffffff, 0x00000000);

	
	//************************************************************* 
	//	  SUBPATH On For IoT									   
	//************************************************************* 
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_hclk_peri_out_sel_sub			
	// [   24] =  1'd0 : rslv_dma_out_sel_sub					
	// [   20] =  1'd0 : rslv_msp_out_sel_sub					
	// [   16] =  1'd0 : rslv_gpu_out_sel_sub_1				
	// [   12] =  1'd1 : rslv_gpu_out_sel_sub_2				
	// [    8] =  1'd0 : rslv_bus_out_sel_sub					
	// [    4] =  1'd1 : rslv_mfd_out_sel_sub					
	// [    0] =  1'd1 : rslv_jpeg_out_sel_sub				
	sdp_set_clkrst_mux(0x005c0280, 0xffffffff, 0x00001011); // CLK_MUX_SUB_SEL_0 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd1 : rslv_freq_check_out_sel_sub 			
	// [   24] =  1'd1 : rslv_promise_out_sel_sub					
	// [   20] =  1'd1 : rslv_emmc_out_sel_sub					
	// [   16] =  1'd1 : rslv_ctr_out_sel_sub						
	// [   12] =  1'd1 : rslv_tdsp_out_sel_sub					
	// [    8] =  1'd0 : rslv_aio_out_sel_sub_1					
	// [    4] =  1'd1 : rslv_aio_out_sel_sub_2					
	// [    0] =  1'd0 : rslv_dmicom_out_sel_sub 				
	sdp_set_clkrst_mux(0x005c028c, 0xffffffff, 0x11111010); // CLK_MUX_SUB_SEL_3 
	sdp_iotmode_trace;
	
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd1 : rslv_busif_out_sel_sub					
	// [   24] =  1'd0 : rslv_ga2d_out_sel_sub					
	// [   20] =  1'd0 : rslv_gfxp_c_out_sel_sub 				
	// [   16] =  1'd0 : rslv_dvde_out_sel_sub					
	// [   12] =  1'd0 : rslv_dpscl_b_out_sel_sub					
	// [    8] =  1'd0 : rslv_frc_bus_out_sel_sub					
	// [    4] =  1'd1 : rslv_srp_out_sel_sub						
	// [    0] =  1'd0 : rslv_bus_sub_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0290, 0xffffffff, 0x10000010); // CLK_MUX_SUB_SEL_4 
	sdp_iotmode_trace;
	
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_dp_o_out_sel_sub					
	// [   24] =  1'd0 : rslv_dpscl_w_out_sel_sub					
	// [   20] =  1'd0 : rslv_avd_out_sel_sub						
	// [   16] =  1'd0 : rslv_henc_out_sel_sub					
	// [   12] =  1'd0 : rslv_dp_nrfc_out_sel_sub					
	// [    8] =  1'd0 : rslv_aipi_ref_out_sel_sub				
	// [    4] =  1'd1 : rslv_hdmi_oclk_out_sel_sub				
	// [    0] =  1'd1 : rslv_hdmi_eclk_out_sel_sub				
	sdp_set_clkrst_mux(0x005c0294, 0xffffffff, 0x00000011); // CLK_MUX_SUB_SEL_5 
	sdp_iotmode_trace;
	
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_frc_tcon_out_sel_sub_1 			
	// [   24] =  1'd0 : rslv_frc_tcon_out_sel_sub_2 			
	// [   20] =  1'd1 : rslv_hdmi_pclk_out_sel_sub				
	// [   16] =  1'd0 : rslv_dpscl_p_out_sel_sub					
	// [   12] =  1'd1 : rslv_vby1rx_p_out_sel_sub_1 			
	// [    8] =  1'd0 : rslv_vby1rx_p_out_sel_sub_2 			
	// [    4] =  1'd1 : rslv_apbif_vby1_out_sel_sub 			
	// [    0] =  1'd1 : rslv_apbif_top_out_sel_sub				
	sdp_set_clkrst_mux(0x005c0298, 0xffffffff, 0x00101011); // CLK_MUX_SUB_SEL_6 
	sdp_iotmode_trace;
	
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [    0] =  1'd1 : rslv_tsf_16x_out_sel_sub				
	sdp_set_clkrst_mux(0x005c029c, 0xffffffff, 0x00000001); // CLK_MUX_SUB_SEL_7 
	sdp_iotmode_trace;

	
	//************************************************************* 
	//	  SUBPATH Mux setting (USBPLL) For IoT					   
	//	  emmc / msp / bus / bus_sub / apbif_top / busif / dma	   
	//************************************************************* 
	// o_clk_emmc : USBPLL selection & half setting at subpath
	// [13:12] =  1'd3 : rslv_emmc_sub_pll_out_sel_1 			
	// [ 9: 8] =  8'd3 : rslv_emmc_sub_pll_out_sel_2 			
	// [    4] =  8'd0 : rslv_emmc_sub_pll_out_sel				
	// [    0] =  8'd0 : rslv_emmc_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c0420, 0xffffffff, 0x00003300); // SUB_PATH_SEL_9 
	sdp_iotmode_trace;
	
	// o_clk_msp : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_msp_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_msp_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_msp_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_msp_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_msp_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c043c, 0xffffffff, 0x00013300); // SUB_PATH_SEL_16 
	sdp_iotmode_trace;
	
	// o_clk_bus_sub : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_bus_sub_out_sel_subpath				
	// [13:12] =  1'd3 : rslv_bus_sub_sub_pll_out_sel_1			
	// [ 9: 8] =  8'd3 : rslv_bus_sub_sub_pll_out_sel_2			
	// [    4] =  8'd0 : rslv_bus_sub_sub_pll_out_sel				
	// [    0] =  8'd0 : rslv_bus_sub_sub_half_out_sel			
	sdp_set_clkrst_mux(0x005c0444, 0xffffffff, 0x00013300); // SUB_PATH_SEL_18 
	sdp_iotmode_trace;
	
	// o_clk_busif : USBPLL selection & half setting at subpath
	// [13:12] =  1'd3 : rslv_busif_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_busif_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_busif_sub_pll_out_sel				
	// [    0] =  8'd0 : rslv_busif_sub_half_out_sel 			
	sdp_set_clkrst_mux(0x005c0484, 0xffffffff, 0x00003300); // SUB_PATH_SEL_34 
	sdp_iotmode_trace;
	
	// o_clk_bus : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_bus_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_bus_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_bus_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_bus_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_bus_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c0494, 0xffffffff, 0x00013300); // SUB_PATH_SEL_38 
	sdp_iotmode_trace;
	
	// o_clk_dma : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_dma_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_dma_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_dma_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_dma_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_dma_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c049c, 0xffffffff, 0x00013300); // SUB_PATH_SEL_40 
	sdp_iotmode_trace;
	
	// o_clk_apbif_top : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_apbif_top_out_sel_subpath			
	// [13:12] =  1'd3 : rslv_apbif_top_sub_pll_out_sel_1			
	// [ 9: 8] =  8'd3 : rslv_apbif_top_sub_pll_out_sel_2			
	// [    4] =  8'd0 : rslv_apbif_top_sub_pll_out_sel			
	// [    0] =  8'd0 : rslv_apbif_top_sub_half_out_sel 		
	sdp_set_clkrst_mux(0x005c047c, 0xffffffff, 0x00003300); // SUB_PATH_SEL_32 
	sdp_iotmode_trace;
	
	
	//************************************************************* 
	//	  SUBPATH prescaler setting (USBPLL) For IoT			   
	//	  emmc / msp / bus / bus_sub / apbif_top / busif / dma	   
	//************************************************************* 
	// o_clk_emmc : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_emmc_sub_prescl_on 				
	// [ 7: 0] =  8'd  9 : rslv_emmc_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0324, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_9 
	sdp_iotmode_trace;
	
	// o_clk_msp : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_msp_sub_prescl_on					
	// [ 7: 0] =  8'd 19 : rslv_msp_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0340, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_16 
	sdp_iotmode_trace;
	
	// o_clk_bus_sub : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_bus_sub_sub_prescl_on				
	// [ 7: 0] =  8'd 19 : rslv_bus_sub_sub_prescl_val				
	sdp_set_clkrst_mux(0x005c0348, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_18 
	sdp_iotmode_trace;
	
	// o_clk_busif : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_busif_sub_prescl_on					
	// [ 7: 0] =  8'd 19 : rslv_busif_sub_prescl_val 			
	sdp_set_clkrst_mux(0x005c0388, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_34 
	sdp_iotmode_trace;
	
	// o_clk_bus : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_bus_sub_prescl_on					
	// [ 7: 0] =  8'd  9 : rslv_bus_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0398, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_38 
	sdp_iotmode_trace;
	
	// o_clk_dma : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_dma_sub_prescl_on					
	// [ 7: 0] =  8'd  9 : rslv_dma_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c03a0, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_40 
	sdp_iotmode_trace;
	
	// o_clk_dma : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_apbif_top_sub_prescl_on				
	// [ 7: 0] =  8'd 19 : rslv_apbif_top_sub_prescl_val 		
	sdp_set_clkrst_mux(0x005c0380, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_32 
	sdp_iotmode_trace;
	
	//************************************************************* 
	//	  AUD AIO clock Off For IoT 							   
	//	  TX3, RX0 clock AUDPLL source							   
	//************************************************************* 
	// [15:14] = 2'b00 : rslv_aud_src_mux_sel					
	// [13:12] = 2'b00 : rslv_aud_src_mux_sel					
	// [11:10] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 9: 8] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 7: 6] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 5: 4] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 3: 2] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 1: 0] = 2'b00 : rslv_aud_src_mux_sel					
	sdp_set_clkrst_mux(0x005c05a4, 0xffffffff, 0x00000000); // AUD_SRC_MUX_SEL 
	sdp_iotmode_trace;


	//************************************************************* 
	//	  Main Clock Mux change (glitchless) For IoT			   
	//************************************************************* 
	// o_clk_27 (main) : Off
	// o_clk_27 (sub ) : Off
	// o_clk_hclk_peri : main selection (COREPLL0 /8 	= 250MHz
	// o_clk_dma 	  : sub  selection (USBPLL	 /2 /10 = 100MHz
	// o_clk_msp 	  : sub  selection (USBPLL	 /2 /20 =  50MHz
	// o_clk_gpu 	  : Off
	// o_clk_bus 	  : sub  selection (USBPLL	 /2 /10 = 100MHz
	// [   12] =  1'd0 : rslv_dma_out_sel_main			
	// [    8] =  1'd0 : rslv_msp_out_sel_main			
	// [    4] =  1'd0 : rslv_gpu_out_sel_main			
	// [    0] =  1'd0 : rslv_bus_out_sel_main			
	sdp_set_clkrst_mux(0x005c0250, 0xffffffff, 0x11110000); // CLK_MUX_MAIN_SEL_0 
	sdp_iotmode_trace;
	
	// o_clk_mfd 	: Off
	// o_clk_jpeg	: Off
	// o_clk_aio 	: Off
	// o_clk_busif	: sub  selection (USBPLL   /2 /20 =  50MHz
	// o_clk_freq	: Off
	// o_promise_clk : Off
	// o_clk_emmc	: sub  selection (USBPLL   /2 /10 = 100MHz
	// o_clk_tsf_16x : Off
	// [   28] =  1'd0 : rslv_mfd_out_sel_main			
	// [   24] =  1'd0 : rslv_jpeg_out_sel_main			
	// [   20] =  1'd0 : rslv_aio_out_sel_main			
	// [   16] =  1'd0 : rslv_busif_out_sel_main 		
	// [   12] =  1'd0 : rslv_freq_check_out_sel_main		
	// [    8] =  1'd0 : rslv_promise_out_sel_main		
	// [    4] =  1'd0 : rslv_emmc_out_sel_main			
	// [    0] =  1'd0 : rslv_tsf_16x_out_sel_main		
	sdp_set_clkrst_mux(0x005c0258, 0xffffffff, 0x00000000); // CLK_MUX_MAIN_SEL_2 
	sdp_iotmode_trace;
	
	// o_clk_ctr 	: Off
	// o_clk_tdsp	: Off
	// o_clk_ga2d	: Off
	// o_clk_gfxp_c	: Off
	// o_clk_dvde	: Off
	// o_clk_dpscl_b : Off
	// o_clk_frc_bus : Off
	// o_clk_srp 	: Off
	// [   28] =  1'd0 : rslv_ctr_out_sel_main			
	// [   24] =  1'd0 : rslv_tdsp_out_sel_main			
	// [    0] =  1'd0 : rslv_srp_out_sel_main			
	//sdp_set_clkrst_mux(0x005c025c, 0xffffffff, 0x00111110); // CLK_MUX_MAIN_SEL_3 
	sdp_set_clkrst_mux(0x005c025c, 0xffffffff, 0x00000000); // CLK_MUX_MAIN_SEL_3 //yongjin
	sdp_iotmode_trace;


	// o_clk_bus_sub : sub  selection (USBPLL   /2 /20 =  50MHz
	// o_clk_dmicom	: Off
	// o_clk_dp_o	: Off
	// o_clk_dpscl_w : Off
	// o_clk_avd 	: Off
	// o_clk_henc	: Off
	// o_clk_dp_nrfc : Off
	// [   28] =  1'd0 : rslv_bus_sub_out_sel_main		
	sdp_set_clkrst_mux(0x005c0260, 0xffffffff, 0x00111111); // CLK_MUX_MAIN_SEL_4 
	sdp_iotmode_trace;

	// o_clk_frc_tcon   : Off
	// o_clk_hdmi_pclk  : Off
	// o_clk_dpscl_p    : Off
	// o_clk_vby1rx_p   : Off
	// o_clk_apbif_vby1 : Off
	// o_clk_apbif_top  : sub  selection (USBPLL   /2 /20 =	50MHz
	// o_clk_hdmi_oclk  : Off
	// o_clk_hdmi_eclk  : Off
	// [   24] =  1'd0 : rslv_hdmi_pclk_out_sel_main 	
	// [   12] =  1'd0 : rslv_apbif_vby1_out_sel_main		
	// [    8] =  1'd0 : rslv_apbif_top_out_sel_main 	
	// [    4] =  1'd0 : rslv_hdmi_oclk_out_sel_main 	
	// [    0] =  1'd0 : rslv_hdmi_eclk_out_sel_main 	
	//sdp_set_clkrst_mux(0x005c0264, 0xffffffff, 0x10100000); // CLK_MUX_MAIN_SEL_5 
	sdp_set_clkrst_mux(0x005c0264, 0xffffffff, 0x10101000); // CLK_MUX_MAIN_SEL_5 //yongjin
	sdp_iotmode_trace;

	// msp, bus_sub, bus, dma, hclk_peri for no glitch
	// [   24] =  1'd1 : rslv_dma_out_sel_sub					
	// [   20] =  1'd1 : rslv_msp_out_sel_sub					
	// [    8] =  1'd1 : rslv_bus_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0280, 0xffffffff, 0x01101111); // CLK_MUX_SUB_SEL_0 
	sdp_iotmode_trace;
	
	// msp, bus_sub, bus, dma, hclk_peri for no glitch
	// [    0] =  1'd1 : rslv_bus_sub_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0290, 0xffffffff, 0x10000011); // CLK_MUX_SUB_SEL_4 
	sdp_iotmode_trace;

	//************************************************************* 
	//	  PLL Off For IoT										   
	//	  PULL0 / PULL1 / AUD0 / AUD1 / CORE1 / DP / VBY1 / FRC    
	//************************************************************* 
	// [   20] =  1'd0 : rslv_pll_pullpll0_reset_n		
	// [   16] =  1'd0 : rslv_pll_pullpll1_reset_n		
	// [   12] =  1'd0 : rslv_pll_audpll0_reset_n			
	// [    8] =  1'd0 : rslv_pll_audpll1_reset_n			
	// [    0] =  1'd0 : rslv_pll_corepll1_reset_n		
	sdp_set_clkrst_mux(0x005c0234, 0xffffffff, 0x00000011); // PLL_RESETN_CTRL_1 
	sdp_iotmode_trace;

	// [   16] =  1'd0 : rslv_pll_dppll1_reset_n 		
	// [   12] =  1'd0 : rslv_pll_vby1pll_reset_n			
	// [    4] =  1'd0 : rslv_pll_frcpll_reset_n 		
	sdp_set_clkrst_mux(0x005c0238, 0xffffffff, 0x00000101); // PLL_RESETN_CTRL_2 
	sdp_iotmode_trace;
}

void jazz_m_clk_rst_iot_off(void)
{
	printk(KERN_INFO "jazz_m_clk_rst_iot_off\n");
	
	//************************************************************* 
	//     PLL On For IoT                                           
	//     PULL0 / PULL1 / VBY1 / FRC                               
	//************************************************************* 
	// [   20] =  1'd1 : rslv_pll_pullpll0_reset_n		
	// [   16] =  1'd1 : rslv_pll_pullpll1_reset_n		
	sdp_set_clkrst_mux(0x005c0234, 0xffffffff, 0x00110011); // PLL_RESETN_CTRL_1 
	sdp_iotmode_trace;

	// [   12] =  1'd1 : rslv_pll_vby1pll_reset_n			
	// [    4] =  1'd1 : rslv_pll_frcpll_reset_n			
	sdp_set_clkrst_mux(0x005c0238, 0xffffffff, 0x00001111); // PLL_RESETN_CTRL_2 
	sdp_iotmode_trace;

	//************************************************************* 
	//     WAIT For PLL LOCKING                                     
	//************************************************************* 
	msleep(200);
	//************************************************************* 
	//     PLL On For IoT                                           
	//     AUD0 / AUD1 / DP                                         
	//************************************************************* 
	// [   12] =  1'd1 : rslv_pll_audpll0_reset_n			
	// [    8] =  1'd1 : rslv_pll_audpll1_reset_n			
	sdp_set_clkrst_mux(0x005c0234, 0xffffffff, 0x00111111); // PLL_RESETN_CTRL_1 
	sdp_iotmode_trace;

	// [   16] =  1'd1 : rslv_pll_dppll1_reset_n			
	sdp_set_clkrst_mux(0x005c0238, 0xffffffff, 0x00011111); // PLL_RESETN_CTRL_2 
	sdp_iotmode_trace;

	//************************************************************* 
	//     WAIT For PLL LOCKING                                     
	//************************************************************* 
	msleep(200);
	//************************************************************* 
	//     AUD AIO clock On For IoT                                 
	//     TX3, RX0 clock Crystal source                            
	//************************************************************* 
	// [15:14] = 2'b00 : rslv_aud_src_mux_sel					
	// [13:12] = 2'b00 : rslv_aud_src_mux_sel					
	// [11:10] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 9: 8] = 2'b10 : rslv_aud_src_mux_sel					
	// [ 7: 6] = 2'b10 : rslv_aud_src_mux_sel					
	// [ 5: 4] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 3: 2] = 2'b00 : rslv_aud_src_mux_sel					
	// [ 1: 0] = 2'b00 : rslv_aud_src_mux_sel					
	sdp_set_clkrst_mux(0x005c05a4, 0xffffffff, 0x00000280); // AUD_SRC_MUX_SEL 
	sdp_iotmode_trace;

	//************************************************************* 
	//     Main Clock Mux change (glitchless) For Normal            
	//************************************************************* 
	// o_clk_27 (main) : main selection (PULLPLL0 /66 =  27.0MHz)
	// o_clk_27 (sub ) : main selection (PULLPLL1 /52 =  27.0MHz)
	// o_clk_hclk_peri : main selection (COREPLL0 /8  = 250.0MHz)
	// o_clk_dma       : main selection (COREPLL0 /4  = 500.0MHz)
	// o_clk_msp       : main selection (VBY1PLL  /2  = 297.0MHz)
	// o_clk_gpu       : main selection (COREPLL0 /4  = 500.0MHz)
	// o_clk_bus       : main selection (PULLPLL0 /4  = 445.5MHz)
	// [   12] =  1'd1 : rslv_dma_out_sel_main			
	// [    8] =  1'd1 : rslv_msp_out_sel_main			
	// [    4] =  1'd1 : rslv_gpu_out_sel_main			
	// [    0] =  1'd1 : rslv_bus_out_sel_main			
	sdp_set_clkrst_mux(0x005c0250, 0xffffffff, 0x11111111); // CLK_MUX_MAIN_SEL_0 
	sdp_iotmode_trace;

	// o_clk_mfd     : main selection (COREPLL0 /6     = 333.3MHz)
	// o_clk_jpeg    : main selection (COREPLL0 /6     = 333.3MHz)
	// o_clk_aio     : main selection (COREPLL0 /6     = 333.3MHz)
	// o_clk_busif   : main selection (COREPLL0 /6     = 333.3MHz)
	// o_clk_freq    : main selection (COREPLL0 /4     = 500.0MHz)
	// o_promise_clk : main selection (COREPLL0 /2 /10 = 100.0MHz)
	// o_clk_emmc    : main selection (PULLPLL1 /2     = 702.0MHz)
	// o_clk_tsf_16x : main selection (COREPLL0 /6     = 333.3MHz)
	// [   28] =  1'd1 : rslv_mfd_out_sel_main			
	// [   24] =  1'd1 : rslv_jpeg_out_sel_main			
	// [   20] =  1'd1 : rslv_aio_out_sel_main			
	// [   16] =  1'd1 : rslv_busif_out_sel_main			
	// [   12] =  1'd1 : rslv_freq_check_out_sel_main		
	// [    8] =  1'd1 : rslv_promise_out_sel_main		
	// [    4] =  1'd1 : rslv_emmc_out_sel_main			
	// [    0] =  1'd1 : rslv_tsf_16x_out_sel_main		
	sdp_set_clkrst_mux(0x005c0258, 0xffffffff, 0x11111111); // CLK_MUX_MAIN_SEL_2 
	sdp_iotmode_trace;

	// o_clk_ctr     : main selection (COREPLL0 /5 = 400.0MHz)
	// o_clk_tdsp    : main selection (COREPLL0 /5 = 400.0MHz)
	// o_clk_ga2d    : main selection (PULLPLL0 /4 = 445.5MHz)
	// o_clk_gfxp_c  : main selection (PULLPLL0 /4 = 445.5MHz)
	// o_clk_dvde    : main selection (PULLPLL0 /4 = 445.5MHz)
	// o_clk_dpscl_b : main selection (PULLPLL0 /4 = 445.5MHz)
	// o_clk_frc_bus : main selection (PULLPLL0 /4 = 445.5MHz)
	// o_clk_srp     : main selection (COREPLL0 /4 = 500.0MHz)
	// [   28] =  1'd1 : rslv_ctr_out_sel_main			
	// [   24] =  1'd1 : rslv_tdsp_out_sel_main			
	// [    0] =  1'd1 : rslv_srp_out_sel_main			
	sdp_set_clkrst_mux(0x005c025c, 0xffffffff, 0x11111111); // CLK_MUX_MAIN_SEL_3 
	sdp_iotmode_trace;

	// o_clk_bus_sub : main selection (VBY1PLL /2 = 297.0MHz)
	// o_clk_dmicom  : low speed clk  (XTAL       =  24.5MHz)
	// o_clk_dp_o    : main selection (DPPLL1  /2 = 540.0MHz)
	// o_clk_dpscl_w : main selection (PULLPLL1/2 = 702.0MHz)
	// o_clk_avd     : main selection (VBY1PLL /2 = 297.0MHz)
	// o_clk_henc    : main selection (VBY1PLL /2 = 297.0MHz)
	// o_clk_dp_nrfc : main selection (VBY1PLL /1 = 594.0MHz)
	// o_clk_aipi(x) : main selection (VBY1PLL /8 =  74.2MHz)
	// [   28] =  1'd1 : rslv_bus_sub_out_sel_main		
	sdp_set_clkrst_mux(0x005c0260, 0xffffffff, 0x11111111); // CLK_MUX_MAIN_SEL_4
	sdp_iotmode_trace;

	// o_clk_frc_tcon   : main selection (FRCPLL   /1      = 300.0MHz)
	// o_clk_hdmi_pclk  : main selection (COREPLL0 /1      =  55.5MHz)
	// o_clk_dpscl_p    : main selection (VBY1PLL  /1      = 594.0MHz)
	// o_clk_vby1rx_p   : sub  selection (VBY1PLL  /8      =  74.2MHz)
	// o_clk_apbif_vby1 : main selection (COREPLL0 /2 /20  =  50.0MHz)
	// o_clk_apbif_top  : main selection (COREPLL0 /2 /4   = 250.0MHz)
	// o_clk_hdmi_oclk  : main selection (COREPLL0 /2 /500 =   2.0MHz)
	// o_clk_hdmi_eclk  : main selection (COREPLL0 /2 /50  =  20.0MHz)
	// [   24] =  1'd1 : rslv_hdmi_pclk_out_sel_main		
	// [   12] =  1'd1 : rslv_apbif_vby1_out_sel_main		
	// [    8] =  1'd1 : rslv_apbif_top_out_sel_main		
	// [    4] =  1'd1 : rslv_hdmi_oclk_out_sel_main		
	// [    0] =  1'd1 : rslv_hdmi_eclk_out_sel_main		
	sdp_set_clkrst_mux(0x005c0264, 0xffffffff, 0x11101111); // CLK_MUX_MAIN_SEL_5 
	sdp_iotmode_trace;

	// msp, bus_sub, bus, dma, hclk_peri dont use subpath
	// [   24] =  1'd0 : rslv_dma_out_sel_sub					
	// [   20] =  1'd0 : rslv_msp_out_sel_sub					
	// [    8] =  1'd0 : rslv_bus_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0280, 0xffffffff, 0x00001011); // CLK_MUX_SUB_SEL_0 
	sdp_iotmode_trace;

	// msp, bus_sub, bus, dma, hclk_peri dont use subpath
	// [    0] =  1'd0 : rslv_bus_sub_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0290, 0xffffffff, 0x10000010); // CLK_MUX_SUB_SEL_4 
	sdp_iotmode_trace;

	//************************************************************* 
	//     SUBPATH Off For Normal                                   
	//************************************************************* 
	// All Clock sub path Off without USB
	// [   28] =  1'd0 : rslv_hclk_peri_out_sel_sub			
	// [   24] =  1'd0 : rslv_dma_out_sel_sub					
	// [   20] =  1'd0 : rslv_msp_out_sel_sub					
	// [   16] =  1'd0 : rslv_gpu_out_sel_sub_1				
	// [   12] =  1'd0 : rslv_gpu_out_sel_sub_2				
	// [    8] =  1'd0 : rslv_bus_out_sel_sub					
	// [    4] =  1'd0 : rslv_mfd_out_sel_sub					
	// [    0] =  1'd0 : rslv_jpeg_out_sel_sub 				
	sdp_set_clkrst_mux(0x005c0280, 0xffffffff, 0x00000000); // CLK_MUX_SUB_SEL_0 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_freq_check_out_sel_sub				
	// [   24] =  1'd0 : rslv_promise_out_sel_sub					
	// [   20] =  1'd0 : rslv_emmc_out_sel_sub					
	// [   16] =  1'd0 : rslv_ctr_out_sel_sub						
	// [   12] =  1'd0 : rslv_tdsp_out_sel_sub					
	// [    8] =  1'd0 : rslv_aio_out_sel_sub_1					
	// [    4] =  1'd0 : rslv_aio_out_sel_sub_2					
	// [    0] =  1'd0 : rslv_dmicom_out_sel_sub					
	sdp_set_clkrst_mux(0x005c028c, 0xffffffff, 0x00000000); // CLK_MUX_SUB_SEL_3 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_busif_out_sel_sub					
	// [   24] =  1'd0 : rslv_ga2d_out_sel_sub					
	// [   20] =  1'd0 : rslv_gfxp_c_out_sel_sub					
	// [   16] =  1'd0 : rslv_dvde_out_sel_sub					
	// [   12] =  1'd0 : rslv_dpscl_b_out_sel_sub					
	// [    8] =  1'd0 : rslv_frc_bus_out_sel_sub					
	// [    4] =  1'd0 : rslv_srp_out_sel_sub						
	// [    0] =  1'd0 : rslv_bus_sub_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0290, 0xffffffff, 0x00000000); // CLK_MUX_SUB_SEL_4 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_dp_o_out_sel_sub					
	// [   24] =  1'd0 : rslv_dpscl_w_out_sel_sub					
	// [   20] =  1'd0 : rslv_avd_out_sel_sub						
	// [   16] =  1'd0 : rslv_henc_out_sel_sub					
	// [   12] =  1'd0 : rslv_dp_nrfc_out_sel_sub					
	// [    8] =  1'd0 : rslv_aipi_ref_out_sel_sub				
	// [    4] =  1'd0 : rslv_hdmi_oclk_out_sel_sub				
	// [    0] =  1'd0 : rslv_hdmi_eclk_out_sel_sub				
	sdp_set_clkrst_mux(0x005c0294, 0xffffffff, 0x00000000); // CLK_MUX_SUB_SEL_5 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [   28] =  1'd0 : rslv_frc_tcon_out_sel_sub_1				
	// [   24] =  1'd0 : rslv_frc_tcon_out_sel_sub_2				
	// [   20] =  1'd0 : rslv_hdmi_pclk_out_sel_sub				
	// [   16] =  1'd0 : rslv_dpscl_p_out_sel_sub					
	// [   12] =  1'd1 : rslv_vby1rx_p_out_sel_sub_1				
	// [    8] =  1'd0 : rslv_vby1rx_p_out_sel_sub_2				
	// [    4] =  1'd0 : rslv_apbif_vby1_out_sel_sub				
	// [    0] =  1'd0 : rslv_apbif_top_out_sel_sub				
	sdp_set_clkrst_mux(0x005c0298, 0xffffffff, 0x00011000); // CLK_MUX_SUB_SEL_6 
	sdp_iotmode_trace;

	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [    0] =  1'd0 : rslv_tsf_16x_out_sel_sub				
	sdp_set_clkrst_mux(0x005c029c, 0xffffffff, 0x00000000); // CLK_MUX_SUB_SEL_7 
	sdp_iotmode_trace;

	sdp_set_clkrst_mux(0x005C0734, 0xffffffff, 0x11111111);
	sdp_set_clkrst_mux(0x005C0738, 0xffffffff, 0x10000011);
	sdp_set_clkrst_mux(0x005C073c, 0xffffffff, 0x00000011);
	sdp_set_clkrst_mux(0x005C0740, 0xffffffff, 0x01111101);
	sdp_set_clkrst_mux(0x005C0744, 0xffffffff, 0x01111111);
	sdp_set_clkrst_mux(0x005C0748, 0xffffffff, 0x00110111);
	sdp_set_clkrst_mux(0x005C074c, 0xffffffff, 0x00011111);
	sdp_set_clkrst_mux(0x005C0760, 0xffffffff, 0x00111111);
	//sdp_set_clkrst_mux(0x005C0764, 0xffffffff, 0x00000000);

	dump_clkrst_regs();
}

int sdp_iotmode_clkrst_down(bool en)
{
	if (en) {
		jazz_m_clk_rst_iot_on();
	}
	else {
		jazz_m_clk_rst_iot_off();
	}
	return 0;
}
int sdp_iotmode_ddrfreq_set(int freqmode) // 0:1250Mhz, 2:312Mhz, 3:156Mhz
{
	return 0;	
}
int sdp_iotmode_powerdown(bool en)
{
	int ret, level;
	
	printk(KERN_INFO "sdp_iotmode_powerdown(%d).\n", en);
	if (en) {
		sdp_iotmode_clkrst_down(true);
		sdp_iotmode_ddrfreq_set(3);
		ret = sdp_cpufreq_get_level(100000, &level);
		if (!ret) {
			sdp_cpufreq_limit(DVFS_LOCK_ID_PM, level);
		}
	}
	else {
		sdp_cpufreq_limit_free(DVFS_LOCK_ID_PM);
		sdp_iotmode_ddrfreq_set(0);
		sdp_iotmode_clkrst_down(false);
	}
	return 0;
}
#else
int sdp_iotmode_powerdown(bool en)
{
	printk(KERN_INFO "sdp_iotmode_powerdown() is not implemented.\n");

	return 0;
}
#endif

EXPORT_SYMBOL(sdp_iotmode_powerdown);

