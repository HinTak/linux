#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>
#include <linux/swapops.h>
#include <linux/mmu_notifier.h>

#include <asm/elf.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include "internal.h"

void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	unsigned long data, text, lib, swap, ptes, pmds;
	unsigned long hiwater_vm, total_vm, hiwater_rss, total_rss;

	/*
	 * Note: to minimize their overhead, mm maintains hiwater_vm and
	 * hiwater_rss only when about to *lower* total_vm or rss.  Any
	 * collector of these hiwater stats must therefore get total_vm
	 * and rss too, which will usually be the higher.  Barriers? not
	 * worth the effort, such snapshots can always be inconsistent.
	 */
	hiwater_vm = total_vm = mm->total_vm;
	if (hiwater_vm < mm->hiwater_vm)
		hiwater_vm = mm->hiwater_vm;
	hiwater_rss = total_rss = get_mm_rss(mm);
	if (hiwater_rss < mm->hiwater_rss)
		hiwater_rss = mm->hiwater_rss;

	data = mm->total_vm - mm->shared_vm - mm->stack_vm;
	text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK)) >> 10;
	lib = (mm->exec_vm << (PAGE_SHIFT-10)) - text;
	swap = get_mm_counter(mm, MM_SWAPENTS);
	ptes = PTRS_PER_PTE * sizeof(pte_t) * atomic_long_read(&mm->nr_ptes);
	pmds = PTRS_PER_PMD * sizeof(pmd_t) * mm_nr_pmds(mm);
	seq_printf(m,
		"VmPeak:\t%8lu kB\n"
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmPin:\t%8lu kB\n"
		"VmHWM:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n"
		"VmPTE:\t%8lu kB\n"
		"VmPMD:\t%8lu kB\n"
		"VmSwap:\t%8lu kB\n",
		hiwater_vm << (PAGE_SHIFT-10),
		total_vm << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		mm->pinned_vm << (PAGE_SHIFT-10),
		hiwater_rss << (PAGE_SHIFT-10),
		total_rss << (PAGE_SHIFT-10),
		data << (PAGE_SHIFT-10),
		mm->stack_vm << (PAGE_SHIFT-10), text, lib,
		ptes >> 10,
		pmds >> 10,
		swap << (PAGE_SHIFT-10));
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	*shared = get_mm_counter(mm, MM_FILEPAGES);
	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
								>> PAGE_SHIFT;
	*data = mm->total_vm - mm->shared_vm;
	*resident = *shared + get_mm_counter(mm, MM_ANONPAGES);
	return mm->total_vm;
}

#ifdef CONFIG_NUMA
/*
 * Save get_task_policy() for show_numa_map().
 */
static void hold_task_mempolicy(struct proc_maps_private *priv)
{
	struct task_struct *task = priv->task;

	task_lock(task);
	priv->task_mempolicy = get_task_policy(task);
	mpol_get(priv->task_mempolicy);
	task_unlock(task);
}
static void release_task_mempolicy(struct proc_maps_private *priv)
{
	mpol_put(priv->task_mempolicy);
}
#else
static void hold_task_mempolicy(struct proc_maps_private *priv)
{
}
static void release_task_mempolicy(struct proc_maps_private *priv)
{
}
#endif

/* return 0 if addr belongs to `name'-d ELF */
int find_match_elf_name(struct task_struct *t, char *name, unsigned long addr)
{

	struct vm_area_struct *vma, *gate_vma;
	struct file *file;
	struct mm_struct *mm;
	
	int ret = -ENOENT;
	mm = get_task_mm(t);
	if (!mm)
		return -EINVAL;

	if (!down_read_trylock(&mm->mmap_sem)) {
		pr_alert("down_read_trylock() failed. Can't compare maps info\n");
		mmput(mm);
		return -EINVAL;
	}


	gate_vma = get_gate_vma(mm);
	vma = mm->mmap;
	if (!vma)
		vma = gate_vma;

	while (vma) {
		if ((addr > vma->vm_start) && (addr < vma->vm_end)) {
			file = vma->vm_file;

			if (file) {
				/* if might be very unlikely but still */
				spin_lock(&file->f_path.dentry->d_lock);
				ret = !strstr(file->f_path.dentry->d_name.name,
						name);
				spin_unlock(&file->f_path.dentry->d_lock);
				/* note the inverted strstr() return value */
				if (ret == 0)
					break;
			}
		}
		if (vma->vm_next)
			vma = vma->vm_next;
		else if (vma == gate_vma)
			vma = NULL;
		else
			vma = gate_vma;
	}

	up_read(&current->mm->mmap_sem);
	mmput(mm);
	return ret;
}

static void vma_stop(struct proc_maps_private *priv)
{
	struct mm_struct *mm = priv->mm;

	release_task_mempolicy(priv);
	up_read(&mm->mmap_sem);
	mmput(mm);
}

static struct vm_area_struct *
m_next_vma(struct proc_maps_private *priv, struct vm_area_struct *vma)
{
	if (vma == priv->tail_vma)
		return NULL;
	return vma->vm_next ?: priv->tail_vma;
}

static void m_cache_vma(struct seq_file *m, struct vm_area_struct *vma)
{
	if (m->count < m->size)	/* vma is copied successfully */
		m->version = m_next_vma(m->private, vma) ? vma->vm_start : -1UL;
}

static void *m_start(struct seq_file *m, loff_t *ppos)
{
	struct proc_maps_private *priv = m->private;
	unsigned long last_addr = m->version;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned int pos = *ppos;

	/* See m_cache_vma(). Zero at the start or after lseek. */
	if (last_addr == -1UL)
		return NULL;

	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = priv->mm;
	if (!mm || !atomic_inc_not_zero(&mm->mm_users))
		return NULL;

	down_read(&mm->mmap_sem);
	hold_task_mempolicy(priv);
	priv->tail_vma = get_gate_vma(mm);

	if (last_addr) {
		vma = find_vma(mm, last_addr);
		if (vma && (vma = m_next_vma(priv, vma)))
			return vma;
	}

	m->version = 0;
	if (pos < mm->map_count) {
		for (vma = mm->mmap; pos; pos--) {
			m->version = vma->vm_start;
			vma = vma->vm_next;
		}
		return vma;
	}

	/* we do not bother to update m->version in this case */
	if (pos == mm->map_count && priv->tail_vma)
		return priv->tail_vma;

	vma_stop(priv);
	return NULL;
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct vm_area_struct *next;

	(*pos)++;
	next = m_next_vma(priv, v);
	if (!next)
		vma_stop(priv);
	return next;
}

static void m_stop(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;

	if (!IS_ERR_OR_NULL(v))
		vma_stop(priv);
	if (priv->task) {
		put_task_struct(priv->task);
		priv->task = NULL;
	}
}

static int proc_maps_open(struct inode *inode, struct file *file,
			const struct seq_operations *ops, int psize)
{
	struct proc_maps_private *priv = __seq_open_private(file, ops, psize);

	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(priv->mm)) {
		int err = PTR_ERR(priv->mm);

		seq_release_private(inode, file);
		return err;
	}

	return 0;
}

static int proc_map_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	return seq_release_private(inode, file);
}

static int do_maps_open(struct inode *inode, struct file *file,
			const struct seq_operations *ops)
{
	return proc_maps_open(inode, file, ops,
				sizeof(struct proc_maps_private));
}

static struct task_struct *taskpid_of_stack(struct proc_maps_private *priv,
				struct vm_area_struct *vma, bool is_pid)
{
	struct inode *inode = priv->inode;
	struct task_struct *task;

