/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/user_namespace.h>
#include <linux/mount.h>
#include <linux/pid_namespace.h>
#include <linux/parser.h>

#include "internal.h"

#ifdef CONFIG_PER_PROCESS
#include <linux/dep_aslr_process_list.h>
#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
DEFINE_MUTEX(process_aslr_dep_lock);
LIST_HEAD(pax_process_list);
bool is_accessible = true;
static char dep_aslr_procfs_buffer[DEP_ASLR_PROCFS_MAX_SIZE];
struct proc_dir_entry *dep_aslr_proc_file;
#endif
#endif /*CONFIG_PER_PROCESS*/

static int proc_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}

static int proc_set_super(struct super_block *sb, void *data)
{
	int err = set_anon_super(sb, NULL);
	if (!err) {
		struct pid_namespace *ns = (struct pid_namespace *)data;
		sb->s_fs_info = get_pid_ns(ns);
	}
	return err;
}

enum {
	Opt_gid, Opt_hidepid, Opt_err,
};

static const match_table_t tokens = {
	{Opt_hidepid, "hidepid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_err, NULL},
};

static int proc_parse_options(char *options, struct pid_namespace *pid)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		args[0].to = args[0].from = NULL;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			pid->pid_gid = make_kgid(current_user_ns(), option);
			break;
		case Opt_hidepid:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0 || option > 2) {
				pr_err("proc: hidepid value must be between 0 and 2.\n");
				return 0;
			}
			pid->hide_pid = option;
			break;
		default:
			pr_err("proc: unrecognized mount option \"%s\" "
			       "or missing value\n", p);
			return 0;
		}
	}

	return 1;
}

int proc_remount(struct super_block *sb, int *flags, char *data)
{
	struct pid_namespace *pid = sb->s_fs_info;
	return !proc_parse_options(data, pid);
}

static struct dentry *proc_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	int err;
	struct super_block *sb;
	struct pid_namespace *ns;
	char *options;

	if (flags & MS_KERNMOUNT) {
		ns = (struct pid_namespace *)data;
		options = NULL;
	} else {
		ns = task_active_pid_ns(current);
		options = data;

		if (!current_user_ns()->may_mount_proc)
			return ERR_PTR(-EPERM);
	}

	sb = sget(fs_type, proc_test_super, proc_set_super, flags, ns);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (!proc_parse_options(options, ns)) {
		deactivate_locked_super(sb);
		return ERR_PTR(-EINVAL);
	}

	if (!sb->s_root) {
		err = proc_fill_super(sb);
		if (err) {
			deactivate_locked_super(sb);
			return ERR_PTR(err);
		}

		sb->s_flags |= MS_ACTIVE;
	}

	return dget(sb->s_root);
}

static void proc_kill_sb(struct super_block *sb)
{
	struct pid_namespace *ns;

	ns = (struct pid_namespace *)sb->s_fs_info;
	kill_anon_super(sb);
	put_pid_ns(ns);
}

