/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/sched_clock.h>
#include <asm/arch_timer.h>
#include <asm/smp_twd.h>
#include <asm/localtimer.h>

#include <mach/map.h>
#include <mach/soc.h>
#include <mach/regs-timer.h>

#define tmr_debug(fmt, ...)
//#define tmr_debug(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)

static int clocksource_id;
static int clockevent_id;
static u32 int_stretch;

static void __iomem *regs;
static struct clk *clk;

enum sdp_timer_type {
	timer_type_sdp,
	timer_type_sdp1302mpw,
};

struct sdp_clock_event_dev {
	u32 irq;
	int irq_affinity;	/* cpu id, -1 for no affinity */
	int timer_id;
	char name[16];
	unsigned long pclk_rate;
	unsigned long clk_rate;
	spinlock_t lock;
	
	struct clock_event_device *evt;
	struct irqaction irqaction;
	enum clock_event_mode mode;
};

extern void sdp_init_clocks(void);

/* FIXME: temporary support sdp1302 mpw */
static void sdp_timer_clear_pending_normal(int id)
{
	unsigned int val;

	val = readl(regs + SDP_TMCONE(id));
	writel(val, regs + SDP_TMCONE(id));
}

static int sdp_timer_pending_normal(int id)
{
	unsigned int val;

	val = readl(regs + SDP_TMCONE(id)) & SDP_TMCONE_IRQMASK;

	return !!val;
}

static void sdp_timer_clear_pending_mpw(int id)
{
	writel((1 << id), regs + SDP_TMCONE(0));
}

static int sdp_timer_pending_mpw(int id)
{
	unsigned int val = readl(regs + SDP_TMCONE(0)) & (1 << id);
	return !!val;
}

static void (*sdp_timer_clear_pending)(int) = sdp_timer_clear_pending_normal;
static int (*sdp_timer_pending)(int) = sdp_timer_pending_normal;

static void __cpuinit sdp_timer_reset(int id)
{
	writel(SDP_TMCON_STOP, regs + SDP_TMCON(id));
}

static void __cpuinit sdp_timer_setup(int id, unsigned int flags)
{
	writel(0x0, regs + SDP_TMDATA64L(id));
	writel(0x0, regs + SDP_TMDATA64H(id));
	writel(flags, regs + SDP_TMCON(id));
	
	/* stretch interrupt line */
	if (unlikely(soc_is_sdp1302mpw())) {
		u32 val = readl(regs + SDP_TMCONE(1));
		val |= (int_stretch) << (id * 4);
		writel(val, regs + SDP_TMCONE(1));
	} else
		writel(int_stretch, regs + SDP_TMCONE(id));
}

/* clockevent */
static void _sdp_set_mode(struct sdp_clock_event_dev *sdp_evt, enum clock_event_mode mode)
{
	unsigned int val;
	unsigned long clock_rate = sdp_evt->clk_rate;
	
	sdp_evt->mode = mode;

	spin_lock(&sdp_evt->lock);	/* this function always runs in irq mode */

	val = readl(regs + SDP_TMCON(sdp_evt->timer_id));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		writel(clock_rate / HZ, regs + SDP_TMDATA64L(sdp_evt->timer_id));
		val |= SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN;
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		sdp_timer_clear_pending(sdp_evt->timer_id);
		val &= ~(SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN);
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		break;
	}
	
	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s: set_mode %d con %x\n", sdp_evt->name, mode, val);
}

static int _sdp_set_next_event(struct sdp_clock_event_dev *sdp_evt, unsigned long cycles)
{
	int id = sdp_evt->timer_id;
	unsigned int val;
	       
	spin_lock(&sdp_evt->lock);

	/* stop timer */
	val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
	writel(val & (~SDP_TMCON_RUN), regs + SDP_TMCON(id));

	/* re-program and enable timer */
	writel(cycles, regs + SDP_TMDATA64L(id));
	writel(val | SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN, regs + SDP_TMCON(id));
	readl(regs + SDP_TMCON(id));	/* flush */

	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s: next_event after %lu cycles (%llu nsec)\n", sdp_evt->name, cycles,
			(unsigned long long)clockevent_delta2ns(cycles, sdp_evt->evt));

	return 0;
}

