
/******************************************************************
* 		File : sdp_messagebox.c
*		Description : 
*		Author : douk.namm@samsung.com		
*		Date : 2014/7/29
*******************************************************************/

//#define SDP_MC_HANDLER_VERSTR	"0.3(decrease buf size, refactoring)"
//#define SDP_MC_HANDLER_VERSTR	"0.4(add timer using when irq is delayed for receive msg)"
//#define SDP_MC_HANDLER_VERSTR	"0.5(change timeout reset point)"
//#define SDP_MC_HANDLER_VERSTR	"0.6(avoid timeout wq run at irq running cpu)"
//#define SDP_MC_HANDLER_VERSTR	"0.7 fix SVACE defects(120350, 120351, 120352)"
//#define SDP_MC_HANDLER_VERSTR	"1.0 new start for kernel 4.1"
//#define SDP_MC_HANDLER_VERSTR	"1.1 fix include soc.h, change register base"
//#define SDP_MC_HANDLER_VERSTR	"1.2 add VD RamDUMP interrupt handling"
#define SDP_MC_HANDLER_VERSTR	"1.3 bugfix out of bounds access in write func"

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>

#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <soc/sdp/soc.h>
#include <soc/sdp/sdp_micom_ir.h>


#define MESSAGE_SIZE_MAX 28

#define MESSAGE_SEND		0x1
#define MESSAGE_UPDATE		0x8/* only Main -> Micom */
#define MESSAGE_VD_RAMDUMP	0x10/* only Micom -> Main */
#define MESSAGE_DEBUG		0x80

#define RECVBOX_OFF 	(0x00)
#define RECV_DATA0_OFF	(RECVBOX_OFF+0x00)
#define RECV_DATA1_OFF	(RECVBOX_OFF+0x04)
#define RECV_DATA2_OFF	(RECVBOX_OFF+0x08)
#define RECV_DATA3_OFF	(RECVBOX_OFF+0x0c)
#define RECV_DATA4_OFF	(RECVBOX_OFF+0x10)
#define RECV_DATA5_OFF	(RECVBOX_OFF+0x14)
#define RECV_DATA6_OFF	(RECVBOX_OFF+0x18)
#define RECV_DATA7_OFF	(RECVBOX_OFF+0x1c)
#define RECV_PEND_OFF	(RECVBOX_OFF+0x40)
#define RECV_CLEAR_OFF	(RECVBOX_OFF+0x44)

#define SENDBOX_OFF 	(0x80)
#define SEND_DATA0_OFF	(SENDBOX_OFF+0x00)
#define SEND_DATA1_OFF	(SENDBOX_OFF+0x04)
#define SEND_DATA2_OFF	(SENDBOX_OFF+0x08)
#define SEND_DATA3_OFF	(SENDBOX_OFF+0x0c)
#define SEND_DATA4_OFF	(SENDBOX_OFF+0x10)
#define SEND_DATA5_OFF	(SENDBOX_OFF+0x14)
#define SEND_DATA6_OFF	(SENDBOX_OFF+0x18)
#define SEND_DATA7_OFF	(SENDBOX_OFF+0x1c)
#define SEND_PEND_OFF	(SENDBOX_OFF+0x40)
#define SEND_CLEAR_OFF	(SENDBOX_OFF+0x44)

#define UPDATE_READY_DONE (0x656e6f64)

#define SDP_MC_HANDLER_IRQ_IN_TIMEOUT (-12345)
#define SDP_MC_HANDLER_RINGBUF_SIZE 16U
//#define SDP_MC_HANDLER_TEST_MODE

typedef void (*sdp_messagebox_callback_t)(unsigned char*pBuffer, unsigned int size);


#ifdef CONFIG_INVOKE_MESSAGEBOX_ISR_VD_RAMDUMP
void invoke_oops_for_vd_ramdump(void) {
	char *killer = NULL;

	panic_on_oops = 1;      /* force panic */
	wmb();
	*killer = 1;
}
#endif

/* rx ring buffer */
struct sdp_mc_ringbuf_t {
	spinlock_t lock;
	unsigned long head;
	unsigned long tail;
	unsigned long __pading[1];

	unsigned long long elapsed_max_time_ns;
	unsigned long long __padding2;


	struct {
		unsigned long long enqueue_ns;
		unsigned long long dequeue_ns;
		u32 buf[8];
	} data[SDP_MC_HANDLER_RINGBUF_SIZE];
};

struct sdp_mc_handler_t {
	struct device *dev;
	spinlock_t recvlock;/* lock for recv register access*/
	struct mutex send_mutex;
	struct mutex recv_mutex;
	struct sdp_mc_ringbuf_t *rx_ringbuf;
	sdp_messagebox_callback_t read_callback;

	unsigned int irq;
	int irq_affinity;
	void __iomem *base;

	/* timeout */
	struct timer_list recv_timeout_timer;
	struct work_struct recv_timeout_work;
	int recv_timeout_cpu;
	u32 recv_timeout_ms;

	/* curent process */
	unsigned long long	cur_isr_start_time_ns;
	unsigned long long	cur_thread_start_time_ns;
	unsigned long long	cur_callback_start_time_ns;

	/* statistics */
	u32 total_send_count;
	u32 total_recv_count;
	u32 total_recv_drop_count;
	u32 total_recv_thread_count;
	unsigned long long	isr_last_called_time_ns;
	unsigned long long	thread_last_called_time_ns;
	unsigned long long	isr_process_max_time_ns;
	unsigned long long	thread_process_max_time_ns;
	unsigned long long	callback_process_max_time_ns;

	/* debugfs */
	struct dentry *dbgfs_root;
	u32 callback_timeout_us;
	u32 dbgfs_test_mode;

#ifdef CONFIG_SDP_MICOM_IRB
	sdp_micom_irr_cb irr_callback;
	void *irr_priv;
#endif/*CONFIG_SDP_MICOM_IRB*/
};

