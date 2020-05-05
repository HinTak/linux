#ifndef __CBR_HEADER_H_
#define __CBR_HEADER_H_

#include <linux/timer.h>

enum CBR_state {STOP, START, COMPLETE_BW, PARTIAL_BW, TIMER_STATE};


struct cbr_channel {


	spinlock_t		lock;
	/* CBR channel details */
	unsigned long		channel_num;
	unsigned long		bitrate;
	

	/* CBR Timing parameters */
	unsigned long		num_slots_per_sec;
	unsigned long		slot_interval;
	unsigned long		data_size_per_slot;

	unsigned long		old_timestamp;
	unsigned long		next_timestamp;
	unsigned long		curent_timestamp;
	unsigned long 		slot_number;


	unsigned long 		slot_start_jiffie;
	unsigned long 		slot_stop_jiffie;

	long			token;
	unsigned int		flip_flag;

	void			*private;

	/* Timer information */
	struct 	 timer_list	timer;	

	/* Buffer information */
	unsigned long 		buff_size;

	enum 	 CBR_state 	state; 
};

#endif /*__CBR_HEADER_H_*/









