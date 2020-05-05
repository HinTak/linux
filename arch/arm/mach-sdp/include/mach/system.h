/*
 * arch/arm/mach-sdp/include/mach/system.h
 *
 * Copyright 2010 Samsung Electronics co.
 * Author: tukho.kim@samsung.com
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/io.h>
#include <plat/sdp_irq.h>

#if defined(CONFIG_USE_EXT_GIC)
extern unsigned int gic_bank_offset;
#else
static const unsigned int gic_bank_offset = 0;
#endif

static inline void arch_idle_poll_legacy(void)
{
	SDP_INTR_REG_T * const p_sdp_intc_reg = (SDP_INTR_REG_T*)SDP_INTC_BASE0; 

	while (!p_sdp_intc_reg->pending) {
	}
}

static inline void arch_idle_poll_gic(void)
{
	int offset = smp_processor_id() * gic_bank_offset;
	volatile u32* gic_pending_reg1 = (volatile u32*)(VA_GIC_DIST_BASE + offset + 0x200);
	volatile u32* gic_pending_reg2 = (volatile u32*)(VA_GIC_DIST_BASE + offset + 0x204); 
	volatile u32* gic_pending_reg3 = (volatile u32*)(VA_GIC_DIST_BASE + offset + 0x208); 
	volatile u32* gic_pending_reg4 = (volatile u32*)(VA_GIC_DIST_BASE + offset + 0x20C); 
	
	while (!gic_pending_reg1 && !gic_pending_reg2 && !gic_pending_reg3 && !gic_pending_reg4) {
	}
}

static inline void arch_idle_poll(void)
{
#if defined(CONFIG_ARM_GIC)
	arch_idle_poll_gic();
#else
	arch_idle_poll_legacy();
#endif
}

static inline void arch_idle(void)
{
#if defined(CONFIG_POLL_INTR_PEN)
	arch_idle_poll()
#else
	cpu_do_idle();
#endif
}

static inline void arch_reset(char mode, const char *cmd)	 //????
//static inline void arch_reset(char mode)
{
        /* use the watchdog timer reset to reset the processor */

        /* (at this point, MMU is shut down, so we use physical addrs) */
        volatile unsigned long *prWTCON = (unsigned long*) (PA_WDT_BASE + 0x00);
        volatile unsigned long *prWTDAT = (unsigned long*) (PA_WDT_BASE + 0x04);
        volatile unsigned long *prWTCNT = (unsigned long*) (PA_WDT_BASE + 0x08);

        /* set the countdown timer to a small value before enableing WDT */
        *prWTDAT = 0x00000100;
        *prWTCNT = 0x00000100;

        /* enable the watchdog timer */
        *prWTCON = 0x00008021;

        /* machine should reboot..... */
        mdelay(5000);
        panic("Watchdog timer reset failed!\n");
        printk(" Jump to address 0 \n");
        cpu_reset(0);
}

/**
  * @fn sdp_get_mem_cfg
  * @brief Get Kernel and System Memory Size for each DDR bus
  * @remarks	
  * @param nType [in]	0:Kernel A Size, 1:System A Size, 2:Kernel B Size, 3:System B Size, 4:Kernel C Size, 5:System C Size
  * @return	memory size(MB). if ocuur error, return -1
  */
int sdp_get_mem_cfg(int nType);

#endif

