/*
 * Novatek Cortex A9 Power Management Functions
 *
 */

#include <linux/suspend.h>
#include <asm/system_misc.h>
#include <asm/suspend.h>
#include <asm/io.h>
#include "mach/pm.h"
#ifdef CONFIG_NVT_HW_CLOCK
#include "mach/nvt_hwclock.h"
#elif defined(CONFIG_NVT_CA53_HW_CLOCK)
#include <mach/nvt_hwclock_ca53.h>
#endif
#ifdef CONFIG_ARM_PSCI
#include <asm/psci.h>
#endif

static int __nt72xxx_suspend_enter(unsigned long unused)
{
#ifdef CONFIG_ARM_PSCI
	const struct psci_power_state ps = {
		.type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
	};
#endif
#if defined(CONFIG_NVT_HW_CLOCK) || defined(CONFIG_NVT_CA53_HW_CLOCK)
	/* HW clock suspend must be the latest. */
	hwclock_suspend();
#endif
#ifdef CONFIG_ARM_PSCI
	psci_ops.cpu_suspend(ps, (u32)virt_to_phys(cpu_resume));
#else
#ifndef CONFIG_ARCH_PRENT17M
	soft_restart(virt_to_phys(nt72xxx_wait_for_die));
#endif
#endif
	return 0;
}

static int nt72xxx_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;

	pr_info("[%s]\n", __func__);

	cpu_suspend(0, __nt72xxx_suspend_enter);
#ifdef CONFIG_CLKSRC_NVT_TIMER
	writel_relaxed(0xC6, nvt_timer_reg_base + 0x4);
#endif

	pr_info("resume successful and back to [%s]\n", __func__);

	return ret;
}

static int nt72xxx_pm_begin(suspend_state_t state)
{
	pr_info("[%s]\n", __func__);
	return 0;
}

static void nt72xxx_pm_end(void)
{
	pr_info("[%s]\n", __func__);
}

static void nt72xxx_pm_finish(void)
{
	pr_info("[%s]\n", __func__);
}

static const struct platform_suspend_ops nt72xxx_pm_ops = {
	.begin		= nt72xxx_pm_begin,
	.end		= nt72xxx_pm_end,
	.enter		= nt72xxx_pm_enter,
	.finish		= nt72xxx_pm_finish,
	.valid		= suspend_valid_only_mem,
};

static int __init nt72xxx_pm_late_init(void)
{
	suspend_set_ops(&nt72xxx_pm_ops);
	return 0;
}

device_initcall(nt72xxx_pm_late_init);
