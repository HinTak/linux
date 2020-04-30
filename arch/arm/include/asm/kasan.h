#ifndef _ASM_ARM_KASAN_H
#define _ASM_ARM_KASAN_H

/*
 * Compiler uses shadow offset assuming that addresses start
 * from 0. Kernel addresses don't start from 0, so shadow
 * for kernel really starts from compiler's shadow offset +
 * 'kernel address space start' >> KASAN_SHADOW_SCALE_SHIFT
 */

#define KASAN_SHADOW_START      (KASAN_SHADOW_OFFSET + \
					(KASAN_SHADOW_OFFSET >> 3))
/* 32 bits for kernel address -> (32 - 3) bits for shadow */
#define KASAN_SHADOW_END        (KASAN_SHADOW_START + (1ULL << (32 - 3)))

#endif
