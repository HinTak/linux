#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define TASK_LEN_BUF	(128)
#define MAX_COUNT		(50)
#define MAX_FAULT_INFO	(10)

#define DEADLOCK_TYPE "DEADLOCK"

static struct mutex kpi_fault_info_lock;
static struct mutex kpi_fault_info_call_lock;

//int kpi_fault_call = 0;
//int save_done = 0;

//unsigned long kpi_fault_pc = 0;
//unsigned long kpi_fault_lr = 0;
//char kpi_fault_thread[TASK_LEN_BUF] = "NONE";
//char kpi_fault_process[TASK_LEN_BUF] = "NONE";
//char kpi_fault_type[TASK_LEN_BUF] = "NONE";

bool deadlock = 0;

//char last_thread_name[TASK_LEN_BUF] = "NONE";

typedef struct _queue
{
	unsigned long pc;
	unsigned long lr;
	
	char thread_name[TASK_LEN_BUF];
	char process_name[TASK_LEN_BUF];
	char type[TASK_LEN_BUF];

	pid_t pgid;
	
	int used;
} fault_info_t;

fault_info_t k_fault_info[MAX_FAULT_INFO];
int write = 0;

//DECLARE_COMPLETION(save_call);
//DECLARE_COMPLETION(save_done);
/*
void clear_kpi_fault(void)
{
	mutex_lock(&kpi_fault_info_lock);
	kpi_fault_pc = 0;
	kpi_fault_lr = 0;
	
	strncpy(kpi_fault_thread, "NONE", TASK_LEN_BUF);
	strncpy(kpi_fault_process, "NONE", TASK_LEN_BUF);
	strncpy(kpi_fault_type, "NONE", TASK_LEN_BUF);
	mutex_unlock(&kpi_fault_info_lock);
}
*/
void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name, char* type, pid_t pgid)
{
	//int i;
	
	printk(KERN_ERR "[kpi_fault_info] [k] %d -> p:0x%08lx l:0x%08lx th:%s pro:%s ty:%s pgid:%d\n", write, pc, lr, thread_name, process_name, type, pgid);
	
	mutex_lock(&kpi_fault_info_call_lock);
	if(!strcmp(type, DEADLOCK_TYPE))
	{
		deadlock = 1;
		
		//strncpy(last_thread_name, thread_name, TASK_LEN_BUF);
	}
	
	/*
	if(!strcmp(type, "crash") && !strcmp(last_thread_name, thread_name))
	{
		printk(KERN_ERR "[kpi_fault_info] [k] DEADLOCK happened before, so crash type is skiped\n");
		strncpy(last_thread_name, "NONE", TASK_LEN_BUF);
	}
	*/
	else
	{
		mutex_lock(&kpi_fault_info_lock);
		if(write >= MAX_FAULT_INFO)
		{
			write = 0;
		}
		
		if(k_fault_info[write].used == 1)
		{
			printk(KERN_ERR "[kpi_fault_info] [k] queue buffer max\n");
		}
		else
		{
			//kpi_fault_call = 1;
			
			k_fault_info[write].pc = pc;
			k_fault_info[write].lr = lr;
			
			if(thread_name != NULL)
			{
				strncpy(k_fault_info[write].thread_name, thread_name, TASK_LEN_BUF);
			}
			
			if(process_name != NULL)
			{
				strncpy(k_fault_info[write].process_name, process_name, TASK_LEN_BUF);
			}
			
			if(type != NULL)
			{
				if(deadlock == 1)
				{
					strncpy(k_fault_info[write].type, DEADLOCK_TYPE, TASK_LEN_BUF);
					deadlock = 0;
				}
				else
				{
					strncpy(k_fault_info[write].type, type, TASK_LEN_BUF);
				}
			}
			
			k_fault_info[write].pgid = pgid;
			
			k_fault_info[write].used = 1;
			++write;
			
			//complete(&save_call);
			/*
			if(wait_for_completion_timeout(&save_done, msecs_to_jiffies(10000)) == 0)
			{
				printk(KERN_ERR "[kpi_fault_info] [k] save_done fail\n");
			}
			
			for(i = 0; i < MAX_COUNT; i++)
			{
				if(save_done == 1)
				{
					break;
				}
				
				msleep(100);
			}
			
			if(save_done == 0)
			{
				printk(KERN_ERR "[save_error_log] save_done fail\n");
			}
			
			save_done = 0;
			*/
		}
		mutex_unlock(&kpi_fault_info_lock);
	}
	mutex_unlock(&kpi_fault_info_call_lock);
}
EXPORT_SYMBOL(set_kpi_fault);

