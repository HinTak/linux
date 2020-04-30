#ifndef HWCLOCK_H
#define HWCLOCK_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif

#include "mtk_hwclock_regs.h"

/* Some definitions only for kernel space */
#ifdef __KERNEL__

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

extern uint32_t mtk_clk_vmap;


static inline void __hwclock_set(uint64_t ns)
{
	__hack_ns = ns;
}

static inline void __hwclock_reset(void)
{
	__hack_ns = 0;
}

#endif /* __KERNEL__ */

static inline unsigned long hwclock_get_pa(void)
{
	return HW_CLOCK_PA;
}

static inline unsigned long hwclock_get_va(void)
{
	if (mtk_clk_vmap == 0)
		return HW_CLOCK_VA;
	else
		return __HW_CLOCK_VA;
}


static inline uint64_t hwclock_raw_ns(volatile uint32_t *addr)
{
	uint32_t vH, vL;
	uint64_t res;

	do {
		vL = addr[0];
		vH = addr[1];
	} while (0);

	res = (((uint64_t)vH << 32) | (uint64_t)vL) * 1000;
	do_div(res, HW_CLOCK_FREQ_M);

	return res;
}

static inline uint64_t hwclock_ns(volatile uint32_t *addr)
{
	uint64_t res;

#ifdef __KERNEL__
	if (__hack_ns)
		return __hack_ns;
#endif

	res = hwclock_raw_ns(addr);

#ifdef __KERNEL__
	/* Catch up time */
	res += __delta_ns;
#endif

	return res;
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

#endif /* HWCLOCK_H */
