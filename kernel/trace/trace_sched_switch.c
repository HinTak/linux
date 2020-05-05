/*
 * trace context switch
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <trace/events/sched.h>

#include "trace.h"

static int			sched_ref;
static DEFINE_MUTEX(sched_register_mutex);

static void
probe_sched_switch(void *ignore, struct task_struct *prev, struct task_struct *next)
{
	if (unlikely(!sched_ref))
		return;

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);
}

static void
probe_sched_wakeup(void *ignore, struct task_struct *wakee, int success)
{
	if (unlikely(!sched_ref))
		return;

	tracing_record_cmdline(current);
}

static int tracing_sched_register(void)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return ret;
	}

	ret = register_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_sched_switch, NULL);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_switch\n");
		goto fail_deprobe_wake_new;
	}

	return ret;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
	return ret;
}

static void tracing_sched_unregister(void)
{
	unregister_trace_sched_switch(probe_sched_switch, NULL);
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
}

static void tracing_start_sched_switch(void)
{
	mutex_lock(&sched_register_mutex);
	if (!(sched_ref++))
		tracing_sched_register();
	mutex_unlock(&sched_register_mutex);
}

static void tracing_stop_sched_switch(void)
{
	mutex_lock(&sched_register_mutex);
	if (!(--sched_ref))
		tracing_sched_unregister();
	mutex_unlock(&sched_register_mutex);
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch();
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch();
}

#ifdef CONFIG_KDEBUGD_FTRACE
static void stop_sched_trace(struct trace_array *tr)
{
	tracing_stop_sched_switch();
}

static int sched_switch_trace_init(struct trace_array *tr)
{
	tracing_reset_online_cpus(&tr->trace_buffer);
	tracing_start_sched_switch();
	return 0;
}

static void sched_switch_trace_reset(struct trace_array *tr)
{
	if (sched_ref)
		stop_sched_trace(tr);
}

static void sched_switch_trace_start(struct trace_array *tr)
{
	/*Do nothing as this variable removed */
	/*sched_stopped = 0;*/
}

static void sched_switch_trace_stop(struct trace_array *tr)
{
	/*Do nothing as this variable removed */
	/*sched_stopped = 1;*/
}

static struct tracer sched_switch_trace __read_mostly = {
	.name           = "sched_switch",
	.init           = sched_switch_trace_init,
	.reset          = sched_switch_trace_reset,
	.start          = sched_switch_trace_start,
	.stop           = sched_switch_trace_stop,
	/*this functionality has been removed in latest kernel */
	/*.wait_pipe      = poll_wait_pipe, */
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sched_switch,
#endif
};

static __init int init_sched_switch_trace(void)
{
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);
#endif /* CONFIG_KDEBUGD_FTRACE */
