#include	"DT_dt_ether_drv.h"	
/* For DynamicTracer-TestPoint */
#include	"DT_dt_ether_drv.h"	
/* For DynamicTracer-TestPoint */
/*==============================================================================*/
/*  Copyright (C) 2009-2015, Heartland Data inc. All Rights Reserved.           */
/*                                                                              */
/*  Title  :   Ethernet Driver                                                  */
/*  FileID :   37a                                                              */
/*  Version:   4.0         */
/*  Data   :   2016/03/28                                                     */
/*  Author :   HLDC                                                             */
/*==============================================================================*/

/*==============================================================================*/
/*  Please customize the code for your environment.                             */
/*==============================================================================*/

/*==============================================================================*/
/*  Desc:   Header for Ethernet Control                                         */
/*==============================================================================*/
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/syscalls.h>

/* cpufreq */
#include <linux/cpufreq.h>

/* meminfo */
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/delay.h>

/*==============================================================================*/
/*  Macro:  DT_UINT                                                             */
/*  Desc:   Please change Test Point argument type for DT10 Project setting.    */
/*==============================================================================*/
#define DT_UINT unsigned int

/*==============================================================================*/
/*  Macro:  DT_ETHER_XXX                                                        */
/*  Desc:   Please select Ethernet mode                                         */
/*==============================================================================*/
#define DT_ETHER_TCPIP_SERVER    0
#define DT_ETHER_TCPIP_CLIENT    1

/*==============================================================================*/
/*  Macro:  DT_INLINE                                                           */
/*  Desc:   Please use "static" instead of "inline" if "inline" cannot be used. */
/*==============================================================================*/
#define DT_INLINE inline
/* #define DT_INLINE static */

/*==============================================================================*/
/*  Macro:  DT_MAX_VAL_SIZE                                                     */
/*  Desc:   Please set the valule of Variables lenght.                          */
/*          But, never set the value over 256.                                  */
/*==============================================================================*/
#define DT_MAX_VAL_SIZE 256

/*==============================================================================*/
/*  Macro:  DT_ADD_CPU_INFO                                                     */
/*  Desc:   Please set 1 when you add CPU Value to Test Point.                  */
/*==============================================================================*/
#define DT_ADD_CPU_INFO    1

#define	PROC_NAME	"dt10proc"
#define MAX_LOG_SIZE	300000 * 18

static unsigned char *buff = NULL;
//static unsigned char buff[MAX_LOG_SIZE];
static unsigned int logWritePos = 0, logReadPos = 0;
static unsigned int dt10busout_flag = 0;
struct timespec now;
unsigned long long start_time;
unsigned long long time = 0;
static DEFINE_SPINLOCK(tplock);

/* meminfo */
static struct task_struct *dt10kthread = NULL;

struct DT10_DATA {
	char tp_func;
	unsigned int addr;
	unsigned int dat;
	unsigned int size;
	unsigned char value[256];
};

#define TP_BusOut		'3'
#define TP_MemoryOutput		'4'

/*==============================================================================*/
/*     Don't change the code from here as possible.                             */
/*==============================================================================*/

/*==============================================================================*/
/*  Macro:  Command Macros                                                      */
/*  Desc:   Please do not change the value.                                     */
/*==============================================================================*/
#define DT_NRMLTP_ID           0x11        /* Normal TP with Time info. */
#define DT_VALTP_ID            0x12        /* Variable TP with Time info. */
#define DT_KINFO_ID            0x16        /* KernelInfo TP with Time info. */

#define DT_TIME_INFO_SIZE      8
#define DT_EVENT_INFO_SIZE     0
#define DT_HEADER_SIZE         4
#define DT_NRMLTP_SIZE         (DT_HEADER_SIZE+6+DT_TIME_INFO_SIZE+DT_EVENT_INFO_SIZE)
#define DT_MAX_VALTP_SIZE      (DT_NRMLTP_SIZE+1+DT_MAX_VAL_SIZE)
#define	DT_KINFO_SIZE          (DT_NRMLTP_SIZE+4+1+20)

