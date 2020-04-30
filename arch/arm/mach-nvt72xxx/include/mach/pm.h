#ifndef __MACH_NVT72XXX_PM_H
#define __MACH_NVT72XXX_PM_H

#include "mach/debug.h"
#ifdef CONFIG_CLKSRC_NVT_TIMER
extern void __iomem *nvt_timer_reg_base;
#endif

#ifdef CONFIG_SUSPEND

extern void nt72xxx_wait_for_die(void);

#endif

#endif