/* XXX: below, global variables are used temporary. */
static struct sdp_mc_handler_t *g_mchnd = NULL;

static unsigned long long sdp_mc_handler_get_nsecs(void)
{
	return sched_clock();
}

#ifdef CONFIG_SDP_MICOM_IRB
int sdp_messagebox_register_irr_cb(sdp_micom_irr_cb irrcb_fn, void *priv)
{
	struct sdp_mc_handler_t *mchnd = g_mchnd;

	if(mchnd->irr_callback != NULL) {
		return -EBUSY;
	}

	mchnd->irr_callback = irrcb_fn;
	mchnd->irr_priv = priv;

	return 0;
}
EXPORT_SYMBOL(sdp_messagebox_register_irr_cb);
#endif/*CONFIG_SDP_MICOM_IRB*/

/* Ring buffer func */
static void sdp_mc_ringbuf_init(struct sdp_mc_ringbuf_t *ringbuf)
{
	spin_lock_init(&ringbuf->lock);
	ringbuf->head = 0;
	ringbuf->tail = 0;
	ringbuf->elapsed_max_time_ns = 0;

	return;
}

static u32 sdp_mc_ringbuf_count(struct sdp_mc_ringbuf_t *ringbuf)
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	tail = ACCESS_ONCE(ringbuf->tail);
	ret = CIRC_CNT(head, tail, SDP_MC_HANDLER_RINGBUF_SIZE);
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static int sdp_mc_ringbuf_enqueue(struct sdp_mc_ringbuf_t *ringbuf, u32 in_data[8])
{
	int ret = 0;
	unsigned long flags;
	unsigned long tail;

	spin_lock_irqsave(&ringbuf->lock, flags);

	tail = ACCESS_ONCE(ringbuf->tail);
	if(CIRC_SPACE(ringbuf->head, tail, SDP_MC_HANDLER_RINGBUF_SIZE) >= 1) {
		ringbuf->data[ringbuf->head].enqueue_ns = sdp_mc_handler_get_nsecs();
		ringbuf->data[ringbuf->head].dequeue_ns = 0;
		memcpy(ringbuf->data[ringbuf->head].buf, in_data, 4*8);
		ringbuf->head = (ringbuf->head + 1) & (SDP_MC_HANDLER_RINGBUF_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static int sdp_mc_ringbuf_dequeue(struct sdp_mc_ringbuf_t *ringbuf, u32 out_data[8])
{
	int ret = 0;
	unsigned long flags;
	unsigned long head;

	spin_lock_irqsave(&ringbuf->lock, flags);

	head = ACCESS_ONCE(ringbuf->head);
	if(CIRC_CNT(head, ringbuf->tail, SDP_MC_HANDLER_RINGBUF_SIZE) >= 1) {
		memcpy(out_data, ringbuf->data[ringbuf->tail].buf, 4*8);
		ringbuf->data[ringbuf->tail].dequeue_ns = sdp_mc_handler_get_nsecs();
		if(ringbuf->elapsed_max_time_ns < (ringbuf->data[ringbuf->tail].dequeue_ns - ringbuf->data[ringbuf->tail].enqueue_ns)) {
			ringbuf->elapsed_max_time_ns = (ringbuf->data[ringbuf->tail].dequeue_ns - ringbuf->data[ringbuf->tail].enqueue_ns);
		}
		ringbuf->tail = (ringbuf->tail + 1) & (SDP_MC_HANDLER_RINGBUF_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

/********************* ISR **************************/
static irqreturn_t messagebox_isr(int irq, void* dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_mc_handler_t *mchnd = NULL;
	unsigned int u32GicSync, pend;
	unsigned long long	isr_process_time_ns = 0;
	const char caller = (irq == SDP_MC_HANDLER_IRQ_IN_TIMEOUT) ? 'T':'I';
	int ret = 0, is_normal = true;
	irqreturn_t irqret = IRQ_HANDLED;

	mchnd = platform_get_drvdata(pdev);
	if(!mchnd) {
		dev_err(dev, "ISR.%c: host is not allocated!", caller);
		return IRQ_NONE;
	}

#ifdef CONFIG_INVOKE_MESSAGEBOX_ISR_VD_RAMDUMP
	pend = readl(mchnd->base+RECV_PEND_OFF);
	if(pend & MESSAGE_VD_RAMDUMP) {
		dev_crit(dev, "ISR.%c: VD Ramdump interrupt occurred!! (pend=0x%08x)", caller, pend);
		writel(MESSAGE_VD_RAMDUMP, mchnd->base+RECV_CLEAR_OFF);

		invoke_oops_for_vd_ramdump();/*not returned*/
	}
#endif

#ifdef SDP_MC_HANDLER_TEST_MODE
	if(irq != SDP_MC_HANDLER_IRQ_IN_TIMEOUT) {
		if(readl(&mchnd->dbgfs_test_mode)&0x2) {
			u32 ms = 50;
			while(ms--) udelay(1000);
		}

		if(readl(&mchnd->dbgfs_test_mode)&0x4) {
			disable_irq_nosync(mchnd->irq);
			return IRQ_HANDLED;
		}
	}
#endif

	spin_lock(&mchnd->recvlock);

	pend = readl(mchnd->base+RECV_PEND_OFF);

	if(!pend) {
		if (irq != SDP_MC_HANDLER_IRQ_IN_TIMEOUT) {
			dev_dbg(dev, "ISR.%c: no pending bits 0x%08x!\n", caller, pend);
		}
		writel(0xff, mchnd->base+RECV_CLEAR_OFF);
		u32GicSync = readl(mchnd->base+RECV_CLEAR_OFF);

		irqret = IRQ_NONE;
		goto unlock;
	}

	mchnd->cur_isr_start_time_ns = sdp_mc_handler_get_nsecs();

	if (MESSAGE_SEND & pend) {	
		u32 buf[8];

		buf[0] = readl(mchnd->base+RECV_DATA0_OFF);
		buf[1] = readl(mchnd->base+RECV_DATA1_OFF);
		buf[2] = readl(mchnd->base+RECV_DATA2_OFF);	
		buf[3] = readl(mchnd->base+RECV_DATA3_OFF);		
		buf[4] = readl(mchnd->base+RECV_DATA4_OFF);
		buf[5] = readl(mchnd->base+RECV_DATA5_OFF);	
		buf[6] = readl(mchnd->base+RECV_DATA6_OFF);		
		buf[7] = readl(mchnd->base+RECV_DATA7_OFF);	

		ret = sdp_mc_ringbuf_enqueue(mchnd->rx_ringbuf, buf);

#ifdef CONFIG_SDP_MICOM_IRB
		if(mchnd->irr_callback) {
			/* 0xCC10FFFF CC:code 01:event type */
			enum sdp_ir_event_e event = (buf[1] >> 16) & 0xFF;
			unsigned int code = (buf[1] >> 24) & 0xFF;
			unsigned int is_xmp_repeat = (buf[2]>>8) & 0xFF;

			if(is_xmp_repeat == 0x1){

				switch(event) {
				case SDP_IR_EVT_KEYPRESS:
				case SDP_IR_EVT_KEYRELEASE:
				case SDP_IR_EVT_KEY_UNDEFINED:
					//print_hex_dump(KERN_ERR, "ISR: IRR Raw: 0x", DUMP_PREFIX_NONE, 16, 4, &buf[1], buf[0], false);
					mchnd->irr_callback(event, code, mchnd->cur_isr_start_time_ns, mchnd->irr_priv);
					break;
				default:
					break;
				}
			}
		}
#endif
			
		writel(MESSAGE_SEND, mchnd->base+RECV_CLEAR_OFF);
		u32GicSync = readl(mchnd->base+RECV_CLEAR_OFF);

		if(ret < 0) {
			struct irq_desc *desc = irq_to_desc(mchnd->irq);
			unsigned long long thread_last_sec, thread_last_point;
			unsigned int wqbusy = 0;

			thread_last_sec = div64_u64(mchnd->thread_last_called_time_ns, 100000UL);/* to div 100us */
			thread_last_point = thread_last_sec;
			thread_last_sec = div64_u64(thread_last_sec, 10000UL);
			thread_last_point = thread_last_point - (thread_last_sec*10000UL);

			wqbusy = work_busy(&mchnd->recv_timeout_work);

			is_normal = false;
			mchnd->total_recv_drop_count++;
			dev_err(dev, "ISR.%c: ring is full!(T[%d] %llu.%04llus, WQ[%d%s%s])\n",
				caller, atomic_read(&desc->threads_active),
				thread_last_sec, thread_last_point,
				mchnd->recv_timeout_cpu, wqbusy&WORK_BUSY_PENDING?"P":"", wqbusy&WORK_BUSY_RUNNING?"R":"");
			print_hex_dump(KERN_DEBUG, "ISR: Recv Data: 0x", DUMP_PREFIX_OFFSET, 16, 4, buf, 0x20, false);
		} else {
			mchnd->total_recv_count++;
		}

		irqret = IRQ_WAKE_THREAD;
	}


	if(is_normal) {
		/* statistics update */
		isr_process_time_ns = sdp_mc_handler_get_nsecs() - mchnd->cur_isr_start_time_ns;
		if(mchnd->isr_process_max_time_ns < isr_process_time_ns) {
			mchnd->isr_process_max_time_ns = isr_process_time_ns;
		}
	}

	/* clear invalid pending bits */
	if(pend & ((~MESSAGE_SEND)&0xff)) {
		dev_warn(dev, "ISR.%c: invalid pending bits 0x%08x!\n", caller, pend);
		writel(pend & ((~MESSAGE_SEND)&0xff), mchnd->base+RECV_CLEAR_OFF);
		u32GicSync = readl(mchnd->base+RECV_CLEAR_OFF);
	}

	if(irq != SDP_MC_HANDLER_IRQ_IN_TIMEOUT) {
		mchnd->isr_last_called_time_ns = mchnd->cur_isr_start_time_ns;
		if(sdp_mc_ringbuf_count(mchnd->rx_ringbuf) >= 2) {
			mod_timer(&mchnd->recv_timeout_timer, jiffies + msecs_to_jiffies(mchnd->recv_timeout_ms));
		}
	}
	mchnd->cur_isr_start_time_ns = 0;

unlock:
	spin_unlock(&mchnd->recvlock);

	return irqret;
}

static irqreturn_t messagebox_thread(int irq, void* dev) {
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_mc_handler_t *mchnd = NULL;
	unsigned long long	thread_process_time_ns = 0;
	const char caller = (irq == SDP_MC_HANDLER_IRQ_IN_TIMEOUT) ? 'T':'I';
	irqreturn_t irqret = IRQ_NONE;

	mchnd = platform_get_drvdata(pdev);
	if(unlikely(!mchnd)) {
		dev_err(dev, "Thread.%c: host is not allocated!", caller);
		return IRQ_NONE;
	}

#ifdef SDP_MC_HANDLER_TEST_MODE
	if(irq != SDP_MC_HANDLER_IRQ_IN_TIMEOUT) {
		while(readl(&mchnd->dbgfs_test_mode)&0x1);
	}
#endif

	mutex_lock(&mchnd->recv_mutex);

	mchnd->cur_thread_start_time_ns = sdp_mc_handler_get_nsecs();

	if(mchnd->rx_ringbuf) {
		unsigned long long	callback_process_time_ns = 0;
		u32 dst[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

		while(sdp_mc_ringbuf_dequeue(mchnd->rx_ringbuf, dst) >= 0) {
			unsigned int msg_size = 0;

			irqret = IRQ_HANDLED;

			msg_size = dst[0];
			if (msg_size > MESSAGE_SIZE_MAX || !msg_size) {
				dev_err(dev, "Thread.%c: invalid msg size %d", caller, msg_size);
				print_hex_dump(KERN_ERR, "Thread: Recv Data: 0x", DUMP_PREFIX_OFFSET, 16, 4, dst, 0x20, false);
				continue;
			}

			mchnd->cur_callback_start_time_ns = sdp_mc_handler_get_nsecs();

			if (mchnd->read_callback) {
				(*mchnd->read_callback)((unsigned char*)&dst[1], msg_size);
			}

			/* statistics update */
			callback_process_time_ns = sdp_mc_handler_get_nsecs() - mchnd->cur_callback_start_time_ns;
			if(mchnd->callback_process_max_time_ns < callback_process_time_ns) {
				mchnd->callback_process_max_time_ns = callback_process_time_ns;
			}

			if(mchnd->callback_timeout_us && (callback_process_time_ns > ((unsigned long long)mchnd->callback_timeout_us*1000))) {
				dev_warn(dev, "Thread.%c: callback process time is over %uus!(%lluns)\n", caller, mchnd->callback_timeout_us, callback_process_time_ns);
				print_hex_dump(KERN_WARNING, "Thread: Recv Data: 0x", DUMP_PREFIX_OFFSET, 16, 4, dst, 0x20, false);
			}

			mchnd->cur_callback_start_time_ns = 0;
		}
	}

	/* statistics update */
	mchnd->total_recv_thread_count++;
	thread_process_time_ns = sdp_mc_handler_get_nsecs() - mchnd->cur_thread_start_time_ns;
	if(mchnd->thread_process_max_time_ns < thread_process_time_ns) {
		mchnd->thread_process_max_time_ns = thread_process_time_ns;
	}
	if(irq != SDP_MC_HANDLER_IRQ_IN_TIMEOUT) {
		mchnd->thread_last_called_time_ns = mchnd->cur_thread_start_time_ns;
	} else {
		/* isr thread delay is end! restore normal mode(1s) */
		mod_timer(&mchnd->recv_timeout_timer, jiffies + msecs_to_jiffies(1000));
	}
	mchnd->cur_thread_start_time_ns = 0;

	mutex_unlock(&mchnd->recv_mutex);
	return irqret;
}

static void messagebox_timeout_work(struct work_struct *w)
{
	struct sdp_mc_handler_t *mchnd = container_of(w,struct sdp_mc_handler_t, recv_timeout_work);
	irqreturn_t irqret = IRQ_NONE;

	irqret = messagebox_thread(SDP_MC_HANDLER_IRQ_IN_TIMEOUT, mchnd->dev);
	if(irqret == IRQ_HANDLED) {
		unsigned long long thread_last_sec, thread_last_point;
		struct irq_desc *desc = irq_to_desc(mchnd->irq);

		thread_last_sec = div64_u64(mchnd->thread_last_called_time_ns, 100000UL);/* to div 100us */
		thread_last_point = thread_last_sec;
		thread_last_sec = div64_u64(thread_last_sec, 10000UL);
		thread_last_point = thread_last_point - (thread_last_sec*10000UL);

		dev_warn(mchnd->dev, "WorkQ: rx buffer handled(last: thread[%d] %llu.%04llus)\n",
			atomic_read(&desc->threads_active), thread_last_sec, thread_last_point);
	} else {
		//dev_info(mchnd->dev, "WorkQ: is not handle by workqueue!\n");
	}
}

static void messagebox_timeout_timercb(unsigned long data) {
	struct sdp_mc_handler_t *mchnd = (struct sdp_mc_handler_t *)data;
	irqreturn_t irqret = IRQ_NONE;
	int rxcnt = 0, ret = 0, cpu = 0;
	unsigned long flags;
	struct cpumask wq_cpumask;

	local_irq_save(flags);
	irqret = messagebox_isr(SDP_MC_HANDLER_IRQ_IN_TIMEOUT, mchnd->dev);
	local_irq_restore(flags);

	rxcnt = sdp_mc_ringbuf_count(mchnd->rx_ringbuf);
	if(rxcnt > 0) {
		unsigned long long isr_last_sec, isr_last_point;
		unsigned long long thread_last_sec, thread_last_point;
		struct irq_desc *desc = irq_to_desc(mchnd->irq);

		isr_last_sec = div64_u64(mchnd->isr_last_called_time_ns, 100000UL);/* to div 100us */
		isr_last_point = isr_last_sec;
		isr_last_sec = div64_u64(isr_last_sec, 10000UL);
		isr_last_point = isr_last_point - (isr_last_sec*10000UL);

		thread_last_sec = div64_u64(mchnd->thread_last_called_time_ns, 100000UL);/* to div 100us */
		thread_last_point = thread_last_sec;
		thread_last_sec = div64_u64(thread_last_sec, 10000UL);
		thread_last_point = thread_last_point - (thread_last_sec*10000UL);

		mod_timer(&mchnd->recv_timeout_timer, jiffies + msecs_to_jiffies(mchnd->recv_timeout_ms));

		/* avoid workqueue run at irq running cpu */
		cpu = smp_processor_id();
		if(cpu == mchnd->irq_affinity) {
			ret = cpumask_andnot(&wq_cpumask, cpu_online_mask, get_cpu_mask(mchnd->irq_affinity));
			if(ret != 0) {
				/* wq_cpumask not empty */
				cpu = cpumask_first(&wq_cpumask);
			}
		}
#ifdef SDP_MC_HANDLER_TEST_MODE
		if(readl(&mchnd->dbgfs_test_mode)&0x8) {
			cpu = mchnd->irq_affinity;
		}
#endif
		ret = schedule_work_on(cpu, &mchnd->recv_timeout_work);
		if(ret != 0) {
			mchnd->recv_timeout_cpu = cpu;
		}

		if(irqret == IRQ_WAKE_THREAD) {
			dev_warn(mchnd->dev, "Timer: pending data handled(%d, last: irq %llu.%04llus, thread[%d] %llu.%04llus)\n",
				rxcnt, isr_last_sec, isr_last_point, atomic_read(&desc->threads_active), thread_last_sec, thread_last_point);
		} else {
			if(ret != 0) {
				dev_warn(mchnd->dev, "Timer: no pending, rxring not empty(%d, last: irq %llu.%04llus, thread[%d] %llu.%04llus)\n",
					rxcnt, isr_last_sec, isr_last_point, atomic_read(&desc->threads_active), thread_last_sec, thread_last_point);
			}
		}
	} else {
		//dev_info(mchnd->dev, "Timer: is not handle by timer!\n");
	}
}

void sdp_messagebox_debug_print(void)
{
	struct sdp_mc_handler_t *mchnd = g_mchnd;
	int i = 0;
	unsigned long head = 0;
	unsigned long long start_time_ns = 0;
	unsigned long long now = sdp_mc_handler_get_nsecs();

	if(!mchnd) {
		pr_err("%s %d: host is not allocated!\n", __FUNCTION__, __LINE__);
		return;
	}

	dev_info(mchnd->dev, "===== MessageBox Status Start =====\n");

	dev_info(mchnd->dev, "Recv Pend: %08x\n", readl(mchnd->base+RECV_PEND_OFF));
	dev_info(mchnd->dev, "Recv Data: %08x %08x %08x %08x\n",
		readl(mchnd->base+RECV_DATA0_OFF), readl(mchnd->base+RECV_DATA1_OFF), readl(mchnd->base+RECV_DATA2_OFF), readl(mchnd->base+RECV_DATA3_OFF));
	dev_info(mchnd->dev, "Recv Data: %08x %08x %08x %08x\n",
		readl(mchnd->base+RECV_DATA4_OFF), readl(mchnd->base+RECV_DATA5_OFF), readl(mchnd->base+RECV_DATA6_OFF), readl(mchnd->base+RECV_DATA7_OFF));

	dev_info(mchnd->dev, "Send Pend: %08x\n", readl(mchnd->base+SEND_PEND_OFF));
	dev_info(mchnd->dev, "Send Data: %08x %08x %08x %08x\n",
		readl(mchnd->base+SEND_DATA0_OFF), readl(mchnd->base+SEND_DATA1_OFF), readl(mchnd->base+SEND_DATA2_OFF), readl(mchnd->base+SEND_DATA3_OFF));
	dev_info(mchnd->dev, "Send Data: %08x %08x %08x %08x\n",
		readl(mchnd->base+SEND_DATA4_OFF), readl(mchnd->base+SEND_DATA5_OFF), readl(mchnd->base+SEND_DATA6_OFF), readl(mchnd->base+SEND_DATA7_OFF));


	start_time_ns = ACCESS_ONCE(mchnd->cur_isr_start_time_ns);
	if(start_time_ns) {
		dev_info(mchnd->dev, "ISR is running. start time %lluns, elapsed time %lluns\n",
			start_time_ns, now - start_time_ns);
	}

	start_time_ns = ACCESS_ONCE(mchnd->cur_thread_start_time_ns);
	if(start_time_ns) {
		dev_info(mchnd->dev, "Thread is running. start time %lluns, elapsed time %lluns\n",
			start_time_ns, now - start_time_ns);
	}

	start_time_ns = ACCESS_ONCE(mchnd->cur_callback_start_time_ns);
	if(start_time_ns) {
		dev_info(mchnd->dev, "Callback is running. start time %lluns, elapsed time %lluns\n",
			start_time_ns, now - start_time_ns);
	}

	dev_info(mchnd->dev, "total_send_count              : %u\n", mchnd->total_send_count);
	dev_info(mchnd->dev, "total_recv_count              : %u\n", mchnd->total_recv_count);
	dev_info(mchnd->dev, "total_recv_drop_count         : %u\n", mchnd->total_recv_drop_count);
	dev_info(mchnd->dev, "rx_ring_count                 : %u/%u\n", sdp_mc_ringbuf_count(mchnd->rx_ringbuf), SDP_MC_HANDLER_RINGBUF_SIZE);
	dev_info(mchnd->dev, "rx_ring elapsed_max_time_ns   : %lluns\n", mchnd->rx_ringbuf->elapsed_max_time_ns);
	dev_info(mchnd->dev, "isr_last_called_time_ns       : %lluns\n", mchnd->isr_last_called_time_ns);
	dev_info(mchnd->dev, "isr_process_max_time_ns       : %lluns\n", mchnd->isr_process_max_time_ns);
	dev_info(mchnd->dev, "thread_process_max_time_ns    : %lluns\n", mchnd->thread_process_max_time_ns);
	dev_info(mchnd->dev, "callback_process_max_time_ns  : %lluns\n", mchnd->callback_process_max_time_ns);

	dev_info(mchnd->dev, "rx ring dump ( now %lluns)\n", now);
	dev_info(mchnd->dev, "nr@id: elapsed(start_time) data values\n");

	head = ACCESS_ONCE(mchnd->rx_ringbuf->head);

	for(i = 1; i <= SDP_MC_HANDLER_RINGBUF_SIZE; i++) {
		unsigned long long enque, deque, elapsed;
		unsigned long long enque_sec, enque_point;
		unsigned long cur = (head-i) & (SDP_MC_HANDLER_RINGBUF_SIZE-1);
		u32 *data = mchnd->rx_ringbuf->data[cur].buf;

		enque = mchnd->rx_ringbuf->data[cur].enqueue_ns;
		deque = mchnd->rx_ringbuf->data[cur].dequeue_ns;
		elapsed = deque ? div64_u64(deque - enque, 1000U) : 0x0;

		enque_sec = div64_u64(enque, 100000UL);/* to div 100us */
		enque_point = enque_sec;
		enque_sec = div64_u64(enque_sec, 10000UL);
		enque_point = enque_point - (enque_sec*10000UL);

		dev_info(mchnd->dev, "%2d@%2lu: %2lluus(%llu.%04llus) 0x%x %08x %08x %08x\n",
			i, cur, elapsed,  enque_sec, enque_point, data[0], data[1], data[2], data[3]);
	}

	dev_info(mchnd->dev, "===== MessageBox Status End =====\n");
}
EXPORT_SYMBOL(sdp_messagebox_debug_print);

int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size)
{
	struct sdp_mc_handler_t *mchnd = g_mchnd;
	//Check Previous Write Operation End
	unsigned int *writebuf;
	unsigned int alignsize;
	unsigned int remainsize; 
	u32 remainbuf = 0;
	int i;
	int count=50;

	if (mchnd == NULL) {
		pr_err("%s %d: mchnd is NULL\n", __FUNCTION__, __LINE__);
		return -1;
	}

	if ( size > MESSAGE_SIZE_MAX || !size ) {
		dev_err(mchnd->dev, "%s size is too big %d\n", __FUNCTION__, size);
		return -1;		
	}

	mutex_lock(&mchnd->send_mutex);
	
	do
	{
		if ( (!(readl(mchnd->base+SEND_PEND_OFF) & MESSAGE_SEND)) && (!(readl(mchnd->base+RECV_PEND_OFF) & MESSAGE_SEND)) ) {
			break;
		}
		msleep(1);
	}while(count--);

	if (!count) {
		dev_err(mchnd->dev, "%s timeout\n", __FUNCTION__);		
		mutex_unlock(&mchnd->send_mutex);
		return 0;
	}

	alignsize = size/sizeof(unsigned int); 	
	remainsize = size%sizeof(unsigned int);		
	writebuf = (unsigned int*)pbuffer;
	
	for (i=0; i < alignsize; i++) {
		writel(writebuf[i], mchnd->base+SEND_DATA1_OFF+(0x4*i));
	}



	switch(remainsize) {
		case 3: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 2] << 16;
		case 2: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 1] << 8;
		case 1: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 0];
			writel(remainbuf, mchnd->base+SEND_DATA1_OFF+(0x4*i));
			break;
		case 0:
		default:
			break;
	}

	writel(size, mchnd->base+SEND_DATA0_OFF);
	writel(MESSAGE_SEND, mchnd->base+SEND_PEND_OFF);

	mod_timer(&mchnd->recv_timeout_timer, jiffies + msecs_to_jiffies(mchnd->recv_timeout_ms));
	mchnd->total_send_count++;

	mutex_unlock(&mchnd->send_mutex);

	return size;
}
EXPORT_SYMBOL(sdp_messagebox_write);


int sdp_messagebox_write_direct(unsigned char* pbuffer, unsigned int size)
{
	struct sdp_mc_handler_t *mchnd = g_mchnd;
	//Check Previous Write Operation End
	unsigned int *writebuf;
	unsigned int alignsize;
	unsigned int remainsize;
	u32 remainbuf = 0;
	int i;
	int count=10;

	if (mchnd == NULL) {
		pr_err("%s %d: mchnd is NULL\n", __FUNCTION__, __LINE__);
		return -1;
	}

	if ( size > MESSAGE_SIZE_MAX || !size ) {
		dev_err(mchnd->dev, "%s size is too big %d\n", __FUNCTION__, size);
		return -1;
	}
	dev_err(mchnd->dev, "send cmd for power off.\n");
	dev_err(mchnd->dev, "micom tx pend(%d) check\n", readl(mchnd->base+SEND_PEND_OFF));
	do
	{
		if (!(readl(mchnd->base+SEND_PEND_OFF) & MESSAGE_SEND)) {
			break;
		}
		usleep_range(1000, 1500);
	}while(count--);
	dev_err(mchnd->dev, "micom tx pend(%d) finish\n", readl(mchnd->base+SEND_PEND_OFF));

	if (!count) {
		dev_err(mchnd->dev, "%s timeout\n", __FUNCTION__);
		return 0;
	}

	alignsize = size/sizeof(unsigned int);
	remainsize = size%sizeof(unsigned int);
	writebuf = (unsigned int*)pbuffer;

	for (i=0; i < alignsize; i++) {
		writel(writebuf[i], mchnd->base+SEND_DATA1_OFF+(0x4*i));
	}

	switch(remainsize) {
		case 3: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 2] << 16;
		case 2: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 1] << 8;
		case 1: remainbuf |= pbuffer[(alignsize * sizeof(unsigned int)) + 0];
			writel(remainbuf, mchnd->base+SEND_DATA1_OFF+(0x4*i));
			break;
		case 0:
		default:
			break;
	}

	writel(size, mchnd->base+SEND_DATA0_OFF);
	writel(MESSAGE_SEND, mchnd->base+SEND_PEND_OFF);


#if defined(CONFIG_ARCH_SDP1406) || defined(CONFIG_ARCH_SDP1406FHD)
	dev_err(mchnd->dev, "Start Hawk-M EDID workaround\n");
	/* EDID workaround */
	/* wait micom ack (0x800590 = 1) */
	while(readl((void *)(0xfe000000 + 0x00800590 - 0x00100000)) != 1);

	/* XXX: see arch/arm/mach-sdp/sleep.S, same logic implemented in there. */
	/* only do on w/o boston board, P12.6, 0=boston, 1=non-boston */
	if (!(readl((void*)(0xfe000000 + 0x00800508 - 0x00100000))) && (sdp_soc() == SDP1406UHD_CHIPID))
		goto out;
	/* skip EDID problem board */
	if (sdp_soc() == SDP1406UHD_CHIPID &&
		readl((void *)0xfee00000) & (1 << 16))
		goto out;

	/* EDID read start toggle reset [5]->1 */
	writel(readl((void *)0xfe480500) | (1 << 5), (void *)0xfe480500);

out:

	/* send to micom (0x800590 = 0) */
	writel(0, (void *)(0xfe000000 + 0x00800590 - 0x00100000));
	/* end of EDID workaround */
#endif

	mod_timer(&mchnd->recv_timeout_timer, jiffies + msecs_to_jiffies(mchnd->recv_timeout_ms));
	mchnd->total_send_count++;

	return size;
}
EXPORT_SYMBOL(sdp_messagebox_write_direct);



int sdp_messagebox_update(unsigned char* pbuffer, unsigned int size)
{
	pr_err("MSGBOX: sdp_messagebox_update is not supported!");
	return 0;
}
EXPORT_SYMBOL(sdp_messagebox_update);


int sdp_messagebox_installcallback(sdp_messagebox_callback_t callbackfn)
{
	struct sdp_mc_handler_t *mchnd = g_mchnd;

	mchnd->read_callback = callbackfn;
	return 0;
}
EXPORT_SYMBOL(sdp_messagebox_installcallback);



/***************** dev attr *****************/
static ssize_t statistics_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_mc_handler_t *mchnd = NULL;
	ssize_t cur_pos = 0;

	mchnd = platform_get_drvdata(pdev);

	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total_send_count              : %u\n", mchnd->total_send_count);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total_recv_count              : %u\n", mchnd->total_recv_count);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total_recv_drop_count         : %u\n", mchnd->total_recv_drop_count);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "total_recv_thread_count       : %u\n", mchnd->total_recv_thread_count);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "rx_ring_count                 : %u/%u\n",
		sdp_mc_ringbuf_count(mchnd->rx_ringbuf), SDP_MC_HANDLER_RINGBUF_SIZE);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "rx_ring elapsed_max_time_ns   : %lluns\n", mchnd->rx_ringbuf->elapsed_max_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "isr_last_called_time_ns       : %lluns\n", mchnd->isr_last_called_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "isr_process_max_time_ns       : %lluns\n", mchnd->isr_process_max_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "thread_process_max_time_ns    : %lluns\n", mchnd->thread_process_max_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "callback_process_max_time_ns  : %lluns\n", mchnd->callback_process_max_time_ns);

	//sdp_messagebox_debug_print();

	return cur_pos;
}
static struct device_attribute dev_attr_statistics = __ATTR_RO(statistics);


