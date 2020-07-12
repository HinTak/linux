/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ij.jang@samsung.com: Samsung sdp external timer support.
 * 
 * always 1/4 mux, 64bit mode 
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>
#include <linux/smp.h>
#include <linux/cpu_pm.h>
#include <linux/cpu.h>

#include <asm/delay.h>

#include "sdp-timer-regs.h"

#define tmr_debug(fmt, ...)
//#define tmr_debug(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#define SDP_MAX_TIMERS	(16)

#define CLOCKEVENT_PRESCALER	(1)
#define CLOCKSOURCE_PRESCALER	(1)

#define CLOCKEVENT_FREQ(tmrfreq)	(tmrfreq / 4 / CLOCKEVENT_PRESCALER)
#define CLOCKSOURCE_FREQ(tmrfreq)	(tmrfreq / 4 / CLOCKSOURCE_PRESCALER)

#define CLOCKEVENT_TMCON	(SDP_TMCON_DOWN | SDP_TMCON_MUX4 | SDP_TMCON_IRQ_ENABLE)
#define CLOCKSOURCE_TMCON	(SDP_TMCON_UP | SDP_TMCON_MUX4)

static int clksource_rating = 300;
static int clkevent_rating =  400;
static int clkevent_global_rating =  400;
static int register_sched_clock = 1;
static int register_delay_timer = 1;

struct sdp_timer {
	struct clk	*clk;
	void __iomem	*regs;
	int		clocksource_id;
	int		clockevent_id;	
	unsigned long	saved_clock_rate;
};

struct sdp_clock_event_dev {
	struct clock_event_device	evt;

	u32			irq;
	int			cpu;
	int			timer_id;
	char			name[16];
	int			oneshot;
};

static struct sdp_timer sdp_timer __read_mostly;

extern void sdp_init_clocks(void);

static void sdp_timer_clear_pending(int id)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int val;

	val = readl(regs + SDP_TMCONE(id));
	writel(val, regs + SDP_TMCONE(id));
}

static int sdp_timer_pending(int id)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int val;

	val = readl(regs + SDP_TMCONE(id)) & SDP_TMCONE_IRQMASK;
	return !!val;
}

static void sdp_timer_stop(int id)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int val;

	val = readl(regs + SDP_TMCON(id));
	writel(val & (~SDP_TMCON_RUN), regs + SDP_TMCON(id));
	readl(regs + SDP_TMCON(id));	/* flush */
}

static void sdp_timer_program(int id, u64 cycles)
{
	unsigned int val;
	void __iomem *regs = sdp_timer.regs;
	writel((u32)cycles, regs + SDP_TMDATA64L(id));
	writel((u32)(cycles >> 32), regs + SDP_TMDATA64H(id));
	
	val = readl(regs + SDP_TMCON(id));
	writel(val | SDP_TMCON_RUN, regs + SDP_TMCON(id));
	readl(regs + SDP_TMCON(id));	/* flush */
}

static void sdp_timer_setup(int id, u32 tmcon, int prescaler, int oneshot)
{
	void __iomem *regs = sdp_timer.regs;
	u32 val;

	BUG_ON(tmcon & SDP_TMCON_RUN);
	writel(tmcon, regs + SDP_TMCON(id));
	readl(regs + SDP_TMCON(id));	/* flush */
	
	val = SDP_TMCONE_IRQMASK | SDP_TMCONE_LEVEL | (oneshot ? SDP_TMCONE_ONESHOT : 0);
	writel(val, regs + SDP_TMCONE(id));
	
	writel((((u32)prescaler - 1) & 0xff) << 16, regs + SDP_TMDATA(id));

	sdp_timer_clear_pending(id);
}

static void sdp_timer_halt(int id)
{
	sdp_timer_setup(id, 0, 0, 0);
}

/* clockevent */
#define to_sdp_evt(evt)	((struct sdp_clock_event_dev *)evt)
static int sdp_set_periodic(struct clock_event_device *dev)
{
	struct sdp_clock_event_dev *sdp_evt = to_sdp_evt(dev);
	
	sdp_evt->oneshot = 0;

	sdp_timer_setup(sdp_evt->timer_id, CLOCKEVENT_TMCON, CLOCKEVENT_PRESCALER, 0);
	sdp_timer_program(sdp_evt->timer_id, CLOCKEVENT_FREQ(sdp_timer.saved_clock_rate) / HZ);

	tmr_debug("%s: periodic\n", sdp_evt->name);
	return 0;
}

