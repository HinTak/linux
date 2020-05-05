/*
 * arch/arm/plat-sdp/sdp_timer.c
 *
 * Copyright (C) 2010 Samsung Electronics.co
 * Author : tukho.kim@samsung.com
 *
 */
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <asm/mach/time.h>
#include <mach/platform.h>

#if !defined(SDP_SYS_TIMER)
#warning SDP_SYS_TIMER is not defined. Use timer0 for system timer.
#define SDP_SYS_TIMER		(0)
#define SDP_TIMER_IRQ		(IRQ_TIMER0)
#endif

#if !defined(SDP_TMR_TICK)
#define SDP_TMR_TICK		(HZ)
#else
#warning SDP_TMR_TICK is not HZ
#endif

#if !defined(SDP_TIMER_NR)
#define SDP_TIMER_NR		(8)
#endif

#if defined(VA_TIMER_BASE)
#define SDP_TIMER_BASE 		VA_TIMER_BASE
#else
# ifndef SDP_TIMER_BASE
# 	error	"SDP Timer base is not defined, Please check sdp platform header file" 
# endif 
#endif

struct sdptmr_base {
	#define TMCON_64BIT_DOWN	(0x0 << 5)
	#define TMCON_64BIT_UP		(0x1 << 5)

	#define TMCON_64BIT		(0x0 << 4)
	#define TMCON_16BIT		(0x1 << 4)

	#define TMCON_MUX04		(0x0 << 2)
	#define TMCON_MUX08		(0x1 << 2)
	#define TMCON_MUX16		(0x2 << 2)
	#define TMCON_MUX32		(0x3 << 2)

	#define TMCON_INT_DMA_EN	(0x1 << 1)
	#define TMCON_RUN		(0x1 << 0)
	u32	control1;
	u32	data;
	u32	count;
	#define TMCON2_INTSTATUS	(1 << 4)
	u32	control2;
};

struct sdptmr_data {
	u32	data_l;
	u32	data_h;
	u32	count_l;
	u32	count_h;
};

struct sdptmr_regs {
	struct sdptmr_base base[8];
	struct sdptmr_data data[8];
};

#define tmr_base_reg(n, reg)		(gp_sdp_timer->base[n].reg)
#define tmr_data_reg(n, reg)		(gp_sdp_timer->data[n].reg)

#define tmr_base_writel(n, v, reg)	writel(v, &tmr_base_reg(n, reg))
#define tmr_base_readl(n, reg)		readl(&tmr_base_reg(n, reg))
#define tmr_data_writel(n, v, reg)	writel(v, &tmr_data_reg(n, reg))
#define tmr_data_readl(n, reg)		readl(&tmr_data_reg(n, reg))

#define systmr_base_writel(v, reg)	tmr_base_writel(SDP_SYS_TIMER, v, reg)
#define systmr_base_readl(reg)		tmr_base_readl(SDP_SYS_TIMER, reg)
#define systmr_data_writel(v, reg)	tmr_data_writel(SDP_SYS_TIMER, v, reg)
#define systmr_data_readl(reg)		tmr_data_readl(SDP_SYS_TIMER, reg)

static struct sdptmr_regs* const gp_sdp_timer = 
			(struct sdptmr_regs *)SDP_TIMER_BASE;

#if defined(CONFIG_SDP1302_TYPE_MPW)
static int systmr_pending(void)
{
	return (tmr_base_readl(0, control2) & (1 << SDP_SYS_TIMER)) ? 1 : 0;
}
static void systmr_pending_clear(void)
{
	tmr_base_writel(0, (1 << SDP_SYS_TIMER), control2);
}
#else
static int systmr_pending(void)
{
	return (tmr_base_readl(SDP_SYS_TIMER, control2) & TMCON2_INTSTATUS) ? 1 : 0;
}
static void systmr_pending_clear(void)
{
	u32 tmp = tmr_base_readl(SDP_SYS_TIMER, control2);
	systmr_base_writel(tmp, control2);
}
#endif

enum clock_event_mode g_clkevt_mode = CLOCK_EVT_MODE_PERIODIC;

// resource 
static spinlock_t sdp_timer_lock;

static void sdp_clkevent_setmode(enum clock_event_mode mode,
				   struct clock_event_device *clk)
{
	u32 val = 0;
	g_clkevt_mode = mode;

	switch(mode){
		case(CLOCK_EVT_MODE_ONESHOT):
			val = TMCON_MUX04 | TMCON_INT_DMA_EN;
			//printk("[%s] oneshot mode\n", __FUNCTION__);
		case(CLOCK_EVT_MODE_PERIODIC):
			val = TMCON_MUX04 | TMCON_INT_DMA_EN | TMCON_RUN;
			//printk("[%s] periodic mode\n", __FUNCTION__);
			break;
        	case (CLOCK_EVT_MODE_UNUSED):
        	case (CLOCK_EVT_MODE_SHUTDOWN):
		default: 
			//printk("[%s] shutdown\n", __FUNCTION__);
			break;
	}
	systmr_base_writel(val, control1);
}

