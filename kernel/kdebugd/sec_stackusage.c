
/*
 *  linux/kernel/sec_stackusage.c
 *
 *  Stack Usage Solution, stack usage related functions
 *
 *  Copyright (C) 2016  Samsung
 *
 *  2016-05-26  Created by Deepak Kumar Singh, SRI-Delhi
 *
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <asm/ptrace.h>
#include <linux/vmalloc.h>

#include <kdebugd.h>
#include "kdbg_util.h"
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#include "agent/agent_core.h"
#include "agent/agent_packet.h"
#include "agent/agent_error.h"
#include "agent/tvis_agent_cmds.h"
#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

int sec_stackusage_init_flag;
int sec_stackusage_run_state;
int sec_stackusage_status;

DEFINE_MUTEX(stackusage_lock);

bool sec_stackusage_init(void);

enum kdbg_print_menu {
	KDBG_PRINT_PROCESS = 0,
	KDBG_PRINT_FIRST_THREAD,
	KDBG_PRINT_LAST_THREAD
};

struct stack_thlist {
	pid_t tid;
	pid_t tgid;
};

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
struct sec_stackusage_struct {
	int tgid;
	int pid;
	int stack_usage;
	int stack_size;
};
#define STRUCT_SIZE sizeof(struct sec_stackusage_struct)

static void send_stack_size_buffer(char *stack_buffer,
		unsigned int packet_cnt, unsigned int timestamp)
{
	unsigned int bytes_written = 0;

	if (packet_cnt == 0)
		return;

	agent_write(KDBG_CMD_CM_STACK_SIZE_TRACER_START, &timestamp, sizeof(timestamp));
	bytes_written = packet_cnt * STRUCT_SIZE;
	agent_write(KDBG_CMD_CM_STACK_SIZE_TRACER_CONT, stack_buffer, bytes_written);
	agent_write(KDBG_CMD_CM_STACK_SIZE_TRACER_END, NULL, 0);
}

static void kdbg_get_stack_size(struct task_struct *tsk, char *stack_buffer,
		unsigned int *packet_count, unsigned int *bytes, unsigned int buf_size)
{
	struct mm_struct *mm = NULL;
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	struct sec_stackusage_struct stack_info;

	if (!tsk || !buf_size) {
		PRINT_KD("tsk = %p buf_size = %d\n", tsk, buf_size);
		return;
	}

	get_task_struct(tsk);

	mm = get_task_mm(tsk);

	if (mm) {
		stack_info.tgid = tsk->tgid;
		stack_info.pid = tsk->pid;

		regs = task_pt_regs(tsk);

		vma = find_vma(tsk->mm, tsk->user_ssp);
		if (vma != NULL) {
#if defined(CONFIG_ARM)
			stack_info.stack_usage =  vma->vm_end - regs->ARM_sp; /* MF?? */
#elif defined(CONFIG_MIPS)
			stack_info.stack_usage =  vma->vm_end - regs->regs[29];
#elif defined(CONFIG_ARM64)
			if (compat_user_mode(regs))
				stack_info.stack_usage =  vma->vm_end - regs->compat_sp;
			else
				stack_info.stack_usage =  vma->vm_end - regs->sp;
#else
#error "Architecture Not Supported!!!"
#endif
			stack_info.stack_size  =  vma->vm_end - vma->vm_start;
		}
		mmput(mm);
		put_task_struct(tsk);

		if (*bytes <= (buf_size - STRUCT_SIZE)) {
			memcpy(stack_buffer + *bytes, &stack_info, STRUCT_SIZE);
			*packet_count = *packet_count + 1;
			*bytes += STRUCT_SIZE;
		} else
			PRINT_KD("Err:No space in stack size buffer\n");
	}
}

void kdbg_agent_get_stack_info(void)
{
	struct task_struct *p, *g;
	static char *stack_buffer;
	unsigned int packet_cnt = 0;
	unsigned int byte_written = 0;
	unsigned int thread_count;
	unsigned int buf_size;
	unsigned int timestamp, i;

	thread_count = nr_threads;
	buf_size = thread_count * sizeof(struct sec_stackusage_struct);

	stack_buffer = vmalloc(buf_size);
	if (stack_buffer == NULL) {
		PRINT_KD("Err:Not enough memory for stack size buffer\n");
		return;
	}

	timestamp = (uint32_t)kdbg_get_uptime();

	rcu_read_lock();
	i = 0;
	do_each_thread(g, p) {
		if (i >= thread_count)
			break;
		kdbg_get_stack_size(p, stack_buffer, &packet_cnt, &byte_written, buf_size);
		/* Check for i==thread_count */
		i++;
	} while_each_thread(g, p);
	rcu_read_unlock();

	send_stack_size_buffer(stack_buffer, packet_cnt, timestamp);

	vfree(stack_buffer);
}
#endif

