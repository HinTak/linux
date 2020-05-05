/* arch/arm/mach-realview/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG_LL_PHYS_BASE		0xfd000000
#ifdef CONFIG_DEBUG_NVT72668_UART0_CA9	/* UART0 */ 
#define DEBUG_LL_UART_OFFSET		0x00090000
#elif CONFIG_DEBUG_NVT72668_UART1_CA9	/* UART1 */
#define DEBUG_LL_UART_OFFSET		0x00091000
#else
#error "Please select UART for early printk"
#endif
#define DEBUG_LL_VIRT_BASE		0xfd000000

		.macro	addruart,rp,rv,tmp
		mov	\rp, #DEBUG_LL_UART_OFFSET
		orr	\rv, \rp, #DEBUG_LL_VIRT_BASE
		orr	\rp, \rp, #DEBUG_LL_PHYS_BASE
		.endm

#define UART_SHIFT 2
#include <asm/hardware/debug-8250.S>
