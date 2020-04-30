/*
 * dvb_ca.c: generic DVB functions for EN50221 CAM interfaces
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

#include "dvb_hcas.h"

#include <t2ddebugd/t2ddebugd.h>
#include <linux/t2d_print.h>

static int dvb_hcas_debug;

module_param_named(hcas_debug, dvb_hcas_debug, int, 0644);
MODULE_PARM_DESC(hcas_debug, "enable verbose debug messages");

#define dprintk if (dvb_hcas_debug) printk

#define DVB_HCAS_SLOTSTATE_NONE	0

/* Information on a CA slot */
struct dvb_hcas_slot {

	/* current state of the HCAS */
	int slot_state;

	/* mutex used for serializing access to one CI slot */
	struct mutex slot_lock;
};

/* Private CA-interface information */
struct dvb_hcas_private {

	/* pointer back to the public data structure */
	struct dvb_hcas *pub;

	/* the DVB device */
	struct dvb_device *dvbdev;

	/* Flags describing the interface (DVB_CA_FLAG_*) */
	u32 flags;

	/* information on each slot */
	struct dvb_hcas_slot *slot_info;

	/* Flag indicating if the CA device is open */
	unsigned int open;

	/* mutex serializing ioctls */
	struct mutex ioctl_mutex;
};


/* ******************************************************************************** */
/* EN50221 IO interface functions */

/**
 * Real ioctl implementation.
 * NOTE: CA_SEND_MSG/CA_GET_MSG ioctls have userspace buffers passed to them.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 * @param cmd IOCTL command.
 * @param arg Associated argument.
 *
 * @return 0 on success, <0 on error.
 */
static int dvb_hcas_io_do_ioctl(struct file *file,
				      unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_hcas_private *ca = dvbdev->priv;
	int err = 0;

//	dprintk("%s\n", __func__);

	if (mutex_lock_interruptible(&ca->ioctl_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
		
	case HCAS_SET_STREAMID:
		{
	        ca_set_hcas_t *tag = (ca_set_hcas_t *)parg;

			if(ca->pub->hcas_set_streamid) {
				ca->pub->hcas_set_streamid(ca->pub, tag);	
			}
			else
				err = -EIO;		
     
	    }
		break;

	case HCAS_CLEAR_STREAMID:
		{
	        ca_set_hcas_t *tag = (ca_set_hcas_t *)parg;

			if(ca->pub->hcas_clear_streamid) {
				ca->pub->hcas_clear_streamid(ca->pub, tag);	
			}
			else
				err = -EIO;		
     
	    }
		break;

	case HCAS_CLEAR_ALL:
		{
	        ca_set_frontend_t *source = (ca_set_frontend_t *)parg;

			if(ca->pub->hcas_clear_all) {
				ca->pub->hcas_clear_all(ca->pub, source);	
			}
			else
				err = -EIO;		
     
	    }
		break;

	case HCAS_START:
		{
	        ca_set_frontend_t *source = (ca_set_frontend_t *)parg;

			if(ca->pub->hcas_start) {
				ca->pub->hcas_start(ca->pub, source);	
			}
			else
				err = -EIO;		
     
	    }
		break;

	case HCAS_STOP:
		{
	        ca_set_frontend_t *source = (ca_set_frontend_t *)parg;

			if(ca->pub->hcas_stop) {
				ca->pub->hcas_stop(ca->pub, source);	
			}
			else
				err = -EIO;		
     
	    }
		break;

	case HCAS_GET_SLOT_INFO:
		break;
		

	default:
		err = -EINVAL;
		break;
	}

	mutex_unlock(&ca->ioctl_mutex);
	return err;
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
static long dvb_hcas_io_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_hcas_io_do_ioctl);
}

