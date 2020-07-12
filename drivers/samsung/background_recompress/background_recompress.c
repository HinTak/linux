#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/kthread.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/freezer.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/background_recompress.h>
#include <linux/kernel_stat.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

#include "bgcomp.h"

#define BACKGROUND_RECOMPRESS_VERSION	1
#define BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE 10

static const char *default_compressor =
#ifdef CONFIG_CRYPTO_ZSTD
"zstd";
#else
"lz4";
#endif

struct bgrecomp_cpuusage_data {
	cputime64_t user;	/* User CPU */
	cputime64_t system;	/* CPU spend in system */
};

struct recomp_stats {
	atomic64_t recomp_count; /* count of recompressed */
	atomic64_t org_compr_data_size; /* origin compressed size of pages stored */
	atomic64_t re_compr_data_size; /* re-compress size of pages stored */
	atomic64_t recomp_cpu_threshold; /* idle threshold */
	atomic64_t recomp_scan_count; /* count of scan in idle state */
	atomic64_t recomp_interval; /* try recompress after interval */
	atomic64_t recompd_timeout; /* bgrecompd wakeup timeout */
	atomic64_t recomp_min; /* min size threshold of adding lru to recompress */
	atomic64_t recomp_max; /* max size threshold of adding lru to recompress */
};

struct background_recomp {
	struct bgcomp *comp;
	struct rw_semaphore init_lock;
	struct recomp_stats stats;
	char compressor[CRYPTO_MAX_ALG_NAME];
	int compress_level;
};

static LIST_HEAD(recomp_list);
static DEFINE_SPINLOCK(recomp_lock);

static struct background_recomp *bgrecomp;
static struct kobject *bgrecomp_kobj;
static struct bgrecomp_cpuusage_data prev_cpu_data[NR_CPUS];
struct page *recomp_page;
static int bgrecomp_get_prev_cpuusage;
#ifdef CONFIG_CRYPTO_ZSTD
extern int ZSTD_maxCLevel(void);
#endif

bool dynamic_bgrecomp_enable;

#define SEC_BGRECOMP_THREAD_DELAY 2
/* Milliseconds bgrecompd should sleep between batches */
#define BGRECOMP_THREAD_SLEEP_MILLISECS(x) ((x) * 1000)
#define BGRECOMP_THREAD_SLEEP_SECS(x) ((x) / 1000)
#define BGRECOMP_THREAD_UPDATE_TICKS (SEC_BGRECOMP_THREAD_DELAY * HZ)

static int bgrecomp_get_maxLevel(void)
{
#ifdef CONFIG_CRYPTO_ZSTD
	return ZSTD_maxCLevel();
#else
	return 0;
#endif
}

/* (x) is the difference between previous ticks and current ticks.
 * So it can be changed from u64 to int.
 */
#define BGRECOMP_TIME_TO_PERCENT(x)	(((int)(x)) * 100 / (int)BGRECOMP_THREAD_UPDATE_TICKS)

static inline void bgrecomp_deprecated_attr_warn(const char *name)
{
    pr_warn_once("%d (%s) Attribute %s (and others) will be removed.\n",
            task_pid_nr(current),
            current->comm,
            name);
}

#define BGRECOMPRESS_ATTR_RO(name)                      \
static ssize_t name##_show(struct device *d,        \
                struct device_attribute *attr, char *b) \
{                                   \
    bgrecomp_deprecated_attr_warn(__stringify(name));            \
    return scnprintf(b, PAGE_SIZE, "%llu\n",            \
        (u64)atomic64_read(&bgrecomp->stats.name));         \
}                                   \
static DEVICE_ATTR_RO(name);

BGRECOMPRESS_ATTR_RO(org_compr_data_size);
BGRECOMPRESS_ATTR_RO(re_compr_data_size);
BGRECOMPRESS_ATTR_RO(recomp_count);

static ssize_t recomp_algorithm_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    size_t sz;

    down_read(&bgrecomp->init_lock);
    sz = bgcomp_available_show(bgrecomp->compressor, buf);
    up_read(&bgrecomp->init_lock);

    return sz;
}

static ssize_t recomp_algorithm_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
    char compressor[CRYPTO_MAX_ALG_NAME];
    size_t sz;

    strlcpy(compressor, buf, sizeof(compressor));
    /* ignore trailing newline */
    sz = strlen(compressor);
    if (sz > 0 && compressor[sz - 1] == '\n')
        compressor[sz - 1] = 0x00;

    if (!bgcomp_available_algorithm(compressor))
        return -EINVAL;

    down_write(&bgrecomp->init_lock);
    strlcpy(bgrecomp->compressor, compressor, sizeof(compressor));
    up_write(&bgrecomp->init_lock);
    return len;
}

