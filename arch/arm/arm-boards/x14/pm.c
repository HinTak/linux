/*-----------------------------------------------------------------------------
    Include Files
------------------------------------------------------------------------------*/
#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>
#include <asm/smp.h>
#include <mach/pm.h>
#include <mach/io.h>
#include <asm/cputype.h>
#include "chip_int.h"
#include "chip_setup.h"
#include "sleep_helper.h"


#define WAKEUP_SAVE_ADDR 0xC0000000
#define INT_MASK_REG_BASE IO_ADDRESS(REG_INT_BASE_PA)

/* for micom control */
#define CONFIG_CONS_INDEX   1
#define CONSOLE_PORT CONFIG_CONS_INDEX
#define REG_UART1_BASE   0xFD220C00 //UART 1
#define REQ_SLEEP       0x25
#define UART_LSR                5           // In:  Line Status Register
#define UART_LSR_THRE                     0x20                                // Transmit-hold-register empty
#define UART_LSR_TEMT                     0x40                                // Transmitter empty
#define UART_TX                         0               // Out: Transmit buffer (DLAB=0), 16-byte FIFO
#define BOTH_EMPTY                  (UART_LSR_TEMT | UART_LSR_THRE)
#define UART1_REG8(addr)  *((volatile unsigned int*)(REG_UART1_BASE + ((addr)<< 3))) //terry
#define UART_REG8_MICOM(addr)       UART1_REG8(addr) //$ UART 1

extern void sleep_save_cpu_registers(void * pbuffer);
extern void sleep_restore_cpu_registers(void * pbuffer);
extern void sleep_set_wakeup_save_addr_phy(unsigned long phy_addr, void *virt_addr);
extern void sleep_clear_wakeup_save_addr_phy(unsigned long phy_addr, void *virt_addr);
extern void sleep_prepare_last(unsigned long wakeup_addr_phy);
extern void sleep_wakeup_first(unsigned long boot_start,void *exit_addr_virt);
extern void sleep_save_neon_regs(void * pbuffer);
extern void sleep_restore_neon_regs(void * pbuffer);
extern void SerPrintfAtomic(const char *fmt,...);

extern void do_self_refresh_mode(unsigned long virt_addr, unsigned long loops_per_jiffy);

extern void __iomem *_gic_cpu_base_addr;
extern void __iomem *_gic_dist_base_addr;
static u32 MStar_Suspend_Buffer[SLEEPDATA_SIZE];
static u32 MStar_IntMaskSave[8];
void SerPrintChar(char ch);
void SerPrintStr(char *p);
void SerPrintStrAtomic(char *p);
int vSerPrintf(const char *fmt, va_list args);
int vSerPrintfAtomic(const char *fmt, va_list args);
void mstar_save_int_mask(void);
void mstar_restore_int_mask(void);
void mstar_clear_int(void);
unsigned long mstar_virt_to_phy(void* virtaddr);
void* mstar_phy_to_virt(unsigned long phyaddr );
void mstar_sleep_cur_cpu_flush(void);
void WriteRegWord(unsigned long addr, unsigned long val);
unsigned long ReadRegWord(unsigned long addr);
void micom_serial_putc (int portnum, char c);
void MICOM_putc (const char c);
int micom_cmd(unsigned char command);
int x14_map_sram(unsigned long start, unsigned long size, int cached);

DEFINE_SPINLOCK(ser_printf_lock);

