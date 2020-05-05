/**
 * @file	fs/vdfs/xattr.c
 * @brief	Operations with catalog tree.
 * @author
 *
 * This file implements bnode operations and its related functions.
 *
 * Copyright 2013 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include <linux/xattr.h>
#include <linux/fs.h>
#include "emmcfs.h"
#include "xattrtree.h"

int emmcfs_fill_btree(struct emmcfs_sb_info *sbi,
		struct emmcfs_btree *btree, struct inode *inode);

static int xattrtree_insert(struct emmcfs_btree *tree, u64 object_id,
		const char *name, size_t val_len, const void *value)
{
	void *insert_data = NULL;
	struct vdfs_xattrtree_key *key;
	u32 key_len;
	int name_len = strlen(name);
	int ret = 0;


	if (name_len >= VDFS_XATTR_NAME_MAX_LEN ||
			val_len >= VDFS_XATTR_VAL_MAX_LEN) {
		EMMCFS_ERR("xattr name or val too long");
		return -EINVAL;
	}

	insert_data = kzalloc(tree->max_record_len, GFP_KERNEL);
	if (!insert_data)
		return -ENOMEM;

	key = insert_data;

	key_len = sizeof(*key) - sizeof(key->name) + name_len;

	strncpy(key->gen_key.magic, VDFS_XATTR_REC_MAGIC,
			sizeof(VDFS_XATTR_REC_MAGIC) - 1);
	key->gen_key.key_len = cpu_to_le32(key_len);
	key->gen_key.record_len = cpu_to_le32(key_len + val_len);

	key->object_id = cpu_to_le64(object_id);
	strncpy(key->name, name, name_len);
	key->name_len = name_len;

	memcpy(get_value_pointer(key), value, val_len);

	ret = emmcfs_btree_insert(tree, insert_data);
	kfree(insert_data);

	return ret;
}

/**
 * @brief		Xattr tree key compare function.
 * @param [in]	__key1	Pointer to the first key
 * @param [in]	__key2	Pointer to the second key
 * @return		Returns value	< 0	if key1 < key2,
					== 0	if key1 = key2,
					> 0	if key1 > key2 (like strcmp)
 */
int vdfs_xattrtree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2)
{
	struct vdfs_xattrtree_key *key1, *key2;
	int diff;


	key1 = container_of(__key1, struct vdfs_xattrtree_key, gen_key);
	key2 = container_of(__key2, struct vdfs_xattrtree_key, gen_key);

	if (key1->object_id != key2->object_id)
		return (__s64) le64_to_cpu(key1->object_id) -
			(__s64) le64_to_cpu(key2->object_id);

	diff = memcmp(key1->name, key2->name,
			min(key1->name_len, key2->name_len));

	if (diff)
		return diff;

	return key1->name_len - key2->name_len;
}

static struct vdfs_xattrtree_key *xattrtree_alloc_key(u64 object_id,
		const char *name)
{
	struct vdfs_xattrtree_key *key;
	int name_len = strlen(name);

	if (name_len >= VDFS_XATTR_NAME_MAX_LEN)
		return ERR_PTR(-EINVAL);

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);


	key->object_id = cpu_to_le64(object_id);
	key->name_len = name_len;
	strncpy(key->name, name, name_len);

	return key;
}


static int xattrtree_fill_record(struct emmcfs_bnode *bnode, int pos,
		struct vdfs_xattrtree_record *record)
{
	int ret = 0;
	struct vdfs_xattrtree_key *found_key;

	found_key = emmcfs_get_btree_record(bnode, pos);
	if (IS_ERR(found_key)) {
		ret = PTR_ERR(found_key);
		goto exit;
	}

	record->btree_rec.bnode = bnode;
	record->btree_rec.pos = pos;
	record->key = found_key;
	record->val = get_value_pointer(found_key);

exit:
	return ret;
}

