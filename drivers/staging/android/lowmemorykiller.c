/* drivers/misc/lowmemorykiller.c
 *
 * The lowmemorykiller driver lets user-space specify a set of memory thresholds
 * where processes with a range of oom_adj values will get killed. Specify the
 * minimum oom_adj values in /proc/sys/vm/lmk/adj and the
 * number of free pages in /proc/sys/vm/lmk/minfree. Both
 * files take a space separated list of numbers in ascending order.
 *
 * For example, write "0 8" to /proc/sys/vm/lmk/adj and
 * "1024 4096" to /proc/sys/vm/lmk/minfree to kill
 * processes with a oom_adj value of 8 or higher when the free memory drops
 * below 4096 pages and kill processes with a oom_adj value of 0 or higher
 * when the free memory drops below 1024 pages.
 *
 * If you write "2" to /proc/sys/vm/lmk/mode then LMK will automatically define
 * minfree parameter proportionally to min_free_kbytes value and
 * /proc/sys/vm/lmk/minfree_ratio values.
 *
 * The driver considers memory used for caches to be free, but if a large
 * percentage of the cached memory is locked this can be very inaccurate
 * and processes may not get killed until the normal oom killer is triggered.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/sched.h>

#include "lowmemorykiller.h"

#define ARRAYSIZE 6

static const int LMK_BACKGROUND_ADJ = 12;
static const int LMK_FOREGROUND_ADJ = 6;

static DEFINE_MUTEX(lowmem_lock); /* to protect from changing parameters during
run */

static uint32_t lowmem_debug_level = 1;
static uint32_t lowmem_enabled;   /* ability to turn on/off from user space:
				     * 0 - LMK is disabled
				     * 1 - LMK is enabled */
static uint32_t lowmem_mode = 2; /* change the way LMK deals with parameters:
				  * 0(default) - adjust minfree param only at
				  * init using min_free_kbytes value, if
				  * min_free_kbytes value changes, print dmesg
				  * message with appropriate params
				  * 1 - always adjust minfree param if
				  * min_free_kbytes value changes
				  * 2 - use user defined minfree_ratio and
				  * adj_ratio params to automatically change
				  * minfree if min_free_kbytes value changes
				  * Mode must be 2 at kernel start to
				  * automatically define new minfree and
				  * minfree_ratio values using watermarks*/

static int lowmem_adj[ARRAYSIZE] = {
	0,
	1,
	6,
	12,
};
static int lowmem_adj_size = 4;

static size_t lowmem_minfree[ARRAYSIZE] = {
	764,  /* pages_min */
	802,
	955,  /* pages_low */
	1146, /* pages_high */
};
static int lowmem_minfree_size = 4;

static long previous_min_pages;  /* pages low watermark, will be defined
at kernel start */
static long previous_high_pages; /* pages high watermark, will be defined
at kernel start */
static long previous_used_high_pages;

static int lowmem_adj_ratio[ARRAYSIZE] = {
	0,
	1,
	6,
	12,
};
static int lowmem_adj_ratio_size = 4;

static size_t lowmem_minfree_ratio[ARRAYSIZE] = { /* will be redefined using
watermarks at kernel start */
	0,   /* pages_min */
	0,
	75,  /* pages_low = (pages_min + pages_high)/2 */
	100, /* pages_high */
};
static int lowmem_minfree_ratio_size = 4;

#ifdef CONFIG_LMK_PRELOAD_APP
static unsigned int lmk_preload_prio(struct task_struct *p)
{
	return p->signal->lmk_preload_prio;
}

static unsigned long lmk_background_time(struct task_struct *p)
{
	return p->signal->lmk_background_time;
}
#endif

/* oom_adj conversion functions */
int oom_score_to_adj(short oom_score_adj);
short oom_adj_to_score(int oom_adj);

#define lowmem_print(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_info("LMK: " x);		\
	} while (0)

#define lowmem_print_cont(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_cont(x);			\
	} while (0)

static int lowmem_shrink(struct shrinker *s, struct shrink_control *sc);

static struct shrinker lowmem_shrinker = {
	.shrink = lowmem_shrink,
	.seeks = DEFAULT_SEEKS * 16
};