void SerPrintChar(char ch)
{
    __asm__ volatile (
        "ldr r5, =0xFD201300\n"
        "1: ldr r4, [r5, #0x28]\n"
        "tst r4, #0x20\n"
        "beq 1b\n"
        "ldr r4, %0\n"
        "strb r4,[r5]\n"
        ::"m"(ch):"r4","r5","cc","memory"
        );
}
void SerPrintStr(char *p)
{
    unsigned int nLen=strlen(p);
    unsigned int i;
    for(i=0;i<nLen;i++)
    {
        if(p[i]=='\n')SerPrintChar('\r');
        SerPrintChar(p[i]);
    }
}
void SerPrintStrAtomic(char *p)
{
    u_long flag;
    spin_lock_irqsave(&ser_printf_lock,flag);
    SerPrintStr(p);
    spin_unlock_irqrestore(&ser_printf_lock,flag);
}
void SerPrintf(const char *fmt,...)
{
    char tmpbuf[500];
    int nLen;
    va_list args;
    va_start(args, fmt);
    nLen=vscnprintf(tmpbuf, sizeof(tmpbuf), fmt, args);
    va_end(args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerPrintStr(tmpbuf);
}
void SerPrintfAtomic(const char *fmt,...)
{
    char tmpbuf[500];
    int nLen;
    va_list args;
    va_start(args, fmt);
    nLen=vscnprintf(tmpbuf, sizeof(tmpbuf), fmt, args);
    va_end(args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerPrintStrAtomic(tmpbuf);
}
int vSerPrintf(const char *fmt, va_list args)
{
    char tmpbuf[500];
    int nLen;
    nLen=vscnprintf(tmpbuf, sizeof(tmpbuf), fmt, args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerPrintStr(tmpbuf);
    return nLen;
}
int vSerPrintfAtomic(const char *fmt, va_list args)
{
    char tmpbuf[500];
    int nLen;
    nLen=vscnprintf(tmpbuf, sizeof(tmpbuf), fmt, args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerPrintStrAtomic(tmpbuf);
    return nLen;
}

void mstar_save_int_mask(void)
{
    volatile unsigned long *int_mask_base=(volatile unsigned long *)INT_MASK_REG_BASE;
    MStar_IntMaskSave[0]=int_mask_base[0x44];
    MStar_IntMaskSave[1]=int_mask_base[0x45];
    MStar_IntMaskSave[2]=int_mask_base[0x46];
    MStar_IntMaskSave[3]=int_mask_base[0x47];
    MStar_IntMaskSave[4]=int_mask_base[0x54];
    MStar_IntMaskSave[5]=int_mask_base[0x55];
    MStar_IntMaskSave[6]=int_mask_base[0x56];
    MStar_IntMaskSave[7]=int_mask_base[0x57];
}
void mstar_restore_int_mask(void)
{
    volatile unsigned long *int_mask_base=(volatile unsigned long *)INT_MASK_REG_BASE;
    int_mask_base[0x44]=MStar_IntMaskSave[0];
    int_mask_base[0x45]=MStar_IntMaskSave[1];
    int_mask_base[0x46]=MStar_IntMaskSave[2];
    int_mask_base[0x47]=MStar_IntMaskSave[3];
    int_mask_base[0x54]=MStar_IntMaskSave[4];
    int_mask_base[0x55]=MStar_IntMaskSave[5];
    int_mask_base[0x56]=MStar_IntMaskSave[6];
    int_mask_base[0x57]=MStar_IntMaskSave[7];
}
void mstar_clear_int(void)
{
    volatile unsigned long *int_mask_base=(volatile unsigned long *)INT_MASK_REG_BASE;
    int_mask_base[0x4c]=0xFFFF;
    int_mask_base[0x4d]=0xFFFF;
    int_mask_base[0x4e]=0xFFFF;
    int_mask_base[0x4f]=0xFFFF;
    int_mask_base[0x5c]=0xFFFF;
    int_mask_base[0x5d]=0xFFFF;
    int_mask_base[0x5e]=0xFFFF;
    int_mask_base[0x5f]=0xFFFF;
}
unsigned long mstar_virt_to_phy(void* virtaddr)
{
    unsigned long rest=0;
    rest=virt_to_phys(virtaddr);
    return rest;
}

void* mstar_phy_to_virt(unsigned long phyaddr )
{
    void *rest=0;
    rest=phys_to_virt(phyaddr);
    return rest;
}

void mstar_sleep_cur_cpu_flush(void)
{
    Chip_Flush_Cache_All_Single();
}

void WriteRegWord(unsigned long addr, unsigned long val)
{
    volatile unsigned long *regaddr=(unsigned long *)addr;
    (*regaddr)=val;
}
unsigned long ReadRegWord(unsigned long addr)
{
    volatile unsigned long *regaddr=(unsigned long *)addr;
    return (*regaddr);
}
void micom_serial_putc (int portnum, char c)
{
        unsigned char  u8Reg;
        //unsigned int u32Timer;

        //Wait for -hold-register empty
        //UART_TIME(u32Timer,10)  //10ms for OS version

        do {
                u8Reg = (unsigned char)UART_REG8_MICOM(UART_LSR);
                if ((u8Reg & UART_LSR_THRE) == UART_LSR_THRE)
                {
                        break;
                }
        }while(1);

        UART_REG8_MICOM(UART_TX) = c;   //put char

        //Wait for both Transmitter empty & Transmit-hold-register empty
        //UART_TIME(u32Timer,10)  //10ms for OS version

        do {
                u8Reg = (unsigned char)UART_REG8_MICOM(UART_LSR);
                if ((u8Reg & BOTH_EMPTY) == BOTH_EMPTY)
                {
                        break;
                }
        }while(1);

}
void MICOM_putc (const char c)
{
    if(c == '\n')
    {
        micom_serial_putc(CONSOLE_PORT, '\r'); //CR
        micom_serial_putc(CONSOLE_PORT, '\n'); //LF
    }
    else
    {
        micom_serial_putc(CONSOLE_PORT, c);
    }
}
/*------------------------------------------------------------------------------
   Command List

        toggle   : 0x35
        shutdown : 0x12
        reboot   : 0x1d
        rollback : 0x34
        ... : 0xd0 , 1
        ... : 0xd1 , 1
------------------------------------------------------------------------------*/
int micom_cmd(unsigned char command)
{
        unsigned char w_databuff[9];
        unsigned char r_databuff[255];
        int retry = 1;//2;//5;
        int i = 0;

        while(retry-- > 0)
        {
                memset(w_databuff, 0, 9);
                memset(r_databuff, 0, 255);

                w_databuff[0] = 0xff;
                w_databuff[1] = 0xff;
                w_databuff[2] = (char)command;

                /* Making Check Sum  */
                w_databuff[8] = w_databuff[2];
//                w_databuff[8] += w_databuff[3];
				w_databuff[8] += 0;

                //Send command to MICOM
                for(i=0; i<9; i++)
                {
                        MICOM_putc(w_databuff[i]);
                }
                //udelay(10*1000); // 10ms spec in
        }
        return 1;
}

/*-----------------------------------------------------------------------------
    Allocated a virtual address of kernel and map to bus address of SRAM 
------------------------------------------------------------------------------*/
#define ROUND_DOWN(value,boundary)	((value) & (~((boundary)-1)))
#define SRAM_ADDR	0X1FC02000
#define SRAM_SIZE		4*1024

static void __iomem *x14_sram_base = NULL;

int x14_map_sram(unsigned long start, unsigned long size, int cached)
{
	if (size == 0)
		return -EINVAL;

	start = ROUND_DOWN(start, PAGE_SIZE);
	x14_sram_base = __arm_ioremap_exec(start, size, cached);
	if (!x14_sram_base) {
		pr_err("SRAM: Could not map\n");
		return -ENOMEM;
	}

	memset_io(x14_sram_base, 0,  size);

	return 0;
}

//static void __iomem *test_ddram = NULL;

/*------------------------------------------------------------------------------
    Function: mstar_pm_enter

    Description:
        Actually enter sleep state
    Input: (The arguments were used by caller to input data.)
        state - suspend state (not used)
    Output: (The arguments were used by caller to receive data.)
        None.
    Return:
        0
    Remark:
        None.
-------------------------------------------------------------------------------*/
static int __cpuinit mstar_pm_enter(suspend_state_t state)
{
	unsigned int rval;
	void *pWakeup = 0;

	printk("[%s] cpu suspend...\n", __func__);

	__asm__ volatile (
		"ldr r1, =MSTAR_WAKEUP_ENTRY\n"
		"str r1, %0"
		:"=m"(pWakeup)::"r1"
	);

	if(x14_sram_base == NULL) {
		rval = (unsigned int)x14_map_sram(SRAM_ADDR, SRAM_SIZE, 1);
		if(rval == 0) {
			printk("x14_sram_base:%lu\n", (unsigned long)x14_sram_base);
		}
		else {
			printk("Fail to allocated x14_sram_base.\n");
		}
	}

	/* clean self-refreash mode state. micom will check this bit. */
	/* bit2: 1---> done. power off */
	/* bit2: 0--->preparing. waiting for done. */
	rval = readw((void *)0xFD001D50);
	rval &= 0xFFFB;
	writew((unsigned short)rval, (void *)0xFD001D50);

	mstar_save_int_mask();
	save_performance_monitors((appf_u32 *)performance_monitor_save);	
	save_a9_timers((appf_u32*)&a9_timer_save, PERI_ADDRESS(PERI_PHYS));
	save_a9_global_timer((appf_u32 *)a9_global_timer_save,PERI_ADDRESS(PERI_PHYS));

	save_gic_interface((appf_u32 *)gic_interface_save,(unsigned)_gic_cpu_base_addr,1);
	save_gic_distributor_private((appf_u32 *)gic_distributor_private_save,(unsigned)_gic_dist_base_addr,1);

	save_cp15((appf_u32 *)cp15_save);// CSSELR
	//save_v7_debug((appf_u32 *)&a9_dbg_data_save);

	save_gic_distributor_shared((appf_u32 *)gic_distributor_shared_save,(unsigned)_gic_dist_base_addr,1);

	save_control_registers(control_data, 0);
	save_mmu(mmu_data);

	save_a9_scu((appf_u32 *)a9_scu_save,PERI_ADDRESS(PERI_PHYS));

	sleep_save_neon_regs(&MStar_Suspend_Buffer[SLEEPSTATE_NEONREG/WORD_SIZE]);
	sleep_save_cpu_registers(MStar_Suspend_Buffer);
	sleep_set_wakeup_save_addr_phy(mstar_virt_to_phy(pWakeup),(void*)pWakeup);
//	sleep_set_wakeup_save_addr_phy(mstar_virt_to_phy((void*)WAKEUP_SAVE_ADDR),(void*)WAKEUP_SAVE_ADDR);

	/* write kernel resume address */
	rval = mstar_virt_to_phy(pWakeup);
	printk("[%s] kernel resume address : 0x%08x\n", __func__, rval);
	writew((unsigned short)rval, (void *)0xFD001D48); //addr_lo
	writew((unsigned short)(rval >> 16), (void *)0xFD001D4C); //addr_hi

	sleep_prepare_last(mstar_virt_to_phy(pWakeup));
	write_actlr(read_actlr() & (unsigned)(~A9_SMP_BIT));//add

	__asm__ volatile (
		"nop\n"
		:::"r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r12"
	);
#if 1
	/* send checking self-refreash mode done command to micom */
	micom_cmd(REQ_SLEEP);

	/* do self-refreash mode */
	do_self_refresh_mode((unsigned long)x14_sram_base, loops_per_jiffy);
	
#endif
	__asm__ volatile(
		"MSTAR_WAKEUP_ENTRY:\n"
		"bl ensure_environment\n"
		"bl use_tmp_stack\n"
		"ldr r1, =exit_addr\n"
		"sub r0, pc,#4 \n"
		"b   sleep_wakeup_first\n"          //sleep_wakeup_first();
		"exit_addr: \n"
		"ldr r0,=MStar_Suspend_Buffer\n"
		"bl sleep_restore_cpu_registers\n"  //sleep_restore_cpu_registers(MStar_Suspend_Buffer)
		:::"r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r12"
	);

	printk("[%s] cpu resume...\n", __func__);

	sleep_restore_neon_regs(&MStar_Suspend_Buffer[SLEEPSTATE_NEONREG/WORD_SIZE]);
	restore_a9_scu((appf_u32 *)a9_scu_save,PERI_ADDRESS(PERI_PHYS));
	restore_mmu(mmu_data);
	restore_control_registers(control_data, 0);
	//restore_v7_debug((appf_u32 *)&a9_dbg_data_save);
	restore_gic_distributor_shared((appf_u32 *)gic_distributor_shared_save,(unsigned)_gic_dist_base_addr,1);
	gic_distributor_set_enabled(TRUE, (unsigned)_gic_dist_base_addr);//add
	restore_gic_distributor_private((appf_u32 *)gic_distributor_private_save,(unsigned)_gic_dist_base_addr,1);
	restore_gic_interface((appf_u32 *)gic_interface_save,(unsigned)_gic_cpu_base_addr,1);

	restore_cp15((appf_u32 *)cp15_save);

	restore_a9_timers((appf_u32*)&a9_timer_save, PERI_ADDRESS(PERI_PHYS));
	restore_a9_global_timer((appf_u32 *)a9_global_timer_save,PERI_ADDRESS(PERI_PHYS));
	restore_performance_monitors((appf_u32 *)performance_monitor_save);

	mstar_restore_int_mask();

	sleep_clear_wakeup_save_addr_phy(mstar_virt_to_phy((void*)WAKEUP_SAVE_ADDR),(void*)WAKEUP_SAVE_ADDR);
	platform_smp_boot_secondary_init();
	mstar_sleep_cur_cpu_flush();

	return 0;
}

int (*mst_pm_begin_cb)(suspend_state_t state);
int (*mst_pm_prepare_cb)(void);
int (*mst_pm_prepare_late_cb)(void);
void (*mst_pm_wake_cb)(void);
void (*mst_pm_finish_cb)(void);
bool (*mst_pm_suspend_again_cb)(void);
void (*mst_pm_end_cb)(void);
void (*mst_pm_recover_cb)(void);
EXPORT_SYMBOL(mst_pm_begin_cb);
EXPORT_SYMBOL(mst_pm_prepare_cb);
EXPORT_SYMBOL(mst_pm_prepare_late_cb);
EXPORT_SYMBOL(mst_pm_wake_cb);
EXPORT_SYMBOL(mst_pm_finish_cb);
EXPORT_SYMBOL(mst_pm_suspend_again_cb);
EXPORT_SYMBOL(mst_pm_end_cb);
EXPORT_SYMBOL(mst_pm_recover_cb);

static int mstar_pm_begin(suspend_state_t state)
{
	if (mst_pm_begin_cb)
		return (*mst_pm_begin_cb)(state);
	else {
		printk("[%s] Error! mst_pm_begin_cb = NULL, Please insert mst_str.ko first!\n", __func__);
		return -ENXIO;
	}
}

static int mstar_pm_prepare(void)
{
	if (mst_pm_prepare_cb)
		return (*mst_pm_prepare_cb)();
	return 0;
}

static int mstar_pm_prepare_late(void)
{
	if (mst_pm_prepare_late_cb)
		return (*mst_pm_prepare_late_cb)();
	return 0;
}

static void mstar_pm_wake(void)
{
	if (mst_pm_wake_cb)
		(*mst_pm_wake_cb)();
}

static void mstar_pm_finish(void)
{
	if (mst_pm_finish_cb)
		(*mst_pm_finish_cb)();
}

static bool mstar_pm_suspend_again(void)
{
	if (mst_pm_suspend_again_cb)
		return (*mst_pm_suspend_again_cb)();
	return 0;
}

static void mstar_pm_end(void)
{
	if (mst_pm_end_cb)
		return (*mst_pm_end_cb)();
}

static void mstar_pm_recover(void)
{
	if (mst_pm_recover_cb)
		return (*mst_pm_recover_cb)();
}

static struct platform_suspend_ops mstar_pm_ops =
{
	.valid            = suspend_valid_only_mem,
	.begin            = mstar_pm_begin,
	.prepare          = mstar_pm_prepare,
	.prepare_late     = mstar_pm_prepare_late,
	.enter            = mstar_pm_enter,
	.wake             = mstar_pm_wake,
	.finish           = mstar_pm_finish,
	.suspend_again    = mstar_pm_suspend_again,
	.end              = mstar_pm_end,
	.recover          = mstar_pm_recover,
};


/*------------------------------------------------------------------------------
    Function: mstar_pm_init

    Description:
        init function of power management
    Input: (The arguments were used by caller to input data.)
        None.
    Output: (The arguments were used by caller to receive data.)
        None.
    Return:
        0
    Remark:
        None.
-------------------------------------------------------------------------------*/
static int __init mstar_pm_init(void)
{
    /* set operation function of suspend */
    suspend_set_ops(&mstar_pm_ops);
    return 0;
}

__initcall(mstar_pm_init);

