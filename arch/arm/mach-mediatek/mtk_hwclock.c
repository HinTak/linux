#include <asm/div64.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#if defined(CONFIG_MTK_HW_CLOCK)
#include <mach/mtk_hwclock.h>
#endif

/* See explanation of those veraibles in the header above */

uint64_t __hack_ns;
uint64_t __delta_ns;

EXPORT_SYMBOL(__hack_ns);
EXPORT_SYMBOL(__delta_ns);

uint32_t mtk_clk_vmap;
EXPORT_SYMBOL(mtk_clk_vmap);


#ifdef CONFIG_MTK_HW_CLOCK
static int systimer_proc_show(struct seq_file *m, void *v)
{
	uint64_t time;

	seq_puts(m, "1st Get Systimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m, "systimer :  time      = 0x%016llx\n", time);
	msleep(10000);
	seq_puts(m, "2nd Get Systimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m, "systimer :  time      = 0x%016llx\n", time);
	msleep(10000);
	seq_puts(m, "3rd Get Systimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m, "systimer :  time      = 0x%016llx\n", time);

	return 0;
}

static int systimer_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, systimer_proc_show, NULL);
}

static const struct file_operations systimer_proc_fops = {
	.owner = THIS_MODULE,
	.open = systimer_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init hwclk_systimer_init(void)
{
	pr_info("TEST MTK SYSTIMER");

	proc_create("systimer", 0, NULL, &systimer_proc_fops);

	return 0;
}

static void __exit hwclk_systimer_exit(void)
{
}

module_init(hwclk_systimer_init);
module_exit(hwclk_systimer_exit);
#endif

