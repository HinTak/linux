#ifndef HWCLOCK_REGS_H
#define HWCLOCK_REGS_H

#define HW_CLOCK_PA 0xffd00200
#define MPLL_REG_PA 0xfd670000

/*The 1st creat static mapping by head.S
* Because it's used to get timestamp info from the very early stage in kernel*/
#define NVT_PHYS_TO_VIRT(x) (((x) & 0x00FFFFFF) | 0xFE000000)

/*The 2nd creat static mapping by v2m.c
* Because the 1st static mapping is cleared
* while kernel try to recreated page table*/
#define __NVT_PHYS_TO_VIRT(x) (((x) & 0x00000FFF) | 0xFEFFE000)

#define __HW_CLOCK_VA __NVT_PHYS_TO_VIRT(HW_CLOCK_PA)
#define HW_CLOCK_VA NVT_PHYS_TO_VIRT(HW_CLOCK_PA)
#define MPLL_REG_VA NVT_PHYS_TO_VIRT(MPLL_REG_PA)

#endif /* HWCLOCK_REGS_H */
