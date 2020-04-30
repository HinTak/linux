#include <linux/time.h>
#include <linux/kernel.h>

#define RECORD_SIZE		(4096*4)
/*#define ENABLE_LET*/
/*typedef*/ struct logitem{
	char event_name[32];
	struct timespec event_time;
} /*logitem*/;
/*typedef*/ struct logtimediff{
	char event_name[64];
	int diff ;/*ns*/
} /*logtimediff*/;
void new_log(char event_name[32], struct timespec time);
void new_timedifflog(char event_name[64], int time);
void init_log(void);
void reset_log(void);
void init_timedifflog(void);
void reset_timedifflog(void);
void start_timediff(void);
void finish_timediff(void);

struct timespec start_ns_diff(void);
int calc_ns_diff(struct timespec start);
/*void get_log()*/
#ifdef ENABLE_LET
#define LOG_EVENT(EVENT)		\
{					\
	struct timespec	tevent;		\
	getrawmonotonic(&tevent);	\
	new_log(#EVENT, tevent);	\
}					\

#define LOG_EVENT_STR(STRING)		\
{					\
	struct timespec	tevent;		\
	getrawmonotonic(&tevent);	\
	new_log(STRING, tevent);	\
}					\

#define LEDS(STRING, diff)			\
{						\
	new_timedifflog(STRING, tevent);	\
}						\

#else
#define LOG_EVENT(EVENT)
#define LOG_EVENT_STR(STRING)
#define init_log()
#define init_timedifflog()
#endif


