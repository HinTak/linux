#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <soc/sdp/soc.h>
#include <mach/map.h>
#include <mach/sdp_smp.h>

#ifdef CONFIG_SDP_HW_CLOCK
#include <mach/sdp_hwclock.h>
#endif

#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>
#include "sdp_micom.h"
#ifdef CONFIG_ARM_PSCI
#include <asm/psci.h>
#endif


/**
 * These are ugly custom interfaces. Should be refined in future.
 */
 
/**
 * sdp_pm_setmode / sdp_pm_getmode - get/set ewbs mode.
 *
 * @mode: 0=normal(defulat), 1=ewbs
 *
 * these are defined to differentiate "ewbs" suspend from normal suspend.
 * the micom module will set this mode.
 * and other drivers will get this mode to decide their behavior.
 */
static int sdp_pm_mode = 0;
int sdp_pm_setmode(int mode) 
{
	sdp_pm_mode = mode;
	return 0;
}
EXPORT_SYMBOL(sdp_pm_setmode);

int sdp_pm_getmode(void)
{
	return sdp_pm_mode;
}
EXPORT_SYMBOL(sdp_pm_getmode);

/**
 * sdp_pm_iot_fn/ sdp_pm_ewbs_fn - iot / ewbs powerdown function.
 *
 * driver will implement and assign the body function.
 */
void (*sdp_pm_ewbs_fn)(void);
EXPORT_SYMBOL(sdp_pm_ewbs_fn);
 
int (*sdp_pm_iot_fn)(bool);
EXPORT_SYMBOL(sdp_pm_iot_fn);

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);
static void sdp1601_iotmode_n_unmask_ext(void) // TODO : should be removed
{
	/* for frc */
	sdp_set_clkrst_mux(0x00FC05A8, 0x00100000, 0x00100000); 	//TCON Clock off
	sdp_set_clkrst_mux(0x00FC05A4, 0x00011111, 0x00011111); 	//FRC Clock off
	sdp_set_clkrst_mux(0x00FC05F8, 0x00011000, 0x00011000); 	//TCON reset off
	sdp_set_clkrst_mux(0x00FC08B0, 0x00010000, 0x00010000); 	//TCON bus_if reset off
	sdp_set_clkrst_mux(0x00FC08A4, 0x00001111, 0x00001111); 	//FRC bus_if reset off
	sdp_set_clkrst_mux(0x00FC05F4, 0x00000011, 0x00000011); 	//FRC reset off

	/* for gpu */
	sdp_set_clkrst_mux(0x00FC05AC, 0x00000001, 0x1<<0);
	sdp_set_clkrst_mux(0x00FC08A8, 0x00010000, 0x1<<16);
	sdp_set_clkrst_mux(0x00FC05FC, 0x00000010, 0x1<<4);
	sdp_set_clkrst_mux(0x00FC05FC, 0x00000001, 0x1<<0);

	/* for dmic */
	sdp_set_clkrst_mux(0x00FC01B8, 0x10, 0x10);
	sdp_set_clkrst_mux(0x00FC0368, 0x300, 0x100);
	sdp_set_clkrst_mux(0x00FC02A8, 0xFF, 0x7);
	mdelay(1);
	sdp_set_clkrst_mux(0x00FC01B8, 0x10, 0x0);
	sdp_set_clkrst_mux(0x00FC08A4, 0x01000000, 0x01000000); //Reset : Off . On.
	sdp_set_clkrst_mux(0x00FC07B0, 0x00000001, 0x00000000);// Request IDLE.

	sdp_set_clkrst_mux(0x00FC07C8, 0x00000001, 0x00000000); 	//FRC0 BUF_IF off
	sdp_set_clkrst_mux(0x00FC07D0, 0x00000001, 0x00000000); 	//FRC1 BUS_IF off
	sdp_set_clkrst_mux(0x00FC07D8, 0x00000001, 0x00000000); 	//FRC2 BUS_IF off
	mdelay(1);
}

/**
 * sdp_iotmode_powerdown - post procedure in entering iot mode.
 *
 * @en: ture = power down, false = power on
 *
 * kerenl will call this after all devices are suspended.
 */
int sdp_iotmode_powerdown(bool en)
{
	if (sdp_pm_iot_fn) { // new style power down.
		sdp_pm_iot_fn(en);
	}
	else { // old style power down : to be unused.
		if (!en) {
			if (soc_is_sdp1601())
				sdp1601_iotmode_n_unmask_ext();
		}
	}
	
	printk(KERN_INFO "sdp_iotmode_powerdown(%d).\n", en);
	return 0;
}
EXPORT_SYMBOL(sdp_iotmode_powerdown);


