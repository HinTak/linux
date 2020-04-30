#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#define DEADLOCK	"DEADLOCK"
#define CPUUSAGE	"CPUUSAGE"
#define HWERROOR	"HWERR"

#define STABILITY_EXCESS_CPU	"STABILITY_EXCESS_CPU"

#define MAX_FAULT_INFO	(10)

#define BUFFER_SIZE_32	(32)
#define BUFFER_SIZE_128	(128)
#define BUFFER_SIZE_256	(256)

#define UUID_LENGTH		(36)

#define NOTCORE		(0)

static struct mutex kpi_fault_info_lock;
static struct mutex kpi_fault_info_call_lock;

static struct mutex uniqueID_lock;

static struct mutex new_folder_name_lock;

static struct mutex coredump_control_lock;

typedef struct _life_cycleID_
{
	char lcid[11];
	struct timeval time;
} life_cycle_ID;

char global_uniqueID[UUID_LENGTH + 1];
int global_uniqueID_flag = false;

life_cycle_ID life_cycleID;

typedef struct _queue_
{
	unsigned long pc;
	unsigned long lr;
	
	unsigned long long start_time;
	unsigned long long end_time;
	
	char thread_name[BUFFER_SIZE_128];
	char process_name[BUFFER_SIZE_128];
	char type[BUFFER_SIZE_128];
	
	char library_name[BUFFER_SIZE_128];
	char executable_path[BUFFER_SIZE_256];
	
	pid_t pid;	
	pid_t pgid;
	
	int used;
} fault_info;

fault_info k_fault_info[MAX_FAULT_INFO];
fault_info netflix_fault_info;

#ifndef CONFIG_VD_RELEASE
static struct mutex gator_fault_info_lock;
fault_info gator_fault_info;
#endif

int write = 0;
int netflix_write = 0;

char g_deadlock_process[BUFFER_SIZE_128];
bool g_deadlock_flag = false;

char g_cpuusage_process[BUFFER_SIZE_128];
bool g_cpuusage_flag = false;

char new_folder_name[BUFFER_SIZE_128];
int new_folder_name_flag = false;

int config = -1;
int error_kind = 0;
char process[BUFFER_SIZE_128];

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
char  *kpi_error[SIGUNUSED + 1] = {
	"Not Valid",
	"SIGHUP",
	"SIGINT",
	"SIGQUIT",
	"SIGILL",
	"SIGTRAP",
	"SIGABRT",
	//"SIGIOT", same as SIGABRT
	"SIGBUS",
	"SIGFPE",
	"SIGKILL",
	"SIGUSR1",
	"SIGSEGV",
	"SIGUSR2",
	"SIGPIPE",
	"SIGALRM",
	"SIGTERM",
	"SIGSTKFLT",
	"SIGCHLD",
	"SIGCONT",
	"SIGSTOP",
	"SIGTSTP",
	"SIGTTIN",
	"SIGTTOU",
	"SIGURG",
	"SIGXCPU",
	"SIGXFSZ",
	"SIGVTALRM",
	"SIGPROF",
	"SIGWINCH",
	"SIGIO",
	"SIGPWR",
	"SIGSYS",
};
#endif

int set_netflix_fault_info(int netflix_index)
{
    netflix_fault_info.pc = k_fault_info[netflix_index].pc;
    netflix_fault_info.lr = k_fault_info[netflix_index].lr;
    snprintf(netflix_fault_info.thread_name, sizeof(netflix_fault_info.thread_name), "%s", k_fault_info[netflix_index].thread_name);
    snprintf(netflix_fault_info.process_name, sizeof(netflix_fault_info.process_name), "%s", k_fault_info[netflix_index].process_name);
    snprintf(netflix_fault_info.type, sizeof(netflix_fault_info.type), "%s", k_fault_info[netflix_index].type);
    netflix_fault_info.pid = k_fault_info[netflix_index].pid;
    netflix_fault_info.pgid = k_fault_info[netflix_index].pgid;
	snprintf(netflix_fault_info.library_name, sizeof(netflix_fault_info.library_name), "%s", k_fault_info[netflix_index].library_name);
	snprintf(netflix_fault_info.executable_path, sizeof(netflix_fault_info.executable_path), "%s", k_fault_info[netflix_index].executable_path);
    netflix_fault_info.used = 1;
    printk(KERN_ERR "[kpi_fault_info] [u] fault info stored in netflix node with write index = %d\n",netflix_index);
    netflix_write = 1;
    return 0;
}