static int sdp_clkevent_nextevent(unsigned long evt,
				 struct clock_event_device *unused)
{
	systmr_base_writel(0, control1);
	systmr_data_writel(evt, data_l);
	systmr_base_writel(TMCON_MUX04 | TMCON_INT_DMA_EN | TMCON_RUN, control1);

//	printk("[%s] evt is %d\n", __FUNCTION__,(u32)evt);

	return 0;
}

struct clock_event_device sdp_clockevent = {
	.name		= "SDP Timer clock event",
	.rating		= 200,
	.shift		= 32,		// nanosecond shift 
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= sdp_clkevent_setmode,
	.set_next_event = sdp_clkevent_nextevent,
};

static irqreturn_t sdp_timer_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = &sdp_clockevent;

	if(systmr_pending()) {
		if (g_clkevt_mode == CLOCK_EVT_MODE_ONESHOT) 
			systmr_base_writel(TMCON_MUX04, control1);
	
		/* pending clear */
		systmr_pending_clear();
		
		evt->event_handler(evt);

		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static struct irqaction sdp_timer_event = {
	.name = "SDP Hrtimer interrupt handler",
#ifdef CONFIG_ARM_GIC
	.flags = IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
#else
	.flags = IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER,
#endif
	.handler = sdp_timer_isr,
};

static cycle_t sdp_clksrc_read (struct clocksource *cs)
{
	return tmr_data_readl(CLKSRC_TIMER, count_l);
}

struct clocksource sdp_clocksource = {
	.name 		= "SDP Timer clock source",
	.rating 	= 200,
	.read 		= sdp_clksrc_read,
	.mask 		= CLOCKSOURCE_MASK(32),
	.mult		= 0,
	.shift 		= 19,		
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init sdp_timer_clksrc_init(unsigned long timer_clock)
{
	unsigned long clksrc_clock;

	/* mux 5, divide 10 */
	clksrc_clock = (timer_clock >> 2) / 10;
	tmr_base_writel(CLKSRC_TIMER, 9 << 16, data);

	// clock source init -> clock source 
	sdp_clocksource.mult = clocksource_khz2mult(clksrc_clock / 1000, sdp_clocksource.shift);
	clocksource_register(&sdp_clocksource);

	tmr_data_writel(CLKSRC_TIMER, 0, data_l);
	tmr_data_writel(CLKSRC_TIMER, 0, data_h);
	tmr_base_writel(CLKSRC_TIMER,
			TMCON_64BIT_UP | TMCON_MUX04 | TMCON_RUN,
			control1);
	
	printk("[%s] HRTIMER Clock-source %d\n", __FUNCTION__,
			(u32)clksrc_clock);
}

/* TODO: setup_sched_clock explicity */
unsigned long long sdp_sched_clock(void)
{
	unsigned long long val;
	
	if(sdp_clocksource.mult == 0)
		return 0;
	
	val = tmr_data_readl(CLKSRC_TIMER, count_l);
	val = (val * sdp_clocksource.mult) >> sdp_clocksource.shift;

	return val;
}

static unsigned long __init sdp_get_timer_clkrate(void)
{
	unsigned long ret = 0;
	struct clk* clk = clk_get(NULL, "sdp_timer");

	if (clk)
		ret = clk_get_rate(clk);
	return ret;
}

/* Initialize Timer */
void __init sdp_timer_init(void)
{
	unsigned long timer_clock;
	unsigned long clkevt_clock;

	systmr_pending_clear();

	// Timer reset & stop
	systmr_base_writel(0, control1);
	systmr_base_writel(1, control1);

	// get Timer source clock 
	timer_clock = sdp_get_timer_clkrate();
	printk(KERN_INFO "HRTIMER: source clock is %u Hz, SYS tick: %d\n", 
			(unsigned int)timer_clock, SDP_TMR_TICK);

	// init lock 
	spin_lock_init(&sdp_timer_lock);

	sdp_timer_clksrc_init(timer_clock);

	systmr_base_writel(1 << 16, data);
	clkevt_clock = timer_clock >> 2;		// MUX04
	clkevt_clock = clkevt_clock >> 1; 	// divide 2
	systmr_data_writel(clkevt_clock / SDP_TMR_TICK, data_l);

	// register timer interrupt service routine
	setup_irq(SDP_TIMER_IRQ, &sdp_timer_event);

	// timer event init
	sdp_clockevent.mult = 
			div_sc(clkevt_clock, NSEC_PER_SEC, sdp_clockevent.shift);
	sdp_clockevent.max_delta_ns =
			clockevent_delta2ns(0xFFFFFFFF, &sdp_clockevent);
	sdp_clockevent.min_delta_ns =
			clockevent_delta2ns(0xf, &sdp_clockevent);

	sdp_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&sdp_clockevent);
	
	printk(KERN_INFO "SDP Timer initialized\n");
}

