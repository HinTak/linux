#ifndef _ARM_KEXEC_H
#define _ARM_KEXEC_H

#ifdef CONFIG_KEXEC

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE	4096

#define KEXEC_ARCH KEXEC_ARCH_ARM

#define KEXEC_ARM_ATAGS_OFFSET  0x1000
#define KEXEC_ARM_ZIMAGE_OFFSET 0x8000

#ifndef __ASSEMBLY__

#ifdef CONFIG_KEXEC_CSYSTEM

/* ARM CORE regs mapping structure */
struct arm_core_regs_t {
	/* COMMON */
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
	unsigned int r4;
	unsigned int r5;
	unsigned int r6;
	unsigned int r7;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int r11;
	unsigned int r12;

	/* SVC */
	unsigned int r13_svc;
	unsigned int r14_svc;
	unsigned int spsr_svc;

	/* PC & CPSR */
	unsigned int pc;
	unsigned int cpsr;

	/* USR/SYS */
	unsigned int r13_usr;
	unsigned int r14_usr;

	/* FIQ */
	unsigned int r8_fiq;
	unsigned int r9_fiq;
	unsigned int r10_fiq;
	unsigned int r11_fiq;
	unsigned int r12_fiq;
	unsigned int r13_fiq;
	unsigned int r14_fiq;
	unsigned int spsr_fiq;

	/* IRQ */
	unsigned int r13_irq;
	unsigned int r14_irq;
	unsigned int spsr_irq;

	/* MON */
	unsigned int r13_mon;	// do not use
	unsigned int r14_mon;	// do not use
	unsigned int spsr_mon;	// do not use

	/* ABT */
	unsigned int r13_abt;
	unsigned int r14_abt;
	unsigned int spsr_abt;

	/* UNDEF */
	unsigned int r13_und;
	unsigned int r14_und;
	unsigned int spsr_und;
};

struct arm_mmu_regs_t {
	int SCTLR;
	int TTBR0;
	int TTBR1;
	int TTBCR;
	int DACR;
	int DFSR;
	int DFAR;
	int IFSR;
	int IFAR;
	int DAFSR;
	int IAFSR;
	int PMRRR;
	int NMRRR;
	int FCSEPID;
	int CONTEXT;
	int URWTPID;
	int UROTPID;
	int POTPIDR;
};

struct arm_regs_t {
	int cpu;
	int pid;
	struct arm_core_regs_t core_regs;
	struct arm_mmu_regs_t mmu_regs;
};

/**
 * crash_setup_core_regs() - save total arm registers for the panic kernel
 * @armregs : registers are saved here
 *
 * Function gets all cuurent machine registers from @regs and cpu itself.
 */