static int sdp_set_oneshot(struct clock_event_device *dev)
{
	struct sdp_clock_event_dev *sdp_evt = to_sdp_evt(dev);
	
	sdp_timer_setup(sdp_evt->timer_id, CLOCKEVENT_TMCON, CLOCKEVENT_PRESCALER, 1);
	sdp_evt->oneshot = 1;

	tmr_debug("%s: shutdown\n", sdp_evt->name);
	return 0;

}

static int sdp_set_shutdown(struct clock_event_device *dev)
{
	struct sdp_clock_event_dev *sdp_evt = to_sdp_evt(dev);
	
	sdp_timer_halt(sdp_evt->timer_id);

	tmr_debug("%s: shutdown\n", sdp_evt->name);
	return 0;
}

static int sdp_set_next_event(unsigned long evt, struct clock_event_device *dev)
{
	struct sdp_clock_event_dev *sdp_evt = to_sdp_evt(dev);
	       
	sdp_timer_stop(sdp_evt->timer_id);	/* XXX: do not remove this! */
	sdp_timer_program(sdp_evt->timer_id, evt);

	tmr_debug("%s: next_event after %lu cycles (%llu nsec)\n", sdp_evt->name, evt,
			(unsigned long long)clockevent_delta2ns(evt, dev));

	return 0;
}

static irqreturn_t sdp_clock_event_isr(int irq, void *dev_id)
{
	struct sdp_clock_event_dev *sdp_evt = dev_id;
	BUG_ON(sdp_evt->irq != irq);

	if (!sdp_timer_pending(sdp_evt->timer_id))
		return IRQ_NONE;

	sdp_timer_clear_pending(sdp_evt->timer_id);
	
	tmr_debug("%s isr: handler=%pF\n", sdp_evt->name, sdp_evt->evt.event_handler);

	if(sdp_evt->evt.event_handler)
		sdp_evt->evt.event_handler(&sdp_evt->evt);
	
	return IRQ_HANDLED;
}

/* calcuate mult value by given shift value */
static void sdp_clockevent_calc_mult(struct clock_event_device *evt, unsigned long rate)
{
	u32 min_ns = (NSEC_PER_SEC / rate + 1);

	clockevents_calc_mult_shift(evt, rate, min_ns);

	evt->max_delta_ns = clockevent_delta2ns(-1, evt);
	evt->min_delta_ns = min_ns;
}

static DEFINE_PER_CPU(struct sdp_clock_event_dev, percpu_sdp_timer);

static struct sdp_clock_event_dev sdp_clockevent_global = {
	.name		= "sdp_timer_global",
};


static int sdp_clockevent_init(struct sdp_clock_event_dev *sdp_evt)
{
	int ret;
	struct clock_event_device *evt = &sdp_evt->evt;

	evt->set_state_periodic = sdp_set_periodic;
	evt->set_state_oneshot = sdp_set_oneshot;
	evt->set_state_shutdown = sdp_set_shutdown ;

	evt->set_next_event = sdp_set_next_event;
	evt->rating = clkevent_rating;
	evt->name = sdp_evt->name;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;

	/* 1/4 mux */	
	sdp_clockevent_calc_mult(evt, CLOCKEVENT_FREQ(sdp_timer.saved_clock_rate));
	
	sdp_timer_halt(sdp_evt->timer_id);

	ret = request_irq(sdp_evt->irq, sdp_clock_event_isr,
				IRQF_TIMER | IRQF_NOBALANCING, sdp_evt->name, sdp_evt);
	if (ret) {
		pr_err("Failed to request_irq for %d(%s).\n", sdp_evt->irq, sdp_evt->name);
		return ret;
	}
	return 0;
}