static ssize_t kpi_fault_info_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[TASK_LEN_BUF];
	//int i;
	
	printk(KERN_ERR "[kpi_fault_info] [u] %d -> write function call\n", write);
	
	mutex_lock(&kpi_fault_info_call_lock);
	if (copy_from_user(buffer, buf, TASK_LEN_BUF))
	{
		mutex_unlock(&kpi_fault_info_call_lock);
		printk(KERN_ERR "[kpi_fault_info] [u] write function copy_from_user fail\n");
		return -EFAULT;
	}
	
	mutex_lock(&kpi_fault_info_lock);
	if(write >= MAX_FAULT_INFO)
	{
		write = 0;
	}
	
	if(k_fault_info[write].used == 1)
	{
		printk(KERN_ERR "[kpi_fault_info] [u] queue buffer max\n");
	}
	else
	{
		//kpi_fault_call = 1;
		
		k_fault_info[write].pc = 0;
		k_fault_info[write].lr = 0;
		
		strncpy(k_fault_info[write].thread_name, "NONE", TASK_LEN_BUF);
		strncpy(k_fault_info[write].process_name, "NONE", TASK_LEN_BUF);
		sscanf(buffer, "%s ", k_fault_info[write].type);
		
		k_fault_info[write].pgid = -1;
		
		k_fault_info[write].used = 1;
		++write;
	}
	mutex_unlock(&kpi_fault_info_lock);
	
	//complete(&save_call);
	/*
	if(wait_for_completion_timeout(&save_done, msecs_to_jiffies(10000)) == 0)
	{
		printk(KERN_ERR "[kpi_fault_info] [u] save_done fail\n");
	}
	
	for(i = 0; i < MAX_COUNT; i++)
	{
		if(save_done == 1)
		{
			break;
		}
		
		msleep(100);
	}
	
	if(save_done == 0)
	{
		printk(KERN_ERR "[save_error_log] save_done fail\n");
	}
	
	save_done = 0;
	*/
	mutex_unlock(&kpi_fault_info_call_lock);
	return (ssize_t)count;
}

static ssize_t kpi_fault_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[512];
	int len = -1;
	static int read = 0;
	
	//wait_for_completion(&save_call);
	
	mutex_lock(&kpi_fault_info_lock);
	if(read >= MAX_FAULT_INFO)
	{
		read = 0;
	}
	
	if(k_fault_info[read].used == 1)
	{
		/* process name : "X" => "XServer" */
		if(!strncmp(k_fault_info[read].process_name, "X", TASK_LEN_BUF))
		{
			strncpy(k_fault_info[read].process_name, "XServer", TASK_LEN_BUF);
		}
		
		//if(kpi_fault_call)
		{
			snprintf(buffer, 512, "%lu %lu %s %s %s %d %c", k_fault_info[read].pc, k_fault_info[read].lr,
						k_fault_info[read].thread_name, k_fault_info[read].process_name,
						k_fault_info[read].type, k_fault_info[read].pgid, '\0');
			len = (int)strlen(buffer);
			
			if(copy_to_user(buf, buffer, (long unsigned int)len))
			{
				mutex_unlock(&kpi_fault_info_lock);
				printk(KERN_ERR "[kpi_fault_info] read function copy_to_user fail\n");
				return -EFAULT;
			}
			
			//kpi_fault_call = 0;
		}
		
		k_fault_info[read].used = 0;
		++read;
	}
	mutex_unlock(&kpi_fault_info_lock);
	return len;
}

static ssize_t save_done_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[TASK_LEN_BUF];
	
	mutex_lock(&kpi_fault_info_lock);
	if (copy_from_user(buffer, buf, TASK_LEN_BUF))
	{
		mutex_unlock(&kpi_fault_info_lock);
		printk(KERN_ERR "[kpi_fault_info] write function copy_from_user fail\n");
		return -EFAULT;
	}
	
	//sscanf(buffer, "%d ", &save_done);
	mutex_unlock(&kpi_fault_info_lock);
	
	//complete(&save_done);
	
	return (ssize_t)count;
}

static int kpi_fault_info_open(struct inode *inode, struct file *file)
{
	return single_open(file,
		(int (*)(struct seq_file *, void *))kpi_fault_proc_read, NULL);
}

static int save_done_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, NULL);
}

static const struct file_operations kpi_fault_info_fops = {
	.open  = kpi_fault_info_open,
	.read  = kpi_fault_proc_read,
	.write = kpi_fault_info_write,
};

static const struct file_operations save_done_fops = {
	.open  = save_done_open,
	.write = save_done_write,
};

static int __init proc_kpi_fault_info_init(void)
{
	mutex_init(&kpi_fault_info_lock);
	mutex_init(&kpi_fault_info_call_lock);
	
	//init_completion(&save_call);
	//init_completion(&save_done);
	
	proc_create("kpi_fault_info", 0, NULL, &kpi_fault_info_fops);
	proc_create("save_done", 0, NULL, &save_done_fops);
	
	return 0;
}
module_init(proc_kpi_fault_info_init);

