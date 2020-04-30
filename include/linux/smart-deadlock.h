#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE

#include <linux/poll.h>

enum smart_deadlock_profile_type {
	SMART_DEADLOCK_PROFILE_INVALID = 0,
	SMART_DEADLOCK_PROFILE_POLL,
	SMART_DEADLOCK_PROFILE_KERN_MUTEX,
	SMART_DEADLOCK_PROFILE_SELECT,
};

struct smart_deadlock_profile {
	int type;
	void *value;
	struct list_head list;
};

#define SMDL_FD_SIZE	20

struct smart_deadlock_fd_list {
	int size;
	int list[SMDL_FD_SIZE];
};

struct smart_deadlock_poll_select {
	struct smart_deadlock_fd_list fd;
	struct timespec t_expire;
};

struct smart_deadlock_kern_mutex {
	struct mutex *lock;
};

struct cmd_get_pc_sp {
	int    		pid;
	void    	*pc;
	void    	*sp;
};
#endif

extern bool smdl_is_lmf;
static inline bool smdl_lmf_status(void)
{
	return smdl_is_lmf;
}

struct service_stat {
	unsigned long idle_first[NR_CPUS];
	unsigned long cpu_data_first[NR_CPUS];
	u64 sum_exec_runtime;
	unsigned long nr_switches;
	unsigned long weight;
	unsigned long sig;
	unsigned long first_bt_time;
};

struct smart_deadlock_task
{
	unsigned long w_syscall_time;
#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
	spinlock_t lock;
	struct smart_deadlock_profile *profile;
#endif
	void *tsk;
	unsigned long nr_switches;
	u64 sum_exec_runtime;
};

struct smart_deadlock_service {
	struct list_head entry;
	char *service_name;
	char *service_binary;
	int service_pid;
	unsigned long int watchdog_time;
	unsigned long int notify_stamp;
	int c_item;
	int status;
	int backtrace_pid;
	int backtrace_count;
	int backtrace_interval;
	int peer_pid;
	struct service_stat *additional_info;
	struct mutex watchdog_lock;
	struct list_head list;
};

struct smart_deadlock_work_tsk {
    struct smart_deadlock_service *service;
};

struct smart_deadlock_meminfo {
	unsigned long memtotal;
	unsigned long memfree;
#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
	unsigned long memavailable;
#endif
	unsigned long hightotal;
	unsigned long highfree;
	unsigned long lowtotal;
	unsigned long lowfree;
	unsigned long swaptotal;
	unsigned long swapfree;
};

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
struct smart_deadlock_function_point
{
	void (*deadlock_set_bt)(char *buf, int size, int type);
	int (*deadlock_check_worker)(void);
	ssize_t (*deadlock_logger_write)(void *buf, int buf_size, int msg_type);
};
#endif

void hook_smart_deadlock_exception_case(int type);

enum smart_deadlock_service_status {
	STATUS_SMART_DEADLOCK_DEADLOCK = 0,
	STATUS_SMART_DEADLOCK_NEED_TO_FREE,
	STATUS_SMART_DEADLOCK_ANR_SERVICE,
	STATUS_SMART_DEADLOCK_PROCESS_SERVICE,
	STATUS_SMART_DEADLOCK_EXCEPTION,
	STATUS_SMART_DEADLOCK_BACKTRACE_ALL,
};

enum smart_deadlock_exception_case {
	SMART_DEADLOCK_LMF_START = 0,
	SMART_DEADLOCK_LMF_STOP,
	SMART_DEADLOCK_OOM,
	SMART_DEADLOCK_CRASH_START,
	SMART_DEADLOCK_CRASH_STOP,
	SMART_DEADLOCK_SUSPEND,
	SMART_DEADLOCK_RESUME,
};

#define SMART_DEADLOCK_CHECK_STATUS(s, value)   (s->status & (1 << value))
#define SMART_DEADLOCK_SET_STATUS(s, value)     (s->status = (s->status | (1 << value)))
#define SMART_DEADLOCK_CLEAR_STATUS(s, value)   (s->status = (s->status & ~(1 << value)))


#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE

void smart_deadlock_init_profile(int type, struct smart_deadlock_profile *profile, void *ptr);
void smart_deadlock_enter_profile(struct smart_deadlock_profile *profile);
void smart_deadlock_leave_profile(struct smart_deadlock_profile *profile);

#endif


