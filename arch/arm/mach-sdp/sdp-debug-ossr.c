/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS54XX - ARM Debug Architecture v7.1 support
 *
 * ij.jang@samsung.com: sdpxxxx port
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

//#include <plat/cpu.h>

#include <asm/cputype.h>

//#include <mach/cpufreq.h>
//#include <mach/regs-pmu.h>
//#include <mach/debug.h>

#define UNLOCK_MAGIC	(0xc5acce55)

#define ossr_dbg(fmt, ...)	trace_printk(fmt, __VA_ARGS__)
/*
 * Debug register base addr
 * (UM 13.Coresight, Figure 13-7)
 */
enum DBG_REG {
	OFFSET_LAR = 0,
	OFFSET_LSR,
	OFFSET_ECR	,
	OFFSET_DBGBCR0	,
	OFFSET_DBGBCR1	,
	OFFSET_DBGBCR2	,
	OFFSET_DBGBCR3	,
	OFFSET_DBGBCR4	,
	OFFSET_DBGBCR5	,
	OFFSET_DBGBVR0	,
	OFFSET_DBGBVR1	,
	OFFSET_DBGBVR2	,
	OFFSET_DBGBVR3	,
	OFFSET_DBGBVR4	,
	OFFSET_DBGBVR5	,
	OFFSET_DBGBXVR0	,
	OFFSET_DBGBXVR1	,
	OFFSET_DBGWVR0	,
	OFFSET_DBGWVR1	,
	OFFSET_DBGWVR2	,
	OFFSET_DBGWVR3	,
	OFFSET_DBGWCR0	,
	OFFSET_DBGWCR1	,
	OFFSET_DBGWCR2	,
	OFFSET_DBGWCR3	,
	OFFSET_DBGVCR,
	NR_OFFSET,
};

enum BREAK_REG {
	DBGBCR0 = 0,
	DBGBCR1,
	DBGBCR2,
	DBGBCR3,
	DBGBCR4,
	DBGBCR5,
	DBGBVR0,
	DBGBVR1,
	DBGBVR2,
	DBGBVR3,
	DBGBVR4,
	DBGBVR5,

	DBGBXVR0,
	DBGBXVR1,

	DBGWVR0,
	DBGWVR1,
	DBGWVR2,
	DBGWVR3,
	DBGWCR0,
	DBGWCR1,
	DBGWCR2,
	DBGWCR3,
	DBGVCR,

	NR_BRK_REG,
};

enum COMMON_REG {
	DBGDSCR = 0,
	DBGWFAR,
	DBGDSCCR,
	DBGDSMCR,
	DBGCLAIM,	/* DBGCLAIMCLR for read, DBGCLAIMSET for write. */
	DBGTRTX,
	DBGTRRX,
	NR_COMM_REG,
};

/* With Trace32 attached debuging, T32 must set FLAG_T32_EN as true. */
//bool FLAG_T32_EN = false;
static void __iomem *ossr_context;
static u32 *comm_stat[NR_CPUS];
static u32 *g_break_stat[NR_CPUS];

inline void os_lock_set(void)
{
	/* DBGOSLAR */
	asm volatile("mcr p14, 0, %0, c1, c0, 4" : : "r" (UNLOCK_MAGIC));
	isb();
}

static void os_lock_clear(void)
{
	/* DBGOSLAR */
	asm volatile("mcr p14, 0, %0, c1, c0, 4" : : "r" (0x1));
	isb();
}