	rcu_read_lock();
	task = pid_task(proc_pid(inode), PIDTYPE_PID);
	if (task) {
		task = task_of_stack(task, vma, is_pid);
		if (task) {
			get_task_struct(task);
			rcu_read_unlock();
			/*ret = task_pid_nr_ns(task, inode->i_sb->s_fs_info);*/
			return task;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static void
show_map_vma(struct seq_file *m, struct vm_area_struct *vma, int is_pid)
{
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	struct proc_maps_private *priv = m->private;
	vm_flags_t flags = vma->vm_flags;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	unsigned long start, end;
	dev_t dev = 0;
	const char *name = NULL;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
	}

	/* We don't show the stack guard page in /proc/maps */
	start = vma->vm_start;
	end = vma->vm_end;

	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_printf(m, "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu ",
			start,
			end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			pgoff,
			MAJOR(dev), MINOR(dev), ino);

	/*
	 * Print the dentry name for named mappings, and a
	 * special [heap] marker for the heap:
	 */
	if (file) {
		seq_pad(m, ' ');
		seq_path(m, &file->f_path, "\n");
		goto done;
	}

	if (vma->vm_ops && vma->vm_ops->name) {
		name = vma->vm_ops->name(vma);
		if (name)
			goto done;
	}

	name = arch_vma_name(vma);
	if (!name) {
		struct task_struct *t;

		if (!mm) {
			name = "[vdso]";
			goto done;
		}

		if (vma->vm_start <= mm->brk &&
		    vma->vm_end >= mm->start_brk) {
			name = "[heap]";
			goto done;
		}

		t = taskpid_of_stack(priv, vma, is_pid);
		if (t != NULL) {
			/*
			 * Thread stack in /proc/PID/task/TID/maps or
			 * the main process stack.
			 */
			if (!is_pid || (vma->vm_start <= mm->start_stack &&
			    vma->vm_end >= mm->start_stack)) {
				name = "[stack]";
			} else {
				/* Thread stack in /proc/PID/maps */
				seq_pad(m, ' ');
				seq_printf(m, "[tstack: %s: %d]", t->comm,
						t->pid);
			}
			put_task_struct(t);
		}
	}

done:
	if (name) {
		seq_pad(m, ' ');
		seq_puts(m, name);
	}
	seq_putc(m, '\n');
}

static int show_map(struct seq_file *m, void *v, int is_pid)
{
	show_map_vma(m, v, is_pid);
	m_cache_vma(m, v);
	return 0;
}

static int show_pid_map(struct seq_file *m, void *v)
{
	return show_map(m, v, 1);
}

static int show_tid_map(struct seq_file *m, void *v)
{
	return show_map(m, v, 0);
}

static const struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_pid_map
};

static const struct seq_operations proc_tid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_map
};

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_maps_op);
}

static int tid_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_tid_maps_op);
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

const struct file_operations proc_tid_maps_operations = {
	.open		= tid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

#ifdef CONFIG_SHOW_FAULT_TRACE_INFO
void show_pid_maps(struct task_struct *task)
{
	struct task_struct *t;
	struct mm_struct *mm;
	struct vm_area_struct *vma, *gate_vma;
	struct file *file;
	struct pt_regs *regs;
	unsigned long long pgoff = 0;
	unsigned long ino = 0;
	dev_t dev = 0;
	int tpid = 0;
	char path_buf[256];
	const char *vma_name = "";

	pr_alert("-----------------------------------------------------------\n");
	pr_alert("* dump maps on pid (%d - %s)\n", task->pid, task->comm);
	pr_alert("-----------------------------------------------------------\n");

	if (!down_read_trylock(&task->mm->mmap_sem)) {
		pr_alert("down_read_trylock() failed... do not dump pid maps info\n");
		return;
	}

	gate_vma = get_gate_vma(task->mm);

	vma = task->mm->mmap;
	regs = task_pt_regs(task);
	if (!vma)
		vma = gate_vma;

	while (vma) {
		file = vma->vm_file;
		if (file) {
			struct inode *inode = file_inode(file);

			dev = inode->i_sb->s_dev;
			ino = inode->i_ino;
			pgoff = ((unsigned long long)vma->vm_pgoff) << PAGE_SHIFT;
		} else {
			dev = 0;
			ino = 0;
			pgoff = 0;
		}

		if (file) {
			vma_name = (const char *)d_path(&(file->f_path), path_buf, 256);
			if (IS_ERR(vma_name))
				vma_name = "";
		} else {
			vma_name = arch_vma_name(vma);

			mm = vma->vm_mm;
			tpid = 0;
			if (!vma_name) {
				vma_name = "";

				if (mm) {
					if (vma->vm_start <= mm->brk &&
							vma->vm_end >= mm->start_brk) {
						vma_name = "[heap]";
					} else if (vma->vm_start <= mm->start_stack &&
							vma->vm_end >= mm->start_stack) {
						vma_name = "[stack]";
					} else {
						t = task;
						do {
							if (vma->vm_start <= t->user_ssp &&
									vma->vm_end >= t->user_ssp){
								tpid = t->pid;
								vma_name = t->comm;
								break;
							}
						} while_each_thread(task, t);
					}
				} else {
					vma_name = "[vdso]";
				}
			}
			if (tpid) {
				snprintf(path_buf, 256, "[tstack: %s: %d]", vma_name, tpid);
				vma_name = path_buf;
			}
		}

		pr_alert("%08lx-%08lx %c%c%c%c %08llx %02x:%02x %-10lu %s\n",
			vma->vm_start,
			vma->vm_end,
			vma->vm_flags & VM_READ ? 'r' : '-',
			vma->vm_flags & VM_WRITE ? 'w' : '-',
			vma->vm_flags & VM_EXEC ? 'x' : '-',
			vma->vm_flags & VM_MAYSHARE ? 's' : 'p',
			pgoff,
			MAJOR(dev), MINOR(dev), ino, vma_name);

		if (vma->vm_next)
			vma = vma->vm_next;
		else if (vma == gate_vma)
			vma = NULL;
		else
			vma = gate_vma;
	}
	up_read(&task->mm->mmap_sem);
	pr_alert("-----------------------------------------------------------\n\n");
}
#endif

/*
 * Proportional Set Size(PSS): my share of RSS.
 *
 * PSS of a process is the count of pages it has in memory, where each
 * page is divided by the number of processes sharing it.  So if a
 * process has 1000 pages all to itself, and 1000 shared with one other
 * process, its PSS will be 1500.
 *
 * To keep (accumulated) division errors low, we adopt a 64bit
 * fixed-point pss counter to minimize division errors. So (pss >>
 * PSS_SHIFT) would be the real byte count.
 *
 * A shift of 12 before division means (assuming 4K page size):
 * 	- 1M 3-user-pages add up to 8KB errors;
 * 	- supports mapcount up to 2^24, or 16M;
 * 	- supports PSS up to 2^52 bytes, or 4PB.
 */
#define PSS_SHIFT 12

#ifdef CONFIG_PROC_PAGE_MONITOR
static void smaps_account(struct mem_size_stats *mss, struct page *page,
		unsigned long size, bool young, bool dirty)
{
	int mapcount;

	if (PageAnon(page))
		mss->anonymous += size;

	mss->resident += size;
	/* Accumulate the size in pages that have been accessed. */
	if (young || PageReferenced(page))
		mss->referenced += size;
	mapcount = page_mapcount(page);
	if (mapcount >= 2) {
		u64 pss_delta;

		if (dirty || PageDirty(page))
			mss->shared_dirty += size;
		else
			mss->shared_clean += size;
		pss_delta = (u64)size << PSS_SHIFT;
		do_div(pss_delta, mapcount);
		mss->pss += pss_delta;
	} else {
		if (dirty || PageDirty(page))
			mss->private_dirty += size;
		else
			mss->private_clean += size;
		mss->pss += (u64)size << PSS_SHIFT;
	}
}

static void smaps_pte_entry(pte_t *pte, unsigned long addr,
		struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;
	struct page *page = NULL;

	if (pte_present(*pte)) {
		page = vm_normal_page(vma, addr, *pte);
	} else if (is_swap_pte(*pte)) {
		swp_entry_t swpent = pte_to_swp_entry(*pte);

		if (!non_swap_entry(swpent)) {
			int mapcount;

			mss->swap += PAGE_SIZE;
			mapcount = swp_swapcount(swpent);
			if (mapcount >= 2) {
				u64 pss_delta = (u64)PAGE_SIZE << PSS_SHIFT;

				do_div(pss_delta, mapcount);
				mss->swap_pss += pss_delta;
			} else {
				mss->swap_pss += (u64)PAGE_SIZE << PSS_SHIFT;
			}
		} else if (is_migration_entry(swpent))
			page = migration_entry_to_page(swpent);
	}

	if (!page)
		return;
	smaps_account(mss, page, PAGE_SIZE, pte_young(*pte), pte_dirty(*pte));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void smaps_pmd_entry(pmd_t *pmd, unsigned long addr,
		struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;
	struct page *page;

	/* FOLL_DUMP will return -EFAULT on huge zero page */
	page = follow_trans_huge_pmd(vma, addr, pmd, FOLL_DUMP);
	if (IS_ERR_OR_NULL(page))
		return;
	mss->anonymous_thp += HPAGE_PMD_SIZE;
	smaps_account(mss, page, HPAGE_PMD_SIZE,
			pmd_young(*pmd), pmd_dirty(*pmd));
}
#else
static void smaps_pmd_entry(pmd_t *pmd, unsigned long addr,
		struct mm_walk *walk)
{
}
#endif

#ifdef CONFIG_VD_MEMINFO
int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
		    struct mm_walk *walk)
#else
static int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			   struct mm_walk *walk)
#endif
{
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;

	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
		smaps_pmd_entry(pmd, addr, walk);
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	/*
	 * The mmap_sem held all the way back in m_start() is what
	 * keeps khugepaged out of here and from collapsing things
	 * in here.
	 */
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE)
		smaps_pte_entry(pte, addr, walk);
	pte_unmap_unlock(pte - 1, ptl);
	if (!preempt_count())
		cond_resched();

	return 0;
}

