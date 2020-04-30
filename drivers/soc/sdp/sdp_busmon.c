
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
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <soc/sdp/soc.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

#define CREATE_TRACE_POINTS
#include "soc/sdp/sdp_busmon_trace.h"

struct sdp_busmon_stat {
	u64 datasum_w;
	u64 datasum_r;
	u32 latmax_w;
	u32 latmax_r;
	u32 waitmax_w;
	u32 waitmax_r;
	u64 latsum_w;
	u64 latsum_r;
	u64 dlatsum_w;
	u64 dlatsum_r;
	u64 reqcnt_w;
	u64 reqcnt_r;
	u64 datasum_filtered_w;
	u64 datasum_filtered_r;
	u64 clkcnt_w;
	u64 clkcnt_r;
	u64 waitsum_w;
	u64 waitsum_r;
	u32 datasum_max_w;
	u32 datasum_max_r;
	u32 datasum_max_t;
	u32 datasum_max_clk;
};

struct sdp_busmon_dev {
	char *name;
	phys_addr_t base;
	u32 freq;
	u32 bl;
	bool state;
	bool update;
	bool trace;
	void __iomem *reg;
	spinlock_t lock;
	struct sdp_busmon_stat stat[2];
};

struct sdp_busmon_ops {
	u32 (*get_prevent_err_w)(void *dev);
	u32 (*get_prevent_err_r)(void *dev);
	int (*set_filter)(void *dev, int ch);
};

#if defined(CONFIG_DEBUG_FS)
struct sdp_busmon_fs {
	struct dentry *root;
	struct dentry *show;
	struct dentry *rate;
	struct dentry *on;
	struct dentry *off;
	struct dentry *enumdevs;
	struct dentry *stat;
	struct dentry *rawstat;
	struct dentry *filter;
};
#endif

struct sdp_busmon_statbuf {
	int wp;
	char *buf;
	int size;
	int filled;
};

struct sdp_busmon {
	int n_devs;
	struct sdp_busmon_dev *devs;
	struct sdp_busmon_ops *ops;
	spinlock_t lock;
	struct delayed_work work;
	u32 freq;
	u32 rate;
	u32 filter;
	u32 trace;
	struct sdp_busmon_statbuf statbuf;
#if defined(CONFIG_DEBUG_FS)
	struct sdp_busmon_fs debugfs;
#endif	
};

static struct sdp_busmon g_busmon;
static struct sdp_busmon* get_busmon(void)
{
	return &g_busmon;
}

static int sdp_busmon_set_devs(struct sdp_busmon_dev *devs, int n_devs)
{
	struct sdp_busmon *busmon = get_busmon();
	int i;

	busmon->devs = devs;
	busmon->n_devs = n_devs;

	for (i = 0; i < busmon->n_devs; i++) {
		spin_lock_init(&busmon->devs[i].lock);
		busmon->devs[i].reg = ioremap(busmon->devs[i].base, PAGE_SIZE);
	}

	return 0;
}

static int sdp_busmon_set_ops(struct sdp_busmon_ops *ops)
{
	struct sdp_busmon *busmon = get_busmon();
	busmon->ops = ops;
	return 0;
}

int sdp_busmon_set_busfreq(u32 freq)
{
	struct sdp_busmon *busmon = get_busmon();
	busmon->freq = freq;
	return 0;
}
EXPORT_SYMBOL(sdp_busmon_set_busfreq);

void* sdp_busmon_get_by_name(char *name, int n)
{
	struct sdp_busmon *busmon = get_busmon();
	int i, len;
	
	for (i = 0; i < busmon->n_devs; i++) {
		len = strlen(busmon->devs[i].name);
		if (!strncmp(name, busmon->devs[i].name, min(n, len)))
			return (void*)&busmon->devs[i];
	}
	return NULL;
}
EXPORT_SYMBOL(sdp_busmon_get_by_name);

void* sdp_busmon_get_by_addr(phys_addr_t addr)
{
	struct sdp_busmon *busmon = get_busmon();
	int i;
	
	for (i = 0; i < busmon->n_devs; i++) {
		if (addr == busmon->devs[i].base)
			return (void*)&busmon->devs[i];
	}
	return NULL;
}
EXPORT_SYMBOL(sdp_busmon_get_by_addr);

int sdp_busmon_set_state(void *dev, bool on)
{
	struct sdp_busmon_dev *busdev = (struct sdp_busmon_dev *)dev;
	unsigned long flags;

	if (!busdev)
		return -EFAULT;
		
	spin_lock_irqsave(&busdev->lock, flags);
	
	busdev->state = on;
	
	spin_unlock_irqrestore(&busdev->lock, flags);
	return 0;
}
EXPORT_SYMBOL(sdp_busmon_set_state);

int sdp_busmon_set_update(void *dev, bool on)
{
	struct sdp_busmon_dev *busdev = (struct sdp_busmon_dev *)dev;

	if (!busdev)
		return -EFAULT;
	
	busdev->update = on;
	return 0;
}
EXPORT_SYMBOL(sdp_busmon_set_update);

int sdp_busmon_set_trace(void *dev, bool on)
{
	struct sdp_busmon_dev *busdev = (struct sdp_busmon_dev *)dev;

	if (!busdev)
		return -EFAULT;
	
	busdev->trace = on;
	busdev->update = on;
	return 0;
}
EXPORT_SYMBOL(sdp_busmon_set_trace);

void sdp_busmon_writel(void *dev, u32 offset, u32 val)
{
	struct sdp_busmon_dev *busdev = (struct sdp_busmon_dev *)dev;
	unsigned long flags;

	if (!busdev)
		return;
		
	spin_lock_irqsave(&busdev->lock, flags);
	if (!busdev->state) {
		spin_unlock_irqrestore(&busdev->lock, flags);
		return;
	}

	writel(val, busdev->reg+offset);
	
	spin_unlock_irqrestore(&busdev->lock, flags);
	return;
}
EXPORT_SYMBOL(sdp_busmon_writel);

