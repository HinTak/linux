#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <linux/vd_suspend_policy.h>

static struct vd_policy_list_head *freeze_seq;

static const char *const freeze_sequence_task[] = {"", "zone-security-m"};

/*Get the first list_name entry in freeze sequence*/
struct vd_policy_list *get_freeze_seq_first_entry(void)
{
	struct vd_policy_list *list_name = freeze_seq->list_name;
	struct vd_policy_list *first_entry;

	down_write(&freeze_seq->vd_policy_list_sem);
	first_entry = list_first_entry_or_null(&list_name->list,
				struct vd_policy_list, list);
	up_write(&freeze_seq->vd_policy_list_sem);
	return first_entry;
}

/*Clean up of freeze sequence*/
static void vd_suspend_policy_clean_list(void)
{
	vd_policy_list_clean(freeze_seq);
}

/*Initialization function*/
static int vd_suspend_policy_init(void)
{
	if (vd_policy_head_init(&freeze_seq, freeze_sequence_task,
			sizeof(freeze_sequence_task)/sizeof(char *)))
		goto clean_up; /*fail to initialize*/

	return 0;

clean_up:
	pr_err("Fail to allocate space for /proc entry\n");
	return -ENOMEM;
}

/*Set value_len of entry 0 and get next entry*/
int vd_suspend_policy_get_next(struct vd_policy_list **curr)
{
	struct vd_policy_list *first_entry = get_freeze_seq_first_entry();
	struct vd_policy_list *entry = *curr;
	int ret = 0;

	if (!entry || !entry->value_len)
		return 0;

	down_write(&freeze_seq->vd_policy_list_sem);
	entry->value_len = 0;

	if (!(list_is_last(&entry->list, &first_entry->list))) {
		*curr = list_next_entry(entry, list);
		ret = 1;
	}
	up_write(&freeze_seq->vd_policy_list_sem);
	return ret;
}

/*Reset value_len of all entries in list to 1*/
void vd_suspend_policy_reset_value(void)
{
	struct vd_policy_list *entry = NULL;
	struct vd_policy_list *list_name = freeze_seq->list_name;

	down_write(&freeze_seq->vd_policy_list_sem);
	list_for_each_entry(entry, &list_name->list, list)
		entry->value_len = 1;
	up_write(&freeze_seq->vd_policy_list_sem);
}

/*Display the list of task name in the task list*/
static int vd_suspend_policy_allow_task_read(struct seq_file *m, void *v)
{
	vd_policy_list_read(freeze_seq, m, v);
	return 0;
}

/*If task is present return success (1) otherwise return 0*/
int vd_suspend_policy_check(const char *task_name,
			struct vd_policy_list *entry)
{
	struct vd_policy_list *list_name = freeze_seq->list_name;

	if (!entry || !entry->value_len)
		return 0;

	down_read(&freeze_seq->vd_policy_list_sem);
	list_for_each_entry_from(entry, &list_name->list, list) {
		if (!strncmp(entry->value_name, task_name, TASK_COMM_LEN)) {
			up_read(&freeze_seq->vd_policy_list_sem);
			return 1;
		}
	}
	up_read(&freeze_seq->vd_policy_list_sem);
	return 0;
}

/*Add task name in freeze sequence if it is not there already*/
int vd_suspend_policy_add_task(const char *task_name)
{
	struct vd_policy_list *list_name = freeze_seq->list_name;
	struct vd_policy_list *tmp, *entry;
	struct list_head *ptr;

	if (check_freezing())
		goto fail_suspend;

	tmp = kzalloc(sizeof(struct vd_policy_list), GFP_KERNEL);
	if (unlikely(tmp == NULL))
		goto fail_no_mem;

	strncpy(tmp->value_name, task_name, TASK_COMM_LEN-1);
	tmp->value_len = 1;

	down_write(&freeze_seq->vd_policy_list_sem);
	list_for_each(ptr, &list_name->list) {
		entry = list_entry(ptr, struct vd_policy_list, list);
		if (!strncmp(entry->value_name, tmp->value_name,
				TASK_COMM_LEN)) {
			up_write(&freeze_seq->vd_policy_list_sem);
			kfree(tmp);
			return 0;
		}
	}
	list_add(&tmp->list, &list_name->list);
	up_write(&freeze_seq->vd_policy_list_sem);
	return 0;

fail_suspend:
	pr_err("System suspending, failed to add task in freeze sequence\n");
	return -EPERM;

fail_no_mem:
	pr_err("Fail to allocate space for task in freeze sequence\n");
	return -ENOMEM;
}

/*Remove task name from freeze sequence if it is there*/
void vd_suspend_policy_remove_task(const char *task_name)
{
	struct vd_policy_list *list_name = freeze_seq->list_name;
	struct vd_policy_list *entry;
	struct list_head *ptr;

	down_write(&freeze_seq->vd_policy_list_sem);
	list_for_each(ptr, &list_name->list) {
		entry = list_entry(ptr, struct vd_policy_list, list);
		if (!strncmp(entry->value_name, task_name, TASK_COMM_LEN)) {
			list_del(&entry->list);
			kfree(entry);
			break;
		}
	}
	up_write(&freeze_seq->vd_policy_list_sem);
}

/*Add task name in list*/
ssize_t vd_suspend_policy_add_proc(struct file *seq,
		const char __user *data, size_t len, loff_t *ppos)
{
	struct vd_policy_list *list_name = freeze_seq->list_name;
	struct vd_policy_list *first_entry;
	struct task_struct *g, *p;
	int ret;

	ret = vd_policy_list_add(freeze_seq, seq, data, len, ppos);

	if (ret < 0)
		goto fail_add;

	down_read(&freeze_seq->vd_policy_list_sem);
	first_entry = list_first_entry(&list_name->list,
				struct vd_policy_list, list);
	up_read(&freeze_seq->vd_policy_list_sem);

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		if (!strncmp(p->comm, first_entry->value_name, TASK_COMM_LEN)) {
			p->suspend_last = 1;
			break;
		}
	}
	read_unlock(&tasklist_lock);
	return ret;

fail_add:
	pr_err("Failed to add task in freeze_sequence\n");
	return ret;
}

static int vd_suspend_policy_open(struct inode *inode, struct file *file)
{
	return single_open(file, vd_suspend_policy_allow_task_read, NULL);
}

static const struct file_operations vd_suspend_policy_fops = {
	.open		= vd_suspend_policy_open,
	.read		= seq_read,
	.write		= vd_suspend_policy_add_proc,
	.release	= single_release
};

static int __init proc_vd_suspend_policy_init(void)
{
	struct proc_dir_entry *proc_file_entry;

	if (!vd_suspend_policy_init()) {
		proc_file_entry = proc_create("vd_suspend_policy_list", 0, NULL,
					&vd_suspend_policy_fops);
		if (proc_file_entry == NULL)
			return -ENOMEM;

		return 0;
	} else
		return -ENOMEM;
}

static void __exit proc_vd_suspend_policy_exit(void)
{
	vd_suspend_policy_clean_list();
	remove_proc_entry("vd_suspend_policy_list", NULL);
}

module_init(proc_vd_suspend_policy_init);
module_exit(proc_vd_suspend_policy_exit);
