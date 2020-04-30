/*
 * Mediatek 8250 DMA driver.
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Long Cheng <long.cheng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "virt-dma.h"

#define MTK_SDMA_REQUESTS	127
#define MTK_SDMA_CHANNELS	(CONFIG_SERIAL_8250_NR_UARTS * 2)

#define DRV_NAME		"8250-mtk-dma"

/*---------------------------------------------------------------------------*/
#define VFF_RX_INT_FLAG_CLR_B	(BIT(0) | BIT(1))
#define VFF_TX_INT_FLAG_CLR_B	0
/*VFF_INT_EN*/
#define VFF_RX_INT_EN0_B	BIT(0)	/*rx_vff_valid_size >= rx_vff_thre */
/*when UART issues flush to DMA and all data in UART VFIFO is transferred to VFF */
#define VFF_RX_INT_EN1_B	BIT(1)
#define VFF_TX_INT_EN_B		BIT(0)	/*tx_vff_left_size >= tx_vff_thrs */
#define VFF_INT_EN_CLR_B	0
/*VFF_RST*/
#define VFF_WARM_RST_B		BIT(0)
/*VFF_EN*/
#define VFF_EN_B		BIT(0)
/*VFF_STOP*/
#define VFF_STOP_B		BIT(0)
#define VFF_STOP_CLR_B		0
/*VFF_FLUSH*/
#define VFF_FLUSH_B		BIT(0)
#define VFF_FLUSH_CLR_B		0
/*VFF_4G_SUPPORT*/
#define VFF_4G_SUPPORT_B	BIT(0)
#define VFF_4G_SUPPORT_CLR_B	0

#define VFF_TX_THRE(n)		((n)*7/8)	/* tx_vff_left_size >= tx_vff_thrs */
#define VFF_RX_THRE(n)		((n)*3/4)	/* trigger level of rx vfifo */

#define MTK_DMA_RING_SIZE	0x0000fffflU
/*---------------------------------------------------------------------------*/

struct mtk_dmadev {
	struct dma_device ddev;
	spinlock_t lock;
	struct tasklet_struct task;
	struct list_head pending;
	void __iomem *base;
	struct clk *clk;
	unsigned int dma_irq;
	unsigned int dma_requests;
	bool support_33bits;
	struct mtk_chan *lch_map[MTK_SDMA_CHANNELS];
};

struct mtk_chan {
	struct virt_dma_chan vc;
	struct list_head node;
	struct dma_slave_config	cfg;
	void __iomem *channel_base;
	struct mtk_dma_desc *desc;

	bool paused;
	bool requested;

	unsigned int dma_sig;
	unsigned int dma_ch;
	unsigned int sgidx;
	unsigned int remain_size;
	unsigned int rx_ptr;

	/*sync*/
	struct completion done;	/* dma transfer done */
	spinlock_t lock;
	atomic_t loopcnt;
	atomic_t entry;		/* entry count */
};

struct mtk_dma_sg {
	dma_addr_t addr;
	unsigned int en;		/* number of elements (24-bit) */
	unsigned int fn;		/* number of frames (16-bit) */
};

struct mtk_dma_desc {
	struct virt_dma_desc vd;
	enum dma_transfer_direction dir;
	dma_addr_t dev_addr;

	unsigned int sglen;
	struct mtk_dma_sg sg[0];
};

enum {
	VFF_INT_FLAG		= 0x00,
	VFF_INT_EN		= 0x04,
	VFF_EN			= 0x08,
	VFF_RST			= 0x0c,
	VFF_STOP		= 0x10,
	VFF_FLUSH		= 0x14,
	VFF_ADDR		= 0x1c,
	VFF_LEN			= 0x24,
	VFF_THRE		= 0x28,
	VFF_WPT			= 0x2c,
	VFF_RPT			= 0x30,
	/*TX: the buffer size HW can read. RX: the buffer size SW can read.*/
	VFF_VALID_SIZE		= 0x3c,
	/*TX: the buffer size SW can write. RX: the buffer size HW can write.*/
	VFF_LEFT_SIZE		= 0x40,
	VFF_DEBUG_STATUS	= 0x50,
	VFF_4G_SUPPORT		= 0x54,
};

