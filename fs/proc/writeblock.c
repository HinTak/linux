#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/blkdev.h>

extern void lock_supers(void);

static ssize_t write_block_write(struct file *file, const char __user *buf,
                size_t count, loff_t *ppos)
{
	char buffer[10];

	if (!count)
		return count;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
        if (buffer[count - 1] == '\n')
        {
                buffer[count - 1] = '\0';
        }

	if (!strcmp(buffer, "1"))
	{
		lock_supers();
		printk( "\n Write block!!!!!!!\n");
	}
	else
	{
		printk("Usage : echo [ 1 ] > /proc/writeblock\n");
		printk("        1 : writeblock\n");
	}

	return count;
}

static const struct file_operations write_block_fops = {
	.write	= write_block_write,
};

static int __init write_block_init(void)
{
	proc_create("writeblock", S_IRUSR | S_IWUSR, NULL, 
			&write_block_fops);

	return 0;
}
module_init(write_block_init);