u32 sdp_busmon_readl(void *dev, u32 offset)
{
	struct sdp_busmon_dev *busdev = (struct sdp_busmon_dev *)dev;
	unsigned long flags;
	u32 val;

	if (!busdev)
		return 0;
		
	spin_lock_irqsave(&busdev->lock, flags);
	if (!busdev->state) {
		spin_unlock_irqrestore(&busdev->lock, flags);
		return 0;
	}

	val = readl(busdev->reg+offset);
	
	spin_unlock_irqrestore(&busdev->lock, flags);
	return val;
}
EXPORT_SYMBOL(sdp_busmon_readl);


static u32 common_get_prevent_err_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x2c) << 16;
}
static u32 common_get_prevent_err_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x2c) & 0xffff0000ul;
}
static u32 common_get_pending_w(void *dev)
{
	return (sdp_busmon_readl(dev, 0x30) >> 28) & 1;
}
static u32 common_get_pending_r(void *dev)
{
	return (sdp_busmon_readl(dev, 0x30) >> 24) & 1;
}
static int common_set_lat_auto(void *dev, bool en)
{
	u32 val;
	val = sdp_busmon_readl(dev, 0x30);
	if (en)
		val |= 0x1ul << 8;
	else
		val &= ~(0x1ul << 8);
	sdp_busmon_writel(dev, 0x30, val);

	return 0;
}
static int common_set_lat_enable(void *dev, bool en)
{
	u32 val;
	val = sdp_busmon_readl(dev, 0x30);
	if (en)
		val |= 0x1ul << 4;
	else
		val &= ~(0x1ul << 4);
	sdp_busmon_writel(dev, 0x30, val);

	return 0;
}
static int common_set_lat_reset(void *dev, bool en)
{
	u32 val;
	val = sdp_busmon_readl(dev, 0x30);
	if (en)
		val |= 0x1ul << 0;
	else
		val &= ~(0x1ul << 0);
	sdp_busmon_writel(dev, 0x30, val);

	return 0;
}
static u32 common_get_datasum_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x40);
}
static u32 common_get_datasum_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x60);
}
static u32 common_get_latmax_w(void *dev)
{
	return (sdp_busmon_readl(dev, 0x44) >> 16) & 0xfffful;
}
static u32 common_get_latmax_r(void *dev)
{
	return (sdp_busmon_readl(dev, 0x64) >> 16) & 0xfffful;
}
static u32 common_get_waitmax_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x44) & 0xfffful;
}
static u32 common_get_waitmax_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x64) & 0xfffful;
}
static u32 common_get_latsum_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x48);
}
static u32 common_get_latsum_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x68);
}
static u32 common_get_dlatsum_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x4c);
}
static u32 common_get_dlatsum_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x6c);
}
static u32 common_get_reqcnt_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x50);
}
static u32 common_get_reqcnt_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x70);
}
static u32 common_get_datasum_filtered_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x54);
}
static u32 common_get_datasum_filtered_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x74);
}
static u32 common_get_clkcnt_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x58);
}
static u32 common_get_clkcnt_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x78);
}
static u32 common_get_waitsum_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x5c);
}
static u32 common_get_waitsum_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x7c);
}

u32 sdp_busmon_get_prevent_err_w(void *dev)
{
	struct sdp_busmon *busmon = get_busmon();
	
	if (busmon->ops && busmon->ops->get_prevent_err_w)
		return busmon->ops->get_prevent_err_w(dev);
		
	return common_get_prevent_err_w(dev);
}
EXPORT_SYMBOL(sdp_busmon_get_prevent_err_w);

u32 sdp_busmon_get_prevent_err_r(void *dev)
{
	struct sdp_busmon *busmon = get_busmon();
	
	if (busmon->ops && busmon->ops->get_prevent_err_r)
		return busmon->ops->get_prevent_err_r(dev);
		
	return common_get_prevent_err_r(dev);
}
EXPORT_SYMBOL(sdp_busmon_get_prevent_err_r);

static u32 sdp_busmon_get_pending_w(void *dev)
{
	return common_get_pending_w(dev);
}
static u32 sdp_busmon_get_pending_r(void *dev)
{
	return common_get_pending_r(dev);
}
static int sdp_busmon_set_lat_auto(void *dev, bool en)
{
	return common_set_lat_auto(dev, en);
}
static int sdp_busmon_set_lat_enable(void *dev, bool en)
{
	return common_set_lat_enable(dev, en);
}
static int sdp_busmon_set_lat_reset(void *dev, bool en)
{
	return common_set_lat_reset(dev, en);
}
static u32 sdp_busmon_get_datasum_w(void *dev)
{
	return common_get_datasum_w(dev);
}
static u32 sdp_busmon_get_datasum_r(void *dev)
{
	return common_get_datasum_r(dev);
}
static u32 sdp_busmon_get_latmax_w(void *dev)
{
	return common_get_latmax_w(dev);
}
static u32 sdp_busmon_get_latmax_r(void *dev)
{
	return common_get_latmax_r(dev);
}
static u32 sdp_busmon_get_waitmax_w(void *dev)
{
	return common_get_waitmax_w(dev);
}
static u32 sdp_busmon_get_waitmax_r(void *dev)
{
	return common_get_waitmax_r(dev);
}
static u32 sdp_busmon_get_latsum_w(void *dev)
{
	return common_get_latsum_w(dev);
}
static u32 sdp_busmon_get_latsum_r(void *dev)
{
	return common_get_latsum_r(dev);
}
static u32 sdp_busmon_get_dlatsum_w(void *dev)
{
	return common_get_dlatsum_w(dev);
}
static u32 sdp_busmon_get_dlatsum_r(void *dev)
{
	return common_get_dlatsum_r(dev);
}
static u32 sdp_busmon_get_reqcnt_w(void *dev)
{
	return common_get_reqcnt_w(dev);
}
static u32 sdp_busmon_get_reqcnt_r(void *dev)
{
	return common_get_reqcnt_r(dev);
}
static u32 sdp_busmon_get_datasum_filtered_w(void *dev)
{
	return common_get_datasum_filtered_w(dev);
}
static u32 sdp_busmon_get_datasum_filtered_r(void *dev)
{
	return common_get_datasum_filtered_r(dev);
}
static u32 sdp_busmon_get_clkcnt_w(void *dev)
{
	return common_get_clkcnt_w(dev);
}
static u32 sdp_busmon_get_clkcnt_r(void *dev)
{
	return common_get_clkcnt_r(dev);
}
static u32 sdp_busmon_get_waitsum_w(void *dev)
{
	return common_get_waitsum_w(dev);
}
static u32 sdp_busmon_get_waitsum_r(void *dev)
{
	return common_get_waitsum_r(dev);
}
static int sdp_busmon_set_filter(void *dev, int ch)
{
	struct sdp_busmon *busmon = get_busmon();
	if (busmon->ops && busmon->ops->set_filter)
		return busmon->ops->set_filter(dev, ch);
	return 0;
}

