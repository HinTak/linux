/*
 * linux/fs/fs-readahead.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Ajeet Kr Yadav <ajeet.y@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Implemention of selective readahead functionality to selectively
 * enable/disable the readahead per device per partition via
 * entries in "/proc/fs/readahead".
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "proc/internal.h"

#define PROC_RA_DIR_NAME "fs/readahead"
#define PROC_FILE_SIZE (2*(sizeof(char)))

#define ASCII_TO_DIGIT(a)	((a) - '0')	/* Only vaild for '0' to '9' */
#define DIGIT_TO_ASCII(d)	((d) + '0')	/* Only vaild for 0 to 9 */

struct ra_proc_file_entry {
	struct list_head list;	/* list for this structure */
	struct proc_dir_entry *proc;	/* device readahead proc file */
	const char *name;	/* alias to proc->name */
	bool state;	/* readahead setting of device */
};

/**
 * These are structure to hold information about
 * "/proc/fs/readahead" directory and files in it
 */
DEFINE_MUTEX(readahead_lock);
LIST_HEAD(ra_list_head);
static struct proc_dir_entry *readahead_dir;

/**
 * This function finds & return the device entry in
 * ra_list_head if it exists otherwise NULL.
 * @param name: device name string, ex: "sda1"
 * @param add: add value to reference count
 * @return on success pointer to node, else NULL.
 * readahead_lock must be held.
 */
static inline
struct ra_proc_file_entry *find_readahead_entry(const char *name)
{
	struct ra_proc_file_entry *ent;

	if (!readahead_dir) {
		pr_debug("Warning: Not found /proc/%s\n",
				PROC_RA_DIR_NAME);
		return NULL;
	}
	/* Search list, return the node with same name */
	list_for_each_entry(ent, &ra_list_head, list) {
		if (!strcmp(name, ent->name))
			return ent;
	}
	/* No match found */
	return NULL;
}

/**
 * This function is called then the /proc file is read
 * @param filp		file pointer
 * @param buffer	user space buffer pointer
 * @param count		number of bytes to read
 * @param offset	file pointer position
 * @return On success no of bytes read, else error value
 */
ssize_t read_readahead_proc(struct file *filp, char __user *buffer,
			    size_t count, loff_t *offset)
{
	struct inode *inode = file_inode(filp);
	struct proc_dir_entry *dp = PDE(inode);
	char val = 0;

	if (!buffer)
		return -EPERM;
	if (*offset)
		return 0;

	mutex_lock(&readahead_lock);
	if (!dp->data) {
		mutex_unlock(&readahead_lock);
		return -EPERM;
	}
	/* Copy data to buffer, return the read data size */
	val = ((struct ra_proc_file_entry *)dp->data)->state;
	*buffer = (char) DIGIT_TO_ASCII(val);
	*(buffer + 1) = '\0';
	*offset = PROC_FILE_SIZE;
	mutex_unlock(&readahead_lock);

	return PROC_FILE_SIZE;
}

/**
 * This function is called with the /proc file is written
 * @param filp          file pointer
 * @param buffer        user space buffer pointer
 * @param count         number of bytes to write
 * @param offset        file pointer position
 * @return On success number of bytes written, else error value
 */
ssize_t write_readahead_proc(struct file *filp, const char __user *buffer,
					size_t count, loff_t *offset)
{
	struct inode *inode = file_inode(filp);
	struct proc_dir_entry *dp = PDE(inode);
	char val = 0;

	if (!buffer)
		return -EPERM;

	if (!count || (count > PROC_FILE_SIZE)) {
		/* Trying to write more data than allowed */
		return -EPERM;
	}

	/* Copy data from user buffer */
	if (copy_from_user(&val, buffer, sizeof(char)))
		return -EFAULT;

	/* Identify the option 0/1 */
	if (val < '0' || val > '1')
		return -EPERM;

	mutex_lock(&readahead_lock);
	if (!dp->data) {
		mutex_unlock(&readahead_lock);
		return -EPERM;
	}
	((struct ra_proc_file_entry *)dp->data)->state = ASCII_TO_DIGIT(val);
	mutex_unlock(&readahead_lock);
	return count;
}

