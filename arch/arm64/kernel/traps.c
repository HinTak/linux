
/*
 * Based on arch/arm/kernel/traps.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>

#include <asm/atomic.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/traps.h>
#include <asm/stacktrace.h>
#include <asm/exception.h>
#include <asm/system_misc.h>

#ifdef CONFIG_VDLP_VERSION_INFO
#include <linux/vdlp_version.h>
#endif

#include <linux/coredump.h>

static const char *handler[]= {
	"Synchronous Abort",
	"IRQ",
	"FIQ",
	"Error"
};

int show_unhandled_signals = 1;

#ifdef CONFIG_VDLP_VERSION_INFO
void show_kernel_patch_version(void)
{
	pr_alert("================================================================================\n");
	pr_alert(" KERNEL Version : %s\n", DTV_KERNEL_VERSION);
	pr_alert("%s\n", DTV_LAST_PATCH);
	pr_alert("================================================================================\n");
}
#endif

#ifdef CONFIG_SHOW_FAULT_TRACE_INFO
void __show_user_stack(struct task_struct *task, unsigned long sp,
		struct pt_regs *regs);
#endif

#ifdef CONFIG_SHOW_THREAD_GROUP_STACK
	void __show_user_stack_tg(struct task_struct *task);
#endif


static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top, bool compat)
{
	unsigned long first;
	mm_segment_t fs;
	int i;
	unsigned int width = compat ? 4 : 8;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%016lx to 0x%016lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < (32 / width)
					&& p < top; i++, p += width) {
			if (p >= bottom && p < top) {
				unsigned long val;

				if (width == 8) {
					if (__get_user(val, (unsigned long *)p) == 0)
						sprintf(str + i * 17, " %016lx", val);
					else
						sprintf(str + i * 17, " ????????????????");
				} else {
					if (__get_user(val, (unsigned int *)p) == 0)
						sprintf(str + i * 9, " %08lx", val);
					else
						sprintf(str + i * 9, " ????????");
				}
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

#ifdef CONFIG_SHOW_FAULT_TRACE_INFO
void __show_user_stack(struct task_struct *task, unsigned long sp,
		struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	vma = find_vma(task->mm, task->user_ssp);
	if (!vma) {
		pr_cont("pid(%d):printing user stack failed.\n",
				(int)task->pid);
		return;
	}

	if (sp < vma->vm_start) {
		pr_cont("pid(%d) : seems stack overflow.\n"
				 "  sp(0x%016lx), stack vma (0x%016lx ~ 0x%016lx)\n",
			(int)task->pid, sp, vma->vm_start, vma->vm_end);
		return;
	}

	if (compat_user_mode(regs))
		pr_cont("pid(%d) stack vma (0x%08lx ~ 0x%08lx)\n",
			 (int)task->pid, vma->vm_start, vma->vm_end);
	else
		pr_cont("pid(%d) stack vma (0x%016lx ~ 0x%016lx)\n",
			 (int)task->pid, vma->vm_start, vma->vm_end);
	dump_mem(KERN_CONT, "User Stack: ", sp, task->user_ssp, compat_user_mode(regs));
}

#ifdef CONFIG_SHOW_THREAD_GROUP_STACK
void __show_user_stack_tg(struct task_struct *task)
{
	struct task_struct *g, *p;
	struct pt_regs *regs;

	pr_cont("--------------------------------------------------------\n");
	pr_cont("* dump all user stack of pid(%d) thread group\n",
		(int)task->pid);
	pr_cont("--------------------------------------------------------\n");

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (task->mm != p->mm)
			continue;
		if (task->pid == p->pid)
			continue;
		regs = task_pt_regs(p);
		__show_user_stack(p, user_stack_pointer(regs), regs);
		pr_cont("\n");
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
	pr_cont("--------------------------------------------------------\n\n");
}
#else
#define __show_user_stack_tg(t)
#endif /* CONFIG_SHOW_THREAD_GROUP_STACK */

/*
 *  Assumes that user program uses frame pointer
 *  TODO : consider context safety
 */
