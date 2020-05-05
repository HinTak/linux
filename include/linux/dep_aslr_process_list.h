#ifndef __DEP_ASLR_PROCESS_LIST_H__
#define __DEP_ASLR_PROCESS_LIST_H__

#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
#define dep_aslr_procfs_name "dep_aslr_disable_per_process"
#define DEP_ASLR_PROCFS_MAX_SIZE 220
#endif

struct process_list {
#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
	struct list_head      list;
#endif
	char *process_name;
	unsigned int  aslr_enable;
	unsigned int  dep_enable;
};

#ifdef CONFIG_REGISTER_PROCESSLIST_BY_PROC
extern struct list_head pax_process_list;
#endif
#endif /*__DEP_ASLR_PROCESS_LIST_H__*/