static ssize_t comp_level_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", bgrecompress_get_recomp_level());
}

static ssize_t comp_level_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int compr_level = 0;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &compr_level);
	if (err || compr_level > bgrecomp_get_maxLevel()) {
		up_write(&bgrecomp->init_lock);
		return -EINVAL;
	}

	bgrecompress_set_comp_level(compr_level);
	set_thread_flag(TIF_BGRECOMPD);
	down_write(&bgrecomp->comp->strm_lock);
	err = bgcomp_level_reset(bgrecomp->comp);
	up_write(&bgrecomp->comp->strm_lock);
	clear_thread_flag(TIF_BGRECOMPD);
	up_write(&bgrecomp->init_lock);

	if (err) 
		pr_info("[background_recompress] fail to reset comp_level(%d)", compr_level);

	return len;
}

static ssize_t recomp_cpu_threshold_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					(int)atomic64_read(&bgrecomp->stats.recomp_cpu_threshold));
}

static ssize_t recomp_cpu_threshold_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int threshold;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &threshold);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recomp_cpu_threshold, threshold);

	return len;
}

static ssize_t recomp_scan_count_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					(int)atomic64_read(&bgrecomp->stats.recomp_scan_count));
}

static ssize_t recomp_scan_count_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int count;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &count);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recomp_scan_count, count);

	return len;
}

static ssize_t recomp_interval_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					(int)atomic64_read(&bgrecomp->stats.recomp_interval));
}

static ssize_t recomp_interval_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int interval;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &interval);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recomp_interval, interval);

	return len;
}

static ssize_t recompd_timeout_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					BGRECOMP_THREAD_SLEEP_SECS((int)atomic64_read(&bgrecomp->stats.recompd_timeout)));
}

static ssize_t recompd_timeout_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int timeout;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &timeout);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recompd_timeout, timeout);

	return len;
}

static ssize_t recomp_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)atomic64_read(&bgrecomp->stats.recomp_min));
}

static ssize_t recomp_min_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int min;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &min);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recomp_min, min);

	return len;
}

static ssize_t recomp_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)atomic64_read(&bgrecomp->stats.recomp_max));
}

static ssize_t recomp_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned int max;

	down_write(&bgrecomp->init_lock);
	err = kstrtouint(buf, BGRECOMPD_COMPRESS_LEVEL_NUMBER_BASE, &max);
	up_write(&bgrecomp->init_lock);
	if (err)
		return -EINVAL;

	atomic64_set(&bgrecomp->stats.recomp_max, max);

	return len;
}

static DEVICE_ATTR_RW(recomp_algorithm);
static DEVICE_ATTR_RW(comp_level);
static DEVICE_ATTR_RW(recomp_cpu_threshold);
static DEVICE_ATTR_RW(recomp_scan_count);
static DEVICE_ATTR_RW(recomp_interval);
static DEVICE_ATTR_RW(recompd_timeout);
static DEVICE_ATTR_RW(recomp_min);
static DEVICE_ATTR_RW(recomp_max);

static struct attribute *bgrecomp_attrs[] = {
	&dev_attr_recomp_algorithm.attr,
	&dev_attr_comp_level.attr,
	&dev_attr_org_compr_data_size.attr,
	&dev_attr_re_compr_data_size.attr,
	&dev_attr_recomp_cpu_threshold.attr,
	&dev_attr_recomp_scan_count.attr,
	&dev_attr_recomp_interval.attr,
	&dev_attr_recomp_count.attr,
	&dev_attr_recompd_timeout.attr,
	&dev_attr_recomp_min.attr,
	&dev_attr_recomp_max.attr,
	NULL,
};

static struct attribute_group bgrecomp_attr_group = {
	.attrs = bgrecomp_attrs,
};

/*
 * If recompress pages exist in bgrecomp_list try to re-compress.
 */
static bool is_bgrecomp_run(void)
{
	return !list_empty(&recomp_list);
}

extern bool check_allocator_chunk(int, int);

/* Converts an allocation size in bytes to size in z4fold chunks */
static bool is_chunk_reduced(size_t org_size, size_t new_size)
{
	return	check_allocator_chunk(org_size, new_size);
}

