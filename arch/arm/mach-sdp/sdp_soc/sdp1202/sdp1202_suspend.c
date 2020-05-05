#include <linux/init.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/pm.h>

#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <mach/platform.h>

/* for micom control */
extern void sdp_micom_uart_init(int port);
extern void sdp_micom_uart_sendcmd(unsigned char *s, unsigned char size);

/* for Fox-MP DDR */
extern bool sdp_is_dual_mp;
extern int sdp_asv_mp_training(unsigned int mp_index);

struct sdp1202_suspend_save {
	u32	entry_point;
	u32	tmp;
};

#define MICOM_PORT	2
#define LONG_CMD	9
#define SHORT_CMD	3
#define REQ_SLEEP	0x25
#define REQ_POWER_OFF	0x12

#define SAVE_REG(x, y)	sdp1202_save_peri.x = readl( y + DIFF_IO_BASE0)
#define LOAD_REG(x, y)	writel(sdp1202_save_peri.x, (y + DIFF_IO_BASE0))
#define SAVE_REGS(x, y, n)	do { static int i=0; for(i = 0; i < n ; i++) sdp1202_save_peri.x[i] = readl (y + DIFF_IO_BASE0 + i * 4);} while(0)
#define LOAD_REGS(x, y, n)	do { static int i=0; for(i = 0; i < n ; i++) writel(sdp1202_save_peri.x[i], (y + DIFF_IO_BASE0 + i * 4));} while(0)

#define UART_ULCON	0x10090A00
#define	UART_UCON	0x10090A04
#define UART_UFCON	0x10090A08
#define UART_UBRDIV	0x10090A28

#define TIMER_CON1	0x10090410
#define TIMER_DATA1	0x10090414
#define TIMER_DATAL1	0x10090490

#define GIC_DIST_CTRL	0x10B81000
#define GIC_DIST_ENABLE	0x10B81100
#define GIC_DIST_PRI	0x10B81400
#define GIC_DIST_TARGET	0x10B81800

#define GIC_CPU_CTRL	0x10B82000
#define GIC_CPU_PRIMASK	0x10B82004

static struct sdp1202_suspend_save *sdp_sarea;

static struct sdp1202_save_peri_t {
	u32 uart_ulcon;
	u32 uart_ucon;
	u32 uart_ufcon;
	u32 uart_ubrdiv;
	u32 timer_con1;
	u32 timer_data1;
	u32 timer_datal1;
	u32 gic_dist_ctrl;
	u32 gic_dist_enable[32];
	u32 gic_dist_pri[32];
	u32 gic_dist_target[40];
	u32 gic_cpu_ctrl;
	u32 gic_cpu_primask;
} sdp1202_save_peri;

static void sdp1202_save_uart(void)
{
	SAVE_REG(uart_ulcon, UART_ULCON);
	SAVE_REG(uart_ucon, UART_UCON);
	SAVE_REG(uart_ufcon, UART_UFCON);
	SAVE_REG(uart_ubrdiv, UART_UBRDIV);
}

static void sdp1202_load_uart(void)
{
	LOAD_REG(uart_ulcon, UART_ULCON);
	LOAD_REG(uart_ucon, UART_UCON);
	LOAD_REG(uart_ufcon, UART_UFCON);
	LOAD_REG(uart_ubrdiv, UART_UBRDIV);
}

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

static void sdp1202_save_gic(void)
{
	SAVE_REG(gic_dist_ctrl, GIC_DIST_CTRL);
	SAVE_REGS(gic_dist_enable, GIC_DIST_ENABLE, 32);
	SAVE_REGS(gic_dist_pri, GIC_DIST_PRI, 32);
	SAVE_REGS(gic_dist_target, GIC_DIST_TARGET, 40);	
	SAVE_REG(gic_cpu_ctrl, GIC_CPU_CTRL);
	SAVE_REG(gic_cpu_primask, GIC_CPU_PRIMASK);	
}
static void sdp1202_load_gic(void)
{
	writel(0, (GIC_DIST_ENABLE + DIFF_IO_BASE0));
	LOAD_REGS(gic_dist_enable, GIC_DIST_ENABLE, 32);
	LOAD_REGS(gic_dist_pri, GIC_DIST_PRI, 32);
	LOAD_REG(gic_dist_ctrl, GIC_DIST_CTRL);
	LOAD_REGS(gic_dist_target, GIC_DIST_TARGET, 40);	
	LOAD_REG(gic_cpu_primask, GIC_CPU_PRIMASK);	
	LOAD_REG(gic_cpu_ctrl, GIC_CPU_CTRL);
}