static irqreturn_t sdp_clock_event_isr(int irq, void *dev_id)
{
	struct sdp_clock_event_dev *sdp_evt = dev_id;
	BUG_ON(sdp_evt->irq != irq);

	if (!sdp_timer_pending(sdp_evt->timer_id))
		return IRQ_NONE;

	spin_lock(&sdp_evt->lock);

	if (sdp_evt->mode != CLOCK_EVT_MODE_PERIODIC) {
		u32 val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
		val &= ~(SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN);
		
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		readl(regs + SDP_TMCON(sdp_evt->timer_id));
	}
	
	sdp_timer_clear_pending(sdp_evt->timer_id);
	
	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s isr: handler=%pF\n", sdp_evt->name, sdp_evt->evt->event_handler);
	
	sdp_evt->evt->event_handler(sdp_evt->evt);
	
	return IRQ_HANDLED;
}

static void __ref _sdp_clockevent_suspend(struct sdp_clock_event_dev *sdp_evt)
{
	pr_info("%s: clockevent device suspend.\n", sdp_evt->name);
}

static void __ref _sdp_clockevent_resume(struct sdp_clock_event_dev *sdp_evt)
{
	const u32 tmcon = SDP_TMCON_64BIT_DOWN | SDP_TMCON_MUX4;
	
	pr_info("%s: clockevent device resume.\n", sdp_evt->name);

	sdp_timer_clear_pending(sdp_evt->timer_id);
	sdp_timer_reset(sdp_evt->timer_id);

	writel(SDP_TMDATA_PRESCALING(1), regs + SDP_TMDATA(sdp_evt->timer_id));

	{
		/* TODO: check IRQ affinity after resume  */
		struct irq_desc *desc = irq_to_desc(sdp_evt->irq);
		const struct cpumask *mask = desc->irq_data.affinity;
		pr_info("%s: irq affinity = 0x%x\n", sdp_evt->name, *(u32*)mask);
	}

	sdp_timer_setup(sdp_evt->timer_id, tmcon);
}

/* calcuate mult value by given shift value */
static void __cpuinit sdp_clockevent_calc_mult(struct clock_event_device *evt, unsigned long rate)
{
	evt->mult = div_sc(rate, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(-1, evt);
	evt->min_delta_ns = clockevent_delta2ns((int_stretch + 1) * 2, evt);
}

static void __cpuinit _sdp_clockevent_init(struct sdp_clock_event_dev *sdp_evt)
{
	/* prescaler  = 2 * 4 */
	const unsigned long clock_rate = sdp_evt->pclk_rate / 8;
	struct irqaction *irqaction = &sdp_evt->irqaction;
	const u32 tmcon = SDP_TMCON_64BIT_DOWN | SDP_TMCON_MUX4;

	spin_lock_init(&sdp_evt->lock);

	sdp_evt->clk_rate = clock_rate;
	sdp_timer_clear_pending(sdp_evt->timer_id);
	sdp_timer_reset(sdp_evt->timer_id);

	writel(SDP_TMDATA_PRESCALING(1), regs + SDP_TMDATA(sdp_evt->timer_id));

	sdp_clockevent_calc_mult(sdp_evt->evt, clock_rate);

	memset(irqaction, 0, sizeof(*irqaction));	
	irqaction->name = sdp_evt->name;
	irqaction->flags = IRQF_TIMER | IRQF_NOBALANCING | IRQF_TRIGGER_RISING;
	irqaction->handler = sdp_clock_event_isr;
	irqaction->dev_id = sdp_evt;
	
	pr_info("%s: clockevent device using timer%d, irq%d.\n",
			sdp_evt->name, sdp_evt->timer_id, sdp_evt->irq);
	pr_info("\trate=%ldhz mult=%u shift=%u stretch=0x%x min/max_delt=%llu/%llu\n",
			clock_rate, sdp_evt->evt->mult, sdp_evt->evt->shift, int_stretch,
			(unsigned long long)sdp_evt->evt->min_delta_ns,
			(unsigned long long)sdp_evt->evt->max_delta_ns);
	
	setup_irq(sdp_evt->irq, irqaction);

	if (sdp_evt->irq_affinity >= 0)
		irq_set_affinity(sdp_evt->irq, cpumask_of(sdp_evt->irq_affinity));
	
	sdp_timer_setup(sdp_evt->timer_id, tmcon);

	clockevents_register_device(sdp_evt->evt);
}

/* global clock event device */
static struct sdp_clock_event_dev sdp_clockevent = {
	.name		= "sdp_event_timer",
};

static void sdp_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_set_mode(&sdp_clockevent, mode);
}

static int sdp_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	return _sdp_set_next_event(&sdp_clockevent, cycles);
}

static void sdp_clockevent_suspend(struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_clockevent_suspend(&sdp_clockevent);
}

static void sdp_clockevent_resume(struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_clockevent_resume(&sdp_clockevent);
}

