/*********************************************************************************************
 *
 *	sdp_bus_err_logger.c (Samsung Soc Bus Err Logger)
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
#include <linux/signal.h>

#include <asm/bug.h>

#ifdef CONFIG_ARCH_SDP1501

#include <mach/map.h>

#define BUS_ERR_LOG_MAIN 0xFC0000
#define BUS_ERR_LOG_SUB  0xFB0000

#define ERRVLD		0x000C
#define ERRCLR		0x0010
#define ERRLOG0		0x0014
#define ERRLOG1		0x0018
#define ERRLOG3		0x0020

static void __iomem *reg_main = NULL;
static void __iomem *reg_sub  = NULL;


static const char* __get_err_type_str(int type)
{
	static const char *text[] = {
		"Read", "Wrap Read", "Undefined", "Exclusive Read", "Write", "Wrap Write"
	};

	if (type < 0 || type >= (sizeof(text)/sizeof(char*)))
		return "Ivaild";

	return text[type];
}
static const char* __get_err_code_str(int code)
{
	static const char *text[] = {
		"SLV err", "DEC err", "Undefined", "Power off err", "Security err", "Security err", "Timeout err"
	};

	if (code < 0 || code >= (sizeof(text)/sizeof(char*)))
		return "Ivaild";

	return text[code];
}
static const char* __get_master_ip_str(int ip)
{
	static const char *text[] = {
		"mPERI_BMMC", "mPERI_MSPI",	"mPERI_SPI2AHB", "mPERI_UDMARX", "mPERI_UDMATX", "Undefined", "Undefined",	"Undefined", 
		"mPERI_I2C_FAST0", "mPERI_I2C_FAST1", "mPERI_I2C_FAST2", "mPERI_I2C_FAST3",	"mUSB2_0", "mUSB2_1", "mUSB2_2", "mUSB2_3",
		"mARM_DAP", "mARM_ETR", "mARM_O5Q", "mARM_PERI", "mAUD_ADMA", "mAUD_ADSP0", "mAUD_ADSP1", "mAUD_AIO",
		"mAVD",	"mCHN_0", "mCHN_1", "mCHN_2", "mCTR_0", "mCTR_1", "mCTR_2", "mCTR_3",
		"mDMICOM", "mDP_NRC0R_C",	"mDP_NRC0R_MAP", "mDP_NRC0R_Y", "mDP_NRC0W_MAP", "mDP_NRC0W_YC", "mDP_SCLR_CVBS", "mDP_SCLR_MAIN",
		"mDP_SCLR_MAP", "mDP_SCLR_SUB",	"mDP_SCLR_VDEC", "mDP_SCLW_MAIN", "mDP_SCLW_SUB", "mDP_SCLW_TTX", "mDVDE_0", "mDVDE_1",
		"mDVDE_2", "mDVDE_3",	"mFRC0_ME_BIF1", "mFRC0_ME_BIF2", "mFRC0_ME_GDC", "mFRC1_MC_BIF1", "mFRC1_MC_BIF2", "mFRC1_MC_BIF3",			
		"mFRC2_3DC_BIF", "mFRC2_3DF_BIF1",	"mFRC2_3DF_BIF2", "mFRC2_3DF_BIF3", "mFRC2_3DF_BIF4", "mGA_R0", "mGA_R1", "mGA_W0",	
		"mGPU_0", "mGPU_1",	"mGPU_2", "mGP_CSR1", "mGP_CSR2", "mGP_GP", "mGP_OSDP", "mGP_SGC",	
		"mGP_SGP", "mHEN_0", "mHEN_1", "mMFD_H2640", "mMFD_H2641", "mMFD_H2642", "mMFD_H2643", "mMFD_JPEG0",	
		"mMFD_JPEG1", "mMICOM",	"mMSP_SE0", "mMSP_TSD", "mPERI_EMAC", "mPERI_EMMC", "mPERI_FDC", "mPERI_GADMAR",
		"mPERI_GADMAW", "mPERI_GZIP",	"mROT_R0", "mROT_W0", "mSRP_ISRPM", "mUSB3_0"
	};

	if (ip < 0 || ip >= (sizeof(text)/sizeof(char*)))
		return "Ivaild";

	return text[ip];
}
static const char* __get_target_ip_str(int ip)
{
	static const char *text[] = {
		"SVC_BUS_MAIN", "SVC_BUS_MAIN_tz", "sBUS_APB", "sBUS_SUB", "sXMIFA0", "sXMIFA1", "sXMIFA2",	"sXMIFB0", 
		"sXMIFB1", "sXMIFB2", "sXMIFC0", "sXMIFC1",	"sXMIFC2", "sXMIFD0", "sXMIFD1", "sXMIFD2",
		"sXMIFE0", "sXMIFE1", " sXMIFE2"
	};

	if (ip < 0 || ip >= (sizeof(text)/sizeof(char*)))
		return "Ivaild";

	return text[ip];
}
static const char* __get_slave_ip_str(int ip)
{
	static const char *text[] = {
		"sSCL_BIF", "sSCL_COE", "sSCL_INA", "sSCL_INI", "sSCL_INL", "sSCL_INP", "sSCL_MAIN", "sSCL_MON", 
		"sSCL_NR0", "sSCL_NR1", "sSCL_NR2", "sSCL_NR3", "sSCL_PRE", "sSCL_SDO", "sSCL_SUB", "sSCL_TDC",
		"sSCL_TMG", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", 
		"sFRC0_GDC_BIF", "sFRC0_INPUT", "sFRC0_ME", "sFRC0_ME_BIF", "sFRC0_MON", "sFRC0_PRE", "sFRC0_TMG", "Undefined", 
		"sNRC0_BIF", "sNRC0_DST", "sNRC0_FDP", "sNRC0_MON", "sNRC0_NNR", "sNRC0_TNR", "Undefined", "Undefined", 
		"sGP_GP", "sGP_MIX", "sGP_MON", "sGP_OSDP", "sGP_SGP", "sGP_SUB", "Undefined", "Undefined", 
		"sHDMI_APB0", "sHDMI_APB1", "sHDMI_APB2", "sHDMI_APB3", "sHDMI_APB4", "Undefined", "Undefined", "Undefined", 
		"sMSP_HCAS", "sMSP_MSP_MON", "sMSP_SE0", "sMSP_SE1", "sMSP_TSD", "Undefined", "Undefined", "Undefined", 
		"sFRC1_MCC", "sFRC1_MCY", "sFRC1_MC_BIF", "sFRC1_MON", "sFRC2_3DF", "sFRC2_3DF_BIF", "sFRC2_LD", "sFRC2_POST", 
		"sPERI_GA_DMA", "sPERI_GZIP", "sPERI_MON", "sPERI_QCS", "sVE_DE", "sVE_GM", "sVE_UBR", "sVE_VE", 
		"sAUD_ADMA", "sAUD_ADSP", "sAUD_ADSP_MON", "Undefined", "sDMICOM0", "sDMICOM1", "sDMICOM_MON", "Undefined", 
		"sNRC1_ARS", "sNRC1_IPC", "sNRC1_UCR", "Undefined", "sNRC2_DB", "sNRC2_DR", "sNRC2_SHD", "Undefined", 
		"sGPU_CTRL", "sGPU_MON", "sGPU_SGC", "Undefined", "sMICOM0", "sMICOM0", "sMICOM_MON", "Undefined", 
		"sTOP_Vx1_PHY_RX", "sVx1_CTRL_RX", "sVx1_CTRL_TX", "Undefined", "sUSB2", "sUSB3_PHY", "sUSB_MON", "Undefined", 
		"sXMIFa_MEMS", "sXMIFa_TZC", "sXMIFa_TZCID", "Undefined", "sXMIFb_MEMS", "sXMIFb_MEMS", "sXMIFb_TZCID", "Undefined", 
		"sXMIFc_MEMS", "sXMIFc_TZC", "sXMIFc_TZCID", "Undefined", "sXMIFd_MEMS", "sXMIFd_TZC", "sXMIFd_TZCID", "Undefined", 
		"sXMIFe_MEMS", "sXMIFe_TZC", "sXMIFe_TZCID", "Undefined", "sAIO", "sAIO_MON", "sAVD", "sAVD_MON", 
		"sCHN", "sCHN_MON", "sCYS_MON", "sCYS_REG", "sDVDE", "sDVDE_MON", "sFRC2_3DC", "sFRC2_MON", 
		"sGA2D", "sGA2D_MON", "sHEN", "sHEN_MON", "sMFD_H264_MFD", "sMFD_H264_MON", "sMFD_JPEG0", "sMFD_JPEG0_MON", 
		"sMFD_JPEG1", "sMFD_JPEG1_MON", "sROT", "sROT_MON", "sSRP", "sSRP_MON", "sTCON", "sTCON_DEMURA", 
		"sTOP_CLKRST", "sTOP_PADCTL", "SVC_BUS_SUB", "SVC_BUS_SUB_remap", "SVC_BUS_SUB_tz", "sAFE", "sCTR_MON", "sCYS/sCYS_APB", 
		"sCYS_GIC", "sCYS_STM", "sEDID", "sFRC2_3DC_BIF", "sGPU", "sHDMI_OCP", "sPERI_AHB", "sPERI_FDC", 
		"sPERI_TZSRAM", "sTOP_Vx1_PHY_TX", "sUSB3_LINK", "sXMIFau_APB", "sXMIFbu_APB", "sXMIFcu_APB", "sXMIFdu_APB", "sXMIFeu_APB"		
	};

	if (ip < 0 || ip >= (sizeof(text)/sizeof(char*)))
		return "Ivaild";

	return text[ip];
}

static int __get_err_type(unsigned int log0)
{
	return ((log0 >> 1)&0xf);
}
static int __get_err_code(unsigned int log0)
{
	return ((log0 >> 8)&0x7);
}
static int __get_master_ip(unsigned int log1)
{
	return ((log1 >> 17)&0x7f);
}
static int __get_target_ip(unsigned int log1)
{
	return ((log1 >> 12)&0x1f);
}
static int __get_slave_ip(unsigned int log1)
{
	return ((log1 >> 8)&0xff);
}	
static void __draw_line(void)
{
	printk(KERN_ERR "======================================================\n");
}
static void __dump_regs(unsigned int paddr, void __iomem *reg, int size)
{
	int offset;
	for (offset=0; offset<size; offset+=16) {
		printk("0x%08x : %08x %08x %08x %08x\n", 
			paddr+offset, readl(reg+offset), readl(reg+offset+0x4), readl(reg+offset+0x8), readl(reg+offset+0xc));
		
	}
}

int sdp_bus_err_logger(void)
{
	int cnt = 0;
	unsigned int log0, log1, log3;
	unsigned int err_type, err_code, master_ip, target_ip, slave_ip, addr; 

	printk(KERN_ERR "\n\nsdp_bus_err_logger start.\n\n");
	
	while (readl(reg_main+ERRVLD) && cnt < 16) {

		__draw_line();
		printk(KERN_ERR "SDP BUS ERR LOGGER (CNT=%d)\n", cnt);
		__draw_line();
		__dump_regs(BUS_ERR_LOG_MAIN, reg_main, 0x40);
		__draw_line();
		__dump_regs(BUS_ERR_LOG_SUB, reg_sub, 0x40);
		__draw_line();

		log0 = readl(reg_main+ERRLOG0);
		log1 = readl(reg_main+ERRLOG1);
		log3 = readl(reg_main+ERRLOG3);

		err_type = __get_err_type(log0);
		err_code = __get_err_code(log0);
		master_ip = __get_master_ip(log1);
		target_ip = __get_target_ip(log1);
		addr = log3;
		printk(KERN_ERR "MAIN : type=%x(%s), code=%x(%s), master=%x(%s), target=%x(%s), addr=%x\n",
			err_type, __get_err_type_str(err_type),
			err_code, __get_err_code_str(err_code),
			master_ip, __get_master_ip_str(master_ip),
			target_ip, __get_target_ip_str(target_ip),
			addr);

		if (target_ip == 3) {
			log0 = readl(reg_sub+ERRLOG0);
			log1 = readl(reg_sub+ERRLOG1);
			log3 = readl(reg_sub+ERRLOG3);
			
			err_type = __get_err_type(log0);
			err_code = __get_err_code(log0);
			slave_ip = __get_slave_ip(log1);
			addr = log3;
			printk(KERN_ERR "SUB  : type=%x(%s), code=%x(%s), slave=%x(%s), addr=%x\n",
				err_type, __get_err_type_str(err_type),
				err_code, __get_err_code_str(err_code),
				slave_ip, __get_slave_ip_str(slave_ip),
				addr);
		}

		printk(KERN_ERR "\n");
		
		writel(1, reg_main+ERRCLR);
		writel(1, reg_sub+ERRCLR);

		mdelay(1);
		cnt++;
	}

	printk(KERN_ERR "\n\nsdp_bus_err_logger end.\n\n");


	return 0;
}

EXPORT_SYMBOL(sdp_bus_err_logger);


typedef	struct 
{
	char name[10];
	u32 base;
} bus_mon_tbl_t;

static const bus_mon_tbl_t bus_mon_tbl[] = {
	{"DMAR",      0x00348000},	
	{"DMAW",      0x00348200},	
	{"GZIP",      0x00348400},	
	{"EMAC",      0x00348600},	
	{"EMMC",      0x00348800},	
	{"FDC",       0x00348A00},	
	{"AHB",       0x00348C00},	
	{"USB2",      0x00560200},	
	{"USB3",      0x00560000},	
	{"CSR1",      0x005E0000},	
	{"CSR2",      0x005E0200},	
	{"GP",        0x005E0400},	
	{"OSDP",      0x005E0600},	
	{"SGP",       0x005E0800},		
	{"SGC",       0x005E0a00},	
	{"GPU0",      0x003C3000},	
	{"GPU1",      0x003C3200},	
	{"GPU2",      0x003C3400},	
	{"GAR0",      0x003D8000},	
	{"GAR1",      0x003D8200},	
	{"GAW0",      0x003D8400},	
//	{"CPU",       0x007B0000},	
	{"PERI",      0x007B0200},	
	{"ETR",       0x007B0400},	
	{"DAP",       0x007B0600},	
	{"MICOM",     0x00B92000},	
	{"DIMICOM",   0x00B95000},	
	{"TSD",       0x00960000},	
	{"SE",        0x00960200},	
	{"ADSP0",     0x00A20000},	
	{"ADSP1",     0x00A20200},	
	{"ADMA",      0x00A20400},	
	{"AIO",       0x00A21000},	
	{"MFD0",      0x00A60000},	
	{"MFD1",      0x00A60200},	
	{"MFD2",      0x00A60400},	
	{"MFD3",      0x00A60600},	
	{"JPEG0",     0x00AA0000},	
	{"HEN0",      0x00AD0000},	
	{"HEN1",      0x00AD0200},	
//	{"DVDE0",     0x00B96000},	
//	{"DVDE1",     0x00B96200},	
//	{"DVDE2",     0x00B96400},	
//	{"DVDE3",     0x00B96600},	
	{"SRP",       0x00B94000},	
	{"CHN0",      0x00B58000},	
	{"CHN1",      0x00B58200},	
	{"CHN2",      0x00B58400},	
	{"AVD",       0x00B68000},	
	{"ROTR",      0x00B90000},	
	{"ROTW",      0x00B90200},	
	{"CTR0",      0x00B91000},	
	{"CTR1",      0x00B91200},	
	{"CTR2",      0x00B91400},	
	{"CTR3",      0x00B91600},	
	{"ME_BIF1",   0x00BB0000},	
	{"ME_BIF2",   0x00BB0200},	
	{"GDC",       0x00BB0400},	
	{"MC_BIF1",   0x00BB1000},	
	{"MC_BIF1",   0x00BB1200},	
	{"MC_BIF3",   0x00BB1400},	
	{"3DC_BIF",   0x00BB2000},	
	{"3DF_BIF1",  0x00BB2200},	
	{"3DF_BIF2",  0x00BB2400},	
	{"3DF_BIF3",  0x00BB2600},	
	{"3DF_BIF4",  0x00BB2800},	
	{"SCLR_MAIN", 0x00CD0000},	
	{"SCLR_SUB",  0x00CD0200},	
	{"SCLR_VDEC", 0x00CD0400},	
	{"SCLR_MAP",  0x00CD0600},	
	{"SCLR_CVBS", 0x00CD0800},	
	{"SCLW_MAIN", 0x00CD0A00},	
	{"SCLW_SUB",  0x00CD0C00},	
	{"SCLW_TTX",  0x00CD0E00},	
	{"NRCR_Y",    0x00D50000},	
	{"NRCR_C",    0x00D50200},	
	{"NRCR_MAP",  0x00D50400},	
	{"NRCW_YC",   0x00D50600},	
	{"NRCW_MAP",  0x00D50800},
};

static u32 __read32_phy(u32 addr) {
	
	return readl((void*)(SFR_VA - 0x00100000 + addr));
}

static u32 __get_pending_request_w(int i) {

	return (__read32_phy(bus_mon_tbl[i].base + 0x30) >> 28) & 0x1;
}
static u32 __get_pending_request_r(int i) {

	return (__read32_phy(bus_mon_tbl[i].base + 0x30) >> 24) & 0x1;
}
static u32 __get_pending_request(int i) {

	return __read32_phy(bus_mon_tbl[i].base + 0x30);
}
static u32 __get_valid_ready_status_w(int i) {

	return __read32_phy(bus_mon_tbl[i].base + 0x84);
}
static u32 __get_valid_ready_status_r(int i) {

	return __read32_phy(bus_mon_tbl[i].base + 0x88);
}

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);
static void __enable_clks(void) {

	// frc
	sdp_set_clkrst_mux(0x005c074c, 0x11111, 0x11111);
	sdp_set_clkrst_mux(0x005C0824, 0x00001100, 0x1100);
	sdp_set_clkrst_mux(0x005C0764, 0x00001100, 0x1100);
	sdp_set_clkrst_mux(0x005C0768, 0x00000100, 0x100);
	sdp_set_clkrst_mux(0x005C0C10, 0x00000001, 0x1);
	sdp_set_clkrst_mux(0x005C0C08, 0x11111111, 0x11111111);
	sdp_set_clkrst_mux(0x005C07C8, 0x00000011, 0x11);
	sdp_set_clkrst_mux(0x005C07CC, 0x00010000, 0x10000);
	sdp_set_clkrst_mux(0x005C0B10, 0x1, 0);
 	sdp_set_clkrst_mux(0x005C0AB0, 0x1, 0);
 	sdp_set_clkrst_mux(0x005C0AB8, 0x1, 0);
 	sdp_set_clkrst_mux(0x005C0AC0, 0x1 , 0);

	// dmicom
	sdp_set_clkrst_mux(0x005c0c04, 0x10000, 0x10000);
	udelay(1);
	sdp_set_clkrst_mux(0x005c0a60, 0xFFFFFFFF, 0x0);
	udelay(5);
	sdp_set_clkrst_mux(0x005c0260, 0x1000000, 0x1000000);
}

int sdp_bus_mon_debug(void)
{
	int i, cnt;
	u32 pend[2];

	printk(KERN_ERR "\n\nsdp_bus_mon_debug start.\n\n");

	__enable_clks();

	for (i = 0; i < sizeof(bus_mon_tbl)/sizeof(bus_mon_tbl_t); i++) {

		cnt = 1000;
		pend[0] = 1;
		pend[1] = 1;
		
		while ((pend[0] || pend[1]) && cnt > 0) {

			if (__get_pending_request_w(i)==0)
				pend[0] = 0;
			if (__get_pending_request_r(i)==0)
				pend[1] = 0;
			udelay(100);
			cnt--;
		}

		if (pend[0] || pend[1]) { // suspect hang 
			printk(KERN_ERR "[%s] is suspected of hang. (%08x: %08x, %08x, %08x)", \
				bus_mon_tbl[i].name, bus_mon_tbl[i].base, \
				__get_pending_request(i), __get_valid_ready_status_w(i), __get_valid_ready_status_r(i));
		}
	}
	
	printk(KERN_ERR "\nsdp_bus_mon_debug end.\n\n");
}

EXPORT_SYMBOL(sdp_bus_mon_debug);


static int do_v7_async_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	if (user_mode(regs)) {
		sdp_bus_err_logger();
	}
	/* in case of kernel fault, sdp_bus_err_logger() will be called by die() later */
	return 1;
}

