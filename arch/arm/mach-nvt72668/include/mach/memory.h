/*
 *  arch/arm/mach-nvt72668/include/mach/memory.h
 *
 *  Copyright (C) 2013 Novatek Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/* The DRAM start address is 576MB(0x24000000)
 * MAX_PHYSMEM_BITS: the maxinum physical memory is 4GB(32 bits)     
 * SECTION_SIZE_BITS: the maxinum DRAM size for single chip is 512MB(29 bits) 
 */ 
   
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#ifdef CONFIG_MACH_NT14U
#define PHYS_OFFSET		UL(0x04400000)
#endif

#ifdef CONFIG_MACH_NT14M
#define PHYS_OFFSET		UL(0x10a00000)
#endif

#ifdef CONFIG_SPARSEMEM

#define MAX_PHYSMEM_BITS        32
#define SECTION_SIZE_BITS       29

#endif
#endif