EXPORT_TRACEPOINT_SYMBOL(busmon_update_event);

void sdp_busmon_update(void)
{
	struct sdp_busmon *busmon = get_busmon();
	struct sdp_busmon_dev *dev;
	int i, wp;
	unsigned long flags;
	u32 dsum_w, dsum_r, clks_w, clks_r, lmax_w, lmax_r;
	
	spin_lock_irqsave(&busmon->lock, flags);
	wp = busmon->statbuf.wp;
	
	for (i = 0; i < busmon->n_devs; i++) {
		dev = &busmon->devs[i];
		if (dev->update) {
			sdp_busmon_set_lat_enable(dev, false);
			sdp_busmon_set_filter(dev, busmon->filter);

			dsum_w = sdp_busmon_get_datasum_w(dev);
			dsum_r = sdp_busmon_get_datasum_r(dev);
			dev->stat[wp].datasum_w += dsum_w;
			dev->stat[wp].datasum_r += dsum_r;

			lmax_w = sdp_busmon_get_latmax_w(dev);
			lmax_r = sdp_busmon_get_latmax_r(dev);
			dev->stat[wp].latmax_w = max(lmax_w, dev->stat[wp].latmax_w);
			dev->stat[wp].latmax_r = max(lmax_r, dev->stat[wp].latmax_r);
			dev->stat[wp].waitmax_w = max(sdp_busmon_get_waitmax_w(dev), dev->stat[wp].waitmax_w);
			dev->stat[wp].waitmax_r = max(sdp_busmon_get_waitmax_r(dev), dev->stat[wp].waitmax_r);
			dev->stat[wp].latsum_w += sdp_busmon_get_latsum_w(dev);
			dev->stat[wp].latsum_r += sdp_busmon_get_latsum_r(dev);
			dev->stat[wp].dlatsum_w += sdp_busmon_get_dlatsum_w(dev);
			dev->stat[wp].dlatsum_r += sdp_busmon_get_dlatsum_r(dev);
			dev->stat[wp].reqcnt_w += sdp_busmon_get_reqcnt_w(dev);
			dev->stat[wp].reqcnt_r += sdp_busmon_get_reqcnt_r(dev);
			dev->stat[wp].datasum_filtered_w += sdp_busmon_get_datasum_filtered_w(dev);
			dev->stat[wp].datasum_filtered_r += sdp_busmon_get_datasum_filtered_r(dev);
			
			clks_w = sdp_busmon_get_clkcnt_w(dev);
			clks_r = sdp_busmon_get_clkcnt_r(dev);
			dev->stat[wp].clkcnt_w += clks_w;
			dev->stat[wp].clkcnt_r += clks_r;
			
			dev->stat[wp].waitsum_w += sdp_busmon_get_waitsum_w(dev);
			dev->stat[wp].waitsum_r += sdp_busmon_get_waitsum_r(dev);

			dev->stat[wp].datasum_max_clk = sdp_busmon_get_clkcnt_r(dev);
			dev->stat[wp].datasum_max_w = max(sdp_busmon_get_datasum_w(dev), dev->stat[wp].datasum_max_w);
			dev->stat[wp].datasum_max_r = max(sdp_busmon_get_datasum_r(dev), dev->stat[wp].datasum_max_r);
			dev->stat[wp].datasum_max_t = \
				max(sdp_busmon_get_datasum_w(dev)+sdp_busmon_get_datasum_r(dev), dev->stat[wp].datasum_max_t);

			if (dev->trace) {
				u64 bw_w = 0, bw_r = 0;
				if (clks_w) {
					bw_w = (u64)dsum_w * dev->freq * dev->bl;
					do_div(bw_w, clks_w);
				}
				if (clks_r) {
					bw_r = (u64)dsum_r * dev->freq * dev->bl;
					do_div(bw_r, clks_r);
				}
				trace_busmon_update_event(dev->name, (u32)bw_w, (u32)bw_r, lmax_w, lmax_r);
			}

			sdp_busmon_set_lat_reset(dev, true);
			sdp_busmon_set_lat_reset(dev, false);
			sdp_busmon_set_lat_auto(dev, false);
			sdp_busmon_set_lat_enable(dev, true);
		}
	}
	spin_unlock_irqrestore(&busmon->lock, flags);
}
EXPORT_SYMBOL(sdp_busmon_update);

void sdp_busmon_show(void)
{
	struct sdp_busmon *busmon = get_busmon();
	int i, cnt, suspected = 0;
	u32 hang[2], pend[2];

	printk(KERN_ERR "\nsdp_busmon_hang start.\n");
	for (i = 0; i < busmon->n_devs; i++) {

		cnt = 1000;
		hang[0] = 1;
		hang[1] = 1;
		
		while ((hang[0] || hang[1]) && cnt > 0) {

			pend[0] = sdp_busmon_get_pending_w(&busmon->devs[i]);
			pend[1] = sdp_busmon_get_pending_r(&busmon->devs[i]);

			if (pend[0] == 0)
				hang[0] = 0;
			if (pend[1] == 0)
				hang[1] = 0;
				
			udelay(100);
			cnt--;
		}

		if (hang[0] || hang[1]) { // suspect hang 
			suspected++;
			printk(KERN_ERR "[%s] is suspected of hang. (%08x: %08x, %08x, %08x)\n", \
				busmon->devs[i].name, (u32)busmon->devs[i].base, \
				sdp_busmon_readl(&busmon->devs[i], 0x30), \
				sdp_busmon_readl(&busmon->devs[i], 0x84), \
				sdp_busmon_readl(&busmon->devs[i], 0x88));
		}
	}
	if (suspected == 0)
		printk(KERN_ERR "no master suspected of hang.\n");
	printk(KERN_ERR "sdp_busmon_hang end.\n\n");
	return;
}
EXPORT_SYMBOL(sdp_busmon_show);


