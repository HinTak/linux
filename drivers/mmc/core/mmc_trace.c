/*
 * mmc_trace.c
 *
 * Copyright (C) 2017 Samsung Electronics
 * Created by Jungseung Lee (js07.lee@samsung.com)
 *
 * 2017-01-19 : Implement RAW mmc cmd tracer.
 * 2017-02-16 : /sys interface added. Add RPMB notification.
 * 2017-03-14 : Add pivot & sync profiling & timeout timer.
 * NOTE:
 */

#include "mmc_trace.h"
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define SUPPRESS_SEND_STATUS		1
#define SUPPRESS_SYNC_NONBLOCK_SB	1

#define SYNC_TIMEOUT_SEC	10
#define SYNC_TIMEOUT_DUMPSIZE	100

#if defined(CONFIG_MMC_TRACE)
#define BUF_SIZE	80
static struct mmc_trace_data MTQ[MMC_TRACE_LIMIT];
static unsigned int mmc_trace_idx;
static unsigned int queue_is_full;
static int mmc_state = TRACE_FINISH;
static int mmc_part_config = PART_CONFIG_ACC_GP;

static DEFINE_SPINLOCK(mmc_trace_lock);
static DECLARE_WAIT_QUEUE_HEAD(mtq_wait);

struct mmc_sync_timer {
	unsigned long sync_waiting;
	unsigned long syncfs_waiting;
	unsigned long fsync_waiting;
	unsigned long sync_r_waiting;	/* sync_file_range */
	struct timer_list sync_timeout;
	struct timer_list syncfs_timeout;
	struct timer_list fsync_timeout;
	struct timer_list sync_r_timeout;
};

struct mmc_sync_timer sync_timer;

void mmc_trace_banner(void) {
	pr_err("\n===========================================================\n");
	pr_err(" CMD0 : GO_IDLE_STATE     | CMD1 : SEND_OP_COND\n");
	pr_err(" CMD2 : ALL_SEND_CID      | CMD3 : SET_RELATIVE_ADDR\n");
	pr_err(" CMD6 : SWITCH            | CMD7 : SELECT/DESELECT_CARD\n");
	pr_err(" CMD8 : SEND_EXT_CSD      | CMD13: SEND_STATUS\n");
	pr_err(" CMD16: SET_BLOCKLEN      | CMD17: READ_SINGLE_BLOCK\n");
	pr_err(" CMD18: READ_MULTIPLE_BL  | CMD23: SET_BLOCK_COUNT\n");
	pr_err(" CMD24: WRITE_BLOCK       | CMD25: WRITE_MULTIPLE_BLOCK\n");
	pr_err(" CMD38: ERASE             |\n");
	pr_err("===========================================================\n");
}

static void handle_opcode(unsigned opcode, char* op)
{
	switch (opcode)	{
	case 0:
		*op = 'I';
		break;
	case 6:
		*op = 'S';
		break;
	case 8:
		*op = 'C';
		break;
	case 13:
		*op = 'T';
		break;
	case 16:
	case 23:
		*op = 'L';
		break;
	case 17:
	case 18:
		*op = 'R';
		break;
	case 24:
	case 25:
		*op = 'W';
		break;
	case 35:
	case 36:
	case 38:
		*op = 'E';
		break;
	default:
		*op = '_';
	}
}

