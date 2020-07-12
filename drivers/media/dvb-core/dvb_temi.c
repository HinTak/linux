/*
 * dvb_temi.c: generic DVB functions for TEMI interfaces
 *
 * Copyright (C) 2004 Andrew de Quincey
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2003 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * based on code:
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <linux/time.h>
#include <linux/ktime.h>

#include "dvb_temi.h"

#include <t2ddebugd/t2ddebugd.h>
#include <linux/t2d_print.h>

static int dvb_temi_debug;

module_param_named(temi_debug, dvb_temi_debug, int, 0644);
MODULE_PARM_DESC(temi_debug, "enable verbose debug messages");

#define dprintk if (dvb_temi_debug) printk

#define DEFAULT_DMX_BUFFER_SIZE	(256*1024)

#define TEMI_BUFFER_SIZE (1024*1024)



/* Private temi-interface information */
struct dvb_temi_private {

	/* pointer back to the public data structure */
	struct dvb_temi *pub;

	/* the DVB device */
	struct dvb_device *dvbdev;

	/* Flags describing the interface (DVB_CA_FLAG_*) */
	u32 flags;

	/* Flag indicating if the temi device is open */
	unsigned int open;

	/* mutex serializing ioctls */
	struct mutex mutex;
	spinlock_t lock;
};

/**
 * Implementation of file open syscall.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_temi_io_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_temi_private *dmx = dvbdev->priv;
	struct dvb_temi *dmxpub = dmx->pub;
	
	int err;
	void *mem;

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;
	
	if (!dvbdev->readers) {
		mutex_unlock(&dmx->mutex);
		return -EBUSY;
	}
	mem = vmalloc((unsigned long)TEMI_BUFFER_SIZE);
	if (!mem) {
		mutex_unlock(&dmx->mutex);
		return -ENOMEM;
	}	

	dvb_ringbuffer_init(&dmxpub->temi_buffer, mem, TEMI_BUFFER_SIZE);


	dprintk("%s\n", __func__);

	if (!try_module_get(dmxpub->owner))
		return -EIO;

	err = dvb_generic_open(inode, file);
	if (err < 0) {
		module_put(dmxpub->owner);
		return err;
	}

	dmx->open++ ;
	dvbdev->readers--;
	dvbdev->users++;
	
	mutex_unlock(&dmx->mutex);
	return 0;
}

/**
 * Implementation of file close syscall.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_temi_io_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_temi_private *dmx = dvbdev->priv;
	struct dvb_temi *dmxpub = dmx->pub;

	int err;

	mutex_lock(&dmx->mutex);

	dprintk("%s\n", __func__);

	/* mark the temi device as closed */
	dvbdev->readers++;
	dvbdev->users--;
	if (dmxpub->temi_buffer.data) {
		void *mem = dmxpub->temi_buffer.data;
		mb();
		spin_lock_irq(&dmx->lock);
		dmxpub->temi_buffer.data = NULL;
		spin_unlock_irq(&dmx->lock);
		vfree(mem);
	}

	if(dmx->open)
	{
		dmx->open--;
	}

	err = dvb_generic_release(inode, file);

	mutex_unlock(&dmx->mutex);
	module_put(dmxpub->owner);

	return err;
}


static int dvb_temi_buffer_write(struct dvb_ringbuffer *buf,
				   const u8 *src, size_t len)
{
	ssize_t free;

	if (!len)
		return 0;
	if (!buf->data)
		return 0;

	free = dvb_ringbuffer_free(buf);

	if (len > (size_t)free) {
		return -EOVERFLOW;
	}
	return dvb_ringbuffer_write(buf, src, len);
}

static ssize_t dvb_temi_buffer_read(struct dvb_ringbuffer *src,
				      int non_blocking, char __user *buf,
				      size_t count, loff_t *ppos)
{
	size_t todo;
	ssize_t avail;
	ssize_t ret = 0;

	if (!src->data)
		return 0;

	if (src->error) {
		ret = src->error;
		dvb_ringbuffer_flush(src);
		return ret;
	}

	for (todo = count; todo > 0; todo -= (size_t)ret) {
		if (non_blocking) {
			int is_empty = dvb_ringbuffer_empty(src);
			if (is_empty) {
				ret = -EWOULDBLOCK;
				break;
			}
		}

		ret = wait_event_interruptible(src->queue,
				       !dvb_ringbuffer_empty(src) ||
				       (src->error != 0));
		if (ret < 0)
			break;

		if (src->error) {
			ret = src->error;
			dvb_ringbuffer_flush(src);
			break;
		}

		avail = dvb_ringbuffer_avail(src);
		if (avail > (ssize_t)todo)
			avail = (ssize_t)todo;

		ret = dvb_ringbuffer_read_user(src, buf, (size_t)avail);
		if (ret < 0)
			break;

		buf += ret;
	}

	return (count - todo) ? (ssize_t)(count - todo) : ret;
}