bool check_kswapd_run_state(void)
{
	pg_data_t *pgdat;
	int nid;
	bool ret = true;
	unsigned int state;
	
	for_each_node_state(nid, N_MEMORY) {
		pgdat = NODE_DATA(nid);
		
		if (pgdat->kswapd) {
			state = (pgdat->kswapd->state | pgdat->kswapd->exit_state) & TASK_REPORT;
	
			if(fls(state) != TASK_RUNNING) {
				ret = false;
				break;
			}
		}
	}

	return ret;
}

unsigned int __get_bgrecomp_min(void)
{
	return (unsigned int)atomic64_read(&bgrecomp->stats.recomp_min);
}
EXPORT_SYMBOL(__get_bgrecomp_min);

unsigned int __get_bgrecomp_max(void)
{
	return (unsigned int)atomic64_read(&bgrecomp->stats.recomp_max);
}
EXPORT_SYMBOL(__get_bgrecomp_max);

void __bgrecompress_list_store(struct list_head *recompd)
{
	spin_lock(&recomp_lock);
	list_add_tail(recompd, &recomp_list);
	spin_unlock(&recomp_lock);
}
EXPORT_SYMBOL(__bgrecompress_list_store);

void __bgrecompress_invalidate(struct list_head *recompd)
{
	spin_lock(&recomp_lock);
	if (!list_empty(recompd))
		list_del_init(recompd);
	spin_unlock(&recomp_lock);
}
EXPORT_SYMBOL(__bgrecompress_invalidate);

int __bgrecompd_decompress_page(unsigned char *cmem, unsigned char *mem, unsigned int size)
{
	int ret = 0;

	if (size == PAGE_SIZE) { 
		memcpy(mem, cmem, PAGE_SIZE);
	} else {
		struct bgcomp_strm *bgstrm = bgcomp_stream_get(bgrecomp->comp);

		if (!bgstrm) {
			pr_err("[background_recompress] Can't reset %s compressing backend\n", bgrecomp->compressor);
			return -EINVAL;
		}

		BUG_ON(!preemptible());

		down_read(&bgrecomp->comp->strm_lock);
		ret = bgcomp_decompress(bgstrm, cmem, size, mem);
		up_read(&bgrecomp->comp->strm_lock);
		bgcomp_stream_put(bgrecomp->comp);
	}

	return ret;
}
EXPORT_SYMBOL(__bgrecompd_decompress_page);

int __bgrecompd_recompress_and_update_page(struct page *page, u32 index, unsigned int clen, unsigned int *rclen)
{
	int ret = 0;
	struct bgcomp_strm *bgstrm;
	unsigned char *cmem, *src;

	bgstrm = bgcomp_stream_get(bgrecomp->comp);
	if (!bgstrm) {
		pr_err("[background_recompress] Can't reset %s compressing backend\n", bgrecomp->compressor);
		return -EINVAL;
	}

	cmem = page_address(page);

	if (!cmem) {
		pr_err("[background_recompress] fail to map");
		goto update_fail;
	}

	BUG_ON(!preemptible());

	/* use MAX level only in bgrecompd */
	set_thread_flag(TIF_BGRECOMPD);
	down_read(&bgrecomp->comp->strm_lock);
	ret = bgcomp_compress(bgstrm, cmem, rclen);
	up_read(&bgrecomp->comp->strm_lock);
	clear_thread_flag(TIF_BGRECOMPD);
	if (ret) {
		pr_err("[background_recompress] fail to compress! err=%d", ret);
		goto comp_fail;
	}

	cmem = NULL;

	src = bgstrm->buffer;
	bgcomp_stream_put(bgrecomp->comp);
	if (is_chunk_reduced(clen, *rclen)) {
		// save new data and free org data. 
		ret = bgrecompress_zram_update(src, index, clen, *rclen);
		if (ret) {
			pr_err("[background_recompress] fail to update! err=%d",ret); 
			goto update_fail;
		}

		atomic64_add(clen, &bgrecomp->stats.org_compr_data_size);
		atomic64_add(*rclen, &bgrecomp->stats.re_compr_data_size);
		atomic64_inc(&bgrecomp->stats.recomp_count);
	}

	return ret;
comp_fail:
	bgcomp_stream_put(bgrecomp->comp);
update_fail:
	return ret;
}

