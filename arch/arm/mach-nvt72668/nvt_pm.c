/*
 * Novatek Cortex A9 Power Management Functions
 *
 */

#include <linux/vmalloc.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/string.h>
#include <asm/system_misc.h>
#include <asm/suspend.h>
#include "mach/pm.h"
#include "core.h"
#ifdef CONFIG_NVT_HW_CLOCK_TRACE
#include "mach/nvt_hwclock.h"
#endif


extern void __iomem * clk_reg_base;
static struct nt72668_soc_pm_info_struct *pm_info;

#define TRUE  1
#define FALSE 0

#ifdef CONFIG_SUSPEND

#ifdef CONFIG_NVT_SUSPEND_MEM_CMP
extern unsigned nt72668_mem1_compare,nt72668_mem2_compare;
#endif

void save_nt72668_ext_timers(unsigned *pointer, unsigned timer_address);
void restore_nt72668_ext_timers(unsigned *pointer, unsigned timer_address);
void save_nt72668_uart(unsigned *pointer, unsigned uart_address);
void restore_nt72668_uart(unsigned *pointer, unsigned uart_address);
void save_nt72668_wdt_timers(unsigned *pointer, unsigned wdt_timer_address);
void restore_nt72668_wdt_timers(unsigned *pointer, unsigned wdt_timer_address);
void save_nt72668_global_timer(unsigned *pointer, unsigned global_timer_address);
void restore_nt72668_global_timer(unsigned *pointer, unsigned global_timer_address);
void save_nt72668_scu(unsigned *pointer, unsigned scu_address);
void restore_nt72668_scu(unsigned *pointer, unsigned scu_address);
void save_nt72668_gic_interface(unsigned *pointer, unsigned gic_interface_address);
int gic_nt72668_distributor_set_enabled(int enabled, unsigned gic_distributor_address);
int save_nt72668_gic_distributor_private(unsigned *pointer, unsigned gic_distributor_address);
int save_nt72668_gic_distributor_shared(unsigned *pointer, unsigned gic_distributor_address);
void restore_nt72668_gic_interface(unsigned *pointer, unsigned gic_interface_address);
void restore_nt72668_gic_distributor_private(unsigned *pointer, unsigned gic_distributor_address);
void restore_nt72668_gic_distributor_shared(unsigned *pointer, unsigned gic_distributor_address);
unsigned nt72668_read_debug_address(void);
void save_nt72668_debug(unsigned *context);
void restore_nt72668_debug(unsigned *context);

struct nv72668_soc_context_struct *nt72668_pm_context;

typedef struct
{
    /* 0x00 */ volatile unsigned timer_counter;
    /* 0x04 */ volatile unsigned timer_control;
} nt72668_ext_timer_registers;

typedef struct
{
    unsigned timer_counter;
    unsigned timer_control;
} nt72668_ext_timer_context;

void save_nt72668_ext_timers(unsigned *pointer, unsigned timer_address)
{
    nt72668_ext_timer_context *context = (nt72668_ext_timer_context *)pointer;
    nt72668_ext_timer_registers *timers = (nt72668_ext_timer_registers *)(timer_address);

    context->timer_control    = timers->timer_control;
    timers->timer_control     = 0;
}

void restore_nt72668_ext_timers(unsigned *pointer, unsigned timer_address)
{
    nt72668_ext_timer_context *context = (nt72668_ext_timer_context *)pointer;
    nt72668_ext_timer_registers *timers = (nt72668_ext_timer_registers *)(timer_address);

    timers->timer_counter = pm_info->ext_timer_counter_low;
    timers->timer_control = context->timer_control;
}

typedef struct
{
    /* 0x00 */ volatile unsigned DLAB0;
    /* 0x04 */ volatile unsigned DLAB1;
    /* 0x08 */ volatile unsigned FCR;
    /* 0x0c */ volatile unsigned LCR;
} nt72668_uart_registers;

