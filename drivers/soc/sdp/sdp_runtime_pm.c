
/*********************************************************************************************
 *
 *	sdp_runtime_pm.c 
 *
 *	author : yongjin79.kim@samsung.com
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


#include <soc/sdp/sdp_runtime_pm.h>


/* state 가 필요한 이유
   usage_count 로만 판단할 경우
   resume 전에 usage_count 를 변경해 놓아야 함, 그럴 경우 
   1. resume 이 되는 중에 (usage_count 가 0 보다 큰 상태) tryuse 가 성공할 수 있음 
   2. suspend / resume 간에는 서로 보호 되지 않음 usage_count 가 서로 반대 조건을 보고 있으므로
*/ 

static bool is_disabled = false;

static LIST_HEAD(sdp_runtime_pm_list);
static DEFINE_MUTEX(sdp_runtime_pm_lock);

/* should be protected by sdp_runtime_pm_lock */
static struct sdp_runtime_pm_node* __add_dev(char *name)
{
	struct sdp_runtime_pm_node *node;

	if (!name)
		return NULL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	strncpy(node->rpm.name, name, sizeof(node->rpm.name)-1);
	spin_lock_init(&node->rpm.lock);
	list_add(&node->list, &sdp_runtime_pm_list);
	return node;
}

static struct sdp_runtime_pm_node* __find_dev(char *name)
{
	struct sdp_runtime_pm_node *node = NULL;

	if (!name)
		return NULL;
	
	mutex_lock(&sdp_runtime_pm_lock);	
	list_for_each_entry(node, &sdp_runtime_pm_list, list) {
		if (!strncmp(node->rpm.name, name, sizeof(node->rpm.name)-1))
			goto exit;
	}
	node = __add_dev(name);
exit:
	mutex_unlock(&sdp_runtime_pm_lock);
	return node;
}

void* sdp_runtime_pm_getdev(char *name)
{
	if (is_disabled)
		return 0;

	return __find_dev(name);
}

int sdp_runtime_pm_tryuse(void *rpm_dev){

	struct sdp_runtime_pm_node *node = (struct sdp_runtime_pm_node *)rpm_dev;
	unsigned long flags;

	if (is_disabled)
		return 0;

	if (!rpm_dev)
		return -EINVAL;

	spin_lock_irqsave(&node->rpm.lock, flags);
	if(node->rpm.state != SDP_RPM_ACTIVE){
		/*
		pr_warn("[sdp_runtime_pm] %s is not active(%d), %s, %s.\n", \
			node->rpm.name, node->rpm.state, in_interrupt()?"Interrupt context":"Process context", current->comm);
		*/
		pr_warn("[sdp_runtime_pm] %s: called in suspend by %s\n", node->rpm.name, current->comm);	
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		return -ENXIO;
	}
	node->rpm.busy_count++;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int sdp_runtime_pm_disuse(void *rpm_dev){

	struct sdp_runtime_pm_node *node = (struct sdp_runtime_pm_node *)rpm_dev;
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

int sdp_runtime_pm_init_and_get(struct sdp_runtime_pm *rpm, char *name)
{
	struct sdp_runtime_pm_node *node;
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
	node->rpm.state = SDP_RPM_ACTIVE;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int sdp_runtime_pm_get(char *name)
{
	struct sdp_runtime_pm_node *node;
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

	while (node->rpm.state == SDP_RPM_SUSPENDING || node->rpm.state == SDP_RPM_RESUMING) {
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		msleep(5);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		if (warn_cnt++ > 1000) {
			pr_err("[sdp_runtime_pm] %s is still suspending or resuming (%d).\n", \
				node->rpm.name, node->rpm.state);
			warn_cnt = 0;
		}
	}
	
	if (node->rpm.usage_count <= 0 && node->rpm.dev && node->rpm.resume) {
		node->rpm.state = SDP_RPM_RESUMING;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		node->rpm.resume(node->rpm.dev);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		node->rpm.state = SDP_RPM_ACTIVE;
		pr_info("[sdp_runtime_pm] %s resumed.\n", node->rpm.name);
	}
	node->rpm.usage_count++;
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int sdp_runtime_pm_put(char *name)
{
	struct sdp_runtime_pm_node *node;
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

	while (node->rpm.state == SDP_RPM_RESUMING) {
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		msleep(5);
		spin_lock_irqsave(&node->rpm.lock, flags);	
		if (warn_cnt++ > 1000) {
			pr_err("[sdp_runtime_pm] %s is still resuming (%d).\n", \
				node->rpm.name, node->rpm.state);
			warn_cnt = 0;
		}
	}

	node->rpm.usage_count--;
	if (node->rpm.usage_count < 0) {
		pr_err("[sdp_runtime_pm] %s unmatched put.\n", node->rpm.name);
		node->rpm.usage_count = 0;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		return 0;
	}
	
	if (node->rpm.usage_count == 0 && node->rpm.dev && node->rpm.suspend) {
		node->rpm.state = SDP_RPM_SUSPENDING;
		spin_unlock_irqrestore(&node->rpm.lock, flags);
		
		while (node->rpm.busy_count > 0 && timeout-- > 0)
			msleep(10);
			
		if (node->rpm.busy_count > 0)
			pr_err("[sdp_runtime_pm] %s suspend timeout. device is still busy.\n", node->rpm.name);
			
		node->rpm.suspend(node->rpm.dev);
		spin_lock_irqsave(&node->rpm.lock, flags);
		node->rpm.state = SDP_RPM_SUSPENDED;
		pr_info("[sdp_runtime_pm] %s suspended.\n", node->rpm.name);
	}
	spin_unlock_irqrestore(&node->rpm.lock, flags);

	return 0;
}

int sdp_runtime_pm_disable(void)
{
	is_disabled = true;
	return 0;
}

EXPORT_SYMBOL(sdp_runtime_pm_init_and_get);
EXPORT_SYMBOL(sdp_runtime_pm_get);
EXPORT_SYMBOL(sdp_runtime_pm_put);
EXPORT_SYMBOL(sdp_runtime_pm_tryuse);
EXPORT_SYMBOL(sdp_runtime_pm_disuse);
EXPORT_SYMBOL(sdp_runtime_pm_getdev);
EXPORT_SYMBOL(sdp_runtime_pm_disable);

MODULE_AUTHOR("yongjin79.kim@samsung.com");
MODULE_DESCRIPTION("helper funtions for SDP PM");
MODULE_LICENSE("Proprietary");


