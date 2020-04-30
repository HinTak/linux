/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *  Copyright (C)  2010  Google, Inc.
 *	Rewritten by David Rientjes
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from __alloc_pages()
 *  in mm/page_alloc.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/cpuset.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/freezer.h>
#include <linux/ftrace.h>
#include <linux/ratelimit.h>

#if defined(CONFIG_SLP_LOWMEM_NOTIFY) && !defined(CONFIG_VD_RELEASE)
#include <linux/sort.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/oom.h>

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
#include <linux/coredump.h>
#endif

#include <linux/oom_graphic_memory.h>

int sysctl_panic_on_oom;
int sysctl_oom_kill_allocating_task;
int sysctl_oom_dump_tasks = 1;
static DEFINE_SPINLOCK(zone_scan_lock);

static LIST_HEAD(graphic_module_list);
static DEFINE_MUTEX(graphic_module_mutex);
static DEFINE_MUTEX(oom_owner_mutex);
static int oom_processing_pid = -1;

static const char const task_state_array[] = {
	'R',          /* Running */
	'S',         /* Sleeping task interruptible */
	'D',         /* task uninterruptible */
	'T',          /* task stopped */
	't',         /* tracing stop  */
	'X',             /* dead */
	'Z',           /* zombie */
};

static inline const char get_task_state(struct task_struct *tsk)
{
	unsigned int state = (tsk->state | tsk->exit_state) & TASK_REPORT;

	return task_state_array[fls(state)];
}

static int total_graphic_used_memory;

struct graphic_func_item {
	struct list_head entry;
	int graphic_type;
	void (*graphic_func)(int type, void *send_data);
	void (*graphic_data_func)(pid_t graphic_pid, int graphic_memory);
};

struct graphic_img_driver_stat_item {
        void (*graphic_memory_stats_func)(struct img_driver_stats * img_mem_stats);
};

struct graphic_img_driver_stat_item pitem;

void graphic_get_pid_memory(pid_t graphic_pid, int graphic_memory)
{
	/*
	 * Once We only consider maximum 50 process.
	 * But we need to think more how to handle it.
	 */
	struct task_struct *tsk = NULL;

	rcu_read_lock();
	tsk = find_task_by_pid_ns(graphic_pid, &init_pid_ns);
	if (tsk)
		get_task_struct(tsk);
	rcu_read_unlock();

	if (tsk) {
		pr_err("%22s(%5d) : %9d kB\n",
			tsk->group_leader->comm, graphic_pid,
			graphic_memory >> 10);
		put_task_struct(tsk);
		total_graphic_used_memory += graphic_memory;
	} else
		pr_err("Task is not existed for the pid(%d)\n",	graphic_pid);
}

