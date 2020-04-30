
/*********************************************************************************************
 *
 *	nvt_iotmode.c
 *
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/suspend.h>

struct nvt_iotmode_ops {
	struct list_head list;
	int priority;
	struct device *dev;
	int (*poweroff)(struct device *dev);
	int (*resume)(struct device *dev);
};

static LIST_HEAD(nvt_iotmode_private_ops);
static DEFINE_MUTEX(nvt_iotmode_private_ops_lock);
static struct notifier_block pm_np;

#define STBC_IR_POWER_FLAG	0x00010000
unsigned int *p_stbc;
extern unsigned int *micom_gpio;
extern int pwsv_is_running(void);

/* nvt_check_stbc_ir is to check if user presses power key or not.
 * If user presses power key, stbc set 1 in reg.
 * After we read, we must clean it for next reading.
 */
static unsigned int nvt_check_stbc_ir(void)
{
	u32 rval = 0;
	u32 wval = 0;

	p_stbc = micom_gpio;

	rval = *(volatile unsigned int*)(p_stbc);
	pr_info("STBC read reg:%x\n", rval);
	if (rval & STBC_IR_POWER_FLAG) {
		/* After read, must clean flag */
		wval = rval & (~STBC_IR_POWER_FLAG);
		*(volatile unsigned int*)(p_stbc) = wval;
		pr_info("STBC write reg:%x\n", wval);
	}
	
	return rval;
}

int check_stbc_power_key(void)
{
	int ret = 0;

	if (nvt_check_stbc_ir() & STBC_IR_POWER_FLAG) {
		/* Mean user pressed power key */
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL(check_stbc_power_key);

int nvt_iotmode_do_poweroff_ops(void)
{
	struct nvt_iotmode_ops *ops;
	list_for_each_entry(ops, &nvt_iotmode_private_ops, list) {
		if (ops->poweroff)
			ops->poweroff(ops->dev);
	}
	return 0;
}

int nvt_iotmode_do_resume_ops(void)
{
	struct nvt_iotmode_ops *ops;
	list_for_each_entry(ops, &nvt_iotmode_private_ops, list) {
		if (ops->resume)
			ops->resume(ops->dev);
	}	
	return 0;
}

int nvt_iotmode_powerdown(bool en)
{
	pr_info("%s (%d).\n", __func__, en);
	if (en)
		nvt_iotmode_do_poweroff_ops();
	else
		nvt_iotmode_do_resume_ops();
	return 0;
}

EXPORT_SYMBOL(nvt_iotmode_powerdown);

int nvt_iotmode_register_ops(int priority, struct device *dev, int (*poweroff)(struct device*), int (*resume)(struct device*))
{
	struct nvt_iotmode_ops *ops, *new_ops;

	new_ops = kzalloc(sizeof(*new_ops), GFP_KERNEL);
	if (!new_ops)
		return -ENOMEM;

	new_ops->priority = priority;
	new_ops->dev = dev;
	new_ops->poweroff = poweroff;
	new_ops->resume = resume;

	mutex_lock(&nvt_iotmode_private_ops_lock);
	
	list_for_each_entry_reverse(ops, &nvt_iotmode_private_ops, list) {
		if (ops->priority < new_ops->priority) {
			list_add(&new_ops->list, &ops->list);
			goto done;
		}
	}
	
	list_add(&new_ops->list, &nvt_iotmode_private_ops);
done:
	mutex_unlock(&nvt_iotmode_private_ops_lock);
	
	return 0;
}

EXPORT_SYMBOL(nvt_iotmode_register_ops);

static int pwr_saving_notify(struct notifier_block *nb,
                               unsigned long mode, void *_unused)
{
	int result;

	if (mode == PM_POST_SUSPEND) {
		if (pwsv_is_running())
			/* Before in power saving loop, we clean flag first */
			nvt_check_stbc_ir();

	}

	return 0;
}

static int __init nvt_powersaving(void)
{
	pm_np.notifier_call = pwr_saving_notify;
	pm_np.priority = 0;
	return register_pm_notifier(&pm_np);
}

late_initcall(nvt_powersaving);
