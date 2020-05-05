#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <mach/soc.h>

#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>

/* for micom control */
extern void sdp_micom_uart_init(unsigned int base, int port);
extern void sdp_micom_request_suspend(void);
extern void sdp_micom_request_poweroff(void);

extern void sdp1302_wait_for_die(void);
extern void sdp1304_wait_for_die(void);
extern void sdp1307_wait_for_die(void);

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
		aligned_size = (size + mask) & (~mask);
	}
	
	BUG_ON(aligned_size > part_size || size > part_size);

	ret = vfs_llseek(fp, part_size - aligned_size, SEEK_SET);
	if(ret <= 0)    
	{
		pr_err("suspend : Error in vfs_llseek!!!\n");
	}

	vfs_read(fp, (char *) data, (size_t)size, &fp->f_pos);
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
		aligned_size = (size + mask) & (~mask);
	}

	BUG_ON(aligned_size > part_size || size > part_size);

	ret = vfs_llseek(fp, part_size - aligned_size, SEEK_SET);
	if(ret <= 0)    {
                pr_err("suspend : Error in vfs_llseek!!!\n");
        }

	ret = vfs_write(fp, (char *) data, size, &fp->f_pos);
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

static int sdp_micom_port, sdp_uart_base;

static void sdp_poweroff(void)
{
	int i;

	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);

	for(i = 0; i < 20 ; i++)
	{
		printk("\n\n<<<<<< send power off cmd to micom >>>>>>\n\n\n");
		sdp_micom_request_poweroff();
		
		mdelay(100);
	}
}

/* sdp1304 */
#define SDP1304_MICOM_PORT	2
#define SDP1304_DDRSAVE_PART "/dev/mmcblk0p12"
#define SDP1304_DDRSAVE_PART_SIZE		8192

#define SDP1304_DDRPHY0_BASE	((void __iomem *) 0xFE414000)
#define SDP1304_DDRPHY1_BASE	((void __iomem *) 0xFE494000)

#define SDP1304_DDRPHY_OFFSETR	0x10
#define SDP1304_DDRPHY_OFFSETW	0x18
#define SDP1304_DDRPHY_CON01	0x4
#define SDP1304_DDRPHY_CON03	0xC
#define SDP1304_DDRPHY_CON05	0x14
#define SDP1304_DDRPHY_CON08	0x20
#define SDP1304_DDRPHY_CON13	0x34
#define SDP1304_DDRPHY_CON19	0x50
#define SDP1304_DDRPHY_CON20	0x54

#define SAVE_REG(x, y)	sdp_save_peri.x = readl((void *) y + DIFF_IO_BASE0)
#define LOAD_REG(x, y)	writel(sdp_save_peri.x, ((void *) y + DIFF_IO_BASE0))
#define SAVE_REGS(x, y, n)	do { static int i=0; for(i = 0; i < n ; i++) sdp_save_peri.x[i] = readl((void *) y + DIFF_IO_BASE0 + i * 4);} while(0)
#define LOAD_REGS(x, y, n)	do { static int i=0; for(i = 0; i < n ; i++) writel(sdp_save_peri.x[i], ((void *) y + DIFF_IO_BASE0 + i * 4));} while(0)

#define UART_ULCON	0x10090A00
#define	UART_UCON	0x10090A04
#define UART_UFCON	0x10090A08
#define UART_UBRDIV	0x10090A28

#define TIMER_CON1	0x10090410
#define TIMER_DATA1	0x10090414
#define TIMER_DATAL1	0x10090498
#define TIMER_DATAL1	0x10090498
#define TIMER_CNTL1	0x10090498

#define GIC_DIST_CTRL	0x10F81000
#define GIC_DIST_ENABLE	0x10F81100
#define GIC_DIST_PRI	0x10F81400
#define GIC_DIST_TARGET	0x10F81800

#define GIC_CPU_CTRL	0x10F82000
#define GIC_CPU_PRIMASK	0x10F82004

static struct sdp1304_save_peri_t {
	u32 uart_ulcon;
	u32 uart_ucon;
	u32 uart_ufcon;
	u32 uart_ubrdiv;