void show_user_stack(struct task_struct *task, struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	vma = find_vma(task->mm, task->user_ssp);
	if (vma) {
		if (compat_user_mode(regs))
			pr_cont("task stack info : pid(%d) stack area (0x%08lx ~ 0x%08lx)\n",
				(int)task->pid, vma->vm_start, vma->vm_end);
		else
			pr_cont("task stack info : pid(%d) stack area (0x%016lx ~ 0x%016lx)\n",
				(int)task->pid, vma->vm_start, vma->vm_end);
	}

	pr_cont("-----------------------------------------------------------\n");
	pr_cont("* dump user stack\n");
	pr_cont("-----------------------------------------------------------\n");
	__show_user_stack(task, user_stack_pointer(regs), regs);
	pr_cont("-----------------------------------------------------------\n\n");
	__show_user_stack_tg(task);
}
#ifdef CONFIG_SHOW_PC_LR_INFO
void show_pc_lr(struct task_struct *task, struct pt_regs *regs)
{
	unsigned long addr_pc_start, addr_lr_start, lr;
	unsigned long addr_pc_end, addr_lr_end;
	struct vm_area_struct *vma;

	if (compat_user_mode(regs))
		lr = regs->compat_lr;
	else
		lr = regs->regs[30];

	pr_cont("\n");
	pr_cont("--------------------------------------------------------------------------------------\n");
	pr_cont("PC, LR MEMINFO\n");
	pr_cont("--------------------------------------------------------------------------------------\n");
	pr_cont("PC:%lx, LR:%lx\n", (unsigned long) regs->pc, lr);

	/*Basic error handling*/
	if (regs->pc > 0x400)
		addr_pc_start = regs->pc - 0x400;   /* pc - 1024 byte */
	else
		addr_pc_start = 0;

	if (regs->pc < (TASK_SIZE-0x400))
		addr_pc_end = regs->pc + 0x400;     /* pc + 1024 byte */
	else
		addr_pc_end = TASK_SIZE-1;

	if (lr > 0x800)
		addr_lr_start = lr - 0x800;   /* lr - 2048 byte*/
	else
		addr_lr_start = 0;

	if (lr < (TASK_SIZE-0x400))
		addr_lr_end = lr + 0x400;     /* lr + 1024 byte */
	else
		addr_lr_end = TASK_SIZE-1;

	/*Calculate vma print range according which contain PC, LR*/
	if (((regs->pc & 0xfff) < 0x400) &&
			!find_vma(task->mm, addr_pc_start))
		addr_pc_start = regs->pc & (~0xfff);
	if (((regs->pc & 0xfff) > 0xBFF) &&
			!find_vma(task->mm, addr_pc_end))
		addr_pc_end = (regs->pc & (~0xfff)) + 0xfff;
	if (((lr & 0xfff) < 0x800) &&
			!find_vma(task->mm, addr_lr_start))
		addr_lr_start = lr & (~0xfff);
	if (((lr & 0xfff) > 0xBFF) &&
			!find_vma(task->mm, addr_lr_end))
		addr_lr_end = (lr & (~0xfff)) + 0xfff;

	/*Find a duplicated address range*/
	if ((addr_lr_start < addr_pc_start) &&
			(addr_lr_end > addr_pc_end))
		addr_pc_start = addr_pc_end;
	else if ((addr_pc_start <= addr_lr_start) &&
			(addr_pc_end >= addr_lr_end))
		addr_lr_start = addr_lr_end;
	else if ((addr_lr_start <= addr_pc_end) &&
			(addr_lr_end > addr_pc_end))
		addr_lr_start = addr_pc_end + 0x4;
	else if ((addr_pc_start <= addr_lr_end) &&
			(addr_pc_end > addr_lr_end))
		addr_pc_start = addr_lr_end + 0x4;

	pr_cont("--------------------------------------------------------------------------------------\n");
	vma = find_vma(task->mm, regs->pc);
	if (vma && (regs->pc >= vma->vm_start))
		dump_mem(KERN_CONT, "PC meminfo ", addr_pc_start, addr_pc_end, true);
	else
		pr_cont("No VMA for ADDR PC\n");

	pr_cont("--------------------------------------------------------------------------------------\n");
	vma = find_vma(task->mm, lr);
	if (vma && (lr >= vma->vm_start))
		dump_mem(KERN_CONT, "LR meminfo ", addr_lr_start, addr_lr_end, true);
	else
		pr_cont("No VMA for ADDR LR\n");

	pr_cont("--------------------------------------------------------------------------------------\n");
	pr_cont("\n");
}

 extern bool within_module_core(unsigned long addr,
		const struct module *mod);
 extern bool within_module_init(unsigned long addr,
		const struct module *mod);

