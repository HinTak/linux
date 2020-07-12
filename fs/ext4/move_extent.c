/*
 * Copyright (c) 2008,2009 NEC Software Tohoku, Ltd.
 * Written by Takashi Sato <t-sato@yk.jp.nec.com>
 *            Akira Fujita <a-fujita@rs.jp.nec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "ext4_extents.h"

/**
 * get_ext_path - Find an extent path for designated logical block number.
 *
 * @inode:	an inode which is searched
 * @lblock:	logical block number to find an extent path
 * @path:	pointer to an extent path pointer (for output)
 *
 * ext4_find_extent wrapper. Return 0 on success, or a negative error value
 * on failure.
 */
int get_ext_path(struct inode *inode, ext4_lblk_t lblock,
		struct ext4_ext_path **ppath)
{
	struct ext4_ext_path *path;

	path = ext4_find_extent(inode, lblock, ppath, EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);
	if (path[ext_depth(inode)].p_ext == NULL) {
		ext4_ext_drop_refs(path);
		kfree(path);
		*ppath = NULL;
		return -ENODATA;
	}
	*ppath = path;
	return 0;
}

/**
 * ext4_double_down_write_data_sem - Acquire two inodes' write lock
 *                                   of i_data_sem
 *
 * Acquire write lock of i_data_sem of the two inodes
 */
void
ext4_double_down_write_data_sem(struct inode *first, struct inode *second)
{
	if (first < second) {
		down_write(&EXT4_I(first)->i_data_sem);
		down_write_nested(&EXT4_I(second)->i_data_sem, SINGLE_DEPTH_NESTING);
	} else {
		down_write(&EXT4_I(second)->i_data_sem);
		down_write_nested(&EXT4_I(first)->i_data_sem, SINGLE_DEPTH_NESTING);

	}
}

/**
 * ext4_double_up_write_data_sem - Release two inodes' write lock of i_data_sem
 *
 * @orig_inode:		original inode structure to be released its lock first
 * @donor_inode:	donor inode structure to be released its lock second
 * Release write lock of i_data_sem of two inodes (orig and donor).
 */
void
ext4_double_up_write_data_sem(struct inode *orig_inode,
			      struct inode *donor_inode)
{
	up_write(&EXT4_I(orig_inode)->i_data_sem);
	up_write(&EXT4_I(donor_inode)->i_data_sem);
}

/**
 * mext_check_coverage - Check that all extents in range has the same type
 *
 * @inode:		inode in question
 * @from:		block offset of inode
 * @count:		block count to be checked
 * @unwritten:		extents expected to be unwritten
 * @err:		pointer to save error value
 *
 * Return 1 if all extents in range has expected type, and zero otherwise.
 */
static int
mext_check_coverage(struct inode *inode, ext4_lblk_t from, ext4_lblk_t count,
		    int unwritten, int *err)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ext;
	int ret = 0;
	ext4_lblk_t last = from + count;
	while (from < last) {
		*err = get_ext_path(inode, from, &path);
		if (*err)
			goto out;
		ext = path[ext_depth(inode)].p_ext;
		if (unwritten != ext4_ext_is_unwritten(ext))
			goto out;
		from += ext4_ext_get_actual_len(ext);
		ext4_ext_drop_refs(path);
	}
	ret = 1;
out:
	ext4_ext_drop_refs(path);
	kfree(path);
	return ret;
}

/**
 * mext_page_double_lock - Grab and lock pages on both @inode1 and @inode2
 *
 * @inode1:	the inode structure
 * @inode2:	the inode structure
 * @index1:	page index
 * @index2:	page index
 * @page:	result page vector
 *
 * Grab two locked pages for inode's by inode order
 */
