/*
 *
 * Copyright (C) 2008-2009 Palm, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * [NOTICE]
 * modified by Baik - Samsung
 * this module must examine about license.
 * If SAMSUNG have a problem with the license, we must re-program module for
 * low memory notification.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/hugetlb.h>

#include <linux/sched.h>

#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/shmem_fs.h>

#include <asm/atomic.h>

#include <linux/slp_lowmem_notify.h>

#define MB(pages) ((K(pages))/1024)
#define K(pages) ((pages) << (PAGE_SHIFT - 10))

bool dump_tasks_once_lv2;

struct memnotify_file_info {
	int last_threshold;
	struct file *file;
	unsigned int nr_events;
};

static DECLARE_WAIT_QUEUE_HEAD(memnotify_wait);
static atomic_t nr_watcher_task = ATOMIC_INIT(0);

#define NR_MEMNOTIFY_LEVEL 3

enum {
	THRESHOLD_NORMAL,
	THRESHOLD_LOW,
	THRESHOLD_CRITICAL
};

static const char *_threshold_string[NR_MEMNOTIFY_LEVEL] = {
	"normal  ",
	"low     ",
	"critical"
};

static const char *threshold_string(int threshold)
{
	if (threshold >= NR_MEMNOTIFY_LEVEL)
		return "";

	return _threshold_string[threshold];
}

static unsigned long memnotify_messages[NR_MEMNOTIFY_LEVEL] = {
	MEMNOTIFY_NORMAL,	/* The happy state */
	MEMNOTIFY_LOW,		/* Userspace drops uneeded memory */
	MEMNOTIFY_CRITICAL	/* Userspace OOM Killer */
};

static atomic_t memnotify_last_threshold = ATOMIC_INIT(THRESHOLD_NORMAL);

static size_t memnotify_enter_thresholds[NR_MEMNOTIFY_LEVEL] = {
	INT_MAX,
	60,
	40
};

static size_t memnotify_leave_thresholds[NR_MEMNOTIFY_LEVEL] = {
	INT_MAX,
	61,
	41
};

static inline unsigned long memnotify_get_available(void)
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

	free_cma = global_page_state(NR_FREE_CMA_PAGES);

	return mem_available - free_cma;
}

static inline unsigned long memnotify_get_used(void)
{
	return totalram_pages - memnotify_get_available();
}

static inline unsigned long memnotify_get_total(void)
{
	return totalram_pages;
}

/* dump_tasks func to dump current memory state of all system tasks. */
extern void dump_tasks(const struct mem_cgroup *mem,
		    const nodemask_t *nodemask, struct seq_file *s);

static struct task_rss_t {
	pid_t pid;
	char comm[TASK_COMM_LEN];
	long rss;
} task_rss[256];		/* array for tasks and rss info */
static int nr_tasks;		/* number of tasks to be stored */
static int check_peak;		/* flag to store the rss or not */
static long old_available;	/* last minimum of free memory size */

static void save_task_rss(void)
{
	struct task_struct *p, *tsk;
	int i;

	nr_tasks = 0;
	read_lock(&tasklist_lock);
	for_each_process(tsk) {
		if (tsk->flags & PF_KTHREAD)
			continue;

		if (!pid_alive(tsk))
			continue;

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		task_rss[nr_tasks].pid = p->pid;
		strlcpy(task_rss[nr_tasks].comm, p->comm,
			sizeof(task_rss[nr_tasks].comm));
		task_rss[nr_tasks].rss = 0;

		/* If except pages in swap, use get_mm_rss() */
		for (i = 0; i < NR_MM_COUNTERS; i++)
			task_rss[nr_tasks].rss +=
			p->mm->rss_stat.count[i].counter;

		if ((++nr_tasks) > ARRAY_SIZE(task_rss)) {
			task_unlock(p);
			break;
		}
		task_unlock(p);
	}
	read_unlock(&tasklist_lock);
}

static void memnotify_wakeup(int threshold)
{
	atomic_set(&memnotify_last_threshold, threshold);
	wake_up_interruptible_all(&memnotify_wait);
}