struct vdfs_xattrtree_record *vdfs_xattrtree_find(struct emmcfs_btree *tree,
		u64 object_id, const char *name,
		enum emmcfs_get_bnode_mode mode)
{
	struct emmcfs_bnode *bnode;
	int pos = 0;
	struct vdfs_xattrtree_record *record;
	struct vdfs_xattrtree_key *key;
	void *err_ret;
	int ret;

	record = kzalloc(sizeof(*record), GFP_KERNEL);
	if (!record)
		return ERR_PTR(-ENOMEM);

	key = xattrtree_alloc_key(object_id, name);
	if (IS_ERR(key)) {
		kfree(record);
		return (void *) key;
	}

	bnode = emmcfs_btree_find(tree, &key->gen_key, &pos, mode);
	if (IS_ERR(bnode)) {
		err_ret = bnode;
		goto err_exit_no_put;
	}

	/* Here is a real BUG_ON, all errors should be returned ONLY via return
	 * value */
	EMMCFS_BUG_ON(pos == -1);

	ret = xattrtree_fill_record(bnode, pos, record);
	if (ret) {
		err_ret = ERR_PTR(ret);
		goto err_exit;
	}

	if (*name != '\0' &&
			tree->comp_fn(&key->gen_key, &record->key->gen_key)) {
		err_ret = ERR_PTR(-ENODATA);
		goto err_exit;
	}

	kfree(key);
	return record;

err_exit:
	emmcfs_put_bnode(bnode);
err_exit_no_put:
	kfree(record);
	kfree(key);
	/* Correct return in case absent xattr is ENODATA */
	if (PTR_ERR(err_ret) == -ENOENT)
		err_ret = ERR_PTR(-ENODATA);
	return err_ret;
}

int xattrtree_remove_record(struct emmcfs_btree *tree, u64 object_id,
		const char *name)
{
	struct vdfs_xattrtree_key *key;
	int ret;

	key = xattrtree_alloc_key(object_id, name);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = emmcfs_btree_remove(tree, &key->gen_key);

	return ret;
}


void xattrtree_release_record(struct vdfs_xattrtree_record *record)
{
	emmcfs_put_bnode(record->btree_rec.bnode);
	kfree(record);
}

static int xattrtree_get_next_record(struct vdfs_xattrtree_record *record)
{
	struct emmcfs_bnode *bnode = record->btree_rec.bnode;
	int pos = record->btree_rec.pos;
	void *raw_record = 0;

	raw_record = emmcfs_get_next_btree_record(&bnode, &pos);

	if (IS_ERR(raw_record))
		return PTR_ERR(raw_record);

	xattrtree_fill_record(bnode, pos, record);

	return 0;
}

struct vdfs_xattrtree_record *xattrtree_get_first_record(
		struct emmcfs_btree *tree, u64 object_id,
		enum emmcfs_get_bnode_mode mode)
{
	struct vdfs_xattrtree_record *record;
	int ret = 0;

	record = vdfs_xattrtree_find(tree, object_id, "",
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record))
		return record;

	ret = xattrtree_get_next_record(record);
	if (ret)
		goto err_exit;

	if (le64_to_cpu(record->key->object_id) != object_id) {
		ret = -ENOENT;
		goto err_exit;
	}

	return record;

err_exit:
	xattrtree_release_record(record);
	return ERR_PTR(ret);

}