void show_pc_lr_kernel(const struct pt_regs *regs)
{
	unsigned long addr_pc, addr_lr;
	unsigned long addr_pc_end = 0, addr_lr_end = 0;
	unsigned long addr_pc_page_end = 0, addr_lr_page_end = 0;
	int valid_pc, valid_lr;
	int valid_pc_mod, valid_lr_mod;
	struct module *mod;

	addr_pc = regs->pc - 0x400;   /* for 1024 byte */
	addr_lr = regs->regs[30] - 0x800;   /* for 2048 byte */

	valid_pc_mod = ((regs->pc >= VMALLOC_START &&
				regs->pc < VMALLOC_END) ||
			(regs->pc >= MODULES_VADDR &&
				regs->pc < MODULES_END));
	valid_lr_mod = ((regs->regs[30] >= VMALLOC_START &&
				regs->regs[30] < VMALLOC_END) ||
			(regs->regs[30] >= MODULES_VADDR &&
				regs->regs[30] < MODULES_END));

	valid_pc = (TASK_SIZE <= regs->pc &&
			regs->pc < (unsigned long)high_memory)
			 || valid_pc_mod;
	valid_lr = (TASK_SIZE <= regs->regs[30] &&
			regs->regs[30] < (unsigned long)high_memory)
			|| valid_lr_mod;

	/* Adjust the addr_pc according to the correct module
			virtual memory range. */
	if (valid_pc) {
		if (addr_pc < TASK_SIZE)
			addr_pc = TASK_SIZE;
		else if (valid_pc_mod) {
			mod = __module_address(regs->pc);
			if (!mod)
				valid_pc = 0;
			else if (!within_module_init(addr_pc, mod) &&
				 !within_module_core(addr_pc, mod)) {
				addr_pc = regs->pc & PAGE_MASK;
				addr_pc_page_end = (regs->pc + PAGE_SIZE) & PAGE_MASK;
				addr_pc_end = (regs->pc + CONFIG_PC_LR_INFO_PLUS_SIZE) < addr_pc_page_end ?
					(regs->pc + CONFIG_PC_LR_INFO_PLUS_SIZE) : (addr_pc_page_end - 0x4);
			}
		} else {
			addr_pc_end = regs->pc + CONFIG_PC_LR_INFO_PLUS_SIZE;
			addr_pc_end = (addr_pc_end) < (unsigned long)high_memory ?
					(addr_pc_end) : (unsigned long)(high_memory - 0x4);
		}
	}

	/* Adjust the addr_lr according to the correct module
			virtual memory range. */
	if (valid_lr) {
		if (addr_lr < TASK_SIZE)
			addr_lr = TASK_SIZE;
		else if (valid_lr_mod) {
			mod = __module_address(regs->regs[30]);
			if (!mod)
				valid_lr = 0;
			else if (!within_module_init(addr_lr, mod) &&
				 !within_module_core(addr_lr, mod)) {
				addr_lr = regs->regs[30] & PAGE_MASK;
				addr_lr_page_end = (regs->regs[30] + PAGE_SIZE) & PAGE_MASK;
				addr_lr_end = (regs->regs[30] + CONFIG_PC_LR_INFO_PLUS_SIZE) < addr_lr_page_end ?
					(regs->regs[30] + CONFIG_PC_LR_INFO_PLUS_SIZE) : (addr_lr_page_end - 0x4);
			}
		} else {
			addr_lr_end = regs->regs[30] + CONFIG_PC_LR_INFO_PLUS_SIZE;
			addr_lr_end = (addr_lr_end) < (unsigned long)high_memory ?
				(addr_lr_end) : (unsigned long)(high_memory - 0x4);
		}
	}

	if (valid_pc && valid_lr) {
		/* find a duplicated address range case1 */
		if ((addr_lr <= regs->pc) && (regs->pc < regs->regs[30]))
			addr_lr = regs->pc + 0x4;
		/* find a duplicated address rage case2 */
		else if ((addr_pc <= regs->regs[30]) &&
				(regs->regs[30] < regs->pc))
			addr_pc = regs->regs[30] + 0x4;
	}

	pr_info("--------------------------------------------------------------------------------------\n");
	pr_info("[VDLP] DISPLAY PC, LR in KERNEL Level\n");
	pr_info("pc:%lx, lr:%lx\n", (unsigned long)regs->pc, (unsigned long)regs->regs[30]);
	pr_info("--------------------------------------------------------------------------------------\n");

	if (valid_pc) {
		dump_mem_kernel("PC meminfo in kernel", addr_pc, regs->pc);
		pr_info("--------------------------------------------------------------------------------------\n");
		if (addr_pc_end)
			dump_mem_kernel("PC PLUS meminfo in kernel", regs->pc + 0x4, addr_pc_end);
	} else {
		pr_info("[VDLP] Invalid pc addr\n");
	}
	pr_info("--------------------------------------------------------------------------------------\n");

	if (valid_lr) {
		dump_mem_kernel("LR meminfo in kernel", addr_lr,
			regs->regs[30]);
		printk("--------------------------------------------------------------------------------------\n");
		if (addr_lr_end)
			dump_mem_kernel("LR PLUS meminfo in kernel", regs->regs[30] + 0x4, addr_lr_end);
	}
	else
		pr_info("[VDLP] Invalid lr addr\n");
	pr_info("--------------------------------------------------------------------------------------\n");
	pr_info("\n");
}