#if defined (CONFIG_KNBD_SUPPORT)
RAW_NOTIFIER_HEAD(lmk_chain);
EXPORT_SYMBOL(lmk_chain);
#endif

void lmk_print_params(void)
{
	unsigned int i;

	lowmem_print(0, "enabled: %s\n", lowmem_enabled ? "yes" : "no");
	if (!lowmem_enabled)
		return;

	lowmem_print(0, "mode: %u\n", lowmem_mode);

	lowmem_print(0, "          adj:");
	for (i = 0; i < lowmem_adj_size; ++i)
		pr_cont("%6u ", lowmem_adj[i]);
	lowmem_print_cont(0, "\n");

	lowmem_print(0, "    adj ratio:");
	for (i = 0; i < lowmem_adj_ratio_size; ++i)
		pr_cont("%6u ", lowmem_adj_ratio[i]);
	lowmem_print_cont(0, "\n");

	lowmem_print(0, "      minfree:");
	for (i = 0; i < lowmem_minfree_size; ++i)
		pr_cont("%6u ", lowmem_minfree[i]);
	lowmem_print_cont(0, "\n");

	lowmem_print(0, "minfree ratio:");
	for (i = 0; i < lowmem_minfree_ratio_size; ++i)
		pr_cont("%6u ", lowmem_minfree_ratio[i]);
	lowmem_print_cont(0, "\n");
}
EXPORT_SYMBOL(lmk_print_params);

/*
 * Change LowmemoryKiller Parameters when min_free_kbytes param is changed
 */
void calculate_lowmemkiller_params(long min_pages, long high_pages)
{
	int array_size, i;
	unsigned long minfree;
	BUG_ON(min_pages <= 0);
	BUG_ON(high_pages <= 0);
	lowmem_print(1, "low_wmark is %ld, high_wmark is %ld\n",
		     min_pages, high_pages);
	mutex_lock(&lowmem_lock);
	switch (lowmem_mode) {
	case 1:
		/* change params proportionally */
		array_size = ARRAYSIZE;
		if (lowmem_adj_size < array_size)
			array_size = lowmem_adj_size;
		if (lowmem_minfree_size < array_size)
			array_size = lowmem_minfree_size;
		lowmem_print(1, "lowmem minfree param changed to: ");
		for (i = 0; i < array_size; i++) {
			lowmem_minfree[i] = lowmem_minfree[i] * high_pages /
					    previous_used_high_pages;
			if (lowmem_minfree[i] < min_pages)
				lowmem_minfree[i] = min_pages+1;
			else
				if (lowmem_minfree[i] > high_pages)
					lowmem_minfree[i] = high_pages;
			lowmem_print_cont(1, " %zu,", lowmem_minfree[i]);
		}
		lowmem_print_cont(1, "\n");
		previous_used_high_pages = high_pages;
		break;
	case 2:
		/* change params using user-defined ratio values */
		array_size = ARRAYSIZE;
		if (lowmem_adj_ratio_size < array_size)
			array_size = lowmem_adj_ratio_size;
		if (lowmem_minfree_ratio_size < array_size)
			array_size = lowmem_minfree_ratio_size;
		lowmem_print(1, "lowmem minfree param changed to: ");
		for (i = 0; i < array_size; i++) {
			lowmem_minfree[i] = min_pages +
					    (lowmem_minfree_ratio[i] *
					    (high_pages - min_pages)) / 100;
			lowmem_adj[i] = lowmem_adj_ratio[i];
			lowmem_print_cont(1, " %zu,", lowmem_minfree[i]);
		}
		lowmem_print_cont(1, "\n");
		lowmem_adj_size = array_size;
		lowmem_minfree_size = array_size;

		for (i = lowmem_minfree_size; i < ARRAYSIZE; i++)
			lowmem_minfree[i] = 0;

		for (i = lowmem_adj_size; i < ARRAYSIZE; i++)
			lowmem_adj[i] = 0;

		previous_used_high_pages = high_pages;
		break;
	case 0:
	default: /* '?' */
		/* print appropriate proportionally adjusted minfree values */
		array_size = ARRAYSIZE;
		if (lowmem_adj_size < array_size)
			array_size = lowmem_adj_size;
		if (lowmem_minfree_size < array_size)
			array_size = lowmem_minfree_size;
		lowmem_print(1, "lowmem minfree param should be changed to: ");
		for (i = 0; i < array_size; i++) {
			minfree = (lowmem_minfree[i] * high_pages) /
				  previous_used_high_pages;
			if (minfree < min_pages)
				minfree = min_pages+1;
			else if (minfree > high_pages)
				minfree = high_pages;

			lowmem_print_cont(1, " %lu,", minfree);
		}
		lowmem_print_cont(1, "\n");
	}
	previous_min_pages = min_pages;
	previous_high_pages = high_pages;
	mutex_unlock(&lowmem_lock);
}
EXPORT_SYMBOL(calculate_lowmemkiller_params);

