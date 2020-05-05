/*
 * memory-foxb.h : Define memory address space.
 * 
 * Copyright (C) 2013 Samsung Electronics co.
 * SeungJun Heo <seungjun.heo@samsung.com>
 * Seihee Chon <sh.chon@samsung.com>
 * Sola lee<sssol.lee@samsung.com>
 */
#ifndef _MEMORY_FOXB_H_
#define _MEMORY_FOXB_H_

/* 
 * node 0:  0x40000000-0x7FFFFFFF - 1GB
 * node 1:  0x80000000-0xBFFFFFFF - 1GB
 */

#define MACH_MEM0_BASE 		(0x40000000)
#define MACH_MEM1_BASE 		(0x80000000)

/* XXX: experimental macros here, referencing meminfo can be overhead. */

#define PLAT_PHYS_OFFSET	MACH_MEM0_BASE

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
extern unsigned long __pv_phys_offset;
#define PHYS_OFFSET __pv_phys_offset
#endif

#if defined(CONFIG_SPARSEMEM)

extern unsigned int sdp_sys_mem0_size;

#define SECTION_SIZE_BITS 	(23)
#define MAX_PHYSMEM_BITS	(32)

/* kernel size limit 1Gbyte */
#define KVADDR_MASK		(0x3FFFFFFF)
/* Bank size limit 1GByte */
#define MACH_MEM_MASK		(0x3FFFFFFF)
#define MACH_MEM_SHIFT		(31)

#define __virt_to_phys(x) ({					\
	phys_addr_t ret = ((phys_addr_t)(x) & KVADDR_MASK);	\
	if (ret >= sdp_sys_mem0_size)				\
	       	ret += MACH_MEM1_BASE - sdp_sys_mem0_size;	\
	else							\
		ret += MACH_MEM0_BASE;				\
	ret;							\
	})

#define __phys_to_virt(x) ({					\
	unsigned long ret;					\
	ret = PAGE_OFFSET + ((phys_addr_t)(x) & MACH_MEM_MASK);	\
	if ((x) & (1 << MACH_MEM_SHIFT))			\
		ret += sdp_sys_mem0_size;			\
	ret;							\
	})

#endif /* CONFIG_SPARSEMEM */

#endif	/* __ASSEMBLY__ */

#endif