static void sdp_clockevent_register_one(struct sdp_clock_event_dev *sdp_evt)
{
	sdp_timer_halt(sdp_evt->timer_id);
	
	if (sdp_evt->cpu >= 0) {
		irq_force_affinity(sdp_evt->irq, cpumask_of(sdp_evt->cpu));
		sdp_evt->evt.cpumask = cpumask_of(sdp_evt->cpu);
	} else {
		sdp_evt->evt.cpumask = cpu_possible_mask;
	}

	clockevents_register_device(&sdp_evt->evt);
}

static void sdp_clockevent_stop(struct sdp_clock_event_dev *sdp_evt)
{
	sdp_timer_halt(sdp_evt->timer_id);
}

static int sdp_timer_cpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		sdp_clockevent_register_one(this_cpu_ptr(&percpu_sdp_timer));
		break;
	case CPU_DYING:
		sdp_clockevent_stop(this_cpu_ptr(&percpu_sdp_timer));
		break;
	}
	return 0;
}

static struct notifier_block sdp_timer_cpu_nb = {
	.notifier_call = sdp_timer_cpu_notify,
};

/* clock source */
static u64 sdp_clocksource_count(void)
{
	void __iomem *regs = sdp_timer.regs;
	u32 high0, high1, low;
	
	/* XXX: our HW supports readq() */
retry_read:
	high0 = readl_relaxed(regs + SDP_TMCNT64H(sdp_timer.clocksource_id));
	low = readl_relaxed(regs + SDP_TMCNT64L(sdp_timer.clocksource_id));
	high1 = readl_relaxed(regs + SDP_TMCNT64H(sdp_timer.clocksource_id));

	barrier();
	if (unlikely(high0 != high1))
		goto retry_read;
	
	return ((u64)high0 << 32) | low;
}

static cycle_t sdp_clocksource_read(struct clocksource *cs)
{
	return sdp_clocksource_count();
}

static u64 notrace sdp_sched_clock_read(void)
{
	return sdp_clocksource_count();
}

static unsigned long sdp_delay_counter_read(void)
{
	return (unsigned long)sdp_clocksource_count();
}

static void sdp_clocksource_enable(struct clocksource *cs)
{
	sdp_timer_setup(sdp_timer.clocksource_id, CLOCKSOURCE_TMCON, CLOCKSOURCE_PRESCALER, 0);
	sdp_timer_program(sdp_timer.clocksource_id, 0ULL);
}

static void __ref sdp_clocksource_suspend(struct clocksource *cs)
{
}

static void __ref sdp_clocksource_resume(struct clocksource *cs)
{
	sdp_clocksource_enable(cs);
}

