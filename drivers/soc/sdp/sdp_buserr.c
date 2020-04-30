
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <asm/bug.h>
#include <linux/spinlock.h>
#include <soc/sdp/soc.h>
#include <soc/sdp/sdp_buserr.h>
#include <linux/printk.h>

#define BUSERR_N_DATA 5
#define BUSERR_N_SUBS 4
#define BUSERR_N_LOGGERS 3
#define BUSERR_N_LOGS 16

struct sdp_buserr_data {
	char *name;
	u32 offset;
	u32 shift;
	u32 mask;
	char **str;
	int n_str;
};

struct sdp_buserr_logger {
	char *name;
	phys_addr_t base;
	size_t size;
	struct sdp_buserr_data data[BUSERR_N_DATA];
	struct sdp_buserr_logger *subs[BUSERR_N_SUBS];
	void __iomem *reg;
	int cnt;
};

struct sdp_buserr {
	int n_loggers;
	struct sdp_buserr_logger *loggers[BUSERR_N_LOGGERS];
	spinlock_t lock;
	struct delayed_work work;

	struct sdp_buserr_param param;
};

static struct sdp_buserr g_buserr;
static struct sdp_buserr* get_buserr(void)
{
	return &g_buserr;
}

static int sdp_buserr_add_logger(struct sdp_buserr_logger *logger)
{
	struct sdp_buserr *buserr = get_buserr();

	if (buserr->n_loggers >= BUSERR_N_LOGGERS)
		return -ENOSPC;

	logger->reg = ioremap(logger->base, logger->size);
	if (!logger->reg)
		return -ENOMEM;
		
	buserr->loggers[buserr->n_loggers++] = logger;
	return 0;
}

static int common_chk(struct sdp_buserr_logger *logger)
{
	if (logger && logger->reg && logger->cnt < BUSERR_N_LOGS)
		return readl(logger->reg + 0x0c);

	return 0;
}

static void common_clr(struct sdp_buserr_logger *logger)
{
	if (logger && logger->reg) {
		writel(0x1, logger->reg + 0x10);
		logger->cnt++;
	}
}

static u32 common_get_val_by_name(struct sdp_buserr_logger *logger, char *name, int n)
{
	int i;
	u32 val;

	for (i = 0; i < BUSERR_N_DATA; i++) {
		if (logger->data[i].name && !strncmp(logger->data[i].name, name, n)) {
			val = readl(logger->reg + logger->data[i].offset);
			val = val >> logger->data[i].shift;
			val = val & logger->data[i].mask;
			return val;
		}
	}
	return (u32)-1;
}

static char* common_get_str_by_name(struct sdp_buserr_logger *logger, char *name, int n)
{
	int i;
	u32 val;

	for (i = 0; i < BUSERR_N_DATA; i++) {
		if (logger->data[i].name && !strncmp(logger->data[i].name, name, n)) {
			val = readl(logger->reg + logger->data[i].offset);
			val = val >> logger->data[i].shift;
			val = val & logger->data[i].mask;
			if (val < logger->data[i].n_str)
				return logger->data[i].str[val];
			else
				return NULL;
		}
	}
	return NULL;
}

static void common_line(void)
{
	printk(KERN_ERR "=================================================\n");
}

static void common_dump(struct sdp_buserr_logger *logger)
{
	int offset;

	common_line();

	printk(KERN_ERR "%s (cnt=%d)\n", logger->name, logger->cnt);

	common_line();

	for (offset=0; offset<logger->size; offset+=16) {
		printk(KERN_ERR "0x%08x : %08x %08x %08x %08x\n", (u32)logger->base+offset, \
			readl(logger->reg+offset), readl(logger->reg+offset+0x4), \
			readl(logger->reg+offset+0x8), readl(logger->reg+offset+0xc));
	}
	common_line();
}

static void common_translate(struct sdp_buserr_logger *logger)
{
	int i, ret;
	u32 val;

	static char strbuf[512];
	char *buf = strbuf;
	int len = sizeof(strbuf)-1;

	for (i = 0; i < BUSERR_N_DATA; i++) {
		if (logger->data[i].name) {
			val = readl(logger->reg + logger->data[i].offset);
			val = val >> logger->data[i].shift;
			val = val & logger->data[i].mask;
			ret = snprintf(buf, len, "%s=0x%x(%s), ", logger->data[i].name, val, \
				val < logger->data[i].n_str ? logger->data[i].str[val] : "NA");
			buf += ret;
			len -= ret;
		}
	}
	*buf = '\0';
	printk(KERN_ERR "%s\n", strbuf);
}