typedef struct
{
    unsigned DLAB0;
    unsigned DLAB1;
    unsigned FCR;
    unsigned LCR;
} nt72668_uart_context;

void save_nt72668_uart(unsigned *pointer, unsigned uart_address)
{
    nt72668_uart_context *context = (nt72668_uart_context *)pointer;
    nt72668_uart_registers *uart = (nt72668_uart_registers *)(uart_address);

	context->LCR = uart->LCR;
	uart->LCR = uart->LCR | 0x80; /* enable LCR DLAB */
	context->DLAB0 = uart->DLAB0;
	context->DLAB1 = uart->DLAB1;
	context->FCR = uart->FCR;
	uart->LCR = context->LCR; /* restore LCR */
}

void restore_nt72668_uart(unsigned *pointer, unsigned uart_address)
{
    nt72668_uart_context *context = (nt72668_uart_context *)pointer;
    nt72668_uart_registers *uart = (nt72668_uart_registers *)(uart_address);

	uart->LCR = context->LCR | 0x80; /* enable LCR DLAB */
	uart->DLAB0 = context->DLAB0;
	uart->DLAB1 = context->DLAB1;
	uart->FCR = context->FCR;
	uart->LCR = context->LCR; /* restore LCR */
}

typedef struct
{
    /* 0x00 */ volatile unsigned timer_load;
    /* 0x04 */ volatile unsigned timer_counter;
    /* 0x08 */ volatile unsigned timer_control;
    /* 0x0c */ volatile unsigned timer_interrupt_status;
                char padding1[0x10];
    /* 0x20 */ volatile unsigned watchdog_load;
    /* 0x24 */ volatile unsigned watchdog_counter;
    /* 0x28 */ volatile unsigned watchdog_control;
    /* 0x2c */ volatile unsigned watchdog_interrupt_status;
    /* 0x30 */ volatile unsigned watchdog_reset_status;
    /* 0x34 */ volatile unsigned watchdog_disable;
} nt72668_wdt_timer_registers;

typedef struct
{
    unsigned timer_load;
    unsigned timer_counter;
    unsigned timer_control;
    unsigned timer_interrupt_status;
    unsigned watchdog_load;
    unsigned watchdog_counter;
    unsigned watchdog_control;
    unsigned watchdog_interrupt_status;
} nt72668_wdt_timer_context;

void save_nt72668_wdt_timers(unsigned *pointer, unsigned wdt_timer_address)
{
    nt72668_wdt_timer_context  *context = (nt72668_wdt_timer_context *)pointer;
    nt72668_wdt_timer_registers *timers = (nt72668_wdt_timer_registers *)(wdt_timer_address);

    /*
     * Stop the timers firstly
     */
    context->timer_control    = timers->timer_control;
    timers->timer_control     = 0;
    context->watchdog_control = timers->watchdog_control;
    timers->watchdog_control  = 0;

    context->timer_load                = timers->timer_load;
    context->timer_counter             = timers->timer_counter;
    context->timer_interrupt_status    = timers->timer_interrupt_status;
    context->watchdog_load             = timers->watchdog_load;
    context->watchdog_counter          = timers->watchdog_counter;
    context->watchdog_interrupt_status = timers->watchdog_interrupt_status;
}

void restore_nt72668_wdt_timers(unsigned *pointer, unsigned wdt_timer_address)
{
    nt72668_wdt_timer_context  *context = (nt72668_wdt_timer_context *)pointer;
    nt72668_wdt_timer_registers *timers = (nt72668_wdt_timer_registers *)(wdt_timer_address);

    timers->timer_control = 0;
    timers->watchdog_control = 0;
    timers->timer_load    = context->timer_load;
    timers->watchdog_load = context->watchdog_load;
    if (context->timer_interrupt_status)
    {
        timers->timer_counter = 1;
    }
    else
    {
        timers->timer_counter = context->timer_counter;
    }
    if (context->watchdog_interrupt_status)
    {
        timers->watchdog_counter = 1;
    }
    else
    {
        timers->watchdog_counter = context->watchdog_counter;
    }
    timers->timer_control = context->timer_control;
    timers->watchdog_control = context->watchdog_control;
}


