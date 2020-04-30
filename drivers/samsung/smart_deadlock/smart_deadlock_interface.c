
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

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
extern ssize_t write_dlog_from_kernel(void *buf, int buf_size);
#define SMART_DEADLOCK_BT_BUG_SIZE 300

void write_dbus_info(const char *fmt, ...)
{
	char dlog_buf[SMART_DEADLOCK_BT_BUG_SIZE] = "ESMART_DEADLOCK";
	int  prefix_size = strlen("ESMART_DEADLOCK") + 1;
	char *msg_buf = dlog_buf + prefix_size;
	va_list arg;
	dlog_buf[prefix_size - 1] = 0;
	dlog_buf[0] = 6;
	va_start(arg, fmt);
	vsnprintf(msg_buf, SMART_DEADLOCK_BT_BUG_SIZE - prefix_size, fmt, arg);
	write_dlog_from_kernel(dlog_buf, prefix_size + strlen(msg_buf) + 1);
	va_end(arg);
}
EXPORT_SYMBOL(write_dbus_info);

struct smart_deadlock_function_point smart_deadlock_ptr = {NULL, };
EXPORT_SYMBOL(smart_deadlock_ptr);
#endif
bool smdl_is_suspend;
EXPORT_SYMBOL(smdl_is_suspend);

bool smdl_is_oom;
EXPORT_SYMBOL(smdl_is_oom);

bool smdl_is_lmf;
EXPORT_SYMBOL(smdl_is_lmf);

bool smdl_is_crash;
EXPORT_SYMBOL(smdl_is_crash);

unsigned long int smdl_suspend_resume_time;
EXPORT_SYMBOL(smdl_suspend_resume_time);

unsigned long int smdl_crash_time;
EXPORT_SYMBOL(smdl_crash_time);

unsigned long int smdl_lmf_time;
EXPORT_SYMBOL(smdl_lmf_time);

void hook_smart_deadlock_put_task(struct task_struct * released_tsk)
{
	struct smart_deadlock_work_tsk *w_tsk = released_tsk->sm_tsk.tsk;
	if( w_tsk && w_tsk->service )
		SMART_DEADLOCK_SET_STATUS(w_tsk->service, STATUS_SMART_DEADLOCK_NEED_TO_FREE);
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
	case SMART_DEADLOCK_CRASH_START:
			smdl_is_crash = true;
			printk(KERN_INFO "[smart-deadlock][Crash occurred above! Now making coredump!]\n");
			smdl_crash_time = jiffies;
			break;
	case SMART_DEADLOCK_CRASH_STOP:
			smdl_is_crash = false;
			smdl_crash_time = jiffies - smdl_crash_time;
			printk(KERN_INFO "[smart-deadlock][Making coredump done! Now restart monitoring!][time:%ld]\n", smdl_crash_time);
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

	tsk = get_pid_task(peer_pid, PIDTYPE_PID);
	w_tsk = (struct smart_deadlock_work_tsk *)current->sm_tsk.tsk;

	if (tsk && w_tsk)
		w_tsk->service->peer_pid = tsk->pid;
	else if (tsk)
		put_task_struct(tsk);
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
	} else {
		printk(KERN_INFO"[smart-deadlock][%s:%d][flag(%x):state(%ld):pending(%d):sig(%p)]\n",
			__func__, __LINE__, tsk->flags, tsk->state, fatal_signal_pending(tsk), tsk->sighand);
	}
	read_unlock(&tasklist_lock);
}
EXPORT_SYMBOL(smart_deadlock_deliver_sig_to_task);


#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
int* get_all_task_pid(int *count)
{
	const int MAX_EXTRA_THREADS = 100;  /* allow for variation of 100 threads */
	const int MAX_THREADS = nr_threads + MAX_EXTRA_THREADS;
	int pid_count = 0;
	struct task_struct *g, *p;
	int *pid_arr =  kzalloc( sizeof(int) * MAX_THREADS, GFP_NOFS);
	if(pid_arr == NULL)
		return NULL;
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (p && p->mm && p->pid) {
			pid_arr[pid_count++] = p->pid;
			if (pid_count == MAX_THREADS)
				goto skip_loop;
		}
	} while_each_thread(g, p);  /* end for pid search */
skip_loop:
	read_unlock(&tasklist_lock);
	*count = pid_count;
	return pid_arr;
}
EXPORT_SYMBOL(get_all_task_pid);

void smart_deadlock_set_bt(char *buf, int size, int type)
{
	if(smart_deadlock_ptr.deadlock_set_bt)
		smart_deadlock_ptr.deadlock_set_bt(buf, size, type);
}
EXPORT_SYMBOL(smart_deadlock_set_bt);

int smart_deadlock_check_worker(void)
{
	if(smart_deadlock_ptr.deadlock_check_worker)
		return smart_deadlock_ptr.deadlock_check_worker();
	else
		return 0;
}
EXPORT_SYMBOL(smart_deadlock_check_worker);


#define SMART_DEADLOCK_MAX_FULL_BT_PID 100

int* smart_deadlock_get_full_bactrace_pid(int main_pid, int *count)
{
	struct task_struct *tsk = NULL;
	int c_pid = 0;
	int pid[SMART_DEADLOCK_MAX_FULL_BT_PID];
	int *r_pid;
	tsk = get_task_struct_by_pid(main_pid);
	if (!IS_ERR(tsk)) {
		struct task_struct *t;
		rcu_read_lock();
		for_each_thread(tsk, t) {
			get_task_struct(t);
			if( c_pid >= SMART_DEADLOCK_MAX_FULL_BT_PID ) {
				put_task_struct(t);
				break;
			}
			pid[c_pid++] = t->pid;
			put_task_struct(t);
		}
		t = NULL;
		rcu_read_unlock();
		put_task_struct(tsk);
	}

	if( c_pid == 0 )
		return NULL;
	r_pid = kzalloc( sizeof(int) * c_pid, GFP_NOFS);
	if(r_pid) {
		int i = 0;
		for( i = 0 ; i < c_pid ; i++ )
			r_pid[i] = pid[i];
	}
	*count = c_pid;
	return r_pid;

}
EXPORT_SYMBOL(smart_deadlock_get_full_bactrace_pid);
#endif

/* the core smart deadlock module will be working by loadable module */
