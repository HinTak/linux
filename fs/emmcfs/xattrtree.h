/**
 * @file	fs/emmcfs/xattrtree.h
 * @brief	Basic B-tree operations - interfaces and prototypes.
 * @date	31/06/2013
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file contains prototypes, constants and enumerations extended attributes
 * tree
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef XATRTREE_H_
#define XATRTREE_H_


#include <linux/emmcfs_fs.h>

#define VDFS_XATTR_REC_MAGIC			"XAre"
#define XATTRTREE_LEAF "XAle"
#define VDFS_XATTRTREE_ROOT_REC_NAME "xattr_root"


#define VDFS_XATTR_NAME_MAX_LEN 200
#define VDFS_XATTR_VAL_MAX_LEN 200

struct vdfs_btree_record {
	struct emmcfs_bnode *bnode;
	int pos;
};

/** @brief	Btree search key for xattr tree
 */
struct vdfs_xattrtree_key {
	/** Key */
	struct emmcfs_generic_key gen_key;
	/** Object ID */
	__le64 object_id;
	__u8 name_len;
	char name[VDFS_XATTR_NAME_MAX_LEN];
} __packed;

/** @brief	Xattr tree information.
 */
struct vdfs_raw_xattrtree_record {
	/** Key */
	struct vdfs_xattrtree_key key;
	/** Value */
	int value;
} __packed;

struct vdfs_xattrtree_record {
	struct vdfs_btree_record btree_rec;
	struct vdfs_xattrtree_key *key;
	void *val;
};


#endif