	u32 gic_dist_ctrl;
	u32 gic_dist_enable[32];
	u32 gic_dist_pri[32];
	u32 gic_dist_target[40];
	u32 gic_cpu_ctrl;
	u32 gic_cpu_primask;
} sdp_save_peri;

static void sdp1304_save_uart(void)
{
	SAVE_REG(uart_ulcon, UART_ULCON);
	SAVE_REG(uart_ucon, UART_UCON);
	SAVE_REG(uart_ufcon, UART_UFCON);
	SAVE_REG(uart_ubrdiv, UART_UBRDIV);
}

static void sdp1304_load_uart(void)
{
	LOAD_REG(uart_ulcon, UART_ULCON);
	LOAD_REG(uart_ucon, UART_UCON);
	LOAD_REG(uart_ufcon, UART_UFCON);
	LOAD_REG(uart_ubrdiv, UART_UBRDIV);
}

#if 0
static void sdp1202_save_timer(void)
{
	SAVE_REG(timer_con1, TIMER_CON1);
	SAVE_REG(timer_data1, TIMER_DATA1);
	SAVE_REG(timer_datal1, TIMER_DATAL1);
}

static void sdp1202_load_timer(void)
{
	LOAD_REG(timer_con1, TIMER_CON1);
	LOAD_REG(timer_data1, TIMER_DATA1);
	LOAD_REG(timer_datal1, TIMER_DATAL1);
}
#endif

static void sdp1304_save_gic(void)
{
	SAVE_REG(gic_dist_ctrl, GIC_DIST_CTRL);
	SAVE_REGS(gic_dist_enable, GIC_DIST_ENABLE, 32);
	SAVE_REGS(gic_dist_pri, GIC_DIST_PRI, 32);
	SAVE_REGS(gic_dist_target, GIC_DIST_TARGET, 40);	
	SAVE_REG(gic_cpu_ctrl, GIC_CPU_CTRL);
	SAVE_REG(gic_cpu_primask, GIC_CPU_PRIMASK);	
}
static void sdp1304_load_gic(void)
{
	writel(0, ((void *) GIC_DIST_ENABLE + DIFF_IO_BASE0));
	LOAD_REGS(gic_dist_pri, GIC_DIST_PRI, 32);
	LOAD_REG(gic_dist_ctrl, GIC_DIST_CTRL);
	LOAD_REGS(gic_dist_target, GIC_DIST_TARGET, 40);	
	LOAD_REG(gic_cpu_primask, GIC_CPU_PRIMASK);	
	LOAD_REG(gic_cpu_ctrl, GIC_CPU_CTRL);
	LOAD_REGS(gic_dist_enable, GIC_DIST_ENABLE, 32);
}

static int sdp1304_get_ddr_param(void __iomem *base, u32 *values)
{
	u32 GCCA, GCC, RVWMC, WVWMC;

	writel(0xD, base + SDP1304_DDRPHY_CON05);
	GCCA = readl(base + SDP1304_DDRPHY_CON19);
	writel(0xE, base + SDP1304_DDRPHY_CON05);
	GCC = readl(base + SDP1304_DDRPHY_CON20);
	writel(8, base + SDP1304_DDRPHY_CON05);
	RVWMC = readl(base + SDP1304_DDRPHY_CON19);
	writel(9, base + SDP1304_DDRPHY_CON05);
	WVWMC = readl(base + SDP1304_DDRPHY_CON19);

	values[0] = GCCA;
	values[1] = GCC;
	values[2] = RVWMC;
	values[3] = WVWMC;

	pr_info("GCCA=0x%08X GCC=0x%08X RVWMC=0x%08X WVWMC=0x%08X\n", GCCA, GCC, RVWMC, WVWMC);

	return 0;
}