static void show_smap_vma_flags(struct seq_file *m, struct vm_area_struct *vma)
{
	/*
	 * Don't forget to update Documentation/ on changes.
	 */
	static const char mnemonics[BITS_PER_LONG][2] = {
		/*
		 * In case if we meet a flag we don't know about.
		 */
		[0 ... (BITS_PER_LONG-1)] = "??",

		[ilog2(VM_READ)]	= "rd",
		[ilog2(VM_WRITE)]	= "wr",
		[ilog2(VM_EXEC)]	= "ex",
		[ilog2(VM_SHARED)]	= "sh",
		[ilog2(VM_MAYREAD)]	= "mr",
		[ilog2(VM_MAYWRITE)]	= "mw",
		[ilog2(VM_MAYEXEC)]	= "me",
		[ilog2(VM_MAYSHARE)]	= "ms",
		[ilog2(VM_GROWSDOWN)]	= "gd",
		[ilog2(VM_PFNMAP)]	= "pf",
		[ilog2(VM_DENYWRITE)]	= "dw",
#ifdef CONFIG_X86_INTEL_MPX
		[ilog2(VM_MPX)]		= "mp",
#endif
		[ilog2(VM_LOCKED)]	= "lo",
		[ilog2(VM_IO)]		= "io",
		[ilog2(VM_SEQ_READ)]	= "sr",
		[ilog2(VM_RAND_READ)]	= "rr",
		[ilog2(VM_DONTCOPY)]	= "dc",
		[ilog2(VM_DONTEXPAND)]	= "de",
		[ilog2(VM_ACCOUNT)]	= "ac",
		[ilog2(VM_NORESERVE)]	= "nr",
		[ilog2(VM_HUGETLB)]	= "ht",
		[ilog2(VM_ARCH_1)]	= "ar",
		[ilog2(VM_DONTDUMP)]	= "dd",
#ifdef CONFIG_MEM_SOFT_DIRTY
		[ilog2(VM_SOFTDIRTY)]	= "sd",
#endif
		[ilog2(VM_MIXEDMAP)]	= "mm",
		[ilog2(VM_HUGEPAGE)]	= "hg",
		[ilog2(VM_NOHUGEPAGE)]	= "nh",
		[ilog2(VM_MERGEABLE)]	= "mg",
	};
	size_t i;

	seq_puts(m, "VmFlags: ");
	for (i = 0; i < BITS_PER_LONG; i++) {
		if (vma->vm_flags & (1UL << i)) {
			seq_printf(m, "%c%c ",
				   mnemonics[i][0], mnemonics[i][1]);
		}
	}
	seq_putc(m, '\n');
}

static int show_smap(struct seq_file *m, void *v, int is_pid)
{
	struct vm_area_struct *vma = v;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pmd_entry = smaps_pte_range,
		.mm = vma->vm_mm,
		.private = &mss,
	};

	memset(&mss, 0, sizeof(mss));
	/* mmap_sem is held in m_start */
	walk_page_vma(vma, &smaps_walk);

	show_map_vma(m, vma, is_pid);

	seq_printf(m,
		   "Size:           %8lu kB\n"
		   "Rss:            %8lu kB\n"
		   "Pss:            %8lu kB\n"
		   "Shared_Clean:   %8lu kB\n"
		   "Shared_Dirty:   %8lu kB\n"
		   "Private_Clean:  %8lu kB\n"
		   "Private_Dirty:  %8lu kB\n"
		   "Referenced:     %8lu kB\n"
		   "Anonymous:      %8lu kB\n"
		   "AnonHugePages:  %8lu kB\n"
		   "Swap:           %8lu kB\n"
		   "SwapPss:        %8lu kB\n"
		   "KernelPageSize: %8lu kB\n"
		   "MMUPageSize:    %8lu kB\n"
		   "Locked:         %8lu kB\n",
		   (vma->vm_end - vma->vm_start) >> 10,
		   mss.resident >> 10,
		   (unsigned long)(mss.pss >> (10 + PSS_SHIFT)),
		   mss.shared_clean  >> 10,
		   mss.shared_dirty  >> 10,
		   mss.private_clean >> 10,
		   mss.private_dirty >> 10,
		   mss.referenced >> 10,
		   mss.anonymous >> 10,
		   mss.anonymous_thp >> 10,
		   mss.swap >> 10,
		   (unsigned long)(mss.swap_pss >> (10 + PSS_SHIFT)),
		   vma_kernel_pagesize(vma) >> 10,
		   vma_mmu_pagesize(vma) >> 10,
		   (vma->vm_flags & VM_LOCKED) ?
			(unsigned long)(mss.pss >> (10 + PSS_SHIFT)) : 0);

	show_smap_vma_flags(m, vma);
	m_cache_vma(m, vma);
	return 0;
}

static int show_pid_smap(struct seq_file *m, void *v)
{
	return show_smap(m, v, 1);
}

static int show_tid_smap(struct seq_file *m, void *v)
{
	return show_smap(m, v, 0);
}

static const struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_pid_smap
};

static const struct seq_operations proc_tid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_smap
};

static int pid_smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_smaps_op);
}

static int tid_smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_tid_smaps_op);
}

const struct file_operations proc_pid_smaps_operations = {
	.open		= pid_smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

const struct file_operations proc_tid_smaps_operations = {
	.open		= tid_smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

/*
 * We do not want to have constant page-shift bits sitting in
 * pagemap entries and are about to reuse them some time soon.
 *
 * Here's the "migration strategy":
 * 1. when the system boots these bits remain what they are,
 *    but a warning about future change is printed in log;
 * 2. once anyone clears soft-dirty bits via clear_refs file,
 *    these flag is set to denote, that user is aware of the
 *    new API and those page-shift bits change their meaning.
 *    The respective warning is printed in dmesg;
 * 3. In a couple of releases we will remove all the mentions
 *    of page-shift in pagemap entries.
 */

static bool soft_dirty_cleared __read_mostly;

enum clear_refs_types {
	CLEAR_REFS_ALL = 1,
	CLEAR_REFS_ANON,
	CLEAR_REFS_MAPPED,
	CLEAR_REFS_SOFT_DIRTY,
	CLEAR_REFS_MM_HIWATER_RSS,
	CLEAR_REFS_LAST,
};

struct clear_refs_private {
	enum clear_refs_types type;
};

#ifdef CONFIG_MEM_SOFT_DIRTY
static inline void clear_soft_dirty(struct vm_area_struct *vma,
		unsigned long addr, pte_t *pte)
{
	/*
	 * The soft-dirty tracker uses #PF-s to catch writes
	 * to pages, so write-protect the pte as well. See the
	 * Documentation/vm/soft-dirty.txt for full description
	 * of how soft-dirty works.
	 */
	pte_t ptent = *pte;

	if (pte_present(ptent)) {
		ptent = pte_wrprotect(ptent);
		ptent = pte_clear_flags(ptent, _PAGE_SOFT_DIRTY);
	} else if (is_swap_pte(ptent)) {
		ptent = pte_swp_clear_soft_dirty(ptent);
	}

	set_pte_at(vma->vm_mm, addr, pte, ptent);
}

static inline void clear_soft_dirty_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	pmd = pmd_wrprotect(pmd);
	pmd = pmd_clear_flags(pmd, _PAGE_SOFT_DIRTY);

	if (vma->vm_flags & VM_SOFTDIRTY)
		vma->vm_flags &= ~VM_SOFTDIRTY;

	set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
}

#else

static inline void clear_soft_dirty(struct vm_area_struct *vma,
		unsigned long addr, pte_t *pte)
{
}

static inline void clear_soft_dirty_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
}
#endif


