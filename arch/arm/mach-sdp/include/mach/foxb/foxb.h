/*
 *  arch/arm/mach-sdp/include/mach/victoria.h
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Author: sh.chon@samsung.com
 *
 */

#ifndef __FOXB_H
#define __FOXB_H

#include <mach/irqs.h>
#include <mach/sdp_soc.h>

/* clock parameters, see clocks.h */
/* XXX: move these out to sdpxxx_clocks.c */
//#define INPUT_FREQ		(24000000)
//#define FIN			(INPUT_FREQ)
/* Only using initialize kernel. XXX: let's remove this out */
//#define PCLK			(150000000)

/* system timer tick or high-resolution timer*/
//#define SYS_TICK		HZ

/* timer definition  */
#define SDP_SYS_TIMER		0
#define SDP_TIMER_IRQ           (IRQ_TIMER0)
#define CLKSRC_TIMER		1
#define SYS_TIMER_PRESCALER 	10 	// PCLK = 647 * 625 * 4 * 100 
//#define TIMER_CLOCK		(REQ_PCLK)
//#define SDP_GET_TIMERCLK(x)	sdp1207_get_clock(x)
//extern unsigned long sdp1207_get_timer_clkrate(void);
//#define SDP_GET_TIMERCLK(x)	sdp1207_get_timer_clkrate()

//extern unsigned long SDP_GET_TIMERCLK(char);
//#include <mach/memory.h>

#endif

