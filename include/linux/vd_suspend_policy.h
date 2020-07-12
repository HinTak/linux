#ifndef _LINUX_VD_SUSPEND_POLICY_H
#define _LINUX_VD_SUSPEND_POLICY_H

#include <../../fs/proc/vd_policy.h>

/*Get the first list_name entry in freeze sequence*/
struct vd_policy_list *get_freeze_seq_first_entry(void);

/*Set value_len of entry 0 and get next entry*/
int vd_suspend_policy_get_next(struct vd_policy_list **);

/*Reset value_len of all entries in list to 1*/
void vd_suspend_policy_reset_value(void);

/*If task is present return success (1) otherwise return 0*/
int vd_suspend_policy_check(const char *, struct vd_policy_list *);

/*Add task name in freeze sequence if it is not there already*/
int vd_suspend_policy_add_task(const char *);

/*Remove task name from freeze sequence if it is there*/
void vd_suspend_policy_remove_task(const char *);
#endif
