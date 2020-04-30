#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/perf_event.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/coredump.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/audit.h>
#include <linux/tracehook.h>
#include <linux/kmod.h>
#include <linux/fsnotify.h>
#include <linux/fs_struct.h>
#include <linux/pipe_fs_i.h>
#include <linux/oom.h>
#include <linux/compat.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/exec.h>

#include <trace/events/task.h>
#include "internal.h"

#include <trace/events/sched.h>

#include <linux/delay.h>
#ifdef CONFIG_FREEZER
#include <linux/freezer.h>
#endif

extern char target_version[];
int core_uses_pid;
unsigned int core_pipe_limit;
char core_pattern[CORENAME_MAX_SIZE] = "core";

struct core_name {
	char *corename;
	int used, size;
};

#ifdef CONFIG_ELF_CORE

/* Maximum Path length for USB path checking */
#define USB_PATH_MAX 20

/* The maximal length of core_pattern is also specified in sysctl.c */
static int core_name_size = CORENAME_MAX_SIZE;

static int expand_corename(struct core_name *cn, int size)
{
	char *corename = krealloc(cn->corename, size, GFP_KERNEL);

	if (!corename)
		return -ENOMEM;

	if (size > core_name_size) /* racy but harmless */
		core_name_size = size;

	cn->size = ksize(corename);
	cn->corename = corename;
	return 0;
}

static int cn_vprintf(struct core_name *cn, const char *fmt, va_list arg)
{
	int free, need;
	va_list arg_copy;

again:
	free = cn->size - cn->used;

	va_copy(arg_copy, arg);
	need = vsnprintf(cn->corename + cn->used, free, fmt, arg_copy);
	va_end(arg_copy);

	if (need < free) {
		cn->used += need;
		return 0;
	}

	if (!expand_corename(cn, cn->size + need - free + 1))
		goto again;

	return -ENOMEM;
}

static int cn_printf(struct core_name *cn, const char *fmt, ...)
{
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	return ret;
}

static int cn_esc_printf(struct core_name *cn, const char *fmt, ...)
{
	int cur = cn->used;
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	for (; cur < cn->used; ++cur) {
		if (cn->corename[cur] == '/')
			cn->corename[cur] = '!';
	}
	return ret;
}

static int cn_print_exe_file(struct core_name *cn)
{
	struct file *exe_file;
	char *pathbuf, *path;
	int ret;

	exe_file = get_mm_exe_file(current->mm);
	if (!exe_file)
		return cn_esc_printf(cn, "%s (path unknown)", current->comm);

	pathbuf = kmalloc(PATH_MAX, GFP_TEMPORARY);
	if (!pathbuf) {
		ret = -ENOMEM;
		goto put_exe_file;
	}

	path = d_path(&exe_file->f_path, pathbuf, PATH_MAX);
	if (IS_ERR(path)) {
		ret = PTR_ERR(path);
		goto free_buf;
	}

	ret = cn_esc_printf(cn, "%s", path);

free_buf:
	kfree(pathbuf);
put_exe_file:
	fput(exe_file);
	return ret;
}

/* format_corename will inspect the pattern parameter, and output a
 * name into corename, which must have space for at least
 * CORENAME_MAX_SIZE bytes plus one byte for the zero terminator.
 */
static int format_corename(struct core_name *cn, struct coredump_params *cprm)
{
	const struct cred *cred = current_cred();
	const char *pat_ptr = core_pattern;
	int ispipe = (*pat_ptr == '|');
	int pid_in_pattern = 0;
	int err = 0;

	cn->used = 0;
	cn->corename = NULL;
	if (expand_corename(cn, core_name_size))
		return -ENOMEM;
	cn->corename[0] = '\0';

	if (ispipe)
		++pat_ptr;

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		if (*pat_ptr != '%') {
			err = cn_printf(cn, "%c", *pat_ptr++);
		} else {
			switch (*++pat_ptr) {
			/* single % at the end, drop that */
			case 0:
				goto out;
			/* Double percent, output one percent */
			case '%':
				err = cn_printf(cn, "%c", '%');
				break;
			/* pid */
			case 'p':
				pid_in_pattern = 1;
				err = cn_printf(cn, "%d",
					      task_tgid_vnr(current));
				break;
			/* global pid */
			case 'P':
				err = cn_printf(cn, "%d",
					      task_tgid_nr(current));
				break;
			case 'i':
				err = cn_printf(cn, "%d",
					      task_pid_vnr(current));
				break;
			case 'I':
				err = cn_printf(cn, "%d",
					      task_pid_nr(current));
				break;
			/* uid */
			case 'u':
				err = cn_printf(cn, "%d", cred->uid);
				break;
			/* gid */
			case 'g':
				err = cn_printf(cn, "%d", cred->gid);
				break;
			case 'd':
				err = cn_printf(cn, "%d",
					__get_dumpable(cprm->mm_flags));
				break;
			/* signal that caused the coredump */
			case 's':
				err = cn_printf(cn, "%ld", cprm->siginfo->si_signo);
				break;
			/* UNIX time of coredump */
			case 't': {
				struct timeval tv;
				do_gettimeofday(&tv);
				err = cn_printf(cn, "%lu", tv.tv_sec);
				break;
			}
			/* tid */
			case 'T': {
				err = cn_printf(cn, "%d", (int)current->pid);
				break;
			}
			/* hostname */
			case 'h':
				down_read(&uts_sem);
				err = cn_esc_printf(cn, "%s",
					      utsname()->nodename);
				up_read(&uts_sem);
				break;
			/* executable */
			case 'e':
				err = cn_esc_printf(cn, "%s", current->comm);
				break;
			case 'E':
				err = cn_print_exe_file(cn);
				break;
			/* core limit size */
			case 'c':
				err = cn_printf(cn, "%lu",
					      rlimit(RLIMIT_CORE));
				break;
			default:
				break;
			}
			++pat_ptr;
		}

		if (err)
			return err;
	}