static int sdp1304_save_ddr_param(void)
{
	u32 values[8], saved_values[8];
	int ret;

	sdp1304_get_ddr_param(SDP1304_DDRPHY0_BASE, &values[0]);
	sdp1304_get_ddr_param(SDP1304_DDRPHY1_BASE, &values[4]);

	ret = sdp_read_mmc(SDP1304_DDRSAVE_PART, saved_values,
			SDP1304_DDRSAVE_PART_SIZE, sizeof(values));
	if(ret < 0)
	{
		pr_err("suspend : Reading DDR value from flash is failed!!!");
		return ret;
	}

	if(memcmp(values, saved_values, sizeof(values)) == 0)		//if same, skip writting
		return 0;

	if(values[1] == 0)
	{
		pr_info("suspend : maybe not cold booting.. skip save ddr param\n");
		return 0;
	}

	ret = sdp_write_mmc(SDP1304_DDRSAVE_PART, values,
			SDP1304_DDRSAVE_PART_SIZE, sizeof(values));
	if(ret < 0)
	{
		pr_err("suspend : Writing DDR value from flash is failed!!!");
		return ret;
	}
	
	return ret;
}

/*******************************
 * SDP1302 GOLF-S
 *******************************/
#define SDP1302_MICOM_PORT		0
#define SDP1302_DDRSAVE_PART		"/dev/mmcblk0p12"
#define SDP1302_DDRSAVE_PART_SIZE	8192
#define SDP1302_DDRSAVE_PART_ALIGN	9
#define SDP1302_PCTL0_BASE		0x18418000
#define SDP1302_PCTL1_BASE		0x18428000

/* policy */
#undef CONFIG_SDP_PM_SUSPEND_POLICY_COLDBOOT
#undef CONFIG_SDP_PM_SUSPEND_POLICY_DIFF
#define CONFIG_SDP_PM_SUSPEND_POLICY_ONETIME

#define SDP1302_SUSPEND_DEBUG

/* at RAM */
struct sdp1302_suspend_save {
	u32     entry_point;
	u32     size;
#define SDP_SF_COLDBOOT		1
	u32	flags;
#ifdef SDP1302_SUSPEND_DEBUG
	u32	pattern[512];
#endif
};

/* at MMC */
struct regset {
	u32	addr;
	u32	val;
};
struct sdp1302_suspend_context_hdr {
	u32     size;
	u32	crc32;
};
struct sdp1302_suspend_context_msg {
	u32 context_in_ram;	/* struct sdp1302_suspend_save */
	u32 n_pairs;
	struct regset regsets[0];
};
struct sdp1302_suspend_context {
	struct sdp1302_suspend_context_hdr hdr;
	struct sdp1302_suspend_context_msg msg;
};

static struct sdp1302_suspend_save sdp1302_sarea;

static void __iomem *sdp1302_pctl0_regs;
static void __iomem *sdp1302_pctl1_regs;

static const u32 sdp1302_reglist[] = {
	0x184285e0,
	0x18428620,
	0x18428660,
	0x184286a0,

	0x18418508,	/* 20 Feb 2014 */
	0x184185e0,
	0x18418620,

	0x184185CC,
	0x184185D0,
	0x184185D8,
	0x184185DC,
	0x184185E4,
	0x1841860C,
	0x18418610,
	0x18418618,
	0x1841861C,
	0x18418624,
	
	0x18428508,	/* 20 Feb 2014 */

	0x184285CC,
	0x184285D0,
	0x184285D8,
	0x184285DC,
	0x184285E4,
	0x1842860C,
	0x18428610,
	0x18428618,
	0x1842861C,
	0x18428624,
	0x1842864C,
	0x18428650,
	0x18428658,
	0x1842865C,
	0x18428664,
	0x1842868C,
	0x18428690,
	0x18428698,
	0x1842869C,
	0x184286A4,
};

static size_t sdp1302_context_msg_size(void)
{
	return sizeof(struct sdp1302_suspend_context_msg) +
		(ARRAY_SIZE(sdp1302_reglist) * sizeof(struct regset));
}
static size_t sdp1302_context_size(void)
{
	return sizeof(struct sdp1302_suspend_context_hdr) +
			sdp1302_context_msg_size();
}