static void handle_etc(struct mmc_trace_data *mtd, int idx,
		       char* m, unsigned int m_size)
{
	char buf[20];
	char data_buf[64];
	u32 arg    = mtd[idx].arg;
	int flags  = mtd[idx].flags;
	int error  = mtd[idx].error;
	unsigned int opcode      = mtd[idx].opcode;
	unsigned int size        = mtd[idx].bytes_xfered;
	unsigned int part_config = mtd[idx].part_config;
	unsigned long data       = mtd[idx].data;

	/* It always less than s32 */
	s32 _time = (s32) ktime_us_delta(mtd[idx].end, mtd[idx].start);

	/* size */
	if (size == 512)
		snprintf(m, m_size - 1, "  0.5K");
	else if (size)
		snprintf(m, m_size - 1, " %4uK", size / 1024);
	else
		snprintf(m, m_size - 1, " %4dK", 0);

	/* bandwidth */
	if (opcode == 17 || opcode == 18 || opcode == 24 || opcode == 25)
		snprintf(buf, 19, " %5dMB", size / _time);
	else
		snprintf(buf, 19, "     0MB");

	strncat(m, buf, 19);

	/* eMMC partition */
	if ((part_config & PART_CONFIG_PARTION) == PART_CONFIG_ACC_RPMB)
		strncat(m, " (RPMB)" , m_size - 1);
	else if ((part_config & PART_CONFIG_PARTION) == PART_CONFIG_ACC_BOOT0)
		strncat(m, " (BOOT0)", m_size - 1);

	/* error */
	if (error) {
		snprintf(buf, 19, " Err:%08x", error);
		strncat(m, buf, 19);
	}

	/* etc */
	if (opcode == 13)
		strncat(m, " (T)", m_size - 1);

	if (opcode == 17 || opcode == 18)
		strncat(m, " (R)", m_size - 1);

	if (opcode == 24 || opcode == 25) {
		if (part_config & WRITE_RETURN_BUSY)
			strncat(m, " (*)", m_size - 1);
		else
			strncat(m, " (W)", m_size - 1);
	}

	if (opcode != 6) goto out;

	if (data && arg == SYS_SYNC_TRACE_NO) {
		char tmp_buf[64];
		char flags_str[16];

		if (flags == SYNC_INODES_SB)
			snprintf(flags_str, 15, "DATA");
		else if (flags == SYNC_FS_SB)
			snprintf(flags_str, 15, "META");
		else if (flags == SYNC_FDATA_BDEV)
			snprintf(flags_str, 15, "BDEV  ");
		else if (flags == SYNC_FWAIT_BDEV)
			snprintf(flags_str, 15, "BDEV_W");


		if (flags == SYNC_INODES_SB || flags == SYNC_FS_SB) {
			struct super_block *sb = (struct super_block *)data;
			struct block_device *s_bdev = sb->s_bdev;

			if (sb->s_bdev && sb->s_type && sb->s_type->name)
				snprintf(data_buf, 63, " (%s - %s : %s",
					 flags_str, sb->s_type->name,
					 bdevname(sb->s_bdev, tmp_buf));
			else if (sb->s_type && sb->s_type->name)
				snprintf(data_buf, 63, " (%s - %s",
					 flags_str, sb->s_type->name);
		}
		else if (flags == SYNC_FDATA_BDEV || flags == SYNC_FWAIT_BDEV) {
			struct block_device *s_bdev = (struct block_device *)data;
			snprintf(data_buf, 63, " (%s - %s",
				 flags_str, bdevname(s_bdev, tmp_buf));
		}
	}

	switch (arg) {
	case 0x03200101:
		strncat(m, " (F)", m_size - 1); break;
	case 0x03210101:
		strncat(m, " (CACHE EN)", m_size - 1); break;
	case 0x03220101:
		strncat(m, " (PoN ON)", m_size - 1); break;
	case 0x03220201:
		strncat(m, " (PoN Short)", m_size - 1); break;
	case 0x03220301:
		strncat(m, " (PoN Long)", m_size - 1); break;
	case 0x03b70201:
		strncat(m, " (BUS Width)", m_size - 1); break;
	case 0x03b78601:
		strncat(m, " (BUS Width)", m_size - 1); break;
	case 0x03b90101:
		strncat(m, " (HS Timing)", m_size - 1); break;
	case 0x03b90301:
		strncat(m, " (HS Timing)", m_size - 1); break;
	case 0x03a10101:
		strncat(m, " (HPI)", m_size - 1); break;
	/* special string by FlashFS */
	case 1:
		strncat(m, " (PIVOT 1)", m_size - 1); break;
	case 2:
		strncat(m, " (PIVOT 2)", m_size - 1); break;
	case 3:
		strncat(m, " (PIVOT 3)", m_size - 1); break;
	case 4:
		strncat(m, " (PIVOT 4)", m_size - 1); break;
	case 5:
		strncat(m, " (PIVOT 5)", m_size - 1); break;
	case SYS_SYNC_TRACE_NO:
		if (flags == SYNC_START)
			strncat(m, " (SYNC\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"]", m_size - 1);
		else if (flags == SYNC_END)
			strncat(m, " (SYNC_____________________]", m_size - 1);
		else if (data)
			strncat(m, data_buf, m_size - 1);
		break;
	case SYS_SYNCFS_TRACE_NO:
		if (flags == SYNC_START)
			strncat(m, " (SYNCFS\"\"\"\"\"\"\"\"\"]", m_size - 1);
		else
			strncat(m, " (SYNCFS_________]", m_size - 1);
		break;
	case SYS_FSYNC_TRACE_NO:
		if (flags == SYNC_START)
			strncat(m, " (FSYNC\"\"]", m_size - 1);
		else
			strncat(m, " (FSYNC__]", m_size - 1);
		break;
	case SYS_SYNC_R_TRACE_NO:
		if (flags == SYNC_START)
			strncat(m, " (SYNC_R\"\"]", m_size - 1);
		else
			strncat(m, " (SYNC_R__]", m_size - 1);
		break;
	}
out:
	m[m_size - 1] = '\0';
	return;
}