int graphic_memory_func_register(int type, void *func)
{
	struct graphic_func_item *new_item = NULL;
	struct list_head *tmp;
	struct graphic_func_item *regi_item;

	pr_info("Graphic func. is register with type %d\n", type);

	if (!func) {
		pr_err("[FAIL] There is no function to register\n");
		return -EINVAL;
	}

	if (type >= MAX_GRAPHIC_TYPE) {
		pr_err("[FAIL] Undefined type(%d) is used\n", type);
		return -EINVAL;
	}

	mutex_lock(&graphic_module_mutex);
	list_for_each(tmp, &graphic_module_list) {
		regi_item = list_entry(tmp, struct graphic_func_item, entry);

		if (regi_item->graphic_type == type) {
			pr_err("[FAIL] This type(%d). is already registered\n"
						, type);
			mutex_unlock(&graphic_module_mutex);
			return -EBUSY;
		}
	}
	mutex_unlock(&graphic_module_mutex);

	new_item = kzalloc(sizeof(*new_item), GFP_KERNEL);
	if (!new_item) {
		pr_err("[FAIL] Fail to allocate memory\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&new_item->entry);
	new_item->graphic_type = type;
	new_item->graphic_func = func;
	new_item->graphic_data_func = graphic_get_pid_memory;

	mutex_lock(&graphic_module_mutex);
	list_add_tail(&new_item->entry, &graphic_module_list);
	mutex_unlock(&graphic_module_mutex);

	return 0;
}
EXPORT_SYMBOL(graphic_memory_func_register);

int graphic_memory_func_unregister(int type)
{
	struct list_head *tmp;
	struct graphic_func_item *regi_item;
	int ret = -ENODATA;

	pr_info("Graphic func. is unregistered with type %d\n", type);

	if (type >= MAX_GRAPHIC_TYPE) {
		pr_err("[FAIL] Undefined type(%d) is used\n", type);
		return -EINVAL;
	}

	mutex_lock(&graphic_module_mutex);
	list_for_each(tmp, &graphic_module_list) {
		regi_item = list_entry(tmp, struct graphic_func_item, entry);

		if (regi_item->graphic_type == type) {
			pr_info("Type is matched(%d:%d)\n",
					type, regi_item->graphic_type);
			list_del_init(&regi_item->entry);
			kfree(regi_item);
			ret = 0;
			goto done;
		}
	}
	pr_err("[FAIL] There is not matched type(%d)\n", type);
done:
	mutex_unlock(&graphic_module_mutex);
	return ret;
}
EXPORT_SYMBOL(graphic_memory_func_unregister);

int graphic_img_driver_stats_register(void *func)
{
	pitem.graphic_memory_stats_func = func;
	return 0;
}
EXPORT_SYMBOL(graphic_img_driver_stats_register);

static inline void print_img_mem_stat(struct img_driver_stats *mem_stats)
{
	pr_err("IMG Graphic Driver Memory Stats:\n");
	pr_err("MemoryUsageKMalloc                %10d\n", mem_stats->ui32MemoryUsageKMalloc);
	pr_err("MemoryUsageKMallocMax             %10d\n", mem_stats->ui32MemoryUsageKMallocMax);
	pr_err("MemoryUsageVMalloc                %10d\n", mem_stats->ui32MemoryUsageVMalloc);
	pr_err("MemoryUsageVMallocMax             %10d\n", mem_stats->ui32MemoryUsageVMallocMax);
	pr_err("MemoryUsageAllocPTMemoryUMA       %10d\n", mem_stats->ui32MemoryUsageAllocPTMemoryUMA);
	pr_err("MemoryUsageAllocPTMemoryUMAMax    %10d\n", mem_stats->ui32MemoryUsageAllocPTMemoryUMAMax);
	pr_err("MemoryUsageVMapPTUMA              %10d\n", mem_stats->ui32MemoryUsageVMapPTUMA);
	pr_err("MemoryUsageVMapPTUMAMax           %10d\n", mem_stats->ui32MemoryUsageVMapPTUMAMax);
	pr_err("MemoryUsageAllocPTMemoryLMA       %10d\n", mem_stats->ui32MemoryUsageAllocPTMemoryLMA);
	pr_err("MemoryUsageAllocPTMemoryLMAMax    %10d\n", mem_stats->ui32MemoryUsageAllocPTMemoryLMAMax);
	pr_err("MemoryUsageIORemapPTLMA           %10d\n", mem_stats->ui32MemoryUsageIORemapPTLMA);
	pr_err("MemoryUsageIORemapPTLMAMax        %10d\n", mem_stats->ui32MemoryUsageIORemapPTLMAMax);
	pr_err("MemoryUsageAllocGPUMemLMA         %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemLMA);
	pr_err("MemoryUsageAllocGPUMemLMAMax      %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemLMAMax);
	pr_err("MemoryUsageAllocGPUMemUMA         %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemUMA);
	pr_err("MemoryUsageAllocGPUMemUMAMax      %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemUMAMax);
	pr_err("MemoryUsageAllocGPUMemUMAPool     %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemUMAPool);
	pr_err("MemoryUsageAllocGPUMemUMAPoolMax  %10d\n", mem_stats->ui32MemoryUsageAllocGPUMemUMAPoolMax);
	pr_err("MemoryUsageMappedGPUMemUMA/LMA    %10d\n", mem_stats->ui32MemoryUsageMappedGPUMemUMA_LMA);
	pr_err("MemoryUsageMappedGPUMemUMA/LMAMax %10d\n", mem_stats->ui32MemoryUsageMappedGPUMemUMA_LMAMax);
	return;
}

void show_graphic_register_func(void)
{
	struct list_head *tmp;
	struct graphic_func_item *regi_item;
	struct img_driver_stats mem_stats = {0};
	int i = 0;

	mutex_lock(&graphic_module_mutex);
	list_for_each(tmp, &graphic_module_list) {
		regi_item = list_entry(tmp, struct graphic_func_item, entry);
		if (regi_item->graphic_func) {
			pr_err("%s Graphics meminfo:", (regi_item->graphic_type
			== GEM_GRAPHIC_TYPE) ? "GEM" : "GPU");
			regi_item->graphic_func(regi_item->graphic_type,
					regi_item->graphic_data_func);
			pr_err("%22s : %9d kB\n",
				"Total", total_graphic_used_memory >> 10);
			total_graphic_used_memory = 0;
		}
	}
	if(pitem.graphic_memory_stats_func) {
		pitem.graphic_memory_stats_func(&mem_stats);
		print_img_mem_stat(&mem_stats);
	}	
	mutex_unlock(&graphic_module_mutex);
}

#define K(x) ((x) << (PAGE_SHIFT-10))

bool only_one_time_call = true;
int panic_ratelimit_interval = 120 * HZ;
int panic_ratelimit_burst = 4;
static DEFINE_RATELIMIT_STATE(oom_panic_rs, 120 * HZ, 4);

static inline void set_ratelimit_state(struct ratelimit_state *rs)
{
	unsigned long flags;

	if (!raw_spin_trylock_irqsave(&rs->lock, flags))
		return;
	rs->interval = panic_ratelimit_interval;
	rs->burst = panic_ratelimit_burst;
	raw_spin_unlock_irqrestore(&rs->lock, flags);
}

static inline void reset_ratelimit_state(struct ratelimit_state *rs)
{
	unsigned long flags;

	if (!raw_spin_trylock_irqsave(&rs->lock, flags))
		return;
	rs->printed = 0;
	rs->begin = 0;
	rs->missed = 0;
	raw_spin_unlock_irqrestore(&rs->lock, flags);
}

#ifdef CONFIG_NUMA
/**
 * has_intersects_mems_allowed() - check task eligiblity for kill
 * @start: task struct of which task to consider
 * @mask: nodemask passed to page allocator for mempolicy ooms
 *
 * Task eligibility is determined by whether or not a candidate task, @tsk,
 * shares the same mempolicy nodes as current if it is bound by such a policy
 * and whether or not it has the same set of allowed cpuset nodes.
 */
static bool has_intersects_mems_allowed(struct task_struct *start,
					const nodemask_t *mask)
{
	struct task_struct *tsk;
	bool ret = false;

	rcu_read_lock();
	for_each_thread(start, tsk) {
		if (mask) {
			/*
			 * If this is a mempolicy constrained oom, tsk's
			 * cpuset is irrelevant.  Only return true if its
			 * mempolicy intersects current, otherwise it may be
			 * needlessly killed.
			 */
			ret = mempolicy_nodemask_intersects(tsk, mask);
		} else {
			/*
			 * This is not a mempolicy constrained oom, so only
			 * check the mems of tsk's cpuset.
			 */
			ret = cpuset_mems_allowed_intersects(current, tsk);
		}
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}
#else
static bool has_intersects_mems_allowed(struct task_struct *tsk,
					const nodemask_t *mask)
{
	return true;
}
#endif /* CONFIG_NUMA */

/*
 * The process p may have detached its own ->mm while exiting or through
 * use_mm(), but one or more of its subthreads may still have a valid
 * pointer.  Return p, or any of its subthreads with a valid ->mm, with
 * task_lock() held.
 */
struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t;

	rcu_read_lock();

	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();

	return t;
}

static inline bool is_task_systemd(struct task_struct *p)
{
	if (p->tgid == 1 || !strncmp(p->comm, "systemd", TASK_COMM_LEN))
		return true;
	return false;
}

/* return true if the task is not adequate as candidate victim task. */
static bool oom_unkillable_task(struct task_struct *p,
		struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	if (p->flags & PF_KTHREAD)
		return true;

	if (is_task_systemd(p))
		return true;

	/* When mem_cgroup_out_of_memory() and p is not member of the group */
	if (memcg && !task_in_mem_cgroup(p, memcg))
		return true;

	/* p may not have freeable memory in nodemask */
	if (!has_intersects_mems_allowed(p, nodemask))
		return true;

	return false;
}

/**
 * oom_badness - heuristic function to determine which candidate task to kill
 * @p: task struct of which task we should calculate
 * @totalpages: total present RAM allowed for page allocation
 *
 * The heuristic for determining which task to kill is made to be as simple and
 * predictable as possible.  The goal is to return the highest value for the
 * task consuming the most memory to avoid subsequent oom failures.
 */
unsigned long oom_badness(struct task_struct *p, struct mem_cgroup *memcg,
			  const nodemask_t *nodemask, unsigned long totalpages)
{
	long points;
	long adj;

	if (oom_unkillable_task(p, memcg, nodemask))
		return 0;

	p = find_lock_task_mm(p);
	if (!p)
		return 0;

	adj = (long)p->signal->oom_score_adj;
	if (adj == OOM_SCORE_ADJ_MIN) {
		task_unlock(p);
		return 0;
	}

	/*
	 * The baseline for the badness score is the proportion of RAM that each
	 * task's rss, pagetable and swap space use.
	 */
	sync_task_rss(p);

	points = get_mm_rss(p->mm) + get_mm_counter(p->mm, MM_SWAPENTS) +
		atomic_long_read(&p->mm->nr_ptes) + mm_nr_pmds(p->mm);
	task_unlock(p);

	/*
	 * Root processes get 3% bonus, just like the __vm_enough_memory()
	 * implementation used by LSMs.
	 */
	if (has_capability_noaudit(p, CAP_SYS_ADMIN))
		points -= (points * 3) / 100;

	/* Normalize to oom_score_adj units */
	adj *= totalpages / 1000;
	points += adj;

	/*
	 * Never return 0 for an eligible task regardless of the root bonus and
	 * oom_score_adj (oom_score_adj can't be OOM_SCORE_ADJ_MIN here).
	 */
	return points > 0 ? points : 1;
}

/*
 * Determine the type of allocation constraint.
 */
#ifdef CONFIG_NUMA
static enum oom_constraint constrained_alloc(struct zonelist *zonelist,
				gfp_t gfp_mask, nodemask_t *nodemask,
				unsigned long *totalpages)
{
	struct zone *zone;
	struct zoneref *z;
	enum zone_type high_zoneidx = gfp_zone(gfp_mask);
	bool cpuset_limited = false;
	int nid;

	/* Default to all available memory */
	*totalpages = totalram_pages + total_swap_pages;

	if (!zonelist)
		return CONSTRAINT_NONE;
	/*
	 * Reach here only when __GFP_NOFAIL is used. So, we should avoid
	 * to kill current.We have to random task kill in this case.
	 * Hopefully, CONSTRAINT_THISNODE...but no way to handle it, now.
	 */
	if (gfp_mask & __GFP_THISNODE)
		return CONSTRAINT_NONE;

	/*
	 * This is not a __GFP_THISNODE allocation, so a truncated nodemask in
	 * the page allocator means a mempolicy is in effect.  Cpuset policy
	 * is enforced in get_page_from_freelist().
	 */
	if (nodemask && !nodes_subset(node_states[N_MEMORY], *nodemask)) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, *nodemask)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_MEMORY_POLICY;
	}

	/* Check this allocation failure is caused by cpuset's wall function */
	for_each_zone_zonelist_nodemask(zone, z, zonelist,
			high_zoneidx, nodemask)
		if (!cpuset_zone_allowed(zone, gfp_mask))
			cpuset_limited = true;

	if (cpuset_limited) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, cpuset_current_mems_allowed)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_CPUSET;
	}
	return CONSTRAINT_NONE;
}
#else
static enum oom_constraint constrained_alloc(struct zonelist *zonelist,
				gfp_t gfp_mask, nodemask_t *nodemask,
				unsigned long *totalpages)
{
	*totalpages = totalram_pages + total_swap_pages;
	return CONSTRAINT_NONE;
}
#endif

enum oom_scan_t oom_scan_process_thread(struct task_struct *task,
		unsigned long totalpages, const nodemask_t *nodemask,
		bool force_kill)
{
	if (oom_unkillable_task(task, NULL, nodemask))
		return OOM_SCAN_CONTINUE;

	/*
	 * This task already has access to memory reserves and is being killed.
	 * Don't allow any other task to have access to the reserves.
	 */
	if (test_tsk_thread_flag(task, TIF_MEMDIE)) {
		if (!force_kill)
			return OOM_SCAN_ABORT;
	}
	if (!task->mm)
		return OOM_SCAN_CONTINUE;

	/*
	 * If task is allocating a lot of memory and has been marked to be
	 * killed first if it triggers an oom, then select it.
	 */
	if (oom_task_origin(task))
		return OOM_SCAN_SELECT;

	if (task_will_free_mem(task) && !force_kill)
		return OOM_SCAN_ABORT;

	return OOM_SCAN_OK;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'.  Returns -1 on scan abort.
 *
 * (not docbooked, we don't want this one cluttering up the manual)
 */
static struct task_struct *select_bad_process(unsigned int *ppoints,
		unsigned long totalpages, const nodemask_t *nodemask,
		bool force_kill)
{
	struct task_struct *p, *t;
	struct task_struct *chosen = NULL;
	unsigned long chosen_points = 0;

	rcu_read_lock();
	for_each_process(p) {
		if (fatal_signal_pending(p)) {
			pr_err_ratelimited(
			"Process %s (pid: %d, state: %c) was skipped during OOM selection\n",
				p->comm, p->pid, get_task_state(p));
			continue;
		}
		for_each_thread(p, t) {
			unsigned int points;

			switch (oom_scan_process_thread(t, totalpages, nodemask,
						force_kill)) {
			case OOM_SCAN_SELECT:
				chosen = t;
				chosen_points = ULONG_MAX;
				/* fall through */
			case OOM_SCAN_CONTINUE:
				continue;
			case OOM_SCAN_ABORT:
				rcu_read_unlock();
				return (struct task_struct *)(-1UL);
			case OOM_SCAN_OK:
				break;
			};
			points = oom_badness(t, NULL, nodemask, totalpages);
			if (!points || points < chosen_points)
				continue;
			if (points == chosen_points &&
					thread_group_leader(chosen))
				continue;

			chosen = t;
			chosen_points = points;
		}
	}

	if (chosen)
		get_task_struct(chosen);
	rcu_read_unlock();

	*ppoints = chosen_points * 1000 / totalpages;
	return chosen;
}

#ifdef CONFIG_VD_MEMINFO
#include <linux/seq_file.h>
struct smap_mem {
	unsigned long total;
	unsigned long vmag[VMAG_CNT];
};

#ifdef CONFIG_PROC_PAGE_MONITOR
extern int smaps_pte_range(pmd_t *pmd, unsigned long addr,
			   unsigned long end, struct mm_walk *walk);
#endif

#define PSS_SHIFT 12

struct mm_sem_struct {
	char task_comm[TASK_COMM_LEN];
	pid_t tgid;
	struct mm_struct *mm;
	struct list_head list;
};

static unsigned long sum_of_pss;

/* Update the counters with the information retrieved from the vma
 * as well as the mem_size_stats as returned by walk_page_range(). */
static inline void update_dtp_counters(struct vm_area_struct *vma ,
				       struct mem_size_stats *mss,
				       struct smap_mem *vmem,
				       struct smap_mem *rss,
				       struct smap_mem *pss,
				       struct smap_mem *shared,
				       struct smap_mem *private,
				       struct smap_mem *swap
				      )
{
	int idx = 0;

	/* Add this to the process's consolidated totals. */
	vmem->total    += (vma->vm_end - vma->vm_start) >> 10;
	rss->total     += mss->resident >> 10;
	pss->total     += (unsigned long)(mss->pss >> (10 + PSS_SHIFT));
	shared->total  += (mss->shared_clean + mss->shared_dirty) >> 10;
	private->total += (mss->private_clean + mss->private_dirty) >> 10;
	swap->total    += mss->swap >> 10;

	/* Add this to the VMA's relevant counters, i.e., Code, Data, LibCode,
	* LibData, Stack or Other. Also classify them according to the following
	* memory types - vmem, rss, pss, shared, private and swap. */
	idx = get_group_idx(vma);
	vmem->vmag[idx]    += (vma->vm_end - vma->vm_start) >> 10;
	rss->vmag[idx]     += mss->resident >> 10;
	pss->vmag[idx]     += (unsigned long)(mss->pss >> (10 + PSS_SHIFT));
	shared->vmag[idx]  += (mss->shared_clean + mss->shared_dirty) >> 10;
	private->vmag[idx] += (mss->private_clean + mss->private_dirty) >> 10;
	swap->vmag[idx]    += mss->swap >> 10;
}

static inline void display_dtp_counters(struct seq_file *s,
					char *comm,
					pid_t tgid,
					struct mm_struct *mm,
					struct smap_mem *vmem,
					struct smap_mem *rss,
					struct smap_mem *pss,
					struct smap_mem *shared,
					struct smap_mem *private,
					struct smap_mem *swap
				       )
{
	if (s) {
		/* Display the consolidated counters for all the VMAs for
		 * this process. */
		seq_printf(s, "\nComm : %s,  Pid : %d\n", comm, tgid);
		seq_printf(s,
			   "=========================================================================\n"
		"                VMSize    Rss  Rss_max  Shared  Private    Pss   Swap\n"
		"  Process Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Code Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Data Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibCode Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibData Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		" Heap-BRK Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Stack Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Other Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n",
		vmem->total, rss->total, K(get_max_total_rss(mm)),
		shared->total, private->total, pss->total, swap->total,
		vmem->vmag[0], rss->vmag[0], K(get_max_rss(mm, 0)),
		shared->vmag[0], private->vmag[0], pss->vmag[0], swap->vmag[0],
		vmem->vmag[1], rss->vmag[1], K(get_max_rss(mm, 1)),
		shared->vmag[1], private->vmag[1], pss->vmag[1], swap->vmag[1],
		vmem->vmag[2], rss->vmag[2], K(get_max_rss(mm, 2)),
		shared->vmag[2], private->vmag[2], pss->vmag[2], swap->vmag[2],
		vmem->vmag[3], rss->vmag[3], K(get_max_rss(mm, 3)),
		shared->vmag[3], private->vmag[3], pss->vmag[3], swap->vmag[3],
		vmem->vmag[4], rss->vmag[4], K(get_max_rss(mm, 4)),
		shared->vmag[4], private->vmag[4], pss->vmag[4], swap->vmag[4],
		vmem->vmag[5], rss->vmag[5], K(get_max_rss(mm, 5)),
		shared->vmag[5], private->vmag[5], pss->vmag[5], swap->vmag[5],
		vmem->vmag[6], rss->vmag[6], K(get_max_rss(mm, 6)),
		shared->vmag[6], private->vmag[6], pss->vmag[6], swap->vmag[6]
		);
	} else {
		/* Display the consolidated counters for all the VMAs for
		 * this process. */
		pr_info("\nComm : %s,  Pid : %d\n", comm, tgid);
		pr_info(
		"=========================================================================\n"
		"                VMSize    Rss  Rss_max  Shared  Private    Pss   Swap\n"
		"  Process Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Code Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Data Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibCode Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibData Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		" Heap-BRK Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Stack Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Other Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n",
		vmem->total, rss->total, K(get_max_total_rss(mm)),
		shared->total, private->total, pss->total, swap->total,
		vmem->vmag[0], rss->vmag[0], K(get_max_rss(mm, 0)),
		shared->vmag[0], private->vmag[0], pss->vmag[0], swap->vmag[0],
		vmem->vmag[1], rss->vmag[1], K(get_max_rss(mm, 1)),
		shared->vmag[1], private->vmag[1], pss->vmag[1], swap->vmag[1],
		vmem->vmag[2], rss->vmag[2], K(get_max_rss(mm, 2)),
		shared->vmag[2], private->vmag[2], pss->vmag[2], swap->vmag[2],
		vmem->vmag[3], rss->vmag[3], K(get_max_rss(mm, 3)),
		shared->vmag[3], private->vmag[3], pss->vmag[3], swap->vmag[3],
		vmem->vmag[4], rss->vmag[4], K(get_max_rss(mm, 4)),
		shared->vmag[4], private->vmag[4], pss->vmag[4], swap->vmag[4],
		vmem->vmag[5], rss->vmag[5], K(get_max_rss(mm, 5)),
		shared->vmag[5], private->vmag[5], pss->vmag[5], swap->vmag[5],
		vmem->vmag[6], rss->vmag[6], K(get_max_rss(mm, 6)),
		shared->vmag[6], private->vmag[6], pss->vmag[6], swap->vmag[6]
		);
	}
}

/* Walk all VMAs to update various counters.
 * mm->mmap_sem must be read-taken
 */
static void dump_task(struct seq_file *s, struct mm_struct *mm, char *comm,
		      pid_t tgid)
{
	struct vm_area_struct *vma;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
#ifdef CONFIG_PROC_PAGE_MONITOR
		.pmd_entry = smaps_pte_range,
#endif
		.pte_entry = NULL,
		.pte_hole = NULL,
		.private = &mss,
	};
	struct smap_mem vmem;
	struct smap_mem rss;
	struct smap_mem pss;
	struct smap_mem shared;
	struct smap_mem private;
	struct smap_mem swap;

	smaps_walk.mm = mm;

	vma = mm->mmap;

	memset(&vmem    , 0, sizeof(struct smap_mem));
	memset(&rss     , 0, sizeof(struct smap_mem));
	memset(&shared  , 0, sizeof(struct smap_mem));
	memset(&private , 0, sizeof(struct smap_mem));
	memset(&pss     , 0, sizeof(struct smap_mem));
	memset(&swap    , 0, sizeof(struct smap_mem));

	while (vma) {
		/* Ignore the huge TLB pages. */
		if (vma->vm_mm && !(vma->vm_flags & VM_HUGETLB)) {
			memset(&mss, 0, sizeof(struct mem_size_stats));

			walk_page_range(vma->vm_start, vma->vm_end,
					&smaps_walk);

			update_dtp_counters(vma, &mss, &vmem, &rss, &pss,
					    &shared, &private, &swap);
		}
		vma = vma->vm_next;
	}

	display_dtp_counters(s, comm, tgid, mm, &vmem, &rss, &pss,
			     &shared, &private, &swap);

	sum_of_pss += pss.total;
}


void dump_tasks(struct mem_cgroup *mem,
		       const nodemask_t *nodemask, struct seq_file *s);

/* if oom_cond == 1 then OOM condition is considered */
static void dump_tasks_plus_oom(struct mem_cgroup *mem,
				struct seq_file *s, int oom_cond)
{
	struct mm_struct *mm = NULL;
	struct vm_area_struct *vma = NULL;
	struct task_struct *g = NULL, *p = NULL;
	struct mm_sem_struct *mm_sem;
	struct list_head *pos, *q;
	struct list_head mm_sem_list = LIST_HEAD_INIT(mm_sem_list);

	/* lock current tasklist state */
	rcu_read_lock();

	/* call original dump_tasks */
	dump_tasks(NULL, NULL, s);

	sum_of_pss = 0;

	/* dump tasks with additional info */
	do_each_thread(g, p) {
		if (mem && !task_in_mem_cgroup(p, mem))
			continue;

		if (!thread_group_leader(p))
			continue;

		task_lock(p);
		mm = p->mm;
		if (!mm) {
			task_unlock(p);
			continue;
		}

		vma = mm->mmap;
		if (!vma) {
			task_unlock(p);
			continue;
		}

		if (down_read_trylock(&mm->mmap_sem)) {
			/* If we took semaphore then dump info here */
			dump_task(s, mm, p->comm, p->tgid);
			up_read(&mm->mmap_sem);
		} else {
			/* Else add mm to dump later list */
			mm_sem = kmalloc(sizeof(*mm_sem), GFP_ATOMIC);
			if (mm_sem == NULL) {
				pr_err("dump_tasks_plus: can't allocate struct mm_sem!");
				task_unlock(p);
				rcu_read_unlock();
				return;
			} else {
				memcpy(mm_sem->task_comm, p->comm,
					sizeof(p->comm));
				mm_sem->tgid = p->tgid;
				mm_sem->mm = mm;
				INIT_LIST_HEAD(&mm_sem->list);
				list_add(&mm_sem->list, &mm_sem_list);

				/* Increase mm usage counter to ensure mm is
				 * still valid without tasklock */
				atomic_inc(&mm->mm_users);
			}
		}

		task_unlock(p);
	} while_each_thread(g, p);

	/* unlock tasklist to take remaining semaphores */
	rcu_read_unlock();

	/* print remaining tasks info */
	list_for_each(pos, &mm_sem_list) {
		mm_sem = list_entry(pos, struct mm_sem_struct, list);

		mm = mm_sem->mm;

		if (oom_cond) {
			/* Can't wait for semaphore in OOM killer context. */
			if (down_read_trylock(&mm->mmap_sem)) {
				dump_task(s, mm, mm_sem->task_comm,
					  mm_sem->tgid);
				up_read(&mm->mmap_sem);
			} else {
				pr_warn("Skipping task with tgid = %d and comm "
				     "'%s'\n", mm_sem->tgid, mm_sem->task_comm);
			}
		} else {
			down_read(&mm->mmap_sem);
			dump_task(s, mm, mm_sem->task_comm, mm_sem->tgid);
			up_read(&mm->mmap_sem);
		}

		/* release mm */
		mmput(mm);
	}

	/* free mm_sem list */
	list_for_each_safe(pos, q, &mm_sem_list) {
		mm_sem = list_entry(pos, struct mm_sem_struct, list);
		list_del(pos);
		kfree(mm_sem);
	}

	if (s)
		seq_printf(s, "\n* Sum of pss : %6lu (kB)\n", sum_of_pss);
	else
		pr_info("\n* Sum of pss : %6lu (kB)\n", sum_of_pss);
}

void dump_tasks_plus(struct mem_cgroup *mem, struct seq_file *s)
{
	dump_tasks_plus_oom(mem, s, 0);
}
#endif

#ifdef CONFIG_VD_MEMINFO
extern int oom_score_to_adj(short oom_score_adj);
#endif

/**
 * dump_tasks - dump current memory state of all system tasks
 * @memcg: current's memory controller, if constrained
 * @nodemask: nodemask passed to page allocator for mempolicy ooms
 *
 * Dumps the current memory state of all eligible tasks.  Tasks not in the same
 * memcg, not in the same cpuset, or bound to a disjoint set of mempolicy nodes
 * are not shown.
 * State information includes task's pid, uid, tgid, vm size, rss, nr_ptes,
 * swapents, oom_score_adj value, and name.
 */
void dump_tasks(struct mem_cgroup *mem,
			const nodemask_t *nodemask, struct seq_file *s)
{
	struct task_struct *p;
	struct task_struct *task;

#ifndef CONFIG_VD_MEMINFO
	pr_warn("[ pid ]   uid  tgid  ppid total_vm      rss nr_ptes nr_pmds swapents oom_score_adj name\n");
#else
	unsigned long cur_cnt[VMAG_CNT], max_cnt[VMAG_CNT];
	unsigned long tot_rss_cnt = 0;
	unsigned long tot_maxrss_cnt = 0;
	unsigned long sum_of_tot_rss_cnt = 0;
	unsigned long sum_of_tot_maxrss_cnt = 0;
	unsigned long sum_of_tot_swap_cnt = 0;
	int i;

	if (s)
		seq_printf(s, "[ pid ]   uid  tgid  ppid total_vm      rss  rss_max"
				"   swap state cpu oom_score_adj "
				"name\n");
	else
		pr_warn("[ pid ]   uid  tgid  ppid total_vm      rss  rss_max"
				"   swap state cpu oom_score_adj name\n");
#endif

	rcu_read_lock();
	for_each_process(p) {
		if (oom_unkillable_task(p, mem, nodemask)
			&& !is_task_systemd(p))
			continue;

		sync_process_rss(p);
		task = find_lock_task_mm(p);
		if (!task) {
			/*
			 * This is a kthread or all of p's threads have already
			 * detached their mm's.  There's no need to report
			 * them; they can't be oom killed anyway.
			 */
			continue;
		}
#ifndef CONFIG_VD_MEMINFO
		pr_warn("[%5d] %5d %5d %5d %8lu %8lu %7ld %7ld %8lu         %5hd %s\n",
			task->pid, from_kuid(&init_user_ns, task_uid(task)),
			task->tgid,
			task_ppid_nr(task),
			task->mm->total_vm, get_mm_rss(task->mm),
			atomic_long_read(&task->mm->nr_ptes),
			mm_nr_pmds(task->mm),
			get_mm_counter(task->mm, MM_SWAPENTS),
			task->signal->oom_score_adj, task->comm);
#else
		for (i = 0; i < VMAG_CNT; i++) {
			get_rss_cnt(task->mm, i, &cur_cnt[i], &max_cnt[i]);
			tot_rss_cnt += cur_cnt[i];
		}
		tot_maxrss_cnt = get_max_total_rss(task->mm);

		if (s)
			seq_printf(s, "[%5d] %5u %5d %5d %8lu %8lu %8lu %6lu %5c %3d"
					"     %5d %s\n",
				p->pid, __task_cred(p)->uid.val, p->tgid,
				task_ppid_nr(p),
				K(task->mm->total_vm), K(tot_rss_cnt),
				K(tot_maxrss_cnt),
				K(get_mm_counter(task->mm, MM_SWAPENTS)),
				get_task_state(p),
				(int)task_cpu(p), p->signal->oom_score_adj,
				p->comm);
		else
			pr_warn("[%5d] %5u %5d %5d %8lu %8lu %8lu %6lu %5c %3d"
				"     %5d %s\n",
				p->pid, __task_cred(p)->uid.val, p->tgid,
				task_ppid_nr(p),
				K(task->mm->total_vm), K(tot_rss_cnt),
				K(tot_maxrss_cnt),
				K(get_mm_counter(task->mm, MM_SWAPENTS)),
				get_task_state(p),
				(int)task_cpu(p), p->signal->oom_score_adj,
				p->comm);

		sum_of_tot_rss_cnt += tot_rss_cnt;
		sum_of_tot_maxrss_cnt += tot_maxrss_cnt;
		sum_of_tot_swap_cnt += get_mm_counter(task->mm, MM_SWAPENTS);
		tot_rss_cnt = 0;
		tot_maxrss_cnt = 0;
#endif

		task_unlock(task);
	}
	rcu_read_unlock();
#ifdef CONFIG_VD_MEMINFO
	if (s) {
		seq_printf(s, "* Sum of rss    : %6lu (kB)\n",
			K(sum_of_tot_rss_cnt));
		seq_printf(s, "* Sum of maxrss : %6lu (kB)\n",
			K(sum_of_tot_maxrss_cnt));
		seq_printf(s, "* Sum of swap   : %6lu (kB)\n",
			K(sum_of_tot_swap_cnt));
	} else {
		pr_warn("* Sum of rss    : %6lu (kB)\n",
			K(sum_of_tot_rss_cnt));
		pr_warn("* Sum of maxrss : %6lu (kB)\n",
			K(sum_of_tot_maxrss_cnt));
		pr_warn("* Sum of swap   : %6lu (kB)\n",
			K(sum_of_tot_swap_cnt));
	}
#endif
}

#if defined(CONFIG_SLP_LOWMEM_NOTIFY) && !defined(CONFIG_VD_RELEASE)
#define MEM_SWAP_RATIO   (1/2)
#define  PRINT_MAX_PROCESS   10
static LIST_HEAD(graphic_process_list);
struct gfx_tsk_arr {
	struct list_head entry;
	pid_t pid;
	unsigned long gem;
	unsigned long gpu;
};
static unsigned int graphics_module_type;
struct dump_tsk_arr {
	pid_t pid;
	unsigned long rss;
	unsigned long swap;
	unsigned long gem;
	unsigned long gpu;
};
static struct dump_tsk_arr tsk_db[PRINT_MAX_PROCESS];

static void low_mem_get_pid_memory(pid_t graphic_pid, int graphic_memory)
{
	struct task_struct *tsk = NULL;
	struct gfx_tsk_arr *new_item = NULL;
	struct list_head *tmp;
	struct gfx_tsk_arr *regi_item;

	rcu_read_lock();
	tsk = find_task_by_pid_ns(graphic_pid, &init_pid_ns);
	if (tsk)
		get_task_struct(tsk);
	rcu_read_unlock();
	if (tsk) {
		list_for_each(tmp, &graphic_process_list) {
			regi_item = list_entry(tmp, struct gfx_tsk_arr, entry);
			if (regi_item->pid == graphic_pid) {
				if (graphics_module_type == GEM_GRAPHIC_TYPE)
					regi_item->gem = graphic_memory >> 10;
				else
					regi_item->gpu = graphic_memory >> 10;
				put_task_struct(tsk);
				return;
			}
		}
		new_item = kzalloc(sizeof(*new_item), GFP_KERNEL);
		if (!new_item) {
			pr_err("[FAIL] Fail to allocate memory\n");
			put_task_struct(tsk);
			return;
		}
		INIT_LIST_HEAD(&new_item->entry);
		new_item->pid = graphic_pid;
		if (graphics_module_type == GEM_GRAPHIC_TYPE) {
			new_item->gem = graphic_memory >> 10;
		} else {
			new_item->gpu = graphic_memory >> 10;
		}
		list_add_tail(&new_item->entry, &graphic_process_list);
		put_task_struct(tsk);
	} else
		pr_err("Task is not existed for the pid(%d)\n", graphic_pid);
}

static void low_mem_show_graphic_register_func(void)
{
	struct list_head *tmp;
	struct graphic_func_item *regi_item;
	int i = 0;

	mutex_lock(&graphic_module_mutex);
	list_for_each(tmp, &graphic_module_list) {
		regi_item = list_entry(tmp, struct graphic_func_item, entry);
		if (regi_item->graphic_func) {
			graphics_module_type = regi_item->graphic_type;
			regi_item->graphic_func(regi_item->graphic_type,
					low_mem_get_pid_memory);
		}
	}
	mutex_unlock(&graphic_module_mutex);
}

static int compare_task_size_values(const void *a, const void *b)
{
	const struct dump_tsk_arr *_a = a;
	const struct dump_tsk_arr *_b = b;
	return (_b->rss + _b->swap*(MEM_SWAP_RATIO) + _b->gem + _b->gpu)
		-(_a->rss + _a->swap*(MEM_SWAP_RATIO) + _a->gem + _a->gpu);
}

static void get_proc_show(void)
{
	struct sysinfo i;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	int lru;
	long cached;
#ifdef CONFIG_VMALLOCUSED_PLUS
	struct vmalloc_info vmi;
	struct vmalloc_usedinfo vmallocused;
#endif
#ifdef CONFIG_CMA
	unsigned long cma_free;
	unsigned long cma_device_used;
#endif
	si_meminfo(&i);
	si_swapinfo(&i);
	available = si_mem_available();
	cached = global_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;
	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
                pages[lru] = global_page_state(NR_LRU_BASE + lru);
	pr_err("MemTotal:       %8lu kB "
#ifdef CONFIG_HIGHMEM
			"HighFree:       %8lu kB "
			"LowFree:        %8lu kB "
#endif
			"SwapFree:       %8lu kB "
			"Anon_Total:	 %8lu kB\n"
			"Cached:         %8lu kB "
			"Shmem:          %8lu kB "
			"Slab_Total:     %8lu kB "
			"KernelStack:    %8lu kB "
			"PageTables:     %8lu kB\n",
			K(i.totalram),
#ifdef CONFIG_HIGHMEM
			K(i.freehigh),
			K(i.freeram-i.freehigh),
#endif			
			K(i.freeswap),
			K(pages[LRU_ACTIVE_ANON] + pages[LRU_INACTIVE_ANON]),
			K(cached),
			K(i.sharedram),
			K((global_page_state(NR_SLAB_RECLAIMABLE))
			+ (global_page_state(NR_SLAB_UNRECLAIMABLE))),
			global_page_state(NR_KERNEL_STACK) * THREAD_SIZE / 1024,
			K(global_page_state(NR_PAGETABLE)));
#ifdef CONFIG_VMALLOCUSED_PLUS
	get_vmalloc_info(&vmi);
	memset(&vmallocused, 0, sizeof(struct vmalloc_usedinfo));
	get_vmallocused(&vmallocused);
	pr_warn("VmallocUsed:    %8lu kB\n",
			(vmi.used - vmallocused.vm_lazy_free) >> 10);
#endif
#ifdef CONFIG_CMA
	cma_free = cma_get_free();
	cma_device_used = cma_get_device_used_pages();
	pr_warn("CMAFree:   %8lu kB"
			" CMAUsed-DMA: %8lu kB\n"
			, K(cma_free)
			, K(cma_device_used));
#endif
}

static inline int find_low(void)
{
	int i = 0, low = INT_MAX, index = 0;
	unsigned long size;

	for (i = 0; i < PRINT_MAX_PROCESS; i++) {
		size = tsk_db[i].rss + tsk_db[i].swap
					*(MEM_SWAP_RATIO) + tsk_db[i].gem
					+ tsk_db[i].gpu;
		if (size < low) {
			index = i;
			low = size;
		}
	}
	return index;
}

void dump_tasks_lmf(struct mem_cgroup *mem,
		const nodemask_t *nodemask, struct seq_file *s)
{
	struct task_struct *p;
	struct task_struct *task;
	struct mm_struct *mm = NULL;
	unsigned long cur_cnt[VMAG_CNT], max_cnt[VMAG_CNT];
	int count = 0;
	int i = 0;
	int index = 0;
	struct gfx_tsk_arr *gfx_item = NULL;
	struct list_head *pos, *q;
	struct dump_tsk_arr tmp_tsk_db;

	low_mem_show_graphic_register_func();
	pr_err("[ pid ]      rss "
			"   swap    gem    gpu    oom_score_adj   name");

	rcu_read_lock();
	for_each_process(p) {
		if (oom_unkillable_task(p, mem, nodemask)
			&& !is_task_systemd(p))
			continue;

		sync_process_rss(p);
		task = find_lock_task_mm(p);
		if (!task) {
			/*
			 * This is a kthread or all of p's threads have already
			 * detached their mm's.  There's no need to report
			 * them; they can't be oom killed anyway.
			 */
			continue;
		}

		for (i = 0; i < VMAG_CNT; i++) {
			get_rss_cnt(task->mm, i, &cur_cnt[i], &max_cnt[i]);
		}

		tmp_tsk_db.pid = task->pid;
		tmp_tsk_db.rss = K(get_mm_rss(task->mm));
		tmp_tsk_db.swap =  get_mm_counter(task->mm,
				MM_SWAPENTS);
		tmp_tsk_db.gem = 0;
		tmp_tsk_db.gpu = 0;
		list_for_each(pos, &graphic_process_list) {
			gfx_item = list_entry(pos, struct gfx_tsk_arr,
					entry);
			if (gfx_item->pid == tmp_tsk_db.pid) {
				tmp_tsk_db.gem = gfx_item->gem;
				tmp_tsk_db.gpu = gfx_item->gpu;
			}
		}
		if (count < PRINT_MAX_PROCESS) {
			tsk_db[count] = tmp_tsk_db;
			count++;
		} else {
			index = find_low();
			if ((tmp_tsk_db.rss + tmp_tsk_db.swap*(MEM_SWAP_RATIO)
					+ tmp_tsk_db.gem + tmp_tsk_db.gpu)
				> (tsk_db[index].rss + tsk_db[index].swap
					*(MEM_SWAP_RATIO) + tsk_db[index].gem
						+ tsk_db[index].gpu)) {
				tsk_db[index] = tmp_tsk_db;
			}
		}
		task_unlock(task);
	}
	rcu_read_unlock();
	list_for_each_safe(pos, q, &graphic_process_list) {
		gfx_item = list_entry(pos, struct gfx_tsk_arr, entry);
		list_del(pos);
		kfree(gfx_item);
	}
	sort(tsk_db, count, sizeof(struct dump_tsk_arr),
			compare_task_size_values, NULL);
	for (i = 0; i < count; i++) {
		rcu_read_lock();
		task = find_task_by_pid_ns(tsk_db[i].pid, &init_pid_ns);
		if (task) {
			get_task_struct(task);
			rcu_read_unlock();
			mm = get_task_mm(task);
			if (mm) {
				pr_warn("[%5d] %10lu %6lu %6lu "
						"%6lu %13d    %s\n",
						tsk_db[i].pid,
						tsk_db[i].rss,
						K(get_mm_counter(task->mm,
								MM_SWAPENTS)),
						tsk_db[i].gem,
						tsk_db[i].gpu,
						task->signal->oom_score_adj,
						task->comm);
				mmput(mm);
				mm = NULL;
			}
			put_task_struct(task);
		} else
			rcu_read_unlock();
	}
	get_proc_show();
}
#endif

static void dump_header(struct task_struct *p, gfp_t gfp_mask, int order,
			struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	task_lock(current);
	pr_warning("%s invoked oom-killer: gfp_mask=0x%x, order=%d, "
		"oom_score_adj=%hd\n",
		current->comm, gfp_mask, order,
		current->signal->oom_score_adj);
	cpuset_print_task_mems_allowed(current);
	task_unlock(current);
	dump_stack();
	mutex_lock(&oom_owner_mutex);
	if (oom_processing_pid == -1) {
		oom_processing_pid = 1;
		mutex_unlock(&oom_owner_mutex);
	} else {

		/* Already print logs on other core no need to do same
		 * Things. So just skiping.
		 */
		mutex_unlock(&oom_owner_mutex);
		return;
	}
	/* get panic There's no process killable if p is NULL */
	if (p == NULL || oom_processing_pid == 1) {
		if (memcg)
			mem_cgroup_print_oom_info(memcg, p);
		else
			show_mem(SHOW_MEM_FILTER_NODES);
		if (sysctl_oom_dump_tasks)
#ifdef CONFIG_VD_MEMINFO
			dump_tasks(NULL, NULL, NULL);
#else
		dump_tasks(memcg, nodemask, NULL);
#endif
		print_slabinfo_oom();
		show_graphic_register_func();
		/* Adding threshold delay, to change system memory
		 * state.
		 */
		oom_processing_pid = -1;
	}
}

/*
 * Number of OOM victims in flight
 */
static atomic_t oom_victims = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(oom_victims_wait);

bool oom_killer_disabled __read_mostly;
static DECLARE_RWSEM(oom_sem);

/**
 * mark_tsk_oom_victim - marks the given task as OOM victim.
 * @tsk: task to mark
 *
 * Has to be called with oom_sem taken for read and never after
 * oom has been disabled already.
 */
void mark_tsk_oom_victim(struct task_struct *tsk)
{
	WARN_ON(oom_killer_disabled);
	/* OOM killer might race with memcg OOM */
	if (test_and_set_tsk_thread_flag(tsk, TIF_MEMDIE))
		return;
	/*
	 * Make sure that the task is woken up from uninterruptible sleep
	 * if it is frozen because OOM killer wouldn't be able to free
	 * any memory and livelock. freezing_slow_path will tell the freezer
	 * that TIF_MEMDIE tasks should be ignored.
	 */
	__thaw_task(tsk);
	atomic_inc(&oom_victims);
}

/**
 * unmark_oom_victim - unmarks the current task as OOM victim.
 *
 * Wakes up all waiters in oom_killer_disable()
 */
void unmark_oom_victim(void)
{
	if (!test_and_clear_thread_flag(TIF_MEMDIE))
		return;

	down_read(&oom_sem);
	/*
	 * There is no need to signal the lasst oom_victim if there
	 * is nobody who cares.
	 */
	if (!atomic_dec_return(&oom_victims) && oom_killer_disabled)
		wake_up_all(&oom_victims_wait);
	up_read(&oom_sem);
}

/**
 * oom_killer_disable - disable OOM killer
 *
 * Forces all page allocations to fail rather than trigger OOM killer.
 * Will block and wait until all OOM victims are killed.
 *
 * The function cannot be called when there are runnable user tasks because
 * the userspace would see unexpected allocation failures as a result. Any
 * new usage of this function should be consulted with MM people.
 *
 * Returns true if successful and false if the OOM killer cannot be
 * disabled.
 */
bool oom_killer_disable(void)
{
	/*
	 * Make sure to not race with an ongoing OOM killer
	 * and that the current is not the victim.
	 */
	down_write(&oom_sem);
	if (test_thread_flag(TIF_MEMDIE)) {
		up_write(&oom_sem);
		return false;
	}

	oom_killer_disabled = true;
	up_write(&oom_sem);

	wait_event(oom_victims_wait, !atomic_read(&oom_victims));

	return true;
}

/**
 * oom_killer_enable - enable OOM killer
 */
void oom_killer_enable(void)
{
	down_write(&oom_sem);
	oom_killer_disabled = false;
	up_write(&oom_sem);
}

#define K(x) ((x) << (PAGE_SHIFT-10))
/*
 * Must be called while holding a reference to p, which will be released upon
 * returning.
 */
void oom_kill_process(struct task_struct *p, gfp_t gfp_mask, int order,
		      unsigned int points, unsigned long totalpages,
		      struct mem_cgroup *memcg, nodemask_t *nodemask,
		      const char *message)
{
	struct task_struct *victim = p;
	struct task_struct *child;
	struct task_struct *t;
	struct mm_struct *mm;
	unsigned int victim_points = 0;
#ifdef CONFIG_KPI_SYSTEM_SUPPORT
	struct task_struct *pgrp_task = NULL;
	pid_t pgid = -1;
	int oom_score_adj = 0;
	struct pid_namespace *ns = NULL;
	char victim_comm[TASK_COMM_LEN], victim_grp_ldr_comm[TASK_COMM_LEN];
#endif
	static DEFINE_RATELIMIT_STATE(oom_rs, DEFAULT_RATELIMIT_INTERVAL,
					      DEFAULT_RATELIMIT_BURST);
#ifdef	CONFIG_ENABLE_OOM_DEBUG_LOGS
	int ori_console_loglevel = console_loglevel;
	/* Set console log level to default level (7). */
	console_default();
#endif

	/*
	 * If the task is already exiting, don't alarm the sysadmin or kill
	 * its children or threads, just set TIF_MEMDIE so it can die quickly
	 */
	task_lock(p);
	if (p->mm && task_will_free_mem(p)) {
		mark_tsk_oom_victim(p);
		task_unlock(p);
		put_task_struct(p);
		return;
	}
	task_unlock(p);

	if (__ratelimit(&oom_rs))
		dump_header(p, gfp_mask, order, memcg, nodemask);

#ifdef	CONFIG_ENABLE_OOM_DEBUG_LOGS
	/* Revert console log level to saved log level value. */
	console_revert(ori_console_loglevel);
#endif

	task_lock(p);
	pr_err("%s: Kill process %d tgid: %d exec_start: %lu (%s) score %d or sacrifice child\n",
		message, task_pid_nr(p), task_tgid_nr(p),
		(unsigned long)((p->se.exec_start) >> 20), p->comm, points);
	task_unlock(p);

	/*
	 * If any of p's children has a different mm and is eligible for kill,
	 * the one with the highest oom_badness() score is sacrificed for its
	 * parent.  This attempts to lose the minimal amount of work done while
	 * still freeing memory.
	 */
	read_lock(&tasklist_lock);
	for_each_thread(p, t) {
		list_for_each_entry(child, &t->children, sibling) {
			unsigned int child_points;

			if (child->mm == p->mm)
				continue;
			/*
			 * oom_badness() returns 0 if the thread is unkillable
			 */
			child_points = oom_badness(child, memcg, nodemask,
								totalpages);
			if (child_points > victim_points) {
				put_task_struct(victim);
				victim = child;
				victim_points = child_points;
				get_task_struct(victim);
			}
		}
	}
	read_unlock(&tasklist_lock);

	p = find_lock_task_mm(victim);
	if (!p) {
		put_task_struct(victim);
		return;
	} else if (victim != p) {
		get_task_struct(p);
		put_task_struct(victim);
		victim = p;
	}

	/* mm cannot safely be dereferenced after task_unlock(victim) */
	mm = victim->mm;
	mark_tsk_oom_victim(victim);
	pr_err("Killed process %d (%s) total-vm:%lukB, anon-rss:%lukB, file-rss:%lukB\n",
		task_pid_nr(victim), victim->comm, K(victim->mm->total_vm),
		K(get_mm_counter(victim->mm, MM_ANONPAGES)),
		K(get_mm_counter(victim->mm, MM_FILEPAGES)));

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
	/* kpi_fault */
	rcu_read_lock();
	ns = task_active_pid_ns(current);
	if (ns) {
		pgid = task_pgrp_vnr(victim);
		pgrp_task = find_task_by_pid_ns(pgid, ns);
		if (pgrp_task) {
			get_task_struct(pgrp_task);
			oom_score_adj = pgrp_task->signal->oom_score_adj;
			put_task_struct(pgrp_task);
		}
	}
	memcpy((void *)victim_comm, (const void *)victim->comm, TASK_COMM_LEN);
	memcpy((void *)victim_grp_ldr_comm, (const void *)
				victim->group_leader->comm, TASK_COMM_LEN);
	rcu_read_unlock();
	task_unlock(victim);
	if (oom_score_adj >= 150 && oom_score_adj <= 200)
		set_kpi_fault(0, 0, victim_comm, victim_grp_ldr_comm,
					"OOM_FG", pgid);
	else
		set_kpi_fault(0, 0, victim_comm, victim_grp_ldr_comm,
					"OOM", pgid);
#else
	task_unlock(victim);
#endif

	/*
	 * Kill all user processes sharing victim->mm in other thread groups, if
	 * any.  They don't get access to memory reserves, though, to avoid
	 * depletion of all memory.  This prevents mm->mmap_sem livelock when an
	 * oom killed thread cannot exit because it requires the semaphore and
	 * its contended by another thread trying to allocate memory itself.
	 * That thread will now get access to memory reserves since it has a
	 * pending fatal signal.
	 */
	rcu_read_lock();
	for_each_process(p)
		if (p->mm == mm && !same_thread_group(p, victim) &&
		    !(p->flags & PF_KTHREAD)) {
			if (p->signal->oom_score_adj == OOM_SCORE_ADJ_MIN)
				continue;

			task_lock(p);	/* Protect ->comm from prctl() */
			pr_err("Kill process %d (%s) sharing same memory\n",
				task_pid_nr(p), p->comm);
			task_unlock(p);
			do_send_sig_info(SIGKILL, SEND_SIG_FORCED, p, true);
		}
	rcu_read_unlock();

	do_send_sig_info(SIGKILL, SEND_SIG_FORCED, victim, true);
	put_task_struct(victim);
}
#undef K

/*
 * Determines whether the kernel must panic because of the panic_on_oom sysctl.
 */
void check_panic_on_oom(enum oom_constraint constraint, gfp_t gfp_mask,
			int order, const nodemask_t *nodemask,
			struct mem_cgroup *memcg)
{
	if (likely(!sysctl_panic_on_oom))
		return;
	if (sysctl_panic_on_oom != 2) {
		/*
		 * panic_on_oom == 1 only affects CONSTRAINT_NONE, the kernel
		 * does not panic for cpuset, mempolicy, or memcg allocation
		 * failures.
		 */
		if (constraint != CONSTRAINT_NONE)
			return;
	}
	dump_header(NULL, gfp_mask, order, memcg, nodemask);
	panic("Out of memory: %s panic_on_oom is enabled\n",
		sysctl_panic_on_oom == 2 ? "compulsory" : "system-wide");
}

static BLOCKING_NOTIFIER_HEAD(oom_notify_list);

int register_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_oom_notifier);

int unregister_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_oom_notifier);

/*
 * Try to acquire the OOM killer lock for the zones in zonelist.  Returns zero
 * if a parallel OOM killing is already taking place that includes a zone in
 * the zonelist.  Otherwise, locks all zones in the zonelist and returns 1.
 */
bool oom_zonelist_trylock(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;
	bool ret = true;

	spin_lock(&zone_scan_lock);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask))
		if (test_bit(ZONE_OOM_LOCKED, &zone->flags)) {
			ret = false;
			goto out;
		}

	/*
	 * Lock each zone in the zonelist under zone_scan_lock so a parallel
	 * call to oom_zonelist_trylock() doesn't succeed when it shouldn't.
	 */
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask))
		set_bit(ZONE_OOM_LOCKED, &zone->flags);