static inline void crash_setup_core_regs(struct arm_core_regs_t *core_regs)
{
	/* we will be in SVC mode when we enter this function. Collect
	   SVC registers along with cmn registers. */
	asm("str r0, [%0,#0]\n\t"			/* R0 is pushed first to core_regs */
	    "mov r0, %0\n\t"				/* R0 will be alias for core_regs */
	    "str r1, [r0,#4]\n\t"			/* R1 */
	    "str r2, [r0,#8]\n\t"			/* R2 */
	    "str r3, [r0,#12]\n\t"			/* R3 */
	    "str r4, [r0,#16]\n\t"			/* R4 */
	    "str r5, [r0,#20]\n\t"			/* R5 */
	    "str r6, [r0,#24]\n\t"			/* R6 */
	    "str r7, [r0,#28]\n\t"			/* R7 */
	    "str r8, [r0,#32]\n\t"			/* R8 */
	    "str r9, [r0,#36]\n\t"			/* R9 */
	    "str r10, [r0,#40]\n\t"			/* R10 */
	    "str r11, [r0,#44]\n\t"			/* R11 */
	    "str r12, [r0,#48]\n\t"			/* R12 */
	    /* SVC */
	    "str r13, [r0,#52]\n\t"			/* R13_SVC */
	    "str r14, [r0,#56]\n\t"			/* R14_SVC */
	    "mrs r1, spsr\n\t"				/* SPSR_SVC */
	    "str r1, [r0,#60]\n\t"
	    /* PC and CPSR */
	    "sub r1, r15, #0x4\n\t"			/* PC */
	    "str r1, [r0,#64]\n\t"
	    "mrs r1, cpsr\n\t"				/* CPSR */
	    "str r1, [r0,#68]\n\t"
	    /* SYS/USR */
	    "mrs r1, cpsr\n\t"				/* switch to SYS mode */
	    "and r1, r1, #0xFFFFFFE0\n\t"
	    "orr r1, r1, #0x1f\n\t"
	    "msr cpsr,r1\n\t"
	    "str r13, [r0,#72]\n\t"			/* R13_USR */
	    "str r14, [r0,#76]\n\t"			/* R14_USR */
	    /* FIQ */
	    "mrs r1, cpsr\n\t"				/* switch to FIQ mode */
	    "and r1,r1,#0xFFFFFFE0\n\t"
	    "orr r1,r1,#0x11\n\t"
	    "msr cpsr,r1\n\t"
	    "str r8, [r0,#80]\n\t"			/* R8_FIQ */
	    "str r9, [r0,#84]\n\t"			/* R9_FIQ */
	    "str r10, [r0,#88]\n\t"			/* R10_FIQ */
	    "str r11, [r0,#92]\n\t"			/* R11_FIQ */
	    "str r12, [r0,#96]\n\t"			/* R12_FIQ */
	    "str r13, [r0,#100]\n\t"		/* R13_FIQ */
	    "str r14, [r0,#104]\n\t"		/* R14_FIQ */
	    "mrs r1, spsr\n\t"				/* SPSR_FIQ */
	    "str r1, [r0,#108]\n\t"
		/* IRQ */
	    "mrs r1, cpsr\n\t"				/* switch to IRQ mode */
	    "and r1, r1, #0xFFFFFFE0\n\t"
	    "orr r1, r1, #0x12\n\t"
	    "msr cpsr,r1\n\t"
	    "str r13, [r0,#112]\n\t"		/* R13_IRQ */
	    "str r14, [r0,#116]\n\t"		/* R14_IRQ */
	    "mrs r1, spsr\n\t"				/* SPSR_IRQ */
	    "str r1, [r0,#120]\n\t"
		/* 
		 * The Monitor mode access is forbid on Aarch32,
		 * and does not worth to save. So skip it.
		 */
	    /* ABT */
	    "mrs r1, cpsr\n\t"				/* switch to Abort mode */
	    "and r1, r1, #0xFFFFFFE0\n\t"
	    "orr r1, r1, #0x17\n\t"
	    "msr cpsr,r1\n\t"
	    "str r13, [r0,#136]\n\t"		/* R13_ABT */
	    "str r14, [r0,#140]\n\t"		/* R14_ABT */
	    "mrs r1, spsr\n\t"				/* SPSR_ABT */
	    "str r1, [r0,#144]\n\t"
	    /* UND */
	    "mrs r1, cpsr\n\t"				/* switch to undef mode */
	    "and r1, r1, #0xFFFFFFE0\n\t"
	    "orr r1, r1, #0x1B\n\t"
	    "msr cpsr,r1\n\t"
	    "str r13, [r0,#148]\n\t"		/* R13_UND */
	    "str r14, [r0,#152]\n\t"		/* R14_UND */
	    "mrs r1, spsr\n\t"				/* SPSR_UND */
	    "str r1, [r0,#156]\n\t"
	    /* restore to SVC mode */
	    "mrs r1, cpsr\n\t"				/* switch to SVC mode */
	    "and r1, r1, #0xFFFFFFE0\n\t"
	    "orr r1, r1, #0x13\n\t"
	    "msr cpsr,r1\n\t" 
		:								/* output */
	    : "r"(core_regs)				/* input */
	    : "%r0", "%r1"					/* clobbered registers */
	);

	return;
}