#ifdef CONFIG_DUMP_RANGE_BASED_ON_REGISTER
void show_register_memory_kernel(struct pt_regs *regs)
{
	unsigned long start_addr_for_printing = 0;
	unsigned long end_addr_for_printing = 0;
	int register_num;

	pr_alert("--------------------------------------------------------------------------------------\n");
	pr_alert("REGISTER MEMORY INFO\n");
	pr_alert("--------------------------------------------------------------------------------------\n");

	for (register_num = 0;
		register_num < sizeof(regs->regs)/sizeof(regs->regs[0]);
					register_num++) {
		pr_alert("\n\n* REGISTER : r%d\n", register_num);

		start_addr_for_printing = (regs->regs[register_num]
					& PAGE_MASK) - 0x1000; /* -4kbyte */
		if (regs->regs[register_num] >= 0xfffffffffffff000) {
			/* if virtual address is 0xffffffffffffffff,
				skip dump address to prevent overflow */
			end_addr_for_printing = 0xffffffffffffffff;
		} else {
			end_addr_for_printing = (regs->regs[register_num]
					& PAGE_MASK) + PAGE_SIZE + 0xfff;
		} /* about 8kbyte */

		if (!virt_addr_valid(regs->regs[register_num])) {
			pr_alert("# Register value 0x%lx is wrong address.\n",
				(unsigned long)regs->regs[register_num]);
			pr_alert("# We can't do anything.\n");
			pr_alert("# So, we search next register.\n");
			continue;
		}

		if (!virt_addr_valid(start_addr_for_printing)) {
			pr_alert("# 'start_addr_for_printing' is wrong address.\n");
			pr_alert("# So, we use just 'regs->regs[register_num] & PAGE_MASK)'\n");
			start_addr_for_printing = (regs->regs[register_num]
							& PAGE_MASK);
		}

		if (!virt_addr_valid(end_addr_for_printing)) {
			pr_alert("# 'end_addr_for_printing' is wrong address.\n");
			pr_alert("# So, we use 'PAGE_ALIGN(regs->regs[register_num]) + PAGE_SIZE-1'\n");
			end_addr_for_printing = (regs->regs[register_num]
							& PAGE_MASK)
							+ PAGE_SIZE-1;
		}

		pr_alert("# r%d register :0x%lx, start_addr : 0x%lx, end_addr : 0x%lx\n",
			register_num, (unsigned long)regs->regs[register_num],
				start_addr_for_printing, end_addr_for_printing);
		pr_alert("--------------------------------------------------------------------------------------\n");
		dump_mem_kernel("meminfo ", start_addr_for_printing,
				end_addr_for_printing);
		pr_alert("--------------------------------------------------------------------------------------\n");
		pr_alert("\n");
	}
}
#endif
#endif /* #ifdef CONFIG_SHOW_PC_LR_INFO */

#ifndef CONFIG_SEPARATE_PRINTK_FROM_USER
#define sep_printk_start
#define sep_printk_end
#else
extern void _sep_printk_start(void);
extern void _sep_printk_end(void);
#define sep_printk_start _sep_printk_start
#define sep_printk_end _sep_printk_end
#endif

DEFINE_MUTEX(dump_info_lock);
EXPORT_SYMBOL(dump_info_lock);