static void common_fillentry(struct sdp_buserr_logger *logger, struct sdp_buserr_entry *entry)
{
	char *src, *dst;
	u32 addr;
	
	src = common_get_str_by_name(logger, "src", 3);
	dst = common_get_str_by_name(logger, "dst", 3);
	addr = common_get_val_by_name(logger, "addr", 4);

	if (src)
		entry->src = src;

	if (dst)
		entry->dst = dst;

	if (src || !strncmp(entry->src, "none", 4))
		entry->addr = addr;
}

static void common_addentry(struct sdp_buserr_entry *entry)
{
	struct sdp_buserr *buserr = get_buserr();
	buserr->param.entries[buserr->param.n_entries++] = *entry;
}

static void common_show(struct sdp_buserr_logger *logger)
{
	u32 val;
	struct sdp_buserr_entry entry = {"none", "none", 0};
	
	common_dump(logger);
	common_translate(logger);
	common_fillentry(logger, &entry);

	val = common_get_val_by_name(logger, "dst", 3);
	if (val < BUSERR_N_SUBS && logger->subs[val]) {
		common_dump(logger->subs[val]);
		common_translate(logger->subs[val]);
		common_fillentry(logger->subs[val], &entry);
		common_clr(logger->subs[val]);
	}

	common_clr(logger);
	common_addentry(&entry);
	mdelay(1);
}

static void common_discard(struct sdp_buserr_logger *logger)
{
	u32 val;
	
	val = common_get_val_by_name(logger, "dst", 3);
	if (val < BUSERR_N_SUBS && logger->subs[val])
		common_clr(logger->subs[val]);
		
	common_clr(logger);
	udelay(100);
}

static bool common_false_detect(struct sdp_buserr_logger *logger)
{
	char *opcode, *errcode, *src, *dst;
	u32 addr;

	opcode  = common_get_str_by_name(logger, "opcode", 6);
	errcode = common_get_str_by_name(logger, "errcode", 7);
	src     = common_get_str_by_name(logger, "src", 3);
	dst     = common_get_str_by_name(logger, "dst", 3);
	addr    = common_get_val_by_name(logger, "addr", 4);

	/* prevent error */
	if (addr >= 0xff0000 && addr < 0xff1000) {
		return true;
	}

	/* false tzasc error by speculation */
	if (!strncmp(opcode, "Wrap Read", 9) &&
		!strncmp(errcode, "DEC err", 7) &&
		!strncmp(src, "mARM_A7QR", 9) &&
		!strncmp(dst, "sXMIF", 5) ) {
		return true;
	}

	return false;
}

static void sdp_buserr_work(struct work_struct *work)
{
	unsigned long flags;
	struct sdp_buserr *buserr = get_buserr();
	int i;

	spin_lock_irqsave(&buserr->lock, flags);

	for (i = 0; i < buserr->n_loggers; i++)
		buserr->loggers[i]->cnt = 0;

	while (common_chk(buserr->loggers[0]) && common_false_detect(buserr->loggers[0]))
		common_discard(buserr->loggers[0]);
	
	spin_unlock_irqrestore(&buserr->lock, flags);

	schedule_delayed_work(&buserr->work, msecs_to_jiffies(1000));
	
	return;
}