static void __get_print_starting_idx( unsigned int *idx, unsigned int *cnt)
{
	if (queue_is_full) {
		//In queue full, ignore oldest record. It might use buffer for next cmd
		*idx = (mmc_trace_idx + 1) % MMC_TRACE_LIMIT;
		*cnt = MMC_TRACE_LIMIT - 1;
	} else {
		*idx = 0;
		*cnt = mmc_trace_idx;
	}
}

static void __mtq_data_to_str(struct mmc_trace_data *mtd,
			int idx, char *buf, unsigned int length)
{
	int ret;
	unsigned int idx_in_q = idx %MMC_TRACE_LIMIT;
	unsigned int us = ktime_to_us(mtd[idx_in_q].start);
	unsigned int size = mtd[idx_in_q].bytes_xfered;
	unsigned int opcode = mtd[idx_in_q].opcode;
	char op;

	handle_opcode(opcode, &op);
	ret = snprintf(buf, length - 1, 
		"[%6d] %5lld.%06ld ARG:%08x f:%03x resp:%08x CMD:%2u(%c) t:%6lldus",
		idx,							/* idx */
		ktime_divns(mtd[idx_in_q].start, NSEC_PER_SEC),	/* start time */
		us % USEC_PER_SEC, mtd[idx_in_q].arg,			/* arg */
		mtd[idx_in_q].flags, mtd[idx_in_q].resp,		/* flags / resp */
		opcode, op,						/* opcode */
		ktime_us_delta(mtd[idx_in_q].end,mtd[idx_in_q].start));/*elapsed time*/
	if (ret < 0)
		return;
	handle_etc(mtd, idx_in_q, buf+ret, length-ret);
}