struct clocksource sdp_clocksource = {
	.name		= "sdp_clocksource",
	.read		= sdp_clocksource_read,
	.suspend	= sdp_clocksource_suspend,
	.resume		= sdp_clocksource_resume,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct delay_timer sdp_delay_timer;

static void __init sdp_register_delay_timer(void)
{
	sdp_delay_timer.freq = CLOCKSOURCE_FREQ(sdp_timer.saved_clock_rate);
	sdp_delay_timer.read_current_timer = sdp_delay_counter_read;
	register_current_timer_delay(&sdp_delay_timer);
}

static void __init sdp_clocksource_init(void)
{
	const u32 clock_rate = CLOCKSOURCE_FREQ(sdp_timer.saved_clock_rate);

	sdp_clocksource.rating = clksource_rating;

	sdp_clocksource_enable(&sdp_clocksource);

	clocksource_register_hz(&sdp_clocksource, clock_rate);

	if (register_sched_clock)
		sched_clock_register(sdp_sched_clock_read, 64, clock_rate);
	
	if (register_delay_timer)
		sdp_register_delay_timer();
}

static void __init sdp_timer_of_init(struct device_node *np)
{
	struct property *prop;
	const __be32 *p;
	u32 timer_id, tmp, irq;
	int cpu;
	struct sdp_clock_event_dev *sdp_evt = NULL;
	const __be32 *localtimers;
	u32 nr_local_timers;

	sdp_timer.regs = of_iomap(np, 0);
	BUG_ON(!sdp_timer.regs);

	sdp_timer.clk = of_clk_get(np, 0);
	if (IS_ERR(sdp_timer.clk)) {
		pr_err("Failed to get clock\n");
		sdp_timer.clk = NULL;
	} else
		clk_prepare_enable(sdp_timer.clk);

	sdp_timer.saved_clock_rate = clk_get_rate(sdp_timer.clk);

	if (!of_property_read_u32(np, "clksource_rating", &tmp))
		clksource_rating = tmp;
	if (!of_property_read_u32(np, "localtimer_rating", &tmp))
		clkevent_rating = tmp;
	if (!of_property_read_u32(np, "clockevent_rating", &tmp))
		clkevent_global_rating = tmp;
	if (of_property_read_bool(np, "no-sched-clock"))
		register_sched_clock = 0;
	if (of_property_read_bool(np, "no-delay-timer"))
		register_delay_timer = 0;

	/* clock source */
	if (!of_property_read_u32(np, "clocksource_id", &sdp_timer.clocksource_id)) {
		sdp_clocksource_init();
		pr_info("sdp-timer: clocksource device registered. (%ldhz, rating=%d, %s)\n",
			CLOCKSOURCE_FREQ(sdp_timer.saved_clock_rate), clksource_rating,
			register_sched_clock ? "sched_clock" : "no-sched-clock");
	}

	/* per-cpu clock event */
	localtimers = of_get_property(np, "clockevent_ids", &nr_local_timers);
	if (localtimers != NULL) {
		nr_local_timers /= sizeof(*localtimers);
		if (nr_local_timers < num_possible_cpus())
			pr_err("sdp_timer: number of hw timers(%u) are not enough to run cpu timer!.\n",
				nr_local_timers);
		goto skip_cpu_timers;
	}

	cpu = 0;
	of_property_for_each_u32(np, "localtimer_ids", prop, p, timer_id) {
		struct sdp_clock_event_dev *this_evt = &per_cpu(percpu_sdp_timer, cpu);
		
		irq = irq_of_parse_and_map(np, (int) timer_id);
		if (!irq) {
			pr_err("sdp_timer: failed to get timer IRQ for cpu%d\n", cpu);
			goto skip_cpu_timers;
		}

		this_evt->irq = irq;
		this_evt->cpu = cpu;
		this_evt->timer_id = (int) timer_id;
		snprintf (this_evt->name, 16, "sdp_timer%d", cpu);	
	
		sdp_clockevent_init(this_evt);

		cpu++;
		if (cpu >= num_possible_cpus())
			break;
	}
	if (cpu == num_possible_cpus()) {
		/* enable this cpus's timer now */
		sdp_evt = this_cpu_ptr(&percpu_sdp_timer);
		sdp_clockevent_register_one(sdp_evt);

		register_cpu_notifier(&sdp_timer_cpu_nb);
		pr_info("sdp-timer: per-cpu timers registered. (%ldhz, rating=%d mult=%u shift=%u)\n",
			CLOCKEVENT_FREQ(sdp_timer.saved_clock_rate), sdp_evt->evt.rating,
			sdp_evt->evt.mult, sdp_evt->evt.shift);
	}

skip_cpu_timers:
	/* global clock event */
	if (!of_property_read_u32(np, "clockevent_id", &timer_id)) {
		irq = irq_of_parse_and_map(np, (int)timer_id);
		if (!irq) {
			pr_err("sdp_timer: failed to get global timer IRQ.\n");
		} else {
			sdp_evt = &sdp_clockevent_global;
			sdp_evt->irq = irq;
			sdp_evt->cpu = -1;
			sdp_evt->timer_id = timer_id;
			
			sdp_clockevent_init(sdp_evt);
			sdp_clockevent_register_one(sdp_evt);
		}
	}

	if (sdp_evt) {
		pr_info("sdp-timer: global timer registered. (%ldhz, rating=%d mult=%u shift=%u)\n",
			CLOCKEVENT_FREQ(sdp_timer.saved_clock_rate), sdp_evt->evt.rating,
			sdp_evt->evt.mult, sdp_evt->evt.shift);
	}
}

CLOCKSOURCE_OF_DECLARE(sdp_timer, "samsung,sdp-timer", sdp_timer_of_init);

