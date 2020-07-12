#ifndef _LINUX_VD_SIGNAL_POLICY_H
#define _LINUX_VD_SIGNAL_POLICY_H

/* check if task name entry is in /proc/vd_signal_policy_list */
extern int vd_signal_policy_check(const char *task_name);
/* Add Process name in /proc/vd_signal_policy_list */
extern ssize_t vd_signal_policy_add(struct file *seq,
				const char __user *data, size_t len, loff_t *ppos);
#endif
