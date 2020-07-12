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

	FLAG_SAVED,
	NR_COMM_REG,
};

/* With Trace32 attached debuging, T32 must set FLAG_T32_EN as true. */
//bool FLAG_T32_EN = false;

static u32 dbg_regs[NR_CPUS][NR_OFFSET];
static DEFINE_RAW_SPINLOCK(debug_lock);
static unsigned int comm_stat[NR_CPUS][NR_COMM_REG];
static unsigned int break_stat[NR_BRK_REG];
static int flag_sleep[NR_CPUS];

inline void os_lock_set(void)
{
	/* DBGOSLAR */
	asm volatile("mcr p14, 0, %0, c1, c0, 4" : : "r" (UNLOCK_MAGIC));
}

static void os_lock_clear(void)
{
	/* DBGOSLAR */
	asm volatile("mcr p14, 0, %0, c1, c0, 4" : : "r" (0x1));
}

static void break_stat_save(void)
{
	pr_debug("%s:\n", __func__);

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

	pr_debug("%s: done\n", __func__);
}

inline void break_stat_restore(void)
{
	int cpu;

	pr_debug("%s:\n", __func__);

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		if (flag_sleep[cpu] == 0)
			break;

	if (cpu < NR_CPUS) {
		u32 *dbg_reg = dbg_regs[cpu];

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
			"r" (dbg_reg[OFFSET_DBGBCR0]),
			"r" (dbg_reg[OFFSET_DBGBCR1]),
			"r" (dbg_reg[OFFSET_DBGBCR2]),
			"r" (dbg_reg[OFFSET_DBGBCR3]),
			"r" (dbg_reg[OFFSET_DBGBCR4]),
			"r" (dbg_reg[OFFSET_DBGBCR5]),
			"r" (dbg_reg[OFFSET_DBGBVR0]),
			"r" (dbg_reg[OFFSET_DBGBVR1]),
			"r" (dbg_reg[OFFSET_DBGBVR2]),
			"r" (dbg_reg[OFFSET_DBGBVR3]),
			"r" (dbg_reg[OFFSET_DBGBVR4]),
			"r" (dbg_reg[OFFSET_DBGBVR5])
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
			"r" (dbg_reg[OFFSET_DBGWVR0]),
			"r" (dbg_reg[OFFSET_DBGWVR1]),
			"r" (dbg_reg[OFFSET_DBGWVR2]),
			"r" (dbg_reg[OFFSET_DBGWVR3]),
			"r" (dbg_reg[OFFSET_DBGWCR0]),
			"r" (dbg_reg[OFFSET_DBGWCR1]),
			"r" (dbg_reg[OFFSET_DBGWCR2]),
			"r" (dbg_reg[OFFSET_DBGWCR3]),
			"r" (dbg_reg[OFFSET_DBGVCR])
		);
	} else {
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
	}

	pr_debug("%s: done\n", __func__);
}

inline void debug_register_save(int cpu)
{
	pr_debug("%s:\n", __func__);

	asm volatile(
		"mrc p14, 0, %0, c0, c2, 2\n"
		"mrc p14, 0, %1, c0, c6, 0\n"
		:
		"=r" (comm_stat[cpu][DBGDSCR]),
		"=r" (comm_stat[cpu][DBGWFAR])
	);

	raw_spin_lock(&debug_lock);
	flag_sleep[cpu] = 1;
	break_stat_save();
	raw_spin_unlock(&debug_lock);

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

	comm_stat[cpu][FLAG_SAVED] = 1;

	pr_debug("%s: %d done\n", __func__, cpu);
}

inline void debug_register_restore(int cpu)
{
	pr_debug("%s:\n", __func__);

	if (!comm_stat[cpu][FLAG_SAVED])
		goto pass;

	asm volatile(
		"mcr p14, 0, %0, c0, c2, 2\n"
		"mcr p14, 0, %1, c0, c6, 0\n"
		: :
		"r" (comm_stat[cpu][DBGDSCR] | (1 << 14)),
		"r" (comm_stat[cpu][DBGWFAR])
	);


	raw_spin_lock(&debug_lock);
	break_stat_restore();
	flag_sleep[cpu] = 0;
	raw_spin_unlock(&debug_lock);

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


	comm_stat[cpu][FLAG_SAVED] = 0;
pass:
	pr_debug("%s: %d done\n", __func__, cpu);
}

static void armdebug_suspend_cpu(int cpu)
{
#if 1
	/*
	 * (C7.3.3)
	 * Start v7.1 Debug OS Save sequen
	 */

	/* 1. Set the OS Lock by writing the key value. */
	os_lock_set();
	
	/* 2. Execute an ISB instruction. */
	isb();

	/* 3. Save the values. */
	debug_register_save(cpu);
#else
	os_lock_set();
	isb();
#endif

	/* OS Double Lock later */
	pr_debug("%s: %d done\n", __func__, cpu);
}

static void armdebug_resume_cpu(int cpu)
{
#if 01
	/*
	 * (C7.3.3)
	 * Start v7.1 Debug OS Restore sequen
	 */
	
	if (!comm_stat[cpu][FLAG_SAVED]) {
		unsigned int reg_DSCR;

		/* For restore breakpoint register from other live cpu. */
		os_lock_set();
		isb();

		raw_spin_lock(&debug_lock);
		break_stat_restore();
		flag_sleep[cpu] = 0;
		raw_spin_unlock(&debug_lock);

		/* HDBGen, Keep DSCR[14] as 1. */
		asm volatile("mrc p14, 0, %0, c0, c2, 2"
				: "=r"(reg_DSCR));
		asm volatile("mcr p14, 0, %0, c0, c2, 2"
				: : "r"(reg_DSCR | (1 << 14)));

		os_lock_clear();
		isb();
	} else {
		/* 1. Set the OS Lock by writing the key value. */
		/* 2. Execute an ISB instruction. */
		os_lock_set();
		isb();

		/* 3. Restore the values. */
		debug_register_restore(cpu);

		/* 4. Execute an ISB instruction. */
		isb();

		/* 5. Clear the OS Lock by writing any non-key value. */
		os_lock_clear();

		/* 6. Execute a Context synchronization operation.
		 *  - the execution of an ISB instruction.
		 */
		isb();
	}
#else
	os_lock_clear();
	isb();
#endif

	pr_debug("%s: %d done\n", __func__, cpu);
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

static int __init sdp_armdebug_init(void)
{
	int ret;
//	if (!FLAG_T32_EN)
//		return 0;

	ret = cpu_pm_register_notifier(&armdebug_cpu_pm_notifier_block);
	if (ret < 0)
		return ret;

	ret = register_cpu_notifier(&armdebug_cpu_notifier_block);
	if (ret < 0)
		return ret;

	return 0;
}

subsys_initcall(sdp_armdebug_init);

