#ifndef HWCLOCK_REGS_H
#define HWCLOCK_REGS_H

#if defined(CONFIG_MACH_MT8167)

#define SYSTIMER_IO_BASE    (0x1000D000)
#define IO_ADDRESS(x)       ((x) + (0xFD000000 - 0x10000000))

#define HW_CLOCK_PA         (0x1000D008)
#define HW_CLOCK_FREQ_M     (13)        /* MHz  */
#define HW_CLOCK_FREQ       (13000000)  /* Hz   */

/*The 1st creat static mapping by head.S
* Because it's used to get timestamp info from the very early stage in kernel*/
#define MTK_PHYS_TO_VIRT(x) (((x) & 0x00FFFFFF) | 0xFE000000)

/*The 2nd creat static mapping by mediatek.c
* Because the 1st static mapping is cleared
* while kernel try to recreated page table*/
#define __MTK_PHYS_TO_VIRT(x) IO_ADDRESS(x)

#define HW_CLOCK_VA MTK_PHYS_TO_VIRT(HW_CLOCK_PA)
#define __HW_CLOCK_VA __MTK_PHYS_TO_VIRT(HW_CLOCK_PA)

#else
	#error Unknown arch
#endif

#endif /* HWCLOCK_REGS_H */