void dump_info(struct task_struct *task, struct pt_regs *regs,
		unsigned long addr)
{
	int old_lvl;
#ifdef CONFIG_KPI_SYSTEM_SUPPORT
	pid_t pgid = task_pgrp_vnr(task);
#endif

#ifdef CONFIG_SEPARATE_PRINTK_FROM_USER
	sep_printk_start();
#endif
	mutex_lock(&dump_info_lock);
	old_lvl = console_loglevel;
	console_verbose();      /* BSP patch : enable console while dump_info */
	preempt_disable();

#ifdef CONFIG_VDLP_VERSION_INFO
	show_kernel_patch_version();
#endif
#ifdef CONFIG_SHOW_PC_LR_INFO
	show_pc_lr(task, regs);
#endif

	if (addr)
		show_pte(task->mm, addr);

	show_regs(regs);
	show_pid_maps(task);
	show_user_stack(task, regs);
	preempt_enable();
	console_revert(old_lvl);        /* VDLinux patch : revert to console loglevel 15 -> old loglevel */
	mutex_unlock(&dump_info_lock);

#ifdef CONFIG_SEPARATE_PRINTK_FROM_USER
	sep_printk_end();
#endif

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
	/* kpi_fault */
	if (compat_user_mode(regs))
		set_kpi_fault((unsigned long)regs->pc,
				(unsigned long) regs->compat_lr,
				task->comm, task->group_leader->comm, "crash", pgid);
	else
		set_kpi_fault((unsigned long)regs->pc,
				(unsigned long) regs->regs[30],
				task->comm, task->group_leader->comm, "crash", pgid);
#endif
}
#endif /* CONFIG_SHOW_FAULT_TRACE_INFO */

static void dump_backtrace_entry(unsigned long where, unsigned long stack)
{
	print_ip_sym(where);
	if (in_exception_text(where))
			dump_mem("", "Exception stack", stack,
				 stack + sizeof(struct pt_regs), false);

}

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	mm_segment_t fs;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%08x) " : "%08x ", val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);

	set_fs(fs);
}

static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (!tsk)
		tsk = current;

	if (regs) {
		frame.fp = regs->regs[29];
		frame.sp = regs->sp;
		frame.pc = regs->pc;
	} else if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)dump_backtrace;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}

	pr_emerg("Call trace:\n");
	while (1) {
		unsigned long where = frame.pc;
		int ret;

		ret = unwind_frame(&frame);
		if (ret < 0)
			break;
		dump_backtrace_entry(where, frame.sp);
	}
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#ifdef CONFIG_SMP
#define S_SMP " SMP"
#else
#define S_SMP ""
#endif

static int __die(const char *str, int err, struct thread_info *thread,
		 struct pt_regs *regs)
{
	struct task_struct *tsk = thread->task;
	static int die_counter;
	int ret;

	pr_emerg("Internal error: %s: %x [#%d]" S_PREEMPT S_SMP "\n",
		 str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, 0, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	__show_regs(regs);
	pr_emerg("Process %.*s (pid: %d, stack limit = 0x%p)\n",
		 TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), thread + 1);

	if (!user_mode(regs) || in_interrupt()) {

		if (regs->sp > (unsigned long)task_stack_page(tsk)) {
			dump_mem(KERN_EMERG, "Stack: ", regs->sp,
				THREAD_SIZE +
				(unsigned long)task_stack_page(tsk), compat_user_mode(regs));
		} else {
			pr_alert("[VDLP] stack dump range change!!\n");
			if (compat_user_mode(regs)) {
				pr_alert("[VDLP] regs->sp(0x%08lx) -> task->stack(0x%08lx)!!\n",
					(unsigned long)regs->sp, (unsigned long)task_stack_page(tsk));
				dump_mem(KERN_EMERG, "Stack: ",
					(unsigned long)task_stack_page(tsk),
					THREAD_SIZE +
					(unsigned long)task_stack_page(tsk), true);
			} else {
				pr_alert("[VDLP] regs->sp(0x%016lx) -> task->stack(0x%016lx)!!\n",
					(unsigned long)regs->sp,
					(unsigned long)task_stack_page(tsk));
				dump_mem(KERN_EMERG, "Stack: ",
					(unsigned long)task_stack_page(tsk),
					THREAD_SIZE +
					(unsigned long)task_stack_page(tsk), false);
			}
		}

		dump_backtrace(regs, tsk);
		dump_instr(KERN_EMERG, regs);
	}