static inline void crash_setup_mmu_regs(struct arm_mmu_regs_t *mmu_regs)
{
	asm("mrc    p15, 0, r1, c1, c0, 0\n\t"	/* SCTLR */
	    "str r1, [%0]\n\t"
	    "mrc    p15, 0, r1, c2, c0, 0\n\t"	/* TTBR0 */
	    "str r1, [%0,#4]\n\t"
	    "mrc    p15, 0, r1, c2, c0,1\n\t"	/* TTBR1 */
	    "str r1, [%0,#8]\n\t"
	    "mrc    p15, 0, r1, c2, c0,2\n\t"	/* TTBCR */
	    "str r1, [%0,#12]\n\t"
	    "mrc    p15, 0, r1, c3, c0,0\n\t"	/* DACR */
	    "str r1, [%0,#16]\n\t"
	    "mrc    p15, 0, r1, c5, c0,0\n\t"	/* DFSR */
	    "str r1, [%0,#20]\n\t"
	    "mrc    p15, 0, r1, c6, c0,0\n\t"	/* DFAR */
	    "str r1, [%0,#24]\n\t"
	    "mrc    p15, 0, r1, c5, c0,1\n\t"	/* IFSR */
	    "str r1, [%0,#28]\n\t"
	    "mrc    p15, 0, r1, c6, c0,2\n\t"	/* IFAR */
	    "str r1, [%0,#32]\n\t"
	    /* Don't populate DAFSR and RAFSR */
	    "mrc    p15, 0, r1, c10, c2,0\n\t"	/* PMRRR */
	    "str r1, [%0,#44]\n\t"
	    "mrc    p15, 0, r1, c10, c2,1\n\t"	/* NMRRR */
	    "str r1, [%0,#48]\n\t"
	    "mrc    p15, 0, r1, c13, c0,0\n\t"	/* FCSEPID */
	    "str r1, [%0,#52]\n\t"
	    "mrc    p15, 0, r1, c13, c0,1\n\t"	/* CONTEXT */
	    "str r1, [%0,#56]\n\t"
	    "mrc    p15, 0, r1, c13, c0,2\n\t"	/* URWTPID */
	    "str r1, [%0,#60]\n\t"
	    "mrc    p15, 0, r1, c13, c0,3\n\t"	/* UROTPID */
	    "str r1, [%0,#64]\n\t"
	    "mrc    p15, 0, r1, c13, c0,4\n\t"	/* POTPIDR */
	    "str r1, [%0,#68]\n\t" 
		:									/* output */
	    : "r"(mmu_regs)						/* input */
	    : "%r1", "memory"					/* clobbered register */
	);
}

static inline void crash_setup_regs(struct arm_regs_t *arm_regs)
{
	crash_setup_core_regs(&arm_regs->core_regs);
	crash_setup_mmu_regs(&arm_regs->mmu_regs);
}
#else 
/**
 * crash_setup_regs() - save registers for the panic kernel
 * @newregs: registers are saved here
 * @oldregs: registers to be saved (may be %NULL)
 *
 * Function copies machine registers from @oldregs to @newregs. If @oldregs is
 * %NULL then current registers are stored there.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs) {
		memcpy(newregs, oldregs, sizeof(*newregs));
	} else {
		__asm__ __volatile__ (
			"stmia	%[regs_base], {r0-r12}\n\t"
			"mov	%[_ARM_sp], sp\n\t"
			"str	lr, %[_ARM_lr]\n\t"
			"adr	%[_ARM_pc], 1f\n\t"
			"mrs	%[_ARM_cpsr], cpsr\n\t"
		"1:"
			: [_ARM_pc] "=r" (newregs->ARM_pc),
			  [_ARM_cpsr] "=r" (newregs->ARM_cpsr),
			  [_ARM_sp] "=r" (newregs->ARM_sp),
			  [_ARM_lr] "=o" (newregs->ARM_lr)
			: [regs_base] "r" (&newregs->ARM_r0)
			: "memory"
		);
	}
}
#endif /* CONFIG_KEXEC_CSYSTEM */

/* Function pointer to optional machine-specific reinitialization */
extern void (*kexec_reinit)(void);

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_KEXEC */

#endif /* _ARM_KEXEC_H */