#ifdef CONFIG_MEMSTAT_INFO

struct smap_mem {
	unsigned long total;
	unsigned long vmag[VMAG_CNT];
};

#define K(x) ((x) << (PAGE_SHIFT-10))

static void show_memstat_vd_memps(struct seq_file *m,
	 struct vm_area_struct *vma, int idx, struct mem_size_stats mss,
	 struct task_struct *task)
{

	unsigned long  dev_rss = 0, dev_pss = 0;
	bool is_name = false;
	const char *name = NULL;
	char path_buf[32];
	char *buf = NULL;
	struct task_struct *t;
	unsigned long start, end;

	start = vma->vm_start;
	end = vma->vm_end;
	if (vma->vm_ops && vma->vm_ops->name) {
		name = vma->vm_ops->name(vma);
		goto done;
	}
	name = arch_vma_name(vma);
	if (name)
		goto done;

	if (vma->vm_file) {
		buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf)
			return;

		name = d_path(&(vma->vm_file->f_path), buf, PAGE_SIZE);
		if (!IS_ERR(name)) {
			if (!strncmp(name, "/dev/", 5)) {
				dev_rss = mss.resident >> 10;
				dev_pss = (unsigned long)
					(mss.pss >> (10 + PSS_SHIFT));
			}
			goto done;
		}
	}
	if (vma->vm_mm) {
		if (vma->vm_start <= vma->vm_mm->brk &&
			vma->vm_end >= vma->vm_mm->start_brk) {
			name = "heap";
		} else if (vma->vm_start <= vma->vm_mm->start_stack &&
			vma->vm_end >= vma->vm_mm->start_stack) {
			name = "stack";
		} else {
			rcu_read_lock();
			get_task_struct(task);
			for_each_thread(task, t) {
				get_task_struct(t);
				if (vma->vm_start <= t->user_ssp &&
					vma->vm_end >= t->user_ssp){
					sprintf(path_buf, "tstack:%s:%d",
						t->comm, t->pid);
					name = path_buf;
					put_task_struct(t);
					break;
				}
				put_task_struct(t);
			}
			put_task_struct(task);
			rcu_read_unlock();
	/*this is an anonymous mapping as obtained via mmap*/
			if (!name)
				name = "anon_mmap";
		}
	} else {
	/*The virtual dynamically linked shared object.*/
		name = "vdso";
	}

done:
	if (!vma->vm_prev) {
		seq_printf(m, "MaxRSS: %lu\n",
			K(get_max_total_rss(vma->vm_mm)));
	}
	if (idx == VMAG_CODE || idx == VMAG_LIBCODE)
		seq_printf(m, "%8lx - %8lx %8lu %8d %8lu %8d %8lu %8lu %8lu"
		" %8lu %8lu [%s]\n", start, end, mss.shared_clean  >> 10, 0,
		mss.private_clean >> 10, 0, mss.resident >> 10,
		(unsigned long)(mss.pss >> (10 + PSS_SHIFT)), dev_rss, dev_pss,
		 mss.swap >> 10, name);
	else
		seq_printf(m, "%8lx - %8lx %8d %8lu %8d %8lu %8lu %8lu %8lu"
			" %8lu %8lu [%s]\n", start, end, 0,
			(mss.shared_clean+mss.shared_dirty) >> 10, 0,
			(mss.private_clean+mss.private_dirty) >> 10,
			mss.resident >> 10,
			(unsigned long)(mss.pss >> (10 + PSS_SHIFT)),
			dev_rss, dev_pss, mss.swap >> 10, name);

	kfree(buf);

}

static int show_memstat(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;
	struct vm_area_struct *vma = v;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pmd_entry = smaps_pte_range,
		.private = &mss,
	};

	int idx = 0;
	static struct smap_mem vmem;
	static struct smap_mem rss;
	static struct smap_mem pss;
	static struct smap_mem shared;
	static struct smap_mem private;
	static struct smap_mem swap;

	if (!vma || !vma->vm_mm)
		return 0;

	smaps_walk.mm = vma->vm_mm;

	memset(&mss, 0, sizeof(mss));
	if (vma->vm_mm && !is_vm_hugetlb_page(vma))
		walk_page_range(vma->vm_start, vma->vm_end, &smaps_walk);

/*	show_map_vma(m, vma); */

	vmem.total    += (vma->vm_end - vma->vm_start) >> 10;
	rss.total     += mss.resident >> 10;
	pss.total     += (unsigned long)(mss.pss >> (10 + PSS_SHIFT));
	shared.total  += (mss.shared_clean + mss.shared_dirty) >> 10;
	private.total += (mss.private_clean + mss.private_dirty) >> 10;
	swap.total    += mss.swap >> 10;

	idx = get_group_idx(vma);
	vmem.vmag[idx]    += (vma->vm_end - vma->vm_start) >> 10;
	rss.vmag[idx]     += mss.resident >> 10;
	pss.vmag[idx]     += (unsigned long)(mss.pss >> (10 + PSS_SHIFT));
	shared.vmag[idx]  += (mss.shared_clean + mss.shared_dirty) >> 10;
	private.vmag[idx] += (mss.private_clean + mss.private_dirty) >> 10;
	swap.vmag[idx]    += mss.swap >> 10;

	/*Following code will be executed only for vd_memps tool
	* and provide with per VM memory information
	* for the process in following format:
	* start - end SCode Sdata PCode Pdata RSS PSS DeviceRSS DevicePSS Name
	*/
	if (!strncmp(current->comm, "vd_memps",9))
		show_memstat_vd_memps(m, vma, idx, mss, task);

	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task->mm)) ? vma->vm_start : 0;

	if (!vma->vm_next) { /* latest vma sequence */
		seq_printf(m,
				" =========================================================================\n"
				"                VMSize    Rss  Rss_max  Shared  Private    Pss   Swap\n"
				"  Process Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"     Code Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"     Data Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"  LibCode Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"  LibData Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				" Heap-BRK Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"    Stack Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
				"    Other Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n",
				vmem.total, rss.total,
				K(get_max_total_rss(vma->vm_mm)),
				shared.total, private.total, pss.total, swap.total,
				vmem.vmag[0], rss.vmag[0],
				K(get_max_rss(vma->vm_mm, 0)), shared.vmag[0],
				private.vmag[0], pss.vmag[0], swap.vmag[0],
				vmem.vmag[1], rss.vmag[1],
				K(get_max_rss(vma->vm_mm, 1)), shared.vmag[1],
				private.vmag[1], pss.vmag[1], swap.vmag[1],
				vmem.vmag[2], rss.vmag[2],
				K(get_max_rss(vma->vm_mm, 2)), shared.vmag[2],
				private.vmag[2], pss.vmag[2], swap.vmag[2],
				vmem.vmag[3], rss.vmag[3],
				K(get_max_rss(vma->vm_mm, 3)), shared.vmag[3],
				private.vmag[3], pss.vmag[3], swap.vmag[3],
				vmem.vmag[4], rss.vmag[4],
				K(get_max_rss(vma->vm_mm, 4)), shared.vmag[4],
				private.vmag[4], pss.vmag[4], swap.vmag[4],
				vmem.vmag[5], rss.vmag[5],
				K(get_max_rss(vma->vm_mm, 5)), shared.vmag[5],
				private.vmag[5], pss.vmag[5], swap.vmag[5],
				vmem.vmag[6], rss.vmag[6],
				K(get_max_rss(vma->vm_mm, 6)), shared.vmag[6],
				private.vmag[6], pss.vmag[6], swap.vmag[6]
			);
		memset(&vmem    , 0, sizeof(struct smap_mem));
		memset(&rss     , 0, sizeof(struct smap_mem));
		memset(&shared  , 0, sizeof(struct smap_mem));
		memset(&private , 0, sizeof(struct smap_mem));
		memset(&pss     , 0, sizeof(struct smap_mem));
		memset(&swap    , 0, sizeof(struct smap_mem));
	}
	return 0;
}