	return ret;
}

static DEFINE_RAW_SPINLOCK(die_lock);

#ifdef CONFIG_PRETTY_SHELL
/* TTY pretty mode, defined in n_tty.c */
extern unsigned char tty_pretty_mode;
#endif

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct thread_info *thread = current_thread_info();
	int ret;

#ifdef CONFIG_PRETTY_SHELL
	tty_pretty_mode = 0;
#endif

	oops_enter();
#ifdef CONFIG_VDLP_VERSION_INFO
	show_kernel_patch_version();
#endif

#ifdef CONFIG_SHOW_PC_LR_INFO
	show_pc_lr_kernel(regs);
#endif
	raw_spin_lock_irq(&die_lock);
#ifdef CONFIG_SMP
	if(setup_max_cpus>0)
	{
		smp_send_stop();
	}
#endif
	console_verbose();
	bust_spinlocks(1);
	ret = __die(str, err, thread, regs);

	if (regs && kexec_should_crash(thread->task))
		crash_kexec(regs);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);

#ifdef CONFIG_EMRG_SAVE_KLOG
	write_emrg_klog(regs, WRITE_RAW_KLOG_TYPE_OOPS);
#endif

#ifdef CONFIG_BUSYLOOP_WHILE_OOPS
	printk( KERN_ALERT "[SELP] while loop ... please attach T32...\n");
	while(1) {};
#endif

	raw_spin_unlock_irq(&die_lock);

#ifdef CONFIG_DTVLOGD
	/* Flush the messages remaining in dlog buffer */
	do_dtvlog(5, NULL, 0);
#endif

	oops_exit();

#ifdef CONFIG_DUMP_RANGE_BASED_ON_REGISTER
	show_register_memory_kernel((void *)regs);
#endif

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void arm64_notify_die(const char *str, struct pt_regs *regs,
		      struct siginfo *info, int err)
{
	if (user_mode(regs)) {
		current->thread.fault_address = 0;
		current->thread.fault_code = err;
		set_flag_block_sigkill(current, info->si_signo);
#ifdef CONFIG_SHOW_FAULT_TRACE_INFO
		dump_info(current, regs, 0);
#endif
		force_sig_info(info->si_signo, info, current);
	} else {
		die(str, regs, err);
	}
}

static LIST_HEAD(undef_hook);
static DEFINE_RAW_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

static int call_undef_hook(struct pt_regs *regs)
{
	struct undef_hook *hook;
	unsigned long flags;
	u32 instr;
	int (*fn)(struct pt_regs *regs, u32 instr) = NULL;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!user_mode(regs))
		return 1;

	if (compat_thumb_mode(regs)) {
		/* 16-bit Thumb instruction */
		if (get_user(instr, (u16 __user *)pc))
			goto exit;
		instr = le16_to_cpu(instr);
		if (aarch32_insn_is_wide(instr)) {
			u32 instr2;

			if (get_user(instr2, (u16 __user *)(pc + 2)))
				goto exit;
			instr2 = le16_to_cpu(instr2);
			instr = (instr << 16) | instr2;
		}
	} else {
		/* 32-bit ARM instruction */
		if (get_user(instr, (u32 __user *)pc))
			goto exit;
		instr = le32_to_cpu(instr);
	}

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
			(regs->pstate & hook->pstate_mask) == hook->pstate_val)
			fn = hook->fn;

	raw_spin_unlock_irqrestore(&undef_lock, flags);
exit:
	return fn ? fn(regs, instr) : 1;
}

asmlinkage void __exception do_undefinstr(struct pt_regs *regs)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);

	/* check for AArch32 breakpoint instructions */
	if (!aarch32_break_handler(regs))
		return;

	if (call_undef_hook(regs) == 0)
		return;

	if (show_unhandled_signals && unhandled_signal(current, SIGILL) &&
	    printk_ratelimit()) {
		pr_info("%s[%d]: undefined instruction: pc=%p\n",
			current->comm, task_pid_nr(current), pc);
		dump_instr(KERN_INFO, regs);
	}

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	arm64_notify_die("Oops - undefined instruction", regs, &info, 0);
}

long compat_arm_syscall(struct pt_regs *regs);