static void sdp1202_wait_for_die(void)
{
	void *pctl_base = (u32*)0x10418000;

	/* now all caches and mmu off, set self-refresh mode, and die */
	writel_relaxed (0x3, pctl_base + 0x4);	/* PCTL::SCTL */
	
	/* here's the WFI */
	asm(".word	0xe320f003\n" : : : "memory", "cc");
}

static void sdp1202_poweroff(void)
{
	unsigned char cmd[10];
	/* send micom suspend off */
	sdp_micom_uart_init(MICOM_PORT);

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = REQ_POWER_OFF;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = REQ_POWER_OFF;
	
	sdp_micom_uart_sendcmd((unsigned char *) cmd, LONG_CMD);
}

static int notrace __sdp1202_suspend_enter(unsigned long unused)
{
	unsigned char cmd[10];
	/* TODO */
	BUG_ON(!sdp_sarea);

	/* save resume address */	
	sdp_sarea->entry_point = virt_to_phys(cpu_resume);

	/* save ddr setting */

	/* send micom suspend off */
	sdp_micom_uart_init(MICOM_PORT);

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = REQ_SLEEP;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = REQ_SLEEP;
	
	sdp_micom_uart_sendcmd((unsigned char *) cmd, LONG_CMD);
	
	soft_restart(virt_to_phys(sdp1202_wait_for_die));

	return 0;
}

/*--------------------*/
static int sdp1202_suspend_enter(suspend_state_t state)
{
	/* gpio, i2c */
//	printk("sdp1202_suspend_enter\n");
	cpu_suspend(0, __sdp1202_suspend_enter);
	return 0;
}

static int sdp1202_suspend_prepare(void)
{
	/* TODO: save IRQ affinities? */
//	printk("sdp1202_suspend_prepare\n");
	return 0;
}

static void sdp1202_suspend_wake(void)
{
	/* TODO: rollback IRQ affinities */
//	printk("sdp1202_suspend_wake\n");
}

static void sdp1202_suspend_finish(void)
{
	/* TODO: rollback IRQ affinities */
//	printk("sdp1202_suspend_finish\n");
}

static const struct platform_suspend_ops sdp1202_suspend_ops = {
	.enter		= sdp1202_suspend_enter,
	.prepare	= sdp1202_suspend_prepare,
	.wake		= sdp1202_suspend_wake,
	.finish		= sdp1202_suspend_finish,
	.valid		= suspend_valid_only_mem,
};

int __init sdp1202_suspend_init(void)
{
	pr_info ("SDP1202 suspend support.\n");

	sdp_sarea = ioremap(0x9DA00000, 1024);
	suspend_set_ops(&sdp1202_suspend_ops);

	pm_power_off = sdp1202_poweroff;
	
	return 0;
}

static int sdp1202_pm_suspend(void)
{
//	printk("sdp1202_pm_suspend\n");
	sdp1202_save_uart();
	sdp1202_save_timer();
	sdp1202_save_gic();
	return 0;
}

static void sdp1202_pm_resume(void)
{
//	printk("sdp1202_pm_resume\n");
	sdp1202_load_uart();
	sdp1202_load_timer();
	sdp1202_load_gic();

	if (sdp_asv_mp_training(0) < 0)
		printk(KERN_ERR "Fox-MP0 training failed\n");
	if(sdp_is_dual_mp)
		if (sdp_asv_mp_training(1) < 0)
			printk(KERN_ERR "Fox-MP1 training failed\n");
}

static struct syscore_ops sdp1202_pm_syscore_ops = {
	.suspend	= sdp1202_pm_suspend,
	.resume		= sdp1202_pm_resume,
};

int __init sdp1202_pm_syscore_init(void)
{
	register_syscore_ops(&sdp1202_pm_syscore_ops);
	return 0;
}


arch_initcall(sdp1202_pm_syscore_init);
late_initcall(sdp1202_suspend_init);
