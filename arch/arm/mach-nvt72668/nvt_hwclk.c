#include <asm/div64.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <mach/nvt_hwclock.h>
#include <mach/clk.h>

unsigned long periph_clk = 0;
uint32_t nvt_clk_vmap = 0;

uint64_t __hack_ns;
uint64_t __delta_ns;
uint64_t __early_boot_ns;

EXPORT_SYMBOL(__hack_ns);
EXPORT_SYMBOL(__delta_ns);
EXPORT_SYMBOL(__early_boot_ns);
EXPORT_SYMBOL(nvt_clk_vmap);
EXPORT_SYMBOL(periph_clk);

#ifdef CONFIG_NVT_HW_CLOCK_TEST
static int gtimer_proc_show(struct seq_file *m, void *v) {
	uint64_t time;
	uint32_t tmp;

	seq_printf(m, "1st Get Gtimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m,"Gtimer :  time      = %llu\n",time);
	msleep(10000);
	seq_printf(m, "2nd Get Gtimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m,"Gtimer :  time      = %llu\n",time);
	msleep(10000);
	seq_printf(m, "3rd Get Gtimer time!\n");
	time = hwclock_ns((uint32_t *)hwclock_get_va());
	seq_printf(m,"Gtimer :  time      = %llu\n",time);

	return 0;
}

static int gtimer_proc_open(struct inode *inode, struct  file *file) {
   return single_open(file, gtimer_proc_show, NULL);
}

static const struct file_operations gtimer_proc_fops = {
   .owner = THIS_MODULE,
   .open = gtimer_proc_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
};

static int __init hwclk_gtimer_init(void)
{
	printk("periph_clk = %lu",periph_clk);
	proc_create("g_timer", 0, NULL, &gtimer_proc_fops);

	return 0;
}

static void __exit hwclk_gtimer_exit(void)
{

}

module_init(hwclk_gtimer_init);
module_exit(hwclk_gtimer_exit);
#endif