int get_netflix_fault_info(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[512];
    int len = -1;

    if( (netflix_write == 1) && ( netflix_fault_info.used == 1))
    {
        printk(KERN_ERR "[kpi_fault_info] netflix fault info  %lu %lu %s %s %s %d %s \n",netflix_fault_info.pc, netflix_fault_info.lr,
                        netflix_fault_info.thread_name, netflix_fault_info.process_name,
                        netflix_fault_info.type, netflix_fault_info.pgid, netflix_fault_info.library_name);
        snprintf(buffer, 512, "%lu|%lu|%s|%s|%s|%d|%s%c", netflix_fault_info.pc, netflix_fault_info.lr,
                        netflix_fault_info.thread_name, netflix_fault_info.process_name,
                        netflix_fault_info.type, netflix_fault_info.pgid, netflix_fault_info.library_name,'\0');

        len = (int)strlen(buffer);

        if(copy_to_user(buf, buffer, (long unsigned int)(len + 1)))
        {

            printk(KERN_ERR "[kpi_fault_info] read function copy_to_user fail\n");

            return -EFAULT;
        }

        netflix_fault_info.used = 0;
    }
    return len;
}

void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name, char* type, pid_t pgid)
{
	char path_buf[BUFFER_SIZE_256];
	char exe_path_buf[BUFFER_SIZE_256];
	const char *name;
	const char *exe_path;
	
	struct task_struct *task = current;
	struct task_struct *t;
	struct vm_area_struct *pc_vma, *lr_vma;
	struct mm_struct *mm;
	struct file *file;
	struct file *exe_file;

	struct timespec ts;

	printk(KERN_ERR "[kpi_fault_info] [k] %d -> write function call\n", write);
	printk(KERN_ERR "[kpi_fault_info] [k] %d -> p:0x%08lx l:0x%08lx th:%s pro:%s ty:%s pgid:%d\n", write, pc, lr, thread_name, process_name, type, pgid);
	
	mutex_lock(&kpi_fault_info_call_lock);
	if(!strcmp(type, DEADLOCK))
	{
		if(process_name != NULL)
		{
			snprintf(g_deadlock_process, sizeof(g_deadlock_process), "%s%c", process_name, '\0');
		}
		
		g_deadlock_flag = true;
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
			/* variable initialization */
			k_fault_info[write].pc = 0;
			k_fault_info[write].lr = 0;
		
			k_fault_info[write].start_time = 0;
			k_fault_info[write].end_time = 0;		
	
			strncpy(k_fault_info[write].thread_name, "NONE", sizeof(k_fault_info[write].thread_name));
			strncpy(k_fault_info[write].process_name, "NONE", sizeof(k_fault_info[write].process_name));
			strncpy(k_fault_info[write].type, "NONE", sizeof(k_fault_info[write].type));
			
			strncpy(k_fault_info[write].library_name, "NONE", sizeof(k_fault_info[write].library_name));
			strncpy(k_fault_info[write].executable_path, "NONE", sizeof(k_fault_info[write].executable_path));
			
			k_fault_info[write].pid	 = -1;
			k_fault_info[write].pgid = -1;
			
			/* set pc or pc offset */
			if(pc != 0)
			{
				k_fault_info[write].pc = pc;
				k_fault_info[write].pid = task->pid;

				k_fault_info[write].start_time = task->group_leader->real_start_time;	
				getrawmonotonic(&ts);
				k_fault_info[write].end_time = timespec_to_ns(&ts);	
		
	
				if(down_read_trylock(&task->mm->mmap_sem))
				{
					pc_vma = find_vma(task->mm, pc);
					
					if(pc_vma && (pc >= pc_vma->vm_start))
					{
						printk(KERN_ERR "[kpi_fault_info] [k] PC Start: 0x%08lx Offset: 0x%08lx\n", pc_vma->vm_start, pc - pc_vma->vm_start);
						
						k_fault_info[write].pc = pc - pc_vma->vm_start;
						
						/* check library name of pc value */
						file = pc_vma->vm_file;
						if(file)
						{
							name = d_path(&(file->f_path), path_buf, sizeof(path_buf));
							
							if(IS_ERR(name))
							{
								printk(KERN_ERR "[kpi_fault_info] [k] d_path error\n");
							}
							else
							{
								if(name != NULL)
								{
									printk(KERN_ERR "[kpi_fault_info] [k] d_path : [%s]\n", name);
									
									strncpy(k_fault_info[write].library_name, name, sizeof(k_fault_info[write].library_name));
								}
								else
								{
									printk(KERN_ERR "[kpi_fault_info] [k] d_path is none\n");
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
								
								strncpy(k_fault_info[write].library_name, name, sizeof(k_fault_info[write].library_name));
							}
							else
							{
								printk(KERN_ERR "[kpi_fault_info] [k] library name is none\n");
							}
						}
					}
					else
					{
						printk(KERN_ERR "[kpi_fault_info] [k] No VMA for ADDR PC\n");
					}

					/* get  path of executable file */
					exe_file = task->mm->exe_file;
					if(exe_file)
					{
						get_file(exe_file);
						exe_path = d_path( &(exe_file->f_path), exe_path_buf, sizeof(exe_path_buf));
						if(IS_ERR(exe_path))
						{
							printk(KERN_ERR "[kpi_fault_info] [k] exec d_path error\n");
						}
						else
						{
							if(exe_path != NULL)
							{
								printk(KERN_ERR "[kpi_fault_info] [k] exe d_path : [%s]\n", exe_path);

								strncpy(k_fault_info[write].executable_path, exe_path, sizeof(k_fault_info[write].executable_path));
							}
							else
							{
								printk(KERN_ERR "[kpi_fault_info] [k] exe d_path is none\n");
							}
						}
					}
					else
					{
						printk(KERN_ERR "[kpi_fault_info] [k] exe_file is none\n");
					}
					
					up_read(&task->mm->mmap_sem);
				}
				else
				{
					printk(KERN_ERR "[kpi_fault_info] [k] down_read_trylock fail\n");
				}
			}
			
			/* set lr or lr offset */
			if(lr != 0)
			{
				k_fault_info[write].lr = lr;
				
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
					}
					
					up_read(&task->mm->mmap_sem);
				}
				else
				{
					printk(KERN_ERR "[kpi_fault_info] [k] down_read_trylock fail\n");
				}
			}
			
			if(thread_name != NULL)
			{
				strncpy(k_fault_info[write].thread_name, thread_name, sizeof(k_fault_info[write].thread_name));
			}
			
			if(process_name != NULL)
			{
				strncpy(k_fault_info[write].process_name, process_name, sizeof(k_fault_info[write].process_name));
			}
			
			if(type != NULL)
			{
				if(g_deadlock_flag && (!strncmp(g_deadlock_process, k_fault_info[write].process_name, sizeof(g_deadlock_process))))
				{
					strncpy(k_fault_info[write].type, DEADLOCK, sizeof(k_fault_info[write].type));
					
					memset(g_deadlock_process, 0, sizeof(g_deadlock_process));
					g_deadlock_flag = false;
				}
				else if(g_cpuusage_flag &&(!strncmp(g_cpuusage_process, k_fault_info[write].process_name, sizeof(g_cpuusage_process))))
				{
					strncpy(k_fault_info[write].type, STABILITY_EXCESS_CPU, sizeof(k_fault_info[write].type));
					
					memset(g_cpuusage_process, 0, sizeof(g_cpuusage_process));
					g_cpuusage_flag = false;
				}
				else
				{
					strncpy(k_fault_info[write].type, type, sizeof(k_fault_info[write].type));
				}
			}
			printk(KERN_ERR "[kpi_fault_info] [K] start time:%llu, end time:%llu",
				k_fault_info[write].start_time,  k_fault_info[write].end_time);			
			
			k_fault_info[write].pgid = pgid;
            
            if((!strncmp(process_name, "GIBBON_MAIN", 11)) || (!strncmp(process_name, "netflix-app", 11)))
            {
                set_netflix_fault_info(write);
            }               
			
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
	char type[BUFFER_SIZE_32];
	char buffer[BUFFER_SIZE_128];

	int len = 0, app_pid = 0;

	printk(KERN_ERR "[kpi_fault_info] [u] %d -> write function call\n", write);

	mutex_lock(&kpi_fault_info_call_lock);

	if(count == 0)
	{
		mutex_unlock(&kpi_fault_info_call_lock);
		return (ssize_t)count;
	}

	if(count >= sizeof(buffer))
	{
		pr_err("Invalid size for writing, Maximum allowed size %d\n", BUFFER_SIZE_128);
		mutex_unlock(&kpi_fault_info_call_lock);
		return -EINVAL;
	}    

	if(copy_from_user(buffer, buf, sizeof(buffer)))
	{
		mutex_unlock(&kpi_fault_info_call_lock);

		printk(KERN_ERR "[kpi_fault_info] [u] write function copy_from_user fail\n");

		return -EFAULT;
	}

	buffer[count] = '\0';

	if(!strncmp(buffer, "netflix-app", strlen("netflix-app")))
	{
		sscanf(buffer, "%*s %s %d", type, &app_pid);
		mutex_unlock(&kpi_fault_info_call_lock);
		set_kpi_fault(0, 0, "netflix-app", "netflix-app", type, app_pid);
		return (ssize_t)count;
	}

	mutex_lock(&kpi_fault_info_lock);

	if(!strncmp(buffer, CPUUSAGE, strlen(CPUUSAGE)))
	{
		sscanf(buffer, "%*s %*d %*d %s ", g_cpuusage_process);

		len = strlen(g_cpuusage_process);
		g_cpuusage_process[len] = '\0';

		g_cpuusage_flag = true;
	}
	else
	{
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
			/* variable initialization */
			k_fault_info[write].pc = 0;
			k_fault_info[write].lr = 0;

			k_fault_info[write].start_time = 0;
			k_fault_info[write].end_time = 0;

			strncpy(k_fault_info[write].thread_name, "NONE", sizeof(k_fault_info[write].thread_name));
			strncpy(k_fault_info[write].process_name, "NONE", sizeof(k_fault_info[write].process_name));
			strncpy(k_fault_info[write].type, "NONE", sizeof(k_fault_info[write].type));

			strncpy(k_fault_info[write].library_name, "NONE", sizeof(k_fault_info[write].library_name));
			strncpy(k_fault_info[write].executable_path, "NONE", sizeof(k_fault_info[write].executable_path));

			k_fault_info[write].pid = -1;	
			k_fault_info[write].pgid = -1;

			/* set type and thread name, process name */
			if(!strncmp(buffer, HWERROOR, strlen(HWERROOR)))
			{
				sscanf(buffer, "%s %s ", k_fault_info[write].type, k_fault_info[write].process_name);

				strncpy(k_fault_info[write].thread_name, k_fault_info[write].process_name, sizeof(k_fault_info[write].thread_name));
			}
			else
			{
				sscanf(buffer, "%s ", k_fault_info[write].type);

				strncpy(k_fault_info[write].thread_name, k_fault_info[write].type, sizeof(k_fault_info[write].thread_name));
				strncpy(k_fault_info[write].process_name, k_fault_info[write].type, sizeof(k_fault_info[write].process_name));
			}

			printk(KERN_ERR "[kpi_fault_info] [u] %d -> p:0x%08lx l:0x%08lx th:%s pro:%s ty:%s pgid:%d\n",
					write, k_fault_info[write].pc, k_fault_info[write].lr, k_fault_info[write].thread_name,
					k_fault_info[write].process_name, k_fault_info[write].type, k_fault_info[write].pgid);

			k_fault_info[write].used = 1;
			++write;
		}
	}
	mutex_unlock(&kpi_fault_info_lock);
	mutex_unlock(&kpi_fault_info_call_lock);
	return (ssize_t)count;
}