out:
	/* Backward compatibility with core_uses_pid:
	 *
	 * If core_pattern does not include a %p (as is the default)
	 * and core_uses_pid is set, then .%pid will be appended to
	 * the filename. Do not do this for piped commands. */
	if (!ispipe && !pid_in_pattern && core_uses_pid) {
		err = cn_printf(cn, ".%d", task_tgid_vnr(current));
		if (err)
			return err;
	}
	return ispipe;
}
#endif /* CONFIG_ELF_CORE */

static int zap_process(struct task_struct *start, int exit_code)
{
	struct task_struct *t;
	int nr = 0;

	start->signal->group_exit_code = exit_code;
	start->signal->group_stop_count = 0;

	t = start;
	do {
		if (t->state & TASK_UNINTERRUPTIBLE) {
			pr_alert("thread '%s' has TASK_UNINTERRUPTIBLE(%ld) state\n",
				t->comm, t->state);
			pr_alert("faulting thread : %s\n", start->comm);
		}
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
		if (t != current && t->mm) {
			sigaddset(&t->pending.signal, SIGKILL);
			signal_wake_up(t, 1);
			nr++;
		}
	} while_each_thread(start, t);

	return nr;
}

static int zap_threads(struct task_struct *tsk, struct mm_struct *mm,
			struct core_state *core_state, int exit_code)
{
	struct task_struct *g, *p;
	unsigned long flags;
	int nr = -EAGAIN;

	spin_lock_irq(&tsk->sighand->siglock);
	if (!signal_group_exit(tsk->signal)) {
		mm->core_state = core_state;
		nr = zap_process(tsk, exit_code);
		tsk->signal->group_exit_task = tsk;
		/* ignore all signals except SIGKILL, see prepare_signal() */
		tsk->signal->flags = SIGNAL_GROUP_COREDUMP;
		clear_tsk_thread_flag(tsk, TIF_SIGPENDING);
	}
	spin_unlock_irq(&tsk->sighand->siglock);
	if (unlikely(nr < 0))
		return nr;

	tsk->flags |= PF_DUMPCORE;
	if (atomic_read(&mm->mm_users) == nr + 1)
		goto done;
	/*
	 * We should find and kill all tasks which use this mm, and we should
	 * count them correctly into ->nr_threads. We don't take tasklist
	 * lock, but this is safe wrt:
	 *
	 * fork:
	 *	None of sub-threads can fork after zap_process(leader). All
	 *	processes which were created before this point should be
	 *	visible to zap_threads() because copy_process() adds the new
	 *	process to the tail of init_task.tasks list, and lock/unlock
	 *	of ->siglock provides a memory barrier.
	 *
	 * do_exit:
	 *	The caller holds mm->mmap_sem. This means that the task which
	 *	uses this mm can't pass exit_mm(), so it can't exit or clear
	 *	its ->mm.
	 *
	 * de_thread:
	 *	It does list_replace_rcu(&leader->tasks, &current->tasks),
	 *	we must see either old or new leader, this does not matter.
	 *	However, it can change p->sighand, so lock_task_sighand(p)
	 *	must be used. Since p->mm != NULL and we hold ->mmap_sem
	 *	it can't fail.
	 *
	 *	Note also that "g" can be the old leader with ->mm == NULL
	 *	and already unhashed and thus removed from ->thread_group.
	 *	This is OK, __unhash_process()->list_del_rcu() does not
	 *	clear the ->next pointer, we will find the new leader via
	 *	next_thread().
	 */
	rcu_read_lock();
	for_each_process(g) {
		if (g == tsk->group_leader)
			continue;
		if (g->flags & PF_KTHREAD)
			continue;
		p = g;
		do {
			if (p->mm) {
				if (unlikely(p->mm == mm)) {
					lock_task_sighand(p, &flags);
					nr += zap_process(p, exit_code);
					p->signal->flags = SIGNAL_GROUP_EXIT;
					unlock_task_sighand(p, &flags);
				}
				break;
			}
		} while_each_thread(g, p);
	}
	rcu_read_unlock();
done:
	atomic_set(&core_state->nr_threads, nr);
	return nr;
}

