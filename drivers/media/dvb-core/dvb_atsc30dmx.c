/*
 * dvb_atsc30dmx.c: generic DVB functions for ATSC3.0 demux interfaces
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

#include "dvb_atsc30dmx.h"

#include <t2ddebugd/t2ddebugd.h>
#include <linux/t2d_print.h>

static int dvb_atsc30dmx_debug;

module_param_named(atsc30_debug, dvb_atsc30dmx_debug, int, 0644);
MODULE_PARM_DESC(atsc30_debug, "enable verbose debug messages");

#define dprintk if (dvb_atsc30dmx_debug) printk

#define DEFAULT_DMX_BUFFER_SIZE	(128*1024)

#define IP_BUFFER_SIZE (8*1024*1024)

#define LMT_BUFFER_SIZE (1024*1024)



/* Private atsc30dmx-interface information */
struct dvb_atsc30dmx_private {

	/* pointer back to the public data structure */
	struct dvb_atsc30dmx *pub;

	/* the DVB device */
	struct dvb_device *dvbdev;

	struct dvb_device *lmt_dvbdev;

	/* Flags describing the interface (DVB_CA_FLAG_*) */
	u32 flags;

	/* Flag indicating if the ATSC30DMX device is open */
	unsigned int open;

	/* mutex serializing ioctls */
	struct mutex mutex;

	/* mutex for ip-packet buffer write */
	struct mutex mutex_ip;

	/* mutex for lmt buffer write */
	struct mutex mutex_lmt;

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
static int dvb_atsc30dmx_io_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	
	int err;
	void *mem;

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;
	
	if (!dvbdev->readers) {
		mutex_unlock(&dmx->mutex);
		return -EBUSY;
	}
	mem = vmalloc((unsigned long)IP_BUFFER_SIZE);
	if (!mem) {
		mutex_unlock(&dmx->mutex);
		return -ENOMEM;
	}
	

	dvb_ringbuffer_init(&dmxpub->ip_buffer, mem, IP_BUFFER_SIZE);


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
 * Implementation of file open syscall.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_atsc30lmt_io_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	
	int err;
	void *mem;

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;
	
	if (!dvbdev->readers) {
		mutex_unlock(&dmx->mutex);
		return -EBUSY;
	}
	mem = vmalloc((unsigned long)LMT_BUFFER_SIZE);
	if (!mem) {
		mutex_unlock(&dmx->mutex);
		return -ENOMEM;
	}	

	dvb_ringbuffer_init(&dmxpub->lmt_buffer, mem, LMT_BUFFER_SIZE);


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
static int dvb_atsc30dmx_io_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	int err;

	mutex_lock(&dmx->mutex);

	dprintk("%s\n", __func__);

