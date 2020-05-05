
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

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
#include <../../../kernel/kdebugd/elf/kdbg_elf_sym_api.h>
#include <../../../kernel/kdebugd/kdbg-trace.h>
struct smart_deadlock_function_point smart_deadlock_ptr = {NULL, };
EXPORT_SYMBOL(smart_deadlock_ptr);
#endif
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

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
enum SMDL_LOG_MSG_TYPE {
	SMDL_LOG_MAIN_TITLE,
	SMDL_LOG_MAIN_SVC_INFO,
	SMDL_LOG_DBUS_INFO,
	SMDL_LOG_ALL_TH_INFO,
};


#define KUBT_TRACE_COUNT	10

#define KDEBUGD_PRINT_ELF   "#%d  0x%08lx in %s () from %s\n"
#define KDEBUGD_PRINT_DWARF "#%d  0x%08lx in %s () at %s:%d\n"
#define KDUBGD_ONLY_ADDR	"#%d  0x%08lx in ??\n"
#define KDEBUGD_MAIN_TITLE	"Pid: %d, Tid: %d, comm: %s[%d] exec_start[%u.%09u]\n"

extern void show_user_backtrace_pid(pid_t pid, int use_ptrace, int load_elf, struct kdbg_bt_buffer *trace);

#define KERNEL_BT_BUF_SIZE	300
void smart_deadlock_show_user_backtrace_pid(int pid, char **buf, int *count)
{
	struct kdbg_bt_buffer kubt_buffer;
	int tmp_count = *count;
	char *kernel_bt_buf = NULL;

	kubt_buffer.max_entries = KUBT_TRACE_COUNT;
	kubt_buffer.nr_entries = 0;
	kubt_buffer.symbol = (struct bt_frame *)kzalloc(sizeof(struct bt_frame) * KUBT_TRACE_COUNT, GFP_NOFS);
	if (kubt_buffer.symbol) {
		int i = 0;
		int call_depth = 0;
		__s32 sec = 0;
		__s32 nsec = 0;
		u64 ts;
		__kernel_size_t alloc_size = 0;
		kernel_bt_buf = kzalloc(KERNEL_BT_BUF_SIZE, GFP_NOFS);
		if (kernel_bt_buf == NULL) {
			kfree(kubt_buffer.symbol);
			return;
		}
		show_user_backtrace_pid((pid_t)pid, 0, 0, &kubt_buffer);

		if (kubt_buffer.nr_entries > 0) {
			ts = kubt_buffer.exec_start;
			nsec = do_div(ts, NSEC_PER_SEC);
			sec = ts;

			snprintf(kernel_bt_buf, KERNEL_BT_BUF_SIZE, KDEBUGD_MAIN_TITLE,
			kubt_buffer.pid, kubt_buffer.tid, kubt_buffer.comm,
			kubt_buffer.cpu_number, sec, nsec);

			alloc_size = strlen(kernel_bt_buf) + 1;
			buf[tmp_count] = kzalloc(alloc_size, GFP_NOFS);
			if (buf[tmp_count]) {
				snprintf(buf[tmp_count], alloc_size, "%s", kernel_bt_buf);
				tmp_count++;
			}
		}

		for (i = 0 ; i < kubt_buffer.nr_entries ; i++) {
			switch (kubt_buffer.symbol[i].type) {
			case KDEBUGD_BACKTRACE_ONLY_ADDR:
				snprintf(kernel_bt_buf, KERNEL_BT_BUF_SIZE, KDUBGD_ONLY_ADDR, call_depth,
				kubt_buffer.symbol[i].addr);
				call_depth++;
				break;
#ifdef CONFIG_ELF_MODULE
			case KDEBUGD_BACKTRACE_ELF:
				snprintf(kernel_bt_buf, KERNEL_BT_BUF_SIZE, KDEBUGD_PRINT_ELF,
				call_depth, kubt_buffer.symbol[i].addr,
				kubt_buffer.symbol[i].sym_name,
				kubt_buffer.symbol[i].lib_name);
				call_depth++;
				break;
#ifdef CONFIG_DWARF_MODULE
			case KDEBUGD_BACKTRACE_DWARF:
				snprintf(kernel_bt_buf, KERNEL_BT_BUF_SIZE, KDEBUGD_PRINT_DWARF,
				call_depth,	kubt_buffer.symbol[i].addr,
				kubt_buffer.symbol[i].sym_name,
				kubt_buffer.symbol[i].df_file_name,
				kubt_buffer.symbol[i].df_line_no);
				call_depth++;
				break;
#endif
#endif
			default:
				snprintf(kernel_bt_buf, KERNEL_BT_BUF_SIZE, "Error kubt Type");
			break;
			}
			alloc_size = strlen(kernel_bt_buf) + 1;
			buf[tmp_count] = kzalloc(alloc_size, GFP_NOFS);
			if (buf[tmp_count]) {
				snprintf(buf[tmp_count], alloc_size, "%s", kernel_bt_buf);
				tmp_count++;
			} else {
				printk(KERN_INFO "[smart-deadlock][kernel_bt_buf Memory Allocation failed]\n");
				break;
			}
		}

		kfree(kubt_buffer.symbol);
		kfree(kernel_bt_buf);
	}

	*count = tmp_count;
}
EXPORT_SYMBOL(smart_deadlock_show_user_backtrace_pid);

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
		if (p && p->pid) {
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


#define SMART_DEADLOCK_BT_BUG_SIZE 300

void write_dbus_info(const char *fmt, ...)
{
	char *smdl_msg_buf = NULL;
	va_list arg;

	if (smart_deadlock_ptr.deadlock_logger_write == NULL)
		return ;

	smdl_msg_buf = kzalloc(SMART_DEADLOCK_BT_BUG_SIZE, GFP_NOFS);
	if (smdl_msg_buf) {
		va_start(arg, fmt);
		vsnprintf(smdl_msg_buf, SMART_DEADLOCK_BT_BUG_SIZE, fmt, arg);
		smart_deadlock_ptr.deadlock_logger_write(smdl_msg_buf, strlen(smdl_msg_buf) + 1, SMDL_LOG_DBUS_INFO);
		va_end(arg);
		kfree(smdl_msg_buf);
	}
}
EXPORT_SYMBOL(write_dbus_info);


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

void smart_deadlock_init_profile(int type, struct smart_deadlock_profile *profile, void *ptr)
{
	INIT_LIST_HEAD(&profile->list);
	profile->value = ptr;
	profile->type = type;
}

void smart_deadlock_enter_profile(struct smart_deadlock_profile *profile)
{
	spin_lock(&current->sm_tsk.lock);
	if (current->sm_tsk.profile)
		list_add_tail(&profile->list, &current->sm_tsk.profile->list);
	else
		current->sm_tsk.profile = profile;
	spin_unlock(&current->sm_tsk.lock);

}

void smart_deadlock_leave_profile(struct smart_deadlock_profile *profile)
{
	if (profile->type == SMART_DEADLOCK_PROFILE_INVALID)
		return;

	BUG_ON(current->sm_tsk.profile == NULL);
	BUG_ON(current->sm_tsk.profile->list.prev != &profile->list);

	spin_lock(&current->sm_tsk.lock);
	if (current->sm_tsk.profile == profile)
		current->sm_tsk.profile = NULL;
	else
		list_del_init(&profile->list);
	spin_unlock(&current->sm_tsk.lock);
}

struct files_struct *smart_deadlock_get_files_struct(struct task_struct *task)
{
	return get_files_struct(task);
}
EXPORT_SYMBOL(smart_deadlock_get_files_struct);

void smart_deadlock_put_files_struct(struct files_struct *files)
{
	return put_files_struct(files);
}
EXPORT_SYMBOL(smart_deadlock_put_files_struct);

#endif

/* the core smart deadlock module will be working by loadable module */