static ssize_t kpi_fault_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[512];
	int len = 0;
	static int read = 0;
    
    struct task_struct *task = current;
	
	mutex_lock(&kpi_fault_info_lock);
	if(read >= MAX_FAULT_INFO)
	{
		read = 0;
	}
	
    if((!strncmp(task->comm, "GIBBON_MAIN", 11)) || (!strncmp(task->comm, "netflix-app", 11)))
    {
        len = get_netflix_fault_info(filp, buf, count, ppos);
    }
    else
    {	
        if(k_fault_info[read].used == 1)
		{
			/* process name : "X" => "XServer" */
			if(!strncmp(k_fault_info[read].process_name, "X", sizeof(k_fault_info[read].process_name)))
			{
				strncpy(k_fault_info[read].process_name, "XServer", sizeof(k_fault_info[read].process_name));
			}

			snprintf(buffer, sizeof(buffer), "%lu %lu %s %s %s %d %d %s %s %llu %llu %c", k_fault_info[read].pc, k_fault_info[read].lr,
					k_fault_info[read].thread_name, k_fault_info[read].process_name,
					k_fault_info[read].type, k_fault_info[read].pid, k_fault_info[read].pgid, 
					k_fault_info[read].library_name, k_fault_info[read].executable_path,
					k_fault_info[read].start_time, k_fault_info[read].end_time, '\0');

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
    }
	mutex_unlock(&kpi_fault_info_lock);
	return len;
}