static int coredump_wait(int exit_code, struct core_state *core_state)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	int core_waiters = -EBUSY;

	init_completion(&core_state->startup);
	core_state->dumper.task = tsk;
	core_state->dumper.next = NULL;

	down_write(&mm->mmap_sem);
	if (!mm->core_state)
		core_waiters = zap_threads(tsk, mm, core_state, exit_code);
	up_write(&mm->mmap_sem);

	if (core_waiters > 0) {
		struct core_thread *ptr;

		wait_for_completion(&core_state->startup);
		/*
		 * Wait for all the threads to become inactive, so that
		 * all the thread context (extended register state, like
		 * fpu etc) gets copied to the memory.
		 */
		ptr = core_state->dumper.next;
		while (ptr != NULL) {
			wait_task_inactive(ptr->task, 0);
			ptr = ptr->next;
		}
	}

	return core_waiters;
}

static void coredump_finish(struct mm_struct *mm, bool core_dumped)
{
	struct core_thread *curr, *next;
	struct task_struct *task;

	spin_lock_irq(&current->sighand->siglock);
	if (core_dumped && !__fatal_signal_pending(current))
		current->signal->group_exit_code |= 0x80;
	current->signal->group_exit_task = NULL;
	current->signal->flags = SIGNAL_GROUP_EXIT;
	spin_unlock_irq(&current->sighand->siglock);

	next = mm->core_state->dumper.next;
	while ((curr = next) != NULL) {
		next = curr->next;
		task = curr->task;
		/*
		 * see exit_mm(), curr->task must not see
		 * ->task == NULL before we read ->next.
		 */
		smp_mb();
		curr->task = NULL;
		wake_up_process(task);
	}

	mm->core_state = NULL;
}

static bool dump_interrupted(void)
{
	/*
	 * SIGKILL or freezing() interrupt the coredumping. Perhaps we
	 * can do try_to_freeze() and check __fatal_signal_pending(),
	 * but then we need to teach dump_write() to restart and clear
	 * TIF_SIGPENDING.
	 */
	return signal_pending(current);
}

#ifdef CONFIG_ELF_CORE
static void wait_for_dump_helpers(struct file *file)
{
	struct pipe_inode_info *pipe = file->private_data;

	pipe_lock(pipe);
	pipe->readers++;
	pipe->writers--;
	wake_up_interruptible_sync(&pipe->wait);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	pipe_unlock(pipe);

	/*
	 * We actually want wait_event_freezable() but then we need
	 * to clear TIF_SIGPENDING and improve dump_interrupted().
	 */
	wait_event_interruptible(pipe->wait, pipe->readers == 1);

	pipe_lock(pipe);
	pipe->readers--;
	pipe->writers++;
	pipe_unlock(pipe);
}

/*
 * umh_pipe_setup
 * helper function to customize the process used
 * to collect the core in userspace.  Specifically
 * it sets up a pipe and installs it as fd 0 (stdin)
 * for the process.  Returns 0 on success, or
 * PTR_ERR on failure.
 * Note that it also sets the core limit to 1.  This
 * is a special value that we use to trap recursive
 * core dumps
 */
static int umh_pipe_setup(struct subprocess_info *info, struct cred *new)
{
	struct file *files[2];
	struct coredump_params *cp = (struct coredump_params *)info->data;
	int err = create_pipe_files(files, 0);
	if (err)
		return err;

	cp->file = files[1];

	err = replace_fd(0, files[0], 0);
	fput(files[0]);
	/* and disallow core files too */
	current->signal->rlim[RLIMIT_CORE] = (struct rlimit){1, 1};

	return err;
}
#endif /* CONFIG_ELF_CORE */

#ifdef CONFIG_VD_PERFORMANCE_MODE
#define USB_CON_MAX_RETRIES 10
#else
#define USB_CON_MAX_RETRIES INT_MAX
#endif

