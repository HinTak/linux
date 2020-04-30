/*
 * kernel/power/suspend.c - Suspend to RAM and standby functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/ftrace.h>
#include <trace/events/power.h>
#include <linux/compiler.h>
#include <linux/moduleparam.h>

#ifdef CONFIG_POWER_SAVING_MODE
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/cpufreq.h>
#include <linux/console.h>
#include <linux/platform_data/sdp-cpufreq.h>
#endif

#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING 
#include <trace/early.h>
#ifdef CONFIG_SDP_HW_CLOCK
#include <mach/sdp_hwclock.h>
#elif defined CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#elif defined(CONFIG_NVT_CA53_HW_CLOCK)
#include <mach/nvt_hwclock_ca53.h>
#endif
#endif //CONFIG_DPM_SHOW_TIME_IN_HWTRACING

#include "power.h"

const char *pm_labels[] = { "mem", "standby", "freeze", NULL };
const char *pm_states[PM_SUSPEND_MAX];

static const struct platform_suspend_ops *suspend_ops;
static const struct platform_freeze_ops *freeze_ops;
static DECLARE_WAIT_QUEUE_HEAD(suspend_freeze_wait_head);

enum freeze_state __read_mostly suspend_freeze_state;
static DEFINE_SPINLOCK(suspend_freeze_lock);

void freeze_set_ops(const struct platform_freeze_ops *ops)
{
	lock_system_sleep();
	freeze_ops = ops;
	unlock_system_sleep();
}

static void freeze_begin(void)
{
	suspend_freeze_state = FREEZE_STATE_NONE;
}

static void freeze_enter(void)
{
	spin_lock_irq(&suspend_freeze_lock);
	if (pm_wakeup_pending())
		goto out;

	suspend_freeze_state = FREEZE_STATE_ENTER;
	spin_unlock_irq(&suspend_freeze_lock);

	get_online_cpus();
	cpuidle_resume();

	/* Push all the CPUs into the idle loop. */
	wake_up_all_idle_cpus();
	pr_debug("PM: suspend-to-idle\n");
	/* Make the current CPU wait so it can enter the idle loop too. */
	wait_event(suspend_freeze_wait_head,
		   suspend_freeze_state == FREEZE_STATE_WAKE);
	pr_debug("PM: resume from suspend-to-idle\n");

	cpuidle_pause();
	put_online_cpus();

	spin_lock_irq(&suspend_freeze_lock);

 out:
	suspend_freeze_state = FREEZE_STATE_NONE;
	spin_unlock_irq(&suspend_freeze_lock);
}

void freeze_wake(void)
{
	unsigned long flags;

	spin_lock_irqsave(&suspend_freeze_lock, flags);
	if (suspend_freeze_state > FREEZE_STATE_NONE) {
		suspend_freeze_state = FREEZE_STATE_WAKE;
		wake_up(&suspend_freeze_wait_head);
	}
	spin_unlock_irqrestore(&suspend_freeze_lock, flags);
}
EXPORT_SYMBOL_GPL(freeze_wake);

static bool valid_state(suspend_state_t state)
{
	/*
	 * PM_SUSPEND_STANDBY and PM_SUSPEND_MEM states need low level
	 * support and need to be valid to the low level
	 * implementation, no valid callback implies that none are valid.
	 */
	return suspend_ops && suspend_ops->valid && suspend_ops->valid(state);
}

/*
 * If this is set, the "mem" label always corresponds to the deepest sleep state
 * available, the "standby" label corresponds to the second deepest sleep state
 * available (if any), and the "freeze" label corresponds to the remaining
 * available sleep state (if there is one).
 */
static bool relative_states;

static int __init sleep_states_setup(char *str)
{
	relative_states = !strncmp(str, "1", 1);
	pm_states[PM_SUSPEND_FREEZE] = pm_labels[relative_states ? 0 : 2];
	return 1;
}

__setup("relative_sleep_states=", sleep_states_setup);