static struct file_system_type proc_fs_type = {
	.name		= "proc",
	.mount		= proc_mount,
	.kill_sb	= proc_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
/**
 * is_duplicate_in_process_list - Check duplication of process name
 *                                in process list
 * @procname: Name of the process
 *
 * Returns linklist node on success or NULL otherwise
 */
static struct process_list *is_duplicate_in_process_list(const char *procname)
{
	struct process_list *plist;
	rcu_read_lock();
	list_for_each_entry_rcu(plist, &pax_process_list, list) {
			if (!strcmp(plist->process_name, procname)) {
				rcu_read_unlock();
				return plist;
			}
	}
	rcu_read_unlock();
	return NULL;
}

/**
 * add_process_list - this function creates a linked list of
 *                    processes and their aslr/dep enable/disable values after
 *                    getting values from /proc/dep_aslr_disable_process_list
 *                    interface
 * @procname: Name of the process
 * @aslr: aslr value for the process
 * @dep: dep value for the process
 *
 * Returns 0 on success or -ENOMEM otherwise
 */
static int add_process_list(char *procname, char *aslr, char *dep)
{
	struct process_list *plist;
	struct process_list *tmplist = NULL;
	int error = -ENOMEM;

	if (!strcmp(procname, "INTERFACE") &&
		!strcmp(aslr, "DONE") && !strcmp(dep, "DONE")) {
		mutex_lock(&process_aslr_dep_lock);
		is_accessible = false;
		mutex_unlock(&process_aslr_dep_lock);
		return 0;
	}

	plist = kzalloc(sizeof(struct process_list), GFP_KERNEL);
	if (plist) {
		error = 0;
		if (aslr && !strcmp(aslr, "NOASLR"))
			plist->aslr_enable = 0;
		else if (aslr && !strcmp(aslr, "ASLR"))
			plist->aslr_enable = 1;
		else
			goto out;

		if (dep && !strcmp(dep, "NODEP"))
			plist->dep_enable = 0;
		else if (dep && !strcmp(dep, "DEP"))
			plist->dep_enable = 1;
		else
			goto out;

		/*if ASLR and DEP both are enable then no need
		to add in process list*/
		if (!plist->aslr_enable && !plist->dep_enable)
			goto out;

		/*if process name is already there in process
		lits then no need to make duplicate entry in
		process list*/
		tmplist = is_duplicate_in_process_list(procname);
		if (!tmplist) {
			plist->process_name =
			(char *)kzalloc((strlen(procname)+1), GFP_KERNEL);
			if (plist->process_name)
				strncpy(plist->process_name, procname,
						strlen(procname) + 1);
			mutex_lock(&process_aslr_dep_lock);
			list_add_rcu(&(plist->list), &(pax_process_list));
			mutex_unlock(&process_aslr_dep_lock);
		} else {
			if ((tmplist->dep_enable == plist->dep_enable) &&
				(tmplist->aslr_enable == plist->aslr_enable))
				goto out; /*entry is duplicate*/

			if (tmplist->dep_enable != plist->dep_enable)
				tmplist->dep_enable =  plist->dep_enable;
			if (tmplist->aslr_enable != plist->aslr_enable)
				tmplist->aslr_enable =  plist->aslr_enable;

			kfree(plist);
		}

	}

	return error;
out:
	kfree(plist);
	return -EINVAL;
}

/**
 * dep_aslr_procfile_read - This function is used to read
 *                          process list from /proc/dep_aslr_dep_per_process
 * @buffer: buffer to copy data
 * @buffer_location: location in buffer
 * @offset: offset in buffer
 * @buffer_length: length of buffer
 * @eof: end of buffer
 * @data: data
 *
 * Returns 0 on success
 */
static int dep_aslr_procfile_read(char *buffer, char **buffer_location,
			 off_t offset, int buffer_length,
			 int *eof, void *data)
{
	struct process_list *plist;

	mutex_lock(&process_aslr_dep_lock);
	if (!is_accessible) {
		mutex_unlock(&process_aslr_dep_lock);
		return -EPERM;
	}
	mutex_unlock(&process_aslr_dep_lock);
	rcu_read_lock();
	list_for_each_entry_rcu(plist, &pax_process_list, list) {
			/*Just trying to align*/
			printk("%-100s  ", plist->process_name);
			if (plist->aslr_enable)
				printk("ASLR");
			else
				printk("NOASLR");

#ifdef CONFIG_PAX_MPROTECT
			if (plist->dep_enable)
				printk("    DEP\n");
			else
				printk("    NODEP\n");
#else
			printk("\n");
#endif
	}
	rcu_read_unlock();

	return 0;
}

/**
 * dep_aslr_procfile_write - this function provides write interface
 *                           for proc entry /proc/dep_aslr_disable_per_process
 * @file: file structure
 * @buffer: buffer to store data
 * @count: counter
 * @data: data to be used
 *
 * Return counter value on success or error otherwise
 */
static int dep_aslr_procfile_write(struct file *file, const char *buffer,
			 unsigned long count, void *data)
{
	char *procname;
	char *aslr;
	char *dep;
	int  error = -1;

	mutex_lock(&process_aslr_dep_lock);
	if (!is_accessible) {
		mutex_unlock(&process_aslr_dep_lock);
		return -EPERM;
	}
	mutex_unlock(&process_aslr_dep_lock);

	if (count >= DEP_ASLR_PROCFS_MAX_SIZE)
		return -EINVAL;

	/* get buffer size          */
	/* write data to the buffer */
	if (copy_from_user(dep_aslr_procfs_buffer, buffer, count))
		return -EFAULT;

	dep_aslr_procfs_buffer[count] = '\0';

	procname = kzalloc(count+1, GFP_KERNEL);
	if (procname == NULL)
		return -1;
	aslr = kzalloc(count, GFP_KERNEL);
	if (aslr == NULL)
		goto free_out_aslr;
	dep = kzalloc(count, GFP_KERNEL);
	if (dep == NULL)
		goto free_out_dep;

	/*due to this parsing it is compulsory to write entries in
	"/etc/process_list"  like this (otherwise theer will be problem
	in getting currect values)
	"process_name NOASLR  NODEP"=>for process name ASLR DEP
				      both are disabled
	"process_name NOASLR  DEP"=>for process name ASLR
				      is disabled and DEP is enabled
	"process_name ASLR    NODEP"=>for process name
				      ASLR is enabled and  DEP is disabled
	*/
	if (sscanf(dep_aslr_procfs_buffer, "%s %s %s",
					procname, aslr, dep) == 3)
		error = add_process_list(procname, aslr, dep);

	kfree(dep);
free_out_dep:
	kfree(aslr);
free_out_aslr:
	kfree(procname);

	if (error < 0)
		return error;
	return count;
}


/**
 * proc_grsecurity_per_process_init - function creats proc interface
 *                                    "/proc/dep_aslr_disable_process_list"
 *
 * Creates interface in proc file system on success.
 */
void proc_grsecurity_per_process_init(void)
{
	if (dep_aslr_proc_file == NULL)
		dep_aslr_proc_file = create_proc_entry(dep_aslr_procfs_name,
							0644, NULL);
	if (dep_aslr_proc_file) {
		dep_aslr_proc_file->read_proc = dep_aslr_procfile_read;
		dep_aslr_proc_file->write_proc = dep_aslr_procfile_write;
		dep_aslr_proc_file->mode      = S_IFREG | S_IRUGO;
		dep_aslr_proc_file->uid       = 0;
		dep_aslr_proc_file->gid       = 0;
		dep_aslr_proc_file->size      = 2056;
	}
}

#endif /*end config CONFIG_REGISTER_PROCESSLIST_BY_PROC*/

void __init proc_root_init(void)
{
	int err;

	proc_init_inodecache();
	err = register_filesystem(&proc_fs_type);
	if (err)
		return;

	proc_self_init();
	proc_symlink("mounts", NULL, "self/mounts");

	proc_net_init();

#ifdef CONFIG_SYSVIPC
	proc_mkdir("sysvipc", NULL);
#endif
	proc_mkdir("fs", NULL);
	proc_mkdir("driver", NULL);
	proc_mkdir("fs/nfsd", NULL); /* somewhere for the nfsd filesystem to be mounted */
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
	/* just give it a mountpoint */
	proc_mkdir("openprom", NULL);
#endif
	proc_tty_init();
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif
	proc_mkdir("bus", NULL);
	proc_sys_init();
#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
	/*Make /proc/interface to provide a list of process*/
	proc_grsecurity_per_process_init();
#endif
}

static int proc_root_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat
)
{
	generic_fillattr(dentry->d_inode, stat);
	stat->nlink = proc_root.nlink + nr_processes();
	return 0;
}

