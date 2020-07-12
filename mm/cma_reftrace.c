#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/cpu.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/ioctls.h>
#include <linux/kallsyms.h>

#include "cma_reftrace.h"

static DEFINE_PER_CPU(struct delayed_work, log_syncer_shepherd);

struct logger_log {
	unsigned long tail;
	unsigned long head;
	unsigned long *log_buf;
};

struct percpu_log_buffer {
	unsigned long tail;
	unsigned long head;
	unsigned long *logger;
};


static struct percpu_log_buffer cpu_ref_trace_buf[NR_CPUS];
static struct logger_log* log;

static int log_initialized __read_mostly = 0;
static struct workqueue_struct *logger_wq;
int log_timer_interval __read_mostly = HZ/4;

static DEFINE_SPINLOCK(logger_lock);

bool is_cma_page(struct page* page)
{
	return (is_zone_cma(page_zone(page))) ? true : false;   
}


void print_pageref_info(unsigned long* ep, char* reason) 
{
	unsigned long ip;
	int k;
	pr_err("[cma_tracer] %s %lu - pfn:%lx ref:%lu type:%lu \n",
			reason, *(ep + 1), ENTRY_TO_PFN(*(ep)),
			ENTRY_TO_REFCOUNT(*(ep)), ENTRY_TO_TYPE(*(ep)));
	for (k = REFTRACE_META_SIZE; k < REFTRACE_ENTRIES_SIZE; k++) {		
		ip = ep[k];
		pr_err("[%d][<%p>] %pS\n", k-REFTRACE_META_SIZE,(void*) ip, 
				(void*) ip);
	}

}

void record_page_ref(struct page* page, int ref, unsigned long *builtin, bool increase)
{
	unsigned long pfn = page_to_pfn(page);
	struct percpu_log_buffer* cur_buff;
	unsigned long *logger;

	struct page_ext* page_ext;
	int index = 0;
	int i;

	if(likely(log_initialized)) {
		int cpu = get_cpu();
		cur_buff = &cpu_ref_trace_buf[cpu];
		logger = cur_buff->logger + cur_buff->tail;
		logger[index++]=  (increase << INCREARE_SHIFT | ref << REFCOUNT_SHIFT | pfn);
		logger[index++] = (unsigned long)(running_clock() >> 20LL);  /*2^20 ~= 10^6 msec*/

		for (i = 0; i < REFTRACE_BACKTRACE_NR; i++) {
			logger[index++] = (unsigned long)builtin[i];
		}

		cur_buff->tail += index;
		if (cur_buff->tail >= (CPU_PER_BUF_SZ-1))    // 512KB per cpu
			cur_buff->tail = 0;

		if (cur_buff->tail == cur_buff->head &&
				(cur_buff->head += index) >= (CPU_PER_BUF_SZ))
			cur_buff->head = 0;
		put_cpu();
	}

	page_ext = lookup_page_ext(page);

	if (test_bit(PAGE_EXT_MIGRATE_FAIL, &page_ext->flags)) {
		print_pageref_info(logger, "MIGRATE_FAIL_PFN");
	}

}
EXPORT_SYMBOL(record_page_ref);
void flush_all_logs(void)
{

	int cpu;
	for_each_possible_cpu(cpu) {
		queue_delayed_work_on(cpu, logger_wq,
				this_cpu_ptr(&log_syncer_shepherd), 0);
	}

}
EXPORT_SYMBOL(flush_all_logs);


static void log_merge_work(struct work_struct *w)
{
	//static int cma_fail_test = 0;
	int cpu;
	size_t cpy_size;
	struct percpu_log_buffer* src_buf;
	int src_size, total_src_size;
	int dst_size;
	int tmp;
	if (!likely(log_initialized))
		goto end;

	spin_lock(&logger_lock);
	cpu = get_cpu();
	src_buf = &cpu_ref_trace_buf[cpu];
	/* calculate size of source*/
	if(src_buf->head > src_buf->tail) {
		/* if 0----tail ---- head ---- CPU_PER_BUF_SZ */
		src_size = CPU_PER_BUF_SZ - src_buf->head;
		total_src_size = src_buf->tail + src_size;
	} else {/* if 0 ----- head ----- tail----CPU_PER_BUFSZ */
		total_src_size = src_size = src_buf->tail - src_buf->head;
	}

	do {
		/* if dst->head ------ dst->tail */
		if(log->tail >= log->head) {
			dst_size =  DEFAULT_LOG_SZ - log->tail;
		} else {
			dst_size = (log->head - log->tail);
		}
		cpy_size = min(src_size,dst_size);
		memcpy((char *)(log->log_buf + log->tail),
				(char *)(src_buf->logger + src_buf->head),
				cpy_size*sizeof(unsigned long));
		/* handle dst buf tail adjust  this is point of next start dst*/
		if((log->tail += cpy_size) >= DEFAULT_LOG_SZ) {
			log->tail -= DEFAULT_LOG_SZ;
		}

		/* handle src buf head adjust this is point next start src*/
		if((src_buf->head += cpy_size)>= (CPU_PER_BUF_SZ)) {
			src_buf->head -= CPU_PER_BUF_SZ;
		}

		src_size-=cpy_size;
		total_src_size -= cpy_size;
		if(src_size <= 0) {
			src_size = total_src_size;
		}
		/* if head == tail, tail += left src size */
		if( log->tail == log->head ) {
			if( log->head + src_size > DEFAULT_LOG_SZ ) {
				log->head = (log->head + src_size) - DEFAULT_LOG_SZ + REFTRACE_ENTRIES_SIZE;
			} else {
				log->head += src_size + REFTRACE_ENTRIES_SIZE;
			}
		}
	} while(src_size > 0);

	// reset logger buffer per cpu
	src_buf->head = src_buf->tail;

	spin_unlock(&logger_lock);
	put_cpu();
end:
	queue_delayed_work_on(cpu, logger_wq,
			this_cpu_ptr(&log_syncer_shepherd),
			round_jiffies_relative(log_timer_interval));
}