typedef struct
{
    /* 0x00 */ volatile unsigned counter_lo;
    /* 0x04 */ volatile unsigned counter_hi;
    /* 0x08 */ volatile unsigned control;
    /* 0x0c */ volatile unsigned status;
    /* 0x10 */ volatile unsigned comparator_lo;
    /* 0x14 */ volatile unsigned comparator_hi;
    /* 0x18 */ volatile unsigned auto_increment;
} nt72668_global_timer_registers;

typedef struct
{
    unsigned counter_lo;
    unsigned counter_hi;
    unsigned control;
    unsigned status;
    unsigned comparator_lo;
    unsigned comparator_hi;
    unsigned auto_increment;
} nt72668_global_timer_context;

#define A9_GT_TIMER_ENABLE          (1<<0)
#define A9_GT_COMPARE_ENABLE        (1<<1)
#define A9_GT_AUTO_INCREMENT_ENABLE (1<<3)
#define A9_GT_EVENT_FLAG            (1<<0)

void save_nt72668_global_timer(unsigned *pointer, unsigned global_timer_address)
{
    nt72668_global_timer_context *context = (void*)pointer;
    nt72668_global_timer_registers *timer = (void*)(global_timer_address);
    unsigned tmp_lo, tmp_hi, tmp2_hi;
    do
    {
        tmp_hi  = timer->counter_hi;
        tmp_lo  = timer->counter_lo;
        tmp2_hi = timer->counter_hi;
    } while (tmp_hi != tmp2_hi);
    context->counter_lo     = tmp_lo;
    context->counter_hi     = tmp_hi;
    context->control        = timer->control;
    context->status         = timer->status;
    context->comparator_lo  = timer->comparator_lo;
    context->comparator_hi  = timer->comparator_hi;
    context->auto_increment = timer->auto_increment;
}

void restore_nt72668_global_timer(unsigned *pointer, unsigned global_timer_address)
{
    nt72668_global_timer_context *context = (void*)pointer;
    nt72668_global_timer_registers *timer = (void*)(global_timer_address);
    /* Is the timer currently enabled? */
    if (timer->control & A9_GT_TIMER_ENABLE)
    {
        /* Temporarily stop the timer */
        timer->control &= (unsigned)(~A9_GT_TIMER_ENABLE);
    }
    else
    {
        timer->counter_lo    = context->counter_lo;
        timer->counter_hi    = context->counter_hi;
    }
    timer->comparator_lo = context->comparator_lo;
    timer->comparator_hi = context->comparator_hi;
    timer->control = context->control;
    timer->auto_increment = context->auto_increment;
}

typedef struct
{
    /* 0x00 */  volatile unsigned int control;
    /* 0x04 */  const unsigned int configuration;
    /* 0x08 */  union
                {
                    volatile unsigned int w;
                    volatile unsigned char b[4];
                } power_status;
    /* 0x0c */  volatile unsigned int invalidate_all;
                char padding1[48];
    /* 0x40 */  volatile unsigned int filtering_start;
    /* 0x44 */  volatile unsigned int filtering_end;
                char padding2[8];
    /* 0x50 */  volatile unsigned int access_control;
    /* 0x54 */  volatile unsigned int ns_access_control;
} nt72668_scu_registers;
/*
 * Ignore power status register.
 */

void save_nt72668_scu(unsigned *pointer, unsigned scu_address)
{
    nt72668_scu_registers *scu = (nt72668_scu_registers *)scu_address;

    pointer[0] = scu->control;
    pointer[1] = scu->power_status.w;
    pointer[2] = scu->filtering_start;
    pointer[3] = scu->filtering_end;
    pointer[4] = scu->access_control;
    pointer[5] = scu->ns_access_control;
}