static int dvb_temi_callback(struct dvb_temi *dmxpub, const u8 *buffer1, size_t buffer1_len, const u8 *buffer2, size_t buffer2_len)
{
	int ret;

	struct dvb_temi_private *dmx = dmxpub->private;
	struct dvb_ringbuffer *buffer = &dmxpub->temi_buffer;
	
	spin_lock(&dmx->lock);

	if (buffer->error) 
	{
		spin_unlock(&dmx->lock);
		wake_up(&buffer->queue);
		return 0;
	}
		
	ret = dvb_temi_buffer_write(buffer, buffer1, buffer1_len);
	if (ret == (int)buffer1_len) 
	{
		ret = dvb_temi_buffer_write(buffer, buffer2, buffer2_len);
	}
	if (ret < 0)
		buffer->error = ret;
	spin_unlock(&dmx->lock);
	wake_up(&buffer->queue);

	return 0;
}


static ssize_t dvb_temi_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_temi_private *dmx = dvbdev->priv;
	struct dvb_temi *dmxpub = dmx->pub;
	
	int ret;
	ret = dvb_temi_buffer_read(&dmxpub->temi_buffer,
			      file->f_flags & O_NONBLOCK, buf, count, ppos);

	return ret;
}

static int dvb_temi_set_buffer_size(struct dvb_temi_private *dmx,
				      unsigned long size)
{
	struct dvb_temi *dmxpub = dmx->pub;
	struct dvb_ringbuffer *buf = &dmxpub->temi_buffer;
	void *newmem;
	void *oldmem;

	dprintk("function : %s\n", __func__);

	if (buf->size == (ssize_t)size)
		return 0;
	if (!size)
		return -EINVAL;

	newmem = vmalloc(size);
	if (!newmem)
		return -ENOMEM;

	oldmem = buf->data;

	spin_lock_irq(&dmx->lock);
	buf->data = newmem;
	buf->size = (ssize_t)size;

	/* reset and not flush in case the buffer shrinks */
	dvb_ringbuffer_reset(buf);
	spin_unlock_irq(&dmx->lock);

	vfree(oldmem);
	return 0;
}


static unsigned int dvb_temi_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_temi_private *dmx = dvbdev->priv;
	struct dvb_temi *dmxpub = dmx->pub;

	unsigned int mask = 0;

	dprintk("function : %s\n", __func__);

	poll_wait(file, &dmxpub->temi_buffer.queue, wait);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (dmxpub->temi_buffer.error)
			mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

		if (!dvb_ringbuffer_empty(&dmxpub->temi_buffer))
			mask |= (POLLIN | POLLRDNORM | POLLPRI);
	} else
		mask |= (POLLOUT | POLLWRNORM | POLLPRI);

	return mask;
}


/* ******************************************************************************** */
/* temi IO interface functions */

/**
 * Real ioctl implementation.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 * @param cmd IOCTL command.
 * @param arg Associated argument.
 *
 * @return 0 on success, <0 on error.
 */
static int dvb_temi_io_do_ioctl(struct file *file,
				      unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_temi_private *dmx = dvbdev->priv;
	struct dvb_temi *dmxpub = dmx->pub;

	int ret = 0;
	unsigned long arg = (unsigned long)parg;

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;

	switch (cmd) {
		case DMX_START:
			{
				if(dmx->pub->start) {
					dvb_ringbuffer_flush(&dmxpub->temi_buffer);
					printk("[LDVB-TEMI] start\n");
					ret = dmx->pub->start(dmx->pub);	
				}
				else
					ret = -EIO; 	
		 
			}
			break;
		
		case DMX_STOP:
			{
				if(dmx->pub->stop) {
					dvb_ringbuffer_flush(&dmxpub->temi_buffer);
					printk("[LDVB-TEMI] stop\n");
					ret = dmx->pub->stop(dmx->pub); 
				}
				else
					ret = -EIO; 	
		 
			}
			break;
		
		case DMX_SET_BUFFER_SIZE:
			printk("[LDVB-TEMI] set buffer size : %d\n",(int)arg);
			ret = dvb_temi_set_buffer_size(dmx, arg);
			break;
		
		case DMX_SET_SOURCE:
			{	
				if(dmx->pub->set_source)
				{
					printk("[LDVB-TEMI] set_source:%d\n", *(int *)parg);
					ret = dmx->pub->set_source(dmx->pub, parg);
				}
				else
					ret = -EIO;
			}
			break;


		case DMX_GET_STATUS:		
			((struct dmx_status *)parg)->data = dvb_ringbuffer_avail(&dmxpub->temi_buffer);
			break;

		default:
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&dmx->mutex);
	return ret;
}