static int kpi_fault_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, (int (*)(struct seq_file *, void *))kpi_fault_proc_read, NULL);
}

bool get_global_uniqueID(char *globalID)
{
	if(global_uniqueID_flag == false)
	{
		return false;
	}
	
	mutex_lock(&uniqueID_lock);
	snprintf(globalID, sizeof(global_uniqueID), "%s%c", global_uniqueID, '\0');
	mutex_unlock(&uniqueID_lock);
	return true;
}
EXPORT_SYMBOL(get_global_uniqueID);

static ssize_t global_uniqueID_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[BUFFER_SIZE_128];
	
	mutex_lock(&uniqueID_lock);
    
    if(count == 0)
    {
        mutex_unlock(&uniqueID_lock);
        return (ssize_t)count;
    }

    if(count >= sizeof(buffer))
    {
        pr_err("Invalid size for writing, Maximum allowed size %d\n", BUFFER_SIZE_128);
        mutex_unlock(&uniqueID_lock);
        return -EINVAL;
    }

	global_uniqueID_flag = false;
	
	if(copy_from_user(buffer, buf, count))
	{
		mutex_unlock(&uniqueID_lock);
		
		printk(KERN_ERR "[kpi_fault_info] [u] global uniqueID write function copy_from_user fail\n");
		
		return -EFAULT;
	}
	
    buffer[count] = '\0';
	strncpy(global_uniqueID, buffer, sizeof(global_uniqueID));
	global_uniqueID[UUID_LENGTH] = '\0';
	
	global_uniqueID_flag = true;
	mutex_unlock(&uniqueID_lock);
	return (ssize_t)count;
}