void restore_nt72668_scu(unsigned *pointer, unsigned scu_address)
{
    nt72668_scu_registers *scu = (nt72668_scu_registers *)scu_address;

    //scu->invalidate_all = 0x000f;
    scu->filtering_start = pointer[2];
    scu->filtering_end = pointer[3];
    scu->access_control = pointer[4];
    scu->ns_access_control = pointer[5];
    scu->power_status.w = pointer[1];
    scu->control = pointer[0];
}

#define GIC_DIST_ENABLE      0x00000001

struct set_and_clear_regs
{
    volatile unsigned int set[32], clear[32];
};

typedef struct
{
    /* 0x000 */  volatile unsigned int control;
                 const unsigned int controller_type;
                 const unsigned int implementer;
                 const char padding1[116];
    /* 0x080 */  volatile unsigned int security[32];
    /* 0x100 */  struct set_and_clear_regs enable;
    /* 0x200 */  struct set_and_clear_regs pending;
    /* 0x300 */  volatile const unsigned int active[32];
                 const char padding2[128];
    /* 0x400 */  volatile unsigned int priority[256];
    /* 0x800 */  volatile unsigned int target[256];
    /* 0xC00 */  volatile unsigned int configuration[64];
    /* 0xD00 */  const char padding3[512];
    /* 0xF00 */  volatile unsigned int software_interrupt;
                 const char padding4[220];
    /* 0xFE0 */  unsigned const int peripheral_id[4];
    /* 0xFF0 */  unsigned const int primecell_id[4];
} nt72668_interrupt_distributor;

typedef struct
{
    /* 0x00 */  volatile unsigned int control;
    /* 0x04 */  volatile unsigned int priority_mask;
    /* 0x08 */  volatile unsigned int binary_point;
    /* 0x0c */  volatile unsigned const int interrupt_ack;
    /* 0x10 */  volatile unsigned int end_of_interrupt;
    /* 0x14 */  volatile unsigned const int running_priority;
    /* 0x18 */  volatile unsigned const int highest_pending;
    /* 0x1c */  volatile unsigned int aliased_binary_point;
} nt72668_cpu_interface;

void save_nt72668_gic_interface(unsigned *pointer, unsigned gic_interface_address)
{
    nt72668_cpu_interface *ci = (nt72668_cpu_interface *)gic_interface_address;

    pointer[0] = ci->control;
    pointer[1] = ci->priority_mask;
    pointer[2] = ci->binary_point;
}

/*
 * Enables or disables the GIC distributor ,
 * Return value is boolean, and reports whether GIC was previously enabled.
 */
int gic_nt72668_distributor_set_enabled(int enabled, unsigned gic_distributor_address)
{
    unsigned tmp;
    nt72668_interrupt_distributor *id = (nt72668_interrupt_distributor *)gic_distributor_address;

    tmp = id->control;
    if (enabled)
    {
        id->control = tmp | GIC_DIST_ENABLE;
    }
    else
    {
        id->control = tmp & (unsigned)(~GIC_DIST_ENABLE);
    }
    return (tmp & GIC_DIST_ENABLE) != 0;
}

/*
 * Saves this CPU's banked parts of the distributor
 * Returns non-zero if an SGI/PPI interrupt is pending
 * Requires 19 words of memory
 */
