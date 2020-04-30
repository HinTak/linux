#ifndef HWCLOCK_H
#define HWCLOCK_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/io.h>
#endif

#include "clk.h"
#include "nvt_hwclock_regs.h"
#ifdef __KERNEL__

#define _MPLL_GetData(u8Offset) \
({	\
 (*((volatile unsigned long *)((unsigned long)MPLL_REG_VA +((u8Offset) * 4)))); \
 })


/*
 * We need this hack to replace tracing timestamps while dumping
 * events from early buffer to the ring buffer.
 */
extern uint64_t __hack_ns;

/*
 * On suspend/resume HW clock is reset to zero.
 * Kernel tracing (kernel/trace) goes nuts about that timestamps
 * from the past, so all we can do from the software side is to
 * accumulate previous timings.
 */
extern uint64_t __delta_ns;

/*
* Global timer enable by onboot but reset to zero by kerenl...
* so need save time when reset and accumulate the timings.
*/
extern uint64_t __early_boot_ns;

extern uint32_t nvt_clk_vmap;
extern unsigned long periph_clk;

static inline void __hwclock_set(uint64_t ns)
{
	__hack_ns = ns;
}

static inline void __hwclock_reset(void)
{
	__hack_ns = 0;
}
#endif

static inline unsigned long hwclock_get_pa(void)
{
	return HW_CLOCK_PA;
}

static inline unsigned long hwclock_get_va(void)
{
	if (nvt_clk_vmap == 0)
		return HW_CLOCK_VA;
	else
		return __HW_CLOCK_VA;
}

static inline unsigned long hwclock_get_freq(void)
{
	unsigned long retVal = 0;

	if (periph_clk == 0){
		/**
		 * Ref arm/arch/mach-nvt72668/clk.c
		 * Get ratio of ARM PLL
		 */
		retVal = (_MPLL_GetData(EN_MPLL_OFF_ARM));
		retVal |= (_MPLL_GetData(EN_MPLL_OFF_ARM + 1) << 8);
		retVal |= (_MPLL_GetData(EN_MPLL_OFF_ARM + 2) << 16);

		retVal *= 12;
		retVal += ((1UL << 17) - 1);
		retVal >>= 17;

		/**
		 * arm_clk *=8
		 * arm_clk *=1000000                                       //MHz
		 * periph_clk = arm_clk/(_PERIPH_CLK_SEL + 1) = arm_clk/8
		 * periph_clk = periph_clk/1000                            //KHz
		*/
		periph_clk = retVal*1000;
	}
	return periph_clk;
}

static inline uint64_t hwclock_raw_ns(volatile uint32_t *addr)
{
	uint32_t value_tmp, value_h, value_l;
	uint64_t res;

	do {
		value_tmp = addr[1];
		value_l = addr[0];
		value_h = addr[1];
	} while (value_h != value_tmp);

	res = (((uint64_t)value_h << 32) | value_l)*1000000;
	do_div(res,hwclock_get_freq());

	return res;
}

static inline uint64_t hwclock_ns(volatile uint32_t *addr)
{
	uint64_t gtime_ns;
#ifdef __KERNEL__
	if (__hack_ns)
		return __hack_ns;
#endif
	gtime_ns = hwclock_raw_ns(addr);

#ifdef __KERNEL__
	gtime_ns += __delta_ns;
#endif

	if(!__delta_ns)
		gtime_ns += __early_boot_ns;

	return gtime_ns;
}

#ifdef __KERNEL__
/*
 * Handle suspend event. On suspend save current timestamp to delta,
 * on resume this value will be accumulated.
 */
static inline void hwclock_suspend(void)
{
	__delta_ns = hwclock_ns((uint32_t *)hwclock_get_va());
}
#endif

/*
 * Handle reset event. On arm_global_timer init save timestamp to early_boot_time,
 * after reset this value will be accumulated.
 */
static inline void hwclock_early_boot_time(void)
{
	__early_boot_ns = hwclock_ns((uint32_t *)hwclock_get_va());
}

#endif /* HWCLOCK_H */