out:
	spin_unlock(&zone_scan_lock);
	return ret;
}

/*
 * Clears the ZONE_OOM_LOCKED flag for all zones in the zonelist so that failed
 * allocation attempts with zonelists containing them may now recall the OOM
 * killer, if necessary.
 */
void oom_zonelist_unlock(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;

	spin_lock(&zone_scan_lock);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask))
		clear_bit(ZONE_OOM_LOCKED, &zone->flags);
	spin_unlock(&zone_scan_lock);
}

/**
 * __out_of_memory - kill the "best" process when we run out of memory
 * @zonelist: zonelist pointer
 * @gfp_mask: memory allocation flags
 * @order: amount of memory being requested as a power of 2
 * @nodemask: nodemask passed to page allocator
 * @force_kill: true if a task must be killed, even if others are exiting
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 */
static void __out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask,
		int order, nodemask_t *nodemask, bool force_kill)
{
	const nodemask_t *mpol_mask;
	struct task_struct *p;
	unsigned long totalpages;
	unsigned long freed = 0;
	unsigned int uninitialized_var(points);
	enum oom_constraint constraint = CONSTRAINT_NONE;
	int killed = 0;

	if (only_one_time_call) {
			if (panic_ratelimit_burst && panic_ratelimit_interval)
				set_ratelimit_state(&oom_panic_rs);
			only_one_time_call = false;
	}

	blocking_notifier_call_chain(&oom_notify_list, 0, &freed);
	if (freed > 0)
		/* Got some memory back in the last second. */
		return;

	/*
	 * If current has a pending SIGKILL or is exiting, then automatically
	 * select it.  The goal is to allow it to allocate so that it may
	 * quickly exit and free its memory.
	 *
	 * But don't select if current has already released its mm and cleared
	 * TIF_MEMDIE flag at exit_mm(), otherwise an OOM livelock may occur.
	 */
	if (current->mm &&
	    (fatal_signal_pending(current) || task_will_free_mem(current))) {
		mark_tsk_oom_victim(current);
		return;
	}
#ifdef CONFIG_SMART_DEADLOCK
	hook_smart_deadlock_exception_case(SMART_DEADLOCK_OOM);
#endif
	/*
	 * Check if there were limitations on the allocation (only relevant for
	 * NUMA) that may require different handling.
	 */
	constraint = constrained_alloc(zonelist, gfp_mask, nodemask,
						&totalpages);
	mpol_mask = (constraint == CONSTRAINT_MEMORY_POLICY) ? nodemask : NULL;
	check_panic_on_oom(constraint, gfp_mask, order, mpol_mask, NULL);

	if (sysctl_oom_kill_allocating_task && current->mm &&
	    !oom_unkillable_task(current, NULL, nodemask) &&
	    current->signal->oom_score_adj != OOM_SCORE_ADJ_MIN) {
		get_task_struct(current);
		oom_kill_process(current, gfp_mask, order, 0, totalpages, NULL,
				 nodemask,
				 "Out of memory (oom_kill_allocating_task)");
		goto out;
	}

	p = select_bad_process(&points, totalpages, mpol_mask, force_kill);
	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (!p) {
		dump_header(NULL, gfp_mask, order, NULL, mpol_mask);
		panic("Out of memory and no killable processes...\n");
	}
	if (p != (void *)-1UL) {
		oom_kill_process(p, gfp_mask, order, points, totalpages, NULL,
				 nodemask, "Out of memory");
		killed = 1;
	}
out:
	/*
	 * Give the killed threads a good chance of exiting before trying to
	 * allocate memory again.
	 */
	if (killed) {
		schedule_timeout_killable(1);
		if (__ratelimit(&oom_panic_rs)) {
			if (oom_panic_rs.printed == panic_ratelimit_burst) {
				dump_header(NULL, gfp_mask, order, NULL, mpol_mask);
#ifdef CONFIG_OOM_PANIC
				panic("Out of memory and couldn't recover lowmem by kill processes...\n");
#else
				pr_err("Out of memory and couldn't recover lowmem by kill processes...\n");
#endif
			}
		} else
			reset_ratelimit_state(&oom_panic_rs);
	}
}

