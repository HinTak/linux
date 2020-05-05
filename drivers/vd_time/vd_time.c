/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/init.h>
#include <linux/module.h>

#include <linux/rwsem.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <linux/errno.h>

/* Uncomment for debug print */
/* #define VD_VERBOSE */

#define VD_VALUES_NUM 4

#define VD_MAX_PROC_SIZE (VD_VALUES_NUM*20 + VD_VALUES_NUM)



struct vdtime_struct {
    /* offset from system time in milliseconds */
	long long	vd_msec_offset;
    /* timezone offset in minutes */
	int		vd_min_tz;
    /* dst offset in minutes */
	int		vd_min_dst;
    /* check sum for stored values */
	int		vd_chsum;
};

struct vdtime_dev_struct {
	struct vdtime_struct vdt;
    /* reader/writer semaphore*/
	struct rw_semaphore vdsem;
};

static struct vdtime_dev_struct	vdtime_dev;
static char vdtime_proc_data[VD_MAX_PROC_SIZE + 1];
static struct proc_dir_entry *vdtime_proc_entry;

/* Check checksum in vdtime structure */
static int _vdtime_check_chsum(struct vdtime_struct *vdt)
{
	size_t i;
	int chsum = 0;
	int *int_p_vd_msec_offset = (int *)(&vdt->vd_msec_offset);

	for (i = 0; i < sizeof(vdt->vd_msec_offset) / sizeof(int); i++)
	    chsum ^= int_p_vd_msec_offset[i];

	chsum ^= vdt->vd_min_tz;
	chsum ^= vdt->vd_min_dst;

    #ifdef VD_VERBOSE
	printk(KERN_ERR "expected chsum = %d\n", chsum);
	printk(KERN_ERR "got chsum = %d\n", vdt->vd_chsum);
    #endif

	return (vdt->vd_chsum == chsum);
}

int vdtime_read_proc(char *buf, char **start, off_t offset,
			int count, int *eof, void *data)
{
	int len;

	down_read(&vdtime_dev.vdsem);

    /* Read /proc/vd_time file: line with vdtime values separated with blanks */
	len = snprintf(buf,VD_MAX_PROC_SIZE, "%Ld %d %d %d\n",
	vdtime_dev.vdt.vd_msec_offset,
	vdtime_dev.vdt.vd_min_tz,
	vdtime_dev.vdt.vd_min_dst,
	vdtime_dev.vdt.vd_chsum);

    #ifdef VD_VERBOSE
	printk(KERN_ERR "read time offset = %llu msec\n",
		 vdtime_dev.vdt.vd_msec_offset);
	printk(KERN_ERR "read tz offset = %d min\n",
		 vdtime_dev.vdt.vd_min_tz);
	printk(KERN_ERR "read dst offset = %d min\n",
		 vdtime_dev.vdt.vd_min_dst);
    #endif

    /* If data length is smaller than what
       the application requested, mark End-Of-File */
	if (len <= count + offset)
	    *eof = 1;

    /* Correct start pointer and length for next read iteration */
	*start = buf + offset;
	len -= offset;
	if (len > count)
		 len = count;
	if (len < 0)
		 len = 0;

    #ifdef VD_VERBOSE
	printk(KERN_ERR "Read %d bytes\n", len);
    #endif

	up_read(&vdtime_dev.vdsem);

    /* Return number of bytes */
	return len;
}


int vdtime_write_proc(struct file *file, const char *buf,
			 unsigned long count, void *data)
{
	struct vdtime_struct user_vdt;
	int num; /* Number of integer values */
	int chsum_match;

	

    /* The /proc/file length is limited with VD_MAX_PROC_SIZE value */
	if (count > VD_MAX_PROC_SIZE) {
	    printk(KERN_ERR "Error writing to /proc/vd_time:\n");
	    printk(KERN_ERR "too long line, %d-byte length is permited\n",
		     VD_MAX_PROC_SIZE);
	    return -EINVAL;
	}
	down_write(&vdtime_dev.vdsem);
    /* Copy data from user space */
	if (copy_from_user(vdtime_proc_data, buf, count)) {
	    up_write(&vdtime_dev.vdsem);
	    return -EFAULT;
        }

	vdtime_proc_data[count] = '\0';

    /* Parse user data */
	num = sscanf(vdtime_proc_data, "%Ld %d %d %d",
	    &user_vdt.vd_msec_offset, &user_vdt.vd_min_tz,
	    &user_vdt.vd_min_dst, &user_vdt.vd_chsum);

    /* Check if user data was correct */
	if (num != VD_VALUES_NUM) {
	    printk(KERN_ERR "Error writing to /proc/vd_time:\n");
	    printk(KERN_ERR
	     "should take %d(got %d) decimal integers separated with spaces\n",
	     VD_VALUES_NUM, num);
	    up_write(&vdtime_dev.vdsem);
	    return -EINVAL;
	}

    #ifdef VD_VERBOSE
	printk(KERN_ERR "write time offset = %lld msec\n",
		 user_vdt.vd_msec_offset);
	printk(KERN_ERR "write tz offset = %d min\n",
		 user_vdt.vd_min_tz);
	printk(KERN_ERR "write dst offset = %d min\n",
		 user_vdt.vd_min_dst);
    #endif


    /* Check checksum here */
	chsum_match = _vdtime_check_chsum(&user_vdt);
	if (!chsum_match) {
	    printk(KERN_ERR "Error writing to /proc/vd_time:\n");
	    printk(KERN_ERR "checksum mismatch\n");
	    up_write(&vdtime_dev.vdsem);
	    return -EINVAL;
	}

    /* If all success set vdtime_dev values */
	vdtime_dev.vdt.vd_msec_offset = user_vdt.vd_msec_offset;
	vdtime_dev.vdt.vd_min_tz = user_vdt.vd_min_tz;
	vdtime_dev.vdt.vd_min_dst = user_vdt.vd_min_dst;
	vdtime_dev.vdt.vd_chsum = user_vdt.vd_chsum;

    #ifdef VD_VERBOSE
	printk(KERN_ERR "Write %d words\n", num);
    #endif

	up_write(&vdtime_dev.vdsem);

    /* Return number of bytes */
	return count;
}

static int __init vdtime_init(void)
{
    /* Init vdtime_device structure */
	init_rwsem(&vdtime_dev.vdsem);
	vdtime_dev.vdt.vd_msec_offset = 0;
	vdtime_dev.vdt.vd_min_tz = 0;
	vdtime_dev.vdt.vd_min_dst = 0;
	vdtime_dev.vdt.vd_chsum = 0;

    /* Create a proc entry */
	vdtime_proc_entry = create_proc_entry("vd_time", 0666, NULL);
	if (!vdtime_proc_entry) {
	    printk(KERN_ERR "Error creating vd_time proc entry");
	    return -ENOMEM;
	}
	vdtime_proc_entry->read_proc = vdtime_read_proc;
	vdtime_proc_entry->write_proc = vdtime_write_proc;

	return 0;
}
static void __exit vdtime_exit(void)
{
	remove_proc_entry("vd_time", NULL);
}

module_init(vdtime_init);
module_exit(vdtime_exit);

MODULE_LICENSE("GPL");