static int __init sdp_bus_err_logger_init(void)
{
	reg_main = ioremap(BUS_ERR_LOG_MAIN, 0x40);
	if (!reg_main) {
		printk(KERN_ERR "sdp_bus_err_log_init main failed.\n");
		return -ENOMEM;
	}
	reg_sub = ioremap(BUS_ERR_LOG_SUB, 0x40);
	if (!reg_sub) {
		iounmap(reg_main);
		printk(KERN_ERR "sdp_bus_err_log_init sub failed.\n");
		return -ENOMEM;
	}

#ifndef CONFIG_ARM_LPAE
	if (cpu_architecture() == CPU_ARCH_ARMv7) {
		/* asynchronous external abort */
		hook_fault_code(22, do_v7_async_abort, SIGBUS, BUS_OBJERR, "imprecise external abort");
	}
#endif
	return 0;
}
#elif defined(CONFIG_ARCH_SDP1406)

#include <mach/map.h>

#define BUS_REG_BASE 0xF00000
#define CLK_RST_BASE 0x5C0000

static u32 __reg_read(u32 addr) {
	
	return readl((void*)(SFR_VA - 0x00100000 + addr));
}
static void __reg_setbit(u32 addr, u32 mask, u32 value) 
{
	u32 val;
	val = readl((void*)(SFR_VA - 0x00100000 + addr));
	val &= (~mask);
	val |= value;
	writel(val, (void*)(SFR_VA - 0x00100000 + addr));
	readl((void*)(SFR_VA - 0x00100000 + addr));
}