/**
 * suspend_set_ops - Set the global suspend method table.
 * @ops: Suspend operations to use.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	suspend_state_t i;
	int j = 0;

	lock_system_sleep();

	suspend_ops = ops;
	for (i = PM_SUSPEND_MEM; i >= PM_SUSPEND_STANDBY; i--)
		if (valid_state(i)) {
			pm_states[i] = pm_labels[j++];
		} else if (!relative_states) {
			pm_states[i] = NULL;
			j++;
		}

	pm_states[PM_SUSPEND_FREEZE] = pm_labels[j];

	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(suspend_set_ops);

/**
 * suspend_valid_only_mem - Generic memory-only valid callback.
 *
 * Platform drivers that implement mem suspend only and only need to check for
 * that in their .valid() callback can use this instead of rolling their own
 * .valid() callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}
EXPORT_SYMBOL_GPL(suspend_valid_only_mem);

static bool sleep_state_supported(suspend_state_t state)
{
	return state == PM_SUSPEND_FREEZE || (suspend_ops && suspend_ops->enter);
}

static int platform_suspend_prepare(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->prepare ?
		suspend_ops->prepare() : 0;
}

static int platform_suspend_prepare_late(suspend_state_t state)
{
	return state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->prepare ?
		freeze_ops->prepare() : 0;
}

static int platform_suspend_prepare_noirq(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->prepare_late ?
		suspend_ops->prepare_late() : 0;
}

static void platform_resume_noirq(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->wake)
		suspend_ops->wake();
}

static void platform_resume_early(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->restore)
		freeze_ops->restore();
}

static void platform_resume_finish(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->finish)
		suspend_ops->finish();
}

static int platform_suspend_begin(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->begin)
		return freeze_ops->begin();
	else if (suspend_ops->begin)
		return suspend_ops->begin(state);
	else
		return 0;
}

static void platform_resume_end(suspend_state_t state)
{
	if (state == PM_SUSPEND_FREEZE && freeze_ops && freeze_ops->end)
		freeze_ops->end();
	else if (suspend_ops->end)
		suspend_ops->end();
}

static void platform_recover(suspend_state_t state)
{
	if (state != PM_SUSPEND_FREEZE && suspend_ops->recover)
		suspend_ops->recover();
}

static bool platform_suspend_again(suspend_state_t state)
{
	return state != PM_SUSPEND_FREEZE && suspend_ops->suspend_again ?
		suspend_ops->suspend_again() : false;
}

#ifdef CONFIG_PM_DEBUG
static unsigned int pm_test_delay = 5;
module_param(pm_test_delay, uint, 0644);
MODULE_PARM_DESC(pm_test_delay,
		 "Number of seconds to wait before resuming from suspend test");
#endif

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
	if (pm_test_level == level) {
		printk(KERN_INFO "suspend debug: Waiting for %d second(s).\n",
				pm_test_delay);
		mdelay(pm_test_delay * 1000);
		return 1;
	}
#endif /* !CONFIG_PM_DEBUG */
	return 0;
}

/**
 * suspend_prepare - Prepare for entering system sleep state.
 *
 * Common code run for every system sleep state that can be entered (except for
 * hibernation).  Run suspend notifiers, allocate the "suspend" console and
 * freeze processes.
 */
static int suspend_prepare(suspend_state_t state)
{
	int error;

	if (!sleep_state_supported(state))
		return -EPERM;

	pm_prepare_console();

	error = pm_notifier_call_chain(PM_SUSPEND_PREPARE);
	if (error)
		goto Finish;

	trace_suspend_resume(TPS("freeze_processes"), 0, true);
	error = suspend_freeze_processes();
	trace_suspend_resume(TPS("freeze_processes"), 0, false);
	if (!error)
		return 0;

	suspend_stats.failed_freeze++;
	dpm_save_failed_step(SUSPEND_FREEZE);
 Finish:
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
	return error;
}

/* default implementation */
void __weak arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __weak arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

#ifdef CONFIG_PM_CRC_CHECK
extern void save_suspend_crc(void);
extern void compare_resume_crc(void);
#endif

#if defined(CONFIG_POWER_SAVING_MODE) || defined(CONFIG_ALWAYS_INSTANT_ON)
unsigned int *micom_gpio = NULL;
#if defined(CONFIG_ARCH_SDP1404)
#define GPIO_US_MCOM		0x1AB00000
#define GPIO_US_MCOM_SIZE	0x2000
#elif defined(CONFIG_ARCH_SDP1406)
#define GPIO_US_MCOM		0x800700
#define GPIO_US_MCOM_SIZE	0x100
#elif defined(CONFIG_ARCH_SDP1501)
#define GPIO_US_MCOM            0x7C1080
#define GPIO_US_MCOM_SIZE       0x100
#elif defined(CONFIG_ARCH_SDP1601)
#define GPIO_US_MCOM            0x9C12C0
#define GPIO_US_MCOM_SIZE       0x40
#elif defined(CONFIG_ARCH_NVT72172)
#define GPIO_US_MCOM            0xfc040210
#define GPIO_US_MCOM_SIZE       0x4
#endif
#endif

