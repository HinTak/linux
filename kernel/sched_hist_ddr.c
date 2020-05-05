#include <linux/mm_types.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/page-flags.h>
#include <linux/mm.h>
#include <asm/memory.h>
#include <asm/io.h>

#ifndef OFFSET_MASK
#define OFFSET_MASK (PAGE_SIZE - 1)
#endif

#define OFFSET_ALIGN_MASK (OFFSET_MASK & ~(0x3))
#define DUMP_SIZE 0x400
#define P2K(x) ((x) << (PAGE_SHIFT - 10))

void __iomem *sched_hist_log_buf = 0;

struct page *simple_follow_page(struct vm_area_struct *vma, unsigned long address,
		unsigned int flags);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct *find_vma2(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = NULL;

	if (mm) {
		struct rb_node *rb_node;

		rb_node = mm->mm_rb.rb_node;
		vma = NULL;

		while (rb_node) {
			struct vm_area_struct *vma_tmp;

			vma_tmp = rb_entry(rb_node,
					struct vm_area_struct, vm_rb);

			if (vma_tmp->vm_end > addr) {
				if (vma_tmp->vm_start <= addr) {
					vma = vma_tmp;
					break;
				}
				rb_node = rb_node->rb_left;
			} else
				rb_node = rb_node->rb_right;
		}
	}
	return vma;
}

unsigned long get_simple_physaddr(struct task_struct *tsk, unsigned long u_addr)
{
	struct page *page;
	struct vm_area_struct *vma = NULL;
	//unsigned long k_addr, k_preaddr, p_addr, pfn, pfn_addr;
	unsigned long p_addr, pfn, pfn_addr;
	int high_mem = 0;

	/* find vma w/ user address */
	vma = find_vma2(tsk->mm, u_addr);
	if (!vma) {
		goto out;
	}

	/* get page struct w/ user address */
	page = simple_follow_page(vma, u_addr, 0);

	/*Aug-17:Added check to see if the returned value is ERROR */
	if (!page || IS_ERR(page)) {
		goto out;
	}

	if (PageHighMem(page)) {
		high_mem = 1;
	}

	/* Calculate pfn, physical address,
	 * kernel mapped address w/ page struct */
	pfn = page_to_pfn(page);

	/*Aug-5-2010:modified comparison operation into macro check */
	/* In MSTAR pfn_valid is expanded as follows
	 * arch/mips/include/asm/page.h:#define pfn_valid(pfn)
	 * ((pfn) >= ARCH_PFN_OFFSET && (pfn) < max_mapnr)
	 * where the value of ARCH_PFN_OFFSET is 0 for MSTAR.
	 * This causes prevent to display warning that
	 * comparison >=0 is always true.
	 * Since this is due to system macro expansion
	 * this warning is acceptable.
	 */
	if (!pfn_valid(pfn)) {
		goto out;
	}

	/*Aug-5-2010:removed custom function to reuse system macro */
	pfn_addr = page_to_phys(page);

	if (!pfn_addr) {
		goto out;
	}

	p_addr = pfn_addr + (OFFSET_ALIGN_MASK & u_addr);

	return p_addr;
out:
	return 0;
}


#define HISTORY_SIZE       100
#define BUF_SIZE_PER_CORE  400
void schedule_history_on_ddr(struct task_struct *pre ,struct task_struct *next, int cpu)
{
	char history_time[HISTORY_SIZE];
	char history_pre [HISTORY_SIZE];
	char history_next[HISTORY_SIZE];

	memset(history_time,0x00,HISTORY_SIZE);
	memset(history_pre ,0x00,HISTORY_SIZE);
	memset(history_next,0x00,HISTORY_SIZE);

	snprintf( history_time , HISTORY_SIZE,
			"===== CORE : %d ===== [time:0x%llx]\n", cpu, sched_clock());

	snprintf( history_pre  , HISTORY_SIZE,
			"PRE : %s [%d] pc:0x%lx lr:0x%lx sp:0x%lx(physical : 0x%lx) - ppid :%s [%d]",
			pre->comm,  pre->pid,
			KSTK_EIP(pre), task_pt_regs(pre)->ARM_lr,
			KSTK_ESP(pre), get_simple_physaddr(pre, KSTK_ESP(pre)),
			pre->real_parent->comm,
			pre->real_parent->pid);

	snprintf( history_next , HISTORY_SIZE,
			"NEXT: %s [%d] pc:0x%lx lr:0x%lx sp:0x%lx(physical : 0x%lx) - ppid :%s [%d]",
			next->comm, next->pid,
			KSTK_EIP(next), task_pt_regs(next)->ARM_lr,
			KSTK_ESP(next), get_simple_physaddr(next,KSTK_ESP(next)),
			next->real_parent->comm,
			next->real_parent->pid);

	memcpy(BUF_SIZE_PER_CORE * cpu + sched_hist_log_buf, history_time, HISTORY_SIZE);
	memcpy(BUF_SIZE_PER_CORE * cpu + sched_hist_log_buf + HISTORY_SIZE, history_pre, HISTORY_SIZE);
	memcpy(BUF_SIZE_PER_CORE * cpu + sched_hist_log_buf + 2 * HISTORY_SIZE, history_next, HISTORY_SIZE);
}

static int __init sched_hist_ddr_init(void)
{
	sched_hist_log_buf = ioremap(CONFIG_SCHED_HISTORY_ON_DDR_BUFADDR,0x3000);   /* 3page */

	if(unlikely(!sched_hist_log_buf))
		return -1;

	return 0;
}

#if 0
static void __exit sched_hist_ddr_exit(void)
{
	int ret;

	ret = misc_deregister(&usben_ioctl_misc);
	if (unlikely(ret))
		printk(KERN_ERR "[usben_ioctl] failed to unregister misc device!\n");
}
#endif

module_init(sched_hist_ddr_init);
//module_exit(usben_ioctl_exit);
