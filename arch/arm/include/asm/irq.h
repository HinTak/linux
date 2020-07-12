#ifndef __ASM_ARM_IRQ_H
#define __ASM_ARM_IRQ_H

#define NR_IRQS_LEGACY	16

/* Don't Change: Keep IRQ_STACK_SIZE size same as THREAD_SIZE */
#define IRQ_STACK_SIZE                 THREAD_SIZE

#ifndef CONFIG_SPARSE_IRQ
#include <mach/irqs.h>
#else
#define NR_IRQS NR_IRQS_LEGACY
#endif

#ifndef irq_canonicalize
#define irq_canonicalize(i)	(i)
#endif

/*
 * Use this value to indicate lack of interrupt
 * capability
 */
#ifndef NO_IRQ
#define NO_IRQ	((unsigned int)(-1))
#endif

#ifndef __ASSEMBLY__

#ifdef CONFIG_IRQ_STACK
#include <linux/percpu.h>
#endif

struct irqaction;
struct pt_regs;

#ifdef CONFIG_IRQ_STACK
DECLARE_PER_CPU(unsigned long, irq_stack_ptr);
extern unsigned long irq_stack[NR_CPUS][IRQ_STACK_SIZE / sizeof(unsigned long)] __aligned(IRQ_STACK_SIZE);
/* Lowest address on the IRQ stack */
#define IRQ_STACK_BASE_PTR(cpu) (*((unsigned long *)per_cpu_ptr(&irq_stack_ptr, cpu)))

/* Highest address on the IRQ stack */
#define IRQ_STACK_PTR(cpu) (*((unsigned long *)per_cpu_ptr(&irq_stack_ptr, cpu)) + IRQ_STACK_SIZE)
/*
 * The offset from irq_stack_ptr where entry.S will store the original
 * stack pointer and frame pointer. Used by unwind_frame() and dump_backtrace().
 */
#define IRQ_STACK_TO_TASK_FRAME(ptr) (*((unsigned long *)(ptr + 0x4)))
#define IRQ_STACK_TO_TASK_STACK(ptr) (*((unsigned long *)(ptr + 0x8)))
#endif

extern void migrate_irqs(void);

extern void asm_do_IRQ(unsigned int, struct pt_regs *);
void handle_IRQ(unsigned int, struct pt_regs *);
void init_IRQ(void);

#ifdef CONFIG_MULTI_IRQ_HANDLER
extern void (*handle_arch_irq)(struct pt_regs *);
extern void set_handle_irq(void (*handle_irq)(struct pt_regs *));
#endif

#ifdef CONFIG_SMP
extern void arch_trigger_all_cpu_backtrace(bool);
#define arch_trigger_all_cpu_backtrace(x) arch_trigger_all_cpu_backtrace(x)
#endif

#endif

#endif

