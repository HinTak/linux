#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MAX_PREFIX_LEN	(64)
#define MAX_MSG_LEN	(256)
#define MAX_BUFFER	(20)

static struct mutex kpi_hw_error_info_lock;

typedef struct _queue
{
	char prefix[MAX_PREFIX_LEN + 1];
	char msg[MAX_MSG_LEN + 1];
	
	int used;
} hw_error_info_t;

hw_error_info_t hw_error_info[MAX_BUFFER];
static int write = 0;

void set_kpi_hw_error(char* prefix, char* msg)
{
	//printk(KERN_ERR "[set_kpi_hw_error] [k] %d -> msg:%s print_option:%d\n", write, msg, print_option);
	
	mutex_lock(&kpi_hw_error_info_lock);
	if(write >= MAX_BUFFER)
	{
		write = 0;
	}
	
	if(hw_error_info[write].used == 1)
	{
		printk(KERN_ERR "[set_kpi_hw_error] [k] queue buffer max\n");
	}
	else
	{
		if(prefix != NULL)
		{
			strncpy(hw_error_info[write].prefix, prefix, MAX_PREFIX_LEN);
		}
		
		if(msg != NULL)
		{
			strncpy(hw_error_info[write].msg, msg, MAX_MSG_LEN);
		}
		
		hw_error_info[write].used = 1;
		write++;
	}
	mutex_unlock(&kpi_hw_error_info_lock);
	
	printk(KERN_ERR "[VD_KPI_HW_ERR] %s %s \n", prefix, msg);
}
EXPORT_SYMBOL(set_kpi_hw_error);

static ssize_t kpi_hw_error_proc_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[MAX_MSG_LEN + 10];
	
	//printk(KERN_ERR "[set_kpi_hw_error] [u] %d -> write function call\n", write);
	
	memset(buffer, 0, MAX_MSG_LEN + 10);
	
	mutex_lock(&kpi_hw_error_info_lock);
	if (copy_from_user(buffer, buf, MAX_MSG_LEN))
	{
		mutex_unlock(&kpi_hw_error_info_lock);
		printk(KERN_ERR "[set_kpi_hw_error] [u] write function copy_from_user fail\n");
		return -EFAULT;
	}
	
	if(write >= MAX_BUFFER)
	{
		write = 0;
	}
	
	if(hw_error_info[write].used == 1)
	{
		printk(KERN_ERR "[set_kpi_hw_error] [u] queue buffer max\n");
	}
	else
	{
		strncpy(hw_error_info[write].prefix, "USER-ERROR", MAX_PREFIX_LEN);
		strncpy(hw_error_info[write].msg, buffer, MAX_MSG_LEN);
		
		hw_error_info[write].used = 1;
		write++;
	}
	mutex_unlock(&kpi_hw_error_info_lock);
	
	printk(KERN_ERR "[VD_KPI_HW_ERR] USER-ERROR %s \n", buffer);
	return (ssize_t)count;
}

static ssize_t kpi_hw_error_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[MAX_PREFIX_LEN + MAX_MSG_LEN + 10];
	int len = -1;
	static int read = 0;
	
	memset(buffer, 0, MAX_PREFIX_LEN + MAX_MSG_LEN + 10);
	
	mutex_lock(&kpi_hw_error_info_lock);
	if(read >= MAX_BUFFER)
	{
		read = 0;
	}
	
	if(hw_error_info[read].used == 1)
	{
		snprintf(buffer, MAX_PREFIX_LEN+MAX_MSG_LEN, "%s %s %c", hw_error_info[read].prefix, hw_error_info[read].msg, '\0');
		len = (int)strlen(buffer);
		
		if(copy_to_user(buf, buffer, (long unsigned int)len))
		{
			mutex_unlock(&kpi_hw_error_info_lock);
			printk(KERN_ERR "[set_kpi_hw_error] read function copy_to_user fail\n");
			return -EFAULT;
		}
		
		hw_error_info[read].used = 0;
		read++;
	}
	mutex_unlock(&kpi_hw_error_info_lock);
	return len;
}

static int kpi_hw_error_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, (int (*)(struct seq_file *, void *))kpi_hw_error_proc_read, NULL);
}

static const struct file_operations kpi_hw_error_info_fops = {
	.open  = kpi_hw_error_info_open,
	.read  = kpi_hw_error_proc_read,
	.write  = kpi_hw_error_proc_write,
};

static int __init proc_kpi_hw_error_info_init(void)
{
	mutex_init(&kpi_hw_error_info_lock);
	
	proc_create("kpi_hw_error_info", 0, NULL, &kpi_hw_error_info_fops);
	
	return 0;
}
module_init(proc_kpi_hw_error_info_init);