extern void sdp1404_wait_for_die(void);
extern void sdp1406uhd_wait_for_die(void);
extern void sdp1406fhd_wait_for_die(void);

extern void sdp1501_wait_for_die(void);
extern void sdp1531_wait_for_die(void);

struct sdp_suspend_save {
	u32	entry_point;
	u32	ctx[1];	/* overide this */
};

static struct sdp_suspend_save *sdp_sarea;

static int sdp_read_mmc_aligned(char *name, u32 *data, int part_size, int size, u8 align_bits)
{
	struct file *fp;
	int ret;
	mm_segment_t old_fs = get_fs();
	int aligned_size = size;

	set_fs(KERNEL_DS);

	fp = filp_open(name, O_RDONLY, 0);

	if(IS_ERR(fp))
	{
		pr_err("suspend : Cannot open %s for DDR param save!!\n", name);
		return -1;
	}

	if (align_bits) {
		u32 mask = (1UL << align_bits) - 1;
		aligned_size = (int) (((u32) size + mask) & (~mask));
	}

	BUG_ON(aligned_size > part_size || size > part_size);

	ret = (int) vfs_llseek(fp, part_size - aligned_size, SEEK_SET);
	if(ret <= 0)
	{
		pr_err("suspend : Error in vfs_llseek!!!\n");
	}

	vfs_read(fp, (char *) data, (size_t) size, &fp->f_pos);
	filp_close(fp, NULL);

	set_fs(old_fs);

	return 0;
}

static int sdp_read_mmc(char *name, u32 *data, int part_size, int size)
{
	return sdp_read_mmc_aligned(name, data, part_size, size, 0);
}

static int sdp_write_mmc_aligned(char *name, u32 *data, int part_size, int size, u8 align_bits)
{
	struct file *fp;
	mm_segment_t old_fs = get_fs();
	int ret;
	int aligned_size = size;

	set_fs(KERNEL_DS);

	fp = filp_open(name, O_WRONLY, 0);

	if(IS_ERR(fp))	{
		pr_err("suspend : Cannot open %s for DDR param save!!\n", name);
		return -1;
	}

	if (align_bits) {
		u32 mask = (1UL << align_bits) - 1;
		aligned_size = (int) (((u32) size + mask) & (~mask));
	}

	BUG_ON(aligned_size > part_size || size > part_size);

	ret = (int) vfs_llseek(fp, part_size - aligned_size, SEEK_SET);
	if(ret <= 0)    {
                pr_err("suspend : Error in vfs_llseek!!!\n");
        }

	ret = vfs_write(fp, (char *) data, (size_t) size, &fp->f_pos);
	if(ret <= 0)	{
		pr_err("suspend : Error in vfs_write!!!\n");
	}
	ret = vfs_fsync(fp, 0);

	filp_close(fp, NULL);

	set_fs(old_fs);

	return 0;
}

static int sdp_write_mmc(char *name, u32 *data, int part_size, int size)
{
	return sdp_write_mmc_aligned(name, data, part_size, size, 0);
}

static unsigned int sdp_uart_base;
static int sdp_micom_port;

void sdp_Micomcmd(u8 cmd);

void sdp_Micomcmd(u8 cmd)
{
	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);
	sdp_micom_send_byte_cmd(cmd);
}

static void sdp_poweroff(void)
{
	int i;

	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);

	for(i = 0; i < 20 ; i++)
	{
		printk(KERN_ERR "\n\n<<<<<< send power off cmd to micom (requested:%s) >>>>>>\n\n\n", current->comm);
		sdp_micom_request_poweroff();
		mdelay(100);
	}
}

#define SDP1404_MICOM_PORT	2

void *sdp1404_gpio_regs = 0;

static int sdp1404_suspend_begin(suspend_state_t state)
{
	return 0;
}

static int notrace __sdp1404_suspend_enter(unsigned long unused)
{
	BUG_ON(!sdp_sarea);

	/* save resume address */
	sdp_sarea->entry_point =(u32)virt_to_phys(cpu_resume);

	/* send micom suspend off */
	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);
	//sdp_micom_request_suspend();

	soft_restart((unsigned long)virt_to_phys(sdp1404_wait_for_die));

	return 0;
}

#if 0//def CONFIG_SDP_MESSAGEBOX
extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);
#endif

static int sdp1406_suspend_begin(suspend_state_t state)
{
	return 0;
}