static void sdp_busmon_work(struct work_struct *work)
{
	struct sdp_busmon *busmon = get_busmon();
	
	sdp_busmon_update();
	
	if (busmon->rate > 0)
		schedule_delayed_work(&busmon->work, msecs_to_jiffies(busmon->rate));
		
	return;
}

/* obsolete codes start */
int sdp_bus_mon_debug(void)
{
	sdp_busmon_show();
	return 0;
}
EXPORT_SYMBOL(sdp_bus_mon_debug);
int sdp_bus_flowctrl_get(const char *id)
{
	return 0;
}
EXPORT_SYMBOL(sdp_bus_flowctrl_get);
/* obsolete codes end */


/* debugfs */
#if defined(CONFIG_DEBUG_FS)
static int sdp_busmon_debug_show_store(void *data, u64 value)
{
	sdp_busmon_show();
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_busmon_debug_show_fops, NULL, sdp_busmon_debug_show_store, "%llu\n");

static int sdp_busmon_debug_rate_show(void *data, u64 *value)
{
	struct sdp_busmon *busmon = get_busmon();
	*value = (u32)busmon->rate;
	return 0;
}
static int sdp_busmon_debug_rate_store(void *data, u64 value)
{
	struct sdp_busmon *busmon = get_busmon();

	busmon->rate = (u32)value;
	
	if (busmon->rate > 0)
		schedule_delayed_work(&busmon->work, 0);
	else
		cancel_delayed_work_sync(&busmon->work);
		
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_busmon_debug_rate_fops, sdp_busmon_debug_rate_show, sdp_busmon_debug_rate_store, "%llu\n");


static int sdp_busmon_debug_on_show(struct seq_file *s, void *data)
{
	struct sdp_busmon *busmon = get_busmon();
	int i;

	for (i = 0; i < busmon->n_devs; i++) {
		if (busmon->devs[i].update)
			seq_printf(s, "%s\n", busmon->devs[i].name);
	}
	return 0;
}
static int sdp_busmon_debug_on_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdp_busmon_debug_on_show, inode->i_private);
}
static ssize_t sdp_busmon_debug_on_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	struct sdp_busmon *busmon = get_busmon();
	void *dev;
	char kbuf[32];
	int i;

	copy_from_user(kbuf, buf, min(count, sizeof(kbuf)-1));

	if (!strncmp(kbuf, "all", 3)) {
		for (i = 0; i < busmon->n_devs; i++) {
			busmon->devs[i].update = true;
			if (busmon->trace)
				busmon->devs[i].trace = true;
		}
	}
	else {
		dev = sdp_busmon_get_by_name(kbuf, count);
		sdp_busmon_set_update(dev, true);
		if (busmon->trace)
			sdp_busmon_set_trace(dev, true);
	}
	return count;
}
static const struct file_operations sdp_busmon_debug_on_fops = {
	.open		= sdp_busmon_debug_on_open,
	.read		= seq_read,
	.write		= sdp_busmon_debug_on_write,
	.llseek		= seq_lseek,
};

static int sdp_busmon_debug_off_show(struct seq_file *s, void *data)
{
	struct sdp_busmon *busmon = get_busmon();
	int i;

	for (i = 0; i < busmon->n_devs; i++) {
		if (!busmon->devs[i].update)
			seq_printf(s, "%s\n", busmon->devs[i].name);
	}
	return 0;
}
static int sdp_busmon_debug_off_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdp_busmon_debug_off_show, inode->i_private);
}
static ssize_t sdp_busmon_debug_off_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	struct sdp_busmon *busmon = get_busmon();
	void *dev;
	char kbuf[32];
	int i;

	copy_from_user(kbuf, buf, min(count, sizeof(kbuf)-1));

	if (!strncmp(kbuf, "all", 3)) {
		for (i = 0; i < busmon->n_devs; i++) {
			busmon->devs[i].update = false;
			busmon->devs[i].trace = false;
		}
	}
	else {
		dev = sdp_busmon_get_by_name(kbuf, count);
		sdp_busmon_set_update(dev, false);
		sdp_busmon_set_trace(dev, false);
	}
	return count;
}
static const struct file_operations sdp_busmon_debug_off_fops = {
	.open		= sdp_busmon_debug_off_open,
	.read		= seq_read,
	.write		= sdp_busmon_debug_off_write,
	.llseek		= seq_lseek,
};

