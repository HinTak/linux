/*
 * Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 *
 * This file contains the spinlock/rwlock implementations for
 * DEBUG_SPINLOCK.
 */

#include <linux/spinlock.h>
#include <linux/nmi.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/jiffies.h>

#ifdef CONFIG_VDLP_VERSION_INFO
#include <linux/vdlp_version.h>
void show_kernel_patch_version(void);
#endif

void __raw_spin_lock_init(raw_spinlock_t *lock, const char *name,
			  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
	lock->raw_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	lock->magic = SPINLOCK_MAGIC;
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
	lock->acquire_tstamp = 0;
}

EXPORT_SYMBOL(__raw_spin_lock_init);

void __rwlock_init(rwlock_t *lock, const char *name,
		   struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
	lock->raw_lock = (arch_rwlock_t) __ARCH_RW_LOCK_UNLOCKED;
	lock->magic = RWLOCK_MAGIC;
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
}

EXPORT_SYMBOL(__rwlock_init);
#ifdef CONFIG_DUMP_SPINLOCK_MEMORY
extern int is_valid_kernel_addr(unsigned long register_value);

#define HOWMUCH_SHIFT              8 /* 256byte. this is configurable */
#define HOWMUCH_SIZE               (_AC(1,UL) << HOWMUCH_SHIFT)
#define HOWMUCH_MASK               (~((1 << HOWMUCH_SHIFT) - 1))

#endif

#ifdef CONFIG_SDP_BUS_ERR_LOGGER
int sdp_bus_flowctrl_get(const char *id);
#endif

/*
 * Returns milliseconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^20ns == 1.048ms.
 */
static unsigned long get_timestamp(void)
{
	return running_clock() >> 20LL;  /* 2^20 ~= 10^6 */
}

static void __spin_dump_lock_state(const char *level,
				raw_spinlock_t *lock, const char *msg)
{
	struct task_struct *owner = NULL;
	unsigned long now = get_timestamp();

#ifdef CONFIG_DUMP_SPINLOCK_MEMORY
        unsigned long start_addr_for_printing = 0;
        unsigned long end_addr_for_printing = 0;
#endif 
	if (lock->owner && lock->owner != SPINLOCK_OWNER_INIT)
		owner = lock->owner;
	printk(KERN_EMERG "%s: spinlock %s on CPU#%d, %s/%d\n",
		level, msg, raw_smp_processor_id(),
		current->comm, task_pid_nr(current));
	printk(KERN_EMERG " lock: %pS, .magic: %08x, .owner: %s/%d, "
			".owner_cpu: %d, .raw_lock: %x\n"
			"\t.current_ts: %lu ms .owner_acquire_ts: %lu ms\n",
		lock, lock->magic,
		owner ? owner->comm : "<none>",
		owner ? task_pid_nr(owner) : -1,
		lock->owner_cpu,
		lock->raw_lock.slock,
		now,
		lock->acquire_tstamp);

#ifdef CONFIG_SDP_BUS_ERR_LOGGER
	printk(KERN_EMERG " sdp_bus_flowctrl[CPU]:%x\n", sdp_bus_flowctrl_get("CPU"));
#endif

#ifdef CONFIG_DUMP_SPINLOCK_MEMORY
	printk("\n* Lock address : %lx\n",(unsigned long)lock);
	if(!is_valid_kernel_addr((unsigned long)lock))
	{
		printk("lock is not valid address.\n");
		return;
	}
	start_addr_for_printing = ((unsigned long)lock & HOWMUCH_MASK) - HOWMUCH_SIZE ;
	end_addr_for_printing = ((unsigned long)lock & HOWMUCH_MASK) + HOWMUCH_SIZE + HOWMUCH_SIZE-1;

	if (!is_valid_kernel_addr(start_addr_for_printing)) {
		printk("# 'start_addr_for_printing' is not valid address.\n");
		printk("# So, we use just 'lock & HOWMUCH_MASK'\n");
		start_addr_for_printing = ((unsigned long)lock & HOWMUCH_MASK);
	}

	if (!is_valid_kernel_addr(end_addr_for_printing)) {
		printk("# 'end_addr_for_printing' is wrong address.\n");
		printk("# So, we use 'lock & HOWMUCH_MASK + HOWMUCH_SIZE-1'\n");
		end_addr_for_printing = ((unsigned long)lock & HOWMUCH_MASK) + HOWMUCH_SIZE-1;
	}

	printk("# dump_start_addr : 0x%lx, dump_end_addr : 0x%lx\n",
			start_addr_for_printing, end_addr_for_printing);
	printk("--------------------------------------------------------------------------------------\n");
	dump_mem_kernel("meminfo ", start_addr_for_printing, end_addr_for_printing);
	printk("--------------------------------------------------------------------------------------\n");
	printk("\n");
#endif
}