static void __draw_line(void)
{
	printk(KERN_ERR "======================================================\n");
}
static void __dump_regs(unsigned int paddr, int size)
{
	int offset;
	for (offset=0; offset<size; offset+=16) {
		printk(KERN_ERR "0x%08x : %08x %08x %08x %08x\n", 
			paddr+offset, __reg_read(paddr+offset), __reg_read(paddr+offset+0x4), __reg_read(paddr+offset+0x8), __reg_read(paddr+offset+0xc));
		
	}
}
static int __get_default_slv_err(void)
{
	return (__reg_read(BUS_REG_BASE)&(1<<0));
}
static int __get_response_err(void)
{
	return (__reg_read(BUS_REG_BASE)&(1<<2));
}
static int __get_timeout_err(void)
{
	return (__reg_read(BUS_REG_BASE)&(1<<3));
}
static void __show_master_err_resp(void)
{
	printk(KERN_ERR "master err reg : 0x220 = %08x, 0x224 = %08x\n", \
		__reg_read(BUS_REG_BASE+0x220), __reg_read(BUS_REG_BASE+0x224));
}
static void __show_slave_err_resp(void)
{
	printk(KERN_ERR "slave err reg : 0x22c = %08x\n", \
		__reg_read(BUS_REG_BASE+0x22c));
}
static void __show_timeout_err(void)
{
	u32 stat_ddra, stat_ddrb, stat_ddrc, stat_slv;
	u32 addr[3];
	int i;

	stat_ddra = __reg_read(BUS_REG_BASE+0x200);
	stat_ddrb = __reg_read(BUS_REG_BASE+0x204);
	stat_ddrc = __reg_read(BUS_REG_BASE+0x208);
	stat_slv  = __reg_read(BUS_REG_BASE+0x20c);

	if (stat_ddra) {
		printk(KERN_ERR "slave : ddr_a\n");
		for (i = 0; i < 3; i++) {
			addr[i] = __reg_read(BUS_REG_BASE+0x1c0+i*4);
			if (addr[i]) {
				printk(KERN_ERR "addr[%d] : rd=%08x, wr=%08x\n", i, addr[i]&0xffff0000, (addr[i]<<16)&0xffff0000);
			}
		}
	}
	if (stat_ddrb) {
		printk(KERN_ERR "slave : ddr_b\n");
		for (i = 0; i < 3; i++) {
			addr[i] = __reg_read(BUS_REG_BASE+0x1d0+i*4);
			if (addr[i]) {
				printk(KERN_ERR "addr[%d] : rd=%08x, wr=%08x\n", i, addr[i]&0xffff0000, (addr[i]<<16)&0xffff0000);
			}
		}
	}
	if (stat_ddrc) {
		printk(KERN_ERR "slave : ddr_c\n");
		for (i = 0; i < 3; i++) {
			addr[i] = __reg_read(BUS_REG_BASE+0x1e0+i*4);
			if (addr[i]) {
				printk(KERN_ERR "addr[%d] : rd=%08x, wr=%08x\n", i, addr[i]&0xffff0000, (addr[i]<<16)&0xffff0000);
			}
		}
	}
	if (stat_slv) {
		printk(KERN_ERR "slave : peri & slv\n");
		for (i = 0; i < 3; i++) {
			addr[i] = __reg_read(BUS_REG_BASE+0x1f0+i*4);
		}
		if (addr[0]) {
			printk(KERN_ERR "addr[peri] : rd=%08x, wr=%08x\n", addr[0]&0xffff0000, (addr[0]<<16)&0xffff0000);
		}
		if (addr[1]) {
			printk(KERN_ERR "addr[slvx] : rd=%08x, wr=%08x\n", addr[1]&0xffff0000, (addr[1]<<16)&0xffff0000);
		}
		if (addr[2]) {
			printk(KERN_ERR "addr[slvn] : rd=%08x, wr=%08x\n", addr[2]&0xffff0000, (addr[2]<<16)&0xffff0000);
		}		
	}	
}
static void __show_clkmux_regs(void)
{
	int i;
	
	if (__reg_read(CLK_RST_BASE+0x0E0)==0x1cec0fee) {

		/* bus reset */
		__reg_setbit(BUS_REG_BASE+0x034, 1<<18, 0);
		__reg_setbit(BUS_REG_BASE+0x038, 1<<15, 0);
		__reg_setbit(BUS_REG_BASE+0x03c, 1, 1);
		__reg_setbit(BUS_REG_BASE+0x034, 1<<18, 1<<18);
		__reg_setbit(BUS_REG_BASE+0x038, 1<<15, 1<<15);
		__reg_setbit(BUS_REG_BASE+0x03c, 1, 1);

		for (i = 0; i < 4; i++ ) {
			printk(KERN_ERR "swrst%d : %08x\n", i, __reg_read(CLK_RST_BASE+0x0E0+i*4));
		}
		for (i = 0; i < 3; i++ ) {
			printk(KERN_ERR "clkmask%d : %08x\n", i, __reg_read(CLK_RST_BASE+0x120+i*4));
		}		
	}	
}