static const struct seq_operations proc_pid_memstat_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_memstat
};

static int memstat_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_memstat_op);
}

const struct file_operations proc_pid_memstat_operations = {
	.open	= memstat_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release = seq_release_private,
};
#endif /* CONFIG_MEMSTAT_INFO ends */
static int clear_refs_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	struct clear_refs_private *cp = walk->private;
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;

	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
		if (cp->type == CLEAR_REFS_SOFT_DIRTY) {
			clear_soft_dirty_pmd(vma, addr, pmd);
			goto out;
		}

		page = pmd_page(*pmd);

		/* Clear accessed and referenced bits. */
		pmdp_test_and_clear_young(vma, addr, pmd);
		ClearPageReferenced(page);
out:
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;

		if (cp->type == CLEAR_REFS_SOFT_DIRTY) {
			clear_soft_dirty(vma, addr, pte);
			continue;
		}

		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/* Clear accessed and referenced bits. */
		ptep_test_and_clear_young(vma, addr, pte);
		ClearPageReferenced(page);
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	return 0;
}

static int clear_refs_test_walk(unsigned long start, unsigned long end,
				struct mm_walk *walk)
{
	struct clear_refs_private *cp = walk->private;
	struct vm_area_struct *vma = walk->vma;

	if (vma->vm_flags & VM_PFNMAP)
		return 1;

	/*
	 * Writing 1 to /proc/pid/clear_refs affects all pages.
	 * Writing 2 to /proc/pid/clear_refs only affects anonymous pages.
	 * Writing 3 to /proc/pid/clear_refs only affects file mapped pages.
	 * Writing 4 to /proc/pid/clear_refs affects all pages.
	 */
	if (cp->type == CLEAR_REFS_ANON && vma->vm_file)
		return 1;
	if (cp->type == CLEAR_REFS_MAPPED && !vma->vm_file)
		return 1;
	return 0;
}

static ssize_t clear_refs_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	enum clear_refs_types type;
	int itype;
	int rv;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	rv = kstrtoint(strstrip(buffer), 10, &itype);
	if (rv < 0)
		return rv;
	type = (enum clear_refs_types)itype;
	if (type < CLEAR_REFS_ALL || type >= CLEAR_REFS_LAST)
		return -EINVAL;

	if (type == CLEAR_REFS_SOFT_DIRTY) {
		soft_dirty_cleared = true;
		pr_warn_once("The pagemap bits 55-60 has changed their meaning!"
			     " See the linux/Documentation/vm/pagemap.txt for "
			     "details.\n");
	}

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;
	mm = get_task_mm(task);
	if (mm) {
		struct clear_refs_private cp = {
			.type = type,
		};
		struct mm_walk clear_refs_walk = {
			.pmd_entry = clear_refs_pte_range,
			.test_walk = clear_refs_test_walk,
			.mm = mm,
			.private = &cp,
		};

		if (type == CLEAR_REFS_MM_HIWATER_RSS) {
			/*
			 * Writing 5 to /proc/pid/clear_refs resets the peak
			 * resident set size to this mm's current rss value.
			 */
			down_write(&mm->mmap_sem);
			reset_mm_hiwater_rss(mm);
			up_write(&mm->mmap_sem);
			goto out_mm;
		}

		down_read(&mm->mmap_sem);
		if (type == CLEAR_REFS_SOFT_DIRTY) {
			for (vma = mm->mmap; vma; vma = vma->vm_next) {
				if (!(vma->vm_flags & VM_SOFTDIRTY))
					continue;
				up_read(&mm->mmap_sem);
				down_write(&mm->mmap_sem);
				for (vma = mm->mmap; vma; vma = vma->vm_next) {
					vma->vm_flags &= ~VM_SOFTDIRTY;
					vma_set_page_prot(vma);
				}
				downgrade_write(&mm->mmap_sem);
				break;
			}
			mmu_notifier_invalidate_range_start(mm, 0, -1);
		}
		walk_page_range(0, ~0UL, &clear_refs_walk);
		if (type == CLEAR_REFS_SOFT_DIRTY)
			mmu_notifier_invalidate_range_end(mm, 0, -1);
		flush_tlb_mm(mm);
		up_read(&mm->mmap_sem);
out_mm:
		mmput(mm);
	}
	put_task_struct(task);

	return count;
}

const struct file_operations proc_clear_refs_operations = {
	.write		= clear_refs_write,
	.llseek		= noop_llseek,
};

typedef struct {
	u64 pme;
} pagemap_entry_t;

struct pagemapread {
	int pos, len;		/* units: PM_ENTRY_BYTES, not bytes */
	pagemap_entry_t *buffer;
	bool v2;
};

#define PAGEMAP_WALK_SIZE	(PMD_SIZE)
#define PAGEMAP_WALK_MASK	(PMD_MASK)

#define PM_ENTRY_BYTES      sizeof(pagemap_entry_t)
#define PM_STATUS_BITS      3
#define PM_STATUS_OFFSET    (64 - PM_STATUS_BITS)
#define PM_STATUS_MASK      (((1LL << PM_STATUS_BITS) - 1) << PM_STATUS_OFFSET)
#define PM_STATUS(nr)       (((nr) << PM_STATUS_OFFSET) & PM_STATUS_MASK)
#define PM_PSHIFT_BITS      6
#define PM_PSHIFT_OFFSET    (PM_STATUS_OFFSET - PM_PSHIFT_BITS)
#define PM_PSHIFT_MASK      (((1LL << PM_PSHIFT_BITS) - 1) << PM_PSHIFT_OFFSET)
#define __PM_PSHIFT(x)      (((u64) (x) << PM_PSHIFT_OFFSET) & PM_PSHIFT_MASK)
#define PM_PFRAME_MASK      ((1LL << PM_PSHIFT_OFFSET) - 1)
#define PM_PFRAME(x)        ((x) & PM_PFRAME_MASK)
/* in "new" pagemap pshift bits are occupied with more status bits */
#define PM_STATUS2(v2, x)   (__PM_PSHIFT(v2 ? x : PAGE_SHIFT))

#define __PM_SOFT_DIRTY      (1LL)
#define PM_PRESENT          PM_STATUS(4LL)
#define PM_SWAP             PM_STATUS(2LL)
#define PM_FILE             PM_STATUS(1LL)
#define PM_NOT_PRESENT(v2)  PM_STATUS2(v2, 0)
#define PM_END_OF_BUFFER    1

static inline pagemap_entry_t make_pme(u64 val)
{
	return (pagemap_entry_t) { .pme = val };
}

static int add_to_pagemap(unsigned long addr, pagemap_entry_t *pme,
			  struct pagemapread *pm)
{
	pm->buffer[pm->pos++] = *pme;
	if (pm->pos >= pm->len)
		return PM_END_OF_BUFFER;
	return 0;
}

static int pagemap_pte_hole(unsigned long start, unsigned long end,
				struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	unsigned long addr = start;
	int err = 0;

	while (addr < end) {
		struct vm_area_struct *vma = find_vma(walk->mm, addr);
		pagemap_entry_t pme = make_pme(PM_NOT_PRESENT(pm->v2));
		/* End of address space hole, which we mark as non-present. */
		unsigned long hole_end;

		if (vma)
			hole_end = min(end, vma->vm_start);
		else
			hole_end = end;

		for (; addr < hole_end; addr += PAGE_SIZE) {
			err = add_to_pagemap(addr, &pme, pm);
			if (err)
				goto out;
		}

		if (!vma)
			break;

		/* Addresses in the VMA. */
		if (vma->vm_flags & VM_SOFTDIRTY)
			pme.pme |= PM_STATUS2(pm->v2, __PM_SOFT_DIRTY);
		for (; addr < min(end, vma->vm_end); addr += PAGE_SIZE) {
			err = add_to_pagemap(addr, &pme, pm);
			if (err)
				goto out;
		}
	}
out:
	return err;
}