/**
 * out_of_memory -  tries to invoke OOM killer.
 * @zonelist: zonelist pointer
 * @gfp_mask: memory allocation flags
 * @order: amount of memory being requested as a power of 2
 * @nodemask: nodemask passed to page allocator
 * @force_kill: true if a task must be killed, even if others are exiting
 *
 * invokes __out_of_memory if the OOM is not disabled by oom_killer_disable()
 * when it returns false. Otherwise returns true.
 */
bool out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask,
		int order, nodemask_t *nodemask, bool force_kill)
{
	bool ret = false;

	down_read(&oom_sem);
	if (!oom_killer_disabled) {
		__out_of_memory(zonelist, gfp_mask, order, nodemask, force_kill);
		ret = true;
	}
	up_read(&oom_sem);

	return ret;
}

/*
 * The pagefault handler calls here because it is out of memory, so kill a
 * memory-hogging task.  If any populated zone has ZONE_OOM_LOCKED set, a
 * parallel oom killing is already in progress so do nothing.
 */
void pagefault_out_of_memory(void)
{
	struct zonelist *zonelist;

	down_read(&oom_sem);
	if (mem_cgroup_oom_synchronize(true))
		goto unlock;

	zonelist = node_zonelist(first_memory_node, GFP_KERNEL);
	if (oom_zonelist_trylock(zonelist, GFP_KERNEL)) {
		if (!oom_killer_disabled)
			__out_of_memory(NULL, 0, 0, NULL, false);
		else
			/*
			 * There shouldn't be any user tasks runable while the
			 * OOM killer is disabled so the current task has to
			 * be a racing OOM victim for which oom_killer_disable()
			 * is waiting for.
			 */
			WARN_ON(test_thread_flag(TIF_MEMDIE));

		oom_zonelist_unlock(zonelist, GFP_KERNEL);
	}
unlock:
	up_read(&oom_sem);
}