static void __spin_dump(const char *level,
			raw_spinlock_t *lock, const char *msg)
{

	__spin_dump_lock_state(level, lock, msg);
#ifdef CONFIG_VDLP_VERSION_INFO
	show_kernel_patch_version();
#endif
	dump_stack();
}

#define spin_dump(l, m) __spin_dump("BUG", (l), (m))
#define spin_dump_vd(l, m) __spin_dump("INFO", (l), (m))
#define UNLOCK_DEADLINE 2000	/* 2000 milliseconds */

static void spin_bug(raw_spinlock_t *lock, const char *msg)
{
	if (!debug_locks_off())
		return;

	spin_dump(lock, msg);
}

#define SPIN_BUG_ON(cond, lock, msg) if (unlikely(cond)) spin_bug(lock, msg)

static inline void
debug_spin_lock_before(raw_spinlock_t *lock)
{
	SPIN_BUG_ON(lock->magic != SPINLOCK_MAGIC, lock, "bad magic");
	SPIN_BUG_ON(lock->owner == current, lock, "recursion");
	SPIN_BUG_ON(lock->owner_cpu == raw_smp_processor_id(),
							lock, "cpu recursion");
}

static inline void debug_spin_lock_after(raw_spinlock_t *lock)
{
	lock->owner_cpu = raw_smp_processor_id();
	lock->owner = current;
	lock->acquire_tstamp = get_timestamp();
}

static inline void debug_spin_unlock(raw_spinlock_t *lock)
{
	SPIN_BUG_ON(lock->magic != SPINLOCK_MAGIC, lock, "bad magic");
	SPIN_BUG_ON(!raw_spin_is_locked(lock), lock, "already unlocked");
	SPIN_BUG_ON(lock->owner != current, lock, "wrong owner");
	SPIN_BUG_ON(lock->owner_cpu != raw_smp_processor_id(),
							lock, "wrong CPU");
	if (time_after_eq(get_timestamp(), (lock->acquire_tstamp + UNLOCK_DEADLINE)))
		spin_dump_vd(lock, "missed unlock deadline");
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
}

static void __spin_lock_debug(raw_spinlock_t *lock)
{
	u64 i;
	u64 loops = (loops_per_jiffy * HZ) / 3;

	for (i = 0; i < loops; i++) {
		if (arch_spin_trylock(&lock->raw_lock))
			return;
		__delay(5);
	}
	/* lockup suspected: */
	spin_dump(lock, "lockup suspected");
#ifdef CONFIG_SMP
	trigger_all_cpu_backtrace();
#endif

	/*
	 * The trylock above was causing a livelock.  Give the lower level arch
	 * specific lock code a chance to acquire the lock. We have already
	 * printed a warning/backtrace at this point. The non-debug arch
	 * specific code might actually succeed in acquiring the lock.  If it is
	 * not successful, the end-result is the same - there is no forward
	 * progress.
	 */
	arch_spin_lock(&lock->raw_lock);
}

