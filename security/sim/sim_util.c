/**
 * @file sim_util.c
 * 
 * @brief sim util
 * 
 * @author Jinbum Park (jinb.park@samsung.com)
 * 
 * @note	Copyright (c) 2016 Samsung Electronics Co., Ltd All Rights Reserved 
 * PROPRIETARY/CONFIDENTIAL
 * This software is the confidential and proprietary information of
 * Samsung Electronics CO.LTD.,. ("Confidential Information").
 * You shall not disclose such Confidential Information and shall
 * use it only in accordance with the terms of the license agreement
 * you entered into with Samsung Electronics CO.LTD.,.
 *
 * SAMSUNG make no representations or warranties about the suitability of the software,
 * either express or implied, including but not limited to the implied warranties of merchantability,
 * fitness for a particular purpose, or non-infringement. 
 * SAMSUNG shall not be liable for any damages suffered by licensee as a result of using, 
 * modifying or distributing this software or its derivatives.
 *
 */

#include <linux/mnt_namespace.h>
#include <linux/nsproxy.h>
#include <linux/security.h>
#include <linux/fs_struct.h>
#include "mount.h"
#include "sim_util.h"

int SimGetFile(char *fpath, struct file **file)
{
	int ret = 0;
	mm_segment_t old_fs = get_fs();

	if(!fpath || !file)
	{
		return -1;
	}

	/* 
		real path checking. It ensure that fpath is absolute path. 
		symbolic link is ignored at filp_open()
	*/
	if(strncmp(fpath, "/", 1) != 0)
	{
		return -1;
	}

/**
  * If SecureZone is enabled, We have to set uepLevel to succeed path-lookup.
  * If we don't, filp_open() returns permission error from SecureZone.
  * 
  * [ Why set uepLevel here?? ]
  * -- kuep verification function changes uepLevel lower.
  * -- Even we set uepLevel to '9',  uepLevel will be reset again after kuep verification function.
  * -- So, we have to set uepLevel right before calling filp_open().
*/
#ifdef CONFIG_SECURITY_SFD_SECURECONTAINER
	current->uepLevel = '9';
#endif

	set_fs(KERNEL_DS);

	*file = filp_open(fpath, O_RDONLY, 0644);
	if(IS_ERR(*file))
	{
		SIM_PRINT_ERROR("filp_open error : %ld", (long)(*file));
		ret = -1;
		*file = NULL;
		goto ERROR_PROC;
	}

ERROR_PROC:
	set_fs(old_fs);
	return ret;
}

void SimCloseFile(struct file *file)
{
	if(file)
	{
		filp_close(file, current->files);
	}
}

int SimReadFile(char *fpath, char **buf)
{
	loff_t fileSize;
	int readSize;
	struct file *file = NULL;
	int ret = 0;
	
	if(!fpath|| !buf)
	{
		return -1;
	}

	ret = SimGetFile(fpath, &file);
	if(ret)
	{
		return ret;
	}

	fileSize = i_size_read(file->f_inode);
	if(!fileSize)
	{
		ret = -1;
		goto out;
	}

	*buf = (char*)kcalloc(fileSize + 1, sizeof(char), GFP_KERNEL);
	if(*buf == NULL)
	{
		ret = -1;
		goto out;
	}

	readSize = kernel_read(file, 0, *buf, fileSize);
	if(readSize != fileSize)
	{
		kfree(*buf);
		*buf = NULL;
		ret = -1;
	}

out:
	SimCloseFile(file);
	return ret;
}


int SimCheckMountRoot(void)
{
	int ret = 0;
	struct path root;
	struct nsproxy *nsp = NULL;
	struct mnt_namespace *ns = NULL;
	struct task_struct *task = current;
	
	if(!task)
	{
		return -1;
	}
	
	get_task_struct(current);
	task_lock(task);
	
	nsp = task->nsproxy;
	if (!nsp || !nsp->mnt_ns)
	{
		ret = -1;
		SIM_PRINT_ERROR("!nsp || !nsp->mnt_ns");
		goto out;
	}
	if (!task->fs)
	{
		ret = -1;
		SIM_PRINT_ERROR("!task->fs");
		goto out;
	}

	ns = nsp->mnt_ns;
	get_mnt_ns(ns);
	get_fs_root(task->fs, &root);

	/* Check root is mounted or not */
	if(root.mnt && root.mnt->mnt_root && root.dentry)
	{
		if(root.mnt->mnt_root == root.dentry && strncmp(root.dentry->d_iname, "/", strlen("/")+1) == 0)
		{
			ret = 0;
			SIM_PRINT_DEBUG("root is mounted");
		}
		else
		{
			ret = -1;
			SIM_PRINT_ERROR("root is not mounted");
		}
	}
	else
	{
		ret = -1;
		SIM_PRINT_ERROR("root NULL");
	}

	path_put(&root);
	put_mnt_ns(ns);
	
out:
	task_unlock(task);
	put_task_struct(task);
	return ret;
}

char* SimStrdup(char *str)
{
	size_t len = 0;
	char *dup = NULL;

	if(!str)
	{
		return NULL;
	}

	len = strlen(str) + 1;
	dup = (char*)kcalloc(len, sizeof(char), GFP_KERNEL);
	if(!dup)
	{
		return NULL;
	}
	
	memcpy(dup, str, len-1);
	return dup;
}