static int sdp_busmon_debug_enumdevs_show(struct seq_file *s, void *data)
{
	struct sdp_busmon *busmon = get_busmon();
	int i;

	for (i = 0; i < busmon->n_devs; i++) {
		seq_printf(s, "%16s %08llx %04u %02u (%s, %s)\n", busmon->devs[i].name, \
			(u64)busmon->devs[i].base, busmon->devs[i].freq, busmon->devs[i].bl, \
			busmon->devs[i].state ? "on ":"off", \
			busmon->devs[i].update ? "update":"do not update");
	}
	return 0;
}
static int sdp_busmon_debug_enumdevs_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdp_busmon_debug_enumdevs_show, inode->i_private);
}
static const struct file_operations sdp_busmon_debug_enumdevs_fops = {
	.open		= sdp_busmon_debug_enumdevs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

static u32 to_bus_freq(u32 m_cycle, u32 m_freq)
{
	struct sdp_busmon *busmon = get_busmon();
	if (!m_freq)
		return 0;
	return (m_cycle * busmon->freq)/m_freq;
}
static int shift_to_u32(u64 val)
{
	int shift = 0;

	while (val > 0x7fffffffull) {
		val >>= 1;
		shift++;
	}
	return shift;
}

static int sdp_busmon_stat_fill(void)
{
	struct sdp_busmon *busmon = get_busmon();
	int i, rp, ret, size;
	unsigned long flags;
	char *buf;

	u64 bw_w, bw_r, bw_f_w, bw_f_r, bw_max_w, bw_max_r, bw_max_t;
	u64 latavg_w, latavg_r, dlatavg_w, dlatavg_r, waitavg_w, waitavg_r;
	u64 duration_w, duration_r, cnt;
	int shift_clk, shift_req;

	busmon->statbuf.filled = 0;
	buf = busmon->statbuf.buf;
	size = busmon->statbuf.size;
	if (!buf)
		return -ENOMEM;

	spin_lock_irqsave(&busmon->lock, flags);
	rp = busmon->statbuf.wp;
	busmon->statbuf.wp = !busmon->statbuf.wp;
	spin_unlock_irqrestore(&busmon->lock, flags);

	for (i = 0; i < busmon->n_devs; i++) {

		if (!busmon->devs[i].state)
			continue;
			
		if (!busmon->freq || !busmon->devs[i].freq ||
			!busmon->devs[i].stat[rp].clkcnt_w || 
			!busmon->devs[i].stat[rp].clkcnt_r) {
			continue;
		}

		shift_clk = shift_to_u32(busmon->devs[i].stat[rp].clkcnt_r);
		shift_req = shift_to_u32(busmon->devs[i].stat[rp].reqcnt_r);

		duration_w = busmon->devs[i].stat[rp].clkcnt_w;
		do_div(duration_w, (busmon->devs[i].freq*1000));
		duration_r = busmon->devs[i].stat[rp].clkcnt_r;
		do_div(duration_r, (busmon->devs[i].freq*1000));

		busmon->devs[i].stat[rp].clkcnt_w >>= shift_clk;
		busmon->devs[i].stat[rp].clkcnt_r >>= shift_clk;
		busmon->devs[i].stat[rp].reqcnt_w >>= shift_req;
		busmon->devs[i].stat[rp].reqcnt_r >>= shift_req;

		if (busmon->devs[i].stat[rp].datasum_max_clk) {
			bw_max_t = (u64)busmon->devs[i].stat[rp].datasum_max_t * busmon->devs[i].freq * busmon->devs[i].bl;
			do_div(bw_max_t, busmon->devs[i].stat[rp].datasum_max_clk);
			bw_max_w = (u64)busmon->devs[i].stat[rp].datasum_max_w * busmon->devs[i].freq * busmon->devs[i].bl;
			do_div(bw_max_w, busmon->devs[i].stat[rp].datasum_max_clk);
			bw_max_r = (u64)busmon->devs[i].stat[rp].datasum_max_r * busmon->devs[i].freq * busmon->devs[i].bl;
			do_div(bw_max_r, busmon->devs[i].stat[rp].datasum_max_clk);
		}
		else {
			bw_max_t = 0;
			bw_max_w = 0;
			bw_max_r = 0;
		}

		bw_w = (busmon->devs[i].stat[rp].datasum_w >> shift_clk) * busmon->devs[i].freq * busmon->devs[i].bl;
		do_div(bw_w, busmon->devs[i].stat[rp].clkcnt_w);
		bw_r = (busmon->devs[i].stat[rp].datasum_r >> shift_clk) * busmon->devs[i].freq * busmon->devs[i].bl;
		do_div(bw_r, busmon->devs[i].stat[rp].clkcnt_r);

		bw_f_w = (busmon->devs[i].stat[rp].datasum_filtered_w >> shift_clk) * busmon->devs[i].freq * busmon->devs[i].bl;
		do_div(bw_f_w, busmon->devs[i].stat[rp].clkcnt_w);
		bw_f_r = (busmon->devs[i].stat[rp].datasum_filtered_r >> shift_clk) * busmon->devs[i].freq * busmon->devs[i].bl;
		do_div(bw_f_r, busmon->devs[i].stat[rp].clkcnt_r);

		latavg_w = busmon->devs[i].stat[rp].latsum_w >> shift_req;
		latavg_r = busmon->devs[i].stat[rp].latsum_r >> shift_req;
		dlatavg_w = busmon->devs[i].stat[rp].dlatsum_w >> shift_req;
		dlatavg_r = busmon->devs[i].stat[rp].dlatsum_r >> shift_req;
		waitavg_w = busmon->devs[i].stat[rp].waitsum_w >> shift_req;
		waitavg_r = busmon->devs[i].stat[rp].waitsum_r >> shift_req;
		if (busmon->devs[i].stat[rp].reqcnt_w) {
			do_div(latavg_w, busmon->devs[i].stat[rp].reqcnt_w);
			do_div(dlatavg_w, busmon->devs[i].stat[rp].reqcnt_w);
			do_div(waitavg_w, busmon->devs[i].stat[rp].reqcnt_w);
		}
		else {
			latavg_w = 0;
			dlatavg_w = 0;
			waitavg_w = 0;
		}
		if (busmon->devs[i].stat[rp].reqcnt_r) {
			do_div(latavg_r, busmon->devs[i].stat[rp].reqcnt_r);
			do_div(dlatavg_r, busmon->devs[i].stat[rp].reqcnt_r);
			do_div(waitavg_r, busmon->devs[i].stat[rp].reqcnt_r);
		}
		else {
			latavg_r = 0;
			dlatavg_r = 0;
			waitavg_r = 0;
		}
		
		ret = snprintf(buf, size, "[%10s] %6u/%6u %4u(%4u) %4u(%4u)/%4u(%4u) ", 
			busmon->devs[i].name, (u32)duration_w, (u32)duration_r, \
			(u32)(bw_w+bw_r), (u32)bw_max_t, (u32)bw_w, (u32)bw_max_w, (u32)bw_r, (u32)bw_max_r);

		buf += ret;
		size -= ret;
		busmon->statbuf.filled += ret;

		ret = snprintf(buf, size, "%4u/%4u %4u/%4u %4u/%4u %4u/%4u %4u/%4u (%4u/%4u)\n", 
			to_bus_freq(busmon->devs[i].stat[rp].latmax_w, busmon->devs[i].freq), \
			to_bus_freq(busmon->devs[i].stat[rp].latmax_r, busmon->devs[i].freq), \
			to_bus_freq(busmon->devs[i].stat[rp].waitmax_w, busmon->devs[i].freq), \
			to_bus_freq(busmon->devs[i].stat[rp].waitmax_r, busmon->devs[i].freq), \
			to_bus_freq(latavg_w, busmon->devs[i].freq), to_bus_freq(latavg_r, busmon->devs[i].freq), \
			to_bus_freq(dlatavg_w, busmon->devs[i].freq), to_bus_freq(dlatavg_r, busmon->devs[i].freq), \
			to_bus_freq(waitavg_w, busmon->devs[i].freq), to_bus_freq(waitavg_r, busmon->devs[i].freq), \
			(u32)bw_f_w, (u32)bw_f_r);

		buf += ret;
		size -= ret;
		busmon->statbuf.filled += ret;

		memset(&busmon->devs[i].stat[rp], 0, sizeof(struct sdp_busmon_stat));
	}

	ret = snprintf(buf, size, "\n");
	busmon->statbuf.filled += ret;

	return 0;
}

static int sdp_busmon_rawstat_fill(void)
{
	struct sdp_busmon *busmon = get_busmon();
	int i, rp, size, ret;
	unsigned long flags;
	char *buf;

	busmon->statbuf.filled = 0;
	buf = busmon->statbuf.buf;
	size = busmon->statbuf.size;
	if (!buf)
		return -ENOMEM;

	spin_lock_irqsave(&busmon->lock, flags);
	rp = busmon->statbuf.wp;
	busmon->statbuf.wp = !busmon->statbuf.wp;
	spin_unlock_irqrestore(&busmon->lock, flags);

	for (i = 0; i < busmon->n_devs; i++) {

		if (!busmon->devs[i].state)
			continue;

		if (!busmon->freq || !busmon->devs[i].freq ||
			!busmon->devs[i].stat[rp].clkcnt_w || 
			!busmon->devs[i].stat[rp].clkcnt_r) {
			continue;
		}
		
		ret = snprintf(buf, size, "[%10s] %llu/%llu %llu/%llu %llu/%llu/%llu/%llu ", 
			busmon->devs[i].name, \
			busmon->devs[i].stat[rp].clkcnt_w, busmon->devs[i].stat[rp].clkcnt_r, \
			busmon->devs[i].stat[rp].reqcnt_w, busmon->devs[i].stat[rp].reqcnt_r, \
			busmon->devs[i].stat[rp].datasum_w, busmon->devs[i].stat[rp].datasum_r, \
			busmon->devs[i].stat[rp].datasum_filtered_w, busmon->devs[i].stat[rp].datasum_filtered_r);

		buf += ret;
		size -= ret;
		busmon->statbuf.filled += ret;
			
		ret = snprintf(buf, size, "%llu/%llu/%llu/%llu/%llu/%llu %u/%u/%u/%u %u/%u/%u/%u\n", 
			busmon->devs[i].stat[rp].latsum_w, busmon->devs[i].stat[rp].latsum_r, \
			busmon->devs[i].stat[rp].dlatsum_w, busmon->devs[i].stat[rp].dlatsum_r, \
			busmon->devs[i].stat[rp].waitsum_w, busmon->devs[i].stat[rp].waitsum_r, \
			busmon->devs[i].stat[rp].datasum_max_t, busmon->devs[i].stat[rp].datasum_max_w, \
			busmon->devs[i].stat[rp].datasum_max_r, busmon->devs[i].stat[rp].datasum_max_clk, \
			busmon->devs[i].stat[rp].latmax_w, busmon->devs[i].stat[rp].latmax_r, \
			busmon->devs[i].stat[rp].waitmax_w, busmon->devs[i].stat[rp].waitmax_r);
		
		buf += ret;
		size -= ret;
		busmon->statbuf.filled += ret;

		memset(&busmon->devs[i].stat[rp], 0, sizeof(struct sdp_busmon_stat));
	}

	ret = snprintf(buf, size, "\n");
	busmon->statbuf.filled += ret;

	return 0;
}

static void alloc_stat_buf(int size)
{
	struct sdp_busmon *busmon = get_busmon();
	busmon->statbuf.buf = vmalloc(size);
	if (busmon->statbuf.buf)
		busmon->statbuf.size = size;
	else
		busmon->statbuf.size = 0;
}

static ssize_t sdp_busmon_debug_stat_read(struct file *filp, char __user *buf, size_t size, loff_t *pos)
{
	struct sdp_busmon *busmon = get_busmon();
	int ret;

	if (*pos == 0) {
		ret = sdp_busmon_stat_fill();
		if (ret < 0)
			return ret;
	}

	if (busmon->statbuf.buf && busmon->statbuf.filled >= size) {
		copy_to_user(buf, busmon->statbuf.buf+(*pos), size);
		busmon->statbuf.filled -= size;
	}
	else {
		copy_to_user(buf, busmon->statbuf.buf+(*pos), busmon->statbuf.filled);
		size = busmon->statbuf.filled;
		busmon->statbuf.filled = 0;
	}

	*pos = *pos + size;

	return size;
}
static const struct file_operations sdp_busmon_debug_stat_fops = {
	.open		= simple_open,
	.read		= sdp_busmon_debug_stat_read,
	.llseek		= generic_file_llseek,
};

static ssize_t sdp_busmon_debug_rawstat_read(struct file *filp, char __user *buf, size_t size, loff_t *pos)
{
	struct sdp_busmon *busmon = get_busmon();
	int ret;
	
	if (*pos == 0) {
		ret = sdp_busmon_rawstat_fill();
		if (ret < 0)
			return ret;
	}

	if (busmon->statbuf.buf && busmon->statbuf.filled >= size) {
		copy_to_user(buf, busmon->statbuf.buf+(*pos), size);
		busmon->statbuf.filled -= size;
	}
	else {
		copy_to_user(buf, busmon->statbuf.buf+(*pos), busmon->statbuf.filled);
		size = busmon->statbuf.filled;
		busmon->statbuf.filled = 0;
	}

	*pos = *pos + size;

	return size;
}
static const struct file_operations sdp_busmon_debug_rawstat_fops = {
	.open		= simple_open,
	.read		= sdp_busmon_debug_rawstat_read,
	.llseek		= generic_file_llseek,
};

static void sdp_busmon_register_debugfs(void)
{
	struct sdp_busmon *busmon = get_busmon();

	busmon->debugfs.root = debugfs_create_dir("sdp_busmon", NULL);
	busmon->debugfs.show = debugfs_create_file("show", 0222, busmon->debugfs.root, NULL, &sdp_busmon_debug_show_fops);
	busmon->debugfs.rate = debugfs_create_file("rate", 0666, busmon->debugfs.root, NULL, &sdp_busmon_debug_rate_fops);
	busmon->debugfs.on = debugfs_create_file("on", 0666, busmon->debugfs.root, NULL, &sdp_busmon_debug_on_fops);
	busmon->debugfs.off = debugfs_create_file("off", 0666, busmon->debugfs.root, NULL, &sdp_busmon_debug_off_fops);
	busmon->debugfs.enumdevs = debugfs_create_file("enumdevs", 0444, busmon->debugfs.root, NULL, &sdp_busmon_debug_enumdevs_fops);
	busmon->debugfs.stat = debugfs_create_file("stat", 0444, busmon->debugfs.root, NULL, &sdp_busmon_debug_stat_fops);
	busmon->debugfs.rawstat = debugfs_create_file("rawstat", 0444, busmon->debugfs.root, NULL, &sdp_busmon_debug_rawstat_fops);
	busmon->debugfs.filter = debugfs_create_u32("filter", 0x666, busmon->debugfs.root, &g_busmon.filter);
	busmon->debugfs.on = debugfs_create_u32("trace", 0666, busmon->debugfs.root, &g_busmon.trace);
}
#endif


/******************************************************************************/
/* sdp1601 / sdp1701                                                          */
/******************************************************************************/

static struct sdp_busmon_dev sdp1601_busmon_devs[] = {
	{"perin",    0x553000, 500,  8, true, },	
	{"cpu",      0x440000, 600, 16, true, },	
	{"srp",      0xb94000, 500, 16, true, },	
	{"chn",      0xb58000, 250, 16, true, },	
	{"avd",      0xb68000, 297,  8, true, },	
	{"hdmi",     0x5c8000, 300,  4, true, },	
	{"tsd/se",   0x970000, 300,  8, true, },	
	{"spro/acp", 0x970800, 300, 16, false, },	
	{"gpu0",     0x543000, 800, 16, true, },	
	{"gpu1",     0x543800, 800, 16, true, },	
	{"gpu2",     0x544000, 800, 16, true, },	
	{"ga2d_r",   0x588000, 450,  8, true, },	
	{"ga2d_w",   0x588800, 450,  8, true, },	
	{"gp_osdp",  0x5e0000, 450, 16, true, },	
	{"gp_gp",    0x5e0800, 450, 16, true, },	
	{"dvde0",    0xb40000, 550, 16, true, },	
	{"dvde1",    0xb40800, 550, 16, true, },	
	{"dvde2",    0xb41000, 550, 32, true, },	
	{"dvde3",    0xb41800, 550,  8, true, },	
	{"mfd0",     0xa70000, 333, 16, true, },	
	{"mfd1",     0xa70800, 333, 16, true, },	
	{"jpeg",     0xa72000, 600,  8, true, },	
	{"hen",      0xad0000, 300, 16, true, },	
	{"adsp",     0xa20000, 660,  8, true, },	
	{"aio",      0xa21000, 334,  8, true, },	
	{"rotr",     0xb90000, 450, 16, true, },	
	{"rotw",     0xb90800, 450, 16, true, },	
	{"ctr0",     0xb91000, 400, 16, true, },	
	{"ctr1",     0xb91800, 400, 16, true, },	
	{"dpve_r",   0x780000, 450, 16, true, },	
	{"dpve_w",   0x780800, 450, 16, true, },	
	{"sclr_0",   0xcd0000, 450, 16, true, },	
	{"sclr_1",   0xcd0800, 450, 16, true, },	
	{"sclr_2",   0xcd1000, 450, 16, true, },	
	{"sclr_3",   0xcd1800, 450,  8, true, },	
	{"sclw_0",   0xcd2000, 450, 16, true, },	
	{"sclw_1",   0xcd2800, 450, 16, true, },	
	{"sclw_2",   0xcd3000, 450,  8, true, },	
	{"nrc_r",    0xd50000, 320, 16, true, },	
	{"nrc_w",    0xd50800, 320, 16, true, },
	{"frc_bif0", 0xbbc000, 450, 16, true, },	
	{"frc_bif1", 0xbbc800, 450, 16, true, },	
	{"frc_bif2", 0xbbd000, 450, 16, true, },	
	{"frc_bif3", 0xbbd800, 450, 16, true, },	
};

static u32 sdp1601_get_prevent_err_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x28) << 4;
}
static u32 sdp1601_get_prevent_err_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x2c) << 4;
}
static int sdp1601_set_filter(void *dev, int ch)
{
	if (ch == 0)
		sdp_busmon_writel(dev, 0x34, 0x2f002f00);
	else if (ch == 1)
		sdp_busmon_writel(dev, 0x34, 0x7f407f40);
	else if (ch == 2)
		sdp_busmon_writel(dev, 0x34, 0xff80ff80);
	else
		sdp_busmon_writel(dev, 0x34, 0x00000000);

	return 0;		
}
static struct sdp_busmon_ops sdp1601_busmon_ops = {
	.get_prevent_err_w = sdp1601_get_prevent_err_w,
	.get_prevent_err_r = sdp1601_get_prevent_err_r,
	.set_filter = sdp1601_set_filter,
};