static int notrace __sdp1406_suspend_enter(unsigned long unused)
{
#if 0//def CONFIG_SDP_MESSAGEBOX
	u8 buff[12] = {0xFF, 0xFF, 0xD3, 0x00, 0x00, 0x00,
			0x00, 0x00, 0xD3, 0x00, 0x00, 0x00};
	int timeout = 100;
#endif
	BUG_ON(!sdp_sarea);

	/* save resume address */
	sdp_sarea->entry_point = (u32)virt_to_phys(cpu_resume);

#if 0//def CONFIG_SDP_MESSAGEBOX
	/* send suspend off to internal micom's msgbox  */
	/* 0xFF 0xFF 0xD3 0x00 0x00 0x00 0x00 0x00 0xD3 (9 byte) */
	do {
		if (sdp_messagebox_write(buff, 9) == 9)
			break;
		udelay(100);
	} while(timeout--);

	if (!timeout)
		pr_err("error: failed to get sdp message box's response\n");
#endif

	if (sdp_soc() == SDP1406FHD_CHIPID)
		soft_restart((unsigned long)virt_to_phys(sdp1406fhd_wait_for_die));
	else
		soft_restart((unsigned long)virt_to_phys(sdp1406uhd_wait_for_die));

	return 0;
}

/*******************************
 * SDP1501 SDP1531 Jazz-M/L
 *******************************/
#define SDP1501_AON_ADDR	(0x7c8000)
#define SDP1501_AON_SIZE	(0x400)
#define SDP1501_AON_MAGIC	(0x15010929)

static void __iomem * sdp1501_ddr_phy[5];	/* 5 channels */

const u32 sdp1501_ddr_phy_bases[5] =
	{ 0xf10000, 0xf20000, 0xf30000, 0xf40000, 0xf50000 };
static const u32 sdp1501_ddr_phy_regs[] = {
	/* gate train */
	0x0F0, 0x0F4, 0x0FC,

	/* read train */
	0x18C, 0x190, 0x19C, 0x1A8,
	0x1B4, 0x1C0, 0x1CC, 0x1D8,
	0x1E4, 0x580,
	
	/* write train */
	0x1F0, 0x1FC, 0x208, 0x214,
	0x220, 0x22C, 0x238, 0x244,
	0x250,
	0x490, 0x49C, 0x4A8, 0x4B4,
	0x4C0, 0x4CC, 0x4D8, 0x4E4,
	0x4F0,

	/* ??? */
	0xB4,
};

static void sdp1501_suspend_init(void)
{
	int i;

	sdp_sarea = ioremap(SDP1501_AON_ADDR, SDP1501_AON_SIZE);
	BUG_ON(!sdp_sarea);

	for (i = 0; i < ARRAY_SIZE(sdp1501_ddr_phy_bases); i++) {
		sdp1501_ddr_phy[i] = ioremap(sdp1501_ddr_phy_bases[i], 0x1000);
		BUG_ON(!sdp1501_ddr_phy[i]);
	}
}

static u32 sdp1501_read_phy_reg(int ch, int offset)
{
	void __iomem *base = sdp1501_ddr_phy[ch];

	/* jazz-l has no A/B channel, should not access at all their slaves */
	if (soc_is_jazzl() && (ch < 2))
		return 0xffffffff;
	else
		return readl_relaxed(base + offset);
}

static int sdp1501_suspend_begin(suspend_state_t state)
{
	u32 *ptr = &sdp_sarea->ctx[0];
	u32 *ddr_ptr;
	int i, j, n;

	sdp_sarea->entry_point = (u32)virt_to_phys(cpu_resume);

	/* save ddr context */

	/* ddr context header = 12 bytes */
	ptr[0] = SDP1501_AON_MAGIC;
	ptr[1] = 0;	/* body length (N words)*/
	ptr[2] = 0;	/* reserved0 */

	ddr_ptr = &ptr[3];
	
	/* ddr context body */
	for (i = 0, n = 0; i < ARRAY_SIZE(sdp1501_ddr_phy_bases); i++) {
		for (j = 0; j < ARRAY_SIZE(sdp1501_ddr_phy_regs); j++) {
			int offset = sdp1501_ddr_phy_regs[j];
			ddr_ptr[n] = sdp1501_read_phy_reg(i, offset);
			n++;
		}
	}
	ptr[1] = n;	/* size of context */

#if 0
	pr_info("sdp1501 running context:\n");
	pr_info("%08x\n", ptr[0]);
	pr_info("%08x\n", ptr[1]);
	pr_info("%08x\n", ptr[2]);

	pr_info("------------------------------\n");

	for (i = 0; i < n; i++) {
		pr_info("%08x\n", ddr_ptr[i]);
	}
#endif

	return 0;
}

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);