asmlinkage long do_ni_syscall(struct pt_regs *regs)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	if (show_unhandled_signals && printk_ratelimit()) {
		pr_info("%s[%d]: syscall %d\n", current->comm,
			task_pid_nr(current), (int)regs->syscallno);
		dump_instr("", regs);
		if (user_mode(regs))
			__show_regs(regs);
	}

	return sys_ni_syscall();
}

static const char *esr_class_str[] = {
	[0 ... ESR_ELx_EC_MAX]		= "UNRECOGNIZED EC",
	[ESR_ELx_EC_UNKNOWN]		= "Unknown/Uncategorized",
	[ESR_ELx_EC_WFx]		= "WFI/WFE",
	[ESR_ELx_EC_CP15_32]		= "CP15 MCR/MRC",
	[ESR_ELx_EC_CP15_64]		= "CP15 MCRR/MRRC",
	[ESR_ELx_EC_CP14_MR]		= "CP14 MCR/MRC",
	[ESR_ELx_EC_CP14_LS]		= "CP14 LDC/STC",
	[ESR_ELx_EC_FP_ASIMD]		= "ASIMD",
	[ESR_ELx_EC_CP10_ID]		= "CP10 MRC/VMRS",
	[ESR_ELx_EC_CP14_64]		= "CP14 MCRR/MRRC",
	[ESR_ELx_EC_ILL]		= "PSTATE.IL",
	[ESR_ELx_EC_SVC32]		= "SVC (AArch32)",
	[ESR_ELx_EC_HVC32]		= "HVC (AArch32)",
	[ESR_ELx_EC_SMC32]		= "SMC (AArch32)",
	[ESR_ELx_EC_SVC64]		= "SVC (AArch64)",
	[ESR_ELx_EC_HVC64]		= "HVC (AArch64)",
	[ESR_ELx_EC_SMC64]		= "SMC (AArch64)",
	[ESR_ELx_EC_SYS64]		= "MSR/MRS (AArch64)",
	[ESR_ELx_EC_IMP_DEF]		= "EL3 IMP DEF",
	[ESR_ELx_EC_IABT_LOW]		= "IABT (lower EL)",
	[ESR_ELx_EC_IABT_CUR]		= "IABT (current EL)",
	[ESR_ELx_EC_PC_ALIGN]		= "PC Alignment",
	[ESR_ELx_EC_DABT_LOW]		= "DABT (lower EL)",
	[ESR_ELx_EC_DABT_CUR]		= "DABT (current EL)",
	[ESR_ELx_EC_SP_ALIGN]		= "SP Alignment",
	[ESR_ELx_EC_FP_EXC32]		= "FP (AArch32)",
	[ESR_ELx_EC_FP_EXC64]		= "FP (AArch64)",
	[ESR_ELx_EC_SERROR]		= "SError",
	[ESR_ELx_EC_BREAKPT_LOW]	= "Breakpoint (lower EL)",
	[ESR_ELx_EC_BREAKPT_CUR]	= "Breakpoint (current EL)",
	[ESR_ELx_EC_SOFTSTP_LOW]	= "Software Step (lower EL)",
	[ESR_ELx_EC_SOFTSTP_CUR]	= "Software Step (current EL)",
	[ESR_ELx_EC_WATCHPT_LOW]	= "Watchpoint (lower EL)",
	[ESR_ELx_EC_WATCHPT_CUR]	= "Watchpoint (current EL)",
	[ESR_ELx_EC_BKPT32]		= "BKPT (AArch32)",
	[ESR_ELx_EC_VECTOR32]		= "Vector catch (AArch32)",
	[ESR_ELx_EC_BRK64]		= "BRK (AArch64)",
};

const char *esr_get_class_string(u32 esr)
{
	return esr_class_str[esr >> ESR_ELx_EC_SHIFT];
}

/*
 * bad_mode handles the impossible case in the exception vector.
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);
	console_verbose();

	pr_crit("Bad mode in %s handler detected, code 0x%08x -- %s\n",
		handler[reason], esr, esr_get_class_string(esr));
	__show_regs(regs);

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	arm64_notify_die("Oops - bad mode", regs, &info, 0);
}

void __pte_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pte %016lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pmd %016lx.\n", file, line, val);
}

void __pud_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pud %016lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pgd %016lx.\n", file, line, val);
}

void __init trap_init(void)
{
	return;
}