void memnotify_threshold(gfp_t gfp_mask)
{
	unsigned long available;
	int threshold;
	int last_threshold;
	int i;

	if (!(gfp_mask & __GFP_WAIT))
		return;

	available = MB(memnotify_get_available());
	threshold = THRESHOLD_NORMAL;
	last_threshold = atomic_read(&memnotify_last_threshold);
	if (last_threshold >= NR_MEMNOTIFY_LEVEL)
		last_threshold = NR_MEMNOTIFY_LEVEL - 1;

	/* we save the tasks and rss info when free memory size is minimum,
	 * which means total used memory is highest at that moment. */
	if (check_peak && (old_available > available)) {
		old_available = available;
		save_task_rss();
	}

	/* Obtain enter threshold level */
	for (i = (NR_MEMNOTIFY_LEVEL - 1); i >= 0; i--) {
		if (available < memnotify_enter_thresholds[i]) {
			threshold = i;
			break;
		}
	}

	/* dump tasks only once */
	if ((threshold == THRESHOLD_CRITICAL) &&
		   (dump_tasks_once_lv2 == false)) {
		dump_tasks_once_lv2 = true;
		dump_tasks(NULL, NULL, NULL);
	}

	/* return quickly when normal case */
	if (likely(threshold == THRESHOLD_NORMAL &&
		   last_threshold == THRESHOLD_NORMAL))
		return;

	/* Need to leave a threshold by a certain margin. */
	if (threshold < last_threshold) {
		int leave_threshold =
		    memnotify_leave_thresholds[last_threshold];

		if (available < leave_threshold)
			threshold = last_threshold;
	}

	if (last_threshold == threshold)
		return;

	/* Rate limited notification of threshold changes. */
	memnotify_wakeup(threshold);

}
EXPORT_SYMBOL(memnotify_threshold);

static int lowmemnotify_open(struct inode *inode, struct file *file)
{
	struct memnotify_file_info *info;
	int err = 0;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}

	info->file = file;
	info->nr_events = 0;
	file->private_data = info;
	atomic_inc(&nr_watcher_task);
 out:
	return err;
}

static int lowmemnotify_release(struct inode *inode, struct file *file)
{
	struct memnotify_file_info *info = file->private_data;

	kfree(info);
	atomic_dec(&nr_watcher_task);
	return 0;
}

static unsigned int lowmemnotify_poll(struct file *file, poll_table * wait)
{
	unsigned int retval = 0;
	struct memnotify_file_info *info = file->private_data;
	int threshold;

	poll_wait(file, &memnotify_wait, wait);

	threshold = atomic_read(&memnotify_last_threshold);

	if (info->last_threshold != threshold) {
		info->last_threshold = threshold;
		retval = POLLIN;
		info->nr_events++;

		pr_info("%s (%d%%, Used %ldMB, Free %ldMB, %s)\n",
			__func__,
			(int)(MB(memnotify_get_used()) * 100
			      / MB(memnotify_get_total())),
			MB(memnotify_get_used()),
			MB(memnotify_get_available()),
			threshold_string(threshold));

	} else if (info->nr_events > 0)
		retval = POLLIN;

	return retval;
}

static ssize_t lowmemnotify_read(struct file *file,
				 char __user *buf, size_t count, loff_t *ppos)
{
	int threshold;
	unsigned long data;
	ssize_t ret = 0;
	struct memnotify_file_info *info = file->private_data;

	if (count < sizeof(unsigned long))
		return -EINVAL;

	threshold = atomic_read(&memnotify_last_threshold);
	data = memnotify_messages[threshold];

	ret = put_user(data, (unsigned long __user *)buf);
	if (0 == ret) {
		ret = sizeof(unsigned long);
		info->nr_events = 0;
	}
	return ret;
}


static ssize_t threshold_lv1_store(struct class *class, struct class_attribute *attr,
			    const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		memnotify_enter_thresholds[THRESHOLD_LOW] = val;
		pr_info("[LMF] lv1 threshold set : %ld MB\n", val);
		/* set leave thresholds with the margin */
	}
	return ret;
}

static ssize_t threshold_lv2_store(struct class *class, struct class_attribute *attr,
			    const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		memnotify_enter_thresholds[THRESHOLD_CRITICAL] = val;
		pr_info("[LMF] lv2 threshold set : %ld MB\n", val);
	}
	return ret;
}

