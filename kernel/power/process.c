/*
 * drivers/power/process.c - Functions for starting/stopping processes on 
 *                           suspend transitions.
 *
 * Originally from swsusp.
 */


#undef DEBUG

#include <linux/interrupt.h>
#include <linux/oom.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <trace/events/power.h>
#include <linux/cpuset.h>
#ifdef CONFIG_PROC_VD_SUSPEND_POLICY
#include <linux/vd_suspend_policy.h>
#endif

/*
 * Timeout for stopping processes
 */
unsigned int __read_mostly freeze_timeout_msecs = 20 * MSEC_PER_SEC;

#ifdef CONFIG_ALWAYS_INSTANT_ON
extern int is_always_instantOn(void);
extern void machine_restart_standby(char *cmd);
#endif

#ifdef CONFIG_SLP_LOWMEM_NOTIFY
extern bool dump_tasks_once_lv2;
#endif

static int try_to_freeze_tasks(bool user_only)
{
	struct task_struct *g, *p;
	unsigned long end_time;
	unsigned int todo;
	bool wq_busy = false;
	struct timeval start, end;
	u64 elapsed_msecs64;
	unsigned int elapsed_msecs;
	bool wakeup = false;
	int sleep_usecs = USEC_PER_MSEC;
#ifdef CONFIG_INFO_TASK_FREEZE_FAIL
#define TASKS 8
	struct task_struct *failed[TASKS];
	int i, failed_cnt;
#endif
#ifdef CONFIG_PROC_VD_SUSPEND_POLICY
	struct vd_policy_list *entry = get_freeze_seq_first_entry();
#endif
	do_gettimeofday(&start);

	end_time = jiffies + msecs_to_jiffies(freeze_timeout_msecs);

	if (!user_only)
		freeze_workqueues_begin();

	while (true) {
		todo = 0;
		read_lock(&tasklist_lock);
		for_each_process_thread(g, p) {
#ifdef CONFIG_PROC_VD_SUSPEND_POLICY
			if (user_only && p->suspend_last &&
				vd_suspend_policy_check(p->comm, entry))
				continue;
#endif
			if (p == current || !freeze_task(p))
				continue;

			if (!freezer_should_skip(p))
#ifndef CONFIG_INFO_TASK_FREEZE_FAIL
				todo++;
#else
			{
				if(todo < TASKS)
					failed[todo] = p;
				todo++;
			}
#endif
		}
		read_unlock(&tasklist_lock);

		if (!user_only) {
#ifdef CONFIG_INFO_TASK_FREEZE_FAIL
			wq_busy = freeze_workqueues_busy(false);
#else
			wq_busy = freeze_workqueues_busy();
#endif
			todo += wq_busy;
		}

#ifdef CONFIG_PROC_VD_SUSPEND_POLICY
		if (user_only && !todo)
			todo = vd_suspend_policy_get_next(&entry);
#endif
		if (!todo || time_after(jiffies, end_time))
			break;

		if (pm_wakeup_pending()) {
			wakeup = true;
			break;
		}

		/*
		 * We need to retry, but first give the freezing tasks some
		 * time to enter the refrigerator.  Start with an initial
		 * 1 ms sleep followed by exponential backoff until 8 ms.
		 */
		usleep_range(sleep_usecs / 2, sleep_usecs);
		if (sleep_usecs < 8 * USEC_PER_MSEC)
			sleep_usecs *= 2;
	}

	do_gettimeofday(&end);
	elapsed_msecs64 = timeval_to_ns(&end) - timeval_to_ns(&start);
	do_div(elapsed_msecs64, NSEC_PER_MSEC);
	elapsed_msecs = elapsed_msecs64;

	if (todo) {
		pr_cont("\n");
		pr_err("Freezing of tasks %s after %d.%03d seconds "
		       "(%d tasks refusing to freeze, wq_busy=%d):\n",
		       wakeup ? "aborted" : "failed",
		       elapsed_msecs / 1000, elapsed_msecs % 1000,
		       todo - wq_busy, wq_busy);

#ifdef CONFIG_INFO_TASK_FREEZE_FAIL
		failed_cnt = todo - wq_busy;
		if(failed_cnt > TASKS)
			failed_cnt = TASKS;

		if(failed_cnt > 0)
		{
			for(i = 0 ; i < failed_cnt ; i++) {
				if(failed[i])
					pr_err("[%s->%s(%d)] Freezing is failed\n", failed[i]->parent?failed[i]->parent->comm:" ", failed[i]->comm, failed[i]->pid);
			}
		}
		if(wq_busy)
			freeze_workqueues_busy(true);
#endif
		if (!wakeup) {
			read_lock(&tasklist_lock);
			for_each_process_thread(g, p) {
				if (p != current && !freezer_should_skip(p)
				    && freezing(p) && !frozen(p))
					sched_show_task(p);
			}
			read_unlock(&tasklist_lock);
		}
	} else {
		pr_cont("(elapsed %d.%03d seconds) ", elapsed_msecs / 1000,
			elapsed_msecs % 1000);
	}

#ifdef CONFIG_PROC_VD_SUSPEND_POLICY
	if (user_only)
		vd_suspend_policy_reset_value();
#endif
	return todo ? -EBUSY : 0;
}

