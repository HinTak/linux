#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define SIZE_256		(256)
#define TASK_LEN_BUF	(128)
#define MAX_COUNT		(50)
#define MAX_FAULT_INFO	(10)

#define DEADLOCK		"DEADLOCK"

static struct mutex kpi_fault_info_lock;
static struct mutex kpi_fault_info_call_lock;

bool deadlock = false;

typedef struct _queue
{
	unsigned long pc;
	unsigned long lr;
	
	char thread_name[TASK_LEN_BUF];
	char process_name[TASK_LEN_BUF];
	char type[TASK_LEN_BUF];
	
	char library_name[TASK_LEN_BUF];
	
	pid_t pgid;
	
	/* cpuusage */
	int duration, usage;
	
	int used;
} fault_info_t;

fault_info_t k_fault_info[MAX_FAULT_INFO];
int write = 0;

void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name, char* type, pid_t pgid)
{
	char path_buf[SIZE_256];
	const char *name;
	
	struct task_struct *task = current;
	struct task_struct *t;
	struct vm_area_struct *pc_vma, *lr_vma;
	struct mm_struct *mm;
	struct file *file;
	
	printk(KERN_ERR "[kpi_fault_info] [k] %d -> p:0x%08lx l:0x%08lx th:%s pro:%s ty:%s pgid:%d\n", write, pc, lr, thread_name, process_name, type, pgid);
	
	mutex_lock(&kpi_fault_info_call_lock);
	if(!strcmp(type, DEADLOCK))
	{
		deadlock = true;
	}
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
			/* set pc or pc offset */
			if(pc != 0)
			{
				if(down_read_trylock(&task->mm->mmap_sem))
				{
					pc_vma = find_vma(task->mm, pc);
					
					if (pc_vma && (pc >= pc_vma->vm_start))
					{
						printk(KERN_ERR "[kpi_fault_info] [k] PC Start: 0x%08lx Offset: 0x%08lx\n", pc_vma->vm_start, pc - pc_vma->vm_start);
						
						k_fault_info[write].pc = pc - pc_vma->vm_start;
						
						/* check library name of pc value */
						file = pc_vma->vm_file;
						if(file)
						{
							name = d_path(&(file->f_path), path_buf, SIZE_256);
							
							if(IS_ERR(name))
							{
								printk(KERN_ERR "[kpi_fault_info] [k] d_path error\n");
								
								strncpy(k_fault_info[write].library_name, "NONE", TASK_LEN_BUF);
							}
							else
							{
								if(name != NULL)
								{
									printk(KERN_ERR "[kpi_fault_info] [k] d_path : [%s]\n", name);
									
									strncpy(k_fault_info[write].library_name, name, TASK_LEN_BUF);
								}
								else
								{
									printk(KERN_ERR "[kpi_fault_info] [k] d_path is none\n");
									
									strncpy(k_fault_info[write].library_name, "NONE", TASK_LEN_BUF);
								}
							}
						}
						else
						{
							name = arch_vma_name(pc_vma);
							
							mm = pc_vma->vm_mm;
							
							if(!name)
							{
								if(mm)
								{
									if(pc_vma->vm_start <= mm->brk && pc_vma->vm_end >= mm->start_brk)
									{
										name = "[heap]";
									}
									else if(pc_vma->vm_start <= mm->start_stack && pc_vma->vm_end >= mm->start_stack)
									{
										name = "[stack]";
									}
									else
									{
										t = task;
										do
										{
											if(pc_vma->vm_start <= t->user_ssp && pc_vma->vm_end >= t->user_ssp)
											{
												name = t->comm;
												break;
											}
										}while_each_thread(task, t);
									}
								}
								else
								{
									name = "[vdso]";
								}
							}
							
							if(name != NULL)
							{
								printk(KERN_ERR "[kpi_fault_info] [k] library name : [%s]\n", name);
								
								strncpy(k_fault_info[write].library_name, name, TASK_LEN_BUF);
							}
							else
							{
								printk(KERN_ERR "[kpi_fault_info] [k] library name is none\n");
								
								strncpy(k_fault_info[write].library_name, "NONE", TASK_LEN_BUF);
							}
						}
					}
					else
					{
						printk(KERN_ERR "[kpi_fault_info] [k] No VMA for ADDR PC\n");
						
						k_fault_info[write].pc = pc;
					}
					
					up_read(&task->mm->mmap_sem);
				}
				else
				{
					printk(KERN_ERR "[kpi_fault_info] [k] down_read_trylock fail\n");
					
					k_fault_info[write].pc = pc;
				}
			}
			else
			{
				k_fault_info[write].pc = pc;
			}
			
			/* set lr or lr offset */
			if(lr != 0)
			{
				if(down_read_trylock(&task->mm->mmap_sem))
				{
					lr_vma = find_vma(task->mm, lr);
					
					if(lr_vma && (lr >= lr_vma->vm_start))
					{
						printk(KERN_ERR "[kpi_fault_info] [k] LR Start: 0x%08lx Offset: 0x%08lx\n", lr_vma->vm_start, lr - lr_vma->vm_start);
						
						k_fault_info[write].lr = lr - lr_vma->vm_start;
					}
					else
					{
						printk(KERN_ERR "[kpi_fault_info] [k] No VMA for ADDR LR\n");
						
						k_fault_info[write].lr = lr;
					}
					
					up_read(&task->mm->mmap_sem);
				}
				else
				{
					printk(KERN_ERR "[kpi_fault_info] [k] down_read_trylock fail\n");
					
					k_fault_info[write].lr = lr;
				}
			}
			else
			{
				k_fault_info[write].lr = lr;
			}
			
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
				if(deadlock)
				{
					strncpy(k_fault_info[write].type, DEADLOCK, TASK_LEN_BUF);
					
					deadlock = false;
				}
				else
				{
					strncpy(k_fault_info[write].type, type, TASK_LEN_BUF);
				}
			}
			
			k_fault_info[write].pgid = pgid;
			
			k_fault_info[write].used = 1;
			++write;
		}
		mutex_unlock(&kpi_fault_info_lock);
	}
	mutex_unlock(&kpi_fault_info_call_lock);
}
EXPORT_SYMBOL(set_kpi_fault);