int recompress(void)
{
	int ret = 0;
	struct bgrecomp_handle *recomp_handle;
	unsigned int clen, rclen=0;
	unsigned int now, saved_time, gap;
	u32 index;
	
	if (recomp_page) {
		spin_lock(&recomp_lock);
		recomp_handle = list_first_entry_or_null(&recomp_list, struct bgrecomp_handle, bgrecomp_lru);

		if (!recomp_handle) {
			ret = RECOMP_GOTO_SLEEP;
			spin_unlock(&recomp_lock);
			goto out;
		}
		clen = bgrecompd_get_clen(recomp_handle->data);
		index =	recomp_handle->index;
		saved_time = bgrecompd_get_time(recomp_handle->data);
		spin_unlock(&recomp_lock);

		now = bgrecompd_get_timestamp() & BGRECOMP_TIME_BIT_MASK;
		if (now < saved_time)
			now += BGRECOMP_TIME_BIT_MASK + 1;

		gap = now - saved_time;
		if (gap < (unsigned int)atomic64_read(&bgrecomp->stats.recomp_interval)) {
			gap = (unsigned int)atomic64_read(&bgrecomp->stats.recomp_interval) - gap;
			if (gap > SEC_BGRECOMP_THREAD_DELAY)
				atomic64_set(&bgrecomp->stats.recompd_timeout, BGRECOMP_THREAD_SLEEP_MILLISECS(gap));

			ret = RECOMP_GOTO_SLEEP;
			goto out;
		}

		ret = zram_handle_recomp_flag(index, RECOMP_SET);
		if (ret) {
			pr_err("[background_recompress] skip!!(%d)", ret);
			goto out;
		}

		spin_lock(&recomp_lock);
		if (recomp_handle == list_first_entry_or_null(&recomp_list, struct bgrecomp_handle, bgrecomp_lru)) {
			BUG_ON(list_empty(&(recomp_handle->bgrecomp_lru)));
			list_del_init(&(recomp_handle->bgrecomp_lru));
		} else
			ret = RECOMP_TRY_NEXT;
		spin_unlock(&recomp_lock);

		if (ret) {
			pr_err("[background_recompress] data changed");
			zram_handle_recomp_flag(index, RECOMP_CLEAR);
			goto out;
		}

		ret = bgrecompress_decompress(recomp_page, index, clen);
		if (ret) {
			pr_err("[background_recompress] fail to decompress!(%d)", ret);
			zram_handle_recomp_flag(index, RECOMP_CLEAR);
			goto out;
		}

		ret = __bgrecompd_recompress_and_update_page(recomp_page, index, clen, &rclen);
		if (ret) {
			zram_handle_recomp_flag(index, RECOMP_CLEAR);
			goto out;
		}
	} else {
		pr_err("Fail to allocate page!!");
		ret = RECOMP_GOTO_SLEEP;
	}

out:
	cond_resched();
	return ret;
}

bool bgrecomp_check_cpu_stat(void)
{
	bool ret = false;
	int cpu = 0;
	int bgrecomp_cpuusage = 0;

	if (!bgrecomp_get_prev_cpuusage) {
		for_each_online_cpu(cpu) {
			prev_cpu_data[cpu].user = kcpustat_cpu(cpu).cpustat[CPUTIME_USER] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

			prev_cpu_data[cpu].system =
			    kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
		}

		bgrecomp_get_prev_cpuusage = 1;

		return ret;
	}

	for_each_online_cpu(cpu) {
		struct bgrecomp_cpuusage_data curr_cpu_data;

		curr_cpu_data.user = kcpustat_cpu(cpu).cpustat[CPUTIME_USER] +
		    kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

		curr_cpu_data.system = kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM] +
		    kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ] + kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
		WARN_ON(!bgrecomp_get_prev_cpuusage);

		bgrecomp_cpuusage += (int)BGRECOMP_TIME_TO_PERCENT(curr_cpu_data.user - prev_cpu_data[cpu].user) +
	    (int)BGRECOMP_TIME_TO_PERCENT(curr_cpu_data.system - prev_cpu_data[cpu].system);

		/* store result for next sampling calculation */
		prev_cpu_data[cpu].user = curr_cpu_data.user;
		prev_cpu_data[cpu].system = curr_cpu_data.system;
	}

	if (bgrecomp_cpuusage / NR_CPUS  < (int)atomic64_read(&bgrecomp->stats.recomp_cpu_threshold))
		ret = true;

	return ret;
}

