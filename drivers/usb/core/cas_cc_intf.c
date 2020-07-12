/*
 * cas_cc_intf.c - Command & Control interface
 *
 * Developed by : Gaurav Singhal (gaurav.s4@samsung.com)
 * Reviewed by :
 *
 * Copyright (C) 2014-2015 Samsung Electronics Co., Ltd.
 *			http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kthread.h>

#include <linux/cas_proto.h>


static DEFINE_SPINLOCK(event_list_lock);


static dev_t cas_devno;
static struct class	*cas_class = NULL;
static struct cdev char_dev;
static struct device *device = NULL;
static struct cas_proto_dev_ctx dev_ctx_arr[] = {
	{.valid = 0, .dongle_id = 0, .udev = NULL},
	{.valid = 0, .dongle_id = 1, .udev = NULL},
	{.valid = 0, .dongle_id = 2, .udev = NULL},
	{.valid = 0, .dongle_id = 3, .udev = NULL}
};
static struct completion intr_event;
static struct list_head event_list;
static struct list_head status_event_list;


extern void register_cas_scsi_callback(void(*callback)(struct cas_proto_evt_ctx *));
extern void unregister_cas_scsi_callback(void);


int find_dongle_by_udev(struct usb_device *udev)
{
	int index = -1;

	for(index = 0; index < MAX_NUM_DONGLE; index++)
	{
		if(dev_ctx_arr[index].udev == udev)
		{
			break;
		}
	}

	if(index == MAX_NUM_DONGLE)
	{
		index = -1;
	}

	return index;
}
EXPORT_SYMBOL(find_dongle_by_udev);


int update_dongle_connect(struct usb_device *udev)
{
	int index = -1;

	for(index = 0; index < MAX_NUM_DONGLE; index++)
	{
		if(dev_ctx_arr[index].valid == 0)
		{
			dev_ctx_arr[index].valid = 1;
			dev_ctx_arr[index].udev = udev;

			break;
		}
	}

	if(index == MAX_NUM_DONGLE)
	{
		index = -1;
	}

	return index;
}
EXPORT_SYMBOL(update_dongle_connect);


int update_dongle_disconnect(struct usb_device *udev)
{
	int index = -1;

	for(index = 0; index < MAX_NUM_DONGLE; index++)
	{
		if(dev_ctx_arr[index].udev == udev)
		{
			dev_ctx_arr[index].valid = 0;
			dev_ctx_arr[index].udev = NULL;

			break;
		}
	}

	if(index == MAX_NUM_DONGLE)
	{
		index = -1;
	}

	return index;
}
EXPORT_SYMBOL(update_dongle_disconnect);


int remove_event_from_queue(int dongle, cas_proto_evt evt, struct cas_proto_evt_ctx **ctx)
{
	struct cas_proto_evt_ctx *evt_ctx = NULL;
	struct list_head *tmp = NULL;
	int retval = 1;

	list_for_each(tmp, &status_event_list)
	{
		evt_ctx = list_first_entry(tmp, struct cas_proto_evt_ctx, list_status_event);
		if(evt_ctx->ctx.dongle_id == dongle && evt_ctx->ctx.cas_evt == evt)
		{
			list_del(&evt_ctx->list);
			list_del(&evt_ctx->list_status_event);
			*ctx = evt_ctx;
			retval = 0;

			break;
		}
	}

	return retval;
}


// To be called from step usb driver and mass storage driver(callback)
void cas_cc_enqueue_event(struct cas_proto_evt_ctx *ctx_list)
{
	unsigned long flags;
	struct cas_proto_evt_ctx *ctx;

	spin_lock_irqsave(&event_list_lock, flags);
	if(ctx_list->ctx.cas_evt == CNC_NOTIFY_DEVICE_DISCONNECT)
	{
		// Check connect event was processed or not
		if(remove_event_from_queue(ctx_list->ctx.dongle_id, CNC_NOTIFY_DEVICE_CONNECT, &ctx))
		{
			list_add_tail(&ctx_list->list, &event_list);
		}
		else
		{
			spin_unlock_irqrestore(&event_list_lock, flags);
			printk(KERN_ERR "[%s] found connect event for remove, ctx -> %p\n", __func__, ctx);
			kfree(ctx);
			ctx = NULL;
			kfree(ctx_list);
			ctx_list = NULL;

			goto out;
		}
	}
	else if(ctx_list->ctx.cas_evt == CNC_NOTIFY_DEVICE_CONNECT)
	{
		list_add_tail(&ctx_list->list_status_event, &status_event_list);
		list_add_tail(&ctx_list->list, &event_list);
	}
	else
	{
		list_add_tail(&ctx_list->list, &event_list);
	}
	spin_unlock_irqrestore(&event_list_lock, flags);
	complete(&intr_event);

out:
	return;		
}
EXPORT_SYMBOL(cas_cc_enqueue_event);


int cas_cc_open(struct inode *inode, struct file *file)
{
	printk(KERN_ERR "%s\n", __func__);

	return 0;
}


int cas_cc_close(struct inode *inode, struct file *file) 
{
	file->private_data = NULL;
	printk(KERN_ERR "%s\n", __func__);

	return 0;
}


ssize_t cas_cc_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct cas_proto_evt_ctx *ctx_list = NULL;
	struct list_head *list = NULL;
	int retval = 0;
	unsigned long flags;

	printk(KERN_ERR "[%s] waiting for event notification\n", __func__);
	// Wait for dongle to send event
	retval = wait_for_completion_interruptible(&intr_event);
	if(retval < 0)
	{
		goto exit;
	}

	list = &event_list;
	spin_lock_irqsave(&event_list_lock, flags);
	if(!list_empty(list))
	{
		ctx_list = list_entry(list->next, struct cas_proto_evt_ctx, list);

		list_del(list->next);
		if( CNC_NOTIFY_DEVICE_CONNECT == ctx_list->ctx.cas_evt)
		{
			list_del(&ctx_list->list_status_event);
		}
		spin_unlock_irqrestore(&event_list_lock, flags);
		printk(KERN_ERR "[%s] user space cas_read, ctx -> %p\n", __func__, ctx_list);
		copy_to_user(buf, &ctx_list->ctx, sizeof(struct cas_proto_ctx));
		kfree(ctx_list);
		ctx_list = NULL;
		retval = sizeof(struct cas_proto_ctx);
	}
	else
	{
		spin_unlock_irqrestore(&event_list_lock, flags);
		printk(KERN_ERR "[%s] event queue empty\n", __func__);
	}

exit:
	return retval;
}


// Mainly used for tvkey dongle reset
ssize_t cas_cc_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	struct usb_device *udev = NULL;
	int dongle_id = 0;

	sscanf(buf, "%d", &dongle_id);
	printk(KERN_ERR "[%s] usb reset request for dongle id is %d\n", __func__, dongle_id);

	if(dongle_id < 0 || dongle_id >= MAX_NUM_DONGLE)
	{
		printk(KERN_ERR "[%s] invalid dongle id for device reset\n", __func__);
		return -EINVAL;
	}

	udev = dev_ctx_arr[dongle_id].udev;
	if(udev)
	{
		usb_reset_device(udev);
	}
	else
	{
		printk("[%s] usb step driver not found\n", __func__);
	}

	return len;
}


const struct file_operations cas_fops = {
	.owner		= THIS_MODULE,
	.open		= cas_cc_open,
	.release	= cas_cc_close,
	.read		= cas_cc_read,
	.write		= cas_cc_write,
};


int cas_cc_create_char_dev(void)
{
	int status = 0;

	cas_class = class_create(THIS_MODULE, "casprotocol");
	if(IS_ERR(cas_class))
	{
		status = PTR_ERR(cas_class);
		goto exit;
	}

	status = alloc_chrdev_region(&cas_devno, 0, 1, "cas_proto");
	if(status)
	{
		class_destroy(cas_class);
		goto exit;
	}

	// Create the device and register it
	device = device_create(cas_class, NULL, cas_devno, NULL, "cas_event");
	if(IS_ERR(device))
	{
		status = PTR_ERR(device);
		unregister_chrdev_region(cas_devno, 1);
		class_destroy(cas_class);
		goto exit;
	}

	// Initialize the character device structure
	cdev_init(&char_dev, &cas_fops);

	// Add the characte device to the system
	status = cdev_add(&char_dev, cas_devno, 1);
	if(status < 0)
	{
		// An error occurs if cdev_add() return a negative value. Return with this status
		unregister_chrdev_region(cas_devno, 1);
		device_destroy(cas_class, cas_devno);
		class_destroy(cas_class);
		goto exit;
	}

	return 0;

exit:
	printk(KERN_ERR "[%s] error code=%d\n", __func__, status);
	return status;
}


void cas_cc_remove_char_dev(void)
{
	device_destroy(cas_class, cas_devno);
	unregister_chrdev_region(cas_devno, 1);
	class_destroy(cas_class);
}


// To be called from usb core init
void register_cas_cc_intf(void)
{
	// Create /dev entry for event notification to user space program
	cas_cc_create_char_dev();

	// register callback to receive probe/disconnect event from SCSI
	register_cas_scsi_callback(cas_cc_enqueue_event);

	init_completion(&intr_event);
	INIT_LIST_HEAD(&event_list);
	INIT_LIST_HEAD(&status_event_list);

	printk(KERN_INFO "[%s] register C&C driver\n", __func__);
}

// To be called from usb core exit
void unregister_cas_cc_intf(void)
{
	cas_cc_remove_char_dev();
	unregister_cas_scsi_callback();
	printk(KERN_INFO "[%s] unregister C&C driver\n", __func__);
}