int get_parameters_count(void __user *buffer, size_t *length)
{
	int count = 0;
	int i;
	char *str = buffer;
	for (i = 0; i < *length - 2; i++)
		if ((str[i] == ' ') && (str[i+1] != ' '))
			count = count + 1;

	if (str[0] != ' ')
		count = count + 1;

	if (count > ARRAYSIZE)
		count = ARRAYSIZE;

	return count;
}

/* lmk parameters (files) */
int lowmem_mode_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int ret;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	mutex_unlock(&lowmem_lock);
	if (!ret)
		calculate_lowmemkiller_params(previous_min_pages,
					      previous_high_pages);

	return ret;
}

int lowmem_minfree_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int i, ret;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!ret) {
		lowmem_minfree_size = get_parameters_count(buffer, length);
		for (i = lowmem_minfree_size; i < ARRAYSIZE; i++)
			lowmem_minfree[i] = 0;
	}

	lowmem_print(1, "lowmem minfree param changed to: ");
	for (i = 0; i < lowmem_minfree_size; i++) {
		lowmem_print_cont(1, " %zu,", lowmem_minfree[i]);
	}
	lowmem_print_cont(1, "\n");

	mutex_unlock(&lowmem_lock);

	return ret;
}

int lowmem_adj_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int i, ret;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!ret) {
		lowmem_adj_size = get_parameters_count(buffer, length);
		for (i = lowmem_adj_size; i < ARRAYSIZE; i++)
			lowmem_adj[i] = 0;
	}
	mutex_unlock(&lowmem_lock);

	return ret;
}

int lowmem_minfree_ratio_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int i, ret;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!ret) {
		lowmem_minfree_ratio_size = get_parameters_count(buffer,
								 length);
		for (i = lowmem_minfree_ratio_size; i < ARRAYSIZE; i++)
			lowmem_minfree_ratio[i] = 0;
	}
	mutex_unlock(&lowmem_lock);
	if ((lowmem_mode == 2) && !ret)
		calculate_lowmemkiller_params(previous_min_pages,
					      previous_high_pages);

	return ret;
}

int lowmem_enabled_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int ret;
	uint32_t old_enabled = lowmem_enabled;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!ret)
		old_enabled -= lowmem_enabled;
	mutex_unlock(&lowmem_lock);
	if (!ret)
		switch (old_enabled) {
		case 1: /* lowmem_enabled changed from 0 to 1 */
			unregister_shrinker(&lowmem_shrinker);
			break;
		case -1: /* lowmem_enabled changed from 1 to 0 */
			register_shrinker(&lowmem_shrinker);
			break;
		}

	return ret;
}

int lowmem_adj_ratio_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int i, ret;

	if (!write)
		return proc_dointvec_minmax(table, write, buffer, length, ppos);

	mutex_lock(&lowmem_lock);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!ret) {
		lowmem_adj_ratio_size = get_parameters_count(buffer, length);
		for (i = lowmem_adj_ratio_size; i < ARRAYSIZE; i++)
			lowmem_adj_ratio[i] = 0;
	}
	mutex_unlock(&lowmem_lock);
	if ((lowmem_mode == 2) && !ret)
		calculate_lowmemkiller_params(previous_min_pages,
					      previous_high_pages);

	return ret;
}

static struct task_struct *get_task(int p)
{
	struct task_struct *task;
	struct pid *pid;
	int ret = 0;