static ssize_t rx_ringbuf_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_mc_handler_t *mchnd = NULL;
	ssize_t cur_pos = 0;
	int i = 0;
	unsigned long head = 0;
	unsigned long long now = sdp_mc_handler_get_nsecs();

	mchnd = platform_get_drvdata(pdev);

	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos, "rx ring dump %u/%u (now %llu.%llus, elapsed max time %lluns)\n",
		sdp_mc_ringbuf_count(mchnd->rx_ringbuf), SDP_MC_HANDLER_RINGBUF_SIZE,
		div64_u64(now, 1000000000), now-(div64_u64(now, 1000000000)*1000000000), mchnd->rx_ringbuf->elapsed_max_time_ns);
	cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos,
		"nr@id: elapsed(start_time) data values\n");

	head = mchnd->rx_ringbuf->head;

	for(i = 1; i <= SDP_MC_HANDLER_RINGBUF_SIZE; i++) {
		unsigned long long enque, deque, elapsed;
		unsigned long cur = (head-i) & (SDP_MC_HANDLER_RINGBUF_SIZE-1);
		u32 *data = mchnd->rx_ringbuf->data[cur].buf;

		enque = mchnd->rx_ringbuf->data[cur].enqueue_ns;
		deque = mchnd->rx_ringbuf->data[cur].dequeue_ns;
		elapsed = deque ? deque - enque : 0x0;

		/* XXX fix it.. */
		if(cur_pos >= PAGE_SIZE-1) {
			break;
		}

		cur_pos += scnprintf(buf + cur_pos, PAGE_SIZE - cur_pos,
			"%2d@%2lu: %5lluus(%2llu.%09llus) 0x%x 0x%08x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			i, cur, div64_u64(elapsed, 1000U), div64_u64(enque, 1000000000), enque - (div64_u64(enque, 1000000000)*1000000000), data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
	}

	return cur_pos;
}
static struct device_attribute dev_attr_rx_ringbuf = __ATTR_RO(rx_ringbuf);