static int
mext_page_double_lock(struct inode *inode1, struct inode *inode2,
		      pgoff_t index1, pgoff_t index2, struct page *page[2])
{
	struct address_space *mapping[2];
	unsigned fl = AOP_FLAG_NOFS;

	BUG_ON(!inode1 || !inode2);
	if (inode1 < inode2) {
		mapping[0] = inode1->i_mapping;
		mapping[1] = inode2->i_mapping;
	} else {
		pgoff_t tmp = index1;
		index1 = index2;
		index2 = tmp;
		mapping[0] = inode2->i_mapping;
		mapping[1] = inode1->i_mapping;
	}

	page[0] = grab_cache_page_write_begin(mapping[0], index1, fl);
	if (!page[0])
		return -ENOMEM;

	page[1] = grab_cache_page_write_begin(mapping[1], index2, fl);
	if (!page[1]) {
		unlock_page(page[0]);
		page_cache_release(page[0]);
		return -ENOMEM;
	}
	/*
	 * grab_cache_page_write_begin() may not wait on page's writeback if
	 * BDI not demand that. But it is reasonable to be very conservative
	 * here and explicitly wait on page's writeback
	 */
	wait_on_page_writeback(page[0]);
	wait_on_page_writeback(page[1]);
	if (inode1 > inode2) {
		struct page *tmp;
		tmp = page[0];
		page[0] = page[1];
		page[1] = tmp;
	}
	return 0;
}

/* Force page buffers uptodate w/o dropping page's lock */
static int
mext_page_mkuptodate(struct page *page, unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	sector_t block;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, block_start, block_end;
	int i, err,  nr = 0, partial = 0;
	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));

	if (PageUptodate(page))
		return 0;

	blocksize = 1 << inode->i_blkbits;
	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	head = page_buffers(page);
	block = (sector_t)page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	for (bh = head, block_start = 0; bh != head || !block_start;
	     block++, block_start = block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
			continue;
		}
		if (buffer_uptodate(bh))
			continue;
		if (!buffer_mapped(bh)) {
			err = ext4_get_block(inode, block, bh, 0);
			if (err) {
				SetPageError(page);
				return err;
			}
			if (!buffer_mapped(bh)) {
				zero_user(page, block_start, blocksize);
				set_buffer_uptodate(bh);
				continue;
			}
		}
		BUG_ON(nr >= MAX_BUF_PER_PAGE);
		arr[nr++] = bh;
	}
	/* No io required */
	if (!nr)
		goto out;

	for (i = 0; i < nr; i++) {
		bh = arr[i];
		if (!bh_uptodate_or_lock(bh)) {
			err = bh_submit_read(bh);
			if (err)
				return err;
		}
	}
out:
	if (!partial)
		SetPageUptodate(page);
	return 0;
}

/**
 * move_extent_per_page - Move extent data per page
 *
 * @o_filp:			file structure of original file
 * @donor_inode:		donor inode
 * @orig_page_offset:		page index on original file
 * @donor_page_offset:		page index on donor file
 * @data_offset_in_page:	block index where data swapping starts
 * @block_len_in_page:		the number of blocks to be swapped
 * @unwritten:			orig extent is unwritten or not
 * @err:			pointer to save return value
 *
 * Save the data in original inode blocks and replace original inode extents
 * with donor inode extents by calling ext4_swap_extents().
 * Finally, write out the saved data in new original inode blocks. Return
 * replaced block count.
 */
