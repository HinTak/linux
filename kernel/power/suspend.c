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
#include <trace/early.h>

#ifdef CONFIG_IOTMODE
#include <linux/delay.h>
#endif
#ifdef CONFIG_SDP_HW_CLOCK
#include <mach/sdp_hwclock.h>
#elif defined CONFIG_NVT_HW_CLOCK
#include <mach/nvt_hwclock.h>
#endif

#include "power.h"

struct pm_sleep_state pm_states[PM_SUSPEND_MAX] = {
	[PM_SUSPEND_FREEZE] = { .label = "freeze", .state = PM_SUSPEND_FREEZE },
	[PM_SUSPEND_STANDBY] = { .label = "standby", },
	[PM_SUSPEND_MEM] = { .label = "mem", },
};


static const struct platform_suspend_ops *suspend_ops;

static bool need_suspend_ops(suspend_state_t state)
{
	return !!(state > PM_SUSPEND_FREEZE);
}

static DECLARE_WAIT_QUEUE_HEAD(suspend_freeze_wait_head);
static bool suspend_freeze_wake;

static void freeze_begin(void)
{
	suspend_freeze_wake = false;
}

static void freeze_enter(void)
{
	wait_event(suspend_freeze_wait_head, suspend_freeze_wake);
}

void freeze_wake(void)
{
	suspend_freeze_wake = true;
	wake_up(&suspend_freeze_wait_head);
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

/**
 * suspend_set_ops - Set the global suspend method table.
 * @ops: Suspend operations to use.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	suspend_state_t i;

	lock_system_sleep();

	suspend_ops = ops;
	for (i = PM_SUSPEND_STANDBY; i <= PM_SUSPEND_MEM; i++)
		pm_states[i].state = valid_state(i) ? i : 0;

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

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
	if (pm_test_level == level) {
		printk(KERN_INFO "suspend debug: Waiting for 5 seconds.\n");
		mdelay(5000);
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

	if (need_suspend_ops(state) && (!suspend_ops || !suspend_ops->enter))
		return -EPERM;

	pm_prepare_console();

	error = pm_notifier_call_chain(PM_SUSPEND_PREPARE);
	if (error)
		goto Finish;

	error = suspend_freeze_processes();
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
void __attribute__ ((weak)) arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}
#ifdef CONFIG_PM_CRC_CHECK
extern void save_suspend_crc(void);
extern void compare_resume_crc(void);
#endif

unsigned int *micom_gpio;

#ifdef CONFIG_IOTMODE
#include <linux/cpufreq.h>
#include <linux/platform_data/sdp-cpufreq.h>
extern void iot_thaw_thread(suspend_state_t state);
extern void iot_run_resume_callbacks(void);
extern void set_iot_counter(int counter);
extern void iot_reboot(int pid, char *process_name);
extern void machine_restart(char *cmd);
extern int read_iot_counter(void);
extern int sdp_iotmode_powerdown(bool en);
extern int iotmode;
extern int wakeup_iot;
extern bool iot_onoff;

#define IOT_TIMESLICE		50 /* 50ms */
#define MAX_IOT_TIMEOUT		10000
#define MIN_IOT_TIMEOUT		10
#define IOT_WDT_TIMEOUT		(20000/IOT_TIMESLICE)	/* iot hub app watchdog timeout = 20s */

#define DEFAULT_MODE	-1

#define OFF_MODE 	0
#define TV_MODE 	1
#define IOT_MODE 	2
#define COLD_REBOOT 	3

#define BOOT_TYPE_WOWLAN_PULSE7 	48 
#define BOOT_TYPE_IOT_KEEP_ALIVE       53
#define BOOT_TYPE_ZIGBEE               54
#define POWER_WAKEUP_REASON_E_PHY_ANYPACKET  55  
#define BOOT_TYPE_PANEL_POWER_KEY      32

#if defined(CONFIG_ARCH_SDP1404)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
#define GPIO_US_MCOM		0x1AB00000
#define GPIO_US_MCOM_SIZE	0x2000
#define ALIVE_MICOM		0x18
#define SUS_MICOM		0x25
int iot_micom_gpio(void)
{
	int ret=0;
	unsigned int * p_gpio0, * p_gpio1 , * p_gpio2;
	unsigned int save_reg0 , save_reg1 , save_reg2;
	if(!iot_onoff)
		return TV_MODE;

	/* US P 0.1 */
        p_gpio0 = micom_gpio + 0x465;
	save_reg0 = *(volatile unsigned int*)p_gpio0; 
	*(volatile unsigned int*)p_gpio0 |= (0x2 << 4);
	/* US P 2.3 */
        p_gpio1 = micom_gpio + 0x486;
	save_reg1 = *(volatile unsigned int*)p_gpio1; 
	*(volatile unsigned int*)p_gpio1 |= (0x2 << 12);
	/* US P 2.4 */
        p_gpio2 = micom_gpio + 0x486;
	save_reg2 = *(volatile unsigned int*)p_gpio2; 
	*(volatile unsigned int*)p_gpio2 |= (0x2 << 16);
	
	p_gpio0 = micom_gpio + 0x467;
	ret |= ((*(volatile unsigned int*)p_gpio0>>1) & 1) << 2;
		
	p_gpio1 = micom_gpio + 0x488;
	ret |= ((*(volatile unsigned int*)p_gpio1>>3) & 1) << 1;

	p_gpio2 = micom_gpio + 0x488;
	ret |= ((*(volatile unsigned int*)p_gpio2>>4) & 1);

	*(volatile unsigned int*)p_gpio0 = save_reg0; 
	*(volatile unsigned int*)p_gpio1 = save_reg1; 
	*(volatile unsigned int*)p_gpio2 = save_reg2; 

	return ret;
}

#elif defined(CONFIG_ARCH_SDP1406)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
#define GPIO_US_MCOM		0x800700
#define GPIO_US_MCOM_SIZE	0x100
int iot_micom_gpio(void)
{
	int ret=0;
	unsigned int * p_gpio;
	if(!iot_onoff)
		return TV_MODE;

	/* MICOM register */
	p_gpio = micom_gpio;
	ret = *(volatile unsigned int*)p_gpio;

	if( ret > TV_MODE) return IOT_MODE;
	else return ret;
}

#elif defined(CONFIG_ARCH_SDP1501)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
#define GPIO_US_MCOM            	0x7C1080
#define GPIO_US_MCOM_SIZE       	0x100
int iot_micom_gpio(void)
{
        int ret=0;
        unsigned int * p_gpio;

        if(!iot_onoff)
                return TV_MODE;

        /* MICOM register */
        p_gpio = micom_gpio;
        ret = *(volatile unsigned int*)(p_gpio+2);

	switch(ret)
	{
		case BOOT_TYPE_WOWLAN_PULSE7:
		case BOOT_TYPE_IOT_KEEP_ALIVE:
		case BOOT_TYPE_ZIGBEE:
		case POWER_WAKEUP_REASON_E_PHY_ANYPACKET:
			return IOT_MODE;
                case BOOT_TYPE_PANEL_POWER_KEY:
                        return COLD_REBOOT;
		default:
			printk(KERN_ERR"IOT] Micom Boot reason : %d\n",ret);
			return TV_MODE;
	}
}

#else
int iot_micom_gpio(void)
{
	return TV_MODE;
}
#endif
#endif //end of CONFIG_IOTMODE

extern void machine_restart_standby(char *cmd);
extern int get_onboot_version(void);

#if defined(CONFIG_ARCH_SDP1404) || defined(CONFIG_ARCH_SDP1406)
int Is_always_Ready(void)
{
	int ret = false;
	int target = get_onboot_version(); 
	printk(KERN_ERR"target year : %d\n",target);
	if(2016 == target)
		ret = true;
	return ret;
}
#elif defined(CONFIG_ARCH_SDP1501)
int Is_always_Ready(void)
{
	int ret = false;
	unsigned int * p_gpio;

	p_gpio = micom_gpio;
	ret = *(volatile unsigned int*)(p_gpio+5);

	printk(KERN_ERR"IOT] Micom Master Standby On Support : %d\n",ret);

	return ret;
}
#else
int Is_always_Ready(void)
{
	int ret = false;
	return ret;
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
	if (need_suspend_ops(state) && suspend_ops->prepare) {
		error = suspend_ops->prepare();
		if (error)
			goto Platform_finish;
	}

	error = dpm_suspend_end(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down\n");
		goto Platform_finish;
	}

	if (need_suspend_ops(state) && suspend_ops->prepare_late) {
		error = suspend_ops->prepare_late();
		if (error)
			goto Platform_wake;
	}

	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	/*
	 * PM_SUSPEND_FREEZE equals
	 * frozen processes + suspended devices + idle processors.
	 * Thus we should invoke freeze_enter() soon after
	 * all the devices are suspended.
	 */
	if (state == PM_SUSPEND_FREEZE) {
		freeze_enter();
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
#ifdef CONFIG_PM_CRC_CHECK
			save_suspend_crc();
#endif
			error = suspend_ops->enter(state);
#ifdef CONFIG_PM_CRC_CHECK
			compare_resume_crc();
#endif
			events_check_enabled = false;
		}
		syscore_resume();
	}

	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

Enable_cpus:
	enable_nonboot_cpus();
#ifdef CONFIG_EARLY_TRACING
	{
#if defined(CONFIG_SDP_HW_CLOCK) || defined(CONFIG_NVT_HW_CLOCK)
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
#ifdef CONFIG_IOTMODE   
	iotmode = iot_micom_gpio();
#endif
 Platform_wake:
	if (need_suspend_ops(state) && suspend_ops->wake)
		suspend_ops->wake();

	dpm_resume_start(PMSG_RESUME);

 Platform_finish:
	if (need_suspend_ops(state) && suspend_ops->finish)
		suspend_ops->finish();

	return error;
}

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;
	bool wakeup = false;

	if (need_suspend_ops(state) && !suspend_ops)
		return -ENOSYS;

	trace_machine_suspend(state);
	if (need_suspend_ops(state) && suspend_ops->begin) {
		error = suspend_ops->begin(state);
		if (error)
			goto Close;
	}
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_SUSPEND);
#endif
	suspend_console();
	ftrace_stop();
	suspend_test_start();

	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to suspend\n");
		printk(KERN_ERR "System will be down\n");
		if(Is_always_Ready())
			machine_restart_standby("Suspend fail - reboot\n");
		else
			pm_power_off();
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;

	kasan_suspend();

	do {
		error = suspend_enter(state, &wakeup);
	} while (!error && !wakeup && need_suspend_ops(state)
		&& suspend_ops->suspend_again && suspend_ops->suspend_again());

	kasan_resume();
 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);

    suspend_test_finish("resume devices");
	ftrace_start();
	resume_console();
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_RESUME);
#endif
 Close:
	if (need_suspend_ops(state) && suspend_ops->end)
		suspend_ops->end();
	trace_machine_suspend(PWR_EVENT_EXIT);
	return error;

 Recover_platform:
	if (need_suspend_ops(state) && suspend_ops->recover)
		suspend_ops->recover();
	goto Resume_devices;
}