	pid = find_get_pid(p);

	if (!pid) {
		lowmem_print(1, "The pid (%d) is unknown!\n", p);
		ret = -ESRCH;
		goto out;
	}

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task) {
		lowmem_print(1, "There is no task with pid = %d!\n", p);
		ret = -ESRCH;
		put_pid(pid);
		goto out;
	}

	put_pid(pid);

	lowmem_print(3, "get_task (pid = %d)\n", p);
	return task;
out:
	return (struct task_struct *)ret;
}

static void put_task(struct task_struct *task)
{
	lowmem_print(3, "put_task (pid = %d)\n", task->pid);
	put_task_struct(task);
}

int map_user_val(int val)
{
	int map_val;
	/*
	 * (Temoraly) Map userdefined prio to LMK adj values:
	 *      6 is mapped to oom_adj = 12 (passive)
	 *      5 is mapped to oom_adj = 6 (active)
	 */
	switch (val) {
	case (6):
		map_val = LMK_BACKGROUND_ADJ;
		break;
	case (5):
		map_val = LMK_FOREGROUND_ADJ;
		break;
	case (OOM_DISABLE):
		map_val = val;
		break;
	default:
		map_val = val;
		lowmem_print(1, "no rule to map user defined value (%d) "
			     "to oom_adj\n", val);
		break;
	}
	return map_val;
}