/* function to swap the tid and tgid of two threads*/
static void swap_threadlist_info(void *va, void *vb)
{
	struct stack_thlist *a = va, *b = vb;
	pid_t ttid = a->tid, ttgid = a->tgid;

	a->tid = b->tid;
	a->tgid = b->tgid;
	b->tid = ttid;
	b->tgid = ttgid;
}

static void show_stack_info(struct task_struct *p, enum kdbg_print_menu flag)
{
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	unsigned long stack_usage = 0;
	unsigned long stack_size = 0;
	struct mm_struct *mm = NULL;
	char buf[25];
	int len = 0;
	if (p == NULL)
		return;

	mm = get_task_mm(p);

	if (mm || p->state == TASK_DEAD || p->state == EXIT_DEAD ||
			p->state == EXIT_ZOMBIE) {
		switch (flag) {
		case KDBG_PRINT_PROCESS:
			len +=  snprintf(buf, sizeof(buf)-len, "%-22s", p->comm);
			break;
		case KDBG_PRINT_FIRST_THREAD:
			len += snprintf(buf, sizeof(buf)-len, " |--%-18s", p->comm);
			break;
		case KDBG_PRINT_LAST_THREAD:
			len += snprintf(buf, sizeof(buf)-len, " `--%-18s", p->comm);
			break;
		default:
			break;
		}
	}


	if (mm) {

		regs = task_pt_regs(p);
		vma = find_vma(p->mm, p->user_ssp);
		if (vma != NULL) {
#if defined(CONFIG_ARM)
			stack_usage =  vma->vm_end - regs->ARM_sp; /* MF?? */
#elif defined(CONFIG_MIPS)
			stack_usage =  vma->vm_end - regs->regs[29];
#elif defined(CONFIG_ARM64)
			if (compat_user_mode(regs))
				stack_usage =  vma->vm_end - regs->compat_sp;
			else
				stack_usage =  vma->vm_end - regs->sp;
#else
#error "Architecture Not Supported!!!"
#endif
			stack_size  =  vma->vm_end - vma->vm_start;
			PRINT_KD("%s  %6d     %10lu         %10lu\n",
					buf, p->pid, stack_usage >> 10,
					stack_size >> 10);
		}
		mmput(mm);
	} else {
		if (p->state == TASK_DEAD || p->state == EXIT_DEAD ||
				p->state == EXIT_ZOMBIE) {
			PRINT_KD("%s           [dead]\n", buf);
		}
	}
}

static void stackusage_show_header(void)
{
	PRINT_KD("Task Name                 Pid     Stack Usage[KB]  Stack Size[KB]\n");
	PRINT_KD("==============            ======  =============    ==============  \n");
}


int sec_stackusage_dump(void)
{

	int thread_count, i, j, k, flag;
	struct stack_thlist *th_list;
	struct task_struct *p = NULL;
	struct task_struct *g = NULL;
	struct task_struct *ts = NULL;

	/* No of threads in system */
	thread_count = nr_threads;

	if (sec_stackusage_init_flag) {

		/* check whether state is running or not.. */
		if (!sec_stackusage_run_state)
			return 0;

		/* Allocating a memory block to store pids and tgids for all threads */
		th_list = kmalloc(sizeof(struct stack_thlist) * (unsigned int)thread_count, GFP_KERNEL);
		/* Check nullity for th_list*/
		if (th_list == NULL) {
			PRINT_KD("unable to allocate memory !!\n");
			return 0;
		}

		rcu_read_lock();
		i = 0;
		do_each_thread(g, p) {
			if (i >= thread_count)
				break;
			th_list[i].tid = p->pid;
			th_list[i].tgid = p->tgid;
			/* Check for i==thread_count */
			i++;
		} while_each_thread(g, p);
		rcu_read_unlock();

		thread_count = i-1;
		if (kdbg_get_task_status()) {/*Process mode*/
			if (sec_stackusage_status) {
				stackusage_show_header();
				for (j = 0; j < thread_count - 1; j++) {
					flag = 1;
					for (k = j+1; k < thread_count; k++) {
						if (th_list[j].tgid > th_list[k].tgid) {
							swap_threadlist_info(&th_list[j], &th_list[k]);
							flag = 0;
						}
					}
					if (flag)
						break;
				}

				for (i = 0; i < thread_count; i++) {
					rcu_read_lock();
					ts = find_task_by_pid_ns(th_list[i].tid, &init_pid_ns);
					if (ts)
						get_task_struct(ts);
					rcu_read_unlock();
					if (ts == NULL)
						continue;

					if (th_list[i].tgid == th_list[i].tid) {
						show_stack_info(ts, KDBG_PRINT_PROCESS);
					} else {
						if (i == thread_count - 1)
							show_stack_info(ts, KDBG_PRINT_LAST_THREAD);
						else {
							if (th_list[i+1].tgid == th_list[i+1].tid)
								show_stack_info(ts, KDBG_PRINT_LAST_THREAD);
							else
								show_stack_info(ts, KDBG_PRINT_FIRST_THREAD);
						}
					}

					put_task_struct(ts);
				}
			}
		} else {
			if (sec_stackusage_status) {
				stackusage_show_header();

				for (i = 0; i < thread_count; i++) {
					rcu_read_lock();
					ts = find_task_by_pid_ns(th_list[i].tid, &init_pid_ns);
					if (ts)
						get_task_struct(ts);
					rcu_read_unlock();
					if (ts == NULL)
						continue;

					show_stack_info(ts, KDBG_PRINT_PROCESS);

					put_task_struct(ts);
				}
			}
		}
		/*free the allocated memory block*/
		kfree(th_list);
	}
	return 0;
}