static ssize_t global_uniqueID_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, global_uniqueID, strlen(global_uniqueID));
}

void set_life_cycleID(void)
{
	struct timeval now;
	
	do_gettimeofday(&now);
	if(life_cycleID.time.tv_sec != 0)
	{
		if(now.tv_sec - life_cycleID.time.tv_sec > 300)	// 5 minute
		{
			snprintf(life_cycleID.lcid, sizeof(life_cycleID.lcid), "%lu%c", now.tv_sec, '\0');
		}
	}
	else
	{
		snprintf(life_cycleID.lcid, sizeof(life_cycleID.lcid), "%lu%c", now.tv_sec, '\0');
	}
	
	life_cycleID.time.tv_sec = now.tv_sec;
}

void get_life_cycleID(char *life_cyclelID)
{
	mutex_lock(&uniqueID_lock);
	set_life_cycleID();
	
	snprintf(life_cyclelID, sizeof(life_cycleID.lcid), "%s%c", life_cycleID.lcid, '\0');
	mutex_unlock(&uniqueID_lock);
}
EXPORT_SYMBOL(get_life_cycleID);

static ssize_t life_cycleID_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	mutex_lock(&uniqueID_lock);
	set_life_cycleID();
	mutex_unlock(&uniqueID_lock);
	
	return simple_read_from_buffer(buf, count, ppos, life_cycleID.lcid, strlen(life_cycleID.lcid));
}

bool get_new_folder_name(char *new_folderpath)
{
	if(new_folder_name_flag == false)
	{
		return false;
	}
	
	mutex_lock(&new_folder_name_lock);
	snprintf(new_folderpath, strlen(new_folder_name) + 2, "%s%c%c", new_folder_name, '/', '\0');
	memset(new_folder_name, 0, sizeof(new_folder_name));
	
	new_folder_name_flag = false;
	mutex_unlock(&new_folder_name_lock);
	return true;
}
EXPORT_SYMBOL(get_new_folder_name);