static void pte_to_pagemap_entry(pagemap_entry_t *pme, struct pagemapread *pm,
		struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	u64 frame, flags;
	struct page *page = NULL;
	int flags2 = 0;

	if (pte_present(pte)) {
		frame = pte_pfn(pte);
		flags = PM_PRESENT;
		page = vm_normal_page(vma, addr, pte);
		if (pte_soft_dirty(pte))
			flags2 |= __PM_SOFT_DIRTY;
	} else if (is_swap_pte(pte)) {
		swp_entry_t entry;
		if (pte_swp_soft_dirty(pte))
			flags2 |= __PM_SOFT_DIRTY;
		entry = pte_to_swp_entry(pte);
		frame = swp_type(entry) |
			(swp_offset(entry) << MAX_SWAPFILES_SHIFT);
		flags = PM_SWAP;
		if (is_migration_entry(entry))
			page = migration_entry_to_page(entry);
	} else {
		if (vma->vm_flags & VM_SOFTDIRTY)
			flags2 |= __PM_SOFT_DIRTY;
		*pme = make_pme(PM_NOT_PRESENT(pm->v2) | PM_STATUS2(pm->v2, flags2));
		return;
	}

	if (page && !PageAnon(page))
		flags |= PM_FILE;
	if ((vma->vm_flags & VM_SOFTDIRTY))
		flags2 |= __PM_SOFT_DIRTY;

	*pme = make_pme(PM_PFRAME(frame) | PM_STATUS2(pm->v2, flags2) | flags);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void thp_pmd_to_pagemap_entry(pagemap_entry_t *pme, struct pagemapread *pm,
		pmd_t pmd, int offset, int pmd_flags2)
{
	/*
	 * Currently pmd for thp is always present because thp can not be
	 * swapped-out, migrated, or HWPOISONed (split in such cases instead.)
	 * This if-check is just to prepare for future implementation.
	 */
	if (pmd_present(pmd))
		*pme = make_pme(PM_PFRAME(pmd_pfn(pmd) + offset)
				| PM_STATUS2(pm->v2, pmd_flags2) | PM_PRESENT);
	else
		*pme = make_pme(PM_NOT_PRESENT(pm->v2) | PM_STATUS2(pm->v2, pmd_flags2));
}
#else
static inline void thp_pmd_to_pagemap_entry(pagemap_entry_t *pme, struct pagemapread *pm,
		pmd_t pmd, int offset, int pmd_flags2)
{
}
#endif

static int pagemap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			     struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct pagemapread *pm = walk->private;
	spinlock_t *ptl;
	pte_t *pte, *orig_pte;
	int err = 0;

	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
		int pmd_flags2;

		if ((vma->vm_flags & VM_SOFTDIRTY) || pmd_soft_dirty(*pmd))
			pmd_flags2 = __PM_SOFT_DIRTY;
		else
			pmd_flags2 = 0;

		for (; addr != end; addr += PAGE_SIZE) {
			unsigned long offset;
			pagemap_entry_t pme;

			offset = (addr & ~PAGEMAP_WALK_MASK) >>
					PAGE_SHIFT;
			thp_pmd_to_pagemap_entry(&pme, pm, *pmd, offset, pmd_flags2);
			err = add_to_pagemap(addr, &pme, pm);
			if (err)
				break;
		}
		spin_unlock(ptl);
		return err;
	}

	if (pmd_trans_unstable(pmd))
		return 0;

	/*
	 * We can assume that @vma always points to a valid one and @end never
	 * goes beyond vma->vm_end.
	 */
	orig_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	for (; addr < end; pte++, addr += PAGE_SIZE) {
		pagemap_entry_t pme;

		pte_to_pagemap_entry(&pme, pm, vma, addr, *pte);
		err = add_to_pagemap(addr, &pme, pm);
		if (err)
			break;
	}
	pte_unmap_unlock(orig_pte, ptl);

	cond_resched();

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static void huge_pte_to_pagemap_entry(pagemap_entry_t *pme, struct pagemapread *pm,
					pte_t pte, int offset, int flags2)
{
	if (pte_present(pte))
		*pme = make_pme(PM_PFRAME(pte_pfn(pte) + offset)	|
				PM_STATUS2(pm->v2, flags2)		|
				PM_PRESENT);
	else
		*pme = make_pme(PM_NOT_PRESENT(pm->v2)			|
				PM_STATUS2(pm->v2, flags2));
}

/* This function walks within one hugetlb entry in the single call */
static int pagemap_hugetlb_range(pte_t *pte, unsigned long hmask,
				 unsigned long addr, unsigned long end,
				 struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	struct vm_area_struct *vma = walk->vma;
	int err = 0;
	int flags2;
	pagemap_entry_t pme;

	if (vma->vm_flags & VM_SOFTDIRTY)
		flags2 = __PM_SOFT_DIRTY;
	else
		flags2 = 0;

	for (; addr != end; addr += PAGE_SIZE) {
		int offset = (addr & ~hmask) >> PAGE_SHIFT;
		huge_pte_to_pagemap_entry(&pme, pm, *pte, offset, flags2);
		err = add_to_pagemap(addr, &pme, pm);
		if (err)
			return err;
	}

	cond_resched();

	return err;
}
#endif /* HUGETLB_PAGE */

/*
 * /proc/pid/pagemap - an array mapping virtual pages to pfns
 *
 * For each page in the address space, this file contains one 64-bit entry
 * consisting of the following:
 *
 * Bits 0-54  page frame number (PFN) if present
 * Bits 0-4   swap type if swapped
 * Bits 5-54  swap offset if swapped
 * Bits 55-60 page shift (page size = 1<<page shift)
 * Bit  61    page is file-page or shared-anon
 * Bit  62    page swapped
 * Bit  63    page present
 *
 * If the page is not present but in swap, then the PFN contains an
 * encoding of the swap file number and the page's offset into the
 * swap. Unmapped pages return a null PFN. This allows determining
 * precisely which pages are mapped (or in swap) and comparing mapped
 * pages between processes.
 *
 * Efficient users of this interface will use /proc/pid/maps to
 * determine which areas of memory are actually mapped and llseek to
 * skip over unmapped regions.
 */
static ssize_t pagemap_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file_inode(file));
	struct mm_struct *mm;
	struct pagemapread pm;
	int ret = -ESRCH;
	struct mm_walk pagemap_walk = {};
	unsigned long src;
	unsigned long svpfn;
	unsigned long start_vaddr;
	unsigned long end_vaddr;
	int copied = 0;

	if (!task)
		goto out;

	ret = -EINVAL;
	/* file position must be aligned */
	if ((*ppos % PM_ENTRY_BYTES) || (count % PM_ENTRY_BYTES))
		goto out_task;

	ret = 0;
	if (!count)
		goto out_task;

	pm.v2 = soft_dirty_cleared;
	pm.len = (PAGEMAP_WALK_SIZE >> PAGE_SHIFT);
	pm.buffer = kmalloc(pm.len * PM_ENTRY_BYTES, GFP_TEMPORARY);
	ret = -ENOMEM;
	if (!pm.buffer)
		goto out_task;

	mm = mm_access(task, PTRACE_MODE_READ);
	ret = PTR_ERR(mm);
	if (!mm || IS_ERR(mm))
		goto out_free;

	pagemap_walk.pmd_entry = pagemap_pte_range;
	pagemap_walk.pte_hole = pagemap_pte_hole;
#ifdef CONFIG_HUGETLB_PAGE
	pagemap_walk.hugetlb_entry = pagemap_hugetlb_range;