#ifdef CONFIG_POWER_SAVING_MODE
extern void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);
extern void pwsv_notice_pwsvmode_submicom(int mode_flags);
extern void pwsv_thaw_threads(suspend_state_t state);
extern void pwsv_reboot(int pwsvmode);
extern void pwsv_reset_thaw_pids(void);
extern void pwsv_clear_suspend_mode(void);
extern void pwsv_init_wdt_counter(int counter);
extern int pwsv_check_wdt(int count);
extern int pwsv_check_wakeup_req(void);
extern void pwsv_init_wakeup_state(void);
extern int pwsv_get_wakeup_state(void);
extern int pwsv_print_status(int cnt);
extern int pwsv_current_mode;	/* current mode */
extern int pwsv_mode_flags;	/* mode flags enabled or not */
int pwsv_loop_cnt = 0;		/* loop count to manage print */
#endif //end of CONFIG_POWER_SAVING_MODE

#ifdef CONFIG_ALWAYS_INSTANT_ON
extern void machine_restart_standby(char *cmd);
extern int get_onboot_version(void);

#if defined(CONFIG_ARCH_SDP1404) || defined(CONFIG_ARCH_SDP1406)
int is_always_instantOn(void)
{
	int ret = false;
	int target = get_onboot_version();
	printk(KERN_ERR"target year : %d\n",target);
	if(2016 == target)
		ret = true;
	return ret;
}
#elif defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
int is_always_instantOn(void)
{
	int ret = false;
	unsigned int * p_gpio;

	p_gpio = micom_gpio;
	ret = *(volatile unsigned int*)(p_gpio+5);

	printk(KERN_ERR"IOT] Micom Master Standby On Support : %d\n",ret);

	return ret;
}
#else
int is_always_instantOn(void)
{
	pr_err("Always Instant On is always on\n");
	return true;
}
#endif
#else
int is_always_instantOn(void)
{
	return false;
}
#endif

#ifdef CONFIG_POWER_SAVING_MODE
/**
 * suspend_enter_pwsv - Make the system enter the given sleep state with pwsv.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter_pwsv(suspend_state_t state, bool *wakeup)
{
	int error;

	error = platform_suspend_prepare(state);
	if (error)
		goto Platform_finish;


	error = dpm_suspend_late(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: late suspend of devices failed\n");
		goto Platform_finish;
	}

	error = platform_suspend_prepare_late(state);
	if (error)
		goto Devices_early_resume;


	error = dpm_suspend_noirq(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: noirq suspend of devices failed\n");
		goto Platform_early_resume;
	}

	error = platform_suspend_prepare_noirq(state);
	if (error)
		goto Platform_wake;

	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	/*
	 * PM_SUSPEND_FREEZE equals
	 * frozen processes + suspended devices + idle processors.
	 * Thus we should invoke freeze_enter() soon after
	 * all the devices are suspended.
	 */
	if (state == PM_SUSPEND_FREEZE) {
		trace_suspend_resume(TPS("machine_suspend"), state, true);
		freeze_enter();
		trace_suspend_resume(TPS("machine_suspend"), state, false);
		goto Platform_wake;
	}

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

	error = syscore_suspend();
	if (!error) {
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"),
				state, true);
#ifdef CONFIG_PM_CRC_CHECK
			save_suspend_crc();
#endif
			error = suspend_ops->enter(state); //// actual point to Suspend ////
#ifdef CONFIG_PM_CRC_CHECK
			compare_resume_crc();
#endif
			trace_suspend_resume(TPS("machine_suspend"),
				state, false);
			events_check_enabled = false;
		}
		syscore_resume();
	}

	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING
        {
#if defined(CONFIG_SDP_HW_CLOCK) || defined(CONFIG_NVT_HW_CLOCK) || defined(CONFIG_NVT_CA53_HW_CLOCK)
                char msg[64];
                uint64_t ns = hwclock_raw_ns((uint32_t *)hwclock_get_va());
                uint32_t rem = do_div(ns, 1000000);

                snprintf(msg, sizeof(msg), "on resume: bare timestamp %llu.%06u",
                         ns, rem);
                trace_early_message(msg);
#else
                trace_early_message("on resume");
#endif
        }