/**
 * Release a DVB temi interface device.
 *
 * @param ca_dev The dvb_device_t instance for the temi device.
 * @param dmx The associated dvb_temi instance.
 */
void dvb_temi_release(struct dvb_temi *pubdmx)
{
	struct dvb_temi_private *dmx = pubdmx->private;

	dprintk("%s\n", __func__);

	dvb_unregister_device(dmx->dvbdev);
	kfree(dmx);
	pubdmx->private = NULL;
}
EXPORT_SYMBOL(dvb_temi_release);


/**
 * Wrapper for ioctl implementation.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 * @param cmd IOCTL command.
 * @param arg Associated argument.
 *
 * @return 0 on success, <0 on error.
 */
static long dvb_temi_io_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_temi_io_do_ioctl);
}

static const struct file_operations dvb_temi_fops = {
	.owner = THIS_MODULE,
	.read = dvb_temi_read,
	.poll = dvb_temi_poll,
	.unlocked_ioctl = dvb_temi_io_ioctl,
	.open = dvb_temi_io_open,
	.release = dvb_temi_io_release,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_temi = {
	.priv = NULL,
	.users = ~0,
	.readers = 1,
	.writers = 1,
	.fops = &dvb_temi_fops,
};


/* ******************************************************************************** */
/* Initialisation/shutdown functions */


/**
 * Initialise a new DVB temi interface device.
 *
 * @param dvb_adapter DVB adapter to attach the new temi device to.
 * @param dmx The dvb_temi instance.
 * @param flags Flags describing the temi device (DVB_CA_FLAG_*).
 * @param slot_count Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_temi_core_debug(void);
#endif

int dvb_temi_init(struct dvb_adapter *dvb_adapter, struct dvb_temi *pubdmx, int flags)
{
	int ret;
	struct dvb_temi_private *dmx = NULL;

	#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("TEMI dvb core debug",
				19, t2ddebug_temi_core_debug, NULL);
	#endif
	
	dprintk("%s\n", __func__);

	/* initialise the system data */
	if ((dmx = kzalloc(sizeof(struct dvb_temi_private), GFP_KERNEL)) == NULL) {
		pr_err("[LDVB-TEMI] info: [Error] failed to kzalloc\n");
		ret = -ENOMEM;
		goto error;
	}
	dmx->pub = pubdmx;
	dmx->flags = (u32)flags;


	dmx->open = 0;
	pubdmx->private = dmx;

	/* register the DVB device */
	ret = dvb_register_device(dvb_adapter, &dmx->dvbdev, &dvbdev_temi, dmx, DVB_DEVICE_TEMI);
	if (ret) {
		pr_err("[LDVB-TEMI] info: [Error] failed to register DVB device\n");
		goto error;
	}

	dvb_ringbuffer_init(&pubdmx->temi_buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);

	pubdmx->temi_cb = dvb_temi_callback;

	mutex_init(&dmx->mutex);
	spin_lock_init(&dmx->lock);

	return 0;

error:
	if (dmx != NULL) {
		if (dmx->dvbdev != NULL)
			dvb_unregister_device(dmx->dvbdev);
		kfree(dmx);
	}
	pubdmx->private = NULL;
	return ret;
}
EXPORT_SYMBOL(dvb_temi_init);

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_temi_core_debug(void)
{
	long event;
	int val;
	const int ID_MAX = 99;

	PRINT_T2D("[%s]\n", __func__);
	while (1) {
                PRINT_T2D("\n");
                PRINT_T2D("================= temi ==================\n");			
                PRINT_T2D(" 1 ) System call Debug Messages(on/off)\n");
                PRINT_T2D("=======================================\n");
                PRINT_T2D("%d ) exit\n", ID_MAX);	
                PRINT_T2D(" => ");
                event = t2d_dbg_get_event_as_numeric(NULL, NULL);
                PRINT_T2D("\n");	
		if (event >= 0 && event < ID_MAX) {
			switch (event) {								
			case 1:
				PRINT_T2D("Turned on(1)or turned off(0)=>");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if(val == 1){
					PRINT_T2D("Enabling Debug print.\n");
					dvb_temi_debug = 1;
				}
				else if(val == 0){
                    PRINT_T2D("Disabling Debug print.\n");
				    dvb_temi_debug = 0;
				}
				break;	
			default:
				break;
			}
		} else {
			break;
		}
	}

	/* TODO: Validate return value from T2D_DBG */
	return 1;
}
#endif