static bool mtk_dma_filter_fn(struct dma_chan *chan, void *param);
static struct of_dma_filter_info mtk_dma_info = {
	.filter_fn = mtk_dma_filter_fn,
};

static inline struct mtk_dmadev *to_mtk_dma_dev(struct dma_device *d)
{
	return container_of(d, struct mtk_dmadev, ddev);
}

static inline struct mtk_chan *to_mtk_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct mtk_chan, vc.chan);
}

static inline struct mtk_dma_desc *to_mtk_dma_desc(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct mtk_dma_desc, vd.tx);
}

static void mtk_dma_chan_write(struct mtk_chan *c, unsigned int reg, unsigned int val)
{
	writel(val, c->channel_base + reg);
}

static unsigned int mtk_dma_chan_read(struct mtk_chan *c, unsigned int reg)
{
	return readl(c->channel_base + reg);
}

static void mtk_dma_desc_free(struct virt_dma_desc *vd)
{
	struct dma_chan *chan = vd->tx.chan;
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->desc != NULL) {
		kfree(c->desc);
		c->desc = NULL;

		if (c->cfg.direction == DMA_DEV_TO_MEM)
			atomic_dec(&c->entry);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

static int mtk_dma_clk_enable(struct mtk_dmadev *mtkd)
{
	int rc;

	rc = clk_prepare_enable(mtkd->clk);
	if (rc) {
		dev_err(mtkd->ddev.dev, "Couldn't enable the clock\n");
		return rc;
	}

	return 0;
}

static void mtk_dma_clk_disable(struct mtk_dmadev *mtkd)
{
	clk_disable_unprepare(mtkd->clk);
}

static void mtk_dma_remove_virt_list(dma_cookie_t cookie, struct virt_dma_chan *vc)
{
	struct virt_dma_desc *vd;

	if (!list_empty(&vc->desc_issued)) {
		list_for_each_entry(vd, &vc->desc_issued, node) {
			if (cookie == vd->tx.cookie) {
				INIT_LIST_HEAD(&vc->desc_issued);
				break;
			}
		}
	}
}

static void mtk_dma_tx_flush(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);

	if (mtk_dma_chan_read(c, VFF_FLUSH) == 0) {
		mtk_dma_chan_write(c, VFF_FLUSH, VFF_FLUSH_B);
		if (atomic_dec_and_test(&c->loopcnt))
			complete(&c->done);
	}
}


/*
 * check whether the dma flush operation is finished or not.
 * return 0 for flush success.
 * return others for flush timeout.
 */
static int mtk_dma_check_flush_result(struct dma_chan *chan)
{
	struct timespec start, end;
	struct mtk_chan *c = to_mtk_dma_chan(chan);

	start = ktime_to_timespec(ktime_get());

	while (mtk_dma_chan_read(c, VFF_FLUSH)) {
		end = ktime_to_timespec(ktime_get());
		if ((end.tv_sec - start.tv_sec) > 1 ||
			((end.tv_sec - start.tv_sec) == 1 && end.tv_nsec > start.tv_nsec)) {
			dev_info(chan->device->dev, "[DMA] Polling flush timeout\n");
			return -1;
		}
	}

	return 0;
}