static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry, unsigned int flags)
{
	if (!proc_lookup(dir, dentry, flags))
		return NULL;
	
	return proc_pid_lookup(dir, dentry, flags);
}

static int proc_root_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos;
	int ret;

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}

	ret = proc_pid_readdir(filp, dirent, filldir);
	return ret;
}

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static const struct file_operations proc_root_operations = {
	.read		 = generic_read_dir,
	.readdir	 = proc_root_readdir,
	.llseek		= default_llseek,
};

/*
 * proc root can do almost nothing..
 */
static const struct inode_operations proc_root_inode_operations = {
	.lookup		= proc_root_lookup,
	.getattr	= proc_root_getattr,
};

/*
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	.low_ino	= PROC_ROOT_INO, 
	.namelen	= 5, 
	.mode		= S_IFDIR | S_IRUGO | S_IXUGO, 
	.nlink		= 2, 
	.count		= ATOMIC_INIT(1),
	.proc_iops	= &proc_root_inode_operations, 
	.proc_fops	= &proc_root_operations,
	.parent		= &proc_root,
	.name		= "/proc",
};

int pid_ns_prepare_proc(struct pid_namespace *ns)
{
	struct vfsmount *mnt;

	mnt = kern_mount_data(&proc_fs_type, ns);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	ns->proc_mnt = mnt;
	return 0;
}

void pid_ns_release_proc(struct pid_namespace *ns)
{
	kern_unmount(ns->proc_mnt);
}
