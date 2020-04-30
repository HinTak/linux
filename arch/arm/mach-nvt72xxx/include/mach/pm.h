#ifndef __MACH_NVT72668_PM_H
#define __MACH_NVT72668_PM_H

#include "mach/debug.h"
#ifdef CONFIG_CLKSRC_NVT_TIMER
extern void __iomem *nvt_timer_reg_base;
#endif

#ifdef CONFIG_SUSPEND

extern void nt72668_wait_for_die(void);

#endif

#endif