#ifndef USER_SPACE
int vdfs_xattrtree_remove_all(struct emmcfs_btree *tree, u64 object_id)
{
	struct vdfs_xattrtree_record *record = 0;
	struct vdfs_xattrtree_key *rm_key =
		kmalloc(sizeof(*rm_key), GFP_KERNEL);
	int ret = 0;

	if (!rm_key)
		return -ENOMEM;


	while (!ret) {
		/*EMMCFS_START_TRANSACTION(tree->sbi);*/
		mutex_w_lock(tree->rw_tree_lock);

		record = xattrtree_get_first_record(tree, object_id,
				EMMCFS_BNODE_MODE_RO);
		if (IS_ERR(record)) {
			if (PTR_ERR(record) == -ENOENT)
				ret = 0;
			else
				ret = PTR_ERR(record);

			mutex_w_unlock(tree->rw_tree_lock);
			/*EMMCFS_STOP_TRANSACTION(tree->sbi);*/
			break;
		}
		memcpy(rm_key, record->key, record->key->gen_key.key_len);
		xattrtree_release_record(record);


		ret = emmcfs_btree_remove(tree, &rm_key->gen_key);
		mutex_w_unlock(tree->rw_tree_lock);
		/*EMMCFS_STOP_TRANSACTION(tree->sbi);*/
	}

	kfree(rm_key);
	return ret;
}

int vdfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	int ret = 0;
	struct inode *inode = dentry->d_inode;
	struct emmcfs_sb_info *sbi = EMMCFS_SB(inode->i_sb);

	EMMCFS_START_TRANSACTION(sbi);
	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);

	if (flags & XATTR_REPLACE) {
		EMMCFS_BUG();
	} else if (flags & XATTR_CREATE) {
		ret = xattrtree_insert(sbi->xattr_tree, inode->i_ino,
				name, size, value);
	}

	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	EMMCFS_STOP_TRANSACTION(sbi);

	return 0;
}


ssize_t vdfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t buf_size)
{
	struct vdfs_xattrtree_record *record;
	struct inode *inode = dentry->d_inode;
	struct emmcfs_sb_info *sbi = EMMCFS_SB(inode->i_sb);
	ssize_t size;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	mutex_r_lock(sbi->xattr_tree->rw_tree_lock);

	record = vdfs_xattrtree_find(sbi->xattr_tree, inode->i_ino, name,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);
		return PTR_ERR(record);
	}

	size = le32_to_cpu(record->key->gen_key.record_len) -
		le32_to_cpu(record->key->gen_key.key_len);
	if (!buffer)
		goto exit;

	if (size > buf_size) {
		size = -ERANGE;
		goto exit;
	}

	memcpy(buffer, record->val, size);
exit:
	xattrtree_release_record(record);
	mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);

	return size;
}


int vdfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	struct emmcfs_sb_info *sbi = EMMCFS_SB(inode->i_sb);
	int ret = 0;
	if (strcmp(name, "") == 0)
		return -EINVAL;


	EMMCFS_START_TRANSACTION(sbi);
	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);

	ret = xattrtree_remove_record(sbi->xattr_tree, inode->i_ino, name);

	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	EMMCFS_STOP_TRANSACTION(sbi);

	return ret;
}

ssize_t vdfs_listxattr(struct dentry *dentry, char *buffer, size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	struct emmcfs_sb_info *sbi = EMMCFS_SB(inode->i_sb);
	struct vdfs_xattrtree_record *record;
	ssize_t size = 0;
	int ret = 0;

	mutex_r_lock(sbi->xattr_tree->rw_tree_lock);
	record = xattrtree_get_first_record(sbi->xattr_tree, inode->i_ino,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);
		if (PTR_ERR(record) == -ENOENT)
			return -ENODATA;
		return PTR_ERR(record);
	}

	while (!ret && le64_to_cpu(record->key->object_id) == inode->i_ino) {
		int name_len = record->key->name_len + 1;

		if (buffer) {
			if (buf_size < name_len) {
				ret = -ERANGE;
				break;
			}
			memcpy(buffer, record->key->name, name_len - 1);
			buffer[name_len - 1] = 0;
			buf_size -= name_len;
			buffer += name_len;
		}

		size += name_len;

		ret = xattrtree_get_next_record(record);
	}

	if (ret == -ENOENT)
		/* It is normal if there is no more records in the btree */
		ret = 0;

	xattrtree_release_record(record);
	mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);


	return ret ? ret : size;
}

