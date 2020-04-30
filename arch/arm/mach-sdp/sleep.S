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
#define SDP1404_DDRA_MEMS_BASE	0x18418000
#define SDP1404_DDRB_MEMS_BASE	0x18438000
#define SDP1404_DDRC_MEMS_BASE	0x18458000
#define SDP1404_DDRA_UPHY_BASE	0x18428000
#define SDP1404_DDRB_UPHY_BASE	0x18448000
#define SDP1404_DDRC_UPHY_BASE	0x18468000
#define SDP1404_TSD_SRAM	0x18100800
#define SDP1406_DDRA_MEMS_BASE	0x00F10000
#define SDP1406_DDRB_MEMS_BASE	0x00F20000
#define SDP1406_DDRC_MEMS_BASE	0x00F30000
#define SDP1406_DDRA_UPHY_BASE	0x00F18000
#define SDP1406_DDRC_UPHY_BASE	0x00F38000
#define SDP1406_TSD_SRAM	0x00900800
#define SDP1406_DEBUG_POINT_BASE	0x0080059C
#define SDP1406_MICOM_BASE		0x00800480
#define SDP1406_MICOM_CMD0		0x00D3FFFF
#define SDP1406_MICOM_CMD1		0x00000000
#define SDP1406_MICOM_CMD2		0x000000D3

#define SDP1501_TSD_SRAM	0x00908000	/* TSD data memory */
#define SDP1501_MICOM_BASE	0x007c0400

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
ENDPROC(sdp1302_wait_for_die)

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
ENDPROC(sdp1307_wait_for_die)

ENTRY(sdp1404_wait_for_die)
	/* copy code to SRAM */
	movw	r0,	#SDP1404_TSD_SRAM & 0xFFFF
	movt	r0,	#SDP1404_TSD_SRAM >> 16

	adr	r1, sdp1404_ddr_selfrefresh_start
	adr	r2, sdp1404_ddr_selfrefresh_end
	sub	r2,	r2, r1
	mov	r7, r0
	
14040:
	ldr	r3, [r1], #0x4
	str	r3, [r0], #0x4
	subs	r2, r2, #0x4
	bne	14040b
		
	mov	r1, #0			@ set up for MCR
	mcr	p15, 0, r1, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r1, c7, c5, 0	@ invalidate icache
	/* icache off */
	mrc	p15, 0, r1, c1, c0, 0
	bic	r1, r1, #0x00001800	@ clear bit (ZI--)
	mcr	p15, 0, r1, c1, c0, 0
	isb
	dsb
	mov	pc, r7	/* jump to sram */