/*==============================================================================*/
/*  Func: _TP_BusOut                                                            */
/*  Desc: Called by Test Point                                                  */
/*==============================================================================*/
void _TP_BusOut(DT_UINT addr, DT_UINT dat)
{
	unsigned long flags;
#if DT_ADD_CPU_INFO
	unsigned int cpu_id;
#endif

	if (dt10busout_flag != 1 || buff == NULL) {
		return;
	}
	if (logWritePos+DT_NRMLTP_SIZE > MAX_LOG_SIZE) {
		return;
	}
	spin_lock_irqsave(&tplock, flags);
	getnstimeofday(&now);
	time = (unsigned long long)(now.tv_sec*1000*1000 + now.tv_nsec/1000);	/* us */
	time -= start_time;

#if DT_ADD_CPU_INFO
	cpu_id = smp_processor_id();
	addr |= (cpu_id << 18);
#endif
	/* tp data */
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = DT_NRMLTP_SIZE;
	buff[logWritePos++] = 0x00;
	buff[logWritePos++] = DT_NRMLTP_ID;		/* ID : Normal TP */
	buff[logWritePos++] = dat;
	buff[logWritePos++] = dat >> 8;
	buff[logWritePos++] = addr;
	buff[logWritePos++] = addr >> 8;
	buff[logWritePos++] = addr >> 16;
	/* time data */
	buff[logWritePos++] = time;
	buff[logWritePos++] = time >> 8;
	buff[logWritePos++] = time >> 16;
	buff[logWritePos++] = time >> 24;
	buff[logWritePos++] = time >> 32;
	buff[logWritePos++] = time >> 40;
	buff[logWritePos++] = time >> 48;
	buff[logWritePos++] = time >> 56;
	spin_unlock_irqrestore(&tplock, flags);
}
EXPORT_SYMBOL(_TP_BusOut);

/*==============================================================================*/
/*  Func: _TP_MemoryOutput                                                      */
/*  Desc: Called by Variable Test Point                                         */
/*==============================================================================*/
void _TP_MemoryOutput(DT_UINT addr, DT_UINT dat, void *value, DT_UINT size)
{
	unsigned long flags;
	unsigned char *p = (unsigned char*)value;
	unsigned int i;
#if DT_ADD_CPU_INFO
	unsigned int cpu_id;
#endif

	if (dt10busout_flag != 1 || buff == NULL) {
		return;
	}
	if (size >= DT_MAX_VAL_SIZE) {
		size = DT_MAX_VAL_SIZE;
	}
	if (logWritePos+DT_NRMLTP_SIZE+1+size > MAX_LOG_SIZE) {
		return;
	}
	spin_lock_irqsave(&tplock, flags);
	getnstimeofday(&now);
	time = (unsigned long long)(now.tv_sec*1000*1000 + now.tv_nsec/1000);	/* us */
	time -= start_time;

#if DT_ADD_CPU_INFO
	cpu_id = smp_processor_id();
	addr |= (cpu_id << 18);
#endif
	/* tp data */
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = (DT_NRMLTP_SIZE+1+size);
	buff[logWritePos++] = (DT_NRMLTP_SIZE+1+size) >> 8;
	buff[logWritePos++] = DT_VALTP_ID;		/* ID : Variable TP */
	buff[logWritePos++] = dat;
	buff[logWritePos++] = dat >> 8;
	buff[logWritePos++] = addr;
	buff[logWritePos++] = addr >> 8;
	buff[logWritePos++] = addr >> 16;
	/* time data */
	buff[logWritePos++] = time;
	buff[logWritePos++] = time >> 8;
	buff[logWritePos++] = time >> 16;
	buff[logWritePos++] = time >> 24;
	buff[logWritePos++] = time >> 32;
	buff[logWritePos++] = time >> 40;
	buff[logWritePos++] = time >> 48;
	buff[logWritePos++] = time >> 56;
	/* variable data */
	buff[logWritePos++] = size;
	for (i = 0; i < size; i++, p++) {
		buff[logWritePos++] = *p;
	}
	spin_unlock_irqrestore(&tplock, flags);
}
EXPORT_SYMBOL(_TP_MemoryOutput);

