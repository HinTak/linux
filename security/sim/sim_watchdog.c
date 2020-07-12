/**
 * @file sim_watchdog.c
 * 
 * @brief sim watchdog
 * 
 * @author Jinbum Park (jinb.park@samsung.com)
 * 
 * @note	Copyright (c) 2016 Samsung Electronics Co., Ltd All Rights Reserved 
 * PROPRIETARY/CONFIDENTIAL
 * This software is the confidential and proprietary information of
 * Samsung Electronics CO.LTD.,. ("Confidential Information").
 * You shall not disclose such Confidential Information and shall
 * use it only in accordance with the terms of the license agreement
 * you entered into with Samsung Electronics CO.LTD.,.
 *
 * SAMSUNG make no representations or warranties about the suitability of the software,
 * either express or implied, including but not limited to the implied warranties of merchantability,
 * fitness for a particular purpose, or non-infringement. 
 * SAMSUNG shall not be liable for any damages suffered by licensee as a result of using, 
 * modifying or distributing this software or its derivatives.
 *
 */

#include <linux/workqueue.h>
#include <linux/reboot.h>
#include "sim_util.h"

/**
 * Constants
*/
#define SIM_WATCHDOG_CHECK_MOUNT_PERIOD (1000 * 90)
#define SIM_WATCHDOG_FAIL_COUNT_FOR_CHECK_MOUNT (5)
#define SIM_WATCHDOG_MAX_FILE_NUM (32)
#define SIM_WATCHDOG_CONFIGURE_FILE "/zone/securezone/rootfs/usr/apps/sim/sim_watchdog.conf"

static unsigned int simWatchdogVerifyPeriodMsec = (1000 * 60);
static unsigned int simWatchdogFailCountForEnforcement = (10);
static unsigned int simWatchdogIsShutdown = (0);

static void SimWatchDogVerifyHandler(struct work_struct *work);
static void SimWatchDogCheckMountHandler(struct work_struct *work);

static struct workqueue_struct *simWatchDogWQ = NULL;
static DECLARE_DELAYED_WORK(simWatchDogVerify, SimWatchDogVerifyHandler);
static DECLARE_DELAYED_WORK(simWatchDogCheckMount, SimWatchDogCheckMountHandler);

static unsigned long simWatchDogVerifyPeriod = 0;
static unsigned long simWatchDogCheckMountPeriod = 0;
static int simWatchDogFailCount = 0;
static int simWatchDogCheckMountFailCount = 0;

/**
 * Return codes of verification
*/
enum
{
	SIM_WATCHDOG_VERIFY_SUCCESS = 0,
	SIM_WATCHDOG_VERIFY_FAIL,
	SIM_WATCHDOG_GET_FILE_ERROR,
};

/**
 * File list to be verified
*/
static char *simWatchDogFileList[SIM_WATCHDOG_MAX_FILE_NUM] = 
{
	NULL,
};

#define ForEachSimWatchDogFileList(fname)\
	unsigned int i;\
	char *fname = NULL;\
	for(i=0; (i < ARRAY_SIZE(simWatchDogFileList)) && (fname = simWatchDogFileList[i]); i++)

/**
 * Function Header for kuep verification
*/
#include <linux/export.h>
#include <linux/moduleloader.h>
#include <linux/ftrace_event.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/rcupdate.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/vermagic.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <linux/license.h>
#include <asm/sections.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>
#include <linux/async.h>
#include <linux/percpu.h>
#include <linux/kmemleak.h>
#include <linux/jump_label.h>
#include <linux/pfn.h>
#include <linux/bsearch.h>
#include <uapi/linux/module.h>

#include <linux/sf_security.h>

/**
 * Verification Function
*/
static int SimWatchDogVerifyFile(char *fname)
{
	int ret = SIM_WATCHDOG_VERIFY_SUCCESS;
	int kuepRet = 0;
	unsigned int i;
	struct file *file = NULL;

	ret = SimGetFile(fname, &file);
	if(ret)
	{
		SIM_PRINT_ERROR("SimGetFile error : %s", fname);
		return SIM_WATCHDOG_GET_FILE_ERROR;
	}

	kuepRet = sf_security_kernel_module_from_file(file);
	if(kuepRet)
	{
		ret = SIM_WATCHDOG_VERIFY_FAIL;
		SIM_PRINT_ERROR("sf_security_kernel_module_from_file fail : %s [%d]", fname, kuepRet);
	}

	SimCloseFile(file);
	return ret;
}

static int SimWatchDogVerifyFileList(void)
{
	int ret;

	ForEachSimWatchDogFileList(fname)
	{
		ret = SimWatchDogVerifyFile(fname);
		if(ret != SIM_WATCHDOG_VERIFY_SUCCESS)
		{
			return ret;
		}
	}
	
	return SIM_WATCHDOG_VERIFY_SUCCESS;
}

/**
 * Enforcement Function
*/
static void SimWatchDogDoEnforcement(void)
{
	simWatchDogFailCount = 0;
	simWatchDogCheckMountFailCount = 0;

	if(simWatchdogIsShutdown)
	{
		kernel_power_off();
	}
	else
	{
		SIM_PRINT_ERROR("Logging Enforcement");
	}
}

