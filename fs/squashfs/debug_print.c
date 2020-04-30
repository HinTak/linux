/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * debug_print.c
 */

#if defined(CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE) && defined(CONFIG_ARM)
#include <linux/io.h>
#include <asm/mach/map.h>
#endif
#include <linux/buffer_head.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/console.h>
#include "../mount.h"

#include "debug_print.h"
#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"

#define	DATABLOCK	0
#define	METABLOCK	1
#define	VERSION_INFO	"Ver 3.1"

static char file_name_buf[PATH_MAX];
static const char *file_name;

static atomic_t	debug_print_once = ATOMIC_INIT(0);

#ifndef CONFIG_SEPARATE_PRINTK_FROM_USER
void _sep_printk_start(void) {}
void _sep_printk_end(void) {}
#else
extern void _sep_printk_start(void);
extern void _sep_printk_end(void);
#endif
#define sep_printk_start _sep_printk_start
#define sep_printk_end _sep_printk_end

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem_be(const char *str, unsigned long bottom,
			unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	pr_err("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p <= top; i++, p += 4) {
			if (p >= bottom && p <= top) {
				unsigned long val;
				if (__get_user(val, (unsigned long *)p) == 0) {
					val = __cpu_to_be32(val);
					sprintf(str + i * 9, " %08lx", val);
				} else
					sprintf(str + i * 9, " ????????");
			}
		}
		pr_err("%04lx:%s\n", first & 0xffff, str);
	}

	set_fs(fs);
}

#ifdef CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE
/* Verfication of flash data.
 * Called with disabled premption.
 */
static void __debug_auto_diagnose(struct debug_print_state *d,
				  int all_block_read,
				  int fail_block)
{
	struct squashfs_sb_info *msblk = d->sb->s_fs_info;
	char *block_buffer = NULL;
	int is_differ = 0;
	int check_unit = 0;
	int i = 0, blk_n;

	if(d->b == 0)
		return;

	pr_err("---------------------------------------------------------------------\n");
	pr_err("[verifying flash data]\n");

	preempt_enable();

	if (all_block_read)
		check_unit = d->b;
	else
		check_unit = 1;

	/* 1. buffer allocation */
	block_buffer = kmalloc(msblk->devblksize * check_unit, GFP_KERNEL);

	if (!block_buffer) {
		pr_emerg("verifying flash failed - not enough memory\n");
		preempt_disable();
		goto diagnose_buff_alloc_fail;
	}

	/* 2. copy original data */
	for (i = fail_block; i < fail_block + check_unit; i++) {
		if (all_block_read)
			memcpy(block_buffer + i * msblk->devblksize,
			       d->bh[i]->b_data, msblk->devblksize);
		else
			memcpy(block_buffer, d->bh[i]->b_data,
			       msblk->devblksize);

		clear_buffer_uptodate(d->bh[i]);
	}


	/* 3. Reread buffer block from flash */
	ll_rw_block(READ, check_unit, d->bh);

	/* 3-1. wait complete read */
	for (i = fail_block ; i < fail_block + check_unit; i++)
		wait_on_buffer(d->bh[i]);

	preempt_disable();


	/* 4. Checking buffer : Original Data vs Reread Data (uncached) */
	for (blk_n = fail_block; blk_n < fail_block + check_unit; blk_n++) {
		u32 *cur = (u32 *) (block_buffer + msblk->devblksize * blk_n);
		u32 *new = (u32 *) (d->bh[blk_n]->b_data);

		for (i = 0; i < msblk->devblksize >> 2; i++) {
			u32 diff = cur[i] ^ new[i];
			if (diff) {
				pr_err("bh[%d] off %d data differs: "
						"orig=%08x reread=%08x "
						"xor=%08x\n",
						blk_n, i << 2,
						cpu_to_be32(cur[i]),
						cpu_to_be32(new[i]),
						cpu_to_be32(diff));
				is_differ = 1;
			}
		}
	}

	if (!is_differ) {
		pr_err("---------------------------------------------------------------------\n");
		pr_err("[ result - Auto diagnose can't find any cause.....;;;;;;;            ]\n");
		pr_err("[        - flash image is broken or update is canceled abnormally??? ]\n");
		pr_err("---------------------------------------------------------------------\n");
	}

	kfree(block_buffer);

diagnose_buff_alloc_fail:
	pr_err("=====================================================================\n");
}
#endif /* CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE */


/*
 * Save latest file name.
 * Here we do not care about anything ...
 * Yeah, that's it! This is the design!
 */
void debug_set_file_name(struct file *file)
{
	if (unlikely(!file))
		return;
	file_name = d_path(&file->f_path, file_name_buf, sizeof(file_name_buf));
	if (IS_ERR(file_name))
		file_name = "(error)";
}