static void turnoff_stackusage(void)
{
	if (sec_stackusage_status) {
		sec_stackusage_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Stack Usage Dump OFF\n");
	}
}

/*
 *Turn the prints of stackusage on
 *or off depending on the previous status.
 */
void sec_stackusage_prints_OnOff(void)
{
	sec_stackusage_status = (sec_stackusage_status) ? 0 : 1;

	if (sec_stackusage_status)
		PRINT_KD("Stack Usage Dump ON\n");
	else
		PRINT_KD("Stack Usage Dump OFF\n");
}

static int sec_stackusage_control(void)
{
	int operation = 0;
	int ret = 1;

	if (!sec_stackusage_init_flag)
		sec_stackusage_init();

	PRINT_KD("\n");
	PRINT_KD("Select Operation....\n");
	PRINT_KD("1. Turn On/Off the Stack Usage prints\n");
	PRINT_KD("==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n\n");

	switch (operation) {
	case 1:
		sec_stackusage_run_state = 1;
		sec_stackusage_prints_OnOff();
		ret = 0;	/* don't print the menu */
		break;
	default:
		break;
	}
	return ret;
}

#if defined(CONFIG_SEC_STACKUSAGE_AUTO_START) && defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)
static struct timer_list stackusage_auto_timer;
static void stackusage_auto_start(unsigned long duration)
{
	static int started;

	BUG_ON(started != 0 && started != 1);

	if (!sec_stackusage_init_flag) {
		PRINT_KD("Error: DiskUsage Not Initialized\n");
		return;
	}

	/* Make the status running */
	if (!started) {
		sec_stackusage_run_state = 1;
		/* timer setup for stop */
		mod_timer(&stackusage_auto_timer, duration);
		started = 1;

	} else {
		if (!sec_stackusage_status)
			sec_stackusage_run_state = 0;
		started = 0;
		del_timer(&stackusage_auto_timer);
	}
	return;

}
#endif

int kdbg_stackusage_init(void)
{
	sec_stackusage_init_flag = 0;

#ifdef CONFIG_SEC_STACKUSAGE_AUTO_START
	sec_stackusage_init();

#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	/* setup your timer to call stackusage_auto_timer_callback */
	setup_timer(&stackusage_auto_timer, stackusage_auto_start, jiffies + msecs_to_jiffies(CONFIG_COUNTER_MON_FINISHED_SEC * 1000));
	stackusage_auto_timer.expires = jiffies + msecs_to_jiffies(CONFIG_COUNTER_MON_START_SEC * 1000);
	/* setup timer interval to 200 msecs */
	add_timer(&stackusage_auto_timer);
#else
	sec_stackusage_run_state = 1;
#endif

#endif

	kdbg_register("COUNTER MONITOR: Stack Usage", sec_stackusage_control,
		      turnoff_stackusage, KDBG_MENU_COUNTER_MONITOR_STACK_USAGE);

	return 0;
}

/* initialize disk usage buffer and variable */

bool sec_stackusage_init(void)
{
	if (register_counter_monitor_func(sec_stackusage_dump) < 0) {
		PRINT_KD("WARN: Fail to Register Counter Monitor function\n");
		PRINT_KD("WARN: Status will not be changed\n");
		return false;
	}

	sec_stackusage_init_flag = 1;
	return true;
}

void sec_stackusage_destroy(void)
{

	/*First Unregister the function*/
	if (unregister_counter_monitor_func(sec_stackusage_dump) < 0)
		PRINT_KD("WARN:  Fail to unregister the function\n");

	sec_stackusage_status = 0;
	sec_stackusage_run_state = 0;
	sec_stackusage_init_flag = 0;
}
