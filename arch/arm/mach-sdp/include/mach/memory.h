/****************************************************************8
 *
 * arch/arm/mach-sdp/include/mach/memory.h
 * 
 * Copyright (C) 2010 Samsung Electronics co.
 * Author : tukho.kim@samsung.com
 *
 */
#ifndef _MACH_MEMORY_H_
#define _MACH_MEMORY_H_

#ifndef __ASSEMBLY__
extern int sdp_mem_nr;
extern phys_addr_t sdp_mem_size[];
extern phys_addr_t sdp_mem_base[];
#endif	/* __ASSEMBLY__ */

#if defined(CONFIG_MACH_FOXAP)
#   include <mach/foxap/memory-foxap.h>
#elif defined(CONFIG_MACH_FOXB)
#   include <mach/foxb/memory-foxb.h>
#elif defined(CONFIG_MACH_GOLFS)
#   include <mach/golfs/memory-golfs.h>
#elif defined(CONFIG_MACH_GOLFP)
#   include <mach/golfp/memory-golfp.h>
#elif defined(CONFIG_MACH_ECHOP)
#   include <mach/echop/memory-echop.h>
#elif defined(CONFIG_OF) && defined(CONFIG_SPARSEMEM)
/* override phys/virt conversion macros */

#define MAX_PHYSMEM_BITS		32
#define SECTION_SIZE_BITS		23

#ifndef __ASSEMBLY__


#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
extern unsigned long	__pv_offset;
extern unsigned long	__pv_phys_offset;
#define PHYS_OFFSET	__pv_phys_offset
#else
#define PHYS_OFFSET	(sdp_sys_mem0_base)
#endif

#define __virt_to_phys(x) ({	\
	phys_addr_t ret = (phys_addr_t)x - PAGE_OFFSET;			\
	phys_addr_t size = 0;                                                                                              \
	int i;                                                                                                             \
		for(i = 0; i < sdp_mem_nr; i++) {                                                                              \
			if (ret < (size + sdp_mem_size[i])) {                                                                      \
				ret = sdp_mem_base[i] + (ret - size);                                                                  \
				break;                                                                                                 \
			}                                                                                                          \
			size += sdp_mem_size[i];                                                                                   \
		}                                                                                                              \
		if(unlikely(i == sdp_mem_nr)) {                                                                                \
			ret = sdp_mem_base[sdp_mem_nr-1] + sdp_mem_size[sdp_mem_nr-1] + ((phys_addr_t)x - PAGE_OFFSET - size);     \
		}                                                                                                              \
	ret;	\
	})


#define __phys_to_virt(x) ({	\
	unsigned long ret;	\
	phys_addr_t size = 0;                                                                                                                \
	int i;                                                                                                                               \
	if(likely(sdp_mem_nr)) {                                                                                                             \
		for(i = 0; i < sdp_mem_nr; i++) {                                                                                                \
			if ((x - sdp_mem_base[i]) < sdp_mem_size[i]) {                                                                               \
				ret = PAGE_OFFSET + (unsigned long)size + (unsigned long)(x - sdp_mem_base[i]);                                          \
				break;                                                                                                                   \
			}                                                                                                                            \
			size += sdp_mem_size[i];                                                                                                     \
		}                                                                                                                                \
		if(unlikely(i == sdp_mem_nr)) {                                                                                                  \
			ret = PAGE_OFFSET + (unsigned long)size + (unsigned long)(x - (sdp_mem_base[sdp_mem_nr-1] + sdp_mem_size[sdp_mem_nr-1]));    \
		}                                                                                                                                \
	} else {                                                                                                                             \
		ret = PAGE_OFFSET + (x - PHYS_OFFSET);/* for early conversion */                                                                 \
	}                                                                                                                                    \
	ret;	\
	})



#endif	/* __ASSEMBLY__ */

#else
#define PLAT_PHYS_OFFSET		UL(0x40000000)
#endif 

#endif