/**
 * suspend_finish - Clean up before finishing the suspend sequence.
 *
 * Call platform code to clean up, restart processes, and free the console that
 * we've allocated. This routine is not called for hibernation.
 */
#ifdef CONFIG_IOTMODE
static void suspend_finish_prepare(void)
{
	suspend_thaw_processes_prepare();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

static void suspend_finish(void)
{
	dpm_resume_end_iot(PMSG_IOTRESUME);
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

#ifdef CONFIG_IOTMODE
	int iot_count = 0;
	int wdt_count = 0;
	int cur_wdt_counter = 0;
	int last_wdt_counter = 0;
#endif
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

	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");

	pr_debug("PM: Preparing system for %s sleep\n", pm_states[state].label);
	error = suspend_prepare(state);
	if (error)
		goto Unlock;

	if (suspend_test(TEST_FREEZER))
		goto Finish;

	pr_debug("PM: Entering %s sleep\n", pm_states[state].label);


#ifdef CONFIG_IOTMODE
	if(iot_onoff){
		trace_early_message("IoT mode Start");
		iotmode = IOT_MODE;
		wakeup_iot = DEFAULT_MODE;
		suspend_finish_prepare();
		pr_debug("IOT] Micom mode MODE : %d USER MODE : %d\n",
					iotmode, wakeup_iot);
		
		/* Power gating for unused device */
		dpm_prepare_iot(PMSG_IOTPOWEROFF);
		dpm_poweroff_iot(PMSG_IOTPOWEROFF);
	
		error = disable_nonboot_cpus();
		if (error)
			pr_warning("IOT] disable_nonboot_cpus fail\n");
		sdp_iotmode_powerdown(true);
		
		/* Run IOT thread */
		iot_thaw_thread(state);
		set_iot_counter(wdt_count);
	
	        while(true){
	
			iot_count++;
			/* Wait for Hun apps */
	                msleep(IOT_TIMESLICE);
			cur_wdt_counter = read_iot_counter();
	
			/* IOT watchdog */
			if(last_wdt_counter < cur_wdt_counter){
				last_wdt_counter = cur_wdt_counter;
				wdt_count = 0;
			}else{
				if(wdt_count > IOT_WDT_TIMEOUT){
					iot_reboot(0,"hub");
				}
				wdt_count++;
			}
	
	               	if(wakeup_iot != DEFAULT_MODE){
	               		pr_debug("IOT] wakeup_iot hub app %d    MODE : %d USER MODE : %d\n",
				iot_count , iotmode, wakeup_iot);
	               	        iotmode = wakeup_iot;
				break;
	               	}

			iotmode = iot_micom_gpio();

                        if(iotmode == COLD_REBOOT)
                        {
                                if(arm_pm_restart)
                                        arm_pm_restart('h',(char *)BOOT_TYPE_PANEL_POWER_KEY);          
                                while(1); // no further life
                        }

			if(iotmode == TV_MODE){
				trace_early_message("Wakeup TV mode");
				break;
			}
			
		        printk(KERN_ERR"IOT] wait hub app %d    MODE : %d USER MODE : %d\n",
						iot_count , iotmode, wakeup_iot);
		}
		sdp_iotmode_powerdown(false);
		enable_nonboot_cpus();

		trace_early_message("IoT mode End");
	}else{
	        pm_restrict_gfp_mask();
	        error = suspend_devices_and_enter(state);
	        pm_restore_gfp_mask();
	}
	
Finish:
	suspend_finish();
	trace_early_message("Tv mode Start");
	pr_debug("PM: Finishing wakeup.\n");
#else
	pm_restrict_gfp_mask();
	error = suspend_devices_and_enter(state);
	pm_restore_gfp_mask();

Finish:
	suspend_finish();
	pr_debug("PM: Finishing wakeup.\n");
#endif

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
#ifdef CONFIG_SCHED_HMP
	set_hmp_boost(1);
#endif
#ifdef CONFIG_IOTMODE
	micom_gpio = ioremap(GPIO_US_MCOM,GPIO_US_MCOM_SIZE);
#endif
	error = enter_state(state);
	if (error) {
		if(Is_always_Ready())
			machine_restart_standby("Suspend fail - reboot\n");
		else
			pm_power_off();
		suspend_stats.fail++;
		dpm_save_failed_errno(error);
#ifdef CONFIG_SCHED_HMP
		set_hmp_boost(0);
#endif
	} else {
		suspend_stats.success++;
#ifdef CONFIG_SCHED_HMP
		set_hmp_boost(0);
		set_hmp_boostpulse(180000000);
#endif
#ifdef CONFIG_IOTMODE
	iounmap(micom_gpio);
#endif
	}
	return error;
}
EXPORT_SYMBOL(pm_suspend);