#endif

 Platform_wake:
	platform_resume_noirq(state);

	dpm_resume_noirq(PMSG_RESUME);

 Platform_early_resume:
	platform_resume_early(state);

 Devices_early_resume:
	dpm_resume_early(PMSG_RESUME);

 Platform_finish:
	platform_resume_finish(state);
	return error;
}
#endif

/**
 * suspend_enter - Make the system enter the given sleep state.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state, bool *wakeup)
{
	int error;

	error = platform_suspend_prepare(state);
	if (error)
		goto Platform_finish;

	error = dpm_suspend_late(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: late suspend of devices failed\n");
		goto Platform_finish;
	}
	error = platform_suspend_prepare_late(state);
	if (error)
		goto Devices_early_resume;

	error = dpm_suspend_noirq(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: noirq suspend of devices failed\n");
		goto Platform_early_resume;
	}
	error = platform_suspend_prepare_noirq(state);
	if (error)
		goto Platform_wake;

	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	/*
	 * PM_SUSPEND_FREEZE equals
	 * frozen processes + suspended devices + idle processors.
	 * Thus we should invoke freeze_enter() soon after
	 * all the devices are suspended.
	 */
	if (state == PM_SUSPEND_FREEZE) {
		trace_suspend_resume(TPS("machine_suspend"), state, true);
		freeze_enter();
		trace_suspend_resume(TPS("machine_suspend"), state, false);
		goto Platform_wake;
	}

	error = disable_nonboot_cpus();
	if (error || suspend_test(TEST_CPUS))
		goto Enable_cpus;

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

	error = syscore_suspend();
	if (!error) {
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"),
				state, true);
#ifdef CONFIG_PM_CRC_CHECK
			save_suspend_crc();
#endif
			error = suspend_ops->enter(state);
#ifdef CONFIG_PM_CRC_CHECK
			compare_resume_crc();
#endif
			trace_suspend_resume(TPS("machine_suspend"),
				state, false);
			events_check_enabled = false;
		}
		syscore_resume();
	}

	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

 Enable_cpus:
	enable_nonboot_cpus();

#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING
        {
#if defined(CONFIG_SDP_HW_CLOCK) || defined(CONFIG_NVT_HW_CLOCK) || defined(CONFIG_NVT_CA53_HW_CLOCK)
                char msg[64];
                uint64_t ns = hwclock_raw_ns((uint32_t *)hwclock_get_va());
                uint32_t rem = do_div(ns, 1000000);

                snprintf(msg, sizeof(msg), "on resume: bare timestamp %llu.%06u",
                         ns, rem);
                trace_early_message(msg);
#else
                trace_early_message("on resume");
#endif
        }
#endif

 Platform_wake:
	platform_resume_noirq(state);
	dpm_resume_noirq(PMSG_RESUME);

 Platform_early_resume:
	platform_resume_early(state);

 Devices_early_resume:
	dpm_resume_early(PMSG_RESUME);

 Platform_finish:
	platform_resume_finish(state);
	return error;
}

#ifdef CONFIG_POWER_SAVING_MODE
/**
 * suspend_devices_and_enter_pwsv - Suspend devices and enter system sleep state with pwsv.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter_pwsv(suspend_state_t state)
{
	int error;
	bool wakeup = false;

	if (!sleep_state_supported(state))
		return -ENOSYS;

	//#no need to call suspend_begin, sdp_suspend_begin is empty
	error = platform_suspend_begin(state);
	if (error)
		goto Close;

#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_SUSPEND);
#endif
	suspend_console();
	suspend_test_start();
	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		pr_err("PM:fail to suspend. [%s] Some devices failed to suspend, "
				"or early wake event detected\n", __func__);
#ifdef CONFIG_ALWAYS_INSTANT_ON
		if(is_always_instantOn())
			machine_restart_standby("suspend_devices_and_enter fail - reboot\n");
		else
			pm_power_off();
#endif
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;

	dpm_suspend_pwsv(PMSG_PWSV_SUSPEND);

	do {
		error = suspend_enter_pwsv(state, &wakeup);
	} while (!error && !wakeup && platform_suspend_again(state));

 Resume_devices:

	dpm_restore_pwsv(PMSG_PWSV_RESTORE);

	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);
	suspend_test_finish("resume devices");
	trace_suspend_resume(TPS("resume_console"), state, true);
	resume_console();
	
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_RESUME);
#endif
	trace_suspend_resume(TPS("resume_console"), state, false);

 Close:
	platform_resume_end(state);
	return error;

 Recover_platform:
	platform_recover(state);
	goto Resume_devices;
}
#endif

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;
	bool wakeup = false;

	if (!sleep_state_supported(state))
		return -ENOSYS;

	error = platform_suspend_begin(state);
	if (error)
		goto Close;
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_SUSPEND);
#endif
	suspend_console();
	suspend_test_start();
	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		pr_err("PM:fail to suspend. [%s] Some devices failed to suspend, "
				"or early wake event detected\n", __func__);
#ifdef CONFIG_ALWAYS_INSTANT_ON
		if(is_always_instantOn())
			machine_restart_standby("suspend_devices_and_enter fail - reboot\n");
		else
			pm_power_off();
#endif
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;

	do {
		error = suspend_enter(state, &wakeup);
	} while (!error && !wakeup && platform_suspend_again(state));

 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);
	suspend_test_finish("resume devices");
	trace_suspend_resume(TPS("resume_console"), state, true);
	resume_console();
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_RESUME);
#endif
	trace_suspend_resume(TPS("resume_console"), state, false);

 Close:
	platform_resume_end(state);
	return error;

 Recover_platform:
	platform_recover(state);
	goto Resume_devices;
}

/**
 * suspend_finish - Clean up before finishing the suspend sequence.
 *
 * Call platform code to clean up, restart processes, and free the console that
 * we've allocated. This routine is not called for hibernation.
 */
