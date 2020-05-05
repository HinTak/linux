
#ifndef SDP_DEV_PM_H
#define SDP_DEV_PM_H

#include <linux/list.h>
#include <linux/mutex.h>

#define SDP_DEV_PM_PRI_RESERVED 0
#define SDP_DEV_PM_PRI_PREPARE  1
#define SDP_DEV_PM_PRI_EARLY    2
#define SDP_DEV_PM_PRI_NORMAL   3
#define SDP_DEV_PM_PRI_LATE     4

struct sdp_dev_pm {
	int priority;
	void *dev;
	int (*suspend)(void *dev);
	int (*resume)(void *dev);
};

struct sdp_dev_pm_group {
	struct list_head head;
	struct mutex lock;
};

int sdp_dev_pm_register(struct sdp_dev_pm *pm, char *name);
int sdp_dev_pm_group_init(struct sdp_dev_pm_group *grp);
int sdp_dev_pm_group_add_by_name(struct sdp_dev_pm_group *grp, char *name);
int sdp_dev_pm_group_suspend(struct sdp_dev_pm_group *grp);
int sdp_dev_pm_group_resume(struct sdp_dev_pm_group *grp);

#endif