static void break_stat_save(int cpu)
{
	u32 *break_stat = g_break_stat[cpu];
	ossr_dbg("%s:\n", __func__);

	asm volatile(
		"mrc p14, 0, %0, c0, c0, 5\n"
		"mrc p14, 0, %1, c0, c1, 5\n"
		"mrc p14, 0, %2, c0, c2, 5\n"
		"mrc p14, 0, %3, c0, c3, 5\n"
		"mrc p14, 0, %4, c0, c4, 5\n"
		"mrc p14, 0, %5, c0, c5, 5\n"
		"mrc p14, 0, %6, c0, c0, 4\n"
		"mrc p14, 0, %7, c0, c1, 4\n"
		"mrc p14, 0, %8, c0, c2, 4\n"
		"mrc p14, 0, %9, c0, c3, 4\n"
		"mrc p14, 0, %10, c0, c4, 4\n"
		"mrc p14, 0, %11, c0, c5, 4\n"
		:
		"=r" (break_stat[DBGBCR0]),
		"=r" (break_stat[DBGBCR1]),
		"=r" (break_stat[DBGBCR2]),
		"=r" (break_stat[DBGBCR3]),
		"=r" (break_stat[DBGBCR4]),
		"=r" (break_stat[DBGBCR5]),
		"=r" (break_stat[DBGBVR0]),
		"=r" (break_stat[DBGBVR1]),
		"=r" (break_stat[DBGBVR2]),
		"=r" (break_stat[DBGBVR3]),
		"=r" (break_stat[DBGBVR4]),
		"=r" (break_stat[DBGBVR5])
	);

	asm volatile(
		"mrc p14, 0, %0, c0, c0, 6\n"
		"mrc p14, 0, %1, c0, c1, 6\n"
		"mrc p14, 0, %2, c0, c2, 6\n"
		"mrc p14, 0, %3, c0, c3, 6\n"
		"mrc p14, 0, %4, c0, c0, 7\n"
		"mrc p14, 0, %5, c0, c1, 7\n"
		"mrc p14, 0, %6, c0, c2, 7\n"
		"mrc p14, 0, %7, c0, c3, 7\n"
		"mrc p14, 0, %8, c0, c7, 0\n"
		:
		"=r" (break_stat[DBGWVR0]),
		"=r" (break_stat[DBGWVR1]),
		"=r" (break_stat[DBGWVR2]),
		"=r" (break_stat[DBGWVR3]),
		"=r" (break_stat[DBGWCR0]),
		"=r" (break_stat[DBGWCR1]),
		"=r" (break_stat[DBGWCR2]),
		"=r" (break_stat[DBGWCR3]),
		"=r" (break_stat[DBGVCR])
	);

	ossr_dbg("%s: done\n", __func__);
}

inline void break_stat_restore(int cpu)
{
	u32 *break_stat = g_break_stat[cpu];
	ossr_dbg("%s:\n", __func__);

	asm volatile(
		"mcr p14, 0, %0, c0, c0, 5\n"
		"mcr p14, 0, %1, c0, c1, 5\n"
		"mcr p14, 0, %2, c0, c2, 5\n"
		"mcr p14, 0, %3, c0, c3, 5\n"
		"mcr p14, 0, %4, c0, c4, 5\n"
		"mcr p14, 0, %5, c0, c5, 5\n"
		"mcr p14, 0, %6, c0, c0, 4\n"
		"mcr p14, 0, %7, c0, c1, 4\n"
		"mcr p14, 0, %8, c0, c2, 4\n"
		"mcr p14, 0, %9, c0, c3, 4\n"
		"mcr p14, 0, %10, c0, c4, 4\n"
		"mcr p14, 0, %11, c0, c5, 4\n"
		: :
		"r" (break_stat[DBGBCR0]),
		"r" (break_stat[DBGBCR1]),
		"r" (break_stat[DBGBCR2]),
		"r" (break_stat[DBGBCR3]),
		"r" (break_stat[DBGBCR4]),
		"r" (break_stat[DBGBCR5]),
		"r" (break_stat[DBGBVR0]),
		"r" (break_stat[DBGBVR1]),
		"r" (break_stat[DBGBVR2]),
		"r" (break_stat[DBGBVR3]),
		"r" (break_stat[DBGBVR4]),
		"r" (break_stat[DBGBVR5])
	);

	asm volatile(
		"mcr p14, 0, %0, c0, c0, 6\n"
		"mcr p14, 0, %1, c0, c1, 6\n"
		"mcr p14, 0, %2, c0, c2, 6\n"
		"mcr p14, 0, %3, c0, c3, 6\n"
		"mcr p14, 0, %4, c0, c0, 7\n"
		"mcr p14, 0, %5, c0, c1, 7\n"
		"mcr p14, 0, %6, c0, c2, 7\n"
		"mcr p14, 0, %7, c0, c3, 7\n"
		"mcr p14, 0, %8, c0, c7, 0\n"
		: :
		"r" (break_stat[DBGWVR0]),
		"r" (break_stat[DBGWVR1]),
		"r" (break_stat[DBGWVR2]),
		"r" (break_stat[DBGWVR3]),
		"r" (break_stat[DBGWCR0]),
		"r" (break_stat[DBGWCR1]),
		"r" (break_stat[DBGWCR2]),
		"r" (break_stat[DBGWCR3]),
		"r" (break_stat[DBGVCR])
	);

	ossr_dbg("%s: done\n", __func__);
}

