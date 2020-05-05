/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

#define SDP1304_PCTL0_BASE	0x10418000
#define SDP1304_PCTL1_BASE	0x10498000
#define SDP1302_PCTL0_BASE	0x18418000
#define SDP1302_PCTL1_BASE	0x18428000
#define SDP1307_PCTL0_BASE			0x19F18000
#define SDP1307_PCTL1_BASE			0x19F28000
#define SDP1307_PCTL2_BASE			0x19FB8000

ENTRY(sdp1304_wait_for_die)
	movw	r0, #SDP1304_PCTL0_BASE & 0xFFFF
	movt	r0, #SDP1304_PCTL0_BASE >> 16
	movw	r1, #SDP1304_PCTL1_BASE & 0xFFFF
	movt	r1, #SDP1304_PCTL1_BASE >> 16
	mov	r2, #0x3
	.balignl 64, 0xe1a00000
	pli	[pc, #0]
	pli	[pc, #32]
	isb
	str	r2, [r0, #0x4]
	str	r2, [r1, #0x4]
1:	dsb
	.word	0xe320f003			/* wfi */
	b	1b
	.balignl 64, 0xe1a00000
ENDPROC(sdp1304_wait_for_die)

ENTRY(sdp1302_wait_for_die)
	movw	r0, #SDP1302_PCTL0_BASE & 0xFFFF
	movt	r0, #SDP1302_PCTL0_BASE >> 16
	movw	r1, #SDP1302_PCTL1_BASE & 0xFFFF
	movt	r1, #SDP1302_PCTL1_BASE >> 16
	mov	r2, #0x3
	.balignl 32, 0xe1a00000
	pli	[pc, #0]
	pli	[pc, #32]
	isb
	str	r2, [r0, #0x4]
	str	r2, [r1, #0x4]
1:	dsb
	.word	0xe320f003			/* wfi */
	b	1b
	.balignl 32, 0xe1a00000
ENDPROC(sdp1304_wait_for_die)

ENTRY(sdp1307_wait_for_die)
	movw	r0, #SDP1307_PCTL0_BASE & 0xFFFF
	movt	r0, #SDP1307_PCTL0_BASE >> 16
	movw	r1, #SDP1307_PCTL1_BASE & 0xFFFF
	movt	r1, #SDP1307_PCTL1_BASE >> 16
	movw	r2, #SDP1307_PCTL2_BASE & 0xFFFF
	movt	r2, #SDP1307_PCTL2_BASE >> 16
	mov	r3, #0x3
	.balignl 32, 0xe1a00000
	pli	[pc, #0]
	pli	[pc, #32]
	isb
	str	r3, [r0, #0x4]
	str	r3, [r1, #0x4]
	str	r3, [r2, #0x4]
1:	dsb
	.word	0xe320f003			/* wfi */
	b	1b
	.balignl 32, 0xe1a00000
ENDPROC(sdp1304_wait_for_die)