static int mtk_dma_tx_write(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	unsigned int txcount = c->remain_size;
	unsigned int send_len, left_size;
	unsigned int wpt, vff_len;

	if (atomic_inc_return(&c->entry) > 1) {
		if (vchan_issue_pending(&c->vc) && !c->desc) {
			spin_lock(&mtkd->lock);
			list_add_tail(&c->node, &mtkd->pending);
			spin_unlock(&mtkd->lock);
			tasklet_schedule(&mtkd->task);
		}
	} else {
		vff_len = mtk_dma_chan_read(c, VFF_LEN);
		while (mtk_dma_chan_read(c, VFF_LEFT_SIZE) > 0) {
			left_size = mtk_dma_chan_read(c, VFF_LEFT_SIZE);

			/*prevent from CPU lock */
			left_size = (left_size > 16) ? (left_size - 16) : (0);
			send_len = min(left_size, c->remain_size);

			while (send_len--) {
				if (mtk_dma_check_flush_result(chan))
					return 0;

				wpt = mtk_dma_chan_read(c, VFF_WPT);

				/*xmit buffer mapping DMA buffer, So don't need write data?*/

				if ((wpt & MTK_DMA_RING_SIZE) == (vff_len - 1))
					mtk_dma_chan_write(c, VFF_WPT, (~wpt)&0x10000);
				else
					mtk_dma_chan_write(c, VFF_WPT, wpt+1);
				c->remain_size--;
			}
			if (c->remain_size == 0)
				break;
		}

		if (txcount != c->remain_size) {
			mtk_dma_chan_write(c, VFF_INT_EN, VFF_TX_INT_EN_B);
			mtk_dma_tx_flush(chan);
		}
	}
	atomic_dec(&c->entry);
	return 0;
}


static void mtk_dma_start_tx(struct mtk_chan *c)
{
	if (mtk_dma_chan_read(c, VFF_LEFT_SIZE) == 0) {
		dev_info(c->vc.chan.device->dev, "%s maybe need fix? %d @L %d\n",
				__func__, mtk_dma_chan_read(c, VFF_LEFT_SIZE), __LINE__);
		mtk_dma_chan_write(c, VFF_INT_EN, VFF_TX_INT_EN_B);
	} else {
		reinit_completion(&c->done);
		atomic_inc(&c->loopcnt);
		atomic_inc(&c->loopcnt);
		mtk_dma_tx_write(&c->vc.chan);
	}
	c->paused = false;
}

static void mtk_dma_get_rx_size(struct mtk_chan *c)
{
	unsigned int count;
	unsigned int rdptr, wrptr, wrreg, rdreg;
	unsigned int rx_size;

	rx_size = c->cfg.src_addr_width*1024;

	rdreg = mtk_dma_chan_read(c, VFF_RPT);
	wrreg = mtk_dma_chan_read(c, VFF_WPT);
	rdptr = rdreg & MTK_DMA_RING_SIZE;
	wrptr = wrreg & MTK_DMA_RING_SIZE;
	count = ((rdreg ^ wrreg) & 0x00010000) ? (wrptr + rx_size - rdptr) : (wrptr - rdptr);

	c->remain_size = count;
	c->rx_ptr = rdptr;

	mtk_dma_chan_write(c, VFF_RPT, wrreg);
}