int save_nt72668_gic_distributor_private(unsigned *pointer, unsigned gic_distributor_address)
{
    nt72668_interrupt_distributor *id = (nt72668_interrupt_distributor *)gic_distributor_address;

    *pointer = id->enable.set[0];
    ++pointer;
    pointer = copy_multi_words(pointer, id->priority, 8);
    pointer = copy_multi_words(pointer, id->target, 8);
    /* Save just the PPI configurations (SGIs are not configurable) */
    *pointer = id->configuration[1];
    ++pointer;
    *pointer = id->pending.set[0];
    if (*pointer)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/*
 * Saves the shared parts of the distributor.
 * Returns non-zero if an SPI interrupt is pending (after saving all required context)
 * Requires 141 words of memory ; num_spis is 224 ; 7+56+56+14+7+1=141
 */
int save_nt72668_gic_distributor_shared(unsigned *pointer, unsigned gic_distributor_address)
{
    nt72668_interrupt_distributor *id = (nt72668_interrupt_distributor *)gic_distributor_address;
    unsigned num_spis, *saved_pending,i;
    int retval = 0;

    /* Calculate how many SPIs the GIC supports */
    num_spis = 32 * (id->controller_type & 0x1f);

    /* Save rest of GIC configuration */
    if (num_spis)
    {
        pointer = copy_multi_words(pointer, id->enable.set + 1,    num_spis / 32);
        pointer = copy_multi_words(pointer, id->priority + 8,      num_spis / 4);
        pointer = copy_multi_words(pointer, id->target + 8,        num_spis / 4);
        pointer = copy_multi_words(pointer, id->configuration + 2, num_spis / 16);
        saved_pending = pointer;
        pointer = copy_multi_words(pointer, id->pending.set + 1,   num_spis / 32);

        /* Check interrupt pending bits */
        for (i=0; i<num_spis/32; ++i)
        {
            if (saved_pending[i])
            {
                retval = -1;
                break;
            }
        }
    }
    /* Save control register */
    *pointer = id->control;

    return retval;
}

void restore_nt72668_gic_interface(unsigned *pointer, unsigned gic_interface_address)
{
    nt72668_cpu_interface *ci = (nt72668_cpu_interface *)gic_interface_address;

    ci->priority_mask = pointer[1];
    ci->binary_point = pointer[2];
    /* Restore control register last */
    ci->control = pointer[0];
}

void restore_nt72668_gic_distributor_private(unsigned *pointer, unsigned gic_distributor_address)
{
    nt72668_interrupt_distributor *id = (nt72668_interrupt_distributor *)gic_distributor_address;

    /* Assume the distributor is disabled so we can write to its config registers */
    id->enable.set[0] = *pointer;
    ++pointer;
    copy_multi_words(id->priority, pointer, 8);
    pointer += 8;
    copy_multi_words(id->target, pointer, 8);
    pointer += 8;
    /* Restore just the PPI configurations (SGIs are not configurable) */
    id->configuration[1] = *pointer;
    ++pointer;
    id->pending.set[0] = *pointer;
}

void restore_nt72668_gic_distributor_shared(unsigned *pointer, unsigned gic_distributor_address)
{
    nt72668_interrupt_distributor *id = (nt72668_interrupt_distributor *)gic_distributor_address;
    unsigned num_spis;

    /* Make sure the distributor is disabled */
    gic_nt72668_distributor_set_enabled(FALSE, gic_distributor_address);

    /* Calculate how many SPIs the GIC supports */
    num_spis = 32 * ((id->controller_type) & 0x1f);

    /* Restore rest of GIC configuration */
    if (num_spis)
    {
        copy_multi_words(id->enable.set + 1, pointer, num_spis / 32);
        pointer += num_spis / 32;
        copy_multi_words(id->priority + 8, pointer, num_spis / 4);
        pointer += num_spis / 4;
        copy_multi_words(id->target + 8, pointer, num_spis / 4);
        pointer += num_spis / 4;
        copy_multi_words(id->configuration + 2, pointer, num_spis / 16);
        pointer += num_spis / 16;
        copy_multi_words(id->pending.set + 1, pointer, num_spis / 32);
        pointer += num_spis / 32;
    }

    /* Restore control register - if the GIC was disabled during save, it will be restored as disabled. */
    id->control = *pointer;

    return;
}

static int nt72668_pm_syscore_suspend(void)
{
	printk("[%s]\n", __FUNCTION__);

	if (pm_info->alloc_success == TRUE) {
		save_nt72668_pmu(nt72668_pm_context->pmu_data);
		save_nt72668_wdt_timers(nt72668_pm_context->wdt_timer_data, pm_info->cpu_a9_addr+A9_MPCORE_TWD_OFFSET);
		save_nt72668_global_timer(nt72668_pm_context->global_timer_data, pm_info->cpu_a9_addr+A9_MPCORE_GIT_OFFSET);
		save_nt72668_gic_interface(nt72668_pm_context->gic_interface_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_CPU_OFFSET);
		save_nt72668_gic_distributor_private(nt72668_pm_context->gic_dist_private_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		save_nt72668_gic_distributor_shared(nt72668_pm_context->gic_dist_shared_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		save_nt72668_scu(nt72668_pm_context->scu_data, pm_info->cpu_a9_addr+A9_MPCORE_SCU_OFFSET);
	}
#ifdef CONFIG_NVT_HW_CLOCK_TRACE
	/* HW clock suspend must be the latest. */
	hwclock_suspend();
#endif

	return 0;
}

static void nt72668_pm_syscore_resume(void)
{
	printk("[%s]\n", __FUNCTION__);

	if (pm_info->alloc_success == TRUE) {
		restore_nt72668_global_timer(nt72668_pm_context->global_timer_data, pm_info->cpu_a9_addr+A9_MPCORE_GIT_OFFSET);
  		restore_nt72668_global_timer(nt72668_pm_context->global_timer_data, pm_info->cpu_a9_addr+A9_MPCORE_GIT_OFFSET);
		restore_nt72668_scu(nt72668_pm_context->scu_data, pm_info->cpu_a9_addr+A9_MPCORE_SCU_OFFSET);
		gic_nt72668_distributor_set_enabled(FALSE, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		restore_nt72668_gic_distributor_shared(nt72668_pm_context->gic_dist_shared_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		gic_nt72668_distributor_set_enabled(TRUE, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		restore_nt72668_gic_distributor_private(nt72668_pm_context->gic_dist_private_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_DIST_OFFSET);
		restore_nt72668_gic_interface(nt72668_pm_context->gic_interface_data, pm_info->cpu_a9_addr+A9_MPCORE_GIC_CPU_OFFSET);
		restore_nt72668_wdt_timers(nt72668_pm_context->wdt_timer_data, pm_info->cpu_a9_addr+A9_MPCORE_TWD_OFFSET);
		restore_nt72668_pmu(nt72668_pm_context->pmu_data);
#ifdef CONFIG_NVT_SUSPEND_MEM_CMP
		if (nt72668_mem1_compare == 1)
			 printk("[%s] Region 1 memory compare failed !\n",__FUNCTION__);
		else
			 printk("[%s] Region 1 memory compare successful !\n",__FUNCTION__);
		if (nt72668_mem2_compare == 1)
			 printk("[%s] Region 2 memory compare failed !\n",__FUNCTION__);
		else
			 printk("[%s] Region 2 memory compare successful !\n",__FUNCTION__);
#endif
	}
}

static struct syscore_ops nt72668_pm_syscore_ops = {
    .suspend	= nt72668_pm_syscore_suspend,
    .resume	= nt72668_pm_syscore_resume,
};

int nt72668_pm_syscore_init(void)
{
    printk("[%s]\n", __FUNCTION__);

    /* Allocate memory space for storing soc related registers */
    pm_info->alloc_success = FALSE;

    nt72668_pm_context = (void *)kmalloc(sizeof(struct nv72668_soc_context_struct), GFP_KERNEL);
    if (nt72668_pm_context == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->uart0_data            = (unsigned *)kmalloc((UART_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->uart0_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->uart1_data            = (unsigned *)kmalloc((UART_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->uart1_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->uart2_data            = (unsigned *)kmalloc((UART_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->uart2_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->pmu_data              = (unsigned *)kmalloc((PMU_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->pmu_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->wdt_timer_data        = (unsigned *)kmalloc((WDT_TIMER_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->wdt_timer_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->global_timer_data     = (unsigned *)kmalloc((GLOBAL_TIMER_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->global_timer_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->gic_interface_data    = (unsigned *)kmalloc((GIC_INTERFACE_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->gic_interface_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->gic_dist_private_data = (unsigned *)kmalloc((GIC_DIST_PRIVATE_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->gic_dist_private_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->gic_dist_shared_data  = (unsigned *)kmalloc((GIC_DIST_SHARED_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->gic_dist_shared_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    nt72668_pm_context->scu_data              = (unsigned *)kmalloc((SCU_DATA_SIZE), GFP_KERNEL);
    if (nt72668_pm_context->scu_data == NULL)
    {
    	printk("[%s] kmalloc failed.\n", __FUNCTION__);
    	return -1;
    }
    pm_info->alloc_success = TRUE;
    register_syscore_ops(&nt72668_pm_syscore_ops);
    return 0;
}

static int __nt72668_suspend_enter(unsigned long unused)
{
	soft_restart(virt_to_phys(nt72668_wait_for_die));

	return 0;
}

static int nt72668_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;

	printk("[%s]\n", __FUNCTION__);

	cpu_suspend(0, __nt72668_suspend_enter);

	printk("resume successful and back to [%s]\n", __FUNCTION__);

	return ret;
}

static int nt72668_pm_begin(suspend_state_t state)
{
	printk("[%s]\n", __FUNCTION__);
	return 0;
}

static void nt72668_pm_end(void)
{
	printk("[%s]\n", __FUNCTION__);
}

static void nt72668_pm_finish(void)
{
	printk("[%s]\n", __FUNCTION__);
}

static const struct platform_suspend_ops nt72668_pm_ops = {
	.begin		= nt72668_pm_begin,
	.end		= nt72668_pm_end,
	.enter		= nt72668_pm_enter,
	.finish		= nt72668_pm_finish,
	.valid		= suspend_valid_only_mem,
};


#ifdef CONFIG_NVT_UART0_NOTTY
#include <linux/delay.h>
extern void nt72668_micom_uart0_sendcmd(unsigned char *s, unsigned char size);

static void nvt_ca9_power_off(void)
{
    int timeout = 10;
    unsigned char databuff[9];

	memset(databuff, 0, 9);

	databuff[0] = 0xff;
	databuff[1] = 0xff;
	databuff[2] = 0x12;
	databuff[3] = 0;
	databuff[8] = (unsigned char)(databuff[8] + databuff[2]);
	databuff[8] = (unsigned char)(databuff[8] + databuff[3]);

    do {
		nt72668_micom_uart0_sendcmd(databuff, 9);
        udelay(100);
    } while(timeout--);

    if (!timeout)
        printk(KERN_ALERT "[MSGBOX] error: failed to get nvt message box's response\n");

    return 0;
}
#endif

static int __init nt72668_pm_late_init(void)
{
	pm_info = kmalloc(sizeof(struct nt72668_soc_pm_info_struct), GFP_KERNEL);
	if (pm_info == NULL)
	{
		printk("[%s] kmalloc failed.\n", __FUNCTION__);
		return -1;
	}

#ifdef CONFIG_NVT_UART0_NOTTY
	// setup pm_power_off function
	pm_power_off = nvt_ca9_power_off;
#else
	#error "nvt_ca9_power_off with message box API.. which one instead??? "
#endif

	pm_info->cpu_a9_addr = (unsigned) ioremap(get_periph_base(), SZ_8K);
	suspend_set_ops(&nt72668_pm_ops);
	nt72668_pm_syscore_init();

	return 0;
}

__initcall(nt72668_pm_late_init);
#endif