int sdp_buserr_show(void)
{
	unsigned long flags;
	struct sdp_buserr *buserr = get_buserr();
	int i;
	
	console_default();

	spin_lock_irqsave(&buserr->lock, flags);
	printk(KERN_ERR "\nsdp_buserr_show start.\n");

	memset(&buserr->param, 0, sizeof(buserr->param));
		
	for (i = 0; i < buserr->n_loggers; i++)
		buserr->loggers[i]->cnt = 0;
		
	for (i = 0; i < buserr->n_loggers; i++) {

		while (common_chk(buserr->loggers[i]))
			common_show(buserr->loggers[i]);
	}

	printk(KERN_ERR "sdp_buserr_show end.\n");
	spin_unlock_irqrestore(&buserr->lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_buserr_show);


/* obsolete codes start */
int sdp_bus_err_logger(void)
{
	return 0;
}
EXPORT_SYMBOL(sdp_bus_err_logger);
/* obsolete codes end */


static char *common_opcode_str[] = {
	"Read", "Wrap Read", "Undefined", "Exclusive Read", "Write", "Wrap Write", "Wrap Write"
};

static char *common_errcode_str[] = {
	"SLV err", "DEC err", "Unsupported", "Power off err", "Security err", "Security err", "Timeout err"
};


/******************************************************************************/
/* sdp1601 / sdp1701                                                          */
/******************************************************************************/

static char *sdp1601_srcid_str[] = {
	"mUSB2_0","mUSB2_1","mUSB2_2","mUSB2_3","mUSB2_4","mARM_A7QR","mARM_A7QW",
	"mARM_DAP","mARM_ETR","mAUD_ADSP0","mAUD_AIO","mAVD","mCHN_0","mCTR_0",
	"mCTR_1","mDMICOM","mDPVE_R","mDPVE_W","mDP_NRC0R_Y","mDP_NRC0W_YC",
	"mDP_SCLR_0","mDP_SCLR_1","mDP_SCLR_2","mDP_SCLR_3","mDP_SCLW_0","mDP_SCLW_1",
	"mDP_SCLW_2","mDVDE_0","mDVDE_1","mDVDE_2","mDVDE_3","mFRC0R_PRE_BIF3",
	"mFRC0W_PRE_BIF1","mFRC1R_POST_BIF3","mFRC1W_PSOT_BIF1","mGA_R0",
	"mGA_W0","mGPU_0","mGPU_1","mGPU_2","mGP_CSR1","mGP_CSR2","mHDMI",
	"mHEN_0","mMFD_H2640","mMFD_H2641","mMFD_JPEG0","mMICOM","mMSP_SPRO",
	"mMSP_TSD","mPERIn","mPERIs","mROT_R0","mROT_W0","mSRP_ISRPM",
};

static char *sdp1601_dstid_lv1_str[] = {
	"SVC_BUS_MAINs","sBUS_SUBn","sBUS_SUBs","sEBUS",
	"sXMIFA0","sXMIFA1","sXMIFA2","sXMIFB0","sXMIFB1",
	"sXMIFB2","sXMIFC0","sXMIFC1","sXMIFC2"
};

static char *sdp1601_dstid_lv2_nm_str[] = {
	"sSCL_00","sSCL_01","sSCL_02","sSCL_03","sSCL_04","sSCL_05","sSCL_06",
	"sSCL_07","sSCL_08","sSCL_09_0","sSCL_09_1","sSCL_09_2","sSCL_09_3",
	"sSCL_09_4","sSCL_10","sSCL_11","sSCL_12","sSCL_MON","Undefined",
	"Undefined","Undefined","Undefined","Undefined","Undefined","Undefined",
	"Undefined","Undefined","Undefined","Undefined","Undefined","Undefined",
	"Undefined","sVE_BIF","sVE_CONTOUR","sVE_DE","sVE_DMS","sVE_GAMMA",
	"sVE_META","sVE_MON","sVE_VE","sVE_WCR","Undefined","Undefined",
	"Undefined","Undefined","Undefined","Undefined","Undefined","sFRC0_BT",
	"sFRC0_INPUT","sFRC0_ME","sFRC0_MON","sFRC0_PRE","sFRC0_PRE_BIF",
	"sFRC0_TMG","Undefined","sNRC0_BIF","sNRC0_DST","sNRC0_FDP","sNRC0_MON",
	"sNRC0_NNR","sNRC0_TNR","Undefined","Undefined","sFRC1_3DF","sFRC1_LD",
	"sFRC1_MC","sFRC1_POST","sFRC1_POST_BIF","Undefined","Undefined",
	"Undefined","sGP_MIX","sGP_MON","sGP_PLN1","sGP_PLN2","sGP_SCR",
	"Undefined","Undefined","Undefined","sDMICOM0","sDMICOM1","sDMICOM_MON",
	"Undefined","sNRC1_ARS","sNRC1_IPC","sNRC1_UCR","Undefined","sNRC2_DB",
	"sNRC2_DR","sNRC2_SHD","Undefined","sGPU_CTRL","sGPU_MON","sGPU_SGC",
	"Undefined","sHDMI_H420","sHDMI_HMON","sHDMI_MON","Undefined",
	"sVx1_ctrl_rx","sVx1_ctrl_tx","sVx1_phy_rx","Undefined","sADSP",
	"sADSP_MON","sAIO","sAIO_MON","sAVD","sAVD_MON","sCHN","sCHN_MON",
	"sDVDE","sDVDE_MON","sGA2D","sGA2D_MON","sHDMI_HDCP","sHDMI_LINK",
	"sHEN","sHEN_MON","sH264_MFD","sH264_MON","sH264_JPEG0","sJPEG_MON",
	"sROT","sROT_MON","sSRP","sSRP_MON","sTCON","sTCON_DEMURA","sTOP_CLKRST",
	"sTOP_PADCTL","SVC_BUS_SUB","SVC_BUS_SUB_tz","sAFE","sCTR_MON",
	"sEBUS_REG","sGPU","sPERI_AXI","Undefined","Undefined","Undefined",
	"Undefined","Undefined",
};

static char *sdp1601_dstid_lv2_sb_str[] = {
	"sATSC","sHCAS","sMSP_MON","sSE0","sSPRO","sTSD","Undefined",
	"Undefined","sMICOM0","sMICOM1","sMICOM_MON","Undefined","sXMIFa_MEMS",
	"sXMIFa_TZC","sXMIFa_TZCID","Undefined","sXMIFb_MEMS","sXMIFb_TZC",
	"sXMIFb_TZCID","Undefined","sXMIFc_MEMS","sXMIFc_TZC","sXMIFc_TZCID",
	"Undefined","sCYS_MON","sCYS_REG","sTOP_CLKRST","sTOP_PADCTL","sUSB2",
	"sUSB_MON","SVC_BUS_SUB","SVC_BUS_SUB_TZ","sCYS","sCYS_GIC","sCYS_SNP",
	"sCYS_STM","sPERI_AXI","sXMIFau_APB","sXMIFbu_APB","sXMIFcu_APB",
};

static struct sdp_buserr_logger sdp1601_buserr_sub_nm = {
	"sdp1601-buserr-sub-nm",
	0xf84000, 
	0x40,
	{
		{ "opcode",   0x14,  1,   0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,   0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "dst(lv2)", 0x18,  6,  0xff, sdp1601_dstid_lv2_nm_str, ARRAY_SIZE(sdp1601_dstid_lv2_nm_str) },
		{ "addr",     0x20,  0,  ~0ul, NULL, 0 },
	},
	{ NULL, },
};

static struct sdp_buserr_logger sdp1601_buserr_sub_sb = {
	"sdp1601-buserr-sub-sb",
	0xf74000, 
	0x40,
	{
		{ "opcode",   0x14,  1,   0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,   0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "dst(lv2)", 0x18,  7,  0x3f, sdp1601_dstid_lv2_sb_str, ARRAY_SIZE(sdp1601_dstid_lv2_sb_str) },
		{ "addr",     0x20,  0,  ~0ul, NULL, 0 },
	},
	{ NULL, },
};

static struct sdp_buserr_logger sdp1601_buserr_main = {
	"sdp1601-buserr-main",
	0xfe8000, 
	0x40,
	{
		{ "opcode",   0x14,  1,  0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,  0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "src",      0x18, 11, 0x3f, sdp1601_srcid_str, ARRAY_SIZE(sdp1601_srcid_str) },
		{ "dst(lv1)", 0x18,  7,  0xf, sdp1601_dstid_lv1_str, ARRAY_SIZE(sdp1601_dstid_lv1_str) },
		{ "addr",     0x20,  0, ~0ul, NULL, 0 },
	},
	{ NULL, &sdp1601_buserr_sub_nm, &sdp1601_buserr_sub_sb, },
};


/******************************************************************************/
/* sdp1803 / sdp1803                                                          */
/******************************************************************************/

static char *sdp1803_srcid_str[] = {
	"mUSB2_0", "mUSB2_1", "mUSB2_2", "mUSB2_3", "mUSB2_4", "mAMIC", "mARM_A7QR", 
	"mARM_A7QW", "mARM_DAP", "mARM_ETR", "mAUD_ADSP", "mAUD_AIO", "mAVD", "mCHN", 
	"mCTR0", "mCTR1", "mDMIC", "mDVDE0", "mDVDE1", "mDVDE2", "mDVDE3", "mFRC0R", 
	"mFRC0W", "mFRC1R", "mFRC1W", "mGAR", "mGAW", "mGP0", "mGP1", "mGPUR", "mGPUW", 
	"mHDMI", "mHEN", "mJPEG", "mMFD0", "mMFD1", "mMSP_SPRO", "mMSP_SPRO_AHB", 
	"mMSP_TSD", "mNRCR", "mNRCW", "mPERIn", "mPERIs", "mRBER", "mRBEW", "mROTR", 
	"mROTW", "mSCLR0", "mSCLR1", "mSCLR2", "mSCLR3", "mSCLW0", "mSCLW1", "mSCLW2", "mSRP",
};

static char *sdp1803_dstid_lv1_str[] = {
	"SVC_BUS_MAIN", "sBUS_SUB", "sEBUS", 
	"sXMIFC0", "sXMIFC1", "sXMIFC2", "sXMIFD0", "sXMIFD1", "sXMIFD2",
};

static char *sdp1803_dstid_lv2_str[] = {
	"sHDMI_HDCP", "sHDMI_LINK_P0", "sHDMI_LINK_P1", "sHDMI_LINK_P2", "sHDMI_LINK_P3", 
	"-", "-", "-", "sMSP_ATSC", "sMSP_HCAS", "sMSP_MON", "sMSP_SE", "sMSP_TSD", 
	"-", "-", "-", "sAUD_AIO", "sAUD_AIO_MON", "sAUD_SPI", "sAUD_SPI_DMA", 
	"sFRC_MC", "sFRC_ME", "sFRC_MON", "sFRC_POST", "sAMIC0", "sAMIC1", "sAMIC_MON", 
	"-", "sDMIC0", "sDMIC1", "sDMIC_MON", "-", "sRBE", "sRBE_BIF", "sRBE_MON", "-", 
	"sXMIFc_MEMS", "sXMIFc_TZC", "sXMIFc_TZCID", "-", "sXMIFd_MEMS", 
	"sXMIFd_TZC", "sXMIFd_TZCID", "-", "sAUD_ADSP", "sAUD_ADSP_MON", "sAVD", "sAVD_MON", 
	"sCHN", "sCHN_MON", "sCYS_MON", "sCYS_PMU", "sDVDE", "sDVDE_MON", "sGP", "sGP_MON", 
	"sGPU_CTRL", "sGPU_MON", "sHDMI_HMON", "sHDMI_MON", "sHEN", "sHEN_MON", 
	"sJPEG", "sJPEG_MON", "sMFD", "sMFD_MON", "sNRC", "sNRC_MON", "sROT", "sROT_MON", 
	"sSCL", "sSCL_MON", "sSERDES_LINK", "sSERDES_PHY", "sSRP", "sSRP_MON", 
	"sTCON", "sTCON_DEMURA", "sTOP_CLKRST", "sTOP_PADCTRL", "sUSB", "sUSB_MON", 
	"sVE", "sVE_GQE", "SVC_BUS_SUB", "SVC_BUS_SUB_tz", "sAFE", "sAMIC_EDID", "sARM_SNP", 
	"sCTR", "sCYS", "sCYS_GIC", "sCYS_STM", "sDSCD", "sEBUS_REG", "sGA", "sGPU", 
	"sMSP_SPRO", "sPERIn", "sPERIs", "sPRE", "sTCON_VX1", "sXMIFcu_APB", "sXMIFdu_APB", 
};

static struct sdp_buserr_logger sdp1803_buserr_sub = {
	"sdp1803-buserr-sub",
	0xf84000, 
	0x40,
	{
		{ "opcode",   0x14,  1,  0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,  0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "dst(lv2)", 0x18,  7,  0x7f, sdp1803_dstid_lv2_str, ARRAY_SIZE(sdp1803_dstid_lv2_str) },
		{ "addr",	  0x20,  0,  ~0ul, NULL, 0 },
	},
	{ NULL, },
};

static struct sdp_buserr_logger sdp1803_buserr_main = {
	"sdp1803-buserr-main",
	0x4fe8000, 
	0x40,
	{
		{ "opcode",   0x14,  1,  0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,  0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "src",      0x18, 15, 0x3f, sdp1803_srcid_str, ARRAY_SIZE(sdp1803_srcid_str) },
		{ "dst(lv1)", 0x18, 11,  0xf, sdp1803_dstid_lv1_str, ARRAY_SIZE(sdp1803_dstid_lv1_str) },
		{ "addr",     0x20,  0, ~0ul, NULL, 0 },
	},
	{ NULL, &sdp1803_buserr_sub, &sdp1803_buserr_sub, },
};

static char *sdp1804_srcid_str[] = {
	"mUSB2_0", "mUSB2_1", "mUSB2_2", "mUSB2_3", "mAMIC", "mARM_A7QR", 
	"mARM_A7QW", "mARM_DAP", "mARM_ETR", "mAUD_ADSP", "mAUD_AIO", "mAVD", "mCHN", 
	"mCTR0", "mCTR1", "mDMIC", "mDVDE0", "mDVDE1", "mDVDE2", "mDVDE3", "mFRC0R", 
	"mFRC0W", "mFRC1R", "mFRC1W", "mGAR", "mGAW", "mGP0", "mGP1", "mGPUR", "mGPUW", 
	"mHDMI", "mHEN", "mJPEG", "mMFD0", "mMFD1", "mMSP_SPRO", "mMSP_SPRO_AHB", 
	"mMSP_TSD", "mNRCR", "mNRCW", "mPERIn", "mPERIs", "mRBER", "mRBEW", "mROTR", 
	"mROTW", "mSCLR0", "mSCLR1", "mSCLR2", "mSCLR3", "mSCLW0", "mSCLW1", "mSCLW2", "mSRP",
};

static struct sdp_buserr_logger sdp1804_buserr_main = {
	"sdp1804-buserr-main",
	0x4fe8000, 
	0x40,
	{
		{ "opcode",   0x14,  1,  0xf, common_opcode_str, ARRAY_SIZE(common_opcode_str) },
		{ "errcode",  0x14,  8,  0x7, common_errcode_str, ARRAY_SIZE(common_errcode_str) },
		{ "src",      0x18, 15, 0x3f, sdp1804_srcid_str, ARRAY_SIZE(sdp1804_srcid_str) },
		{ "dst(lv1)", 0x18, 11,  0xf, sdp1803_dstid_lv1_str, ARRAY_SIZE(sdp1803_dstid_lv1_str) },
		{ "addr",     0x20,  0, ~0ul, NULL, 0 },
	},
	{ NULL, &sdp1803_buserr_sub, &sdp1803_buserr_sub, },
};


static BLOCKING_NOTIFIER_HEAD(sdp_buserr_notifier);

int sdp_buserr_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sdp_buserr_notifier, nb);
}
EXPORT_SYMBOL(sdp_buserr_register_notifier);