static void log_flush_work(int cpu)
{
	size_t cpy_size;
	struct percpu_log_buffer* src_buf;
	int src_size, total_src_size;
	int dst_size;
	int tmp;
	if (!likely(log_initialized))
		return;

	spin_lock(&logger_lock);
	src_buf = &cpu_ref_trace_buf[cpu];
	/* calculate size of source*/
	if(src_buf->head > src_buf->tail) {
		/* if 0----tail ---- head ---- CPU_PER_BUF_SZ */
		src_size = CPU_PER_BUF_SZ - src_buf->head;
		total_src_size = src_buf->tail + src_size;
	} else {/* if 0 ----- head ----- tail----CPU_PER_BUFSZ */
		total_src_size = src_size = src_buf->tail - src_buf->head;
	}

	do {
		/* if dst->head ------ dst->tail */
		if(log->tail >= log->head) {
			dst_size =  DEFAULT_LOG_SZ - log->tail;
		} else {
			dst_size = (log->head - log->tail);
		}
		cpy_size = min(src_size,dst_size);
		memcpy((char *)(log->log_buf + log->tail),
				(char *)(src_buf->logger + src_buf->head),
				cpy_size*sizeof(unsigned long));
		/* handle dst buf tail adjust  this is point of next start dst*/
		if((log->tail += cpy_size) >= DEFAULT_LOG_SZ) {
			log->tail -= DEFAULT_LOG_SZ;
		}

		/* handle src buf head adjust this is point next start src*/
		if((src_buf->head += cpy_size)>= (CPU_PER_BUF_SZ)) {
			src_buf->head -= CPU_PER_BUF_SZ;
		}

		src_size-=cpy_size;
		total_src_size -= cpy_size;
		if(src_size <= 0) {
			src_size = total_src_size;
		}
		/* if head == tail, tail += left src size */
		if( log->tail == log->head ) {
			if( log->head + src_size > DEFAULT_LOG_SZ ) {
				log->head = (log->head + src_size) - DEFAULT_LOG_SZ + REFTRACE_ENTRIES_SIZE;
			} else {
				log->head += src_size + REFTRACE_ENTRIES_SIZE;
			}
		}
	} while(src_size > 0);

	// reset logger buffer per cpu
	src_buf->head = src_buf->tail;

	spin_unlock(&logger_lock);
}



#define LOG_ENTRIES_SIZE(n) REFTRACE_ENTRIES_SIZE * sizeof(unsigned long) * n
#define LOG_NEXT_OFFSET(size) size * REFTRACE_ENTRIES_SIZE

void get_log_entries(void* dst, size_t size)
{
	spin_lock(&logger_lock);
	if(log->head != log->tail) {
		memcpy(dst, log->log_buf + log->head,
				LOG_ENTRIES_SIZE(size));
		if(log->head + LOG_NEXT_OFFSET(size) >= DEFAULT_LOG_SZ) {
			log->head = log->head + LOG_NEXT_OFFSET(size) - DEFAULT_LOG_SZ;
		} else {
			log->head += LOG_NEXT_OFFSET(size);
		}
	}
	spin_unlock(&logger_lock);
}
EXPORT_SYMBOL(get_log_entries);

static int match_with_pfn(void* entry, unsigned long pfn)
{
	if(ENTRY_TO_PFN(*(unsigned long*)(entry)) == pfn)
		return 1;
	return 0;
}

/*
 * get trace log corresponding pfn.
 */

