#ifndef HWCLOCK_REGS_H
#define HWCLOCK_REGS_H

#if defined(CONFIG_ARCH_SDP1501)
	#define HW_CLOCK_PA   0x007900C0
	#define HW_CLOCK_FREQ 24576			/* KHz   */
	#define HW_CLOCK_VA	((((HW_CLOCK_PA) & 0x00FFFFFF) | 0xFE000000) - 0x00100000)
#elif defined(CONFIG_ARCH_SDP1601)
	#define HW_CLOCK_PA   0x004000C0
	#define HW_CLOCK_FREQ 24576
	#define HW_CLOCK_VA	((((HW_CLOCK_PA) & 0x00FFFFFF) | 0xFE000000) - 0x00100000)
#elif defined(CONFIG_ARCH_SDP1412)
	#define HW_CLOCK_PA	(0x10f800c0)
	#define HW_CLOCK_FREQ	(24000)
	#define HW_CLOCK_VA	(0xfe000000 + (HW_CLOCK_PA & 0x00ffffff))
#elif defined(CONFIG_ARCH_SDP1803)
	#define HW_CLOCK_PA   0x004000C0
	#define HW_CLOCK_FREQ 24576
	#define HW_CLOCK_VA	(((HW_CLOCK_PA) & 0x000FFFFF) | 0xFE000000)
#else
	#error Unknown arch
#endif


#endif /* HWCLOCK_REGS_H */