static void __init sdp_clockevent_init(int irq, int timer_id)
{
	struct clock_event_device *evt;
       
	evt = kzalloc(sizeof(*sdp_clockevent.evt), GFP_ATOMIC);
	BUG_ON(!evt);

	sdp_clockevent.irq = irq;
	sdp_clockevent.irq_affinity = -1;
	sdp_clockevent.timer_id = timer_id;
	sdp_clockevent.pclk_rate = clk_get_rate(clk);
	sdp_clockevent.evt = evt;

	evt->set_mode = sdp_set_mode;
	evt->set_next_event = sdp_set_next_event;
	evt->cpumask = cpumask_of(0);
	evt->rating = 300;
	evt->shift = 32;
	evt->name = sdp_clockevent.name;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->suspend = sdp_clockevent_suspend;
	evt->resume = sdp_clockevent_resume;

	_sdp_clockevent_init(&sdp_clockevent);
}

/* clock source */
static inline u32 sdp_clocksource_count(void)
{
	return readl(regs + SDP_TMCNT64L(clocksource_id));
}

static cycle_t sdp_clocksource_read(struct clocksource *cs)
{
	return sdp_clocksource_count();
}

static u32 notrace sdp_sched_clock_read(void)
{
	return sdp_clocksource_count();
}

static void __ref sdp_clocksource_suspend(struct clocksource *cs)
{
}

static void __ref sdp_clocksource_resume(struct clocksource *cs)
{
	unsigned int flags = SDP_TMCON_64BIT_UP | SDP_TMCON_MUX4 |
				SDP_TMCON_RUN;
	sdp_timer_reset(clocksource_id);
	writel(SDP_TMDATA_PRESCALING(9), regs + SDP_TMDATA(clocksource_id));
	sdp_timer_setup(clocksource_id, flags);
}

struct clocksource sdp_clocksource = {
	.name		= "sdp_clocksource",
	.rating		= 300,
	.read		= sdp_clocksource_read,
	.suspend	= sdp_clocksource_suspend,
	.resume		= sdp_clocksource_resume,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 19,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init sdp_clocksource_init(void)
{
	unsigned long clock_rate = clk_get_rate(clk) / 4;
	unsigned int flags = SDP_TMCON_64BIT_UP | SDP_TMCON_MUX4 |
				SDP_TMCON_RUN;

	sdp_timer_reset(clocksource_id);

	clock_rate /= 10;	/* div 10 */

	writel(SDP_TMDATA_PRESCALING(9), regs + SDP_TMDATA(clocksource_id));

	sdp_clocksource.mult =
		clocksource_hz2mult(clock_rate, sdp_clocksource.shift);

	pr_info("clocksource %ldHZ, mult=%u, shift=%u\n", clock_rate,
			sdp_clocksource.mult, sdp_clocksource.shift);

	clocksource_register(&sdp_clocksource);

	if(!(soc_is_sdp1202() || soc_is_sdp1304()))
		setup_sched_clock(sdp_sched_clock_read, 32, clock_rate);
	
	sdp_timer_setup(clocksource_id, flags);
}

static const struct of_device_id sdp_timer_match[] __initconst = {
	{ .compatible = "samsung,sdp-timer" },
	{}
};

static void sdp_soc_timer_init(void)
{
	struct device_node *np;
	u32 stretch;
	u32 evt_irq;
	u32 timer_id;

	/* default ids */
	clockevent_id = 0;
	clocksource_id = 1;

	np = of_find_matching_node(NULL, sdp_timer_match);
	if (!np) {
		pr_err("Failed to find timer DT node\n");
		BUG();
	}

	if (!of_property_read_u32(np, "clockevent_id", &timer_id))
		clockevent_id = timer_id;
	if (!of_property_read_u32(np, "clocksource_id", &timer_id))
		clocksource_id = timer_id;
	BUG_ON(clockevent_id == clocksource_id);

	regs = of_iomap(np, 0);
	if (!regs) {
		pr_err("Can't map timer registers");
		BUG();
	}

	evt_irq = irq_of_parse_and_map(np, 0);
	if (evt_irq == NO_IRQ) {
		pr_err("Failed to map timer IRQ\n");
		BUG();
	}

	/* FIXME: temporary hard coded */
	clk = clk_get(NULL, "apb_pclk");
	if (IS_ERR(clk)) {
		pr_err("Failed to get clock\n");
		BUG();
	}

	if (!of_property_read_u32(np, "int_stretch", &stretch)) {
		int_stretch = stretch & 0xf;
	}

	of_node_put(np);

	sdp_clocksource_init();
	sdp_clockevent_init(evt_irq, clockevent_id);
}

#if defined(CONFIG_LOCAL_TIMERS)
/* TODO: hotplug cpu */
static DEFINE_PER_CPU(struct sdp_clock_event_dev, percpu_sdp_tick);

static void sdp_tick_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	//BUG_ON(sdp_evt->evt != evt);
	_sdp_set_mode(sdp_evt, mode);
}