/* for /sys/kernel/debug/mmc_trace interface */
/* this function doesn't not called asynchrnously : don't use any lock */
static void __mmc_trace_show(struct seq_file *m)
{
	int idx, j, cnt;

	if (!mmc_trace_idx) {
		seq_printf(m, "No mmc Statistics found !!\n");
		return;
	}

	__get_print_starting_idx( &idx, &cnt);

	for (j = 0; j < cnt; ++idx, idx %= MMC_TRACE_LIMIT)
	{
		unsigned int us = ktime_to_us(MTQ[idx].start);
		unsigned int size = MTQ[idx].bytes_xfered;
		unsigned int opcode = MTQ[idx].opcode;
		char op;
		char helper[BUF_SIZE];

		memset(helper, 0, BUF_SIZE);

		handle_opcode(opcode, &op);

		seq_printf(m, "[%5d] %5lld.%06ld ARG:%08x f:%03x resp:%08x CMD:%2d(%c) t:%6lldus",
		       idx,						/* idx */
		       ktime_divns(MTQ[idx].start, NSEC_PER_SEC),	/* start time */
		       us % USEC_PER_SEC, MTQ[idx].arg,			/* arg */
		       MTQ[idx].flags, MTQ[idx].resp,			/* flags / resp */
		       opcode, op,					/* opcode */
		       ktime_us_delta(MTQ[idx].end, MTQ[idx].start));	/* elapsed time */

		handle_etc(MTQ, idx, helper, BUF_SIZE);

		seq_printf(m, " %s\n", helper);

		j++;

	}
	seq_printf(m, "===========================================================\n");

	return;
}

void show_mmc_trace(int offset) {

	int idx, j, cnt;

	if (!mmc_trace_idx) {
		pr_err("No mmc Statistics found !!\n");
		return;
	}

	__get_print_starting_idx( &idx, &cnt);

	/* Tune idx and cnt value w/ offset value */
	if (offset) {
		if (offset < mmc_trace_idx) {
			idx = mmc_trace_idx - offset;
			idx %= MMC_TRACE_LIMIT;
			cnt = offset;
		} else {
			idx = 0;
			cnt = mmc_trace_idx;
		}
	} else
		mmc_trace_banner();

	for (j = 0; j < cnt; ++idx, idx %= MMC_TRACE_LIMIT)
	{
		unsigned int us = ktime_to_us(MTQ[idx].start);
		unsigned int size = MTQ[idx].bytes_xfered;
		unsigned int opcode = MTQ[idx].opcode;
		char op;
		char helper[BUF_SIZE];

		memset(helper, 0, BUF_SIZE);

		handle_opcode(opcode, &op);

		pr_err("[%5d] %5lld.%06ld ARG:%08x f:%03x resp:%08x CMD:%2d(%c) t:%6lldus",
		       j++,
		       ktime_divns(MTQ[idx].start, NSEC_PER_SEC),
		       us % USEC_PER_SEC, MTQ[idx].arg,
		       MTQ[idx].flags, MTQ[idx].resp,
		       opcode, op,
		       ktime_us_delta(MTQ[idx].end, MTQ[idx].start));

		handle_etc(MTQ, idx, helper, BUF_SIZE);
		pr_cont(" %s\n", helper);

		/* prevent false-positive lock-up detect */
		if (j % 100 == 0)
			touch_softlockup_watchdog();
	}
	pr_err("===========================================================\n");
}

void mmc_cmd_start_trace(struct mmc_request *mrq) {

	unsigned int idx;
	u64 ts_start_nsec;

	if (!mrq || !mrq->cmd) {
		pr_debug("mmc_trace : wrong mmc_request !!\n");
		return;
	}

	if (mmc_state != TRACE_FINISH) {
		pr_debug("mmc_trace : mmc_state - %d, something wrong???\n",
		       mmc_state);
		return;
	}

	ts_start_nsec = sched_clock();
	spin_lock(&mmc_trace_lock);
	idx = mmc_trace_idx % MMC_TRACE_LIMIT;

	MTQ[idx].start.tv64  = ts_start_nsec;
	MTQ[idx].opcode = mrq->cmd->opcode;
	MTQ[idx].arg    = mrq->cmd->arg;
	MTQ[idx].flags  = mrq->cmd->flags;

	mmc_state = TRACE_START;
	spin_unlock(&mmc_trace_lock);
	return;
}