/******************************************************************************/
/* sdp1803 / sdp1804                                                          */
/******************************************************************************/

static struct sdp_busmon_dev sdp1803_busmon_devs[] = {
	{"peris",     0x320000, 500,  8, true, },
	{"cpu",       0x440000, 600, 16, true, },
	{"gpu",       0x544000, 800, 16, true, },
	{"perin",     0x553000, 250,  8, true, },
	{"ga_r",      0x588000, 450,  8, true, },
	{"ga_w",      0x589000, 450,  8, true, },
	{"hdmi",      0x5bc000, 250,  4, true, },
	{"gp_osdp",   0x5e0000, 600, 16, true, },
	{"gp_gp",     0x5e4000, 600, 16, true, },
	{"srp",       0x5f0000, 500, 16, true, },
	{"dvde0",     0x840000, 600, 16, true, },
	{"dvde1",     0x841000, 600, 16, true, },
	{"dvde2",     0x842000, 600, 16, true, },
	{"dvde3",     0x843000, 600,  8, true, },
	{"tsd/se",    0x7f2000, 300,  8, true, },
	{"amic",      0x978000,  24,  4, false, },
	{"dmicom",    0x980000, 150,  4, false, },
	{"adsp",      0xa10000, 600, 16, true, },
	{"aio",       0xa2c000, 333,  8, true, },
	{"mfd0",      0xa60000, 333, 16, true, },
	{"mfd1",      0xa62000, 333, 16, true, },
	{"jpeg",      0xa80000, 600,  8, true, },
	{"hen0",      0xaa0000, 300,  8, true, },
	{"ctr0",      0xab0000, 460, 16, true, },
	{"ctr1",      0xab1000, 460, 16, true, },
	{"chn",       0xb58000, 250,  8, true, },
	{"avd",       0xb68000, 297,  8, true, },
	{"rot_r",     0xb90000, 450, 16, true, },
	{"rot_w",     0xb91000, 450, 16, true, },
	{"frc_bif0",  0xbc0000, 450, 16, true, },
	{"frc_bif1",  0xbc1000, 450, 16, true, },
	{"frc_bif2",  0xbc2000, 450, 16, true, },
	{"frc_bif3",  0xbc3000, 450, 16, true, },
	{"sclr_0",    0x7c0000, 600, 16, true, },
	{"sclr_1",    0x7c1000, 600, 16, true, },
	{"sclr_2",    0x7c2000, 600, 16, true, },
	{"sclr_3",    0x7c3000, 600,  8, true, },
	{"sclw_0",    0x7c4000, 600, 16, true, },
	{"sclw_1",    0x7c5000, 600, 16, true, },
	{"sclw_2",    0x7c6000, 600,  8, true, },
	{"nrc_r",     0x7d0000, 300, 16, true, },
	{"nrc_w",     0x7d1000, 300, 16, true, },
	{"rber",      0x7e0000, 450, 16, true, },  //muse-m only
	{"rbew",      0x7e1000, 450, 16, true, },  //muse-m only
	{"spro0",     0x964000, 600, 16, false, }, //muse-m only
	{"spro1",     0x966000, 600,  4, false, }, //muse-m only
};