static ssize_t threshold_leave_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		memnotify_leave_thresholds[THRESHOLD_LOW] = val;
		memnotify_leave_thresholds[THRESHOLD_CRITICAL] = val;
		pr_info("[LMF] leave threshold set : %ld MB\n", val);
	}
	return ret;
}


static ssize_t dump_tasks_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		if (val == 1) {
#ifndef CONFIG_VD_RELEASE
			dump_tasks(NULL, NULL, NULL);
			pr_info("[LMF]dump_tasks was called by resourced\n");
#endif
		}
		val = 0;
	}
	return ret;
}


static CLASS_ATTR(threshold_lv1, S_IWUSR, NULL, threshold_lv1_store);
static CLASS_ATTR(threshold_lv2, S_IWUSR, NULL, threshold_lv2_store);
static CLASS_ATTR(threshold_leave, S_IWUSR, NULL, threshold_leave_store);
static CLASS_ATTR(dump_tasks, S_IWUSR, NULL, dump_tasks_store);


static const struct file_operations memnotify_fops = {
	.open = lowmemnotify_open,
	.release = lowmemnotify_release,
	.read = lowmemnotify_read,
	.poll = lowmemnotify_poll,
};

static struct device *memnotify_device;
static struct class *memnotify_class;
static int memnotify_major = -1;

static int __init lowmemnotify_init(void)
{
	int err;

	pr_info("[LMF] Low Memory Notify loaded\n");

	memnotify_enter_thresholds[0] = MB(totalram_pages);
	memnotify_leave_thresholds[0] = MB(totalram_pages);

	memnotify_major = register_chrdev(0, MEMNOTIFY_DEVICE, &memnotify_fops);
	if (memnotify_major < 0) {
		pr_err("Unable to get major number for memnotify dev\n");
		err = -EBUSY;
		goto error_create_chr_dev;
	}

	memnotify_class = class_create(THIS_MODULE, MEMNOTIFY_DEVICE);
	if (IS_ERR(memnotify_class)) {
		err = PTR_ERR(memnotify_class);
		goto error_class_create;
	}

	memnotify_device =
	    device_create(memnotify_class, NULL, MKDEV(memnotify_major, 0),
			  NULL, MEMNOTIFY_DEVICE);

	if (IS_ERR(memnotify_device)) {
		err = PTR_ERR(memnotify_device);
		goto error_create_class_dev;
	}

	err = class_create_file(memnotify_class, &class_attr_threshold_lv1);
	if (err) {
		pr_err("%s: couldn't create threshold level 1.\n", __func__);
		goto error_create_threshold_lv1_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_threshold_lv2);
	if (err) {
		pr_err("%s: couldn't create threshold level 2.\n", __func__);
		goto error_create_threshold_lv2_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_threshold_leave);
	if (err) {
		pr_err("%s: couldn't create threshold leave.\n", __func__);
		goto error_create_threshold_leave_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_dump_tasks);
	if (err) {
		pr_err("%s: couldn't create calling_dumptasks.\n", __func__);
		goto error_create_dump_tasks;
	}

	/* set initial free memory with total memory size */
	old_available = MB(memnotify_get_total());

	return 0;

 error_create_dump_tasks:
	class_remove_file(memnotify_class, &class_attr_threshold_leave);
 error_create_threshold_leave_class_file:
	class_remove_file(memnotify_class, &class_attr_threshold_lv2);
 error_create_threshold_lv2_class_file:
	class_remove_file(memnotify_class, &class_attr_threshold_lv1);
 error_create_threshold_lv1_class_file:
	device_del(memnotify_device);
 error_create_class_dev:
	class_destroy(memnotify_class);
 error_class_create:
	unregister_chrdev(memnotify_major, MEMNOTIFY_DEVICE);
 error_create_chr_dev:

	return err;
}

static void __exit lowmemnotify_exit(void)
{
	if (memnotify_device)
		device_del(memnotify_device);
	if (memnotify_class)
		class_destroy(memnotify_class);
	if (memnotify_major >= 0)
		unregister_chrdev(memnotify_major, MEMNOTIFY_DEVICE);
}

module_init(lowmemnotify_init);
module_exit(lowmemnotify_exit);

MODULE_LICENSE("GPL");
