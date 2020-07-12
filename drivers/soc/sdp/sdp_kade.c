/*********************************************************************************************
 *
 *	sdp_hwmem.c (Samsung Soc DMA memory allocation)
 *
 *	author : tukho.kim@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
// Jul,09,2010 	tukho.kim	created
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>		
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/jiffies.h> 
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include <asm/memory.h>		// alloc_page_node
#include <asm/uaccess.h>	// copy_from_user
#include <asm-generic/mman.h>	// copy_from_user
#include <asm/cacheflush.h>	// copy_from_user

#include <soc/sdp/sdp_kade.h>
#include <soc/sdp/soc.h>
#include "common.h"

#define DRV_KADE_NAME		"sdp_kade"
#define SDP_KADE_MINOR		121

static enum sdp_kade_sys_errorno sdp_kade_system_status = 0;

void sdp_kade_set_system_error(unsigned int error)
{

	sdp_kade_system_status |= error;
}

EXPORT_SYMBOL(sdp_kade_set_system_error);

unsigned int sdp_kade_get_system_error(void)
{
	return sdp_kade_system_status;
}

EXPORT_SYMBOL(sdp_kade_get_system_error);


static long 
sdp_kade_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	int ret_val = 0;
	unsigned long tmp;
	
	switch(cmd){
		case (CMD_KADE_GET_SYSERROR): 
			tmp = sdp_kade_get_system_error();
			copy_to_user((void *) args, &tmp, sizeof(unsigned long));
			break;
		case (CMD_KADE_GET_CHIPID2): 
			tmp = sdp_soc();
			copy_to_user((void *) args, &tmp, sizeof(unsigned long));
			break;
		case (CMD_KADE_SET_ERROR):
			copy_from_user(&tmp, (void *) args, sizeof(unsigned long));
			sdp_kade_set_system_error(tmp);
			break;
		default:
			ret_val = -EINVAL;
			break;
	}	

	return ret_val;
}

static int sdp_kade_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int sdp_kade_release (struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations sdp_kade_fops = {
	.owner = THIS_MODULE,
	.open  = sdp_kade_open,
	.release = sdp_kade_release,
	.unlocked_ioctl = sdp_kade_ioctl,
};

static struct miscdevice sdp_kade_dev = {
	.minor = SDP_KADE_MINOR,
	.name = DRV_KADE_NAME,
	.fops = &sdp_kade_fops,
	.mode = 0600,
#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	.lab_smk64 = "*",
#endif	
};

static int __init sdp_kade_init(void)
{
	int ret_val = 0;

	ret_val = misc_register(&sdp_kade_dev);

	if(ret_val){
		printk(KERN_ERR "[ERR]%s: misc register failed\n", DRV_KADE_NAME);
	}
	else {
		printk(KERN_INFO"[DRV_KADE_NAME] %s initialized\n", DRV_KADE_NAME);
	}

	sdp_kade_system_status = 0;
	
	return ret_val;
}

static void __exit sdp_kade_exit(void)
{

	misc_deregister(&sdp_kade_dev);

	return;
}

module_init(sdp_kade_init);
module_exit(sdp_kade_exit);

MODULE_AUTHOR("seungjun.heo@samsung.com");
MODULE_DESCRIPTION("Driver for SDP Kade");
MODULE_LICENSE("GPL");

