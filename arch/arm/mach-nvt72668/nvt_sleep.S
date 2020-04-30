/*
 * Novatek NT72668 suspend/resume code.
 *
 */

#include <linux/linkage.h>
#include "mach/debug.h"
#include "mach/motherboard.h"

#if defined(CONFIG_SUSPEND)

	.macro	senduart,rd,rx
	str	\rd, [\rx, #NT72668_UARTx_RBR]
	.endm

	.macro	busyuart,rd,rx
1002:	ldr	\rd, [\rx, #NT72668_UARTx_LSR]
	and	\rd, \rd, #(NT72668_UART_LSR_TXEMPTY | NT72668_UART_LSR_THREMPTY)
	teq	\rd, #(NT72668_UART_LSR_TXEMPTY | NT72668_UART_LSR_THREMPTY)
	bne	1002b
	.endm

/*
 *
 * void nt72668_wait_for_die(void)
 *
 * This function performs the CPU power down sequence.
 */
.globl nt72668_wait_for_die
ENTRY(nt72668_wait_for_die)
	.balign 32, 0xe1a00000

#if defined(CONFIG_NVT_SUSPEND_MEM_CMP)
	@ Backup region1 image to another address space
	ldr	r0, =NT72668_DMA_CTRL_BASE
	ldr	r1, =NT72668_REGION1_BASE
	ldr	r2, =NT72668_REGION1_BACKUP_BASE
	ldr	r3, =NT72668_REGION1_BACKUP_SIZE
	ldr	r4, =NT72668_DMA_MODE_BLK_2_BLK
	ldr 	r5, =NT72668_DMA_ENABLE
	str	r1, [r0, #NT72668_DMA_SRC_BASE_OFFSET]	@ Set DMA Source
	str	r2, [r0, #NT72668_DMA_DEST_BASE_OFFSET]	@ Set DMA Destination
	str	r3, [r0, #NT72668_DMA_SIZE_BASE_OFFSET]	@ Set DMA byte count
	str	r4, [r0, #NT72668_DMA_MODE_BASE_OFFSET]	@ Set DMA mode
	str	r5, [r0, #NT72668_DMA_ENABLE_BASE_OFFSET]	@ DMA trigger
1:
	ldr	r1, [r0, #NT72668_DMA_INT_BASE_OFFSET]	@ Check DMA finish
	teq	r1, #NT72668_DMA_FINISH
	bne	1b
	@ Backup region2 image to another address space
	ldr	r1, =NT72668_REGION2_BASE
	ldr	r2, =NT72668_REGION2_BACKUP_BASE
	ldr	r3, =NT72668_REGION2_BACKUP_SIZE
	str	r1, [r0, #NT72668_DMA_SRC_BASE_OFFSET]	@ Set DMA Source
	str	r2, [r0, #NT72668_DMA_DEST_BASE_OFFSET]	@ Set DMA Destination
	str	r3, [r0, #NT72668_DMA_SIZE_BASE_OFFSET]	@ Set DMA byte count
	str	r4, [r0, #NT72668_DMA_MODE_BASE_OFFSET]	@ Set DMA mode
	str	r5, [r0, #NT72668_DMA_ENABLE_BASE_OFFSET]	@ DMA trigger
2:
	ldr	r1, [r0, #NT72668_DMA_INT_BASE_OFFSET]	@ Check DMA finish
	teq	r1, #NT72668_DMA_FINISH
	bne	2b
#endif

	/*
	 * mapping SRAM to CPU side
	 */
	movw    r1,#NT72668_IMVQ_REMAP_BASE_LOW
	movt    r1,#NT72668_IMVQ_REMAP_BASE_HIGH
	ldr     r2,[r1]
	bic     r2,r2,#(1 << 0)
	bic     r2,r2,#(1 << 1)
	str     r2,[r1]
	/*
	 * copy execution code to SRAM
	 */
	adr     r1,power_off_start
	movw    r0,#NT72668_IMVQ_SRAM_BASE_LOW
	movt    r0,#NT72668_IMVQ_SRAM_BASE_HIGH
	mov     r4,r0
	adr     r2,power_off_end
copy_1:
	ldr     r3,[r1],#0x4
	str     r3,[r0],#0x4
	cmp     r1,r2
	bne     copy_1
	mov     pc,r4
/*
 * set DRAM enter self-refresh mode and request micom to power off
 */
power_off_start:
	adr     r1,power_off_tbl_addr
	adr     r5,power_off_tbl_addr_end
	adr     r2,power_off_tbl_value
out_val:
	ldr     r3,[r2],#0x4
	ldr     r4,[r1],#0x4
	str     r3,[r4]
	cmp     r1,r5
	bne     out_val

	movw	r1,#NT72668_MAU0_STATUS_LOW
	movt	r1,#NT72668_MAU0_STATUS_HIGH
mau0_busy:
	ldr     r2,[r1]
	and     r2,r2,#0x1
	mov     r3,#0x1
	cmp     r2,r3
	bne     mau0_busy

	@ output successful message
	movw	r3,#NT72668_UART1_BASE_LOW
	movt	r3,#NT72668_UART1_BASE_HIGH
	mov	r0,#0x40
	str	r0,[r3]
	str	r0,[r3]
	mov	r0,#13
	str	r0,[r3]
	mov	r0,#10
	str	r0,[r3]

#if defined(CONFIG_NVT_SUSPEND_MICOM_UART)
	mov	r0,#0x0
	mov	r6,#0x3
	adr     r2,UART_power_off_tbl_end
	movw    r3,#NT72668_STBCUART_BASE_LOW
	movt    r3,#NT72668_STBCUART_BASE_HIGH
retry_loop:
	adr     r1,UART_power_off_tbl_start
out_val2:
	ldr     r4,[r1],#0x4
	busyuart r5, r3
	senduart r4, r3
	cmp	r1,r2
	bne	out_val2
	add	r0,r0,#0x1
	cmp     r0,r6
	bne     retry_loop
#endif
#if defined(CONFIG_NVT_SUSPEND_MICOM_IPC)
	adr     r1,ipc_tbl_addr_start
	adr     r2,ipc_tbl_value_start
#if defined(CONFIG_NVT_SUSPEND_IPC_AUTO_POWERON)
	ldr     r3,[r2],#0x4
	ldr     r4,[r1],#0x4
	str     r3,[r4]

	ldr     r3,[r2],#0x4
	ldr     r4,[r1],#0x4
	movw    r5,#0
	movt    r5,#0xffff
cont_1:
	ldr     r0,[r4]
	ands    r0,r0,r5
	cmp     r0,r3
	bne     cont_1
#endif
#if defined(CONFIG_NVT_SUSPEND_IPC_SLEEP)
	ldr     r3,[r2],#0x4
	ldr     r4,[r1],#0x4
	str     r3,[r4]
#endif
#endif
	isb
	dsb
loop:
	wfi
	b loop

power_off_tbl_addr:
	@ set DDR enter self-refresh mode
	.word 0xfd010000
	.word 0xfd0e0000
	.word 0xfd01014c
	.word 0xfd0e014c
	.word 0xfd010000
	.word 0xfd0e0000
	@ request micom to power off
#if defined(CONFIG_NVT_SUSPEND_MICOM_IPC)
	.word 0xfc040204
	.word 0xfc040204
	.word 0xfc040208
power_off_tbl_addr_end:
ipc_tbl_addr_start:
#if defined(CONFIG_NVT_SUSPEND_IPC_AUTO_POWERON)
	.word 0xfc040000
	.word 0xfc040004
#endif
#if defined(CONFIG_NVT_SUSPEND_IPC_SLEEP)
	.word 0xfc040000
#endif
#else
power_off_tbl_addr_end:
#endif
power_off_tbl_value:
	.word 0x00000005
	.word 0x00000005
	.word 0x003c00c0
	.word 0x003c00c0
	.word 0x8
	.word 0x8
#if defined(CONFIG_NVT_SUSPEND_MICOM_IPC)
	.word NT72668_IPC_KEY_PASS_0
	.word NT72668_IPC_KEY_PASS_1
	.word NT72668_IPC_KEY_PASS_2
ipc_tbl_value_start:
#if defined(CONFIG_NVT_SUSPEND_IPC_AUTO_POWERON)
	.word NT72668_IPC_AUTO_POWERON_REQ
	.word NT72668_IPC_AUTO_POWERON_RES
#endif
#if defined(CONFIG_NVT_SUSPEND_IPC_SLEEP)
	.word NT72668_IPC_SLEEP_REQ
#endif
#endif
#if defined(CONFIG_NVT_SUSPEND_MICOM_UART)
UART_power_off_tbl_start:
	.word 0xff
	.word 0xff
	.word MICOM_REQ_SLEEP
	.word 0x0
	.word 0x0
	.word 0x0
	.word 0x0
	.word 0x0
	.word MICOM_REQ_SLEEP
UART_power_off_tbl_end:
#endif
power_off_end:
ENDPROC(nt72668_wait_for_die)

ENTRY(save_nt72668_pmu)
	push	{r4, r8, r9, r10}
	@ Ignore:
	@	Count Enable Clear Register
	@	Software Increment Register
	@	Interrupt Enable Clear Register
	mrc     p15,0,r8,c9,c12,0	@ PMon: Control Register
  	bic     r1,r8,#1
   	mcr     p15,0,r1,c9,c12,0	@ disable counter updates from here
   	isb							@ 0b0 => PMCR<0>
   	mrc     p15,0,r9,c9,c12,3	@ PMon: Overflow Flag Status Reg
   	mrc     p15,0,r10,c9,c12,5	@ PMon: Event Counter Selection Reg
   	stm     r0!, {r8-r10}
   	ubfx	r9,r8,#11,#5		@ extract # of event counters, N
   	tst     r9, r9
   	beq     svpmu1

svpmu0:
	subs	r9,r9,#1	        @ decrement N
	mcr	p15,0,r9,c9,c12,5	@ PMon: select CounterN
	isb
	mrc	p15,0,r3,c9,c13,1	@ PMon: save Event Type register
	mrc	p15,0,r4,c9,c13,2	@ PMon: save Event Counter register
	stm	r0!, {r3,r4}
	bne	svpmu0

svpmu1:
	mrc    p15,0,r1,c9,c13,0	@ PMon: Cycle Count Register
	mrc    p15,0,r2,c9,c14,0	@ PMon: User Enable Register
	mrc    p15,0,r3,c9,c14,1	@ PMon: Interrupt Enable Set Reg
	mrc    p15,0,r4,c9,c12,1	@ PMon: Count Enable Set Register
	stm    r0!, {r1-r4}

	pop    {r4, r8, r9, r10}
	bx     lr
ENDPROC(save_nt72668_pmu)

ENTRY(restore_nt72668_pmu)
	push   {r4-r5, r8-r10, lr}
	@ NOTE: all counters disabled by PMCR<0> == 0 on reset

	@ Restore performance counters
	ldm    r0!,{r8-r10}		@ recover first block of PMon context
					@ (PMCR, PMOVSR, PMSELR)
	mov    r1, #0			@ generate register of all 0's
	mvn    r2, #0			@ generate register of all 1's
	mcr    p15,0,r2,c9,c14,2	@ disable all counter related interrupts
	mcr    p15,0,r2,c9,c12,3	@ clear all overflow flags
	isb

	ubfx    r12,r8,#11,#5		@ extract # of event counters, N (0-31)
	tst    r12, r12
	beq    rtpmu20
	mov    r3, r12			@ for N >0, generate a 2nd copy of N
	mov    r4, #1
	lsl    r4, r4, r3
	sub    r4, r4, #1		@ set bits<N-1:0> to all 1's

rtpmu2:
	subs   r3,r3,#1			@ decrement N
	mcr    p15,0,r3,c9,c12,5	@ select Event CounterN
	isb
	mrc    p15,0,r5,c9,c13,1   	@ read Event Type register
	bfc    r5,#0,#8
	mcr    p15,0,r5,c9,c13,1   	@ set Event Type to 0x0
	mcr    p15,0,r2,c9,c13,2 	@ set Event Counter to all 1's
	isb
	bne    rtpmu2

	mov    r3, #1
	bic    r5, r9, #1<<31
	mcr    p15,0,r5,c9,c12,1   	@ enable Event Counters
					@ (PMOVSR bits set)
	mcr    p15,0,r3,c9,c12,0   	@ set the PMCR global enable bit
	isb
	mcr    p15,0,r9,c9,c12,4   	@ set event count overflow bits
	isb
	mcr    p15,0,r4,c9,c12,2   	@ disable Event Counters

	@ restore the event counters
rtpmu10:
	subs   r12,r12,#1		@ decrement N
	mcr    p15,0,r12,c9,c12,5  	@ select Event CounterN
	isb
	ldm    r0!,{r3-r4}
	mcr    p15,0,r3,c9,c13,1	@ restore Event Type
	mcr    p15,0,r4,c9,c13,2	@ restore Event Counter
	isb
	bne    rtpmu10

rtpmu20:
	tst    r9, #0x80000000		@ check for cycle count overflow flag
	beq    rtpmu40
	mcr    p15,0,r2,c9,c13,0   	@ set Cycle Counter to all 1's
	isb
	mov    r3, #0x80000000
	mcr    p15,0,r3,c9,c12,1	@ enable the Cycle Counter
	isb

rtpmu30:
	mrc    p15,0,r4,c9,c12,3	@ check cycle count overflow now set
	movs   r4,r4				@ test bit<31>
	bpl    rtpmu30
	mcr    p15,0,r3,c9,c12,2	@ disable the Cycle Counter

rtpmu40:
	mcr    p15,0,r1,c9,c12,0	@ clear the PMCR global enable bit
	isb

	@ restore the remaining PMon registers
	ldm    r0!,{r1-r4}
	mcr    p15,0,r1,c9,c13,0	@ restore Cycle Count Register
	mcr    p15,0,r2,c9,c14,0	@ restore User Enable Register
	mcr    p15,0,r3,c9,c14,1	@ restore Interrupt Enable Set Reg
	mcr    p15,0,r4,c9,c12,1	@ restore Count Enable Set Register
	mcr    p15,0,r10,c9,c12,5	@ restore Event Counter Selection
	isb
	mcr    p15,0,r8,c9,c12,0	@ restore the PM Control Register
	isb

	pop    {r4-r5, r8-r10, pc}
ENDPROC(restore_nt72668_pmu)

/*
 * unsigned * copy_multi_words(volatile unsigned *dest, volatile unsigned *source, unsigned num_words)
 * r0: Destination start address
 * r1: Source start address
 * r2: Number of words to copy
 * Return value is updated destination pointer
 */
ENTRY(copy_multi_words)
	cmp    r2, #0
	beq    copy_multi_words1
copy_multi_words0:
	ldr    r3, [r1], #4
	str    r3, [r0], #4
	subs   r2, r2, #1
	bne    copy_multi_words0
copy_multi_words1:
	bx     lr
ENDPROC(copy_multi_words)

ENTRY(nt72668_cpu_resume)
	@ Novatek resume entry point

#if defined(CONFIG_NVT_SUSPEND_MEM_CMP)
@ compare region1 image with backup one
region1_compare:
	adr	r6, nt72668_mem1_compare
	ldr	r1, =NT72668_REGION1_BASE
	ldr	r2, =NT72668_REGION1_BACKUP_BASE
	ldr r5, =NT72668_REGION1_BACKUP_SIZE
	add	r5, r5, r2
cont_cmp_1:
	ldr     r3,[r1],#0x4
	ldr     r4,[r2],#0x4
	cmp 	r3,r4
	bne	cmp_failed_1
	cmp     r2,r5
	bne     cont_cmp_1
	mov	r0,#0
	b	cont_resume_1
cmp_failed_1:
	mov	r0,#1
cont_resume_1:
	str	r0,[r6]

@ compare region2 image with backup one
region2_compare:
	adr	r6, nt72668_mem2_compare
	ldr	r1, =NT72668_REGION2_BASE
	ldr	r2, =NT72668_REGION2_BACKUP_BASE
	ldr r5, =NT72668_REGION2_BACKUP_SIZE
	add	r5, r5, r2
cont_cmp_2:
	ldr     r3,[r1],#0x4
	ldr     r4,[r2],#0x4
	cmp 	r3,r4
	bne	cmp_failed_2
	cmp     r2,r5
	bne     cont_cmp_2
	mov	r0,#0
	b	cont_resume_2
cmp_failed_2:
	mov	r0,#1
cont_resume_2:
	str	r0,[r6]
#endif
	b	cpu_resume	@ jump to generic resume
#if defined(CONFIG_NVT_SUSPEND_MEM_CMP)
.globl nt72668_mem1_compare
nt72668_mem1_compare:
	.long	0               @ store memory compare result
.globl nt72668_mem2_compare
nt72668_mem2_compare:
	.long	0               @ store memory compare result
#endif
ENDPROC(nt72668_cpu_resume)
#endif
