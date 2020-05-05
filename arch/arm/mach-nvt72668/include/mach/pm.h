#ifndef __MACH_NVT72668_PM_H
#define __MACH_NVT72668_PM_H

#include "mach/debug.h"

#ifdef CONFIG_SUSPEND

extern void nt72668_wait_for_die(void);
extern int nt72668_pm_syscore_init(void);
extern unsigned * copy_multi_words(volatile unsigned *dest, volatile unsigned *source, unsigned num_words);
extern void save_nt72668_pmu(unsigned *pointer);
extern void restore_nt72668_pmu(unsigned *pointer);
extern void nt72668_micom_uart_init(void);
extern void nt72668_micom_suspend(void);
extern void nt72668_micom_poweroff(void);
#ifdef CONFIG_NVT_SUSPEND_HANDLE_DEBUG_REG
extern unsigned nt72668_read_drar(void);
extern unsigned nt72668_read_dsar(void);
#endif


/*
 * Maximum size of each item of context, in bytes
 */ 

#define EXT_TIMER_DATA_SIZE           8
#define UART_DATA_SIZE               16 /* x3 UART */
#define PMU_DATA_SIZE                76
#define WDT_TIMER_DATA_SIZE          32
#define GLOBAL_TIMER_DATA_SIZE       28
#define GIC_INTERFACE_DATA_SIZE      12
#define GIC_DIST_PRIVATE_DATA_SIZE   76  
#define GIC_DIST_SHARED_DATA_SIZE   564  
#define SCU_DATA_SIZE                24
#define DEBUG_DATA_SIZE             268

struct nv72668_soc_context_struct
{
	unsigned *ext_timer_data;
	unsigned *uart0_data;
	unsigned *uart1_data;
	unsigned *uart2_data;
	unsigned *pmu_data;
	unsigned *wdt_timer_data;
	unsigned *global_timer_data;
	unsigned *gic_interface_data;
	unsigned *gic_dist_private_data;
	unsigned *gic_dist_shared_data;
	unsigned *scu_data;
#ifdef CONFIG_NVT_SUSPEND_HANDLE_DEBUG_REG	
	unsigned *debug_data;
#endif	
};

struct nt72668_soc_pm_info_struct {
	unsigned cpu_a9_addr;
	unsigned ext_timer_addr;
	unsigned ext_timer_counter_low;
	unsigned uart0_addr;
	unsigned uart1_addr;
	unsigned uart2_addr;
	unsigned alloc_success;
};

#endif

#endif




