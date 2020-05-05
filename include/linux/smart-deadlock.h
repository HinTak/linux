
struct smart_deadlock_task
{
    int w_syscall_time;
    void *tsk;
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
	void *additional_info; 
	struct list_head list;
};

struct smart_deadlock_work_tsk {
    struct smart_deadlock_service *service;
};

#ifdef CONFIG_SMART_DEADLOCK_PROFILE_MODE
struct smart_deadlock_function_point
{
	void (*deadlock_set_bt)(char *buf, int size, int type);
	int (*deadlock_check_worker)(void);
};
#endif

void hook_smart_deadlock_exception_case(int type);

enum smart_deadlock_service_status {
	STATUS_SMART_DEADLOCK_DEADLOCK = 0,
	STATUS_SMART_DEADLOCK_NEED_TO_FREE,
	STATUS_SMART_DEADLOCK_ANR_SERVICE,
	STATUS_SMART_DEADLOCK_PROCESS_SERVICE,
	STATUS_SMART_DEADLOCK_EXCEPTION,
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

