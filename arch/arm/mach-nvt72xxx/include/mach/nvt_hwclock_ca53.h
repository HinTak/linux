#ifndef HWCLOCK_H
#define HWCLOCK_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif

#ifdef __KERNEL__


/*
 * We need this hack to replace tracing timestamps while dumping
 * events from early buffer to the ring buffer..
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
extern uint64_t __ca53_mul;

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
	return 0;
}

static inline unsigned long hwclock_get_va(void)
{
	return 0;
}

static inline u32 read_cntfrq(void)
{
	unsigned int freq;

	asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (freq));
	return freq;
}

static inline u64 read_cntpct(void)
{
	unsigned long long cval;

	asm volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r" (cval));
	return cval;
}

static inline u64 read_cntvct(void)
{
	unsigned long long cval;

	asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (cval));
	return cval;
}

static inline uint64_t hwclock_raw_ns(uint32_t *addr)
{
	u64 frq, cnt, ret;
	u32 tmp;

	frq = read_cntfrq();
	cnt = read_cntvct();

	/*10 ^ 16 /frq  */
#if 0
	if (__ca53_mul == 0)
		__ca53_mul = div64_u64(10000000000000000, frq);

	ret = div_u64_rem((cnt >> 32) * __ca53_mul, 10000000, &tmp) << 32;
	ret += div64_u64(((u64)tmp << 32) + (cnt & 0xFFFFFFFF) * __ca53_mul, 10000000);
#else
	/* 10^9 * 2 ^ 24 */
	if (__ca53_mul == 0)
		__ca53_mul = div64_u64(16777216000000000, frq);
	ret = ((cnt >> 32) * __ca53_mul) << 8;  /* (x >> 24) << 32  =>  x << 8 */
	ret += ((cnt & 0xFFFFFFFF) * __ca53_mul) >> 24;
#endif

	return ret;
}

static inline uint64_t hwclock_ns(uint32_t *addr)
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
/*
	if (gtime_ns > 10000000000)
		printk("[hwclock] hwclock_ns __hack_ns %llu __delta_ns %llu gtime_ns %llu\n", __hack_ns, __delta_ns, gtime_ns);
*/
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
 * Handle reset event.
 * On arm_global_timer init save timestamp to early_boot_time,
 * after reset this value will be accumulated.
 */
static inline void hwclock_early_boot_time(void)
{
	/* ca53 timer do not reset when kernel init*/
}

#endif /* HWCLOCK_H */
