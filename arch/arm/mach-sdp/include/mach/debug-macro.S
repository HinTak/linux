/*
 * arch/arm/mach-sdp/include/mach/debug-macro.S
 *
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * support early printk console for SDP SoC.
 */

#include <mach/regs-serial.h>

	.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT
	.endm

	.macro	senduart, rd, rx
		str	\rd, [\rx, #SDP_UTXH]	@ TXDATA
	.endm

	.macro	waituart, rd, rx
1001:		ldr	\rd, [\rx, #SDP_UFSTAT]	@ get status register
		tst	\rd, #SDP_UFSTAT_TXFIFO_FULL
		bne	1001b			@ wait if FIFO is full
	.endm

	.macro	busyuart, rd, rx
1002:		ldr	\rd, [\rx, #SDP_UFSTAT]	@ get status register
		tst	\rd, #SDP_UFSTAT_TXFIFO_MASK
		bne	1002b			@ wait if FIFO is not empty
	.endm