	/* mark the ATSC30DMX device as closed */
	dvbdev->readers++;
	dvbdev->users--;
	if (dmxpub->ip_buffer.data) {
		void *mem = dmxpub->ip_buffer.data;
		mb();
		spin_lock_irq(&dmx->lock);
		dmxpub->ip_buffer.data = NULL;
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
EXPORT_SYMBOL(dvb_atsc30dmx_release);


/**
 * Implementation of file close syscall.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_atsc30lmt_io_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;

	int err;

	mutex_lock(&dmx->mutex);

	dprintk("%s\n", __func__);

	/* mark the atsc30lmt device as closed */
	dvbdev->readers++;
	dvbdev->users--;
	if (dmxpub->lmt_buffer.data) {
		void *mem = dmxpub->lmt_buffer.data;
		mb();
		spin_lock_irq(&dmx->lock);
		dmxpub->lmt_buffer.data = NULL;
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

static int dvb_atsc30dmx_buffer_write(struct dvb_ringbuffer *buf,
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

static ssize_t dvb_atsc30dmx_buffer_read(struct dvb_ringbuffer *src,
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

		ret = wait_event_interruptible(src->queue, !dvb_ringbuffer_empty(src));
		
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




static int dvb_atsc30dmx_callback(struct dvb_atsc30dmx *dmxpub, const u8 *buffer1, size_t buffer1_len, const u8 *buffer2, size_t buffer2_len)
{
	int ret;
	struct dvb_atsc30dmx_private *dmx = dmxpub->private;
	struct dvb_ringbuffer *buffer = &dmxpub->ip_buffer;

	mutex_lock(&dmx->mutex_ip);
		
	spin_lock(&dmx->lock);						

	if (buffer->error) 
	{
		spin_unlock(&dmx->lock);
		wake_up(&buffer->queue);
		mutex_unlock(&dmx->mutex_ip);
		return 0;
	}

	ret = dvb_atsc30dmx_buffer_write(buffer, buffer1, buffer1_len);
	if (ret == (int)buffer1_len) 
	{
		ret = dvb_atsc30dmx_buffer_write(buffer, buffer2, buffer2_len);
	}

	
	if (ret < 0)
		buffer->error = ret;
	spin_unlock(&dmx->lock);
	wake_up(&buffer->queue);
	mutex_unlock(&dmx->mutex_ip);
	return 0;
}



static int dvb_atsc30lmt_callback(struct dvb_atsc30dmx *dmxpub, const u8 *buffer1, size_t buffer1_len, const u8 *buffer2, size_t buffer2_len)
{
	int ret;

	struct dvb_atsc30dmx_private *dmx = dmxpub->private;
	struct dvb_ringbuffer *buffer = &dmxpub->lmt_buffer;

	mutex_lock(&dmx->mutex_lmt);

	spin_lock(&dmx->lock);

	if (buffer->error) 
	{
		spin_unlock(&dmx->lock);		
		wake_up(&buffer->queue);
		mutex_unlock(&dmx->mutex_lmt);
		return 0;
	}
		
	ret = dvb_atsc30dmx_buffer_write(buffer, buffer1, buffer1_len);
	if (ret == (int)buffer1_len) 
	{
		ret = dvb_atsc30dmx_buffer_write(buffer, buffer2, buffer2_len);
	}
	
	
	
	if (ret < 0)
		buffer->error = ret;
	spin_unlock(&dmx->lock);
	wake_up(&buffer->queue);
	
	mutex_unlock(&dmx->mutex_lmt);

	return 0;
}


static ssize_t dvb_atsc30dmx_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;

	int ret;
	ret = dvb_atsc30dmx_buffer_read(&dmxpub->ip_buffer,
			      file->f_flags & O_NONBLOCK, buf, count, ppos);
	
	return ret;
}


static ssize_t dvb_atsc30lmt_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	
	int ret;
	ret = dvb_atsc30dmx_buffer_read(&dmxpub->lmt_buffer,
			      file->f_flags & O_NONBLOCK, buf, count, ppos);

	return ret;
}


static int dvb_atsc30dmx_set_buffer_size(struct dvb_atsc30dmx_private *dmx,
				      unsigned long size)
{
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	struct dvb_ringbuffer *buf = &dmxpub->ip_buffer;
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

static int dvb_atsc30lmt_set_buffer_size(struct dvb_atsc30dmx_private *dmx,
				      unsigned long size)
{
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	struct dvb_ringbuffer *buf = &dmxpub->lmt_buffer;
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


static unsigned int dvb_atsc30dmx_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;

	unsigned int mask = 0;

	dprintk("function : %s\n", __func__);

	poll_wait(file, &dmxpub->ip_buffer.queue, wait);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (dmxpub->ip_buffer.error)
			mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

		if (!dvb_ringbuffer_empty(&dmxpub->ip_buffer))
			mask |= (POLLIN | POLLRDNORM | POLLPRI);
	} else
		mask |= (POLLOUT | POLLWRNORM | POLLPRI);

	return mask;
}

static unsigned int dvb_atsc30lmt_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;

	unsigned int mask = 0;

	dprintk("function : %s\n", __func__);

	poll_wait(file, &dmxpub->lmt_buffer.queue, wait);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (dmxpub->lmt_buffer.error)
			mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

		if (!dvb_ringbuffer_empty(&dmxpub->lmt_buffer))
			mask |= (POLLIN | POLLRDNORM | POLLPRI);
	} else
		mask |= (POLLOUT | POLLWRNORM | POLLPRI);

	return mask;
}


/* ******************************************************************************** */
/* ATSC30DMX IO interface functions */

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
static int dvb_atsc30dmx_io_do_ioctl(struct file *file,
				      unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;
	int ret = 0;
	unsigned long arg = (unsigned long)parg;

//	dprintk("%s\n", __func__);

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;

	switch (cmd) {

	case DMX_START:
		{
			if(dmx->pub->start) {
				dvb_ringbuffer_flush(&dmxpub->ip_buffer);
				printk("[LDVB-ATSC30] start\n");
				ret = dmx->pub->start(dmx->pub);	
			}
			else
				ret = -EIO;		
     
	    }
		break;

	case DMX_STOP:
		{
			if(dmx->pub->stop) {
				dvb_ringbuffer_flush(&dmxpub->ip_buffer);
				printk("[LDVB-ATSC30] stop\n");
				ret = dmx->pub->stop(dmx->pub);	
			}
			else
				ret = -EIO;		
     
	    }
		break;

	case DMX_SET_BUFFER_SIZE:
		printk("[LDVB-ATSC30] set buffer size : %d\n",(int)arg);
		ret = dvb_atsc30dmx_set_buffer_size(dmx, arg);
		break;

	case DMX_SET_SOURCE:
		{	
			if(dmx->pub->set_source)
			{
				printk("[LDVB-ATSC30] set_source:%d\n", *(int *)parg);
				ret = dmx->pub->set_source(dmx->pub, parg);
			}
			else
				ret = -EIO;
		}
		break;

	case DMX_GET_STATUS:		
		mutex_lock(&dmx->mutex_ip);
		((struct dmx_status *)parg)->data = dvb_ringbuffer_avail(&dmxpub->ip_buffer);
		mutex_unlock(&dmx->mutex_ip);
		break;

		
	case DMX_GET_ALP_STATUS:
		if(dmx->pub->get_status)
		{
			dmx->pub->get_status(dmx->pub, (struct dmx_status *)arg);
		}
		else
		{
			ret = -EIO;
		}
		break;

	case DMX_SET_PLP_MODE:
		{	
			printk("[LDVB-ATSC30] set plp mode : %d\n", *(int *)parg);
			
			if(dmx->pub->set_plp_mode)
			{				
				ret = dmx->pub->set_plp_mode(dmx->pub, parg);
			}
			else
			{
				printk("set_plp_mode function null\n");
				ret = -EIO;
			}
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dmx->mutex);
	return ret;
}


/* ******************************************************************************** */
/* atsc30lmt IO interface functions */

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
static int dvb_atsc30lmt_io_do_ioctl(struct file *file,
				      unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_atsc30dmx_private *dmx = dvbdev->priv;
	struct dvb_atsc30dmx *dmxpub = dmx->pub;

	int ret = 0;
	unsigned long arg = (unsigned long)parg;

	if (mutex_lock_interruptible(&dmx->mutex))
		return -ERESTARTSYS;

	switch (cmd) {

	case DMX_SET_BUFFER_SIZE:
		printk("[LDVB-ATSC30-LMT] set buffer size : %d\n", (int)arg);
		ret = dvb_atsc30lmt_set_buffer_size(dmx, arg);
		break;

	case DMX_GET_STATUS:		
		mutex_lock(&dmx->mutex_lmt);
		((struct dmx_status *)parg)->data = dvb_ringbuffer_avail(&dmxpub->lmt_buffer);
		mutex_unlock(&dmx->mutex_lmt);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dmx->mutex);
	return ret;
}


/**
 * Release a DVB ATSC30DMX interface device.
 *
 * @param ca_dev The dvb_device_t instance for the ATSC30DMX device.
 * @param dmx The associated dvb_atsc30dmx instance.
 */
void dvb_atsc30dmx_release(struct dvb_atsc30dmx *pubdmx)
{
	struct dvb_atsc30dmx_private *dmx = pubdmx->private;

	dprintk("%s\n", __func__);

	dvb_unregister_device(dmx->dvbdev);
	dvb_unregister_device(dmx->lmt_dvbdev);
	kfree(dmx);
	pubdmx->private = NULL;
}



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
static long dvb_atsc30dmx_io_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_atsc30dmx_io_do_ioctl);
}


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
static long dvb_atsc30lmt_io_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_atsc30lmt_io_do_ioctl);
}