static void mtk_dma_start_rx(struct mtk_chan *c)
{
	struct dma_chan *chan = &(c->vc.chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	struct mtk_dma_desc *d = c->desc;

	if (mtk_dma_chan_read(c, VFF_VALID_SIZE) != 0 && d != NULL && d->vd.tx.cookie != 0) {
		mtk_dma_get_rx_size(c);
		mtk_dma_remove_virt_list(d->vd.tx.cookie, &c->vc);
		vchan_cookie_complete(&d->vd);
	} else {
		if (mtk_dma_chan_read(c, VFF_VALID_SIZE) != 0) {
			spin_lock(&mtkd->lock);
			if (list_empty(&mtkd->pending))
				list_add_tail(&c->node, &mtkd->pending);
			spin_unlock(&mtkd->lock);
			tasklet_schedule(&mtkd->task);
			} else {
				if (atomic_read(&c->entry) > 0)
					atomic_set(&c->entry, 0);
			}
	}
}

static void mtk_dma_reset(struct mtk_chan *c)
{
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(c->vc.chan.device);

	mtk_dma_chan_write(c, VFF_ADDR, 0);
	mtk_dma_chan_write(c, VFF_THRE, 0);
	mtk_dma_chan_write(c, VFF_LEN, 0);
	mtk_dma_chan_write(c, VFF_RST, VFF_WARM_RST_B);

	while
		(mtk_dma_chan_read(c, VFF_EN));

	if (c->cfg.direction == DMA_DEV_TO_MEM)
		mtk_dma_chan_write(c, VFF_RPT, 0);
	else if (c->cfg.direction == DMA_MEM_TO_DEV)
		mtk_dma_chan_write(c, VFF_WPT, 0);
	else
		dev_info(c->vc.chan.device->dev, "Unknown direction.\n");

	if (mtkd->support_33bits)
		mtk_dma_chan_write(c, VFF_4G_SUPPORT, VFF_4G_SUPPORT_CLR_B);
}

static void mtk_dma_stop(struct mtk_chan *c)
{
	int polling_cnt;

	mtk_dma_chan_write(c, VFF_FLUSH, VFF_FLUSH_CLR_B);

	polling_cnt = 0;
	while (mtk_dma_chan_read(c, VFF_FLUSH))	{
		polling_cnt++;
		if (polling_cnt > 10000) {
			dev_info(c->vc.chan.device->dev,
				"mtk_uart_stop_dma: polling VFF_FLUSH fail VFF_DEBUG_STATUS=0x%x\n",
				mtk_dma_chan_read(c, VFF_DEBUG_STATUS));
			break;
		}
	}

	polling_cnt = 0;
	/*set stop as 1 -> wait until en is 0 -> set stop as 0*/
	mtk_dma_chan_write(c, VFF_STOP, VFF_STOP_B);
	while (mtk_dma_chan_read(c, VFF_EN)) {
		polling_cnt++;
		if (polling_cnt > 10000) {
			dev_info(c->vc.chan.device->dev,
				"mtk_uart_stop_dma: polling VFF_EN fail VFF_DEBUG_STATUS=0x%x\n",
				mtk_dma_chan_read(c, VFF_DEBUG_STATUS));
			break;
		}
	}
	mtk_dma_chan_write(c, VFF_STOP, VFF_STOP_CLR_B);
	mtk_dma_chan_write(c, VFF_INT_EN, VFF_INT_EN_CLR_B);

	if (c->cfg.direction == DMA_DEV_TO_MEM)
		mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_RX_INT_FLAG_CLR_B);
	else
		mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_TX_INT_FLAG_CLR_B);

	c->paused = true;
}

/*
 * We need to deal with 'all channels in-use'
 */
static void mtk_dma_rx_sched(struct mtk_chan *c)
{
	struct dma_chan *chan = &(c->vc.chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);

	if (atomic_read(&c->entry) < 1) {
		mtk_dma_start_rx(c);
	} else {
		spin_lock(&mtkd->lock);
		if (list_empty(&mtkd->pending))
			list_add_tail(&c->node, &mtkd->pending);
		spin_unlock(&mtkd->lock);
		tasklet_schedule(&mtkd->task);
	}
}

/*
 * This callback schedules all pending channels. We could be more
 * clever here by postponing allocation of the real DMA channels to
 * this point, and freeing them when our virtual channel becomes idle.
 *
 * We would then need to deal with 'all channels in-use'
 */
static void mtk_dma_sched(unsigned long data)
{
	struct mtk_dmadev *mtkd = (struct mtk_dmadev *)data;
	struct mtk_chan *c;
	struct virt_dma_desc *vd;
	dma_cookie_t cookie;
	LIST_HEAD(head);
	unsigned long flags;

	spin_lock_irq(&mtkd->lock);
	list_splice_tail_init(&mtkd->pending, &head);
	spin_unlock_irq(&mtkd->lock);

	if (!list_empty(&head)) {
		c = list_first_entry(&head, struct mtk_chan, node);
		cookie = c->vc.chan.cookie;

		spin_lock_irqsave(&c->vc.lock, flags);
		if (c->cfg.direction == DMA_DEV_TO_MEM) {
			list_del_init(&c->node);
			mtk_dma_rx_sched(c);
		} else if (c->cfg.direction == DMA_MEM_TO_DEV) {
			vd = vchan_find_desc(&c->vc, cookie);

			c->desc = to_mtk_dma_desc(&vd->tx);
			list_del_init(&c->node);
			mtk_dma_start_tx(c);
		} else {
			dev_warn(c->vc.chan.device->dev, "%s maybe error @Line%d\n",
				__func__, __LINE__);
		}
		spin_unlock_irqrestore(&c->vc.lock, flags);
	}
}

