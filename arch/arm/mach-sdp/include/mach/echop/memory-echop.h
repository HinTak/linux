/*
 * memory-echop.h : Define memory address space.
 * 
 * Copyright (C) 2011-2013 Samsung Electronics co.
 * Ikjoon Jang <seungjun.heo@samsung.com>
 *
 */
#ifndef _MEMORY_ECHOP_H_
#define _MEMORY_ECHOP_H_

/* Echo-P DDR Bank structure:
 * bank 0:  0x40000000-0x7FFFFFFF - 512MB
 * bank 1:  0x80000000-0x9FFFFFFF - 512MB
 * bank 2:  0xA0000000-0xBFFFFFFF - 512MB
 */

/* Address space for DTV memory map */
#define MACH_MEM0_BASE 		(0x40000000)
#define MACH_MEM1_BASE 		(0x80000000)
#define MACH_MEM2_BASE 		(0xA0000000)

/* default values */
#define SYS_MEM0_SIZE		(64 << 20)
#define SYS_MEM1_SIZE		(64 << 20)
#define SYS_MEM2_SIZE		(64 << 20)

#define MACH_MEM0_SIZE 		SZ_256M
#define MACH_MEM1_SIZE 		SZ_256M
#define MACH_MEM2_SIZE 		SZ_256M

/* see arch/arm/mach-sdp/Kconfig */
#if defined(CONFIG_SDP_SINGLE_DDR_B)
#define PLAT_PHYS_OFFSET		MACH_MEM1_BASE
#elif defined(CONFIG_SDP_SINGLE_DDR_C)
#define PLAT_PHYS_OFFSET		MACH_MEM2_BASE
#else
#define PLAT_PHYS_OFFSET		MACH_MEM0_BASE
#endif

#ifndef __ASSEMBLY__
extern unsigned int sdp_sys_mem0_size;
extern unsigned int sdp_sys_mem1_size;
 
#if defined(CONFIG_SPARSEMEM)

/* Bank size limit 512MByte */
#define SECTION_SIZE_BITS	29
#define MAX_PHYSMEM_BITS	32

#define MACH_MEM_MASK	(0x1FFFFFFF)
#define MACH_MEM_SHIFT	(29)

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
extern unsigned long __pv_phys_offset;
#define PHYS_OFFSET __pv_phys_offset
#endif

#define __phys_to_virt(x) \
	({ u32 val = PAGE_OFFSET; \
           switch((x >> MACH_MEM_SHIFT) - 2) { \
		case 3:			\
			val += sdp_sys_mem1_size; \
		case 2:			\
			val += sdp_sys_mem0_size; \
			break;		\
		case 1:			\
			val += 0x20000000; \
		default:		\
		case 0:			\
			break;		\
	   } \
	   val += (x & MACH_MEM_MASK); \
	   val;})

#define __virt_to_phys(x) \
	({ phys_addr_t val = (phys_addr_t)x - PAGE_OFFSET; \
	   if(val < sdp_sys_mem0_size) val += MACH_MEM0_BASE; \
           else { \
		val -= sdp_sys_mem0_size; \
		if (val < sdp_sys_mem1_size) val += MACH_MEM1_BASE; \
		else { \
		    val -= sdp_sys_mem1_size; \
		    val += MACH_MEM2_BASE; \
		} \
	   } \
	 val;})

#endif	/* SPARSEMEM */
#endif	/* __ASSEMBLY__ */

#endif