/**
 * @brief			xattr tree constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
int vdsf_fill_xattr_tree(struct emmcfs_sb_info *sbi)

{
	struct inode *inode = NULL;
	struct emmcfs_btree *xattr_tree;
	int err = 0;

	EMMCFS_LOG_FUNCTION_START(sbi);
	xattr_tree = kzalloc(sizeof(*xattr_tree), GFP_KERNEL);
	if (!xattr_tree) {
		EMMCFS_LOG_FUNCTION_END(sbi, -ENOMEM);
		return -ENOMEM;
	}

	xattr_tree->btree_type = VDFS_BTREE_XATTRS;
	xattr_tree->max_record_len = sizeof(struct vdfs_xattrtree_key) +
			VDFS_XATTR_VAL_MAX_LEN;
	inode = emmcfs_iget(sbi->sb, VDFS_XATTR_TREE_INO, NULL);
	if (IS_ERR(inode)) {
		int ret;
		kfree(xattr_tree);
		ret = PTR_ERR(inode);
		EMMCFS_LOG_FUNCTION_END(sbi, ret);
		return ret;
	}

	err = emmcfs_fill_btree(sbi, xattr_tree, inode);
	if (err)
		goto err_put_inode;

	xattr_tree->comp_fn = vdfs_xattrtree_cmpfn;
	sbi->xattr_tree = xattr_tree;
	EMMCFS_LOG_FUNCTION_END(sbi, 0);
	return 0;

err_put_inode:
	iput(inode);
	EMMCFS_ERR("can not read extended attrubute tree");
	EMMCFS_LOG_FUNCTION_END(sbi, err);
	return err;
}

#endif

#ifdef USER_SPACE
void dummy_xattrtree_record_init(struct vdfs_raw_xattrtree_record *xattr_record)
{
	int key_len, name_len;
	memset(xattr_record, 0, sizeof(*xattr_record));
	set_magic(xattr_record->key.gen_key.magic, XATTRTREE_LEAF);


	name_len = strlen(VDFS_XATTRTREE_ROOT_REC_NAME);
	key_len = sizeof(xattr_record->key) - sizeof(xattr_record->key.name) +
		name_len;



	xattr_record->key.gen_key.key_len = cpu_to_le32(key_len);
	/* Xattr root record has no value, so record_len == key_len */
	xattr_record->key.gen_key.record_len = key_len;
	xattr_record->key.name_len = name_len;
	strncpy(xattr_record->key.name, VDFS_XATTRTREE_ROOT_REC_NAME, name_len);
}

static void xattrtree_init_root_bnode(struct emmcfs_bnode *root_bnode)
{
	struct vdfs_raw_xattrtree_record xattr_record;

	temp_stub_init_new_node_descr(root_bnode, EMMCFS_NODE_LEAF);
	dummy_xattrtree_record_init(&xattr_record);
	temp_stub_insert_into_node(root_bnode, &xattr_record, 0);
}

int init_xattrtree(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct vdfs_tools_btree_info *xattr_btree = &sbi->xattrtree;
	struct emmcfs_bnode *root_bnode = 0;

	log_activity("Create xattr tree");


	ret = btree_init(sbi, xattr_btree, VDFS_BTREE_XATTRS,
			sizeof(struct vdfs_xattrtree_key) +
			sizeof(struct vdfs_raw_xattrtree_record));

	if (ret)
		goto error_exit;

	/* Init root bnode */
	root_bnode = emmcfs_alloc_new_bnode(&xattr_btree->host);
	if (IS_ERR(root_bnode)) {
		ret = (PTR_ERR(root_bnode));
		root_bnode = 0;
		goto error_exit;
	}
	xattrtree_init_root_bnode(root_bnode);

	return 0;

error_exit:

	log_error("Can't init xattr tree");
	btree_destroy_tree(xattr_btree);
	return ret;
}
#endif
