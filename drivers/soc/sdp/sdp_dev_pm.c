
/*********************************************************************************************
 *
 *	sdp_dev_pm.c 
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

#include <soc/sdp/sdp_dev_pm.h>

#define SDP_DEV_PM_STR_N 16

struct sdp_dev_pm_dev_node {
	struct list_head list;
	char name[SDP_DEV_PM_STR_N];
	struct sdp_dev_pm pm;
};

struct sdp_dev_pm_grp_node {
	struct list_head list;
	char name[SDP_DEV_PM_STR_N];
};

static LIST_HEAD(sdp_dev_pm_list);
static DEFINE_MUTEX(sdp_dev_pm_lock);

int sdp_dev_pm_register(struct sdp_dev_pm *pm, char *name)
{
	struct sdp_dev_pm_dev_node *node;
	struct device *dev;

	if (!pm)
		return -EINVAL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->pm = *pm;
	strncpy(node->name, name, sizeof(node->name)-1);

	dev = (struct device *)node->pm.dev;
	dev->power.pwsv_magic = PWSV_MAGIC;
	
	mutex_lock(&sdp_dev_pm_lock);	
	list_add(&node->list, &sdp_dev_pm_list);
	mutex_unlock(&sdp_dev_pm_lock);

	return 0;
}

int sdp_dev_pm_group_init(struct sdp_dev_pm_group *grp)
{
	if (!grp)
		return -EINVAL;

	INIT_LIST_HEAD(&grp->head);
	mutex_init(&grp->lock);

	return 0;
}

static struct sdp_dev_pm_dev_node* find_dev(char *name)
{
	struct sdp_dev_pm_dev_node *node;
	
	mutex_lock(&sdp_dev_pm_lock);	
	list_for_each_entry(node, &sdp_dev_pm_list, list) {
		if (!strncmp(node->name, name, sizeof(node->name))) {
			mutex_unlock(&sdp_dev_pm_lock);
			return node;
		}
	}
	mutex_unlock(&sdp_dev_pm_lock);
	
	return NULL;
}
	
int sdp_dev_pm_group_add_by_name(struct sdp_dev_pm_group *grp, char *name)
{
	struct sdp_dev_pm_grp_node *node;
	
	if (!grp || !name)
		return -EINVAL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	strncpy(node->name, name, sizeof(node->name)-1);

	mutex_lock(&grp->lock);
	list_add(&node->list, &grp->head);
	mutex_unlock(&grp->lock);

	return 0;
}

int sdp_dev_pm_group_suspend(struct sdp_dev_pm_group *grp)
{
	struct sdp_dev_pm_dev_node *dev_node;
	struct sdp_dev_pm_grp_node *grp_node;
	int pri;

	if (!grp)
		return -EINVAL;

	for (pri = SDP_DEV_PM_PRI_LATE; pri >= SDP_DEV_PM_PRI_RESERVED; pri--) {
		list_for_each_entry(grp_node, &grp->head, list) {
			dev_node = find_dev(grp_node->name);
			if (dev_node && dev_node->pm.priority == pri && dev_node->pm.dev && dev_node->pm.suspend) {
				dev_node->pm.suspend(dev_node->pm.dev);
				pr_info("sdp_dev_pm : %s suspended.\n", dev_node->name);
			}
		}
	}
	
	return 0;
}

int sdp_dev_pm_group_resume(struct sdp_dev_pm_group *grp)
{
	struct sdp_dev_pm_dev_node *dev_node;
	struct sdp_dev_pm_grp_node *grp_node;
	int pri;

	if (!grp)
		return -EINVAL;

	for (pri = SDP_DEV_PM_PRI_RESERVED; pri <= SDP_DEV_PM_PRI_LATE; pri++) {
		list_for_each_entry_reverse(grp_node, &grp->head, list) {
			dev_node = find_dev(grp_node->name);
			if (dev_node && dev_node->pm.priority == pri && dev_node->pm.dev && dev_node->pm.resume) {
				dev_node->pm.resume(dev_node->pm.dev);
				pr_info("sdp_dev_pm : %s resumed.\n", dev_node->name);
			}
		}
	}
	
	return 0;
}

EXPORT_SYMBOL(sdp_dev_pm_register);
EXPORT_SYMBOL(sdp_dev_pm_group_init);
EXPORT_SYMBOL(sdp_dev_pm_group_add_by_name);
EXPORT_SYMBOL(sdp_dev_pm_group_suspend);
EXPORT_SYMBOL(sdp_dev_pm_group_resume);