void do_raw_spin_lock(raw_spinlock_t *lock)
{
	debug_spin_lock_before(lock);
	if (unlikely(!arch_spin_trylock(&lock->raw_lock)))
		__spin_lock_debug(lock);
	debug_spin_lock_after(lock);
}

int do_raw_spin_trylock(raw_spinlock_t *lock)
{
	int ret = arch_spin_trylock(&lock->raw_lock);

	if (ret)
		debug_spin_lock_after(lock);
#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	SPIN_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void do_raw_spin_unlock(raw_spinlock_t *lock)
{
	debug_spin_unlock(lock);
	arch_spin_unlock(&lock->raw_lock);
}

static void rwlock_bug(rwlock_t *lock, const char *msg)
{
	if (!debug_locks_off())
		return;

	printk(KERN_EMERG "BUG: rwlock %s on CPU#%d, %s/%d, %p\n",
		msg, raw_smp_processor_id(), current->comm,
		task_pid_nr(current), lock);
	dump_stack();
}

#define RWLOCK_BUG_ON(cond, lock, msg) if (unlikely(cond)) rwlock_bug(lock, msg)

#if 0		/* __write_lock_debug() can lock up - maybe this can too? */
static void __read_lock_debug(rwlock_t *lock)
{
	u64 i;
	u64 loops = loops_per_jiffy * HZ;
	int print_once = 1;

	for (;;) {
		for (i = 0; i < loops; i++) {
			if (arch_read_trylock(&lock->raw_lock))
				return;
			__delay(1);
		}
		/* lockup suspected: */
		if (print_once) {
			print_once = 0;
			printk(KERN_EMERG "BUG: read-lock lockup on CPU#%d, "
					"%s/%d, %p\n",
				raw_smp_processor_id(), current->comm,
				current->pid, lock);
			dump_stack();
		}
	}
}
#endif

void do_raw_read_lock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	arch_read_lock(&lock->raw_lock);
}

int do_raw_read_trylock(rwlock_t *lock)
{
	int ret = arch_read_trylock(&lock->raw_lock);

#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	RWLOCK_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void do_raw_read_unlock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	arch_read_unlock(&lock->raw_lock);
}

static inline void debug_write_lock_before(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	RWLOCK_BUG_ON(lock->owner == current, lock, "recursion");
	RWLOCK_BUG_ON(lock->owner_cpu == raw_smp_processor_id(),
							lock, "cpu recursion");
}

static inline void debug_write_lock_after(rwlock_t *lock)
{
	lock->owner_cpu = raw_smp_processor_id();
	lock->owner = current;
}

static inline void debug_write_unlock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	RWLOCK_BUG_ON(lock->owner != current, lock, "wrong owner");
	RWLOCK_BUG_ON(lock->owner_cpu != raw_smp_processor_id(),
							lock, "wrong CPU");
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
}

#if 0		/* This can cause lockups */
static void __write_lock_debug(rwlock_t *lock)
{
	u64 i;
	u64 loops = loops_per_jiffy * HZ;
	int print_once = 1;

	for (;;) {
		for (i = 0; i < loops; i++) {
			if (arch_write_trylock(&lock->raw_lock))
				return;
			__delay(1);
		}
		/* lockup suspected: */
		if (print_once) {
			print_once = 0;
			printk(KERN_EMERG "BUG: write-lock lockup on CPU#%d, "
					"%s/%d, %p\n",
				raw_smp_processor_id(), current->comm,
				current->pid, lock);
			dump_stack();
		}
	}
}
#endif

void do_raw_write_lock(rwlock_t *lock)
{
	debug_write_lock_before(lock);
	arch_write_lock(&lock->raw_lock);
	debug_write_lock_after(lock);
}

int do_raw_write_trylock(rwlock_t *lock)
{
	int ret = arch_write_trylock(&lock->raw_lock);

	if (ret)
		debug_write_lock_after(lock);
#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	RWLOCK_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void do_raw_write_unlock(rwlock_t *lock)
{
	debug_write_unlock(lock);
	arch_write_unlock(&lock->raw_lock);
}
