#ifndef _MMC_TRACE_H
#define _MMC_TRACE_H

#include <linux/time.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/console.h>
#include <linux/sched.h>

#define MMC_TRACE_LIMIT 25000

struct mmc_trace_data {
	ktime_t start;
	ktime_t end;
	u32 opcode;
	u32 arg;
	u32 resp;			// mmc_command->resp[0]
	unsigned int flags;
	unsigned long data;		// special data for sync
	int error;			// mmc_command
	unsigned int bytes_xfered;      // mmc_data
	unsigned int part_config;	// partition & WRITE_RETURN
};

#define TRACE_START	0
#define TRACE_FINISH	1

#define PART_CONFIG_ACC_BOOT0	0
#define PART_CONFIG_ACC_RPMB	1
#define PART_CONFIG_ACC_GP	2
#define PART_CONFIG_PARTION	127
#define WRITE_RETURN_BUSY	(1<<9)

#define SYS_SYNC_TRACE_NO	378
#define SYS_SYNCFS_TRACE_NO	379
#define SYS_FSYNC_TRACE_NO	380
#define SYS_SYNC_R_TRACE_NO	381

#define SYNC_START		0
#define SYNC_END		1
#define SYNC_INODES_SB		2	/* data sync */
#define SYNC_FS_SB		3	/* meta nowait/wait sync */
#define SYNC_FDATA_BDEV		4	/* bdev sync */
#define SYNC_FWAIT_BDEV		5	/* bdev wait sync */

extern void console_forbid_async_printk(void);
extern void console_permit_async_printk(void);

#if defined(CONFIG_SEPARATE_PRINTK_FROM_USER) && !defined(MODULE)
void _sep_printk_start(void);
void _sep_printk_end(void);
#else
#define _sep_printk_start() {}
#define _sep_printk_end() {}
#endif

#if defined(CONFIG_MMC_TRACE)
void show_mmc_trace(int offset);
void mmc_cmd_start_trace(struct mmc_request *mrq);
void mmc_cmd_finish_trace(struct mmc_request *mrq);
#else
static void show_mmc_trace(int offset){};
static inline void mmc_cmd_start_trace(struct mmc_request *mrq){};
static inline void mmc_cmd_finish_trace(struct mmc_request *mrq){};
#endif

//#define MMC_SYNC_PROFILE

#if defined(CONFIG_MMC_TRACE) && defined(MMC_SYNC_PROFILE)
void mmc_sync_profile(unsigned long sync_type, unsigned int state, void *data);
#else
static void mmc_sync_profile(unsigned long sync_type, unsigned int state, void *data){};
#endif

#endif