int try_background_recompress(void *tmp) {
	int scan_cnt = 0;

	set_user_nice(current, MAX_NICE);
	while (!kthread_should_stop()) {
		if (bgrecomp_check_cpu_stat() && is_bgrecomp_run()) {
			if (!check_kswapd_run_state() &&
				!recompress() &&
				scan_cnt < (int)atomic64_read(&bgrecomp->stats.recomp_scan_count)) {
				scan_cnt++;
				continue;
			} else {
				scan_cnt = 0;
			}
		}

		try_to_freeze();

		schedule_timeout_interruptible(
				msecs_to_jiffies((int)atomic64_read(&bgrecomp->stats.recompd_timeout)));
		if ((unsigned int)atomic64_read(&bgrecomp->stats.recompd_timeout) > BGRECOMP_THREAD_SLEEP_MILLISECS(SEC_BGRECOMP_THREAD_DELAY))
			atomic64_set(&bgrecomp->stats.recompd_timeout, BGRECOMP_THREAD_SLEEP_MILLISECS(SEC_BGRECOMP_THREAD_DELAY));
	}

	return 0;
}

static bool init_bgrecomp(void)
{
	struct bgcomp *comp;

	bgrecompress_set_comp_level(CONFIG_BACKGROUND_RECOMPRESS_LEVEL);
	bgrecomp = kzalloc(sizeof(struct background_recomp), GFP_KERNEL);

	if (!bgrecomp) {
		pr_err("[background_recompress] Can't allocate background_recomp");
		return false;
	}
	
	init_rwsem(&bgrecomp->init_lock);
	strlcpy(bgrecomp->compressor, default_compressor, sizeof(bgrecomp->compressor));
	set_thread_flag(TIF_BGRECOMPD);
	comp = bgcomp_create(bgrecomp->compressor);
	clear_thread_flag(TIF_BGRECOMPD);
	if (IS_ERR(comp)) {
		pr_err("[background_recompress] Can't initialize %s compressing backend(%li)\n", 
				bgrecomp->compressor, PTR_ERR(comp));
		return false;
	}
	bgrecomp->comp = comp;

	recomp_page = alloc_page(GFP_KERNEL | __GFP_ZERO | __GFP_NO_KSWAPD);
	if (!recomp_page) {
		pr_err("[background_recompress] Can't alloc recomp page");
		return false;
	}

	atomic64_set(&bgrecomp->stats.recomp_min, CONFIG_BACKGROUND_RECOMPRESS_MIN_SIZE);
	atomic64_set(&bgrecomp->stats.recomp_max, CONFIG_BACKGROUND_RECOMPRESS_MAX_SIZE);
	atomic64_set(&bgrecomp->stats.recomp_cpu_threshold, CONFIG_BACKGROUND_RECOMPRESS_CPU_THRESHOLD);
	atomic64_set(&bgrecomp->stats.recomp_scan_count, CONFIG_BACKGROUND_RECOMPRESS_SCAN_COUNT);
	atomic64_set(&bgrecomp->stats.recomp_interval, CONFIG_BACKGROUND_RECOMPRESS_INTERVAL);
	atomic64_set(&bgrecomp->stats.recompd_timeout, BGRECOMP_THREAD_SLEEP_MILLISECS(SEC_BGRECOMP_THREAD_DELAY));

	return true;
}

static int __init set_dynamic_bgrecomp_enable(char *str)
{
	dynamic_bgrecomp_enable = true;
	return 0;
}
early_param("only_entry_model", set_dynamic_bgrecomp_enable);

static int __init background_recompress_init(void)
{
	struct task_struct *tsk;
	int ret = 0;

	if (!dynamic_bgrecomp_enable)
		return ret;

	pr_info("[background_recompress][Ver(%d)]\n", BACKGROUND_RECOMPRESS_VERSION);

	if (!init_bgrecomp()) {
		ret = -EINVAL;
		goto out;
	}

	bgrecomp_kobj = kobject_create_and_add("background_recompress", kernel_kobj);
	if (!bgrecomp_kobj) {
		printk(KERN_ERR "[background_recompress] failed to create kobj\n");
		ret = -EINVAL;
		goto kobj_out;
	}

	ret = sysfs_create_group(bgrecomp_kobj, &bgrecomp_attr_group);
	if (ret) {
		printk(KERN_ERR "[background_recompress] failed to create sysfs\n");
		ret = -EINVAL;
		goto sysfs_out;
	}

	tsk = kthread_run(try_background_recompress, NULL , "bgrecompd");
	if (IS_ERR(tsk)) {
		pr_err("[background_recompress] unable to create bgrompd");
		ret = PTR_ERR(tsk);
		goto sysfs_out;
	}

	return 0;

sysfs_out:
	kobject_put(bgrecomp_kobj);
kobj_out:
	kfree(bgrecomp);
	if (recomp_page)
		__free_page(recomp_page);
out:
	return ret;
}

module_init(background_recompress_init);