#if (defined(CONFIG_DTVLOGD) && defined(CONFIG_BINFMT_ELF_COMP))
void create_serial_log(const char *dtv_filepath)
{
	struct inode *inode;
	mm_segment_t fs = get_fs();
	char dtv_filename[CORENAME_MAX_SIZE + 1] = "";
	char process_name[TASK_COMM_LEN] = "??";

	int validlen1 = 0, validlen2 = 0;
	char *dtvbuf1 = NULL, *dtvbuf2 = NULL;
	struct file *dtvfile = NULL;
	loff_t pos;
	int ret = -1;
	int replaced_special_chars = 0;

	if (!dtv_filepath)
		goto log_fail;

	get_task_comm(process_name, current->group_leader);
	set_fs(KERNEL_DS);
	snprintf(dtv_filename, sizeof(dtv_filename),
			"%s/Dtvlog.%s.%d.txt",
			dtv_filepath,
			process_name,
			current->pid);

recheck_special_chars:
	dtvfile = filp_open(dtv_filename,
			O_CREAT | O_WRONLY | O_NOFOLLOW,
			0666);

	if (IS_ERR(dtvfile)) {
		if (((int)dtvfile == -EINVAL) && !replaced_special_chars) {
			printk(KERN_ALERT "[DTVLOGD_WARNING] File open: invalid arg: '%s'\n",
					dtv_filename);
			snprintf(dtv_filename, sizeof(dtv_filename),
					"%s/Dtvlog.%s.%d.txt",
					dtv_filepath,
					"ill_fmt",
					current->pid);
			printk(KERN_ALERT "[DTVLOGD_WARNING] Changing file to: '%s'\n",
					dtv_filename);
			replaced_special_chars = 1;
			goto recheck_special_chars;

		} else {
			printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] File open error: %d\n",
					dtv_filename, (int)dtvfile);
			goto log_fail_set_fs;
		}
	}

	/* error checks - reference do_coredump */
	inode = dtvfile->f_path.dentry->d_inode;
	if (inode->i_nlink > 1) {
		printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] More than one Hard link to the file\n",
				dtv_filename);
		goto log_fail_close;
	}

	if (d_unhashed(dtvfile->f_path.dentry)) {
		printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] file dentry not hashed\n",
				dtv_filename);
		goto log_fail_close;
	}

	if (!S_ISREG(inode->i_mode)) {
		printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] Not a regular file\n",
				dtv_filename);
		goto log_fail_close;
	}

	if (!dtvfile->f_op) {
		printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] File Operation handlers not registered\n",
				dtv_filename);
		goto log_fail_close;
	}
	ret = do_truncate(dtvfile->f_path.dentry, 0, 0, dtvfile);
	if (ret) {
		printk(KERN_ALERT "[DTVLOGD_FAIL|%s|File IO Error] Failed to truncate file, errno : %d\n",
				dtv_filename, ret);
		goto log_fail_close;
	}

	dtvlogd_write_stop();
	ret = acquire_dtvlogd_all_buffer(&dtvbuf1, &validlen1,
			&dtvbuf2, &validlen2);
	if (!ret) {
		/*
		 * Buffer can be:
		 *  empty: validlen1 = 0, validlen1 = 0
		 *  single: validlen1 > 0, validlen2 = 0
		 *  or of 2 parts:
		 *   validlen1 > 0, validlen2 > 0
		 */
		pos = dtvfile->f_pos;
		if (validlen1 > 0)
			__kernel_write(dtvfile, dtvbuf1, validlen1, &pos);
		dtvfile->f_pos = pos;

		if (validlen2 > 0)
			__kernel_write(dtvfile, dtvbuf2, validlen2, &pos);
		dtvfile->f_pos = pos;
	} else {
		printk(KERN_ALERT "[DTVLOGD_FAIL] failed to get dtvlogd buffer\n");
	}

	dtvlogd_write_start();

log_fail_close:
	filp_close(dtvfile, NULL);
log_fail_set_fs:
	set_fs(fs);
log_fail:
	if (!ret)
		printk(KERN_CRIT "Serial logs saved to %s\n", dtv_filename);
	else
		printk(KERN_ALERT "[DTVLOGD_FAIL]: Couldn't save serial logs\n");

	return;
}
#endif