static int sdp_tick_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	//BUG_ON(sdp_evt->evt != evt);
	return _sdp_set_next_event(sdp_evt, cycles);
}

static void sdp_localtimer_suspend(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	BUG_ON(sdp_evt->evt != evt);
	return _sdp_clockevent_suspend(sdp_evt);
}

static void sdp_localtimer_resume(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	BUG_ON(sdp_evt->evt != evt);
	return _sdp_clockevent_resume(sdp_evt);
}

static int __cpuinit sdp_localtimer_setup(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	unsigned int cpu = smp_processor_id();

	sdp_evt->evt = evt;

	evt->set_mode = sdp_tick_set_mode;
	evt->set_next_event = sdp_tick_set_next_event;
	evt->cpumask = cpumask_of(cpu);
	evt->rating = 400;
	evt->shift = 32;
	evt->name = sdp_evt->name;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->suspend = sdp_localtimer_suspend;
	evt->resume = sdp_localtimer_resume;

	_sdp_clockevent_init(sdp_evt);
	
	return 0;
}

static void sdp_localtimer_stop(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);

	BUG_ON(sdp_evt->evt != evt);
	sdp_evt->evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	remove_irq(sdp_evt->irq, &sdp_evt->irqaction);
	pr_info ("%s: stopped.\n", sdp_evt->name);
}

static struct local_timer_ops sdp_lt_ops __cpuinitdata = {
	.setup	= sdp_localtimer_setup,
	.stop	= sdp_localtimer_stop,
};

static const struct of_device_id sdp_localtimer_match[] __initconst = {
	{ .compatible = "samsung,sdp-localtimer" },
	{}
};

static int __init sdp_localtimer_of_register(void)
{
	struct device_node *np;
	const __be32 *timer_ids;
	int timer_cnts;
	int i, ret;

	np = of_find_matching_node(NULL, sdp_localtimer_match);
	if (!np)
		return -ENODEV;

	timer_ids = of_get_property(np, "timer_ids", &timer_cnts);
	timer_cnts /= sizeof(*timer_ids);
	if (timer_cnts < NR_CPUS) {
		pr_err("Failed to get timer id for localtimer\n");
		ret = -EINVAL;
		goto err_sdp_localtimer_init;
	}
	timer_cnts = min(timer_cnts, NR_CPUS);

	for (i=0; i<timer_cnts; i++) {
		u32 irq = irq_of_parse_and_map(np, i);
		struct sdp_clock_event_dev *sdp_evt = &per_cpu(percpu_sdp_tick, i);
		
		if (irq <= 0) {
			pr_err("Failed to get timer IRQ for cpu%d\n", i);
			ret = -EINVAL;
			goto err_sdp_localtimer_init;
		}

		sdp_evt->irq = irq;
		sdp_evt->irq_affinity = i;
		sdp_evt->timer_id = (int)be32_to_cpu(timer_ids[i]);
		sdp_evt->pclk_rate = clk_get_rate(clk);
		sprintf (sdp_evt->name, "sdp_tick%d", i);	
		BUG_ON(sdp_evt->timer_id == clockevent_id ||
				sdp_evt->timer_id == clocksource_id);
		
		pr_info("%s: localtimer for cpu%d probed: timer%d irq%d\n",
				sdp_evt->name, i, sdp_evt->timer_id, irq);
	}

	ret = local_timer_register(&sdp_lt_ops);

err_sdp_localtimer_init:
	of_node_put(np);
	return ret;
}

#else	/* !CONFIG_LOCAL_TIMERS */
static int __init sdp_localtimer_of_register(void) {return 0;}
#endif

static void __init sdp_timer_init(void)
{
	sdp_init_clocks();

	/* sdp1302 mpw patch */
	if (soc_is_sdp1302mpw()) {
		pr_info("sdp1302 mpw timer support!\n");
		sdp_timer_clear_pending = sdp_timer_clear_pending_mpw;
		sdp_timer_pending = sdp_timer_pending_mpw;
	}

	/* local timer */
	if (soc_is_sdp1202() || soc_is_sdp1304()) {
		sdp_soc_timer_init();
		arch_timer_sched_clock_init();
		arch_timer_of_register();
	} else {
		sdp_soc_timer_init();
		if (sdp_localtimer_of_register())
			twd_local_timer_of_register();
	}
}

struct sys_timer sdp_timer = {
	.init = sdp_timer_init,
};
