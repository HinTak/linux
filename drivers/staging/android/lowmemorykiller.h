#ifndef __LOWMEMORYKILLER_H
#define __LOWMEMORYKILLER_H

#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/notifier.h>

#define DEV_NAME_DEF	"dev/lmk"
#define LMK_MINOR	242

/*
 * Params for ioctl call
 *
 * REG_MANAGER_IOCTL:
 *	pid - process id
 *	val - ignored
 * SET_KILL_PRIO_IOCTL:
 *	pid - process id
 *	val - priority for killing app in lowmem condition.
 *	      higher val is for not important application.
 *	      three values are alowed:
 *	      passive - 6
 *	      active - 5
 *	      OOM_DISABLE - protect app.
 *	preload_prio - indicates PRELOADED_APP priority.
 *	               Task with highest value will be killed first.
 */
struct lmk_ioctl {
	int pid;
	int val;

#ifdef CONFIG_LMK_PRELOAD_APP
	unsigned int preload_prio;
#endif
};

#define LMK_MAGIC       'l'

#define SET_KILL_PRIO_IOCTL  _IOW(LMK_MAGIC, 1, struct lmk_ioctl)
#define REG_MANAGER_IOCTL _IOW(LMK_MAGIC, 2, struct lmk_ioctl)

#if defined (CONFIG_KNBD_SUPPORT)

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
extern struct raw_notifier_head lmk_chain;

static inline int register_lmk_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&lmk_chain, nb);
}

static inline int unregister_lmk_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&lmk_chain, nb);
}

static inline int call_lmk_notifiers(unsigned long val)
{
	return raw_notifier_call_chain(&lmk_chain, val, NULL);
}
#else
static inline int register_lmk_notifier(struct notifier_block *nb) {}
static inline int unregister_lmk_notifier(struct notifier_block *nb) {}
static inline int call_lmk_notifiers(unsigned long val) {}
#endif

#endif

#define LMK_NOTIFY_SHRINK 0

#endif