static int
move_extent_per_page(struct file *o_filp, struct inode *donor_inode,
		     pgoff_t orig_page_offset, pgoff_t donor_page_offset,
		     int data_offset_in_page,
		     int block_len_in_page, int unwritten, int *err)
{
	struct inode *orig_inode = file_inode(o_filp);
	struct page *pagep[2] = {NULL, NULL};
	handle_t *handle;
	ext4_lblk_t orig_blk_offset, donor_blk_offset;
	unsigned long blocksize = orig_inode->i_sb->s_blocksize;
	unsigned int tmp_data_size, data_size, replaced_size;
	int err2, jblocks, retries = 0;
	int replaced_count = 0;
	int from = data_offset_in_page << orig_inode->i_blkbits;
	int blocks_per_page = PAGE_CACHE_SIZE >> orig_inode->i_blkbits;
	struct super_block *sb = orig_inode->i_sb;

	/*
	 * It needs twice the amount of ordinary journal buffers because
	 * inode and donor_inode may change each different metadata blocks.
	 */
again:
	*err = 0;
	jblocks = ext4_writepage_trans_blocks(orig_inode) * 2;
	handle = ext4_journal_start(orig_inode, EXT4_HT_MOVE_EXTENTS, jblocks);
	if (IS_ERR(handle)) {
		*err = PTR_ERR(handle);
		return 0;
	}

	orig_blk_offset = orig_page_offset * blocks_per_page +
		data_offset_in_page;

	donor_blk_offset = donor_page_offset * blocks_per_page +
		data_offset_in_page;

	/* Calculate data_size */
	if ((orig_blk_offset + block_len_in_page - 1) ==
	    ((orig_inode->i_size - 1) >> orig_inode->i_blkbits)) {
		/* Replace the last block */
		tmp_data_size = orig_inode->i_size & (blocksize - 1);
		/*
		 * If data_size equal zero, it shows data_size is multiples of
		 * blocksize. So we set appropriate value.
		 */
		if (tmp_data_size == 0)
			tmp_data_size = blocksize;

		data_size = tmp_data_size +
			((block_len_in_page - 1) << orig_inode->i_blkbits);
	} else
		data_size = block_len_in_page << orig_inode->i_blkbits;

	replaced_size = data_size;

	*err = mext_page_double_lock(orig_inode, donor_inode, orig_page_offset,
				     donor_page_offset, pagep);
	if (unlikely(*err < 0))
		goto stop_journal;
	/*
	 * If orig extent was unwritten it can become initialized
	 * at any time after i_data_sem was dropped, in order to
	 * serialize with delalloc we have recheck extent while we
	 * hold page's lock, if it is still the case data copy is not
	 * necessary, just swap data blocks between orig and donor.
	 */
	if (unwritten) {
		ext4_double_down_write_data_sem(orig_inode, donor_inode);
		/* If any of extents in range became initialized we have to
		 * fallback to data copying */
		unwritten = mext_check_coverage(orig_inode, orig_blk_offset,
						block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		unwritten &= mext_check_coverage(donor_inode, donor_blk_offset,
						 block_len_in_page, 1, err);
		if (*err)
			goto drop_data_sem;

		if (!unwritten) {
			ext4_double_up_write_data_sem(orig_inode, donor_inode);
			goto data_copy;
		}
		if ((page_has_private(pagep[0]) &&
		     !try_to_release_page(pagep[0], 0)) ||
		    (page_has_private(pagep[1]) &&
		     !try_to_release_page(pagep[1], 0))) {
			*err = -EBUSY;
			goto drop_data_sem;
		}
		replaced_count = ext4_swap_extents(handle, orig_inode,
						   donor_inode, orig_blk_offset,
						   donor_blk_offset,
						   block_len_in_page, 1, err);
	drop_data_sem:
		ext4_double_up_write_data_sem(orig_inode, donor_inode);
		goto unlock_pages;
	}
data_copy:
	*err = mext_page_mkuptodate(pagep[0], from, from + replaced_size);
	if (*err)
		goto unlock_pages;

	/* At this point all buffers in range are uptodate, old mapping layout
	 * is no longer required, try to drop it now. */
	if ((page_has_private(pagep[0]) && !try_to_release_page(pagep[0], 0)) ||
	    (page_has_private(pagep[1]) && !try_to_release_page(pagep[1], 0))) {
		*err = -EBUSY;
		goto unlock_pages;
	}
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	replaced_count = ext4_swap_extents(handle, orig_inode, donor_inode,
					       orig_blk_offset, donor_blk_offset,
					   block_len_in_page, 1, err);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	if (*err) {
		if (replaced_count) {
			block_len_in_page = replaced_count;
			replaced_size =
				block_len_in_page << orig_inode->i_blkbits;
		} else
			goto unlock_pages;
	}
	/* Perform all necessary steps similar write_begin()/write_end()
	 * but keeping in mind that i_size will not change */
	*err = __block_write_begin(pagep[0], from, replaced_size,
				   ext4_get_block);
	if (!*err)
		*err = block_commit_write(pagep[0], from, from + replaced_size);

	if (unlikely(*err < 0))
		goto repair_branches;

	/* Even in case of data=writeback it is reasonable to pin
	 * inode to transaction, to prevent unexpected data loss */
	*err = ext4_jbd2_file_inode(handle, orig_inode);

unlock_pages:
	unlock_page(pagep[0]);
	page_cache_release(pagep[0]);
	unlock_page(pagep[1]);
	page_cache_release(pagep[1]);
stop_journal:
	ext4_journal_stop(handle);
	if (*err == -ENOSPC &&
	    ext4_should_retry_alloc(sb, &retries))
		goto again;
	/* Buffer was busy because probably is pinned to journal transaction,
	 * force transaction commit may help to free it. */
	if (*err == -EBUSY && retries++ < 4 && EXT4_SB(sb)->s_journal &&
	    jbd2_journal_force_commit_nested(EXT4_SB(sb)->s_journal))
		goto again;
	return replaced_count;

repair_branches:
	/*
	 * This should never ever happen!
	 * Extents are swapped already, but we are not able to copy data.
	 * Try to swap extents to it's original places
	 */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	replaced_count = ext4_swap_extents(handle, donor_inode, orig_inode,
					       orig_blk_offset, donor_blk_offset,
					   block_len_in_page, 0, &err2);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	if (replaced_count != block_len_in_page) {
		EXT4_ERROR_INODE_BLOCK(orig_inode, (sector_t)(orig_blk_offset),
				       "Unable to copy data block,"
				       " data will be lost.");
		*err = -EIO;
	}
	replaced_count = 0;
	goto unlock_pages;
}

/**
 * mext_check_arguments - Check whether move extent can be done
 *
 * @orig_inode:		original inode
 * @donor_inode:	donor inode
 * @orig_start:		logical start offset in block for orig
 * @donor_start:	logical start offset in block for donor
 * @len:		the number of blocks to be moved
 *
 * Check the arguments of ext4_move_extents() whether the files can be
 * exchanged with each other.
 * Return 0 on success, or a negative error value on failure.
 */
static int
mext_check_arguments(struct inode *orig_inode,
		     struct inode *donor_inode, __u64 orig_start,
		     __u64 donor_start, __u64 *len)
{
	__u64 orig_eof, donor_eof;
	unsigned int blkbits = orig_inode->i_blkbits;
	unsigned int blocksize = 1 << blkbits;

	orig_eof = (i_size_read(orig_inode) + blocksize - 1) >> blkbits;
	donor_eof = (i_size_read(donor_inode) + blocksize - 1) >> blkbits;


	if (donor_inode->i_mode & (S_ISUID|S_ISGID)) {
		ext4_debug("ext4 move extent: suid or sgid is set"
			   " to donor file [ino:orig %lu, donor %lu]\n",
			   orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if (IS_IMMUTABLE(donor_inode) || IS_APPEND(donor_inode))
		return -EPERM;

	/* Ext4 move extent does not support swapfile */
	if (IS_SWAPFILE(orig_inode) || IS_SWAPFILE(donor_inode)) {
		ext4_debug("ext4 move extent: The argument files should "
			"not be swapfile [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EBUSY;
	}

	/* Ext4 move extent supports only extent based file */
	if (!(ext4_test_inode_flag(orig_inode, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: orig file is not extents "
			"based file [ino:orig %lu]\n", orig_inode->i_ino);
		return -EOPNOTSUPP;
	} else if (!(ext4_test_inode_flag(donor_inode, EXT4_INODE_EXTENTS))) {
		ext4_debug("ext4 move extent: donor file is not extents "
			"based file [ino:donor %lu]\n", donor_inode->i_ino);
		return -EOPNOTSUPP;
	}

	if ((!orig_inode->i_size) || (!donor_inode->i_size)) {
		ext4_debug("ext4 move extent: File size is 0 byte\n");
		return -EINVAL;
	}

	/* Start offset should be same */
	if ((orig_start & ~(PAGE_MASK >> orig_inode->i_blkbits)) !=
	    (donor_start & ~(PAGE_MASK >> orig_inode->i_blkbits))) {
		ext4_debug("ext4 move extent: orig and donor's start "
			"offset are not alligned [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	if ((orig_start >= EXT_MAX_BLOCKS) ||
	    (donor_start >= EXT_MAX_BLOCKS) ||
	    (*len > EXT_MAX_BLOCKS) ||
	    (donor_start + *len >= EXT_MAX_BLOCKS) ||
	    (orig_start + *len >= EXT_MAX_BLOCKS))  {
		ext4_debug("ext4 move extent: Can't handle over [%u] blocks "
			"[ino:orig %lu, donor %lu]\n", EXT_MAX_BLOCKS,
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}
	if (orig_eof < orig_start + *len - 1)
		*len = orig_eof - orig_start;
	if (donor_eof < donor_start + *len - 1)
		*len = donor_eof - donor_start;
	if (!*len) {
		ext4_debug("ext4 move extent: len should not be 0 "
			"[ino:orig %lu, donor %lu]\n", orig_inode->i_ino,
			donor_inode->i_ino);
		return -EINVAL;
	}

	return 0;
}

/**
 * ext4_move_extents - Exchange the specified range of a file
 *
 * @o_filp:		file structure of the original file
 * @d_filp:		file structure of the donor file
 * @orig_blk:		start offset in block for orig
 * @donor_blk:		start offset in block for donor
 * @len:		the number of blocks to be moved
 * @moved_len:		moved block length
 *
 * This function returns 0 and moved block length is set in moved_len
 * if succeed, otherwise returns error value.
 *
 */
int
ext4_move_extents(struct file *o_filp, struct file *d_filp, __u64 orig_blk,
		  __u64 donor_blk, __u64 len, __u64 *moved_len)
{
	struct inode *orig_inode = file_inode(o_filp);
	struct inode *donor_inode = file_inode(d_filp);
	struct ext4_ext_path *path = NULL;
	int blocks_per_page = PAGE_CACHE_SIZE >> orig_inode->i_blkbits;
	ext4_lblk_t o_end, o_start = orig_blk;
	ext4_lblk_t d_start = donor_blk;
	int ret;

	if (orig_inode->i_sb != donor_inode->i_sb) {
		ext4_debug("ext4 move extent: The argument files "
			"should be in same FS [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* orig and donor should be different inodes */
	if (orig_inode == donor_inode) {
		ext4_debug("ext4 move extent: The argument files should not "
			"be same inode [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}

	/* Regular file check */
	if (!S_ISREG(orig_inode->i_mode) || !S_ISREG(donor_inode->i_mode)) {
		ext4_debug("ext4 move extent: The argument files should be "
			"regular file [ino:orig %lu, donor %lu]\n",
			orig_inode->i_ino, donor_inode->i_ino);
		return -EINVAL;
	}
	/* TODO: This is non obvious task to swap blocks for inodes with full
	   jornaling enabled */
	if (ext4_should_journal_data(orig_inode) ||
	    ext4_should_journal_data(donor_inode)) {
		return -EINVAL;
	}
	/* Protect orig and donor inodes against a truncate */
	lock_two_nondirectories(orig_inode, donor_inode);

	/* Wait for all existing dio workers */
	ext4_inode_block_unlocked_dio(orig_inode);
	ext4_inode_block_unlocked_dio(donor_inode);
	inode_dio_wait(orig_inode);
	inode_dio_wait(donor_inode);

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(orig_inode, donor_inode);
	/* Check the filesystem environment whether move_extent can be done */
	ret = mext_check_arguments(orig_inode, donor_inode, orig_blk,
				    donor_blk, &len);
	if (ret)
		goto out;
	o_end = o_start + len;

	while (o_start < o_end) {
		struct ext4_extent *ex;
		ext4_lblk_t cur_blk, next_blk;
		pgoff_t orig_page_index, donor_page_index;
		int offset_in_page;
		int unwritten, cur_len;

		ret = get_ext_path(orig_inode, o_start, &path);
		if (ret)
			goto out;
		ex = path[path->p_depth].p_ext;
		next_blk = ext4_ext_next_allocated_block(path);
		cur_blk = le32_to_cpu(ex->ee_block);
		cur_len = ext4_ext_get_actual_len(ex);
		/* Check hole before the start pos */
		if (cur_blk + cur_len - 1 < o_start) {
			if (next_blk == EXT_MAX_BLOCKS) {
				o_start = o_end;
				ret = -ENODATA;
				goto out;
			}
			d_start += next_blk - o_start;
			o_start = next_blk;
			continue;
		/* Check hole after the start pos */
		} else if (cur_blk > o_start) {
			/* Skip hole */
			d_start += cur_blk - o_start;
			o_start = cur_blk;
			/* Extent inside requested range ?*/
			if (cur_blk >= o_end)
				goto out;
		} else { /* in_range(o_start, o_blk, o_len) */
			cur_len += cur_blk - o_start;
		}
		unwritten = ext4_ext_is_unwritten(ex);
		if (o_end - o_start < cur_len)
			cur_len = o_end - o_start;

		orig_page_index = o_start >> (PAGE_CACHE_SHIFT -
					       orig_inode->i_blkbits);
		donor_page_index = d_start >> (PAGE_CACHE_SHIFT -
					       donor_inode->i_blkbits);
		offset_in_page = o_start % blocks_per_page;
		if (cur_len > blocks_per_page- offset_in_page)
			cur_len = blocks_per_page - offset_in_page;
		/*
		 * Up semaphore to avoid following problems:
		 * a. transaction deadlock among ext4_journal_start,
		 *    ->write_begin via pagefault, and jbd2_journal_commit
		 * b. racing with ->readpage, ->write_begin, and ext4_get_block
		 *    in move_extent_per_page
		 */
		ext4_double_up_write_data_sem(orig_inode, donor_inode);
		/* Swap original branches with new branches */
		move_extent_per_page(o_filp, donor_inode,
				     orig_page_index, donor_page_index,
				     offset_in_page, cur_len,
				     unwritten, &ret);
		ext4_double_down_write_data_sem(orig_inode, donor_inode);
		if (ret < 0)
			break;
		o_start += cur_len;
		d_start += cur_len;
	}
	*moved_len = o_start - orig_blk;
	if (*moved_len > len)
		*moved_len = len;

out:
	if (*moved_len) {
		ext4_discard_preallocations(orig_inode);
		ext4_discard_preallocations(donor_inode);
	}

	ext4_ext_drop_refs(path);
	kfree(path);
	ext4_double_up_write_data_sem(orig_inode, donor_inode);
	ext4_inode_resume_unlocked_dio(orig_inode);
	ext4_inode_resume_unlocked_dio(donor_inode);
	unlock_two_nondirectories(orig_inode, donor_inode);

	return ret;
}


#ifdef CONFIG_EXT4_FS_SPLIT_FILE
/**
 * ext4_split_file - Split File from offset orig_start, create a new file
 *             with extents from orig_start to EOF of src file(o_filp).
 *
 * @o_filp:    src file descriptor
 * @dest_file: destination filename to create
 * @orig_start:        split point, its a logical block number.
 *
 * ext4_split_pvr . Return 0 on success, or a negative error value
 * on failure.
 */
	int
ext4_split_file(struct file *o_filp, char *dest_file, __u64 orig_start)
{
	int jblocks = 0;
	handle_t *dhandle;
	int err = 0;
	struct inode *isrc;
	struct inode *idest;
	int depth;
	struct file *dfilp = NULL;
	__u64 start_offset = orig_start;

	isrc = o_filp->f_path.dentry->d_inode;

	/* Regular file check */
	if (!S_ISREG(isrc->i_mode)) {
		ext_debug("ext4 split file: The argument file should be "
				"regular file [ino:orig %lu]\n", isrc->i_ino);
		err = -EINVAL;
		goto split_error;
	}

	/* check for offset within range */
	if (orig_start >= isrc->i_size) {
		ext_debug("Invalid offset split point\n");
		return -EIO;
	}

	/* from here onword split happens on block level */
	orig_start >>= isrc->i_sb->s_blocksize_bits;

	/* create file of name */
	if (dest_file) {
		dfilp = filp_open(dest_file,
				O_CREAT | O_RDWR | O_LARGEFILE, 0755);
		if (IS_ERR(dfilp))
			return PTR_ERR(dfilp);

		idest = dfilp->f_path.dentry->d_inode;
		if (idest->i_sb != isrc->i_sb) {
			ext_debug("New file path is from different fs\n");
			err = -EINVAL;
			goto split_error;
		}
	} else {
		ext_debug("No destination file specified\n");
		return -EIO;
	}

	depth = ext_depth(isrc);

	/* Reset dest file */
	jblocks = ext4_writepage_trans_blocks(isrc);
	dhandle = ext4_journal_start(idest, EXT4_HT_MOVE_EXTENTS, jblocks);
	if (IS_ERR(dhandle)) {
		err = -EIO;
		goto split_error;
	}

	ext4_ext_tree_init(dhandle, idest);
	ext4_journal_stop(dhandle);
	mutex_lock(&idest->i_mutex);

	/*
	 * Write out all dirty pages to avoid race conditions
	 * Then release them.
	 */
	if (isrc->i_mapping->nrpages &&
			mapping_tagged(isrc->i_mapping, PAGECACHE_TAG_DIRTY)) {

		err = filemap_write_and_wait_range(isrc->i_mapping,
				orig_start, -1);
		if (err)
			goto split_error;
	}

	/* Now release the pages */
	truncate_inode_pages_range(isrc->i_mapping, orig_start, -1);

	down_write(&EXT4_I(idest)->i_data_sem);
	down_write(&EXT4_I(isrc)->i_data_sem);
	ext4_discard_preallocations(isrc);
	err = ext4_split_move_extents(isrc, orig_start, idest);
	ext4_es_remove_extent(isrc, orig_start, EXT_MAX_BLOCKS - orig_start);
	up_write(&EXT4_I(isrc)->i_data_sem);
	up_write(&EXT4_I(idest)->i_data_sem);

	mutex_unlock(&idest->i_mutex);

	/*
	 * Since file-> pos keeps tracks of the seek pointer for a file,
	 * so a read might have taken file->pos ahead of the split position.
	 * After split adjust the file->pos so that seek does not return wrong
	 * position even though I/O will not be allowed
	 */
	if (current->files && !err) {
		struct files_struct *files = current->files;
		struct fdtable *fdt = NULL;
		unsigned int nr_open_fds = 0;

		spin_lock(&files->file_lock);
		fdt = files_fdtable(files); /* pointer to fd array */
		nr_open_fds = fdt->max_fds;

		while (nr_open_fds > 0) {
			if (fdt->fd[nr_open_fds - 1]) {
				if ((GET_INODE_FROM_FILP(o_filp)
							== GET_INODE_FROM_FILP(fdt->fd[nr_open_fds - 1]))
						&& (GET_SUPERDEV_FROM_FILP(o_filp)
							== GET_SUPERDEV_FROM_FILP(fdt->fd[nr_open_fds - 1]))) {
					if (fdt->fd[nr_open_fds - 1]->f_pos > start_offset) {
						spin_lock(&fdt->fd[nr_open_fds - 1]->f_lock);
						fdt->fd[nr_open_fds - 1]->f_pos = start_offset;
						spin_unlock(&fdt->fd[nr_open_fds - 1]->f_lock);
					}
				}
			}
			nr_open_fds--;
		}
		spin_unlock(&files->file_lock);
	}

split_error:
	if (dfilp)
		filp_close(dfilp, NULL);

	if (err) {
		ext_debug("SPLIT operation failed\n");
		return err;
	}

	return 0; /* success */
}
#endif /* CONFIG_EXT4_FS_SPLIT_FILE */

#ifdef CONFIG_EXT4_FS_MERGE_FILE
#include <linux/mount.h>
/*
 * ersae_file  - unlink file after merge
 *             decrement counter as required
 * @file:      file descriptor to unlink
 */
void erase_file(struct file *file)
{
	if (file->f_mode & FMODE_WRITE){
		struct vfsmount *mnt = file->f_path.mnt;
		struct dentry *dentry = file->f_path.dentry;
		struct inode *inode = dentry->d_inode;
		put_write_access(inode);
		mnt_drop_write(mnt);
	}

	mutex_lock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
	vfs_unlink(file->f_path.dentry->d_parent->d_inode,
			file->f_path.dentry, NULL);
	dput(file->f_path.dentry);
	mntput(file->f_path.mnt);
	mutex_unlock(&file->f_path.dentry->d_parent->d_inode->i_mutex);

	return;
}

/**
 * ext4_merge_file - Merge File from offset 0, to destination file
 *             and remove source file
 *
 * @o_filp:    src file descriptor
 * @dest_file: destination filename
 *
 * ext4_merge_pvr . Return 0 on success, or a negative error value
 * on failure.
 */
int ext4_merge_file(struct file *o_filp, char *dest_file)
{
	int err = 0;
	struct inode *isrc;
	struct inode *idest;
	struct file *dfilp = NULL;

	isrc = o_filp->f_path.dentry->d_inode;

	/* open/create file of name */
	if (dest_file) {
		dfilp = filp_open(dest_file,
				O_RDWR | O_LARGEFILE, 0755);

		if (IS_ERR(dfilp))
			return PTR_ERR(dfilp);

		idest = dfilp->f_path.dentry->d_inode;
	} else {
		ext_debug("No destination file specified\n");
		return -EIO;
	}

	/* Regular file check */
	if (!S_ISREG(isrc->i_mode) || !S_ISREG(idest->i_mode)) {
		ext_debug("ext4 merge file: The argument files should be "
				"regular file [ino:orig %lu, target %lu]\n",
				isrc->i_ino, idest->i_ino);
		err = -EINVAL;
		goto merge_error;
	}

	/* src and dest should be different file */
	if (isrc->i_ino == idest->i_ino) {
		ext4_debug("ext4 merge file: The argument files should not "
				"be same file [ino:src %lu, dest %lu]\n",
				isrc->i_ino, idest->i_ino);
		return -EINVAL;
	}

	mutex_lock(&idest->i_mutex);

	/*
	 * Write out all dirty pages to avoid race conditions
	 * Then release them.
	 */
	if (idest->i_mapping->nrpages &&
			mapping_tagged(idest->i_mapping, PAGECACHE_TAG_DIRTY)) {

		err = filemap_write_and_wait_range(idest->i_mapping, 0, -1);
		if (err)
			goto merge_error;
	}

	/* Now release the pages */
	truncate_inode_pages_range(idest->i_mapping, 0, -1);

	down_write(&EXT4_I(idest)->i_data_sem);
	down_write(&EXT4_I(isrc)->i_data_sem);
	err = ext4_split_move_extents(idest, 0, isrc);
	ext4_es_remove_extent(idest, 0, EXT_MAX_BLOCKS);
	ext4_discard_preallocations(idest);
	up_write(&EXT4_I(isrc)->i_data_sem);
	up_write(&EXT4_I(idest)->i_data_sem);

	mutex_unlock(&idest->i_mutex);

merge_error:

	if (err) {
		ext_debug("MERGE operation failed\n");
		filp_close(dfilp, NULL);
		return err;
	} else {
		/* this will erase the destination file */
		erase_file(dfilp);
	}

	return 0; /* success */
}
#endif /* CONFIG_EXT4_FS_MERGE_FILE */