void do_coredump(const siginfo_t *siginfo)
{
	struct core_state core_state;
	struct mm_struct *mm = current->mm;
	const struct cred *old_cred;
	struct cred *cred;
	int retval = 0;
	int flag = 0;
	bool need_suid_safe = false;
	bool core_dumped = false;
#ifdef CONFIG_ELF_CORE
	int ispipe;
	char comp_corename[NAME_MAX + 1] = "";
	struct files_struct *displaced;
	struct core_name cn;
	struct linux_binfmt *binfmt;
	struct inode *inode;
	static atomic_t core_dump_count = ATOMIC_INIT(0);
	char usb_corepath[USB_PATH_MAX + 1] = "";
	char tmp_corepath[] = {"/tmp"};
#endif /* CONFIG_ELF_CORE */

	struct coredump_params cprm = {
		.siginfo = siginfo,
		.regs = signal_pt_regs(),
		.limit = rlimit(RLIMIT_CORE),
		/*
		 * We must use the same mm->flags while dumping core to avoid
		 * inconsistency of bit flags, since this flag is not protected
		 * by any locks.
		 */
		.mm_flags = mm->flags,
	};
#ifdef CONFIG_BINFMT_ELF_COMP
#define COMP_CORENAME_PATH  7
	int err;
	struct path path;
	static const char * const usb_corepath_check[] = {"/opt/storage/usb", "/opt/media"};
	extern struct module *ultimate_module_check(const char *name);
	struct module *mod_usbcore = NULL, *mod_ehci = NULL, *mod_storage = NULL;
	const char *usb_module_list[3] = {"usbcore", "ehci_hcd", "usb_storage"};
	unsigned char is_usbmodule_loaded = 0;
	const char *usb_mount_list[2][6] = { {"sda", "sda1", "sdb", "sdb1", "sdc", "sdc1"},
					{"USBDriveA", "USBDriveA1", "USBDriveB", "USBDriveB1",
					"USBDriveC", "USBDriveC1"} };
	int cnt = 0;
	int usb_mount_list_cnt = 0;
	struct file *binary_file;
	struct task_struct *tsk;
	char binary_name[NAME_MAX + 1] = "";
	char tsk_name[TASK_COMM_LEN] = "";
#ifdef CONFIG_DTVLOGD
	char dtv_filepath[CORENAME_MAX_SIZE + 1] = "";
#endif
#endif

	audit_core_dumps(siginfo->si_signo);

	if (!__get_dumpable(cprm.mm_flags)) {
		pr_alert("[COREDUMP_FAIL|Permission Error] Core dump files are not created for set-user-ID\n");
		pr_alert("\t\tmm_flags- %x\n", (unsigned int)cprm.mm_flags);
	}

	cred = prepare_creds();
	if (!cred) {
		pr_alert("[COREDUMP_FAIL|Function Error] Creating new task credentials failed..");
		goto fail;
	}
	/*
	 * We cannot trust fsuid as being the "true" uid of the process
	 * nor do we know its entire history. We only know it was tainted
	 * so we dump it as root in mode 2, and only into a controlled
	 * environment (pipe handler or fully qualified path).
	 */
	if (__get_dumpable(cprm.mm_flags) == SUID_DUMP_ROOT) {
		flag = O_EXCL;          /* Stop rewrite attacks */
		/* Setuid core dump mode */
		cred->fsuid = GLOBAL_ROOT_UID;	/* Dump root private */
		need_suid_safe = true;
	}

	retval = coredump_wait(siginfo->si_signo, &core_state);
	if (retval < 0) {
		pr_alert("[COREDUMP_FAIL|COREDUMP_WAIT FAIL] Process termination is in progress\n");
		goto fail_creds;
	}

	old_cred = override_creds(cred);

#ifdef CONFIG_MINIMAL_CORE
	do_minimal_core(&cprm);
#endif

#ifdef CONFIG_ELF_CORE
	binfmt = mm->binfmt;
	if (!binfmt || !binfmt->core_dump) {
		pr_alert("[COREDUMP_FAIL|Function Error]");
		pr_alert(" Binfmt handler/functions not defined\n");
		goto fail_unlock_nonfree;
	}

	ispipe = format_corename(&cn, &cprm);

	if (ispipe) {
		int dump_count;
		char **helper_argv;
		struct subprocess_info *sub_info;

		if (ispipe < 0) {
			printk(KERN_WARNING "format_corename failed\n");
			printk(KERN_WARNING "Aborting core\n");
			goto fail_unlock;
		}

		if (cprm.limit == 1) {
			/* See umh_pipe_setup() which sets RLIMIT_CORE = 1.
			 *
			 * Normally core limits are irrelevant to pipes, since
			 * we're not writing to the file system, but we use
			 * cprm.limit of 1 here as a special value, this is a
			 * consistent way to catch recursive crashes.
			 * We can still crash if the core_pattern binary sets
			 * RLIM_CORE = !1, but it runs as root, and can do
			 * lots of stupid things.
			 *
			 * Note that we use task_tgid_vnr here to grab the pid
			 * of the process group leader.  That way we get the
			 * right pid if a thread in a multi-threaded
			 * core_pattern process dies.
			 */
			printk(KERN_WARNING
				"Process %d(%s) has RLIMIT_CORE set to 1\n",
				task_tgid_vnr(current), current->comm);
			printk(KERN_WARNING "Aborting core\n");
			goto fail_unlock;
		}
		cprm.limit = RLIM_INFINITY;

		dump_count = atomic_inc_return(&core_dump_count);
		if (core_pipe_limit && (core_pipe_limit < dump_count)) {
			printk(KERN_WARNING "Pid %d(%s) over core_pipe_limit\n",
			       task_tgid_vnr(current), current->comm);
			printk(KERN_WARNING "Skipping core dump\n");
			goto fail_dropcount;
		}

		helper_argv = argv_split(GFP_KERNEL, cn.corename, NULL);
		if (!helper_argv) {
			printk(KERN_WARNING "%s failed to allocate memory\n",
			       __func__);
			goto fail_dropcount;
		}

		retval = -ENOMEM;
		sub_info = call_usermodehelper_setup(helper_argv[0],
						helper_argv, NULL, GFP_KERNEL,
						NULL, NULL, NULL);
		if (sub_info)
			retval = call_usermodehelper_exec(sub_info,
							  UMH_WAIT_EXEC);

		argv_free(helper_argv);
		if (retval) {
			printk(KERN_INFO "Core dump to |%s pipe failed\n",
			       cn.corename);
			goto fail_dropcount;
		}
	}

	if (cprm.limit < binfmt->min_coredump) {
		pr_alert("[COREDUMP_WARNING|Taskname(PID):%s(%d)] RLIMIT_CORE(%lu) is less than min coredump size(%lu)\n",
				current->comm, current->pid, cprm.limit, binfmt->min_coredump);
		goto fail_dropcount;
	}

#ifdef CONFIG_BINFMT_ELF_COMP
	/* Change code for saving CoreDump file */
	mutex_lock(&module_mutex);
	/* looks like we don't really care about these modules, just checking
	* if they have been loaded, so it's safe to unlock. in any other case
	* make sure you hold this mutex. */
	mod_usbcore = (struct module *)ultimate_module_check(usb_module_list[0]);
	mod_ehci = (struct module *)ultimate_module_check(usb_module_list[1]);
	mod_storage = (struct module *)ultimate_module_check(usb_module_list[2]);

	mutex_unlock(&module_mutex);

	 /* find the binary name */
	binary_file = get_mm_exe_file(current->mm);
	if (binary_file) {
		strncpy(binary_name, binary_file->f_path.dentry->d_name.name, NAME_MAX);
		binary_name[NAME_MAX] = '\0';
		fput(binary_file);
	}

	/* find the parent's task name */
	rcu_read_lock();
	tsk = find_task_by_vpid(current->tgid);
	if (tsk) {
		get_task_struct(tsk);
		get_task_comm(tsk_name, tsk);
		put_task_struct(tsk);
	}
	rcu_read_unlock();

	if (mod_usbcore && mod_ehci && mod_storage) {
		int retries = 0;
		is_usbmodule_loaded = 1;        /* all usb modules loaded */
		while (1) {
			pr_alert("***** Coredump : Insert USB memory stick, mount check per 10sec... *****\n");

#ifdef CONFIG_FREEZER
			if (pm_freezing) {
				pr_alert("[COREDUMP_FAIL|%s %d|Suspend in Progress] Quitting Coredump.\n",
					current->comm, current->pid);
				goto fail_dropcount;
			}
#endif
			
			for (cnt = 0; cnt < sizeof(usb_corepath_check) / sizeof(char *); cnt++) {
				err = kern_path(usb_corepath_check[cnt], LOOKUP_DIRECTORY, &path);
				if (!err) {
					strncpy(usb_corepath, usb_corepath_check[cnt], USB_PATH_MAX);
					path_put(&path);
					usb_mount_list_cnt = cnt;
					break;
				}
			}

			if (!err)
				break;

detect:
			retries++;

			if (retries >= USB_CON_MAX_RETRIES) {
				pr_alert("[COREDUMP_FAIL] USB Checking time out.\n");
				goto fail_dropcount;
			}

			mdelay(10 * 1000);
		}

		for (cnt = 0; cnt < (COMP_CORENAME_PATH - 1); cnt++) {
			/* check if the task name and binary name is same */
			if (unlikely(binary_file == NULL) || !strncmp(binary_name, tsk_name, TASK_COMM_LEN)) {
				if (unlikely(tsk == NULL) || current->pid == current->tgid) { /* Unknown Thread or Process */
					snprintf(comp_corename, sizeof(comp_corename),
						"%s/%s/Coredump.%s.%d", usb_corepath, usb_mount_list[usb_mount_list_cnt][cnt],
					current->comm, current->pid);
				} else { /* Thread */
					snprintf(comp_corename, sizeof(comp_corename),
						"%s/%s/Coredump.%s.%s.%d", usb_corepath, usb_mount_list[usb_mount_list_cnt][cnt],
					tsk_name, current->comm, current->pid);
				}
			} else {
			if (unlikely(tsk == NULL) || current->pid == current->tgid) { /* Unknown Thread or Process */
					snprintf(comp_corename, sizeof(comp_corename),
							"%s/%s/Coredump.%s(%s).%d", usb_corepath, usb_mount_list[usb_mount_list_cnt][cnt],
						current->comm, binary_name, current->pid);
				} else { /* Thread */
					snprintf(comp_corename, sizeof(comp_corename),
						"%s/%s/Coredump.%s.%s(%s).%d", usb_corepath, usb_mount_list[usb_mount_list_cnt][cnt],
					tsk_name, current->comm, binary_name, current->pid);
				}
			}
			/* add target_version if present */
			if (strncmp(target_version, "", sizeof(""))) {
				strncat(comp_corename, ".", sizeof(comp_corename) - strlen(comp_corename) - 1);
				strncat(comp_corename, target_version, sizeof(comp_corename) - strlen(comp_corename) - 1);
				/* remove the extra '\n' character in comp_corename */
				if (comp_corename[strlen(comp_corename) - 1] == '\n')
					comp_corename[strlen(comp_corename) - 1] = '\0';
			}

			/* add the extension */
			strncat(comp_corename, ".gz", sizeof(comp_corename) - strlen(comp_corename) - 1);

recheck:
			cprm.file = filp_open(comp_corename,
					      O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE | flag, 0600);
			if (!IS_ERR(cprm.file)) {
				pr_alert("***** USB detected *****\n");
				pr_alert("***** Create pid : %d coredump file to USB mount dir %s ******\n",
					current->pid, comp_corename);
				break;
			} else {
				if ((size_t)cprm.file == -EINVAL) {
					pr_alert("[COREDUMP_WARNING] File Open Failed: Invalid Argument '%s'\n", comp_corename);
					snprintf(comp_corename, sizeof(comp_corename),
					"%s/%s/Coredump.ill_fmt.%d.gz", usb_corepath, usb_mount_list[usb_mount_list_cnt][cnt],
					current->pid);
					goto recheck;
				} else if ((size_t)cprm.file != -ENOENT) {
#ifdef CONFIG_ARM64
					pr_alert("[COREDUMP_WARNING] File Open Failed with error no %lld : '%s'\n",
						(long long int)cprm.file, comp_corename);
#else
					pr_alert("[COREDUMP_WARNING] File Open Failed with error no %d : '%s'\n",
						(int)cprm.file, comp_corename);
#endif
				}
			}
		}

		if (IS_ERR(cprm.file))
			goto detect;
	} else {
		is_usbmodule_loaded = 0;        /* return NULL, usb modules not loaded */
		pr_alert("***** USB modules not loaded ******\n");

		/* check if the task name and binary name is same */
		if (unlikely(binary_file == NULL) || !strncmp(binary_name, tsk_name, TASK_COMM_LEN)) {
			if (unlikely(tsk == NULL) || current->pid == current->tgid) { /* Unknown Thread or Process */
				snprintf(comp_corename, sizeof(comp_corename), "%s/Coredump.%s.%d", tmp_corepath,
					current->comm, current->pid);
			} else { /* Thread */
				snprintf(comp_corename, sizeof(comp_corename), "%s/Coredump.%s.%s.%d", tmp_corepath,
					tsk_name, current->comm, current->pid);
			}
		} else {
			if (unlikely(tsk == NULL) || current->pid == current->tgid) { /* Unknown Thread or Process */
				snprintf(comp_corename, sizeof(comp_corename), "%s/Coredump.%s(%s).%d", tmp_corepath,
					current->comm, binary_name, current->pid);
			} else { /* Thread */
				snprintf(comp_corename, sizeof(comp_corename), "%s/Coredump.%s.%s(%s).%d", tmp_corepath,
					tsk_name, current->comm, binary_name, current->pid);
			}
		}
		/* add target_version if present */
		if (strncmp(target_version, "", sizeof(""))) {
			strncat(comp_corename, ".", sizeof(comp_corename) - strlen(comp_corename) - 1);
			strncat(comp_corename, target_version, sizeof(comp_corename) - strlen(comp_corename) - 1);
			/* remove the extra '\n' character in comp_corename */
			if (comp_corename[strlen(comp_corename) - 1] == '\n')
				comp_corename[strlen(comp_corename) - 1] = '\0';
		}

		/* add the extension */
		strncat(comp_corename, ".gz", sizeof(comp_corename) - strlen(comp_corename) - 1);

		pr_alert("***** Create coredump file to tmpfs %s ******\n", comp_corename);
		cprm.file = filp_open(comp_corename, O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE | flag, 0666);
		if (IS_ERR(cprm.file)) {
			pr_alert("***** Coredump Fail... can't create corefile to %s dir *****\n", tmp_corepath);
#ifdef CONFIG_ARM64
			pr_alert("[COREDUMP_FAIL|%s|File IO Error] Unable to open file errno:%lld\n",
				comp_corename, (long long int)cprm.file);
#else
			pr_alert("[COREDUMP_FAIL|%s|File IO Error] Unable to open file errno:%d\n",
					comp_corename, (int)cprm.file);
#endif
			goto fail_dropcount;
		}
	}
#else   /* original coredump */
	if (need_suid_safe && cn.corename[0] != '/') {
		printk(KERN_WARNING "Pid %d(%s) can only dump core "\
			"to fully qualified path!\n",
			task_tgid_vnr(current), current->comm);
		printk(KERN_WARNING "Skipping core dump\n");
		goto fail_dropcount;
	}

	/*
	 * Unlink the file if it exists unless this is a SUID
	 * binary - in that case, we're running around with root
	 * privs and don't want to unlink another user's coredump.
	 */
	if (!need_suid_safe) {
		mm_segment_t old_fs;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		/*
		 * If it doesn't exist, that's fine. If there's some
		 * other problem, we'll catch it at the filp_open().
		 */
		(void) sys_unlink((const char __user *)cn.corename);
		set_fs(old_fs);
	}

	/*
	 * There is a race between unlinking and creating the
	 * file, but if that causes an EEXIST here, that's
	 * fine - another process raced with us while creating
	 * the corefile, and the other process won. To userspace,
	 * what matters is that at least one of the two processes
	 * writes its coredump successfully, not which one.
	 */
	cprm.file = filp_open(cn.corename,
			 O_CREAT | 2 | O_NOFOLLOW |
			 O_LARGEFILE | O_EXCL,
			 0600);
	if (IS_ERR(cprm.file)) {
#ifdef CONFIG_ARM64
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] File Open Error : %lld\n",
			cn.corename, (long long int)cprm.file);