#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
static void
sdp_micom_handler_add_debugfs(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_mc_handler_t *mchnd = NULL;
	struct dentry *root;

	mchnd = platform_get_drvdata(pdev);

	root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	mchnd->dbgfs_root = root;

	debugfs_create_u32("callback_timeout_us", S_IRUGO|S_IWUGO, root, &mchnd->callback_timeout_us);
	debugfs_create_u32("recv_timeout_ms", S_IRUGO|S_IWUGO, root, &mchnd->recv_timeout_ms);

#ifdef SDP_MC_HANDLER_TEST_MODE
	debugfs_create_u32("test_mode", S_IRUGO|S_IWUGO, root, &mchnd->dbgfs_test_mode);
#endif

	return;

err_root:
	mchnd->dbgfs_root = NULL;
	return;
}
#endif


static int sdp_messagebox_probe (struct platform_device *pdev)
{	
	struct device *dev = &pdev->dev;
	struct sdp_mc_handler_t *mchnd = NULL;
	int irq;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	mchnd = kzalloc(sizeof(struct sdp_mc_handler_t), GFP_KERNEL);
	if(!mchnd) {
		dev_err(dev, "mchnd allocate failed!\n");
		ret = -ENOMEM;
		goto _err_free;
	}

	mchnd->dev = dev;

	mchnd->base = of_iomap(dev->of_node, 0);
	BUG_ON(!mchnd->base);

	mchnd->rx_ringbuf = kzalloc(sizeof(struct sdp_mc_ringbuf_t), GFP_KERNEL);
	if(!mchnd->rx_ringbuf) {
		dev_err(dev, "rx_ringbuf allocate failed!\n");
		ret = -ENOMEM;
		goto _err_free;
	}
	sdp_mc_ringbuf_init(mchnd->rx_ringbuf);

	spin_lock_init(&mchnd->recvlock);
	mutex_init(&mchnd->send_mutex);
	mutex_init(&mchnd->recv_mutex);

	mchnd->recv_timeout_ms = 25U;
	setup_timer(&mchnd->recv_timeout_timer, messagebox_timeout_timercb, (unsigned long)mchnd);
	INIT_WORK(&mchnd->recv_timeout_work, messagebox_timeout_work);

	mchnd->callback_timeout_us = 100000UL;
	platform_set_drvdata(pdev, mchnd);
	g_mchnd = mchnd;

	ret = device_create_file(dev, &dev_attr_statistics);
	ret = device_create_file(dev, &dev_attr_rx_ringbuf);

#ifdef CONFIG_DEBUG_FS
	sdp_micom_handler_add_debugfs(dev);
#endif

	/* Check Pending */
	if(readl(mchnd->base+RECV_PEND_OFF)) {
		dev_warn(dev, "probe() pending bits is 0x%08x, now clear all pending bits!\n", readl(mchnd->base+RECV_PEND_OFF));
	}
	/* Pending Clear */
	writel(0xff, mchnd->base+RECV_CLEAR_OFF);
	readl(mchnd->base+RECV_CLEAR_OFF);

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		ret = -ENODEV;
		goto _err_free;
	}

	mchnd->irq = irq;
	ret = request_threaded_irq( irq, messagebox_isr, messagebox_thread, 0x0, pdev->name, (void*)dev);
	if (ret) {
		dev_err(dev, "request_irq failed\n");
		ret = -ENODEV;
		goto _err_free;
	}

	
	if(of_property_read_u32(dev->of_node, "irq-affinity", &mchnd->irq_affinity)==0) {
		if(num_online_cpus() > 1) {
			irq_set_affinity(irq, cpumask_of(mchnd->irq_affinity));
		}
	} else {
		mchnd->irq_affinity = 0;
		irq_set_affinity(irq, cpumask_of(mchnd->irq_affinity));
		dev_info(dev, "cpu affinity get fail, set to cpu0\n");
	}

	dev_info(dev, "probe done. ver %s, irq %d(cpu%d), rx ringbuf 0x%p(%u)\n", SDP_MC_HANDLER_VERSTR,
		irq, mchnd->irq_affinity, mchnd->rx_ringbuf, SDP_MC_HANDLER_RINGBUF_SIZE);

	return 0;