void mmc_cmd_finish_trace(struct mmc_request *mrq) {

	unsigned int idx, pre_idx;
	u64 ts_end_nsec;
	u32 opcode = mrq->cmd->opcode;
	struct mmc_trace_data *pre_MT;

	if (!mrq || !mrq->cmd) {
		pr_debug("mmc_trace : wrong mmc_request !!\n");
		return;
	}

	if (mmc_state != TRACE_START) {
		pr_debug("mmc_trace : mmc_state - %d, something wrong!!!\n",
		       mmc_state);
		return;
	}

	ts_end_nsec = sched_clock();
	spin_lock(&mmc_trace_lock);

	idx = mmc_trace_idx % MMC_TRACE_LIMIT;
	pre_idx = (mmc_trace_idx - 1) % MMC_TRACE_LIMIT;
	pre_MT = &MTQ[pre_idx];

#if SUPPRESS_SEND_STATUS
	if ((MTQ[pre_idx].opcode == 24 || //write cmd
	     MTQ[pre_idx].opcode == 25 ||	//multiple write cmd
	     MTQ[pre_idx].opcode == 6)	//Switch cmd
	     && opcode == 13 &&	//Status cmd
	     mrq->cmd->arg == 0x10000)
	{
		/* Write BUSY Handling + CMD6 */
		/* It prevents huge number of CMD13(SEND_STATUS) */
		if (mrq->cmd->resp[0] == 0x900)
		{
			pre_MT->end.tv64 = ts_end_nsec;
		}
		else if (mrq->cmd->resp[0] == 0xe00)
		{
			/* the write returned BUSY at least one time */
			pre_MT->part_config |= WRITE_RETURN_BUSY;
		}
		else
			pr_debug("mmc_trace : Oops, Is it possible ????\n");
	}
	else
#endif
	{
		if (opcode == 6)	/* SWITCH */
		{
			if (mrq->cmd->arg  == 0x03b34b01)	/* 4b */
				mmc_part_config = PART_CONFIG_ACC_RPMB;
			else if (mrq->cmd->arg  == 0x03b34801)	/* 48 */
				mmc_part_config = PART_CONFIG_ACC_GP;
			else if (mrq->cmd->arg  == 0x03b34901)	/* 49 */
				mmc_part_config = PART_CONFIG_ACC_BOOT0;
		}

		MTQ[idx].part_config = mmc_part_config;
		MTQ[idx].end.tv64 = ts_end_nsec;
		MTQ[idx].resp  = mrq->cmd->resp[0];
		MTQ[idx].error = mrq->cmd->error;

		if (mrq->data)
			MTQ[idx].bytes_xfered = mrq->data->bytes_xfered;

		mmc_trace_idx++;
	}

	mmc_state = TRACE_FINISH;
	spin_unlock(&mmc_trace_lock);

	if (unlikely(mmc_trace_idx == MMC_TRACE_LIMIT))
		queue_is_full = 1;
	wake_up_interruptible(&mtq_wait);
	return;
}

static int mmc_trace_show(struct seq_file *m, void *v)
{
	__mmc_trace_show(m);
	return 0;
}