static void sdp1501_pm_set_ewbs_demdulator(void)
{
	u32 base = SFR_VA + 0x00b50000 - 0x00100000;
	writel(0x30000000, (void*)(base+0x1604)); // set demodulator not to use ddr

	pr_info("[soc-ewbs] set demodulator not to use ddr\n");
}
static void sdp1501_pm_set_ewbs_powergating(void)
{
	pr_info("[soc-ewbs] sdp_pm_set_ewbs_powergating\n");

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
	
	// All Clock sub path without msp, bus, bus_sub, dma, hclk_peri & PLL Off clock
	// [    0] =  1'd1 : rslv_tsf_16x_out_sel_sub				
	sdp_set_clkrst_mux(0x005c029c, 0xffffffff, 0x00000001); // CLK_MUX_SUB_SEL_7 

	
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
	
	// o_clk_msp : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_msp_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_msp_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_msp_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_msp_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_msp_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c043c, 0xffffffff, 0x00013300); // SUB_PATH_SEL_16 
	
	// o_clk_bus_sub : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_bus_sub_out_sel_subpath				
	// [13:12] =  1'd3 : rslv_bus_sub_sub_pll_out_sel_1			
	// [ 9: 8] =  8'd3 : rslv_bus_sub_sub_pll_out_sel_2			
	// [    4] =  8'd0 : rslv_bus_sub_sub_pll_out_sel				
	// [    0] =  8'd0 : rslv_bus_sub_sub_half_out_sel			
	sdp_set_clkrst_mux(0x005c0444, 0xffffffff, 0x00013300); // SUB_PATH_SEL_18 
	
	// o_clk_busif : USBPLL selection & half setting at subpath
	// [13:12] =  1'd3 : rslv_busif_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_busif_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_busif_sub_pll_out_sel				
	// [    0] =  8'd0 : rslv_busif_sub_half_out_sel 			
	sdp_set_clkrst_mux(0x005c0484, 0xffffffff, 0x00003300); // SUB_PATH_SEL_34 
	
	// o_clk_bus : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_bus_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_bus_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_bus_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_bus_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_bus_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c0494, 0xffffffff, 0x00013300); // SUB_PATH_SEL_38 
	
	// o_clk_dma : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_dma_out_sel_subpath					
	// [13:12] =  1'd3 : rslv_dma_sub_pll_out_sel_1				
	// [ 9: 8] =  8'd3 : rslv_dma_sub_pll_out_sel_2				
	// [    4] =  8'd0 : rslv_dma_sub_pll_out_sel					
	// [    0] =  8'd0 : rslv_dma_sub_half_out_sel				
	sdp_set_clkrst_mux(0x005c049c, 0xffffffff, 0x00013300); // SUB_PATH_SEL_40 
	
	// o_clk_apbif_top : USBPLL selection & half setting at subpath
	// [   16] =  1'd1 : rslv_apbif_top_out_sel_subpath			
	// [13:12] =  1'd3 : rslv_apbif_top_sub_pll_out_sel_1			
	// [ 9: 8] =  8'd3 : rslv_apbif_top_sub_pll_out_sel_2			
	// [    4] =  8'd0 : rslv_apbif_top_sub_pll_out_sel			
	// [    0] =  8'd0 : rslv_apbif_top_sub_half_out_sel 		
	sdp_set_clkrst_mux(0x005c047c, 0xffffffff, 0x00003300); // SUB_PATH_SEL_32 
	
	
	//************************************************************* 
	//	  SUBPATH prescaler setting (USBPLL) For IoT			   
	//	  emmc / msp / bus / bus_sub / apbif_top / busif / dma	   
	//************************************************************* 
	// o_clk_emmc : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_emmc_sub_prescl_on 				
	// [ 7: 0] =  8'd  9 : rslv_emmc_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0324, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_9 
	
	// o_clk_msp : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_msp_sub_prescl_on					
	// [ 7: 0] =  8'd 19 : rslv_msp_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0340, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_16 
	
	// o_clk_bus_sub : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_bus_sub_sub_prescl_on				
	// [ 7: 0] =  8'd 19 : rslv_bus_sub_sub_prescl_val				
	sdp_set_clkrst_mux(0x005c0348, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_18 
	
	// o_clk_busif : prescaler (USBPLL /2 / 20 = 50MHz)
	// [   12] =  8'd1 : rslv_busif_sub_prescl_on					
	// [ 7: 0] =  8'd 19 : rslv_busif_sub_prescl_val 			
	sdp_set_clkrst_mux(0x005c0388, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_34 
	
	// o_clk_bus : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_bus_sub_prescl_on					
	// [ 7: 0] =  8'd  9 : rslv_bus_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c0398, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_38 
	
	// o_clk_dma : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_dma_sub_prescl_on					
	// [ 7: 0] =  8'd  9 : rslv_dma_sub_prescl_val					
	sdp_set_clkrst_mux(0x005c03a0, 0xffffffff, 0x00001009); // SUB_SCALE_CTRL_40 
	
	// o_clk_dma : prescaler (USBPLL /2 / 10 = 100MHz)
	// [   12] =  8'd1 : rslv_apbif_top_sub_prescl_on				
	// [ 7: 0] =  8'd 19 : rslv_apbif_top_sub_prescl_val 		
	sdp_set_clkrst_mux(0x005c0380, 0xffffffff, 0x00001013); // SUB_SCALE_CTRL_32 
	
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


	// o_clk_bus_sub : sub  selection (USBPLL   /2 /20 =  50MHz
	// o_clk_dmicom	: Off
	// o_clk_dp_o	: Off
	// o_clk_dpscl_w : Off
	// o_clk_avd 	: Off
	// o_clk_henc	: Off
	// o_clk_dp_nrfc : Off
	// [   28] =  1'd0 : rslv_bus_sub_out_sel_main		
	sdp_set_clkrst_mux(0x005c0260, 0xffffffff, 0x00111111); // CLK_MUX_MAIN_SEL_4 

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

	// msp, bus_sub, bus, dma, hclk_peri for no glitch
	// [   24] =  1'd1 : rslv_dma_out_sel_sub					
	// [   20] =  1'd1 : rslv_msp_out_sel_sub					
	// [    8] =  1'd1 : rslv_bus_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0280, 0xffffffff, 0x01101111); // CLK_MUX_SUB_SEL_0 
	
	// msp, bus_sub, bus, dma, hclk_peri for no glitch
	// [    0] =  1'd1 : rslv_bus_sub_out_sel_sub					
	sdp_set_clkrst_mux(0x005c0290, 0xffffffff, 0x10000011); // CLK_MUX_SUB_SEL_4 

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

	// [   16] =  1'd0 : rslv_pll_dppll1_reset_n 		
	// [   12] =  1'd0 : rslv_pll_vby1pll_reset_n			
	// [    4] =  1'd0 : rslv_pll_frcpll_reset_n 		
	sdp_set_clkrst_mux(0x005c0238, 0xffffffff, 0x00001101); // PLL_RESETN_CTRL_2 
}


static int notrace __sdp1501_suspend_enter(unsigned long unused)
{
	if (sdp_pm_getmode() == 1) { // ewbs mode
		sdp1501_pm_set_ewbs_demdulator();
		sdp1501_pm_set_ewbs_powergating();
	}
	
	if (soc_is_jazzm())
		soft_restart((unsigned long)virt_to_phys(sdp1501_wait_for_die));
	else
		soft_restart((unsigned long)virt_to_phys(sdp1531_wait_for_die));
	return 0;
}

/*******************************
 * SDP1601 Kant-M
 *******************************/
static int notrace __sdp1601_suspend_enter(unsigned long unused)
{
#ifdef CONFIG_ARM_PSCI
	const struct psci_power_state ps = {
               .type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
	};

	if (sdp_pm_getmode() == 1 && sdp_pm_ewbs_fn)
		sdp_pm_ewbs_fn();
	
	psci_ops.cpu_suspend(ps, (u32)virt_to_phys(cpu_resume));
#endif
	return 0;
}

/*******************************
 * SDP1412 Hawk-A
 *******************************/
static int notrace __sdp1412_suspend_enter(unsigned long unused)
{
#ifdef CONFIG_ARM_PSCI
	const struct psci_power_state ps = {
               .type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
	};

	psci_ops.cpu_suspend(ps, (u32)virt_to_phys(cpu_resume));
#endif
	return 0;
}
/************************************************************/

/*******************************
 * SDP1803 Muse-M
 *******************************/
static int notrace __sdp1803_suspend_enter(unsigned long unused)
{
#ifdef CONFIG_ARM_PSCI
	const struct psci_power_state ps = {
               .type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
	};

	if (sdp_pm_getmode() == 1 && sdp_pm_ewbs_fn)
		sdp_pm_ewbs_fn();
	
	psci_ops.cpu_suspend(ps, (u32)virt_to_phys(cpu_resume));
#endif
	return 0;
}

static int sdp_suspend_begin(suspend_state_t state)
{
	pr_info("sdp_suspend_begin : state=%d\n", state);

	if(soc_is_sdp1404())
		return sdp1404_suspend_begin(state);
	else if(soc_is_sdp1406())
		return sdp1406_suspend_begin(state);
	else if(soc_is_jazz())
		return sdp1501_suspend_begin(state);
	else if(soc_is_sdp1601())
	{
		// no need to do anything as psci driver will take care
	}
	return 0;
}

static int sdp_suspend_enter(suspend_state_t state)
{
	if(soc_is_sdp1404())
		cpu_suspend(0, __sdp1404_suspend_enter);
	else if(soc_is_sdp1406())
		cpu_suspend(0, __sdp1406_suspend_enter);
	else if(soc_is_jazz())
		cpu_suspend(0, __sdp1501_suspend_enter);
	else if(soc_is_sdp1601())
		cpu_suspend(0, __sdp1601_suspend_enter);
	else if(soc_is_sdp1412())
		cpu_suspend(0, __sdp1412_suspend_enter);
	else if(soc_is_muse())
		cpu_suspend(0, __sdp1803_suspend_enter);
	return 0;
}

static const struct platform_suspend_ops sdp_suspend_ops = {
	.begin		= sdp_suspend_begin,
	.enter		= sdp_suspend_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init sdp_suspend_init(void)
{
	pr_info ("SDP suspend support.\n");

	suspend_set_ops(&sdp_suspend_ops);

	if(soc_is_sdp1404()) {
		pm_power_off = sdp_poweroff;
		sdp_sarea = ioremap((PHYS_OFFSET)-0x100, 0x100);
		sdp_uart_base = 0x10090A00;
		sdp_micom_port = SDP1404_MICOM_PORT;
		sdp1404_gpio_regs = ioremap(0x11250C00, 0x400);

		BUG_ON(!sdp_sarea || !sdp1404_gpio_regs);
	} else if(soc_is_sdp1406()) {
		sdp_sarea = ioremap(0x800588, 0x10); /* micom free register 0x800588~0x80059F */
	} else if(soc_is_jazz()) {
		sdp1501_suspend_init();
	}

	return 0;
}
late_initcall(sdp_suspend_init);

/* sdp1406 syscore */
struct sdp1406_syscore {
	u32 ulcon;
	u32 ucon;
	u32 ufcon;
	u32 ubrdiv;

	u32 gic_dist_ctrl;
	u32 gic_dist_enable[32];
	u32 gic_dist_pri[32];
	u32 gic_dist_target[48];
	u32 gic_cpu_ctrl;
	u32 gic_cpu_primask;
} sdp1406_syscore_context;

static void sdp1406_restore_ldo(void)
{
	u32 val;
	u32 base = 0xFE480500;

	/* restore LDO when no bus timeout */
	if ((readl((void*)0xFEE00000) & (1 << 16)) == 0) {
		pr_info("%s: restore ldo\n", __func__);
		val = readl((void*)base) & ~(1 << 19);
		writel(val, (void*)base);
	} else {
		pr_info("%s: ignore ldo\n", __func__);
	}
}

static void sdp1406_pm_suspend(void)
{
	u32 base;
	int i;

	/* save uart */
	base = 0xfe000000 + 0x00190a00 - 0x00100000;
	sdp1406_syscore_context.ulcon = readl((void *)(base + 0x00));
	sdp1406_syscore_context.ucon = readl((void *)(base + 0x04));
	sdp1406_syscore_context.ufcon = readl((void *)(base + 0x08));
	sdp1406_syscore_context.ubrdiv = readl((void *)(base + 0x28));

	/* save gic */
	base = 0xfe000000 + 0x00781000 - 0x00100000;

	sdp1406_syscore_context.gic_dist_ctrl = readl((void *)base);
	for (i = 0; i < 32; i++) {
		sdp1406_syscore_context.gic_dist_enable[i] =
			readl((void *)(base + 0x100 + (u32)i * 4));
		sdp1406_syscore_context.gic_dist_pri[i] =
			readl((void *)(base + 0x400 + (u32)i * 4));
	}
	for (i = 0; i < 48; i++)
		sdp1406_syscore_context.gic_dist_target[i] =
			readl((void *)(base + 0x800 + (u32)i * 4));

	base = 0xfe000000 + 0x00782000 - 0x00100000;
	sdp1406_syscore_context.gic_cpu_ctrl = readl((void *)base);
	sdp1406_syscore_context.gic_cpu_primask = readl((void *)(base + 0x4));

	sdp1406_restore_ldo();
}

static void sdp1406_pm_resume(void)
{
	u32 base;
	int i;

	/* save uart */
	base = 0xfe000000 + 0x00190a00 - 0x00100000;
	writel(sdp1406_syscore_context.ulcon, (void *)(base + 0x00));
	writel(sdp1406_syscore_context.ufcon, (void *)(base + 0x08));
	writel(sdp1406_syscore_context.ubrdiv, (void *)(base + 0x28));
	writel(sdp1406_syscore_context.ucon, (void *)(base + 0x04));

	/* save gic */
	base = 0xfe000000 + 0x00781000 - 0x00100000;
	writel(0, (void *)(base + 0x100)); /* dist_enable_set  = 0 */

	for (i = 0; i < 32; i++)
		writel(sdp1406_syscore_context.gic_dist_pri[i],
				(void *)(base + 0x400 + (u32)i * 4));

	writel(sdp1406_syscore_context.gic_dist_ctrl, (void *)base);

	for (i = 0; i < 48; i++)
		writel(sdp1406_syscore_context.gic_dist_target[i],
				(void *)(base + 0x800 + (u32)i * 4));

	writel(sdp1406_syscore_context.gic_cpu_primask, (void *)(base + 0x1000 + 0x4));
	writel(sdp1406_syscore_context.gic_cpu_ctrl, (void *)(base + 0x1000));

	for (i = 0; i < 32; i++)
		writel(sdp1406_syscore_context.gic_dist_enable[i],
				(void *)(base + 0x100 + (u32)i * 4));
}

static void sdp1406_pm_shutdown(void)
{
	sdp1406_restore_ldo();
}
/* end fo sdp1406 syscore */

/****************************************
 * SDP1501 Jazz-M syscore
 ****************************************/
static void sdp1501_pm_suspend(void)
{
}

static void sdp1501_pm_resume(void)
{
}

/****************************************
 * SDP1601 / SDP1701 Kant-M syscore
 ****************************************/
#define PHY_SFR0_BASE		(0x100000)
#define AON_MISC_GPR_STRT	(0x9C1200)
#define AON_MISC_GPR_END	(0x9C12FF)

static int __sdp1701_set_aon_misc_gpr(u32 phy_addr, u32 mask, u32 value)
{
	void __iomem *addr;
	u32 val;

	if( phy_addr < AON_MISC_GPR_STRT || phy_addr > AON_MISC_GPR_END ) {
		pr_err("%s: invalid address for misc-gpr(aon)! addr=0x%08X\n",
			__FUNCTION__, phy_addr);
		return -EINVAL;
	}

	addr = (void*)(SFR_VA + phy_addr - PHY_SFR0_BASE);
	
	val = readl(addr);
	val &= (~mask);
	val |= value;
	writel(val, addr);
	
	pr_info("%s: [0x%x:%p][0x%x]\n",__FUNCTION__,phy_addr,addr,readl(addr));

	return 0;
}

/* USB PLL RST OFF */ 
static void __sdp1701_pm_usb_pll_rst_off(void)
{
	const u32 USB_PLL_RST = 0x9C1254;
	const u32 USB_PLL_RST_MASK = (0x1 << 16);

	int ret = __sdp1701_set_aon_misc_gpr(USB_PLL_RST
				,USB_PLL_RST_MASK,USB_PLL_RST_MASK);
	if( ret != 0 ) {
		pr_err("[PM] error[0x%X] : USB PLL RST OFF\n",ret);
	}
}

/* USB CLOCK RST OFF */ 
static void __kantm_pm_allusb_clk_off(void)
{
	const u32 USB_CLK_RST = 0x00F405E0;
	const u32 USB_CLK_RST_MASK = 0x00011111;

	int ret = sdp_set_clkrst_mux(USB_CLK_RST,USB_CLK_RST_MASK,0x0);
	if( ret != 0 ) {
		pr_err("[PM] err[0x%X] : USB CLOCK OFF\n",ret);
	}

	if( soc_is_sdp1701() ) {
		__sdp1701_pm_usb_pll_rst_off();
	}
}

static void kantm_pm_suspend(void)
{
	__kantm_pm_allusb_clk_off();
}

static void kantm_pm_shutdown(void)
{
	__kantm_pm_allusb_clk_off();
}


/****************************************
 * SDP1803/1804 Muse-M/L syscore
 ****************************************/
/* USB PLL RST OFF */ 
static void __sdp180x_pm_usb_pll_rst_off(void)
{
	void __iomem *addr;
	u32 val;
	u32 mask, value;
	int i;
	const u32 USB_PLL_RST = 0x9C1254;
	const u32 USB_PLL_RST_MASK = (0x1 << 16);

	const u32 USB_PORT_SIDDQ = 0x9C093C;
	const u32 USB_PROT_SIDDQ_MASK = (0x1 << 20);

	addr = ioremap(USB_PLL_RST, 0x4);
	mask = USB_PLL_RST_MASK;
	value = USB_PLL_RST_MASK;
	
	val = readl(addr);
	val &= (~mask);
	val |= value;
	writel(val, addr);
	pr_info("%s: [0x%x:%p][0x%x]\n",__FUNCTION__,USB_PLL_RST,addr,readl(addr));
	iounmap(addr);

	addr = ioremap(USB_PORT_SIDDQ, 0x50);
	mask = USB_PROT_SIDDQ_MASK;
	value = USB_PROT_SIDDQ_MASK;
	for(i=0;i<5;i++) {
		val = readl(addr+0x10*i);
		val &= (~mask);
		val |= value;
		writel(val, addr+0x10*i);
		pr_info("%s: [0x%x:%p][0x%x]\n",__FUNCTION__,USB_PORT_SIDDQ+0x10*i,addr+0x10*i,readl(addr+0x10*i));
	}
	iounmap(addr);

}

u32 sdp_read_clkrst_mux(u32 phy_addr);

/* USB CLOCK RST OFF */ 
static void __sdp180x_pm_allusb_clk_off(void)
{
	int ret = 0;
	const u32 USB_CLK_RST = 0x00F507F0;
	const u32 USB_CLK_RST_MASK = 0x00001110;
	const u32 USB_CLK_RST2 = 0x00F507F4;
	const u32 USB_CLK_RST2_MASK = 0x10000000;
	

	ret = sdp_set_clkrst_mux(USB_CLK_RST,USB_CLK_RST_MASK,0x0);
	if( ret != 0 ) {
		pr_err("[PM] err[0x%X] : USB CLOCK OFF\n",ret);
	}
	ret = sdp_set_clkrst_mux(USB_CLK_RST2,USB_CLK_RST2_MASK,0x0);
	if( ret != 0 ) {
		pr_err("[PM] err[0x%X] : USB CLOCK OFF\n",ret);
	}

	pr_info("%s: [0x%x][0x%x]\n",__FUNCTION__,sdp_read_clkrst_mux(USB_CLK_RST),sdp_read_clkrst_mux(USB_CLK_RST2));

	__sdp180x_pm_usb_pll_rst_off();
}

static void sdp180x_pm_suspend(void)
{
	__sdp180x_pm_allusb_clk_off();
}

static void sdp180x_pm_shutdown(void)
{
	__sdp180x_pm_allusb_clk_off();
}


/******************************************/
static int sdp_pm_suspend(void)
{
	if(soc_is_sdp1406()) {
		sdp1406_pm_suspend();
	} else if(soc_is_jazz()) {
		sdp1501_pm_suspend();
	}
	else if( soc_is_sdp1601() ) {
		kantm_pm_suspend();
	}
	else if( soc_is_muse() ) {
		sdp180x_pm_suspend();
	}

#ifdef CONFIG_SDP_HW_CLOCK
	/* HW clock suspend must be the latest. */
	hwclock_suspend();
#endif

	return 0;
}

static void sdp_pm_resume(void)
{
	if(soc_is_sdp1406()) {
		sdp1406_pm_resume();
	} else if(soc_is_jazz()) {
		sdp1501_pm_resume();
	}
}

static void sdp_pm_shutdown(void)
{
	if (soc_is_sdp1406())
		sdp1406_pm_shutdown();
	else if( soc_is_sdp1601() )
		kantm_pm_shutdown();
	else if( soc_is_muse() )
		sdp180x_pm_shutdown();	
}

static struct syscore_ops sdp_pm_syscore_ops = {
	.suspend	= sdp_pm_suspend,
	.resume		= sdp_pm_resume,
	.shutdown	= sdp_pm_shutdown,
};

static int __init sdp_pm_syscore_init(void)
{
	register_syscore_ops(&sdp_pm_syscore_ops);
	return 0;
}
arch_initcall(sdp_pm_syscore_init);

