
#include <asm/cacheflush.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/uaccess.h>
#include <linux/cpu.h>
#include <linux/swap.h>

bool smdl_is_suspend;
EXPORT_SYMBOL(smdl_is_suspend);

bool smdl_is_oom;
EXPORT_SYMBOL(smdl_is_oom);

bool smdl_is_lmf;
EXPORT_SYMBOL(smdl_is_lmf);

unsigned long int smdl_suspend_resume_time;
EXPORT_SYMBOL(smdl_suspend_resume_time);

unsigned long int smdl_lmf_time;
EXPORT_SYMBOL(smdl_lmf_time);

void hook_smart_deadlock_put_task(struct task_struct * released_tsk)
{
	struct smart_deadlock_work_tsk *w_tsk = released_tsk->sm_tsk.tsk;
	if (w_tsk && w_tsk->service) {
		SMART_DEADLOCK_SET_STATUS(w_tsk->service, STATUS_SMART_DEADLOCK_NEED_TO_FREE);
		released_tsk->sm_tsk.tsk = NULL;
		kfree(w_tsk);
	}
}
EXPORT_SYMBOL(hook_smart_deadlock_put_task);

void hook_smart_deadlock_exception_case(int type)
{
	switch (type) {
	case SMART_DEADLOCK_LMF_START:
			smdl_is_lmf = true;
			printk(KERN_INFO "[smart-deadlock][LMF occur!]\n");
			smdl_lmf_time = jiffies;
			break;
	case SMART_DEADLOCK_LMF_STOP:
			smdl_is_lmf = false;
			printk(KERN_INFO "[smart-deadlock][LMF run success!]\n");
			smdl_lmf_time = jiffies - smdl_lmf_time;
			break;
	case SMART_DEADLOCK_OOM:
			if (!smdl_is_oom) {
				smdl_is_oom = true;
				printk(KERN_INFO "[smart-deadlock][OOM occur!]\n");
			}
			break;
	case SMART_DEADLOCK_SUSPEND:
			smdl_is_suspend = true;
			smdl_suspend_resume_time = jiffies;
			break;
	case SMART_DEADLOCK_RESUME:
			smdl_is_suspend = false;
			smdl_suspend_resume_time = jiffies - smdl_suspend_resume_time;
			break;
	default:
			printk(KERN_INFO "[smart-deadlock][%s:%d][type:%d][Invalid exception case!]\n", __func__, __LINE__, type);
			break;
	}
}
EXPORT_SYMBOL(hook_smart_deadlock_exception_case);

void hook_smart_deadlock_unix_wait_for_peer_enter(struct pid *peer_pid)
{
	struct task_struct *tsk = NULL;
	struct smart_deadlock_work_tsk *w_tsk = NULL;

	w_tsk = (struct smart_deadlock_work_tsk *)current->sm_tsk.tsk;

	if (w_tsk) {
		tsk = get_pid_task(peer_pid, PIDTYPE_PID);
		if (tsk) {
			w_tsk->service->peer_pid = tsk->pid;
			put_task_struct(tsk);
		}
	}
}
EXPORT_SYMBOL(hook_smart_deadlock_unix_wait_for_peer_enter);

void hook_smart_deadlock_unix_wait_for_peer_leave(void)
{
	struct smart_deadlock_work_tsk *w_tsk = NULL;
	w_tsk = (struct smart_deadlock_work_tsk *)current->sm_tsk.tsk;

	if (w_tsk)
		w_tsk->service->peer_pid = 0;
}
EXPORT_SYMBOL(hook_smart_deadlock_unix_wait_for_peer_leave);

struct task_struct *get_task_struct_by_pid(pid_t pid)
{
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if(task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task)
		return ERR_PTR(-ESRCH);

	return task;
}
EXPORT_SYMBOL(get_task_struct_by_pid);

void smart_deadlock_put_task_struct(struct task_struct *tsk)
{
	put_task_struct(tsk);
}
EXPORT_SYMBOL(smart_deadlock_put_task_struct);

void smart_deadlock_deliver_sig_to_task(int sig, struct task_struct *tsk)
{
	read_lock(&tasklist_lock);
	if ((!(tsk->flags & PF_EXITING) && !unlikely(fatal_signal_pending(tsk)))
			&& tsk->sighand) {
		force_sig(sig, tsk);
	}
	read_unlock(&tasklist_lock);
}
EXPORT_SYMBOL(smart_deadlock_deliver_sig_to_task);

void print_meminfo_module(struct smart_deadlock_meminfo *meminfo_data)
{
	struct sysinfo i;
#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
	long available;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	unsigned long pages_active, pages_inactive;
	struct zone *zone;
#endif

#define K(x) ((x) << (PAGE_SHIFT - 10))
	si_meminfo(&i);
	si_swapinfo(&i);

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
	pages_active = global_page_state(NR_LRU_BASE + LRU_ACTIVE_FILE);
	pages_inactive = global_page_state(NR_LRU_BASE + LRU_INACTIVE_FILE);
	for_each_zone(zone)
		wmark_low += zone->watermark[WMARK_LOW];

	/*
	 * Estimate the amount of memory available for userspace allocations,
	 * without causing swapping.
	 *
	 * Free memory cannot be taken below the low watermark, before the
	 * system starts swapping.
	 */
	available = i.freeram - wmark_low;

	/*
	 * Not all the page cache can be freed, otherwise the system will
	 * start swapping. Assume at least half of the page cache, or the
	 * low watermark worth of cache, needs to stay.
	 */
	pagecache = pages_active + pages_inactive;
	pagecache -= min(pagecache / 2, wmark_low);
	available += pagecache;

	/*
	 * Part of the reclaimable slab consists of items that are in use,
	 * and cannot be freed. Cap this estimate at the low watermark.
	 */
	available += global_page_state(NR_SLAB_RECLAIMABLE) -
		min(global_page_state(NR_SLAB_RECLAIMABLE) / 2, wmark_low);

	if (available < 0)
		available = 0;

	meminfo_data->memavailable = K(available);
#endif
	meminfo_data->memtotal = K(i.totalram);
	meminfo_data->memfree = K(i.freeram);
	meminfo_data->hightotal = K(i.totalhigh);
	meminfo_data->highfree = K(i.freehigh);
	meminfo_data->lowtotal = K(i.totalram-i.totalhigh);
	meminfo_data->lowfree = K(i.freeram-i.freehigh);
	meminfo_data->swaptotal = K(i.totalswap);
	meminfo_data->swapfree = K(i.freeswap);
}
EXPORT_SYMBOL(print_meminfo_module);

/* the core smart deadlock module will be working by loadable module */