static u32 sdp1803_get_prevent_err_w(void *dev)
{
	return sdp_busmon_readl(dev, 0x2c) << 16;
}
static u32 sdp1803_get_prevent_err_r(void *dev)
{
	return sdp_busmon_readl(dev, 0x2c) & 0xffff0000ul;
}
static int sdp1803_set_filter(void *dev, int ch)
{
	if (ch == 0)
		sdp_busmon_writel(dev, 0x34, 0x9f009f00);
	else if (ch == 1)
		sdp_busmon_writel(dev, 0x34, 0xffa0ffa0);
	else
		sdp_busmon_writel(dev, 0x34, 0x00000000);

	return 0;		
}
static struct sdp_busmon_ops sdp1803_busmon_ops = {
	.get_prevent_err_w = sdp1803_get_prevent_err_w,
	.get_prevent_err_r = sdp1803_get_prevent_err_r,
	.set_filter = sdp1803_set_filter,
};

static void sdp1804_fixup_devs(void)
{
	int i;
	sdp1803_busmon_devs[17].bl = 8; //adsp bl=8
	for (i = 10; i <= 13; i++)
		sdp1803_busmon_devs[i].freq = 460; //dvde freq=460

}

static int __init sdp_busmon_init(void)
{
	struct sdp_busmon *busmon = get_busmon();

	spin_lock_init(&busmon->lock);

	if (soc_is_sdp1601()) {
		sdp_busmon_set_devs(sdp1601_busmon_devs, ARRAY_SIZE(sdp1601_busmon_devs));
		sdp_busmon_set_ops(&sdp1601_busmon_ops);
		sdp_busmon_set_busfreq(500);
	}
	else if (soc_is_sdp1803()) {
		sdp_busmon_set_devs(sdp1803_busmon_devs, ARRAY_SIZE(sdp1803_busmon_devs));
		sdp_busmon_set_ops(&sdp1803_busmon_ops);
		sdp_busmon_set_busfreq(500);
	}
	else if (soc_is_sdp1804()) {
		sdp_busmon_set_devs(sdp1803_busmon_devs, ARRAY_SIZE(sdp1803_busmon_devs)-4);
		sdp_busmon_set_ops(&sdp1803_busmon_ops);
		sdp_busmon_set_busfreq(500);
		sdp1804_fixup_devs();
	}

	INIT_DELAYED_WORK(&busmon->work, sdp_busmon_work);

	alloc_stat_buf(16*1024);

#if defined(CONFIG_DEBUG_FS)
	sdp_busmon_register_debugfs();
#endif

	return 0;
}

subsys_initcall(sdp_busmon_init);

MODULE_AUTHOR("yongjin79.kim@samsung.com");
MODULE_DESCRIPTION("driver for sdp bus monitor");