/* Print everything we can */
void debug_print(struct debug_print_state *d)
{
	int all_block_read = 1;
	int fail_block = 0;
	struct buffer_head *fail_bh;

	int i = 0;
	char bdev_name[BDEVNAME_SIZE];
	struct squashfs_sb_info *msblk = d->sb->s_fs_info;

	/* We do debug printing only once */
	if (atomic_inc_not_zero(&debug_print_once))
		return;

	/* Decompression failed during inflate partial block.
	   Now, we know what is fail_block */
	if (d->k < d->b - 1) {
		fail_block = d->k;
		all_block_read = 0;
	}

	fail_bh = d->bh[fail_block];

	/* To avoid printk log dropping */
	console_forbid_async_printk();

	console_flush_messages();

	/* Start separation print from user */
	sep_printk_start();

	pr_err("---------------------------------------------------------------------\n");
	pr_err(" Current : %s(%d)\n", current->comm, task_pid_nr(current));
	pr_err("---------------------------------------------------------------------\n");
	pr_err("== [System Arch] Squashfs Debugger - %7s ======== Core : %2d ====\n",
	       VERSION_INFO, current_thread_info()->cpu);
	pr_err("---------------------------------------------------------------------\n");
	if (d->block_type == METABLOCK) {
		pr_err("[MetaData Block]\nBlock @ 0x%llx, %scompressed,"
		       " src size %d, size %d, nr of b: %d\n",
		       d->index, d->compressed ? "" : "un", d->srclength,
		       d->__length, d->b);
		pr_err("- Metablock 0 is broken.. compressed block - bh[0]\n");
	} else {
		pr_err("[DataBlock]\nBlock @ 0x%llx, %scompressed,"
		       " src size %d, size %d, nr of bh : %d\n",
		       d->index, d->compressed ? "" : "un", d->srclength,
		       d->__length, d->b);
		pr_err("- %s all compressed block (%d/%d)\n",
		       all_block_read ? "Read" : "Didn't read",
		       all_block_read ? d->b : d->k + 1, d->b);
	}

	pr_err("---------------------------------------------------------------------\n");
	pr_err("[Block: 0x%08llx(0x%08llx) ~ 0x%08llx(0x%08llx)]\n",
	       d->index >> msblk->devblksize_log2, d->index,
	       (d->index + d->srclength) >> msblk->devblksize_log2,
	       d->index + d->srclength);

	pr_err("\tlength : %d, device block_size : %d\n",
		d->__length, msblk->devblksize);
	pr_err("---------------------------------------------------------------------\n");

	if (all_block_read)
		pr_err("<< First Block Info >>\n");
	else
		pr_err("<< Fail Block Info >>\n");

	if (fail_bh) {
		unsigned long long b_blocknr = fail_bh->b_blocknr;
		size_t b_size    = fail_bh->b_size;

		pr_err("- bh->b_blocknr : %8llu (0x%08llx x %4zu byte = 0x%08llx)\n",
		       (unsigned long long)b_blocknr,
		       (unsigned long long)b_blocknr, b_size,
		       (unsigned long long)(b_blocknr * b_size));
		pr_err("- bi_sector     : %8llu (0x%08llx x  512 byte = 0x%08llx)\n",
		       (unsigned long long)(b_blocknr * (b_size >> 9)),
		       (unsigned long long)(b_blocknr * (b_size >> 9)),
		       /* sector size = 512byte fixed */
		       (unsigned long long)(b_blocknr * b_size));
		pr_err("- bh[%d]->b_data : 0x%p\n", fail_block, fail_bh->b_data);
	}
	pr_err("---------------------------------------------------------------------\n");
	pr_err("Device : %s\n", bdevname(d->sb->s_bdev, bdev_name));
	pr_err("---------------------------------------------------------------------\n");
#ifdef CONFIG_FCOUNT_DEBUG
	print_mounts();
#endif
	pr_err("---------------------------------------------------------------------\n");

	if (d->block_type == METABLOCK)
		pr_err("MetaData Access Error : Maybe mount or ls problem..????\n");
	else {
		pr_err(" - CAUTION : Below is the information just for reference ....!!\n");
		pr_err(" - LAST ACCESS FILE : %s\n", file_name);
	}

	pr_err("---------------------------------------------------------------------\n");

	for (i = 0 ; i < d->b; i++) {
		pr_err("bh[%2d]:0x%p", i, d->bh[i]);
		if (d->bh[i]) {
			pr_cont(" | bh[%2d]->b_data:0x%p | ", i,
			       d->bh[i]->b_data);
			pr_cont("bh value :0x%08x ",
			    __cpu_to_be32(*(unsigned int *)(d->bh[i]->b_data)));
			if (fail_block == i)
				pr_cont("*");
		}
		pr_cont("\n");
	}
	pr_err("---------------------------------------------------------------------\n");
	pr_err("[ Original Data Buffer ]\n");

	for (i = 0 ; i < d->b; i++) {
		pr_err("bh[%2d]:0x%p", i, d->bh[i]);
		if (d->bh[i]) {
			pr_cont(" | bh[%2d]->b_data:0x%p | ",
			       i, d->bh[i]->b_data);
			dump_mem_be("DUMP BH->b_data",
				    (unsigned long)d->bh[i]->b_data,
				    (unsigned long)d->bh[i]->b_data +
						  msblk->devblksize - 4);
		}
		pr_err("\n");
	}

#ifdef CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE
	/* Do something probably very useful */
	__debug_auto_diagnose(d, all_block_read, fail_block);
#endif
	/* End separation */
	sep_printk_end();
	console_permit_async_printk();
}