void mmc_add_pivot_or_delay(unsigned long arg, unsigned int flags,
			    unsigned long data, int delay_ms)
{
	u64 ts_nsec;
	unsigned int idx;

	spin_lock_irq(&mmc_trace_lock);

	idx = mmc_trace_idx % MMC_TRACE_LIMIT;

	/* replace data of mmc_cmd_start to next idx */
	if (mmc_state == TRACE_START) {
		MTQ[idx + 1].start.tv64  = MTQ[idx].start.tv64;
		MTQ[idx + 1].opcode = MTQ[idx].opcode;
		MTQ[idx + 1].arg    = MTQ[idx].arg;
		MTQ[idx + 1].flags  = MTQ[idx].flags;
	}

	ts_nsec = sched_clock();
	idx = mmc_trace_idx % MMC_TRACE_LIMIT;

	MTQ[idx].start.tv64  = ts_nsec;
	MTQ[idx].end.tv64    = ts_nsec;
	MTQ[idx].opcode = 6; /* SWITCH */
	MTQ[idx].arg    = arg;
	MTQ[idx].flags  = flags;
	MTQ[idx].data   = data;
	MTQ[idx].part_config = PART_CONFIG_ACC_GP;
	MTQ[idx].resp  = 0;
	MTQ[idx].error = 0;

	mmc_trace_idx++;

	if (delay_ms > 1000) {
		while (delay_ms >= 0){
			touch_softlockup_watchdog();
			mdelay(100);
			delay_ms -= 100;
		}
	}
	else if (delay_ms) {
		touch_softlockup_watchdog();
		mdelay(delay_ms);
	}

	spin_unlock_irq(&mmc_trace_lock);

	return;
}

#ifdef MMC_SYNC_PROFILE
static void sync_timeout_fn(unsigned long data)
{
	pr_err("=========================================================\n");
	pr_err(" FlashFS : *SYNC* takes TOO much time..!!\n");
	show_mmc_trace(SYNC_TIMEOUT_DUMPSIZE);
}

static void syncfs_timeout_fn(unsigned long data)
{
	struct super_block *sb;
	char bdev_name[BDEVNAME_SIZE];

	pr_err("=========================================================\n");
	pr_err(" FlashFS : *SYNCFS* takes TOO much time..!!\n");

	sb = (struct super_block *)data;

	pr_err("    Device     : %s\n", bdevname(sb->s_bdev, bdev_name));
	if (sb->s_type && sb->s_type->name) {
		pr_err("    FileSystem : %s\n", sb->s_type->name);
	}

	show_mmc_trace(SYNC_TIMEOUT_DUMPSIZE);
}

static void fsync_timeout_fn(unsigned long data)
{
	char file_name_buf[500];
	const char *file_name;
	struct file *file = (struct file*) data;

	pr_err("=========================================================\n");
	pr_err(" FlashFS : *FSYNC/SYNC_R* takes TOO much time..!!");

	if (file) {
		file_name = d_path(&file->f_path,
				   file_name_buf,
				   sizeof(file_name_buf));
		if (IS_ERR(file_name))
			file_name = "(error)";
		pr_err("Access file : %s \n", file_name);
	}

	show_mmc_trace(SYNC_TIMEOUT_DUMPSIZE);
}

void mmc_sync_profile(unsigned long sync_type, unsigned int state, void *data)
{
	struct timer_list* timer;
	unsigned long* waiting;

	void (*mmc_sync_fn)(unsigned long);

	switch (sync_type) {

	case SYS_SYNC_TRACE_NO:
		timer = &(sync_timer.sync_timeout);
		waiting = &(sync_timer.sync_waiting);
		mmc_sync_fn = sync_timeout_fn;
		break;
	case SYS_SYNCFS_TRACE_NO:
		timer = &(sync_timer.syncfs_timeout);
		waiting = &(sync_timer.syncfs_waiting);
		mmc_sync_fn = syncfs_timeout_fn;
		break;
	case SYS_FSYNC_TRACE_NO:
		timer = &(sync_timer.fsync_timeout);
		waiting = &(sync_timer.fsync_waiting);
		mmc_sync_fn = fsync_timeout_fn;
		break;
	case SYS_SYNC_R_TRACE_NO:
		timer = &(sync_timer.sync_r_timeout);
		waiting = &(sync_timer.sync_r_waiting);
		mmc_sync_fn = fsync_timeout_fn;
		break;
	}

	if (state == SYNC_START) {
		if (!test_and_set_bit(0, waiting)) {
			setup_timer(timer, mmc_sync_fn, (unsigned long)data);
			mod_timer(timer, jiffies + SYNC_TIMEOUT_SEC * HZ);
		}
	} else if (state == SYNC_END) {
		if (test_and_clear_bit(0, waiting))
			del_timer(timer);
	} else {
		if (sync_type != SYS_SYNC_TRACE_NO)
			return;			/* it's impossible */
#if SUPPRESS_SYNC_NONBLOCK_SB
		if (state == SYNC_INODES_SB || state == SYNC_FS_SB) {
			struct super_block *sb = (struct super_block *)data;
			if (!sb->s_bdev)	/* handle number of non-block fs */
				return;
		}
#endif
	}

	mmc_add_pivot_or_delay(sync_type, state, (unsigned long)data, 0);
}
#endif

