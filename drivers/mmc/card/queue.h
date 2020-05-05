#ifndef MMC_QUEUE_H
#define MMC_QUEUE_H

#include <linux/blk_types.h>

struct request;
struct task_struct;

struct mmc_blk_request {
	struct mmc_request	mrq;
	struct mmc_command	sbc;
	struct mmc_command	cmd;
	struct mmc_command	stop;
	struct mmc_data		data;
};

#ifdef CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM

/*
 * Maximum hw block is 128K + extra page
 */
#define MMC_HW_MAX_IBUFF_PAGES 33
#define MMC_HW_MAX_IBUFF_SZ    (MMC_HW_MAX_IBUFF_PAGES << PAGE_SHIFT)

struct mmc_hw_desc {
	void			*hw_ibuff;
	struct bio_vec		io_vec[MMC_HW_MAX_IBUFF_PAGES];
};

#endif

struct mmc_queue_req {
	struct request		*req;
	struct mmc_blk_request	brq;
	struct scatterlist	*sg;
	char			*bounce_buf;
	struct scatterlist	*bounce_sg;
	unsigned int		bounce_sg_len;
	struct mmc_async_req	mmc_active;
#ifdef CONFIG_HW_DECOMP_BLK_MMC_SUBSYSTEM
	struct mmc_hw_desc      hw_desc;
#endif
};

struct mmc_queue {
	struct mmc_card		*card;
	struct task_struct	*thread;
	struct semaphore	thread_sem;
	unsigned int		flags;
	int			(*issue_fn)(struct mmc_queue *, struct request *);
	void			*data;
	struct request_queue	*queue;
	struct mmc_queue_req	mqrq[2];
	struct mmc_queue_req	*mqrq_cur;
	struct mmc_queue_req	*mqrq_prev;
};

extern int mmc_init_queue(struct mmc_queue *, struct mmc_card *, spinlock_t *,
			  const char *);
extern void mmc_cleanup_queue(struct mmc_queue *);
extern void mmc_queue_suspend(struct mmc_queue *);
extern void mmc_queue_resume(struct mmc_queue *);

extern unsigned int mmc_queue_map_sg(struct mmc_queue *,
				     struct mmc_queue_req *);
extern void mmc_queue_bounce_pre(struct mmc_queue_req *);
extern void mmc_queue_bounce_post(struct mmc_queue_req *);

#endif