int sdp_buserr_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&sdp_buserr_notifier, nb);
}
EXPORT_SYMBOL(sdp_buserr_unregister_notifier);

static int sdp_buserr_call_notifier(void)
{
	int ret;
	printk(KERN_ERR "sdp_buserr user callback start.\n");
	ret = blocking_notifier_call_chain(&sdp_buserr_notifier, 0, &(get_buserr()->param));
	printk(KERN_ERR "sdp_buserr user callback end.\n");
	return ret;
}

static int do_v7_async_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	sdp_buserr_show();
	sdp_buserr_call_notifier();
	return 1;
}





/* start sdp1803 USB buserr debug ****************************/
int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);
static void common_register_dump(u32 phy_addr, u32 size)
{
	void __iomem *addr;
	u32 val;
	int offset, cnt, ret;

	char strbuf[512];
	char *buf;
	int len;

	if(!size) return;

	addr = ioremap(phy_addr, size);
	offset=0;

	do {
		buf = strbuf;
		len = sizeof(strbuf)-1;
		ret = snprintf(buf, len, "0x%08x :", phy_addr+offset);
		buf += ret;
		len -= ret;

		cnt = 0;
		do {
			ret = snprintf(buf, len, " %08x", readl(addr+offset));
			buf += ret;
			len -= ret;

			offset+=0x4;
			cnt++;
		}while(cnt < 8 && offset < size);
		
		*buf = '\0';
		printk(KERN_ERR "%s\n", strbuf);
		
	}while(offset < size);
	
	iounmap(addr);
}
static void sdp1803_private_usb_dump(void)
{
	const u32 USB_PLL_RST = 0x9C1248;
	const u32 USB_PHY_RST = 0xF507F0;
	const u32 USB_PHY_CTRL = 0x9C0900;
	const u32 USB_PHY_CTRL_SIZE = 0x90;
	const u32 USB_LINK_CTRL = 0x4D0000;
	const u32 USB_LINK_CTRL_SIZE = 0x50;
	const u32 USB_EHCI_PORT_A = 0x480000;
	const u32 USB_EHCI_PORT_B = 0x490000;
	const u32 USB_EHCI_PORT_C = 0x4A0000;
	const u32 USB_EHCI_PORT_E = 0x4C0000;
	const u32 USB_EHCI_PORT_SIZE = 0x60;		
	common_line();

	printk(KERN_ERR "private_usb_dump  DUMP\n");
	common_line();

	common_register_dump(USB_PLL_RST, 0x10);
	common_register_dump(USB_PHY_RST, 0x8);
	common_register_dump(USB_PHY_CTRL, USB_PHY_CTRL_SIZE);

	sdp_set_clkrst_mux(USB_PHY_RST+0x4, 0x01000000, 0x00000000);
	mdelay(1);
	sdp_set_clkrst_mux(USB_PHY_RST+0x4, 0x01000000, 0x01000000);
	common_register_dump(USB_LINK_CTRL, USB_LINK_CTRL_SIZE);
	common_register_dump(USB_EHCI_PORT_A, USB_EHCI_PORT_SIZE);
	common_register_dump(USB_EHCI_PORT_B, USB_EHCI_PORT_SIZE);
	common_register_dump(USB_EHCI_PORT_C, USB_EHCI_PORT_SIZE);
	if (soc_is_sdp1803())
		common_register_dump(USB_EHCI_PORT_E, USB_EHCI_PORT_SIZE);
	
	common_line();
}
static void sdp1803_private_arm_dump(void)
{
	printk(KERN_ERR "private_arm_dump\n");
	common_register_dump(0x440000, 0x40);
}