inline void debug_register_save(int cpu)
{
	ossr_dbg("%s:\n", __func__);

	asm volatile(
		"mrc p14, 0, %0, c0, c2, 2\n"
		"mrc p14, 0, %1, c0, c6, 0\n"
		:
		"=r" (comm_stat[cpu][DBGDSCR]),
		"=r" (comm_stat[cpu][DBGWFAR])
	);

	break_stat_save(cpu);

	/* In save sequence, read DBGCLAIMCLR. */
	asm volatile(
		"mrc p14, 0, %0, c7, c9, 6"
		:
		"=r" (comm_stat[cpu][DBGCLAIM])
	);

	asm volatile(
		"mrc p14, 0, %0, c0, c3, 2\n"
		"mrc p14, 0, %1, c0, c0, 2\n"
		:
		"=r" (comm_stat[cpu][DBGTRTX]),
		"=r" (comm_stat[cpu][DBGTRRX])
	);

	isb();

	ossr_dbg("%s: %d done\n", __func__, cpu);
}

inline void debug_register_restore(int cpu)
{
	ossr_dbg("%s:\n", __func__);

	asm volatile(
		"mcr p14, 0, %0, c0, c2, 2\n"
		"mcr p14, 0, %1, c0, c6, 0\n"
		: :
		"r" (comm_stat[cpu][DBGDSCR] | (1 << 14)),
		"r" (comm_stat[cpu][DBGWFAR])
	);

	break_stat_restore(cpu);

	/* In save sequence, write DBGCLAIMSET. */
	asm volatile(
		"mcr p14, 0, %0, c7, c8, 6"
		: :
		"r" (comm_stat[cpu][DBGCLAIM])
	);

	asm volatile(
		"mcr p14, 0, %0, c0, c3, 2\n"
		"mcr p14, 0, %1, c0, c0, 2\n"
		: :
		"r" (comm_stat[cpu][DBGTRTX]),
		"r" (comm_stat[cpu][DBGTRRX])
	);
	
	isb();
	
	ossr_dbg("%s: %d done\n", __func__, cpu);
}

static void armdebug_suspend_cpu(int cpu)
{
	/*
	 * (C7.3.3)
	 * Start v7.1 Debug OS Save sequen
	 */

	/* 1. Set the OS Lock by writing the key value. */
	/* 2. Execute an ISB instruction. */
	os_lock_set();

	/* 3. Save the values. */
	debug_register_save(cpu);

	/* OS Double Lock later */
	ossr_dbg("%s: %d done\n", __func__, cpu);
}

static void armdebug_resume_cpu(int cpu)
{
	/*
	 * (C7.3.3)
	 * Start v7.1 Debug OS Restore sequen
	 */
	/* 1. Set the OS Lock by writing the key value. */
	/* 2. Execute an ISB instruction. */
	os_lock_set();

	/* 3. Restore the values. */
	/* 4. Execute an ISB instruction. */
	debug_register_restore(cpu);

	/* 5. Clear the OS Lock by writing any non-key value. */
	/* 6. Execute a Context synchronization operation. */
	os_lock_clear();

	trace_printk("%s: %d done\n", __func__, cpu);
}

static int armdebug_cpu_pm_notifier(struct notifier_block *self,
		unsigned long cmd, void *v)
{
	int cpu = smp_processor_id();

	switch (cmd) {
	case CPU_PM_ENTER:
		armdebug_suspend_cpu(cpu);
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		armdebug_resume_cpu(cpu);
		break;
	case CPU_CLUSTER_PM_ENTER:
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		break;
	}

	return NOTIFY_OK;
}

static int __cpuinit armdebug_cpu_notifier(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
	case CPU_DOWN_FAILED:
		armdebug_resume_cpu(cpu);
		break;
	case CPU_DYING:
		armdebug_suspend_cpu(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata armdebug_cpu_pm_notifier_block = {
	.notifier_call = armdebug_cpu_pm_notifier,
};

static struct notifier_block __cpuinitdata armdebug_cpu_notifier_block = {
	.notifier_call = armdebug_cpu_notifier,
};

static void sdp_init_os_lock(void *ptr)
{
	os_lock_clear();
}

static int __init sdp_armdebug_init(void)
{
	int ret, i;
//	if (!FLAG_T32_EN)
//		return 0;

	ossr_context = ioremap(0xbfff1000, 0x1000);
	BUG_ON(!ossr_context);

	for ( i = 0; i < NR_CPUS; i++) {
		comm_stat[i] = ossr_context + sizeof(u32) * (NR_COMM_REG + NR_BRK_REG) * i;
		g_break_stat[i] = comm_stat[i] + NR_COMM_REG;
	}

	ret = cpu_pm_register_notifier(&armdebug_cpu_pm_notifier_block);
	if (ret < 0)
		return ret;

	ret = register_cpu_notifier(&armdebug_cpu_notifier_block);
	if (ret < 0)
		return ret;

	smp_call_function(sdp_init_os_lock, NULL, 0);
	os_lock_clear();

	pr_info("sdp-debug-ossr: arm debug v7.1 support.\n");
	return 0;
}

subsys_initcall(sdp_armdebug_init);