#endif
	pagemap_walk.mm = mm;
	pagemap_walk.private = &pm;

	src = *ppos;
	svpfn = src / PM_ENTRY_BYTES;
	start_vaddr = svpfn << PAGE_SHIFT;
	end_vaddr = TASK_SIZE_OF(task);

	/* watch out for wraparound */
	if (svpfn > TASK_SIZE_OF(task) >> PAGE_SHIFT)
		start_vaddr = end_vaddr;

	/*
	 * The odds are that this will stop walking way
	 * before end_vaddr, because the length of the
	 * user buffer is tracked in "pm", and the walk
	 * will stop when we hit the end of the buffer.
	 */
	ret = 0;
	while (count && (start_vaddr < end_vaddr)) {
		int len;
		unsigned long end;

		pm.pos = 0;
		end = (start_vaddr + PAGEMAP_WALK_SIZE) & PAGEMAP_WALK_MASK;
		/* overflow ? */
		if (end < start_vaddr || end > end_vaddr)
			end = end_vaddr;
		down_read(&mm->mmap_sem);
		ret = walk_page_range(start_vaddr, end, &pagemap_walk);
		up_read(&mm->mmap_sem);
		start_vaddr = end;

		len = min(count, PM_ENTRY_BYTES * pm.pos);
		if (copy_to_user(buf, pm.buffer, len)) {
			ret = -EFAULT;
			goto out_mm;
		}
		copied += len;
		buf += len;
		count -= len;
	}
	*ppos += copied;
	if (!ret || ret == PM_END_OF_BUFFER)
		ret = copied;

out_mm:
	mmput(mm);
out_free:
	kfree(pm.buffer);
out_task:
	put_task_struct(task);
out:
	return ret;
}

static int pagemap_open(struct inode *inode, struct file *file)
{
	/* do not disclose physical addresses: attack vector */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	pr_warn_once("Bits 55-60 of /proc/PID/pagemap entries are about "
			"to stop being page-shift some time soon. See the "
			"linux/Documentation/vm/pagemap.txt for details.\n");
	return 0;
}

const struct file_operations proc_pagemap_operations = {
	.llseek		= mem_lseek, /* borrow this */
	.read		= pagemap_read,
	.open		= pagemap_open,
};
#endif /* CONFIG_PROC_PAGE_MONITOR */


/*
 * Forced PID reclaim flags
 */
enum reclaim_type {
	RECLAIM_FILE = 1,
	RECLAIM_ANON,
	RECLAIM_ALL,
};

enum reclaim_ret_type {
	RECLAIM_RET_OK	= 0,
	RECLAIM_RET_SKIP_ON_RQ,
	RECLAIM_RET_SKIP_SIGNAL_PENDDING,
	RECLAIM_RET_OVER_MAX_COUNT,
	RECLAIM_RET_KSWAPD_RUN,
};

struct reclaim_stat {
	struct vm_area_struct *vma;
	unsigned long vm_start;
	int reclaim_max_page;
	int reclaimed_page;
	int ret;
};

struct usr_reclaim_stat {
	int type;
	unsigned long vm_start;
    int reclaim_max_page;
    int reclaimed_page;
	int ret;
};

#define INFINITY	0x0FFFFFFF

static int reclaim_pte_range(pmd_t *pmd,
			unsigned long addr,
			unsigned long end,
			struct mm_walk *walk)
{
	struct mm_struct *mm = walk->mm;
	struct reclaim_stat *rs = walk->private;
	struct vm_area_struct *vma = rs->vma;
	pte_t *orig_pte, *pte, ptent;
	spinlock_t *ptl;
	struct page *page;
	LIST_HEAD(page_list);
	int isolated = 0;

	split_huge_page_pmd(vma, addr, pmd);
	if (pmd_trans_unstable(pmd))
		return 0;

	orig_pte = pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;

		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/* Can't reclaim shared pages */
		if (page_mapcount(page) != 1)
			continue;

		if (PageTransCompound(page)) {
			get_page(page);
			if (!trylock_page(page)) {
				put_page(page);
				goto out;
			}
			pte_unmap_unlock(orig_pte, ptl);

			if (split_huge_page(page)) {
				unlock_page(page);
				put_page(page);
				orig_pte = pte_offset_map_lock(mm, pmd,
								addr, &ptl);
				goto out;
			}
			put_page(page);
			unlock_page(page);
			pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
			pte--;
			addr -= PAGE_SIZE;
			continue;
		}

		VM_BUG_ON_PAGE(PageTransCompound(page), page);

		/*
		 * Try to remove that page from its LRU list and put it in
		 * forced reclaim list.
		 */
		if (isolate_lru_page(page))
			continue;

		list_add(&page->lru, &page_list);
		inc_zone_page_state(page, NR_ISOLATED_ANON +
					page_is_file_cache(page));
		isolated++;

		/*
		 * Reschedule every SWAP_CLUSTER_MAX pages.
		 */
		if (isolated >= SWAP_CLUSTER_MAX) {
			pte_unmap_unlock(orig_pte, ptl);
			rs->reclaimed_page += reclaim_pages_from_list(&page_list);
			isolated = 0;
			
			// if the page count of relcaim is over than max page count
			if (rs->reclaimed_page >= rs->reclaim_max_page)	{
				rs->ret = RECLAIM_RET_OVER_MAX_COUNT;
				goto out_without_unlock;
			}

			cond_resched();
			orig_pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
		}
	}

out:
	pte_unmap_unlock(orig_pte, ptl);
	reclaim_pages_from_list(&page_list);

out_without_unlock:
	cond_resched();
	return 0;
}

void do_task_reclaim(struct task_struct *task, int max_page, enum reclaim_type type,
					unsigned long scan_vm_start, struct reclaim_stat *ret)
{
	struct reclaim_stat rs;
	struct mm_struct *mm;
	struct vm_area_struct *vma;

	mm = get_task_mm(task);
	if (mm) {
		struct mm_walk reclaim_walk = {
			.pmd_entry = reclaim_pte_range,
			.mm = mm,
		};

		rs.ret = 0;
		rs.reclaim_max_page = max_page;
		rs.reclaimed_page = 0;
		rs.vm_start = 0;
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			rs.vma = vma;
			rs.ret = 0;
			rs.vm_start = vma->vm_start;
			reclaim_walk.private = &rs;

			// scan from vm start address to be requested by argument
			if (vma->vm_start < scan_vm_start)
				continue;

			/* bail out */
			if (signal_pending(task)) {
				rs.ret = RECLAIM_RET_SKIP_SIGNAL_PENDDING;
				break;
			}

			if (task->on_rq) {
				rs.ret = RECLAIM_RET_SKIP_ON_RQ;
				break;
			}

			if (is_vm_hugetlb_page(vma))
				continue;

			if (!vma_is_anonymous(vma) && !(type & RECLAIM_FILE))
				continue;

			if (vma_is_anonymous(vma) && !(type & RECLAIM_ANON))
				continue;

			walk_page_range(vma->vm_start,
					vma->vm_end,
					&reclaim_walk);

			if (rs.ret != RECLAIM_RET_OK)
				break;
		}
	
		if (!vma)
			rs.vm_start = 0;

		flush_tlb_mm(mm);
		up_read(&mm->mmap_sem);
		mmput(mm);

		if (ret)
			memcpy(ret, &rs, sizeof(struct reclaim_stat));
	}
}

static ssize_t proc_reclaim_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int itype;
	int rv;
	enum reclaim_type type;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	rv = kstrtoint(strstrip(buffer), 10, &itype);
	if (rv < 0)
		return rv;

	type = (enum reclaim_type)itype;
	if (type < RECLAIM_FILE || type > RECLAIM_ALL)
		return -EINVAL;

	task = get_proc_task(file->f_path.dentry->d_inode);
	if (!task)
		return -ESRCH;

	if (task->on_rq)
		goto out;

	do_task_reclaim(task, INFINITY, type, 0, NULL);

out:
	put_task_struct(task);
	return count;
}

static long ioctl_reclaim_write(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct reclaim_stat rs;
	struct usr_reclaim_stat urs;
	struct task_struct *task;
	enum reclaim_type type;

	void __user *ubuf = (void __user *)arg;

	if(copy_from_user(&urs, ubuf, sizeof(struct usr_reclaim_stat)))
		return -EFAULT;

	type = (enum reclaim_type)urs.type;
	if (type < RECLAIM_FILE || type > RECLAIM_ALL)
		return -EINVAL;

	task = get_proc_task(file->f_path.dentry->d_inode);
	if (!task)
		return -ESRCH;

	if (task->on_rq) {
		urs.ret = RECLAIM_RET_SKIP_ON_RQ;
		goto out;
	}

	do_task_reclaim(task, urs.reclaim_max_page, type, urs.vm_start, &rs);

	urs.ret = rs.ret;
	urs.vm_start = rs.vm_start;
	urs.reclaimed_page = rs.reclaimed_page;
out:
	put_task_struct(task);

	if (copy_to_user(ubuf, &urs, sizeof(struct usr_reclaim_stat)))
        return -EFAULT;

	return 0;
}