sdp1404_ddr_selfrefresh_start:
	movw	r0, #SDP1404_DDRA_MEMS_BASE & 0xFFFF
	movt	r0, #SDP1404_DDRA_MEMS_BASE >> 16
	movw	r1, #SDP1404_DDRB_MEMS_BASE & 0xFFFF
	movt	r1, #SDP1404_DDRB_MEMS_BASE >> 16
	movw	r2, #SDP1404_DDRC_MEMS_BASE & 0xFFFF
	movt	r2, #SDP1404_DDRC_MEMS_BASE >> 16
	movw	r3, #SDP1404_DDRA_UPHY_BASE & 0xFFFF
	movt	r3, #SDP1404_DDRA_UPHY_BASE >> 16
	movw	r4, #SDP1404_DDRB_UPHY_BASE & 0xFFFF
	movt	r4, #SDP1404_DDRB_UPHY_BASE >> 16
	movw	r5, #SDP1404_DDRC_UPHY_BASE & 0xFFFF
	movt	r5, #SDP1404_DDRC_UPHY_BASE >> 16

	mov	r6, #0xF
	mov	r7, #0x5
	mov	r8, #0x2
	mov	r9, #0x2

	ldr	r10, [r3, #0x120]
	bic	r10, r10, #0x10000000
	ldr	r11, [r4, #0x120]
	bic	r11, r11, #0x10000000

	//switch off AXI port
	str	r6, [r0, #0x108]
	str	r6, [r1, #0x108]
	str	r6, [r2, #0x108]
	
	mov     r6, #0x5
wait5us:
	subs    r6, r6, #0x1
	bne     wait5us
	
	//wait TRQ empty 
14041:
	ldr	r6, [r0, #0x2EC]
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x2EC]
	cmpeq	r6, #0x0
	ldreq	r6, [r2, #0x2EC]
	cmpeq	r6, #0x0
	bne	14041b

	//Change State of Main FSM(access --> IDLE)
	str	r7, [r0, #0x408]
	str	r7, [r1, #0x408]
	str	r7, [r2, #0x408]

	//wait IDLE
14042:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x0
	ldreq	r6, [r2, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x0
	bne	14042b
	
	mov	r6, #0x0
	str	r6, [r3, #0x19C]
	str	r6, [r4, #0x19C]


	//Change State of Main FSM(IDLE --> low power)
	str	r8, [r0, #0x408]
	str	r8, [r1, #0x408]

	//wait lowpower
14042:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x5
	ldreq	r6, [r1, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x5
	bne	14042b

	//off dynamic impedance calibration
	ldr	r9, [r3, #0x170]
	bic	r9, r9, #0x1
	str	r9, [r3, #0x170]
	ldr	r9, [r4, #0x170]
	bic	r9, r9, #0x1
	str	r9, [r4, #0x170]

	//wait 100us
	mov	r6, #0xA0
wait100us:
	subs	r6, r6, #0x1
	bne	wait100us

	//wait lowpower
lowcke:
	ldr	r6, [r0, #0x450]
	and	r6, r6, #0x3
	cmp	r6, #0x1
	ldreq	r6, [r1, #0x450]
	andeq	r6, r6, #0x3
	cmpeq	r6, #0x1
	bne	lowcke


	//Pad reciever turn off
	str	r10, [r3, #0x120]
	str	r11, [r4, #0x120]

	//Internal VREF turn off
	ldr	r6, [r3, #0x1C8]
	bic	r6, r6, #0x4
	str	r6, [r3, #0x1C8]
	ldr	r6, [r4, #0x1C8]
	bic	r6, r6, #0x4
	str	r6, [r4, #0x1C8]

1:	dsb
	isb

	movw	r0, #0x0
	movt	r0, #0x1125
	
	ldr	r1, [r0, #0xDA0]
	orr	r1, r1, #0x20
	str	r1, [r0, #0xDA0]

	.word	0xe320f003			/* wfi */
	b	1b
sdp1404_ddr_selfrefresh_end:

ENDPROC(sdp1404_wait_for_die)

ENTRY(sdp1406uhd_wait_for_die)
	/* copy code to TSD SRAM */
	movw	r0,	#SDP1406_TSD_SRAM & 0xFFFF
	movt	r0,	#SDP1406_TSD_SRAM >> 16

	adr	r1, sdp1406uhd_ddr_selfrefresh_start
	adr	r2, sdp1406uhd_ddr_selfrefresh_end
	sub	r2,	r2, r1
	mov	r7, r0
	
140610:
	ldr	r3, [r1], #0x4
	str	r3, [r0], #0x4
	subs	r2, r2, #0x4
	bne	140610b
		
	mov	r1, #0			@ set up for MCR
	mcr	p15, 0, r1, c8, c7, 0	@ invalidate TLBs
	isb
	mcr	p15, 0, r1, c7, c5, 0	@ invalidate icache
	isb
	mcr p15, 0, r1, c7, c5, 6	@ invalidate BTB
	isb
	/* icache off */
	mrc	p15, 0, r1, c1, c0, 0
	bic	r1, r1, #0x00001800	@ clear bit (ZI--)
	mcr	p15, 0, r1, c1, c0, 0
	isb
	dsb
	mov	pc, r7	/* jump to TSD SRAM */

	/* this code is excuted in TSD SRAM */
sdp1406uhd_ddr_selfrefresh_start:
	movw	r0, #SDP1406_DDRA_MEMS_BASE & 0xFFFF
	movt	r0, #SDP1406_DDRA_MEMS_BASE >> 16
	movw	r1, #SDP1406_DDRB_MEMS_BASE & 0xFFFF
	movt	r1, #SDP1406_DDRB_MEMS_BASE >> 16
	movw	r2, #SDP1406_DDRC_MEMS_BASE & 0xFFFF
	movt	r2, #SDP1406_DDRC_MEMS_BASE >> 16
	movw	r3, #SDP1406_DDRA_UPHY_BASE & 0xFFFF
	movt	r3, #SDP1406_DDRA_UPHY_BASE >> 16
//	movw	r4, #SDP1406_DDRB_UPHY_BASE & 0xFFFF
//	movt	r4, #SDP1406_DDRB_UPHY_BASE >> 16
	movw	r5, #SDP1406_DDRC_UPHY_BASE & 0xFFFF
	movt	r5, #SDP1406_DDRC_UPHY_BASE >> 16
	movw	r10, #SDP1406_DEBUG_POINT_BASE & 0xFFFF
	movt	r10, #SDP1406_DEBUG_POINT_BASE >> 16

	mov	r6, #0x7
	mov	r7, #0x5
	mov	r8, #0x2 @ fsm idle -> low power
	mov	r9, #0x2 @ dynamic impedence calib off

	/* switch off AXI port */
	str	r6, [r0, #0x108]
	str	r6, [r1, #0x108]
	str	r6, [r2, #0x108]

	// write debug point
	mov r11, #0xB1
	str r11, [r10]

	mov r6, #0x5
wait5us_1406:
	subs r6, r6, #0x1
	bne wait5us_1406
	
	/* wait TRQ empty */
140611:
	ldr	r6, [r0, #0x2EC]
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x2EC]
	cmpeq	r6, #0x0
	ldreq	r6, [r2, #0x2EC]
	cmpeq	r6, #0x0
	bne	140611b

	/* Change State of Main FSM(access -> IDLE) */
	str	r7, [r0, #0x408]
	str	r7, [r1, #0x408]
	str	r7, [r2, #0x408]

	// write debug point
	mov r11, #0xB2
	str r11, [r10]

	/* wait for IDLE */
140612:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x0
	ldreq	r6, [r2, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x0
	bne	140612b

	mov r6, #0x0
	str r6, [r3, #0x19C]
	str r6, [r5, #0x19C]
	
	/* Change State of Main FSM(IDLE -> low power) */
	str	r8, [r0, #0x408]
//	str	r8, [r1, #0x408]
	str	r8, [r2, #0x408]

	// write debug point
	mov r11, #0xB3
	str r11, [r10]

	/* wait for lowpower */
140613:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x5
//	ldreq	r6, [r1, #0x40C]
//	andeq	r6, r6, #0x7
//	cmpeq	r6, #0x5
	ldreq	r6, [r2, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x5
	bne	140613b

	/* off dynamic impedance calibration */
	ldr	r9, [r3, #0x170]
	bic	r9, r9, #0x1
	str	r9, [r3, #0x170]
	ldr	r9, [r5, #0x170]
	bic	r9, r9, #0x1
	str	r9, [r5, #0x170]

	//wait 100us
	mov	r6, #0xA0
wait100us_1406:
	subs	r6, r6, #0x1
	bne	wait100us_1406

	//wait lowpower
lowcke1406:
	ldr	r6, [r0, #0x450]
	and	r6, r6, #0x3
	cmp	r6, #0x1
	ldreq	r6, [r2, #0x450]
	andeq	r6, r6, #0x3
	cmpeq	r6, #0x1
	bne	lowcke1406

	// write debug point
	mov r11, #0xB4
	str r11, [r10]

	/* wait over 100us */
	mov	r6, #0xA0
140614:
	subs	r6, r6, #0x1
	bne	140614b

	// write debug point
	mov r11, #0xB5
	str r11, [r10]

	/* PHY_PAD_CTRL receiver_en to 0 */
	ldr	r10, [r3, #0x120]
	bic	r10, r10, #0x10000000
	str	r10, [r3, #0x120]
	
	ldr	r11, [r5, #0x120]
	bic	r11, r11, #0x10000000
	str	r11, [r5, #0x120]

	/* Internal VREF turn off */
	ldr	r6, [r3, #0x1C8]
	bic	r6, r6, #0x4
	str	r6, [r3, #0x1C8]
	ldr	r6, [r5, #0x1C8]
	bic	r6, r6, #0x4
	str	r6, [r5, #0x1C8]

	/* send power off to micom */
	movw	r0, #SDP1406_MICOM_BASE & 0xFFFF
	movt	r0, #SDP1406_MICOM_BASE >> 16
	movw	r3, #SDP1406_MICOM_CMD0 & 0xFFFF
	movt	r3, #SDP1406_MICOM_CMD0 >> 16
	movw	r4, #SDP1406_MICOM_CMD1 & 0xFFFF
	movt	r4, #SDP1406_MICOM_CMD1 >> 16
	movw	r5, #SDP1406_MICOM_CMD2 & 0xFFFF
	movt	r5, #SDP1406_MICOM_CMD2 >> 16

// check micom free
mwait1406:
	ldr r1, [r0, #0x40]
	and r1, r1, #0x1
	cmp r1, #0x1
	beq mwait1406
	
	// write size
	mov r2, #0x9
	str r2, [r0]
	
	// write data
	str r3, [r0, #0x4]
	str r4, [r0, #0x8]
	str r5, [r0, #0xc]
	
	// interrupt to sub-micom
	mov r6, #0x1
	str r6, [r0, #0x40]

// EDID workaround
	movw	r0, #0x800590 & 0xFFFF
	movt	r0, #0x800590 >> 16
	movw	r2, #0x580500 & 0xFFFF
	movt	r2, #0x580500 >> 16
	
	// wait micom ack (0x800590 = 1)
mwait140601:
	ldr r1, [r0]
	cmp r1, #0x1
	bne mwait140601

#if defined(CONFIG_ARCH_SDP1406) || defined(CONFIG_ARCH_SDP1406FHD)
	/* XXX: see drivers/misc/sdp_messagebox.c, same logic implemented in there. */
	/* only do on w/o boston board */
	ldr	r0, =0x00800508	/* P12.6, 0=boston, 1=non-boston */
	ldr	r0, [r0]
	cmp	r0, #0x1
	bne	11f

	// skip EDID problem board
	movw r5, #0x00F00000 & 0xFFFF
	movt r5, #0x00F00000 >> 16
	ldr r3, [r5]
	and r3, r3, #0x10000
	cmp r3, #0x10000
	beq 1f

	// EDID read start toggle reset [5] ->1
	ldr r3, [r2]
	orr r3, r3, #0x20
	str r3, [r2]

11:
	// send to micom (0x800590 = 0)
#endif
	mov r4, #0x0
	str r4, [r0]
// end of EDID workaround

1:	dsb

	.word	0xe320f003			/* wfi */
	b	1b
	
	.ltorg

sdp1406uhd_ddr_selfrefresh_end:

ENDPROC(sdp1406uhd_wait_for_die)

ENTRY(sdp1406fhd_wait_for_die)
	/* master reject */
	mov r6, #0xe00000
	add r4, r6, #0x1000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x1100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x2000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x2100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x2200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x3000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x3100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x4200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x6000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	mov r6, #0xe10000
	add r4, r6, #0x0
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x1000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x1100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x3000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x3100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x3200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x4000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x4100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x4200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa300
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa400
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xa500
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5300
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5400
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x5500
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x7000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x7100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x8000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x8100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x8200
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x8300
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x8400
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0xb000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x2000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	add r4, r6, #0x2100
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	mov r6, #0xb60000
	add r4, r6, #0x1000
	ldr r5, [r4, #0x34]
	orr r5, r5, #0x10
	str r5, [r4, #0x34]

	/* cpu prevent */
	/* XXX0 prevent settings for cpu 
	0xE04120[1:0] = 0                
	0xE04110 = 0x00f200f0            
	0xE04114 = 0x00790079            
	0xE04118 = 0x00900080            
	0xE0411c = 0x005c0058            
	0xE04120[1:0] = 3                
	*/                               
	mov     r4, #0xe00000            
	add     r4, r4, #0x4100          
	                                 
	ldr     r5, [r4, #0x20]          
	bic     r5, r5, #3               
	str     r5, [r4, #0x20]          
	                                 
	movw    r5, #0x00f0              
	movt    r5, #0x00f2              
	str     r5, [r4, #0x10]          
	                                 
	movw    r5, #0x0079              
	movt    r5, #0x0079              
	str     r5, [r4, #0x14]          
	                                 
	movw    r5, #0x0080              
	movt    r5, #0x0090              
	str     r5, [r4, #0x18]          
	                                 
	movw    r5, #0x0058              
	movt    r5, #0x005c              
	str     r5, [r4, #0x1c]          
	                                 
	ldr     r5, [r4, #0x20]          
	orr     r5, r5, #3               
	str     r5, [r4, #0x20] 

	/* copy code to TSD SRAM */
	movw	r0,	#SDP1406_TSD_SRAM & 0xFFFF
	movt	r0,	#SDP1406_TSD_SRAM >> 16

	adr	r1, sdp1406fhd_ddr_selfrefresh_start
	adr	r2, sdp1406fhd_ddr_selfrefresh_end
	sub	r2,	r2, r1
	mov	r7, r0
	
141520:
	ldr	r3, [r1], #0x4
	str	r3, [r0], #0x4
	subs	r2, r2, #0x4
	bne	141520b
		
	mov	r1, #0			@ set up for MCR
	mcr	p15, 0, r1, c8, c7, 0	@ invalidate TLBs
	isb
	mcr	p15, 0, r1, c7, c5, 0	@ invalidate icache
	isb
	mcr p15, 0, r1, c7, c5, 6	@ invalidate BTB
	isb
	/* icache off */
	mrc	p15, 0, r1, c1, c0, 0
	bic	r1, r1, #0x00001800	@ clear bit (ZI--)
	mcr	p15, 0, r1, c1, c0, 0
	isb
	dsb
	mov	pc, r7	/* jump to TSD SRAM */

	/* this code is excuted in TSD SRAM */
sdp1406fhd_ddr_selfrefresh_start:
	movw	r0, #SDP1406_DDRA_MEMS_BASE & 0xFFFF
	movt	r0, #SDP1406_DDRA_MEMS_BASE >> 16
	movw	r1, #SDP1406_DDRB_MEMS_BASE & 0xFFFF
	movt	r1, #SDP1406_DDRB_MEMS_BASE >> 16
//	movw	r2, #SDP1406_DDRC_MEMS_BASE & 0xFFFF
//	movt	r2, #SDP1406_DDRC_MEMS_BASE >> 16
	movw	r3, #SDP1406_DDRA_UPHY_BASE & 0xFFFF
	movt	r3, #SDP1406_DDRA_UPHY_BASE >> 16
//	movw	r4, #SDP1406_DDRB_UPHY_BASE & 0xFFFF
//	movt	r4, #SDP1406_DDRB_UPHY_BASE >> 16
//	movw	r5, #SDP1406_DDRC_UPHY_BASE & 0xFFFF
//	movt	r5, #SDP1406_DDRC_UPHY_BASE >> 16
	movw	r10, #SDP1406_DEBUG_POINT_BASE & 0xFFFF
	movt	r10, #SDP1406_DEBUG_POINT_BASE >> 16

	mov	r6, #0x7 @ axi switch off
	mov	r7, #0x5 @ fsm access -> idle
	mov	r8, #0x2 @ fsm idle -> low power
	mov	r9, #0x2 @ dynamic impedence calib off

	/* switch off AXI port */
	str	r6, [r0, #0x108]
	str	r6, [r1, #0x108]
//	str	r6, [r2, #0x108]

	// write debug point
	mov r11, #0xC1
	str r11, [r10]
	
	mov r6, #0x5
wait5us_1415:
	subs r6, r6, #0x1
	bne wait5us_1415
	
	/* wait TRQ empty */
141521:
	ldr	r6, [r0, #0x2EC]
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x2EC]
	cmpeq	r6, #0x0
//	ldreq	r6, [r2, #0x2EC]
//	cmpeq	r6, #0x0
	bne	141521b

	/* Change State of Main FSM(access -> IDLE) */
	str	r7, [r0, #0x408]
	str	r7, [r1, #0x408]
//	str	r7, [r2, #0x408]

	// write debug point
	mov r11, #0xC2
	str r11, [r10]

	/* wait for IDLE */
141522:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x0
	ldreq	r6, [r1, #0x40C]
	andeq	r6, r6, #0x7
	cmpeq	r6, #0x0
//	ldreq	r6, [r2, #0x40C]
//	andeq	r6, r6, #0x7
//	cmpeq	r6, #0x0
	bne	141522b

	mov r6, #0x0
	str r6, [r3, #0x19C]
//	str r6, [r5, #0x19C]
	
	/* Change State of Main FSM(IDLE -> low power) */
	str	r8, [r0, #0x408]
//	str	r8, [r1, #0x408]
//	str	r8, [r2, #0x408]

	// write debug point
	mov r11, #0xC3
	str r11, [r10]

	/* wait for lowpower */
141523:
	ldr	r6, [r0, #0x40C]
	and	r6, r6, #0x7
	cmp	r6, #0x5
//	ldreq	r6, [r1, #0x40C]
//	andeq	r6, r6, #0x7
//	cmpeq	r6, #0x5
//	ldreq	r6, [r2, #0x40C]
//	andeq	r6, r6, #0x7
//	cmpeq	r6, #0x5
	bne	141523b

	/* off dynamic impedance calibration */
	ldr	r9, [r3, #0x170]
	bic	r9, r9, #0x1
	str	r9, [r3, #0x170]

	//wait 100us
	mov	r6, #0xA0
wait100us_1415:
	subs	r6, r6, #0x1
	bne	wait100us_1415
	
	//wait lowpower
lowcke1415:
	ldr	r6, [r0, #0x450]
	and	r6, r6, #0x3
	cmp	r6, #0x1
	bne	lowcke1415
	
	// write debug point
	mov r11, #0xC4
	str r11, [r10]

	/* wait over 100us */
	mov	r6, #0xA0
141524:
	subs	r6, r6, #0x1
	bne	141524b

	// write debug point
	mov r11, #0xC5
	str r11, [r10]

	/* PHY_PAD_CTRL receiver_en to 0 */
	ldr	r10, [r3, #0x120]
	bic	r10, r10, #0x10000000
	str	r10, [r3, #0x120]
	
//	ldr	r11, [r5, #0x120]
//	bic	r11, r11, #0x10000000
//	str	r11, [r5, #0x120]

	/* Internal VREF turn off */
	ldr	r6, [r3, #0x1C8]
	bic	r6, r6, #0x4
	str	r6, [r3, #0x1C8]
//	str	r6, [r5, #0x1C8]

	/* send power off to micom */
	movw	r0, #SDP1406_MICOM_BASE & 0xFFFF
	movt	r0, #SDP1406_MICOM_BASE >> 16
	movw	r3, #SDP1406_MICOM_CMD0 & 0xFFFF
	movt	r3, #SDP1406_MICOM_CMD0 >> 16
	movw	r4, #SDP1406_MICOM_CMD1 & 0xFFFF
	movt	r4, #SDP1406_MICOM_CMD1 >> 16
	movw	r5, #SDP1406_MICOM_CMD2 & 0xFFFF
	movt	r5, #SDP1406_MICOM_CMD2 >> 16

// check micom free
mwait1415:
	ldr r1, [r0, #0x40]
	and r1, r1, #0x1
	cmp r1, #0x1
	beq mwait1415
	
	// write size
	mov r2, #0x9
	str r2, [r0]
	
	// write data
	str r3, [r0, #0x4]
	str r4, [r0, #0x8]
	str r5, [r0, #0xc]
	
	// interrupt to sub-micom
	mov r6, #0x1
	str r6, [r0, #0x40]

// EDID workaround
	movw	r0, #0x800590 & 0xFFFF
	movt	r0, #0x800590 >> 16
	movw	r2, #0x580500 & 0xFFFF
	movt	r2, #0x580500 >> 16
	
	// wait micom ack (0x800590 = 1)
mwait141501:
	ldr r1, [r0]
	cmp r1, #0x1
	bne mwait141501

	// EDID read start toggle reset [5] -> 1
	ldr r3, [r2]
	orr r3, r3, #0x20
	str r3, [r2]

	// send to micom (0x800590 = 0)
	mov r4, #0x0
	str r4, [r0]
// end of EDID workaround

1:	dsb

	.word	0xe320f003			/* wfi */
	b	1b
sdp1406fhd_ddr_selfrefresh_end:

ENDPROC(sdp1406fhd_wait_for_die)
.ltorg

/****************************************
 * SDP1501 Jazz-M
 ****************************************/
	.macro	writel, base, offset, val, tmp
	ldr	\tmp, =\val
	str	\tmp, [\base, \offset]
	.endm


ENTRY(sdp1501_wait_for_die)
	mov	r5,	#0x1f
	b	sdp15x1_wait_for_die
ENDPROC(sdp1501_wait_for_die)

ENTRY(sdp1531_wait_for_die)
	mov	r5,	#0x1c
	b	sdp15x1_wait_for_die
ENDPROC(sdp1531_wait_for_die)

ENTRY(sdp15x1_wait_for_die)
sdp15x1_wait_for_die:

	@ sdp15x1 TSD clock mask off, this is masked on iot mode.
	ldr	r0, =0x005c073c
	ldr	r1, [r0]
	orr	r1, r1, #(1<<4)
	str	r1, [r0]

	ldr	r0, =(10000)	@ 10~100usec wait, approx 1ns per loop at 1Ghz, 10ns at 100Mhz
1:	subs	r0, r0, #1
	bgt	1b

	/* copy code to TSD SRAM */
	ldr	r0, =SDP1501_TSD_SRAM

	adr	r1, sdp1501_power_off
	adr	r2, sdp1501_power_off_end
	mov	r4, r0

sdp1501_copy_code:
	cmp	r1, r2
	bxge	r4		@ jump

	ldr	r3, [r1], #0x4
	str	r3, [r0], #0x4

	b	sdp1501_copy_code
.ltorg

/* XXX: executed in TSD SRAM,
   should be PIC + only 'word' access is permitted.
   arguments: r5 = channel mask */
sdp1501_power_off:
	dsb
	isb

	/* turn off AXI ports */
	ands	r6, r5, #1
	ldrne	r0, =0xf18000
	blne	sdp1501_power_off_ddr
	
	ands	r6, r5, #2
	ldrne	r0, =0xf28000
	blne	sdp1501_power_off_ddr

	ands	r6, r5, #4
	ldrne	r0, =0xf38000
	blne	sdp1501_power_off_ddr

	ands	r6, r5, #8
	ldrne	r0, =0xf48000
	blne	sdp1501_power_off_ddr

	ands	r6, r5, #0x10
	ldrne	r0, =0xf58000
	blne	sdp1501_power_off_ddr

	/* send power-off command to micom */
	ldr	r0, =SDP1501_MICOM_BASE

	@ flush pending	
1:	ldr	r1, [r0, #0xc0]
	cmp	r1, #1
	beq	1b

retry_micom_request:
	@ send command
	writel	r0, #0x80, 0x00000009, r1	@ cmd length = 9
	writel	r0, #0x84, 0x00D3FFFF, r1
	writel	r0, #0x88, 0x00000000, r1
	writel	r0, #0x8c, 0x000000D3, r1

	@ set pending & wait
	writel	r0, #0xc0, 0x00000001, r1	@ cm0 side pending set
2:	ldr	r1, [r0, #0xc0]
	cmp	r1, #1
	beq	2b

	@ wait for ack
	ldr	r3, =0x80000	@ timeout = 524,288 loops, approx 100ns per a loop?
3:	ldr	r1, [r0, #0x40]			@ wait for cpu side pending set
	cmp	r1, #1
	bne	10f
	subs	r3, r3, #1
	ble	retry_micom_request
	b	3b

	@ skip checking ack data body
10:
	mov	r1, #1				@ cpu side pending clear
	str	r1, [r0, #0x44]

	@ wait for die
20:	dsb
	wfi
	b	20b

	/* r0: = MEMS base address */
sdp1501_power_off_ddr:
	/* switch off AXI port */
	writel	r0, #0x108, 0x00000007, r2
4:	ldr	r1, [r0, #0x2EC]
	cmp	r1, #0x00
	bne	4b

	/* memory state need to be stabilized before entering low-power mode:
		stop control-update, dynamic-training, and refresh */

	/* stop 'control-update' : 0x424 bit[3:0] := 0x8 */
	ldr	r1, [r0, #0x424]
	bic	r1, r1, #0xf
	orr	r1, r1, #0x8
	str	r1, [r0, #0x424]
	
	/* stop 'dynamic-training' : 0x46c := 0x0 */
	writel	r0, #0x46c, #0, r2

	/* wait 0.5msec */
	ldr     r2, =12288      @ cycles for 500usec, 500 * 24576000 / 1000000
	ldr	r1, =0x790000	@ cpu register
	ldr	r3, [r1, #0xc4]	@ r3 := start counter
7:	ldr	r4, [r1, #0xc4]	@ r4 := current counter
	sub	r4, r4, r3
	cmp	r4, r2
	blt	7b

	/* IDLE state */
	writel	r0, #0x408, 0x00000005, r2
5:	ldr	r1, [r0, #0x40C]
	ands	r1, r1, #0x0F
	bne	5b

	/* give 6 refresh cycles (1900nsec / cycle) */
	writel	r0, #0x42c, #5, r2
	ldr	r3, [r0, #0x400]
	orr	r1, r3, #0x200
	/* give refresh cycles and enter Lower-power state */
	mov	r2, #0x2
	str	r1, [r0, #0x400]
	str	r2, [r0, #0x408]
	dsb

	ldr	r3, =0x0380000F	@ mask
	ldr	r4, =0x03800008	@ value
6:	ldr	r1, [r0, #0x40C]
	and	r1, r1, r3
	cmp	r1, r4
	bne	6b
	
	mov	pc, lr
	
.ltorg
sdp1501_power_off_end:
ENDPROC(sdp15x1_wait_for_die)