#ifdef SDP1302_SUSPEND_DEBUG
static void sdp1302_dump_context(const char *msg, struct sdp1302_suspend_context *ctx)
{
	int i;
	u32 *ptr = (u32*)ctx;
	pr_info("%s:\n", msg);
	for (i=0; i < (sdp1302_context_size() / sizeof(u32)); i++) {
		pr_info("%08x\n", ptr[i]);
	}
}
#else
static void sdp1302_dump_context(const char *msg, struct sdp1302_suspend_context *ctx) {}
#endif

static void sdp1302_get_current_context(struct sdp1302_suspend_context_msg *ctx)
{
	int i;
	for (i=0; i<ARRAY_SIZE(sdp1302_reglist); i++) {
		void *addr;
		if ((sdp1302_reglist[i] >> 16) == 0x1841)
			addr = sdp1302_pctl0_regs + (sdp1302_reglist[i] - SDP1302_PCTL0_BASE);
		else if ((sdp1302_reglist[i] >> 16) == 0x1842)
			addr = sdp1302_pctl1_regs + (sdp1302_reglist[i] - SDP1302_PCTL1_BASE);
		else
			addr = (void*)0xe;	/* fault */
		
		ctx->regsets[i].addr = sdp1302_reglist[i];
		ctx->regsets[i].val = readl(addr);
	}
	ctx->n_pairs = ARRAY_SIZE(sdp1302_reglist);
	ctx->context_in_ram = (u32)virt_to_phys(&sdp1302_sarea);
}

static struct sdp1302_suspend_context *sdp1302_alloc_context(void)
{
	struct sdp1302_suspend_context *ctx = kmalloc(sdp1302_context_size(), GFP_KERNEL);
	return ctx;
}

#if defined(CONFIG_SDP_PM_SUSPEND_POLICY_DIFF)
static int sdp1302_need_save_context(struct sdp1302_suspend_context *ctx_cur)
{
	int ret;
	struct sdp1302_suspend_context *ctx = sdp1302_alloc_context();
	
	ret = sdp_read_mmc_aligned(SDP1302_DDRSAVE_PART, (u32*)ctx,
			SDP1302_DDRSAVE_PART_SIZE, sdp1302_context_size(),
			SDP1302_DDRSAVE_PART_ALIGN);
	if (ret < 0) {
		pr_err("suspend: failed to read suspend area\n");
		return ret;
	}

	kfree(ctx);	
	return !!(memcmp(&ctx_cur->msg, &ctx->msg, sdp1302_context_msg_size()));
}
#elif defined(CONFIG_SDP_PM_SUSPEND_POLICY_ONETIME)
static int sdp1302_need_save_context(struct sdp1302_suspend_context *ctx_cur)
{
	int ret;
	struct sdp1302_suspend_context *ctx = sdp1302_alloc_context();
	u32 crc32;
	
	ret = sdp_read_mmc_aligned(SDP1302_DDRSAVE_PART, (u32*)ctx,
			SDP1302_DDRSAVE_PART_SIZE, sdp1302_context_size(),
			SDP1302_DDRSAVE_PART_ALIGN);
	if (ret < 0) {
		pr_err("suspend: failed to read suspend area\n");
		kfree(ctx);
		return ret;
	}

	sdp1302_dump_context("saved context", ctx);

	/* basic sanity check */
	crc32 = crc32(0, &ctx->msg, sdp1302_context_msg_size());

	ret = 0;	

	if (ctx->hdr.size != ctx_cur->hdr.size) {
		pr_warn("wrong size\n");
		ret = 1;
	} else if (ctx->hdr.crc32 != crc32) {
		pr_warn("bad crc\n");
		ret = 1;
	} else if (ctx->msg.context_in_ram != ctx_cur->msg.context_in_ram) {
		pr_warn("ram context address changed\n");
		ret = 1;
	} else {
		struct sdp1302_suspend_save *ram1, *ram2;
		ram1 = phys_to_virt(ctx->msg.context_in_ram);
		ram2 = phys_to_virt(ctx_cur->msg.context_in_ram);
		if (ram1->entry_point != ram2->entry_point) {
			pr_warn("entry point changed.\n");
			ret = 1;
		}
		if (ram1->size != ram2->size) {
			pr_warn("ram context wrong size\n");
			ret = 1;
		}
	}
	kfree(ctx);

	return ret;
}
#elif defined(CONFIG_SDP_PM_SUSPEND_POLICY_COLDBOOT)
static int sdp1302_need_save_context(struct sdp1302_suspend_context *ctx_cur)
{
	return !!(sdp1302_sarea.flags & SDP_SF_COLDBOOT);
}
#else
static int sdp1302_need_save_context(struct sdp1302_suspend_context *ctx_cur)
{
	return 1;
}
#endif