static int mtk_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	int ret;

	ret = -EBUSY;

	if (mtkd->lch_map[c->dma_ch] == NULL) {
		c->channel_base = mtkd->base + 0x80 * c->dma_ch;
		mtkd->lch_map[c->dma_ch] = c;
		ret = 1;
	}
	c->requested = false;
	mtk_dma_reset(c);

	return ret;
}

static void mtk_dma_free_chan_resources(struct dma_chan *chan)
{
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	struct mtk_chan *c = to_mtk_dma_chan(chan);

	if (c->requested) {
		c->requested = false;
		free_irq(mtkd->dma_irq + c->dma_ch, chan);
	}

	c->channel_base = NULL;
	mtkd->lch_map[c->dma_ch] = NULL;
	vchan_free_chan_resources(&c->vc);

	dev_dbg(mtkd->ddev.dev, "freeing channel for %u\n", c->dma_sig);
	c->dma_sig = 0;

	tasklet_kill(&mtkd->task);
}

static enum dma_status mtk_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);

	spin_lock_irqsave(&c->vc.lock, flags);
	if (ret == DMA_IN_PROGRESS) {
		c->rx_ptr = mtk_dma_chan_read(c, VFF_RPT) & MTK_DMA_RING_SIZE;
		txstate->residue = c->rx_ptr;
	} else if (ret == DMA_COMPLETE && c->cfg.direction == DMA_DEV_TO_MEM) {
		txstate->residue = c->remain_size;
	} else {
		txstate->residue = 0;
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	return ret;
}

static unsigned int mtk_dma_desc_size(struct mtk_dma_desc *d)
{
	struct mtk_dma_sg *sg;
	unsigned int i;
	unsigned int size;

	for (size = i = 0; i < d->sglen; i++) {
		sg = &d->sg[i];
		size += sg->en * sg->fn;
	}
	return size;
}

static struct dma_async_tx_descriptor *mtk_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sglen,
	enum dma_transfer_direction dir, unsigned long tx_flags, void *context)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct scatterlist *sgent;
	struct mtk_dma_desc *d;
	dma_addr_t dev_addr;
	unsigned int i, j, en, frame_bytes;

	en = frame_bytes = 1;

	if (dir == DMA_DEV_TO_MEM) {
		dev_addr = c->cfg.src_addr;
		dev_width = c->cfg.src_addr_width;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_addr = c->cfg.dst_addr;
		dev_width = c->cfg.dst_addr_width;
	} else {
		dev_err(chan->device->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	/* Now allocate and setup the descriptor. */
	d = kzalloc(sizeof(*d) + sglen * sizeof(d->sg[0]), GFP_ATOMIC);
	if (!d)
		return NULL;

	d->dir = dir;
	d->dev_addr = dev_addr;

	j = 0;
	for_each_sg(sgl, sgent, sglen, i) {
		d->sg[j].addr = sg_dma_address(sgent);
		d->sg[j].en = en;
		d->sg[j].fn = sg_dma_len(sgent) / frame_bytes;
		j++;
	}

	d->sglen = j;

	if (dir == DMA_MEM_TO_DEV)
		c->remain_size = mtk_dma_desc_size(d);

	return vchan_tx_prep(&c->vc, &d->vd, tx_flags);
}

static void mtk_dma_issue_pending(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd;
	struct virt_dma_desc *vd;
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->cfg.direction == DMA_DEV_TO_MEM) {
		cookie = c->vc.chan.cookie;
		mtkd = to_mtk_dma_dev(chan->device);
		if (vchan_issue_pending(&c->vc) && !c->desc) {
			vd = vchan_find_desc(&c->vc, cookie);
			c->desc = to_mtk_dma_desc(&vd->tx);
			if (atomic_read(&c->entry) > 0)
				atomic_set(&c->entry, 0);
		}
	} else if (c->cfg.direction == DMA_MEM_TO_DEV) {
		cookie = c->vc.chan.cookie;
		if (vchan_issue_pending(&c->vc) && !c->desc) {
			vd = vchan_find_desc(&c->vc, cookie);
			c->desc = to_mtk_dma_desc(&vd->tx);
			mtk_dma_start_tx(c);
		}
	} else
		dev_info(c->vc.chan.device->dev, "Unknown direction\n");
	spin_unlock_irqrestore(&c->vc.lock, flags);
}