/**
 * freeze_processes - Signal user space processes to enter the refrigerator.
 * The current thread will not be frozen.  The same process that calls
 * freeze_processes must later call thaw_processes.
 *
 * On success, returns 0.  On failure, -errno and system is fully thawed.
 */
int freeze_processes(void)
{
	int error;

	error = __usermodehelper_disable(UMH_FREEZING);
	if (error)
		return error;

	/* Make sure this task doesn't get frozen */
	current->flags |= PF_SUSPEND_TASK;

	if (!pm_freezing)
		atomic_inc(&system_freezing_cnt);

	pm_wakeup_clear();
	pr_info("Freezing user space processes ... ");
	pm_freezing = true;
	error = try_to_freeze_tasks(true);
	if (!error) {
		__usermodehelper_set_disable_depth(UMH_DISABLED);
		pr_cont("done.");
#ifdef CONFIG_SLP_LOWMEM_NOTIFY
		dump_tasks_once_lv2 = false;
#endif
	}
	pr_cont("\n");
	BUG_ON(in_atomic());

	/*
	 * Now that the whole userspace is frozen we need to disbale
	 * the OOM killer to disallow any further interference with
	 * killable tasks.
	 */
	if (!error && !oom_killer_disable())
		error = -EBUSY;

	if (error)
	{
#ifdef CONFIG_ALWAYS_INSTANT_ON
		printk(KERN_ERR "PM:fail to suspend. [%s] Failed to Freeze user space "
						"processes - cold power off\n", __func__);
		if(is_always_instantOn())
			machine_restart_standby("Suspend fail - reboot\n");
		else
			pm_power_off();
#endif
		thaw_processes();
	}
	return error;
}

/**
 * freeze_kernel_threads - Make freezable kernel threads go to the refrigerator.
 *
 * On success, returns 0.  On failure, -errno and only the kernel threads are
 * thawed, so as to give a chance to the caller to do additional cleanups
 * (if any) before thawing the userspace tasks. So, it is the responsibility
 * of the caller to thaw the userspace tasks, when the time is right.
 */
int freeze_kernel_threads(void)
{
	int error;

	pr_info("Freezing remaining freezable tasks ... ");

	pm_nosig_freezing = true;
	error = try_to_freeze_tasks(false);
	if (!error)
		pr_cont("done.");

	pr_cont("\n");
	BUG_ON(in_atomic());

	if (error)
	{
#ifdef CONFIG_ALWAYS_INSTANT_ON
		printk(KERN_ERR "PM:fail to suspend. [%s] Failed to Freeze freezable "
						"tasks - cold power off\n", __func__);
		if(is_always_instantOn())
			machine_restart_standby("Suspend fail - reboot\n");
		else
			pm_power_off();
#endif
		thaw_kernel_threads();
	}
	return error;
}

#ifdef CONFIG_POWER_SAVING_MODE

//it is only called by thaw_processes_prepare below
static void thaw_processes_finish(void)
{
        struct task_struct *g, *p;

	pwsv_freezing = false;

        printk("Restarting tasks ... ");

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		__thaw_task(p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

}
void thaw_processes_prepare(void)
{

	if(pwsv_freezing){
		thaw_processes_finish();
		return;
	}

        if (pm_freezing)
                atomic_dec(&system_freezing_cnt);
        pm_freezing = false;
        pm_nosig_freezing = false;

        oom_killer_enable();

        __usermodehelper_set_disable_depth(UMH_FREEZING);
        thaw_workqueues();

	if(pwsv_current_mode != PWSV_MODE_TV){
		pwsv_freezing = true;
	}else{
		thaw_processes_finish();
	}
	usermodehelper_enable();

	schedule();
}

//nobody uses this function
void thaw_processes(void)
{
	thaw_processes_prepare();
	printk("done.\n");
}
#else

void thaw_processes(void)
{
	struct task_struct *g, *p;
	struct task_struct *curr = current;

	trace_suspend_resume(TPS("thaw_processes"), 0, true);
	if (pm_freezing)
		atomic_dec(&system_freezing_cnt);
	pm_freezing = false;
	pm_nosig_freezing = false;

	oom_killer_enable();

	pr_info("Restarting tasks ... ");

	__usermodehelper_set_disable_depth(UMH_FREEZING);
	thaw_workqueues();

	cpuset_wait_for_hotplug();

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		/* No other threads should have PF_SUSPEND_TASK set */
		WARN_ON((p != curr) && (p->flags & PF_SUSPEND_TASK));
		__thaw_task(p);
	}
	read_unlock(&tasklist_lock);

	WARN_ON(!(curr->flags & PF_SUSPEND_TASK));
	curr->flags &= ~PF_SUSPEND_TASK;

	usermodehelper_enable();

	schedule();
	pr_cont("done.\n");
	trace_suspend_resume(TPS("thaw_processes"), 0, false);
}
#endif

void thaw_kernel_threads(void)
{
	struct task_struct *g, *p;

	pm_nosig_freezing = false;
	pr_info("Restarting kernel threads ... ");

	thaw_workqueues();

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		if (p->flags & (PF_KTHREAD | PF_WQ_WORKER))
			__thaw_task(p);
	}
	read_unlock(&tasklist_lock);

	schedule();
	pr_cont("done.\n");
}