static int sdp1302_suspend_begin(suspend_state_t state)
{
	int ret = 0;

	if(state == PM_SUSPEND_MEM) {
		struct sdp1302_suspend_context *ctx_cur = sdp1302_alloc_context();
		sdp1302_get_current_context(&ctx_cur->msg);
		ctx_cur->hdr.size = sdp1302_context_msg_size();
		ctx_cur->hdr.crc32 = crc32(0, &ctx_cur->msg, ctx_cur->hdr.size);
		sdp1302_dump_context("running context", ctx_cur);

		ret = sdp1302_need_save_context(ctx_cur);
		WARN_ON(ret < 0);
		if (ret < 0) {
			kfree(ctx_cur);
			return ret;
		}
#ifdef SDP1302_SUSPEND_DEBUG
		if (ret)
			pr_info("runnning context need to be saved to mmc.\n");
		else
			pr_info("no need to saved to mmc.\n");

		pr_info("context in ram = %p, %dbytes, entry = %p(%pf)\n",
				&sdp1302_sarea, sizeof(sdp1302_sarea), cpu_resume, cpu_resume);
		memset(sdp1302_sarea.pattern, 0x5a, sizeof(sdp1302_sarea.pattern));
#endif
		if (ret == 1) {
			ret = sdp_write_mmc_aligned(SDP1302_DDRSAVE_PART, (u32*)ctx_cur,
					SDP1302_DDRSAVE_PART_SIZE, sdp1302_context_size(),
					SDP1302_DDRSAVE_PART_ALIGN);
		}
		kfree(ctx_cur);
	}
	return ret;
}

static int notrace __sdp1302_suspend_enter(unsigned long unused)
{
	sdp1302_sarea.flags &= ~SDP_SF_COLDBOOT;
	sdp1302_sarea.entry_point = virt_to_phys(cpu_resume);

	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);
	sdp_micom_request_suspend();

	soft_restart((unsigned long) virt_to_phys(sdp1302_wait_for_die));

	return 0;
}

static int sdp1304_suspend_begin(suspend_state_t state)
{
	if(state == PM_SUSPEND_MEM) {
		sdp1304_save_ddr_param();
	}
	return 0;
}

static int notrace __sdp1304_suspend_enter(unsigned long unused)
{
	BUG_ON(!sdp_sarea);

	/* save resume address */	
	sdp_sarea->entry_point =(u32)virt_to_phys(cpu_resume);

	/* send micom suspend off */
	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);
	sdp_micom_request_suspend();

	soft_restart((unsigned long)virt_to_phys(sdp1304_wait_for_die));

	return 0;
}

static int sdp_suspend_begin(suspend_state_t state)
{
	pr_info("sdp_suspend_begin : state=%d\n", state);

	if(soc_is_sdp1304())
		return sdp1304_suspend_begin(state);
	else if(soc_is_sdp1302())
		return sdp1302_suspend_begin(state);
	
	return 0;
}

static int sdp_suspend_enter(suspend_state_t state)
{
	if(soc_is_sdp1304())
		cpu_suspend(0, __sdp1304_suspend_enter);
	else if(soc_is_sdp1302())
		cpu_suspend(0, __sdp1302_suspend_enter);
	return 0;
}

static const struct platform_suspend_ops sdp_suspend_ops = {
	.begin		= sdp_suspend_begin,
	.enter		= sdp_suspend_enter,
	.valid		= suspend_valid_only_mem,
};