static ssize_t new_folder_name_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[BUFFER_SIZE_128];
	int len;

	mutex_lock(&new_folder_name_lock);

	if(count == 0)
	{
		mutex_unlock(&new_folder_name_lock);
		return (ssize_t)count;
	}

	if(count >= sizeof(buffer))
	{
		pr_err("Invalid size for writing, Maximum allowed size %d\n", BUFFER_SIZE_128);
		mutex_unlock(&new_folder_name_lock);
		return -EINVAL;
	}

	if(copy_from_user(buffer, buf, count))
	{
		mutex_unlock(&new_folder_name_lock);

		printk(KERN_ERR "[kpi_fault_info] [u] new folder name write function copy_from_user fail\n");

		return -EFAULT;
	}

	buffer[count] = '\0';	
	memset(new_folder_name, 0, sizeof(new_folder_name));
	sscanf(buffer, "%s ", new_folder_name);
	len = (int)strlen(new_folder_name);
	new_folder_name[len] = '\0';

	printk(KERN_ERR "[kpi_fault_info] [u] new folder name : %s\n", new_folder_name);

	new_folder_name_flag = true;
	mutex_unlock(&new_folder_name_lock);
	return (ssize_t)count;
}

static ssize_t coredump_control_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	int idx = 0, len = 0;
	char buffer[BUFFER_SIZE_128];
	char error_kind_str[BUFFER_SIZE_128];

	mutex_lock(&coredump_control_lock);

	if(count == 0)
	{
		mutex_unlock(&coredump_control_lock);
		return (ssize_t)count;
	}

	if(count >= sizeof(buffer))
	{
		pr_err("Invalid size for writing, Maximum allowed size %d\n", BUFFER_SIZE_128);
		mutex_unlock(&coredump_control_lock);
		return -EINVAL;
	}

	if(copy_from_user(buffer, buf, count))
	{
		mutex_unlock(&coredump_control_lock);

		printk(KERN_ERR "[kpi_fault_info] [u] coredump control write function copy_from_user fail\n");

		return -EFAULT;
	}

	buffer[count] = '\0';
	sscanf(buffer, "%d %*s %*s", &config);
	if((config != NOTCORE) && (config != -1))
	{
		sscanf(buffer, "%*d %s %s", error_kind_str, process);
		len = (int)strlen(process);
		process[len] = '\0';

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
		while(idx < SIGUNUSED + 1)
		{
			if(!strncmp(kpi_error[idx], error_kind_str, sizeof(error_kind_str)))
			{
				error_kind = idx;
				break;
			}

			idx++;
		}
#endif
	}
	mutex_unlock(&coredump_control_lock);
	return (ssize_t)count;
}

int get_Coredump_config(char *process_name, int *signal_type)
{
	int ret = -1;

    mutex_lock(&coredump_control_lock);

    ret = config;
	if(ret == -1)
	{
        mutex_unlock(&coredump_control_lock);
		return -1;
	}
	else if(ret == 0)
	{
		config = -1; 
        mutex_unlock(&coredump_control_lock);
		return 0;
	}
	
	snprintf(process_name, strlen(process) + 1, "%s%c", process, '\0');
	memset(process, 0, sizeof(process));
	
	*signal_type = error_kind;
	error_kind = 0;
	
	config = -1;
	mutex_unlock(&coredump_control_lock);
	return ret;
}
EXPORT_SYMBOL(get_Coredump_config);

#ifndef CONFIG_VD_RELEASE
void set_gator_fault(void)
{
	struct task_struct *fault_task;

	fault_task = current;

	mutex_lock(&gator_fault_info_lock);
	if (gator_fault_info.used == 1) {
		pr_err("[set_gator_fault] queue buffer max\n");
	} else {
		snprintf(gator_fault_info.process_name,
			sizeof(gator_fault_info.process_name),
				"%s", fault_task->group_leader->comm);
		gator_fault_info.pid = fault_task->pid;
		gator_fault_info.used = 1;
		pr_err("[set_gator_fault] pro:%s pid:%d\n",
			 gator_fault_info.process_name, gator_fault_info.pid);
	}
	mutex_unlock(&gator_fault_info_lock);
}
EXPORT_SYMBOL(set_gator_fault);