/*==============================================================================*/
/*  Func:   _TP_BusKernelInfo                                                   */
/*  Desc:   Called by KernelInfo TestPoint                                      */
/*==============================================================================*/
void _TP_BusKernelInfo(unsigned int addr, unsigned int dat, struct task_struct *next)
{
	unsigned long flags;
#if DT_ADD_CPU_INFO
	unsigned int cpu_id;
#endif
	unsigned int tgid = next->mm?next->tgid:0;

	if (dt10busout_flag != 1 || buff == NULL) {
		return;
	}
	if (logWritePos+DT_KINFO_SIZE > MAX_LOG_SIZE) {
		return;
	}
	spin_lock_irqsave(&tplock, flags);
	getnstimeofday(&now);
	time = (unsigned long long)(now.tv_sec*1000*1000 + now.tv_nsec/1000);	/* us */
	time -= start_time;

#if DT_ADD_CPU_INFO
	cpu_id = smp_processor_id();
	addr |= (cpu_id << 18);
#endif
	/* tp data */
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = 0xFF;
	buff[logWritePos++] = DT_KINFO_SIZE;
	buff[logWritePos++] = 0x00;
	buff[logWritePos++] = DT_KINFO_ID;		/* ID : KernelInfo TP */
	buff[logWritePos++] = dat;
	buff[logWritePos++] = dat >> 8;
	buff[logWritePos++] = addr;
	buff[logWritePos++] = addr >> 8;
	buff[logWritePos++] = addr >> 16;
	/* time data */
	buff[logWritePos++] = time;
	buff[logWritePos++] = time >> 8;
	buff[logWritePos++] = time >> 16;
	buff[logWritePos++] = time >> 24;
	buff[logWritePos++] = time >> 32;
	buff[logWritePos++] = time >> 40;
	buff[logWritePos++] = time >> 48;
	buff[logWritePos++] = time >> 56;
	/* Process ID */
	buff[logWritePos++] = next->pid;
	buff[logWritePos++] = next->pid >> 8;
	buff[logWritePos++] = next->pid >> 16;
	buff[logWritePos++] = next->pid >> 24;

	/* Size of Value(Process ID: 4Byte and Command: 16Byte) */
	buff[logWritePos++] = 20;

	/* Thread Group ID */
	buff[logWritePos++] =  tgid;
	buff[logWritePos++] =  tgid >> 8;
	buff[logWritePos++] =  tgid >> 16;
	buff[logWritePos++] =  tgid >> 24;

	/* Command */
	memcpy(&buff[logWritePos], next->comm, TASK_COMM_LEN);
	logWritePos += 16;
	spin_unlock_irqrestore(&tplock, flags);
}
EXPORT_SYMBOL(_TP_BusKernelInfo);