#else
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] File Open Error : %d\n",
			cn.corename, (int)cprm.file);
#endif
		goto fail_dropcount;
	}
	snprintf(comp_corename, sizeof(comp_corename), "%s",
						(char *)cprm.file->f_path.dentry->d_name.name);
#endif
	inode = file_inode(cprm.file);
	if (inode->i_nlink > 1) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error]  More than one Hard link to the file\n",
				comp_corename);
		goto close_fail;
	}
	if (d_unhashed(cprm.file->f_path.dentry)) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] File inode has non anonymous-DCACHE_DISCONNECTED\n",
				comp_corename);
		goto close_fail;
	}
	/*
	 * AK: actually i see no reason to not allow this for named
	 * pipes etc, but keep the previous behaviour for now.
	 */
	if (!S_ISREG(inode->i_mode)) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] Not a regular file\n", comp_corename);
		goto close_fail;
	}
	/*
	 * Don't dump core if the filesystem changed owner or mode
	 * of the file during file creation. This is an issue when
	 * a process dumps core while its cwd is e.g. on a vfat
	 * filesystem.
	 */
#ifndef	CONFIG_ALLOW_ALL_USER_COREDUMP
	if (!uid_eq(inode->i_uid, current_fsuid())) {
		pr_alert("[COREDUMP_FAIL|%s|Permission Error] User-ID verfication failed for file\n",
				comp_corename);
		goto close_fail;
	}