/**
 * Implementation of file open syscall.
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_hcas_io_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_hcas_private *ca = dvbdev->priv;
	int err;

	dprintk("%s\n", __func__);

	if (!try_module_get(ca->pub->owner))
		return -EIO;

	err = dvb_generic_open(inode, file);
	if (err < 0) {
		module_put(ca->pub->owner);
		return err;
	}

	ca->open++ ;

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
static int dvb_hcas_io_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_hcas_private *ca = dvbdev->priv;
	int err;

	dprintk("%s\n", __func__);

	/* mark the CA device as closed */

	if(ca->open)
	{
		ca->open--;
	}

	err = dvb_generic_release(inode, file);

	module_put(ca->pub->owner);

	return err;
}
EXPORT_SYMBOL(dvb_hcas_release);


static const struct file_operations dvb_hcas_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dvb_hcas_io_ioctl,
	.open = dvb_hcas_io_open,
	.release = dvb_hcas_io_release,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_hcas = {
	.priv = NULL,
	.users = ~0,
	.readers = (~0)-1,
	.writers = 1,
	.fops = &dvb_hcas_fops,
};


/* ******************************************************************************** */
/* Initialisation/shutdown functions */


/**
 * Initialise a new DVB CA EN50221 interface device.
 *
 * @param dvb_adapter DVB adapter to attach the new CA device to.
 * @param ca The dvb_ca instance.
 * @param flags Flags describing the CA device (DVB_CA_FLAG_*).
 * @param slot_count Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_hcas_core_debug(void);
#endif

int dvb_hcas_init(struct dvb_adapter *dvb_adapter, struct dvb_hcas *pubca, int flags)
{
	int ret;
	struct dvb_hcas_private *ca = NULL;

	#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("HCAS dvb core debug",
				17, t2ddebug_hcas_core_debug, NULL);
	#endif
	
	dprintk("%s\n", __func__);

	/* initialise the system data */
	if ((ca = kzalloc(sizeof(struct dvb_hcas_private), GFP_KERNEL)) == NULL) {
		pr_err("[LDVB-HCAS] info: [Error] failed to kzalloc\n");
		ret = -ENOMEM;
		goto error;
	}
	ca->pub = pubca;
	ca->flags = (u32)flags;

	if ((ca->slot_info = kcalloc(1, sizeof(struct dvb_hcas_slot), GFP_KERNEL)) == NULL) {
		pr_err("[LDVB-HCAS] info: [Error] failed to kcalloc\n");
		ret = -ENOMEM;
		goto error;
	}

	ca->open = 0;
	pubca->private = ca;

	/* register the DVB device */
	ret = dvb_register_device(dvb_adapter, &ca->dvbdev, &dvbdev_hcas, ca, DVB_DEVICE_HCAS);
	if (ret) {
		pr_err("[LDVB-HCAS] info: [Error] failed to register DVB device\n");
		goto error;
	}


	memset(ca->slot_info, 0, sizeof(struct dvb_hcas_slot));
	ca->slot_info->slot_state = DVB_HCAS_SLOTSTATE_NONE;
	mutex_init(&ca->slot_info->slot_lock);
	mutex_init(&ca->ioctl_mutex);
	
	return 0;

error:
	if (ca != NULL) {
		if (ca->dvbdev != NULL)
			dvb_unregister_device(ca->dvbdev);
		kfree(ca->slot_info);
		kfree(ca);
	}
	pubca->private = NULL;
	return ret;
}
EXPORT_SYMBOL(dvb_hcas_init);



#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_hcas_core_debug(void)
{
	long event;
	int val;
	const int ID_MAX = 99;

	PRINT_T2D("[%s]\n", __func__);
	while (1) {
                PRINT_T2D("\n");
                PRINT_T2D("================= HCAS ==================\n");			
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
					dvb_hcas_debug = 1;
				}
				else if(val == 0){
                    PRINT_T2D("Disabling Debug print.\n");
				    dvb_hcas_debug = 0;
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


/**
 * Release a DVB CA EN50221 interface device.
 *
 * @param ca_dev The dvb_device_t instance for the CA device.
 * @param ca The associated dvb_ca instance.
 */
void dvb_hcas_release(struct dvb_hcas *pubca)
{
	struct dvb_hcas_private *ca = pubca->private;

	dprintk("%s\n", __func__);

	kfree(ca->slot_info);
	dvb_unregister_device(ca->dvbdev);
	kfree(ca);
	pubca->private = NULL;
}