static int SimWatchDogParseAndFillConfigure(char *str)
{
	int ret = 0;
	unsigned int i = 0;
	unsigned int arraySize = ARRAY_SIZE(simWatchDogFileList);
	char *token;
	char *dupstr = NULL;
	const char *delim = "\n";

	if(!str || !arraySize)
	{
		return -1;
	}

	dupstr = SimStrdup(str);
	if(!dupstr)
	{
		return -1;
	}

	while( (token = strsep(&dupstr, delim)) && (i < arraySize) )
	{
		/* verify period msec */
		if(token[0] == '^')
		{
			ret = kstrtouint((const char*)(token + 1), 10, &simWatchdogVerifyPeriodMsec);
			if(ret)
			{
				break;
			}
			simWatchDogVerifyPeriod = msecs_to_jiffies(simWatchdogVerifyPeriodMsec);
		}
		/* fail count for enforcement */
		else if(token[0] == '&')
		{
			ret = kstrtouint((const char*)(token + 1), 10, &simWatchdogFailCountForEnforcement);
			if(ret)
			{
				break;
			}
		}
		/* enforcement rule */
		else if(token[0] == '*')
		{
			ret = kstrtouint((const char*)(token + 1), 10, &simWatchdogIsShutdown);
			if(ret)
			{
				break;
			}
		}
		/* file name to be verified */
		else if(token[0] == '-')
		{
			simWatchDogFileList[i] = SimStrdup(token + 1);
			if(simWatchDogFileList[i] == NULL)
			{
				ret = -1;
				break;
			}
			i++;
		}
		/* Etc */
	}
	
	if(i == 0)
	{
		SIM_PRINT_ERROR("Nothing to be verified");
		ret = -1;
	}

	if(dupstr)
	{
		kfree(dupstr);
	}
	return ret;
}


/**
 * Read configure file and Fill simWatchDogFileList
*/
static int SimWatchDogReadConfigureFile(void)
{
	int ret;
	char *buf = NULL;

	/* Verify configure file first */
	ret = SimWatchDogVerifyFile(SIM_WATCHDOG_CONFIGURE_FILE);
	if(ret != SIM_WATCHDOG_VERIFY_SUCCESS)
	{
		return ret;
	}

	ret = SimReadFile(SIM_WATCHDOG_CONFIGURE_FILE, &buf);
	if(ret)
	{
		return ret;
	}

	ret = SimWatchDogParseAndFillConfigure(buf);
	return ret;
}

/**
 * Workqueue Handler for Verification
*/
static void SimWatchDogVerifyHandler(struct work_struct *work)
{
	int ret;

	/* Do WatchDog-Checking */
	ret = SimWatchDogVerifyFileList();
	if(ret != SIM_WATCHDOG_VERIFY_SUCCESS) 
	{
		simWatchDogFailCount++;
	}

	/* Do enforcement if fail count is more than ** */
	if(simWatchDogFailCount >= simWatchdogFailCountForEnforcement)
	{
		/* After call SimWatchDogDoEnforcement(), VerifyHandler will never work agein */
		SimWatchDogDoEnforcement();
		return;
	}

	/* Queue work again */
	queue_delayed_work(simWatchDogWQ, &simWatchDogVerify, simWatchDogVerifyPeriod);
}

/**
 * Workqueue Handler for Checking mount of rootfs
*/
static void SimWatchDogCheckMountHandler(struct work_struct *work)
{
	int ret;

	ret = SimCheckMountRoot();
	if(ret)
	{
		SIM_PRINT_ERROR("SimCheckMountRoot error");
		goto error;
	}

	ret = SimWatchDogReadConfigureFile();
	if(ret)
	{
		/* Stop watchdog!! */
		SIM_PRINT_ERROR("SimWatchDogReadConfigureFile error. Stop watchdog!!");
		return;
	}

	/* Start VerifyHandler */
	SIM_PRINT_DEBUG("Start SimWatchDogVerifyHandler");
	queue_delayed_work(simWatchDogWQ, &simWatchDogVerify, simWatchDogVerifyPeriod);
	return;

error:
	if(++simWatchDogCheckMountFailCount >= SIM_WATCHDOG_FAIL_COUNT_FOR_CHECK_MOUNT)
	{
		/* After call SimWatchDogDoEnforcement(), CheckMountHandler will never work again */
		SIM_PRINT_ERROR("CheckMount fail. Do enforcement");
		SimWatchDogDoEnforcement();
	}
	else
	{
		/* Do CheckMountHandler again */
		queue_delayed_work(simWatchDogWQ, &simWatchDogCheckMount, simWatchDogCheckMountPeriod);
	}
	return;
}

static __init int SimWatchDogInit(void)
{
	simWatchDogCheckMountPeriod = msecs_to_jiffies(SIM_WATCHDOG_CHECK_MOUNT_PERIOD);

	if(!simWatchDogWQ)
	{
		simWatchDogWQ = create_singlethread_workqueue("sim_watchdog");
	}

	if(simWatchDogWQ)
	{
		queue_delayed_work(simWatchDogWQ, &simWatchDogCheckMount, simWatchDogCheckMountPeriod);
	}
	else
	{
		SIM_PRINT_ERROR("create_singlethread_workqueue error");
	}

	return 0;
}

__initcall(SimWatchDogInit);