#endif
	if (!cprm.file->f_op) {
			pr_alert("[COREDUMP_FAIL|%s|File IO Error] File Operation handlers not registered for file",
					comp_corename);
		goto close_fail;
	}
	/*if ((inode->i_mode & 0677) != 0600) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] File inode mode not valid\n",
				comp_corename);
		goto close_fail;
	}*/
	if (!(cprm.file->f_mode & FMODE_CAN_WRITE)) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] File write mode not set\n",
				comp_corename);
		goto close_fail;
	}
	retval = do_truncate(cprm.file->f_path.dentry, 0, 0, cprm.file);
	if (retval) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] Failed to truncate file, errno : %d\n",
				comp_corename, retval);
		goto close_fail;
	}

	/* get us an unshared descriptor table; almost always a no-op */
	retval = unshare_files(&displaced);
	if (retval) {
		pr_alert("[COREDUMP_FAIL|%s|File IO Error] Failed to unshare file descriptor table\n",
				comp_corename);
		goto close_fail;
	}
	if (displaced)
		put_files_struct(displaced);
	if (!dump_interrupted()) {
#ifdef CONFIG_BINFMT_ELF_COMP
		pr_alert("* Ultimate CoreDump v1.0 : started dumping core into '%s' file *\n", comp_corename);
#else
		pr_alert("* Original coredump : started dumping core into '%s' file *\n", comp_corename);
#endif
		file_start_write(cprm.file);
		core_dumped = binfmt->core_dump(&cprm);
		file_end_write(cprm.file);
#ifdef CONFIG_BINFMT_ELF_COMP
		if (is_usbmodule_loaded)
			pr_alert("***** Create coredump file to USB mount dir '%s' ******\n", comp_corename);
		else
			pr_alert("***** Create coredump file to tmpfs '%s' ******\n", comp_corename);
#ifdef CONFIG_DTVLOGD
		if (is_usbmodule_loaded) {
			snprintf(dtv_filepath, sizeof(dtv_filepath),
				"%s/%s",usb_corepath,
				usb_mount_list[usb_mount_list_cnt][cnt]);

		} else {
			snprintf(dtv_filepath, sizeof(dtv_filepath),
				"%s",tmp_corepath);
		}

		printk(KERN_ALERT "***** Create Dtvlog file to %s directory *****\n",
				dtv_filepath);
		create_serial_log(dtv_filepath);
#endif

#endif
		if (core_dumped) {
			retval = vfs_fsync(cprm.file, 0);
			if (retval < 0)
				pr_alert("[COREDUMP_WARNING] CoreDump can be corrupted , Sync fail [error=%d]\n", retval);

			pr_alert("***** CoreDump: finished dumping core for %s(%d)\n",
					current->comm, current->pid);
		}
		else
			pr_alert("[COREDUMP_FAIL]: finished dumping core with ERRROR\n");
	}
	if (ispipe && core_pipe_limit)
		wait_for_dump_helpers(cprm.file);