#ifdef CONFIG_POWER_SAVING_MODE
static void suspend_finish_prepare(void)
{
	suspend_thaw_processes_prepare();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

static void suspend_finish(void)
{
	dpm_resume_end_pwsv(PMSG_PWSV_RESUME);
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

#else
static void suspend_finish(void)
{
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}
#endif

/**
 * enter_state - Do common work needed to enter system sleep state.
 * @state: System sleep state to enter.
 *
 * Make sure that no one else is trying to put the system into a sleep state.
 * Fail if that's not the case.  Otherwise, prepare for system suspend, make the
 * system enter the given sleep state and clean up after wakeup.
 */
static int enter_state(suspend_state_t state)
{
	int error;
#ifdef CONFIG_POWER_SAVING_MODE
	int wdt_count = 0;
	int wdt_pwsvmode = 0;
#endif

	trace_suspend_resume(TPS("suspend_enter"), state, true);
	if (state == PM_SUSPEND_FREEZE) {
#ifdef CONFIG_PM_DEBUG
		if (pm_test_level != TEST_NONE && pm_test_level <= TEST_CPUS) {
			pr_warning("PM: Unsupported test mode for freeze state,"
				   "please choose none/freezer/devices/platform.\n");
			return -EAGAIN;
		}
#endif
	} else if (!valid_state(state)) {
		return -EINVAL;
	}
	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;

	if (state == PM_SUSPEND_FREEZE)
		freeze_begin();

	trace_suspend_resume(TPS("sync_filesystems"), 0, true);
	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");
	trace_suspend_resume(TPS("sync_filesystems"), 0, false);

	pr_debug("PM: Preparing system for %s sleep\n", pm_states[state]);
	error = suspend_prepare(state);
	if (error)
		goto Unlock;

	if (suspend_test(TEST_FREEZER))
		goto Finish;

	trace_suspend_resume(TPS("suspend_enter"), state, false);
	pr_debug("PM: Entering %s sleep\n", pm_states[state]);

#ifdef CONFIG_POWER_SAVING_MODE
	if(pwsv_mode_flags) {
#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING
		trace_early_message("PWSV mode Start");
#endif

		if(pwsv_mode_flags & PWSV_MODE_INACTIVE) {
			pwsv_current_mode = PWSV_MODE_INACTIVE;
			pr_err("PWSV] enter Inactive mode\n");
		} else if (pwsv_mode_flags & PWSV_MODE_IOT) {
			pwsv_current_mode = PWSV_MODE_IOT;
			pr_err("PWSV] enter IoT mode\n");
		} else {
			BUG_ON("Impossible routine : synchronization error?\n");
		}

		pwsv_init_wakeup_state();
		suspend_finish_prepare();
		pr_debug("PWSV] Micom mode MODE : %d USER MODE : %d\n",
				pwsv_current_mode, pwsv_get_wakeup_state());

		/* Power gating for unused device */
		dpm_poweroff_pwsv(PMSG_PWSV_POWEROFF);
		dpm_poweroff_pwsv_late(PMSG_PWSV_POWEROFF);

		error = disable_nonboot_cpus();
		if (error)
			pr_warning("PWSV] disable_nonboot_cpus fail\n");

		/* Run Necessary thread for each PowerSaving mode */
		pwsv_notice_pwsvmode_submicom(pwsv_mode_flags);
		pwsv_thaw_threads(state);
		pwsv_init_wdt_counter(wdt_count);

		while(true){

			/* Wait for Apps */
			msleep(PWSV_TIMESLICE);

			wdt_pwsvmode = pwsv_check_wdt(wdt_count++);
			if(wdt_pwsvmode > 0)
				pwsv_reboot(wdt_pwsvmode);
			else if(wdt_pwsvmode < 0)
				wdt_count = 0;

			if((pwsv_mode_flags & PWSV_MODE_SUSPEND) && 
				(pwsv_current_mode != PWSV_MODE_INACTIVE)) {
				pr_err("PWSV] enter suspend\n");
				/* freeze again */
				error = suspend_prepare(state);
				if (!error)
				{
					pm_restrict_gfp_mask();
					error = suspend_devices_and_enter_pwsv(state);	//SUSPEND&RESUME
					pm_restore_gfp_mask();
				
					/* prepare to thaw */
					/* pm freezing state is set again by freezing thawed process (suspend_prepare)
					   so, pwsv_freezing need to unset to unset pm freezing on suspend_finish_prepare */ 
					pwsv_freezing = false;
					suspend_finish_prepare();					
					pwsv_thaw_threads(state);					
					pwsv_clear_suspend_mode(); // clear mode in case of success, it means retry
				}
				else
				{
					pr_err("PWSV] fail to freeze tasks\n");
				}
			}

			if(pwsv_check_wakeup_req()) {
				pr_err("PWSV] wakeup Current MODE : %d Requested MODE : %d\n",
						pwsv_current_mode, pwsv_get_wakeup_state());

				pwsv_current_mode = pwsv_get_wakeup_state();
				if(pwsv_current_mode == PWSV_BOOTREASON_COLD_REBOOT) {

					/* Need to know boot reason and should not be stucked.
					   So, we use console_trylock in console_flush_on_panic */
					console_flush_on_panic(); 

					if(arm_pm_restart)
						arm_pm_restart(REBOOT_HARD, (char *)BOOT_TYPE_PANEL_POWER_KEY);
					while(1);
				}
#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING
				trace_early_message("Wakeup from PWSV mode ");
#endif
				break;
			}

			pwsv_print_status(++pwsv_loop_cnt);
		}
		pwsv_loop_cnt = 0;
		pwsv_reset_thaw_pids();		//initialize saved pid count to 0
		enable_nonboot_cpus();
#ifdef CONFIG_DPM_SHOW_TIME_IN_HWTRACING
		trace_early_message("IoT mode End");
#endif
	}else
#endif
	{
		pm_restrict_gfp_mask();
		error = suspend_devices_and_enter(state);
		pm_restore_gfp_mask();
	}
 Finish:
	pr_debug("PM: Finishing wakeup.\n");
	suspend_finish();

 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}

/**
 * pm_suspend - Externally visible function for suspending the system.
 * @state: System sleep state to enter.
 *
 * Check if the value of @state represents one of the supported states,
 * execute enter_state() and update system suspend statistics.
 */
int pm_suspend(suspend_state_t state)
{
	int error;

	if (state <= PM_SUSPEND_ON || state >= PM_SUSPEND_MAX)
		return -EINVAL;

#if defined(CONFIG_POWER_SAVING_MODE) || defined(CONFIG_ALWAYS_INSTANT_ON)
	micom_gpio = ioremap(GPIO_US_MCOM,GPIO_US_MCOM_SIZE);
#endif

	error = enter_state(state);
	if (error) {
		pr_err("PM:fail to suspend. [%s] returned error value %d\n"
				, __func__, error);
#ifdef CONFIG_ALWAYS_INSTANT_ON
		if(is_always_instantOn())
			machine_restart_standby("pm_suspend fail - reboot\n");
		else
			pm_power_off();
#endif
		suspend_stats.fail++;
		dpm_save_failed_errno(error);
	} else {
		suspend_stats.success++;
	}
#if defined(CONFIG_POWER_SAVING_MODE) || defined(CONFIG_ALWAYS_INSTANT_ON)
	iounmap(micom_gpio);
#endif
	return error;
}
EXPORT_SYMBOL(pm_suspend);