static long lmk_ioctl(struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	struct task_struct *task;
	struct lmk_ioctl new;
	int  ret = 0;

	switch (cmd) {
	case (0x00):
	case (0x10):
	case (0x20):
		lowmem_print(1, "The command (0x%x) is depricated!\n", cmd);
		ret = -EINVAL;
		break;
	case (SET_KILL_PRIO_IOCTL):
		if (copy_from_user(&new, (void __user *)arg, sizeof(new))) {
			lowmem_print(1, "unable to get lmk_ioctl params\n");
			ret = -EFAULT;
			break;
		}

		lowmem_print(4, "The oom_adj = %d, pid = %d\n", new.val,
			     new.pid);

		task = get_task(new.pid);

		ret = PTR_RET(task);

		if (ret) {
			lowmem_print(1, "The pid (%d) is not valid!\n",
				     new.pid);
			break;
		}

		new.val = map_user_val(new.val);
		if (new.val == oom_score_to_adj(task->signal->oom_score_adj)) {
			lowmem_print(4, "The oom_adj for pid (%d) is not "
				     "changed!\n", new.pid);
			goto PUT_TASK;
		}

		if ((new.val < OOM_ADJUST_MIN || new.val > OOM_ADJUST_MAX) &&
		    new.val != OOM_DISABLE) {
			ret = -EINVAL;
			lowmem_print(1, "The oom_adj (%d) is not valid!\n",
				     new.val);
			goto PUT_TASK;
		}

		ret = 0;
		task->signal->oom_score_adj = oom_adj_to_score(new.val);

#ifdef CONFIG_LMK_PRELOAD_APP
		if (new.val == LMK_BACKGROUND_ADJ) {
			/* The task has become a background one */
			task->signal->lmk_preload_prio = new.preload_prio;
			task->signal->lmk_background_time = jiffies;
		} else {
			task->signal->lmk_preload_prio = 0;
			task->signal->lmk_background_time = 0;
		}

		lowmem_print(1, "The oom_adj = %d preload_prio = %u "
			     "background_time = %lu ret = %d pid = %d\n",
			     oom_score_to_adj(task->signal->oom_score_adj),
			     lmk_preload_prio(task), lmk_background_time(task),
			     ret, new.pid);
#else
		lowmem_print(1, "The oom_adj = %d ret = %d pid = %d\n",
			     oom_score_to_adj(task->signal->oom_score_adj), ret,
			     new.pid);
#endif

PUT_TASK:
		put_task(task);
		break;
	case (REG_MANAGER_IOCTL):
	default:
		lowmem_print(1, "The command (0x%x) is unknown!\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct file_operations lmk_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = lmk_ioctl,
	.compat_ioctl = lmk_ioctl,
};

static struct miscdevice lmk_misc = {
	.minor = LMK_MINOR,
	.name = "lmk",
	.fops = &lmk_fops,
};

static unsigned long lowmem_deathpending_timeout;

#define K(x) ((x) << (PAGE_SHIFT - 10))
extern long buff_for_perf_kb;
static int lowmem_shrink(struct shrinker *s, struct shrink_control *sc)
{
	struct task_struct *tsk;
#ifdef CONFIG_LMK_PRELOAD_APP
	struct task_struct *tsk_preloaded = NULL;
	struct task_struct *tsk_background = NULL;
#endif
	struct task_struct *selected = NULL;
	int rem = 0;
	int tasksize;
	int i;
	int min_adj = OOM_ADJUST_MAX + 1;
	int selected_tasksize = 0;
	int selected_oom_adj;
	int array_size = ARRAYSIZE;
	int other_free, other_file;

#if CONFIG_OOM_RESCUER
	if (sc->gfp_mask & __GFP_NORESCUE)
		return -1;

#endif
#if defined (CONFIG_KNBD_SUPPORT)
	if (raw_notifier_call_chain(&lmk_chain, LMK_NOTIFY_SHRINK, NULL)
		!= NOTIFY_DONE)
		return -1;
#endif

	mutex_lock(&lowmem_lock);

	other_free = global_page_state(NR_FREE_PAGES) +
		     global_page_state(NR_FILE_PAGES) -
		     global_page_state(NR_SHMEM)-(buff_for_perf_kb/4);
	other_file = global_page_state(NR_FILE_PAGES) -
		     global_page_state(NR_SHMEM);

#ifdef CONFIG_CMA_APP_ALLOC
	/* CMA pages must be treated as unavailable */
	other_free -= global_page_state(NR_FREE_CMA_PAGES);
#endif

	/* Avoid negative values */
	if (other_free < 0)
		other_free = 0;

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;
	if (lowmem_minfree_size < array_size)
		array_size = lowmem_minfree_size;
	for (i = 0; i < array_size; i++) {
		if (other_free < lowmem_minfree[i] &&
		    other_file < lowmem_minfree[i]) {
			min_adj = lowmem_adj[i];
			break;
		}
	}
	if (sc->nr_to_scan > 0)
		lowmem_print(3, "lowmem_shrink %lu, %x, ofree %d %d, ma %d\n",
			     sc->nr_to_scan, sc->gfp_mask, other_free,
			     other_file, min_adj);
	rem = global_page_state(NR_ACTIVE_ANON) +
		global_page_state(NR_ACTIVE_FILE) +
		global_page_state(NR_INACTIVE_ANON) +
		global_page_state(NR_INACTIVE_FILE);
	if (sc->nr_to_scan <= 0 || min_adj == OOM_ADJUST_MAX + 1) {
		lowmem_print(5, "LMK - lowmem_shrink %lu, %x, return %d\n",
			     sc->nr_to_scan, sc->gfp_mask, rem);
		goto out;
	}
	selected_oom_adj = min_adj;
	rcu_read_lock();
	for_each_process(tsk) {
		struct task_struct *p;
		int oom_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		if (is_global_init(tsk))
			continue;

		if (test_tsk_thread_flag(tsk, TIF_MEMDIE) &&
		    time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			rcu_read_unlock();
			rem = 0;
			goto out;
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		if (!p->signal) {
			task_unlock(p);
			continue;
		}

		oom_adj = oom_score_to_adj(p->signal->oom_score_adj);
		if ((oom_adj < min_adj) || (oom_adj == OOM_DISABLE)) {
			lowmem_print(3, "task %d (%s), adj %d has been skipped"
				     "\n", p->pid, p->comm, oom_adj);

			task_unlock(p);
			continue;
		}
		tasksize = get_mm_rss(p->mm);
		if (tasksize <= 0)
			goto next_task;

#ifdef CONFIG_LMK_PRELOAD_APP
		if (oom_adj == LMK_BACKGROUND_ADJ) {
			if (lmk_preload_prio(p) > 0) {
				/* found a preloaded background task */
				if (!tsk_preloaded)
					tsk_preloaded = p;

				/* select by highest preload_prio value */
				if (lmk_preload_prio(p) >
				    lmk_preload_prio(tsk_preloaded))
					tsk_preloaded = p;
			} else {
				/* found an ordinary background task */
				if (!tsk_background)
					tsk_background = p;

				/* select by lowest background_time value */
				if (time_before(lmk_background_time(p),
				    lmk_background_time(tsk_background)))
					tsk_background = p;
			}
		}

		/* Preloaded tasks are always selected first */
		if (tsk_preloaded) {
			if (selected != tsk_preloaded) {
				selected = tsk_preloaded;
				goto print_selected;
			} else {
				goto next_task;
			}
		}

		/* Background tasks are second candidates to be killed */
		if (tsk_background) {
			if (selected != tsk_background) {
				selected = tsk_background;
				goto print_selected;
			} else {
				goto next_task;
			}
		}
#endif

		/* All other task are selected by oom_adj and memory usage */
		if (selected) {
			if (oom_adj < selected_oom_adj)
				goto next_task;
			if (oom_adj == selected_oom_adj &&
				tasksize <= selected_tasksize)
				goto next_task;
		}
		selected = p;

#ifdef CONFIG_LMK_PRELOAD_APP
print_selected:
#endif
		selected_tasksize = tasksize;
		selected_oom_adj = oom_adj;
#ifdef CONFIG_LMK_PRELOAD_APP
		lowmem_print(2, "select %d (%s), adj %d, preload_prio = %u, "
			     "background_time = %lu, size %d, to kill\n",
			     p->pid, p->comm, oom_adj, lmk_preload_prio(p),
			     lmk_background_time(p), tasksize);
#else
		lowmem_print(2, "select %d (%s), adj %d, size %d, to kill\n",
			     p->pid, p->comm, oom_adj, tasksize);
#endif
next_task:
		task_unlock(p);
	}
#ifdef CONFIG_LMK_PRELOAD_APP
	/* If we found both preloaded task and ordinary background task,
	   select the one with lowest background_time */
	if (tsk_preloaded && tsk_background &&
			time_before(lmk_background_time(tsk_background),
				    lmk_background_time(tsk_preloaded))) {
		selected = tsk_background;
		task_lock(selected);
		lowmem_print(2, "select %d (%s), adj %d, preload_prio = %u, "
			"background_time = %lu, size %lu, to kill\n",
			selected->pid, selected->comm,
			oom_score_to_adj(selected->signal->oom_score_adj),
			lmk_preload_prio(selected),
			lmk_background_time(selected),
			get_mm_rss(selected->mm));
		task_unlock(selected);
	}
#endif
	if (selected) {
#ifdef CONFIG_LMK_PRELOAD_APP
		lowmem_print(1, "send sigkill to %d (%s), adj %d, "
			"preload_prio = %u, background_time = %lu, size %d, "
			"vd_memfree %lu, ofree %d, of %d\n",
			selected->pid, selected->comm,
			selected_oom_adj, lmk_preload_prio(selected),
			lmk_background_time(selected), selected_tasksize,
			K(global_page_state(NR_FREE_PAGES) +
			global_page_state(NR_FILE_PAGES) -
			global_page_state(NR_SHMEM)) - buff_for_perf_kb,
			K(other_free), K(other_file));
#else
		lowmem_print(1, "send sigkill to %d (%s), adj %d, size %d, "
			     "vd_memfree %lu, ofree %d, of %d\n",
			     selected->pid, selected->comm,
			     selected_oom_adj, selected_tasksize,
			     K(global_page_state(NR_FREE_PAGES) +
			     global_page_state(NR_FILE_PAGES) -
			     global_page_state(NR_SHMEM)) - buff_for_perf_kb,
			     K(other_free), K(other_file));
#endif

		lowmem_deathpending_timeout = jiffies + HZ;

		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, selected, true);

		set_tsk_thread_flag(selected, TIF_MEMDIE);
		rem -= selected_tasksize;

	}
	lowmem_print(4, "lowmem_shrink %lu, %x, return %d\n",
		     sc->nr_to_scan, sc->gfp_mask, rem);
	rcu_read_unlock();
out:
	mutex_unlock(&lowmem_lock);
	return rem;
}

static int zero;
static int __maybe_unused one = 1;
static int __maybe_unused two = 2;
static int __maybe_unused five = 5;
static int max = INT_MAX;
static int min_adj = OOM_ADJUST_MIN;
static int max_adj = OOM_ADJUST_MAX;

static ctl_table lmk_table[] = {
	{
		.procname	= "cost",
		.data		= &lowmem_shrinker.seeks,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &max
	},
	{
		.procname	= "debug_level",
		.data		= &lowmem_debug_level,
		.maxlen		= sizeof(uint),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &five
	},
	{
		.procname	= "enabled",
		.data		= &lowmem_enabled,
		.maxlen		= sizeof(uint),
		.mode		= 0644,
		.proc_handler	= lowmem_enabled_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &one
	},
	{
		.procname	= "mode",
		.data		= &lowmem_mode,
		.maxlen		= sizeof(uint),
		.mode		= 0644,
		.proc_handler	= lowmem_mode_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &two
	},
	{
		.procname	= "minfree",
		.data		= &lowmem_minfree,
		.maxlen		= sizeof(lowmem_minfree),
		.mode		= 0644,
		.proc_handler	= lowmem_minfree_sysctl_handler,
		.extra1		= &one,
		.extra2		= &max
	},
	{
		.procname	= "adj",
		.data		= &lowmem_adj,
		.maxlen		= sizeof(lowmem_adj),
		.mode		= 0644,
		.proc_handler	= lowmem_adj_sysctl_handler,
		.extra1		= &min_adj,
		.extra2		= &max_adj
	},
		{
		.procname	= "minfree_ratio",
		.data		= &lowmem_minfree_ratio,
		.maxlen		= sizeof(lowmem_minfree_ratio),
		.mode		= 0644,
		.proc_handler	= lowmem_minfree_ratio_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &max
	},
	{
		.procname	= "adj_ratio",
		.data		= &lowmem_adj_ratio,
		.maxlen		= sizeof(lowmem_adj_ratio),
		.mode		= 0644,
		.proc_handler	= lowmem_adj_ratio_sysctl_handler,
		.extra1		= &min_adj,
		.extra2		= &max_adj
	},
	{0}
};

static ctl_table lmk_vm_table[] = {
	{
		.procname	= "lmk",
		.mode		= 0555,
		.child		= lmk_table
	},
	{0}
};

static ctl_table lmk_root_table[] = {
	{
		.procname	= "vm",
		.mode		= 0555,
		.child		= lmk_vm_table
	},
	{0}
};

static struct ctl_table_header *lmk_table_header;

static int __init lowmem_init(void)
{
	int ret;

	lowmem_mode = 0;
	lmk_table_header = register_sysctl_table(lmk_root_table);
	if (!lmk_table_header)
		return -ENOMEM;
	mutex_lock(&lowmem_lock);
	if (lowmem_enabled)
		register_shrinker(&lowmem_shrinker);
	mutex_unlock(&lowmem_lock);

	ret = misc_register(&lmk_misc);
	if (unlikely(ret)) {
		pr_err("Failed to register LMK misc device!\n");
		return ret;
	}

	return 0;
}

static void __exit lowmem_exit(void)
{
	int ret;

	mutex_lock(&lowmem_lock);
	if (lowmem_enabled)
		unregister_shrinker(&lowmem_shrinker);
	mutex_unlock(&lowmem_lock);
	unregister_sysctl_table(lmk_table_header);

	ret = misc_deregister(&lmk_misc);
	if (unlikely(ret))
		pr_err("Failed to unregister LMK misc device!\n");
}

module_param_named(cost, lowmem_shrinker.seeks, int, S_IRUGO | S_IWUSR);
module_param_array_named(adj, lowmem_adj, int, &lowmem_adj_size,
			 S_IRUGO | S_IWUSR);
module_param_array_named(minfree, lowmem_minfree, uint, &lowmem_minfree_size,
			 S_IRUGO | S_IWUSR);
module_param_named(debug_level, lowmem_debug_level, uint, S_IRUGO | S_IWUSR);

module_init(lowmem_init);
module_exit(lowmem_exit);

MODULE_LICENSE("GPL");