static irqreturn_t mtk_dma_rx_interrupt(int irq, void *dev_id)
{
	struct dma_chan *chan = (struct dma_chan *)dev_id;
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_RX_INT_FLAG_CLR_B);

	if (atomic_inc_return(&c->entry) > 1) {
		spin_lock(&mtkd->lock);
		if (list_empty(&mtkd->pending))
			list_add_tail(&c->node, &mtkd->pending);
		spin_unlock(&mtkd->lock);
		tasklet_schedule(&mtkd->task);
	} else {
		mtk_dma_start_rx(c);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t mtk_dma_tx_interrupt(int irq, void *dev_id)
{
	struct dma_chan *chan = (struct dma_chan *)dev_id;
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
	struct mtk_dma_desc *d = c->desc;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->remain_size != 0) {
		/* maybe never can enter*/
		dev_info(chan->device->dev, "%s have error! remain size[%d]\n",
			__func__, (int)c->remain_size);

		spin_lock(&mtkd->lock);
		list_add_tail(&c->node, &mtkd->pending);
		spin_unlock(&mtkd->lock);
		tasklet_schedule(&mtkd->task);
	} else {
		mtk_dma_remove_virt_list(d->vd.tx.cookie, &c->vc);
		vchan_cookie_complete(&d->vd);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_TX_INT_FLAG_CLR_B);

	if (atomic_dec_and_test(&c->loopcnt))
		complete(&c->done);

	return IRQ_HANDLED;

}

static int mtk_dma_slave_config(struct dma_chan *chan,
			struct dma_slave_config *cfg)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(c->vc.chan.device);
	int ret;

	c->cfg = *cfg;

	if (cfg->direction == DMA_DEV_TO_MEM) {
		mtk_dma_chan_write(c, VFF_ADDR, cfg->src_addr);
		mtk_dma_chan_write(c, VFF_LEN, cfg->src_addr_width*1024);
		mtk_dma_chan_write(c, VFF_THRE, VFF_RX_THRE(cfg->src_addr_width*1024));
		mtk_dma_chan_write(c, VFF_INT_EN, VFF_RX_INT_EN0_B | VFF_RX_INT_EN1_B);
		mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_RX_INT_FLAG_CLR_B);
		mtk_dma_chan_write(c, VFF_EN, VFF_EN_B);

		if (!c->requested) {
			atomic_set(&c->entry, 0);
			c->requested = true;
			ret = request_irq(mtkd->dma_irq + c->dma_ch, mtk_dma_rx_interrupt,
						IRQF_TRIGGER_NONE, DRV_NAME, chan);
			if (ret) {
				dev_err(chan->device->dev, "Cannot request IRQ\n");
				return IRQ_NONE;
			}
		}
	} else if (cfg->direction == DMA_MEM_TO_DEV)	{
		mtk_dma_chan_write(c, VFF_ADDR, cfg->dst_addr);
		mtk_dma_chan_write(c, VFF_LEN, cfg->dst_addr_width*1024);
		mtk_dma_chan_write(c, VFF_THRE, VFF_TX_THRE(cfg->dst_addr_width*1024));
		mtk_dma_chan_write(c, VFF_INT_FLAG, VFF_TX_INT_FLAG_CLR_B);
		mtk_dma_chan_write(c, VFF_EN, VFF_EN_B);

		if (!c->requested) {
			c->requested = true;
			ret = request_irq(mtkd->dma_irq + c->dma_ch, mtk_dma_tx_interrupt,
					IRQF_TRIGGER_NONE, DRV_NAME, chan);
			if (ret) {
				dev_err(chan->device->dev, "Cannot request IRQ\n");
				return IRQ_NONE;
			}
		}
	} else
		dev_info(chan->device->dev, "Unknown direction!\n");

	if (mtkd->support_33bits)
		mtk_dma_chan_write(c, VFF_4G_SUPPORT, VFF_4G_SUPPORT_B);

	if (mtk_dma_chan_read(c, VFF_EN) != VFF_EN_B) {
		dev_err(chan->device->dev, "Start DMA fail\n");
		return -EINVAL;
	}

	return 0;
}

