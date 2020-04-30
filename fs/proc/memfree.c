#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/blkdev.h>

#define K(x) ((x) << (PAGE_SHIFT - 10))

long buff_for_perf_kb = 30720;

static int memfree_proc_show(struct seq_file *m, void *v)
{
	unsigned long wmark_low = 0;
	unsigned long pagecache;
	unsigned long free_cma;
	unsigned long free_ram;
	unsigned long mem_available;
	struct zone *zone;

	for_each_zone(zone)
		wmark_low += zone->watermark[WMARK_LOW];

	free_ram = global_page_state(NR_FREE_PAGES);
	mem_available = free_ram - wmark_low;
	pagecache = global_page_state(NR_LRU_BASE + LRU_ACTIVE_FILE) +
		global_page_state(NR_LRU_BASE + LRU_INACTIVE_FILE);
	pagecache -= min(pagecache / 2, wmark_low);
	mem_available += pagecache;
	mem_available += global_page_state(NR_SLAB_RECLAIMABLE) -
		min(global_page_state(NR_SLAB_RECLAIMABLE) / 2, wmark_low);

	free_cma = cma_get_free();

	seq_printf(m, "%lu\n", K(mem_available - free_cma));
	return 0;
}

static int memfree_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, memfree_proc_show, NULL);
}

static const struct file_operations memfree_proc_fops = {
	.open           = memfree_proc_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init proc_memfree_init(void)
{
	proc_create("vd_memfree", 0, NULL, &memfree_proc_fops);
	return 0;
}
module_init(proc_memfree_init);