static ssize_t kpi_fault_info_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[TASK_LEN_BUF];
	
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
		if(!strncmp(buffer, "CPUUSAGE", 8))
		{
			sscanf(buffer, "%s %d %d ", k_fault_info[write].type, &k_fault_info[write].duration, &k_fault_info[write].usage);
		}
		else
		{
			sscanf(buffer, "%s ", k_fault_info[write].type);
			
			k_fault_info[write].pc = 0;
			k_fault_info[write].lr = 0;
		}
		
		strncpy(k_fault_info[write].thread_name, "NONE", TASK_LEN_BUF);
		strncpy(k_fault_info[write].process_name, "NONE", TASK_LEN_BUF);
		
		strncpy(k_fault_info[write].library_name, "NONE", TASK_LEN_BUF);
		
		k_fault_info[write].pgid = -1;
		
		k_fault_info[write].used = 1;
		++write;
	}
	mutex_unlock(&kpi_fault_info_lock);
	mutex_unlock(&kpi_fault_info_call_lock);
	return (ssize_t)count;
}

static ssize_t kpi_fault_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[512];
	int len = -1;
	static int read = 0;
	
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
		
		if(!strncmp(k_fault_info[read].type, "CPUUSAGE", 8))
		{
			snprintf(buffer, 512, "%d %d %s %s %s %d %s %c", k_fault_info[read].duration, k_fault_info[read].usage,
						k_fault_info[read].thread_name, k_fault_info[read].process_name,
						k_fault_info[read].type, k_fault_info[read].pgid, k_fault_info[read].library_name, '\0');
		}
		else
		{
			snprintf(buffer, 512, "%lu %lu %s %s %s %d %s %c", k_fault_info[read].pc, k_fault_info[read].lr,
						k_fault_info[read].thread_name, k_fault_info[read].process_name,
						k_fault_info[read].type, k_fault_info[read].pgid, k_fault_info[read].library_name, '\0');
		}
		
		len = (int)strlen(buffer);
		
		if(copy_to_user(buf, buffer, (long unsigned int)len))
		{
			mutex_unlock(&kpi_fault_info_lock);
			
			printk(KERN_ERR "[kpi_fault_info] read function copy_to_user fail\n");
			
			return -EFAULT;
		}
		
		k_fault_info[read].used = 0;
		++read;
	}
	mutex_unlock(&kpi_fault_info_lock);
	return len;
}

static int kpi_fault_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, (int (*)(struct seq_file *, void *))kpi_fault_proc_read, NULL);
}

static const struct file_operations kpi_fault_info_fops = {
	.open  = kpi_fault_info_open,
	.read  = kpi_fault_proc_read,
	.write = kpi_fault_info_write,
};

static int __init proc_kpi_fault_info_init(void)
{
	mutex_init(&kpi_fault_info_lock);
	mutex_init(&kpi_fault_info_call_lock);
	
	proc_create("kpi_fault_info", 0, NULL, &kpi_fault_info_fops);
	
	return 0;
}
module_init(proc_kpi_fault_info_init);

