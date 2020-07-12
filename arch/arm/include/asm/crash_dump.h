#ifndef __ASM_ARM_ARCH_CRASH_DUMP_H
#define __ASM_ARM_ARCH_CRASH_DUMP_H

#if (defined CONFIG_CRASH_DUMP_CSYSTEM) && (defined CONFIG_ARCH_PHYS_ADDR_T_64BIT)
/*
 * With the CONFIG_ARM_LPAE plus CONFIG_ARCH_PHYS_ADDR_T_64BIT,
 * the elf header made by kexec-tools can be elf64 bit format,
 * because the normal kernel might use physical memory bigger then
 * 0xFFFFFFFF. To handle this situation, the crash_kernel should
 * accept the elf64 header.
 */
#define vmcore_elf64_check_arch(x) 1

#endif

#endif