close_fail:
	if (cprm.file)
		filp_close(cprm.file, NULL);
fail_dropcount:
	if (ispipe)
		atomic_dec(&core_dump_count);
fail_unlock:
	kfree(cn.corename);
fail_unlock_nonfree:
#endif
	coredump_finish(mm, core_dumped);
	revert_creds(old_cred);
fail_creds:
	put_cred(cred);
fail:
	return;
}

/*
 * Core dumping helper functions.  These are the only things you should
 * do on a core-file: use only these functions to write out all the
 * necessary info.
 */
int dump_emit(struct coredump_params *cprm, const void *addr, int nr)
{
	struct file *file = cprm->file;
	loff_t pos = file->f_pos;
	ssize_t n;

	if (cprm->written + nr > cprm->limit) {
			pr_alert("%s failed as cprm->written(%llu) + nr(%d) > cprm->limit(%lu)",
				__func__, cprm->written, nr, cprm->limit);
		return 0;
	}

	while (nr) {
		if (dump_interrupted()) {
			pr_alert("%s failed as dump_interrupted due to pending signal",
					__func__);
			return 0;
		}
		n = __kernel_write(file, addr, nr, &pos);
		if (n <= 0) {
			pr_alert("%s failed as __kernel_write failure:%d",
				__func__, n);
			return 0;
		}
		file->f_pos = pos;
		cprm->written += n;
		nr -= n;
	}
	return 1;
}
EXPORT_SYMBOL(dump_emit);

int dump_skip(struct coredump_params *cprm, size_t nr)
{
	static char zeroes[PAGE_SIZE];
	struct file *file = cprm->file;
	if (file->f_op->llseek && file->f_op->llseek != no_llseek) {
		if (cprm->written + nr > cprm->limit)
			return 0;
		if (dump_interrupted() ||
		    file->f_op->llseek(file, nr, SEEK_CUR) < 0)
			return 0;
		cprm->written += nr;
		return 1;
	} else {
		while (nr > PAGE_SIZE) {
			if (!dump_emit(cprm, zeroes, PAGE_SIZE))
				return 0;
			nr -= PAGE_SIZE;
		}
		return dump_emit(cprm, zeroes, nr);
	}
}
EXPORT_SYMBOL(dump_skip);

int dump_align(struct coredump_params *cprm, int align)
{
	unsigned mod = cprm->written & (align - 1);
	if (align & (align - 1))
		return 0;
	return mod ? dump_skip(cprm, align - mod) : 1;
}
EXPORT_SYMBOL(dump_align);
