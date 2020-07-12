
#ifndef NDP_RUNTIME_PM_H
#define NDP_RUNTIME_PM_H

#include <linux/list.h>
#include <linux/spinlock.h>

#define NDP_RUNTIME_PM_STR_N 16

enum {
	NDP_RPM_ACTIVE,
	NDP_RPM_RESUMING,
	NDP_RPM_SUSPENDING,
	NDP_RPM_SUSPENDED
};

struct ndp_runtime_pm {
	char name[NDP_RUNTIME_PM_STR_N];
	void *dev;
	int (*suspend)(void *dev);
	int (*resume)(void *dev);
	spinlock_t lock;
	int usage_count;
	int busy_count;
	int state;
};

struct ndp_runtime_pm_node {
	struct list_head list;
	struct ndp_runtime_pm rpm;
};

int ndp_runtime_pm_init_and_get(struct ndp_runtime_pm *rpm, char *name);
int ndp_runtime_pm_get(char *name);
int ndp_runtime_pm_put(char *name);
int ndp_runtime_pm_tryuse(void *rpm_dev);
int ndp_runtime_pm_disuse(void *rpm_dev);
void* ndp_runtime_pm_getdev(char *name);

#endif

