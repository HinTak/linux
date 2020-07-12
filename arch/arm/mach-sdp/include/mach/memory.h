#ifndef _MACH_MEMORY_H_
#define _MACH_MEMORY_H_

#include <asm/page.h>

#if defined(CONFIG_SPARSEMEM)

#ifdef CONFIG_ARCH_PHYS_ADDR_T_64BIT
#define MAX_PHYSMEM_BITS		33
#define SECTION_SIZE_BITS		24
#else
#define MAX_PHYSMEM_BITS		32
#define SECTION_SIZE_BITS		23
#endif

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT

extern unsigned long __pv_phys_pfn_offset;
extern u64 __pv_offset;
extern void fixup_pv_table(const void *, unsigned long);
extern const void *__pv_table_begin, *__pv_table_end;

#define PHYS_OFFSET	((phys_addr_t)__pv_phys_pfn_offset << PAGE_SHIFT)
#define PHYS_PFN_OFFSET	(__pv_phys_pfn_offset)

#endif
#ifdef CONFIG_SPARSE_LOWMEM_EXT_MAP
#define __virt_to_phys(x)	__sdp_dma_virt_to_phys(x)
#define __phys_to_virt(x)	__sdp_dma_phys_to_virt(x)
#define __virt_to_phys_ext(x)	__sdp_virt_to_phys_ext(x)
#define __phys_to_virt_ext(x)	__sdp_phys_to_virt_ext(x)

phys_addr_t __sdp_dma_virt_to_phys(unsigned long x);
unsigned long __sdp_dma_phys_to_virt(phys_addr_t x);
phys_addr_t __sdp_virt_to_phys_ext(unsigned long x);
unsigned long __sdp_phys_to_virt_ext(phys_addr_t x);
phys_addr_t __sdp_virt_to_phys(unsigned long x);
unsigned long __sdp_phys_to_virt(phys_addr_t x);


#else
#define __virt_to_phys(x)	__sdp_virt_to_phys(x)
#define __phys_to_virt(x)	__sdp_phys_to_virt(x)

phys_addr_t __sdp_virt_to_phys(unsigned long x);
unsigned long __sdp_phys_to_virt(phys_addr_t x);
#endif
#endif	/* __ASSEMBLY__ */

#else
#define PLAT_PHYS_OFFSET		UL(0x40000000)
#endif 

#endif

