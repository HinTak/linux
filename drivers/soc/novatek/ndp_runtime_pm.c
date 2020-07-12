
/*********************************************************************************************
 *
 *	ndp_runtime_pm.c 
 *
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <soc/nvt/ndp_runtime_pm.h>

static bool is_disabled = false;

static LIST_HEAD(ndp_runtime_pm_list);
static DEFINE_MUTEX(ndp_runtime_pm_lock);

/* should be protected by ndp_runtime_pm_lock */
static struct ndp_runtime_pm_node* __add_dev(char *name)
{
	struct ndp_runtime_pm_node *node;

	if (!name)
		return NULL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	strncpy(node->rpm.name, name, sizeof(node->rpm.name)-1);
	spin_lock_init(&node->rpm.lock);
	list_add(&node->list, &ndp_runtime_pm_list);
	return node;
}

static struct ndp_runtime_pm_node* __find_dev(char *name)
{
	struct ndp_runtime_pm_node *node = NULL;

	if (!name)
		return NULL;
	
	mutex_lock(&ndp_runtime_pm_lock);	
	list_for_each_entry(node, &ndp_runtime_pm_list, list) {
		if (!strncmp(node->rpm.name, name, sizeof(node->rpm.name)-1))
			goto exit;
	}
	node = __add_dev(name);
exit:
	mutex_unlock(&ndp_runtime_pm_lock);
	return node;
}

void* ndp_runtime_pm_getdev(char *name)
{
	if (is_disabled)
		return 0;

	return __find_dev(name);
}

int ndp_runtime_pm_tryuse(void *rpm_dev){

	struct ndp_runtime_pm_node *node = (struct ndp_runtime_pm_node *)rpm_dev;
	unsigned long flags;

	if (is_disabled)
		return 0;

	if (!rpm_dev)
		return -EINVAL;

	spin_lock_irqsave(&node->rpm.lock, flags);
	if(node->rpm.state != NDP_RPM_ACTIVE){
		/*
		pr_warn("[ndp_runtime_pm] %s is not active(%d), %s, %s.\n", \
			node->rpm.name, node->rpm.state, in_interrupt()?"Interrupt context":"Process context", current->comm);
		*/
		pr_warn("[ndp_runtime_pm] %s: called in suspend by %s\n", node->rpm.name, current->comm);	
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		return -ENXIO;
	}
	node->rpm.busy_count++;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int ndp_runtime_pm_disuse(void *rpm_dev){

	struct ndp_runtime_pm_node *node = (struct ndp_runtime_pm_node *)rpm_dev;
	unsigned long flags;

	if (is_disabled)
		return 0;

	if (!rpm_dev)
		return -EINVAL;

	spin_lock_irqsave(&node->rpm.lock, flags);
	node->rpm.busy_count--;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int ndp_runtime_pm_init_and_get(struct ndp_runtime_pm *rpm, char *name)
{
	struct ndp_runtime_pm_node *node;
	unsigned long flags;

	if (is_disabled)
		return 0;

	if (!rpm || !name)
		return -EINVAL;

	node = __find_dev(name);
	if (!node)
		return -ENODEV;

	spin_lock_irqsave(&node->rpm.lock, flags);
	node->rpm.dev = rpm->dev;
	node->rpm.suspend = rpm->suspend;
	node->rpm.resume = rpm->resume;
	node->rpm.usage_count++;
	node->rpm.state = NDP_RPM_ACTIVE;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int ndp_runtime_pm_get(char *name)
{
	struct ndp_runtime_pm_node *node;
	unsigned long flags;
	int warn_cnt = 0;

	if (is_disabled)
		return 0;

	if (!name)
		return -EINVAL;

	node = __find_dev(name);
	if (!node)
		return -ENOENT;

	spin_lock_irqsave(&node->rpm.lock, flags);

	while (node->rpm.state == NDP_RPM_SUSPENDING || node->rpm.state == NDP_RPM_RESUMING) {
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		msleep(5);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		if (warn_cnt++ > 1000) {
			pr_err("[ndp_runtime_pm] %s is still suspending or resuming (%d).\n", \
				node->rpm.name, node->rpm.state);
			warn_cnt = 0;
		}
	}
	
	if (node->rpm.usage_count <= 0 && node->rpm.dev && node->rpm.resume) {
		node->rpm.state = NDP_RPM_RESUMING;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		node->rpm.resume(node->rpm.dev);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		node->rpm.state = NDP_RPM_ACTIVE;
		pr_info("[ndp_runtime_pm] %s resumed.\n", node->rpm.name);
	}
	node->rpm.usage_count++;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int ndp_runtime_pm_put(char *name)
{
	struct ndp_runtime_pm_node *node;
	int timeout = 1000;
	unsigned long flags;
	int warn_cnt = 0;

	if (is_disabled)
		return 0;

	if (!name)
		return -EINVAL;

	node = __find_dev(name);
	if (!node)
		return -ENODEV;

	spin_lock_irqsave(&node->rpm.lock, flags);

	while (node->rpm.state == NDP_RPM_RESUMING) {
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		msleep(5);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		if (warn_cnt++ > 1000) {
			pr_err("[ndp_runtime_pm] %s is still resuming (%d).\n", \
				node->rpm.name, node->rpm.state);
			warn_cnt = 0;
		}
	}

	node->rpm.usage_count--;
	if (node->rpm.usage_count < 0) {
		pr_err("[ndp_runtime_pm] %s unmatched put.\n", node->rpm.name);
		node->rpm.usage_count = 0;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		return 0;
	}
	
	if (node->rpm.usage_count == 0 && node->rpm.dev && node->rpm.suspend) {
		node->rpm.state = NDP_RPM_SUSPENDING;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		
		while (node->rpm.busy_count > 0 && timeout-- > 0)
			msleep(10);
			
		if (node->rpm.busy_count > 0)
			pr_err("[ndp_runtime_pm] %s suspend timeout. device is still busy.\n", node->rpm.name);
			
		node->rpm.suspend(node->rpm.dev);
		spin_lock_irqsave(&node->rpm.lock, flags);
		node->rpm.state = NDP_RPM_SUSPENDED;
		pr_info("[ndp_runtime_pm] %s suspended.\n", node->rpm.name);
	}
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int ndp_runtime_pm_disable(void)
{
	is_disabled = true;
	return 0;
}

EXPORT_SYMBOL(ndp_runtime_pm_init_and_get);
EXPORT_SYMBOL(ndp_runtime_pm_get);
EXPORT_SYMBOL(ndp_runtime_pm_put);
EXPORT_SYMBOL(ndp_runtime_pm_tryuse);
EXPORT_SYMBOL(ndp_runtime_pm_disuse);
EXPORT_SYMBOL(ndp_runtime_pm_getdev);
EXPORT_SYMBOL(ndp_runtime_pm_disable);

MODULE_DESCRIPTION("helper funtions for NDP PM");
MODULE_LICENSE("Proprietary");