int __init sdp_suspend_init(void)
{
	pr_info ("SDP suspend support.\n");
	
	suspend_set_ops(&sdp_suspend_ops);
	pm_power_off = sdp_poweroff;

	if(soc_is_sdp1304()) {
		sdp_sarea = ioremap((PHYS_OFFSET)-0x100, 0x100);
		sdp_uart_base = 0x10090A00;
		sdp_micom_port = SDP1304_MICOM_PORT;
	} else if(soc_is_sdp1302()) {
		sdp1302_sarea.size = sizeof(sdp1302_sarea);
		sdp1302_sarea.flags = SDP_SF_COLDBOOT;
		sdp_uart_base = 0x10090A00;
		sdp_micom_port = SDP1302_MICOM_PORT;

		sdp1302_pctl0_regs = ioremap(SDP1302_PCTL0_BASE, 0x1000);
		sdp1302_pctl1_regs = ioremap(SDP1302_PCTL1_BASE, 0x1000);

		BUG_ON(!sdp1302_pctl0_regs || !sdp1302_pctl1_regs);
	}
	
	return 0;
}
late_initcall(sdp_suspend_init);

/* pm_syscore */
struct sdp1302_uart_context {
	u32 ulcon;
	u32 ucon;
	u32 ufcon;
	u32 ubrdiv;
};

struct sdp1302_syscore_context {
	struct sdp1302_uart_context uart[4];
} sdp1302_syscore_context;

static void sdp1302_save_uart_context(void* base, struct sdp1302_uart_context *ctx)
{
	ctx->ulcon = readl(base + 0x00);
	ctx->ucon = readl(base + 0x04);
	ctx->ufcon = readl(base + 0x08);
	ctx->ubrdiv = readl(base + 0x28);
}

static void sdp1302_restore_uart_context(void *base, struct sdp1302_uart_context *ctx)
{
	writel(ctx->ulcon, base + 0x00);
	writel(ctx->ufcon, base + 0x08);
	writel(ctx->ubrdiv, base + 0x28);
	writel(ctx->ucon, base + 0x04);
}

static void sdp1302_pm_suspend(void)
{
	void *regs;
	regs = (void*)0xfe090a00;
	sdp1302_save_uart_context(regs + 0x00, &sdp1302_syscore_context.uart[0]);
	sdp1302_save_uart_context(regs + 0x40, &sdp1302_syscore_context.uart[1]);
	sdp1302_save_uart_context(regs + 0x80, &sdp1302_syscore_context.uart[2]);
	sdp1302_save_uart_context(regs + 0xc0, &sdp1302_syscore_context.uart[3]);
}

extern void sdp_scu_enable(void);

static void sdp1302_pm_resume(void)
{
	void *regs;

	/* UART */
	regs = (void*)0xfe090a00;
	sdp1302_restore_uart_context(regs + 0x00, &sdp1302_syscore_context.uart[0]);
	sdp1302_restore_uart_context(regs + 0x40, &sdp1302_syscore_context.uart[1]);
	sdp1302_restore_uart_context(regs + 0x80, &sdp1302_syscore_context.uart[2]);
	sdp1302_restore_uart_context(regs + 0xc0, &sdp1302_syscore_context.uart[3]);

	/* SCU */
	sdp_scu_enable();
}

static int sdp_pm_suspend(void)
{
//	printk("sdp_pm_suspend\n");
	if(soc_is_sdp1304())	{
		sdp1304_save_uart();
		sdp1304_save_gic();
	} else if (soc_is_sdp1302()) {
		sdp1302_pm_suspend();
	}
	return 0;
}

static void sdp_pm_resume(void)
{
//	printk("sdp_pm_resume\n");
	if(soc_is_sdp1304())	{
		sdp1304_load_uart();
		sdp1304_load_gic();
	} else if (soc_is_sdp1302()) {
		sdp1302_pm_resume();
	}
}

static struct syscore_ops sdp_pm_syscore_ops = {
	.suspend	= sdp_pm_suspend,
	.resume		= sdp_pm_resume,
};

int __init sdp_pm_syscore_init(void)
{
	register_syscore_ops(&sdp_pm_syscore_ops);
	return 0;
}
arch_initcall(sdp_pm_syscore_init);