void get_log_from_pfn(unsigned long pfn)
{
	unsigned long head = log->head, end_loop;

	unsigned long data;
	int k;
	unsigned long ip;
	int cpu;
	for_each_online_cpu(cpu) {
		log_flush_work(cpu);
	}
	if(log->head != log->tail) {
		spin_lock(&logger_lock);
		head = log->head;
		if(log->head > log->tail) {
			end_loop = DEFAULT_LOG_SZ;
			for(;head < end_loop; head += REFTRACE_ENTRIES_SIZE) {
				if(match_with_pfn((log->log_buf + head),pfn)) 
					print_pageref_info(log->log_buf + head, "");	

			}
			head = 0;
		}

		end_loop = log->tail;
		for(;head < end_loop; head += REFTRACE_ENTRIES_SIZE) {
			if(match_with_pfn((log->log_buf + head),pfn))
				print_pageref_info(log->log_buf + head, "");	
		}
		spin_unlock(&logger_lock);
	}
}
EXPORT_SYMBOL(get_log_from_pfn);

size_t get_log_size(void)
{
	size_t buf_size = 0;
	if(log->head == log->tail)
		return buf_size;
	else if(log->head > log->tail) {
		buf_size = (DEFAULT_LOG_SZ - log->head) + log->tail;
	} else {
		buf_size = log->tail - log->head;
	}
	return buf_size;
}

static long get_log_from_buffer(void __user* dst, size_t size)
{
	size_t written = 0, copy_size;
	unsigned long head = 0;
	int temp = 0;
	int size_per_item = sizeof(unsigned long);

	spin_lock(&logger_lock);

	if(log->head != log->tail) {
		head = log->head;

		if(log->head > log->tail) {
			copy_size = DEFAULT_LOG_SZ - head;
			if(size > copy_size) {
				pr_err("[%d]copy_to_user (%d, %d) \n", __LINE__, copy_size, copy_size * size_per_item);
				temp = copy_to_user(dst, (void *)(log->log_buf + head), copy_size * size_per_item);
				if (temp) {
					pr_err("[%d]return error 1, temp:%d, write:%d\n", __LINE__,
							temp, (int)copy_size * size_per_item - temp );
					spin_unlock(&logger_lock);
					return -EFAULT;
				}
				pr_err("[%d]temp:%d, write:%d\n", __LINE__, temp, (int)copy_size * size_per_item - temp);
			}
			written = copy_size;
			head = 0;
		}

		copy_size = log->tail - head;
		pr_err("[%d]size:%u, written + copy_size:%u, written:%u \n", __LINE__, size, written + copy_size, written);
		if(size >= (written + copy_size)) {
			pr_err("[%d]copy_to_user (%d, %d) \n", __LINE__, copy_size, copy_size * size_per_item);
			temp = copy_to_user(dst + written * size_per_item, (void *)(log->log_buf + head), copy_size * size_per_item);
			if (temp) {
				pr_err("[%d]return error 2, temp:%d, write:%d\n",__LINE__, temp, (int)copy_size * size_per_item - temp);
				spin_unlock(&logger_lock);
				return -EFAULT;
			}
			pr_err("[%d]temp:%d, write:%d\n", __LINE__, temp, (int)copy_size * size_per_item - temp);
		}
		// reset logger buffer
		log->head = log->tail = 0;
	}

	spin_unlock(&logger_lock);

	return 0;
}

static int __init cma_logger_init(void)
{
	int cpu;

	log = kzalloc(sizeof(struct logger_log), GFP_KERNEL);
	if(!log)
		return -ENOMEM;

	log->log_buf = vmalloc((DEFAULT_LOG_SZ * sizeof(unsigned long)));
	if(!log->log_buf)
		goto release_log;

	log->head = log->tail = 0;
	logger_wq = alloc_workqueue("logger_wq", WQ_FREEZABLE, 0);

	if (!logger_wq)
		goto release_resource;

	for_each_possible_cpu(cpu)
		INIT_DELAYED_WORK(per_cpu_ptr(&log_syncer_shepherd, cpu),
				log_merge_work);

	for_each_online_cpu(cpu) {
		cpu_ref_trace_buf[cpu].head = cpu_ref_trace_buf[cpu].tail = 0;
		cpu_ref_trace_buf[cpu].logger = kmalloc((CPU_PER_BUF_SZ*sizeof(unsigned long)), GFP_KERNEL);
		queue_delayed_work_on(cpu, logger_wq,
				&per_cpu(log_syncer_shepherd, cpu), round_jiffies_relative(log_timer_interval));
	}


	pr_info("log_buf:0x%p logger_size:%d \n",log->log_buf, DEFAULT_LOG_SZ * sizeof(unsigned long));

	log_initialized = 1;
	return 0;
release_resource:
	vfree(log->log_buf);
release_log:
	kfree(log);	
	return -ENOMEM;
}
static void __exit cma_logger_exit(void)
{
	vfree(log->log_buf);
	kfree(log);
}
device_initcall(cma_logger_init);
module_exit(cma_logger_exit);

