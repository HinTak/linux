/* linux/arch/arm/mach-sdp/cpuidle.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/clockchips.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/uaccess.h>

#include <asm/idmap.h>
#include <asm/delay.h>
#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/unified.h>
#include <asm/cpuidle.h>
#include <mach/sdp_smp.h>
#include <mach/sdp_cpuidle.h>
#include <mach/soc.h>

#include "common.h"

static struct sdp_cpuidle_ops *sdp_cpuidle_ops;

static int sdp_enter_idle(struct cpuidle_device *dev,
			  struct cpuidle_driver *drv,
			  int index);

static int sdp_enter_lowpower(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index);

static struct cpuidle_state sdp_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= sdp_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 10,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "WFI",
		.desc			= "ARM WFI",
	},
	[1] = {
		.enter			= sdp_enter_lowpower,
		.exit_latency		= 2000,
		.target_residency	= 10000,
		/* Set CPUIDLE_FLAG_TIMER_STOP for arm arch timer which has C3STOP feature,
		 * When sdp-timer is used, tick_notify() will ignores notifications anyway. */
		.flags			= CPUIDLE_FLAG_TIMER_STOP,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, sdp_cpuidle_device);

static struct cpuidle_driver sdp_idle_driver = {
	.name		= "sdp_idle",
	.owner		= THIS_MODULE,
};

int sdp_cpuidle_init(struct sdp_cpuidle_ops *ops)
{
	if (ops)
		sdp_cpuidle_ops = ops;

	return 0;
}

static int sdp_enter_idle(struct cpuidle_device *dev,
			  struct cpuidle_driver *drv,
			  int index)
{
	cpu_do_idle();
	return 0;
}

static int c1_finisher(unsigned long flags)
{
	unsigned int cpu = read_cpuid_mpidr() & 0xf;

	/* set entry vector, cpu_resume */
	sdp_cpuidle_ops->set_entry(cpu, (u32)virt_to_phys(cpu_resume));

	/* power down, this never fails.  */
	return sdp_cpuidle_ops->powerdown_cpu(cpu);
}

#undef SDP_CPUIDLE_DEBUG

#if defined(SDP_CPUIDLE_DEBUG)
int sdp_cpuidle_debug[4];
#define mark_debug(cpu, x) do { ACCESS_ONCE(sdp_cpuidle_debug[cpu]) = x; } while (0)
#else
#define mark_debug(cpu, x)
#endif

static int sdp_enter_lowpower(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index)
{
	int ret = 0;

	mark_debug(dev->cpu, 0);

	BUG_ON(!irqs_disabled());

	cpu_pm_enter();

	/* XXX: Disabling this could make cpu supurious waking up in power off sequence.*/
	gic_cpu_if_down();

	/* power down */
	ret = cpu_suspend((unsigned long)dev, c1_finisher);

	BUG_ON(ret);

	mark_debug(dev->cpu, 1);

	cpu_pm_exit();

	mark_debug(dev->cpu, 2);

	return (ret == 0) ? index : -1;
}

static int __init sdp_init_cpuidle(void)
{
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &sdp_idle_driver;
	int i;
	int cpu_id;
	int ret;

	/* setup cpuidle driver */
	drv->state_count = ARRAY_SIZE(sdp_cpuidle_set);

	for (i = 0; i < drv->state_count; i++)
		memcpy(&drv->states[i], &sdp_cpuidle_set[i],
			sizeof(struct cpuidle_state));
	drv->safe_state_index = 0;
	ret = cpuidle_register_driver(&sdp_idle_driver);
	if (ret < 0)
		printk(KERN_ERR "error : cpuidle driver register fail\n");

	for_each_possible_cpu(cpu_id) {
		dev = &per_cpu(sdp_cpuidle_device, cpu_id);
		dev->cpu = cpu_id;

		dev->state_count = ARRAY_SIZE(sdp_cpuidle_set);
		
		/* default disable for state1 */
		dev->states_usage[1].disable = 1;

		if (cpuidle_register_device(dev)) {
			printk(KERN_ERR "CPUidle register device failed\n");
			return -EIO;
		}
	}

	pr_info("sdp cpuidle driver registered.\n");
	
	return 0;
}
late_initcall(sdp_init_cpuidle);

void sdp_cpuidle_enable_state(int state, int enable)
{
	int cpu;

	BUG_ON(state < 0);

	for_each_possible_cpu(cpu) {
		struct cpuidle_device *dev = &per_cpu(sdp_cpuidle_device, cpu);
		BUG_ON(state >= dev->state_count);
		dev->states_usage[state].disable = !enable;
	}
	if (!enable)
		kick_all_cpus_sync();
}