/*==============================================================================*/
/*  Func:   dt10_cpu_freq                                                   	*/
/*  Desc:   getting core frequency                                    			*/
/*==============================================================================*/
void dt10_cpu_freq(unsigned int cpu, unsigned long freq)
{
	unsigned long cpufreq[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	cpufreq[cpu] = freq;
	
	switch(cpu)
	{
		case 0:
		//DT-VarPT cpufreq[0]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_0 );
		break;
		case 1:
		//DT-VarPT cpufreq[1]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_1 );
		break;
		case 2:
		//DT-VarPT cpufreq[2]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_2 );
		break;
		case 3:
		//DT-VarPT cpufreq[3]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_3 );
		break;
		case 4:
		//DT-VarPT cpufreq[4]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_4 );
		break;
		case 5:
		//DT-VarPT cpufreq[5]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_5 );
		break;
		case 6:
		//DT-VarPT cpufreq[6]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_6 );
		break;
		case 7:
		//DT-VarPT cpufreq[7]
		__DtTestPoint( __DtFunc_dt10_cpu_freq, __DtStep_7 );
		default:
		break;
	}
}
EXPORT_SYMBOL(dt10_cpu_freq);

/*==============================================================================*/
/*  Func:   dt10_mem_check                                                   	*/
/*  Desc:   getting available memsize                                    		*/
/*==============================================================================*/
#define K(x) ((x) << (PAGE_SHIFT - 10))
#define	DT_MEMINFO_OUTPUT_PERIOD_TIME	100

static int dt10_mem_check(void *arg)
{
	struct sysinfo i;
	unsigned long available_memsize;
	long cached;

	while (!kthread_should_stop()) {
		si_meminfo(&i);
		cached = global_page_state(NR_FILE_PAGES) - total_swapcache_pages() - i.bufferram;
	
		if (cached < 0) {
			cached = 0;
		}
		available_memsize = K(i.freeram)  + K(i.bufferram) + K(cached);

		/* dt10meminfo available_memsize */
		//DT-VarPT available_memsize
		__DtTestPoint( __DtFunc_dt10_mem_check, __DtStep_0 );
		msleep(DT_MEMINFO_OUTPUT_PERIOD_TIME);
	}
	return 0;
}
/*==============================================================================*/

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static ssize_t dt10_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
#else
static int dt10_proc_write(struct file *filp, const char *buf, unsigned long len, void *data)
#endif
{
	struct timespec now;
	struct DT10_DATA dt10_data;
	unsigned long cpufreq;
	int i;

	if (copy_from_user(&dt10_data, buf, len)) {
		return -EFAULT;
	}

	switch (dt10_data.tp_func) {
		case '0':
			dt10busout_flag = 0;
			if (buff) {
				vfree(buff);
				buff = NULL;
			}
			logWritePos = 0;
			logReadPos = 0;
			printk( KERN_INFO "[DT10] Trace Stop.\n" );
			break;
		case '1':
			if (buff == NULL) {
				buff = vmalloc(MAX_LOG_SIZE);
				if (buff == NULL) {
					printk(KERN_INFO "DT10 Cannot alloc vmalloc\n");
					return -EFAULT;
				}
				getnstimeofday(&now);
				start_time = (unsigned long long)((now.tv_sec*1000*1000) + (now.tv_nsec/1000));
			}
			dt10kthread = kthread_run(dt10_mem_check, NULL, "dt10_mem_check");
			if (IS_ERR(dt10kthread)) {
				printk(KERN_INFO "DT10 Cannot run kthread\n");
				return len;
			}
			dt10busout_flag = 1;
			printk( KERN_INFO "[DT10] Trace Start.\n" );
			for_each_online_cpu(i) {
				cpufreq = cpufreq_get(i);
				dt10_cpu_freq(cpufreq, i);
			}
			break;
		case TP_BusOut:
			_TP_BusOut(dt10_data.addr, dt10_data.dat);
			break;
		case TP_MemoryOutput:
			_TP_MemoryOutput(dt10_data.addr, dt10_data.dat, dt10_data.value, dt10_data.size);
			break;
		default:
			break;
	}

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static ssize_t dt10_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *data)
#else
static int dt10_proc_read(char *buf, char **start, off_t off, int count, int *eof, void *data)
#endif
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&tplock, flags);
	for (i = 0; ((i < count) && (logReadPos < logWritePos)); i++) {
		buf[i] = buff[logReadPos++];
	}
	if (logReadPos == logWritePos) {
		logReadPos = 0;
		logWritePos = 0;
	}
#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	*start = buf;
	if (i == 0) {
		*eof = 1;
	}
#endif
	spin_unlock_irqrestore(&tplock, flags);

	return i;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static const struct file_operations dt_proc_fops = {
	.write = dt10_proc_write,
	.read = dt10_proc_read,
};
#endif

static int __init dt_ether_init(void)
{
	struct proc_dir_entry* entry;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	entry = proc_create(PROC_NAME, 666, NULL, &dt_proc_fops);
	if ( !entry ) {
		printk( KERN_ERR "proc entry fail\n" );
		return -EBUSY;
	}
#else
	entry = create_proc_entry(PROC_NAME, 666, NULL);
	entry->read_proc = dt10_proc_read;
	entry->write_proc = dt10_proc_write;
#endif
	printk( KERN_INFO "dt10proc load OK\n" );

//	buff = vmalloc(MAX_LOG_SIZE);	
//	if( buff == NULL ){
//	    printk(KERN_INFO "DT10 Cannot alloc vmalloc\n");
//	}	

	return 0;
}

static void __exit dt_ether_exit(void)
{
//	vfree(buff);
	remove_proc_entry(PROC_NAME, NULL);
	printk( KERN_INFO "dt10proc unload\n" );
}

module_init( dt_ether_init );
module_exit( dt_ether_exit );

MODULE_DESCRIPTION("dt_ether_drv");
MODULE_LICENSE("GPL2");