static int mtk_dma_terminate_all(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_dma_chan(chan);
	struct mtk_dmadev *mtkd = to_mtk_dma_dev(c->vc.chan.device);
	unsigned long flags;
	LIST_HEAD(head);

	if (atomic_read(&c->loopcnt) != 0)
		wait_for_completion(&c->done);

	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->desc) {
		mtk_dma_remove_virt_list(c->desc->vd.tx.cookie, &c->vc);
		spin_unlock_irqrestore(&c->vc.lock, flags);

		mtk_dma_desc_free(&c->desc->vd);

		spin_lock_irqsave(&c->vc.lock, flags);
		if (!c->paused)	{
			spin_lock(&mtkd->lock);
			list_del_init(&c->node);
			spin_unlock(&mtkd->lock);
			mtk_dma_stop(c);
		}
	}
	vchan_get_all_descriptors(&c->vc, &head);
	spin_unlock_irqrestore(&c->vc.lock, flags);

	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static int mtk_dma_pause(struct dma_chan *chan)
{
	/* Pause/Resume only allowed with cyclic mode */
	return -EINVAL;
}

static int mtk_dma_resume(struct dma_chan *chan)
{
	/* Pause/Resume only allowed with cyclic mode */
	return -EINVAL;
}

static int mtk_dma_chan_init(struct mtk_dmadev *mtkd)
{
	struct mtk_chan *c;

	c = devm_kzalloc(mtkd->ddev.dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->vc.desc_free = mtk_dma_desc_free;
	vchan_init(&c->vc, &mtkd->ddev);
	spin_lock_init(&c->lock);
	INIT_LIST_HEAD(&c->node);

	init_completion(&c->done);
	atomic_set(&c->loopcnt, 0);
	atomic_set(&c->entry, 0);

	return 0;
}

static void mtk_dma_free(struct mtk_dmadev *mtkd)
{
	tasklet_kill(&mtkd->task);
	while (!list_empty(&mtkd->ddev.channels)) {
		struct mtk_chan *c = list_first_entry(&mtkd->ddev.channels,
			struct mtk_chan, vc.chan.device_node);

		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
		devm_kfree(mtkd->ddev.dev, c);
	}
}

static const struct of_device_id mtk_uart_dma_match[] = {
	{ .compatible = "mediatek,mt6577-uart-dma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_uart_dma_match);

static int mtk_dma_probe(struct platform_device *pdev)
{
	struct mtk_dmadev *mtkd;
	struct resource *res;
	int rc, i;

	mtkd = devm_kzalloc(&pdev->dev, sizeof(*mtkd), GFP_KERNEL);
	if (!mtkd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	mtkd->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mtkd->base))
		return PTR_ERR(mtkd->base);

	mtkd->dma_irq = platform_get_irq(pdev, 0);
	if ((int)mtkd->dma_irq < 0) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return mtkd->dma_irq;
	}

	mtkd->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mtkd->clk)) {
		dev_err(&pdev->dev, "No clock specified\n");
		return PTR_ERR(mtkd->clk);
	}

	if (of_property_read_bool(pdev->dev.of_node, "dma-33bits")) {
		dev_info(&pdev->dev, "Support dma 33bits\n");
		mtkd->support_33bits = true;
	}

	if (mtkd->support_33bits)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(33));
	else
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	dma_cap_set(DMA_SLAVE, mtkd->ddev.cap_mask);
	mtkd->ddev.device_alloc_chan_resources = mtk_dma_alloc_chan_resources;
	mtkd->ddev.device_free_chan_resources = mtk_dma_free_chan_resources;
	mtkd->ddev.device_tx_status = mtk_dma_tx_status;
	mtkd->ddev.device_issue_pending = mtk_dma_issue_pending;
	mtkd->ddev.device_prep_slave_sg = mtk_dma_prep_slave_sg;
	mtkd->ddev.device_config = mtk_dma_slave_config;
	mtkd->ddev.device_pause = mtk_dma_pause;
	mtkd->ddev.device_resume = mtk_dma_resume;
	mtkd->ddev.device_terminate_all = mtk_dma_terminate_all;
	mtkd->ddev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	mtkd->ddev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	mtkd->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	mtkd->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	mtkd->ddev.dev = &pdev->dev;
	INIT_LIST_HEAD(&mtkd->ddev.channels);
	INIT_LIST_HEAD(&mtkd->pending);

	spin_lock_init(&mtkd->lock);

	tasklet_init(&mtkd->task, mtk_dma_sched, (unsigned long)mtkd);

	mtkd->dma_requests = MTK_SDMA_REQUESTS;
	if (pdev->dev.of_node && of_property_read_u32(pdev->dev.of_node,
							"dma-requests",
							&mtkd->dma_requests)) {
		dev_info(&pdev->dev,
			 "Missing dma-requests property, using %u.\n",
			 MTK_SDMA_REQUESTS);
	}

	for (i = 0; i < MTK_SDMA_CHANNELS; i++) {
		rc = mtk_dma_chan_init(mtkd);
		if (rc) {
			goto err_no_dma;
		}
	}

	mtk_dma_clk_enable(mtkd);

	rc = dma_async_device_register(&mtkd->ddev);
	if (rc) {
		pr_warn("MTK-UART-DMA: failed to register slave DMA engine device: %d\n",
			rc);
		mtk_dma_clk_disable(mtkd);
		goto err_no_dma;
	}

	platform_set_drvdata(pdev, mtkd);

	if (pdev->dev.of_node) {
		mtk_dma_info.dma_cap = mtkd->ddev.cap_mask;

		/* Device-tree DMA controller registration */
		rc = of_dma_controller_register(pdev->dev.of_node,
				of_dma_simple_xlate, &mtk_dma_info);
		if (rc) {
			pr_warn("MTK-UART-DMA: failed to register DMA controller\n");
			dma_async_device_unregister(&mtkd->ddev);
			mtk_dma_clk_disable(mtkd);
			goto err_no_dma;
		}
	}

	return rc;