static const struct file_operations dvb_atsc30dmx_fops = {
	.owner = THIS_MODULE,
	.read = dvb_atsc30dmx_read,
	.poll = dvb_atsc30dmx_poll,
	.unlocked_ioctl = dvb_atsc30dmx_io_ioctl,
	.open = dvb_atsc30dmx_io_open,
	.release = dvb_atsc30dmx_io_release,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_atsc30dmx = {
	.priv = NULL,
	.users = ~0,
	.readers = 1,
	.writers = 1,
	.fops = &dvb_atsc30dmx_fops,
};


static const struct file_operations dvb_atsc30lmt_fops = {
	.owner = THIS_MODULE,
	.read = dvb_atsc30lmt_read,
	.poll = dvb_atsc30lmt_poll,
	.unlocked_ioctl = dvb_atsc30lmt_io_ioctl,
	.open = dvb_atsc30lmt_io_open,
	.release = dvb_atsc30lmt_io_release,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_atsc30lmt = {
	.priv = NULL,
	.users = ~0,
	.readers = 1,
	.writers = 1,
	.fops = &dvb_atsc30lmt_fops,
};



/* ******************************************************************************** */
/* Initialisation/shutdown functions */


/**
 * Initialise a new DVB ATSC30DMX interface device.
 *
 * @param dvb_adapter DVB adapter to attach the new ATSC30DMX device to.
 * @param dmx The dvb_atsc30dmx instance.
 * @param flags Flags describing the ATSC30DMX device (DVB_CA_FLAG_*).
 * @param slot_count Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_atsc30dmx_core_debug(void);
#endif

int dvb_atsc30dmx_init(struct dvb_adapter *dvb_adapter, struct dvb_atsc30dmx *pubdmx, int flags)
{
	int ret;
	struct dvb_atsc30dmx_private *dmx = NULL;

	#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("ATSC30 dvb core debug",
				18, t2ddebug_atsc30dmx_core_debug, NULL);
	#endif
	
	dprintk("%s\n", __func__);

	/* initialise the system data */
	if ((dmx = kzalloc(sizeof(struct dvb_atsc30dmx_private), GFP_KERNEL)) == NULL) {
		pr_err("[LDVB-ATSC30DMX] info: [Error] failed to kzalloc\n");
		ret = -ENOMEM;
		goto error;
	}
	dmx->pub = pubdmx;
	dmx->flags = (u32)flags;


	dmx->open = 0;
	pubdmx->private = dmx;

	/* register the DVB device */
	ret = dvb_register_device(dvb_adapter, &dmx->dvbdev, &dvbdev_atsc30dmx, dmx, DVB_DEVICE_ATSC30);
	if (ret) {
		pr_err("[LDVB-ATSC30DMX] info: [Error] failed to register DVB device\n");
		goto error;
	}

	dvb_ringbuffer_init(&pubdmx->ip_buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);

	pubdmx->atsc30dmx_cb = dvb_atsc30dmx_callback;

	ret = dvb_register_device(dvb_adapter, &dmx->lmt_dvbdev, &dvbdev_atsc30lmt, dmx, DVB_DEVICE_LMT);
	if (ret) {
		pr_err("[LDVB-ATSC30DMX] info: [Error] failed to register LMT device\n");
		goto error;
	}

	dvb_ringbuffer_init(&pubdmx->lmt_buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);

	pubdmx->atsc30lmt_cb = dvb_atsc30lmt_callback;


	mutex_init(&dmx->mutex);
	mutex_init(&dmx->mutex_ip);
	mutex_init(&dmx->mutex_lmt);
	
	spin_lock_init(&dmx->lock);

	return 0;

error:
	if (dmx != NULL) {
		if (dmx->dvbdev != NULL)
			dvb_unregister_device(dmx->dvbdev);
		if (dmx->lmt_dvbdev != NULL)
					dvb_unregister_device(dmx->lmt_dvbdev);

		kfree(dmx);
	}
	pubdmx->private = NULL;
	return ret;
}
EXPORT_SYMBOL(dvb_atsc30dmx_init);

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_atsc30dmx_core_debug(void)
{
	long event;
	int val;
	const int ID_MAX = 99;

	PRINT_T2D("[%s]\n", __func__);
	while (1) {
                PRINT_T2D("\n");
                PRINT_T2D("================= ATSC30DMX ==================\n");			
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
					dvb_atsc30dmx_debug = 1;
				}
				else if(val == 0){
                    PRINT_T2D("Disabling Debug print.\n");
				    dvb_atsc30dmx_debug = 0;
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


