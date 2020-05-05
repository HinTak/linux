
#ifndef SDP_RUNTIME_PM_H
#define SDP_RUNTIME_PM_H

#include <linux/list.h>
#include <linux/spinlock.h>

#define SDP_RUNTIME_PM_STR_N 16

enum {
	SDP_RPM_ACTIVE,
	SDP_RPM_RESUMING,
	SDP_RPM_SUSPENDING,
	SDP_RPM_SUSPENDED
};

struct sdp_runtime_pm {
	char name[SDP_RUNTIME_PM_STR_N];
	void *dev;
	int (*suspend)(void *dev);
	int (*resume)(void *dev);
	spinlock_t lock;
	int usage_count;
	int busy_count;
	int state;
};

struct sdp_runtime_pm_node {
	struct list_head list;
	struct sdp_runtime_pm rpm;
};


int sdp_runtime_pm_init_and_get(struct sdp_runtime_pm *rpm, char *name);
int sdp_runtime_pm_get(char *name);
int sdp_runtime_pm_put(char *name);
int sdp_runtime_pm_tryuse(void *rpm_dev);
int sdp_runtime_pm_disuse(void *rpm_dev);
void* sdp_runtime_pm_getdev(char *name);

#endif