static const struct file_operations readahead_fops = {
	.write          = write_readahead_proc,
	.read           = read_readahead_proc,
};

/**
 * This function creates an instance of proc file in
 * "/proc/fs/readahead" directory. If device name is
 * valid than it checks whether it already had an entry
 * corresponding to this, if not it creates the new entry.
 * @param dev_name	device name string
 * @return on success pointer to state, else NULL
 */
bool create_readahead_proc(const char *dev_name)
{
	struct proc_dir_entry *proc_file = NULL;
	struct ra_proc_file_entry *proc_ent = NULL;
	bool state = false;

	if (!dev_name)
		return state;

	/* Check wheather "/proc/fs/readahead/xxx" exists */
	mutex_lock(&readahead_lock);
	proc_ent = find_readahead_entry(dev_name);
	if (!proc_ent) {
		/* New device mounted, create ra_proc_file_entry entry */
		proc_ent = kmalloc(sizeof(struct ra_proc_file_entry),
				   GFP_KERNEL);
		if (!proc_ent)
			goto err;

		/* 1st instance, readahead enabled (default) */
		proc_ent->state = true;

		/* Create the proc_dir_entry for new device */
		proc_file = proc_create(dev_name, 0644, readahead_dir,
					&readahead_fops);
		if (!proc_file) {
			kfree(proc_ent);
			pr_warn("Warning: Cannot create /proc/%s/%s\n",
					PROC_RA_DIR_NAME, dev_name);
			goto err;
		}

		/* Initialise proc_dir_entry structure */
		proc_file->mode = S_IFREG | S_IRUGO;
		proc_file->uid = GLOBAL_ROOT_UID;
		proc_file->gid = GLOBAL_ROOT_GID;
		proc_file->data = proc_ent;

		/* Fill ra_proc_file_entry entries */
		proc_ent->name = proc_file->name;
		proc_ent->proc = proc_file;

		/* Add new ra_proc_file_entry to list */
		list_add(&proc_ent->list, &ra_list_head);
	}
	state = proc_ent->state;
err:
	mutex_unlock(&readahead_lock);
	pr_debug("\n##### Create readahead proc: %s\n", dev_name);
	return state;
}
EXPORT_SYMBOL_GPL(create_readahead_proc);

/**
 * This function removes an instance of proc file from
 * "/proc/fs/readahead" directory. First it finds the
 * entry corresponding to this device, once found it
 * first decreases it ref count, if it zero than it safe to remove
 * the entry
 * @param dev_name	device name string
 * @return void
 */
void
remove_readahead_proc(const char *dev_name)
{
	struct ra_proc_file_entry *proc_ent = NULL;

	if (!dev_name)
		return;
	mutex_lock(&readahead_lock);
	/* Remove the proc directory */
	proc_ent = find_readahead_entry(dev_name);
	if (proc_ent) {
		remove_proc_entry(dev_name, readahead_dir);
		list_del(&proc_ent->list);
		kfree(proc_ent);
	}
	mutex_unlock(&readahead_lock);

	pr_debug("\n##### Remove readahead proc: %s\n", dev_name);
}
EXPORT_SYMBOL_GPL(remove_readahead_proc);


/**
 * This function finds & return the readahead state
 * of the given device if found, else NULL.
 * @param name: device name string, ex: "sda1"
 * @return on success pointer to state, else NULL
 */
bool
get_readahead_entry(const char *name)
{
	struct ra_proc_file_entry *ent = NULL;
	bool state = true; /* by default readahead is enabled */

	mutex_lock(&readahead_lock);
	ent = find_readahead_entry(name);
	if (ent)
		state = ent->state;
	mutex_unlock(&readahead_lock);

	return state;
}
EXPORT_SYMBOL_GPL(get_readahead_entry);

/**
 * This function is called when the module is loaded.
 * It creates "/proc/fs/readahead" directory.
 * @param void
 * @return On success 0, else error value
 */
void
readahead_init(void)
{
	/* Create the "/proc/fs/readahead" directory */
	readahead_dir = proc_mkdir(PROC_RA_DIR_NAME, NULL);
	if (!readahead_dir)
		pr_warn("Warning: Cannot create /proc/%s\n", PROC_RA_DIR_NAME);
}