int sdp_bus_err_logger(void)
{
	int cnt = 0;
	unsigned int log0, log1, log3;
	unsigned int err_type, err_code, master_ip, target_ip, slave_ip, addr; 

	printk(KERN_ERR "\n\nsdp_bus_err_logger start.\n\n");

	__draw_line();
	printk(KERN_ERR "SDP BUS ERR LOGGER\n");
	__draw_line();
	__dump_regs(BUS_REG_BASE, 0x10);
	__dump_regs(BUS_REG_BASE+0x180, 0xC0);
	__draw_line();

	if (__get_default_slv_err()) {
		printk(KERN_ERR "[default slave access flag]\n");
	}
	if (__get_response_err()) {
		printk(KERN_ERR "[master & slave response error flag]\n");
		__show_master_err_resp();
		__show_slave_err_resp();
	}
	if (__get_timeout_err()) {
		printk(KERN_ERR "[timeout error flag]\n");
		__show_timeout_err();
		__show_clkmux_regs();
	}

	printk(KERN_ERR "\nsdp_bus_err_logger end.\n\n");

	return 0;
}
EXPORT_SYMBOL(sdp_bus_err_logger);

int sdp_bus_mon_debug(void)
{
	printk(KERN_ERR "\n\nsdp_bus_mon_debug not implemented.\n\n");
	return 0;
}

EXPORT_SYMBOL(sdp_bus_mon_debug);

static int __init sdp_bus_err_logger_init(void)
{
	return 0;
}

#else
int sdp_bus_err_logger(void)
{
	printk(KERN_ERR "\n\nsdp_bus_err_logger not implemented.\n\n");
	return 0;
}

EXPORT_SYMBOL(sdp_bus_err_logger);

int sdp_bus_mon_debug(void)
{
	printk(KERN_ERR "\n\nsdp_bus_mon_debug not implemented.\n\n");
	return 0;
}

EXPORT_SYMBOL(sdp_bus_mon_debug);

static int __init sdp_bus_err_logger_init(void)
{
	return 0;
}
#endif

subsys_initcall(sdp_bus_err_logger_init);

MODULE_AUTHOR("yongjin79.kim@samsung.com");
MODULE_DESCRIPTION("Driver for SDP bus error logger");