const struct file_operations proc_reclaim_operations = {
	.write		= proc_reclaim_write,
	.unlocked_ioctl = ioctl_reclaim_write,
	.llseek		= noop_llseek,
};

#ifdef CONFIG_NUMA

struct numa_maps {
	unsigned long pages;
	unsigned long anon;
	unsigned long active;
	unsigned long writeback;
	unsigned long mapcount_max;
	unsigned long dirty;
	unsigned long swapcache;
	unsigned long node[MAX_NUMNODES];
};

struct numa_maps_private {
	struct proc_maps_private proc_maps;
	struct numa_maps md;
};

static void gather_stats(struct page *page, struct numa_maps *md, int pte_dirty,
			unsigned long nr_pages)
{
	int count = page_mapcount(page);

	md->pages += nr_pages;
	if (pte_dirty || PageDirty(page))
		md->dirty += nr_pages;

	if (PageSwapCache(page))
		md->swapcache += nr_pages;

	if (PageActive(page) || PageUnevictable(page))
		md->active += nr_pages;

	if (PageWriteback(page))
		md->writeback += nr_pages;

	if (PageAnon(page))
		md->anon += nr_pages;

	if (count > md->mapcount_max)
		md->mapcount_max = count;

	md->node[page_to_nid(page)] += nr_pages;
}

static struct page *can_gather_numa_stats(pte_t pte, struct vm_area_struct *vma,
		unsigned long addr)
{
	struct page *page;
	int nid;

	if (!pte_present(pte))
		return NULL;

	page = vm_normal_page(vma, addr, pte);
	if (!page)
		return NULL;

	if (PageReserved(page))
		return NULL;

	nid = page_to_nid(page);
	if (!node_isset(nid, node_states[N_MEMORY]))
		return NULL;

	return page;
}

static int gather_pte_stats(pmd_t *pmd, unsigned long addr,
		unsigned long end, struct mm_walk *walk)
{
	struct numa_maps *md = walk->private;
	struct vm_area_struct *vma = walk->vma;
	spinlock_t *ptl;
	pte_t *orig_pte;
	pte_t *pte;

	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
		pte_t huge_pte = *(pte_t *)pmd;
		struct page *page;

		page = can_gather_numa_stats(huge_pte, vma, addr);
		if (page)
			gather_stats(page, md, pte_dirty(huge_pte),
				     HPAGE_PMD_SIZE/PAGE_SIZE);
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	orig_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	do {
		struct page *page = can_gather_numa_stats(*pte, vma, addr);
		if (!page)
			continue;
		gather_stats(page, md, pte_dirty(*pte), 1);

	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(orig_pte, ptl);
	return 0;
}
#ifdef CONFIG_HUGETLB_PAGE
static int gather_hugetlb_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	struct numa_maps *md;
	struct page *page;

	if (!pte_present(*pte))
		return 0;

	page = pte_page(*pte);
	if (!page)
		return 0;

	md = walk->private;
	gather_stats(page, md, pte_dirty(*pte), 1);
	return 0;
}

#else
static int gather_hugetlb_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	return 0;
}
#endif

/*
 * Display pages allocated per node and memory policy via /proc.
 */
static int show_numa_map(struct seq_file *m, void *v, int is_pid)
{
	struct numa_maps_private *numa_priv = m->private;
	struct proc_maps_private *proc_priv = &numa_priv->proc_maps;
	struct vm_area_struct *vma = v;
	struct numa_maps *md = &numa_priv->md;
	struct file *file = vma->vm_file;
	struct mm_struct *mm = vma->vm_mm;
	struct mm_walk walk = {
		.hugetlb_entry = gather_hugetlb_stats,
		.pmd_entry = gather_pte_stats,
		.private = md,
		.mm = mm,
	};
	struct mempolicy *pol;
	char buffer[64];
	int nid;

	if (!mm)
		return 0;

	/* Ensure we start with an empty set of numa_maps statistics. */
	memset(md, 0, sizeof(*md));

	pol = __get_vma_policy(vma, vma->vm_start);
	if (pol) {
		mpol_to_str(buffer, sizeof(buffer), pol);
		mpol_cond_put(pol);
	} else {
		mpol_to_str(buffer, sizeof(buffer), proc_priv->task_mempolicy);
	}

	seq_printf(m, "%08lx %s", vma->vm_start, buffer);

	if (file) {
		seq_puts(m, " file=");
		seq_path(m, &file->f_path, "\n\t= ");
	} else if (vma->vm_start <= mm->brk && vma->vm_end >= mm->start_brk) {
		seq_puts(m, " heap");
	} else {
		struct task_struct *t = taskpid_of_stack(proc_priv, vma, is_pid);
		if (t != NULL) {
			/*
			 * Thread stack in /proc/PID/task/TID/maps or
			 * the main process stack.
			 */
			if (!is_pid || (vma->vm_start <= mm->start_stack &&
			    vma->vm_end >= mm->start_stack))
				seq_puts(m, " stack");
			else
				seq_printf(m, " stack:%d", t->pid);

			put_task_struct(t);
		}
	}

	if (is_vm_hugetlb_page(vma))
		seq_puts(m, " huge");

	/* mmap_sem is held by m_start */
	walk_page_vma(vma, &walk);

	if (!md->pages)
		goto out;

	if (md->anon)
		seq_printf(m, " anon=%lu", md->anon);

	if (md->dirty)
		seq_printf(m, " dirty=%lu", md->dirty);

	if (md->pages != md->anon && md->pages != md->dirty)
		seq_printf(m, " mapped=%lu", md->pages);

	if (md->mapcount_max > 1)
		seq_printf(m, " mapmax=%lu", md->mapcount_max);

	if (md->swapcache)
		seq_printf(m, " swapcache=%lu", md->swapcache);

	if (md->active < md->pages && !is_vm_hugetlb_page(vma))
		seq_printf(m, " active=%lu", md->active);

	if (md->writeback)
		seq_printf(m, " writeback=%lu", md->writeback);

	for_each_node_state(nid, N_MEMORY)
		if (md->node[nid])
			seq_printf(m, " N%d=%lu", nid, md->node[nid]);

	seq_printf(m, " kernelpagesize_kB=%lu", vma_kernel_pagesize(vma) >> 10);
out:
	seq_putc(m, '\n');
	m_cache_vma(m, vma);
	return 0;
}

static int show_pid_numa_map(struct seq_file *m, void *v)
{
	return show_numa_map(m, v, 1);
}

static int show_tid_numa_map(struct seq_file *m, void *v)
{
	return show_numa_map(m, v, 0);
}

static const struct seq_operations proc_pid_numa_maps_op = {
	.start  = m_start,
	.next   = m_next,
	.stop   = m_stop,
	.show   = show_pid_numa_map,
};

static const struct seq_operations proc_tid_numa_maps_op = {
	.start  = m_start,
	.next   = m_next,
	.stop   = m_stop,
	.show   = show_tid_numa_map,
};

static int numa_maps_open(struct inode *inode, struct file *file,
			  const struct seq_operations *ops)
{
	return proc_maps_open(inode, file, ops,
				sizeof(struct numa_maps_private));
}

static int pid_numa_maps_open(struct inode *inode, struct file *file)
{
	return numa_maps_open(inode, file, &proc_pid_numa_maps_op);
}

static int tid_numa_maps_open(struct inode *inode, struct file *file)
{
	return numa_maps_open(inode, file, &proc_tid_numa_maps_op);
}

const struct file_operations proc_pid_numa_maps_operations = {
	.open		= pid_numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

const struct file_operations proc_tid_numa_maps_operations = {
	.open		= tid_numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};
#endif /* CONFIG_NUMA */
