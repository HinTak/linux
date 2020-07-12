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
#include <soc/sdp/soc.h>

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
		.flags			= CPUIDLE_FLAG_TIME_VALID,
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
	ktime_t time_start, time_end;
	s64 diff;

	time_start = ktime_get();
	
	cpu_do_idle();

	time_end = ktime_get();

	local_irq_enable();

	diff = ktime_to_us(ktime_sub(time_end, time_start));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;
	
	return index;
}

static int c1_finisher(unsigned long flags)
{
	unsigned int cpu = read_cpuid_mpidr() & 0xf;

	/* set entry vector, cpu_resume */
	sdp_cpuidle_ops->set_entry(cpu, (u32)virt_to_phys(cpu_resume));

	/* power down */
	setup_mm_for_reboot();
	
	set_cr(get_cr() & ~((u32) CR_C));
	
	flush_cache_louis();
	asm volatile ("clrex");

	set_auxcr(get_auxcr() & ~(1UL << 6));
	dsb();

	sdp_cpuidle_ops->powerdown_cpu(cpu);
	dsb();
	wfi();

	return 1;
}

static int c1_enter_lowpower(struct cpuidle_device *dev,
				  struct cpuidle_driver *drv,
				  int index)
{
	ktime_t time_start, time_end;
	s64 diff;
	int ret;
	unsigned int cpu;

	cpu = smp_processor_id();
	if (cpu != 1)
		return sdp_enter_idle(dev, drv, index);

	time_start = ktime_get();

	BUG_ON(!irqs_disabled());

	cpu_pm_enter();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	/* power down */
	ret = cpu_suspend((unsigned long)dev, c1_finisher);
	if (ret)
		BUG();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

	cpu_pm_exit();

	time_end = ktime_get();

	diff = ktime_to_us(ktime_sub(time_end, time_start));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int)diff;

	local_irq_enable();

	return index;	
}

static int sdp_enter_lowpower(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index)
{
	if (soc_is_sdp1406())
		c1_enter_lowpower(dev, drv, index);
	else
	sdp_enter_idle(dev, drv, index);

	return index;
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

	for_each_cpu(cpu_id, cpu_online_mask) {
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
	
	return 0;
}
device_initcall(sdp_init_cpuidle);