static ssize_t
mmc_trace_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char kbuf[9], *pos;
	unsigned long value;
	int delay = 0;
	ssize_t ret;

	/* Only allow a very narrow range of strings to be written */
	ret = -EINVAL;

	if ((*ppos != 0) || (count >= sizeof(kbuf)))
		goto out;

	ret = -EFAULT;
	if (copy_from_user(kbuf, buf, count))
		goto out;
	kbuf[count] = '\0';
	pos = kbuf;

	ret = -EINVAL;

	if (count >= 4 && kbuf[count - 3] == 'm' && kbuf[count - 2] == 's')
	{
		/* handling delay */
		kbuf[count - 3] = '\0';
		if (kstrtoul(kbuf, 0, &value))
			goto out;

		mmc_add_pivot_or_delay(value, 0, 0, value);
	}
	else
	{
		/* handling pivot */
		if (kstrtoul(kbuf, 0, &value))
			goto out;

		mmc_add_pivot_or_delay(value, 0, 0, 0);
	}

	/* Report a successful write */
	*ppos = count;
	ret = count;
out:
	return ret;
}

static int mmc_trace_open(struct inode *inode, struct file *file)
{
        return single_open(file, mmc_trace_show, NULL);
}

static const struct file_operations mmc_trace_fops = {
	.open           = mmc_trace_open,
	.read           = seq_read,
	.write          = mmc_trace_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static ssize_t proc_mmc_trace_open(struct inode *inode, struct file *file)
{
	//In queue full, ignore oldest record. It might use buffer for next cmd
	if (queue_is_full)
		file->private_data = (void*)(mmc_trace_idx-MMC_TRACE_LIMIT + 1);
	else
		file->private_data = (void*)(0);
	return 0;
}

static ssize_t proc_mmc_trace_read(struct file *file, char __user *buf,
			size_t size, loff_t *ppos)
{
	int ret;
	size_t written = 0;
	char str_buf[BUF_SIZE*2] = {0,};
	unsigned int idx = (uint32_t)file->private_data;
	if (size < sizeof(str_buf))
		return -ENOBUFS;
	ret = wait_event_interruptible(mtq_wait, idx != mmc_trace_idx);
	if (ret)
		return 0;
	do {
		__mtq_data_to_str(MTQ, idx, str_buf, sizeof(str_buf));
		strncat(str_buf, "\n", sizeof(str_buf)-1);//-1 for null byte
		ret = strlen(str_buf);
		copy_to_user(buf + written, str_buf, ret);
		written += ret;
		idx++;
	} while(idx != mmc_trace_idx && (written+sizeof(str_buf)) < size);
	file->private_data = (void *)(idx);
	return written;
}

static const struct file_operations proc_mmc_trace_fops = {
	.open	= proc_mmc_trace_open,
	.read      = proc_mmc_trace_read,
};

static int __init mmc_trace_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_file("mmc_trace", S_IRUSR | S_IWUSR, NULL,
				NULL, &mmc_trace_fops);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	proc_create("mmc_trace", S_IRUSR, NULL, &proc_mmc_trace_fops);
	return 0;
}
module_init(mmc_trace_init)

#endif