static int sdp1803_buserr_callback(struct notifier_block *notifier, unsigned long val, void *data)
{
	struct sdp_buserr_param *param = data;
	int i;
	pr_err("[sdp1803_buserr_callabck] n_entries = %d\n", param->n_entries);
	for (i = 0; i < param->n_entries; i++) {
		pr_err("[sdp1803_buserr_callabck] %d src=%s, dst=%s, addr=%08x\n",
			i, param->entries[i].src, param->entries[i].dst, param->entries[i].addr);

		if (!strncmp(param->entries[i].dst, "sUSB", 4) || 
			(param->entries[i].addr >= 0x480000 && param->entries[i].addr < 0x4D0050)) {
			sdp1803_private_usb_dump();
		}
		if (!strncmp(param->entries[i].src, "mARM_A7Q", 8) && (param->entries[i].addr == 0xff0000)) {
			sdp1803_private_arm_dump();
		}
	}
	return NOTIFY_OK;
}
static struct notifier_block sdp1803_buserr_nb = {
	.notifier_call = sdp1803_buserr_callback,
};
/* end of sdp1803 USB buserr debug ****************************/






static int __init sdp_buserr_init(void)
{
	struct sdp_buserr *buserr = get_buserr();

	spin_lock_init(&buserr->lock);

	if (soc_is_sdp1601()) {
		sdp_buserr_add_logger(&sdp1601_buserr_main);
		sdp_buserr_add_logger(&sdp1601_buserr_sub_nm);
		sdp_buserr_add_logger(&sdp1601_buserr_sub_sb);
	}
	else if (soc_is_sdp1803())  {
		sdp_buserr_add_logger(&sdp1803_buserr_main);
		sdp_buserr_add_logger(&sdp1803_buserr_sub);
		sdp_buserr_register_notifier(&sdp1803_buserr_nb);
	}
	else if (soc_is_sdp1804())  {
		sdp_buserr_add_logger(&sdp1804_buserr_main);
		sdp_buserr_add_logger(&sdp1803_buserr_sub);
		sdp_buserr_register_notifier(&sdp1803_buserr_nb);
	}

#ifdef CONFIG_ARM_LPAE
	hook_fault_code(17, do_v7_async_abort, SIGBUS, 0, "asynchronous external abort");
#else
	hook_fault_code(22, do_v7_async_abort, SIGBUS, BUS_OBJERR, "imprecise external abort");
#endif

	INIT_DELAYED_WORK(&buserr->work, sdp_buserr_work);
	schedule_delayed_work(&buserr->work, msecs_to_jiffies(1000));

	return 0;
}

subsys_initcall(sdp_buserr_init);

MODULE_AUTHOR("yongjin79.kim@samsung.com");
MODULE_DESCRIPTION("driver for sdp bus error logger");


