/**
 * @file sim_util.h
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

#ifndef _SIM_UTIL_H
#define _SIM_UTIL_H

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#define SIM_BUILTIN_TAG "SIM-BUILTIN"
#define SIM_PRINT_ERROR(...) printk(KERN_ERR "[" SIM_BUILTIN_TAG "] " __VA_ARGS__)

#ifdef CONFIG_SECURITY_SIM_DEBUG
#define SIM_PRINT_DEBUG(...) printk(KERN_INFO "[" SIM_BUILTIN_TAG "] " __VA_ARGS__)
#else
#define SIM_PRINT_DEBUG(...)
#endif

int SimGetFile(char *fpath, struct file **file);
void SimCloseFile(struct file *file);
int SimReadFile(char *fpath, char **buf);
int SimCheckMountRoot(void);
char* SimStrdup(char *str);

#endif