static ssize_t gator_fault_proc_write(struct file *filp,
	const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[32];

	mutex_lock(&gator_fault_info_lock);

	if(!count){
		mutex_unlock(&gator_fault_info_lock);
		return (ssize_t)count;
	}

	if(!(count < sizeof(buffer))){
		mutex_unlock(&gator_fault_info_lock);
		pr_err("Invalid size for writing, Maximum allowed size %u\n", sizeof(buffer));
		return -EINVAL;
	}

	if(copy_from_user(buffer, buf, count))
	{
		mutex_unlock(&gator_fault_info_lock);
		pr_err("[gator_fault_proc_write] write to user fail\n");	
		return -EFAULT;
	}

	buffer[count] = '\0';

	if (gator_fault_info.used == 1) {
		mutex_unlock(&gator_fault_info_lock);
		pr_err("[gator_fault_proc_write] queue buffer max\n");
		return (ssize_t)count;
	}

	gator_fault_info.pid = 0;	
	snprintf(gator_fault_info.process_name, 
			sizeof(gator_fault_info.process_name), "NULL");

	sscanf(buffer, "%d|%s", &(gator_fault_info.pid),
			gator_fault_info.process_name);
	gator_fault_info.used = 1;

	pr_err("[gator_fault_proc_write] pro:%s pid:%d\n",
			gator_fault_info.process_name, gator_fault_info.pid);

	mutex_unlock(&gator_fault_info_lock);
	return (ssize_t)count;
}

static ssize_t gator_fault_proc_read(struct file *filp,
	char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[128];
	int len = 0;

	mutex_lock(&gator_fault_info_lock);

	if (gator_fault_info.used == 1) {
		snprintf(buffer, sizeof(buffer), "%d|%s",
			gator_fault_info.pid, gator_fault_info.process_name);

		len = (int)strlen(buffer);
		if (copy_to_user(buf, buffer, (unsigned long)(len+1))) {
			pr_err("[gator_fault_proc_read] read function copy_to_user fail\n");
			len = -EFAULT;
		} else {
			gator_fault_info.used = 0;
			pr_err("[gator_fault_proc_read] %s\n", buffer);
		}
	}
	mutex_unlock(&gator_fault_info_lock);
	return len;
}

static int gator_fault_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, (int (*)(struct seq_file *,
			 void *))gator_fault_proc_read, NULL);
}

static const struct file_operations gator_fault_info_fops = {
	.open  = gator_fault_info_open,
	.read  = gator_fault_proc_read,
	.write = gator_fault_proc_write,
};
#endif

static const struct file_operations kpi_fault_info_fops = {
	.open  = kpi_fault_info_open,
	.read  = kpi_fault_proc_read,
	.write = kpi_fault_info_write,
};

const struct file_operations global_uniqueID_fops = {
	.read  = global_uniqueID_read,
	.write = global_uniqueID_write,
};

const struct file_operations life_cycleID_fops = {
	.read  = life_cycleID_read,
};

const struct file_operations new_folder_name_fops = {
	.write = new_folder_name_write,
};

const struct file_operations coredump_control_fops = {
	.write = coredump_control_write,
};

static int __init proc_kpi_fault_info_init(void)
{
	mutex_init(&kpi_fault_info_lock);
	mutex_init(&kpi_fault_info_call_lock);
	
	mutex_init(&uniqueID_lock);
	
	mutex_init(&new_folder_name_lock);
	
	mutex_init(&coredump_control_lock);
	
	proc_create("kpi_fault_info", 0666, NULL, &kpi_fault_info_fops);
	
	proc_create("global_uniqueID", 0644, NULL, &global_uniqueID_fops);
	proc_create("life_cycleID", 0666, NULL, &life_cycleID_fops);
	
	proc_create("new_folder_name", 0666, NULL, &new_folder_name_fops);
	
	proc_create("coredump_control", 0666, NULL, &coredump_control_fops);

#ifndef CONFIG_VD_RELEASE
	mutex_init(&gator_fault_info_lock);
	gator_fault_info.used = 0;
	proc_create("gator_fault_info", 0666, NULL, &gator_fault_info_fops);
#endif

	return 0;
}
module_init(proc_kpi_fault_info_init);