_err_free:

	g_mchnd = NULL;
	platform_set_drvdata(pdev, NULL);

	if(mchnd) {
		if(mchnd->rx_ringbuf) {
			kfree(mchnd->rx_ringbuf);
			mchnd->rx_ringbuf = NULL;
		}

		kfree(mchnd);
	}
	return ret;
}


static int sdp_messagebox_remove(struct platform_device *pdev)
{
	struct sdp_mc_handler_t *mchnd = NULL;

	mchnd = platform_get_drvdata(pdev);

	g_mchnd = NULL;
	platform_set_drvdata(pdev, NULL);

	if(mchnd) {
		if(mchnd->rx_ringbuf) {
			kfree(mchnd->rx_ringbuf);
			mchnd->rx_ringbuf = NULL;
		}

		kfree(mchnd);
	}

	return 0;		
}

static int sdp_messagebox_suspend(struct device *dev)
{
	return 0;
}

static int sdp_messagebox_resume(struct device *dev)
{
	return 0;
}

static const struct of_device_id sdp_messagebox_dt_match[] = {
	{ .compatible = "samsung,sdp-messagebox" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_messagebox_dt_match);

static struct dev_pm_ops sdp_messagebox_pm = {
	.suspend_noirq	= sdp_messagebox_suspend,
	.resume_noirq	= sdp_messagebox_resume,
};


static struct platform_driver sdp_messagebox_driver = {
	.probe		= sdp_messagebox_probe,
	.remove 	= sdp_messagebox_remove,
	.driver 	= {
		.name = "sdp-mchnd",
#ifdef CONFIG_OF
		.of_match_table = sdp_messagebox_dt_match,
#endif
		.pm	= &sdp_messagebox_pm,
	},
};


static int __init
sdp_messagebox_init (void)
{
	return platform_driver_register(&sdp_messagebox_driver);	
}

static void __exit sdp_messagebox_exit(void)
{
	platform_driver_unregister(&sdp_messagebox_driver);
}


arch_initcall(sdp_messagebox_init);
module_exit(sdp_messagebox_exit);


MODULE_DESCRIPTION("Samsung DTV messagebox driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("douk.nam <douk.nam@samsung.com>");