err_no_dma:
	mtk_dma_free(mtkd);
	return rc;
}

static int mtk_dma_remove(struct platform_device *pdev)
{
	struct mtk_dmadev *mtkd = platform_get_drvdata(pdev);

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	mtk_dma_clk_disable(mtkd);

	dma_async_device_unregister(&mtkd->ddev);

	mtk_dma_free(mtkd);

	return 0;
}

static struct platform_driver mtk_dma_driver = {
	.probe	= mtk_dma_probe,
	.remove	= mtk_dma_remove,
	.driver = {
		.name = "mtk-uart-dma-engine",
		.of_match_table = of_match_ptr(mtk_uart_dma_match),
	},
};


static bool mtk_dma_filter_fn(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->driver == &mtk_dma_driver.driver) {
		struct mtk_dmadev *mtkd = to_mtk_dma_dev(chan->device);
		struct mtk_chan *c = to_mtk_dma_chan(chan);
		unsigned int req = *(unsigned int *)param;

		if (req <= mtkd->dma_requests) {
			c->dma_sig = req;
			c->dma_ch = req;
			return true;
		}
	}
	return false;
}

static int mtk_dma_init(void)
{
	return platform_driver_register(&mtk_dma_driver);
}
subsys_initcall(mtk_dma_init);

static void __exit mtk_dma_exit(void)
{
	platform_driver_unregister(&mtk_dma_driver);
}
module_exit(mtk_dma_exit);
